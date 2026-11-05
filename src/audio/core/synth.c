/**
 * @file audio/core/synth.c
 * @brief The synth source: band limited oscillators and seeded noise, each with a linear or
 * curved envelope, mixed, optionally driven through the oversampled soft clipper of saturate.h,
 * into a state variable filter of one or two stages, a transistor ladder or the three
 * bandpasses of a formant filter (formant.h), whose cutoff or vowel an envelope may move, and
 * an amp, rendered per frame from a copy of the patch. A change of the
 * pitch glides exponentially in frequency over the glide of the patch, and a legato play
 * retargets the layers of a voice without a restart of its envelopes. A layer may play as
 * unison voices, detuned and spread in stereo, which render through
 * box_audio_synth_render_wide, so a patch without them renders as before at the same cost. A
 * pluck layer is a Karplus-Strong string instead of an oscillator, tuned with an allpass for the
 * fraction of a frame, which follows the pitch of its oscillator and ends the voice once it is
 * silent; with unison it rings a string per voice, detuned and panned as the voices of an
 * oscillator, each plucked with its own noise. A layer may be hard synced, its wave restarted
 * on every cycle of its frequency at a ratio of it, the restart band limited by a polyBLEP. Up
 * to two LFOs move the pitch, the level, the cutoff or the vowel, the pulse width of squares
 * and the ratio of hard sync, per control block of frames and interpolated per frame
 * in between. A wavetable layer reads the frames of a table the main thread built, at the mip
 * level of its speed and at a position between two frames that its envelope and the LFOs move.
 * A comb filter is a feedback comb tuned to the note of the first layer, read at a fractional
 * delay with a 3rd order Lagrange interpolation and damped by a one pole lowpass at the cutoff
 * in its loop, whose phase delay at the fundamental comes off the delay so the comb stays in
 * tune; its delay lines come from the core, one per channel.
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

/** Seconds each of the two lowpasses of a drift walk takes to move 1 - 1/e of the way to its
 * target. A jump of the target from -1 to 1 moves the value, -1..1, by at most
 * 2 / (e * this) per second, so a drift of 20 cents moves about 1.2 cents per 50 ms */
#define BOX_AUDIO_SYNTH_DRIFT_SMOOTH 0.6

/** Shortest and longest seconds a drift walk holds a target */
#define BOX_AUDIO_SYNTH_DRIFT_HOLD_MIN 0.2
#define BOX_AUDIO_SYNTH_DRIFT_HOLD_MAX 2.0

/** Share of the drift of the pitch the cutoff drifts by, in cents */
#define BOX_AUDIO_SYNTH_DRIFT_CUTOFF 0.25

/** Shortest fade in seconds of an LFO with a delay, so its depth does not jump in at once */
#define BOX_AUDIO_SYNTH_LFO_FADE_MIN 0.005f

/** Pi */
#define BOX_AUDIO_SYNTH_PI 3.14159265358979323846

/** Hz of the lowest pole of the filter of pink noise, under which it is flat */
#define BOX_AUDIO_SYNTH_PINK_LOW 5.0

/** Ratio of the frequencies of two poles of the filter of pink noise, two octaves; the zero of
 * each section lies half way between, an octave over its pole */
#define BOX_AUDIO_SYNTH_PINK_SPACING 4.0

/** Hz of the leak of the integrator of brown noise, under which it is flat */
#define BOX_AUDIO_SYNTH_BROWN_LEAK 10.0

/** Hz of the highpass that keeps brown noise from its DC */
#define BOX_AUDIO_SYNTH_BROWN_DC 5.0

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

/** Shortest delay of a comb filter in frames, so the 4 taps of its interpolation lie in the
 * past: Nyquist */
#define BOX_AUDIO_SYNTH_COMB_DELAY_MIN 2.0

/** Longest delay of a comb filter in frames, so its last tap lies within its delay line */
#define BOX_AUDIO_SYNTH_COMB_DELAY_MAX ((double)(BOX_AUDIO_SYNTH_COMB_FRAMES - 3))

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
 * of the trapezoidal state variable filter, or of the ladder, which stay stable at any cutoff
 * and while it moves. Each stage is stable on its own, so their cascade is too.
 *
 * A comb filter takes the share of its lowpass instead, 1 - exp(-2 pi cutoff / rate), and 1,
 * undamped, at the top of the cutoff.
 *
 * @param src The source
 * @param rate Frames per second
 */
static void box_audio_synth_coefs(
    box_audio_synth_source_t *src,
    float rate)
{
    float g;
    int32_t s;
    if (src->filter == BOX_AUDIO_SYNTH_COMB) {
        src->comb_a = src->cutoff < src->cutoff_top ? 1 - box_audio_synth_exp2(
            -2 * BOX_AUDIO_SYNTH_PI * src->cutoff / ((double)rate * BOX_AUDIO_SYNTH_LN2)) : 1.0;
        goto done;
    }

    g = box_audio_svf_g(src->cutoff, rate);
    if (src->model == BOX_AUDIO_SYNTH_LADDER) {
        src->ladder_coefs.g = g;
    } else {
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

done:
    return;
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

/** Move the vowel of a formant filter by the level of the filter envelope: the vowel of the
 * patch plus vowel_amount times the level and the vowels of the LFOs, clamped to
 * 0..BOX_AUDIO_SYNTH_VOWEL_MAX.
 *
 * @param src The source, with a formant filter
 * @param level Level of the filter envelope, 0..1
 * @param rate Frames per second
 */
static void box_audio_synth_vowel(
    box_audio_synth_source_t *src,
    float level,
    float rate)
{
    double v = (double)src->vowel + (double)src->vowel_amount * (double)level;
    if (src->lfo_vowel) {
        v += src->lfo_vowels;
    }

    v = v < BOX_AUDIO_SYNTH_VOWEL_MAX ? (v > 0 ? v : 0.0) : BOX_AUDIO_SYNTH_VOWEL_MAX;
    box_audio_formant_vowel(&src->formant, (float)v, rate, src->cutoff_top);
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

/** Pole or zero of a first order section at a frequency, exp(-2 pi f / rate), the matched z
 * transform of it.
 *
 * @param hz Frequency in Hz
 * @param rate Frames per second
 * @return The pole or zero, 0..1
 */
static double box_audio_synth_color_root(
    double hz,
    float rate)
{
    return box_audio_synth_exp2(-2 * BOX_AUDIO_SYNTH_PI * hz /
        ((double)rate * BOX_AUDIO_SYNTH_LN2));
}

/** Set the filter that colors the noise of a layer for a rate and clear its state.
 *
 * Pink: sections of a pole and a zero an octave over it, the poles two octaves apart up to
 * Nyquist. Brown: a leaky integrator, then a highpass of its DC. The gain is 1 over the RMS
 * gain, from the partial fractions of the cascade: H = d + sum r_k / (1 - p_k / z).
 *
 * @param f The filter
 * @param color A box_audio_synth_color_t
 * @param rate Frames per second
 */
static void box_audio_synth_color_init(
    box_audio_synth_color_filter_t *f,
    uint32_t color,
    float rate)
{
    double p[BOX_AUDIO_SYNTH_COLOR_SECTIONS], z[BOX_AUDIO_SYNTH_COLOR_SECTIONS];
    double r[BOX_AUDIO_SYNTH_COLOR_SECTIONS], d = 1, h0, energy, hz, num, den;
    int32_t i, j, n = 0;
    for (i = 0; i < BOX_AUDIO_SYNTH_UNISON_MAX; i ++) {
        for (j = 0; j <= BOX_AUDIO_SYNTH_COLOR_SECTIONS; j ++) {
            f->state[i][j] = 0;
        }
    }

    if (color == BOX_AUDIO_SYNTH_PINK) {
        for (hz = BOX_AUDIO_SYNTH_PINK_LOW; hz < 0.5 * (double)rate &&
            n < BOX_AUDIO_SYNTH_COLOR_SECTIONS; hz *= BOX_AUDIO_SYNTH_PINK_SPACING)
        {
            f->pole[n] = (float)box_audio_synth_color_root(hz, rate);
            f->zero[n] = (float)box_audio_synth_color_root(hz * 2, rate);
            n ++;
        }
    } else if (color == BOX_AUDIO_SYNTH_BROWN) {
        f->pole[0] = (float)box_audio_synth_color_root(BOX_AUDIO_SYNTH_BROWN_LEAK, rate);
        f->zero[0] = 0;
        f->pole[1] = (float)box_audio_synth_color_root(BOX_AUDIO_SYNTH_BROWN_DC, rate);
        f->zero[1] = 1;
        n = 2;
    }

    for (i = 0; i < n; i ++) {
        p[i] = (double)f->pole[i];
        z[i] = (double)f->zero[i];
        d *= z[i] / p[i];
    }

    h0 = d;
    for (i = 0; i < n; i ++) {
        num = 1;
        den = 1;
        for (j = 0; j < n; j ++) {
            num *= 1 - z[j] / p[i];
            den *= j != i ? 1 - p[j] / p[i] : 1.0;
        }

        r[i] = num / den;
        h0 += r[i];
    }

    energy = h0 * h0;
    for (i = 0; i < n; i ++) {
        for (j = 0; j < n; j ++) {
            energy += r[i] * r[j] * p[i] * p[j] / (1 - p[i] * p[j]);
        }
    }

    f->sections = n;
    f->gain = n && energy > 0 ?
        (float)box_audio_synth_exp2(-0.5 * box_audio_synth_log2(energy)) : 1.0f;
}

/** Color a frame of white noise of a unison voice by the filter of its layer.
 *
 * @param f The filter, with sections
 * @param voice The unison voice, 0 for a layer without unison
 * @param x The white noise
 * @return The colored noise, at about the RMS level of the white
 */
static inline float box_audio_synth_color_next(
    box_audio_synth_color_filter_t *f,
    int32_t voice,
    float x)
{
    float *s = f->state[voice], in;
    int32_t i, n = f->sections;
    for (i = 0; i < n; i ++) {
        in = x;
        x = in - f->zero[i] * s[i] + f->pole[i] * s[i + 1];
        s[i] = in;
    }

    s[n] = x;

    return x * f->gain;
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
    double frames[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    double octaves = 0, vowels = 0, c, w, r;
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
        frames[d->layer] += (double)(d->position * v);
        dip[d->layer] *= 1 - d->amplitude * m->gain * 0.5f * (1 - m->value);
        octaves += (double)(d->cutoff * v);
        vowels += (double)(d->vowel * v);
    }

    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        box_audio_synth_osc_t *o = &part->osc;
        bool bend = src->bends && part->bend.on;
        c = cents[0] + cents[l + 1];
        if (bend) {
            c += 100 * box_audio_synth_bend_offset(part);
        }

        if (src->drifts) {
            box_audio_synth_unison_t *u = &part->unison;
            if (u->voices > 1) {
                for (k = 0; k < u->voices; k ++) {
                    r = box_audio_synth_exp2(src->drift * (double)u->walk[k].value *
                        (1.0 / 1200));
                    if (now) {
                        u->drift[k] = r;
                    }

                    u->drift_step[k] = (r - u->drift[k]) * (1.0 / BOX_AUDIO_SYNTH_CONTROL);
                }
            } else {
                c += src->drift * (double)part->walk.value;
            }
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

        if (o->table) {
            r = frames[0] + frames[l + 1];
            if (now) {
                o->position_lfo = r;
            }

            o->position_step = (r - o->position_lfo) * (1.0 / BOX_AUDIO_SYNTH_CONTROL);
        }
    }

    if (now) {
        src->tremolo = dip[0];
    }

    src->tremolo_step = (dip[0] - src->tremolo) * (1.0f / BOX_AUDIO_SYNTH_CONTROL);
    if (src->drifts) {
        octaves += src->drift * BOX_AUDIO_SYNTH_DRIFT_CUTOFF *
            (double)src->drift_cutoff.value * (1.0 / 1200);
    }

    if (src->comb[0] && src->layers > 0) {
        c = cents[0];
        if (src->bends && src->part[0]->bend.on) {
            c += 100 * box_audio_synth_bend_offset(src->part[0]);
        }

        src->comb_pitch = c != 0 ? box_audio_synth_exp2(c * (1.0 / 1200)) : 1.0;
    }

    if (src->lfo_cutoff) {
        src->lfo_octaves = octaves;
        box_audio_synth_cutoff(src, src->amount != 0 ? src->fenv.value : 0.0f, rate);
    }

    if (src->lfo_vowel) {
        src->lfo_vowels = vowels;
        box_audio_synth_vowel(src, src->vowel_amount != 0 ? src->fenv.value : 0.0f, rate);
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

/** Control blocks a drift walk holds its next target for, a random
 * BOX_AUDIO_SYNTH_DRIFT_HOLD_MIN to _MAX seconds.
 *
 * @param rng The generator of the drift
 * @param rate Frames per second
 * @return The blocks
 */
static int32_t box_audio_synth_walk_hold(
    uint32_t *rng,
    float rate)
{
    double t = BOX_AUDIO_SYNTH_DRIFT_HOLD_MIN + (BOX_AUDIO_SYNTH_DRIFT_HOLD_MAX -
        BOX_AUDIO_SYNTH_DRIFT_HOLD_MIN) * 0.5 * (1.0 + (double)box_audio_synth_noise(rng));

    return box_audio_synth_blocks((int32_t)(t * (double)rate));
}

/** Start a drift walk at a random value, settled there, toward a random target.
 *
 * @param w The walk
 * @param rng The generator of the drift
 * @param rate Frames per second
 */
static void box_audio_synth_walk_start(
    box_audio_synth_walk_t *w,
    uint32_t *rng,
    float rate)
{
    w->value = box_audio_synth_noise(rng);
    w->a = w->value;
    w->target = box_audio_synth_noise(rng);
    w->left = box_audio_synth_walk_hold(rng, rate);
}

/** Advance a drift walk by a control block: a new target once it held the last for its
 * time, and both lowpasses toward it. Each output is a mix of values in -1..1, so the value
 * stays in -1..1.
 *
 * @param w The walk
 * @param rng The generator of the drift
 * @param coef Share of the way each lowpass moves per control block
 * @param rate Frames per second
 */
static void box_audio_synth_walk_next(
    box_audio_synth_walk_t *w,
    uint32_t *rng,
    float coef,
    float rate)
{
    if (-- w->left <= 0) {
        w->target = box_audio_synth_noise(rng);
        w->left = box_audio_synth_walk_hold(rng, rate);
    }

    w->a += (w->target - w->a) * coef;
    w->value += (w->a - w->value) * coef;
}

/** Start the drift of a play: its own generator, seeded from the seed of the patch and the
 * count of its plays apart from the noise and the LFOs, so they stay as without drift.
 *
 * It picks the start phase of each oscillator with free_phase, and with drift a walk for each
 * layer, each of its unison voices and the cutoff.
 *
 * @param core The core
 * @param src The source, its layers set
 * @param p The patch, clamped
 * @param plays Count of the plays of the patch
 */
static void box_audio_synth_drift_start(
    const box_audio_core_t *core,
    box_audio_synth_source_t *src,
    const box_audio_synth_patch_t *p,
    uint32_t plays)
{
    float rate = core->sample_rate;
    int32_t l, k;
    src->drift_rng = box_audio_synth_hash(p->seed ^ 0x44524654u, plays);
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        part->start = p->free_phase ?
            0.5 * (1.0 + (double)box_audio_synth_noise(&src->drift_rng)) : 0.0;
        part->start = part->start < 1 ? part->start : 0.0;
    }

    if (!src->drifts) {
        goto done;
    }

    src->drift_coef = (float)(1 - box_audio_synth_exp2(-(double)BOX_AUDIO_SYNTH_CONTROL /
        (BOX_AUDIO_SYNTH_DRIFT_SMOOTH * (double)rate * BOX_AUDIO_SYNTH_LN2)));
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        box_audio_synth_walk_start(&part->walk, &src->drift_rng, rate);
        for (k = 0; k < part->unison.voices && part->unison.voices > 1; k ++) {
            box_audio_synth_walk_start(&part->unison.walk[k], &src->drift_rng, rate);
            part->unison.drift[k] = 1;
            part->unison.drift_step[k] = 0;
        }
    }

    box_audio_synth_walk_start(&src->drift_cutoff, &src->drift_rng, rate);
    src->lfo_cutoff = src->lfo_cutoff || (src->filter &&
        src->filter != BOX_AUDIO_SYNTH_FORMANT);

done:
    return;
}

/** Advance the drift of a source by a control block: the walk of each layer, each of its
 * unison voices and the cutoff.
 *
 * @param src The source, with drift
 * @param rate Frames per second
 */
static void box_audio_synth_drift_block(
    box_audio_synth_source_t *src,
    float rate)
{
    float coef = src->drift_coef;
    int32_t l, k;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        if (part->unison.voices > 1) {
            for (k = 0; k < part->unison.voices; k ++) {
                box_audio_synth_walk_next(&part->unison.walk[k], &src->drift_rng, coef, rate);
            }
        } else {
            box_audio_synth_walk_next(&part->walk, &src->drift_rng, coef, rate);
        }
    }

    if (src->filter) {
        box_audio_synth_walk_next(&src->drift_cutoff, &src->drift_rng, coef, rate);
    }
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

/** Arctangent of a value that is not negative, from a polynomial on 0..1 and pi / 2 minus the
 * arctangent of the inverse over 1.
 *
 * @param x The value, 0 or more
 * @return The arctangent in radians, within 1e-5
 */
static double box_audio_synth_atan(
    double x)
{
    double t = x > 1 ? 1.0 / x : x, t2 = t * t, a;
    a = t * (0.99997726 + t2 * (-0.33262347 + t2 * (0.19354346 + t2 * (-0.11643287 +
        t2 * (0.05265332 + t2 * -0.01172120)))));

    return x > 1 ? 0.5 * BOX_AUDIO_SYNTH_PI - a : a;
}

/** Delay in frames a comb filter aims at for its note: the period of the first layer times
 * the pitch of the comb and its tune, within BOX_AUDIO_SYNTH_COMB_MIN and the delay line.
 *
 * Less the phase delay of the lowpass of its loop at its lowest peak, the note with positive
 * feedback and half of it with negative, so that peak stays in tune.
 *
 * The lowpass y = a x + b y1, b = 1 - a, delays a frequency w by
 * atan(b sin w / (1 - b cos w)) / w frames.
 *
 * @param src The source, with a comb filter
 * @param rate Frames per second
 * @return The delay in frames
 */
static double box_audio_synth_comb_aim(
    const box_audio_synth_source_t *src,
    float rate)
{
    double top = (double)rate / BOX_AUDIO_SYNTH_COMB_MIN, inc = 0, n, b, w, sn, cs;
    if (src->layers > 0) {
        inc = src->part[0]->osc.inc * src->comb_pitch * src->comb_tune;
    }

    top = top < BOX_AUDIO_SYNTH_COMB_DELAY_MAX ? top : BOX_AUDIO_SYNTH_COMB_DELAY_MAX;
    n = inc * top > 1 ? 1.0 / inc : top;
    if (src->comb_a < 1) {
        b = 1 - src->comb_a;
        w = src->comb_feedback < 0 ? 0.5 / n : 1.0 / n;
        sn = (double)box_audio_core_sin(w);
        cs = (double)box_audio_core_sin(w + 0.25);
        n -= box_audio_synth_atan(b * sn / (1 - b * cs)) /
            (2 * BOX_AUDIO_SYNTH_PI * w);
    }

    return n > BOX_AUDIO_SYNTH_COMB_DELAY_MIN ? n : BOX_AUDIO_SYNTH_COMB_DELAY_MIN;
}

/** Start a pass of the envelopes, the oscillators and the filter envelope, at the start of a
 * play and at each restart of a loop, and pluck the strings. The state of the filter goes on;
 * a comb filter takes the delay of the note at once.
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
        o->phase = part->start;
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
        if (o->table) {
            o->position_base = (double)layer->position < (double)(o->table_frames - 1) ?
                (double)layer->position : (double)(o->table_frames - 1);
            o->position_env = layer->position_envelope;
            o->position_lfo = 0;
            o->position_step = 0;
            o->position = o->position_base;
        }

        for (n = 0; n < part->unison.voices && part->unison.voices > 1 && !part->string; n ++) {
            box_audio_synth_noise(&src->rng);
            part->unison.phase[n] = (double)(src->rng >> 11) * (1.0 / 2097152.0);
            part->unison.wraps[n] = 0;
        }

        box_audio_synth_color_init(&part->color, layer->color, rate);
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
    if (src->fenv_on) {
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

    if (src->filter == BOX_AUDIO_SYNTH_FORMANT) {
        box_audio_synth_vowel(src, src->vowel_amount != 0 ? src->fenv.value : 0.0f, rate);
    } else {
        box_audio_synth_coefs(src, rate);
    }

    if (src->comb[0]) {
        src->comb_delay = box_audio_synth_comb_aim(src, rate);
        src->comb_step = 0;
        src->comb_control = BOX_AUDIO_SYNTH_CONTROL;
    }
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
        src->lfo_cutoff = src->lfo_cutoff || (src->filter &&
            src->filter != BOX_AUDIO_SYNTH_FORMANT && d->cutoff != 0);
        src->lfo_vowel = src->lfo_vowel || (src->filter == BOX_AUDIO_SYNTH_FORMANT &&
            d->vowel != 0);
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

/** Start the drive of a source: take the clipper of its patch and clear its state.
 *
 * @param src The source
 * @param curve The clipper of the patch
 * @param dc Whether the clipper has an offset, whose DC a blocker takes off
 * @param rate Frames per second
 */
static void box_audio_synth_drive_start(
    box_audio_synth_source_t *src,
    const box_audio_saturate_curve_t *curve,
    bool dc,
    float rate)
{
    int32_t c;
    src->drive_curve = *curve;
    src->drive_dc = dc;
    src->drive_dc_coef = (float)(1.0 - 2.0 * 3.14159265358979323846 *
        (double)BOX_AUDIO_SYNTH_DRIVE_DC / (double)rate);
    for (c = 0; c < 2; c ++) {
        box_audio_oversample_clear(&src->drive_os[c]);
        src->drive_blocker[c].x1 = 0;
        src->drive_blocker[c].y1 = 0;
    }
}

/** Run a sample of the mix of the layers of a channel through the drive.
 *
 * @param src The source, with a drive
 * @param c The channel, 0 left or mono, 1 right
 * @param x The mix
 * @return The driven mix, BOX_AUDIO_SATURATE_LATENCY frames late
 */
static inline float box_audio_synth_drive_next(
    box_audio_synth_source_t *src,
    int32_t c,
    float x)
{
    float y = box_audio_saturate_next(&src->drive_os[c], &src->drive_curve, x);
    if (src->drive_dc) {
        y = box_audio_dc_next(&src->drive_blocker[c], y, src->drive_dc_coef);
    }

    return y;
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
    box_audio_synth_comb_t *comb;
    int32_t l, k, i;
    bool formant;
    source->kind = BOX_AUDIO_SOURCE_SYNTH;
    src->layers = (int32_t)p->layers;
    src->unison = false;
    src->stereo = false;
    src->strings = 0;
    src->tables = false;
    for (l = 0; l < src->layers; l ++) {
        src->part[l] = &core->synth_parts[core->synth_free[-- core->synth_free_count]];
        src->part[l]->layer = p->layer[l];
        src->part[l]->string = NULL;
        box_audio_synth_table_take(core, &src->part[l]->osc,
            p->layer[l].wave == BOX_AUDIO_SYNTH_WAVETABLE ? p->layer[l].table : 0);
        src->tables = src->tables || src->part[l]->osc.table != NULL;
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

    src->comb[0] = NULL;
    src->comb[1] = NULL;
    for (k = 0; k < slot->combs; k ++) {
        comb = &core->synth_combs[core->comb_free[-- core->comb_free_count]];
        for (i = 0; i < BOX_AUDIO_SYNTH_COMB_FRAMES; i ++) {
            comb->line[i] = 0;
        }

        comb->write = 0;
        comb->damp = 0;
        src->comb[k] = comb;
    }

    src->comb_feedback = p->feedback;
    src->comb_gain = 1 - (p->feedback < 0 ? -p->feedback : p->feedback);
    src->comb_tune = (double)p->tune;
    src->comb_pitch = 1;
    src->comb_a = 1;
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
    formant = src->filter == BOX_AUDIO_SYNTH_FORMANT;
    src->amount = src->filter && !formant ? p->filter_amount : 0.0f;
    src->keytrack = src->filter && !formant ? p->keytrack : 0.0f;
    src->vowel = formant ? p->vowel : 0.0f;
    src->vowel_amount = formant ? p->filter_vowel : 0.0f;
    if (p->filter_envelope_velocity > 0 && velocity < 1) {
        src->amount *= box_audio_synth_velocity(p->filter_envelope_velocity, velocity);
        src->vowel_amount *= box_audio_synth_velocity(p->filter_envelope_velocity, velocity);
    }

    src->fenv_on = src->amount != 0 || src->vowel_amount != 0;
    if (formant) {
        box_audio_formant_init(&src->formant, p->resonance);
    }

    src->glide_frames = box_audio_synth_frames(p->glide, core->sample_rate);
    src->stages = box_audio_svf_init(src->svf, p->slope, p->resonance);
    src->drive = p->drive;
    src->svf_r[0] = src->svf[0];
    src->svf_r[1] = src->svf[1];
    src->model = src->filter && src->filter <= BOX_AUDIO_SYNTH_NOTCH ?
        (int32_t)p->model : BOX_AUDIO_SYNTH_SVF;
    if (src->model == BOX_AUDIO_SYNTH_LADDER) {
        box_audio_ladder_init(&src->ladder_coefs, src->ladder, src->filter, p->slope,
            p->resonance, p->drive);
    }

    src->has_drive = p->has_drive != 0;
    if (src->has_drive) {
        box_audio_synth_drive_start(src, &slot->drive, p->pre_asymmetry > 0, core->sample_rate);
    }

    src->lfos = (int32_t)p->lfos;
    src->lfo_cutoff = false;
    src->lfo_octaves = 0;
    src->lfo_vowel = false;
    src->lfo_vowels = 0;
    if (src->lfos) {
        box_audio_synth_lfo_start(core, src, p, slot->plays);
    }

    src->drift = (double)p->drift;
    src->drifts = p->drift > 0;
    box_audio_synth_drift_start(core, src, p, slot->plays);
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

    src->mods = src->lfos > 0 || src->bends || src->drifts;
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

    if (src->fenv_on && src->fenv.segment == BOX_AUDIO_SYNTH_HOLD &&
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

    if (src->fenv_on) {
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

    if (src->fenv_on) {
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

    if (src->drifts) {
        box_audio_synth_drift_block(src, rate);
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
    int32_t l, k;
    if (-- src->lfo_control <= 0) {
        src->lfo_control = BOX_AUDIO_SYNTH_CONTROL;
        box_audio_synth_lfo_block(src, rate);
    }

    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_osc_t *o = &src->part[l]->osc;
        if (src->drifts) {
            box_audio_synth_unison_t *u = &src->part[l]->unison;
            for (k = 0; k < u->voices && u->voices > 1; k ++) {
                u->drift[k] += u->drift_step[k];
            }
        }

        o->vibrato += o->vibrato_step;
        o->width += o->width_step;
        o->tremolo += o->tremolo_step;
        if (o->synced) {
            o->sync += o->sync_step;
        }
    }

    src->tremolo += src->tremolo_step;
}

/** Move the position of every wavetable layer of a source for a frame: its position, plus
 * the step of the LFOs toward their aim, plus its depth times the level of its envelope,
 * clamped to the frames of its table.
 *
 * @param src The source, with a wavetable layer
 * @param levels The level of each envelope for the frame
 */
static inline void box_audio_synth_tables_next(
    box_audio_synth_source_t *src,
    const float *levels)
{
    double p, top;
    int32_t l;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_osc_t *o = &src->part[l]->osc;
        if (!o->table) {
            continue;
        }

        o->position_lfo += o->position_step;
        p = o->position_base + o->position_lfo + (double)(o->position_env * levels[o->env]);
        top = (double)(o->table_frames - 1);
        o->position = p > 0 ? (p < top ? p : top) : 0.0;
    }
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

/** Offset of each mip level of a wavetable in samples of one frame; the level holds the
 * offset times the frames of the table after the levels before it */
static const int32_t box_audio_synth_table_offset[BOX_AUDIO_TABLE_LEVELS] = {
    0, 2048, 3072, 3584, 3840, 3968, 4032, 4064, 4096, 4128 };

_Static_assert(4128 + BOX_AUDIO_TABLE_LEVEL_LENGTH(BOX_AUDIO_TABLE_LEVELS - 1) ==
    BOX_AUDIO_TABLE_FRAME_FLOATS, "the mip levels of a wavetable fill its frame floats");

/** Read a period of a mip level of a wavetable between samples, with the optimal 4 point 3rd
 * order interpolator for audio oversampled twice of Niemitalo, which a level is at least: its
 * images stay under -70 dB of a saw.
 *
 * @param y The period, mask + 1 samples
 * @param i Index of the sample at or before the read point
 * @param mask Samples of the period minus 1, a power of 2 minus 1
 * @param z Place of the read point between sample i and the next minus a half, -0.5..0.5
 * @return The sample
 */
static inline float box_audio_synth_table_tap(
    const float *y,
    int32_t i,
    int32_t mask,
    float z)
{
    float ym1 = y[(i - 1) & mask], y0 = y[i & mask], y1 = y[(i + 1) & mask];
    float y2 = y[(i + 2) & mask];
    float even1 = y1 + y0, odd1 = y1 - y0, even2 = y2 + ym1, odd2 = y2 - ym1;
    float c0 = even1 * 0.45868970870461956f + even2 * 0.04131401926395584f;
    float c1 = odd1 * 0.48068024766578432f + odd2 * 0.17577925564495955f;
    float c2 = even1 * -0.246185007019907091f + even2 * 0.24614027139700284f;
    float c3 = odd1 * -0.36030925263849456f + odd2 * 0.10174985775982505f;

    return ((c3 * z + c2) * z + c1) * z + c0;
}

/** Read a mip level of the wavetable of an oscillator at a phase, at the frame of its position
 * and, between two frames, mixed linearly with the next.
 *
 * @param o The oscillator, with a table
 * @param k The mip level
 * @param t Phase in turns, 0..1
 * @return The sample
 */
static inline float box_audio_synth_table_level(
    const box_audio_synth_osc_t *o,
    int32_t k,
    double t)
{
    int32_t len = BOX_AUDIO_TABLE_LEVEL_LENGTH(k), a = (int32_t)o->position, i;
    const float *level = o->table + o->table_frames * box_audio_synth_table_offset[k];
    double x = t * (double)len, w = o->position - (double)a;
    float y, z;
    i = (int32_t)x;
    z = (float)(x - (double)i) - 0.5f;
    y = box_audio_synth_table_tap(level + a * len, i, len - 1, z);
    if (w > 0 && a + 1 < o->table_frames) {
        y += (float)w * (box_audio_synth_table_tap(level + (a + 1) * len, i, len - 1, z) - y);
    }

    return y;
}

/** Render a frame of a wavetable at a phase: the mip level of the speed of the phase, the
 * first whose highest harmonic stays under Nyquist at it. Over the upper half of the octave
 * of a level it crossfades linearly into the next level, of half the harmonics, which it
 * reaches at the top of the octave, so a sweep moves from level to level without a step and
 * never aliases.
 *
 * @param o The oscillator, with a table
 * @param t Phase in turns, 0..1
 * @param dt Turns per frame, the magnitude of the speed of the phase
 * @return The sample
 */
static inline float box_audio_synth_table(
    const box_audio_synth_osc_t *o,
    double t,
    double dt)
{
    double top = 1.0 / (2 * BOX_AUDIO_TABLE_HARMONICS), fade;
    int32_t k = 0;
    float y;
    while (k < BOX_AUDIO_TABLE_LEVELS - 1 && dt > top) {
        top += top;
        k ++;
    }

    y = box_audio_synth_table_level(o, k, t);
    fade = k < BOX_AUDIO_TABLE_LEVELS - 1 ? 4 * dt / top - 3 : 0.0;
    if (fade > 0) {
        y += (float)fade * (box_audio_synth_table_level(o, k + 1, t) - y);
    }

    return y;
}

/** Render a frame of a wave at a phase.
 *
 *
 * A phase that moves backwards takes the magnitude of its speed as dt: the polyBLEP is odd in
 * time and the step changes sign with the direction, the polyBLAMP is even in time and the
 * change of slope keeps its sign, so both corrections stay the same function of the phase.
 *
 * @param o The oscillator, whose wave and wavetable it renders
 * @param t Phase in turns, 0..1
 * @param dt Turns per frame, the magnitude of the speed of the phase
 * @param width Square: share of the period it is high
 * @param rng State of the noise
 * @return The sample, -1..1, a pulse 2 width - 2..2 width
 */
static inline float box_audio_synth_wave(
    const box_audio_synth_osc_t *o,
    double t,
    double dt,
    double width,
    uint32_t *rng)
{
    float x;
    switch (o->wave) {
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
    case BOX_AUDIO_SYNTH_WAVETABLE:
        x = o->table ? box_audio_synth_table(o, t, dt) : box_audio_core_sin(t);
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
 * @param o The oscillator, whose wave is not noise
 * @param t Phase in turns, 0..1
 * @param dt Turns per frame, for the mip level of a wavetable
 * @param width Square: share of the period it is high
 * @return The value
 */
static inline double box_audio_synth_naive(
    const box_audio_synth_osc_t *o,
    double t,
    double dt,
    double width)
{
    double x;
    switch (o->wave) {
    case BOX_AUDIO_SYNTH_SAW:
        x = 2 * t - 1;
        break;
    case BOX_AUDIO_SYNTH_SQUARE:
        x = (t < width ? 1.0 : -1.0) - (2 * width - 1);
        break;
    case BOX_AUDIO_SYNTH_TRIANGLE:
        x = t < 0.5 ? 4 * t - 1 : 3 - 4 * t;
        break;
    case BOX_AUDIO_SYNTH_WAVETABLE:
        x = o->table ? box_audio_synth_table(o, t, dt) : box_audio_core_sin(t);
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
        x = box_audio_synth_wave(o, box_audio_synth_wrap(u), ds, width, rng);
    } else {
        if (o->wave == BOX_AUDIO_SYNTH_SQUARE && width != 0.5) {
            lo = ds < 0.5 ? ds : 0.5;
            width = width > lo ? (width < 1 - lo ? width : 1 - lo) : lo;
        }

        start = box_audio_synth_wrap(shift);
        x = box_audio_synth_naive(o, box_audio_synth_wrap(u), ds, width);
        if (t > 1 - dt) {
            x += box_audio_synth_edges(o->wave, width, u, ds, u - ds, end) +
                box_audio_synth_edges(o->wave, width, u - ratio, ds, shift, u - ratio + ds);
        } else {
            x += box_audio_synth_edges(o->wave, width, u, ds, shift, u + ds) +
                box_audio_synth_edges(o->wave, width, u + ratio, ds, u + ratio - ds, end);
        }

        end = box_audio_synth_wrap(end);
        x += 0.5 * (box_audio_synth_naive(o, start, ds, width) -
            box_audio_synth_naive(o, end, ds, width)) * (double)box_audio_synth_blep(t, dt);
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
 * @param color The filter that colors its noise
 * @param rng State of the noise
 * @return The sample, -1..1
 */
static inline float box_audio_synth_osc_next(
    box_audio_synth_osc_t *o,
    box_audio_synth_color_filter_t *color,
    uint32_t *rng)
{
    float x = o->synced ? box_audio_synth_sync(o, o->phase, o->inc, 0, 0, rng) :
        box_audio_synth_wave(o, o->phase, o->inc, o->width, rng);
    if (color->sections) {
        x = box_audio_synth_color_next(color, 0, x);
    }

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
                box_audio_synth_wave(o, o->phase, inc, o->width, &src->rng);
            if (part->color.sections) {
                x = box_audio_synth_color_next(&part->color, 0, x);
            }

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
                box_audio_synth_wave(o, t, dt, o->width, &src->rng);
            if (src->part[l]->color.sections) {
                x = box_audio_synth_color_next(&src->part[l]->color, 0, x);
            }

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
        if (src->drifts) {
            dt *= u->drift[k];
        }

        dt = dt < src->inc_max ? dt : src->inc_max;
        if (o->synced) {
            x = box_audio_synth_sync(o, u->phase[k], dt, shift, 0, &src->rng);
        } else {
            t = shift != 0 ? box_audio_synth_wrap(u->phase[k] + shift) : u->phase[k];
            x = box_audio_synth_wave(o, t, dt, o->width, &src->rng);
        }

        if (part->color.sections) {
            x = box_audio_synth_color_next(&part->color, k, x);
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
            if (src->drifts) {
                dt *= u->drift[k];
            }

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
                    box_audio_synth_wave(o, box_audio_synth_wrap(o->phase + shift), inc,
                    o->width, &src->rng);
                if (part->color.sections) {
                    x = box_audio_synth_color_next(&part->color, 0, x);
                }

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

/** Move the vowel of a formant filter by its filter envelope, every BOX_AUDIO_SYNTH_CONTROL
 * frames, unless an LFO moves the vowel, which then takes the level of the envelope per control
 * block.
 *
 * @param src The source, with a formant filter
 * @param rate Frames per second
 */
static inline void box_audio_synth_formant_control(
    box_audio_synth_source_t *src,
    float rate)
{
    if (src->vowel_amount != 0 && src->fenv.segment < BOX_AUDIO_SYNTH_DONE) {
        float level = box_audio_synth_env_next(&src->fenv);
        if (!src->lfo_vowel && (-- src->control <= 0 ||
            src->fenv.segment >= BOX_AUDIO_SYNTH_DONE))
        {
            src->control = BOX_AUDIO_SYNTH_CONTROL;
            box_audio_synth_vowel(src, level, rate);
        }
    }
}

/** Move the delay of a comb filter by a frame toward its aim, which it takes from the note
 * every BOX_AUDIO_SYNTH_CONTROL frames, so it follows a glide and the LFOs smoothly.
 *
 * @param src The source, with a comb filter
 * @param rate Frames per second
 */
static inline void box_audio_synth_comb_control(
    box_audio_synth_source_t *src,
    float rate)
{
    if (-- src->comb_control <= 0) {
        src->comb_control = BOX_AUDIO_SYNTH_CONTROL;
        src->comb_step = (box_audio_synth_comb_aim(src, rate) - src->comb_delay) *
            (1.0 / BOX_AUDIO_SYNTH_CONTROL);
    }

    src->comb_delay += src->comb_step;
}

/** Run a sample of a channel through a comb filter: y = x + feedback lowpass(delayed(y)), the
 * delayed output read at the fractional delay of the comb by a 3rd order Lagrange
 * interpolation of the 4 frames around it, and returned times the gain of the comb.
 *
 * @param src The source, with a comb filter
 * @param c The delay line of the channel
 * @param x The input
 * @return The output
 */
static inline float box_audio_synth_comb_next(
    const box_audio_synth_source_t *src,
    box_audio_synth_comb_t *c,
    float x)
{
    const int32_t mask = BOX_AUDIO_SYNTH_COMB_FRAMES - 1;
    int32_t d = (int32_t)src->comb_delay, at = c->write - d;
    float f = (float)(src->comb_delay - (double)d), y;
    float fm = f - 1, fm2 = f - 2, fp = f + 1;
    float r = -f * fm * fm2 * (1.0f / 6) * c->line[(at + 1) & mask] +
        fp * fm * fm2 * 0.5f * c->line[at & mask] -
        fp * f * fm2 * 0.5f * c->line[(at - 1) & mask] +
        fp * f * fm * (1.0f / 6) * c->line[(at - 2) & mask];
    c->damp = box_audio_clean(c->damp + (float)src->comb_a * (r - c->damp));
    y = box_audio_clean(x + src->comb_feedback * c->damp);
    c->line[c->write] = y;
    c->write = (c->write + 1) & mask;

    return y * src->comb_gain;
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
            if (src->tables) {
                box_audio_synth_tables_next(src, levels);
            }

            box_audio_synth_mix_wide(src, levels, true, &l, &r);
        } else {
            if (src->tables) {
                box_audio_synth_tables_next(src, levels);
            }

            box_audio_synth_mix_wide(src, levels, false, &l, &r);
        }

        if (src->has_drive) {
            l = box_audio_synth_drive_next(src, 0, l);
            if (stereo) {
                r = box_audio_synth_drive_next(src, 1, r);
            }
        }

        if (src->filter == BOX_AUDIO_SYNTH_FORMANT) {
            box_audio_synth_formant_control(src, rate);
            l = box_audio_formant_next(&src->formant, src->formant.svf, l, src->drive);
            if (stereo) {
                r = box_audio_formant_next(&src->formant, src->formant.svf_r, r, src->drive);
            }
        } else if (src->filter == BOX_AUDIO_SYNTH_COMB) {
            box_audio_synth_control(src, rate);
            box_audio_synth_comb_control(src, rate);
            l = box_audio_synth_comb_next(src, src->comb[0], l);
            if (stereo) {
                r = box_audio_synth_comb_next(src, src->comb[1], r);
            }
        } else if (src->filter && src->model == BOX_AUDIO_SYNTH_LADDER) {
            box_audio_synth_control(src, rate);
            l = box_audio_ladder_next(&src->ladder[0], &src->ladder_coefs, l);
            if (stereo) {
                r = box_audio_ladder_next(&src->ladder[1], &src->ladder_coefs, r);
            }
        } else if (src->filter) {
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

        if (src->tables) {
            box_audio_synth_tables_next(src, levels);
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
                    box_audio_synth_osc_next(o, &src->part[l]->color, &src->rng);
                mix += x * o->level * levels[o->env];
            }
        } else {
            for (l = 0; l < layers; l ++) {
                box_audio_synth_osc_t *o = &src->part[l]->osc;
                mix += box_audio_synth_osc_next(o, &src->part[l]->color, &src->rng) *
                    o->level * levels[o->env];
            }
        }

        if (src->has_drive) {
            mix = box_audio_synth_drive_next(src, 0, mix);
        }

        if (src->filter == BOX_AUDIO_SYNTH_FORMANT) {
            box_audio_synth_formant_control(src, rate);
            y = box_audio_formant_next(&src->formant, src->formant.svf, mix, src->drive);
        } else if (src->filter == BOX_AUDIO_SYNTH_COMB) {
            box_audio_synth_control(src, rate);
            box_audio_synth_comb_control(src, rate);
            y = box_audio_synth_comb_next(src, src->comb[0], mix);
        } else if (src->filter) {
            if (src->amount != 0 && src->fenv.segment < BOX_AUDIO_SYNTH_DONE) {
                float level = box_audio_synth_env_next(&src->fenv);
                if (!src->lfo_cutoff && (-- src->control <= 0 ||
                    src->fenv.segment >= BOX_AUDIO_SYNTH_DONE))
                {
                    src->control = BOX_AUDIO_SYNTH_CONTROL;
                    box_audio_synth_cutoff(src, level, rate);
                }
            }

            if (src->model == BOX_AUDIO_SYNTH_LADDER) {
                y = box_audio_ladder_next(&src->ladder[0], &src->ladder_coefs, mix);
            } else {
                y = mix;
                if (src->stages > 1) {
                    y = box_audio_svf_drive(&src->svf[1], src->filter, y, src->drive);
                }

                y = box_audio_svf_drive(&src->svf[0], src->filter, y, src->drive);
            }
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
