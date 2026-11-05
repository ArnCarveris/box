/**
 * @file audio/core/pitch_shifter.c
 * @brief The pitch shifter kind of effect: a delay line pitch shifter per channel (shift.h),
 * the right channel detuned the other way by the stereo, its output fed back into its line.
 *
 * The lines live on the memory of the slot. The pitch ratios, the window, the feedback and the
 * mix move linearly frame by frame over BOX_AUDIO_PITCH_SHIFTER_RAMP, so a render is the same
 * whatever its sizes. Its params are a box_audio_pitch_shifter_params_t.
 *
 * The feedback is soft clipped (box_audio_shift_clip), so no setting makes it grow without
 * bound. The wet signal comes up to a window late and the dry signal is not delayed.
 */

#include "internal.h"

/** A wet signal whose channels sum to less than this, -140 dB, is silent */
#define BOX_AUDIO_PITCH_SHIFTER_SILENCE 1e-7f

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_pitch_shifter_clamp(
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

/** Get the frames of the line of each channel at a sample rate.
 *
 * @param sample_rate Frames per second
 * @return Frames of a line
 */
static int32_t box_audio_pitch_shifter_length(
    float sample_rate)
{
    return box_audio_shift_length(
        (int32_t)(BOX_AUDIO_PITCH_SHIFTER_WINDOW_MAX * 0.001f * sample_rate) + 1);
}

int32_t box_audio_pitch_shifter_floats(
    float sample_rate)
{
    float rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;

    return box_audio_pitch_shifter_length(rate) * 2;
}

/** Clamp the params of an effect into the target of its pitch shifter; NaN becomes the low
 * end, of the pitch 0.
 *
 * @param fx The effect
 */
static void box_audio_pitch_shifter_target(
    box_audio_effect_t *fx)
{
    box_audio_pitch_shifter_t *ps = &fx->data.pitch_shifter;
    const float *p = fx->params;
    ps->target.semitones = p[0] == p[0] ? box_audio_pitch_shifter_clamp(p[0],
        -BOX_AUDIO_PITCH_SHIFTER_SEMITONES, BOX_AUDIO_PITCH_SHIFTER_SEMITONES) : 0;
    ps->target.cents = p[1] == p[1] ? box_audio_pitch_shifter_clamp(p[1],
        -BOX_AUDIO_PITCH_SHIFTER_CENTS, BOX_AUDIO_PITCH_SHIFTER_CENTS) : 0;
    ps->target.stereo = box_audio_pitch_shifter_clamp(p[2], 0, 1);
    ps->target.window = box_audio_pitch_shifter_clamp(p[3], BOX_AUDIO_PITCH_SHIFTER_WINDOW_MIN,
        BOX_AUDIO_PITCH_SHIFTER_WINDOW_MAX);
    ps->target.feedback = box_audio_pitch_shifter_clamp(p[4], 0,
        BOX_AUDIO_PITCH_SHIFTER_FEEDBACK_MAX);
    ps->target.mix = box_audio_pitch_shifter_clamp(p[5], 0, 1);
}

/** Get the pitch ratio of a channel at the target.
 *
 * @param ps The pitch shifter
 * @param channel 0 for left, 1 for right
 * @return The ratio
 */
static float box_audio_pitch_shifter_ratio(
    const box_audio_pitch_shifter_t *ps,
    int32_t channel)
{
    float cents = ps->target.cents;
    if (channel) {
        cents *= 1 - 2 * ps->target.stereo;
    }

    return (float)box_audio_synth_exp2(
        ((double)ps->target.semitones * 100.0 + (double)cents) / 1200.0);
}

/** Set every ramp to the target at once.
 *
 * @param core The core
 * @param ps The pitch shifter
 */
static void box_audio_pitch_shifter_settle(
    const box_audio_core_t *core,
    box_audio_pitch_shifter_t *ps)
{
    box_audio_ramp_set(&ps->ratio[0], box_audio_pitch_shifter_ratio(ps, 0));
    box_audio_ramp_set(&ps->ratio[1], box_audio_pitch_shifter_ratio(ps, 1));
    box_audio_ramp_set(&ps->window, ps->target.window * 0.001f * core->sample_rate);
    box_audio_ramp_set(&ps->feedback, ps->target.feedback);
    box_audio_ramp_set(&ps->mix, ps->target.mix);
}

void box_audio_pitch_shifter_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_pitch_shifter_t *ps = &fx->data.pitch_shifter;
    int32_t length = box_audio_pitch_shifter_length(core->sample_rate);
    box_audio_pitch_shifter_target(fx);
    box_audio_shift_init(&ps->shift[0], fx->memory, length);
    box_audio_shift_init(&ps->shift[1], fx->memory + length, length);
    ps->back[0] = 0;
    ps->back[1] = 0;
    ps->still = 0;
    box_audio_pitch_shifter_settle(core, ps);
    box_audio_ramp_set(&ps->feed, 1);
    if (fade_in) {
        box_audio_ramp_set(&ps->feed, 0);
        box_audio_ramp_to(&ps->feed, 1,
            (int32_t)(BOX_AUDIO_EFFECT_FADE * 0.5f * core->sample_rate));
    }
}

void box_audio_pitch_shifter_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_pitch_shifter_t *ps = &fx->data.pitch_shifter;
    int32_t ramp = (int32_t)(BOX_AUDIO_PITCH_SHIFTER_RAMP * core->sample_rate);
    box_audio_pitch_shifter_target(fx);
    if (fx->idle) {
        box_audio_pitch_shifter_settle(core, ps);
    } else {
        box_audio_ramp_to(&ps->ratio[0], box_audio_pitch_shifter_ratio(ps, 0), ramp);
        box_audio_ramp_to(&ps->ratio[1], box_audio_pitch_shifter_ratio(ps, 1), ramp);
        box_audio_ramp_to(&ps->window, ps->target.window * 0.001f * core->sample_rate, ramp);
        box_audio_ramp_to(&ps->feedback, ps->target.feedback, ramp);
        box_audio_ramp_to(&ps->mix, ps->target.mix, ramp);
    }
}

/** Run the pitch shifter over a block.
 *
 * @param ps The pitch shifter
 * @param l Left samples, the input and the output
 * @param r Right samples
 * @param frames Frames of the block
 * @return Frames at the end of the block in which the wet signal was silent
 */
static int32_t box_audio_pitch_shifter_block(
    box_audio_pitch_shifter_t *ps,
    float *l,
    float *r,
    int32_t frames)
{
    int32_t i, still = 0;
    for (i = 0; i < frames; i ++) {
        float feed = box_audio_ramp_next(&ps->feed);
        float fb = box_audio_ramp_next(&ps->feedback);
        double window = (double)box_audio_ramp_next(&ps->window);
        double rl = (double)box_audio_ramp_next(&ps->ratio[0]);
        double rr = (double)box_audio_ramp_next(&ps->ratio[1]);
        float wl = box_audio_shift_next(&ps->shift[0], l[i] * feed + fb * ps->back[0], rl,
            window);
        float wr = box_audio_shift_next(&ps->shift[1], r[i] * feed + fb * ps->back[1], rr,
            window);
        float wet = box_audio_ramp_next(&ps->mix), dry = 1 - wet;
        float sum = (wl < 0 ? -wl : wl) + (wr < 0 ? -wr : wr);
        ps->back[0] = box_audio_clean(box_audio_shift_clip(wl));
        ps->back[1] = box_audio_clean(box_audio_shift_clip(wr));
        still = sum < BOX_AUDIO_PITCH_SHIFTER_SILENCE ? still + 1 : 0;
        l[i] = l[i] * dry + wl * wet;
        r[i] = r[i] * dry + wr * wet;
    }

    return still;
}

bool box_audio_pitch_shifter_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_pitch_shifter_t *ps = &fx->data.pitch_shifter;
    int32_t still, length = ps->shift[0].length;
    bool result = false;
    (void)core;
    still = box_audio_pitch_shifter_block(ps, l, r, frames);
    ps->still = still < frames ? still : (ps->still < (1 << 30) ? ps->still + frames : ps->still);
    if (silent && fx->quiet >= length && ps->still >= length) {
        box_audio_shift_clear(&ps->shift[0]);
        box_audio_shift_clear(&ps->shift[1]);
        ps->back[0] = 0;
        ps->back[1] = 0;
        ps->still = 0;
        result = true;
    }

    return result;
}

void box_audio_pitch_shifter_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_pitch_shifter_t *ps = &fx->data.pitch_shifter;
    (void)core;
    (void)frames;
    box_audio_ramp_set(&ps->ratio[0], ps->ratio[0].target);
    box_audio_ramp_set(&ps->ratio[1], ps->ratio[1].target);
    box_audio_ramp_set(&ps->window, ps->window.target);
    box_audio_ramp_set(&ps->feedback, ps->feedback.target);
    box_audio_ramp_set(&ps->mix, ps->mix.target);
    box_audio_ramp_set(&ps->feed, ps->feed.target);
}
