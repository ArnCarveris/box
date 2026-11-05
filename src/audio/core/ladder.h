/**
 * @file audio/core/ladder.h
 * @brief The transistor ladder of the core: the 4-pole filter of the synth for
 * BOX_AUDIO_SYNTH_LADDER.
 *
 * Four one-pole stages, each with the tanh of its input and of its output as a transistor
 * ladder has them, inside a feedback loop with a tanh of its own, in the style of
 * Huovilainen. It is solved with zero delay feedback, after the cheap non-linear filters of
 * Mystran: the slope tanh(v) / v of each nonlinearity is taken from the state of its stage,
 * which makes the frame a linear system that is solved at once. Every stage is integrated with
 * the trapezoidal rule from g = tan(pi cutoff / rate), so the ladder is in tune up to the
 * clamp of the cutoff under Nyquist without oversampling, and the slopes never exceed 1, so it
 * stays bounded at any cutoff while it moves.
 *
 * The feedback is BOX_AUDIO_LADDER_K times the resonance, with BOX_AUDIO_LADDER_EXCESS more at
 * the top, so it passes 4, where the linear ladder oscillates, close to resonance 1; the tanh
 * of the loop holds the oscillation at a steady level, a near sine at the cutoff. The lowpass
 * takes a share BOX_AUDIO_LADDER_BASS of the input into the feedback sum, which gives back
 * most of the passband a ladder loses as its resonance rises. The other kinds mix the outputs
 * of the stages, as a multimode ladder does. Private to src/audio/core; included by
 * internal.h.
 */

#ifndef BOX_AUDIO_CORE_LADDER_H
#define BOX_AUDIO_CORE_LADDER_H

#include "core.h"

/** Feedback of the ladder at resonance 1 without the excess: the linear ladder oscillates at
 * a feedback of 4 */
#define BOX_AUDIO_LADDER_K 4.0f

/** Feedback over BOX_AUDIO_LADDER_K at resonance 1, which comes in as resonance^16, so the
 * ladder oscillates from a resonance of about 0.97 */
#define BOX_AUDIO_LADDER_EXCESS 0.3f

/** Share of the input in the feedback sum of the lowpass: its passband gain is
 * (1 + share k) / (1 + k) at a feedback of k, against 1 / (1 + k) without */
#define BOX_AUDIO_LADDER_BASS 0.75f

/** Gain into the ladder at drive 0: a signal of 1 / this starts to saturate */
#define BOX_AUDIO_LADDER_GAIN_MIN 0.5f

/** Gain into the ladder at drive 1 */
#define BOX_AUDIO_LADDER_GAIN_MAX 4.0f

/** Outputs of the ladder, from the kind and the slope */
typedef enum box_audio_ladder_tap_t {
    BOX_AUDIO_LADDER_LP2 = 0,  /**< Lowpass, the second stage */
    BOX_AUDIO_LADDER_LP4 = 1,  /**< Lowpass, the fourth stage */
    BOX_AUDIO_LADDER_HP2 = 2,  /**< Highpass of 2 poles */
    BOX_AUDIO_LADDER_HP4 = 3,  /**< Highpass of 4 poles */
    BOX_AUDIO_LADDER_BP2 = 4,  /**< Bandpass of 2 poles */
    BOX_AUDIO_LADDER_BP4 = 5,  /**< Bandpass of 4 poles */
    BOX_AUDIO_LADDER_NOTCH2 = 6, /**< Notch of 2 poles */
    BOX_AUDIO_LADDER_NOTCH4 = 7  /**< Notch of 4 poles, two notches of 2 poles in a row */
} box_audio_ladder_tap_t;

/** The coefficients of a ladder, which both channels share */
typedef struct box_audio_ladder_coefs_t {
    float g;              /**< tan(pi cutoff / rate) */
    float k;              /**< Feedback */
    float comp;           /**< Factor of the input in the feedback sum: 1 + share k for the
                           * lowpass, 1 for the other kinds */
    float gain;           /**< Gain into the ladder, from the drive */
    float out;            /**< Gain out of the ladder, 1 / gain */
    int32_t tap;          /**< A box_audio_ladder_tap_t */
} box_audio_ladder_coefs_t;

/** The state of a ladder, per channel */
typedef struct box_audio_ladder_t {
    float s[4];           /**< States of the trapezoidal integrators of the stages */
    float x1;             /**< Last input, for the estimate of the nonlinearity of the loop */
} box_audio_ladder_t;

/** Set the coefficients of a ladder but its g, and clear the state of its channels.
 *
 * @param c The coefficients
 * @param l The two channels
 * @param filter A box_audio_synth_filter_t, not BOX_AUDIO_SYNTH_FILTER_NONE
 * @param slope A box_audio_synth_slope_t
 * @param resonance 0..1
 * @param drive 0..1
 */
static inline void box_audio_ladder_init(
    box_audio_ladder_coefs_t *c,
    box_audio_ladder_t *l,
    int32_t filter,
    uint32_t slope,
    float resonance,
    float drive)
{
    float r2 = resonance * resonance, r4 = r2 * r2, r8 = r4 * r4;
    int32_t poles4 = slope == BOX_AUDIO_SYNTH_SLOPE_24;
    c->g = 0;
    c->k = BOX_AUDIO_LADDER_K * resonance + BOX_AUDIO_LADDER_EXCESS * r8 * r8;
    c->comp = 1;
    if (filter == BOX_AUDIO_SYNTH_LOWPASS) {
        c->comp = 1 + BOX_AUDIO_LADDER_BASS * c->k;
        c->tap = poles4 ? BOX_AUDIO_LADDER_LP4 : BOX_AUDIO_LADDER_LP2;
    } else if (filter == BOX_AUDIO_SYNTH_HIGHPASS) {
        c->tap = poles4 ? BOX_AUDIO_LADDER_HP4 : BOX_AUDIO_LADDER_HP2;
    } else if (filter == BOX_AUDIO_SYNTH_BANDPASS) {
        c->tap = poles4 ? BOX_AUDIO_LADDER_BP4 : BOX_AUDIO_LADDER_BP2;
    } else {
        c->tap = poles4 ? BOX_AUDIO_LADDER_NOTCH4 : BOX_AUDIO_LADDER_NOTCH2;
    }

    c->gain = BOX_AUDIO_LADDER_GAIN_MIN +
        (BOX_AUDIO_LADDER_GAIN_MAX - BOX_AUDIO_LADDER_GAIN_MIN) * drive;
    c->out = 1.0f / c->gain;
    l[0] = (box_audio_ladder_t){0};
    l[1] = (box_audio_ladder_t){0};
}

/** The slope tanh(v) / v of a value, from the rational tanh of box_audio_svf_clip, which is
 * 1 past 3: 1 at 0, falling to 0 as |v| grows, never above 1. Without a division by v, so 0 is
 * safe.
 *
 * @param v The value
 * @return tanh(v) / v
 */
static inline float box_audio_ladder_slope(
    float v)
{
    float a = v < 0 ? -v : v, z = a < 3.0f ? a : 3.0f, zz = z * z;
    float over = a > 3.0f ? 3.0f / a : 1.0f;

    return over * (27.0f + zz) / (27.0f + 9.0f * zz);
}

/** Filter a frame through a ladder.
 *
 * @param l The state of the channel
 * @param c The coefficients
 * @param x The input
 * @return The output
 */
static inline float box_audio_ladder_next(
    box_audio_ladder_t *l,
    const box_audio_ladder_coefs_t *c,
    float x)
{
    float f = c->g, k = c->k, in = x * c->gain * c->comp, half = 0.5f * (in + l->x1);
    float t_in, t0, t1, t2, t3, g0, g1, g2, g3, a0, a1, a2, a3, s, p, u, v0, v1, v2, v3, y;
    l->x1 = in;

    t_in = box_audio_ladder_slope(half - k * l->s[3]);
    t0 = box_audio_ladder_slope(l->s[0]);
    t1 = box_audio_ladder_slope(l->s[1]);
    t2 = box_audio_ladder_slope(l->s[2]);
    t3 = box_audio_ladder_slope(l->s[3]);

    g0 = 1.0f / (1.0f + f * t0);
    g1 = 1.0f / (1.0f + f * t1);
    g2 = 1.0f / (1.0f + f * t2);
    g3 = 1.0f / (1.0f + f * t3);
    a0 = g0 * f * t_in;
    a1 = g1 * f * t0;
    a2 = g2 * f * t1;
    a3 = g3 * f * t2;

    s = g3 * l->s[3] + a3 * (g2 * l->s[2] + a2 * (g1 * l->s[1] + a1 * g0 * l->s[0]));
    p = a3 * a2 * a1 * a0;
    v3 = (s + p * in) / (1.0f + k * p);
    u = t_in * (in - k * v3);
    v0 = g0 * (l->s[0] + f * u);
    v1 = g1 * (l->s[1] + f * t0 * v0);
    v2 = g2 * (l->s[2] + f * t1 * v1);
    l->s[0] = box_audio_clean(2 * v0 - l->s[0]);
    l->s[1] = box_audio_clean(2 * v1 - l->s[1]);
    l->s[2] = box_audio_clean(2 * v2 - l->s[2]);
    l->s[3] = box_audio_clean(2 * v3 - l->s[3]);

    switch (c->tap) {
    case BOX_AUDIO_LADDER_LP2:
        y = v1;
        break;
    case BOX_AUDIO_LADDER_LP4:
        y = v3;
        break;
    case BOX_AUDIO_LADDER_HP2:
        y = u - 2 * v0 + v1;
        break;
    case BOX_AUDIO_LADDER_HP4:
        y = u - 4 * v0 + 6 * v1 - 4 * v2 + v3;
        break;
    case BOX_AUDIO_LADDER_BP2:
        y = 2 * (v0 - v1);
        break;
    case BOX_AUDIO_LADDER_BP4:
        y = 4 * (v1 - 2 * v2 + v3);
        break;
    case BOX_AUDIO_LADDER_NOTCH2:
        y = u - 2 * v0 + 2 * v1;
        break;
    default:
        y = u - 4 * v0 + 8 * v1 - 8 * v2 + 4 * v3;
        break;
    }

    return y * c->out;
}

#endif
