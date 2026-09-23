/**
 * @file renderer/transition.h
 * @brief Transition records the renderer evaluates next to the instances.
 *
 * A record is sent once when a static instance transition starts: channel, start
 * and target values, start time and parameters. The renderer thread evaluates
 * it every frame from the record alone.
 */

#ifndef BOX_RENDERER_TRANSITION_H
#define BOX_RENDERER_TRANSITION_H

#include <stdint.h>
#include <stdbool.h>
#include "vec.h"

/** Floats the widest transitioned value is made of */
#define BOX_DISPLAY_FLOATS (4)

/** Local position of the instance, three floats */
#define BOX_DISPLAY_POSITION (0)

/** Euler rotation of the instance, three floats */
#define BOX_DISPLAY_ROTATION (1)

/** Local scale of the instance, three floats */
#define BOX_DISPLAY_SCALE (2)

/** Base color of the instance, three display encoded bytes */
#define BOX_DISPLAY_RGB (3)

/** Resolved tint of the instance, four display encoded bytes */
#define BOX_DISPLAY_TINT (4)

/** Opacity of the instance, one float */
#define BOX_DISPLAY_OPACITY (5)

/** Emission of the instance: strength and three display encoded bytes */
#define BOX_DISPLAY_EMISSIVE (6)

/** Channels of an instance a transition can move */
#define BOX_DISPLAY_CHANNELS (7)

/** Constant rate from start to end */
#define BOX_TIMING_LINEAR (0)

/** Slow start and a slower end */
#define BOX_TIMING_EASE (1)

/** Slow start, full rate at the end */
#define BOX_TIMING_EASE_IN (2)

/** Full rate at the start, slow end */
#define BOX_TIMING_EASE_OUT (3)

/** Symmetric slow start and slow end */
#define BOX_TIMING_EASE_IN_OUT (4)

/** One value of one instance that the renderer interpolates every frame */
typedef struct box_transition_rec_t {
    double start;                 /**< Packet time the transition started */
    float from[BOX_DISPLAY_FLOATS]; /**< Value the channel showed when it started */
    float to[BOX_DISPLAY_FLOATS];   /**< Value that was assigned */
    float duration;               /**< Seconds the value takes to reach the target */
    float delay;                  /**< Seconds the value holds still before it moves */
    uint32_t timing;              /**< Curve the progress follows, a BOX_TIMING value */
    uint32_t channel;             /**< Channel the floats belong to */
    uint32_t first;               /**< First float of the channel the record moves */
    uint32_t count;               /**< Floats the record moves */
} box_transition_rec_t;

/** Transitions of one static instance, sent once when they start.
 *
 * The values are the ones the author assigned, which is where every record
 * ends, and the rest is the context the instance is composed from: the world
 * transform of its parent, the scale and bounds of its mesh and its depth.
 */
typedef struct box_transition_cmd_t {
    uint32_t mesh;        /**< Mesh handle of the instance */
    uint32_t slot;        /**< Static instance slot */
    uint32_t first;       /**< First record of the instance in the packet */
    uint32_t count;       /**< Records the instance carries */
    double until;         /**< Packet time the last record of the instance ends */
    float parent[16];     /**< World transform of the parent, column major */
    float mesh_scale[3];  /**< Scale the mesh bakes into the instance transform */
    float depth;          /**< Offset along the world z axis of the instance */
    float bounds_min[3];  /**< Local bounds minimum of the unscaled mesh */
    float bounds_max[3];  /**< Local bounds maximum of the unscaled mesh */
    float values[BOX_DISPLAY_CHANNELS][BOX_DISPLAY_FLOATS]; /**< Values that were assigned */
    uint32_t present;     /**< Bit per channel the entity carries */
    uint32_t posed;       /**< The transform is composed again from the pose */
    uint32_t oriented;    /**< The rotation channel holds an Orientation */
} box_transition_cmd_t;

/** Instance whose transitions have finished, reported with the returned packet */
typedef struct box_transition_done_t {
    uint32_t mesh; /**< Mesh handle of the instance */
    uint32_t slot; /**< Static instance slot */
} box_transition_done_t;

/** Progress of a timing function.
 *
 * @param timing The timing function, a BOX_TIMING value
 * @param t Linear progress from 0 to 1
 * @return Eased progress from 0 to 1
 */
static inline float box_transition_curve(
    uint32_t timing,
    float t)
{
    float result = t;
    float inv = 1.0f - t;
    if (timing == BOX_TIMING_EASE_IN) {
        result = t * t;
    } else if (timing == BOX_TIMING_EASE_OUT) {
        result = 1.0f - inv * inv;
    } else if (timing == BOX_TIMING_EASE_IN_OUT) {
        result = t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * inv * inv;
    } else if (timing == BOX_TIMING_EASE) {
        result = t * t * (3.0f - 2.0f * t);
    }

    return result;
}

/** One over a duration, the factor the progress of a transition is scaled by.
 *
 * @param duration Seconds the value takes to reach the target
 * @return The factor, zero for a duration of zero
 */
static inline double box_transition_rate(
    float duration)
{
    return duration > 0.0f ? 1.0 / (double)duration : 0.0;
}

/** Interpolate one transition at a time.
 *
 * Pure function of its arguments, shared by the game side and the renderer
 * thread.
 *
 * @param start Time the transition started
 * @param delay Seconds the value holds still before it moves
 * @param duration Seconds the value takes to reach the target
 * @param rate One over the duration, from box_transition_rate
 * @param timing Curve the progress follows, a BOX_TIMING value
 * @param from Value the transition started from
 * @param to Value that was assigned
 * @param count Floats to interpolate
 * @param time The time to evaluate at
 * @param out Receives count floats
 * @return true while the transition has not reached its end
 */
static inline bool box_transition_lerp(
    double start,
    float delay,
    float duration,
    double rate,
    uint32_t timing,
    const float *from,
    const float *to,
    int32_t count,
    double time,
    float *out)
{
    double elapsed = time - start - (double)delay;
    double span = (double)duration;
    bool running = elapsed < span;
    float t;
    if (!running) {
        t = 1.0f;
    } else if (elapsed <= 0.0) {
        t = 0.0f;
    } else {
        t = box_transition_curve(timing, (float)(elapsed * rate));
    }

    int32_t i;
    for (i = 0; i < count; i ++) {
        out[i] = from[i] + (to[i] - from[i]) * t;
    }

    return running;
}

struct box_instance_t;

/** Compute the instance data a set of transitions displays at a time.
 *
 * @param cmd The transitions of the instance
 * @param records The records of the instance, cmd->count of them
 * @param time The time to evaluate at
 * @param out The instance data, its other fields are left alone
 * @return true while one of the records has not reached its end
 */
bool box_transition_fill(
    const box_transition_cmd_t *cmd,
    const box_transition_rec_t *records,
    double time,
    struct box_instance_t *out);

/** Entry index of an instance that carries no transition */
#define BOX_TRANSITION_NONE (0xFFFFFFFFu)

/** When one record of one channel runs, read once per frame.
 *
 * The frame pass walks a whole channel in order, so the fields it needs of a
 * record sit next to each other and the ones it does not need are elsewhere.
 */
typedef struct box_transition_time_t {
    double start;    /**< Packet time the record started */
    double rate;     /**< One over the duration, so the pass never divides */
    float delay;     /**< Seconds the value holds still before it moves */
    float duration;  /**< Seconds the value takes to reach the target */
    uint32_t timing; /**< Curve the progress follows, a BOX_TIMING value */
    uint32_t owner;  /**< Entry the record belongs to */
} box_transition_time_t;

/** The floats one record of one channel moves.
 *
 * Both ends cover the whole channel, whatever part of it the transition was
 * given, so the interpolation is the same four lanes for every record and the
 * floats the transition leaves alone interpolate from themselves.
 */
typedef struct box_transition_span_t {
    float from[BOX_DISPLAY_FLOATS]; /**< Values the channel showed when it started */
    float to[BOX_DISPLAY_FLOATS];   /**< Values that were assigned */
} box_transition_span_t;

/** The values one record of one channel shows this frame */
typedef struct box_transition_out_t {
    float value[BOX_DISPLAY_FLOATS]; /**< Floats of the channel the frame shows */
} box_transition_out_t;

/** The active records of one channel, as the arrays one pass reads and writes */
typedef struct box_transition_lane_t {
    box_vec_t times; /**< box_transition_time_t, one per record of the channel */
    box_vec_t spans; /**< box_transition_span_t, one per record of the channel */
    box_vec_t outs;  /**< box_transition_out_t, one per record of the channel */
} box_transition_lane_t;

/** One instance the renderer interpolates, without the records themselves */
typedef struct box_transition_entry_t {
    box_transition_cmd_t cmd; /**< The command that started the records */
    uint32_t lane[BOX_DISPLAY_CHANNELS]; /**< Record of the channel, or BOX_TRANSITION_NONE */
    uint32_t moved;   /**< Bit per channel a record of the entry moves */
    uint32_t running; /**< Bit per channel that has not reached its end */
} box_transition_entry_t;

/** The instances the renderer interpolates and their records per channel.
 *
 * The entries hold what composing an instance needs and the lanes hold what
 * interpolating a value needs, so a frame reads a hundred thousand records as
 * seven runs of contiguous memory instead of walking the instances.
 */
typedef struct box_transition_pool_t {
    box_vec_t entries; /**< box_transition_entry_t, one per instance */
    box_vec_t bases;   /**< Instance data of every entry, of the size init was given */
    box_transition_lane_t lanes[BOX_DISPLAY_CHANNELS]; /**< Records of every channel */
} box_transition_pool_t;

/** Prepare a pool for instances of a given data size.
 *
 * @param pool The pool
 * @param base_size Size of the instance data of one entry
 */
void box_transition_pool_init(
    box_transition_pool_t *pool,
    uint32_t base_size);

/** Release the storage of a pool.
 *
 * @param pool The pool
 */
void box_transition_pool_fini(
    box_transition_pool_t *pool);

/** Entry of an index.
 *
 * @param pool The pool
 * @param index The entry index
 * @return The entry
 */
box_transition_entry_t* box_transition_pool_entry(
    const box_transition_pool_t *pool,
    uint32_t index);

/** Instance data of an index.
 *
 * @param pool The pool
 * @param index The entry index
 * @return The instance data the records are composed into
 */
void* box_transition_pool_base(
    const box_transition_pool_t *pool,
    uint32_t index);

/** Add or replace the records of one instance.
 *
 * @param pool The pool
 * @param index Entry to replace, BOX_TRANSITION_NONE to add one
 * @param cmd The command of the instance
 * @param records The records of the command, cmd->count of them
 * @param base The instance data the records are composed into
 * @return The entry index
 */
uint32_t box_transition_pool_start(
    box_transition_pool_t *pool,
    uint32_t index,
    const box_transition_cmd_t *cmd,
    const box_transition_rec_t *records,
    const void *base);

/** Drop an entry, moving the last one into the hole it leaves.
 *
 * The caller finds the entry that moved at the index it dropped, as long as
 * that index is still in the pool, and points the instance at it again.
 *
 * @param pool The pool
 * @param index The entry index
 */
void box_transition_pool_stop(
    box_transition_pool_t *pool,
    uint32_t index);

/** Interpolate every record of the pool at a time, one pass per channel.
 *
 * @param pool The pool
 * @param time The time to evaluate at
 */
void box_transition_pool_eval(
    box_transition_pool_t *pool,
    double time);

/** Compose the values an entry shows into its instance data.
 *
 * The values come from the lanes the entry has a record in, so an entry is
 * composed after the pass that interpolated them.
 *
 * @param pool The pool
 * @param index The entry index
 * @return true while a record of the entry has not reached its end
 */
bool box_transition_pool_compose(
    const box_transition_pool_t *pool,
    uint32_t index);

#endif
