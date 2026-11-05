/**
 * @file audio/core/saturator.c
 * @brief The saturator kind of effect: on each channel a highpass, a tone tilt, the
 * oversampled clipper of its mode (saturate.h), a DC blocker, the inverse tilt, a lowpass, a
 * sample and hold, a bit crusher and a mix; its params are a box_audio_saturator_params_t.
 *
 * The wet signal comes BOX_AUDIO_SATURATE_LATENCY frames late, the wavefolder
 * BOX_AUDIO_SATURATE_LATENCY_FOLD more, and the dry signal runs through a line as long. The
 * tanh mode without the filters and the hold runs as it did before the modes, sample for sample.
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

/** A turn of the sample and hold, 2^32 */
#define BOX_AUDIO_SATURATOR_TURN 4294967296.0

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

/** Magnitude of a value.
 *
 * @param v The value
 * @return |v|
 */
static inline float box_audio_saturator_abs(
    float v)
{
    return v < 0 ? -v : v;
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
    s->target.mode = p[4] >= 0 && p[4] < (float)BOX_AUDIO_SATURATOR_MODES - 0.5f ?
        (float)(int32_t)(p[4] + 0.5f) : (float)BOX_AUDIO_SATURATOR_TANH;
    s->target.low_cut = box_audio_saturator_clamp(p[5], 0, BOX_AUDIO_SATURATOR_CUT_MAX);
    s->target.high_cut = box_audio_saturator_clamp(p[6], 0, BOX_AUDIO_SATURATOR_CUT_MAX);
    s->target.tone = box_audio_saturator_clamp(p[7], -1, 1);
    s->target.rate = box_audio_saturator_clamp(p[8], 0, 1e6f);
    if (s->target.rate > 0 && s->target.rate < BOX_AUDIO_SATURATOR_RATE_MIN) {
        s->target.rate = BOX_AUDIO_SATURATOR_RATE_MIN;
    }
}

/** The values the ramps of the filters and the hold move to for the target: the low cut
 * under the high cut that counts as off, the high cut at it when it is off, and the rate at
 * the rate of the core when it is off or above it.
 *
 * @param s The saturator
 * @param low Receives the low cut
 * @param high Receives the high cut
 * @param rate Receives the rate
 */
static void box_audio_saturator_filters(
    const box_audio_saturator_t *s,
    float *low,
    float *high,
    float *rate)
{
    float h = s->target.high_cut;
    *low = s->target.low_cut < s->high_off ? s->target.low_cut : s->high_off;
    *high = h > 0 && h < s->high_off ? h : s->high_off;
    *rate = s->target.rate > 0 && s->target.rate < s->rate_off ? s->target.rate : s->rate_off;
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

/** Set the shape of a mode other than tanh for a drive and an asymmetry.
 *
 * @param s The saturator
 * @param drive 0..1
 * @param asymmetry 0..1
 */
static void box_audio_saturator_shape(
    box_audio_saturator_t *s,
    float drive,
    float asymmetry)
{
    float gain = (float)box_audio_synth_exp2((double)(drive * BOX_AUDIO_SATURATOR_DRIVE_DB) *
        BOX_AUDIO_SATURATOR_OCTAVES);
    box_audio_saturate_shape(&s->shape, s->mode, gain,
        s->mode == BOX_AUDIO_SATURATOR_RECTIFY ? asymmetry : asymmetry * BOX_AUDIO_SATURATOR_BIAS,
        BOX_AUDIO_SATURATE_REFERENCE);
    s->shape_at[0] = drive;
    s->shape_at[1] = asymmetry;
}

/** Set the coefficients of the tilts for a tone: the bilinear transform of
 * (a s + w) / (s + a w) for the pivot w of BOX_AUDIO_SATURATOR_TONE_HZ before the clipper, 1
 * at the pivot, a at the highs and 1 / a at the lows, and its exact inverse after it.
 *
 * @param s The saturator
 * @param rate Frames per second
 * @param tone -1..1, not 0
 */
static void box_audio_saturator_tilt(
    box_audio_saturator_t *s,
    float rate,
    float tone)
{
    double k = (double)box_audio_svf_g(BOX_AUDIO_SATURATOR_TONE_HZ, rate);
    double a = box_audio_synth_exp2((double)(tone * BOX_AUDIO_SATURATOR_TONE_DB) *
        BOX_AUDIO_SATURATOR_OCTAVES);
    double b0 = (a + k) / (1 + a * k), b1 = (k - a) / (1 + a * k);
    double a1 = (a * k - 1) / (1 + a * k);
    s->tilt[0] = (float)b0;
    s->tilt[1] = (float)b1;
    s->tilt[2] = (float)a1;
    s->tilt[3] = (float)(1.0 / b0);
    s->tilt[4] = (float)(a1 / b0);
    s->tilt[5] = (float)(b1 / b0);
    s->tone_at = tone;
}

/** Clear the highpasses of the low cut.
 *
 * @param s The saturator
 */
static void box_audio_saturator_low_clear(
    box_audio_saturator_t *s)
{
    int32_t c;
    for (c = 0; c < 2; c ++) {
        s->low[c].ic1 = 0;
        s->low[c].ic2 = 0;
    }

    s->low_at = 0;
}

/** Clear the lowpasses of the high cut.
 *
 * @param s The saturator
 */
static void box_audio_saturator_high_clear(
    box_audio_saturator_t *s)
{
    int32_t c;
    for (c = 0; c < 2; c ++) {
        s->high[c].ic1 = 0;
        s->high[c].ic2 = 0;
    }

    s->high_at = s->high_off;
}

/** Clear the tilts.
 *
 * @param s The saturator
 */
static void box_audio_saturator_tilt_clear(
    box_audio_saturator_t *s)
{
    int32_t c;
    for (c = 0; c < 2; c ++) {
        s->pre[c] = (box_audio_saturate_tilt_t){0};
        s->post[c] = (box_audio_saturate_tilt_t){0};
    }

    s->tone_at = 0;
}

/** Clear the sample and hold, so that it takes the next sample.
 *
 * @param s The saturator
 */
static void box_audio_saturator_hold_clear(
    box_audio_saturator_t *s)
{
    s->hold[0] = 0;
    s->hold[1] = 0;
    s->phase = UINT32_MAX;
    s->rate_at = s->rate_off;
}

/** Set the step of the sample and hold for a rate, rounded up, so that a rate that divides
 * the rate of the core holds each sample for the same frames.
 *
 * @param s The saturator
 * @param rate Hz, under the rate of the core
 * @param rate_hz Frames per second of the core
 */
static void box_audio_saturator_step(
    box_audio_saturator_t *s,
    float rate,
    float rate_hz)
{
    double step = (double)rate / (double)rate_hz * BOX_AUDIO_SATURATOR_TURN;
    s->rate_step = (uint32_t)step;
    if ((double)s->rate_step < step) {
        s->rate_step ++;
    }

    s->rate_at = rate;
}

/** Clear the second oversamplers of the wavefolder.
 *
 * @param s The saturator
 */
static void box_audio_saturator_fold_clear(
    box_audio_saturator_t *s)
{
    int32_t c;
    for (c = 0; c < 2; c ++) {
        box_audio_oversample_clear(&s->fold[c]);
        s->late[c] = 0;
    }
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

/** Clear the oversamplers, the DC blockers, the filters, the samples held and the dry lines;
 * the ADAA starts from the driven input of silence. The phase of the sample and hold runs on,
 * so that it takes its samples at the same frames however the frames are split into blocks.
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
        s->adaa[c] = (double)s->shape.bias;
        for (i = 0; i < 32; i ++) {
            s->dry[c][i] = 0;
        }

        s->hold[c] = 0;
    }

    box_audio_saturator_fold_clear(s);
    box_audio_saturator_low_clear(s);
    box_audio_saturator_high_clear(s);
    box_audio_saturator_tilt_clear(s);
    s->pos = 0;
}

void box_audio_saturator_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_saturator_t *s = &fx->data.saturator;
    float low, high, rate;
    int32_t c;
    (void)fade_in;
    box_audio_saturator_target(fx);
    s->high_off = BOX_AUDIO_SATURATOR_CUT_NYQUIST * core->sample_rate;
    s->rate_off = core->sample_rate;
    s->mode = (int32_t)s->target.mode;
    for (c = 0; c < 2; c ++) {
        s->low[c].k = (float)BOX_AUDIO_SVF_SQRT2;
        s->high[c].k = (float)BOX_AUDIO_SVF_SQRT2;
    }

    s->shape = (box_audio_saturate_shape_t){0};
    if (s->mode != BOX_AUDIO_SATURATOR_TANH) {
        box_audio_saturator_shape(s, s->target.drive, s->target.asymmetry);
    }

    box_audio_saturator_clear(s);
    box_audio_saturator_hold_clear(s);
    s->dc_coef = (float)(1.0 - 2.0 * 3.14159265358979323846 *
        (double)BOX_AUDIO_SATURATOR_DC / (double)core->sample_rate);
    box_audio_saturator_filters(s, &low, &high, &rate);
    box_audio_ramp_set(&s->drive, s->target.drive);
    box_audio_ramp_set(&s->asymmetry, s->target.asymmetry);
    box_audio_ramp_set(&s->bits, s->target.bits);
    box_audio_ramp_set(&s->mix, s->target.mix);
    box_audio_ramp_set(&s->low_cut, low);
    box_audio_ramp_set(&s->high_cut, high);
    box_audio_ramp_set(&s->tone, s->target.tone);
    box_audio_ramp_set(&s->rate, rate);
    box_audio_saturator_curve(s, s->target.drive, s->target.asymmetry);
    box_audio_saturator_scale(s, s->target.bits);
}

void box_audio_saturator_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_saturator_t *s = &fx->data.saturator;
    int32_t ramp = (int32_t)(BOX_AUDIO_SATURATOR_RAMP * core->sample_rate);
    float low, high, rate;
    box_audio_saturator_target(fx);
    box_audio_saturator_filters(s, &low, &high, &rate);
    if ((int32_t)s->target.mode != s->mode) {
        s->mode = (int32_t)s->target.mode;
        if (s->mode != BOX_AUDIO_SATURATOR_TANH) {
            box_audio_saturator_shape(s, s->drive.value, s->asymmetry.value);
        }

        if (s->mode == BOX_AUDIO_SATURATOR_FOLD) {
            box_audio_saturator_fold_clear(s);
        }
    }

    box_audio_ramp_to(&s->drive, s->target.drive, ramp);
    box_audio_ramp_to(&s->asymmetry, s->target.asymmetry, ramp);
    box_audio_ramp_to(&s->bits, s->target.bits, ramp);
    box_audio_ramp_to(&s->mix, s->target.mix, ramp);
    box_audio_ramp_to(&s->low_cut, low, ramp);
    box_audio_ramp_to(&s->high_cut, high, ramp);
    box_audio_ramp_to(&s->tone, s->target.tone, ramp);
    box_audio_ramp_to(&s->rate, rate, ramp);
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
    float driven = 0, clipped = 0, sum = 0, gain, slope;
    float rate_hz = core->sample_rate;
    bool result = false;
    int32_t i, c;
    for (i = 0; i < frames; i ++) {
        float drive = box_audio_ramp_next(&s->drive);
        float asymmetry = box_audio_ramp_next(&s->asymmetry);
        float bits = box_audio_ramp_next(&s->bits);
        float mix = box_audio_ramp_next(&s->mix);
        float low = box_audio_ramp_next(&s->low_cut);
        float high = box_audio_ramp_next(&s->high_cut);
        float tone = box_audio_ramp_next(&s->tone);
        float rate = box_audio_ramp_next(&s->rate);
        float comp;
        int32_t mode = s->mode;
        uint32_t late = mode == BOX_AUDIO_SATURATOR_FOLD ?
            BOX_AUDIO_SATURATE_LATENCY + BOX_AUDIO_SATURATE_LATENCY_FOLD :
            BOX_AUDIO_SATURATE_LATENCY;
        uint32_t at = s->pos & 31, from = (s->pos - late) & 31;
        bool hold = rate < s->rate_off, take = true;
        if (mode == BOX_AUDIO_SATURATOR_TANH) {
            if (drive != s->curve_at[0] || asymmetry != s->curve_at[1]) {
                box_audio_saturator_curve(s, drive, asymmetry);
            }

            comp = s->curve.comp;
        } else {
            if (drive != s->shape_at[0] || asymmetry != s->shape_at[1]) {
                box_audio_saturator_shape(s, drive, asymmetry);
            }

            comp = s->shape.comp;
        }

        if (bits != s->scale_at) {
            box_audio_saturator_scale(s, bits);
        }

        if (low > 0) {
            if (low != s->low_at) {
                float g = box_audio_svf_g((double)low, rate_hz);
                box_audio_svf_coefs(&s->low[0], g);
                box_audio_svf_coefs(&s->low[1], g);
                s->low_at = low;
            }
        } else if (s->low_at != 0) {
            box_audio_saturator_low_clear(s);
        }

        if (high < s->high_off) {
            if (high != s->high_at) {
                float g = box_audio_svf_g((double)high, rate_hz);
                box_audio_svf_coefs(&s->high[0], g);
                box_audio_svf_coefs(&s->high[1], g);
                s->high_at = high;
            }
        } else if (s->high_at != s->high_off) {
            box_audio_saturator_high_clear(s);
        }

        if (tone != 0) {
            if (tone != s->tone_at) {
                box_audio_saturator_tilt(s, rate_hz, tone);
            }
        } else if (s->tone_at != 0) {
            box_audio_saturator_tilt_clear(s);
        }

        if (hold) {
            uint32_t phase = s->phase;
            if (rate != s->rate_at) {
                box_audio_saturator_step(s, rate, rate_hz);
            }

            s->phase = phase + s->rate_step;
            take = s->phase < phase;
        } else if (s->rate_at != s->rate_off) {
            box_audio_saturator_hold_clear(s);
        }

        for (c = 0; c < 2; c ++) {
            float x = io[c][i], in = x, a, b, ca, cb, y, d, m;
            if (low > 0) {
                in = box_audio_svf_next(&s->low[c], BOX_AUDIO_SYNTH_HIGHPASS, in);
            }

            if (tone != 0) {
                in = box_audio_saturate_tilt_next(&s->pre[c], s->tilt, in);
            }

            box_audio_oversample_up(&s->os[c], in, &a, &b);
            if (mode == BOX_AUDIO_SATURATOR_TANH) {
                ca = box_audio_saturate_clip(&s->curve, a);
                cb = box_audio_saturate_clip(&s->curve, b);
            } else if (mode == BOX_AUDIO_SATURATOR_HARD) {
                ca = box_audio_saturate_adaa(&s->shape, &s->adaa[c], a);
                cb = box_audio_saturate_adaa(&s->shape, &s->adaa[c], b);
            } else if (mode == BOX_AUDIO_SATURATOR_FOLD) {
                ca = box_audio_saturate_fold_next(&s->shape, &s->fold[c], &s->adaa[c],
                    &s->late[c], a);
                cb = box_audio_saturate_fold_next(&s->shape, &s->fold[c], &s->adaa[c],
                    &s->late[c], b);
            } else {
                ca = box_audio_saturate_shape_clip(&s->shape, a);
                cb = box_audio_saturate_shape_clip(&s->shape, b);
            }

            a = a < 0 ? -a : a;
            b = b < 0 ? -b : b;
            m = a > b ? a : b;
            driven = m > driven ? m : driven;
            a = ca < 0 ? -ca : ca;
            b = cb < 0 ? -cb : cb;
            m = a > b ? a : b;
            clipped = m > clipped ? m : clipped;
            y = box_audio_oversample_down(&s->os[c], ca, cb) * comp;
            y = box_audio_clean(box_audio_dc_next(&s->dc[c], y, s->dc_coef));
            if (tone != 0) {
                y = box_audio_saturate_tilt_next(&s->post[c], s->tilt + 3, y);
            }

            if (high < s->high_off) {
                y = box_audio_svf_next(&s->high[c], BOX_AUDIO_SYNTH_LOWPASS, y);
            }

            if (hold) {
                if (take) {
                    s->hold[c] = y;
                }

                y = s->hold[c];
            }

            if (s->scale > 0) {
                y = box_audio_saturator_crush(y, s->scale);
            }

            d = s->dry[c][from];
            s->dry[c][at] = x;
            io[c][i] = box_audio_clean(d * (1 - mix) + y * mix);
        }

        s->pos ++;
    }

    if (s->mode == BOX_AUDIO_SATURATOR_TANH) {
        gain = s->curve.gain;
        slope = s->curve.slope;
    } else {
        gain = s->shape.gain;
        slope = s->shape.slope;
    }

    driven *= gain;
    if (driven > BOX_AUDIO_SATURATOR_PEAK_MIN && clipped > 0) {
        float db = (float)(BOX_AUDIO_SATURATOR_DB * box_audio_synth_log2(
            (double)driven / ((double)clipped * (double)slope)));
        if (db >= BOX_AUDIO_SATURATOR_REDUCTION_MIN) {
            box_audio_effect_reduction(core, fx, db);
        }
    }

    if (silent) {
        for (c = 0; c < 2; c ++) {
            sum += box_audio_oversample_level(&s->os[c]);
            sum += box_audio_oversample_level(&s->fold[c]) +
                box_audio_saturator_abs(s->late[c]);
            sum += (s->dc[c].x1 < 0 ? -s->dc[c].x1 : s->dc[c].x1) +
                (s->dc[c].y1 < 0 ? -s->dc[c].y1 : s->dc[c].y1);
            sum += box_audio_saturator_abs(s->low[c].ic1) + box_audio_saturator_abs(s->low[c].ic2) +
                box_audio_saturator_abs(s->high[c].ic1) +
                box_audio_saturator_abs(s->high[c].ic2) +
                box_audio_saturator_abs(s->pre[c].x1) + box_audio_saturator_abs(s->pre[c].y1) +
                box_audio_saturator_abs(s->post[c].x1) + box_audio_saturator_abs(s->post[c].y1) +
                box_audio_saturator_abs(s->hold[c]);
            for (i = 0; i < 32; i ++) {
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
    float rate;
    box_audio_ramp_set(&s->drive, s->drive.target);
    box_audio_ramp_set(&s->asymmetry, s->asymmetry.target);
    box_audio_ramp_set(&s->bits, s->bits.target);
    box_audio_ramp_set(&s->mix, s->mix.target);
    box_audio_ramp_set(&s->low_cut, s->low_cut.target);
    box_audio_ramp_set(&s->high_cut, s->high_cut.target);
    box_audio_ramp_set(&s->tone, s->tone.target);
    box_audio_ramp_set(&s->rate, s->rate.target);
    rate = s->rate.value;
    if (rate < s->rate_off) {
        if (rate != s->rate_at) {
            box_audio_saturator_step(s, rate, core->sample_rate);
        }

        s->phase += (uint32_t)frames * s->rate_step;
    } else if (s->rate_at != s->rate_off) {
        box_audio_saturator_hold_clear(s);
    }
}
