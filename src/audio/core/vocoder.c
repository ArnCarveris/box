/**
 * @file audio/core/vocoder.c
 * @brief The vocoder kind of effect: the output of the key bus, the modulator, shapes the input
 * of its own bus, the carrier, through matched banks of bandpasses with envelope followers.
 *
 * Each band is two cascaded state variable bandpasses (svf.h) per channel on the modulator and
 * on the carrier, log spaced from low to high; the carrier bands move by the shift against the
 * modulator bands, for a formant shift. The envelope of each modulator band scales the carrier
 * band of the same index and the scaled carrier bands are summed. White noise added to the
 * carrier of the high bands lets consonants through.
 *
 * The bands and their state live on the memory of the slot, BOX_AUDIO_VOCODER_BANDS_MAX bands
 * of 32 floats, so the state of the kind stays small. Every BOX_AUDIO_VOCODER_CONTROL frames
 * while low, high, q or shift move it sets the coefficients anew, on a count of frames that
 * does not depend on where a block ends. Its params are a box_audio_vocoder_params_t.
 */

#include "internal.h"

/** Pi */
#define BOX_AUDIO_VOCODER_PI 3.14159265358979323846

/** 1 / ln(2) */
#define BOX_AUDIO_VOCODER_LOG2E 1.4426950408889634

/** Share of the -3 dB width of a bandpass that is left after a second one of the same Q,
 * sqrt(sqrt(2) - 1) */
#define BOX_AUDIO_VOCODER_CASCADE 0.64359425290558262

/** Mean of the absolute value of noise over its RMS level, sqrt(2 / pi), which the envelope
 * of a band of noise reads */
#define BOX_AUDIO_VOCODER_MEAN_ABS 0.79788456080286536

/** Lowest center of a shifted carrier band in Hz */
#define BOX_AUDIO_VOCODER_SHIFTED_MIN 10.0

/** Share of the rate under which the bands stay */
#define BOX_AUDIO_VOCODER_TOP 0.45f

/** Scale of a value of the noise to -1..1, 2^-31 */
#define BOX_AUDIO_VOCODER_NOISE_SCALE (1.0f / 2147483648.0f)

/** Seed of the noise, never 0 */
#define BOX_AUDIO_VOCODER_SEED 0x9E3779B9u

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_vocoder_clamp(
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

int32_t box_audio_vocoder_floats(
    float sample_rate)
{
    (void)sample_rate;

    return (int32_t)(sizeof(box_audio_vocoder_band_t) / sizeof(float)) *
        BOX_AUDIO_VOCODER_BANDS_MAX;
}

/** Clamp the params of an effect into the target of its vocoder and set the coefficients of
 * its envelopes.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_vocoder_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_vocoder_t *v = &fx->data.vocoder;
    const float *p = fx->params;
    float top = BOX_AUDIO_VOCODER_TOP * core->sample_rate;
    float span = (float)box_audio_synth_exp2(BOX_AUDIO_VOCODER_SPAN_MIN);
    float bands = box_audio_vocoder_clamp(p[0], BOX_AUDIO_VOCODER_BANDS_MIN,
        BOX_AUDIO_VOCODER_BANDS_MAX);
    double rate = (double)core->sample_rate;
    v->target.bands = (float)(int32_t)(bands + 0.5f);
    v->target.high = box_audio_vocoder_clamp(p[2], BOX_AUDIO_VOCODER_FREQUENCY_MIN * span, top);
    v->target.low = box_audio_vocoder_clamp(p[1], BOX_AUDIO_VOCODER_FREQUENCY_MIN,
        v->target.high / span);
    v->target.q = box_audio_vocoder_clamp(p[3], BOX_AUDIO_VOCODER_Q_MIN,
        BOX_AUDIO_VOCODER_Q_MAX);
    v->target.attack = box_audio_vocoder_clamp(p[4], BOX_AUDIO_DYNAMICS_ATTACK_MIN,
        BOX_AUDIO_DYNAMICS_ATTACK_MAX);
    v->target.release = box_audio_vocoder_clamp(p[5], BOX_AUDIO_DYNAMICS_RELEASE_MIN,
        BOX_AUDIO_DYNAMICS_RELEASE_MAX);
    v->target.shift = box_audio_vocoder_clamp(p[6], -1, 1);
    v->target.unvoiced = box_audio_vocoder_clamp(p[7], 0, 1);
    v->target.mix = box_audio_vocoder_clamp(p[8], 0, 1);
    v->attack = (float)(1 - box_audio_synth_exp2(-BOX_AUDIO_VOCODER_LOG2E /
        ((double)v->target.attack * rate)));
    v->release = (float)(1 - box_audio_synth_exp2(-BOX_AUDIO_VOCODER_LOG2E /
        ((double)v->target.release * rate)));
}

/** Set the coefficients of a bandpass stage pair from its center and its damping.
 *
 * @param c Receives a1, a2, a3 and k
 * @param hz Center in Hz
 * @param k Damping, 1 / Q
 * @param rate Frames per second
 */
static void box_audio_vocoder_stage(
    float *c,
    double hz,
    float k,
    float rate)
{
    float g = box_audio_svf_g(hz, rate);
    c[0] = 1.0f / (1.0f + g * (g + k));
    c[1] = g * c[0];
    c[2] = g * c[1];
    c[3] = k;
}

/** Get the noise equivalent width of a band as a share of half the rate: pi f / (4 Q) for two
 * cascaded bandpasses, at most 1.
 *
 * @param hz Center in Hz
 * @param q Q of a stage
 * @param rate Frames per second
 * @return The share
 */
static double box_audio_vocoder_width(
    double hz,
    double q,
    double rate)
{
    double result = BOX_AUDIO_VOCODER_PI * hz / (4 * q) / (0.5 * rate);

    return result < 1 ? result : 1.0;
}

/** Set the coefficients and the gains of the bands that run from the low, high, q and shift
 * that run, and the first band that takes the noise.
 *
 * @param core The core
 * @param v The vocoder
 */
static void box_audio_vocoder_coefs(
    const box_audio_core_t *core,
    box_audio_vocoder_t *v)
{
    double rate = (double)core->sample_rate, top = BOX_AUDIO_VOCODER_TOP * rate;
    double lo = (double)v->low.value, hi = (double)v->high.value;
    double d = (hi - lo) / (double)(v->bands - 1), step = box_audio_synth_exp2(d);
    double q = box_audio_synth_exp2(0.5 * d) / (step - 1) * BOX_AUDIO_VOCODER_CASCADE *
        (double)v->q.value;
    double shift = box_audio_synth_exp2((double)(v->shift.value *
        BOX_AUDIO_VOCODER_SHIFT_OCTAVES));
    double sum = 0, gain;
    float k = (float)(1.0 / q);
    int32_t b;
    v->unvoiced_from = v->bands - v->bands / 4;
    for (b = 0; b < v->bands; b ++) {
        box_audio_vocoder_band_t *band = &v->band[b];
        double fm = box_audio_synth_exp2(lo + d * (double)b), fc = fm * shift;
        fm = fm < top ? fm : top;
        if (!(fc >= BOX_AUDIO_VOCODER_SHIFTED_MIN)) {
            fc = BOX_AUDIO_VOCODER_SHIFTED_MIN;
        } else if (fc > top) {
            fc = top;
        }

        if (fm >= BOX_AUDIO_VOCODER_UNVOICED_FROM && b < v->unvoiced_from) {
            v->unvoiced_from = b;
        }

        box_audio_vocoder_stage(band->mod_c, fm, k, core->sample_rate);
        box_audio_vocoder_stage(band->car_c, fc, k, core->sample_rate);
        band->gain = (float)(box_audio_synth_exp2(-0.5 * box_audio_synth_log2(
            box_audio_vocoder_width(fm, q, rate))) / BOX_AUDIO_VOCODER_MEAN_ABS);
        sum += box_audio_vocoder_width(fc, q, rate);
    }

    gain = box_audio_synth_exp2(-0.5 * box_audio_synth_log2(sum)) /
        (double)BOX_AUDIO_VOCODER_REFERENCE;
    for (b = 0; b < v->bands; b ++) {
        v->band[b].gain = (float)((double)v->band[b].gain * gain);
    }
}

/** Apply the bands of the target on clear bands.
 *
 * @param core The core
 * @param v The vocoder
 */
static void box_audio_vocoder_settle(
    const box_audio_core_t *core,
    box_audio_vocoder_t *v)
{
    float *f = (float*)v->band;
    int32_t i, n = box_audio_vocoder_floats(core->sample_rate);
    for (i = 0; i < n; i ++) {
        f[i] = 0;
    }

    v->bands = (int32_t)v->target.bands;
    v->swap = false;
    box_audio_vocoder_coefs(core, v);
    box_audio_ramp_set(&v->fade, 1);
}

/** Get the control blocks of a ramp of BOX_AUDIO_VOCODER_RAMP.
 *
 * @param core The core
 * @return Control blocks
 */
static int32_t box_audio_vocoder_controls(
    const box_audio_core_t *core)
{
    return (int32_t)(BOX_AUDIO_VOCODER_RAMP * core->sample_rate) / BOX_AUDIO_VOCODER_CONTROL;
}

void box_audio_vocoder_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_vocoder_t *v = &fx->data.vocoder;
    (void)fade_in;
    box_audio_vocoder_target(core, fx);
    v->band = (box_audio_vocoder_band_t*)fx->memory;
    v->rng = BOX_AUDIO_VOCODER_SEED;
    v->control = 0;
    box_audio_ramp_set(&v->low, (float)box_audio_synth_log2((double)v->target.low));
    box_audio_ramp_set(&v->high, (float)box_audio_synth_log2((double)v->target.high));
    box_audio_ramp_set(&v->q, v->target.q);
    box_audio_ramp_set(&v->shift, v->target.shift);
    box_audio_ramp_set(&v->unvoiced, v->target.unvoiced);
    box_audio_ramp_set(&v->mix, v->target.mix);
    box_audio_vocoder_settle(core, v);
}

void box_audio_vocoder_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_vocoder_t *v = &fx->data.vocoder;
    int32_t ramp = (int32_t)(BOX_AUDIO_VOCODER_RAMP * core->sample_rate);
    int32_t controls = box_audio_vocoder_controls(core);
    box_audio_vocoder_target(core, fx);
    box_audio_ramp_to(&v->low, (float)box_audio_synth_log2((double)v->target.low), controls);
    box_audio_ramp_to(&v->high, (float)box_audio_synth_log2((double)v->target.high),
        controls);
    box_audio_ramp_to(&v->q, v->target.q, controls);
    box_audio_ramp_to(&v->shift, v->target.shift, controls);
    box_audio_ramp_to(&v->unvoiced, v->target.unvoiced, ramp);
    box_audio_ramp_to(&v->mix, v->target.mix, ramp);
    if ((int32_t)v->target.bands != v->bands && !v->swap) {
        v->swap = true;
        box_audio_ramp_to(&v->fade, 0, ramp);
    }
}

/** Filter a frame through two cascaded state variable bandpasses of the same coefficients,
 * unity at their center.
 *
 * @param s States ic1 and ic2 of the two stages
 * @param c a1, a2, a3 and k
 * @param x The input
 * @return The output
 */
static inline float box_audio_vocoder_bandpass(
    float (*s)[2],
    const float *c,
    float x)
{
    int32_t k;
    for (k = 0; k < 2; k ++) {
        float v3 = x - s[k][1];
        float v1 = c[0] * s[k][0] + c[1] * v3;
        float v2 = s[k][1] + c[1] * s[k][0] + c[2] * v3;
        s[k][0] = box_audio_clean(2 * v1 - s[k][0]);
        s[k][1] = box_audio_clean(2 * v2 - s[k][1]);
        x = c[3] * v1;
    }

    return x;
}

/** Get the next value of the noise, xorshift32.
 *
 * @param v The vocoder
 * @return A value in -1..1
 */
static inline float box_audio_vocoder_noise(
    box_audio_vocoder_t *v)
{
    uint32_t x = v->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->rng = x;

    return (float)(int32_t)x * BOX_AUDIO_VOCODER_NOISE_SCALE;
}

bool box_audio_vocoder_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_vocoder_t *v = &fx->data.vocoder;
    float *io[2] = { l, r };
    const float *key[2];
    uint32_t bus = fx->key;
    int32_t i, b, c;
    (void)silent;
    if (!(bus < BOX_AUDIO_BUS_MAX && fx->bus < BOX_AUDIO_BUS_MAX && bus != fx->bus &&
        core->bus_active[bus] && core->bus_active[fx->bus] &&
        core->bus_position[bus] < core->bus_position[fx->bus]))
    {
        goto done;
    }

    key[0] = core->buses[bus].l;
    key[1] = core->buses[bus].r;
    for (i = 0; i < frames; i ++) {
        float noise, mix, fade, x[2], xn[2], m[2], wet[2] = { 0, 0 };
        if (v->control == 0) {
            if (v->low.left || v->high.left || v->q.left || v->shift.left) {
                box_audio_ramp_next(&v->low);
                box_audio_ramp_next(&v->high);
                box_audio_ramp_next(&v->q);
                box_audio_ramp_next(&v->shift);
                box_audio_vocoder_coefs(core, v);
            }

            v->control = BOX_AUDIO_VOCODER_CONTROL;
        }

        v->control --;
        noise = box_audio_vocoder_noise(v) * box_audio_ramp_next(&v->unvoiced) *
            BOX_AUDIO_VOCODER_NOISE;
        mix = box_audio_ramp_next(&v->mix);
        fade = box_audio_ramp_next(&v->fade);
        for (c = 0; c < 2; c ++) {
            x[c] = io[c][i];
            xn[c] = x[c] + noise;
            m[c] = key[c][i];
        }

        for (b = 0; b < v->bands; b ++) {
            box_audio_vocoder_band_t *band = &v->band[b];
            const float *in = b < v->unvoiced_from ? x : xn;
            for (c = 0; c < 2; c ++) {
                float y = box_audio_vocoder_bandpass(band->mod[c], band->mod_c, m[c]);
                float a = y < 0 ? -y : y, e = band->env[c];
                e = box_audio_clean(e + (a - e) * (a > e ? v->attack : v->release));
                band->env[c] = e;
                y = box_audio_vocoder_bandpass(band->car[c], band->car_c, in[c]);
                wet[c] += y * e * band->gain;
            }
        }

        for (c = 0; c < 2; c ++) {
            io[c][i] = x[c] * (1 - mix) + wet[c] * (mix * fade);
        }

        if (v->swap && v->fade.left == 0 && v->fade.value == 0) {
            box_audio_vocoder_settle(core, v);
            box_audio_ramp_set(&v->fade, 0);
            box_audio_ramp_to(&v->fade, 1, (int32_t)(BOX_AUDIO_VOCODER_RAMP *
                core->sample_rate));
        }
    }

done:
    return false;
}

void box_audio_vocoder_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_vocoder_t *v = &fx->data.vocoder;
    (void)frames;
    if (v->swap || v->bands != (int32_t)v->target.bands) {
        box_audio_vocoder_settle(core, v);
    }

    box_audio_ramp_set(&v->unvoiced, v->unvoiced.target);
    box_audio_ramp_set(&v->mix, v->mix.target);
}
