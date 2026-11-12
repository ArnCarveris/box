// Medium, phases and ray marching of the physically based atmosphere after Hillaire 2020,
// distances in km, shared by the LUT passes (atmosphere.wgsl) and the shells of the planets
// (planet_atmosphere.wgsl). The includer declares the uniform u, whose first members are the
// medium (box_render_atmosphere_desc_t: radii, rayleigh, mie_scattering, mie_extinction, ozone),
// and the transmittance_lut, multi_lut and lut_sampler bindings.
#pragma once
#include "atmosphere_medium.h"

const PI: f32 = 3.14159265358979323846;

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

// Cosine of the sun at which it sets below the ground horizon of a radius.
fn shadow_horizon(r: f32) -> f32 {
    let rb = bottom_radius();
    return -sqrt(max(0.0, 1.0 - (rb * rb) / max(r * r, 1e-6)));
}

// Sunlight that clears the ground at a radius, a smoothstep of half width in cosine around the
// horizon of the radius.
fn earth_shadow_soft(r: f32, mu_sun: f32, width: f32) -> f32 {
    let horizon = shadow_horizon(r);
    return smoothstep(horizon - width, horizon + width, mu_sun);
}

fn earth_shadow(r: f32, mu_sun: f32) -> f32 {
    return earth_shadow_soft(r, mu_sun, 0.02);
}

fn multi_to_uv(cos_sun: f32, r: f32) -> vec2f {
    let alt = r - bottom_radius();
    return vec2f(clamp(0.5 + 0.5 * cos_sun, 0.0, 1.0),
        clamp(alt / (top_radius() - bottom_radius()), 0.0, 1.0));
}

// Single scattering of a light (the sun or the moon) plus multi scattering along a ray inside
// the atmosphere up to max_t, with sigma scaled by k and the Mie phase asymmetry g, integrated as
// (S - S T) / sigma_t per step. ro is relative to the planet center.
fn scatter_ray(ro: vec3f, rd: vec3f, max_t: f32, steps: i32, k: f32, ground: bool, sun: vec3f,
    sun_color: vec3f, g: f32) -> Scatter {
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
    let mie = mie_phase(cos_view, g);
    for (var i = 0; i < steps; i = i + 1) {
        let pos = ro + rd * (seg.x + (f32(i) + 0.5) * dt);
        let st = scatter_step(pos, dt, k, rayleigh, mie, sun, sun_color, 0.02);
        s.light = s.light + s.throughput * st.light;
        s.throughput = s.throughput * st.throughput;
    }

    return s;
}

// One step of scatter_ray at a sample position: light is the in scattered light of the step
// as seen from its start, throughput its transmittance. shadow is the half width of the earth
// shadow transition in cosine.
fn scatter_step(pos: vec3f, dt: f32, k: f32, rayleigh: f32, mie: f32, sun: vec3f,
    sun_color: vec3f, shadow: f32) -> Scatter {
    let r = length(pos);
    let m = sample_medium(max(r - bottom_radius(), 0.0), k);
    let step_t = exp(-m.extinction * dt);
    let mu_sun = clamp(dot(pos / max(r, 1e-4), sun), -1.0, 1.0);
    let sun_t = sample_transmittance(r, mu_sun) * earth_shadow_soft(r, mu_sun, shadow);
    let single = (m.scattering_rayleigh * rayleigh + m.scattering_mie * mie) * sun_t;
    let psi = textureSampleLevel(multi_lut, lut_sampler, multi_to_uv(mu_sun, r), 0.0).rgb;
    let scattered = (single + psi * m.scattering) * sun_color;
    var s: Scatter;
    s.light = (scattered - scattered * step_t) / max(m.extinction, vec3f(1e-6));
    s.throughput = step_t;
    return s;
}

// Fraction of a march of the shell at a fraction x of its steps: the steps are spread by
// spread (0 even, 1 quadratic) so they are dense around the fraction low, the lowest point of
// the ray, where the medium is densest.
fn shell_spread(x: f32, low: f32, spread: f32) -> f32 {
    var q = x;
    if (x < low) {
        let a = 1.0 - x / low;
        q = low * (1.0 - a * a);
    } else if (low < 1.0) {
        let b = (x - low) / (1.0 - low);
        q = low + (1.0 - low) * b * b;
    }

    return mix(x, q, spread);
}

// scatter_ray for the shells of planets, which are seen from far away: the march of a ray that
// may end on the ground takes a fractional number of steps, so it does not jump between
// neighboring pixels, spread by spread around the lowest point of the ray, each sampled at a
// fraction jitter of the step instead of its middle. The earth shadow of a step is widened by
// the range of the horizons over the radii of the step, so its samples do not turn on one by
// one across the terminator.
fn scatter_shell(ro: vec3f, rd: vec3f, max_t: f32, steps: f32, spread: f32, jitter: f32,
    sun: vec3f, sun_color: vec3f, g: f32) -> Scatter {
    var s: Scatter;
    s.light = vec3f(0.0);
    s.throughput = vec3f(1.0);
    let seg = ray_segment(ro, rd, max_t, true);
    if (seg.y <= seg.x) {
        return s;
    }

    let len = seg.y - seg.x;
    let low = clamp((-dot(ro, rd) - seg.x) / len, 0.0, 1.0);
    let n = max(steps, 1.0);
    let count = i32(ceil(n));
    let cos_view = clamp(dot(rd, sun), -1.0, 1.0);
    let rayleigh = rayleigh_phase(cos_view);
    let mie = mie_phase(cos_view, g);
    var t0 = seg.x;
    var h0 = shadow_horizon(length(ro + rd * t0));
    for (var i = 0; i < count; i = i + 1) {
        let x0 = f32(i) / n;
        let x1 = min(f32(i + 1) / n, 1.0);
        let t1 = seg.x + len * shell_spread(x1, low, spread);
        let dt = t1 - t0;
        let h1 = shadow_horizon(length(ro + rd * t1));
        let pos = ro + rd * (seg.x + len * shell_spread(mix(x0, x1, jitter), low, spread));
        let shadow = max(0.02, 0.5 * abs(h1 - h0));
        let st = scatter_step(pos, dt, 1.0, rayleigh, mie, sun, sun_color, shadow);
        s.light = s.light + s.throughput * st.light;
        s.throughput = s.throughput * st.throughput;
        t0 = t1;
        h0 = h1;
    }

    return s;
}
