/**
 * @file audio/core/delay.c
 * @brief The delay kind of effect: a line per channel read with linear interpolation, a one
 * pole lowpass in the feedback path, ping pong and a mix.
 *
 * The lines live on the memory of the effect slot, BOX_AUDIO_DELAY_MAX seconds each. A change
 * of the time crossfades from the old to the new read position over BOX_AUDIO_DELAY_CROSSFADE,
 * so the pitch does not glide and nothing clicks. Its params are a box_audio_delay_params_t.
 *
 * The delay counts the frames it wrote since its line was last silent and reads older frames
 * as silence, so neither a start nor the end of its tail clears the memory.
 */

#include "internal.h"

/** Repeats that sum to less than this, -140 dB, have decayed to silence */
#define BOX_AUDIO_DELAY_SILENCE 1e-7f

/** Share of the rate at and above which the damping is off */
#define BOX_AUDIO_DELAY_DAMPING_OFF 0.49f

/** Largest damping kept in Hz, before the check against the rate */
#define BOX_AUDIO_DELAY_DAMPING_HUGE 1000000.0f

/** 2 pi divided by ln(2), for the pole of the damping filter as a power of 2 */
#define BOX_AUDIO_DELAY_TAU_LOG2E 9.064720283654388

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_delay_clamp(
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

/** Get the frames of each line at a sample rate: the longest time, a frame for the
 * interpolation and a frame for the write position.
 *
 * @param sample_rate Frames per second
 * @return Frames of a line
 */
static int32_t box_audio_delay_length(
    float sample_rate)
{
    return (int32_t)(BOX_AUDIO_DELAY_MAX * sample_rate) + 2;
}

int32_t box_audio_delay_floats(
    float sample_rate)
{
    float rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;

    return box_audio_delay_length(rate) * 2;
}

/** Set a read position for a time.
 *
 * @param core The core
 * @param dl The delay
 * @param tap The read position
 * @param time Seconds, clamped
 */
static void box_audio_delay_tap(
    const box_audio_core_t *core,
    const box_audio_delay_t *dl,
    box_audio_delay_tap_t *tap,
    float time)
{
    double frames = (double)time * (double)core->sample_rate;
    double longest = (double)(dl->length - 2);
    if (!(frames >= 1)) {
        frames = 1;
    } else if (frames > longest) {
        frames = longest;
    }

    tap->time = time;
    tap->frames = (int32_t)frames;
    tap->frac = (float)(frames - (double)tap->frames);
}

/** Clamp the params of an effect into the target of its delay; NaN becomes the low end. Sets
 * the coefficient of the damping filters.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_delay_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_delay_t *dl = &fx->data.delay;
    const float *p = fx->params;
    float fc;
    dl->target.time = box_audio_delay_clamp(p[0], 0, BOX_AUDIO_DELAY_MAX);
    dl->target.feedback = box_audio_delay_clamp(p[1], 0, BOX_AUDIO_DELAY_FEEDBACK_MAX);
    dl->target.damping = box_audio_delay_clamp(p[2], BOX_AUDIO_DELAY_DAMPING_MIN,
        BOX_AUDIO_DELAY_DAMPING_HUGE);
    dl->target.ping_pong = box_audio_delay_clamp(p[3], 0, 1) >= 0.5f ? 1.0f : 0.0f;
    dl->target.mix = box_audio_delay_clamp(p[4], 0, 1);
    fc = dl->target.damping;
    dl->damp = 1;
    if (fc < BOX_AUDIO_DELAY_DAMPING_OFF * core->sample_rate) {
        dl->damp = (float)(1 - box_audio_synth_exp2(
            -BOX_AUDIO_DELAY_TAU_LOG2E * (double)fc / (double)core->sample_rate));
    }
}

/** Start a crossfade from the read position that runs to the one of a time.
 *
 * @param core The core
 * @param dl The delay
 * @param time Seconds, clamped
 */
static void box_audio_delay_cross(
    const box_audio_core_t *core,
    box_audio_delay_t *dl,
    float time)
{
    dl->from = dl->to;
    box_audio_delay_tap(core, dl, &dl->to, time);
    dl->crossfade = true;
    box_audio_ramp_set(&dl->xfade, 0);
    box_audio_ramp_to(&dl->xfade, 1,
        (int32_t)(BOX_AUDIO_DELAY_CROSSFADE * core->sample_rate));
}

/** Jump to the pending time, without a crossfade, for a delay that is silent.
 *
 * @param core The core
 * @param dl The delay
 */
static void box_audio_delay_settle(
    const box_audio_core_t *core,
    box_audio_delay_t *dl)
{
    box_audio_delay_tap(core, dl, &dl->to, dl->pending);
    dl->from = dl->to;
    dl->crossfade = false;
    box_audio_ramp_set(&dl->xfade, 1);
}

void box_audio_delay_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_delay_t *dl = &fx->data.delay;
    box_audio_delay_target(core, fx);
    dl->length = box_audio_delay_length(core->sample_rate);
    dl->line[0] = fx->memory;
    dl->line[1] = fx->memory + dl->length;
    dl->pos = 0;
    dl->valid = 0;
    dl->lp[0] = 0;
    dl->lp[1] = 0;
    dl->still = 0;
    dl->pending = dl->target.time;
    box_audio_delay_settle(core, dl);
    box_audio_ramp_set(&dl->feedback, dl->target.feedback);
    box_audio_ramp_set(&dl->pong, dl->target.ping_pong);
    box_audio_ramp_set(&dl->mix, dl->target.mix);
    box_audio_ramp_set(&dl->feed, 1);
    if (fade_in) {
        box_audio_ramp_set(&dl->feed, 0);
        box_audio_ramp_to(&dl->feed, 1,
            (int32_t)(BOX_AUDIO_EFFECT_FADE * 0.5f * core->sample_rate));
    }
}

void box_audio_delay_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_delay_t *dl = &fx->data.delay;
    int32_t ramp = (int32_t)(BOX_AUDIO_DELAY_RAMP * core->sample_rate);
    box_audio_delay_target(core, fx);
    box_audio_ramp_to(&dl->feedback, dl->target.feedback, ramp);
    box_audio_ramp_to(&dl->pong, dl->target.ping_pong, ramp);
    box_audio_ramp_to(&dl->mix, dl->target.mix, ramp);
    dl->pending = dl->target.time;
    if (fx->idle) {
        box_audio_delay_settle(core, dl);
    } else if (!dl->crossfade && dl->pending != dl->to.time) {
        box_audio_delay_cross(core, dl, dl->pending);
    }
}

/** Read a line at a read position, with linear interpolation; frames older than the ones
 * written since the line was silent read as silence.
 *
 * @param dl The delay
 * @param line The line
 * @param tap The read position
 * @return The sample
 */
static inline float box_audio_delay_read(
    const box_audio_delay_t *dl,
    const float *line,
    const box_audio_delay_tap_t *tap)
{
    int32_t i0 = dl->pos - tap->frames, i1 = i0 - 1;
    float a, b;
    i0 += i0 < 0 ? dl->length : 0;
    i1 += i1 < 0 ? dl->length : 0;
    a = tap->frames <= dl->valid ? line[i0] : 0.0f;
    b = tap->frames < dl->valid ? line[i1] : 0.0f;

    return a + tap->frac * (b - a);
}

/** Run the delay over a block.
 *
 * @param core The core
 * @param dl The delay
 * @param l Left samples, the input and the output
 * @param r Right samples
 * @param frames Frames of the block
 * @return Frames at the end of the block in which the repeats summed under
 * BOX_AUDIO_DELAY_SILENCE
 */
static int32_t box_audio_delay_block(
    const box_audio_core_t *core,
    box_audio_delay_t *dl,
    float *l,
    float *r,
    int32_t frames)
{
    float *bl = dl->line[0], *br = dl->line[1];
    float damp = dl->damp;
    int32_t i, still = 0;
    for (i = 0; i < frames; i ++) {
        float feed = box_audio_ramp_next(&dl->feed);
        float xl = l[i] * feed, xr = r[i] * feed;
        float yl = box_audio_delay_read(dl, bl, &dl->to);
        float yr = box_audio_delay_read(dl, br, &dl->to);
        float fb, pong, wl, wr, pl, pr, wet, dry, sum;
        if (dl->crossfade) {
            float g = box_audio_ramp_next(&dl->xfade);
            float ol = box_audio_delay_read(dl, bl, &dl->from);
            float orr = box_audio_delay_read(dl, br, &dl->from);
            yl = ol + g * (yl - ol);
            yr = orr + g * (yr - orr);
            if (dl->xfade.left == 0) {
                dl->crossfade = false;
                dl->from = dl->to;
                if (dl->pending != dl->to.time) {
                    box_audio_delay_cross(core, dl, dl->pending);
                }
            }
        }

        dl->lp[0] = box_audio_clean(dl->lp[0] + damp * (yl - dl->lp[0]));
        dl->lp[1] = box_audio_clean(dl->lp[1] + damp * (yr - dl->lp[1]));
        fb = box_audio_ramp_next(&dl->feedback);
        pong = box_audio_ramp_next(&dl->pong);
        wl = xl + fb * dl->lp[0];
        wr = xr + fb * dl->lp[1];
        pl = (xl + xr) * 0.5f + fb * dl->lp[1];
        pr = fb * dl->lp[0];
        bl[dl->pos] = box_audio_clean(wl + pong * (pl - wl));
        br[dl->pos] = box_audio_clean(wr + pong * (pr - wr));
        dl->pos = dl->pos + 1 < dl->length ? dl->pos + 1 : 0;
        dl->valid = dl->valid < dl->length ? dl->valid + 1 : dl->valid;
        sum = (yl < 0 ? -yl : yl) + (yr < 0 ? -yr : yr);
        still = sum < BOX_AUDIO_DELAY_SILENCE ? still + 1 : 0;
        wet = box_audio_ramp_next(&dl->mix);
        dry = 1 - wet;
        l[i] = l[i] * dry + yl * wet;
        r[i] = r[i] * dry + yr * wet;
    }

    return still;
}

bool box_audio_delay_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_delay_t *dl = &fx->data.delay;
    int32_t still, longest;
    bool result = false;
    still = box_audio_delay_block(core, dl, l, r, frames);
    dl->still = still < frames ? still : (dl->still < (1 << 30) ? dl->still + frames : dl->still);
    longest = dl->to.frames + 2;
    if (dl->crossfade && dl->from.frames + 2 > longest) {
        longest = dl->from.frames + 2;
    }

    if (silent && fx->quiet >= longest && dl->still >= longest) {
        dl->valid = 0;
        dl->lp[0] = 0;
        dl->lp[1] = 0;
        dl->still = 0;
        box_audio_delay_settle(core, dl);
        result = true;
    }

    return result;
}

void box_audio_delay_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    (void)frames;
    box_audio_delay_t *dl = &fx->data.delay;
    if (dl->crossfade || dl->pending != dl->to.time) {
        box_audio_delay_settle(core, dl);
    }

    box_audio_ramp_set(&dl->feedback, dl->feedback.target);
    box_audio_ramp_set(&dl->pong, dl->pong.target);
    box_audio_ramp_set(&dl->mix, dl->mix.target);
    box_audio_ramp_set(&dl->feed, dl->feed.target);
}
