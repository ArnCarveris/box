/**
 * @file audio/core/tape_stop.c
 * @brief The tape stop kind of effect: the speed of a read of the input ramps down to a halt
 * and back up, so the pitch and the time slow down together.
 *
 * The line of each channel lives on the memory of the effect slot and records every frame; the
 * read lags behind the input by the frames it lost to a speed under 1, and drops the lag at a
 * halt or, up to speed, crossfades to the input. Its params are a box_audio_tape_stop_params_t.
 *
 * Every step is per frame, so a render is the same whatever its sizes. Frames older than the
 * ones it wrote read as silence, so the memory needs no clearing.
 */

#include "internal.h"

/** Distance of the phase to an end of the ramp under which it is at the end, so that the sum
 * of its steps over stop_time or start_time lands on the frame of the end */
#define BOX_AUDIO_TAPE_STOP_EPSILON 1e-9

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_tape_stop_clamp(
    float v,
    float lo,
    float hi)
{
    if (!(v >= lo)) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }

    return v;
}

/** Get the frames of each line at a sample rate: the longest lag and the frames of the
 * interpolation.
 *
 * @param sample_rate Frames per second
 * @return Frames of a line
 */
static int32_t box_audio_tape_stop_length(
    float sample_rate)
{
    return (int32_t)(BOX_AUDIO_TAPE_STOP_MAX * sample_rate) + 8;
}

int32_t box_audio_tape_stop_floats(
    float sample_rate)
{
    float rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;

    return box_audio_tape_stop_length(rate) * 2;
}

/** Clamp the params of an effect into the target of its tape stop; NaN becomes the low end,
 * or 0 for the curve. Sets the steps of the phase and its exponent.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_tape_stop_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_tape_stop_t *ts = &fx->data.tape_stop;
    const float *p = fx->params;
    ts->target.active = box_audio_tape_stop_clamp(p[0], 0, 1);
    ts->target.stop_time = box_audio_tape_stop_clamp(p[1], BOX_AUDIO_TAPE_STOP_TIME_MIN,
        BOX_AUDIO_TAPE_STOP_TIME_MAX);
    ts->target.start_time = box_audio_tape_stop_clamp(p[2], BOX_AUDIO_TAPE_STOP_TIME_MIN,
        BOX_AUDIO_TAPE_STOP_TIME_MAX);
    ts->target.curve = p[3] == p[3] ? box_audio_tape_stop_clamp(p[3], -1, 1) : 0;
    ts->down = 1.0 / ((double)ts->target.stop_time * (double)core->sample_rate);
    ts->up = 1.0 / ((double)ts->target.start_time * (double)core->sample_rate);
    ts->power = ts->target.curve == 0 ? 1.0 :
        box_audio_synth_exp2(2.0 * (double)ts->target.curve);
}

void box_audio_tape_stop_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_tape_stop_t *ts = &fx->data.tape_stop;
    (void)fade_in;
    box_audio_tape_stop_target(core, fx);
    ts->length = box_audio_tape_stop_length(core->sample_rate);
    ts->line[0] = fx->memory;
    ts->line[1] = fx->memory + ts->length;
    ts->pos = 0;
    ts->valid = 0;
    ts->phase = 0;
    ts->lag = 0;
    ts->from = 0;
    ts->crossfade = false;
    box_audio_ramp_set(&ts->xfade, 1);
}

void box_audio_tape_stop_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_tape_stop_target(core, fx);
}

/** Get the speed at the phase of the ramp, 1 - phase^power.
 *
 * @param ts The tape stop
 * @return The speed, 0..1
 */
static inline double box_audio_tape_stop_speed(
    const box_audio_tape_stop_t *ts)
{
    double p = ts->phase, result;
    if (p <= 0) {
        result = 1;
    } else if (p >= 1) {
        result = 0;
    } else if (ts->power == 1.0) {
        result = 1 - p;
    } else {
        result = 1 - box_audio_synth_exp2(ts->power * box_audio_synth_log2(p));
        result = result > 0 ? result : 0;
    }

    return result;
}

/** Read both lines a lag behind the last input with Catmull-Rom interpolation; a whole lag
 * reads exactly. The four points are back + 1 to back - 2 frames back, the newest last.
 *
 * Frames after the last input read as the last input, older ones than it wrote as silence.
 *
 * @param ts The tape stop
 * @param last Frame of the line of the last input
 * @param lag Frames behind it, 0..length - 8
 * @param yl Receives the left sample
 * @param yr Receives the right sample
 */
static inline void box_audio_tape_stop_read(
    const box_audio_tape_stop_t *ts,
    int32_t last,
    double lag,
    float *yl,
    float *yr)
{
    int32_t back = (int32_t)lag, d, k, i;
    float t = (float)(lag - (double)back), a[4], b[4];
    if (t != 0) {
        back ++;
        t = 1 - t;
    }

    for (d = 0; d < 4; d ++) {
        a[d] = 0;
        b[d] = 0;
        k = back + 1 - d;
        k = k > 0 ? k : 0;
        if ((d == 1 || t != 0) && k < ts->valid) {
            i = last - k;
            i += i < 0 ? ts->length : 0;
            a[d] = ts->line[0][i];
            b[d] = ts->line[1][i];
        }
    }

    if (t == 0) {
        *yl = a[1];
        *yr = b[1];
    } else {
        float c1 = 0.5f * (a[2] - a[0]);
        float c2 = a[0] - 2.5f * a[1] + 2.0f * a[2] - 0.5f * a[3];
        float c3 = 0.5f * (a[3] - a[0]) + 1.5f * (a[1] - a[2]);
        float e1 = 0.5f * (b[2] - b[0]);
        float e2 = b[0] - 2.5f * b[1] + 2.0f * b[2] - 0.5f * b[3];
        float e3 = 0.5f * (b[3] - b[0]) + 1.5f * (b[1] - b[2]);
        *yl = ((c3 * t + c2) * t + c1) * t + a[1];
        *yr = ((e3 * t + e2) * t + e1) * t + b[1];
    }
}

bool box_audio_tape_stop_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_tape_stop_t *ts = &fx->data.tape_stop;
    float *bl = ts->line[0], *bright = ts->line[1];
    double longest = (double)(ts->length - 8);
    bool on = ts->target.active >= 0.5f;
    int32_t i, last;
    (void)silent;
    for (i = 0; i < frames; i ++) {
        double v;
        float yl, yr, ol, orr, gain;
        last = ts->pos;
        bl[last] = box_audio_clean(l[i]);
        bright[last] = box_audio_clean(r[i]);
        ts->pos = ts->pos + 1 < ts->length ? ts->pos + 1 : 0;
        ts->valid = ts->valid < ts->length ? ts->valid + 1 : ts->valid;
        if (on) {
            ts->phase = ts->phase + ts->down < 1 - BOX_AUDIO_TAPE_STOP_EPSILON ?
                ts->phase + ts->down : 1;
        } else {
            ts->phase = ts->phase - ts->up > BOX_AUDIO_TAPE_STOP_EPSILON ?
                ts->phase - ts->up : 0;
        }

        v = box_audio_tape_stop_speed(ts);
        if (v <= 0) {
            ts->lag = 0;
            ts->crossfade = false;
            box_audio_ramp_set(&ts->xfade, 1);
            l[i] = 0;
            r[i] = 0;
            continue;
        }

        ts->lag = ts->lag + (1 - v) < longest ? ts->lag + (1 - v) : longest;
        if (ts->crossfade) {
            ts->from = ts->from + (1 - v) < longest ? ts->from + (1 - v) : longest;
        } else if (v >= 1 && ts->lag > 0) {
            ts->from = ts->lag;
            ts->lag = 0;
            ts->crossfade = true;
            box_audio_ramp_set(&ts->xfade, 0);
            box_audio_ramp_to(&ts->xfade, 1,
                (int32_t)(BOX_AUDIO_TAPE_STOP_CROSSFADE * core->sample_rate));
        }

        if (ts->lag == 0 && !ts->crossfade) {
            continue;
        }

        box_audio_tape_stop_read(ts, last, ts->lag, &yl, &yr);
        if (ts->crossfade) {
            float g = box_audio_ramp_next(&ts->xfade);
            box_audio_tape_stop_read(ts, last, ts->from, &ol, &orr);
            yl = ol + g * (yl - ol);
            yr = orr + g * (yr - orr);
            if (ts->xfade.left == 0) {
                ts->crossfade = false;
            }
        }

        gain = v >= BOX_AUDIO_TAPE_STOP_FADE ? 1.0f : (float)(v / BOX_AUDIO_TAPE_STOP_FADE);
        l[i] = yl * gain;
        r[i] = yr * gain;
    }

    return false;
}

void box_audio_tape_stop_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    (void)core;
    (void)fx;
    (void)frames;
}
