/**
 * @file audio/core/shift.h
 * @brief The delay line pitch shifter of the core, shared by the pitch shifter effect and the
 * shimmer of the reverb.
 *
 * A shifter is a line read by BOX_AUDIO_SHIFT_TAPS taps spread over a window, each sliding
 * through it at 1 - ratio frames per frame and wrapping around, so it plays its input ratio
 * times as fast, up to the window late. Private to src/audio/core; included by internal.h.
 *
 * A sine window, sin(pi phase), fades each tap in and out over its pass, so a wrap is silent;
 * two taps half a window apart have gains sin and cos, whose powers sum to 1, a constant power
 * crossfade. The phase is a double, so a render is the same whatever its sizes.
 */

#ifndef BOX_AUDIO_CORE_SHIFT_H
#define BOX_AUDIO_CORE_SHIFT_H

#include "core.h"

/** Read taps of a shifter, spread evenly over its window */
#define BOX_AUDIO_SHIFT_TAPS 2

/** Largest magnitude a shifter outputs into a feedback path; a larger sample is soft clipped
 * toward twice this, so a feedback loop around a shifter stays bounded */
#define BOX_AUDIO_SHIFT_CLIP 1.0f

/** A pitch shifter on a line of its own */
typedef struct box_audio_shift_t {
    float *line;       /**< The line, length floats */
    int32_t length;    /**< Frames of the line: the longest window and 3 */
    int32_t pos;       /**< Next write position */
    int32_t valid;     /**< Frames written since the line was cleared, up to length */
    double phase;      /**< Place of the first tap in the window, 0..1, 0 nearest */
} box_audio_shift_t;

/** Get the frames of the line of a shifter whose window is at most some frames: the window, a
 * frame for the nearest read, a frame for the interpolation and a frame for the write.
 *
 * @param window Longest window in frames
 * @return Frames of the line
 */
static inline int32_t box_audio_shift_length(
    int32_t window)
{
    return window + 3;
}

/** Put a shifter on its line and clear it.
 *
 * @param s The shifter
 * @param line The line, length floats
 * @param length Frames of the line
 */
static inline void box_audio_shift_init(
    box_audio_shift_t *s,
    float *line,
    int32_t length)
{
    s->line = line;
    s->length = length;
    s->pos = 0;
    s->valid = 0;
    s->phase = 0;
}

/** Forget what a shifter wrote, so it reads silence, and put its taps back at their start.
 *
 * @param s The shifter
 */
static inline void box_audio_shift_clear(
    box_audio_shift_t *s)
{
    s->pos = 0;
    s->valid = 0;
    s->phase = 0;
}

/** Read the line of a shifter frames back, with linear interpolation; frames older than the
 * ones written since it was cleared read as silence.
 *
 * @param s The shifter
 * @param back Frames back, 1..length - 2
 * @return The sample
 */
static inline float box_audio_shift_read(
    const box_audio_shift_t *s,
    double back)
{
    int32_t whole = (int32_t)back, i0 = s->pos - whole, i1 = i0 - 1;
    float frac = (float)(back - (double)whole), a, b;
    i0 += i0 < 0 ? s->length : 0;
    i1 += i1 < 0 ? s->length : 0;
    a = whole <= s->valid ? s->line[i0] : 0.0f;
    b = whole < s->valid ? s->line[i1] : 0.0f;

    return a + frac * (b - a);
}

/** Run a shifter for a frame: read its taps, then write its input and move the taps on.
 *
 * @param s The shifter
 * @param in The input of the frame
 * @param ratio Pitch ratio, 2 an octave up, 0.5 an octave down, 1 unchanged
 * @param window Window in frames, 1..length - 3
 * @return The shifted signal of the frame
 */
static inline float box_audio_shift_next(
    box_audio_shift_t *s,
    float in,
    double ratio,
    double window)
{
    float result = 0;
    double longest = (double)(s->length - 3);
    double phase;
    int32_t t;
    if (!(window >= 1)) {
        window = 1;
    } else if (window > longest) {
        window = longest;
    }

    for (t = 0; t < BOX_AUDIO_SHIFT_TAPS; t ++) {
        phase = s->phase + (double)t / (double)BOX_AUDIO_SHIFT_TAPS;
        phase -= phase >= 1 ? 1 : 0;
        result += box_audio_core_sin(phase * 0.5) *
            box_audio_shift_read(s, 1 + window * phase);
    }

    s->line[s->pos] = box_audio_clean(in);
    s->pos = s->pos + 1 < s->length ? s->pos + 1 : 0;
    s->valid = s->valid < s->length ? s->valid + 1 : s->valid;
    phase = s->phase + (1 - ratio) / window;
    phase -= (double)(int32_t)phase;
    phase += phase < 0 ? 1 : 0;
    s->phase = phase < 1 ? phase : 0;

    return result;
}

/** Soft clip a shifted sample before it goes back into a feedback path: unchanged up to
 * BOX_AUDIO_SHIFT_CLIP, then bent with a continuous slope toward twice it, never past it.
 *
 * @param x The sample
 * @return The clipped sample
 */
static inline float box_audio_shift_clip(
    float x)
{
    float a = x < 0 ? -x : x, result = x;
    if (a > BOX_AUDIO_SHIFT_CLIP) {
        a = BOX_AUDIO_SHIFT_CLIP * (2 - BOX_AUDIO_SHIFT_CLIP / a);
        result = x < 0 ? -a : a;
    }

    return result;
}

#endif
