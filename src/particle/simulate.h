/**
 * @file particle/simulate.h
 * @brief C twin of the particle simulation of etc/shaders/particle_sim.wgsl and
 * etc/shaders/include/particle.wgsl: the hash, the range draws, the spawn shapes, the step and
 * the changes of a particle over its life.
 *
 * Every function here has a WGSL twin with the same name minus the box_ prefix; a change to
 * one is a change to both. Lifetimes are whole microseconds, so the spawn accounting of the
 * extract and the integrate pass agree on the frame every particle dies in.
 */

#ifndef BOX_PARTICLE_SIMULATE_H
#define BOX_PARTICLE_SIMULATE_H

#include <box.h>
#include <math.h>
#include <string.h>

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

/** First of the three hash streams of the color of a particle, one per channel */
#define BOX_PARTICLE_STREAM_COLOR (15u)

/** Hash stream of the phase of the wander field of an emitter, drawn for indices 0 to 2 */
#define BOX_PARTICLE_STREAM_WANDER (18u)

/** Hash stream of the radial speed of a particle */
#define BOX_PARTICLE_STREAM_RADIAL (19u)

/** Ground collision mode of an emitter without a ground plane */
#define BOX_PARTICLE_GROUND_NONE (0u)

/** Ground collision mode of particles that bounce off the ground plane */
#define BOX_PARTICLE_GROUND_BOUNCE (1u)

/** Ground collision mode of particles that die on the ground plane */
#define BOX_PARTICLE_GROUND_DIE (2u)

/** Longest lifetime in microseconds, so a lifetime and a frame delta fit in 32 bits */
#define BOX_PARTICLE_LIFE_MAX_US (4000000000u)

/** Sort key of a free slot, after every live particle */
#define BOX_PARTICLE_SORT_DEAD (0xFFFFFFFFu)

/** Period of each axis of the argument of the wander field: sin(q) and sin(1.618 q) both repeat
 * after 1000 pi, up to 1e-4 radians for the float 1.618 */
#define BOX_PARTICLE_WANDER_PERIOD (3141.592653589793)

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

/** The forces of a frame of the step, as the wind, turbulence, wander and drift members of
 * Emitter in particle.wgsl */
typedef struct box_particle_forces_t {
    float acceleration[3];  /**< Emitter acceleration plus gravity, world units per second
                             *   squared */
    float wind[3];          /**< Velocity the drag pulls toward */
    float drag;             /**< Rate of exponential decay of the difference with the wind */
    float turbulence;       /**< Acceleration of the wander field */
    float scale;            /**< Frequency multiplier of the wander field */
    float phase[3];         /**< Offset of the wander field, from the seed and the frame */
    float drift[3];         /**< Velocity of the frame of reference, added to the position
                             *   only */
} box_particle_forces_t;

/** The ground plane of an emitter, as the ground and collide members of Emitter */
typedef struct box_particle_ground_t {
    uint32_t mode;          /**< BOX_PARTICLE_GROUND_NONE, _BOUNCE or _DIE */
    float height;           /**< World height of the plane */
    float bounce;           /**< Share of the speed into the plane a hit keeps, 0 to 1 */
    float friction;         /**< Share of the speed along the plane a hit loses, 0 to 1 */
} box_particle_ground_t;

/** What a particle draws from its streams, as the spawn and the draw shaders draw it */
typedef struct box_particle_sample_t {
    float direction[3];     /**< Unit direction in emitter space, zero for none */
    float speed;            /**< Initial speed */
    float radial;           /**< Initial speed away from the origin of the emitter */
    uint32_t life;          /**< Lifetime in microseconds */
    float size;             /**< Diameter */
    float rotation;         /**< Initial rotation in degrees */
    float spin;             /**< Spin in degrees per second */
    float opacity;          /**< Opacity */
    float color[3];         /**< Linear color */
} box_particle_sample_t;

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

/** Draw one stream of a particle from a range.
 *
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param stream The stream of the value
 * @param min Smallest value
 * @param max Largest value, a max not above min hands out min
 * @return The value
 */
static inline float box_particle_draw(
    uint32_t seed,
    uint32_t index,
    uint32_t stream,
    float min,
    float max)
{
    return box_particle_range(min, max,
        box_particle_unit(box_particle_hash(seed, index, stream)));
}

/** Decode a display encoded channel to linear.
 *
 * @param c The channel, 0 to 1
 * @return The linear value
 */
static inline float box_particle_decode(
    float c)
{
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Draw the color of a particle, each channel from its own stream around the color.
 *
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param color Display encoded color, 0 to 1 per channel
 * @param variance Display encoded amount per channel the color is drawn around color
 * @param out Receives the linear color, of the display encoded draw clamped to 0 to 1
 */
static inline void box_particle_color(
    uint32_t seed,
    uint32_t index,
    const float *color,
    const float *variance,
    float *out)
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        float u = box_particle_unit(
            box_particle_hash(seed, index, BOX_PARTICLE_STREAM_COLOR + (uint32_t)c));
        float v = color[c] + (2.0f * u - 1.0f) * variance[c];
        out[c] = box_particle_decode(fminf(fmaxf(v, 0.0f), 1.0f));
    }
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

/** Get the radial velocity of a particle: its radial speed along the offset of its spawn point
 * from the origin of the emitter.
 *
 * @param offset World offset of the spawn point from the origin of the emitter, the spawn
 *               position in emitter space through the rotation and scale of the emitter
 * @param speed The radial speed
 * @param out Receives the velocity, zero for an offset of zero
 */
static inline void box_particle_radial(
    const float *offset,
    float speed,
    float *out)
{
    float len2 = offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
    float k = len2 > 0.0f ? speed / sqrtf(len2) : 0.0f;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = offset[i] * k;
    }
}

/** Draw the phase of the wander field of an emitter from its seed, which the extract sends in
 * the block, so it has no WGSL twin.
 *
 * @param seed The seed of the emitter
 * @param out Receives the phase, 0 to 64 per axis
 */
static inline void box_particle_phase(
    uint32_t seed,
    float *out)
{
    uint32_t c;
    for (c = 0; c < 3; c ++) {
        out[c] = 64.0f * box_particle_unit(
            box_particle_hash(seed, c, BOX_PARTICLE_STREAM_WANDER));
    }
}

/** Offset the phase of the wander field of an emitter by the displacement of its frame of
 * reference, so the field at a world position is the one at the position minus the offset.
 *
 * The shift wraps at BOX_PARTICLE_WANDER_PERIOD, so the phase stays small however far the frame
 * moved. The extract sends the phase in the block, so it has no WGSL twin.
 *
 * @param phase The phase from the seed
 * @param offset World displacement of the frame of reference
 * @param scale Frequency multiplier of the field
 * @param out Receives the phase
 */
static inline void box_particle_frame_phase(
    const float *phase,
    const double *offset,
    float scale,
    float *out)
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        out[c] = (float)((double)phase[c] -
            fmod(offset[c] * (double)scale, BOX_PARTICLE_WANDER_PERIOD));
    }
}

/** Evaluate the wander field: a sum of sines of the position times the scale, offset by the
 * phase, times the turbulence. No component depends on its own axis, so the field has no
 * divergence, and each lies in -turbulence to turbulence.
 *
 * @param position World position
 * @param phase Offset of the field
 * @param scale Frequency multiplier
 * @param turbulence Acceleration of the field
 * @param out Receives the acceleration
 */
static inline void box_particle_wander(
    const float *position,
    const float *phase,
    float scale,
    float turbulence,
    float *out)
{
    float q[3];
    int32_t c;
    for (c = 0; c < 3; c ++) {
        q[c] = position[c] * scale + phase[c];
    }

    out[0] = 0.5f * turbulence * (sinf(q[1]) + sinf(1.618f * q[2]));
    out[1] = 0.5f * turbulence * (sinf(q[2]) + sinf(1.618f * q[0]));
    out[2] = 0.5f * turbulence * (sinf(q[0]) + sinf(1.618f * q[1]));
}

/** Advance a particle by a frame: v += (acceleration + wander(p)) * delta, then the exact drag
 * v = wind + (v - wind) * exp(-drag * delta), then p += (v + drift) * delta. The drift moves
 * the particle without entering its velocity.
 *
 * @param position World position, advanced in place
 * @param velocity World velocity, advanced in place
 * @param forces The forces of the frame
 * @param delta Seconds of the frame
 */
static inline void box_particle_integrate(
    float *position,
    float *velocity,
    const box_particle_forces_t *forces,
    float delta)
{
    float wander[3], keep = expf(-forces->drag * delta);
    int32_t i;
    box_particle_wander(position, forces->phase, forces->scale, forces->turbulence, wander);
    for (i = 0; i < 3; i ++) {
        velocity[i] += (forces->acceleration[i] + wander[i]) * delta;
        velocity[i] = forces->wind[i] + (velocity[i] - forces->wind[i]) * keep;
        position[i] += (velocity[i] + forces->drift[i]) * delta;
    }
}

/** Collide a particle with the ground plane after its step: a particle below the plane is
 * lifted onto it, keeps bounce of its speed into the plane, reversed, and loses friction of
 * its speed along it, or dies.
 *
 * @param position World position, lifted in place
 * @param velocity World velocity, changed in place
 * @param ground The ground plane
 * @return Whether the particle lives on
 */
static inline bool box_particle_collide(
    float *position,
    float *velocity,
    const box_particle_ground_t *ground)
{
    bool result = true;
    if (ground->mode == BOX_PARTICLE_GROUND_NONE || position[1] >= ground->height) {
        goto done;
    }

    if (ground->mode == BOX_PARTICLE_GROUND_DIE) {
        result = false;
        goto done;
    }

    position[1] = ground->height;
    if (velocity[1] < 0.0f) {
        velocity[1] = -velocity[1] * ground->bounce;
    }

    velocity[0] *= 1.0f - ground->friction;
    velocity[2] *= 1.0f - ground->friction;
done:
    return result;
}

/** Draw the properties of a particle of an emitter as the spawn and the draw shaders do, each
 * from its own stream.
 *
 * @param emitter The emitter
 * @param index The spawn index of the particle
 * @param out Receives the draws
 */
void box_particle_sample(
    const ParticleEmitter *emitter,
    uint32_t index,
    box_particle_sample_t *out);

/** Decode an 8 bit display encoded channel to linear.
 *
 * @param c The channel
 * @return The linear value, 0 to 1
 */
static inline float box_particle_linear(
    uint8_t c)
{
    return box_particle_decode((float)c / 255.0f);
}

/** Get the normalized age of a particle.
 *
 * @param age Seconds the particle lived
 * @param life Seconds it lives
 * @return The age over the life, 0 to 1, 0 for a life of 0
 */
static inline float box_particle_norm_age(
    float age,
    float life)
{
    float t = life > 0.0f ? age / life : 0.0f;

    return fminf(fmaxf(t, 0.0f), 1.0f);
}

/** Evaluate a ParticleCurve at a normalized age, linear between its four keys.
 *
 * @param keys Values at the ages 0, 1/3, 2/3 and 1
 * @param t The normalized age, clamped to 0 to 1
 * @return The value
 */
static inline float box_particle_curve(
    const float *keys,
    float t)
{
    float s = fminf(fmaxf(t, 0.0f), 1.0f) * 3.0f;
    float i = fminf(floorf(s), 2.0f);
    int32_t k = (int32_t)i;

    return keys[k] + (keys[k + 1] - keys[k]) * (s - i);
}

/** Get the area under a ParticleCurve from age 0 up to a normalized age.
 *
 * @param keys Values at the ages 0, 1/3, 2/3 and 1
 * @param t The normalized age, clamped to 0 to 1
 * @return The area, in normalized age times value
 */
static inline float box_particle_curve_area(
    const float *keys,
    float t)
{
    float s = fminf(fmaxf(t, 0.0f), 1.0f) * 3.0f, sum = 0.0f;
    int32_t k;
    for (k = 0; k < 3; k ++) {
        float f = fminf(fmaxf(s - (float)k, 0.0f), 1.0f);
        sum += f * keys[k] + 0.5f * f * f * (keys[k + 1] - keys[k]);
    }

    return sum / 3.0f;
}

/** Get the degrees a particle has turned by its spin, the area under its spin curve up to its
 * age; a constant curve turns it by its spin times the key times its age.
 *
 * @param spin Spin the particle spawned with, degrees per second
 * @param keys Keys of the spin curve
 * @param age Seconds the particle lived
 * @param life Seconds it lives
 * @return The degrees
 */
static inline float box_particle_turned(
    float spin,
    const float *keys,
    float age,
    float life)
{
    float result;
    if (keys[0] == keys[1] && keys[1] == keys[2] && keys[2] == keys[3]) {
        result = spin * keys[0] * age;
    } else {
        result = spin * life * box_particle_curve_area(keys, box_particle_norm_age(age, life));
    }

    return result;
}

/** Get the rotation of a particle in degrees: the rotation it spawned with, the turns of the
 * rotation curve and what its spin turned it by.
 *
 * @param rotation Rotation it spawned with in degrees
 * @param spin Spin it spawned with in degrees per second
 * @param rotation_keys Keys of the rotation curve, in turns
 * @param spin_keys Keys of the spin curve
 * @param age Seconds the particle lived
 * @param life Seconds it lives
 * @return The degrees
 */
static inline float box_particle_rotation(
    float rotation,
    float spin,
    const float *rotation_keys,
    const float *spin_keys,
    float age,
    float life)
{
    float t = box_particle_norm_age(age, life);

    return rotation + 360.0f * box_particle_curve(rotation_keys, t) +
        box_particle_turned(spin, spin_keys, age, life);
}

/** Evaluate a ParticleGradient at a normalized age, linear between its four linear colors.
 *
 * @param keys Linear colors at the ages 0, 1/3, 2/3 and 1, four floats each, w unused
 * @param t The normalized age, clamped to 0 to 1
 * @param out Receives the linear color
 */
static inline void box_particle_gradient(
    const float *keys,
    float t,
    float *out)
{
    float s = fminf(fmaxf(t, 0.0f), 1.0f) * 3.0f;
    float i = fminf(floorf(s), 2.0f);
    int32_t k = (int32_t)i * 4, c;
    for (c = 0; c < 3; c ++) {
        out[c] = keys[k + c] + (keys[k + 4 + c] - keys[k + c]) * (s - i);
    }
}

/** Get the frame of a sprite sheet a particle shows.
 *
 * @param columns Frames across the sheet, 0 and 1 for one
 * @param rows Frames down the sheet, 0 and 1 for one
 * @param fps Frames a second, looping, 0 to play the sheet once over the life
 * @param age Seconds the particle lived
 * @param life Seconds it lives
 * @return The frame, row by row from the top left, 0 to columns times rows minus 1
 */
static inline uint32_t box_particle_frame(
    uint32_t columns,
    uint32_t rows,
    float fps,
    float age,
    float life)
{
    uint32_t frames = (columns > 1 ? columns : 1) * (rows > 1 ? rows : 1);
    float f = fps > 0.0f
        ? floorf(fmaxf(age, 0.0f) * fps)
        : floorf(box_particle_norm_age(age, life) * (float)frames);
    uint32_t result = (uint32_t)f;

    return fps > 0.0f ? result % frames : (result < frames ? result : frames - 1);
}

/** Get the rectangle of a frame of a sprite sheet in texture coordinates.
 *
 * @param frame The frame, row by row from the top left
 * @param columns Frames across the sheet, 0 and 1 for one
 * @param rows Frames down the sheet, 0 and 1 for one
 * @param out Receives u and v of the top left corner, then the width and height
 */
static inline void box_particle_frame_rect(
    uint32_t frame,
    uint32_t columns,
    uint32_t rows,
    float *out)
{
    uint32_t c = columns > 1 ? columns : 1, r = rows > 1 ? rows : 1;
    out[2] = 1.0f / (float)c;
    out[3] = 1.0f / (float)r;
    out[0] = (float)(frame % c) * out[2];
    out[1] = (float)((frame / c) % r) * out[3];
}

/** Get the fade of a soft particle in front of the geometry behind it.
 *
 * @param scene View depth of the geometry behind the fragment
 * @param depth View depth of the fragment of the particle
 * @param softness World units the particle fades over, 0 or less for no fade
 * @return The opacity factor, 0 at the geometry to 1 a softness or more in front of it
 */
static inline float box_particle_soft(
    float scene,
    float depth,
    float softness)
{
    float result = 1.0f;
    if (softness > 0.0f) {
        result = fminf(fmaxf((scene - depth) / softness, 0.0f), 1.0f);
    }

    return result;
}

/** Get the view depth of a depth buffer value from the depth rows of a projection.
 *
 * @param p The projection, z and w rows of its z and w columns: P[2][2], P[3][2], P[2][3] and
 *          P[3][3] in column major order
 * @param d The depth buffer value, reversed: 1 at the near plane
 * @return The distance in front of the camera, infinite for a value with no point
 */
static inline float box_particle_view_depth(
    const float *p,
    float d)
{
    float den = d * p[2] - p[0];
    float result = INFINITY;
    if (den != 0.0f) {
        result = -(p[1] - d * p[3]) / den;
    }

    return result;
}

/** Get the length factor of a stretched sprite.
 *
 * @param stretch Length added per unit of speed, 0 or less for none
 * @param speed Speed across the screen in world units per second
 * @return The factor its length along the velocity is scaled by, at least 1
 */
static inline float box_particle_stretch(
    float stretch,
    float speed)
{
    return 1.0f + fmaxf(stretch, 0.0f) * fmaxf(speed, 0.0f);
}

/** Get the sort key of a live particle: ascending keys draw back to front.
 *
 * @param depth View depth of the particle
 * @return The key, below BOX_PARTICLE_SORT_DEAD
 */
static inline uint32_t box_particle_sort_key(
    float depth)
{
    uint32_t bits;
    memcpy(&bits, &depth, sizeof(bits));
    bits ^= (bits >> 31) ? 0xFFFFFFFFu : 0x80000000u;
    bits = ~bits;

    return bits == BOX_PARTICLE_SORT_DEAD ? BOX_PARTICLE_SORT_DEAD - 1u : bits;
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
