// Fog volumes: the FogVolumes a view draws (box_render_fog_volume_t) go through a froxel grid
// aligned with its frustum (include/froxel.wgsl), in three entry points:
//
// - inject, one thread per cell, sums the density of every volume at a point of the cell, the
//   share of the cell its box covers taken at BOX_FROXEL_SAMPLES points along the depth of the
//   cell, and lights it once: the sun through the cascades, its shadow taken at the same points,
//   and the cookie, the ambient and the point and spot lights of the cluster of the cell. It
//   stores the light scattered toward the eye per unit length (rgb) and the extinction (a) of
//   the cell. With a valid history the point and the depth samples are jittered inside the cell
//   by the 3D sequence of the frame (temporal.wgsl), interleaved per cell, and the result is
//   blended with the cells of the previous frame (history) at the center of the cell,
//   reprojected into the previous grid; without one, or in a cell no volume reaches, the cell is
//   sampled at its center alone.
// - integrate, one thread per column of cells, accumulates the cells front to back, each slice
//   integrated analytically, and stores the light scattered toward the camera (rgb) and the
//   transmittance (a) from the camera to the far side of each slice.
// - vs and fs, one fullscreen triangle in the forward pass after the opaque meshes and the sky,
//   in a pass that reads the depth of the view, look the integrated grid up at the depth of each
//   pixel. A pixel without depth takes the last slice, so fog ends at fog_distance. In the slice
//   of the depth it weighs the slice by the share of the density of the volumes before the depth
//   along the ray of the pixel (slice_share), so faces that cross the slice do not step. The
//   particles and blended draws after it fog themselves with one lookup at their own depth
//   (fog_blend.wgsl).
//
// A view with transmissive draws splits each ray at the nearest transmissive surface of its
// pixel (glass_texture, the depth of the transmissive draws over the opaque depth): before its
// snapshot it applies the part behind the surface (SEGMENT_BEHIND), which the surface refracts,
// and after its last transmissive draw the part from the camera to the surface (SEGMENT_FRONT),
// which fogs the surface as it fogs an opaque one at its depth. The part between depths a and b
// is T(b) / T(a) and (L(b) - L(a)) / T(a) of two lookups.
#include "include/lights.wgsl"
#include "include/shadow.wgsl"
#include "include/point_shadow.wgsl"
#include "include/attenuation.wgsl"
#include "include/cookie.wgsl"
#include "include/ambient.wgsl"
#include "include/aerial.wgsl"
#include "include/fullscreen.wgsl"
#include "include/froxel.wgsl"
#include "include/temporal.wgsl"

const PI: f32 = 3.141592653589793;

// Point and spot lights of the cluster of a cell it keeps at most,
// BOX_FOG_VOLUME_LIGHT_STEPS.
const FOG_LIGHTS: u32 = 8u;

// Segments of the rays an apply draw takes (Froxel.segment.x): the whole ray to the opaque
// depth, the part behind the transmissive surface of the pixel, else the whole ray, and the
// part in front of it, nothing where the pixel has none.
const SEGMENT_FULL: u32 = 0u;
const SEGMENT_BEHIND: u32 = 1u;
const SEGMENT_FRONT: u32 = 2u;

// Shapes of the fog of a volume in its box (FogVolumeShape). An unbounded volume has no box: it
// fills all space, its height measured from its center.
const FOG_SHAPE_BOX: u32 = 0u;
const FOG_SHAPE_ELLIPSOID: u32 = 1u;
const FOG_SHAPE_UNBOUNDED: u32 = 2u;

// Distance inside an unbounded volume, deeper than any edge_fade.
const FOG_UNBOUNDED_INSIDE: f32 = 1e30;

// Share of the shadow distance over which the sun's shadow of the fog fades out, at least, so
// the shafts end without a ring where shadow_point stops at shadow_params.y.
const FOG_SHADOW_FADE: f32 = 0.25;

// Largest value a cell stores, below the largest half float, so the height falloff of an
// unbounded volume far below its base does not overflow the grid.
const FOG_CELL_MAX: f32 = 60000.0;

// Width of the noise values over which edge_noise lets the fog of a border in: the threshold of
// the noise moves from 0 deep inside to 1 + this at the surface.
const FOG_EDGE_NOISE_WIDTH: f32 = 0.3;

struct FogVolume {
    center: vec3f,
    density: f32,
    axes: array<vec4f, 3>,
    extent: vec3f,
    absorption: f32,
    color: vec3f,
    asymmetry: f32,
    edge_fade: f32,
    sun_inscatter: f32,
    ambient_inscatter: f32,
    light_inscatter: f32,
    // World offset of the noise, the wind times the world time wrapped to 10 noise_scale
    offset: vec3f,
    noise_strength: f32,
    noise_scale: f32,
    // FOG_SHAPE_BOX, FOG_SHAPE_ELLIPSOID or FOG_SHAPE_UNBOUNDED
    shape: u32,
    height_falloff: f32,
    edge_noise: f32,
};

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> clusters: array<u32>;

// Bindings of group 1, the inject, integrate and apply layouts of gpu_fog_volume.c each with
// those their entry point reads.
@group(1) @binding(0) var<storage, read> volumes: array<FogVolume>;
@group(1) @binding(1) var<uniform> froxel: Froxel;
@group(1) @binding(2) var depth_texture: texture_depth_2d;
@group(1) @binding(3) var noise: texture_3d<f32>;
@group(1) @binding(4) var noise_sampler: sampler;
// Depth of the nearest transmissive surface over the opaque depth, the opaque depth itself for
// SEGMENT_FULL.
@group(1) @binding(5) var glass_texture: texture_depth_2d;
@group(1) @binding(6) var scatter_out: texture_storage_3d<rgba16float, write>;
@group(1) @binding(7) var scatter_in: texture_3d<f32>;
@group(1) @binding(8) var integrated_out: texture_storage_3d<rgba16float, write>;
@group(1) @binding(9) var integrated: texture_3d<f32>;
@group(1) @binding(10) var grid_sampler: sampler;
// Injected cells of the previous frame of the view, and their clamping trilinear sampler.
@group(1) @binding(11) var history: texture_3d<f32>;
@group(1) @binding(12) var history_sampler: sampler;

// Henyey-Greenstein phase of an asymmetry g times 4 pi, so isotropic scattering is 1.
fn phase(cos_theta: f32, g: f32) -> f32 {
    let d = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4);
    return (1.0 - g * g) / (d * sqrt(d));
}

// Distance of a point of the local frame of a volume inside its box, negative outside.
fn inside(fog: FogVolume, q: vec3f) -> f32 {
    let d = fog.extent - abs(q);
    return min(min(d.x, d.y), d.z);
}

// Distance of a point of the local frame of a volume inside the surface of its shape, negative
// outside, at most the true distance and concave along a segment for every shape. The ellipsoid
// takes the distance in the unit sphere scaled by its smallest semi-axis.
fn shape_inside(fog: FogVolume, q: vec3f) -> f32 {
    if (fog.shape == FOG_SHAPE_UNBOUNDED) {
        return FOG_UNBOUNDED_INSIDE;
    }

    if (fog.shape == FOG_SHAPE_ELLIPSOID) {
        let e = max(fog.extent, vec3f(1e-6));
        return (1.0 - length(q / e)) * min(min(e.x, e.y), e.z);
    }

    return inside(fog, q);
}

// Share of the density of a volume at a point of its local frame: 0 at the surface of its shape,
// 1 from edge_fade inside.
fn edge(fog: FogVolume, q: vec3f) -> f32 {
    let d = shape_inside(fog, q);
    if (fog.edge_fade <= 0.0) {
        return select(0.0, 1.0, d > 0.0);
    }

    return smoothstep(0.0, fog.edge_fade, d);
}

// A world position in the local frame of a volume.
fn volume_local(fog: FogVolume, p: vec3f) -> vec3f {
    let rel = p - fog.center;
    return vec3f(dot(rel, fog.axes[0].xyz), dot(rel, fog.axes[1].xyz),
        dot(rel, fog.axes[2].xyz));
}

// Share of the density of a volume left at a height q.y of its local frame by its
// height_falloff: 1 at the bottom of the box, thinning out exponentially above it. An unbounded
// volume measures it from its center as HeightFog does from its base height, growing below it,
// the exponent clamped as fog.wgsl does.
fn height_share(fog: FogVolume, q: vec3f) -> f32 {
    if (fog.height_falloff <= 0.0) {
        return 1.0;
    }

    if (fog.shape == FOG_SHAPE_UNBOUNDED) {
        return exp(clamp(-fog.height_falloff * q.y, -80.0, 80.0));
    }

    return exp(-fog.height_falloff * max(q.y + fog.extent.y, 0.0));
}

// The share of a border edge_noise leaves of a noise value v at an edge factor e: a threshold on
// the noise that moves from 0 deep inside (e 1) to above every value at the surface (e 0), so
// the fog reaches further out where the noise is high, mixed with the plain factor.
fn edge_eroded(fog: FogVolume, e: f32, v: f32) -> f32 {
    let t = (1.0 - e) * (1.0 + FOG_EDGE_NOISE_WIDTH);
    return mix(e, smoothstep(t - FOG_EDGE_NOISE_WIDTH, t, v), fog.edge_noise);
}

// Density of a volume at a point p of the world, of the share of the volume it covers there
// (edge) and the share its height leaves (height_share). The noise is sampled at the mip of
// footprint, the size of the cell in the world, so a far cell reads the noise averaged over it,
// which its jittered history converges to, instead of a point of texels much finer than it.
fn density(fog: FogVolume, cover: f32, p: vec3f, h: f32, footprint: f32) -> f32 {
    var value = fog.density * cover * h;
    if (value > 0.0 && fog.noise_strength > 0.0) {
        let uvw = (p - fog.offset) / fog.noise_scale;
        let lod = log2(footprint / fog.noise_scale * f32(BOX_FROXEL_NOISE_SIZE));
        let n = textureSampleLevel(noise, noise_sampler, uvw, max(lod, 0.0)).r;
        let detail = textureSampleLevel(noise, noise_sampler, uvw * 3.1 + vec3f(0.37),
            max(lod + log2(3.1), 0.0)).r;
        let v = n * 0.75 + detail * 0.25;
        value *= mix(1.0, saturate(v * 2.0), fog.noise_strength);
        // The faded border is a threshold on the same noise: the cover is the edge factor of
        // the cell, swapped for its eroded share.
        if (fog.edge_noise > 0.0 && fog.edge_fade > 0.0 && cover < 1.0) {
            value *= edge_eroded(fog, cover, v) / cover;
        }
    }

    return value;
}

// Light of the cell a thread injects, shared by its volumes: the ambient, the sun through the
// cookie over pi and its direction, without its shadow, and the point and spot lights of the
// cluster of the cell, each its color times attenuation over pi and shadow, and its direction.
var<private> cell_ambient: vec3f;
var<private> cell_sun: vec3f;
var<private> cell_sun_dir: vec3f;
var<private> cell_lights: array<vec3f, FOG_LIGHTS>;
var<private> cell_light_dirs: array<vec3f, FOG_LIGHTS>;
var<private> cell_light_count: u32;
// Density of the volumes at the BOX_FROXEL_SAMPLES points of the cell, which weighs the shadow
// of the sun at them.
var<private> cell_weights: array<f32, BOX_FROXEL_SAMPLES>;

// Light a cell at a world position: the ambient and the sun.
fn light_cell(p: vec3f) {
    let up = vec3f(0.0, 1.0, 0.0);
    cell_ambient = (ambient(up) + ambient(-up)) * 0.5;
    cell_sun = vec3f(0.0);
    cell_sun_dir = up;
    if (view.lights.x > 0u) {
        cell_sun_dir = normalize(lights[0].position);
        cell_sun = lights[0].color / PI * light_cookie(p, cell_sun_dir, up);
    }
}

// Shadow of the sun at a point of the fog: shadow_point faded toward 1 over the last part of the
// shadow distance, from the fade start of the surfaces or FOG_SHADOW_FADE of the distance before
// its end, whichever is earlier, so fog that goes on past the shadows shows no ring or wall
// where they end.
fn fog_shadow(p: vec3f) -> f32 {
    let end = view.shadow_params.y;
    let start = min(view.shadow_params.x, end * (1.0 - FOG_SHADOW_FADE));
    let depth = -(view.view * vec4f(p, 1.0)).z;
    let fade = clamp((depth - start) / max(end - start, 1e-4), 0.0, 1.0);
    if (fade >= 1.0) {
        return 1.0;
    }

    return mix(shadow_point(p), 1.0, fade);
}

// Share of the sun the cell sees through the cascades: its shadow at the BOX_FROXEL_SAMPLES
// points of the cell from a to b, offset inside their strata by offset, weighed by the density
// there (cell_weights), so where a face of a volume meets a wall the light outside the wall does
// not leak into the fog inside it.
fn sun_visible(a: vec3f, b: vec3f, offset: f32) -> f32 {
    var visible = 0.0;
    var total = 0.0;
    for (var j = 0u; j < BOX_FROXEL_SAMPLES; j++) {
        let w = cell_weights[j];
        if (w > 0.0) {
            visible += w * fog_shadow(mix(a, b, froxel_sample(j, offset)));
            total += w;
        }
    }

    return select(0.0, visible / total, total > 0.0);
}

// Keep the first FOG_LIGHTS point and spot lights of the cluster of a world position that reach
// it, with their shadows.
fn light_cell_local(p: vec3f) {
    cell_light_count = 0u;
    if (view.lights.y <= view.lights.x) {
        return;
    }

    let cluster = cluster_index(view, cluster_view_position(view, p));
    let first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster];
    let count = min(cluster_lights(clusters[2u + 2u * cluster]), FOG_LIGHTS);
    for (var i = 0u; i < count; i++) {
        let light = lights[clusters[first + i]];
        let delta = light.position - p;
        let dist = length(delta);
        let l = delta / max(dist, 1e-4);
        let a = attenuation(light, l, dist);
        if (a <= 0.0) {
            continue;
        }

        var visible = 1.0;
        if (light.shadow < view.spot_config.x) {
            visible = punctual_shadow_point(light, p);
        }

        cell_lights[cell_light_count] = light.color * (a / PI * visible);
        cell_light_dirs[cell_light_count] = l;
        cell_light_count += 1u;
    }
}

// Light the point and spot lights of the cell scatter toward the eye along a ray direction with
// the phase of an asymmetry.
fn light_local(dir: vec3f, g: f32) -> vec3f {
    var sum = vec3f(0.0);
    for (var i = 0u; i < cell_light_count; i++) {
        sum += cell_lights[i] * phase(dot(dir, cell_light_dirs[i]), g);
    }

    return sum;
}

// Whether inject blends with the cells of the previous frame.
fn history_blends() -> bool {
    return froxel.prev_range.w > 0.5 && history_valid();
}

// Texture coordinate of the injected grid of the previous frame at a world position, through
// the previous view projection and slicing, and in w 1 when the position is inside that grid.
fn history_uvw(p: vec3f) -> vec4f {
    let r = reproject(p);
    let depth = dot(froxel.prev_depth_plane, vec4f(p, 1.0));
    let uvw = vec3f(r.xy * froxel.size.zw, froxel_share_of(depth, froxel.prev_range.x,
        froxel.prev_range.y, froxel.prev_range.z > 0.5));
    let inside = all(uvw.xy >= vec2f(0.0)) && all(uvw.xy <= vec2f(1.0)) &&
        depth >= froxel.prev_range.x && depth <= froxel.prev_range.y;
    return vec4f(uvw, select(0.0, 1.0, inside));
}

// The injected cells of the previous frame at a texture coordinate of its grid, Catmull-Rom
// across and down from 9 bilinear taps and linear along the depth: a moving camera samples the
// history between its cells every frame, which a bilinear sample alone would blur a little more
// each time, so edges such as the side of a shaft would smear while the camera moves.
fn history_sample(uvw: vec3f) -> vec4f {
    let size = vec2f(froxel.grid.xy);
    let pos = uvw.xy * size;
    let center = floor(pos - 0.5) + 0.5;
    let f = pos - center;
    let w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    let w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    let w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    let w3 = f * f * (-0.5 + 0.5 * f);
    let w12 = w1 + w2;
    let t0 = (center - 1.0) / size;
    let t12 = (center + w2 / w12) / size;
    let t3 = (center + 2.0) / size;
    let x = array<f32, 3>(t0.x, t12.x, t3.x);
    let y = array<f32, 3>(t0.y, t12.y, t3.y);
    let wx = array<f32, 3>(w0.x, w12.x, w3.x);
    let wy = array<f32, 3>(w0.y, w12.y, w3.y);
    var result = vec4f(0.0);
    for (var j = 0u; j < 3u; j++) {
        for (var i = 0u; i < 3u; i++) {
            result += textureSampleLevel(history, history_sampler, vec3f(x[i], y[j], uvw.z),
                0.0) * (wx[i] * wy[j]);
        }
    }

    return max(result, vec4f(0.0));
}

// Point a cell is sampled at this frame, from its top left near corner by 0..1 of its size
// across, down and along its slice: the 3D sequence of the frame shifted by the noise of the
// cell, so neighbouring cells take different points, or its center without a history.
fn cell_jitter(id: vec3u) -> vec3f {
    if (!history_blends()) {
        return vec3f(0.5);
    }

    return temporal_interleaved_3d(vec2f(id.xy) + f32(id.z) * vec2f(17.0, 29.0));
}

@compute @workgroup_size(BOX_FROXEL_INJECT_GROUP, BOX_FROXEL_INJECT_GROUP, BOX_FROXEL_INJECT_GROUP)
fn inject(@builtin(global_invocation_id) id: vec3u) {
    if (any(id >= froxel.grid.xyz)) {
        return;
    }

    let jitter = cell_jitter(id);
    let uv = froxel_cell_point_uv(id.xy, jitter.xy);
    let ray = froxel_ray(uv);
    let slices = f32(froxel.grid.z);
    let p = ray.origin + ray.dir * froxel_depth((f32(id.z) + jitter.z) / slices);
    // The near and far side of the cell along its ray, between which the faces of the volumes
    // and the sun's shadow are sampled, so they cover a share of the cell instead of stepping
    // from slice to slice.
    let a = ray.origin + ray.dir * froxel_depth(f32(id.z) / slices);
    let b = ray.origin + ray.dir * froxel_depth((f32(id.z) + 1.0) / slices);
    let dir = normalize(ray.dir);
    // The unjittered center of the cell, and the distance from it to its far corners: a cell
    // no box comes that near holds no fog at any jittered point, so it takes no history, and a
    // volume that is removed or moved away leaves no fog behind.
    let center_ray = froxel_ray(froxel_cell_uv(id.xy));
    let center = center_ray.origin + center_ray.dir * froxel_depth((f32(id.z) + 0.5) / slices);
    let far_ = froxel_depth((f32(id.z) + 1.0) / slices);
    let corner_a = froxel_ray(froxel_cell_point_uv(id.xy, vec2f(0.0)));
    let corner_b = froxel_ray(froxel_cell_point_uv(id.xy, vec2f(1.0)));
    let reach = max(length(corner_a.origin + corner_a.dir * far_ - center),
        length(corner_b.origin + corner_b.dir * far_ - center));
    // Half the largest extent of the cell, along its ray or across it, about the width of a
    // cell: the mip of the whole extent averages the noise over more than the cell.
    let footprint = max(length(b - a) * 0.5, reach);
    var occupied = false;
    var lit = false;
    var local = false;
    var extinction = 0.0;
    var scatter = vec3f(0.0);
    // The light of the sun the volumes scatter before its shadow (sun_visible)
    var sunlit = vec3f(0.0);
    for (var j = 0u; j < BOX_FROXEL_SAMPLES; j++) {
        cell_weights[j] = 0.0;
    }

    for (var i = 0u; i < froxel.grid.w; i++) {
        let fog = volumes[i];
        // The box bounds every shape but the unbounded one, which reaches every cell.
        occupied = occupied || fog.shape == FOG_SHAPE_UNBOUNDED ||
            inside(fog, volume_local(fog, center)) > -reach;
        let qa = volume_local(fog, a);
        let qb = volume_local(fog, b);
        var h = 1.0;
        if (fog.height_falloff > 0.0) {
            h = height_share(fog, volume_local(fog, p));
        }

        // The distance inside a shape is concave along a segment: a cell whose sides are both
        // edge_fade inside is covered whole.
        var cover = 1.0;
        if (min(shape_inside(fog, qa), shape_inside(fog, qb)) < max(fog.edge_fade, 1e-6)) {
            cover = 0.0;
            for (var j = 0u; j < BOX_FROXEL_SAMPLES; j++) {
                let e = edge(fog, mix(qa, qb, froxel_sample(j, jitter.z)));
                cover += e;
                cell_weights[j] += fog.density * h * e;
            }

            cover /= f32(BOX_FROXEL_SAMPLES);
        } else {
            for (var j = 0u; j < BOX_FROXEL_SAMPLES; j++) {
                cell_weights[j] += fog.density * h;
            }
        }

        let sigma = density(fog, cover, p, h, footprint);
        if (sigma <= 0.0) {
            continue;
        }

        if (!lit) {
            light_cell(p);
            lit = true;
        }

        let albedo = fog.color * (sigma * (1.0 - fog.absorption));
        sunlit += albedo * cell_sun *
            (phase(dot(dir, cell_sun_dir), fog.asymmetry) * fog.sun_inscatter);
        var light = cell_ambient * fog.ambient_inscatter;
        if (fog.light_inscatter > 0.0) {
            if (!local) {
                light_cell_local(p);
                local = true;
            }

            light += light_local(dir, fog.asymmetry) * fog.light_inscatter;
        }

        extinction += sigma;
        scatter += albedo * light;
    }

    if (any(sunlit > vec3f(0.0))) {
        scatter += sunlit * sun_visible(a, b, jitter.z);
    }

    if (extinction > 0.0 && view.aerial.w > 0.0) {
        scatter *= aerial_sample(vec4f(uv * view.viewport.xy, 0.0, 1.0), p).a;
    }

    var result = min(vec4f(scatter, extinction), vec4f(FOG_CELL_MAX));
    if (occupied && history_blends()) {
        // The previous cells are kept at their centers: the unjittered center of this cell
        // lands between them in the previous grid.
        let c = history_uvw(center);
        if (c.w > 0.5) {
            // The first frames after a reset average all of them, so the cells converge from
            // the center sample as fast as the sequence allows.
            let n = f32(temporal_frame());
            let blend = min(BOX_FROXEL_HISTORY_BLEND, n / (n + 1.0));
            let previous = history_sample(c.xyz);
            result = mix(result, previous, blend);
        }
    }

    textureStore(scatter_out, id, result);
}

@compute @workgroup_size(BOX_FROXEL_INTEGRATE_GROUP, BOX_FROXEL_INTEGRATE_GROUP, 1)
fn integrate(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= froxel.grid.x || id.y >= froxel.grid.y) {
        return;
    }

    let stretch = length(froxel_ray(froxel_cell_uv(id.xy)).dir);
    let slices = froxel.grid.z;
    var near_ = froxel_depth(0.0);
    var transmittance = 1.0;
    var inscatter = vec3f(0.0);
    for (var z = 0u; z < slices; z++) {
        let far_ = froxel_depth(f32(z + 1u) / f32(slices));
        let ds = (far_ - near_) * stretch;
        let cell = textureLoad(scatter_in, vec3u(id.xy, z), 0);
        let sigma = cell.a;
        let step_t = exp(-sigma * ds);
        var sint = cell.rgb * ds;
        if (sigma > 1e-5) {
            sint = (cell.rgb - cell.rgb * step_t) / sigma;
        }

        inscatter += transmittance * sint;
        transmittance *= step_t;
        textureStore(integrated_out, vec3u(id.xy, z), vec4f(inscatter, transmittance));
        near_ = far_;
    }
}

// Whether the segment of the local frame of a volume from qa to qb meets its box, always for an
// unbounded volume.
fn segment_meets(fog: FogVolume, qa: vec3f, qb: vec3f) -> bool {
    if (fog.shape == FOG_SHAPE_UNBOUNDED) {
        return true;
    }

    let d = qb - qa;
    let inv = 1.0 / select(d, vec3f(1e-6), abs(d) < vec3f(1e-6));
    let t0 = (-fog.extent - qa) * inv;
    let t1 = (fog.extent - qa) * inv;
    let lo = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0));
    let hi = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), 1.0));
    return lo <= hi;
}

// Share of the density of the volumes in slice s along the ray of a uv of the image that lies
// before share f of the slice, the share f itself where the slice is empty or evenly dense, in
// closed form where a volume covers the slice whole.
// The grid knows the density of a cell as a whole, so where a face crosses the slice of a surface
// the lookup weighs the slice by where its density is along the ray instead of by depth: a thin
// slab on a floor or a volume against a wall does not step where the slices cross it.
fn slice_share(uv: vec2f, s: f32, f: f32) -> f32 {
    let slices = f32(froxel.grid.z);
    let ray = froxel_ray(uv);
    let a = ray.origin + ray.dir * froxel_depth(s / slices);
    let b = ray.origin + ray.dir * froxel_depth((s + 1.0) / slices);
    let n = f32(BOX_FROXEL_APPLY_SAMPLES);
    var before = 0.0;
    var total = 0.0;
    for (var i = 0u; i < froxel.grid.w; i++) {
        let fog = volumes[i];
        let qa = volume_local(fog, a);
        let qb = volume_local(fog, b);
        // A volume that covers the slice whole: its density thins exponentially with height
        // along it, the height linear in the share of the slice, so its share before f and its
        // weight in the slice are the integrals of the exponential, f and the density without
        // a falloff.
        if (min(shape_inside(fog, qa), shape_inside(fog, qb)) >= max(fog.edge_fade, 1e-6)) {
            let k = clamp(max(fog.height_falloff, 0.0) * (qb.y - qa.y), -60.0, 60.0);
            var share = f;
            var weight = 1.0;
            if (abs(k) > 1e-4) {
                let whole = 1.0 - exp(-k);
                share = (1.0 - exp(-k * f)) / whole;
                weight = whole / k;
            }

            let g = fog.density * height_share(fog, qa) * weight;
            before += g * share;
            total += g;
            continue;
        }

        if (!segment_meets(fog, qa, qb)) {
            continue;
        }

        for (var k = 0u; k < BOX_FROXEL_APPLY_SAMPLES; k++) {
            let p = ray.origin + ray.dir * froxel_depth((s + (f32(k) + 0.5) / n) / slices);
            let q = volume_local(fog, p);
            let g = fog.density * edge(fog, q) * height_share(fog, q) / n;
            before += g * saturate(f * n - f32(k));
            total += g;
        }
    }

    if (total <= 1e-6) {
        return f;
    }

    return before / total;
}

// Light scattered toward the camera (rgb) and transmittance (a) from the camera to a clip depth
// of a uv of the image, the far end of the grid for no depth. The grid stores them at the far
// side of each slice: the lookup takes the far sides of the slice of the depth and of the slice
// before it, nothing before the first, and weighs them by the share of the slice's density
// before the depth.
fn lookup(uv: vec2f, depth: f32) -> vec4f {
    var d = froxel.range.z;
    if (depth > 0.0) {
        d = froxel_view_depth(froxel_world(uv, depth));
    }

    let slices = f32(froxel.grid.z);
    let t = froxel_share(d) * slices;
    let s = min(floor(t), slices - 1.0);
    let xy = uv * froxel.size.zw;
    let far_ = textureSampleLevel(integrated, grid_sampler, vec3f(xy, (s + 0.5) / slices), 0.0);
    var near_ = vec4f(0.0, 0.0, 0.0, 1.0);
    if (s > 0.5) {
        near_ = textureSampleLevel(integrated, grid_sampler, vec3f(xy, (s - 0.5) / slices), 0.0);
    }

    return mix(near_, far_, slice_share(uv, s, saturate(t - s)));
}

@fragment
fn fs(in: FullscreenOut) -> @location(0) vec4f {
    let pixel = vec2i(in.pos.xy);
    let uv = in.pos.xy * view.viewport.zw;
    let depth = textureLoad(depth_texture, pixel, 0);
    var result = lookup(uv, depth);
    if (froxel.segment.x != SEGMENT_FULL) {
        // Reversed Z: a transmissive surface in front of the opaque depth is greater.
        let glass = textureLoad(glass_texture, pixel, 0);
        let has_glass = glass > depth;
        if (froxel.segment.x == SEGMENT_FRONT) {
            if (!has_glass) {
                discard;
            }

            result = lookup(uv, glass);
        } else if (has_glass) {
            let front = lookup(uv, glass);
            let t = max(front.a, 1e-4);
            result = vec4f(max(result.rgb - front.rgb, vec3f(0.0)) / t, saturate(result.a / t));
        }
    }

    return result;
}
