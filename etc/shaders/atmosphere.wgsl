// Lookup tables of the physically based atmosphere after Hillaire 2020, distances in km. The
// transmittance, multi scattering and sky irradiance LUTs belong to a scene, the sky view and
// aerial perspective to a view. Every pass is a compute entry point with 8x8x1 workgroups that
// writes an rgba16float storage texture, the 2D LUTs included, so no pass needs a render
// target. The sky draw of a view is a fragment entry point that also composites its cloud layers
// (CloudLayer), and so does the environment cubemap of a scene, seen from its capture point.
#include "include/atmosphere_common.wgsl"
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
    // x: cloud layers of the sky draw or the environment, y: km per world unit, per view
    clouds: vec4f,
    // the cloud layers by ascending altitude, per view
    cloud_layers: array<CloudLayer, 4>,
};

// A cloud layer of the sky draw
struct CloudLayer {
    // x: world Y of the layer minus that of the camera, y: one over its size, zw: texture
    // coordinate of the camera, wrapped
    plane: vec4f,
    // threshold, softness, density
    shape: vec4f,
    // rgb: linear tint
    tint: vec4f,
};

@group(0) @binding(0) var<uniform> u: Atmosphere;
@group(0) @binding(1) var transmittance_lut: texture_2d<f32>;
@group(0) @binding(2) var lut_sampler: sampler;
@group(0) @binding(3) var multi_lut: texture_2d<f32>;
@group(0) @binding(4) var sky_lut: texture_2d<f32>;
@group(0) @binding(5) var out_2d: texture_storage_2d<rgba16float, write>;
@group(0) @binding(6) var out_3d: texture_storage_3d<rgba16float, write>;
@group(0) @binding(7) var out_cube: texture_storage_2d_array<rgba16float, write>;
@group(0) @binding(8) var cloud_sampler: sampler;
@group(0) @binding(9) var cloud_0: texture_2d<f32>;
@group(0) @binding(10) var cloud_1: texture_2d<f32>;
@group(0) @binding(11) var cloud_2: texture_2d<f32>;
@group(0) @binding(12) var cloud_3: texture_2d<f32>;

const TRANSMITTANCE_STEPS: i32 = BOX_ATMOS_TRANSMITTANCE_STEPS;
const MULTI_DIRECTIONS: i32 = 8;
const MULTI_STEPS: i32 = 20;
const SKY_STEPS: i32 = 30;
const AERIAL_STEPS: i32 = 6;
const IRRADIANCE_DIRECTIONS: i32 = 8;
const IRRADIANCE_STEPS: i32 = 20;
// Coefficient u of the linear limb darkening law 1 - u (1 - mu)
const LIMB_DARKENING: f32 = 0.6;

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

// Single scattering of a light plus multi scattering along a ray from the camera, see
// scatter_ray, with the Mie phase asymmetry of the view.
fn in_scatter(ro: vec3f, rd: vec3f, max_t: f32, steps: i32, k: f32, ground: bool, sun: vec3f,
    sun_color: vec3f) -> Scatter {
    return scatter_ray(ro, rd, max_t, steps, k, ground, sun, sun_color, u.view.y);
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

// Sky irradiance of a horizontal surface facing up, per unit of sun irradiance: the single and
// multi scattered light of the sky (scatter_ray, isotropic Mie, the sun itself excluded)
// integrated over the upper hemisphere with the cosine, in Bruneton's sense of the ground
// irradiance. u is the cosine of the sun zenith angle from -1 to 1, v the altitude from the
// ground to the top, as the multi scattering LUT. The hemisphere is sampled by cosine: azimuths
// spread evenly and the sine of the zenith angle the square root of an even fraction, so the
// irradiance is pi times the mean radiance.
@compute @workgroup_size(8, 8, 1)
fn irradiance(@builtin(global_invocation_id) gid: vec3u) {
    let dims = textureDimensions(out_2d);
    if (gid.x >= dims.x || gid.y >= dims.y) {
        return;
    }

    let uv = (vec2f(gid.xy) + 0.5) / vec2f(dims);
    let cos_sun = uv.x * 2.0 - 1.0;
    let alt = uv.y * (top_radius() - bottom_radius());
    let ro = vec3f(0.0, bottom_radius() + max(alt, BOX_ATMOS_PLANET_OFFSET), 0.0);
    let sun = vec3f(sqrt(max(0.0, 1.0 - cos_sun * cos_sun)), cos_sun, 0.0);
    let samples = 1.0 / f32(IRRADIANCE_DIRECTIONS * IRRADIANCE_DIRECTIONS);
    var sum = vec3f(0.0);
    for (var i = 0; i < IRRADIANCE_DIRECTIONS; i = i + 1) {
        for (var j = 0; j < IRRADIANCE_DIRECTIONS; j = j + 1) {
            let phi = 2.0 * PI * (f32(i) + 0.5) / f32(IRRADIANCE_DIRECTIONS);
            let sin_theta = sqrt((f32(j) + 0.5) / f32(IRRADIANCE_DIRECTIONS));
            let cos_theta = sqrt(max(0.0, 1.0 - sin_theta * sin_theta));
            let rd = vec3f(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
            let s = scatter_ray(ro, rd, 1e9, IRRADIANCE_STEPS, 1.0, true, sun, vec3f(1.0), 0.0);
            sum = sum + s.light * samples;
        }
    }

    textureStore(out_2d, vec2i(gid.xy), vec4f(max(sum * PI, vec3f(0.0)), 1.0));
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
    let r = camera_radius();
    let ground = ray_sphere(vec3f(0.0, r, 0.0), rd, bottom_radius());
    let haze = sky_sample(rd);
    var radiance = haze;
    if (ground.x > 0.0) {
        let irradiance = env_ground_light(u.sun.xyz, u.sun_color.rgb) +
            env_ground_light(u.moon.xyz, u.moon_color.rgb) + sky * (PI / f32(ENV_SAMPLES));
        radiance = u.ground_albedo.rgb * irradiance / PI;
    }

    // The clouds seen from the capture point, the texel footprint from the next texels
    if (u.clouds.x > 0.0) {
        let drd_x = cube_direction(gid.z, gid.xy + vec2u(1u, 0u), dims.x) - rd;
        let drd_y = cube_direction(gid.z, gid.xy + vec2u(0u, 1u), dims.x) - rd;
        let clouds = cloud_layers(r, rd, haze, drd_x, drd_y);
        radiance = radiance * (1.0 - clouds.a) + clouds.rgb;
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

// Smallest ramp of the coverage remap, which makes a softness of 0 a step at the threshold,
// BOX_RENDER_CLOUD_MIN_RAMP
const CLOUD_MIN_RAMP: f32 = 1e-5;
// Smallest |y| of a ray direction that hits a cloud layer, which keeps the distance and the
// texture gradients finite at the horizon
const CLOUD_MIN_GRAZING: f32 = 1e-4;
// Fraction of the light a cloud scatters rather than absorbs
const CLOUD_ALBEDO: f32 = 0.9;
// Henyey-Greenstein asymmetry of the glow of the clouds toward the sun and moon
const CLOUD_FORWARD_G: f32 = 0.6;
// Weight of the forward glow in the phase of the clouds, the rest isotropic
const CLOUD_FORWARD_WEIGHT: f32 = 0.5;
// Optical depth of a fully opaque layer for the light that crosses it, seen from the side away
// from the light
const CLOUD_THICKNESS: f32 = 1.6;
// Weight of the zenith in the sky light of the clouds, the rest from the horizon
const CLOUD_ZENITH_MIX: f32 = 0.6;
// Most taps of the coverage of a pixel along the long axis of its texture footprint. The remap
// sharpens a filtered value, so toward the horizon, where a pixel covers a long strip of the
// plane, one tap turns rows of pixels into hard stripes; the mean of the remapped taps is the
// coverage of the strip.
const CLOUD_TAPS: i32 = 4;
// Ratio of the long to the short axis of the footprint per tap: a rounder footprint takes
// fewer taps, one up to this ratio
const CLOUD_TAP_ANISOTROPY: f32 = 4.0;

fn hg_phase(c: f32, g: f32) -> f32 {
    let d = 1.0 + g * g - 2.0 * g * c;
    return (1.0 - g * g) / (4.0 * PI * pow(max(d, 1e-6), 1.5));
}

// Texture value of cloud layer i, sampled with explicit gradients, which need no uniform
// control flow.
fn cloud_texture(i: i32, uv: vec2f, dx: vec2f, dy: vec2f) -> f32 {
    var result = 0.0;
    switch (i) {
        case 0: {
            result = textureSampleGrad(cloud_0, cloud_sampler, uv, dx, dy).r;
        }
        case 1: {
            result = textureSampleGrad(cloud_1, cloud_sampler, uv, dx, dy).r;
        }
        case 2: {
            result = textureSampleGrad(cloud_2, cloud_sampler, uv, dx, dy).r;
        }
        default: {
            result = textureSampleGrad(cloud_3, cloud_sampler, uv, dx, dy).r;
        }
    }

    return result;
}

// Coverage of a texture value t, saturate((t - threshold) / (2 softness) + 0.5)
fn cloud_coverage(t: f32, threshold: f32, softness: f32) -> f32 {
    return saturate((t - threshold) / max(2.0 * softness, CLOUD_MIN_RAMP) + 0.5);
}

// Mean coverage of cloud layer i over the texture footprint of a pixel at uv, dx and dy the
// change of uv over the pixel and shape its threshold and softness: up to CLOUD_TAPS remapped
// taps spread over the long axis, each filtered over its part of it.
fn cloud_footprint(i: i32, uv: vec2f, dx: vec2f, dy: vec2f, shape: vec2f) -> f32 {
    let lx = dot(dx, dx);
    let ly = dot(dy, dy);
    let long_x = lx >= ly;
    let ratio = sqrt(max(lx, ly) / max(min(lx, ly), 1e-30));
    let taps = clamp(i32(ceil(ratio / CLOUD_TAP_ANISOTROPY)), 1, CLOUD_TAPS);
    let major = select(dy, dx, long_x) / f32(taps);
    let minor = select(dx, dy, long_x);
    var result = 0.0;
    for (var k = 0; k < taps; k = k + 1) {
        let p = uv + major * (f32(k) + 0.5 - 0.5 * f32(taps));
        result += cloud_coverage(cloud_texture(i, p, major, minor), shape.x, shape.y);
    }

    return result / f32(taps);
}

// Radiance a light (the sun or the moon) of top of atmosphere irradiance color in direction l
// scatters toward the camera along rd from a cloud at pos, relative to the planet center in km.
// From the side of the layer away from the light the light first crosses the cloud.
fn cloud_light(pos: vec3f, rd: vec3f, l: vec3f, color: vec3f, alpha: f32, lit_side: bool)
    -> vec3f
{
    let r = length(pos);
    let mu = clamp(dot(pos / max(r, 1e-4), l), -1.0, 1.0);
    let t = sample_transmittance(r, mu) * earth_shadow(r, mu);
    let iso = 1.0 / (4.0 * PI);
    // The layer sends the light it scatters into the two hemispheres, so twice the phase
    let phase = 2.0 * mix(iso, hg_phase(dot(rd, l), CLOUD_FORWARD_G), CLOUD_FORWARD_WEIGHT);
    let crossed = select(exp(-CLOUD_THICKNESS * alpha), 1.0, lit_side);
    return color * t * (CLOUD_ALBEDO * phase * crossed);
}

// Transmittance of the atmosphere between the camera at radius r0 and the point at radius r1
// along rd, mu0 and mu1 the cosines of rd with the up direction at both, from the ratio of the
// transmittances toward the top; a ray down takes the reverse ray, which does not hit the ground.
fn cloud_aerial_transmittance(r0: f32, mu0: f32, r1: f32, mu1: f32) -> vec3f {
    var t = vec3f(1.0);
    if (mu0 >= 0.0) {
        t = sample_transmittance(r0, mu0) / max(sample_transmittance(r1, mu1), vec3f(1e-6));
    } else {
        t = sample_transmittance(r1, -mu1) / max(sample_transmittance(r0, -mu0), vec3f(1e-6));
    }

    return clamp(t, vec3f(0.0), vec3f(1.0));
}

// The cloud layers along rd seen from a camera at radius r0, whose world X and Z the layers
// hold as their texture coordinate and whose world Y as their height, composited from far to
// near: above the camera from the highest, below from the lowest. haze is the sky in the
// direction, which the clouds fade into with the aerial perspective; drd_x and drd_y the change
// of rd over a pixel. The result is the premultiplied color and the alpha of the layers, over a
// background b: b (1 - a) + rgb.
fn cloud_layers(r0: f32, rd: vec3f, haze: vec3f, drd_x: vec3f, drd_y: vec3f) -> vec4f {
    let count = min(i32(u.clouds.x), 4);
    var color = vec3f(0.0);
    var clear_sky = 1.0;
    if (count == 0) {
        return vec4f(0.0);
    }

    let ro = vec3f(0.0, r0, 0.0);
    let ambient = CLOUD_ALBEDO * mix(sky_horizon(rd), sky_sample(vec3f(0.0, 1.0, 0.0)),
        CLOUD_ZENITH_MIX);
    let moon_color = u.moon_color.rgb * u.moon.w;
    let y = select(-1.0, 1.0, rd.y >= 0.0) * max(abs(rd.y), CLOUD_MIN_GRAZING);
    for (var k = 0; k < count; k = k + 1) {
        let i = select(k, count - 1 - k, rd.y > 0.0);
        let layer = u.cloud_layers[i];
        let height = layer.plane.x;
        let dist = height / y;
        if (height == 0.0 || dist <= 0.0) {
            continue;
        }

        // uv = origin + rd.xz height / (rd.y size) and its change over a pixel
        let s = height * layer.plane.y;
        let uv = layer.plane.zw + rd.xz * (s / y);
        let duv_x = s * (drd_x.xz * y - rd.xz * drd_x.y) / (y * y);
        let duv_y = s * (drd_y.xz * y - rd.xz * drd_y.y) / (y * y);
        let alpha = saturate(cloud_footprint(i, uv, duv_x, duv_y, layer.shape.xy) *
            layer.shape.z);
        if (alpha <= 0.0) {
            continue;
        }

        let pos = ro + rd * (dist * u.clouds.y);
        let r1 = length(pos);
        let mu1 = clamp(dot(pos / max(r1, 1e-4), rd), -1.0, 1.0);
        let below = height > 0.0;
        var light = cloud_light(pos, rd, u.sun.xyz, u.sun_color.rgb, alpha,
            below == (u.sun.y < 0.0));
        if (any(moon_color > vec3f(0.0))) {
            light += cloud_light(pos, rd, u.moon.xyz, moon_color, alpha,
                below == (u.moon.y < 0.0));
        }

        var t = vec3f(1.0);
        if (u.view.w > 0.0) {
            t = pow(cloud_aerial_transmittance(r0, clamp(rd.y, -1.0, 1.0), r1, mu1),
                vec3f(u.view.w));
        }

        let seen = (light + ambient) * layer.tint.rgb * t + haze * (1.0 - t);
        color = mix(color, seen, alpha);
        clear_sky = clear_sky * (1.0 - alpha);
    }

    return vec4f(color, 1.0 - clear_sky);
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
    let drd_x = dpdx(rd);
    let drd_y = dpdy(rd);
    var uv = direction_to_sky_uv(rd, r);
    uv.y = min(uv.y, 0.5);
    let sky = sample_sky_lut(uv);
    let background = sky + sun_disk(rd, r, sun_cos, sin_sun, aa_w) +
        moon_disk(rd, r, moon_cos, sin_moon, moon_aa_w);
    let clouds = cloud_layers(r, rd, sky, drd_x, drd_y);
    return vec4f(background * (1.0 - clouds.a) + clouds.rgb, 1.0);
}
