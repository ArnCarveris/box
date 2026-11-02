// Fog volumes: one box per FogVolume a view draws (box_render_fog_volume_t), raymarched in its
// forward pass after the opaque meshes and the sky, in a pass that reads the depth of the view
// (group 1) instead of writing it.
//
// The vertex stage builds the 36 vertices of the box from the vertex index, one instance per
// volume, centered on its center, along its axes by its extent. The pipeline culls the front
// faces, so the back faces cover the box from outside and from inside alike, and draws without
// a depth test; the clip depth is kept above the far plane, so a box that reaches past it still
// covers its pixels.
//
// A fragment casts the ray of its pixel from the near plane through inv_view_proj, which works
// for orthographic cameras too, intersects the box in its local frame and clamps the segment to
// the camera side of the near plane and to the distance of the scene depth; depth 0 is no
// geometry. A pixel whose ray misses the box, or meets geometry before it, is discarded.
//
// The segment is marched in steps scaled by its share of the diagonal of the box, at least 4,
// or 8 with noise, from an offset of one interleaved gradient noise step of the pixel, which does
// not change with the frame. The density is the density of the volume times a smoothstep from 0
// at the faces of the box to 1 at edge_fade inside.
//
// With a noise_strength above 0 the density is also scaled by the tileable 3D noise texture
// (group 1), sampled twice at the world position moved back by the offset of the volume and
// divided by noise_scale: a coarse sample and a detail sample at 3.1 times the frequency. The
// offset is the wind times the world time, computed on the CPU in double precision and wrapped to
// 10 noise_scale, over which both samples repeat, so the drift stays smooth after hours. The
// noise is in world space, not in the frame of the box, so a volume that moves or turns does not
// drag its billows along, and volumes that overlap share them. A volume without noise takes no
// samples. Each step integrates the in scattered light of the step analytically over its
// extinction (Frostbite, Hillaire 2015) and the march stops once the transmittance falls under
// TRANSMITTANCE_MIN.
//
// The light scattered at a point is the mean ambient of the view over up and down (ambient.wgsl,
// the environment or the hemisphere, not the probe volume) times ambient_inscatter, plus the
// first directional light over pi through its cascades (shadow_point) and light cookie, times
// the Henyey-Greenstein phase of the volume times 4 pi, as in fog.wgsl, and sun_inscatter, plus
// the point and spot lights of the clusters of the view (light_at). The other directional lights
// are not marched. A view that applies the aerial perspective attenuates the scattered light by
// the transmittance of the atmosphere at the middle of the segment.
//
// The point and spot lights are marched when light_inscatter is above 0 and the view has any. To
// bound their cost the march is split into FOG_LIGHT_SEGMENTS segments of its steps; the first
// step of a segment with density looks up the cluster of the middle of the segment and keeps its
// first FOG_LIGHT_SEGMENTS lights for the steps of the segment, so a ray makes at most that many
// cluster lookups and a dense cluster may lose lights. A light adds its color times attenuation
// (attenuation.wgsl) over pi, the phase and light_inscatter, through one depth comparison of its
// shadow tile when it has one (punctual_shadow_point). A volume or a view without them takes
// none of this path.
//
// The output is the scattered light in rgb and the transmittance of the background in a, blended
// as color = src + dst * src.a with the alpha of the target kept. The fragment discards and
// marches in non uniform control flow, so every texture read is a textureLoad or an explicit
// level or gradient sample.
#include "include/lights.wgsl"
#include "include/shadow.wgsl"
#include "include/point_shadow.wgsl"
#include "include/attenuation.wgsl"
#include "include/cookie.wgsl"
#include "include/ambient.wgsl"
#include "include/aerial.wgsl"

const PI: f32 = 3.141592653589793;

// Transmittance under which the march stops.
const TRANSMITTANCE_MIN: f32 = 0.001;

// Steps of a march at least.
const STEPS_MIN: i32 = 4;

// Steps of a march at least when the volume has noise.
const STEPS_MIN_NOISE: i32 = 8;

// Cluster lookups of a march at most, and the lights of each it keeps at most,
// BOX_FOG_VOLUME_LIGHT_STEPS.
const FOG_LIGHT_SEGMENTS: i32 = 8;

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
    steps: i32,
    // World offset of the noise, the wind times the world time wrapped to 10 noise_scale
    offset: vec3f,
    noise_strength: f32,
    noise_scale: f32,
    light_inscatter: f32,
    pad0: f32,
    pad1: f32,
};

struct FogDraw {
    inv_view_proj: mat4x4f,
};

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> clusters: array<u32>;

@group(1) @binding(0) var<storage, read> volumes: array<FogVolume>;
@group(1) @binding(1) var<uniform> draw: FogDraw;
@group(1) @binding(2) var depth_texture: texture_depth_2d;
@group(1) @binding(3) var noise: texture_3d<f32>;
@group(1) @binding(4) var noise_sampler: sampler;

struct VertexOut {
    @builtin(position) clip: vec4f,
    @location(0) @interpolate(flat) instance: u32,
};

// Normal and the two tangents of each face of the box, n = u x v, so the corners
// n + s.x * u + s.y * v in the order of CORNERS turn counter clockwise seen from outside.
const FACE_N = array<vec3f, 6>(
    vec3f(1.0, 0.0, 0.0), vec3f(-1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0),
    vec3f(0.0, -1.0, 0.0), vec3f(0.0, 0.0, 1.0), vec3f(0.0, 0.0, -1.0));
const FACE_U = array<vec3f, 6>(
    vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0), vec3f(0.0, 0.0, 1.0),
    vec3f(1.0, 0.0, 0.0), vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0));
const FACE_V = array<vec3f, 6>(
    vec3f(0.0, 0.0, 1.0), vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0),
    vec3f(0.0, 0.0, 1.0), vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0));

// Corners of the two triangles of a face.
const CORNERS = array<vec2f, 6>(
    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
    vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));

@vertex
fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> VertexOut {
    let fog = volumes[instance];
    let face = vertex / 6u;
    let s = CORNERS[vertex % 6u];
    let local = (FACE_N[face] + FACE_U[face] * s.x + FACE_V[face] * s.y) * fog.extent;
    let world = fog.center + fog.axes[0].xyz * local.x + fog.axes[1].xyz * local.y +
        fog.axes[2].xyz * local.z;
    var out: VertexOut;
    out.clip = view.view_proj * vec4f(world, 1.0);
    out.clip.z = max(out.clip.z, 0.0);
    out.instance = instance;
    return out;
}

// World position of a uv of the image at a clip depth.
fn world_at(uv: vec2f, depth: f32) -> vec4f {
    return draw.inv_view_proj * vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
}

// Henyey-Greenstein phase of an asymmetry g times 4 pi, so isotropic scattering is 1.
fn phase(cos_theta: f32, g: f32) -> f32 {
    let d = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4);
    return (1.0 - g * g) / (d * sqrt(d));
}

// Interleaved gradient noise of a pixel (Jimenez 2014), in [0, 1).
fn gradient_noise(pixel: vec2f) -> f32 {
    return fract(52.9829189 * fract(dot(pixel, vec2f(0.06711056, 0.00583715))));
}

// Share of the density of a volume at a point of its local frame: 0 at the faces of the box, 1
// from edge_fade inside.
fn edge(fog: FogVolume, q: vec3f) -> f32 {
    let inside = fog.extent - abs(q);
    let d = min(min(inside.x, inside.y), inside.z);
    if (fog.edge_fade <= 0.0) {
        return select(0.0, 1.0, d > 0.0);
    }

    return smoothstep(0.0, fog.edge_fade, d);
}

// Density of a volume at a point, q in its local frame and p in the world.
fn density(fog: FogVolume, q: vec3f, p: vec3f) -> f32 {
    var value = fog.density * edge(fog, q);
    if (value > 0.0 && fog.noise_strength > 0.0) {
        let uvw = (p - fog.offset) / fog.noise_scale;
        let n = textureSampleLevel(noise, noise_sampler, uvw, 0.0).r;
        let detail = textureSampleLevel(noise, noise_sampler, uvw * 3.1 + vec3f(0.37), 0.0).r;
        let v = n * 0.75 + detail * 0.25;
        value *= mix(1.0, saturate(v * 2.0), fog.noise_strength);
    }

    return value;
}

// Point and spot lights of the segment of the march the fragment is in, indices into lights.
var<private> segment_lights: array<u32, FOG_LIGHT_SEGMENTS>;
var<private> segment_count: u32;

// Keep the first FOG_LIGHT_SEGMENTS point and spot lights of the cluster of a position for the
// steps of a segment.
fn segment_lookup(p: vec3f) {
    let cluster = cluster_index(view, cluster_view_position(view, p));
    let first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster];
    let count = min(cluster_lights(clusters[2u + 2u * cluster]), u32(FOG_LIGHT_SEGMENTS));
    for (var i = 0u; i < count; i++) {
        segment_lights[i] = clusters[first + i];
    }
    segment_count = count;
}

// Light the point and spot lights of the segment scatter at a position toward the eye along a
// ray direction: color times attenuation over pi, the phase of the volume and light_inscatter,
// through one comparison of the shadow tile of a light that has one.
fn light_at(fog: FogVolume, p: vec3f, dir: vec3f) -> vec3f {
    var sum = vec3f(0.0);
    for (var i = 0u; i < segment_count; i++) {
        let light = lights[segment_lights[i]];
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

        sum += light.color * (a / PI * phase(dot(dir, l), fog.asymmetry) * visible);
    }

    return sum * fog.light_inscatter;
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4f {
    let fog = volumes[in.instance];
    let uv = in.clip.xy * view.viewport.zw;
    let depth = textureLoad(depth_texture, vec2i(in.clip.xy), 0);
    let near_ = world_at(uv, 1.0);
    let origin = near_.xyz / near_.w;
    let far_ = world_at(uv, max(depth, 1e-7));
    let ray = far_.xyz / far_.w - origin;
    let scene = length(ray);
    let dir = ray / max(scene, 1e-6);

    let rel = origin - fog.center;
    let ax = fog.axes[0].xyz;
    let ay = fog.axes[1].xyz;
    let az = fog.axes[2].xyz;
    let o = vec3f(dot(rel, ax), dot(rel, ay), dot(rel, az));
    let d = vec3f(dot(dir, ax), dot(dir, ay), dot(dir, az));
    let tiny = select(vec3f(-1e-8), vec3f(1e-8), d >= vec3f(0.0));
    let inv = 1.0 / select(d, tiny, abs(d) < vec3f(1e-8));
    let t0 = (-fog.extent - o) * inv;
    let t1 = (fog.extent - o) * inv;
    let lo = min(t0, t1);
    let hi = max(t0, t1);
    let t_near = max(max(max(lo.x, lo.y), lo.z), 0.0);
    var t_far = min(min(hi.x, hi.y), hi.z);
    if (depth > 0.0) {
        t_far = min(t_far, scene);
    }

    if (t_far <= t_near) {
        discard;
    }

    let span = t_far - t_near;
    let diagonal = max(2.0 * length(fog.extent), 1e-5);
    let steps_min = select(STEPS_MIN, STEPS_MIN_NOISE, fog.noise_strength > 0.0);
    let budget = max(fog.steps, steps_min);
    let steps = clamp(i32(ceil(f32(budget) * span / diagonal)), steps_min, budget);
    let ds = span / f32(steps);
    let jitter = gradient_noise(floor(in.clip.xy));

    let up = vec3f(0.0, 1.0, 0.0);
    let ambient_light = (ambient(up) + ambient(-up)) * 0.5 * fog.ambient_inscatter;
    let sun = view.lights.x > 0u;
    var l = up;
    var sun_light = vec3f(0.0);
    if (sun) {
        l = normalize(lights[0].position);
        sun_light = lights[0].color / PI * phase(dot(dir, l), fog.asymmetry) *
            fog.sun_inscatter;
    }

    let local = fog.light_inscatter > 0.0 && view.lights.y > view.lights.x;
    let segments = min(steps, FOG_LIGHT_SEGMENTS);
    var segment = -1;
    segment_count = 0u;

    var transmittance = 1.0;
    var inscatter = vec3f(0.0);
    for (var i = 0; i < steps; i++) {
        let t = t_near + (f32(i) + jitter) * ds;
        let p = origin + dir * t;
        let sigma = density(fog, o + d * t, p);
        if (sigma <= 0.0) {
            continue;
        }

        var light = ambient_light;
        if (sun) {
            light += sun_light * shadow_point(p) * light_cookie(p, l, up);
        }

        if (local) {
            let k = i * segments / steps;
            if (k != segment) {
                segment = k;
                let middle = t_near + (f32(k) + 0.5) * span / f32(segments);
                segment_lookup(origin + dir * middle);
            }

            light += light_at(fog, p, dir);
        }

        let extinction = sigma;
        let scatter = sigma * (1.0 - fog.absorption);
        let step_t = exp(-extinction * ds);
        let s = fog.color * scatter * light;
        let sint = (s - s * step_t) / max(extinction, 1e-5);
        inscatter += transmittance * sint;
        transmittance *= step_t;
        if (transmittance < TRANSMITTANCE_MIN) {
            break;
        }
    }

    if (view.aerial.w > 0.0) {
        let mid = origin + dir * (0.5 * (t_near + t_far));
        inscatter *= aerial_sample(in.clip, mid).a;
    }

    return vec4f(inscatter, transmittance);
}
