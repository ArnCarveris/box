// Wind sway of the vertices of a WindSway material (Scene.wind), the same in every pass that
// places them: the forward and depth prepass vertex stages of the mesh shaders, the shadow
// casters and the wireframe. The includer declares the material table (material.wgsl).
//
// The main bend is a function of the world position of a vertex only, so every shape of a plant
// with the same bend, height and base moves together without cracks: the height above the base
// over the height, clamped and squared, times the bend and the strength of the wind there. The
// strength is the speed times a gust factor read from a smooth noise field of gust patches that
// travels along the wind (its wrapped offset), and the plants sway around their lean in a slow
// wave along the wind. A vertex that moves sideways drops so the length to the base is about
// kept. The flutter adds a fast per vertex motion that grows with the distance from the origin
// of the shape over the flutter reach.
#pragma once

// Gust patches over which the gust field repeats along each axis (BOX_WIND_GUST_PERIOD), the
// period the gust offset wraps to.
const WIND_PERIOD: i32 = 32;

// Relative amplitudes of the sway along and across the wind (BOX_RENDER_WIND_SWAY and
// BOX_RENDER_WIND_CROSS).
const WIND_SWAY: f32 = 0.25;
const WIND_CROSS: f32 = 0.12;

// Wind speed of the full flutter (BOX_WIND_FULL_SPEED).
const WIND_FULL: f32 = 10.0;

// Flag of a record with a WindSway (BOX_RENDER_WIND_PRESENT).
const WIND_PRESENT: u32 = 1u;

const WIND_TAU: f32 = 6.283185307179586;

// World offset of a vertex and the derivative of its horizontal offset along world Y, which the
// normal bends by.
struct WindMove {
    offset: vec3f,
    slope: vec3f,
};

// Whether a material moves its vertices: it has a WindSway and the wind blows.
fn wind_sways(m: Material) -> bool {
    return (m.wind_flags & WIND_PRESENT) != 0u && m.wind_speed > 0.0;
}

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

// The move of a vertex at a world position of a shape whose origin is at origin, by the
// WindSway and the wind of its material; wind_sways(m) is true.
fn wind_move(world: vec3f, origin: vec3f, m: Material) -> WindMove {
    let dir = m.wind_direction;
    let across = vec2f(-dir.y, dir.x);
    let u = (world.xz - m.wind_offset) * m.wind_frequency;
    let gust = wind_noise(u);
    let strength = m.wind_speed * (1.0 + m.wind_gust * (2.0 * gust - 1.0));
    let phase = WIND_TAU * m.wind_sway - 0.5 * dot(world.xz, dir) * m.wind_frequency;
    let along = 1.0 + WIND_SWAY * sin(phase);
    let cross = WIND_CROSS * sin(2.0 * phase + 1.3);

    let h = world.y - m.wind_base;
    let t = clamp(h / m.wind_height, 0.0, 1.0);
    let amount = m.wind_bend * strength;
    let bend = amount * t * t;
    let d = dir * (bend * along) + across * (bend * cross);
    let rate = select(0.0, amount * 2.0 * t / m.wind_height, h > 0.0 && h < m.wind_height);
    let ds = dir * (rate * along) + across * (rate * cross);
    let drop = select(0.0, min(0.5 * dot(d, d) / max(h, 1e-3), h), h > 0.0);

    var o: WindMove;
    o.offset = vec3f(d.x, -drop, d.y);
    o.slope = vec3f(ds.x, 0.0, ds.y);
    if (m.wind_flutter > 0.0) {
        let reach = clamp(length(world - origin) / m.wind_reach, 0.0, 1.0);
        let size = m.wind_flutter * clamp(strength / WIND_FULL, 0.0, 1.0) * reach;
        let q = dot(world, vec3f(3.1, 1.7, 2.3));
        let a = WIND_TAU * m.wind_flutter_phase;
        o.offset += size * vec3f(0.4 * sin(a + q), sin(2.0 * a + 1.3 * q),
            0.4 * sin(a + 0.7 * q + 1.0));
    }
    return o;
}
