/**
 * @file audio/core/frequency_shifter.c
 * @brief The frequency shifter kind of effect: the analytic signal of each channel from an
 * allpass pair Hilbert transformer, times a complex oscillator, so every partial moves by the
 * same number of Hz; or the real part times a sine, a ring modulator. Its output feeds back into
 * its input and is mixed with the dry signal.
 *
 * The Hilbert transformer is the halfband lowpass of HIIR moved up by a quarter of the rate, its
 * coefficients set for the rate when it starts. The phases of the oscillators advance a whole
 * step per frame. Its params are a box_audio_frequency_shifter_params_t.
 */

#include "internal.h"

/** States that sum to less than this, -140 dB, have decayed to silence */
#define BOX_AUDIO_FREQUENCY_SHIFTER_SILENCE 1e-7f

/** Level the feedback is softly limited to, so that it stays bounded where the paths of the
 * Hilbert transformer are not 90 degrees apart */
#define BOX_AUDIO_FREQUENCY_SHIFTER_LIMIT 4.0f

/** Units of the phase of an oscillator per turn */
#define BOX_AUDIO_FREQUENCY_SHIFTER_TURN 4294967296.0

/** Pi */
#define BOX_AUDIO_FREQUENCY_SHIFTER_PI 3.14159265358979323846

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_frequency_shifter_clamp(
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

/** Sine of a phase in turns in double precision, without libm, for the coefficients.
 *
 * @param turns The phase in turns
 * @return The sine, within 1e-14
 */
static double box_audio_frequency_shifter_sin(
    double turns)
{
    double x, x2;
    turns -= (double)(int64_t)turns;
    if (turns < 0) {
        turns += 1;
    }

    if (turns >= 0.5) {
        turns -= 1;
    }

    x = turns * 2 * BOX_AUDIO_FREQUENCY_SHIFTER_PI;
    if (x > BOX_AUDIO_FREQUENCY_SHIFTER_PI * 0.5) {
        x = BOX_AUDIO_FREQUENCY_SHIFTER_PI - x;
    } else if (x < -BOX_AUDIO_FREQUENCY_SHIFTER_PI * 0.5) {
        x = -BOX_AUDIO_FREQUENCY_SHIFTER_PI - x;
    }

    x2 = x * x;

    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110 *
        (1 - x2 / 156 * (1 - x2 / 210 * (1 - x2 / 272))))))));
}

/** Square root by Newton's method, without libm.
 *
 * @param v The value
 * @return Its square root, 0 for a value of 0 or less
 */
static double box_audio_frequency_shifter_sqrt(
    double v)
{
    double x = v > 1 ? v : 1.0;
    int32_t i;
    for (i = 0; i < 64 && v > 0; i ++) {
        x = 0.5 * (x + v / x);
    }

    return v > 0 ? x : 0.0;
}

/** Integer power.
 *
 * @param q The base
 * @param n The exponent, 0 or more
 * @return q to the n
 */
static double box_audio_frequency_shifter_pow(
    double q,
    int32_t n)
{
    double result = 1;
    int32_t i;
    for (i = 0; i < n && result != 0; i ++) {
        result *= q;
    }

    return result;
}

/** Set the coefficients of the Hilbert transformer for its transition band at a rate, as the
 * designer of the polyphase halfband filters of HIIR does: the transition gives the modulus
 * k and the nome q of the elliptic functions, whose series give the poles.
 *
 * @param coef Receives BOX_AUDIO_FREQUENCY_SHIFTER_ALLPASSES coefficients
 * @param rate Sample rate
 */
static void box_audio_frequency_shifter_design(
    double *coef,
    double rate)
{
    const int32_t n = BOX_AUDIO_FREQUENCY_SHIFTER_ALLPASSES, order = 2 * n + 1;
    double t = (double)BOX_AUDIO_FREQUENCY_SHIFTER_LOW / rate, a, k, kk, e, e2, e4, q, q4;
    int32_t c;
    if (!(t >= 1e-6)) {
        t = 1e-6;
    } else if (t > 0.1) {
        t = 0.1;
    }

    a = (1 - 2 * t) / 8;
    k = box_audio_frequency_shifter_sin(a) / box_audio_frequency_shifter_sin(a + 0.25);
    k *= k;
    kk = box_audio_frequency_shifter_sqrt(box_audio_frequency_shifter_sqrt(1 - k * k));
    e = 0.5 * (1 - kk) / (1 + kk);
    e2 = e * e;
    e4 = e2 * e2;
    q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
    q4 = box_audio_frequency_shifter_sqrt(box_audio_frequency_shifter_sqrt(q));
    for (c = 1; c <= n; c ++) {
        double num = 0, den = 0, term, ww, x;
        int32_t i, sign;
        for (i = 0, sign = 1; i < 64; i ++, sign = -sign) {
            term = box_audio_frequency_shifter_pow(q, i * (i + 1));
            if (term < 1e-100) {
                break;
            }

            num += term * (double)sign *
                box_audio_frequency_shifter_sin((double)((2 * i + 1) * c) / (2.0 * order));
        }

        for (i = 1, sign = -1; i < 64; i ++, sign = -sign) {
            term = box_audio_frequency_shifter_pow(q, i * i);
            if (term < 1e-100) {
                break;
            }

            den += term * (double)sign *
                box_audio_frequency_shifter_sin((double)(2 * i * c) / (2.0 * order) + 0.25);
        }

        ww = num * q4 / (den + 0.5);
        ww *= ww;
        x = box_audio_frequency_shifter_sqrt((1 - ww * k) * (1 - ww / k)) / (1 + ww);
        coef[c - 1] = (1 - x) / (1 + x);
    }
}

/** Clamp the params of an effect into the target of its shifter.
 *
 * @param fx The effect
 */
static void box_audio_frequency_shifter_target(
    box_audio_effect_t *fx)
{
    box_audio_frequency_shifter_t *fs = &fx->data.frequency_shifter;
    const float *p = fx->params;
    fs->target.shift = box_audio_frequency_shifter_clamp(p[0],
        -BOX_AUDIO_FREQUENCY_SHIFTER_MAX, BOX_AUDIO_FREQUENCY_SHIFTER_MAX);
    fs->target.mode = p[1] >= 0.5f ? (float)BOX_AUDIO_FREQUENCY_RING :
        (float)BOX_AUDIO_FREQUENCY_SHIFT;
    fs->target.stereo = box_audio_frequency_shifter_clamp(p[2], 0, 1);
    fs->target.feedback = box_audio_frequency_shifter_clamp(p[3],
        -BOX_AUDIO_FREQUENCY_SHIFTER_FEEDBACK_MAX, BOX_AUDIO_FREQUENCY_SHIFTER_FEEDBACK_MAX);
    fs->target.mix = box_audio_frequency_shifter_clamp(p[4], 0, 1);
}

/** Clear the allpasses, the DC blockers and the feedback.
 *
 * @param fs The shifter
 */
static void box_audio_frequency_shifter_clear(
    box_audio_frequency_shifter_t *fs)
{
    int32_t c, p, i;
    for (c = 0; c < 2; c ++) {
        for (p = 0; p < 2; p ++) {
            for (i = 0; i <= BOX_AUDIO_FREQUENCY_SHIFTER_SECTIONS; i ++) {
                fs->node[c][p][i][0] = 0;
                fs->node[c][p][i][1] = 0;
            }
        }

        fs->prev[c] = 0;
        fs->last[c] = 0;
        fs->dc[c].x1 = 0;
        fs->dc[c].y1 = 0;
    }
}

/** Get the step of the phase of an oscillator per frame for a shift.
 *
 * @param fs The shifter
 * @param hz Shift in Hz, either way
 * @return The step in 2^-32 turns
 */
static uint32_t box_audio_frequency_shifter_step(
    const box_audio_frequency_shifter_t *fs,
    float hz)
{
    return (uint32_t)(int64_t)((double)hz * fs->turn);
}

void box_audio_frequency_shifter_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_frequency_shifter_t *fs = &fx->data.frequency_shifter;
    double rate = (double)core->sample_rate;
    (void)fade_in;
    box_audio_frequency_shifter_target(fx);
    box_audio_frequency_shifter_design(fs->coef, rate);
    box_audio_frequency_shifter_clear(fs);
    fs->dc_coef = (float)(1.0 - 2.0 * BOX_AUDIO_FREQUENCY_SHIFTER_PI *
        (double)BOX_AUDIO_FREQUENCY_SHIFTER_LOW / rate);
    fs->turn = BOX_AUDIO_FREQUENCY_SHIFTER_TURN / rate;
    fs->phase[0] = 0;
    fs->phase[1] = 0;
    box_audio_ramp_set(&fs->shift, fs->target.shift);
    box_audio_ramp_set(&fs->ring, fs->target.mode);
    box_audio_ramp_set(&fs->stereo, fs->target.stereo);
    box_audio_ramp_set(&fs->feedback, fs->target.feedback);
    box_audio_ramp_set(&fs->mix, fs->target.mix);
}

void box_audio_frequency_shifter_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_frequency_shifter_t *fs = &fx->data.frequency_shifter;
    int32_t ramp = (int32_t)(BOX_AUDIO_FREQUENCY_SHIFTER_RAMP * core->sample_rate);
    box_audio_frequency_shifter_target(fx);
    box_audio_ramp_to(&fs->shift, fs->target.shift, ramp);
    box_audio_ramp_to(&fs->ring, fs->target.mode, ramp);
    box_audio_ramp_to(&fs->stereo, fs->target.stereo, ramp);
    box_audio_ramp_to(&fs->feedback, fs->target.feedback, ramp);
    box_audio_ramp_to(&fs->mix, fs->target.mix, ramp);
}

/** Run a path of the Hilbert transformer for a frame: sections y = a (x + y[-2]) - x[-2], every
 * other coefficient from the first.
 *
 * @param node Input and output of each section one and two frames back
 * @param coef The coefficients
 * @param first Index of the first coefficient of the path, 0 or 1
 * @param v Input of the path
 * @return Output of the path
 */
static inline double box_audio_frequency_shifter_path(
    double node[BOX_AUDIO_FREQUENCY_SHIFTER_SECTIONS + 1][2],
    const double *coef,
    int32_t first,
    double v)
{
    int32_t i;
    for (i = 0; i < BOX_AUDIO_FREQUENCY_SHIFTER_SECTIONS; i ++) {
        double y = coef[2 * i + first] * (v + node[i + 1][1]) - node[i][1];
        node[i][1] = node[i][0];
        node[i][0] = v;
        v = y;
    }

    node[i][1] = node[i][0];
    node[i][0] = v;

    return v;
}

bool box_audio_frequency_shifter_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_frequency_shifter_t *fs = &fx->data.frequency_shifter;
    float *io[2] = { l, r };
    bool result = false;
    double sum = 0;
    int32_t i, c, p, k;
    (void)core;
    for (i = 0; i < frames; i ++) {
        float shift = box_audio_ramp_next(&fs->shift);
        float ring = box_audio_ramp_next(&fs->ring);
        float stereo = box_audio_ramp_next(&fs->stereo);
        float fb = box_audio_ramp_next(&fs->feedback);
        float wet = box_audio_ramp_next(&fs->mix);
        float hz[2];
        hz[0] = shift;
        hz[1] = shift * (1 - 2 * stereo);
        for (c = 0; c < 2; c ++) {
            double turns = (double)fs->phase[c] / BOX_AUDIO_FREQUENCY_SHIFTER_TURN;
            double co = (double)box_audio_core_sin(turns + 0.25);
            double si = (double)box_audio_core_sin(turns);
            float x = io[c][i];
            double v = (double)x + (double)fb * (double)fs->last[c], re, im, up, rm, y;
            re = box_audio_frequency_shifter_path(fs->node[c][0], fs->coef, 0, v);
            im = box_audio_frequency_shifter_path(fs->node[c][1], fs->coef, 1, fs->prev[c]);
            fs->prev[c] = v;
            up = re * co - im * si;
            rm = re * co;
            y = up + (double)ring * (rm - up);
            fs->last[c] = box_audio_clean(BOX_AUDIO_FREQUENCY_SHIFTER_LIMIT * box_audio_tanh(
                box_audio_dc_next(&fs->dc[c], box_audio_clean((float)y), fs->dc_coef) /
                BOX_AUDIO_FREQUENCY_SHIFTER_LIMIT));
            io[c][i] = box_audio_clean(x * (1 - wet) + (float)y * wet);
            fs->phase[c] += box_audio_frequency_shifter_step(fs, hz[c]);
        }
    }

    if (silent) {
        for (c = 0; c < 2; c ++) {
            for (p = 0; p < 2; p ++) {
                for (k = 0; k <= BOX_AUDIO_FREQUENCY_SHIFTER_SECTIONS; k ++) {
                    double a = fs->node[c][p][k][0], b = fs->node[c][p][k][1];
                    sum += (a < 0 ? -a : a) + (b < 0 ? -b : b);
                }
            }

            sum += fs->prev[c] < 0 ? -fs->prev[c] : fs->prev[c];
            sum += fs->last[c] < 0 ? -fs->last[c] : fs->last[c];
            sum += (fs->dc[c].x1 < 0 ? -fs->dc[c].x1 : fs->dc[c].x1) +
                (fs->dc[c].y1 < 0 ? -fs->dc[c].y1 : fs->dc[c].y1);
        }

        if (sum < BOX_AUDIO_FREQUENCY_SHIFTER_SILENCE) {
            box_audio_frequency_shifter_clear(fs);
            result = true;
        }
    }

    return result;
}

void box_audio_frequency_shifter_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_frequency_shifter_t *fs = &fx->data.frequency_shifter;
    (void)core;
    box_audio_ramp_set(&fs->shift, fs->shift.target);
    box_audio_ramp_set(&fs->ring, fs->ring.target);
    box_audio_ramp_set(&fs->stereo, fs->stereo.target);
    box_audio_ramp_set(&fs->feedback, fs->feedback.target);
    box_audio_ramp_set(&fs->mix, fs->mix.target);
    fs->phase[0] += (uint32_t)frames * box_audio_frequency_shifter_step(fs, fs->shift.value);
    fs->phase[1] += (uint32_t)frames * box_audio_frequency_shifter_step(fs,
        fs->shift.value * (1 - 2 * fs->stereo.value));
}
