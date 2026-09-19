/**
 * @file renderer/transition.h
 * @brief Transition records the renderer evaluates next to the instances.
 *
 * A transition that moves a value of a static instance travels to the renderer
 * once, when it starts, and the renderer thread computes the value the frame
 * shows from the record alone. The record names a channel of the instance, the
 * value it started from, the value that was assigned, the time it started and
 * the parameters, which is everything the interpolation needs, so the main
 * thread does nothing per frame while the transition runs.
 */

#ifndef BOX_RENDERER_TRANSITION_H
#define BOX_RENDERER_TRANSITION_H

#include <stdint.h>
#include <stdbool.h>

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

/** Interpolate one transition at a time.
 *
 * This is the whole of the per frame work of a transition and touches nothing
 * but its arguments, so the game side and the renderer thread share it.
 *
 * @param start Time the transition started
 * @param delay Seconds the value holds still before it moves
 * @param duration Seconds the value takes to reach the target
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
        t = box_transition_curve(timing, (float)(elapsed / span));
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

#endif
