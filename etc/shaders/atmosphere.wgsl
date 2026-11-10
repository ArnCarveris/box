// Lookup tables of the physically based atmosphere after Hillaire 2020, distances in km. The
// transmittance and multi scattering LUTs belong to a scene, the sky view and aerial perspective
// to a view. Every pass is a compute entry point with 8x8x1 workgroups that writes an
// rgba16float storage texture, the 2D LUTs included, so no pass needs a render target.
#include "include/atmosphere_medium.h"
#include "include/cube.wgsl"

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
    // cosine and sine of the angular radius of the sun disk, disk intensity, per view
    sun_disk: vec4f,
    // xyz: direction toward the moon, w: its illumination, per view
    moon: vec4f,
    // rgb: top of atmosphere irradiance of the moon, per view
    moon_color: vec4f,
    // cosine and sine of the angular radius of the moon disk, disk intensity times albedo,
    // earthshine, per view
    moon_disk: vec4f,
    // clip to world space of the view
    inv_view_proj: mat4x4f,
    // xyz: direction toward the main light, w: the fade of its disk at the horizon, per view
    fog_light: vec4f,
    // rgb: radiance toward the moon when it is the main light, w: 1 when it is, per view
    fog_moon: vec4f,
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
@group(0) @binding(7) var out_cube: texture_storage_2d_array<rgba16float, write>;

const PI: f32 = 3.14159265358979323846;
const TRANSMITTANCE_STEPS: i32 = BOX_ATMOS_TRANSMITTANCE_STEPS;
const MULTI_DIRECTIONS: i32 = 8;
const MULTI_STEPS: i32 = 20;
const SKY_STEPS: i32 = 30;
const AERIAL_STEPS: i32 = 6;
// Coefficient u of the linear limb darkening law 1 - u (1 - mu)
const LIMB_DARKENING: f32 = 0.6;

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

// The sky view LUT at a uv, the azimuth u wrapping and the zenith v clamped. The shared sampler
// clamps both axes, so the two texels of u are each sampled at their center, where only v is
// filtered, and blended here; texel 0 and the last texel then blend across world -Z, where
// atan2(x, z) wraps from +pi to -pi.
fn sample_sky_lut(uv: vec2f) -> vec3f {
    let width = f32(textureDimensions(sky_lut).x);
    let x = uv.x * width - 0.5;
    let x0 = floor(x);
    let t = x - x0;
    let u0 = (x0 - width * floor(x0 / width) + 0.5) / width;
    let u1 = (x0 + 1.0 - width * floor((x0 + 1.0) / width) + 0.5) / width;
    let a = textureSampleLevel(sky_lut, lut_sampler, vec2f(u0, uv.y), 0.0).rgb;
    let b = textureSampleLevel(sky_lut, lut_sampler, vec2f(u1, uv.y), 0.0).rgb;
    return mix(a, b, t);
}

// The u of the sky view LUT of the azimuth of a world direction. atan2(0, 0) is undefined (NaN on
// Metal), and the hand filtered u of sample_sky_lut would carry it into the result, so straight
// up or down, where every azimuth is the same, takes the center column.
fn sky_azimuth_u(rd: vec3f) -> f32 {
    if (rd.x == 0.0 && rd.z == 0.0) {
        return 0.5;
    }
    return atan2(rd.x, rd.z) / (2.0 * PI) + 0.5;
}

fn camera_radius() -> f32 {
    return bottom_radius() + max(u.view.x, BOX_ATMOS_PLANET_OFFSET);
}

// Single scattering of a light (the sun or the moon) plus multi scattering along a ray inside
// the atmosphere, with sigma scaled by k, integrated as (S - S T) / sigma_t per step.
fn in_scatter(ro: vec3f, rd: vec3f, max_t: f32, steps: i32, k: f32, ground: bool, sun: vec3f,
    sun_color: vec3f) -> Scatter {
    var s: Scatter;
    s.light = vec3f(0.0);
    s.throughput = vec3f(1.0);
    let seg = ray_segment(ro, rd, max_t, ground);
    if (seg.y <= seg.x) {
        return s;
    }

    let dt = (seg.y - seg.x) / f32(steps);
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
        let scattered = (single + psi * m.scattering) * sun_color;
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
    let ro = vec3f(0.0, r, 0.0);
    let rd = sky_uv_to_direction((vec2f(gid.xy) + 0.5) / vec2f(dims), r);
    var light = in_scatter(ro, rd, 1e9, SKY_STEPS, 1.0, true, u.sun.xyz, u.sun_color.rgb).light;
    let moon_light = u.moon_color.rgb * u.moon.w;
    if (any(moon_light > vec3f(0.0))) {
        light += in_scatter(ro, rd, 1e9, SKY_STEPS, 1.0, true, u.moon.xyz, moon_light).light;
    }

    let night = 1.0 - smoothstep(-0.05, 0.02, u.sun.y);
    textureStore(out_2d, vec2i(gid.xy), vec4f(light + u.night.rgb * night, 1.0));
}

// Number of horizon samples of the fog ambient
const FOG_HORIZON_SAMPLES: i32 = 6;
// Mix of the fog ambient from the horizon toward the zenith
const FOG_ZENITH_MIX: f32 = 0.15;

// The sky view in a direction, from the camera
fn sky_sample(rd: vec3f) -> vec3f {
    return sample_sky_lut(direction_to_sky_uv(rd, camera_radius()));
}

// The sky view at the horizon toward the azimuth of a direction
fn sky_horizon(rd: vec3f) -> vec3f {
    let xz = max(length(rd.xz), 1e-6);
    return sky_sample(vec3f(rd.x / xz, 0.0, rd.z / xz));
}

@compute @workgroup_size(8, 8, 1)
fn fog(@builtin(global_invocation_id) gid: vec3u) {
    let dims = textureDimensions(out_2d);
    if (gid.x >= dims.x || gid.y >= dims.y) {
        return;
    }

    var horizon = vec3f(0.0);
    for (var i = 0; i < FOG_HORIZON_SAMPLES; i = i + 1) {
        let a = (f32(i) + 0.5) * (2.0 * PI / f32(FOG_HORIZON_SAMPLES));
        horizon = horizon + sky_horizon(vec3f(sin(a), 0.0, cos(a)));
    }

    let ambient = mix(horizon / f32(FOG_HORIZON_SAMPLES), sky_sample(vec3f(0.0, 1.0, 0.0)),
        FOG_ZENITH_MIX);
    var result = ambient;
    if (gid.x == 1u) {
        result = vec3f(0.0);
        if (u.fog_moon.w > 0.5) {
            result = u.fog_moon.rgb;
        } else if (length(u.fog_light.xyz) > 1e-4) {
            result = max(sky_horizon(u.fog_light.xyz) - ambient, vec3f(0.0)) * u.fog_light.w;
        }
    }

    textureStore(out_2d, vec2i(gid.xy), vec4f(result, 1.0));
}

// Sky irradiance samples of a workgroup of the environment, one per invocation
const ENV_SAMPLES: u32 = 64u;
// Sample directions per axis of the stratified sky irradiance
const ENV_SAMPLES_AXIS: u32 = 8u;

var<workgroup> env_samples: array<vec3f, 64>;

// Radiance of the sky view in sample i of a cosine weighted stratified set over the upper
// hemisphere, so pi times the mean of the samples is the irradiance of the ground.
fn env_sky_sample(i: u32) -> vec3f {
    let u1 = (f32(i % ENV_SAMPLES_AXIS) + 0.5) / f32(ENV_SAMPLES_AXIS);
    let u2 = (f32(i / ENV_SAMPLES_AXIS) + 0.5) / f32(ENV_SAMPLES_AXIS);
    let r = sqrt(u1);
    let phi = 2.0 * PI * u2;
    return sky_sample(vec3f(r * cos(phi), sqrt(max(0.0, 1.0 - u1)), r * sin(phi)));
}

// Irradiance of a light at the ground from top of atmosphere irradiance in a direction.
fn env_ground_light(dir: vec3f, color: vec3f) -> vec3f {
    let mu = clamp(dir.y, -1.0, 1.0);
    return color * sample_transmittance(bottom_radius(), mu) * max(mu, 0.0);
}

@compute @workgroup_size(8, 8, 1)
fn environment(@builtin(global_invocation_id) gid: vec3u,
    @builtin(local_invocation_index) lane: u32)
{
    env_samples[lane] = env_sky_sample(lane);
    workgroupBarrier();
    for (var stride = ENV_SAMPLES / 2u; stride > 0u; stride = stride / 2u) {
        if (lane < stride) {
            env_samples[lane] = env_samples[lane] + env_samples[lane + stride];
        }

        workgroupBarrier();
    }

    let sky = env_samples[0];
    let dims = textureDimensions(out_cube);
    if (gid.x >= dims.x || gid.y >= dims.y || gid.z >= 6u) {
        return;
    }

    let rd = cube_direction(gid.z, gid.xy, dims.x);
    let ground = ray_sphere(vec3f(0.0, camera_radius(), 0.0), rd, bottom_radius());
    var radiance = sky_sample(rd);
    if (ground.x > 0.0) {
        let irradiance = env_ground_light(u.sun.xyz, u.sun_color.rgb) +
            env_ground_light(u.moon.xyz, u.moon_color.rgb) + sky * (PI / f32(ENV_SAMPLES));
        radiance = u.ground_albedo.rgb * irradiance / PI;
    }

    textureStore(out_cube, vec2i(gid.xy), i32(gid.z), vec4f(radiance, 1.0));
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
        var s = in_scatter(vec3f(0.0, camera_radius(), 0.0), rd, distance, AERIAL_STEPS,
            u.view.w, false, u.sun.xyz, u.sun_color.rgb);
        let moon_light = u.moon_color.rgb * u.moon.w;
        if (any(moon_light > vec3f(0.0))) {
            let m = in_scatter(vec3f(0.0, camera_radius(), 0.0), rd, distance, AERIAL_STEPS,
                u.view.w, false, u.moon.xyz, moon_light);
            s.light = s.light + m.light * s.throughput;
        }
        let t = (s.throughput.x + s.throughput.y + s.throughput.z) / 3.0;
        let horizon = sample_sky_lut(vec2f(sky_azimuth_u(rd), 0.5));
        result = vec4f(mix(s.light, horizon, clamp(1.0 - t, 0.0, 1.0)), t);
    }

    textureStore(out_3d, vec3i(gid), result);
}

// The depth is compared with equal, so the position is invariant
struct SkyOut {
    @invariant @builtin(position) pos: vec4f,
    @location(0) ndc: vec2f,
};

@vertex fn sky_vs(@builtin(vertex_index) i: u32) -> SkyOut {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u)) * 2.0 - 1.0;
    var o: SkyOut;
    o.pos = vec4f(p, 0.0, 1.0);
    o.ndc = p;
    return o;
}

// Point of the sky view LUT of a world direction seen from a radius, the inverse of
// sky_uv_to_direction.
fn direction_to_sky_uv(rd: vec3f, r: f32) -> vec2f {
    let horizon = horizon_angle(r);
    let zenith = acos(clamp(rd.y, -1.0, 1.0));
    var v = 0.5;
    if (zenith < horizon) {
        v = 0.5 * sqrt(zenith / max(horizon, 1e-6));
    } else {
        v = 0.5 + 0.5 * sqrt(max(zenith - horizon, 0.0) / max(PI - horizon, 1e-6));
    }

    return vec2f(sky_azimuth_u(rd), clamp(v, 0.0, 1.0));
}

// Radiance of the sun disk in a direction seen from a radius, sun_cos and sin_sun the cosine and
// sine of the angle between the direction and the sun, aa_w the width of the anti-aliased edge
// in that cosine.
fn sun_disk(rd: vec3f, r: f32, sun_cos: f32, sin_sun: f32, aa_w: f32) -> vec3f {
    let sun = u.sun.xyz;
    let disk_cos = u.sun_disk.x;
    let sin_r = u.sun_disk.y;
    var result = vec3f(0.0);
    if (u.sun_disk.z > 0.0 && sun_cos > disk_cos - aa_w) {
        let ground = ray_sphere(vec3f(0.0, r, 0.0), rd, bottom_radius());
        if (ground.y < 0.0) {
            let t_sun = sample_transmittance(r, clamp(sun.y, -1.0, 1.0));
            // The sine of the angle from the center, exact near it where the cosine is not
            let s = min(sin_sun / max(sin_r, 1e-6), 1.0);
            let mu = sqrt(max(0.0, 1.0 - s * s));
            let limb = 1.0 - LIMB_DARKENING * (1.0 - mu);
            let aa = smoothstep(disk_cos - aa_w, disk_cos + aa_w, sun_cos);
            result = t_sun * u.sun_color.rgb / PI * u.sun_disk.z * limb * aa;
        }
    }

    return result;
}

// Radiance of the moon disk in a direction seen from a radius, moon_cos and sin_moon the
// cosine and sine of the angle between the direction and the moon, aa_w the width of the
// anti-aliased edge in that cosine. The direction hits a unit sphere at the distance that makes
// it subtend the moon radius; the normal at the hit, from the offset of the ray from the
// center, is exact near it where the cosine is not.
fn moon_disk(rd: vec3f, r: f32, moon_cos: f32, sin_moon: f32, aa_w: f32) -> vec3f {
    let moon = u.moon.xyz;
    let disk_cos = u.moon_disk.x;
    let sin_r = u.moon_disk.y;
    var result = vec3f(0.0);
    if (u.moon_disk.z > 0.0 && u.moon.w > 0.0 && moon_cos > disk_cos - aa_w) {
        let ground = ray_sphere(vec3f(0.0, r, 0.0), rd, bottom_radius());
        if (ground.y < 0.0) {
            let t_moon = sample_transmittance(r, clamp(moon.y, -1.0, 1.0));
            // Offset of the center from the ray, perpendicular to it, in moon radii
            var w = (moon_cos * rd - moon) / max(sin_r, 1e-6);
            let s = min(sin_moon / max(sin_r, 1e-6), 1.0);
            w = w * (s / max(length(w), 1e-6));
            let n = w - sqrt(max(0.0, 1.0 - s * s)) * rd;
            // The illumination in moon.w is zero without a sun, where u.sun is only a fallback
            let lit = select(0.0, max(0.0, dot(n, u.sun.xyz)), u.moon.w > 0.0);
            let shade = lit + u.moon_disk.w * (1.0 - lit);
            let aa = smoothstep(disk_cos - aa_w, disk_cos + aa_w, moon_cos);
            result = t_moon * u.moon_color.rgb / PI * u.moon_disk.z * shade * aa;
        }
    }

    return result;
}

@fragment fn sky_fs(f: SkyOut) -> @location(0) vec4f {
    let rd = normalize(unproject(f.ndc, 0.5) - unproject(f.ndc, 1.0));
    let r = camera_radius();
    let sun_cos = dot(rd, u.sun.xyz);
    // Derivatives in uniform control flow, before any branch. The cosine changes with the sine
    // of the angle from the sun, so outside the disk the width is scaled to the one at its edge.
    let sin_sun = length(cross(rd, u.sun.xyz));
    let aa_w = max(fwidth(sun_cos) * min(1.0, u.sun_disk.y / max(sin_sun, 1e-6)), 1e-6);
    let moon_cos = dot(rd, u.moon.xyz);
    let sin_moon = length(cross(rd, u.moon.xyz));
    let moon_aa_w = max(fwidth(moon_cos) * min(1.0, u.moon_disk.y / max(sin_moon, 1e-6)), 1e-6);
    var uv = direction_to_sky_uv(rd, r);
    uv.y = min(uv.y, 0.5);
    let sky = sample_sky_lut(uv);
    return vec4f(sky + sun_disk(rd, r, sun_cos, sin_sun, aa_w) +
        moon_disk(rd, r, moon_cos, sin_moon, moon_aa_w), 1.0);
}
