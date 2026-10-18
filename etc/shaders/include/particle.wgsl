// Particle emitters: the parameter block of an emitter for one frame, the particle a pool slot
// holds and the seeded draws, shared by the simulation and the draw. Every function has a C
// twin with a box_ prefix in src/particle/simulate.h; a change to one is a change to both.
// Lifetimes are whole microseconds, so the spawn accounting of the CPU and the integrate pass
// agree on the frame each particle dies in.
#pragma once

// box_render_particle_block_t: ranges are (min, max) pairs, a max not above min hands out min.
struct Emitter {
    // World transform of the emitter
    model: mat4x4f,
    // xyz: unit direction in emitter space or zero, w: cosine of the spread
    direction: vec4f,
    // xyz: half size of a box, w: radius of a sphere or disc
    extents: vec4f,
    // xyz: world acceleration, w: seconds of the frame
    acceleration: vec4f,
    // rgb: linear color
    color: vec4f,
    // xy: speed range, zw: size range
    speed_size: vec4f,
    // xy: initial rotation range in degrees, zw: spin range in degrees per second
    rotation_spin: vec4f,
    // xy: opacity range
    opacity: vec4f,
    // x: EmitterShape, y: seed, z: particles to spawn, w: spawn index of the first of them
    spawn: vec4u,
    // x: used slots, y: first spawn slot in the slot buffer, z: mesh table points,
    // w: index of the draw arguments
    pool: vec4u,
    // x: microseconds of the frame, y: shortest lifetime, z: microseconds the longest is above
    life: vec4u,
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

// Slot of a spawn with a lifetime of 0, which takes no slot
const NO_SLOT: u32 = 0xFFFFFFFFu;

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
