// Particle emitters: the parameter block of an emitter for one frame, the particle a pool slot
// holds and the seeded draws, shared by the simulation and the draw. Every function has a C
// twin with a box_ prefix in src/particle/simulate.h; a change to one is a change to both.
// Lifetimes are whole microseconds, so the spawn accounting of the CPU and the integrate pass
// agree on the frame each particle dies in.
//
// Curves are rows of the curve table, which a shader that includes this file declares as
// curves: array<f32>, BOX_PARTICLE_CURVE_SAMPLES floats per row, baked by the extract.
#pragma once

// Rows of the curves of a ParticleMotion, box_particle_motion_rows_t: two vector curves of four
// floats per sample, x, y and z of the velocity and w the speed, and x, y and z of the force and
// w the drag. rows is the first row of the velocity, of its max, of the force and of its max,
// CURVE_CONSTANT for a constant vector or one without a max.
struct MotionRows {
    rows: vec4u,
    // Velocity xyz and speed w while constant
    velocity: vec4f,
    // Force xyz and drag w while constant
    force: vec4f,
};

// The curves of ParticleMotion of a particle at its age, box_particle_motion_t.
struct Motion {
    velocity: vec3f,
    speed: f32,
    force: vec3f,
    drag: f32,
};

// box_render_particle_block_t: ranges are (min, max) pairs, a max not above min hands out min.
struct Emitter {
    model: mat4x4f,
    // xyz: unit direction in emitter space or zero, w: cosine of the spread
    direction: vec4f,
    // xyz: half size of a box, w: radius of a sphere or disc
    extents: vec4f,
    // xyz: world acceleration plus gravity, w: seconds of the frame
    acceleration: vec4f,
    // rgb: display encoded color, w: X of the world offset added to the live particles before
    // they move, the move of the render origin since their last frame for a world space pool
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
    // w: first point of the mesh table in the table buffer
    pool: vec4u,
    // x: microseconds of the frame, y: shortest lifetime, z: microseconds the longest is above,
    // w: slot of the card words of a ParticleCard pool from the start of the pool, 0 for others
    life: vec4u,
    // xyz: world velocity the drag pulls toward, w: drag rate
    wind: vec4f,
    // x: acceleration of the wander field, y: its frequency scale, z: uniform world scale of
    // the emitter, the multiplier of the velocity and force of its ParticleMotion, w: 1 over the
    // gust scale of the Wind with ParticleForces.global_wind, 0 when the Wind pulls nothing
    turbulence: vec4f,
    // xyz: phase of the wander field drawn from the seed, offset by the displacement of the
    // frame of reference, w: wrapped world X offset of the gust field of the global wind
    wander: vec4f,
    // rgb: display encoded color variance, w: Y of the shift
    variance: vec4f,
    // xyz: world velocity every spawn of the frame gets, w: wrapped world Z offset of the gust
    // field of the global wind
    inherit: vec4f,
    // x: height of the ground plane, y: bounce, z: friction, w: Z of the shift
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
    // xy: world X and Z velocity of the Wind times ParticleForces.global_wind and the world
    // scale at a gust factor of 1, z: its gust; 0 without global wind
    global_wind: vec3f,
    // 1 when the emitter has a ParticleAppearance
    styled: u32,
    // Rows of the max curves of the size, opacity, rotation and spin, which each particle blends
    // toward by its own draw, CURVE_CONSTANT for a curve without max_keys; the area of the max
    // spin is in the row after it
    curve_max: vec4u,
    // x: first of the GRADIENT_ROWS rows of the max gradient, CURVE_CONSTANT for none, y: 1
    // when the emitter has a ParticleMotion that changes its motion, z: first slot of the pool
    // in the particle arena, w: 1 when its particles live in the space of model
    gradient_max: vec4u,
    // Rows of the curves of the ParticleMotion, read only while gradient_max.y is 1
    motion: MotionRows,
    sampled: vec4u,
    origin: vec4i,
};

const SAMPLED_SPAWN_GROUND: u32 = 1u;
const SAMPLED_COLLIDE_TERRAIN: u32 = 2u;

// A pool slot, box_particle_t: remaining is 0 for a free slot. Position and velocity are in the
// world, or in the space of model for a local emitter.
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
const PARTICLE_CARD: u32 = 2u;

// ParticleBlend
const PARTICLE_ALPHA: u32 = 0u;
const PARTICLE_ADDITIVE: u32 = 1u;

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
const STREAM_CURVE: u32 = 20u;
const STREAM_MOTION: u32 = 25u;
const STREAM_CARD: u32 = 29u;

// Curves of ParticleCurves, the offsets of their blend streams from STREAM_CURVE,
// BOX_PARTICLE_CURVE_*
const CURVE_SIZE: u32 = 0u;
const CURVE_OPACITY: u32 = 1u;
const CURVE_ROTATION: u32 = 2u;
const CURVE_SPIN: u32 = 3u;
const CURVE_COLOR: u32 = 4u;

// Quantities of ParticleMotion, the offsets of their blend streams from STREAM_MOTION,
// BOX_PARTICLE_MOTION_*
const MOTION_VELOCITY: u32 = 0u;
const MOTION_SPEED: u32 = 1u;
const MOTION_FORCE: u32 = 2u;
const MOTION_DRAG: u32 = 3u;

// Ground collision modes, BOX_PARTICLE_GROUND_*
const GROUND_NONE: u32 = 0u;
const GROUND_BOUNCE: u32 = 1u;
const GROUND_DIE: u32 = 2u;

// Card words a pool slot holds, BOX_PARTICLE_CARD_WORDS
const CARD_WORDS: u32 = 8u;

// Resting cards, BOX_PARTICLE_CARD_REST_SPEED, _HEIGHT and _RATE
const CARD_REST_SPEED: f32 = 0.5;
const CARD_REST_HEIGHT: f32 = 0.01;
const CARD_REST_RATE: f32 = 8.0;

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

// The blend of a particle between a curve and its max curve, the same for its whole life.
fn particle_blend(seed: u32, index: u32, curve: u32) -> f32 {
    return particle_unit(particle_hash(seed, index, STREAM_CURVE + curve));
}

// The blend of a particle for a curve whose max curve is at max_row: 0 for a curve without a max
// curve, which does not blend, so that an emitter without max curves hashes nothing for them.
fn particle_curve_blend(seed: u32, index: u32, curve: u32, max_row: u32) -> f32 {
    if (max_row == CURVE_CONSTANT) {
        return 0.0;
    }

    return particle_blend(seed, index, curve);
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

// Advance a particle by a frame: v += (acceleration + wander(p) + force) * delta, then the exact
// drag v = wind + (v - wind) * exp(-drag * motion drag * delta), then
// p += (speed * v + velocity + drift) * delta, with the force, drag, speed and velocity of the
// motion curves. wind.w is the drag, turbulence.x the acceleration of the wander field and
// turbulence.y its scale. The drift and the velocity curve move the particle without entering
// its velocity; particle_motion_none() leaves every value as without them.
fn particle_integrate(p: ptr<function, Particle>, acceleration: vec3f, wind: vec4f,
    turbulence: vec2f, phase: vec3f, drift: vec3f, motion: Motion, delta: f32)
{
    let wander = particle_wander((*p).position, phase, turbulence.y, turbulence.x);
    let v = (*p).velocity + (acceleration + wander + motion.force) * delta;
    (*p).velocity = wind.xyz + (v - wind.xyz) * exp(-wind.w * max(motion.drag, 0.0) * delta);
    (*p).position += ((*p).velocity * motion.speed + motion.velocity + drift) * delta;
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

// The forces of a frame of the step of a particle of a local emitter in the space of the
// emitter, as the members of Emitter: xyz of the acceleration, wind with the drag in w,
// turbulence and drift.
struct LocalForces {
    acceleration: vec3f,
    wind: vec4f,
    turbulence: vec2f,
    drift: vec3f,
};

// The inverse of the 3x3 of the model of an emitter, which takes world directions into the
// space of a local emitter: the cross products of its columns over its determinant, zero for a
// model without volume.
fn particle_inverse(model: mat4x4f) -> mat3x3f {
    let a = model[0].xyz;
    let b = model[1].xyz;
    let c = model[2].xyz;
    let rows = mat3x3f(
        vec3f(b.y * c.z - b.z * c.y, b.z * c.x - b.x * c.z, b.x * c.y - b.y * c.x),
        vec3f(c.y * a.z - c.z * a.y, c.z * a.x - c.x * a.z, c.x * a.y - c.y * a.x),
        vec3f(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x));
    let det = a.x * rows[0].x + a.y * rows[0].y + a.z * rows[0].z;
    let k = select(0.0, 1.0 / det, det != 0.0);
    return transpose(rows) * k;
}

// Prepare the step of a particle of a local emitter: add the wander field at its world position,
// taken into the space of the emitter, to its velocity, and turn the world acceleration, wind,
// drift and the velocity and force of the motion into that space, so particle_integrate with
// the results moves it as a world particle under world forces. The forces keep no turbulence,
// as the wander field is in the velocity already.
fn particle_local(p: ptr<function, Particle>, model: mat4x4f, inverse: mat3x3f,
    forces: ptr<function, LocalForces>, motion: ptr<function, Motion>, phase: vec3f, delta: f32)
{
    let world = (model * vec4f((*p).position, 1.0)).xyz;
    let wander = inverse * particle_wander(world, phase, (*forces).turbulence.y,
        (*forces).turbulence.x);
    (*p).velocity += wander * delta;
    (*forces).acceleration = inverse * (*forces).acceleration;
    (*forces).wind = vec4f(inverse * (*forces).wind.xyz, (*forces).wind.w);
    (*forces).drift = inverse * (*forces).drift;
    (*forces).turbulence.x = 0.0;
    (*motion).velocity = inverse * (*motion).velocity;
    (*motion).force = inverse * (*motion).force;
}

// Collide a particle of a local emitter with the world ground plane after its step, as
// particle_collide does at its world position and velocity, which a hit takes back into the
// space of the emitter. Returns whether the particle lives on.
fn particle_collide_local(p: ptr<function, Particle>, model: mat4x4f, inverse: mat3x3f,
    ground: vec3f, mode: u32) -> bool
{
    if (mode == GROUND_NONE) {
        return true;
    }

    var w = *p;
    w.position = (model * vec4f((*p).position, 1.0)).xyz;
    if (w.position.y >= ground.x) {
        return true;
    }

    w.velocity = (model * vec4f((*p).velocity, 0.0)).xyz;
    let result = particle_collide(&w, ground, mode);
    (*p).position = inverse * (w.position - model[3].xyz);
    (*p).velocity = inverse * w.velocity;
    return result;
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

// The curve of a particle at a normalized age: the curve, or with a max curve at max_row its
// blend toward it by the draw u of the particle. Without a max curve it reads one row at most.
fn particle_curve_range(row: u32, max_row: u32, value: f32, t: f32, u: f32) -> f32 {
    let a = particle_curve(row, value, t);
    if (max_row == CURVE_CONSTANT) {
        return a;
    }

    return a + (particle_curve(max_row, value, t) - a) * u;
}

// Area under the curve of a particle up to a normalized age: with a max curve the two areas
// blended by the draw u of the particle, the area under the blended curve.
fn particle_curve_area_range(row: u32, max_row: u32, value: f32, t: f32, u: f32) -> f32 {
    let a = particle_curve_area(row, value, t);
    if (max_row == CURVE_CONSTANT) {
        return a;
    }

    return a + (particle_curve_area(max_row, value, t) - a) * u;
}

// The curves of an emitter without ParticleMotion, which change nothing.
fn particle_motion_none() -> Motion {
    return Motion(vec3f(0.0), 1.0, vec3f(0.0), 1.0);
}

// The normalized age of a particle from its lifetime and the microseconds it has left, as the
// draw computes it.
fn particle_life_age(life_us: u32, remaining: u32) -> f32 {
    let age = f32(life_us - remaining) * 1e-6;
    return particle_norm_age(age, f32(life_us) * 1e-6);
}

// The blend of a particle between the curves of a quantity of ParticleMotion and their max
// curves, the same for its whole life.
fn particle_motion_blend(seed: u32, index: u32, which: u32) -> f32 {
    return particle_unit(particle_hash(seed, index, STREAM_MOTION + which));
}

// A vector curve at a normalized age from its rows of the curve table, four floats per sample,
// linear between two samples; a constant vector reads no row.
fn particle_vector(row: u32, value: vec4f, t: f32) -> vec4f {
    if (row == CURVE_CONSTANT) {
        return value;
    }

    let at = particle_curve_sample(t);
    let k = row * CURVE_SAMPLES + u32(at.x) * 4u;
    let a = vec4f(curves[k], curves[k + 1u], curves[k + 2u], curves[k + 3u]);
    let b = vec4f(curves[k + 4u], curves[k + 5u], curves[k + 6u], curves[k + 7u]);
    return a + (b - a) * at.y;
}

// The vector curve of a particle at a normalized age: the vector, or with a max vector at
// max_row each value blended toward that of the max by its own blend u of the particle.
fn particle_vector_range(row: u32, max_row: u32, value: vec4f, t: f32, u: vec4f) -> vec4f {
    let a = particle_vector(row, value, t);
    if (max_row == CURVE_CONSTANT) {
        return a;
    }

    return a + (particle_vector(max_row, value, t) - a) * u;
}

// The velocity and speed curves of ParticleMotion of a particle at a normalized age, the three
// axes of the velocity blended by one draw and the speed by another, its force 0 and drag 1.
fn particle_motion_velocity(m: MotionRows, seed: u32, index: u32, t: f32) -> Motion {
    let uv = particle_motion_blend(seed, index, MOTION_VELOCITY);
    let u = vec4f(uv, uv, uv, particle_motion_blend(seed, index, MOTION_SPEED));
    let v = particle_vector_range(m.rows.x, m.rows.y, m.velocity, t, u);
    return Motion(v.xyz, v.w, vec3f(0.0), 1.0);
}

// The curves of ParticleMotion of a particle at a normalized age, each blended toward its max
// curve by the draw of its quantity.
fn particle_motion(m: MotionRows, seed: u32, index: u32, t: f32) -> Motion {
    let uf = particle_motion_blend(seed, index, MOTION_FORCE);
    let u = vec4f(uf, uf, uf, particle_motion_blend(seed, index, MOTION_DRAG));
    let f = particle_vector_range(m.rows.z, m.rows.w, m.force, t, u);
    var out = particle_motion_velocity(m, seed, index, t);
    out.force = f.xyz;
    out.drag = f.w;
    return out;
}

// The velocity a particle moves with but for the drift, which a stretched sprite stretches
// along: speed times its stored velocity plus the velocity curve.
fn particle_moving(velocity: vec3f, motion: Motion) -> vec3f {
    return velocity * motion.speed + motion.velocity;
}

// The curves of ParticleMotion of a particle under the uniform world scale of its emitter,
// turbulence z of the block: the velocity and force times it, the speed and drag as they are.
fn particle_motion_scaled(motion: Motion, scale: f32) -> Motion {
    return Motion(motion.velocity * scale, motion.speed, motion.force * scale, motion.drag);
}

// Degrees a particle has turned by its spin: the area under the spin curve up to its age, and
// for a constant curve the spin times its value times the age.
fn particle_turned(spin: f32, row: u32, max_row: u32, value: f32, age: f32, life: f32,
    u: f32) -> f32
{
    if (row == CURVE_CONSTANT) {
        return spin * value * age;
    }

    return spin * life *
        particle_curve_area_range(row, max_row, value, particle_norm_age(age, life), u);
}

// Rotation of a particle in degrees: spawned, plus the turns of the rotation curve, plus spin.
fn particle_rotation(rotation: f32, spin: f32, rotation_row: u32, rotation_max: u32,
    rotation_value: f32, rotation_u: f32, spin_row: u32, spin_max: u32, spin_value: f32,
    spin_u: f32, age: f32, life: f32) -> f32
{
    let t = particle_norm_age(age, life);
    return rotation +
        360.0 * particle_curve_range(rotation_row, rotation_max, rotation_value, t, rotation_u) +
        particle_turned(spin, spin_row, spin_max, spin_value, age, life, spin_u);
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

// The gradient of a particle at a normalized age: the gradient, or with a max gradient at
// max_row its blend toward it in linear light by the draw u of the particle.
fn particle_gradient_range(row: u32, max_row: u32, value: vec3f, t: f32, u: f32) -> vec3f {
    let a = particle_gradient(row, value, t);
    if (max_row == CURVE_CONSTANT) {
        return a;
    }

    return mix(a, particle_gradient(max_row, value, t), u);
}

// Size or opacity of a particle: the value it spawned with times its curve, clamped at 0 as a
// curve that overshoots its keys may go below it.
fn particle_scaled(base: f32, row: u32, max_row: u32, value: f32, t: f32, u: f32) -> f32 {
    return max(base * particle_curve_range(row, max_row, value, t, u), 0.0);
}

// Color of a particle: the linear color it spawned with times its gradient, clamped at 0.
fn particle_tinted(color: vec3f, row: u32, max_row: u32, value: vec3f, t: f32, u: f32) -> vec3f {
    return max(color * particle_gradient_range(row, max_row, value, t, u), vec3f(0.0));
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


// The card word of the particle in slot i of a ParticleCard pool whose slots start at base and
// whose card words start cards slots after them, word i % CARD_WORDS of the slot
// base + cards + i / CARD_WORDS: 0 while the card is in the air, else 1 plus its age in
// microseconds when it came to rest.
fn particle_card_word(base: u32, cards: u32, i: u32) -> u32 {
    let k = base + cards + i / CARD_WORDS;
    switch (i % CARD_WORDS) {
        case 0u: { return bitcast<u32>(particles[k].position.x); }
        case 1u: { return bitcast<u32>(particles[k].position.y); }
        case 2u: { return bitcast<u32>(particles[k].position.z); }
        case 3u: { return particles[k].remaining; }
        case 4u: { return bitcast<u32>(particles[k].velocity.x); }
        case 5u: { return bitcast<u32>(particles[k].velocity.y); }
        case 6u: { return bitcast<u32>(particles[k].velocity.z); }
        default: { return particles[k].index; }
    }
}

// Product a b of two quaternions, b applied first.
fn particle_quat_mul(a: vec4f, b: vec4f) -> vec4f {
    return vec4f(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

// A vector rotated by a unit quaternion.
fn particle_quat_rotate(q: vec4f, v: vec3f) -> vec3f {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Initial orientation of a card, uniform over the rotations by Shoemake's method from the first
// three of its STREAM_CARD streams.
fn particle_card_orientation(seed: u32, index: u32) -> vec4f {
    let u0 = particle_unit(particle_hash(seed, index, STREAM_CARD));
    let u1 = particle_unit(particle_hash(seed, index, STREAM_CARD + 1u));
    let u2 = particle_unit(particle_hash(seed, index, STREAM_CARD + 2u));
    let a = sqrt(1.0 - u0);
    let b = sqrt(u0);
    let p = TAU * u1;
    let r = TAU * u2;
    return vec4f(a * sin(p), a * cos(p), b * sin(r), b * cos(r));
}

// World axis a card turns around, uniform over the sphere, from the last two of its STREAM_CARD
// streams.
fn particle_card_axis(seed: u32, index: u32) -> vec3f {
    let u0 = particle_unit(particle_hash(seed, index, STREAM_CARD + 3u));
    let u1 = particle_unit(particle_hash(seed, index, STREAM_CARD + 4u));
    let y = 1.0 - 2.0 * u0;
    let ring = sqrt(max(0.0, 1.0 - y * y));
    let phi = TAU * u1;
    return vec3f(ring * cos(phi), y, ring * sin(phi));
}

// An orientation turned around a unit world axis by an angle in radians.
fn particle_card_turn(q: vec4f, axis: vec3f, radians: f32) -> vec4f {
    return particle_quat_mul(vec4f(axis * sin(0.5 * radians), cos(0.5 * radians)), q);
}

// How far a card at rest has eased toward lying flat, seconds after it came to rest.
fn particle_card_ease(seconds: f32) -> f32 {
    return 1.0 - exp(-CARD_REST_RATE * max(seconds, 0.0));
}

// A card eased toward lying flat: its normal, the z axis of the card, turned by a share of the
// angle to the vertical on the side it faces, around the horizontal axis between the two.
fn particle_card_flat(q: vec4f, ease: f32) -> vec4f {
    let n = particle_quat_rotate(q, vec3f(0.0, 0.0, 1.0));
    let up = select(-1.0, 1.0, n.y >= 0.0);
    let len = sqrt(n.x * n.x + n.z * n.z);
    if (len < 1e-6) {
        return q;
    }

    let axis = vec3f(-n.z * up / len, 0.0, n.x * up / len);
    return particle_card_turn(q, axis, atan2(len, abs(n.y)) * ease);
}

// Orientation of a card: its initial orientation turned around its axis by radians while it is
// in the air, a rested below 0, else turned by landed and eased toward flat over rested seconds.
fn particle_card(seed: u32, index: u32, radians: f32, landed: f32, rested: f32) -> vec4f {
    let q = particle_card_orientation(seed, index);
    let axis = particle_card_axis(seed, index);
    if (rested < 0.0) {
        return particle_card_turn(q, axis, radians);
    }

    return particle_card_flat(particle_card_turn(q, axis, landed), particle_card_ease(rested));
}

// Whether a card in the air comes to rest after its step: on the ground plane within
// CARD_REST_HEIGHT of it and slower than CARD_REST_SPEED, both times the world scale.
fn particle_card_rests(mode: u32, y: f32, speed: f32, height: f32, scale: f32) -> bool {
    return mode == GROUND_BOUNCE && y - height <= CARD_REST_HEIGHT * scale &&
        speed < CARD_REST_SPEED * scale;
}
