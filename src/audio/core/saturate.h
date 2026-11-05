/**
 * @file audio/core/saturate.h
 * @brief The tanh soft clipper of the core and its 2x oversampler, shared by the saturator
 * effect and the drive of a synth voice; small state and plain floats. Private to src/audio/core.
 *
 * The clipper is tanh(g x + b) - tanh(b), level compensated at BOX_AUDIO_SATURATE_REFERENCE;
 * the oversampler is a polyphase halfband FIR of 31 taps up and down, flat to 17 kHz and 70 dB
 * down from 31 kHz at 48 kHz, BOX_AUDIO_SATURATE_LATENCY frames late.
 */

#ifndef BOX_AUDIO_CORE_SATURATE_H
#define BOX_AUDIO_CORE_SATURATE_H

#include <stdint.h>

/** Frames of the base rate that the oversampler delays its signal, up and down together */
#define BOX_AUDIO_SATURATE_LATENCY 15

/** Peak level of the input whose gain the level compensation undoes, -14 dBFS: a sine about
 * that loud keeps its RMS within 0.7 dB over a drive of 0 to +30 dB */
#define BOX_AUDIO_SATURATE_REFERENCE 0.2f

/** Largest magnitude tanh takes; above it the rational form is 1 within a float */
#define BOX_AUDIO_SATURATE_TANH_MAX 7.90531110763549805f

/** Under this magnitude tanh(x) is x within a float */
#define BOX_AUDIO_SATURATE_TANH_TINY 0.0004f

/** Coefficients of the halfband at the odd offsets 1, 3, .. 15 from its center, which is
 * 0.5; the even offsets are 0. They sum to 0.25 */
#define BOX_AUDIO_SATURATE_H0 3.143334437e-01f
#define BOX_AUDIO_SATURATE_H1 (-9.460322499e-02f)
#define BOX_AUDIO_SATURATE_H2 4.605905032e-02f
#define BOX_AUDIO_SATURATE_H3 (-2.374254954e-02f)
#define BOX_AUDIO_SATURATE_H4 1.162484303e-02f
#define BOX_AUDIO_SATURATE_H5 (-5.037464796e-03f)
#define BOX_AUDIO_SATURATE_H6 1.760299718e-03f
#define BOX_AUDIO_SATURATE_H7 (-3.943973965e-04f)

/** Smallest magnitude the state of the DC blocker keeps, so that no denormal forms */
#define BOX_AUDIO_SATURATE_TINY 1e-30f

/** The 2x oversampler of a channel: the last 16 inputs, and the last 16 odd and 8 even
 * samples of the clipped signal at the doubled rate */
typedef struct box_audio_oversample_t {
    float up[16];             /**< Inputs at the base rate, a ring */
    float odd[16];            /**< Odd samples of the clipped signal, a ring */
    float even[8];            /**< Even samples of the clipped signal, a ring */
    uint32_t pos;             /**< Frames run, the write position of the rings */
} box_audio_oversample_t;

/** The DC blocker after the clipper: a one pole highpass */
typedef struct box_audio_dc_t {
    float x1;                 /**< Input of the frame before */
    float y1;                 /**< Output of the frame before */
} box_audio_dc_t;

/** The constants of the clipper for a gain and a bias, which change only with them */
typedef struct box_audio_saturate_curve_t {
    float gain;               /**< Linear gain before the clipper */
    float bias;               /**< Offset added to the driven input */
    float offset;             /**< tanh of the bias, taken off the output */
    float comp;               /**< Gain after the clipper, the level compensation */
    float slope;              /**< 1 / the slope of the clipper at silence, 1 / (1 - offset^2):
                               * the clipper of a small signal times it is the driven input */
} box_audio_saturate_curve_t;

/** tanh by a rational form of degree 13 over 6, within 3e-7 and odd, without libm; tanh(0)
 * is 0 exactly.
 *
 * @param x The value; NaN becomes 0
 * @return tanh of x
 */
static inline float box_audio_tanh(
    float x)
{
    float x2, p, q, a = x < 0 ? -x : x, result;
    if (x > BOX_AUDIO_SATURATE_TANH_MAX) {
        x = BOX_AUDIO_SATURATE_TANH_MAX;
    } else if (x < -BOX_AUDIO_SATURATE_TANH_MAX) {
        x = -BOX_AUDIO_SATURATE_TANH_MAX;
    } else if (!(a >= 0)) {
        x = 0;
    }

    if (a < BOX_AUDIO_SATURATE_TANH_TINY) {
        result = x;
    } else {
        x2 = x * x;
        p = x2 * -2.76076847742355e-16f + 2.00018790482477e-13f;
        p = x2 * p + -8.60467152213735e-11f;
        p = x2 * p + 5.12229709037114e-08f;
        p = x2 * p + 1.48572235717979e-05f;
        p = x2 * p + 6.37261928875436e-04f;
        p = x2 * p + 4.89352455891786e-03f;
        q = x2 * 1.19825839466702e-06f + 1.18534705686654e-04f;
        q = x2 * q + 2.26843463243900e-03f;
        q = x2 * q + 4.89352518554385e-03f;
        result = x * p / q;
    }

    return result;
}

/** Set the constants of the clipper for a gain and a bias. The level compensation is
 * 2 r / (tanh(g r + b) - tanh(b - g r)) for the reference r: the inverse of the mean gain of
 * the clipper over a peak of r either way.
 *
 * @param curve Receives the constants
 * @param gain Linear gain before the clipper, positive
 * @param bias Offset added to the driven input
 */
static inline void box_audio_saturate_curve(
    box_audio_saturate_curve_t *curve,
    float gain,
    float bias)
{
    float r = BOX_AUDIO_SATURATE_REFERENCE;
    float span = box_audio_tanh(gain * r + bias) - box_audio_tanh(bias - gain * r);
    curve->gain = gain;
    curve->bias = bias;
    curve->offset = box_audio_tanh(bias);
    curve->comp = span > 0 ? 2 * r / span : 1.0f;
    curve->slope = 1.0f / (1.0f - curve->offset * curve->offset);
}

/** The clipper, before its level compensation: tanh(g x + b) - tanh(b), 0 for silence.
 *
 * @param curve The constants
 * @param x The input
 * @return The clipped input
 */
static inline float box_audio_saturate_clip(
    const box_audio_saturate_curve_t *curve,
    float x)
{
    return box_audio_tanh(curve->gain * x + curve->bias) - curve->offset;
}

/** Interpolate the next input of a channel to the doubled rate: the two samples at the
 * doubled rate of the input BOX_AUDIO_SATURATE_LATENCY / 2 frames back, rounded up. Each call
 * is followed by a call of box_audio_oversample_down for the same frame.
 *
 * @param os The oversampler
 * @param x The input
 * @param a Receives the even sample, the input 8 frames back
 * @param b Receives the odd sample, halfway to the input 7 frames back
 */
static inline void box_audio_oversample_up(
    box_audio_oversample_t *os,
    float x,
    float *a,
    float *b)
{
    const float *u = os->up;
    uint32_t p = os->pos;
    os->up[p & 15] = x;
    *a = u[(p - 8) & 15];
    *b = 2 * (BOX_AUDIO_SATURATE_H0 * (u[(p - 8) & 15] + u[(p - 7) & 15]) +
        BOX_AUDIO_SATURATE_H1 * (u[(p - 9) & 15] + u[(p - 6) & 15]) +
        BOX_AUDIO_SATURATE_H2 * (u[(p - 10) & 15] + u[(p - 5) & 15]) +
        BOX_AUDIO_SATURATE_H3 * (u[(p - 11) & 15] + u[(p - 4) & 15]) +
        BOX_AUDIO_SATURATE_H4 * (u[(p - 12) & 15] + u[(p - 3) & 15]) +
        BOX_AUDIO_SATURATE_H5 * (u[(p - 13) & 15] + u[(p - 2) & 15]) +
        BOX_AUDIO_SATURATE_H6 * (u[(p - 14) & 15] + u[(p - 1) & 15]) +
        BOX_AUDIO_SATURATE_H7 * (u[(p - 15) & 15] + u[p & 15]));
}

/** Decimate the two samples at the doubled rate of a frame back to the base rate, centered
 * on an even sample 7 frames back, and end the frame.
 *
 * @param os The oversampler
 * @param a The even sample, from box_audio_oversample_up and through the clipper
 * @param b The odd sample
 * @return The output, BOX_AUDIO_SATURATE_LATENCY frames after the input of the
 *   oversampler
 */
static inline float box_audio_oversample_down(
    box_audio_oversample_t *os,
    float a,
    float b)
{
    const float *o = os->odd;
    uint32_t p = os->pos;
    float result;
    os->odd[p & 15] = b;
    os->even[p & 7] = a;
    result = 0.5f * os->even[(p - 7) & 7] +
        BOX_AUDIO_SATURATE_H0 * (o[(p - 7) & 15] + o[(p - 8) & 15]) +
        BOX_AUDIO_SATURATE_H1 * (o[(p - 6) & 15] + o[(p - 9) & 15]) +
        BOX_AUDIO_SATURATE_H2 * (o[(p - 5) & 15] + o[(p - 10) & 15]) +
        BOX_AUDIO_SATURATE_H3 * (o[(p - 4) & 15] + o[(p - 11) & 15]) +
        BOX_AUDIO_SATURATE_H4 * (o[(p - 3) & 15] + o[(p - 12) & 15]) +
        BOX_AUDIO_SATURATE_H5 * (o[(p - 2) & 15] + o[(p - 13) & 15]) +
        BOX_AUDIO_SATURATE_H6 * (o[(p - 1) & 15] + o[(p - 14) & 15]) +
        BOX_AUDIO_SATURATE_H7 * (o[p & 15] + o[(p - 15) & 15]);
    os->pos = p + 1;

    return result;
}

/** Sum of the magnitudes of the state of an oversampler, 0 once its input was silent for
 * 16 frames.
 *
 * @param os The oversampler
 * @return The sum
 */
static inline float box_audio_oversample_level(
    const box_audio_oversample_t *os)
{
    float sum = 0;
    int32_t i;
    for (i = 0; i < 16; i ++) {
        sum += (os->up[i] < 0 ? -os->up[i] : os->up[i]) +
            (os->odd[i] < 0 ? -os->odd[i] : os->odd[i]);
    }

    for (i = 0; i < 8; i ++) {
        sum += os->even[i] < 0 ? -os->even[i] : os->even[i];
    }

    return sum;
}

/** Clear an oversampler to silence.
 *
 * @param os The oversampler
 */
static inline void box_audio_oversample_clear(
    box_audio_oversample_t *os)
{
    int32_t i;
    for (i = 0; i < 16; i ++) {
        os->up[i] = 0;
        os->odd[i] = 0;
    }

    for (i = 0; i < 8; i ++) {
        os->even[i] = 0;
    }

    os->pos = 0;
}

/** Run a sample through a DC blocker: y = x - x1 + coef y1.
 *
 * @param dc The DC blocker
 * @param x The input
 * @param coef Pole, 1 - 2 pi cutoff / rate
 * @return The output
 */
static inline float box_audio_dc_next(
    box_audio_dc_t *dc,
    float x,
    float coef)
{
    float y = x - dc->x1 + coef * dc->y1, a = y < 0 ? -y : y;
    dc->x1 = x;
    dc->y1 = a >= BOX_AUDIO_SATURATE_TINY ? y : 0.0f;

    return y;
}

#endif
