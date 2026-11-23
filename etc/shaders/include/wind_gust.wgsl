// Gust field of the Wind, shared by the wind sway of the meshes (wind.wgsl) and the particles
// with ParticleForces.global_wind (particle_sim.wgsl), so plants and particles feel the same
// gust at the same place and time: broad bands of gust stretched across the wind, the noise of
// gust patches over world X and Z averaged across the wind, with finer detail of a quarter patch,
// that travel along the wind by the offset the CPU integrates (WindStatus), so a gust front rolls
// through the plants as a wave. Every sample of the field repeats over the period of the offset,
// so the field does not jump when the offset wraps. The C twin of the field is box_particle_wind_*
// in src/particle/simulate.h.
#pragma once

// Gust patches over which the gust field repeats along each axis (BOX_WIND_GUST_PERIOD), the
// period the gust offset wraps to.
const WIND_PERIOD: i32 = 32;

// Spacing in gust patches across the wind of the samples a band averages
// (BOX_PARTICLE_WIND_BAND_SPACING), and the contrast that restores the spread of the average
// (BOX_PARTICLE_WIND_BAND_CONTRAST).
const WIND_BAND_SPACING: f32 = 1.1;
const WIND_BAND_CONTRAST: f32 = 2.2;

// Detail patches per gust patch, a whole number so the detail repeats with the field
// (BOX_PARTICLE_WIND_DETAIL), and how far the detail moves the field
// (BOX_PARTICLE_WIND_DETAIL_AMOUNT).
const WIND_DETAIL: f32 = 4.0;
const WIND_DETAIL_AMOUNT: f32 = 0.15;

// Value in 0..1 of a lattice point of the gust field, which repeats every WIND_PERIOD points.
fn wind_hash(c: vec2i) -> f32 {
    let p = vec2u(((c % WIND_PERIOD) + WIND_PERIOD) % WIND_PERIOD);
    var h = p.x * 374761393u + p.y * 668265263u + 1013904223u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h = h ^ (h >> 16u);
    return f32(h & 0xFFFFu) / 65535.0;
}

// Smooth value noise of the gust field in 0..1 at a point in gust patches.
fn wind_noise(u: vec2f) -> f32 {
    let i = floor(u);
    let f = u - i;
    let c = vec2i(i);
    let s = f * f * (3.0 - 2.0 * f);
    let a = wind_hash(c);
    let b = wind_hash(c + vec2i(1, 0));
    let d = wind_hash(c + vec2i(0, 1));
    let e = wind_hash(c + vec2i(1, 1));
    return mix(mix(a, b, s.x), mix(d, e, s.x), s.y);
}

// Gust field in 0..1 at a point in gust patches, for a wind that blows along dir (normalized):
// the noise averaged across the wind into a band, raised back to its contrast and eased, plus a
// finer detail.
fn wind_band(u: vec2f, dir: vec2f) -> f32 {
    let a = vec2f(-dir.y, dir.x) * WIND_BAND_SPACING;
    let sum = wind_noise(u - 1.5 * a) + wind_noise(u - 0.5 * a) + wind_noise(u + 0.5 * a) +
        wind_noise(u + 1.5 * a);
    let b = clamp(0.5 + WIND_BAND_CONTRAST * (0.25 * sum - 0.5), 0.0, 1.0);
    let band = b * b * (3.0 - 2.0 * b);
    let detail = wind_noise(u * WIND_DETAIL + vec2f(7.0, 13.0));
    return clamp(band + 2.0 * WIND_DETAIL_AMOUNT * (detail - 0.5), 0.0, 1.0);
}

// Gust factor between 1 - gust and 1 + gust that multiplies the wind speed at the world X and Z
// of a point: offset is the wrapped offset of the field, dir the normalized direction of the
// wind, frequency 1 over the gust scale.
fn wind_gust(xz: vec2f, offset: vec2f, dir: vec2f, frequency: f32, gust: f32) -> f32 {
    let n = wind_band((xz - offset) * frequency, dir);
    return 1.0 + gust * (2.0 * n - 1.0);
}
