/**
 * @file particle/simulate.h
 * @brief C twin of the particle simulation of etc/shaders/particle_sim.wgsl and
 * etc/shaders/include/particle.wgsl: the hash, the range draws, the spawn shapes and the step.
 *
 * Every function here has a WGSL twin with the same name minus the box_ prefix; a change to
 * one is a change to both. Lifetimes are whole microseconds, so the spawn accounting of the
 * extract and the integrate pass agree on the frame every particle dies in.
 */

#ifndef BOX_PARTICLE_SIMULATE_H
#define BOX_PARTICLE_SIMULATE_H

#include <box.h>
#include <math.h>

/** Hash stream of the lifetime of a particle */
#define BOX_PARTICLE_STREAM_LIFE (0u)

/** Hash stream of the speed of a particle */
#define BOX_PARTICLE_STREAM_SPEED (1u)

/** Hash stream of the size of a particle */
#define BOX_PARTICLE_STREAM_SIZE (2u)

/** Hash stream of the initial rotation of a particle */
#define BOX_PARTICLE_STREAM_ROTATION (3u)

/** Hash stream of the spin of a particle */
#define BOX_PARTICLE_STREAM_SPIN (4u)

/** Hash stream of the opacity of a particle */
#define BOX_PARTICLE_STREAM_OPACITY (5u)

/** First of the three hash streams of the spawn position of a particle */
#define BOX_PARTICLE_STREAM_SHAPE (6u)

/** First of the two hash streams of the direction of a particle in its cone */
#define BOX_PARTICLE_STREAM_DIRECTION (9u)

/** Hash stream of the mesh table point a particle spawns at */
#define BOX_PARTICLE_STREAM_POINT (11u)

/** Hash stream of the triangle of a mesh table point */
#define BOX_PARTICLE_STREAM_TRIANGLE (12u)

/** First of the two hash streams of the barycentric coordinates of a mesh table point */
#define BOX_PARTICLE_STREAM_BARY (13u)

/** Longest lifetime in microseconds, so a lifetime and a frame delta fit in 32 bits */
#define BOX_PARTICLE_LIFE_MAX_US (4000000000u)

/** Points of the table an EmitterMesh spawns from */
#define BOX_PARTICLE_MESH_POINTS (4096)

/** A sampled point of the surface of a mesh, as TablePoint in particle.wgsl */
typedef struct box_particle_point_t {
    float position[4];      /**< Position in the space of the source shape, w unused */
    float normal[4];        /**< Unit normal of the surface, w unused */
} box_particle_point_t;

/** A particle as the pools hold it, as Particle in particle.wgsl.
 *
 * Size, rotation, spin and opacity are drawn again from the seed and the index when the
 * particle is drawn, so a slot holds only what moves.
 */
typedef struct box_particle_t {
    float position[3];      /**< World position */
    uint32_t remaining;     /**< Microseconds left to live, 0 for a free slot */
    float velocity[3];      /**< World velocity */
    uint32_t index;         /**< Spawn index of the particle in its emitter */
} box_particle_t;

/** Hash a word, the PCG hash of Jarzynski and Olano, "Hash Functions for GPU Rendering".
 *
 * @param v The word
 * @return The hash
 */
static inline uint32_t box_particle_pcg(
    uint32_t v)
{
    uint32_t state = v * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;

    return (word >> 22u) ^ word;
}

/** Hash the draw of one stream of one particle of an emitter.
 *
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param stream The stream, one per value drawn
 * @return The hash
 */
static inline uint32_t box_particle_hash(
    uint32_t seed,
    uint32_t index,
    uint32_t stream)
{
    return box_particle_pcg(box_particle_pcg(seed + box_particle_pcg(stream)) + index);
}

/** Turn a hash into a number in [0, 1), from its upper 24 bits so the float is exact.
 *
 * @param h The hash
 * @return The number
 */
static inline float box_particle_unit(
    uint32_t h)
{
    return (float)(h >> 8u) * (1.0f / 16777216.0f);
}

/** Draw from a range with a number in [0, 1).
 *
 * @param min Smallest value
 * @param max Largest value, a max not above min hands out min
 * @param u The number
 * @return The value
 */
static inline float box_particle_range(
    float min,
    float max,
    float u)
{
    return max > min ? min + (max - min) * u : min;
}

/** Convert seconds to whole microseconds, clamped to 0 to BOX_PARTICLE_LIFE_MAX_US.
 *
 * @param seconds The seconds
 * @return The microseconds, 0 for a value that is not a positive number
 */
static inline uint32_t box_particle_us(
    double seconds)
{
    double us = seconds * 1e6;
    uint32_t result = 0;
    if (us >= (double)BOX_PARTICLE_LIFE_MAX_US) {
        result = BOX_PARTICLE_LIFE_MAX_US;
    } else if (us > 0) {
        result = (uint32_t)(us + 0.5);
    }

    return result;
}

/** Draw the lifetime of a particle in whole microseconds, integers only so the GPU and the
 * spawn accounting agree.
 *
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param min Shortest lifetime in microseconds
 * @param span Microseconds the longest lifetime is above min, 0 for min
 * @return The lifetime, min to min + span
 */
static inline uint32_t box_particle_life(
    uint32_t seed,
    uint32_t index,
    uint32_t min,
    uint32_t span)
{
    uint32_t h = box_particle_hash(seed, index, BOX_PARTICLE_STREAM_LIFE);

    return span ? min + h % (span + 1u) : min;
}

/** Age a particle by a frame.
 *
 * @param remaining Microseconds the particle has left, 0 when it is dead
 * @param delta Microseconds of the frame
 * @return Microseconds it has left after the frame, 0 once its age reached its lifetime
 */
static inline uint32_t box_particle_age(
    uint32_t remaining,
    uint32_t delta)
{
    return remaining > delta ? remaining - delta : 0u;
}

/** Draw a spawn position inside a shape, in emitter space.
 *
 * @param shape The EmitterShape, EmitterMesh spawns at the origin here
 * @param radius Radius of a sphere or disc
 * @param extents Half size of a box
 * @param u Three numbers in [0, 1)
 * @param out Receives the position
 */
static inline void box_particle_shape(
    uint32_t shape,
    float radius,
    const float *extents,
    const float *u,
    float *out)
{
    out[0] = out[1] = out[2] = 0.0f;
    if (shape == EmitterSphere) {
        float y = 1.0f - 2.0f * u[0];
        float ring = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float phi = 6.28318530718f * u[1];
        float r = radius * cbrtf(u[2]);
        out[0] = r * ring * cosf(phi);
        out[1] = r * y;
        out[2] = r * ring * sinf(phi);
    } else if (shape == EmitterBox) {
        out[0] = (2.0f * u[0] - 1.0f) * extents[0];
        out[1] = (2.0f * u[1] - 1.0f) * extents[1];
        out[2] = (2.0f * u[2] - 1.0f) * extents[2];
    } else if (shape == EmitterDisc) {
        float r = radius * sqrtf(u[0]);
        float phi = 6.28318530718f * u[1];
        out[0] = r * cosf(phi);
        out[2] = r * sinf(phi);
    }
}

/** Draw a direction in the cone around a unit direction, uniform over the solid angle.
 *
 * @param n The unit direction, or zero for none
 * @param cos_spread Cosine of the half angle of the cone
 * @param u Two numbers in [0, 1)
 * @param out Receives the unit direction, zero when n is zero
 */
static inline void box_particle_cone(
    const float *n,
    float cos_spread,
    const float *u,
    float *out)
{
    float cos_t = 1.0f - u[0] * (1.0f - cos_spread);
    float sin_t = sqrtf(fmaxf(0.0f, 1.0f - cos_t * cos_t));
    float phi = 6.28318530718f * u[1];
    float x = sin_t * cosf(phi), y = sin_t * sinf(phi);
    float sign = n[2] >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign + n[2]);
    float b = n[0] * n[1] * a;
    float t[3] = { 1.0f + sign * n[0] * n[0] * a, sign * b, -sign * n[0] };
    float s[3] = { b, sign + n[1] * n[1] * a, -n[1] };
    int32_t i;
    if (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f) {
        out[0] = out[1] = out[2] = 0.0f;
    } else {
        for (i = 0; i < 3; i ++) {
            out[i] = t[i] * x + s[i] * y + n[i] * cos_t;
        }
    }
}

/** Advance a particle by a frame with semi implicit Euler.
 *
 * @param position World position, advanced in place
 * @param velocity World velocity, advanced in place
 * @param acceleration Acceleration in world units per second squared
 * @param delta Seconds of the frame
 */
static inline void box_particle_integrate(
    float *position,
    float *velocity,
    const float *acceleration,
    float delta)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        velocity[i] += acceleration[i] * delta;
        position[i] += velocity[i] * delta;
    }
}

/** Sample points over the surface of a mesh, area weighted, as the table of an EmitterMesh.
 *
 * Point j lies on the triangle its BOX_PARTICLE_STREAM_TRIANGLE draw picks by area, at the
 * barycentric coordinates of its two BOX_PARTICLE_STREAM_BARY draws. A mesh without area
 * gives no points.
 *
 * @param mesh The mesh, in unit shape space
 * @param scale Scale of the shape the mesh is drawn with
 * @param seed Seed of the emitter
 * @param out Receives the points, count of them
 * @param count Points to sample
 * @return Points written, count or 0
 */
int32_t box_particle_mesh_table(
    const box_mesh_t *mesh,
    const float *scale,
    uint32_t seed,
    box_particle_point_t *out,
    int32_t count);

#endif
