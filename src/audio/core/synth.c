/**
 * @file audio/core/synth.c
 * @brief The synth source: band limited oscillators and seeded noise, each with a linear or
 * curved envelope, mixed into a state variable filter of one or two stages, whose cutoff an
 * envelope may move, and an amp, rendered per frame from a copy of the patch. A change of the
 * pitch glides exponentially in frequency over the glide of the patch, and a legato play
 * retargets the layers of a voice without a restart of its envelopes.
 *
 * Every voice holds its own copy of the patch, so a play allocates nothing and a new patch for
 * the same sound reaches the next play while the voices that play keep theirs.
 *
 * Like the rest of the core it uses no libm: the sine is box_audio_core_sin and the exponential
 * sweeps and the filter envelope use the series of box_audio_synth_log2 and
 * box_audio_synth_exp2.
 */

#include "internal.h"

/** Natural logarithm of 2 */
#define BOX_AUDIO_SYNTH_LN2 0.69314718055994530942

/** Square root of 2: the damping of the filter at resonance 0, a Butterworth response */
#define BOX_AUDIO_SYNTH_SQRT2 1.41421356237309504880

/** Damping of the filter at resonance 1, a Q of 100 that rings without blowing up */
#define BOX_AUDIO_SYNTH_DAMPING_MIN 0.02

/** Damping of the first stage of the 24 dB filter, 2 cos(pi / 8): with the second stage at
 * resonance 0 the two make a fourth order Butterworth response */
#define BOX_AUDIO_SYNTH_BUTTERWORTH_1 1.84775906502257351225

/** Damping of the second, resonant stage of the 24 dB filter at resonance 0, 2 cos(3 pi / 8) */
#define BOX_AUDIO_SYNTH_BUTTERWORTH_2 0.76536686473017954346

/** Frames between two updates of the coefficients of a filter whose cutoff an envelope moves */
#define BOX_AUDIO_SYNTH_CONTROL 8

/** Largest share of the sample rate an oscillator plays at, under Nyquist with room for the
 * correction of the band limited waves */
#define BOX_AUDIO_SYNTH_NYQUIST 0.48

/** Largest share of the sample rate of a cutoff; a higher one, Nyquist included, is clamped */
#define BOX_AUDIO_SYNTH_CUTOFF_NYQUIST 0.49

/** Lowest frequency of an oscillator in Hz */
#define BOX_AUDIO_SYNTH_FREQUENCY_MIN 1.0f

/** Lowest cutoff in Hz */
#define BOX_AUDIO_SYNTH_CUTOFF_MIN 10.0f

/** Highest frequency or cutoff a patch keeps in Hz, before the clamp to the rate */
#define BOX_AUDIO_SYNTH_HZ_MAX 1000000.0f

/** Smallest pitch factor of a play, which the pitch variance cannot go under */
#define BOX_AUDIO_SYNTH_VARIANCE_MIN 0.01f

/** Clamp a value of a patch; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_synth_clamp(
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

/** Clamp a curve of an envelope; NaN and curves too small to bend a segment are linear.
 *
 * @param v The curve
 * @return The curve, 0 for linear
 */
static float box_audio_synth_curve_clamp(
    float v)
{
    float a = v < 0 ? -v : v;
    if (!(a >= BOX_AUDIO_SYNTH_CURVE_MIN)) {
        v = 0;
    } else if (a > BOX_AUDIO_SYNTH_CURVE_MAX) {
        v = v < 0 ? -BOX_AUDIO_SYNTH_CURVE_MAX : BOX_AUDIO_SYNTH_CURVE_MAX;
    }

    return v;
}

/** Clamp the times, the level and the curves of an envelope.
 *
 * @param e The envelope
 */
static void box_audio_synth_envelope_clamp(
    box_audio_synth_envelope_t *e)
{
    e->attack = box_audio_synth_clamp(e->attack, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
    e->decay = box_audio_synth_clamp(e->decay, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
    e->sustain = box_audio_synth_clamp(e->sustain, 0, 1);
    e->release = box_audio_synth_clamp(e->release, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
    e->hold = box_audio_synth_clamp(e->hold, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
    e->curve = box_audio_synth_curve_clamp(e->curve);
    e->attack_curve = box_audio_synth_curve_clamp(e->attack_curve);
}

/** Clamp a frequency a sweep ends at; NaN and values that are not positive mean no sweep.
 *
 * @param v The frequency
 * @param lo Lowest frequency
 * @return The frequency, 0 for no sweep
 */
static float box_audio_synth_end_clamp(
    float v,
    float lo)
{
    return v > 0 ? box_audio_synth_clamp(v, lo, BOX_AUDIO_SYNTH_HZ_MAX) : 0.0f;
}

/** Clamp every value of a patch, so a voice never sees NaN, a negative time or an unknown
 * kind.
 *
 * @param p The patch
 */
static void box_audio_synth_patch_clamp(
    box_audio_synth_patch_t *p)
{
    uint32_t i;
    if (p->layers > BOX_AUDIO_SYNTH_LAYERS) {
        p->layers = BOX_AUDIO_SYNTH_LAYERS;
    }

    p->pitch_variance = box_audio_synth_clamp(p->pitch_variance, 0, 1);
    p->level_variance = box_audio_synth_clamp(p->level_variance, 0, 1);
    p->glide = box_audio_synth_clamp(p->glide, 0, BOX_AUDIO_SYNTH_GLIDE_MAX);
    box_audio_synth_envelope_clamp(&p->envelope);
    if (p->filter > BOX_AUDIO_SYNTH_BANDPASS) {
        p->filter = BOX_AUDIO_SYNTH_FILTER_NONE;
    }

    p->cutoff = box_audio_synth_clamp(p->cutoff, BOX_AUDIO_SYNTH_CUTOFF_MIN,
        BOX_AUDIO_SYNTH_HZ_MAX);
    p->resonance = box_audio_synth_clamp(p->resonance, 0, 1);
    if (p->slope > BOX_AUDIO_SYNTH_SLOPE_24) {
        p->slope = BOX_AUDIO_SYNTH_SLOPE_12;
    }

    p->filter_amount = p->filter_amount == p->filter_amount ? box_audio_synth_clamp(
        p->filter_amount, -BOX_AUDIO_SYNTH_OCTAVES_MAX, BOX_AUDIO_SYNTH_OCTAVES_MAX) : 0.0f;
    box_audio_synth_envelope_clamp(&p->filter_envelope);
    for (i = 0; i < BOX_AUDIO_SYNTH_LAYERS; i ++) {
        box_audio_synth_layer_t *l = &p->layer[i];
        if (l->wave > BOX_AUDIO_SYNTH_NOISE) {
            l->wave = BOX_AUDIO_SYNTH_SINE;
        }

        l->has_envelope = l->has_envelope != 0;
        l->frequency = box_audio_synth_clamp(l->frequency, BOX_AUDIO_SYNTH_FREQUENCY_MIN,
            BOX_AUDIO_SYNTH_HZ_MAX);
        l->frequency_end = box_audio_synth_end_clamp(l->frequency_end,
            BOX_AUDIO_SYNTH_FREQUENCY_MIN);
        l->sweep = box_audio_synth_clamp(l->sweep, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
        l->level = box_audio_synth_clamp(l->level, 0, BOX_AUDIO_VOLUME_MAX);
        box_audio_synth_envelope_clamp(&l->envelope);
    }
}

void box_audio_synth_command(
    box_audio_core_t *core,
    const box_audio_cmd_synth_t *cmd)
{
    box_audio_sound_t *s;
    int32_t slot = -1, i;
    if (cmd->sound >= BOX_AUDIO_SOUND_MAX) {
        goto ignored;
    }

    s = &core->sounds[cmd->sound];
    if (s->state == BOX_AUDIO_SOUND_READY && s->synth) {
        slot = s->synth - 1;
    } else if (s->state == BOX_AUDIO_SOUND_FREE) {
        for (i = 0; i < BOX_AUDIO_SYNTH_MAX && slot < 0; i ++) {
            if (!core->synths[i].used) {
                slot = i;
            }
        }

        if (slot >= 0) {
            core->synths[slot].used = true;
            core->synths[slot].plays = 0;
            s->data = NULL;
            s->frames = 0;
            s->channels = 1;
            s->rate = 1;
            s->users = 0;
            s->state = BOX_AUDIO_SOUND_READY;
            s->synth = slot + 1;
        }
    }

    if (slot < 0) {
        goto ignored;
    }

    core->synths[slot].patch = cmd->patch;
    box_audio_synth_patch_clamp(&core->synths[slot].patch);
    goto done;
ignored:
    box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_SYNTH, 0);
done:
    return;
}

/** Base 2 logarithm, from the exponent of the double and the series of atanh.
 *
 * @param x A positive finite value
 * @return The logarithm, within 1e-12
 */
static double box_audio_synth_log2(
    double x)
{
    union { double d; uint64_t u; } v;
    double m, s, s2, sum;
    int32_t e;
    v.d = x;
    e = (int32_t)((v.u >> 52) & 0x7FF) - 1023;
    v.u = (v.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    m = v.d;
    if (m > BOX_AUDIO_SYNTH_SQRT2) {
        m *= 0.5;
        e ++;
    }

    s = (m - 1) / (m + 1);
    s2 = s * s;
    sum = 1 + s2 * (1.0 / 3 + s2 * (1.0 / 5 + s2 * (1.0 / 7 + s2 * (1.0 / 9 + s2 *
        (1.0 / 11 + s2 * (1.0 / 13 + s2 * (1.0 / 15 + s2 / 17)))))));

    return (double)e + 2 * s * sum / BOX_AUDIO_SYNTH_LN2;
}

/** Power of 2, from the series of exp over the fraction and the exponent of the double.
 *
 * @param y The exponent, clamped to -1000..1000
 * @return 2 to the y
 */
static double box_audio_synth_exp2(
    double y)
{
    union { double d; uint64_t u; } v;
    double r, term = 1, sum = 1;
    int64_t n;
    int32_t i;
    if (!(y >= -1000)) {
        y = -1000;
    } else if (y > 1000) {
        y = 1000;
    }

    n = (int64_t)y;
    if ((double)n > y) {
        n --;
    }

    r = (y - (double)n) * BOX_AUDIO_SYNTH_LN2;
    for (i = 1; i < 18; i ++) {
        term *= r / i;
        sum += term;
    }

    v.u = (uint64_t)(n + 1023) << 52;

    return sum * v.d;
}

/** The factor per frame of an exponential sweep.
 *
 * @param from Value at the start, positive
 * @param to Value at the end, positive
 * @param frames Frames of the sweep, at least 1
 * @return The factor
 */
static double box_audio_synth_ratio(
    double from,
    double to,
    int32_t frames)
{
    return box_audio_synth_exp2(box_audio_synth_log2(to / from) / (double)frames);
}

/** Frames of a time at a rate, rounded to the nearest.
 *
 * @param seconds The time, 0..BOX_AUDIO_SYNTH_LENGTH_MAX
 * @param rate Frames per second
 * @return Number of frames
 */
static int32_t box_audio_synth_frames(
    float seconds,
    float rate)
{
    return (int32_t)((double)seconds * (double)rate + 0.5);
}

/** Start an envelope: its segments in frames, a pass at most BOX_AUDIO_SYNTH_LENGTH_MAX long,
 * and every segment that changes the level at least BOX_AUDIO_SYNTH_EDGE long, so it neither
 * clicks nor runs forever. With a sustain of 0 the envelope ends after its decay.
 *
 * @param e The envelope of the voice
 * @param d The envelope of the patch
 * @param rate Frames per second
 */
static void box_audio_synth_env_init(
    box_audio_synth_env_t *e,
    const box_audio_synth_envelope_t *d,
    float rate)
{
    int32_t edge = box_audio_synth_frames(BOX_AUDIO_SYNTH_EDGE, rate);
    int32_t max = box_audio_synth_frames(BOX_AUDIO_SYNTH_LENGTH_MAX, rate);
    int32_t n[4], excess, i, cut;
    static const int32_t order[4] = {
        BOX_AUDIO_SYNTH_HOLD, BOX_AUDIO_SYNTH_DECAY, BOX_AUDIO_SYNTH_ATTACK,
        BOX_AUDIO_SYNTH_RELEASE };
    edge = edge > 0 ? edge : 1;
    n[BOX_AUDIO_SYNTH_ATTACK] = box_audio_synth_frames(d->attack, rate);
    n[BOX_AUDIO_SYNTH_DECAY] = box_audio_synth_frames(d->decay, rate);
    n[BOX_AUDIO_SYNTH_HOLD] = box_audio_synth_frames(d->hold, rate);
    n[BOX_AUDIO_SYNTH_RELEASE] = box_audio_synth_frames(d->release, rate);
    excess = n[0] + n[1] + n[2] + n[3] - max;
    for (i = 0; i < 4 && excess > 0; i ++) {
        cut = n[order[i]] < excess ? n[order[i]] : excess;
        n[order[i]] -= cut;
        excess -= cut;
    }

    if (n[BOX_AUDIO_SYNTH_ATTACK] < edge) {
        n[BOX_AUDIO_SYNTH_ATTACK] = edge;
    }

    if (d->sustain < 1 && n[BOX_AUDIO_SYNTH_DECAY] < edge) {
        n[BOX_AUDIO_SYNTH_DECAY] = edge;
    }

    if (d->sustain > 0 && n[BOX_AUDIO_SYNTH_RELEASE] < edge) {
        n[BOX_AUDIO_SYNTH_RELEASE] = edge;
    } else if (d->sustain <= 0) {
        n[BOX_AUDIO_SYNTH_HOLD] = 0;
        n[BOX_AUDIO_SYNTH_RELEASE] = 0;
    }

    for (i = 0; i < 4; i ++) {
        e->frames[i] = n[i];
    }

    e->curve[BOX_AUDIO_SYNTH_ATTACK] = d->attack_curve;
    e->curve[BOX_AUDIO_SYNTH_DECAY] = d->curve;
    e->curve[BOX_AUDIO_SYNTH_HOLD] = 0;
    e->curve[BOX_AUDIO_SYNTH_RELEASE] = d->curve;
    e->curved = false;
    e->sustain = d->sustain;
    e->segment = -1;
    e->left = 0;
    e->value = 0;
    e->target = 0;
    e->step = 0;
}

/** Enter a segment of an envelope; one without frames takes its level at once, which the
 * edge of BOX_AUDIO_SYNTH_EDGE keeps to segments that do not change it.
 *
 * A linear segment adds a step per frame. A curved one of curve c over n frames follows
 * v(k) = a + (v0 - a) r^k with r = exp(-c / n), whose asymptote a lies past the target so
 * that v(n) is the target: one multiply and one add per frame, exp only here, and the last
 * frame takes the target, so the segment ends on time.
 *
 * @param e The envelope
 * @param segment A box_audio_synth_segment_t
 */
static void box_audio_synth_env_enter(
    box_audio_synth_env_t *e,
    int32_t segment)
{
    int32_t n;
    double c;
    e->segment = segment;
    if (segment >= BOX_AUDIO_SYNTH_DONE) {
        e->curved = false;
        e->value = 0;
        e->target = 0;
        e->left = 0;
        goto done;
    }

    e->target = segment == BOX_AUDIO_SYNTH_ATTACK ? 1.0f :
        (segment == BOX_AUDIO_SYNTH_RELEASE ? 0.0f : e->sustain);
    n = e->frames[segment];
    e->curved = false;
    if (n <= 0) {
        e->value = e->target;
        e->left = 0;
    } else {
        e->left = n;
        e->step = (e->target - e->value) / (float)n;
        c = e->curve[segment];
        if (c != 0 && e->target != e->value) {
            double r = box_audio_synth_exp2(-c / ((double)n * BOX_AUDIO_SYNTH_LN2));
            double end = box_audio_synth_exp2(-c / BOX_AUDIO_SYNTH_LN2);
            double a = ((double)e->target - (double)e->value * end) / (1 - end);
            e->curved = true;
            e->level = e->value;
            e->mul = r;
            e->add = a * (1 - r);
        }
    }

done:
    return;
}

/** Advance an envelope by a frame.
 *
 * @param e The envelope
 * @return Its level for the frame
 */
static inline float box_audio_synth_env_next(
    box_audio_synth_env_t *e)
{
    while (!e->left && e->segment < BOX_AUDIO_SYNTH_DONE) {
        box_audio_synth_env_enter(e, e->segment + 1);
    }

    if (e->left > 0) {
        e->left --;
        if (!e->left) {
            e->value = e->target;
        } else if (e->curved) {
            e->level = e->level * e->mul + e->add;
            e->value = (float)e->level;
        } else {
            e->value = e->value + e->step;
        }
    }

    return e->value;
}

/** Set the coefficients of the stages of the filter from its cutoff: g = tan(pi cutoff / rate)
 * of the trapezoidal state variable filter, which stays stable at any cutoff and while it
 * moves. Each stage is stable on its own, so their cascade is too.
 *
 * @param src The source
 * @param rate Frames per second
 */
static void box_audio_synth_coefs(
    box_audio_synth_source_t *src,
    float rate)
{
    double turns = src->cutoff / (2.0 * (double)rate);
    float g = box_audio_core_sin(turns) / box_audio_core_sin(turns + 0.25);
    int32_t s;
    for (s = 0; s < src->stages; s ++) {
        box_audio_synth_svf_t *f = &src->svf[s];
        f->a1 = 1.0f / (1.0f + g * (g + f->k));
        f->a2 = g * f->a1;
        f->a3 = g * f->a2;
    }
}

/** Move the cutoff by the level of the filter envelope, exponentially in frequency: the cutoff
 * of the patch times 2 to the amount times the level, clamped to the range of a cutoff.
 *
 * @param src The source
 * @param level Level of the filter envelope, 0..1
 * @param rate Frames per second
 */
static void box_audio_synth_cutoff(
    box_audio_synth_source_t *src,
    float level,
    float rate)
{
    double cut = src->cutoff_base * box_audio_synth_exp2((double)src->amount * (double)level);
    cut = cut < src->cutoff_top ? cut : src->cutoff_top;
    src->cutoff = cut > BOX_AUDIO_SYNTH_CUTOFF_MIN ? cut : BOX_AUDIO_SYNTH_CUTOFF_MIN;
    box_audio_synth_coefs(src, rate);
}

/** Start a pass of the envelopes, the oscillators and the filter envelope, at the start of a
 * play and at each restart of a loop. The state of the filter goes on.
 *
 * @param core The core
 * @param src The source
 */
static void box_audio_synth_pass(
    box_audio_core_t *core,
    box_audio_synth_source_t *src)
{
    const box_audio_synth_patch_t *p = &src->patch;
    float rate = core->sample_rate;
    double top = BOX_AUDIO_SYNTH_NYQUIST * (double)rate, lo, hi, cut;
    uint32_t l;
    int32_t n;
    if (top > BOX_AUDIO_SYNTH_FREQUENCY_MAX) {
        top = BOX_AUDIO_SYNTH_FREQUENCY_MAX;
    }

    src->inc_max = top / (double)rate;
    src->envs = 0;
    src->glide_left = 0;
    for (l = 0; l < p->layers; l ++) {
        const box_audio_synth_layer_t *layer = &p->layer[l];
        box_audio_synth_osc_t *o = &src->osc[l];
        o->wave = (int32_t)layer->wave;
        o->level = layer->level;
        o->env = layer->has_envelope ? (int32_t)l + 1 : 0;
        o->phase = 0;
        src->envs |= 1u << o->env;
        if (layer->has_envelope) {
            box_audio_synth_env_init(&src->env[o->env], &layer->envelope, rate);
        }

        lo = (double)layer->frequency * (double)src->pitch;
        lo = lo < top ? (lo > BOX_AUDIO_SYNTH_FREQUENCY_MIN ? lo : BOX_AUDIO_SYNTH_FREQUENCY_MIN) :
            top;
        o->inc = lo / (double)rate;
        o->inc_end = o->inc;
        o->ratio = 1;
        o->sweep = 0;
        o->glide = 1;
        n = box_audio_synth_frames(layer->sweep, rate);
        if (layer->frequency_end > 0 && n > 0) {
            hi = (double)layer->frequency_end * (double)src->pitch;
            hi = hi < top ? (hi > BOX_AUDIO_SYNTH_FREQUENCY_MIN ? hi :
                BOX_AUDIO_SYNTH_FREQUENCY_MIN) : top;
            o->inc_end = hi / (double)rate;
            o->ratio = box_audio_synth_ratio(lo, hi, n);
            o->sweep = n;
        }
    }

    if (src->envs & 1u) {
        box_audio_synth_env_init(&src->env[0], &p->envelope, rate);
    }

    top = BOX_AUDIO_SYNTH_CUTOFF_NYQUIST * (double)rate;
    cut = (double)p->cutoff < top ? (double)p->cutoff : top;
    src->cutoff = cut;
    src->cutoff_base = (double)p->cutoff;
    src->cutoff_top = top;
    src->control = BOX_AUDIO_SYNTH_CONTROL;
    if (src->amount != 0) {
        box_audio_synth_env_init(&src->fenv, &p->filter_envelope, rate);
    }

    box_audio_synth_coefs(src, rate);
}

/** Hash the seed of a patch with the count of its plays into the state of a play.
 *
 * @param seed The seed
 * @param plays The count
 * @return The state, never 0
 */
static uint32_t box_audio_synth_hash(
    uint32_t seed,
    uint32_t plays)
{
    uint32_t x = seed ^ (plays * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;

    return x ? x : 0x6D2B79F5u;
}

/** Next value of the white noise of a source, xorshift32.
 *
 * @param state The state, never 0
 * @return A value in -1..1
 */
static inline float box_audio_synth_noise(
    uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;

    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

void box_audio_source_synth(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t sound,
    float pitch,
    bool loop)
{
    box_audio_synth_slot_t *slot = &core->synths[core->sounds[sound].synth - 1];
    box_audio_synth_source_t *src = &source->data.synth;
    source->kind = BOX_AUDIO_SOURCE_SYNTH;
    src->patch = slot->patch;
    slot->plays ++;
    src->rng = box_audio_synth_hash(src->patch.seed, slot->plays);
    src->variance = 1.0f + src->patch.pitch_variance * box_audio_synth_noise(&src->rng);
    src->variance = src->variance > BOX_AUDIO_SYNTH_VARIANCE_MIN ? src->variance :
        BOX_AUDIO_SYNTH_VARIANCE_MIN;
    src->amp = 1.0f + src->patch.level_variance * box_audio_synth_noise(&src->rng);
    src->amp = src->amp > 0 ? src->amp : 0.0f;
    src->pitch = pitch * src->variance;
    src->loop = loop;
    src->filter = (int32_t)src->patch.filter;
    src->amount = src->filter ? src->patch.filter_amount : 0.0f;
    src->glide_frames = box_audio_synth_frames(src->patch.glide, core->sample_rate);
    src->svf[0] = (box_audio_synth_svf_t){0};
    src->svf[1] = (box_audio_synth_svf_t){ .k = (float)BOX_AUDIO_SYNTH_BUTTERWORTH_1 };
    if (src->patch.slope == BOX_AUDIO_SYNTH_SLOPE_24) {
        src->stages = 2;
        src->svf[0].k = (float)(BOX_AUDIO_SYNTH_BUTTERWORTH_2 -
            (BOX_AUDIO_SYNTH_BUTTERWORTH_2 - BOX_AUDIO_SYNTH_DAMPING_MIN) *
            (double)src->patch.resonance);
    } else {
        src->stages = 1;
        src->svf[0].k = (float)(BOX_AUDIO_SYNTH_SQRT2 -
            (BOX_AUDIO_SYNTH_SQRT2 - BOX_AUDIO_SYNTH_DAMPING_MIN) *
            (double)src->patch.resonance);
    }

    box_audio_synth_pass(core, src);
}

/** Factor an oscillator still moves by over the rest of the glide that runs.
 *
 * @param src The source
 * @param o The oscillator
 * @return The factor, 1 without a glide
 */
static double box_audio_synth_glide_rest(
    const box_audio_synth_source_t *src,
    const box_audio_synth_osc_t *o)
{
    return src->glide_left > 0 ? box_audio_synth_exp2(
        box_audio_synth_log2(o->glide) * (double)src->glide_left) : 1.0;
}

/** Move an oscillator by a factor from where it stands, at once or over a glide of frames.
 * The source starts the glide once every oscillator took its factor.
 *
 * @param src The source
 * @param o The oscillator
 * @param factor Factor of inc, the rest of a glide that runs included
 * @param frames Frames of the glide, 0 to jump
 */
static void box_audio_synth_move(
    const box_audio_synth_source_t *src,
    box_audio_synth_osc_t *o,
    double factor,
    int32_t frames)
{
    if (frames > 0) {
        o->glide = box_audio_synth_ratio(1.0, factor, frames);
    } else {
        o->inc = o->inc * factor < src->inc_max ? o->inc * factor : src->inc_max;
        o->inc_end = o->inc_end * factor < src->inc_max ? o->inc_end * factor : src->inc_max;
        o->glide = 1;
    }
}

void box_audio_synth_pitch(
    box_audio_synth_source_t *src,
    float pitch)
{
    float p = pitch * src->variance;
    double f = (double)p / (double)src->pitch;
    int32_t l;
    if (src->glide_frames > 0 || src->glide_left > 0) {
        for (l = 0; l < (int32_t)src->patch.layers; l ++) {
            box_audio_synth_osc_t *o = &src->osc[l];
            box_audio_synth_move(src, o, box_audio_synth_glide_rest(src, o) * f,
                src->glide_frames);
        }

        src->glide_left = src->glide_frames;
    } else {
        for (l = 0; l < (int32_t)src->patch.layers; l ++) {
            box_audio_synth_osc_t *o = &src->osc[l];
            o->inc = o->inc * f < src->inc_max ? o->inc * f : src->inc_max;
            o->inc_end = o->inc_end * f < src->inc_max ? o->inc_end * f : src->inc_max;
        }
    }

    src->pitch = p;
}

bool box_audio_synth_legato(
    box_audio_core_t *core,
    box_audio_synth_source_t *src,
    uint32_t sound,
    float pitch)
{
    const box_audio_synth_patch_t *np = &core->synths[core->sounds[sound].synth - 1].patch;
    float rate = core->sample_rate, p = pitch * src->variance;
    double top = BOX_AUDIO_SYNTH_NYQUIST * (double)rate, f = (double)p / (double)src->pitch;
    double hi, factor;
    int32_t n = box_audio_synth_frames(np->glide, rate), e, l;
    bool held = false, result = false;
    if (top > BOX_AUDIO_SYNTH_FREQUENCY_MAX) {
        top = BOX_AUDIO_SYNTH_FREQUENCY_MAX;
    }

    for (e = 0; e <= BOX_AUDIO_SYNTH_LAYERS; e ++) {
        if ((src->envs & (1u << e)) && src->env[e].segment < BOX_AUDIO_SYNTH_RELEASE) {
            held = true;
        }
    }

    if (!held) {
        goto done;
    }

    for (e = 0; e <= BOX_AUDIO_SYNTH_LAYERS; e ++) {
        box_audio_synth_env_t *env = &src->env[e];
        if ((src->envs & (1u << e)) && env->segment == BOX_AUDIO_SYNTH_HOLD &&
            env->frames[BOX_AUDIO_SYNTH_HOLD] > 0)
        {
            env->left = env->frames[BOX_AUDIO_SYNTH_HOLD];
        }
    }

    if (src->amount != 0 && src->fenv.segment == BOX_AUDIO_SYNTH_HOLD &&
        src->fenv.frames[BOX_AUDIO_SYNTH_HOLD] > 0)
    {
        src->fenv.left = src->fenv.frames[BOX_AUDIO_SYNTH_HOLD];
    }

    for (l = 0; l < (int32_t)src->patch.layers; l ++) {
        box_audio_synth_osc_t *o = &src->osc[l];
        if (l < (int32_t)np->layers) {
            box_audio_synth_layer_t *layer = &src->patch.layer[l];
            const box_audio_synth_layer_t *next = &np->layer[l];
            layer->frequency = next->frequency;
            layer->frequency_end = next->frequency_end;
            layer->sweep = next->sweep;
            hi = (double)next->frequency * (double)p;
            hi = hi < top ? (hi > BOX_AUDIO_SYNTH_FREQUENCY_MIN ? hi :
                BOX_AUDIO_SYNTH_FREQUENCY_MIN) : top;
            hi /= (double)rate;
            o->sweep = 0;
            if (n > 0) {
                o->inc_end = hi;
                o->glide = box_audio_synth_ratio(o->inc, hi, n);
            } else {
                o->inc = hi;
                o->inc_end = hi;
                o->glide = 1;
            }
        } else {
            factor = box_audio_synth_glide_rest(src, o) * f;
            box_audio_synth_move(src, o, factor, n);
        }
    }

    src->patch.glide = np->glide;
    src->glide_frames = n;
    src->glide_left = n;
    src->pitch = p;
    result = true;
done:
    return result;
}

/** Advance the glide of a source by a frame: every oscillator moves by its factor, and the end
 * of its sweep with it.
 *
 * @param src The source
 */
static void box_audio_synth_glide_next(
    box_audio_synth_source_t *src)
{
    int32_t l;
    for (l = 0; l < (int32_t)src->patch.layers; l ++) {
        box_audio_synth_osc_t *o = &src->osc[l];
        o->inc = o->inc * o->glide;
        o->inc = o->inc < src->inc_max ? o->inc : src->inc_max;
        if (o->sweep > 0) {
            o->inc_end = o->inc_end * o->glide;
            o->inc_end = o->inc_end < src->inc_max ? o->inc_end : src->inc_max;
        }
    }

    src->glide_left --;
}

/** Advance the envelopes a source uses by a frame.
 *
 * @param src The source
 * @param levels Receives the level of each envelope
 * @return Whether one of them still plays
 */
static inline bool box_audio_synth_envs_next(
    box_audio_synth_source_t *src,
    float *levels)
{
    bool result = false;
    int32_t e;
    for (e = 0; e <= BOX_AUDIO_SYNTH_LAYERS; e ++) {
        if (src->envs & (1u << e)) {
            levels[e] = box_audio_synth_env_next(&src->env[e]);
            result = result || src->env[e].segment < BOX_AUDIO_SYNTH_DONE;
        }
    }

    return result;
}

/** The correction of polyBLEP for a step of the wave at phase 0.
 *
 * @param t Phase in turns, 0..1
 * @param dt Turns per frame
 * @return The correction
 */
static inline float box_audio_synth_blep(
    double t,
    double dt)
{
    float result = 0;
    if (t < dt) {
        t /= dt;
        result = (float)(t + t - t * t - 1);
    } else if (t > 1 - dt) {
        t = (t - 1) / dt;
        result = (float)(t * t + t + t + 1);
    }

    return result;
}

/** The correction of polyBLAMP for a corner of the wave at phase 0, the integral of the
 * polyBLEP: a change of the slope by 1 per frame.
 *
 * @param t Phase in turns, 0..1
 * @param dt Turns per frame
 * @return The correction
 */
static inline float box_audio_synth_blamp(
    double t,
    double dt)
{
    float result = 0;
    if (t < dt) {
        t = t / dt - 1;
        result = (float)(t * t * t * (-1.0 / 3));
    } else if (t > 1 - dt) {
        t = (t - 1) / dt + 1;
        result = (float)(t * t * t * (1.0 / 3));
    }

    return result;
}

/** Render a frame of an oscillator and advance it.
 *
 * @param o The oscillator
 * @param rng State of the noise
 * @return The sample, -1..1
 */
static inline float box_audio_synth_osc_next(
    box_audio_synth_osc_t *o,
    uint32_t *rng)
{
    double t = o->phase, dt = o->inc;
    float x;
    switch (o->wave) {
    case BOX_AUDIO_SYNTH_SAW:
        x = (float)(2 * t - 1) - box_audio_synth_blep(t, dt);
        break;
    case BOX_AUDIO_SYNTH_SQUARE:
        x = (t < 0.5 ? 1.0f : -1.0f) + box_audio_synth_blep(t, dt) -
            box_audio_synth_blep(t < 0.5 ? t + 0.5 : t - 0.5, dt);
        break;
    case BOX_AUDIO_SYNTH_TRIANGLE:
        x = (float)(t < 0.5 ? 4 * t - 1 : 3 - 4 * t) + (float)(4 * dt) *
            (box_audio_synth_blamp(t, dt) -
             box_audio_synth_blamp(t < 0.5 ? t + 0.5 : t - 0.5, dt));
        break;
    case BOX_AUDIO_SYNTH_NOISE:
        x = box_audio_synth_noise(rng);
        break;
    default:
        x = box_audio_core_sin(t);
        break;
    }

    t += dt;
    o->phase = t >= 1 ? t - 1 : t;
    if (o->sweep > 0) {
        o->sweep --;
        o->inc = o->sweep ? o->inc * o->ratio : o->inc_end;
    }

    return x;
}

/** Filter a frame through a stage of the filter.
 *
 * @param f The stage
 * @param filter A box_audio_synth_filter_t, not BOX_AUDIO_SYNTH_FILTER_NONE
 * @param x The input
 * @return The output
 */
static inline float box_audio_synth_svf_next(
    box_audio_synth_svf_t *f,
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
    } else {
        y = f->k * v1;
    }

    return y;
}

int32_t box_audio_synth_render(
    box_audio_core_t *core,
    box_audio_synth_source_t *src,
    int32_t frames)
{
    float *out = core->src_l, rate = core->sample_rate;
    float levels[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    int32_t layers = (int32_t)src->patch.layers, i, l;
    for (i = 0; i < frames; i ++) {
        float mix = 0, y;
        if (!box_audio_synth_envs_next(src, levels)) {
            if (!src->loop || !src->envs) {
                break;
            }

            box_audio_synth_pass(core, src);
            box_audio_synth_envs_next(src, levels);
        }

        if (src->glide_left > 0) {
            box_audio_synth_glide_next(src);
        }

        for (l = 0; l < layers; l ++) {
            box_audio_synth_osc_t *o = &src->osc[l];
            mix += box_audio_synth_osc_next(o, &src->rng) * o->level * levels[o->env];
        }

        if (src->filter) {
            if (src->amount != 0 && src->fenv.segment < BOX_AUDIO_SYNTH_DONE) {
                float level = box_audio_synth_env_next(&src->fenv);
                if (-- src->control <= 0 || src->fenv.segment >= BOX_AUDIO_SYNTH_DONE) {
                    src->control = BOX_AUDIO_SYNTH_CONTROL;
                    box_audio_synth_cutoff(src, level, rate);
                }
            }

            y = mix;
            if (src->stages > 1) {
                y = box_audio_synth_svf_next(&src->svf[1], src->filter, y);
            }

            y = box_audio_synth_svf_next(&src->svf[0], src->filter, y);
        } else {
            y = mix;
        }

        out[i] = box_audio_clean(y * src->amp);
    }

    return i;
}
