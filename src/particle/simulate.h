/**
 * @file particle/simulate.h
 * @brief The simulation of one particle in C, the twin of etc/shaders/particle_sim.wgsl.
 *
 * The particles live in renderer buffers and are moved by a compute pass, so
 * nothing here runs in a frame. The functions below are the C twin of the ones
 * the shader is built from: the hash the random draws come from, the range a
 * property is drawn between, the cone a particle leaves in, the step the
 * integrate pass takes and the answer of a particle that hits a surface. A
 * change to one of them is a change to both places, and the tests check these
 * against the motion they are supposed to produce.
 *
 * The step is a semi implicit Euler step with the drag solved exactly: forces
 * are added to the velocity, the drag pulls that velocity towards the wind over
 * exp(-drag * delta), which is the closed form of the differential equation and
 * is stable at any timestep, and the position follows the new velocity.
 */

#ifndef BOX_PARTICLE_SIMULATE_H
#define BOX_PARTICLE_SIMULATE_H

#include <box/math.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

/** Streams of one particle, which is how far apart two draws of it are hashed */
#define BOX_PARTICLE_STREAMS (16)

/** Stream the color of a particle is drawn from */
#define BOX_PARTICLE_STREAM_COLOR (11)

/** Waves per world unit of the turbulence field, which sets how large its cells are */
#define BOX_PARTICLE_TURBULENCE_SCALE (0.35f)

/** Emitters the turbulence field is offset apart by, so two of them do not share it */
#define BOX_PARTICLE_TURBULENCE_SEEDS (1024)

/** Forces one step of the simulation moves a particle by */
typedef struct box_particle_forces_t {
    vec3 acceleration; /**< Constant acceleration of the emitter and its gravity */
    vec3 wind;         /**< Velocity the drag pulls the particle towards */
    float drag;        /**< Fraction of the difference with the wind lost per second */
} box_particle_forces_t;

/** Surface a particle ran into, as the integrate pass resolves it */
typedef struct box_particle_hit_t {
    vec3 normal;  /**< Unit normal of the surface, pointing away from it */
    float depth;  /**< How far the particle sits behind the surface */
} box_particle_hit_t;

/** Scramble a word into an unrelated one.
 *
 * @param value The word
 * @return The hashed word
 */
static inline uint32_t box_particle_hash(
    uint32_t value)
{
    uint32_t x = value;
    x = x ^ (x >> 16);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16);

    return x;
}

/** One random draw of one property of one particle.
 *
 * The draw only depends on the seed of the emitter, the index of the particle
 * in the stream of the emitter and which property is asked for, so it holds
 * whatever slot the particle lands in and whatever device runs the pass.
 *
 * @param seed Seed of the emitter
 * @param particle Index of the particle in the stream of the emitter
 * @param stream Property that is drawn, below BOX_PARTICLE_STREAMS
 * @return A value from 0 up to but not including 1
 */
static inline float box_particle_random(
    uint32_t seed,
    uint32_t particle,
    uint32_t stream)
{
    uint32_t h = box_particle_hash(
        seed ^ box_particle_hash(particle * BOX_PARTICLE_STREAMS + stream));

    return (float)h * (1.0f / 4294967296.0f);
}

/** Value a range hands out for a draw.
 *
 * @param min Lowest value of the range
 * @param max Highest value of the range
 * @param t The draw, from 0 up to 1
 * @return The value, which is min for a range that does not rise
 */
static inline float box_particle_between(
    float min,
    float max,
    float t)
{
    float span = max - min;
    if (span < 0.0f) {
        span = 0.0f;
    }

    return min + span * t;
}

/** Unit direction inside the cone of a spread around an axis.
 *
 * The draw is uniform over the cap the cosine of the spread cuts out of the
 * sphere, so a wide cone does not crowd its rim.
 *
 * @param axis Unit axis of the cone
 * @param cos_spread Cosine of the half angle of the cone
 * @param u Draw that picks the ring, from 0 up to 1
 * @param v Draw that picks the angle around the axis, from 0 up to 1
 * @return The direction
 */
static inline vec3 box_particle_cone(
    vec3 axis,
    float cos_spread,
    float u,
    float v)
{
    float c = cos_spread;
    if (c < -1.0f) {
        c = -1.0f;
    }

    if (c > 1.0f) {
        c = 1.0f;
    }

    c = c + (1.0f - c) * u;
    float s = sqrtf(fmaxf(1.0f - c * c, 0.0f));
    float phi = v * 6.2831853f;
    vec3 up = vec3_new(0, 1, 0);
    if (fabsf(axis.y) > 0.99f) {
        up = vec3_new(1, 0, 0);
    }

    vec3 t = vec3_normalize(vec3_cross(up, axis));
    vec3 b = vec3_cross(axis, t);
    vec3 result = vec3_add(vec3_add(vec3_scale(t, cosf(phi) * s),
        vec3_scale(b, sinf(phi) * s)), vec3_scale(axis, c));

    return vec3_normalize(result);
}

/** Turbulence the field of an emitter gives a particle at a point.
 *
 * The field is three sines of the point, which is smooth, costs three
 * instructions and does not need a table: a particle that passes through the
 * same place is pushed the same way, so the wander of a stream is as
 * reproducible as the rest of the simulation. The seed offsets the field, so
 * two emitters in the same place do not wander alike.
 *
 * @param position Position of the particle in world space
 * @param seed Seed of the emitter
 * @param amount Amplitude of the field in world units per second squared
 * @return The acceleration of the wander
 */
static inline vec3 box_particle_wander(
    vec3 position,
    uint32_t seed,
    float amount)
{
    vec3 result = vec3_new(0, 0, 0);
    if (amount > 0.0f) {
        float offset = (float)(seed % BOX_PARTICLE_TURBULENCE_SEEDS) * 0.37f;
        float x = position.x * BOX_PARTICLE_TURBULENCE_SCALE + offset;
        float y = position.y * BOX_PARTICLE_TURBULENCE_SCALE + offset;
        float z = position.z * BOX_PARTICLE_TURBULENCE_SCALE + offset;
        result = vec3_new(sinf(y * 1.3f + z * 0.7f), sinf(z * 1.1f + x * 0.9f),
            sinf(x * 1.7f + y * 0.5f));
        result = vec3_scale(result, amount);
    }

    return result;
}

/** Velocity of a particle after one step of the forces.
 *
 * @param velocity Velocity at the start of the step
 * @param f The forces
 * @param wander Turbulence of this step, already scaled by its amplitude
 * @param delta Seconds the step advances
 * @return The velocity at the end of the step
 */
static inline vec3 box_particle_accelerate(
    vec3 velocity,
    const box_particle_forces_t *f,
    vec3 wander,
    float delta)
{
    vec3 result = vec3_add(velocity,
        vec3_scale(vec3_add(f->acceleration, wander), delta));
    if (f->drag > 0.0f) {
        float keep = expf(-f->drag * delta);
        result = vec3_add(f->wind, vec3_scale(vec3_sub(result, f->wind), keep));
    }

    return result;
}

/** Move a particle one step of the simulation.
 *
 * @param f The forces
 * @param wander Turbulence of this step, already scaled by its amplitude
 * @param delta Seconds the step advances
 * @param position The position, moved by the step
 * @param velocity The velocity, changed by the forces
 */
static inline void box_particle_step(
    const box_particle_forces_t *f,
    vec3 wander,
    float delta,
    vec3 *position,
    vec3 *velocity)
{
    velocity[0] = box_particle_accelerate(velocity[0], f, wander, delta);
    position[0] = vec3_add(position[0], vec3_scale(velocity[0], delta));
}

/** Answer of a particle that ended its step behind a surface.
 *
 * The particle is lifted back onto the surface, the speed it had into the
 * surface is turned around and scaled by the bounce and the speed it had along
 * the surface is scaled down by the friction. A particle that is already moving
 * away keeps its direction, which is what stops a particle resting on a surface
 * from being thrown off it.
 *
 * @param hit The surface
 * @param bounce Fraction of the speed into the surface that is kept
 * @param friction Fraction of the speed along the surface that is lost
 * @param position The position, lifted onto the surface
 * @param velocity The velocity, turned around by the surface
 */
static inline void box_particle_bounce(
    const box_particle_hit_t *hit,
    float bounce,
    float friction,
    vec3 *position,
    vec3 *velocity)
{
    position[0] = vec3_add(position[0], vec3_scale(hit->normal, hit->depth));
    float into = vec3_dot(velocity[0], hit->normal);
    vec3 along = vec3_sub(velocity[0], vec3_scale(hit->normal, into));
    if (into < 0.0f) {
        into = -into * bounce;
    }

    velocity[0] = vec3_add(vec3_scale(along, 1.0f - friction),
        vec3_scale(hit->normal, into));
}

/** Test a particle against the horizontal ground plane.
 *
 * @param position Position of the particle
 * @param height Height of the plane
 * @param hit Receives the surface when the particle is under the plane
 * @return true when the particle is under the plane
 */
static inline bool box_particle_ground(
    vec3 position,
    float height,
    box_particle_hit_t *hit)
{
    bool result = position.y < height;
    if (result) {
        hit->normal = vec3_new(0, 1, 0);
        hit->depth = height - position.y;
    }

    return result;
}

/** Test a particle against a sphere collider.
 *
 * @param position Position of the particle
 * @param center Center of the sphere
 * @param radius Radius of the sphere
 * @param hit Receives the surface when the particle is inside the sphere
 * @return true when the particle is inside the sphere
 */
static inline bool box_particle_sphere(
    vec3 position,
    vec3 center,
    float radius,
    box_particle_hit_t *hit)
{
    vec3 d = vec3_sub(position, center);
    float distance = vec3_length(d);
    bool result = distance < radius;
    if (result) {
        hit->normal = distance > 1e-6f ? vec3_scale(d, 1.0f / distance) :
            vec3_new(0, 1, 0);
        hit->depth = radius - distance;
    }

    return result;
}

/** Test a particle against an axis aligned box collider.
 *
 * The particle leaves over the face it sits closest to, which is the shortest
 * way out of the box.
 *
 * @param position Position of the particle
 * @param center Center of the box
 * @param half Half extents of the box
 * @param hit Receives the surface when the particle is inside the box
 * @return true when the particle is inside the box
 */
static inline bool box_particle_box(
    vec3 position,
    vec3 center,
    vec3 half,
    box_particle_hit_t *hit)
{
    vec3 d = vec3_sub(position, center);
    float out_x = half.x - fabsf(d.x);
    float out_y = half.y - fabsf(d.y);
    float out_z = half.z - fabsf(d.z);
    bool result = out_x > 0.0f && out_y > 0.0f && out_z > 0.0f;
    if (result) {
        hit->normal = vec3_new(d.x < 0.0f ? -1.0f : 1.0f, 0, 0);
        hit->depth = out_x;
        if (out_y < hit->depth) {
            hit->normal = vec3_new(0, d.y < 0.0f ? -1.0f : 1.0f, 0);
            hit->depth = out_y;
        }

        if (out_z < hit->depth) {
            hit->normal = vec3_new(0, 0, d.z < 0.0f ? -1.0f : 1.0f);
            hit->depth = out_z;
        }
    }

    return result;
}

#endif
