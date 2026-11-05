/**
 * @file audio/core/synth.c
 * @brief The synth source: band limited oscillators and seeded noise, each with a linear or
 * curved envelope, mixed into a state variable filter of one or two stages, whose cutoff an
 * envelope may move, and an amp, rendered per frame from a copy of the patch. A change of the
 * pitch glides exponentially in frequency over the glide of the patch, and a legato play
 * retargets the layers of a voice without a restart of its envelopes. A layer may play as
 * unison voices, detuned and spread in stereo, which render through
 * box_audio_synth_render_wide, so a patch without them renders as before at the same cost. A
 * pluck layer is a Karplus-Strong string instead of an oscillator, tuned with an allpass for the
 * fraction of a frame, which follows the pitch of its oscillator and ends the voice once it is
 * silent; with unison it rings a string per voice, detuned and panned as the voices of an
 * oscillator, each plucked with its own noise. A layer may be hard synced, its wave restarted
 * on every cycle of its frequency at a ratio of it, the restart band limited by a polyBLEP. Up
 * to two LFOs move the pitch, the level, the cutoff, the pulse width of squares and the ratio
 * of hard sync, per control block of frames and interpolated per frame
 * in between.
 *
 * Every voice holds its own copy of the patch, so a play allocates nothing. Its layers are parts
 * of the core, one per layer (box_audio_synth_part_t), and each pluck layer takes a string of
 * the core with its delay line (box_audio_synth_string_t) for each of its unison voices.
 *
 * Like the rest of the core it uses no libm: the sine is box_audio_core_sin and the exponential
 * sweeps and the filter envelope use the series of box_audio_synth_log2 and
 * box_audio_synth_exp2.
 */

#include "internal.h"

/** Natural logarithm of 2 */
#define BOX_AUDIO_SYNTH_LN2 0.69314718055994530942

/** Square root of 2 */
#define BOX_AUDIO_SYNTH_SQRT2 1.41421356237309504880

/** Frames between two updates of the coefficients of a filter whose cutoff an envelope moves */
#define BOX_AUDIO_SYNTH_CONTROL 8

/** Largest share of the sample rate an oscillator plays at, under Nyquist with room for the
 * correction of the band limited waves */
#define BOX_AUDIO_SYNTH_NYQUIST 0.48

/** Largest share of the sample rate of a cutoff; a higher one, Nyquist included, is clamped */
#define BOX_AUDIO_SYNTH_CUTOFF_NYQUIST 0.49

/** Smallest pitch factor of a play, which the pitch variance cannot go under */
#define BOX_AUDIO_SYNTH_VARIANCE_MIN 0.01f

/** Seconds over which an LFO wave that jumps, square, saw or random, moves to its new value */
#define BOX_AUDIO_SYNTH_LFO_SMOOTH 0.002

/** Shortest fade in seconds of an LFO with a delay, so its depth does not jump in at once */
#define BOX_AUDIO_SYNTH_LFO_FADE_MIN 0.005f

/** Pi */
#define BOX_AUDIO_SYNTH_PI 3.14159265358979323846

/** Turns per radian */
#define BOX_AUDIO_SYNTH_TURNS (1.0 / 6.28318530717958647692)

/** Gain of each of n unison voices, 1 / sqrt(n), so their power sums to that of one */
static const double box_audio_synth_unison_gain[BOX_AUDIO_SYNTH_UNISON_MAX + 1] = {
    1.0, 1.0, 0.70710678118654752440, 0.57735026918962576451, 0.5, 0.44721359549995793928,
    0.40824829046386301637, 0.37796447300922722721 };

/** Base 2 exponent of a fall of 60 dB, 3 log2(10) */
#define BOX_AUDIO_SYNTH_DB60 9.96578428466208704361

/** Shortest period of a string in frames: a delay line of 2 frames, the lowpass and the
 * allpass */
#define BOX_AUDIO_SYNTH_STRING_PERIOD_MIN 4.0

/** Longest period of a string in frames, within its delay line */
#define BOX_AUDIO_SYNTH_STRING_PERIOD_MAX ((double)(BOX_AUDIO_SYNTH_STRING_FRAMES - 2))

/** Largest gain of the loop of a string, under 1 so that every string decays */
#define BOX_AUDIO_SYNTH_STRING_GAIN_MAX 0.99999

/** Frames between the exact tunings of a string while its pitch moves, the control block of
 * the LFOs: between them a cheap tuning follows the pitch every frame, so a vibrato costs its
 * two sines and exponential once a block and not every frame */
#define BOX_AUDIO_SYNTH_STRING_RETUNE 8

/** Shortest period of a string in frames the cheap tuning serves, under which the series of
 * its sines would lose accuracy and a moving pitch tunes exactly every frame */
#define BOX_AUDIO_SYNTH_STRING_CHEAP 16.0

/** Frames of the crossfade of a string from its old read point to the new one when its delay
 * line moves by whole frames */
#define BOX_AUDIO_SYNTH_STRING_FADE 8

/** Frames of the past a new read point of a string runs through its lowpass and allpass
 * before it plays, so the allpass starts settled: its coefficient is at most a third, so the
 * error of its start falls under 2 % */
#define BOX_AUDIO_SYNTH_STRING_PRIME 4

/** Peak of a period of a string under which it is silent, -80 dB of its pluck */
#define BOX_AUDIO_SYNTH_STRING_SILENCE 1e-4f

/** Envelope of the layers of strings without one, a level of 1 that never ends */
#define BOX_AUDIO_SYNTH_ENV_FREE (BOX_AUDIO_SYNTH_LAYERS + 1)

/** Base 2 logarithm, from the exponent of the double and the series of atanh.
 *
 * @param x A positive finite value
 * @return The logarithm, within 1e-12
 */
double box_audio_synth_log2(
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
double box_audio_synth_exp2(
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

/** The factor and the term per step of a curve of c from one value to another over n steps:
 * v(k) = a + (v0 - a) r^k with r = exp(-c / n), whose asymptote a lies past the target so that
 * v(n) is the target.
 *
 * @param from Value at the start
 * @param to Value at the end, not from
 * @param n Steps, at least 1
 * @param c The curve, not 0
 * @param mul Receives the factor per step, r
 * @param add Receives the term per step, a (1 - r)
 */
static void box_audio_synth_curve_coefs(
    double from,
    double to,
    int32_t n,
    double c,
    double *mul,
    double *add)
{
    double r = box_audio_synth_exp2(-c / ((double)n * BOX_AUDIO_SYNTH_LN2));
    double end = box_audio_synth_exp2(-c / BOX_AUDIO_SYNTH_LN2);
    double a = (to - from * end) / (1 - end);
    *mul = r;
    *add = a * (1 - r);
}

/** Start a curve from one value to another over steps, with the math of the curved segments
 * of an envelope, or linear for a curve of 0.
 *
 * @param k The curve
 * @param from Value at the start
 * @param to Value at the end
 * @param n Steps, 0 to take the end at once
 * @param c The curve, 0 linear, positive fast at first
 */
static void box_audio_synth_curve_start(
    box_audio_synth_curve_t *k,
    double from,
    double to,
    int32_t n,
    double c)
{
    k->target = to;
    if (n <= 0) {
        k->level = to;
        k->left = 0;
        k->mul = 1;
        k->add = 0;
    } else {
        k->level = from;
        k->left = n;
        if (c != 0 && to != from) {
            box_audio_synth_curve_coefs(from, to, n, c, &k->mul, &k->add);
        } else {
            k->mul = 1;
            k->add = (to - from) / (double)n;
        }
    }
}

/** Advance a curve by a step; its last step takes the target.
 *
 * @param k The curve
 * @return Its value after the step
 */
static double box_audio_synth_curve_next(
    box_audio_synth_curve_t *k)
{
    if (k->left > 0) {
        k->left --;
        k->level = k->left ? k->level * k->mul + k->add : k->target;
    }

    return k->level;
}

/** Start an envelope: its segments in frames, a pass at most BOX_AUDIO_SYNTH_LENGTH_MAX long,
 * and every segment that changes the level at least BOX_AUDIO_SYNTH_EDGE long, so it neither
 * clicks nor runs forever.
 *
 * With a sustain of 0 the envelope ends after its decay, gated or not.
 *
 * @param e The envelope of the voice
 * @param d The envelope of the patch
 * @param rate Frames per second
 * @param gate Whether it sustains in its hold until a release
 */
static void box_audio_synth_env_init(
    box_audio_synth_env_t *e,
    const box_audio_synth_envelope_t *d,
    float rate,
    bool gate)
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

    e->release = n[BOX_AUDIO_SYNTH_RELEASE] > edge ? n[BOX_AUDIO_SYNTH_RELEASE] : edge;
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
    e->gate = gate;
    e->segment = -1;
    e->left = 0;
    e->value = 0;
    e->target = 0;
    e->step = 0;
}

/** Enter a segment of an envelope; one without frames takes its level at once, which the
 * edge of BOX_AUDIO_SYNTH_EDGE keeps to segments that do not change it.
 *
 * The hold of a gated envelope with a sustain lasts until a release: box_audio_synth_env_step
 * renews its frames.
 *
 * A linear segment adds a step per frame. A curved one of curve c over n frames follows
 * box_audio_synth_curve_coefs: one multiply and one add per frame, exp only here, and the last
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
    if (segment == BOX_AUDIO_SYNTH_HOLD && e->gate && e->sustain > 0) {
        n = INT32_MAX;
    }

    e->curved = false;
    if (n <= 0) {
        e->value = e->target;
        e->left = 0;
    } else {
        e->left = n;
        e->step = (e->target - e->value) / (float)n;
        c = e->curve[segment];
        if (c != 0 && e->target != e->value) {
            e->curved = true;
            e->level = e->value;
            box_audio_synth_curve_coefs((double)e->value, (double)e->target, n, c, &e->mul,
                &e->add);
        }
    }

done:
    return;
}

/** Advance an envelope by a frame other than one inside a linear segment: one that enters the
 * next segments, the last frame of a segment, which takes its target, or one of a curve.
 *
 * @param e The envelope
 * @return Its level for the frame
 */
static float box_audio_synth_env_step(
    box_audio_synth_env_t *e)
{
    if (e->gate && e->left == 1 && e->segment == BOX_AUDIO_SYNTH_HOLD) {
        e->left = INT32_MAX;
    }

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

/** Advance an envelope by a frame. A frame inside a linear segment adds the step here, as
 * before curves, so a patch without curves pays a test of curved per frame and nothing else;
 * every other frame takes box_audio_synth_env_step.
 *
 * @param e The envelope
 * @return Its level for the frame
 */
static inline float box_audio_synth_env_next(
    box_audio_synth_env_t *e)
{
    float result;
    if (e->left > 1 && !e->curved) {
        e->left --;
        e->value = e->value + e->step;
        result = e->value;
    } else {
        result = box_audio_synth_env_step(e);
    }

    return result;
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
    float g = box_audio_svf_g(src->cutoff, rate);
    int32_t s;
    for (s = 0; s < src->stages; s ++) {
        box_audio_svf_t *f = &src->svf[s];
        box_audio_svf_coefs(f, g);
        if (src->stereo) {
            src->svf_r[s].a1 = f->a1;
            src->svf_r[s].a2 = f->a2;
            src->svf_r[s].a3 = f->a3;
        }
    }
}

/** Set the indexes and the feedback of the oscillators from the patch.
 *
 * FM is not band limited, so the index of phase modulation is limited: by Carson's rule the
 * carrier plus (index + 1) times the modulator, the higher ends of sweeps, stays under the top.
 *
 * @param src The source
 */
static void box_audio_synth_limits(
    box_audio_synth_source_t *src)
{
    double fc, fm, limit, index;
    int32_t l;
    for (l = 0; l < src->layers; l ++) {
        const box_audio_synth_layer_t *layer = &src->part[l]->layer;
        box_audio_synth_osc_t *o = &src->part[l]->osc;
        o->feedback = (float)((double)layer->feedback * BOX_AUDIO_SYNTH_TURNS);
        if (o->target < 0) {
            o->index = 0;
        } else if (o->ring) {
            o->index = layer->index;
        } else {
            const box_audio_synth_osc_t *c = &src->part[o->target]->osc;
            fc = c->inc > c->inc_end ? c->inc : c->inc_end;
            fm = o->inc > o->inc_end ? o->inc : o->inc_end;
            limit = (src->inc_max - fc) / fm - 1;
            limit = limit > 0 ? limit : 0;
            index = (double)layer->index < limit ? (double)layer->index : limit;
            o->index = (float)(index * BOX_AUDIO_SYNTH_TURNS);
        }
    }
}

/** Move the cutoff by the level of the filter envelope, exponentially in frequency: the cutoff
 * of the patch times 2 to the amount times the level, to the octaves of the LFOs and to the
 * keytrack times the key, clamped to the range of a cutoff.
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
    double octaves = (double)src->amount * (double)level, cut;
    if (src->lfo_cutoff) {
        octaves += src->lfo_octaves;
    }

    if (src->keytrack != 0) {
        octaves += (double)src->keytrack * src->key;
    }

    cut = src->cutoff_base * box_audio_synth_exp2(octaves);
    cut = cut < src->cutoff_top ? cut : src->cutoff_top;
    src->cutoff = cut > BOX_AUDIO_SYNTH_CUTOFF_MIN ? cut : BOX_AUDIO_SYNTH_CUTOFF_MIN;
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

/** Semitones the pitch envelope of a layer moves its pitch by: amount times the share of its
 * decay left, or after the release from the offset at its start toward release_amount.
 *
 * @param part The part of the layer, with a pitch envelope
 * @return The semitones
 */
static double box_audio_synth_bend_offset(
    const box_audio_synth_part_t *part)
{
    const box_audio_synth_pitch_envelope_t *d = &part->layer.pitch_envelope;
    const box_audio_synth_bend_t *b = &part->bend;
    double to = (double)d->release_amount;

    return b->released ? to + (b->from - to) * b->release.level :
        (double)d->amount * b->decay.level;
}

/** Aim every destination of the LFOs of a source at their values: the factors of the pitch,
 * the level and the pulse width of each oscillator, which move there linearly over a control
 * block or take them at once, and the cutoff, which takes them at once.
 *
 * The pitch envelope of a layer adds to its pitch and to its ratio of hard sync.
 *
 * The pitch takes 2 to the cents over 1200, one exp2 per pitched oscillator. The level dips
 * from 1 at the top of the wave to 1 - amplitude at its bottom, so an LFO in its delay leaves
 * it at 1.
 *
 * @param src The source
 * @param rate Frames per second
 * @param now Whether the destinations take their values at once, at the start of a pass
 */
static void box_audio_synth_lfo_aim(
    box_audio_synth_source_t *src,
    float rate,
    bool now)
{
    double cents[1 + BOX_AUDIO_SYNTH_LAYERS] = {0}, width[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    double semitones[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    double octaves = 0, c, w, r;
    float dip[1 + BOX_AUDIO_SYNTH_LAYERS], v;
    int32_t k, l;
    for (l = 0; l <= BOX_AUDIO_SYNTH_LAYERS; l ++) {
        dip[l] = 1;
    }

    for (k = 0; k < src->lfos; k ++) {
        const box_audio_synth_lfo_t *d = &src->lfo_patch[k];
        const box_audio_synth_mod_t *m = &src->lfo[k];
        v = m->value * m->gain;
        cents[d->layer] += (double)(d->pitch * v);
        width[d->layer] += (double)(d->pulse_width * v);
        semitones[d->layer] += (double)(d->sync * v);
        dip[d->layer] *= 1 - d->amplitude * m->gain * 0.5f * (1 - m->value);
        octaves += (double)(d->cutoff * v);
    }

    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        box_audio_synth_osc_t *o = &part->osc;
        bool bend = src->bends && part->bend.on;
        c = cents[0] + cents[l + 1];
        if (bend) {
            c += 100 * box_audio_synth_bend_offset(part);
        }

        c = c != 0 ? box_audio_synth_exp2(c * (1.0 / 1200)) : 1.0;
        w = o->width_base + width[0] + width[l + 1];
        w = w > BOX_AUDIO_SYNTH_PULSE_MIN ? (w < 1 - BOX_AUDIO_SYNTH_PULSE_MIN ? w :
            1 - BOX_AUDIO_SYNTH_PULSE_MIN) : BOX_AUDIO_SYNTH_PULSE_MIN;
        if (now) {
            o->vibrato = c;
            o->width = w;
            o->tremolo = dip[l + 1];
        }

        o->vibrato_step = (c - o->vibrato) * (1.0 / BOX_AUDIO_SYNTH_CONTROL);
        o->width_step = (w - o->width) * (1.0 / BOX_AUDIO_SYNTH_CONTROL);
        o->tremolo_step = (dip[l + 1] - o->tremolo) * (1.0f / BOX_AUDIO_SYNTH_CONTROL);
        if (o->synced) {
            r = semitones[0] + semitones[l + 1];
            if (bend) {
                r += (double)part->layer.pitch_envelope.sync * part->bend.decay.level;
            }

            r = r != 0 ? o->sync_base * box_audio_synth_exp2(r * (1.0 / 12)) : o->sync_base;
            r = r > 1 ? (r < BOX_AUDIO_SYNTH_SYNC_MAX ? r : BOX_AUDIO_SYNTH_SYNC_MAX) : 1.0;
            if (now) {
                o->sync = r;
            }

            o->sync_step = (r - o->sync) * (1.0 / BOX_AUDIO_SYNTH_CONTROL);
        }
    }

    if (now) {
        src->tremolo = dip[0];
    }

    src->tremolo_step = (dip[0] - src->tremolo) * (1.0f / BOX_AUDIO_SYNTH_CONTROL);
    if (src->lfo_cutoff) {
        src->lfo_octaves = octaves;
        box_audio_synth_cutoff(src, src->amount != 0 ? src->fenv.value : 0.0f, rate);
    }
}

/** Value of the wave of an LFO at its phase, before its smoothing and its fade.
 *
 * @param m The LFO
 * @return The value, -1..1
 */
static float box_audio_synth_lfo_wave(
    const box_audio_synth_mod_t *m)
{
    double t = m->phase;
    float x;
    switch (m->wave) {
    case BOX_AUDIO_SYNTH_LFO_TRIANGLE:
        x = (float)(t < 0.25 ? 4 * t : (t < 0.75 ? 2 - 4 * t : 4 * t - 4));
        break;
    case BOX_AUDIO_SYNTH_LFO_SQUARE:
        x = t < 0.5 ? 1.0f : -1.0f;
        break;
    case BOX_AUDIO_SYNTH_LFO_SAW:
        x = (float)(t < 0.5 ? 2 * t : 2 * t - 2);
        break;
    case BOX_AUDIO_SYNTH_LFO_RANDOM:
        x = m->held;
        break;
    default:
        x = box_audio_core_sin(t);
        break;
    }

    return x;
}

/** Start the LFOs of a pass: each from its start phase, before its delay and faded out, but
 * one without either at its full depth. The pass then sets the destinations to their values.
 *
 * @param src The source
 * @param rate Frames per second
 */
static void box_audio_synth_lfo_pass(
    box_audio_synth_source_t *src,
    float rate)
{
    int32_t k, n, least = box_audio_synth_frames(BOX_AUDIO_SYNTH_LFO_FADE_MIN, rate);
    for (k = 0; k < src->lfos; k ++) {
        const box_audio_synth_lfo_t *d = &src->lfo_patch[k];
        box_audio_synth_mod_t *m = &src->lfo[k];
        m->wave = (int32_t)d->wave;
        m->inc = (double)d->rate * BOX_AUDIO_SYNTH_CONTROL / (double)rate;
        m->phase = m->start;
        m->delay = box_audio_synth_frames(d->delay, rate);
        n = box_audio_synth_frames(d->fade, rate);
        n = m->delay > 0 && n < least ? least : n;
        m->fade_step = n > BOX_AUDIO_SYNTH_CONTROL ?
            (float)BOX_AUDIO_SYNTH_CONTROL / (float)n : 1.0f;
        m->gain = m->delay <= 0 && m->fade_step >= 1 ? 1.0f : 0.0f;
        m->smooth = m->wave == BOX_AUDIO_SYNTH_LFO_SQUARE || m->wave == BOX_AUDIO_SYNTH_LFO_SAW ||
            m->wave == BOX_AUDIO_SYNTH_LFO_RANDOM;
        m->held = m->wave == BOX_AUDIO_SYNTH_LFO_RANDOM ?
            box_audio_synth_noise(&src->lfo_rng) : 0.0f;
        m->value = box_audio_synth_lfo_wave(m);
    }

    src->lfo_control = 1;
    src->lfo_octaves = 0;
}

/** Control blocks of a number of frames, rounded up, at least 1.
 *
 * @param frames The frames, 0 or more
 * @return The blocks
 */
static int32_t box_audio_synth_blocks(
    int32_t frames)
{
    int32_t n = (frames + BOX_AUDIO_SYNTH_CONTROL - 1) / BOX_AUDIO_SYNTH_CONTROL;

    return n > 0 ? n : 1;
}

/** Start the pitch envelopes of a pass: each at its amount, which decays to 0 over its decay
 * in control blocks, not released.
 *
 * @param src The source
 * @param rate Frames per second
 */
static void box_audio_synth_bend_pass(
    box_audio_synth_source_t *src,
    float rate)
{
    int32_t l, n;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        const box_audio_synth_pitch_envelope_t *d = &part->layer.pitch_envelope;
        if (part->bend.on) {
            n = box_audio_synth_frames(d->decay, rate);
            n = n > 0 ? box_audio_synth_blocks(n) : 0;
            box_audio_synth_curve_start(&part->bend.decay, 1.0, 0.0, n, (double)d->curve);
            box_audio_synth_curve_start(&part->bend.release, 1.0, 1.0, 0, 0.0);
            part->bend.released = false;
            part->bend.from = 0;
        }
    }
}

/** Advance the pitch envelopes of a source by a control block. One whose layer has an
 * amplitude envelope, its own or that of the patch, starts its release once that envelope
 * reached its release, over the frames left of it, from its offset toward release_amount.
 *
 * @param src The source
 */
static void box_audio_synth_bend_block(
    box_audio_synth_source_t *src)
{
    int32_t l, n;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        box_audio_synth_bend_t *b = &part->bend;
        int32_t env = part->osc.env;
        if (!b->on) {
            continue;
        }

        if (!b->released && env != BOX_AUDIO_SYNTH_ENV_FREE) {
            const box_audio_synth_env_t *e = env == 0 ? &src->env : &part->env;
            if (e->segment >= BOX_AUDIO_SYNTH_RELEASE) {
                n = e->segment == BOX_AUDIO_SYNTH_RELEASE ? e->left : 0;
                b->from = box_audio_synth_bend_offset(part);
                b->released = true;
                box_audio_synth_curve_start(&b->release, 1.0, 0.0,
                    n > 0 ? box_audio_synth_blocks(n) : 0,
                    (double)part->layer.pitch_envelope.curve);
            }
        }

        box_audio_synth_curve_next(&b->decay);
        if (b->released) {
            box_audio_synth_curve_next(&b->release);
        }
    }
}

/** Set the unison voices of a part from its layer: the detune of each voice, evenly from half
 * the spread down to half of it up, and its gains, 1 / sqrt(voices) times the equal power pan
 * of the core at its place, evenly from -width to width.
 *
 * @param part The part, its layer clamped
 */
static void box_audio_synth_unison_init(
    box_audio_synth_part_t *part)
{
    box_audio_synth_unison_t *u = &part->unison;
    const box_audio_synth_layer_t *layer = &part->layer;
    double gain, at, turns;
    int32_t k;
    u->voices = (int32_t)layer->voices;
    if (u->voices <= 1) {
        goto done;
    }

    gain = box_audio_synth_unison_gain[u->voices];
    for (k = 0; k < u->voices; k ++) {
        at = (double)k / (double)(u->voices - 1);
        u->detune[k] = box_audio_synth_exp2((double)layer->detune * (at - 0.5) / 1200.0);
        turns = ((double)layer->width * (2 * at - 1) + 1) * 0.125;
        u->left[k] = (float)(BOX_AUDIO_SYNTH_SQRT2 * gain * box_audio_core_sin(turns + 0.25));
        u->right[k] = (float)(BOX_AUDIO_SYNTH_SQRT2 * gain * box_audio_core_sin(turns));
    }

done:
    return;
}

/** Tune a string to a frequency: a period of n frames is its delay line of d frames, the
 * lowpass of one frame and an allpass of the fraction f = n - 1 - d, 0.5 to 1.5 frames.
 *
 * The coefficient c = sin(w (1 - f) / 2) / sin(w (1 + f) / 2) gives the allpass a phase delay
 * of exactly f at the fundamental w, so the pitch is exact at high notes too.
 *
 * The gain of the loop makes the fundamental fall 60 dB over the decay, the loss of the
 * lowpass at it included.
 *
 * @param s The string
 * @param inc Turns per frame of its oscillator
 */
static void box_audio_synth_string_tune(
    box_audio_synth_string_t *s,
    double inc)
{
    double n = 1.0 / inc, f, w, h, g;
    int32_t d;
    n = n < s->period_max ? n : s->period_max;
    n = n > BOX_AUDIO_SYNTH_STRING_PERIOD_MIN ? n : BOX_AUDIO_SYNTH_STRING_PERIOD_MIN;
    d = (int32_t)(n - 1.5);
    f = n - 1 - (double)d;
    w = 1.0 / n;
    s->inc = inc;
    s->period = n;
    s->delay = d;
    s->c = (float)((double)box_audio_core_sin(w * (1 - f) * 0.5) /
        (double)box_audio_core_sin(w * (1 + f) * 0.5));
    h = (double)s->b1 + 2 * (double)s->b0 * (double)box_audio_core_sin(w + 0.25);
    g = box_audio_synth_exp2(-BOX_AUDIO_SYNTH_DB60 * n / s->decay_frames) / h;
    s->gain = (float)(g < BOX_AUDIO_SYNTH_STRING_GAIN_MAX ? g : BOX_AUDIO_SYNTH_STRING_GAIN_MAX);
}

/** Run the past of the delay line of a string through its lowpass and allpass at its new read
 * point, so the state of both is as if the read point had always been there: the lowpass
 * exactly, the allpass from its input BOX_AUDIO_SYNTH_STRING_PRIME frames back.
 *
 * @param s The string, tuned to its new delay
 */
static void box_audio_synth_string_prime(
    box_audio_synth_string_t *s)
{
    const int32_t mask = BOX_AUDIO_SYNTH_STRING_FRAMES - 1;
    int32_t back = BOX_AUDIO_SYNTH_STRING_PRIME, at, i;
    float x, f;
    back = s->delay + back + 3 < BOX_AUDIO_SYNTH_STRING_FRAMES ? back :
        BOX_AUDIO_SYNTH_STRING_FRAMES - 3 - s->delay;
    at = s->write - s->delay - back;
    s->x1 = s->line[(at - 1) & mask];
    s->x2 = s->line[(at - 2) & mask];
    s->f1 = s->b0 * (s->x1 + s->line[(at - 3) & mask]) + s->b1 * s->x2;
    s->a1 = s->f1;
    for (i = 0; i < back; i ++) {
        x = s->line[(at + i) & mask];
        f = s->b0 * (x + s->x2) + s->b1 * s->x1;
        s->a1 = box_audio_clean(s->c * (f - s->a1) + s->f1);
        s->x2 = s->x1;
        s->x1 = x;
        s->f1 = f;
    }
}

/** Tune a string cheaply to a frequency while its pitch moves: its delay line and its
 * allpass as box_audio_synth_string_tune does, without its gain, which moves little.
 *
 * The period comes from the last one by two terms of the series of 1 / (1 - e), e the step
 * of the pitch, or by a division when the pitch jumps.
 *
 * The sines of the coefficient come from the first terms of their series: within a few
 * millionths of the exact one, far under a cent, from BOX_AUDIO_SYNTH_STRING_CHEAP frames on.
 *
 * @param s The string
 * @param inc Turns per frame of its oscillator
 */
static void box_audio_synth_string_follow(
    box_audio_synth_string_t *s,
    double inc)
{
    double e = 1 - inc * s->period, n, f, w = inc;
    float a, b;
    int32_t d;
    n = e > -0.01 && e < 0.01 ? s->period * (1 + e * (1 + e)) : 1.0 / inc;
    if (n > s->period_max) {
        n = s->period_max;
        w = 1.0 / n;
    }

    d = (int32_t)(n - 1.5);
    f = n - 1 - (double)d;
    a = (float)(BOX_AUDIO_SYNTH_PI * (1 - f) * w);
    b = (float)(BOX_AUDIO_SYNTH_PI * (1 + f) * w);
    a *= a;
    b *= b;
    s->inc = inc;
    s->period = n;
    s->delay = d;
    s->c = (float)(1 - f) * (1 - a * (1.0f / 6 - a * (1.0f / 120))) /
        ((float)(1 + f) * (1 - b * (1.0f / 6 - b * (1.0f / 120))));
}

/** Retune a string whose pitch moves: exactly every BOX_AUDIO_SYNTH_STRING_RETUNE frames and
 * at a short period, cheaply on the frames between.
 *
 * When the delay line changes by whole frames the allpass fraction jumps from one end of its
 * range to the other, and the read point by a frame. The old read point and its allpass play
 * on for BOX_AUDIO_SYNTH_STRING_FADE frames and crossfade into the new, primed one.
 *
 * A crossing during a crossfade ends it: the read point that was new becomes the old one.
 *
 * @param s The string
 * @param inc Turns per frame of its oscillator
 */
static void box_audio_synth_string_move(
    box_audio_synth_string_t *s,
    double inc)
{
    int32_t delay = s->delay;
    float c = s->c;
    if (-- s->retune <= 0 || inc * BOX_AUDIO_SYNTH_STRING_CHEAP > 1) {
        s->retune = BOX_AUDIO_SYNTH_STRING_RETUNE;
        box_audio_synth_string_tune(s, inc);
    } else {
        box_audio_synth_string_follow(s, inc);
    }

    if (s->delay != delay && !s->rough) {
        s->delay_old = delay;
        s->c_old = c;
        s->x1_old = s->x1;
        s->x2_old = s->x2;
        s->f1_old = s->f1;
        s->a1_old = s->a1;
        s->fade = BOX_AUDIO_SYNTH_STRING_FADE;
        box_audio_synth_string_prime(s);
    }
}

/** Pluck a string: tune it and fill its delay line with a burst of seeded noise of a period,
 * through a one pole lowpass of its hardness and one of its brightness, without its mean so
 * the string carries no DC, and scaled to a peak of 1.
 *
 * @param s The string
 * @param layer Its layer, clamped
 * @param inc Turns per frame of its oscillator
 * @param rate Frames per second
 * @param rng State of the noise of the source
 */
static void box_audio_synth_string_pluck(
    box_audio_synth_string_t *s,
    const box_audio_synth_layer_t *layer,
    double inc,
    float rate,
    uint32_t *rng)
{
    const int32_t mask = BOX_AUDIO_SYNTH_STRING_FRAMES - 1;
    double sum = 0, top = (double)rate / BOX_AUDIO_SYNTH_PLUCK_MIN;
    float y = 0, mean, peak = 0, a = 0.5f - 0.48f * layer->brightness;
    float h = 0.05f + 0.95f * layer->hardness, b = 0.1f + 0.9f * layer->brightness, z = 0;
    int32_t i, n;
    s->decay_frames = (double)layer->decay * (double)rate;
    s->period_max = top < BOX_AUDIO_SYNTH_STRING_PERIOD_MAX ? top :
        BOX_AUDIO_SYNTH_STRING_PERIOD_MAX;
    s->b0 = 0.5f * a;
    s->b1 = 1 - a;
    box_audio_synth_string_tune(s, inc);
    for (i = 0; i < BOX_AUDIO_SYNTH_STRING_FRAMES; i ++) {
        s->line[i] = 0;
    }

    n = s->delay;
    for (i = 0; i < n; i ++) {
        y += h * (box_audio_synth_noise(rng) - y);
        z += b * (y - z);
        s->line[(BOX_AUDIO_SYNTH_STRING_FRAMES + i - n) & mask] = z;
        sum += (double)z;
    }

    mean = (float)(sum / (double)n);
    for (i = 0; i < n; i ++) {
        float v = s->line[(BOX_AUDIO_SYNTH_STRING_FRAMES + i - n) & mask] - mean;
        float m = v < 0 ? -v : v;
        s->line[(BOX_AUDIO_SYNTH_STRING_FRAMES + i - n) & mask] = v;
        peak = m > peak ? m : peak;
    }

    if (peak > 0) {
        for (i = 0; i < n; i ++) {
            s->line[(BOX_AUDIO_SYNTH_STRING_FRAMES + i - n) & mask] /= peak;
        }
    }

    s->write = 0;
    s->x1 = 0;
    s->x2 = 0;
    s->f1 = 0;
    s->a1 = 0;
    s->peak = 0;
    s->window = n + 2;
    s->retune = 0;
    s->fade = 0;
    s->ringing = true;
}

/** Start a pass of the envelopes, the oscillators and the filter envelope, at the start of a
 * play and at each restart of a loop, and pluck the strings. The state of the filter goes on.
 *
 * @param core The core
 * @param src The source
 */
static void box_audio_synth_pass(
    box_audio_core_t *core,
    box_audio_synth_source_t *src)
{
    float rate = core->sample_rate;
    double top = BOX_AUDIO_SYNTH_NYQUIST * (double)rate, lo, hi, cut, inc;
    box_audio_synth_string_t *s;
    int32_t l, n;
    if (top > BOX_AUDIO_SYNTH_FREQUENCY_MAX) {
        top = BOX_AUDIO_SYNTH_FREQUENCY_MAX;
    }

    src->inc_max = top / (double)rate;
    src->envs = 0;
    src->glide_left = 0;
    src->modulated = false;
    src->ringing = 0;
    src->ringing_free = 0;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        const box_audio_synth_layer_t *layer = &part->layer;
        box_audio_synth_osc_t *o = &part->osc;
        o->wave = (int32_t)layer->wave;
        o->level = layer->level;
        o->env = layer->has_envelope ? l + 1 : (layer->rings_free ? BOX_AUDIO_SYNTH_ENV_FREE : 0);
        o->phase = 0;
        o->target = (int32_t)layer->modulates - 1;
        o->ring = layer->modulation == BOX_AUDIO_SYNTH_RING;
        o->audible = layer->audible != 0;
        o->fb1 = 0;
        o->fb2 = 0;
        o->shift = 0;
        o->width_base = (double)layer->pulse_width;
        o->width = o->width_base;
        o->width_step = 0;
        o->vibrato = 1;
        o->vibrato_step = 0;
        o->tremolo = 1;
        o->tremolo_step = 0;
        o->sub = layer->sub;
        o->sub_octave = layer->sub_octave >= 2 ? 2 : 1;
        o->wraps = 0;
        o->sync_base = (double)layer->sync > 1 ? (double)layer->sync : 1.0;
        o->sync = o->sync_base;
        o->sync_step = 0;
        o->synced = o->sync_base > 1;
        for (n = 0; n < src->lfos; n ++) {
            o->synced = o->synced || (src->lfo_patch[n].layer == (uint32_t)l + 1 &&
                src->lfo_patch[n].sync != 0);
        }

        o->synced = o->synced && o->wave != BOX_AUDIO_SYNTH_NOISE &&
            o->wave != BOX_AUDIO_SYNTH_PLUCK;
        for (n = 0; n < part->unison.voices && part->unison.voices > 1 && !part->string; n ++) {
            box_audio_synth_noise(&src->rng);
            part->unison.phase[n] = (double)(src->rng >> 11) * (1.0 / 2097152.0);
            part->unison.wraps[n] = 0;
        }

        src->modulated = src->modulated || o->target >= 0 || layer->feedback > 0;
        if (o->env != BOX_AUDIO_SYNTH_ENV_FREE) {
            src->envs |= 1u << o->env;
        }

        if (layer->has_envelope) {
            box_audio_synth_env_init(&part->env, &layer->envelope, rate, src->gate);
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

        for (s = part->string, n = 0; s; s = s->next, n ++) {
            inc = o->inc;
            if (part->unison.voices > 1) {
                inc *= part->unison.detune[n];
                inc = inc < src->inc_max ? inc : src->inc_max;
            }

            box_audio_synth_string_pluck(s, layer, inc, rate, &src->rng);
            src->ringing ++;
            src->ringing_free += o->env == BOX_AUDIO_SYNTH_ENV_FREE;
        }
    }

    if (src->modulated) {
        box_audio_synth_limits(src);
    }

    if (src->envs & 1u) {
        box_audio_synth_env_init(&src->env, &src->envelope, rate, src->gate);
    }

    top = BOX_AUDIO_SYNTH_CUTOFF_NYQUIST * (double)rate;
    cut = src->cutoff_base;
    if (src->keytrack != 0) {
        cut *= box_audio_synth_exp2((double)src->keytrack * src->key);
        cut = cut > BOX_AUDIO_SYNTH_CUTOFF_MIN ? cut : BOX_AUDIO_SYNTH_CUTOFF_MIN;
    }

    cut = cut < top ? cut : top;
    src->cutoff = cut;
    src->cutoff_top = top;
    src->control = BOX_AUDIO_SYNTH_CONTROL;
    if (src->amount != 0) {
        box_audio_synth_env_init(&src->fenv, &src->filter_envelope, rate, src->gate);
    }

    if (src->mods) {
        if (src->lfos) {
            box_audio_synth_lfo_pass(src, rate);
        } else {
            src->lfo_control = 1;
        }

        if (src->bends) {
            box_audio_synth_bend_pass(src, rate);
        }

        box_audio_synth_lfo_aim(src, rate, true);
    }

    box_audio_synth_coefs(src, rate);
}

/** Start the LFOs of a play: a copy of those of the patch, their own generator, seeded from
 * the seed of the patch and the count of its plays apart from the noise, so the noise and the
 * variances stay as without LFOs, and the start phase of each.
 *
 * @param core The core
 * @param src The source
 * @param p The patch, clamped
 * @param plays Count of the plays of the patch
 */
static void box_audio_synth_lfo_start(
    const box_audio_core_t *core,
    box_audio_synth_source_t *src,
    const box_audio_synth_patch_t *p,
    uint32_t plays)
{
    int32_t k;
    src->lfo_rng = box_audio_synth_hash(p->seed ^ 0x4C464F21u, plays);
    src->lfo_smooth = (float)(1 - box_audio_synth_exp2(-(double)BOX_AUDIO_SYNTH_CONTROL /
        (BOX_AUDIO_SYNTH_LFO_SMOOTH * (double)core->sample_rate * BOX_AUDIO_SYNTH_LN2)));
    for (k = 0; k < src->lfos; k ++) {
        const box_audio_synth_lfo_t *d = &p->lfo[k];
        src->lfo_patch[k] = *d;
        src->lfo[k].start = d->random_phase ?
            0.5 * (1.0 + (double)box_audio_synth_noise(&src->lfo_rng)) : (double)d->phase;
        src->lfo_cutoff = src->lfo_cutoff || (src->filter && d->cutoff != 0);
    }
}

/** Scale the decay and the release of an envelope of a play by the key of its note:
 * 2^(-key_scale key), clamped to the times of an envelope. An envelope without key_scale
 * stays as it is.
 *
 * @param e The envelope of the play
 * @param key The key in octaves
 */
static void box_audio_synth_key_scale(
    box_audio_synth_envelope_t *e,
    double key)
{
    double f, t;
    if (e->key_scale > 0) {
        f = box_audio_synth_exp2(-(double)e->key_scale * key);
        t = (double)e->decay * f;
        e->decay = (float)(t < BOX_AUDIO_SYNTH_LENGTH_MAX ? t : BOX_AUDIO_SYNTH_LENGTH_MAX);
        t = (double)e->release * f;
        e->release = (float)(t < BOX_AUDIO_SYNTH_LENGTH_MAX ? t : BOX_AUDIO_SYNTH_LENGTH_MAX);
    }
}

/** Aim the key of a source at the key of a pitch ratio, over the frames of a glide or at the
 * next frame, so the cutoff follows it. Without keytrack the key stays.
 *
 * @param src The source
 * @param pitch The pitch ratio, without the variance
 * @param frames Frames of the glide, 0 to jump
 */
static void box_audio_synth_key_aim(
    box_audio_synth_source_t *src,
    float pitch,
    int32_t frames)
{
    if (src->keytrack != 0) {
        src->key_target = box_audio_synth_log2((double)pitch);
        src->key_left = frames > 0 ? frames : 1;
        src->key_step = (src->key_target - src->key) / (double)src->key_left;
        src->key_control = 1;
    }
}

/** Advance the key of a source by a frame toward its target, and move the cutoff with it
 * every BOX_AUDIO_SYNTH_CONTROL frames and at its end. An LFO of the cutoff or a filter
 * envelope that runs takes the key in its own update of the cutoff instead.
 *
 * @param src The source, whose key moves
 * @param rate Frames per second
 */
static void box_audio_synth_key_next(
    box_audio_synth_source_t *src,
    float rate)
{
    if (-- src->key_left == 0) {
        src->key = src->key_target;
    } else {
        src->key += src->key_step;
    }

    if (!src->lfo_cutoff && (src->amount == 0 || src->fenv.segment >= BOX_AUDIO_SYNTH_DONE) &&
        (-- src->key_control <= 0 || src->key_left == 0))
    {
        src->key_control = BOX_AUDIO_SYNTH_CONTROL;
        box_audio_synth_cutoff(src, 0.0f, rate);
    }
}

/** The factor by which velocity scales a value of which it scales a share: 1 - share +
 * share * velocity, 1 at velocity 1 or a share of 0.
 *
 * @param share Share of the value that velocity scales, 0..1
 * @param velocity The velocity, 0..1
 * @return The factor, 0..1
 */
static float box_audio_synth_velocity(
    float share,
    float velocity)
{
    return 1 - share + share * velocity;
}

/** Take the velocity of a note into the layers of a play: the index of each modulator and the
 * hardness of each pluck by their share, before its first pass. A layer with a share of 0
 * keeps its value as it is.
 *
 * @param src The source, its parts holding copies of the layers of the patch
 * @param velocity The velocity, 0..1
 */
static void box_audio_synth_velocity_layers(
    box_audio_synth_source_t *src,
    float velocity)
{
    int32_t l;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_layer_t *layer = &src->part[l]->layer;
        if (layer->index_velocity > 0) {
            layer->index *= box_audio_synth_velocity(layer->index_velocity, velocity);
        }

        if (layer->hardness_velocity > 0) {
            layer->hardness *= box_audio_synth_velocity(layer->hardness_velocity, velocity);
        }
    }
}

void box_audio_source_synth(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t sound,
    float pitch,
    float velocity,
    bool loop,
    bool gate)
{
    box_audio_synth_slot_t *slot = &core->synths[core->sounds[sound].synth - 1];
    const box_audio_synth_patch_t *p = &slot->patch;
    box_audio_synth_source_t *src = &source->data.synth;
    box_audio_synth_string_t **at;
    int32_t l, k;
    source->kind = BOX_AUDIO_SOURCE_SYNTH;
    src->layers = (int32_t)p->layers;
    src->unison = false;
    src->stereo = false;
    src->strings = 0;
    for (l = 0; l < src->layers; l ++) {
        src->part[l] = &core->synth_parts[core->synth_free[-- core->synth_free_count]];
        src->part[l]->layer = p->layer[l];
        src->part[l]->string = NULL;
        box_audio_synth_unison_init(src->part[l]);
        if (src->part[l]->unison.voices > 1) {
            src->unison = true;
            src->stereo = src->stereo || p->layer[l].width > 0;
        }

        if (p->layer[l].wave == BOX_AUDIO_SYNTH_PLUCK) {
            at = &src->part[l]->string;
            for (k = 0; k < (int32_t)p->layer[l].voices; k ++) {
                *at = &core->synth_strings[core->string_free[-- core->string_free_count]];
                at = &(*at)->next;
            }

            *at = NULL;
            src->strings ++;
        }
    }

    src->velocity = velocity;
    if (velocity < 1) {
        box_audio_synth_velocity_layers(src, velocity);
    }

    src->key = box_audio_synth_log2((double)pitch);
    src->key_target = src->key;
    src->key_step = 0;
    src->key_left = 0;
    src->key_control = 0;
    src->envelope = p->envelope;
    box_audio_synth_key_scale(&src->envelope, src->key);
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_key_scale(&src->part[l]->layer.envelope, src->key);
    }

    src->filter_envelope = p->filter_envelope;
    src->cutoff_base = (double)p->cutoff;
    if (p->filter_velocity != 0 && velocity < 1) {
        src->cutoff_base *= box_audio_synth_exp2((double)p->filter_velocity *
            ((double)velocity - 1));
        src->cutoff_base = src->cutoff_base > BOX_AUDIO_SYNTH_CUTOFF_MIN ? src->cutoff_base :
            BOX_AUDIO_SYNTH_CUTOFF_MIN;
    }

    slot->plays ++;
    src->rng = box_audio_synth_hash(p->seed, slot->plays);
    src->variance = 1.0f + p->pitch_variance * box_audio_synth_noise(&src->rng);
    src->variance = src->variance > BOX_AUDIO_SYNTH_VARIANCE_MIN ? src->variance :
        BOX_AUDIO_SYNTH_VARIANCE_MIN;
    src->amp = 1.0f + p->level_variance * box_audio_synth_noise(&src->rng);
    src->amp = src->amp > 0 ? src->amp : 0.0f;
    src->pitch = pitch * src->variance;
    src->loop = loop;
    src->gate = gate;
    src->filter = (int32_t)p->filter;
    src->amount = src->filter ? p->filter_amount : 0.0f;
    src->keytrack = src->filter ? p->keytrack : 0.0f;
    if (p->filter_envelope_velocity > 0 && velocity < 1) {
        src->amount *= box_audio_synth_velocity(p->filter_envelope_velocity, velocity);
    }

    src->glide_frames = box_audio_synth_frames(p->glide, core->sample_rate);
    src->stages = box_audio_svf_init(src->svf, p->slope, p->resonance);
    src->drive = p->drive;
    src->svf_r[0] = src->svf[0];
    src->svf_r[1] = src->svf[1];

    src->lfos = (int32_t)p->lfos;
    src->lfo_cutoff = false;
    src->lfo_octaves = 0;
    if (src->lfos) {
        box_audio_synth_lfo_start(core, src, p, slot->plays);
    }

    src->bends = false;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_layer_t *layer = &src->part[l]->layer;
        const box_audio_synth_pitch_envelope_t *d;
        if (!layer->has_pitch_envelope) {
            layer->pitch_envelope = p->pitch_envelope;
        }

        d = &layer->pitch_envelope;
        src->part[l]->bend.on = d->amount != 0 || d->release_amount != 0 || d->sync != 0;
        src->bends = src->bends || src->part[l]->bend.on;
    }

    src->mods = src->lfos > 0 || src->bends;
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
        for (l = 0; l < src->layers; l ++) {
            box_audio_synth_osc_t *o = &src->part[l]->osc;
            box_audio_synth_move(src, o, box_audio_synth_glide_rest(src, o) * f,
                src->glide_frames);
        }

        src->glide_left = src->glide_frames;
    } else {
        for (l = 0; l < src->layers; l ++) {
            box_audio_synth_osc_t *o = &src->part[l]->osc;
            o->inc = o->inc * f < src->inc_max ? o->inc * f : src->inc_max;
            o->inc_end = o->inc_end * f < src->inc_max ? o->inc_end * f : src->inc_max;
        }
    }

    box_audio_synth_key_aim(src, pitch, src->glide_frames);

    if (src->modulated) {
        box_audio_synth_limits(src);
    }

    src->pitch = p;
}

/** An envelope of a source.
 *
 * @param src The source
 * @param e 0 for the envelope of the patch, 1 + layer for the own one of a layer
 * @return The envelope
 */
static box_audio_synth_env_t *box_audio_synth_env_of(
    box_audio_synth_source_t *src,
    int32_t e)
{
    return e == 0 ? &src->env : &src->part[e - 1]->env;
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

    for (e = 0; e <= src->layers; e ++) {
        if ((src->envs & (1u << e)) &&
            box_audio_synth_env_of(src, e)->segment < BOX_AUDIO_SYNTH_RELEASE)
        {
            held = true;
        }
    }

    if (!held) {
        goto done;
    }

    for (e = 0; e <= src->layers; e ++) {
        box_audio_synth_env_t *env = box_audio_synth_env_of(src, e);
        if ((src->envs & (1u << e)) && env->segment == BOX_AUDIO_SYNTH_HOLD &&
            env->frames[BOX_AUDIO_SYNTH_HOLD] > 0 && !env->gate)
        {
            env->left = env->frames[BOX_AUDIO_SYNTH_HOLD];
        }
    }

    if (src->amount != 0 && src->fenv.segment == BOX_AUDIO_SYNTH_HOLD &&
        src->fenv.frames[BOX_AUDIO_SYNTH_HOLD] > 0 && !src->fenv.gate)
    {
        src->fenv.left = src->fenv.frames[BOX_AUDIO_SYNTH_HOLD];
    }

    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_osc_t *o = &src->part[l]->osc;
        if (l < (int32_t)np->layers) {
            box_audio_synth_layer_t *layer = &src->part[l]->layer;
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

    src->glide_frames = n;
    src->glide_left = n;
    src->pitch = p;
    box_audio_synth_key_aim(src, pitch, n);
    if (src->modulated) {
        box_audio_synth_limits(src);
    }

    result = true;
done:
    return result;
}

/** Gate or ungate an envelope that has not reached its release.
 *
 * @param e The envelope
 * @param gate Whether to gate it
 */
static void box_audio_synth_env_gate(
    box_audio_synth_env_t *e,
    bool gate)
{
    if (e->gate != gate && e->segment < BOX_AUDIO_SYNTH_RELEASE) {
        e->gate = gate;
        if (e->segment == BOX_AUDIO_SYNTH_HOLD && e->sustain > 0) {
            e->left = gate ? INT32_MAX : e->frames[BOX_AUDIO_SYNTH_HOLD];
        }
    }
}

void box_audio_synth_gate(
    box_audio_synth_source_t *src,
    bool gate)
{
    int32_t e;
    src->gate = gate;
    for (e = 0; e <= src->layers; e ++) {
        if (src->envs & (1u << e)) {
            box_audio_synth_env_gate(box_audio_synth_env_of(src, e), gate);
        }
    }

    if (src->amount != 0) {
        box_audio_synth_env_gate(&src->fenv, gate);
    }
}

/** Move an envelope that has not reached its release into it, from its current level over the
 * release of a note, with its curve.
 *
 * @param e The envelope
 */
static void box_audio_synth_env_release(
    box_audio_synth_env_t *e)
{
    if (e->segment < BOX_AUDIO_SYNTH_RELEASE) {
        e->gate = false;
        e->frames[BOX_AUDIO_SYNTH_RELEASE] = e->release;
        if (e->curved) {
            e->value = (float)e->level;
        }

        box_audio_synth_env_enter(e, BOX_AUDIO_SYNTH_RELEASE);
    }
}

void box_audio_synth_release(
    box_audio_synth_source_t *src)
{
    int32_t e;
    src->loop = false;
    src->gate = false;
    for (e = 0; e <= src->layers; e ++) {
        if (src->envs & (1u << e)) {
            box_audio_synth_env_release(box_audio_synth_env_of(src, e));
        }
    }

    if (src->amount != 0) {
        box_audio_synth_env_release(&src->fenv);
    }
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
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_osc_t *o = &src->part[l]->osc;
        o->inc = o->inc * o->glide;
        o->inc = o->inc < src->inc_max ? o->inc : src->inc_max;
        if (o->sweep > 0) {
            o->inc_end = o->inc_end * o->glide;
            o->inc_end = o->inc_end < src->inc_max ? o->inc_end : src->inc_max;
        }
    }

    if (-- src->glide_left == 0 && src->modulated) {
        box_audio_synth_limits(src);
    }
}

/** Advance the LFOs of a source by a control block, through their delays, fades and phases,
 * and its pitch envelopes, and aim every destination at their values at the end of the
 * block.
 *
 * @param src The source
 * @param rate Frames per second
 */
static void box_audio_synth_lfo_block(
    box_audio_synth_source_t *src,
    float rate)
{
    float raw;
    double t;
    int32_t k;
    for (k = 0; k < src->lfos; k ++) {
        box_audio_synth_mod_t *m = &src->lfo[k];
        if (m->delay > 0) {
            m->delay -= BOX_AUDIO_SYNTH_CONTROL;
        } else {
            m->gain = m->gain + m->fade_step < 1 ? m->gain + m->fade_step : 1.0f;
            t = m->phase + m->inc;
            if (t >= 1) {
                t -= (double)(int64_t)t;
                if (m->wave == BOX_AUDIO_SYNTH_LFO_RANDOM) {
                    m->held = box_audio_synth_noise(&src->lfo_rng);
                }
            }

            m->phase = t;
        }

        raw = box_audio_synth_lfo_wave(m);
        m->value = m->smooth ? m->value + (raw - m->value) * src->lfo_smooth : raw;
    }

    if (src->bends) {
        box_audio_synth_bend_block(src);
    }

    box_audio_synth_lfo_aim(src, rate, false);
}

/** Advance the LFOs of a source by a frame: a control block every BOX_AUDIO_SYNTH_CONTROL
 * frames, and a step of each destination toward its aim in between.
 *
 * @param src The source
 * @param rate Frames per second
 */
static inline void box_audio_synth_lfo_next(
    box_audio_synth_source_t *src,
    float rate)
{
    int32_t l;
    if (-- src->lfo_control <= 0) {
        src->lfo_control = BOX_AUDIO_SYNTH_CONTROL;
        box_audio_synth_lfo_block(src, rate);
    }

    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_osc_t *o = &src->part[l]->osc;
        o->vibrato += o->vibrato_step;
        o->width += o->width_step;
        o->tremolo += o->tremolo_step;
        if (o->synced) {
            o->sync += o->sync_step;
        }
    }

    src->tremolo += src->tremolo_step;
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
    int32_t l;
    if (src->envs & 1u) {
        levels[0] = box_audio_synth_env_next(&src->env);
        result = src->env.segment < BOX_AUDIO_SYNTH_DONE;
    }

    for (l = 0; l < src->layers; l ++) {
        if (src->envs & (2u << l)) {
            box_audio_synth_env_t *e = &src->part[l]->env;
            levels[l + 1] = box_audio_synth_env_next(e);
            result = result || e->segment < BOX_AUDIO_SYNTH_DONE;
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

/** Render a frame of a pulse, a square that is high for a share of its period other than half,
 * without its DC: a step up at phase 0 and one down at the width, each with its polyBLEP. The
 * width keeps a frame from either end, so the two steps never fall in one frame.
 *
 * @param t Phase in turns, 0..1
 * @param dt Turns per frame
 * @param w Share of the period it is high
 * @return The sample, 2 w - 2..2 w
 */
static inline float box_audio_synth_pulse(
    double t,
    double dt,
    double w)
{
    double lo = dt < 0.5 ? dt : 0.5;
    w = w > lo ? (w < 1 - lo ? w : 1 - lo) : lo;

    return (float)((t < w ? 1.0 : -1.0) - (2 * w - 1)) + box_audio_synth_blep(t, dt) -
        box_audio_synth_blep(t < w ? t + 1 - w : t - w, dt);
}

/** Render a frame of a wave at a phase.
 *
 *
 * A phase that moves backwards takes the magnitude of its speed as dt: the polyBLEP is odd in
 * time and the step changes sign with the direction, the polyBLAMP is even in time and the
 * change of slope keeps its sign, so both corrections stay the same function of the phase.
 *
 * @param wave A box_audio_synth_wave_t
 * @param t Phase in turns, 0..1
 * @param dt Turns per frame, the magnitude of the speed of the phase
 * @param width Square: share of the period it is high
 * @param rng State of the noise
 * @return The sample, -1..1, a pulse 2 width - 2..2 width
 */
static inline float box_audio_synth_wave(
    int32_t wave,
    double t,
    double dt,
    double width,
    uint32_t *rng)
{
    float x;
    switch (wave) {
    case BOX_AUDIO_SYNTH_SAW:
        x = (float)(2 * t - 1) - box_audio_synth_blep(t, dt);
        break;
    case BOX_AUDIO_SYNTH_SQUARE:
        if (width != 0.5) {
            x = box_audio_synth_pulse(t, dt, width);
        } else {
            x = (t < 0.5 ? 1.0f : -1.0f) + box_audio_synth_blep(t, dt) -
                box_audio_synth_blep(t < 0.5 ? t + 0.5 : t - 0.5, dt);
        }

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

    return x;
}

/** Wrap a phase into 0..1.
 *
 * @param t Phase in turns
 * @return The phase, 0..1
 */
static inline double box_audio_synth_wrap(
    double t)
{
    if (t < 0 || t >= 1) {
        t -= (double)(int64_t)t;
        t = t < 0 ? t + 1 : t;
        t = t >= 1 ? t - 1 : t;
    }

    return t;
}

/** Value of a wave at a phase without its corrections, a pulse without its DC, as
 * box_audio_synth_wave renders it away from its edges.
 *
 * @param wave A box_audio_synth_wave_t, not noise
 * @param t Phase in turns, 0..1
 * @param width Square: share of the period it is high
 * @return The value
 */
static inline double box_audio_synth_naive(
    int32_t wave,
    double t,
    double width)
{
    double x;
    switch (wave) {
    case BOX_AUDIO_SYNTH_SAW:
        x = 2 * t - 1;
        break;
    case BOX_AUDIO_SYNTH_SQUARE:
        x = (t < width ? 1.0 : -1.0) - (2 * width - 1);
        break;
    case BOX_AUDIO_SYNTH_TRIANGLE:
        x = t < 0.5 ? 4 * t - 1 : 3 - 4 * t;
        break;
    default:
        x = box_audio_core_sin(t);
        break;
    }

    return x;
}

/** Slope of a triangle or a sine at a phase, per turn.
 *
 * @param wave BOX_AUDIO_SYNTH_TRIANGLE or BOX_AUDIO_SYNTH_SINE
 * @param t Phase in turns, 0..1
 * @return The slope
 */
static inline double box_audio_synth_slope(
    int32_t wave,
    double t)
{
    return wave == BOX_AUDIO_SYNTH_TRIANGLE ? (t < 0.5 ? 4.0 : -4.0) :
        2 * BOX_AUDIO_SYNTH_PI * (double)box_audio_core_sin(t + 0.25);
}

/** Corrections of the edges of a wave that fall at unwrapped phases e, lo < e <= hi, seen from
 * an unwrapped phase s: the polyBLEP of each step of a saw or a square, the polyBLAMP of each
 * corner of a triangle, as box_audio_synth_wave adds them.
 *
 * A synced wave restarts, so an edge near the restart counts only on the side of it that the
 * wave crossed it on.
 *
 * @param wave A box_audio_synth_wave_t, not noise
 * @param width Square: share of the period it is high, clamped as box_audio_synth_pulse does
 * @param s Phase in turns, unwrapped
 * @param ds Turns per frame, at most a half
 * @param lo Edges after this phase
 * @param hi Edges up to this phase, at most a turn and ds after lo
 * @return The correction
 */
static inline double box_audio_synth_edges(
    int32_t wave,
    double width,
    double s,
    double ds,
    double lo,
    double hi)
{
    double at[2] = { 0, 0.5 }, step[2] = { 8, -8 }, result = 0, e, d;
    int32_t i, n = 2;
    if (wave == BOX_AUDIO_SYNTH_SAW) {
        n = 1;
        step[0] = -2;
    } else if (wave == BOX_AUDIO_SYNTH_SQUARE) {
        at[1] = width;
        step[0] = 2;
        step[1] = -2;
    } else if (wave != BOX_AUDIO_SYNTH_TRIANGLE) {
        n = 0;
    }

    for (i = 0; i < n; i ++) {
        for (e = lo - box_audio_synth_wrap(lo - at[i]) + 1; e <= hi; e += 1) {
            d = s - e;
            if (d > -ds && d < ds) {
                d = d < 0 ? d + 1 : d;
                result += wave == BOX_AUDIO_SYNTH_TRIANGLE ?
                    0.5 * step[i] * ds * (double)box_audio_synth_blamp(d, ds) :
                    0.5 * step[i] * (double)box_audio_synth_blep(d, ds);
            }
        }
    }

    return result;
}

/** Render a frame of a hard synced oscillator: its wave at the phase of the master times the
 * ratio, so it restarts at phase 0 on every wrap of the master, plus a shift.
 *
 * Near a wrap each edge of the wave is corrected on the side of the restart it falls on, and
 * a polyBLEP on the master phase, at the wrap's fraction of a frame, corrects the jump of the
 * restart by its size; a polyBLAMP the change of slope of a triangle or a sine.
 *
 * @param o The oscillator, synced
 * @param t Phase of the master in turns, 0..1
 * @param dt Turns per frame of the master
 * @param shift Turns added to the synced phase, its modulation and feedback
 * @param moved Change of the shift since the last frame, for the speed of the edges
 * @param rng State of the noise
 * @return The sample
 */
static inline float box_audio_synth_sync(
    const box_audio_synth_osc_t *o,
    double t,
    double dt,
    double shift,
    double moved,
    uint32_t *rng)
{
    double ratio = o->sync, ds = dt * ratio + moved, u = t * ratio + shift, end = ratio + shift;
    double width = o->width, start, x, lo;
    if (moved != 0) {
        ds -= (double)(int64_t)ds;
        ds = ds >= 0.5 ? ds - 1 : (ds < -0.5 ? ds + 1 : ds);
        ds = ds < 0 ? -ds : ds;
    }

    ds = ds < BOX_AUDIO_SYNTH_NYQUIST ? ds : BOX_AUDIO_SYNTH_NYQUIST;
    if (t >= dt && t <= 1 - dt) {
        x = box_audio_synth_wave(o->wave, box_audio_synth_wrap(u), ds, width, rng);
    } else {
        if (o->wave == BOX_AUDIO_SYNTH_SQUARE && width != 0.5) {
            lo = ds < 0.5 ? ds : 0.5;
            width = width > lo ? (width < 1 - lo ? width : 1 - lo) : lo;
        }

        start = box_audio_synth_wrap(shift);
        x = box_audio_synth_naive(o->wave, box_audio_synth_wrap(u), width);
        if (t > 1 - dt) {
            x += box_audio_synth_edges(o->wave, width, u, ds, u - ds, end) +
                box_audio_synth_edges(o->wave, width, u - ratio, ds, shift, u - ratio + ds);
        } else {
            x += box_audio_synth_edges(o->wave, width, u, ds, shift, u + ds) +
                box_audio_synth_edges(o->wave, width, u + ratio, ds, u + ratio - ds, end);
        }

        end = box_audio_synth_wrap(end);
        x += 0.5 * (box_audio_synth_naive(o->wave, start, width) -
            box_audio_synth_naive(o->wave, end, width)) * (double)box_audio_synth_blep(t, dt);
        if (o->wave == BOX_AUDIO_SYNTH_TRIANGLE || o->wave == BOX_AUDIO_SYNTH_SINE) {
            x += 0.5 * ratio * (box_audio_synth_slope(o->wave, start) -
                box_audio_synth_slope(o->wave, end)) * dt * (double)box_audio_synth_blamp(t, dt);
        }
    }

    return (float)x;
}

/** Render a frame of the sub oscillator of a layer: a square sub_octave octaves under it at
 * the phase of the layer, with its polyBLEP. Its phase is that of the layer plus the wraps of
 * it, over 2 or 4, so it flips on every wrap of the layer and stays locked to it.
 *
 * @param t Phase of the layer in turns, 0..1, without its modulation and feedback
 * @param wraps Times the phase of the layer wrapped
 * @param octave Octaves under the layer, 1 or 2
 * @param dt Turns per frame of the layer
 * @return The sample, -1..1
 */
static inline float box_audio_synth_sub(
    double t,
    uint32_t wraps,
    int32_t octave,
    double dt)
{
    double scale = octave > 1 ? 0.25 : 0.5;
    double s = (t + (double)(wraps & (octave > 1 ? 3u : 1u))) * scale;
    dt *= scale;

    return (s < 0.5 ? 1.0f : -1.0f) + box_audio_synth_blep(s, dt) -
        box_audio_synth_blep(s < 0.5 ? s + 0.5 : s - 0.5, dt);
}

/** Advance the phase and the sweep of an oscillator by a frame.
 *
 * @param o The oscillator
 * @param inc Turns of the frame: its inc, or its inc times the vibrato of the LFOs
 */
static inline void box_audio_synth_osc_advance(
    box_audio_synth_osc_t *o,
    double inc)
{
    double t = o->phase + inc;
    o->wraps += t >= 1;
    o->phase = t >= 1 ? t - 1 : t;
    if (o->sweep > 0) {
        o->sweep --;
        o->inc = o->sweep ? o->inc * o->ratio : o->inc_end;
    }
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
    float x = o->synced ? box_audio_synth_sync(o, o->phase, o->inc, 0, 0, rng) :
        box_audio_synth_wave(o->wave, o->phase, o->inc, o->width, rng);
    if (o->sub != 0) {
        x += o->sub * box_audio_synth_sub(o->phase, o->wraps, o->sub_octave, o->inc);
    }

    box_audio_synth_osc_advance(o, o->inc);

    return x;
}

/** Render a frame of a string that rings and advance it.
 *
 * A string follows the turns per frame it is given, glides, sweeps and vibrato included: tuned
 * cheaply every frame and exactly every BOX_AUDIO_SYNTH_STRING_RETUNE frames while it moves,
 * never at a steady pitch. A period under BOX_AUDIO_SYNTH_STRING_SILENCE leaves src->ringing.
 *
 * Per frame the frame out of the delay line goes through the lowpass (b0, b1, b0), the
 * allpass and the gain of the loop back into it; during the crossfade of a crossing the old
 * read point too, and the sample and the frame back into the line move from it to the new.
 *
 * @param src The source
 * @param s The string, ringing
 * @param rings_free Whether its layer has no envelope, so it counts in ringing_free
 * @param inc Turns of the frame
 * @return The sample, the frame out of the delay line
 */
static inline float box_audio_synth_string_ring(
    box_audio_synth_source_t *src,
    box_audio_synth_string_t *s,
    bool rings_free,
    double inc)
{
    const int32_t mask = BOX_AUDIO_SYNTH_STRING_FRAMES - 1;
    float x, f, a, m;
    if (inc != s->inc) {
        box_audio_synth_string_move(s, inc);
    }

    x = s->line[(s->write - s->delay) & mask];
    f = s->b0 * (x + s->x2) + s->b1 * s->x1;
    a = box_audio_clean(s->c * (f - s->a1) + s->f1);
    s->x2 = s->x1;
    s->x1 = x;
    s->f1 = f;
    s->a1 = a;
    if (s->fade > 0) {
        float xo = s->line[(s->write - s->delay_old) & mask], fo, ao, w;
        fo = s->b0 * (xo + s->x2_old) + s->b1 * s->x1_old;
        ao = box_audio_clean(s->c_old * (fo - s->a1_old) + s->f1_old);
        s->x2_old = s->x1_old;
        s->x1_old = xo;
        s->f1_old = fo;
        s->a1_old = ao;
        w = (float)s->fade * (1.0f / (BOX_AUDIO_SYNTH_STRING_FADE + 1));
        x += (xo - x) * w;
        a += (ao - a) * w;
        s->fade --;
    }

    s->line[s->write] = box_audio_clean(s->gain * a);
    s->write = (s->write + 1) & mask;
    m = x < 0 ? -x : x;
    s->peak = m > s->peak ? m : s->peak;
    if (-- s->window <= 0) {
        if (s->peak < BOX_AUDIO_SYNTH_STRING_SILENCE) {
            s->ringing = false;
            src->ringing --;
            src->ringing_free -= rings_free;
        }

        s->peak = 0;
        s->window = s->delay + 2;
    }

    return x;
}

/** Render a frame of the string of a pluck layer of one voice and advance it and its
 * oscillator, while it rings.
 *
 * @param src The source
 * @param part The part of the string
 * @param inc Turns of the frame: the inc of its oscillator, or that times its vibrato
 * @return The sample, 0 once the string is silent
 */
static inline float box_audio_synth_string_next(
    box_audio_synth_source_t *src,
    box_audio_synth_part_t *part,
    double inc)
{
    box_audio_synth_string_t *s = part->string;
    box_audio_synth_osc_t *o = &part->osc;
    float x = 0;
    if (s->ringing) {
        x = box_audio_synth_string_ring(src, s, o->env == BOX_AUDIO_SYNTH_ENV_FREE, inc);
        box_audio_synth_osc_advance(o, inc);
    }

    return x;
}

/** Whether a source with strings plays on: while one of its envelopes plays, unless every
 * layer is a string and all of them, those of every unison voice, are silent, and while a
 * string without an envelope rings.
 *
 * @param src The source, with strings
 * @param envs Whether one of its envelopes plays
 * @return Whether it plays on
 */
static inline bool box_audio_synth_strings_play(
    const box_audio_synth_source_t *src,
    bool envs)
{
    bool result = envs || src->ringing_free > 0;
    if (src->strings == src->layers && !src->ringing) {
        result = false;
    }

    return result;
}

/** Turns per frame of an oscillator with the vibrato of the LFOs, under the top.
 *
 * @param src The source
 * @param o The oscillator
 * @return The turns
 */
static inline double box_audio_synth_osc_inc(
    const box_audio_synth_source_t *src,
    const box_audio_synth_osc_t *o)
{
    double inc = o->inc * o->vibrato;

    return inc < src->inc_max ? inc : src->inc_max;
}

/** Render a frame of the layers of a source with LFOs in which no layer modulates another.
 *
 * @param src The source
 * @param levels The level of each envelope for the frame
 * @return The mix of the layers
 */
static inline float box_audio_synth_layers_lfo(
    box_audio_synth_source_t *src,
    const float *levels)
{
    float mix = 0, x;
    int32_t l;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        box_audio_synth_osc_t *o = &part->osc;
        double inc = box_audio_synth_osc_inc(src, o);
        if (o->wave == BOX_AUDIO_SYNTH_PLUCK) {
            x = box_audio_synth_string_next(src, part, inc);
        } else {
            x = o->synced ? box_audio_synth_sync(o, o->phase, inc, 0, 0, &src->rng) :
                box_audio_synth_wave(o->wave, o->phase, inc, o->width, &src->rng);
            if (o->sub != 0) {
                x += o->sub * box_audio_synth_sub(o->phase, o->wraps, o->sub_octave, inc);
            }

            box_audio_synth_osc_advance(o, inc);
        }

        mix += x * o->level * o->tremolo * levels[o->env];
    }

    return mix;
}

/** Turns per frame an oscillator moves at under modulation, for its polyBLEP and polyBLAMP.
 *
 * The change of its modulated phase since the last frame, wrapped into -0.5..0.5 so that a
 * phase moving backwards is a negative speed and not nearly a turn, as a magnitude under the
 * top. A speed of 0 takes neither correction, so nothing divides by it.
 *
 * @param src The source
 * @param d Change of the phase in turns, its inc plus the change of its modulation
 * @return The magnitude of the speed, 0..inc_max
 */
static inline double box_audio_synth_speed(
    const box_audio_synth_source_t *src,
    double d)
{
    d -= (double)(int64_t)d;
    d = d >= 0.5 ? d - 1 : (d < -0.5 ? d + 1 : d);
    d = d < 0 ? -d : d;

    return d < src->inc_max ? d : src->inc_max;
}

/** Render a frame of the layers of a source in which a layer modulates another or itself.
 *
 * Modulators come after their carriers, so the layers render from the last: each adds its wave
 * times its envelope, scaled by its index, to the phase of its target, or multiplies it. With
 * LFOs each also takes its vibrato and its tremolo, the signal of a modulator included.
 *
 * The edges of a saw, square or triangle are smoothed with the speed of its modulated phase:
 * its inc plus the change of its modulation and feedback since the last frame. A frame in
 * which they did not change keeps its inc, bit identical to the unmodulated wave.
 *
 * @param src The source
 * @param levels The level of each envelope for the frame
 * @param lfo Whether the source has LFOs, a constant of each call
 * @return The mix of the layers
 */
static inline float box_audio_synth_modulate(
    box_audio_synth_source_t *src,
    const float *levels,
    bool lfo)
{
    double offset[BOX_AUDIO_SYNTH_LAYERS], t, shift, moved;
    float ring[BOX_AUDIO_SYNTH_LAYERS], mix = 0, x, signal, sub = 0;
    int32_t l;
    for (l = 0; l < src->layers; l ++) {
        offset[l] = 0;
        ring[l] = 1;
    }

    for (l = src->layers - 1; l >= 0; l --) {
        box_audio_synth_osc_t *o = &src->part[l]->osc;
        float level = lfo ? levels[o->env] * o->tremolo : levels[o->env];
        double inc = lfo ? box_audio_synth_osc_inc(src, o) : o->inc, dt = inc;
        double fb = (double)(o->feedback * 0.5f * (o->fb1 + o->fb2));
        t = o->phase + offset[l] + fb;
        if (t < 0 || t >= 1) {
            t -= (double)(int64_t)t;
            t = t < 0 ? t + 1 : t;
            t = t >= 1 ? t - 1 : t;
        }

        shift = offset[l] + fb;
        moved = shift - o->shift;
        if (shift != o->shift) {
            dt = box_audio_synth_speed(src, inc + (shift - o->shift));
            o->shift = shift;
        }

        if (o->wave == BOX_AUDIO_SYNTH_PLUCK) {
            x = box_audio_synth_string_next(src, src->part[l], inc);
        } else {
            x = o->synced ? box_audio_synth_sync(o, o->phase, inc, shift, moved, &src->rng) :
                box_audio_synth_wave(o->wave, t, dt, o->width, &src->rng);
            if (o->sub != 0) {
                sub = o->sub * box_audio_synth_sub(o->phase, o->wraps, o->sub_octave, inc);
            }

            box_audio_synth_osc_advance(o, inc);
        }

        o->fb2 = o->fb1;
        o->fb1 = x;
        if (o->sub != 0) {
            x += sub;
        }

        x *= ring[l];
        if (o->target < 0) {
            mix += x * o->level * level;
            continue;
        }

        signal = x * level;
        if (o->ring) {
            ring[o->target] *= 1 - o->index + o->index * signal;
        } else {
            offset[o->target] += (double)(o->index * signal);
        }

        if (o->audible) {
            mix += x * o->level * level;
        }
    }

    return mix;
}

/** Render a frame of the unison voices of a layer and advance them, with the sweep of its
 * oscillator.
 *
 * @param src The source
 * @param part The part of the layer, with more than one voice
 * @param shift Turns added to the phase of every voice, its modulation and feedback
 * Each voice has a sub of its own when the layer has one, from the phase of the voice without
 * the shift.
 *
 * @param inc Turns of the frame of its oscillator: its inc, or that times its vibrato
 * @param left Receives the left sample
 * @param right Receives the right sample
 * @param sub Receives the mean of the subs on both channels, 0 without a sub
 */
static inline void box_audio_synth_unison_next(
    box_audio_synth_source_t *src,
    box_audio_synth_part_t *part,
    double shift,
    double inc,
    float *left,
    float *right,
    float *sub)
{
    box_audio_synth_osc_t *o = &part->osc;
    box_audio_synth_unison_t *u = &part->unison;
    float l = 0, r = 0, sl = 0, sr = 0, x, y;
    double dt, t;
    int32_t k;
    for (k = 0; k < u->voices; k ++) {
        dt = inc * u->detune[k];
        dt = dt < src->inc_max ? dt : src->inc_max;
        if (o->synced) {
            x = box_audio_synth_sync(o, u->phase[k], dt, shift, 0, &src->rng);
        } else {
            t = shift != 0 ? box_audio_synth_wrap(u->phase[k] + shift) : u->phase[k];
            x = box_audio_synth_wave(o->wave, t, dt, o->width, &src->rng);
        }

        l += x * u->left[k];
        r += x * u->right[k];
        if (o->sub != 0) {
            y = o->sub * box_audio_synth_sub(u->phase[k], u->wraps[k], o->sub_octave, dt);
            sl += y * u->left[k];
            sr += y * u->right[k];
        }

        t = u->phase[k] + dt;
        u->wraps[k] += t >= 1;
        u->phase[k] = t >= 1 ? t - 1 : t;
    }

    box_audio_synth_osc_advance(o, inc);
    if (o->sub != 0) {
        l += sl;
        r += sr;
    }

    *left = l;
    *right = r;
    *sub = 0.5f * (sl + sr);
}

/** Render a frame of the strings of the unison voices of a pluck layer and advance them, each
 * at the detune of its voice and with its gains, and its oscillator while one rings.
 *
 * @param src The source
 * @param part The part of the layer, with more than one voice
 * @param inc Turns of the frame of its oscillator: its inc, or that times its vibrato
 * @param left Receives the left sample
 * @param right Receives the right sample
 */
static inline void box_audio_synth_strings_next(
    box_audio_synth_source_t *src,
    box_audio_synth_part_t *part,
    double inc,
    float *left,
    float *right)
{
    box_audio_synth_osc_t *o = &part->osc;
    box_audio_synth_unison_t *u = &part->unison;
    box_audio_synth_string_t *s;
    bool rings_free = o->env == BOX_AUDIO_SYNTH_ENV_FREE, rang = false;
    float l = 0, r = 0, x;
    double dt;
    int32_t k;
    for (s = part->string, k = 0; s; s = s->next, k ++) {
        if (s->ringing) {
            dt = inc * u->detune[k];
            dt = dt < src->inc_max ? dt : src->inc_max;
            x = box_audio_synth_string_ring(src, s, rings_free, dt);
            l += x * u->left[k];
            r += x * u->right[k];
            rang = true;
        }
    }

    if (rang) {
        box_audio_synth_osc_advance(o, inc);
    }

    *left = l;
    *right = r;
}

/** Render a frame of the layers of a source in which a layer has unison voices, in stereo.
 *
 * As box_audio_synth_modulate, the layers render from the last. A layer without unison is the
 * same on both channels. A unison layer that modulates, or feeds back, does so with the mean
 * of its channels, and a modulation of a unison layer moves the phase of each of its voices.
 *
 * A pluck layer with unison voices rings a string for each, which phase modulation leaves.
 *
 * @param src The source
 * @param levels The level of each envelope for the frame
 * @param lfo Whether the source has LFOs, the vibrato and tremolo of each layer, a constant
 * @param left Receives the left mix of the layers
 * @param right Receives the right mix
 */
static inline void box_audio_synth_mix_wide(
    box_audio_synth_source_t *src,
    const float *levels,
    bool lfo,
    float *left,
    float *right)
{
    double offset[BOX_AUDIO_SYNTH_LAYERS], shift;
    float ring[BOX_AUDIO_SYNTH_LAYERS], l = 0, r = 0, x, xl, xr, gain, signal, sub;
    int32_t n;
    for (n = 0; n < src->layers; n ++) {
        offset[n] = 0;
        ring[n] = 1;
    }

    for (n = src->layers - 1; n >= 0; n --) {
        box_audio_synth_part_t *part = src->part[n];
        box_audio_synth_osc_t *o = &part->osc;
        float level = lfo ? levels[o->env] * o->tremolo : levels[o->env];
        double inc = lfo ? box_audio_synth_osc_inc(src, o) : o->inc;
        shift = offset[n];
        if (o->feedback != 0) {
            shift += (double)(o->feedback * 0.5f * (o->fb1 + o->fb2));
        }

        sub = 0;
        if (part->unison.voices > 1) {
            if (part->string) {
                box_audio_synth_strings_next(src, part, inc, &xl, &xr);
            } else {
                box_audio_synth_unison_next(src, part, shift, inc, &xl, &xr, &sub);
            }

            x = 0.5f * (xl + xr);
            o->fb2 = o->fb1;
            o->fb1 = o->sub != 0 ? x - sub : x;
        } else {
            if (o->wave == BOX_AUDIO_SYNTH_PLUCK) {
                x = box_audio_synth_string_next(src, part, inc);
            } else {
                x = o->synced ? box_audio_synth_sync(o, o->phase, inc, shift, 0, &src->rng) :
                    box_audio_synth_wave(o->wave, box_audio_synth_wrap(o->phase + shift), inc,
                    o->width, &src->rng);
                if (o->sub != 0) {
                    sub = o->sub * box_audio_synth_sub(o->phase, o->wraps, o->sub_octave, inc);
                }

                box_audio_synth_osc_advance(o, inc);
            }

            o->fb2 = o->fb1;
            o->fb1 = x;
            if (o->sub != 0) {
                x += sub;
            }

            xl = x;
            xr = x;
        }

        if (o->target < 0 || o->audible) {
            gain = ring[n] * o->level * level;
            l += xl * gain;
            r += xr * gain;
        }

        if (o->target >= 0) {
            signal = x * ring[n] * level;
            if (o->ring) {
                ring[o->target] *= 1 - o->index + o->index * signal;
            } else {
                offset[o->target] += (double)(o->index * signal);
            }
        }
    }

    *left = l;
    *right = r;
}

/** Move the cutoff of a source by its filter envelope, every BOX_AUDIO_SYNTH_CONTROL frames,
 * unless an LFO moves the cutoff, which then takes the level of the envelope per control block.
 *
 * @param src The source, with a filter
 * @param rate Frames per second
 */
static inline void box_audio_synth_control(
    box_audio_synth_source_t *src,
    float rate)
{
    if (src->amount != 0 && src->fenv.segment < BOX_AUDIO_SYNTH_DONE) {
        float level = box_audio_synth_env_next(&src->fenv);
        if (!src->lfo_cutoff && (-- src->control <= 0 ||
            src->fenv.segment >= BOX_AUDIO_SYNTH_DONE))
        {
            src->control = BOX_AUDIO_SYNTH_CONTROL;
            box_audio_synth_cutoff(src, level, rate);
        }
    }
}

/** Render a block of a source in which a layer has unison voices: into core->src_l, and
 * core->src_r through the stages of the right channel when it is stereo.
 *
 * @param core The core
 * @param src The source
 * @param frames Frames to render
 * @return Frames it rendered
 */
static int32_t box_audio_synth_render_wide(
    box_audio_core_t *core,
    box_audio_synth_source_t *src,
    int32_t frames)
{
    float *out_l = core->src_l, *out_r = core->src_r, rate = core->sample_rate;
    float levels[2 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    bool stereo = src->stereo;
    int32_t i;
    levels[BOX_AUDIO_SYNTH_ENV_FREE] = 1;
    for (i = 0; i < frames; i ++) {
        float l, r, gain;
        bool playing = box_audio_synth_envs_next(src, levels);
        if (src->strings) {
            playing = box_audio_synth_strings_play(src, playing);
        }

        if (!playing) {
            if (!src->loop || (!src->envs && !src->strings)) {
                break;
            }

            box_audio_synth_pass(core, src);
            box_audio_synth_envs_next(src, levels);
        }

        if (src->glide_left > 0) {
            box_audio_synth_glide_next(src);
        }

        if (src->key_left > 0) {
            box_audio_synth_key_next(src, rate);
        }

        if (src->mods) {
            box_audio_synth_lfo_next(src, rate);
            box_audio_synth_mix_wide(src, levels, true, &l, &r);
        } else {
            box_audio_synth_mix_wide(src, levels, false, &l, &r);
        }

        if (src->filter) {
            box_audio_synth_control(src, rate);
            if (src->stages > 1) {
                l = box_audio_svf_drive(&src->svf[1], src->filter, l, src->drive);
                if (stereo) {
                    r = box_audio_svf_drive(&src->svf_r[1], src->filter, r, src->drive);
                }
            }

            l = box_audio_svf_drive(&src->svf[0], src->filter, l, src->drive);
            if (stereo) {
                r = box_audio_svf_drive(&src->svf_r[0], src->filter, r, src->drive);
            }
        }

        gain = src->mods ? src->amp * src->tremolo : src->amp;
        out_l[i] = box_audio_clean(l * gain);
        if (stereo) {
            out_r[i] = box_audio_clean(r * gain);
        }
    }

    return i;
}

int32_t box_audio_synth_render(
    box_audio_core_t *core,
    box_audio_synth_source_t *src,
    int32_t frames)
{
    float *out = core->src_l, rate = core->sample_rate;
    float levels[2 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    int32_t layers = src->layers, i, l;
    if (src->unison) {
        i = box_audio_synth_render_wide(core, src, frames);
        goto done;
    }

    levels[BOX_AUDIO_SYNTH_ENV_FREE] = 1;
    for (i = 0; i < frames; i ++) {
        float mix = 0, y;
        bool playing = box_audio_synth_envs_next(src, levels);
        if (src->strings) {
            playing = box_audio_synth_strings_play(src, playing);
        }

        if (!playing) {
            if (!src->loop || (!src->envs && !src->strings)) {
                break;
            }

            box_audio_synth_pass(core, src);
            box_audio_synth_envs_next(src, levels);
        }

        if (src->glide_left > 0) {
            box_audio_synth_glide_next(src);
        }

        if (src->key_left > 0) {
            box_audio_synth_key_next(src, rate);
        }

        if (src->mods) {
            box_audio_synth_lfo_next(src, rate);
        }

        if (src->modulated) {
            mix = src->mods ? box_audio_synth_modulate(src, levels, true) :
                box_audio_synth_modulate(src, levels, false);
        } else if (src->mods) {
            mix = box_audio_synth_layers_lfo(src, levels);
        } else if (src->strings) {
            for (l = 0; l < layers; l ++) {
                box_audio_synth_osc_t *o = &src->part[l]->osc;
                float x = o->wave == BOX_AUDIO_SYNTH_PLUCK ?
                    box_audio_synth_string_next(src, src->part[l], o->inc) :
                    box_audio_synth_osc_next(o, &src->rng);
                mix += x * o->level * levels[o->env];
            }
        } else {
            for (l = 0; l < layers; l ++) {
                box_audio_synth_osc_t *o = &src->part[l]->osc;
                mix += box_audio_synth_osc_next(o, &src->rng) * o->level * levels[o->env];
            }
        }

        if (src->filter) {
            if (src->amount != 0 && src->fenv.segment < BOX_AUDIO_SYNTH_DONE) {
                float level = box_audio_synth_env_next(&src->fenv);
                if (!src->lfo_cutoff && (-- src->control <= 0 ||
                    src->fenv.segment >= BOX_AUDIO_SYNTH_DONE))
                {
                    src->control = BOX_AUDIO_SYNTH_CONTROL;
                    box_audio_synth_cutoff(src, level, rate);
                }
            }

            y = mix;
            if (src->stages > 1) {
                y = box_audio_svf_drive(&src->svf[1], src->filter, y, src->drive);
            }

            y = box_audio_svf_drive(&src->svf[0], src->filter, y, src->drive);
        } else {
            y = mix;
        }

        if (src->mods) {
            y = y * src->amp * src->tremolo;
        } else {
            y = y * src->amp;
        }

        out[i] = box_audio_clean(y);
    }

done:
    return i;
}
