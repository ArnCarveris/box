// Particle emitters: the parameter block of an emitter for one frame, the particle a pool slot
// holds and the seeded draws, shared by the simulation and the draw. Every function has a C
// twin with a box_ prefix in src/particle/simulate.h; a change to one is a change to both.
// Lifetimes are whole microseconds, so the spawn accounting of the CPU and the integrate pass
// agree on the frame each particle dies in.
//
// Curves are rows of the curve table, which a shader that includes this file declares as
// curves: array<f32>, BOX_PARTICLE_CURVE_SAMPLES floats per row, baked by the extract.
#pragma once

// box_render_particle_block_t: ranges are (min, max) pairs, a max not above min hands out min.
struct Emitter {
    // World transform of the emitter
    model: mat4x4f,
    // xyz: unit direction in emitter space or zero, w: cosine of the spread
    direction: vec4f,
    // xyz: half size of a box, w: radius of a sphere or disc
    extents: vec4f,
    // xyz: world acceleration plus gravity, w: seconds of the frame
    acceleration: vec4f,
    // rgb: display encoded color
    color: vec4f,
    // xy: speed range, zw: size range
    speed_size: vec4f,
    // xy: initial rotation range in degrees, zw: spin range in degrees per second
    rotation_spin: vec4f,
    // xy: opacity range, zw: range of the speed away from the origin of the emitter
    opacity_radial: vec4f,
    // x: EmitterShape, y: seed, z: particles to spawn, w: spawn index of the first of them
    spawn: vec4u,
    // x: used slots, y: first spawn slot in the slot buffer, z: mesh table points,
    // w: index of the draw arguments
    pool: vec4u,
    // x: microseconds of the frame, y: shortest lifetime, z: microseconds the longest is above,
    // w: first sort key of the pool in the key buffer
    life: vec4u,
    // xyz: world velocity the drag pulls toward, w: drag rate
    wind: vec4f,
    // x: acceleration of the wander field, y: its frequency scale
    turbulence: vec4f,
    // xyz: phase of the wander field drawn from the seed, offset by the displacement of the
    // frame of reference
    wander: vec4f,
    // rgb: display encoded color variance
    variance: vec4f,
    // xyz: world velocity every spawn of the frame gets
    inherit: vec4f,
    // x: height of the ground plane, y: bounce, z: friction
    ground: vec4f,
    // World velocity of the frame of reference, added to the position only
    drift: vec3f,
    // Ground collision mode, GROUND_*
    collide: u32,
    // x: ParticleShape drawn, y: ParticleBlend, z: frames across the sheet, w: frames down
    look: vec4u,
    // x: frames a second, y: softness, z: stretch, w: fraction lit
    params: vec4f,
    // Rows of the size, opacity, rotation (turns) and spin curves in the curve table,
    // CURVE_CONSTANT for a constant curve; the area of the spin curve is in the row after it
    curve_rows: vec4u,
    // Values of those curves while constant
    curve_values: vec4f,
    // Linear color of a constant gradient
    gradient: vec3f,
    // First of the GRADIENT_ROWS rows of the gradient, CURVE_CONSTANT for a constant one
    gradient_row: u32,
    // x: vertices per particle, y: 1 when drawn through the sort keys, z: keys sorted, w: 1 when
    // the emitter has a ParticleAppearance
    draw: vec4u,
};

// A corner of a triangle of a billboard mesh, box_render_particle_corner_t: xyz the position
// times the shape scale and the normal, w the texture coordinates.
struct Corner {
    position: vec4f,
    normal: vec4f,
};

// A pool slot, box_particle_t: remaining is 0 for a free slot.
struct Particle {
    position: vec3f,
    remaining: u32,
    velocity: vec3f,
    index: u32,
};

// A sampled point of the surface of the source of an EmitterMesh, in its shape space.
struct TablePoint {
    position: vec4f,
    normal: vec4f,
};

const EMITTER_POINT: u32 = 0u;
const EMITTER_SPHERE: u32 = 1u;
const EMITTER_BOX: u32 = 2u;
const EMITTER_DISC: u32 = 3u;
const EMITTER_MESH: u32 = 4u;

// ParticleShape
const PARTICLE_CIRCLE: u32 = 0u;
const PARTICLE_SPRITE: u32 = 1u;
const PARTICLE_BILLBOARD: u32 = 2u;

// ParticleBlend
const PARTICLE_ALPHA: u32 = 0u;
const PARTICLE_ADDITIVE: u32 = 1u;

// Sort key of a free slot, after every live particle
const SORT_DEAD: u32 = 0xFFFFFFFFu;

// Hash streams, BOX_PARTICLE_STREAM_*
const STREAM_LIFE: u32 = 0u;
const STREAM_SPEED: u32 = 1u;
const STREAM_SIZE: u32 = 2u;
const STREAM_ROTATION: u32 = 3u;
const STREAM_SPIN: u32 = 4u;
const STREAM_OPACITY: u32 = 5u;
const STREAM_SHAPE: u32 = 6u;
const STREAM_DIRECTION: u32 = 9u;
const STREAM_POINT: u32 = 11u;
const STREAM_COLOR: u32 = 15u;
const STREAM_RADIAL: u32 = 19u;

// Ground collision modes, BOX_PARTICLE_GROUND_*
const GROUND_NONE: u32 = 0u;
const GROUND_BOUNCE: u32 = 1u;
const GROUND_DIE: u32 = 2u;

// Slot of a spawn with a lifetime of 0, which takes no slot
const NO_SLOT: u32 = 0xFFFFFFFFu;

// Samples of a row of the curve table, BOX_PARTICLE_CURVE_SAMPLES, from age 0 to 1
const CURVE_SAMPLES: u32 = 64u;

// Row of a constant curve, which reads no row, BOX_PARTICLE_CURVE_CONSTANT
const CURVE_CONSTANT: u32 = 0xFFFFFFFFu;

// Rows of the curve table a gradient takes, BOX_PARTICLE_GRADIENT_ROWS
const GRADIENT_ROWS: u32 = 4u;

const TAU: f32 = 6.28318530718;

// PCG hash of Jarzynski and Olano, "Hash Functions for GPU Rendering".
fn particle_pcg(v: u32) -> u32 {
    let state = v * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Hash of the draw of one stream of one particle of an emitter.
fn particle_hash(seed: u32, index: u32, stream: u32) -> u32 {
    return particle_pcg(particle_pcg(seed + particle_pcg(stream)) + index);
}

// A number in [0, 1) from the upper 24 bits of a hash, exact in f32.
fn particle_unit(h: u32) -> f32 {
    return f32(h >> 8u) * (1.0 / 16777216.0);
}

// A draw from a range with a number in [0, 1).
fn particle_range(r: vec2f, u: f32) -> f32 {
    return select(r.x, r.x + (r.y - r.x) * u, r.y > r.x);
}

// A draw of one stream of a particle from a range.
fn particle_draw(seed: u32, index: u32, stream: u32, r: vec2f) -> f32 {
    return particle_range(r, particle_unit(particle_hash(seed, index, stream)));
}

// A display encoded color decoded to linear.
fn particle_decode(c: vec3f) -> vec3f {
    return select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
}

// The linear color of a particle, each channel drawn from its own stream around the display
// encoded color and clamped to 0 to 1 before it is decoded.
fn particle_color(seed: u32, index: u32, color: vec3f, variance: vec3f) -> vec3f {
    let u = vec3f(
        particle_unit(particle_hash(seed, index, STREAM_COLOR)),
        particle_unit(particle_hash(seed, index, STREAM_COLOR + 1u)),
        particle_unit(particle_hash(seed, index, STREAM_COLOR + 2u)));
    return particle_decode(clamp(color + (2.0 * u - 1.0) * variance, vec3f(0.0), vec3f(1.0)));
}

// The lifetime of a particle in microseconds, integers only.
fn particle_life(seed: u32, index: u32, min: u32, span: u32) -> u32 {
    let h = particle_hash(seed, index, STREAM_LIFE);
    return select(min, min + h % (span + 1u), span != 0u);
}

// Microseconds a particle has left after a frame, 0 once its age reached its lifetime.
fn particle_age(remaining: u32, delta: u32) -> u32 {
    return select(0u, remaining - delta, remaining > delta);
}

// A spawn position inside a shape in emitter space; a mesh spawns at its table point instead.
fn particle_shape(shape: u32, radius: f32, extents: vec3f, u: vec3f) -> vec3f {
    var result = vec3f(0.0);
    if (shape == EMITTER_SPHERE) {
        let y = 1.0 - 2.0 * u.x;
        let ring = sqrt(max(0.0, 1.0 - y * y));
        let phi = TAU * u.y;
        let r = radius * pow(u.z, 1.0 / 3.0);
        result = vec3f(r * ring * cos(phi), r * y, r * ring * sin(phi));
    } else if (shape == EMITTER_BOX) {
        result = (2.0 * u - 1.0) * extents;
    } else if (shape == EMITTER_DISC) {
        let r = radius * sqrt(u.x);
        let phi = TAU * u.y;
        result = vec3f(r * cos(phi), 0.0, r * sin(phi));
    }
    return result;
}

// A unit direction in the cone of cosine cos_spread around the unit direction n, uniform over
// the solid angle, in the frame of Duff et al., "Building an Orthonormal Basis, Revisited".
fn particle_cone(n: vec3f, cos_spread: f32, u: vec2f) -> vec3f {
    let cos_t = 1.0 - u.x * (1.0 - cos_spread);
    let sin_t = sqrt(max(0.0, 1.0 - cos_t * cos_t));
    let phi = TAU * u.y;
    let x = sin_t * cos(phi);
    let y = sin_t * sin(phi);
    let sign = select(-1.0, 1.0, n.z >= 0.0);
    let a = -1.0 / (sign + n.z);
    let b = n.x * n.y * a;
    let t = vec3f(1.0 + sign * n.x * n.x * a, sign * b, -sign * n.x);
    let s = vec3f(b, sign + n.y * n.y * a, -n.y);
    return select(t * x + s * y + n * cos_t, vec3f(0.0), all(n == vec3f(0.0)));
}

// The radial velocity of a particle: its radial speed along the world offset of its spawn
// point from the origin of the emitter, zero for an offset of zero.
fn particle_radial(offset: vec3f, speed: f32) -> vec3f {
    let len2 = dot(offset, offset);
    return select(vec3f(0.0), offset * (speed * inverseSqrt(len2)), len2 > 0.0);
}

// The wander field: a sum of sines of the position times the scale, offset by the phase,
// times the turbulence. No component depends on its own axis, so it has no divergence.
fn particle_wander(position: vec3f, phase: vec3f, scale: f32, turbulence: f32) -> vec3f {
    let q = position * scale + phase;
    return 0.5 * turbulence * vec3f(
        sin(q.y) + sin(1.618 * q.z),
        sin(q.z) + sin(1.618 * q.x),
        sin(q.x) + sin(1.618 * q.y));
}

// Advance a particle by a frame: v += (acceleration + wander(p)) * delta, then the exact drag
// v = wind + (v - wind) * exp(-drag * delta), then p += (v + drift) * delta. wind.w is the drag,
// turbulence.x the acceleration of the wander field and turbulence.y its scale. The drift
// moves the particle without entering its velocity.
fn particle_integrate(p: ptr<function, Particle>, acceleration: vec3f, wind: vec4f,
    turbulence: vec2f, phase: vec3f, drift: vec3f, delta: f32)
{
    let wander = particle_wander((*p).position, phase, turbulence.y, turbulence.x);
    let v = (*p).velocity + (acceleration + wander) * delta;
    (*p).velocity = wind.xyz + (v - wind.xyz) * exp(-wind.w * delta);
    (*p).position += ((*p).velocity + drift) * delta;
}

// Collide a particle with the ground plane of height ground.x after its step: a particle
// below it is lifted onto it, keeps ground.y of its speed into it, reversed, and loses ground.z
// of its speed along it, or dies. Returns whether the particle lives on.
fn particle_collide(p: ptr<function, Particle>, ground: vec3f, mode: u32) -> bool {
    if (mode == GROUND_NONE || (*p).position.y >= ground.x) {
        return true;
    }

    if (mode == GROUND_DIE) {
        return false;
    }

    (*p).position.y = ground.x;
    if ((*p).velocity.y < 0.0) {
        (*p).velocity.y = -(*p).velocity.y * ground.y;
    }

    (*p).velocity.x *= 1.0 - ground.z;
    (*p).velocity.z *= 1.0 - ground.z;
    return true;
}

// The age of a particle over its life, 0 to 1.
fn particle_norm_age(age: f32, life: f32) -> f32 {
    return clamp(select(0.0, age / life, life > 0.0), 0.0, 1.0);
}

// The sample a normalized age falls after in a row of the curve table, and in y how far it is
// toward the next.
fn particle_curve_sample(t: f32) -> vec2f {
    let s = clamp(t, 0.0, 1.0) * f32(CURVE_SAMPLES - 1u);
    let i = min(floor(s), f32(CURVE_SAMPLES - 2u));
    return vec2f(i, s - i);
}

// A curve at a normalized age from its row of the curve table, linear between two samples; a
// constant curve reads no row.
fn particle_curve(row: u32, value: f32, t: f32) -> f32 {
    if (row == CURVE_CONSTANT) {
        return value;
    }

    let at = particle_curve_sample(t);
    let k = row * CURVE_SAMPLES + u32(at.x);
    let a = curves[k];
    return a + (curves[k + 1u] - a) * at.y;
}

// Area under a curve from age 0 up to a normalized age, from the accumulated area in the row
// after its values: area[i] + f dt (v[i] + f (v[i + 1] - v[i]) / 2), exact for the samples.
fn particle_curve_area(row: u32, value: f32, t: f32) -> f32 {
    if (row == CURVE_CONSTANT) {
        return value * clamp(t, 0.0, 1.0);
    }

    let at = particle_curve_sample(t);
    let k = row * CURVE_SAMPLES + u32(at.x);
    let v = curves[k];
    let dt = 1.0 / f32(CURVE_SAMPLES - 1u);
    return curves[k + CURVE_SAMPLES] + at.y * dt * (v + 0.5 * at.y * (curves[k + 1u] - v));
}

// Degrees a particle has turned by its spin: the area under the spin curve up to its age, and
// for a constant curve the spin times its value times the age.
fn particle_turned(spin: f32, row: u32, value: f32, age: f32, life: f32) -> f32 {
    if (row == CURVE_CONSTANT) {
        return spin * value * age;
    }

    return spin * life * particle_curve_area(row, value, particle_norm_age(age, life));
}

// Rotation of a particle in degrees: spawned, plus the turns of the rotation curve, plus spin.
fn particle_rotation(rotation: f32, spin: f32, rotation_row: u32, rotation_value: f32,
    spin_row: u32, spin_value: f32, age: f32, life: f32) -> f32
{
    let t = particle_norm_age(age, life);
    return rotation + 360.0 * particle_curve(rotation_row, rotation_value, t) +
        particle_turned(spin, spin_row, spin_value, age, life);
}

// A gradient at a normalized age from its rows of the curve table, a linear color of four
// floats per sample; a constant gradient reads no row.
fn particle_gradient(row: u32, value: vec3f, t: f32) -> vec3f {
    if (row == CURVE_CONSTANT) {
        return value;
    }

    let at = particle_curve_sample(t);
    let k = (row * CURVE_SAMPLES + u32(at.x) * 4u);
    let a = vec3f(curves[k], curves[k + 1u], curves[k + 2u]);
    let b = vec3f(curves[k + 4u], curves[k + 5u], curves[k + 6u]);
    return mix(a, b, at.y);
}

// Frame of a sprite sheet a particle shows, row by row from the top left.
fn particle_frame(columns: u32, rows: u32, fps: f32, age: f32, life: f32) -> u32 {
    let frames = max(columns, 1u) * max(rows, 1u);
    if (fps > 0.0) {
        return u32(floor(max(age, 0.0) * fps)) % frames;
    }

    return min(u32(floor(particle_norm_age(age, life) * f32(frames))), frames - 1u);
}

// Rectangle of a frame in texture coordinates: xy the top left corner, zw the size.
fn particle_frame_rect(frame: u32, columns: u32, rows: u32) -> vec4f {
    let c = max(columns, 1u);
    let r = max(rows, 1u);
    let size = vec2f(1.0 / f32(c), 1.0 / f32(r));
    return vec4f(f32(frame % c) * size.x, f32((frame / c) % r) * size.y, size);
}

// Fade of a soft particle a view depth in front of the geometry behind it.
fn particle_soft(scene: f32, depth: f32, softness: f32) -> f32 {
    if (softness <= 0.0) {
        return 1.0;
    }

    return clamp((scene - depth) / softness, 0.0, 1.0);
}

// View depth of a reversed depth buffer value from P[2][2], P[3][2], P[2][3] and P[3][3].
fn particle_view_depth(p: vec4f, d: f32) -> f32 {
    let den = d * p.z - p.x;
    if (den == 0.0) {
        return 3.4e38;
    }

    return -(p.y - d * p.w) / den;
}

// Length factor of a sprite stretched along its velocity across the screen.
fn particle_stretch(stretch: f32, speed: f32) -> f32 {
    return 1.0 + max(stretch, 0.0) * max(speed, 0.0);
}

// Sort key of a live particle; ascending keys draw back to front.
fn particle_sort_key(depth: f32) -> u32 {
    let bits = bitcast<u32>(depth);
    let ordered = bits ^ select(0x80000000u, 0xFFFFFFFFu, (bits >> 31u) != 0u);
    return min(~ordered, SORT_DEAD - 1u);
}
