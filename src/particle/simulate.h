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

/** First of the five hash streams of the blend of a particle between a curve of ParticleCurves
 * and its max_keys, one per field in the order of BOX_PARTICLE_CURVE_SIZE to _COLOR */
#define BOX_PARTICLE_STREAM_CURVE (20u)

/** Curve of the size, the offset of its stream from BOX_PARTICLE_STREAM_CURVE */
#define BOX_PARTICLE_CURVE_SIZE (0u)

/** Curve of the opacity */
#define BOX_PARTICLE_CURVE_OPACITY (1u)

/** Curve of the rotation */
#define BOX_PARTICLE_CURVE_ROTATION (2u)

/** Curve of the spin */
#define BOX_PARTICLE_CURVE_SPIN (3u)

/** Gradient of the color */
#define BOX_PARTICLE_CURVE_COLOR (4u)

/** First of the four hash streams of the blend of a particle between a curve of ParticleMotion
 * and its max_keys, one per quantity in the order of BOX_PARTICLE_MOTION_VELOCITY to _DRAG: the
 * three axes of the velocity share one, as do those of the force */
#define BOX_PARTICLE_STREAM_MOTION (25u)

/** Blend of the velocity curves, the offset of its stream from BOX_PARTICLE_STREAM_MOTION */
#define BOX_PARTICLE_MOTION_VELOCITY (0u)

/** Blend of the speed curve */
#define BOX_PARTICLE_MOTION_SPEED (1u)

/** Blend of the force curves */
#define BOX_PARTICLE_MOTION_FORCE (2u)

/** Blend of the drag curve */
#define BOX_PARTICLE_MOTION_DRAG (3u)

/** Rows of the curve table a vector curve of ParticleMotion takes, four floats per sample: the
 * velocity x, y and z and the speed, or the force x, y and z and the drag */
#define BOX_PARTICLE_VECTOR_ROWS (4)

/** Ground collision mode of an emitter without a ground plane */
#define BOX_PARTICLE_GROUND_NONE (0u)

/** Ground collision mode of particles that bounce off the ground plane */
#define BOX_PARTICLE_GROUND_BOUNCE (1u)

/** Ground collision mode of particles that die on the ground plane */
#define BOX_PARTICLE_GROUND_DIE (2u)

/** Longest lifetime in microseconds, so a lifetime and a frame delta fit in 32 bits */
#define BOX_PARTICLE_LIFE_MAX_US (4000000000u)

/** Period of each axis of the argument of the wander field: sin(q) and sin(1.618 q) both repeat
 * after 1000 pi, up to 1e-4 radians for the float 1.618 */
#define BOX_PARTICLE_WANDER_PERIOD (3141.592653589793)

/** Points of the table an EmitterMesh spawns from */
#define BOX_PARTICLE_MESH_POINTS (4096)

/** Samples of a row of the curve table, from age 0 to 1: 63 intervals, so keys at 1/3 and 2/3,
 * as the float of 1.0f / 3.0f and 2.0f / 3.0f, fall on the samples 21 and 42 */
#define BOX_PARTICLE_CURVE_SAMPLES (64)

/** Row of a constant curve, which reads no row of the curve table */
#define BOX_PARTICLE_CURVE_CONSTANT (0xFFFFFFFFu)

/** Rows of the curve table a gradient takes, a linear color of four floats per sample */
#define BOX_PARTICLE_GRADIENT_ROWS (4)

/** Rows of the curve table a spin curve takes, its values and their accumulated area */
#define BOX_PARTICLE_SPIN_ROWS (2)

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

/** The rows of the curves of a ParticleMotion, as MotionRows in particle.wgsl. The velocity and
 * the speed are one vector curve of four floats per sample, the force and the drag another, so
 * the block of an emitter names two rows and their max rows */
typedef struct box_particle_motion_rows_t {
    uint32_t velocity_row;  /**< First of the BOX_PARTICLE_VECTOR_ROWS rows of the velocity and
                             *   the speed, BOX_PARTICLE_CURVE_CONSTANT while all four are
                             *   constant */
    uint32_t velocity_max_row; /**< First row of their max curves, BOX_PARTICLE_CURVE_CONSTANT
                             *   when they have none */
    uint32_t force_row;     /**< First row of the force and the drag, as velocity_row */
    uint32_t force_max_row; /**< First row of their max curves, as velocity_max_row */
    float velocity[4];      /**< Velocity xyz and speed w while constant */
    float force[4];         /**< Force xyz and drag w while constant */
} box_particle_motion_rows_t;

/** The curves of ParticleMotion of a particle at its age, as Motion in particle.wgsl */
typedef struct box_particle_motion_t {
    float velocity[3];      /**< World velocity added to the movement only */
    float speed;            /**< Multiplier of the stored velocity in the movement */
    float force[3];         /**< World acceleration added to the acceleration */
    float drag;             /**< Multiplier of the drag rate */
} box_particle_motion_t;

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

/** Get the blend of a particle between a curve and its max curve, the same for its whole life.
 *
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param curve The curve, BOX_PARTICLE_CURVE_SIZE to BOX_PARTICLE_CURVE_COLOR
 * @return The blend, 0 to 1
 */
static inline float box_particle_blend(
    uint32_t seed,
    uint32_t index,
    uint32_t curve)
{
    return box_particle_unit(box_particle_hash(seed, index, BOX_PARTICLE_STREAM_CURVE + curve));
}

/** Get the blend of a particle for a curve with a max curve: 0 for a curve without one, which
 * does not blend, so that an emitter without max curves hashes nothing for them.
 *
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param curve The curve, BOX_PARTICLE_CURVE_SIZE to BOX_PARTICLE_CURVE_COLOR
 * @param max_row Row of its max curve, BOX_PARTICLE_CURVE_CONSTANT for none
 * @return The blend, 0 to 1
 */
static inline float box_particle_curve_blend(
    uint32_t seed,
    uint32_t index,
    uint32_t curve,
    uint32_t max_row)
{
    float result = 0.0f;
    if (max_row != BOX_PARTICLE_CURVE_CONSTANT) {
        result = box_particle_blend(seed, index, curve);
    }

    return result;
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

/** Advance a particle by a frame: v += (acceleration + wander(p) + force) * delta, then the
 * exact drag v = wind + (v - wind) * exp(-drag * motion drag * delta), then
 * p += (speed * v + velocity + drift) * delta, with the curves of ParticleMotion.
 *
 * The drift and the velocity curve move the particle without entering its velocity.
 *
 * Without motion the curves are 0 and 1, which leave every value as without them, bit for bit.
 *
 * @param position World position, advanced in place
 * @param velocity World velocity, advanced in place
 * @param forces The forces of the frame
 * @param motion The curves of ParticleMotion at the age of the particle after the frame,
 *               box_particle_motion, NULL for none
 * @param delta Seconds of the frame
 */
static inline void box_particle_integrate(
    float *position,
    float *velocity,
    const box_particle_forces_t *forces,
    const box_particle_motion_t *motion,
    float delta)
{
    static const box_particle_motion_t none = {
        .velocity = {0, 0, 0}, .speed = 1.0f, .force = {0, 0, 0}, .drag = 1.0f
    };
    const box_particle_motion_t *m = motion ? motion : &none;
    float wander[3], keep = expf(-forces->drag * fmaxf(m->drag, 0.0f) * delta);
    int32_t i;
    box_particle_wander(position, forces->phase, forces->scale, forces->turbulence, wander);
    for (i = 0; i < 3; i ++) {
        velocity[i] += (forces->acceleration[i] + wander[i] + m->force[i]) * delta;
        velocity[i] = forces->wind[i] + (velocity[i] - forces->wind[i]) * keep;
        position[i] += (velocity[i] * m->speed + m->velocity[i] + forces->drift[i]) * delta;
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

/** Bake a ParticleCurve to a row of BOX_PARTICLE_CURVE_SAMPLES samples from age 0 to 1, as the
 * curve table holds it, by box_curve_bake: sample i is at the time i / 63, so a key at the float
 * of 1.0f / 3.0f or 2.0f / 3.0f lands exactly on the sample 21 or 42.
 *
 * The Curve views the keys of the ParticleCurve, so it allocates nothing.
 *
 * @param curve The curve, valid by box_particle_curve_check
 * @param fallback The value without keys, the default of its field
 * @param out Receives BOX_PARTICLE_CURVE_SAMPLES samples
 */
static inline void box_particle_curve_bake(
    const ParticleCurve *curve,
    float fallback,
    float *out)
{
    Curve view = { .kind = curve->kind, .keys = curve->keys };
    box_curve_bake(&view, 0.0f, 1.0f, BOX_PARTICLE_CURVE_SAMPLES, fallback, out);
}

/** Bake the accumulated area under a row of samples, the row the curve table holds after the
 * values of a spin curve: area[i] is the area under the sampled curve from age 0 to sample i,
 * exact for the curve linear between the samples.
 *
 * @param values BOX_PARTICLE_CURVE_SAMPLES samples from age 0 to 1
 * @param out Receives BOX_PARTICLE_CURVE_SAMPLES areas, in normalized age times value
 */
static inline void box_particle_curve_bake_area(
    const float *values,
    float *out)
{
    double dt = 1.0 / (double)(BOX_PARTICLE_CURVE_SAMPLES - 1), sum = 0.0;
    int32_t i;
    out[0] = 0.0f;
    for (i = 1; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
        sum += 0.5 * dt * ((double)values[i - 1] + (double)values[i]);
        out[i] = (float)sum;
    }
}

/** Bake a ParticleGradient to BOX_PARTICLE_GRADIENT_ROWS rows of the curve table, a linear color
 * of four floats per sample, w 1: each channel of the linear colors of the keys is a Curve of the
 * kind of the gradient with flat slopes, baked as a ParticleCurve; without keys it is white.
 *
 * @param gradient The gradient, valid by box_particle_gradient_check
 * @param out Receives BOX_PARTICLE_CURVE_SAMPLES colors of four floats
 */
static inline void box_particle_gradient_bake(
    const ParticleGradient *gradient,
    float *out)
{
    const ParticleGradientKey *keys = ecs_vec_first_t(&gradient->keys, ParticleGradientKey);
    int32_t c, i, count = ecs_vec_count(&gradient->keys);
    float row[BOX_PARTICLE_CURVE_SAMPLES];
    CurveKey local[BOX_PARTICLE_CURVE_SAMPLES];
    CurveKey *channel = count > BOX_PARTICLE_CURVE_SAMPLES
        ? ecs_os_malloc_n(CurveKey, count) : local;
    Curve curve = {
        .kind = gradient->kind,
        .keys = { .array = channel, .count = count, .size = count }
    };
#ifdef FLECS_SANITIZE
    curve.keys.elem_size = ECS_SIZEOF(CurveKey);
#endif
    for (c = 0; c < 3; c ++) {
        for (i = 0; i < count; i ++) {
            const rgb *color = &keys[i].color;
            uint8_t v = c == 0 ? color->r : (c == 1 ? color->g : color->b);
            channel[i] = (CurveKey){ .time = keys[i].time, .value = box_particle_linear(v) };
        }

        box_curve_bake(&curve, 0.0f, 1.0f, BOX_PARTICLE_CURVE_SAMPLES, 1.0f, row);
        for (i = 0; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
            out[i * 4 + c] = row[i];
        }
    }

    for (i = 0; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
        out[i * 4 + 3] = 1.0f;
    }

    if (channel != local) {
        ecs_os_free(channel);
    }
}

/** Get the sample a normalized age falls after in a row and how far it is toward the next.
 *
 * @param t The normalized age, clamped to 0 to 1
 * @param f Receives the fraction of the interval, 0 to 1
 * @return The sample, 0 to BOX_PARTICLE_CURVE_SAMPLES - 2
 */
static inline int32_t box_particle_curve_sample(
    float t,
    float *f)
{
    float s = fminf(fmaxf(t, 0.0f), 1.0f) * (float)(BOX_PARTICLE_CURVE_SAMPLES - 1);
    float i = fminf(floorf(s), (float)(BOX_PARTICLE_CURVE_SAMPLES - 2));
    *f = s - i;

    return (int32_t)i;
}

/** Evaluate a curve at a normalized age from its row in the curve table, linear between two
 * samples; a constant curve reads no row.
 *
 * @param table The curve table, BOX_PARTICLE_CURVE_SAMPLES floats per row
 * @param row Row of the curve, BOX_PARTICLE_CURVE_CONSTANT for a constant
 * @param value The value of a constant curve
 * @param t The normalized age, clamped to 0 to 1
 * @return The value
 */
static inline float box_particle_curve(
    const float *table,
    uint32_t row,
    float value,
    float t)
{
    float result = value, f;
    if (row != BOX_PARTICLE_CURVE_CONSTANT) {
        const float *v = table + (size_t)row * BOX_PARTICLE_CURVE_SAMPLES;
        int32_t k = box_particle_curve_sample(t, &f);
        result = v[k] + (v[k + 1] - v[k]) * f;
    }

    return result;
}

/** Get the area under a curve from age 0 up to a normalized age, from its row and the row of
 * accumulated area after it: area[i] + f dt (v[i] + f (v[i + 1] - v[i]) / 2), exact for the
 * sampled curve.
 *
 * @param table The curve table, BOX_PARTICLE_CURVE_SAMPLES floats per row
 * @param row Row of the values, the area in the row after it, BOX_PARTICLE_CURVE_CONSTANT for
 *        a constant
 * @param value The value of a constant curve
 * @param t The normalized age, clamped to 0 to 1
 * @return The area, in normalized age times value
 */
static inline float box_particle_curve_area(
    const float *table,
    uint32_t row,
    float value,
    float t)
{
    float result, f;
    if (row == BOX_PARTICLE_CURVE_CONSTANT) {
        result = value * fminf(fmaxf(t, 0.0f), 1.0f);
    } else {
        const float *v = table + (size_t)row * BOX_PARTICLE_CURVE_SAMPLES;
        const float *area = v + BOX_PARTICLE_CURVE_SAMPLES;
        float dt = 1.0f / (float)(BOX_PARTICLE_CURVE_SAMPLES - 1);
        int32_t k = box_particle_curve_sample(t, &f);
        result = area[k] + f * dt * (v[k] + 0.5f * f * (v[k + 1] - v[k]));
    }

    return result;
}

/** Evaluate the curve of a particle at a normalized age: the curve, or with a max curve its
 * blend toward it by the draw of the particle. A curve without a max curve reads one row at most.
 *
 * @param table The curve table
 * @param row Row of the curve, BOX_PARTICLE_CURVE_CONSTANT for a constant
 * @param max_row Row of the max curve, BOX_PARTICLE_CURVE_CONSTANT for none
 * @param value The value of a constant curve
 * @param t The normalized age, clamped to 0 to 1
 * @param u The blend of the particle, box_particle_blend
 * @return The value
 */
static inline float box_particle_curve_range(
    const float *table,
    uint32_t row,
    uint32_t max_row,
    float value,
    float t,
    float u)
{
    float result = box_particle_curve(table, row, value, t);
    if (max_row != BOX_PARTICLE_CURVE_CONSTANT) {
        float max = box_particle_curve(table, max_row, value, t);
        result = result + (max - result) * u;
    }

    return result;
}

/** Get the area under the curve of a particle from age 0 up to a normalized age: with a max
 * curve the areas of the two blended by the draw of the particle, which is the area under the
 * blended curve.
 *
 * @param table The curve table
 * @param row Row of the values, the area in the row after it, BOX_PARTICLE_CURVE_CONSTANT for
 *        a constant
 * @param max_row Row of the values of the max curve, its area in the row after it,
 *        BOX_PARTICLE_CURVE_CONSTANT for none
 * @param value The value of a constant curve
 * @param t The normalized age, clamped to 0 to 1
 * @param u The blend of the particle, box_particle_blend
 * @return The area, in normalized age times value
 */
static inline float box_particle_curve_area_range(
    const float *table,
    uint32_t row,
    uint32_t max_row,
    float value,
    float t,
    float u)
{
    float result = box_particle_curve_area(table, row, value, t);
    if (max_row != BOX_PARTICLE_CURVE_CONSTANT) {
        float max = box_particle_curve_area(table, max_row, value, t);
        result = result + (max - result) * u;
    }

    return result;
}

/** Get the normalized age of a particle from its lifetime and the microseconds it has left, as
 * the draw computes it, integers until the division.
 *
 * @param life_us Lifetime of the particle in microseconds, box_particle_life
 * @param remaining Microseconds it has left
 * @return The age over the life, 0 to 1
 */
static inline float box_particle_life_age(
    uint32_t life_us,
    uint32_t remaining)
{
    float age = (float)(life_us - remaining) * 1e-6f;

    return box_particle_norm_age(age, (float)life_us * 1e-6f);
}

/** Get the blend of a particle between the curves of a quantity of ParticleMotion and their max
 * curves, the same for its whole life.
 *
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param which BOX_PARTICLE_MOTION_VELOCITY to BOX_PARTICLE_MOTION_DRAG
 * @return The blend, 0 to 1
 */
static inline float box_particle_motion_blend(
    uint32_t seed,
    uint32_t index,
    uint32_t which)
{
    return box_particle_unit(box_particle_hash(seed, index, BOX_PARTICLE_STREAM_MOTION + which));
}

/** Evaluate a vector curve at a normalized age from its rows in the curve table, four floats per
 * sample, linear between two samples; a constant vector reads no row.
 *
 * @param table The curve table, BOX_PARTICLE_CURVE_SAMPLES floats per row
 * @param row First row of the vector, BOX_PARTICLE_CURVE_CONSTANT for a constant
 * @param value The four values of a constant vector
 * @param t The normalized age, clamped to 0 to 1
 * @param out Receives the four values
 */
static inline void box_particle_vector(
    const float *table,
    uint32_t row,
    const float *value,
    float t,
    float *out)
{
    float f;
    int32_t c;
    if (row == BOX_PARTICLE_CURVE_CONSTANT) {
        for (c = 0; c < 4; c ++) {
            out[c] = value[c];
        }
    } else {
        int32_t k = box_particle_curve_sample(t, &f);
        const float *v = table + (size_t)row * BOX_PARTICLE_CURVE_SAMPLES + k * 4;
        for (c = 0; c < 4; c ++) {
            out[c] = v[c] + (v[c + 4] - v[c]) * f;
        }
    }
}

/** Evaluate the vector curve of a particle at a normalized age: the vector, or with a max vector
 * each of its four values blended toward that of the max by its own blend of the particle.
 *
 * @param table The curve table
 * @param row First row of the vector, BOX_PARTICLE_CURVE_CONSTANT for a constant
 * @param max_row First row of the max vector, BOX_PARTICLE_CURVE_CONSTANT for none
 * @param value The four values of a constant vector
 * @param t The normalized age, clamped to 0 to 1
 * @param u The four blends of the particle
 * @param out Receives the four values
 */
static inline void box_particle_vector_range(
    const float *table,
    uint32_t row,
    uint32_t max_row,
    const float *value,
    float t,
    const float *u,
    float *out)
{
    float max[4];
    int32_t c;
    box_particle_vector(table, row, value, t, out);
    if (max_row != BOX_PARTICLE_CURVE_CONSTANT) {
        box_particle_vector(table, max_row, value, t, max);
        for (c = 0; c < 4; c ++) {
            out[c] = out[c] + (max[c] - out[c]) * u[c];
        }
    }
}

/** Evaluate the velocity and speed curves of ParticleMotion of a particle at a normalized age,
 * the three axes of the velocity blended by one draw and the speed by another.
 *
 * @param table The curve table
 * @param rows The rows of the curves
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param t The normalized age
 * @param out Receives the velocity and the speed, its force and drag untouched
 */
static inline void box_particle_motion_velocity(
    const float *table,
    const box_particle_motion_rows_t *rows,
    uint32_t seed,
    uint32_t index,
    float t,
    box_particle_motion_t *out)
{
    float uv = box_particle_motion_blend(seed, index, BOX_PARTICLE_MOTION_VELOCITY);
    float u[4] = {uv, uv, uv, box_particle_motion_blend(seed, index, BOX_PARTICLE_MOTION_SPEED)};
    float v[4];
    int32_t c;
    box_particle_vector_range(table, rows->velocity_row, rows->velocity_max_row, rows->velocity,
        t, u, v);
    for (c = 0; c < 3; c ++) {
        out->velocity[c] = v[c];
    }

    out->speed = v[3];
}

/** Evaluate the curves of ParticleMotion of a particle at a normalized age, each blended toward
 * its max curve by the draw of its quantity.
 *
 * @param table The curve table
 * @param rows The rows of the curves
 * @param seed The seed of the emitter
 * @param index The spawn index of the particle
 * @param t The normalized age, box_particle_life_age
 * @param out Receives the curves
 */
static inline void box_particle_motion(
    const float *table,
    const box_particle_motion_rows_t *rows,
    uint32_t seed,
    uint32_t index,
    float t,
    box_particle_motion_t *out)
{
    float uf = box_particle_motion_blend(seed, index, BOX_PARTICLE_MOTION_FORCE);
    float u[4] = {uf, uf, uf, box_particle_motion_blend(seed, index, BOX_PARTICLE_MOTION_DRAG)};
    float f[4];
    int32_t c;
    box_particle_motion_velocity(table, rows, seed, index, t, out);
    box_particle_vector_range(table, rows->force_row, rows->force_max_row, rows->force, t, u, f);
    for (c = 0; c < 3; c ++) {
        out->force[c] = f[c];
    }

    out->drag = f[3];
}

/** Get the velocity a particle moves with but for the drift, which a stretched sprite stretches
 * along: speed times its stored velocity plus the velocity curve.
 *
 * @param velocity The stored velocity
 * @param motion The velocity and speed of its ParticleMotion, NULL for none
 * @param out Receives the velocity, may alias velocity
 */
static inline void box_particle_moving(
    const float *velocity,
    const box_particle_motion_t *motion,
    float *out)
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        out[c] = motion ? velocity[c] * motion->speed + motion->velocity[c] : velocity[c];
    }
}

/** Scale the curves of ParticleMotion of a particle by the uniform world scale of its emitter,
 * turbulence z of the block: the velocity and the force, not the speed and the drag.
 *
 * @param motion The curves, scaled in place
 * @param scale The uniform world scale
 */
static inline void box_particle_motion_scaled(
    box_particle_motion_t *motion,
    float scale)
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        motion->velocity[c] *= scale;
        motion->force[c] *= scale;
    }
}

/** Get the degrees a particle has turned by its spin, the area under its spin curve up to its
 * age; a constant curve turns it by its spin times the value times its age.
 *
 * @param spin Spin the particle spawned with, degrees per second
 * @param table The curve table
 * @param row Row of the spin curve, BOX_PARTICLE_CURVE_CONSTANT for a constant
 * @param max_row Row of its max curve, BOX_PARTICLE_CURVE_CONSTANT for none
 * @param value The value of a constant spin curve
 * @param age Seconds the particle lived
 * @param life Seconds it lives
 * @param u The blend of the particle between the two curves
 * @return The degrees
 */
static inline float box_particle_turned(
    float spin,
    const float *table,
    uint32_t row,
    uint32_t max_row,
    float value,
    float age,
    float life,
    float u)
{
    float result;
    if (row == BOX_PARTICLE_CURVE_CONSTANT) {
        result = spin * value * age;
    } else {
        result = spin * life * box_particle_curve_area_range(
            table, row, max_row, value, box_particle_norm_age(age, life), u);
    }

    return result;
}

/** Get the rotation of a particle in degrees: the rotation it spawned with, the turns of the
 * rotation curve and what its spin turned it by.
 *
 * @param rotation Rotation it spawned with in degrees
 * @param spin Spin it spawned with in degrees per second
 * @param table The curve table
 * @param rotation_row Row of the rotation curve, in turns, or BOX_PARTICLE_CURVE_CONSTANT
 * @param rotation_max Row of its max curve, BOX_PARTICLE_CURVE_CONSTANT for none
 * @param rotation_value Value of a constant rotation curve
 * @param rotation_u Blend of the particle between the rotation curves
 * @param spin_row Row of the spin curve, or BOX_PARTICLE_CURVE_CONSTANT
 * @param spin_max Row of its max curve, BOX_PARTICLE_CURVE_CONSTANT for none
 * @param spin_value Value of a constant spin curve
 * @param spin_u Blend of the particle between the spin curves
 * @param age Seconds the particle lived
 * @param life Seconds it lives
 * @return The degrees
 */
static inline float box_particle_rotation(
    float rotation,
    float spin,
    const float *table,
    uint32_t rotation_row,
    uint32_t rotation_max,
    float rotation_value,
    float rotation_u,
    uint32_t spin_row,
    uint32_t spin_max,
    float spin_value,
    float spin_u,
    float age,
    float life)
{
    float t = box_particle_norm_age(age, life);

    return rotation + 360.0f * box_particle_curve_range(
            table, rotation_row, rotation_max, rotation_value, t, rotation_u) +
        box_particle_turned(spin, table, spin_row, spin_max, spin_value, age, life, spin_u);
}

/** Evaluate a gradient at a normalized age from its rows in the curve table, linear between two
 * samples; a constant gradient reads no row.
 *
 * @param table The curve table
 * @param row First of the BOX_PARTICLE_GRADIENT_ROWS rows of the gradient, or
 *        BOX_PARTICLE_CURVE_CONSTANT
 * @param value Linear color of a constant gradient
 * @param t The normalized age, clamped to 0 to 1
 * @param out Receives the linear color
 */
static inline void box_particle_gradient(
    const float *table,
    uint32_t row,
    const float *value,
    float t,
    float *out)
{
    float f;
    int32_t c;
    if (row == BOX_PARTICLE_CURVE_CONSTANT) {
        for (c = 0; c < 3; c ++) {
            out[c] = value[c];
        }
    } else {
        const float *g = table + (size_t)row * BOX_PARTICLE_CURVE_SAMPLES;
        int32_t k = box_particle_curve_sample(t, &f) * 4;
        for (c = 0; c < 3; c ++) {
            out[c] = g[k + c] + (g[k + 4 + c] - g[k + c]) * f;
        }
    }
}

/** Evaluate the gradient of a particle at a normalized age: the gradient, or with a max
 * gradient its blend toward it in linear light by the draw of the particle.
 *
 * @param table The curve table
 * @param row First row of the gradient, or BOX_PARTICLE_CURVE_CONSTANT
 * @param max_row First row of the max gradient, BOX_PARTICLE_CURVE_CONSTANT for none
 * @param value Linear color of a constant gradient
 * @param t The normalized age, clamped to 0 to 1
 * @param u The blend of the particle, box_particle_blend
 * @param out Receives the linear color
 */
static inline void box_particle_gradient_range(
    const float *table,
    uint32_t row,
    uint32_t max_row,
    const float *value,
    float t,
    float u,
    float *out)
{
    float max[3];
    int32_t c;
    box_particle_gradient(table, row, value, t, out);
    if (max_row != BOX_PARTICLE_CURVE_CONSTANT) {
        box_particle_gradient(table, max_row, value, t, max);
        for (c = 0; c < 3; c ++) {
            out[c] = out[c] + (max[c] - out[c]) * u;
        }
    }
}

/** Get the size or opacity of a particle: the value it spawned with times its curve at a
 * normalized age, clamped at 0 as a curve that overshoots its keys may go below it.
 *
 * @param base The value it spawned with
 * @param table The curve table
 * @param row Row of the curve, BOX_PARTICLE_CURVE_CONSTANT for a constant
 * @param max_row Row of its max curve, BOX_PARTICLE_CURVE_CONSTANT for none
 * @param value The value of a constant curve
 * @param t The normalized age, clamped to 0 to 1
 * @param u The blend of the particle between the two curves
 * @return The value, 0 or more
 */
static inline float box_particle_scaled(
    float base,
    const float *table,
    uint32_t row,
    uint32_t max_row,
    float value,
    float t,
    float u)
{
    return fmaxf(base * box_particle_curve_range(table, row, max_row, value, t, u), 0.0f);
}

/** Get the color of a particle: the linear color it spawned with times its gradient at a
 * normalized age, each channel clamped at 0.
 *
 * @param color The linear color it spawned with
 * @param table The curve table
 * @param row First row of the gradient, or BOX_PARTICLE_CURVE_CONSTANT
 * @param max_row First row of the max gradient, BOX_PARTICLE_CURVE_CONSTANT for none
 * @param value Linear color of a constant gradient
 * @param t The normalized age, clamped to 0 to 1
 * @param u The blend of the particle between the two gradients
 * @param out Receives the linear color, may alias color
 */
static inline void box_particle_tinted(
    const float *color,
    const float *table,
    uint32_t row,
    uint32_t max_row,
    const float *value,
    float t,
    float u,
    float *out)
{
    float g[3];
    int32_t c;
    box_particle_gradient_range(table, row, max_row, value, t, u, g);
    for (c = 0; c < 3; c ++) {
        out[c] = fmaxf(color[c] * g[c], 0.0f);
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
