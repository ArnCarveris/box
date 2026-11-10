/**
 * @file audio/core/ramp.h
 * @brief Linear ramps of the core, which smooth a change of a gain or a parameter.
 */

#ifndef BOX_AUDIO_CORE_RAMP_H
#define BOX_AUDIO_CORE_RAMP_H

#include <stdint.h>

/** A value that moves linearly to its target over a number of frames */
typedef struct box_audio_ramp_t {
    float value;   /**< Current value */
    float target;  /**< Value it moves to */
    float step;    /**< Change per frame */
    int32_t left;  /**< Frames until it reaches the target, 0 when it is there */
} box_audio_ramp_t;

/** Move a ramp to a target over a number of frames.
 *
 * @param ramp The ramp
 * @param target The target
 * @param frames Frames to reach it, at least 1
 */
static inline void box_audio_ramp_to(
    box_audio_ramp_t *ramp,
    float target,
    int32_t frames)
{
    ramp->target = target;
    if (ramp->value == target) {
        ramp->left = 0;
        ramp->step = 0;
    } else {
        ramp->left = frames > 0 ? frames : 1;
        ramp->step = (target - ramp->value) / (float)ramp->left;
    }
}

/** Set a ramp to a value at once.
 *
 * @param ramp The ramp
 * @param value The value
 */
static inline void box_audio_ramp_set(
    box_audio_ramp_t *ramp,
    float value)
{
    ramp->value = value;
    ramp->target = value;
    ramp->step = 0;
    ramp->left = 0;
}

/** Advance a ramp by a frame.
 *
 * @param ramp The ramp
 * @return Its value for the frame
 */
static inline float box_audio_ramp_next(
    box_audio_ramp_t *ramp)
{
    if (ramp->left > 0) {
        ramp->left --;
        ramp->value = ramp->left ? ramp->value + ramp->step : ramp->target;
    }

    return ramp->value;
}

#endif
