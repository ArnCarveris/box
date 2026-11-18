// Gust field of the Wind, shared by the wind sway of the meshes (wind.wgsl) and the particles
// with ParticleForces.global_wind (particle_sim.wgsl), so plants and particles feel the same
// gust at the same place and time: a smooth noise field of gust patches over world X and Z that
// travels along the wind by the offset the CPU integrates (WindStatus). The C twin of the field
// is box_particle_wind_* in src/particle/simulate.h.
#pragma once

// Gust patches over which the gust field repeats along each axis (BOX_WIND_GUST_PERIOD), the
// period the gust offset wraps to.
const WIND_PERIOD: i32 = 32;

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

// Gust factor between 1 - gust and 1 + gust that multiplies the wind speed at the world X and Z
// of a point: offset is the wrapped offset of the field, frequency 1 over the gust scale.
fn wind_gust(xz: vec2f, offset: vec2f, frequency: f32, gust: f32) -> f32 {
    let n = wind_noise((xz - offset) * frequency);
    return 1.0 + gust * (2.0 * n - 1.0);
}
