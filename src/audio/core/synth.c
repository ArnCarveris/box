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
 * tune; its delay lines come from the core, one per channel. Once the envelopes end it rings on
 * without input until its output is silent.
 *
 * Every voice holds its own copy of the patch, so a play allocates nothing. Its layers are parts
 * of the core, one per layer (box_audio_synth_part_t), and each pluck layer takes a string of
 * the core with its delay line (box_audio_synth_string_t) for each of its unison voices.
 *
 * Like the rest of the core it uses no libm: the sine is box_audio_core_sin and the exponential
 * sweeps and the filter envelope use the series of box_audio_synth_log2 and
 * box_audio_synth_exp2.
 */

#include "synth_internal.h"

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
double box_audio_synth_ratio(
    double from,
    double to,
    int32_t frames)
{
    return box_audio_synth_exp2(box_audio_synth_log2(to / from) / (double)frames);
}
int32_t box_audio_synth_frames(
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
void box_audio_synth_env_init(
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
void box_audio_synth_env_enter(
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
float box_audio_synth_env_step(
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
void box_audio_synth_coefs(
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
void box_audio_synth_limits(
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
            index = (double)layer->index;
            if (o->index_route != 0) {
                index += (double)o->index_route;
                index = index > 0 ? (index < 1 ? index : 1.0) : 0.0;
            }

            o->index = (float)index;
        } else {
            const box_audio_synth_osc_t *c = &src->part[o->target]->osc;
            fc = c->inc > c->inc_end ? c->inc : c->inc_end;
            fm = o->inc > o->inc_end ? o->inc : o->inc_end;
            limit = (src->inc_max - fc) / fm - 1;
            limit = limit > 0 ? limit : 0;
            index = (double)layer->index;
            if (o->index_route != 0) {
                index += (double)o->index_route;
                index = index > 0 ? (index < BOX_AUDIO_SYNTH_INDEX_MAX ? index :
                    (double)BOX_AUDIO_SYNTH_INDEX_MAX) : 0.0;
            }

            index = index < limit ? index : limit;
            o->index = (float)(index * BOX_AUDIO_SYNTH_TURNS);
        }
    }
}
void box_audio_synth_cutoff(
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

    if (src->pressure != 0 && !(src->route_sources & (1u << BOX_AUDIO_SYNTH_FROM_PRESSURE))) {
        octaves += (double)src->pressure * (double)BOX_AUDIO_SYNTH_PRESSURE_OCTAVES;
    }

    cut = src->cutoff_base * box_audio_synth_exp2(octaves);
    cut = cut < src->cutoff_top ? cut : src->cutoff_top;
    src->cutoff = cut > BOX_AUDIO_SYNTH_CUTOFF_MIN ? cut : BOX_AUDIO_SYNTH_CUTOFF_MIN;
    box_audio_synth_coefs(src, rate);
}
void box_audio_synth_vowel(
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
uint32_t box_audio_synth_hash(
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
void box_audio_synth_color_init(
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

/** Equal power gains of a pan, as those of a voice in the core: 1 for both channels at the
 * center and sqrt 2 at a side.
 *
 * @param pan Pan, -1..1
 * @param left Receives the gain of the left channel
 * @param right Receives the gain of the right channel
 */
static void box_audio_synth_pan(
    double pan,
    float *left,
    float *right)
{
    double turns;
    if (pan == 0) {
        *left = 1;
        *right = 1;
    } else if (pan <= -1) {
        *left = (float)BOX_AUDIO_SYNTH_SQRT2;
        *right = 0;
    } else if (pan >= 1) {
        *left = 0;
        *right = (float)BOX_AUDIO_SYNTH_SQRT2;
    } else {
        turns = (pan + 1) * 0.125;
        *left = (float)(BOX_AUDIO_SYNTH_SQRT2 * box_audio_core_sin(turns + 0.25));
        *right = (float)(BOX_AUDIO_SYNTH_SQRT2 * box_audio_core_sin(turns));
    }
}

/** Value of the source of a route, at the control block that runs.
 *
 * @param src The source
 * @param r The route
 * @return The value: 0..1 for the envelopes, the velocity, mod and pressure, -1..1 for the
 * LFOs and the random value, octaves for the key
 */
static float box_audio_synth_route_value(
    const box_audio_synth_source_t *src,
    const box_audio_synth_route_t *r)
{
    float result = 0;
    int32_t e, k;
    switch (r->source) {
    case BOX_AUDIO_SYNTH_FROM_AMP:
        e = 0;
        if (r->layer > 0) {
            e = src->part[r->layer - 1]->osc.env;
        } else if (!(src->envs & 1u) && src->layers > 0) {
            e = src->part[0]->osc.env;
        }

        if (e == BOX_AUDIO_SYNTH_ENV_FREE) {
            result = 1;
        } else if (e > 0) {
            result = src->part[e - 1]->env.value;
        } else if (src->envs & 1u) {
            result = src->env.value;
        }

        break;
    case BOX_AUDIO_SYNTH_FROM_FILTER:
        result = src->fenv_on ? src->fenv.value : 0.0f;
        break;
    case BOX_AUDIO_SYNTH_FROM_MOD_ENV:
        result = src->menv_on ? src->menv.value : 0.0f;
        break;
    case BOX_AUDIO_SYNTH_FROM_LFO1:
    case BOX_AUDIO_SYNTH_FROM_LFO2:
        k = (int32_t)r->source - BOX_AUDIO_SYNTH_FROM_LFO1;
        result = k < src->lfos ? src->lfo[k].value * src->lfo[k].gain : 0.0f;
        break;
    case BOX_AUDIO_SYNTH_FROM_VELOCITY:
        result = src->velocity;
        break;
    case BOX_AUDIO_SYNTH_FROM_KEY:
        result = (float)src->key;
        break;
    case BOX_AUDIO_SYNTH_FROM_RANDOM:
        result = src->route_random;
        break;
    case BOX_AUDIO_SYNTH_FROM_MOD:
        result = (float)src->mod;
        break;
    default:
        result = src->pressure;
        break;
    }

    return result;
}

/** Set the filter of a source to the resonance it holds, without touching its state: the
 * damping of the SVF, the feedback of the ladder or the widths of the formants. A comb takes
 * no resonance.
 *
 * @param src The source, with a filter
 * @param rate Frames per second
 */
static void box_audio_synth_resonance(
    box_audio_synth_source_t *src,
    float rate)
{
    if (src->filter == BOX_AUDIO_SYNTH_FORMANT) {
        src->formant.narrow = (float)(1.0 - (1.0 - BOX_AUDIO_FORMANT_NARROW) *
            (double)src->resonance);
        src->formant.vowel = -1;
        if (!src->lfo_vowel) {
            box_audio_synth_vowel(src, src->vowel_amount != 0 ? src->fenv.value : 0.0f, rate);
        }
    } else if (src->filter == BOX_AUDIO_SYNTH_COMB) {
        goto done;
    } else if (src->model == BOX_AUDIO_SYNTH_LADDER) {
        box_audio_ladder_resonance(&src->ladder_coefs, src->filter, src->resonance);
    } else {
        box_audio_svf_damping(src->svf, src->slope, src->resonance);
        src->svf_r[0].k = src->svf[0].k;
        src->svf_r[1].k = src->svf[1].k;
        if (!src->lfo_cutoff) {
            box_audio_synth_coefs(src, rate);
        }
    }

done:
    return;
}

/** Aim the bows of the bowed layers of a source at the pressure, speed and position of their
 * layers plus what the routes add to them, clamped, which the bows then move to smoothly.
 *
 * @param src The source, with routes to the bow
 * @param bow What the routes add to the pressure, the speed and the position: index 0 of each
 *  every layer, 1 + layer a layer
 * @param now Whether the bows take their aims at once, at the start of a pass
 */
static void box_audio_synth_bow_routes(
    box_audio_synth_source_t *src,
    double bow[3][1 + BOX_AUDIO_SYNTH_LAYERS],
    bool now)
{
    double x;
    int32_t l;
    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_part_t *part = src->part[l];
        box_audio_synth_bow_t *b = &part->bow;
        if (part->osc.wave != BOX_AUDIO_SYNTH_BOWED) {
            continue;
        }

        x = (double)b->pressure + bow[0][0] + bow[0][l + 1];
        b->pressure_aim = (float)(x > 0 ? (x < 1 ? x : 1.0) : 0.0);
        x = (double)b->speed + bow[1][0] + bow[1][l + 1];
        b->speed_aim = (float)(x > 0 ? (x < 1 ? x : 1.0) : 0.0);
        x = (double)b->position + bow[2][0] + bow[2][l + 1];
        x = x > BOX_AUDIO_SYNTH_BOW_POSITION_MIN ? (x < BOX_AUDIO_SYNTH_BOW_POSITION_MAX ? x :
            (double)BOX_AUDIO_SYNTH_BOW_POSITION_MAX) : (double)BOX_AUDIO_SYNTH_BOW_POSITION_MIN;
        b->position_aim = (float)x;
        if (now) {
            b->pressure_play = b->pressure_aim;
            b->speed_play = b->speed_aim;
            b->position_play = b->position_aim;
            b->slope = BOX_AUDIO_SYNTH_BOW_SLOPE - BOX_AUDIO_SYNTH_BOW_SLOPE_RANGE *
                b->pressure_play;
        }
    }
}

/** Run the routes of the modulation matrix of a source for a control block: each adds the value of
 * its source times its amount to its target, on top of the fixed routes.
 *
 * The pitch, the pulse width, the level, the ratio of hard sync and the position of a wavetable
 * add to those of the LFOs, which aim them; the cutoff and the vowel add to the octaves and the
 * vowels of the LFOs; the index, the detune, the resonance and the pan take their values here.
 *
 * A route of a layer moves that layer, one of the patch every layer: index 0 of the arrays is
 * every layer, 1 + layer a layer.
 *
 * @param src The source, with routes
 * @param rate Frames per second
 * @param now Whether the pan takes its value at once, at the start of a pass, else over the
 * control block
 * @param cents Cents of the pitch of each layer, which it adds to
 * @param width Pulse width of each layer, which it adds to
 * @param dip Factor of the level of each layer, which it multiplies
 * @param semitones Semitones of the ratio of hard sync of each layer, which it adds to
 * @param frames Frames of the position of each wavetable layer, which it adds to
 * @param octaves Octaves of the cutoff, which it adds to
 * @param vowels Vowels of the formant filter, which it adds to
 */
static void box_audio_synth_routes(
    box_audio_synth_source_t *src,
    float rate,
    bool now,
    double *cents,
    double *width,
    float *dip,
    double *semitones,
    double *frames,
    double *octaves,
    double *vowels)
{
    double index[1 + BOX_AUDIO_SYNTH_LAYERS] = {0}, detune[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    double level[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    double bow[3][1 + BOX_AUDIO_SYNTH_LAYERS] = {{0}};
    double pan = 0, resonance = 0, x, at;
    float f, gl, gr;
    bool limits = false;
    int32_t k, l;
    for (k = 0; k < src->routes; k ++) {
        const box_audio_synth_route_t *r = &src->route[k];
        x = (double)r->amount * (double)box_audio_synth_route_value(src, r);
        switch (r->target) {
        case BOX_AUDIO_SYNTH_TO_CUTOFF:
            *octaves += x;
            break;
        case BOX_AUDIO_SYNTH_TO_RESONANCE:
            resonance += x;
            break;
        case BOX_AUDIO_SYNTH_TO_INDEX:
            index[r->layer] += x;
            break;
        case BOX_AUDIO_SYNTH_TO_PITCH:
            cents[r->layer] += 100 * x;
            break;
        case BOX_AUDIO_SYNTH_TO_PAN:
            pan += x;
            break;
        case BOX_AUDIO_SYNTH_TO_DETUNE:
            detune[r->layer] += x;
            break;
        case BOX_AUDIO_SYNTH_TO_PULSE_WIDTH:
            width[r->layer] += x;
            break;
        case BOX_AUDIO_SYNTH_TO_SYNC:
            semitones[r->layer] += x;
            break;
        case BOX_AUDIO_SYNTH_TO_POSITION:
            frames[r->layer] += x;
            break;
        case BOX_AUDIO_SYNTH_TO_VOWEL:
            *vowels += x;
            break;
        case BOX_AUDIO_SYNTH_TO_BOW_PRESSURE:
        case BOX_AUDIO_SYNTH_TO_BOW_SPEED:
        case BOX_AUDIO_SYNTH_TO_BOW_POSITION:
            bow[r->target - BOX_AUDIO_SYNTH_TO_BOW_PRESSURE][r->layer] += x;
            break;
        default:
            level[r->layer] += x;
            break;
        }
    }

    if (src->route_targets & BOX_AUDIO_SYNTH_BOW_TARGETS) {
        box_audio_synth_bow_routes(src, bow, now);
    }

    if (src->route_targets & (1u << BOX_AUDIO_SYNTH_TO_LEVEL)) {
        dip[0] *= (float)(1 + level[0] > 0 ? 1 + level[0] : 0.0);
        for (l = 0; l < src->layers; l ++) {
            dip[l + 1] *= (float)(1 + level[l + 1] > 0 ? 1 + level[l + 1] : 0.0);
        }
    }

    if (src->route_targets & (1u << BOX_AUDIO_SYNTH_TO_INDEX)) {
        for (l = 0; l < src->layers; l ++) {
            box_audio_synth_osc_t *o = &src->part[l]->osc;
            f = (float)(index[0] + index[l + 1]);
            if (o->target >= 0 && f != o->index_route) {
                o->index_route = f;
                limits = true;
            }
        }

        if (limits) {
            box_audio_synth_limits(src);
        }
    }

    if (src->route_targets & (1u << BOX_AUDIO_SYNTH_TO_DETUNE)) {
        for (l = 0; l < src->layers; l ++) {
            box_audio_synth_part_t *part = src->part[l];
            box_audio_synth_unison_t *u = &part->unison;
            if (u->voices <= 1) {
                continue;
            }

            x = (double)part->layer.detune + detune[0] + detune[l + 1];
            x = x > 0 ? (x < BOX_AUDIO_SYNTH_DETUNE_MAX ? x : BOX_AUDIO_SYNTH_DETUNE_MAX) : 0.0;
            if (x != u->spread) {
                u->spread = x;
                for (k = 0; k < u->voices; k ++) {
                    at = (double)k / (double)(u->voices - 1);
                    u->detune[k] = box_audio_synth_exp2(x * (at - 0.5) / 1200.0);
                }
            }
        }
    }

    if ((src->route_targets & (1u << BOX_AUDIO_SYNTH_TO_RESONANCE)) && src->filter) {
        x = (double)src->resonance_base + resonance;
        f = (float)(x > 0 ? (x < 1 ? x : 1.0) : 0.0);
        if (f != src->resonance) {
            src->resonance = f;
            box_audio_synth_resonance(src, rate);
        }
    }

    if (src->panned) {
        box_audio_synth_pan(pan, &gl, &gr);
        if (now) {
            src->pan_l = gl;
            src->pan_r = gr;
        }

        src->pan_l_step = (gl - src->pan_l) * (1.0f / BOX_AUDIO_SYNTH_CONTROL);
        src->pan_r_step = (gr - src->pan_r) * (1.0f / BOX_AUDIO_SYNTH_CONTROL);
    }
}
void box_audio_synth_lfo_aim(
    box_audio_synth_source_t *src,
    float rate,
    bool now)
{
    double cents[1 + BOX_AUDIO_SYNTH_LAYERS] = {0}, width[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    double semitones[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    double frames[1 + BOX_AUDIO_SYNTH_LAYERS] = {0};
    double octaves = 0, vowels = 0, c, w, r;
    double mod = src->route_sources & (1u << BOX_AUDIO_SYNTH_FROM_MOD) ? 1.0 : src->mod;
    float dip[1 + BOX_AUDIO_SYNTH_LAYERS], v;
    int32_t k, l;
    for (l = 0; l <= BOX_AUDIO_SYNTH_LAYERS; l ++) {
        dip[l] = 1;
    }

    for (k = 0; k < src->lfos; k ++) {
        const box_audio_synth_lfo_t *d = &src->lfo_patch[k];
        const box_audio_synth_mod_t *m = &src->lfo[k];
        v = m->value * m->gain;
        cents[d->layer] += (double)(d->pitch * v) * mod;
        width[d->layer] += (double)(d->pulse_width * v);
        semitones[d->layer] += (double)(d->hard_sync * v);
        frames[d->layer] += (double)(d->position * v);
        dip[d->layer] *= 1 - d->amplitude * m->gain * 0.5f * (1 - m->value);
        octaves += (double)(d->cutoff * v);
        vowels += (double)(d->vowel * v);
    }

    if (src->routes) {
        box_audio_synth_routes(src, rate, now, cents, width, dip, semitones, frames,
            &octaves, &vowels);
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
float box_audio_synth_lfo_wave(
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
void box_audio_synth_lfo_pass(
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
void box_audio_synth_drift_start(
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
void box_audio_synth_drift_block(
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
void box_audio_synth_bend_pass(
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
void box_audio_synth_bend_block(
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
