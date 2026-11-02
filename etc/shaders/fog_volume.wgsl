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
// from an offset of one interleaved gradient noise step of the pixel, which does not change with
// the frame. The density is the density of the volume times a smoothstep from 0 at the faces of
// the box to 1 at edge_fade inside. Each step integrates the in scattered light of the step
// analytically over its extinction (Frostbite, Hillaire 2015) and the march stops once the
// transmittance falls under TRANSMITTANCE_MIN.
//
// The light scattered at a point is the mean ambient of the view over up and down (ambient.wgsl,
// the environment or the hemisphere, not the probe volume) times ambient_inscatter, plus the
// first directional light over pi through its cascades (shadow_point) and light cookie, times
// the Henyey-Greenstein phase of the volume times 4 pi, as in fog.wgsl, and sun_inscatter. The
// other lights are not marched. A view that applies the aerial perspective attenuates the
// scattered light by the transmittance of the atmosphere at the middle of the segment.
//
// The output is the scattered light in rgb and the transmittance of the background in a, blended
// as color = src + dst * src.a with the alpha of the target kept. The fragment discards and
// marches in non uniform control flow, so every texture read is a textureLoad or an explicit
// level or gradient sample.
#include "include/lights.wgsl"
#include "include/shadow.wgsl"
#include "include/cookie.wgsl"
#include "include/ambient.wgsl"
#include "include/aerial.wgsl"

const PI: f32 = 3.141592653589793;

// Transmittance under which the march stops.
const TRANSMITTANCE_MIN: f32 = 0.001;

// Steps of a march at least.
const STEPS_MIN: i32 = 4;

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
};

struct FogDraw {
    inv_view_proj: mat4x4f,
};

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;

@group(1) @binding(0) var<storage, read> volumes: array<FogVolume>;
@group(1) @binding(1) var<uniform> draw: FogDraw;
@group(1) @binding(2) var depth_texture: texture_depth_2d;

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
    let budget = max(fog.steps, STEPS_MIN);
    let steps = clamp(i32(ceil(f32(budget) * span / diagonal)), STEPS_MIN, budget);
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

    var transmittance = 1.0;
    var inscatter = vec3f(0.0);
    for (var i = 0; i < steps; i++) {
        let t = t_near + (f32(i) + jitter) * ds;
        let density = fog.density * edge(fog, o + d * t);
        if (density <= 0.0) {
            continue;
        }

        let p = origin + dir * t;
        var light = ambient_light;
        if (sun) {
            light += sun_light * shadow_point(p) * light_cookie(p, l, up);
        }

        let extinction = density;
        let scatter = density * (1.0 - fog.absorption);
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
