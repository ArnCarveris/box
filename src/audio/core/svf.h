/**
 * @file audio/core/svf.h
 * @brief The trapezoidal state variable filter of the core, shared by the filter of the synth
 * and the filter effect.
 *
 * A stage is the SVF of Simper, integrated with the trapezoidal rule from g = tan(pi cutoff /
 * rate). Each output is a fixed mix of its two states, and the states stay bounded for any
 * positive g and damping, so a stage is stable at any cutoff and resonance and while they
 * move. The 24 dB filter is a cascade of two stages, each stable on its own, so the cascade is
 * too. Private to src/audio/core; included by internal.h.
 *
 * Drive soft clips the input and the bandpass state of a stage, which bounds the resonance
 * peak; the clipper never grows a value, so a driven stage is as stable as a linear one.
 * Drive 0 skips it.
 */

#ifndef BOX_AUDIO_CORE_SVF_H
#define BOX_AUDIO_CORE_SVF_H

#include "core.h"

/** Square root of 2: the damping of a stage at resonance 0, a Butterworth response */
#define BOX_AUDIO_SVF_SQRT2 1.41421356237309504880

/** Damping of a stage at resonance 1, a Q of 100 that rings without blowing up */
#define BOX_AUDIO_SVF_DAMPING_MIN 0.02

/** Damping of the first stage of the 24 dB filter, 2 cos(pi / 8): with the second stage at
 * resonance 0 the two make a fourth order Butterworth response */
#define BOX_AUDIO_SVF_BUTTERWORTH_1 1.84775906502257351225

/** Damping of the second, resonant stage of the 24 dB filter at resonance 0, 2 cos(3 pi / 8) */
#define BOX_AUDIO_SVF_BUTTERWORTH_2 0.76536686473017954346

/** Gain of the soft clipper of the input of a stage at drive 1: a signal of 1 / this
 * starts to saturate */
#define BOX_AUDIO_SVF_DRIVE_INPUT 4.0f

/** Gain of the soft clipper of the bandpass state of a stage at drive 1: the state, and so
 * the resonance peak, stays under 1 / this */
#define BOX_AUDIO_SVF_DRIVE_STATE 2.0f

/** A stage of a trapezoidal state variable filter */
typedef struct box_audio_svf_t {
    float k;              /**< Damping, 2 / Q */
    float a1, a2, a3;     /**< Coefficients */
    float ic1, ic2;       /**< State */
} box_audio_svf_t;

/** Set the damping of the stages of a filter for a slope and a resonance, without touching
 * their state: stage 0 is the resonant one, stage 1 runs before it at 24 dB.
 *
 * @param svf The two stages
 * @param slope A box_audio_synth_slope_t
 * @param resonance 0..1
 * @return Stages that run: 1 at 12 dB per octave, 2 at 24
 */
static inline int32_t box_audio_svf_damping(
    box_audio_svf_t *svf,
    uint32_t slope,
    float resonance)
{
    int32_t result;
    svf[1].k = (float)BOX_AUDIO_SVF_BUTTERWORTH_1;
    if (slope == BOX_AUDIO_SYNTH_SLOPE_24) {
        result = 2;
        svf[0].k = (float)(BOX_AUDIO_SVF_BUTTERWORTH_2 -
            (BOX_AUDIO_SVF_BUTTERWORTH_2 - BOX_AUDIO_SVF_DAMPING_MIN) * (double)resonance);
    } else {
        result = 1;
        svf[0].k = (float)(BOX_AUDIO_SVF_SQRT2 -
            (BOX_AUDIO_SVF_SQRT2 - BOX_AUDIO_SVF_DAMPING_MIN) * (double)resonance);
    }

    return result;
}

/** Clear the stages of a filter and set their damping, as box_audio_svf_damping.
 *
 * @param svf The two stages
 * @param slope A box_audio_synth_slope_t
 * @param resonance 0..1
 * @return Stages that run: 1 at 12 dB per octave, 2 at 24
 */
static inline int32_t box_audio_svf_init(
    box_audio_svf_t *svf,
    uint32_t slope,
    float resonance)
{
    svf[0] = (box_audio_svf_t){0};
    svf[1] = (box_audio_svf_t){0};

    return box_audio_svf_damping(svf, slope, resonance);
}

/** Get the g of a cutoff: tan(pi cutoff / rate), from the sine of the core.
 *
 * @param cutoff Hz, under half the rate
 * @param rate Frames per second
 * @return g
 */
static inline float box_audio_svf_g(
    double cutoff,
    float rate)
{
    double turns = cutoff / (2.0 * (double)rate);

    return box_audio_core_sin(turns) / box_audio_core_sin(turns + 0.25);
}

/** Set the coefficients of a stage from a g and its damping.
 *
 * @param f The stage
 * @param g tan(pi cutoff / rate)
 */
static inline void box_audio_svf_coefs(
    box_audio_svf_t *f,
    float g)
{
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

/** Filter a frame through a stage.
 *
 * @param f The stage
 * @param filter A box_audio_synth_filter_t, not BOX_AUDIO_SYNTH_FILTER_NONE
 * @param x The input
 * @return The output
 */
static inline float box_audio_svf_next(
    box_audio_svf_t *f,
    int32_t filter,
    float x)
{
    float v1, v2, v3, y;
    v3 = x - f->ic2;
    v1 = f->a1 * f->ic1 + f->a2 * v3;
    v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = box_audio_clean(2 * v1 - f->ic1);
    f->ic2 = box_audio_clean(2 * v2 - f->ic2);
    if (filter == BOX_AUDIO_SYNTH_LOWPASS) {
        y = v2;
    } else if (filter == BOX_AUDIO_SYNTH_HIGHPASS) {
        y = x - f->k * v1 - v2;
    } else if (filter == BOX_AUDIO_SYNTH_NOTCH) {
        y = x - f->k * v1;
    } else {
        y = f->k * v1;
    }

    return y;
}

/** Soft clip a value: tanh(a v) / a, from the rational approximation
 * z (27 + z^2) / (27 + 9 z^2) of tanh(z), which reaches 1 with a flat slope at z = 3 and
 * holds there. Never larger than |v|. Without branches, so it stays cheap in the loop.
 *
 * @param v The value
 * @param a Gain, above 0
 * @return The clipped value, within 1 / a
 */
static inline float box_audio_svf_clip(
    float v,
    float a)
{
    float z = a * v, zz;
    z = z < 3.0f ? z : 3.0f;
    z = z > -3.0f ? z : -3.0f;
    zz = z * z;

    return z * (27.0f + zz) / (a * (27.0f + 9.0f * zz));
}

/** Filter a frame through a stage with drive: the input and the bandpass state go through
 * box_audio_svf_clip. A drive of 0 is box_audio_svf_next.
 *
 * @param f The stage
 * @param filter A box_audio_synth_filter_t, not BOX_AUDIO_SYNTH_FILTER_NONE
 * @param x The input
 * @param drive 0..1
 * @return The output
 */
static inline float box_audio_svf_drive(
    box_audio_svf_t *f,
    int32_t filter,
    float x,
    float drive)
{
    float v1, v2, v3, y;
    if (!(drive > 0)) {
        y = box_audio_svf_next(f, filter, x);
    } else {
        x = box_audio_svf_clip(x, drive * BOX_AUDIO_SVF_DRIVE_INPUT);
        v3 = x - f->ic2;
        v1 = f->a1 * f->ic1 + f->a2 * v3;
        v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
        f->ic1 = box_audio_clean(box_audio_svf_clip(2 * v1 - f->ic1,
            drive * BOX_AUDIO_SVF_DRIVE_STATE));
        f->ic2 = box_audio_clean(2 * v2 - f->ic2);
        if (filter == BOX_AUDIO_SYNTH_LOWPASS) {
            y = v2;
        } else if (filter == BOX_AUDIO_SYNTH_HIGHPASS) {
            y = x - f->k * v1 - v2;
        } else if (filter == BOX_AUDIO_SYNTH_NOTCH) {
            y = x - f->k * v1;
        } else {
            y = f->k * v1;
        }
    }

    return y;
}

#endif
