/**
 * @file audio/core/eq.c
 * @brief The equalizer kind of effect: BOX_AUDIO_EQ_BANDS bands in a row, each an RBJ cookbook
 * biquad (a peak, a shelf or a cut of 12 dB per octave) in transposed direct form II, one per
 * channel.
 *
 * A change of a band moves its coefficients linearly over BOX_AUDIO_EQ_RAMP. A biquad is
 * stable when a1 and a2 lie in the triangle |a2| < 1, |a1| < 1 + a2, which is convex, so every
 * set on the way between two stable ones is stable too.
 *
 * A band that is off or flat has the coefficients of a wire, and once its state drained it is
 * skipped: an equalizer whose bands are all flat passes its input bit for bit.
 */

#include "internal.h"

/** log2(10) / 40: the exponent of 2 of the amplitude A of a gain in dB, 10 ^ (dB / 40) */
#define BOX_AUDIO_EQ_DB_EXP2 0.08304820237218405

/** Largest share of the rate of a frequency */
#define BOX_AUDIO_EQ_NYQUIST 0.49f

/** A gain under this many dB, either way, is flat */
#define BOX_AUDIO_EQ_FLAT 0.001f

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_eq_clamp(
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

/** Compute the coefficients of a band from its params, clamped; a flat band gets those of a
 * wire.
 *
 * @param core The core
 * @param p The params of the band, as sent
 * @param c Receives b0, b1, b2, a1 and a2
 */
static void box_audio_eq_coefs(
    const box_audio_core_t *core,
    const float *p,
    float *c)
{
    float kind = p[0] >= 0 && p[0] <= BOX_AUDIO_EQ_HIGH_CUT ? p[0] : BOX_AUDIO_EQ_OFF;
    double f = box_audio_eq_clamp(p[1], BOX_AUDIO_EQ_FREQUENCY_MIN,
        BOX_AUDIO_EQ_NYQUIST * core->sample_rate);
    double gain = box_audio_eq_clamp(p[2], -BOX_AUDIO_EQ_GAIN_MAX, BOX_AUDIO_EQ_GAIN_MAX);
    double q = box_audio_eq_clamp(p[3], BOX_AUDIO_EQ_Q_MIN, BOX_AUDIO_EQ_Q_MAX);
    double turns = f / (double)core->sample_rate;
    double cs, sn, alpha, a, sa, b0, b1, b2, a0, a1, a2;
    int32_t k = (int32_t)kind, i;
    bool flat = k == BOX_AUDIO_EQ_OFF || (k <= BOX_AUDIO_EQ_HIGH_SHELF &&
        gain < BOX_AUDIO_EQ_FLAT && gain > -BOX_AUDIO_EQ_FLAT);
    if (flat) {
        c[0] = 1;
        for (i = 1; i < BOX_AUDIO_EQ_COEFS; i ++) {
            c[i] = 0;
        }

        goto done;
    }

    cs = (double)box_audio_core_sin(turns + 0.25);
    sn = (double)box_audio_core_sin(turns);
    alpha = sn / (2 * q);
    a = box_audio_synth_exp2(gain * BOX_AUDIO_EQ_DB_EXP2);
    sa = box_audio_synth_exp2(gain * BOX_AUDIO_EQ_DB_EXP2 * 0.5);
    if (k == BOX_AUDIO_EQ_PEAK) {
        b0 = 1 + alpha * a;
        b1 = -2 * cs;
        b2 = 1 - alpha * a;
        a0 = 1 + alpha / a;
        a1 = -2 * cs;
        a2 = 1 - alpha / a;
    } else if (k == BOX_AUDIO_EQ_LOW_SHELF) {
        b0 = a * ((a + 1) - (a - 1) * cs + 2 * sa * alpha);
        b1 = 2 * a * ((a - 1) - (a + 1) * cs);
        b2 = a * ((a + 1) - (a - 1) * cs - 2 * sa * alpha);
        a0 = (a + 1) + (a - 1) * cs + 2 * sa * alpha;
        a1 = -2 * ((a - 1) + (a + 1) * cs);
        a2 = (a + 1) + (a - 1) * cs - 2 * sa * alpha;
    } else if (k == BOX_AUDIO_EQ_HIGH_SHELF) {
        b0 = a * ((a + 1) + (a - 1) * cs + 2 * sa * alpha);
        b1 = -2 * a * ((a - 1) + (a + 1) * cs);
        b2 = a * ((a + 1) + (a - 1) * cs - 2 * sa * alpha);
        a0 = (a + 1) - (a - 1) * cs + 2 * sa * alpha;
        a1 = 2 * ((a - 1) - (a + 1) * cs);
        a2 = (a + 1) - (a - 1) * cs - 2 * sa * alpha;
    } else if (k == BOX_AUDIO_EQ_LOW_CUT) {
        b0 = (1 + cs) * 0.5;
        b1 = -(1 + cs);
        b2 = (1 + cs) * 0.5;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
    } else {
        b0 = (1 - cs) * 0.5;
        b1 = 1 - cs;
        b2 = (1 - cs) * 0.5;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
    }

    c[0] = (float)(b0 / a0);
    c[1] = (float)(b1 / a0);
    c[2] = (float)(b2 / a0);
    c[3] = (float)(a1 / a0);
    c[4] = (float)(a2 / a0);

done:
    return;
}

/** Check whether coefficients are those of a wire.
 *
 * @param c b0, b1, b2, a1 and a2
 * @return Whether the biquad passes its input
 */
static bool box_audio_eq_wire(
    const float *c)
{
    return c[0] == 1 && c[1] == 0 && c[2] == 0 && c[3] == 0 && c[4] == 0;
}

/** Set the targets of the bands from the params of an effect, and move each band that
 * changed to them over a ramp, or at once.
 *
 * @param core The core
 * @param fx The effect
 * @param ramp Frames of the move, 0 to jump
 */
static void box_audio_eq_target(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t ramp)
{
    box_audio_eq_t *eq = &fx->data.eq;
    int32_t b, i;
    for (b = 0; b < BOX_AUDIO_EQ_BANDS; b ++) {
        box_audio_eq_filter_t *f = &eq->band[b];
        bool same = true;
        box_audio_eq_coefs(core, &fx->params[b * 4], f->to);
        for (i = 0; i < BOX_AUDIO_EQ_COEFS; i ++) {
            same = same && f->c[i] == f->to[i];
        }

        if (same || ramp <= 0) {
            f->left = 0;
            for (i = 0; i < BOX_AUDIO_EQ_COEFS; i ++) {
                f->c[i] = f->to[i];
                f->step[i] = 0;
            }
        } else {
            f->left = ramp;
            for (i = 0; i < BOX_AUDIO_EQ_COEFS; i ++) {
                f->step[i] = (f->to[i] - f->c[i]) / (float)ramp;
            }
        }

        f->active = f->active || f->left > 0 || !box_audio_eq_wire(f->c);
    }
}

void box_audio_eq_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_eq_t *eq = &fx->data.eq;
    int32_t b;
    (void)fade_in;
    for (b = 0; b < BOX_AUDIO_EQ_BANDS; b ++) {
        eq->band[b] = (box_audio_eq_filter_t){ .c = { 1, 0, 0, 0, 0 } };
    }

    box_audio_eq_target(core, fx, 0);
}

void box_audio_eq_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_eq_target(core, fx, (int32_t)(BOX_AUDIO_EQ_RAMP * core->sample_rate));
}

/** Run a band over a channel of a block that it does not move in.
 *
 * @param c Coefficients
 * @param s The two states of the channel
 * @param x Samples, in place
 * @param frames Frames of the block
 */
static void box_audio_eq_still(
    const float *c,
    float *s,
    float *x,
    int32_t frames)
{
    float b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
    float s1 = s[0], s2 = s[1];
    int32_t i;
    for (i = 0; i < frames; i ++) {
        float in = x[i], y = b0 * in + s1;
        s1 = box_audio_clean(b1 * in - a1 * y + s2);
        s2 = box_audio_clean(b2 * in - a2 * y);
        x[i] = y;
    }

    s[0] = s1;
    s[1] = s2;
}

/** Run a band over a block while its coefficients move, frame by frame, so the move ends on
 * the same frame whatever the render sizes.
 *
 * @param f The band
 * @param l Left samples, in place
 * @param r Right samples, in place
 * @param frames Frames of the block
 */
static void box_audio_eq_moving(
    box_audio_eq_filter_t *f,
    float *l,
    float *r,
    int32_t frames)
{
    int32_t i, k;
    for (i = 0; i < frames; i ++) {
        float x, y;
        if (f->left > 0) {
            f->left --;
            for (k = 0; k < BOX_AUDIO_EQ_COEFS; k ++) {
                f->c[k] = f->left ? f->c[k] + f->step[k] : f->to[k];
            }
        }

        x = l[i];
        y = f->c[0] * x + f->s[0][0];
        f->s[0][0] = box_audio_clean(f->c[1] * x - f->c[3] * y + f->s[0][1]);
        f->s[0][1] = box_audio_clean(f->c[2] * x - f->c[4] * y);
        l[i] = y;
        x = r[i];
        y = f->c[0] * x + f->s[1][0];
        f->s[1][0] = box_audio_clean(f->c[1] * x - f->c[3] * y + f->s[1][1]);
        f->s[1][1] = box_audio_clean(f->c[2] * x - f->c[4] * y);
        r[i] = y;
    }
}

bool box_audio_eq_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_eq_t *eq = &fx->data.eq;
    int32_t b;
    (void)core;
    (void)silent;
    for (b = 0; b < BOX_AUDIO_EQ_BANDS; b ++) {
        box_audio_eq_filter_t *f = &eq->band[b];
        if (!f->active) {
            continue;
        }

        if (f->left > 0) {
            box_audio_eq_moving(f, l, r, frames);
        } else {
            box_audio_eq_still(f->c, f->s[0], l, frames);
            box_audio_eq_still(f->c, f->s[1], r, frames);
        }

        f->active = f->left > 0 || !box_audio_eq_wire(f->c) || f->s[0][0] != 0 ||
            f->s[0][1] != 0 || f->s[1][0] != 0 || f->s[1][1] != 0;
    }

    return false;
}

void box_audio_eq_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    (void)frames;
    (void)core;
    (void)fx;
}
