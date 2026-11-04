/**
 * @file audio/core/phaser.c
 * @brief The phaser kind of effect: a chain of first order allpasses per channel, swept by a
 * sine LFO around a center frequency, with feedback and a mix.
 *
 * n allpasses turn the phase from 0 at DC to n half turns at Nyquist, so with the dry signal
 * they cancel at n / 2 notches. The LFO moves their break frequency exponentially every
 * BOX_AUDIO_MODULATION_CONTROL frames. Its params are a box_audio_phaser_params_t.
 *
 * The phase of the LFO is a fixed point count of turns that advances by a whole step per frame,
 * the same whatever the render sizes and over the skipped blocks of an idle phaser; a change of
 * the rate changes the step, not the phase.
 */

#include "internal.h"

/** States that sum to less than this, -140 dB, have decayed to silence */
#define BOX_AUDIO_PHASER_SILENCE 1e-7f

/** Lowest break frequency of a sweep in Hz */
#define BOX_AUDIO_PHASER_SWEEP_MIN 10.0

/** Highest break frequency of a sweep as a share of the rate */
#define BOX_AUDIO_PHASER_SWEEP_MAX 0.45

/** Turns per unit of the phase of the LFO */
#define BOX_AUDIO_PHASER_TURN (1.0 / 4294967296.0)

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_phaser_clamp(
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

/** Clamp the params of an effect into the target of its phaser; NaN becomes the low end. Sets
 * the step of the LFO, so a change of the rate keeps its phase.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_phaser_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_phaser_t *ph = &fx->data.phaser;
    const float *p = fx->params;
    float stages = box_audio_phaser_clamp(p[0], BOX_AUDIO_PHASER_STAGES_MIN,
        BOX_AUDIO_PHASER_STAGES_MAX);
    int32_t even = 2 * (int32_t)(stages * 0.5f + 0.5f);
    if (even > BOX_AUDIO_PHASER_STAGES_MAX) {
        even = BOX_AUDIO_PHASER_STAGES_MAX;
    }

    ph->target.stages = (float)even;
    ph->target.rate = box_audio_phaser_clamp(p[1], 0, BOX_AUDIO_MODULATION_RATE_MAX);
    ph->target.depth = box_audio_phaser_clamp(p[2], 0, 1);
    ph->target.center = box_audio_phaser_clamp(p[3], BOX_AUDIO_PHASER_CENTER_MIN,
        BOX_AUDIO_PHASER_CENTER_MAX);
    ph->target.feedback = box_audio_phaser_clamp(p[4], -BOX_AUDIO_PHASER_FEEDBACK_MAX,
        BOX_AUDIO_PHASER_FEEDBACK_MAX);
    ph->target.mix = box_audio_phaser_clamp(p[5], 0, 1);
    ph->target.stereo = box_audio_phaser_clamp(p[6], 0, 1);
    ph->step = (uint32_t)((double)ph->target.rate / (double)core->sample_rate * 4294967296.0);
}

/** Set the coefficients of both channels for a phase of the LFO, at the depth, center and
 * stereo offset that run: (tan(w / 2) - 1) / (tan(w / 2) + 1) of the swept frequency w, its
 * tangent a sine over a cosine in turns.
 *
 * @param core The core
 * @param ph The phaser
 * @param phase Phase of the LFO in 2^-32 turns
 */
static void box_audio_phaser_coefs(
    const box_audio_core_t *core,
    box_audio_phaser_t *ph,
    uint32_t phase)
{
    double rate = (double)core->sample_rate, turns = (double)phase * BOX_AUDIO_PHASER_TURN;
    double sweep = (double)(BOX_AUDIO_PHASER_OCTAVES * ph->depth.value);
    double hi = BOX_AUDIO_PHASER_SWEEP_MAX * rate;
    int32_t c;
    for (c = 0; c < 2; c ++) {
        double offset = c ? (double)ph->stereo.value * 0.5 : 0.0;
        double lfo = (double)box_audio_core_sin(turns + offset);
        double hz = box_audio_synth_exp2((double)ph->octave.value + sweep * lfo);
        double x, s, k;
        if (!(hz >= BOX_AUDIO_PHASER_SWEEP_MIN)) {
            hz = BOX_AUDIO_PHASER_SWEEP_MIN;
        } else if (hz > hi) {
            hz = hi;
        }

        x = hz / (2.0 * rate);
        s = (double)box_audio_core_sin(x);
        k = (double)box_audio_core_sin(x + 0.25);
        ph->coef[c] = (float)((s - k) / (s + k));
    }
}

/** Clear the chain: every allpass and the feedback.
 *
 * @param ph The phaser
 */
static void box_audio_phaser_clear(
    box_audio_phaser_t *ph)
{
    int32_t c, k;
    for (c = 0; c < 2; c ++) {
        for (k = 0; k < BOX_AUDIO_PHASER_STAGES_MAX; k ++) {
            ph->state[c][k] = 0;
        }

        ph->last[c] = 0;
    }
}

/** Apply the stages of the target on a clear chain.
 *
 * @param ph The phaser
 */
static void box_audio_phaser_settle(
    box_audio_phaser_t *ph)
{
    ph->stages = (int32_t)ph->target.stages;
    ph->swap = false;
    box_audio_phaser_clear(ph);
    box_audio_ramp_set(&ph->fade, 1);
}

/** Get the control blocks of a ramp of BOX_AUDIO_MODULATION_RAMP.
 *
 * @param core The core
 * @return Control blocks
 */
static int32_t box_audio_phaser_controls(
    const box_audio_core_t *core)
{
    return (int32_t)(BOX_AUDIO_MODULATION_RAMP * core->sample_rate) /
        BOX_AUDIO_MODULATION_CONTROL;
}

void box_audio_phaser_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_phaser_t *ph = &fx->data.phaser;
    (void)fade_in;
    box_audio_phaser_target(core, fx);
    box_audio_phaser_settle(ph);
    ph->phase = 0;
    ph->control = 0;
    ph->stale = false;
    box_audio_ramp_set(&ph->depth, ph->target.depth);
    box_audio_ramp_set(&ph->octave, (float)box_audio_synth_log2((double)ph->target.center));
    box_audio_ramp_set(&ph->stereo, ph->target.stereo);
    box_audio_ramp_set(&ph->feedback, ph->target.feedback);
    box_audio_ramp_set(&ph->mix, ph->target.mix);
    box_audio_phaser_coefs(core, ph, 0);
}

void box_audio_phaser_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_phaser_t *ph = &fx->data.phaser;
    int32_t ramp = (int32_t)(BOX_AUDIO_MODULATION_RAMP * core->sample_rate);
    int32_t controls = box_audio_phaser_controls(core);
    box_audio_phaser_target(core, fx);
    box_audio_ramp_to(&ph->depth, ph->target.depth, controls);
    box_audio_ramp_to(&ph->octave, (float)box_audio_synth_log2((double)ph->target.center),
        controls);
    box_audio_ramp_to(&ph->stereo, ph->target.stereo, controls);
    box_audio_ramp_to(&ph->feedback, ph->target.feedback, ramp);
    box_audio_ramp_to(&ph->mix, ph->target.mix, ramp);
    if ((int32_t)ph->target.stages != ph->stages) {
        if (fx->idle) {
            box_audio_phaser_settle(ph);
        } else if (!ph->swap) {
            ph->swap = true;
            box_audio_ramp_to(&ph->fade, 0, ramp);
        }
    }
}

/** Run the phaser over a block.
 *
 * @param core The core
 * @param ph The phaser
 * @param l Left samples, the input and the output
 * @param r Right samples
 * @param frames Frames of the block
 */
static void box_audio_phaser_block(
    const box_audio_core_t *core,
    box_audio_phaser_t *ph,
    float *l,
    float *r,
    int32_t frames)
{
    float *io[2] = { l, r };
    int32_t i, c, k;
    for (i = 0; i < frames; i ++) {
        float fb, wet, fade;
        if (ph->control == 0) {
            box_audio_ramp_next(&ph->depth);
            box_audio_ramp_next(&ph->octave);
            box_audio_ramp_next(&ph->stereo);
            box_audio_phaser_coefs(core, ph, ph->phase);
            ph->control = BOX_AUDIO_MODULATION_CONTROL;
        }

        ph->control --;
        fb = box_audio_ramp_next(&ph->feedback);
        wet = box_audio_ramp_next(&ph->mix);
        fade = box_audio_ramp_next(&ph->fade);
        for (c = 0; c < 2; c ++) {
            float a = ph->coef[c], x = io[c][i], v = x + fb * ph->last[c];
            float *s = ph->state[c];
            for (k = 0; k < ph->stages; k ++) {
                float y = a * v + s[k];
                s[k] = box_audio_clean(v - a * y);
                v = y;
            }

            v = box_audio_clean(v);
            ph->last[c] = v;
            io[c][i] = x * (1 - wet) + v * (wet * fade);
        }

        ph->phase += ph->step;
        if (ph->swap && ph->fade.left == 0 && ph->fade.value == 0) {
            int32_t ramp = (int32_t)(BOX_AUDIO_MODULATION_RAMP * core->sample_rate);
            box_audio_phaser_settle(ph);
            box_audio_ramp_set(&ph->fade, 0);
            box_audio_ramp_to(&ph->fade, 1, ramp);
        }
    }
}

bool box_audio_phaser_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_phaser_t *ph = &fx->data.phaser;
    bool result = false;
    float sum = 0;
    int32_t c, k;
    if (ph->stale) {
        uint32_t back = (uint32_t)(BOX_AUDIO_MODULATION_CONTROL - ph->control);
        ph->stale = false;
        if (ph->control) {
            box_audio_phaser_coefs(core, ph, ph->phase - back * ph->step);
        }
    }

    box_audio_phaser_block(core, ph, l, r, frames);
    if (silent) {
        for (c = 0; c < 2; c ++) {
            sum += ph->last[c] < 0 ? -ph->last[c] : ph->last[c];
            for (k = 0; k < ph->stages; k ++) {
                float s = ph->state[c][k];
                sum += s < 0 ? -s : s;
            }
        }

        if (sum < BOX_AUDIO_PHASER_SILENCE) {
            if (ph->swap) {
                box_audio_phaser_settle(ph);
            }

            box_audio_phaser_clear(ph);
            result = true;
        }
    }

    return result;
}

void box_audio_phaser_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_phaser_t *ph = &fx->data.phaser;
    (void)core;
    if (ph->swap || ph->stages != (int32_t)ph->target.stages) {
        box_audio_phaser_settle(ph);
    }

    ph->phase += (uint32_t)frames * ph->step;
    ph->control = ((ph->control - frames) % BOX_AUDIO_MODULATION_CONTROL +
        BOX_AUDIO_MODULATION_CONTROL) % BOX_AUDIO_MODULATION_CONTROL;
    ph->stale = true;
    box_audio_ramp_set(&ph->depth, ph->depth.target);
    box_audio_ramp_set(&ph->octave, ph->octave.target);
    box_audio_ramp_set(&ph->stereo, ph->stereo.target);
    box_audio_ramp_set(&ph->feedback, ph->feedback.target);
    box_audio_ramp_set(&ph->mix, ph->mix.target);
}
