/**
 * @file audio/core/filter.c
 * @brief The filter kind of effect: the trapezoidal state variable filter of the synth
 * (svf.h) on both channels of a bus, a lowpass, a highpass, a bandpass or a notch of 12 or 24
 * dB per octave, with a dry and wet mix.
 *
 * Its params are a box_audio_filter_params_t. The cutoff moves exponentially in frequency
 * toward a new value, and the resonance toward its own, with new coefficients every
 * BOX_AUDIO_FILTER_CONTROL frames, as the filter envelope of the synth does.
 *
 * The SVF is stable at any cutoff and resonance and while they move. A change of the kind or
 * the slope fades the wet signal out, swaps and fades it back in. The drive moves over
 * BOX_AUDIO_FILTER_RAMP, as the mix does.
 */

#include "internal.h"

/** Frames between two updates of the coefficients while the cutoff or the resonance moves */
#define BOX_AUDIO_FILTER_CONTROL 8

/** Largest share of the rate of a cutoff */
#define BOX_AUDIO_FILTER_NYQUIST 0.49f

/** The cutoff and the resonance snap to their targets once this close, in octaves and in
 * resonance */
#define BOX_AUDIO_FILTER_SNAP 1e-5

/** 1 / ln(2) */
#define BOX_AUDIO_FILTER_LOG2E 1.4426950408889634

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_filter_clamp(
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

/** Clamp the params of an effect into the target of its filter; NaN becomes the low end.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_filter_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_filter_t *f = &fx->data.filter;
    const float *p = fx->params;
    float kind = box_audio_filter_clamp(p[0], BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_NOTCH);
    f->target.kind = (float)(int32_t)kind;
    f->target.cutoff = box_audio_filter_clamp(p[1], BOX_AUDIO_FILTER_CUTOFF_MIN,
        BOX_AUDIO_FILTER_NYQUIST * core->sample_rate);
    f->target.resonance = box_audio_filter_clamp(p[2], 0, 1);
    f->target.slope = p[3] >= 0.5f ? (float)BOX_AUDIO_SYNTH_SLOPE_24 :
        (float)BOX_AUDIO_SYNTH_SLOPE_12;
    f->target.mix = box_audio_filter_clamp(p[4], 0, 1);
    f->target.drive = box_audio_filter_clamp(p[5], 0, 1);
    f->octave_target = box_audio_synth_log2((double)f->target.cutoff);
}

/** Set the coefficients of the stages from the cutoff and the resonance that run.
 *
 * @param core The core
 * @param f The filter
 */
static void box_audio_filter_coefs(
    const box_audio_core_t *core,
    box_audio_filter_t *f)
{
    double cutoff = box_audio_synth_exp2(f->octave);
    float g = box_audio_svf_g(cutoff, core->sample_rate);
    int32_t s;
    box_audio_svf_damping(f->svf, f->slope, f->resonance);
    box_audio_svf_damping(f->svf_r, f->slope, f->resonance);
    for (s = 0; s < f->stages; s ++) {
        box_audio_svf_coefs(&f->svf[s], g);
        f->svf_r[s].a1 = f->svf[s].a1;
        f->svf_r[s].a2 = f->svf[s].a2;
        f->svf_r[s].a3 = f->svf[s].a3;
    }
}

/** Apply the kind and the slope of the target on clear stages.
 *
 * @param core The core
 * @param f The filter
 */
static void box_audio_filter_apply(
    const box_audio_core_t *core,
    box_audio_filter_t *f)
{
    f->kind = (int32_t)f->target.kind;
    f->slope = (uint32_t)f->target.slope;
    f->stages = box_audio_svf_init(f->svf, f->slope, f->resonance);
    box_audio_svf_init(f->svf_r, f->slope, f->resonance);
    f->swap = false;
    box_audio_filter_coefs(core, f);
}

void box_audio_filter_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_filter_t *f = &fx->data.filter;
    (void)fade_in;
    box_audio_filter_target(core, fx);
    f->octave = f->octave_target;
    f->resonance = f->target.resonance;
    f->smooth = (float)(1 - box_audio_synth_exp2(-BOX_AUDIO_FILTER_LOG2E *
        (double)BOX_AUDIO_FILTER_CONTROL /
        ((double)BOX_AUDIO_FILTER_SMOOTH * (double)core->sample_rate)));
    f->control = BOX_AUDIO_FILTER_CONTROL;
    f->moving = false;
    box_audio_filter_apply(core, f);
    box_audio_ramp_set(&f->mix, f->target.mix);
    box_audio_ramp_set(&f->drive, f->target.drive);
    box_audio_ramp_set(&f->fade, 1);
}

void box_audio_filter_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_filter_t *f = &fx->data.filter;
    int32_t ramp = (int32_t)(BOX_AUDIO_FILTER_RAMP * core->sample_rate);
    box_audio_filter_target(core, fx);
    f->moving = f->moving || f->octave != f->octave_target ||
        f->resonance != f->target.resonance;
    box_audio_ramp_to(&f->mix, f->target.mix, ramp);
    box_audio_ramp_to(&f->drive, f->target.drive, ramp);
    if ((int32_t)f->target.kind != f->kind || (uint32_t)f->target.slope != f->slope) {
        f->swap = true;
        box_audio_ramp_to(&f->fade, 0, ramp);
    } else if (f->swap) {
        f->swap = false;
        box_audio_ramp_to(&f->fade, 1, ramp);
    }
}

/** Move the cutoff and the resonance a step toward their targets, for a control block.
 *
 * @param core The core
 * @param f The filter
 */
static void box_audio_filter_smooth(
    const box_audio_core_t *core,
    box_audio_filter_t *f)
{
    double d = f->octave_target - f->octave;
    float e = f->target.resonance - f->resonance;
    bool near = d < BOX_AUDIO_FILTER_SNAP && d > -BOX_AUDIO_FILTER_SNAP;
    bool close = e < (float)BOX_AUDIO_FILTER_SNAP && e > -(float)BOX_AUDIO_FILTER_SNAP;
    f->octave = near ? f->octave_target : f->octave + d * (double)f->smooth;
    f->resonance = close ? f->target.resonance : f->resonance + e * f->smooth;
    f->moving = !(near && close);
    box_audio_filter_coefs(core, f);
}

/** Filter a frame of a channel through the stages.
 *
 * @param f The filter
 * @param svf The stages of the channel
 * @param x The input
 * @param drive 0..1
 * @return The output
 */
static inline float box_audio_filter_next(
    const box_audio_filter_t *f,
    box_audio_svf_t *svf,
    float x,
    float drive)
{
    if (f->stages > 1) {
        x = box_audio_svf_drive(&svf[1], f->kind, x, drive);
    }

    return box_audio_svf_drive(&svf[0], f->kind, x, drive);
}

bool box_audio_filter_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_filter_t *f = &fx->data.filter;
    int32_t i;
    (void)silent;
    for (i = 0; i < frames; i ++) {
        float a = l[i], b = r[i], wet, drive;
        if (f->moving && -- f->control <= 0) {
            f->control = BOX_AUDIO_FILTER_CONTROL;
            box_audio_filter_smooth(core, f);
        }

        wet = box_audio_ramp_next(&f->mix) * box_audio_ramp_next(&f->fade);
        drive = box_audio_ramp_next(&f->drive);
        l[i] = a + (box_audio_filter_next(f, f->svf, a, drive) - a) * wet;
        r[i] = b + (box_audio_filter_next(f, f->svf_r, b, drive) - b) * wet;
        if (f->swap && f->fade.left == 0 && f->fade.value == 0) {
            box_audio_filter_apply(core, f);
            box_audio_ramp_to(&f->fade, 1, (int32_t)(BOX_AUDIO_FILTER_RAMP * core->sample_rate));
        }
    }

    return false;
}

void box_audio_filter_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    (void)frames;
    (void)core;
    (void)fx;
}
