/**
 * @file audio/core/saturate.h
 * @brief The tanh soft clipper of the core and its 2x oversampler, shared by the saturator
 * effect and the drive of a synth voice; small state and plain floats. Private to src/audio/core.
 *
 * The clipper is tanh(g x + b) - tanh(b), level compensated at BOX_AUDIO_SATURATE_REFERENCE;
 * the oversampler is a polyphase halfband FIR of 31 taps up and down, flat to 17 kHz and 70 dB
 * down from 31 kHz at 48 kHz, BOX_AUDIO_SATURATE_LATENCY frames late.
 *
 * The other modes of the saturator are a shape: a tube curve, a hard clipper with a rounded
 * corner and a sine wavefolder, both with first order ADAA and the folder at 4x, and a tanh
 * rectifier, level compensated by the RMS of a sine of the reference.
 */

#ifndef BOX_AUDIO_CORE_SATURATE_H
#define BOX_AUDIO_CORE_SATURATE_H

#include <stdint.h>
#include "core.h"

/** Frames of the base rate that the oversampler delays its signal, up and down together */
#define BOX_AUDIO_SATURATE_LATENCY 15

/** Frames of the base rate that the wavefolder delays its signal more than the other curves:
 * it runs at four times the rate, through a second oversampler at the doubled rate, and a
 * sample more of the doubled rate makes that delay whole frames */
#define BOX_AUDIO_SATURATE_LATENCY_FOLD 8

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

/** Points of a period of the sine whose RMS box_audio_saturate_curve_sine keeps */
#define BOX_AUDIO_SATURATE_SINE_POINTS 64

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

/** Set the constants of the clipper for a gain and a bias, with a level compensation that
 * keeps the RMS of a sine of a peak: the RMS of the sine over the RMS of the clipped sine
 * without its mean, which a DC blocker after the clipper takes off.
 *
 * The RMS is taken over BOX_AUDIO_SATURATE_SINE_POINTS points at the middles of the steps of
 * a period, turned by a rotation, and its square root by Newton, without libm. The call is
 * slower than box_audio_saturate_curve and belongs where the gain changes seldom.
 *
 * @param curve Receives the constants
 * @param gain Linear gain before the clipper, positive
 * @param bias Offset added to the driven input
 * @param peak Peak of the sine whose RMS the clipper keeps, positive
 */
static inline void box_audio_saturate_curve_sine(
    box_audio_saturate_curve_t *curve,
    float gain,
    float bias,
    float peak)
{
    const double c = 0.99518472667219688, s = 0.098017140329560602;
    double x = 0.99879545620517239, y = 0.049067674327418015, t, sum = 0, square = 0;
    double power, target, root;
    int32_t i;
    box_audio_saturate_curve(curve, gain, bias);
    for (i = 0; i < BOX_AUDIO_SATURATE_SINE_POINTS; i ++) {
        double v = (double)box_audio_saturate_clip(curve, peak * (float)y);
        sum += v;
        square += v * v;
        t = x * c - y * s;
        y = x * s + y * c;
        x = t;
    }

    sum /= BOX_AUDIO_SATURATE_SINE_POINTS;
    power = square / BOX_AUDIO_SATURATE_SINE_POINTS - sum * sum;
    if (power > 0) {
        target = 2 * power;
        root = target > 1 ? target : 1;
        for (i = 0; i < 40; i ++) {
            root = 0.5 * (root + target / root);
        }

        curve->comp = (float)((double)peak / root);
    }
}

/** Run a sample of a channel through the clipper at twice the rate and its level
 * compensation.
 *
 * @param os The oversampler of the channel
 * @param curve The constants
 * @param x The input
 * @return The clipped output, BOX_AUDIO_SATURATE_LATENCY frames after its input
 */
static inline float box_audio_saturate_next(
    box_audio_oversample_t *os,
    const box_audio_saturate_curve_t *curve,
    float x)
{
    float a, b;
    box_audio_oversample_up(os, x, &a, &b);
    a = box_audio_saturate_clip(curve, a);
    b = box_audio_saturate_clip(curve, b);

    return box_audio_oversample_down(os, a, b) * curve->comp;
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

/** Share of the level dependent offset of the tube curve: its positive half has a slope of
 * 1 + this far out, its negative half 1 - this, so the positive half clips earlier */
#define BOX_AUDIO_SATURATE_TUBE_EVEN 0.5f

/** Half width of the rounded corner of the hard clipper, around a driven input of 1 */
#define BOX_AUDIO_SATURATE_KNEE 0.1f

/** Under this step of the driven input between two samples ADAA takes the curve at their
 * middle, where the difference of the antiderivatives loses its precision */
#define BOX_AUDIO_SATURATE_ADAA_MIN 1e-5

/** Under this half step the sinc of the ADAA of the wavefolder is its series */
#define BOX_AUDIO_SATURATE_SINC_SERIES 0.5

/** Turns of a radian, 1 / (2 pi) */
#define BOX_AUDIO_SATURATE_TURNS 0.15915494309189533577

/** Step of the driven input of the slope of a shape at its bias */
#define BOX_AUDIO_SATURATE_SLOPE_STEP 1e-3f

/** The constants of a shape, the clipper of a mode other than BOX_AUDIO_SATURATOR_TANH, for a
 * gain, a bias and a blend, which change only with them */
typedef struct box_audio_saturate_shape_t {
    int32_t mode;             /**< A box_audio_saturator_mode_t */
    float gain;               /**< Linear gain before the curve */
    float bias;               /**< Offset added to the driven input, 0 for the rectifier */
    float blend;              /**< The rectifier: 0 is half wave, 1 is full wave */
    float offset;             /**< The curve of the bias, taken off the output */
    float comp;               /**< Gain after the curve, the level compensation */
    float slope;              /**< 1 / the slope of the curve at the bias, 1 for the
                               * rectifier */
} box_audio_saturate_shape_t;

/** A first order filter, y = c0 x + c1 x1 - c2 y1, the tone tilt of the saturator */
typedef struct box_audio_saturate_tilt_t {
    float x1;                 /**< Input of the frame before */
    float y1;                 /**< Output of the frame before */
} box_audio_saturate_tilt_t;

/** The tube curve: tanh(u + e u^2 / (1 + |u|)) for the share e of
 * BOX_AUDIO_SATURATE_TUBE_EVEN. The square term is an offset that grows with the level, so the
 * curve makes even harmonics without a bias, and it rises monotonically.
 *
 * @param u The driven input
 * @return The curve, within -1..1
 */
static inline float box_audio_saturate_tube(
    float u)
{
    float a = u < 0 ? -u : u;

    return box_audio_tanh(u + BOX_AUDIO_SATURATE_TUBE_EVEN * u * u / (1.0f + a));
}

/** The hard clipper: u up to 1 - k, 1 from 1 + k, a parabola that meets both with their slope
 * in between, for the half width k of BOX_AUDIO_SATURATE_KNEE; odd.
 *
 * @param u The driven input
 * @return The clipped input, within -1..1
 */
static inline float box_audio_saturate_hard(
    float u)
{
    const float k = BOX_AUDIO_SATURATE_KNEE;
    float t = u < 0 ? -u : u, e, r;
    if (t <= 1 - k) {
        r = t;
    } else if (t < 1 + k) {
        e = t - (1 - k);
        r = t - e * e / (4 * k);
    } else {
        r = 1;
    }

    return u < 0 ? -r : r;
}

/** The antiderivative of box_audio_saturate_hard, 0 at 0; even.
 *
 * @param u The driven input
 * @return The integral of the clipper from 0 to u
 */
static inline double box_audio_saturate_hard_integral(
    double u)
{
    const double k = BOX_AUDIO_SATURATE_KNEE;
    double t = u < 0 ? -u : u, e, result;
    if (t <= 1 - k) {
        result = 0.5 * t * t;
    } else if (t < 1 + k) {
        e = t - (1 - k);
        result = 0.5 * t * t - e * e * e / (12 * k);
    } else {
        result = 0.5 * (1 + k) * (1 + k) - 2 * k * k / 3 + (t - (1 + k));
    }

    return result;
}

/** The sine wavefolder: sin(u), which folds the driven input back each time it passes pi / 2.
 *
 * @param u The driven input
 * @return The folded input, within -1..1
 */
static inline float box_audio_saturate_fold(
    float u)
{
    return box_audio_core_sin((double)u * BOX_AUDIO_SATURATE_TURNS);
}

/** The rectifier: v, its negative half turned up and scaled by the blend.
 *
 * @param v The input
 * @param blend 0 for half wave, 1 for full wave
 * @return The rectified input
 */
static inline float box_audio_saturate_rectify(
    float v,
    float blend)
{
    return v >= 0 ? v : -blend * v;
}

/** The curve of a mode at a driven input, without the offset of its bias.
 *
 * @param mode A box_audio_saturator_mode_t
 * @param u The driven input
 * @param blend The blend of the rectifier
 * @return The curve
 */
static inline float box_audio_saturate_shape_curve(
    int32_t mode,
    float u,
    float blend)
{
    float result;
    if (mode == BOX_AUDIO_SATURATOR_TUBE) {
        result = box_audio_saturate_tube(u);
    } else if (mode == BOX_AUDIO_SATURATOR_HARD) {
        result = box_audio_saturate_hard(u);
    } else if (mode == BOX_AUDIO_SATURATOR_FOLD) {
        result = box_audio_saturate_fold(u);
    } else if (mode == BOX_AUDIO_SATURATOR_RECTIFY) {
        result = box_audio_saturate_rectify(box_audio_tanh(u), blend);
    } else {
        result = box_audio_tanh(u);
    }

    return result;
}

/** A shape at an input, without ADAA: the curve of g x + b less the curve of b, 0 for
 * silence.
 *
 * @param shape The constants
 * @param x The input
 * @return The shaped input, before the level compensation
 */
static inline float box_audio_saturate_shape_clip(
    const box_audio_saturate_shape_t *shape,
    float x)
{
    return box_audio_saturate_shape_curve(shape->mode, shape->gain * x + shape->bias,
        shape->blend) - shape->offset;
}

/** Set the constants of a shape, with a level compensation that keeps the RMS of a sine of a
 * peak without its mean, as box_audio_saturate_curve_sine does for the tanh clipper. The call
 * belongs where the gain changes seldom.
 *
 * @param shape Receives the constants
 * @param mode A box_audio_saturator_mode_t other than BOX_AUDIO_SATURATOR_TANH
 * @param gain Linear gain before the curve, positive
 * @param bias Offset added to the driven input; the rectifier takes it as its blend
 * @param peak Peak of the sine whose RMS the shape keeps, positive
 */
static inline void box_audio_saturate_shape(
    box_audio_saturate_shape_t *shape,
    int32_t mode,
    float gain,
    float bias,
    float peak)
{
    const double c = 0.99518472667219688, s = 0.098017140329560602;
    double x = 0.99879545620517239, y = 0.049067674327418015, t, sum = 0, square = 0;
    double power, target, root;
    float d = BOX_AUDIO_SATURATE_SLOPE_STEP, slope;
    int32_t i;
    shape->mode = mode;
    shape->gain = gain;
    shape->bias = mode == BOX_AUDIO_SATURATOR_RECTIFY ? 0.0f : bias;
    shape->blend = mode == BOX_AUDIO_SATURATOR_RECTIFY ? bias : 0.0f;
    shape->offset = box_audio_saturate_shape_curve(mode, shape->bias, shape->blend);
    shape->comp = 1.0f;
    shape->slope = 1.0f;
    if (mode != BOX_AUDIO_SATURATOR_RECTIFY) {
        slope = (box_audio_saturate_shape_curve(mode, shape->bias + d, 0) -
            box_audio_saturate_shape_curve(mode, shape->bias - d, 0)) / (2 * d);
        if (slope > 1e-3f) {
            shape->slope = 1.0f / slope;
        }
    }

    for (i = 0; i < BOX_AUDIO_SATURATE_SINE_POINTS; i ++) {
        double v = (double)box_audio_saturate_shape_clip(shape, peak * (float)y);
        sum += v;
        square += v * v;
        t = x * c - y * s;
        y = x * s + y * c;
        x = t;
    }

    sum /= BOX_AUDIO_SATURATE_SINE_POINTS;
    power = square / BOX_AUDIO_SATURATE_SINE_POINTS - sum * sum;
    if (power > 0) {
        target = 2 * power;
        root = target > 1 ? target : 1;
        for (i = 0; i < 40; i ++) {
            root = 0.5 * (root + target / root);
        }

        shape->comp = (float)((double)peak / root);
    }
}

/** sin(h) / h, from its series for a small h so that it keeps its precision.
 *
 * @param h The value
 * @return The sinc
 */
static inline double box_audio_saturate_sinc(
    double h)
{
    double a = h < 0 ? -h : h, h2 = h * h, result;
    if (a < BOX_AUDIO_SATURATE_SINC_SERIES) {
        result = 1 - h2 / 6 * (1 - h2 / 20 * (1 - h2 / 42));
    } else {
        result = (double)box_audio_core_sin(h * BOX_AUDIO_SATURATE_TURNS) / h;
    }

    return result;
}

/** A sample at the oversampled rate through the hard clipper or the wavefolder of a shape with
 * first order antiderivative anti-aliasing (ADAA): (F(u) - F(u0)) / (u - u0) for the
 * antiderivative F, the curve through a box filter of a sample, half a sample late.
 *
 * The box filter takes most of the harmonics above the Nyquist frequency out before they
 * fold. The folder takes the difference of the cosines as sin(m) sinc(h) for the middle m and
 * the half step h, which keeps its precision for any step.
 *
 * @param shape The constants, of BOX_AUDIO_SATURATOR_HARD or BOX_AUDIO_SATURATOR_FOLD
 * @param last The driven input of the sample before, receives the one of this sample
 * @param x The input
 * @return The shaped input, before the level compensation; 0 while the input stays 0
 */
static inline float box_audio_saturate_adaa(
    const box_audio_saturate_shape_t *shape,
    double *last,
    float x)
{
    double u = (double)(shape->gain * x + shape->bias), u0 = *last, d = u - u0, y;
    *last = u;
    if (shape->mode == BOX_AUDIO_SATURATOR_HARD) {
        if (d > BOX_AUDIO_SATURATE_ADAA_MIN || d < -BOX_AUDIO_SATURATE_ADAA_MIN) {
            y = (box_audio_saturate_hard_integral(u) - box_audio_saturate_hard_integral(u0)) /
                d;
        } else {
            y = (double)box_audio_saturate_hard((float)(0.5 * (u + u0)));
        }
    } else {
        y = (double)box_audio_saturate_fold((float)(0.5 * (u + u0))) *
            box_audio_saturate_sinc(0.5 * d);
    }

    return (float)y - shape->offset;
}

/** A sample at the doubled rate through the wavefolder of a shape with ADAA at four times the
 * rate, by a second oversampler, which lowers the aliases of a much folded sine 40 dB; held
 * back a sample, so that it comes BOX_AUDIO_SATURATE_LATENCY_FOLD frames of the base rate late.
 *
 * @param shape The constants, of BOX_AUDIO_SATURATOR_FOLD
 * @param os The second oversampler, which runs at the doubled rate
 * @param last The driven input of the sample before at four times the rate
 * @param late The output held back, receives the one of this sample
 * @param x The input
 * @return The folded input, before the level compensation
 */
static inline float box_audio_saturate_fold_next(
    const box_audio_saturate_shape_t *shape,
    box_audio_oversample_t *os,
    double *last,
    float *late,
    float x)
{
    float a, b, result = *late;
    box_audio_oversample_up(os, x, &a, &b);
    a = box_audio_saturate_adaa(shape, last, a);
    b = box_audio_saturate_adaa(shape, last, b);
    *late = box_audio_oversample_down(os, a, b);

    return result;
}

/** Run a sample through a first order filter.
 *
 * @param f The state
 * @param c The coefficients c0, c1 and c2 of y = c0 x + c1 x1 - c2 y1
 * @param x The input
 * @return The output
 */
static inline float box_audio_saturate_tilt_next(
    box_audio_saturate_tilt_t *f,
    const float *c,
    float x)
{
    float y = c[0] * x + c[1] * f->x1 - c[2] * f->y1, a = y < 0 ? -y : y;
    f->x1 = x;
    f->y1 = a >= BOX_AUDIO_SATURATE_TINY ? y : 0.0f;

    return y;
}

#endif
