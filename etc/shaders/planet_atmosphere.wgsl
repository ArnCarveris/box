// Atmosphere shells of the planets a view draws (PlanetAtmosphere): one draw per planet of the
// back faces of the box around its top sphere, raymarched in the forward pass of the view after
// its opaque meshes and its sky, in a pass that reads the depth of the view instead of writing
// it, before its fog volumes, particles and blended draws.
//
// A pixel marches from where its ray enters the top sphere, or from the camera inside it, to
// where it leaves it, hits the ground sphere or the scene depth, whichever is first, with steps
// packed around the lowest point of the ray and jittered per pixel against banding, and the
// transmittance and multi scattering LUTs of the planet (atmosphere.wgsl). The target is
// multiplied by the mean transmittance and gets the in scattered light added.
//
// Group 0 is the view group of the forward pass, which this shader does not read.
#include "include/atmosphere_common.wgsl"

// Steps of a march along the longest chord of the shell, the one that grazes the ground
const SHELL_STEPS: f32 = 32.0;

// Fewest steps of a march; shorter rays, such as those that look down on the ground, take
// fewer steps in proportion to their length, a fractional count so it changes smoothly
const SHELL_MIN_STEPS: f32 = 12.0;

// How much the steps of a march are packed around the lowest point of the ray (0 even, 1
// quadratic), where the medium is densest
const SHELL_SPREAD: f32 = 0.75;

struct Planet {
    // bottom radius, top radius, Rayleigh scale height, Mie scale height in km
    radii: vec4f,
    // rgb: Rayleigh scattering at density 1, also its extinction
    rayleigh: vec4f,
    // rgb: Mie scattering at density 1
    mie_scattering: vec4f,
    // rgb: Mie extinction at density 1
    mie_extinction: vec4f,
    // rgb: ozone absorption at the peak of its tent
    ozone: vec4f,
    // rgb: linear albedo of the ground
    ground_albedo: vec4f,
    // xyz: world position of the center, w: km per world unit
    center: vec4f,
    // xyz: direction toward the sun, w: Mie anisotropy
    sun: vec4f,
    // rgb: top of atmosphere irradiance of the sun, w: scale of the in scattered light
    sun_color: vec4f,
    // xy: inverse of the render width and height in pixels
    viewport: vec4f,
    // world to clip space of the view
    view_proj: mat4x4f,
    // clip to world space of the view
    inv_view_proj: mat4x4f,
};

@group(1) @binding(0) var<uniform> u: Planet;
@group(1) @binding(1) var depth_texture: texture_depth_2d;
@group(1) @binding(2) var transmittance_lut: texture_2d<f32>;
@group(1) @binding(3) var multi_lut: texture_2d<f32>;
@group(1) @binding(4) var lut_sampler: sampler;

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
fn vs(@builtin(vertex_index) vertex: u32) -> @builtin(position) vec4f {
    let face = vertex / 6u;
    let s = CORNERS[vertex % 6u];
    let extent = u.radii.y / max(u.center.w, 1e-30);
    let world = u.center.xyz + (FACE_N[face] + FACE_U[face] * s.x + FACE_V[face] * s.y) * extent;
    var clip = u.view_proj * vec4f(world, 1.0);
    clip.z = max(clip.z, 0.0);
    return clip;
}

// Noise in [0, 1) that differs between neighboring pixels (Jimenez 2014), offset from the
// dither of the tonemapper so the two do not line up.
fn interleaved_gradient_noise(p: vec2f) -> f32 {
    let q = p + vec2f(5.588238);
    return fract(52.9829189 * fract(0.06711056 * q.x + 0.00583715 * q.y));
}

// World position of a uv of the image at a clip depth.
fn world_at(uv: vec2f, depth: f32) -> vec3f {
    let h = u.inv_view_proj * vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    return h.xyz / h.w;
}

@fragment
fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let uv = pos.xy * u.viewport.xy;
    let depth = textureLoad(depth_texture, vec2i(pos.xy), 0);
    let origin = world_at(uv, 1.0);
    let ray = world_at(uv, max(depth, 1e-7)) - origin;
    let scene = length(ray);
    if (!(scene > 0.0)) {
        discard;
    }

    let km = u.center.w;
    let rd = ray / scene;
    var ro = (origin - u.center.xyz) * km;
    var max_t = 1e9;
    if (depth > 0.0) {
        max_t = scene * km;
    }

    // Start the ray just before the top sphere, so a camera far away keeps the precision of the
    // sphere tests near the limb
    let skip = max(-dot(ro, rd) - top_radius(), 0.0);
    ro = ro + rd * skip;
    max_t = max_t - skip;
    if (max_t <= 0.0) {
        discard;
    }

    let seg = ray_segment(ro, rd, max_t, true);
    if (seg.y <= seg.x) {
        discard;
    }

    let rt = top_radius();
    let rb = bottom_radius();
    let chord = 2.0 * sqrt(max(rt * rt - rb * rb, 1e-6));
    let steps = clamp(SHELL_STEPS * (seg.y - seg.x) / chord, SHELL_MIN_STEPS, SHELL_STEPS);
    let jitter = interleaved_gradient_noise(floor(pos.xy));
    let s = scatter_shell(ro, rd, max_t, steps, SHELL_SPREAD, jitter, u.sun.xyz,
        u.sun_color.rgb, u.sun.w);
    let light = s.light * u.sun_color.w;
    let t = clamp((s.throughput.x + s.throughput.y + s.throughput.z) / 3.0, 0.0, 1.0);
    if (any(light != light) || t != t) {
        discard;
    }

    return vec4f(max(light, vec3f(0.0)), t);
}
