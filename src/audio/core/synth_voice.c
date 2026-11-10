/**
 * @file audio/core/synth_voice.c
 * @brief The voices of the synth source: the start of a play, its passes, its strings and
 * bows, and the pitch, expression, legato, gate and release of a voice that plays.
 */

#include "synth_internal.h"

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
    u->spread = (double)layer->detune;
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
void box_audio_synth_string_move(
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
double box_audio_synth_comb_aim(
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

/** Tune a bowed string to a frequency and a pressure of its bow, and the gain of its loop, which
 * makes the fundamental fall 60 dB over BOX_AUDIO_SYNTH_BOW_DECAY periods without the lowpass once
 * the bow lifted, at most BOX_AUDIO_SYNTH_BOW_DECAY_MAX seconds.
 *
 * A period of n frames is its two delay lines together, its loop, and the phase delay of the one
 * pole lowpass at the bridge at the fundamental w, atan(b sin w / (1 - b cos w)) / w frames for
 * its pole b, less the advance of the corner by the friction.
 *
 * The advance is BOX_AUDIO_SYNTH_BOW_ADVANCE of the time constant of the lowpass, so the settled
 * pitch is exact, and the fractional delays of both sides follow the loop. The lowpass cuts at a
 * harmonic of the note, BOX_AUDIO_SYNTH_BOW_HARMONIC raised by brightness and bow pressure.
 *
 * @param s The string, bowed
 * @param inc Turns per frame of its oscillator
 * @param pressure Pressure of the bow, 0..1
 */
static void box_audio_synth_bow_tune(
    box_audio_synth_string_t *s,
    double inc,
    float pressure)
{
    double n = 1.0 / inc, b, w, sn, cs, g, cut, decay;
    n = n < s->period_max ? n : s->period_max;
    n = n > BOX_AUDIO_SYNTH_BOW_PERIOD_MIN ? n : BOX_AUDIO_SYNTH_BOW_PERIOD_MIN;
    w = 1.0 / n;
    cut = w * BOX_AUDIO_SYNTH_BOW_HARMONIC * box_audio_synth_exp2((double)s->tone +
        BOX_AUDIO_SYNTH_BOW_PRESSURE_OCTAVES * ((double)pressure - 0.5));
    cut = cut < BOX_AUDIO_SYNTH_BOW_CUTOFF_MAX ? cut : BOX_AUDIO_SYNTH_BOW_CUTOFF_MAX;
    s->a = (float)(1 - box_audio_synth_exp2(-2 * BOX_AUDIO_SYNTH_PI * cut /
        BOX_AUDIO_SYNTH_LN2));
    b = 1 - (double)s->a;
    sn = (double)box_audio_core_sin(w);
    cs = (double)box_audio_core_sin(w + 0.25);
    s->inc = inc;
    s->period = n;
    s->pressure = pressure;
    s->lag = box_audio_synth_atan(b * sn / (1 - b * cs)) / (2 * BOX_AUDIO_SYNTH_PI * w) -
        BOX_AUDIO_SYNTH_BOW_ADVANCE / (2 * BOX_AUDIO_SYNTH_PI * cut);
    s->loop = n - s->lag;
    decay = BOX_AUDIO_SYNTH_BOW_DECAY * n;
    decay = decay < s->decay_frames ? decay : s->decay_frames;
    g = box_audio_synth_exp2(-BOX_AUDIO_SYNTH_DB60 * n / decay);
    s->gain = (float)(g < BOX_AUDIO_SYNTH_STRING_GAIN_MAX ? g : BOX_AUDIO_SYNTH_STRING_GAIN_MAX);
}

/** Tune a bowed string cheaply to a frequency while its pitch moves, as
 * box_audio_synth_string_follow does a pluck.
 *
 * The period comes from the last one by two terms of the series of 1 / (1 - e), or by a division
 * when the pitch jumps; the lowpass, its phase delay and the gain are those of the last exact
 * tuning, which move little.
 *
 * @param s The string, bowed
 * @param inc Turns per frame of its oscillator
 */
static void box_audio_synth_bow_follow(
    box_audio_synth_string_t *s,
    double inc)
{
    double e = 1 - inc * s->period, n;
    n = e > -0.01 && e < 0.01 ? s->period * (1 + e * (1 + e)) : 1.0 / inc;
    n = n < s->period_max ? n : s->period_max;
    n = n > BOX_AUDIO_SYNTH_BOW_PERIOD_MIN ? n : BOX_AUDIO_SYNTH_BOW_PERIOD_MIN;
    s->inc = inc;
    s->period = n;
    s->loop = n - s->lag;
}
void box_audio_synth_bow_move(
    box_audio_synth_string_t *s,
    double inc,
    float pressure)
{
    if (-- s->retune <= 0) {
        s->retune = BOX_AUDIO_SYNTH_STRING_RETUNE;
        box_audio_synth_bow_tune(s, inc, pressure);
    } else {
        box_audio_synth_bow_follow(s, inc);
    }
}

/** Start a bowed string at rest: tune it, its lowpass at the bridge from the brightness of its
 * layer and the pressure of its bow, and clear its delay lines, so it sounds only once the bow
 * moves.
 *
 * @param s The string
 * @param layer Its layer, clamped
 * @param inc Turns per frame of its oscillator
 * @param pressure Pressure of the bow, 0..1
 * @param rate Frames per second
 */
static void box_audio_synth_bow_string(
    box_audio_synth_string_t *s,
    const box_audio_synth_layer_t *layer,
    double inc,
    float pressure,
    float rate)
{
    double top = (double)rate / BOX_AUDIO_SYNTH_PLUCK_MIN;
    int32_t i;
    s->decay_frames = BOX_AUDIO_SYNTH_BOW_DECAY_MAX * (double)rate;
    s->period_max = top < BOX_AUDIO_SYNTH_STRING_PERIOD_MAX ? top :
        BOX_AUDIO_SYNTH_STRING_PERIOD_MAX;
    s->tone = (float)BOX_AUDIO_SYNTH_BOW_OCTAVES * layer->brightness;
    box_audio_synth_bow_tune(s, inc, pressure);
    for (i = 0; i < BOX_AUDIO_SYNTH_STRING_FRAMES; i ++) {
        s->line[i] = 0;
        s->bridge[i] = 0;
    }

    s->write = 0;
    s->lp = 0;
    s->dc.x1 = 0;
    s->dc.y1 = 0;
    s->peak = 0;
    s->bowed = true;
    s->window = (int32_t)s->period + 2;
    s->retune = 0;
    s->fade = 0;
    s->ringing = true;
}

/** Start the bow of a bowed layer: its pressure, speed, position and noise from its layer, the
 * velocity of the play included, which the routes then move.
 *
 * @param part The part of the layer, bowed
 * @param rate Frames per second
 */
static void box_audio_synth_bow_start(
    box_audio_synth_part_t *part,
    float rate)
{
    const box_audio_synth_layer_t *layer = &part->layer;
    box_audio_synth_bow_t *b = &part->bow;
    b->pressure = layer->bow_pressure;
    b->speed = layer->bow_speed;
    b->position = layer->bow_position;
    b->noise = layer->bow_noise * BOX_AUDIO_SYNTH_BOW_HAIR;
    b->pressure_aim = b->pressure;
    b->speed_aim = b->speed;
    b->position_aim = b->position;
    b->pressure_play = b->pressure;
    b->speed_play = b->speed;
    b->position_play = b->position;
    b->slope = BOX_AUDIO_SYNTH_BOW_SLOPE - BOX_AUDIO_SYNTH_BOW_SLOPE_RANGE * b->pressure;
    b->smooth = (float)(1 - box_audio_synth_exp2(-1.0 / (BOX_AUDIO_SYNTH_BOW_SMOOTH *
        (double)rate * BOX_AUDIO_SYNTH_LN2)));
    b->dc = (float)(1.0 - 2.0 * BOX_AUDIO_SYNTH_PI * BOX_AUDIO_SYNTH_BOW_DC / (double)rate);
}
void box_audio_synth_pass(
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
    src->bowing = 0;
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
        o->index_route = 0;
        o->synced = o->sync_base > 1;
        for (n = 0; n < src->lfos; n ++) {
            o->synced = o->synced || (src->lfo_patch[n].layer == (uint32_t)l + 1 &&
                src->lfo_patch[n].hard_sync != 0);
        }

        o->synced = o->synced && o->wave != BOX_AUDIO_SYNTH_NOISE &&
            o->wave != BOX_AUDIO_SYNTH_PLUCK && o->wave != BOX_AUDIO_SYNTH_BOWED;
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

        lo = (double)layer->frequency * (double)src->pitch * src->bend;
        lo = lo < top ? (lo > BOX_AUDIO_SYNTH_FREQUENCY_MIN ? lo : BOX_AUDIO_SYNTH_FREQUENCY_MIN) :
            top;
        o->inc = lo / (double)rate;
        o->inc_end = o->inc;
        o->ratio = 1;
        o->sweep = 0;
        o->glide = 1;
        n = box_audio_synth_frames(layer->sweep, rate);
        if (layer->frequency_end > 0 && n > 0) {
            hi = (double)layer->frequency_end * (double)src->pitch * src->bend;
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

            if (o->wave == BOX_AUDIO_SYNTH_BOWED) {
                box_audio_synth_bow_string(s, layer, inc, layer->bow_pressure, rate);
                src->bowing ++;
            } else {
                box_audio_synth_string_pluck(s, layer, inc, rate, &src->rng);
            }

            src->ringing ++;
            src->ringing_free += o->env == BOX_AUDIO_SYNTH_ENV_FREE;
        }

        if (o->wave == BOX_AUDIO_SYNTH_BOWED) {
            box_audio_synth_bow_start(part, rate);
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

    if (src->pressure != 0 && src->filter && src->filter != BOX_AUDIO_SYNTH_FORMANT &&
        !(src->route_sources & (1u << BOX_AUDIO_SYNTH_FROM_PRESSURE)))
    {
        cut *= box_audio_synth_exp2((double)src->pressure *
            (double)BOX_AUDIO_SYNTH_PRESSURE_OCTAVES);
    }

    cut = cut < top ? cut : top;
    src->cutoff = cut;
    src->cutoff_top = top;
    src->control = BOX_AUDIO_SYNTH_CONTROL;
    if (src->fenv_on) {
        box_audio_synth_env_init(&src->fenv, &src->filter_envelope, rate, src->gate);
    }

    if (src->menv_on) {
        box_audio_synth_env_init(&src->menv, &src->mod_envelope, rate, src->gate);
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
 * A synced LFO runs at the tempo of the play over its sync and starts at its phase +
 * fract(beat / sync), the same phase of the grid for every note.
 *
 * @param core The core
 * @param src The source
 * @param p The patch, clamped
 * @param plays Count of the plays of the patch
 * @param beat Beat of the play, 0..BOX_AUDIO_BEAT_WRAP
 * @param tempo Beats per second of the play, clamped
 */
static void box_audio_synth_lfo_start(
    const box_audio_core_t *core,
    box_audio_synth_source_t *src,
    const box_audio_synth_patch_t *p,
    uint32_t plays,
    float beat,
    float tempo)
{
    double t;
    int32_t k;
    src->lfo_rng = box_audio_synth_hash(p->seed ^ 0x4C464F21u, plays);
    src->lfo_smooth = (float)(1 - box_audio_synth_exp2(-(double)BOX_AUDIO_SYNTH_CONTROL /
        (BOX_AUDIO_SYNTH_LFO_SMOOTH * (double)core->sample_rate * BOX_AUDIO_SYNTH_LN2)));
    for (k = 0; k < src->lfos; k ++) {
        const box_audio_synth_lfo_t *d = &p->lfo[k];
        src->lfo_patch[k] = *d;
        src->lfo[k].start = d->random_phase ?
            0.5 * (1.0 + (double)box_audio_synth_noise(&src->lfo_rng)) : (double)d->phase;
        if (d->sync > 0) {
            src->lfo_patch[k].rate = tempo / d->sync < BOX_AUDIO_SYNTH_LFO_RATE_MAX ?
                tempo / d->sync : BOX_AUDIO_SYNTH_LFO_RATE_MAX;
            t = src->lfo[k].start + (double)beat / (double)d->sync;
            t -= (double)(int64_t)t;
            src->lfo[k].start = t < 1 ? t : 0.0;
        }

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
        if (src->bend != 1) {
            src->key_target += box_audio_synth_log2(src->bend);
        }

        src->key_left = frames > 0 ? frames : 1;
        src->key_step = (src->key_target - src->key) / (double)src->key_left;
        src->key_control = 1;
    }
}
void box_audio_synth_key_next(
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

/** Take the velocity of a note into the layers of a play: the index of each modulator, the hardness
 * of each pluck and the pressure and speed of each bow by their share, before its first pass. A
 * layer with a share of 0 keeps its value as it is.
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

        if (layer->bow_velocity > 0) {
            layer->bow_pressure *= box_audio_synth_velocity(layer->bow_velocity, velocity);
            layer->bow_speed *= box_audio_synth_velocity(layer->bow_velocity, velocity);
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

/** Start the routes of the modulation matrix of a play: a copy of those of the patch, the sources
 * and targets they use, the mod envelope and the filter envelope when a route uses them, and the
 * pan at the center. After the filter and the LFOs.
 *
 * The random value of the play is seeded from the seed of the patch and the count of its plays
 * apart from the noise and the LFOs.
 *
 * @param src The source
 * @param p The patch, clamped
 * @param plays Count of the plays of the patch
 */
static void box_audio_synth_routes_start(
    box_audio_synth_source_t *src,
    const box_audio_synth_patch_t *p,
    uint32_t plays)
{
    uint32_t rng;
    int32_t k;
    src->routes = (int32_t)p->routes;
    src->route_sources = 0;
    src->route_targets = 0;
    for (k = 0; k < src->routes; k ++) {
        src->route[k] = p->route[k];
        src->route_sources |= 1u << p->route[k].source;
        src->route_targets |= 1u << p->route[k].target;
    }

    src->route_random = 0;
    if (src->route_sources & (1u << BOX_AUDIO_SYNTH_FROM_RANDOM)) {
        rng = box_audio_synth_hash(p->seed ^ 0x524F5554u, plays);
        src->route_random = box_audio_synth_noise(&rng);
    }

    src->menv_on = (src->route_sources & (1u << BOX_AUDIO_SYNTH_FROM_MOD_ENV)) != 0;
    src->mod_envelope = p->mod_envelope;
    box_audio_synth_key_scale(&src->mod_envelope, src->key);
    src->fenv_routed = !src->fenv_on &&
        (src->route_sources & (1u << BOX_AUDIO_SYNTH_FROM_FILTER)) != 0;
    src->fenv_on = src->fenv_on || src->fenv_routed;
    if ((src->route_targets & (1u << BOX_AUDIO_SYNTH_TO_CUTOFF)) && src->filter &&
        src->filter != BOX_AUDIO_SYNTH_FORMANT)
    {
        src->lfo_cutoff = true;
    }

    if ((src->route_targets & (1u << BOX_AUDIO_SYNTH_TO_VOWEL)) &&
        src->filter == BOX_AUDIO_SYNTH_FORMANT)
    {
        src->lfo_vowel = true;
    }

    src->panned = (src->route_targets & (1u << BOX_AUDIO_SYNTH_TO_PAN)) != 0;
    src->pan_l = 1;
    src->pan_r = 1;
    src->pan_l_step = 0;
    src->pan_r_step = 0;
}

void box_audio_source_synth(
    box_audio_core_t *core,
    box_audio_source_t *source,
    uint32_t sound,
    float pitch,
    float velocity,
    float beat,
    float tempo,
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

        if (p->layer[l].wave == BOX_AUDIO_SYNTH_PLUCK ||
            p->layer[l].wave == BOX_AUDIO_SYNTH_BOWED)
        {
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
        comb->peak = 0;
        src->comb[k] = comb;
    }

    src->comb_feedback = p->feedback;
    src->comb_gain = 1 - (p->feedback < 0 ? -p->feedback : p->feedback);
    src->comb_tune = (double)p->tune;
    src->comb_pitch = 1;
    src->comb_a = 1;
    src->comb_window = 1;
    src->comb_ringing = true;
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
    src->slope = p->slope;
    src->resonance_base = p->resonance;
    src->resonance = p->resonance;
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
        beat = beat > 0 && beat < (float)BOX_AUDIO_BEAT_WRAP ? beat : 0.0f;
        tempo = tempo > 0 ? (tempo > BOX_AUDIO_SYNTH_TEMPO_MIN ? (tempo <
            BOX_AUDIO_SYNTH_TEMPO_MAX ? tempo : BOX_AUDIO_SYNTH_TEMPO_MAX) :
            BOX_AUDIO_SYNTH_TEMPO_MIN) : BOX_AUDIO_SYNTH_TEMPO;
        box_audio_synth_lfo_start(core, src, p, slot->plays, beat, tempo);
    }

    box_audio_synth_routes_start(src, p, slot->plays);

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

    src->mods = src->lfos > 0 || src->bends || src->drifts || src->routes > 0;
    src->bend = 1;
    src->mod = 1;
    src->pressure = 0;
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

void box_audio_synth_express(
    box_audio_synth_source_t *src,
    double bend,
    float mod,
    float pressure,
    float rate)
{
    double f = bend / src->bend, d;
    bool cut = false;
    int32_t l;
    if (f != 1) {
        for (l = 0; l < src->layers; l ++) {
            box_audio_synth_osc_t *o = &src->part[l]->osc;
            o->inc = o->inc * f < src->inc_max ? o->inc * f : src->inc_max;
            o->inc_end = o->inc_end * f < src->inc_max ? o->inc_end * f : src->inc_max;
        }

        if (src->keytrack != 0) {
            d = box_audio_synth_log2(f);
            src->key += d;
            src->key_target += d;
            cut = true;
        }

        if (src->modulated) {
            box_audio_synth_limits(src);
        }

        src->bend = bend;
    }

    src->mod = (double)mod;
    if (pressure != src->pressure) {
        src->pressure = pressure;
        cut = true;
    }

    if (cut && src->filter && src->filter != BOX_AUDIO_SYNTH_FORMANT && !src->lfo_cutoff &&
        (src->amount == 0 || src->fenv.segment >= BOX_AUDIO_SYNTH_DONE))
    {
        box_audio_synth_cutoff(src, 0.0f, rate);
    }
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

    if (src->menv_on && src->menv.segment == BOX_AUDIO_SYNTH_HOLD &&
        src->menv.frames[BOX_AUDIO_SYNTH_HOLD] > 0 && !src->menv.gate)
    {
        src->menv.left = src->menv.frames[BOX_AUDIO_SYNTH_HOLD];
    }

    for (l = 0; l < src->layers; l ++) {
        box_audio_synth_osc_t *o = &src->part[l]->osc;
        if (l < (int32_t)np->layers) {
            box_audio_synth_layer_t *layer = &src->part[l]->layer;
            const box_audio_synth_layer_t *next = &np->layer[l];
            layer->frequency = next->frequency;
            layer->frequency_end = next->frequency_end;
            layer->sweep = next->sweep;
            hi = (double)next->frequency * (double)p * src->bend;
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

    if (src->menv_on) {
        box_audio_synth_env_gate(&src->menv, gate);
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

    if (src->menv_on) {
        box_audio_synth_env_release(&src->menv);
    }
}
