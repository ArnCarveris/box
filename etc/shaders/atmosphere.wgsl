// Lookup tables of the physically based atmosphere after Hillaire 2020, distances in km. The
// transmittance and multi scattering LUTs belong to a scene, the sky view and aerial perspective
// to a view. Every pass is a compute entry point with 8x8x1 workgroups that writes an
// rgba16float storage texture, the 2D LUTs included, so no pass needs a render target.
//
// transmittance (256x64): Bruneton's (r, mu) mapping, x the distance to the top between its
// shortest and its horizon value, y the distance to the horizon; 40 midpoint steps.
// multi_scattering (32x32): x the cosine of the sun zenith from -1 to 1, y the altitude;
// psi_ms = L2 / (1 - F) over 64 directions of 20 steps, the ground bouncing ground_albedo.
// sky_view (192x108): x the absolute world azimuth atan2(x, z), y the zenith angle, squared on
// both sides of the horizon at y = 0.5; single scattering of the sun plus multi scattering,
// integrated as (S - S T) / sigma_t, plus the night floor that fades in as the sun sets.
// aerial (32x32x32, a 3D texture): x and y the uv of the screen, slice z the in scatter (blended
// toward the horizon color of the sky view by 1 - T) and the mean transmittance T from the
// camera to the distance z / (N - 1) * aerial_distance along the ray of its texel, 6 steps.
// Slice 0 is the identity (0, 0, 0, 1), so a distance d samples the volume at
// w = (d / aerial_distance * (N - 1) + 0.5) / N and a distance of 0 gives the identity.
//
// The planet center is under the camera: a world direction is a direction of the planet frame,
// the camera at (0, bottom + altitude, 0). The earth shadow is analytic, a smoothstep around
// the cosine of the horizon, instead of a ray test against the planet.
#include "include/atmosphere_medium.h"

struct Atmosphere {
    // bottom radius, top radius, Rayleigh scale height, Mie scale height
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
    // xyz: direction toward the sun, per view
    sun: vec4f,
    // rgb: top of atmosphere irradiance of the sun, per view
    sun_color: vec4f,
    // rgb: linear night tint times the night intensity, per view
    night: vec4f,
    // camera altitude, Mie anisotropy, aerial distance, aerial intensity, per view
    view: vec4f,
    // clip to world space of the view
    inv_view_proj: mat4x4f,
};

struct Medium {
    scattering_rayleigh: vec3f,
    scattering_mie: vec3f,
    scattering: vec3f,
    extinction: vec3f,
};

// In scattered light along a ray and the transmittance from its start
struct Scatter {
    light: vec3f,
    throughput: vec3f,
};

@group(0) @binding(0) var<uniform> u: Atmosphere;
@group(0) @binding(1) var transmittance_lut: texture_2d<f32>;
@group(0) @binding(2) var lut_sampler: sampler;
@group(0) @binding(3) var multi_lut: texture_2d<f32>;
@group(0) @binding(4) var sky_lut: texture_2d<f32>;
@group(0) @binding(5) var out_2d: texture_storage_2d<rgba16float, write>;
@group(0) @binding(6) var out_3d: texture_storage_3d<rgba16float, write>;

const PI: f32 = 3.14159265358979323846;
const TRANSMITTANCE_STEPS: i32 = BOX_ATMOS_TRANSMITTANCE_STEPS;
const MULTI_DIRECTIONS: i32 = 8;
const MULTI_STEPS: i32 = 20;
const SKY_STEPS: i32 = 30;
const AERIAL_STEPS: i32 = 6;

fn bottom_radius() -> f32 {
    return u.radii.x;
}

fn top_radius() -> f32 {
    return u.radii.y;
}

fn sample_medium(alt: f32, k: f32) -> Medium {
    let density_rayleigh = exp(-alt / u.radii.z);
    let density_mie = exp(-alt / u.radii.w);
    let density_ozone = max(0.0, 1.0 - abs(alt - BOX_ATMOS_OZONE_CENTER) / BOX_ATMOS_OZONE_WIDTH);
    var m: Medium;
    m.scattering_rayleigh = u.rayleigh.rgb * density_rayleigh * k;
    m.scattering_mie = u.mie_scattering.rgb * density_mie * k;
    m.scattering = m.scattering_rayleigh + m.scattering_mie;
    m.extinction = (u.rayleigh.rgb * density_rayleigh + u.mie_extinction.rgb * density_mie +
        u.ozone.rgb * density_ozone) * k;
    return m;
}

fn rayleigh_phase(c: f32) -> f32 {
    return (3.0 / (16.0 * PI)) * (1.0 + c * c);
}

// Cornette-Shanks
fn mie_phase(c: f32, g: f32) -> f32 {
    let k = (3.0 / (8.0 * PI)) * (1.0 - g * g) / (2.0 + g * g);
    let denom = 1.0 + g * g - 2.0 * g * c;
    return k * (1.0 + c * c) / max(pow(max(denom, 1e-6), 1.5), 1e-6);
}

// Distances along a ray to where it enters and leaves a sphere around the planet center, y
// negative when it misses the sphere.
fn ray_sphere(ro: vec3f, rd: vec3f, r: f32) -> vec2f {
    let b = dot(ro, rd);
    let c = dot(ro, ro) - r * r;
    let disc = b * b - c;
    if (disc < 0.0) {
        return vec2f(-1.0);
    }

    let sd = sqrt(disc);
    return vec2f(-b - sd, -b + sd);
}

// Part of a ray inside the atmosphere up to max_t: start, end and 1 when it ends on the ground
// (only tested with ground), end not above start when it misses.
fn ray_segment(ro: vec3f, rd: vec3f, max_t: f32, ground: bool) -> vec3f {
    let top = ray_sphere(ro, rd, top_radius());
    let start = max(top.x, 0.0);
    var end = min(top.y, max_t);
    var hit = 0.0;
    if (ground) {
        let g = ray_sphere(ro, rd, bottom_radius());
        if (g.y >= 0.0 && g.x > start && g.x < end) {
            end = g.x;
            hit = 1.0;
        }
    }

    return vec3f(start, end, hit);
}

fn uv_to_transmittance(uv: vec2f) -> vec2f {
    let rb = bottom_radius();
    let rt = top_radius();
    let h = sqrt(max(0.0, rt * rt - rb * rb));
    let rho = h * uv.y;
    let r = sqrt(rho * rho + rb * rb);
    let d_min = rt - r;
    let d_max = rho + h;
    let d = d_min + uv.x * (d_max - d_min);
    var mu = 1.0;
    if (d > 0.0) {
        mu = (h * h - rho * rho - d * d) / (2.0 * r * d);
    }

    return vec2f(r, clamp(mu, -1.0, 1.0));
}

fn transmittance_to_uv(r: f32, mu: f32) -> vec2f {
    let rb = bottom_radius();
    let rt = top_radius();
    let h = sqrt(max(0.0, rt * rt - rb * rb));
    let rho = sqrt(max(0.0, r * r - rb * rb));
    let disc = max(0.0, r * r * (mu * mu - 1.0) + rt * rt);
    let d = max(0.0, -r * mu + sqrt(disc));
    let d_min = rt - r;
    let d_max = rho + h;
    var x_mu = 0.0;
    if (d_max > d_min + 1e-6) {
        x_mu = (d - d_min) / (d_max - d_min);
    }

    let x_r = rho / max(h, 1e-6);
    return vec2f(clamp(x_mu, 0.0, 1.0), clamp(x_r, 0.0, 1.0));
}

fn sample_transmittance(r: f32, mu: f32) -> vec3f {
    return textureSampleLevel(transmittance_lut, lut_sampler, transmittance_to_uv(r, mu), 0.0).rgb;
}

fn earth_shadow(r: f32, mu_sun: f32) -> f32 {
    let rb = bottom_radius();
    let horizon = -sqrt(max(0.0, 1.0 - (rb * rb) / max(r * r, 1e-6)));
    return smoothstep(horizon - 0.02, horizon + 0.02, mu_sun);
}

fn multi_to_uv(cos_sun: f32, r: f32) -> vec2f {
    let alt = r - bottom_radius();
    return vec2f(clamp(0.5 + 0.5 * cos_sun, 0.0, 1.0),
        clamp(alt / (top_radius() - bottom_radius()), 0.0, 1.0));
}

// Zenith angle of the horizon seen from a radius
fn horizon_angle(r: f32) -> f32 {
    let rb = bottom_radius();
    let c = -sqrt(max(0.0, r * r - rb * rb)) / max(r, 1e-4);
    return acos(clamp(c, -1.0, 1.0));
}

fn sky_uv_to_direction(uv: vec2f, r: f32) -> vec3f {
    let horizon = horizon_angle(r);
    var zenith = 0.0;
    if (uv.y < 0.5) {
        let t = uv.y / 0.5;
        zenith = horizon * t * t;
    } else {
        let t = (uv.y - 0.5) / 0.5;
        zenith = horizon + (PI - horizon) * t * t;
    }

    let azimuth = (uv.x - 0.5) * 2.0 * PI;
    let sz = sin(zenith);
    return vec3f(sz * sin(azimuth), cos(zenith), sz * cos(azimuth));
}

fn camera_radius() -> f32 {
    return bottom_radius() + max(u.view.x, BOX_ATMOS_PLANET_OFFSET);
}

// Single scattering of the sun plus multi scattering along a ray inside the atmosphere, with
// sigma scaled by k, integrated as (S - S T) / sigma_t per step.
fn in_scatter(ro: vec3f, rd: vec3f, max_t: f32, steps: i32, k: f32, ground: bool) -> Scatter {
    var s: Scatter;
    s.light = vec3f(0.0);
    s.throughput = vec3f(1.0);
    let seg = ray_segment(ro, rd, max_t, ground);
    if (seg.y <= seg.x) {
        return s;
    }

    let dt = (seg.y - seg.x) / f32(steps);
    let sun = u.sun.xyz;
    let cos_view = clamp(dot(rd, sun), -1.0, 1.0);
    let rayleigh = rayleigh_phase(cos_view);
    let mie = mie_phase(cos_view, u.view.y);
    for (var i = 0; i < steps; i = i + 1) {
        let pos = ro + rd * (seg.x + (f32(i) + 0.5) * dt);
        let r = length(pos);
        let m = sample_medium(max(r - bottom_radius(), 0.0), k);
        let step_t = exp(-m.extinction * dt);
        let mu_sun = clamp(dot(pos / max(r, 1e-4), sun), -1.0, 1.0);
        let sun_t = sample_transmittance(r, mu_sun) * earth_shadow(r, mu_sun);
        let single = (m.scattering_rayleigh * rayleigh + m.scattering_mie * mie) * sun_t;
        let psi = textureSampleLevel(multi_lut, lut_sampler, multi_to_uv(mu_sun, r), 0.0).rgb;
        let scattered = (single + psi * m.scattering) * u.sun_color.rgb;
        let integral = (scattered - scattered * step_t) / max(m.extinction, vec3f(1e-6));
        s.light = s.light + s.throughput * integral;
        s.throughput = s.throughput * step_t;
    }

    return s;
}

@compute @workgroup_size(8, 8, 1)
fn transmittance(@builtin(global_invocation_id) gid: vec3u) {
    let dims = textureDimensions(out_2d);
    if (gid.x >= dims.x || gid.y >= dims.y) {
        return;
    }

    let p = uv_to_transmittance((vec2f(gid.xy) + 0.5) / vec2f(dims));
    let ro = vec3f(0.0, p.x, 0.0);
    let rd = vec3f(sqrt(max(0.0, 1.0 - p.y * p.y)), p.y, 0.0);
    let seg = ray_segment(ro, rd, 1e9, true);
    var depth = vec3f(0.0);
    if (seg.y > seg.x) {
        let dt = (seg.y - seg.x) / f32(TRANSMITTANCE_STEPS);
        for (var i = 0; i < TRANSMITTANCE_STEPS; i = i + 1) {
            let pos = ro + rd * (seg.x + (f32(i) + 0.5) * dt);
            depth = depth + sample_medium(max(length(pos) - bottom_radius(), 0.0), 1.0).extinction *
                dt;
        }
    }

    textureStore(out_2d, vec2i(gid.xy), vec4f(exp(-depth), 1.0));
}

// Isotropic single scattering of a ray, its ground bounce, and in w the fraction of light the
// ray transfers to second order, f_ms of Hillaire.
fn multi_ray(ro: vec3f, rd: vec3f, sun: vec3f) -> vec4f {
    let seg = ray_segment(ro, rd, 1e9, true);
    if (seg.y <= seg.x) {
        return vec4f(0.0);
    }

    let dt = (seg.y - seg.x) / f32(MULTI_STEPS);
    let phase = 1.0 / (4.0 * PI);
    var throughput = vec3f(1.0);
    var light = vec3f(0.0);
    var transfer = vec3f(0.0);
    for (var i = 0; i < MULTI_STEPS; i = i + 1) {
        let pos = ro + rd * (seg.x + (f32(i) + 0.5) * dt);
        let r = length(pos);
        let m = sample_medium(max(r - bottom_radius(), 0.0), 1.0);
        let step_t = exp(-m.extinction * dt);
        let mu_sun = clamp(dot(pos / max(r, 1e-4), sun), -1.0, 1.0);
        let sun_t = sample_transmittance(r, mu_sun) * earth_shadow(r, mu_sun);
        let integral = (m.scattering - m.scattering * step_t) / max(m.extinction, vec3f(1e-6));
        light = light + throughput * sun_t * phase * m.scattering * dt;
        transfer = transfer + throughput * integral;
        throughput = throughput * step_t;
    }

    if (seg.z > 0.0) {
        let ground = ro + rd * seg.y;
        let mu_sun = clamp(dot(ground / max(length(ground), 1e-4), sun), -1.0, 1.0);
        if (mu_sun > 0.0) {
            let sun_t = sample_transmittance(bottom_radius(), mu_sun);
            light = light + throughput * sun_t * u.ground_albedo.rgb * mu_sun / PI;
        }
    }

    return vec4f(light, (transfer.x + transfer.y + transfer.z) / 3.0);
}

@compute @workgroup_size(8, 8, 1)
fn multi_scattering(@builtin(global_invocation_id) gid: vec3u) {
    let dims = textureDimensions(out_2d);
    if (gid.x >= dims.x || gid.y >= dims.y) {
        return;
    }

    let uv = (vec2f(gid.xy) + 0.5) / vec2f(dims);
    let cos_sun = uv.x * 2.0 - 1.0;
    let alt = uv.y * (top_radius() - bottom_radius());
    let ro = vec3f(0.0, bottom_radius() + max(alt, BOX_ATMOS_PLANET_OFFSET), 0.0);
    let sun = vec3f(sqrt(max(0.0, 1.0 - cos_sun * cos_sun)), cos_sun, 0.0);
    let samples = 1.0 / f32(MULTI_DIRECTIONS * MULTI_DIRECTIONS);
    var second = vec3f(0.0);
    var transfer = 0.0;
    for (var i = 0; i < MULTI_DIRECTIONS; i = i + 1) {
        for (var j = 0; j < MULTI_DIRECTIONS; j = j + 1) {
            let phi = 2.0 * PI * (f32(i) + 0.5) / f32(MULTI_DIRECTIONS);
            let cos_theta = 1.0 - 2.0 * (f32(j) + 0.5) / f32(MULTI_DIRECTIONS);
            let sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
            let rd = vec3f(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
            let s = multi_ray(ro, rd, sun);
            second = second + s.rgb * samples;
            transfer = transfer + s.w * samples;
        }
    }

    textureStore(out_2d, vec2i(gid.xy), vec4f(second / max(1.0 - transfer, 1e-4), 1.0));
}

@compute @workgroup_size(8, 8, 1)
fn sky_view(@builtin(global_invocation_id) gid: vec3u) {
    let dims = textureDimensions(out_2d);
    if (gid.x >= dims.x || gid.y >= dims.y) {
        return;
    }

    let r = camera_radius();
    let rd = sky_uv_to_direction((vec2f(gid.xy) + 0.5) / vec2f(dims), r);
    let s = in_scatter(vec3f(0.0, r, 0.0), rd, 1e9, SKY_STEPS, 1.0, true);
    let night = 1.0 - smoothstep(-0.05, 0.02, u.sun.y);
    textureStore(out_2d, vec2i(gid.xy), vec4f(s.light + u.night.rgb * night, 1.0));
}

fn unproject(ndc: vec2f, z: f32) -> vec3f {
    let h = u.inv_view_proj * vec4f(ndc, z, 1.0);
    return h.xyz / h.w;
}

@compute @workgroup_size(8, 8, 1)
fn aerial(@builtin(global_invocation_id) gid: vec3u) {
    let dims = textureDimensions(out_3d);
    if (gid.x >= dims.x || gid.y >= dims.y || gid.z >= dims.z) {
        return;
    }

    let uv = (vec2f(gid.xy) + 0.5) / vec2f(dims.xy);
    let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let rd = normalize(unproject(ndc, 0.5) - unproject(ndc, 1.0));
    let distance = f32(gid.z) / f32(max(dims.z, 2u) - 1u) * u.view.z;
    var result = vec4f(0.0, 0.0, 0.0, 1.0);
    if (distance > 0.0) {
        let s = in_scatter(vec3f(0.0, camera_radius(), 0.0), rd, distance, AERIAL_STEPS,
            u.view.w, false);
        let t = (s.throughput.x + s.throughput.y + s.throughput.z) / 3.0;
        let horizon_u = clamp(atan2(rd.x, rd.z) / (2.0 * PI) + 0.5, 0.0, 1.0);
        let horizon = textureSampleLevel(sky_lut, lut_sampler, vec2f(horizon_u, 0.5), 0.0).rgb;
        result = vec4f(mix(s.light, horizon, clamp(1.0 - t, 0.0, 1.0)), t);
    }

    textureStore(out_3d, vec3i(gid), result);
}
