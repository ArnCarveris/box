/**
 * @file audio/core/saturator.c
 * @brief The saturator kind of effect: the oversampled tanh soft clipper of saturate.h on each
 * channel, its level compensated, with a bias for asymmetry, a DC blocker, a bit crusher and a
 * mix, reporting how hard the clipper squashes as its reduction.
 *
 * The wet signal leaves the oversampler BOX_AUDIO_SATURATE_LATENCY frames late, so the dry
 * signal runs through a line as long. The constants of the clipper change only while the drive
 * or the asymmetry move. Its params are a box_audio_saturator_params_t.
 */

#include "internal.h"

/** States that sum to less than this, -140 dB, have decayed to silence */
#define BOX_AUDIO_SATURATOR_SILENCE 1e-7f

/** Octaves of gain per dB, log2(10) / 20 */
#define BOX_AUDIO_SATURATOR_OCTAVES 0.16609640474436813

/** dB per octave of level, 20 log10(2) */
#define BOX_AUDIO_SATURATOR_DB 6.0205999132796239

/** A reduction under this many dB counts as none: the clipper is all but linear */
#define BOX_AUDIO_SATURATOR_REDUCTION_MIN 0.01f

/** Peak of the driven input under which the clipper counts as linear */
#define BOX_AUDIO_SATURATOR_PEAK_MIN 1e-6f

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_saturator_clamp(
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

/** Clamp the params of an effect into the target of its saturator.
 *
 * @param fx The effect
 */
static void box_audio_saturator_target(
    box_audio_effect_t *fx)
{
    box_audio_saturator_t *s = &fx->data.saturator;
    const float *p = fx->params;
    s->target.drive = box_audio_saturator_clamp(p[0], 0, 1);
    s->target.asymmetry = box_audio_saturator_clamp(p[1], 0, 1);
    s->target.bits = box_audio_saturator_clamp(p[2], BOX_AUDIO_SATURATOR_BITS_MIN,
        BOX_AUDIO_SATURATOR_BITS_MAX);
    s->target.mix = box_audio_saturator_clamp(p[3], 0, 1);
}

/** Set the clipper for a drive and an asymmetry.
 *
 * @param s The saturator
 * @param drive 0..1
 * @param asymmetry 0..1
 */
static void box_audio_saturator_curve(
    box_audio_saturator_t *s,
    float drive,
    float asymmetry)
{
    float gain = (float)box_audio_synth_exp2((double)(drive * BOX_AUDIO_SATURATOR_DRIVE_DB) *
        BOX_AUDIO_SATURATOR_OCTAVES);
    box_audio_saturate_curve(&s->curve, gain, asymmetry * BOX_AUDIO_SATURATOR_BIAS);
    s->curve_at[0] = drive;
    s->curve_at[1] = asymmetry;
}

/** Set the steps of the bit crusher for its bits.
 *
 * @param s The saturator
 * @param bits BOX_AUDIO_SATURATOR_BITS_MIN..MAX
 */
static void box_audio_saturator_scale(
    box_audio_saturator_t *s,
    float bits)
{
    s->scale = bits < BOX_AUDIO_SATURATOR_BITS_MAX ?
        (float)box_audio_synth_exp2((double)bits - 1.0) : 0.0f;
    s->scale_at = bits;
}

/** Clear the oversamplers, the DC blockers and the dry lines.
 *
 * @param s The saturator
 */
static void box_audio_saturator_clear(
    box_audio_saturator_t *s)
{
    int32_t c, i;
    for (c = 0; c < 2; c ++) {
        box_audio_oversample_clear(&s->os[c]);
        s->dc[c].x1 = 0;
        s->dc[c].y1 = 0;
        for (i = 0; i < 16; i ++) {
            s->dry[c][i] = 0;
        }
    }

    s->pos = 0;
}

void box_audio_saturator_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_saturator_t *s = &fx->data.saturator;
    (void)fade_in;
    box_audio_saturator_target(fx);
    box_audio_saturator_clear(s);
    s->dc_coef = (float)(1.0 - 2.0 * 3.14159265358979323846 *
        (double)BOX_AUDIO_SATURATOR_DC / (double)core->sample_rate);
    box_audio_ramp_set(&s->drive, s->target.drive);
    box_audio_ramp_set(&s->asymmetry, s->target.asymmetry);
    box_audio_ramp_set(&s->bits, s->target.bits);
    box_audio_ramp_set(&s->mix, s->target.mix);
    box_audio_saturator_curve(s, s->target.drive, s->target.asymmetry);
    box_audio_saturator_scale(s, s->target.bits);
}

void box_audio_saturator_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_saturator_t *s = &fx->data.saturator;
    int32_t ramp = (int32_t)(BOX_AUDIO_SATURATOR_RAMP * core->sample_rate);
    box_audio_saturator_target(fx);
    box_audio_ramp_to(&s->drive, s->target.drive, ramp);
    box_audio_ramp_to(&s->asymmetry, s->target.asymmetry, ramp);
    box_audio_ramp_to(&s->bits, s->target.bits, ramp);
    box_audio_ramp_to(&s->mix, s->target.mix, ramp);
}

/** Round a sample to the steps of the bit crusher: scale steps of each sign, the highest
 * scale - 1 steps up, so that 0 stays 0 and bits b give 2^b levels.
 *
 * @param y The sample, finite
 * @param scale Steps of each sign
 * @return The rounded sample
 */
static inline float box_audio_saturator_crush(
    float y,
    float scale)
{
    double v = (double)y * (double)scale, lo = -(double)scale, hi = (double)scale - 1.0;
    v = v < lo ? lo : (v > hi ? hi : v);
    v = (double)((int64_t)(v + 65536.5) - 65536);

    return (float)(v / (double)scale);
}

bool box_audio_saturator_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_saturator_t *s = &fx->data.saturator;
    float *io[2] = { l, r };
    float driven = 0, clipped = 0, sum = 0;
    bool result = false;
    int32_t i, c;
    for (i = 0; i < frames; i ++) {
        float drive = box_audio_ramp_next(&s->drive);
        float asymmetry = box_audio_ramp_next(&s->asymmetry);
        float bits = box_audio_ramp_next(&s->bits);
        float mix = box_audio_ramp_next(&s->mix);
        uint32_t at = s->pos & 15, from = (s->pos + 1) & 15;
        if (drive != s->curve_at[0] || asymmetry != s->curve_at[1]) {
            box_audio_saturator_curve(s, drive, asymmetry);
        }

        if (bits != s->scale_at) {
            box_audio_saturator_scale(s, bits);
        }

        for (c = 0; c < 2; c ++) {
            float x = io[c][i], a, b, ca, cb, y, d, m;
            box_audio_oversample_up(&s->os[c], x, &a, &b);
            ca = box_audio_saturate_clip(&s->curve, a);
            cb = box_audio_saturate_clip(&s->curve, b);
            a = a < 0 ? -a : a;
            b = b < 0 ? -b : b;
            m = a > b ? a : b;
            driven = m > driven ? m : driven;
            a = ca < 0 ? -ca : ca;
            b = cb < 0 ? -cb : cb;
            m = a > b ? a : b;
            clipped = m > clipped ? m : clipped;
            y = box_audio_oversample_down(&s->os[c], ca, cb) * s->curve.comp;
            y = box_audio_clean(box_audio_dc_next(&s->dc[c], y, s->dc_coef));
            if (s->scale > 0) {
                y = box_audio_saturator_crush(y, s->scale);
            }

            d = s->dry[c][from];
            s->dry[c][at] = x;
            io[c][i] = box_audio_clean(d * (1 - mix) + y * mix);
        }

        s->pos ++;
    }

    driven *= s->curve.gain;
    if (driven > BOX_AUDIO_SATURATOR_PEAK_MIN && clipped > 0) {
        float db = (float)(BOX_AUDIO_SATURATOR_DB * box_audio_synth_log2(
            (double)driven / ((double)clipped * (double)s->curve.slope)));
        if (db >= BOX_AUDIO_SATURATOR_REDUCTION_MIN) {
            box_audio_effect_reduction(core, fx, db);
        }
    }

    if (silent) {
        for (c = 0; c < 2; c ++) {
            sum += box_audio_oversample_level(&s->os[c]);
            sum += (s->dc[c].x1 < 0 ? -s->dc[c].x1 : s->dc[c].x1) +
                (s->dc[c].y1 < 0 ? -s->dc[c].y1 : s->dc[c].y1);
            for (i = 0; i < 16; i ++) {
                sum += s->dry[c][i] < 0 ? -s->dry[c][i] : s->dry[c][i];
            }
        }

        if (sum < BOX_AUDIO_SATURATOR_SILENCE) {
            box_audio_saturator_clear(s);
            result = true;
        }
    }

    return result;
}

void box_audio_saturator_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_saturator_t *s = &fx->data.saturator;
    (void)core;
    (void)frames;
    box_audio_ramp_set(&s->drive, s->drive.target);
    box_audio_ramp_set(&s->asymmetry, s->asymmetry.target);
    box_audio_ramp_set(&s->bits, s->bits.target);
    box_audio_ramp_set(&s->mix, s->mix.target);
}
