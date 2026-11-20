// Fog volumes: the FogVolumes a view draws (box_render_fog_volume_t) go through a froxel grid
// aligned with its frustum (include/froxel.wgsl), in three entry points:
//
// - inject, one thread per cell, sums the density of every volume at the center of the cell, the
//   share of the cell its box covers taken at BOX_FROXEL_SAMPLES points along the depth of the
//   cell, and lights it once: the sun through the cascades, its shadow taken at the same points,
//   and the cookie, the ambient and the point and spot lights of the cluster of the cell. It
//   stores the light scattered toward the eye per unit length (rgb) and the extinction (a) of
//   the cell.
// - integrate, one thread per column of cells, accumulates the cells front to back, each slice
//   integrated analytically, and stores the light scattered toward the camera (rgb) and the
//   transmittance (a) from the camera to the far side of each slice.
// - vs and fs, one fullscreen triangle in the forward pass after the opaque meshes and the sky,
//   in a pass that reads the depth of the view, look the integrated grid up at the depth of each
//   pixel. A pixel without depth takes the last slice, so fog ends at fog_distance.
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
    pad0: f32,
    pad1: f32,
    pad2: f32,
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

// Share of the density of a volume at a point of its local frame: 0 at the faces of the box, 1
// from edge_fade inside.
fn edge(fog: FogVolume, q: vec3f) -> f32 {
    let d = inside(fog, q);
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

// Density of a volume at a point p of the world, of the share of the volume it covers there
// (edge).
fn density(fog: FogVolume, cover: f32, p: vec3f) -> f32 {
    var value = fog.density * cover;
    if (value > 0.0 && fog.noise_strength > 0.0) {
        let uvw = (p - fog.offset) / fog.noise_scale;
        let n = textureSampleLevel(noise, noise_sampler, uvw, 0.0).r;
        let detail = textureSampleLevel(noise, noise_sampler, uvw * 3.1 + vec3f(0.37), 0.0).r;
        let v = n * 0.75 + detail * 0.25;
        value *= mix(1.0, saturate(v * 2.0), fog.noise_strength);
    }

    return value;
}

// Light of the cell a thread injects, shared by its volumes: the ambient, the sun through the
// cascades and the cookie over pi and its direction, and the point and spot lights of the
// cluster of the cell, each its color times attenuation over pi and shadow, and its direction.
var<private> cell_ambient: vec3f;
var<private> cell_sun: vec3f;
var<private> cell_sun_dir: vec3f;
var<private> cell_lights: array<vec3f, FOG_LIGHTS>;
var<private> cell_light_dirs: array<vec3f, FOG_LIGHTS>;
var<private> cell_light_count: u32;

// Light a cell at a world position: the ambient and the sun, its shadow averaged over the
// BOX_FROXEL_SAMPLES points of the cell from a to b.
fn light_cell(p: vec3f, a: vec3f, b: vec3f) {
    let up = vec3f(0.0, 1.0, 0.0);
    cell_ambient = (ambient(up) + ambient(-up)) * 0.5;
    cell_sun = vec3f(0.0);
    cell_sun_dir = up;
    if (view.lights.x > 0u) {
        var visible = 0.0;
        for (var j = 0u; j < BOX_FROXEL_SAMPLES; j++) {
            visible += shadow_point(mix(a, b, froxel_sample(j)));
        }

        cell_sun_dir = normalize(lights[0].position);
        cell_sun = lights[0].color / PI * (visible / f32(BOX_FROXEL_SAMPLES)) *
            light_cookie(p, cell_sun_dir, up);
    }
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

@compute @workgroup_size(BOX_FROXEL_INJECT_GROUP, BOX_FROXEL_INJECT_GROUP, BOX_FROXEL_INJECT_GROUP)
fn inject(@builtin(global_invocation_id) id: vec3u) {
    if (any(id >= froxel.grid.xyz)) {
        return;
    }

    let uv = froxel_cell_uv(id.xy);
    let ray = froxel_ray(uv);
    let slices = f32(froxel.grid.z);
    let p = ray.origin + ray.dir * froxel_depth((f32(id.z) + 0.5) / slices);
    // The near and far side of the cell along its ray, between which the faces of the volumes
    // and the sun's shadow are sampled, so they cover a share of the cell instead of stepping
    // from slice to slice.
    let a = ray.origin + ray.dir * froxel_depth(f32(id.z) / slices);
    let b = ray.origin + ray.dir * froxel_depth((f32(id.z) + 1.0) / slices);
    let dir = normalize(ray.dir);
    var lit = false;
    var local = false;
    var extinction = 0.0;
    var scatter = vec3f(0.0);
    for (var i = 0u; i < froxel.grid.w; i++) {
        let fog = volumes[i];
        let qa = volume_local(fog, a);
        let qb = volume_local(fog, b);
        // The distance inside a box is concave along a segment: a cell whose sides are both
        // edge_fade inside is covered whole.
        var cover = 1.0;
        if (min(inside(fog, qa), inside(fog, qb)) < max(fog.edge_fade, 1e-6)) {
            cover = 0.0;
            for (var j = 0u; j < BOX_FROXEL_SAMPLES; j++) {
                cover += edge(fog, mix(qa, qb, froxel_sample(j)));
            }

            cover /= f32(BOX_FROXEL_SAMPLES);
        }

        let sigma = density(fog, cover, p);
        if (sigma <= 0.0) {
            continue;
        }

        if (!lit) {
            light_cell(p, a, b);
            lit = true;
        }

        var light = cell_ambient * fog.ambient_inscatter +
            cell_sun * (phase(dot(dir, cell_sun_dir), fog.asymmetry) * fog.sun_inscatter);
        if (fog.light_inscatter > 0.0) {
            if (!local) {
                light_cell_local(p);
                local = true;
            }

            light += light_local(dir, fog.asymmetry) * fog.light_inscatter;
        }

        extinction += sigma;
        scatter += fog.color * (sigma * (1.0 - fog.absorption)) * light;
    }

    if (extinction > 0.0 && view.aerial.w > 0.0) {
        scatter *= aerial_sample(vec4f(uv * view.viewport.xy, 0.0, 1.0), p).a;
    }

    textureStore(scatter_out, id, vec4f(scatter, extinction));
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

// Light scattered toward the camera (rgb) and transmittance (a) from the camera to a clip depth
// of a uv of the image, the far end of the grid for no depth.
fn lookup(uv: vec2f, depth: f32) -> vec4f {
    var d = froxel.range.z;
    if (depth > 0.0) {
        d = froxel_view_depth(froxel_world(uv, depth));
    }

    let c = froxel_uvw(uv, d);
    let s = textureSampleLevel(integrated, grid_sampler, c.xyz, 0.0);
    return mix(vec4f(0.0, 0.0, 0.0, 1.0), s, c.w);
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
