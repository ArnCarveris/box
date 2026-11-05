/**
 * @file audio/core/dynamics.c
 * @brief The dynamics kinds of effect: a compressor that may key on another bus, for ducking,
 * a gate and a transient shaper, all stereo linked on a peak detector; the compressor and the
 * gate report their gain reduction.
 *
 * The compressor is feed-forward and smooths the level in dB; a soft knee turns it into a gain.
 * The gate has a hold and BOX_AUDIO_GATE_HYSTERESIS. The transient shaper compares a fast and
 * a slow envelope of the peak in dB, without a threshold; see core.h for all three.
 */

#include "internal.h"

/** 1 / ln(2) */
#define BOX_AUDIO_DYNAMICS_LOG2E 1.4426950408889634

/** dB per octave of level, 20 log10(2) */
#define BOX_AUDIO_DYNAMICS_DB 6.0205999132796239f

/** Octaves of level per dB, log2(10) / 20 */
#define BOX_AUDIO_DYNAMICS_OCTAVES 0.16609640474436813f

/** Lowest level the compressor detects, BOX_AUDIO_DYNAMICS_FLOOR in linear */
#define BOX_AUDIO_DYNAMICS_TINY 1e-6f

/** dB from its target at which the smoothed level of the compressor snaps to it */
#define BOX_AUDIO_DYNAMICS_SNAP 1e-6

/** A gain this close to its target snaps to it */
#define BOX_AUDIO_GATE_SNAP 1e-7f

/** An envelope of a transient shaper under this level with nothing coming in falls to 0;
 * under it, BOX_AUDIO_DYNAMICS_TINY, the gain reads it as that level anyway */
#define BOX_AUDIO_TRANSIENT_SNAP 1e-6

/** Gain of a transient shaper in dB this close to its target snaps to it */
#define BOX_AUDIO_TRANSIENT_GAIN_SNAP 1e-6

/** Clamp a parameter; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_dynamics_clamp(
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

/** Base 2 logarithm of a positive normal float, from its exponent and the series of atanh of
 * its mantissa, within 1e-7.
 *
 * @param x The value, positive and normal
 * @return The logarithm
 */
static inline float box_audio_dynamics_log2(
    float x)
{
    union { float f; uint32_t u; } v;
    int32_t e;
    float m, s, s2;
    v.f = x;
    e = (int32_t)((v.u >> 23) & 0xFF) - 127;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
    m = v.f;
    if (m > 1.41421356f) {
        m *= 0.5f;
        e ++;
    }

    s = (m - 1) / (m + 1);
    s2 = s * s;

    return (float)e + s * (2.88539008f + s2 * (0.961796694f + s2 * (0.577078016f +
        s2 * (0.412198583f + s2 * 0.320598898f))));
}

/** Power of 2 from the exponent and a series over the fraction, within 1e-6 relative.
 *
 * @param y The exponent, clamped to -126..126
 * @return 2 to the y
 */
static inline float box_audio_dynamics_exp2(
    float y)
{
    union { float f; uint32_t u; } v;
    int32_t n;
    float r, p;
    if (!(y > -126.0f)) {
        y = -126.0f;
    } else if (y > 126.0f) {
        y = 126.0f;
    }

    n = (int32_t)y;
    if ((float)n > y) {
        n --;
    }

    r = (y - (float)n) * 0.693147181f;
    p = 1 + r * (1 + r * (0.5f + r * (0.166666667f + r * (0.0416666667f + r *
        (0.00833333333f + r * (0.00138888889f + r * 0.000198412698f))))));
    v.u = (uint32_t)(n + 127) << 23;

    return p * v.f;
}

/** Share of a distance that stays per frame for an exponential move that covers 63% of it in
 * a time.
 *
 * @param core The core
 * @param seconds The time
 * @return The share
 */
static double box_audio_dynamics_coef(
    const box_audio_core_t *core,
    float seconds)
{
    return box_audio_synth_exp2(-BOX_AUDIO_DYNAMICS_LOG2E /
        ((double)seconds * (double)core->sample_rate));
}

/** Linear gain of a level in dB, at the precision of the double functions.
 *
 * @param db The level
 * @return The gain
 */
static float box_audio_dynamics_gain(
    float db)
{
    return (float)box_audio_synth_exp2((double)db * (double)BOX_AUDIO_DYNAMICS_OCTAVES);
}

/** Start a peak detector clear.
 *
 * @param core The core
 * @param p The detector
 */
static void box_audio_peak_start(
    const box_audio_core_t *core,
    box_audio_peak_t *p)
{
    int32_t frames = (int32_t)(BOX_AUDIO_DYNAMICS_WINDOW * core->sample_rate);
    p->frames = frames > 0 ? frames : 1;
    p->left = p->frames;
    p->current = 0;
    p->last = 0;
}

/** Feed a frame to a peak detector.
 *
 * @param p The detector
 * @param l Left sample
 * @param r Right sample
 * @return The peak of the current and the last window
 */
static inline float box_audio_peak_next(
    box_audio_peak_t *p,
    float l,
    float r)
{
    float a = l < 0 ? -l : l, b = r < 0 ? -r : r, result;
    a = b > a ? b : a;
    p->current = a > p->current ? a : p->current;
    result = p->current > p->last ? p->current : p->last;
    if (-- p->left <= 0) {
        p->last = p->current;
        p->current = 0;
        p->left = p->frames;
    }

    return result;
}

/** Clamp the params of an effect into the target of its compressor and set its ballistics.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_compressor_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_compressor_t *c = &fx->data.compressor;
    const float *p = fx->params;
    c->target.threshold = box_audio_dynamics_clamp(p[0], BOX_AUDIO_COMPRESSOR_THRESHOLD_MIN, 0);
    c->target.ratio = box_audio_dynamics_clamp(p[1], 1, BOX_AUDIO_COMPRESSOR_RATIO_MAX);
    c->target.attack = box_audio_dynamics_clamp(p[2], BOX_AUDIO_DYNAMICS_ATTACK_MIN,
        BOX_AUDIO_DYNAMICS_ATTACK_MAX);
    c->target.release = box_audio_dynamics_clamp(p[3], BOX_AUDIO_DYNAMICS_RELEASE_MIN,
        BOX_AUDIO_DYNAMICS_RELEASE_MAX);
    c->target.knee = box_audio_dynamics_clamp(p[4], 0, BOX_AUDIO_COMPRESSOR_KNEE_MAX);
    c->target.makeup = box_audio_dynamics_clamp(p[5], 0, BOX_AUDIO_COMPRESSOR_MAKEUP_MAX);
    c->target.mix = box_audio_dynamics_clamp(p[6], 0, 1);
    c->attack = box_audio_dynamics_coef(core, c->target.attack);
    c->release = box_audio_dynamics_coef(core, c->target.release);
}

void box_audio_compressor_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_compressor_t *c = &fx->data.compressor;
    (void)fade_in;
    box_audio_compressor_target(core, fx);
    box_audio_peak_start(core, &c->peak);
    c->level = BOX_AUDIO_DYNAMICS_FLOOR;
    box_audio_ramp_set(&c->threshold, c->target.threshold);
    box_audio_ramp_set(&c->knee, c->target.knee);
    box_audio_ramp_set(&c->slope, 1.0f / c->target.ratio - 1.0f);
    box_audio_ramp_set(&c->makeup, c->target.makeup);
    box_audio_ramp_set(&c->mix, c->target.mix);
}

void box_audio_compressor_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_compressor_t *c = &fx->data.compressor;
    int32_t ramp = (int32_t)(BOX_AUDIO_COMPRESSOR_RAMP * core->sample_rate);
    box_audio_compressor_target(core, fx);
    box_audio_ramp_to(&c->threshold, c->target.threshold, ramp);
    box_audio_ramp_to(&c->knee, c->target.knee, ramp);
    box_audio_ramp_to(&c->slope, 1.0f / c->target.ratio - 1.0f, ramp);
    box_audio_ramp_to(&c->makeup, c->target.makeup, ramp);
    box_audio_ramp_to(&c->mix, c->target.mix, ramp);
}

/** The gain computer: the change of a level in dB, 0 or less, with a soft knee.
 *
 * @param level The level in dB
 * @param threshold Threshold in dB
 * @param knee Width of the knee in dB
 * @param slope 1 / ratio - 1
 * @return The change in dB
 */
static inline float box_audio_compressor_curve(
    float level,
    float threshold,
    float knee,
    float slope)
{
    float over = level - threshold, result;
    if (2 * over <= -knee) {
        result = 0;
    } else if (2 * over < knee) {
        float d = over + 0.5f * knee;
        result = slope * d * d / (2 * knee);
    } else {
        result = slope * over;
    }

    return result;
}

bool box_audio_compressor_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_compressor_t *c = &fx->data.compressor;
    const float *kl = l, *kr = r;
    uint32_t key = fx->key;
    float most = 0;
    int32_t i;
    (void)silent;
    if (key < BOX_AUDIO_BUS_MAX && fx->bus < BOX_AUDIO_BUS_MAX && key != fx->bus &&
        core->bus_active[key] && core->bus_active[fx->bus] &&
        core->bus_position[key] < core->bus_position[fx->bus])
    {
        kl = core->buses[key].l;
        kr = core->buses[key].r;
    }

    for (i = 0; i < frames; i ++) {
        float x = box_audio_peak_next(&c->peak, kl[i], kr[i]);
        float db = x > BOX_AUDIO_DYNAMICS_TINY ?
            BOX_AUDIO_DYNAMICS_DB * box_audio_dynamics_log2(x) : BOX_AUDIO_DYNAMICS_FLOOR;
        float threshold = box_audio_ramp_next(&c->threshold);
        float knee = box_audio_ramp_next(&c->knee);
        float slope = box_audio_ramp_next(&c->slope);
        float makeup = box_audio_ramp_next(&c->makeup);
        float mix = box_audio_ramp_next(&c->mix);
        float lo = threshold - 0.5f * knee, change, gain;
        double to = db > lo ? db : lo, d;
        c->level = c->level > lo ? c->level : lo;
        d = c->level - to;
        d *= d > 0 ? c->release : c->attack;
        c->level = d < BOX_AUDIO_DYNAMICS_SNAP && d > -BOX_AUDIO_DYNAMICS_SNAP ? to : to + d;
        change = c->level > lo ?
            box_audio_compressor_curve((float)c->level, threshold, knee, slope) : 0;
        most = -change > most ? -change : most;
        change += makeup;
        gain = change == 0 ? 1.0f :
            box_audio_dynamics_exp2(change * BOX_AUDIO_DYNAMICS_OCTAVES);
        gain = 1 + (gain - 1) * mix;
        l[i] *= gain;
        r[i] *= gain;
    }

    box_audio_effect_reduction(core, fx, most);

    return false;
}

/** Clamp the params of an effect into the target of its gate and set its levels.
 *
 * @param core The core
 * @param fx The effect
 */
static void box_audio_gate_target(
    const box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_gate_t *g = &fx->data.gate;
    const float *p = fx->params;
    g->target.threshold = box_audio_dynamics_clamp(p[0], BOX_AUDIO_GATE_MIN, 0);
    g->target.attack = box_audio_dynamics_clamp(p[1], BOX_AUDIO_DYNAMICS_ATTACK_MIN,
        BOX_AUDIO_DYNAMICS_ATTACK_MAX);
    g->target.hold = box_audio_dynamics_clamp(p[2], 0, BOX_AUDIO_GATE_HOLD_MAX);
    g->target.release = box_audio_dynamics_clamp(p[3], BOX_AUDIO_DYNAMICS_RELEASE_MIN,
        BOX_AUDIO_DYNAMICS_RELEASE_MAX);
    g->target.range = box_audio_dynamics_clamp(p[4], BOX_AUDIO_GATE_MIN, 0);
    g->open_level = box_audio_dynamics_gain(g->target.threshold);
    g->close_level = box_audio_dynamics_gain(g->target.threshold - BOX_AUDIO_GATE_HYSTERESIS);
    g->floor = box_audio_dynamics_gain(g->target.range);
    g->attack = box_audio_dynamics_coef(core, g->target.attack);
    g->release = box_audio_dynamics_coef(core, g->target.release);
    g->hold_frames = (int32_t)(g->target.hold * core->sample_rate + 0.5f);
    g->hold = g->hold < g->hold_frames ? g->hold : g->hold_frames;
}

void box_audio_gate_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_gate_t *g = &fx->data.gate;
    (void)fade_in;
    box_audio_gate_target(core, fx);
    box_audio_peak_start(core, &g->peak);
    g->open = true;
    g->hold = g->hold_frames;
    g->gain = 1;
}

void box_audio_gate_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_gate_target(core, fx);
}

bool box_audio_gate_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_gate_t *g = &fx->data.gate;
    float least = 1;
    int32_t i;
    (void)silent;
    for (i = 0; i < frames; i ++) {
        float x = box_audio_peak_next(&g->peak, l[i], r[i]), gain;
        double to, d;
        if (x >= (g->open ? g->close_level : g->open_level)) {
            g->open = true;
            g->hold = g->hold_frames;
        } else if (g->hold > 0) {
            g->hold --;
        } else {
            g->open = false;
        }

        to = g->open ? 1.0 : (double)g->floor;
        if (g->gain != to) {
            g->gain = to + (g->gain - to) * (to > g->gain ? g->attack : g->release);
            d = g->gain - to;
            if (d < BOX_AUDIO_GATE_SNAP && d > -BOX_AUDIO_GATE_SNAP) {
                g->gain = to;
            }
        }

        gain = (float)g->gain;
        least = gain < least ? gain : least;
        l[i] *= gain;
        r[i] *= gain;
    }

    if (least < 1) {
        box_audio_effect_reduction(core, fx, (float)(-(double)BOX_AUDIO_DYNAMICS_DB *
            box_audio_synth_log2((double)least)));
    }

    return false;
}

/** Clamp the params of an effect into the target of its transient shaper.
 *
 * @param fx The effect
 */
static void box_audio_transient_shaper_target(
    box_audio_effect_t *fx)
{
    box_audio_transient_shaper_t *t = &fx->data.transient_shaper;
    const float *p = fx->params;
    t->target.attack = box_audio_dynamics_clamp(p[0], -1, 1);
    t->target.sustain = box_audio_dynamics_clamp(p[1], -1, 1);
}

void box_audio_transient_shaper_start(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    bool fade_in)
{
    box_audio_transient_shaper_t *t = &fx->data.transient_shaper;
    (void)fade_in;
    box_audio_transient_shaper_target(fx);
    box_audio_peak_start(core, &t->peak);
    t->fast = 0;
    t->slow = 0;
    t->gain = 0;
    t->coef[0] = box_audio_dynamics_coef(core, BOX_AUDIO_TRANSIENT_FAST_ATTACK);
    t->coef[1] = box_audio_dynamics_coef(core, BOX_AUDIO_TRANSIENT_FAST_RELEASE);
    t->coef[2] = box_audio_dynamics_coef(core, BOX_AUDIO_TRANSIENT_SLOW_ATTACK);
    t->coef[3] = box_audio_dynamics_coef(core, BOX_AUDIO_TRANSIENT_SLOW_RELEASE);
    t->smooth = box_audio_dynamics_coef(core, BOX_AUDIO_TRANSIENT_SMOOTH);
    box_audio_ramp_set(&t->attack, t->target.attack);
    box_audio_ramp_set(&t->sustain, t->target.sustain);
}

void box_audio_transient_shaper_command(
    box_audio_core_t *core,
    box_audio_effect_t *fx)
{
    box_audio_transient_shaper_t *t = &fx->data.transient_shaper;
    int32_t ramp = (int32_t)(BOX_AUDIO_TRANSIENT_RAMP * core->sample_rate);
    box_audio_transient_shaper_target(fx);
    box_audio_ramp_to(&t->attack, t->target.attack, ramp);
    box_audio_ramp_to(&t->sustain, t->target.sustain, ramp);
}

/** Move an envelope one frame toward a level, falling to 0 once it is under
 * BOX_AUDIO_TRANSIENT_SNAP with nothing coming in.
 *
 * @param env The envelope
 * @param x The level
 * @param attack Share of the distance that stays on the way up
 * @param release Share of the distance that stays on the way down
 * @return The new envelope
 */
static inline double box_audio_transient_follow(
    double env,
    double x,
    double attack,
    double release)
{
    env = x + (env - x) * (x > env ? attack : release);
    if (x == 0 && env < BOX_AUDIO_TRANSIENT_SNAP) {
        env = 0;
    }

    return env;
}

bool box_audio_transient_shaper_render(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    float *l,
    float *r,
    int32_t frames,
    bool silent)
{
    box_audio_transient_shaper_t *t = &fx->data.transient_shaper;
    int32_t i;
    (void)core;
    for (i = 0; i < frames; i ++) {
        float x = box_audio_peak_next(&t->peak, l[i], r[i]);
        float attack = box_audio_ramp_next(&t->attack);
        float sustain = box_audio_ramp_next(&t->sustain);
        float fast, slow, held, diff, up, down, gain;
        double to, d;
        x = x >= BOX_AUDIO_EFFECT_FLOOR ? x : 0;
        t->fast = box_audio_transient_follow(t->fast, x, t->coef[0], t->coef[1]);
        t->slow = box_audio_transient_follow(t->slow, x, t->coef[2], t->coef[3]);
        fast = (float)t->fast > BOX_AUDIO_DYNAMICS_TINY ? (float)t->fast :
            BOX_AUDIO_DYNAMICS_TINY;
        slow = (float)t->slow > BOX_AUDIO_DYNAMICS_TINY ? (float)t->slow :
            BOX_AUDIO_DYNAMICS_TINY;
        held = x > fast ? x : fast;
        diff = fast == slow ? 0 : BOX_AUDIO_DYNAMICS_DB *
            (box_audio_dynamics_log2(fast) - box_audio_dynamics_log2(slow));
        up = diff > 0 ? (diff < BOX_AUDIO_TRANSIENT_RANGE ? diff : BOX_AUDIO_TRANSIENT_RANGE) : 0;
        diff = held >= slow ? 0 : BOX_AUDIO_DYNAMICS_DB *
            (box_audio_dynamics_log2(slow) - box_audio_dynamics_log2(held));
        down = diff < BOX_AUDIO_TRANSIENT_RANGE ? diff : BOX_AUDIO_TRANSIENT_RANGE;
        to = (double)(attack * up + sustain * down);
        if (t->gain != to) {
            t->gain = to + (t->gain - to) * t->smooth;
            d = t->gain - to;
            if (d < BOX_AUDIO_TRANSIENT_GAIN_SNAP && d > -BOX_AUDIO_TRANSIENT_GAIN_SNAP) {
                t->gain = to;
            }
        }

        if (t->gain != 0) {
            gain = box_audio_dynamics_exp2((float)t->gain * BOX_AUDIO_DYNAMICS_OCTAVES);
            l[i] *= gain;
            r[i] *= gain;
        }
    }

    return silent && t->fast == 0 && t->slow == 0 && t->gain == 0 &&
        t->peak.current < BOX_AUDIO_EFFECT_FLOOR && t->peak.last < BOX_AUDIO_EFFECT_FLOOR;
}

void box_audio_transient_shaper_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    box_audio_transient_shaper_t *t = &fx->data.transient_shaper;
    box_audio_peak_t *p = &t->peak;
    (void)core;
    p->left = p->frames - (int32_t)(((int64_t)(p->frames - p->left) + frames) % p->frames);
    box_audio_ramp_set(&t->attack, t->attack.target);
    box_audio_ramp_set(&t->sustain, t->sustain.target);
}

void box_audio_dynamics_rest(
    box_audio_core_t *core,
    box_audio_effect_t *fx,
    int32_t frames)
{
    (void)frames;
    (void)core;
    (void)fx;
}
