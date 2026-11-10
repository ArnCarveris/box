/**
 * @file audio/core/synth_render.c
 * @brief The render of the synth source: per frame its envelopes, LFOs, oscillators,
 * wavetables, strings, bows, unison voices, drive, filters and comb filter.
 */

#include "synth_internal.h"

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
    if (src->panned) {
        src->pan_l += src->pan_l_step;
        src->pan_r += src->pan_r_step;
    }
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

/** Advance the envelopes that only the routes of a source use by a frame: the mod envelope,
 * and the filter envelope when the filter does not advance it.
 *
 * @param src The source, with routes
 */
static inline void box_audio_synth_route_envs_next(
    box_audio_synth_source_t *src)
{
    if (src->menv_on) {
        box_audio_synth_env_next(&src->menv);
    }

    if (src->fenv_routed) {
        box_audio_synth_env_next(&src->fenv);
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

/** Render a frame of a wavetable at a phase: the mip level of the speed of the phase, the first
 * whose highest harmonic stays under Nyquist at it.
 *
 * Over the upper half of the octave of a level it crossfades linearly into the next level, of half
 * the harmonics, which it reaches at the top of the octave, so a sweep moves from level to level
 * without a step and never aliases.
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

/** Move the bow of a bowed layer a frame toward the pressure, speed and position it aims at.
 *
 * @param b The bow
 */
static inline void box_audio_synth_bow_next(
    box_audio_synth_bow_t *b)
{
    b->pressure_play = box_audio_clean(b->pressure_play +
        (b->pressure_aim - b->pressure_play) * b->smooth);
    b->speed_play = box_audio_clean(b->speed_play + (b->speed_aim - b->speed_play) * b->smooth);
    b->position_play += (b->position_aim - b->position_play) * b->smooth;
    b->slope = BOX_AUDIO_SYNTH_BOW_SLOPE - BOX_AUDIO_SYNTH_BOW_SLOPE_RANGE * b->pressure_play;
}

/** Read a delay line of a bowed string at a fractional delay by a 3rd order Lagrange
 * interpolation of the 4 frames around it, as the comb filter reads its line.
 *
 * @param line The delay line, a ring of BOX_AUDIO_SYNTH_STRING_FRAMES
 * @param write Index the next frame is written to
 * @param delay Frames of the delay, BOX_AUDIO_SYNTH_BOW_DELAY_MIN..MAX
 * @return The frame
 */
static inline float box_audio_synth_bow_read(
    const float *line,
    int32_t write,
    double delay)
{
    const int32_t mask = BOX_AUDIO_SYNTH_STRING_FRAMES - 1;
    int32_t d = (int32_t)delay, at = write - d;
    float f = (float)(delay - (double)d), fm = f - 1, fm2 = f - 2, fp = f + 1;

    return -f * fm * fm2 * (1.0f / 6) * line[(at + 1) & mask] +
        fp * fm * fm2 * 0.5f * line[at & mask] -
        fp * f * fm2 * 0.5f * line[(at - 1) & mask] +
        fp * f * fm * (1.0f / 6) * line[(at - 2) & mask];
}

/** Render a frame of a bowed string that rings and advance it. The waves that arrive at the bow,
 * from the bridge through its lowpass and gain and from the nut, both inverted, sum to the
 * velocity of the string there.
 *
 * The difference of the velocity of the bow to it goes through the friction curve, which sticks or
 * slips, and what the bow adds, with the noise of the hair, goes out to both sides. The slope of
 * the friction is over the velocity of the bow, so the motion scales with it.
 *
 * The bow touches the string while the level of the envelope is over 0, with a contact that rises
 * up to BOX_AUDIO_SYNTH_BOW_LIFT. A window of a period without the bow whose peak is under
 * BOX_AUDIO_SYNTH_STRING_SILENCE leaves src->ringing and src->bowing.
 *
 * @param src The source
 * @param b The bow of its layer
 * @param s The string, ringing
 * @param inc Turns of the frame
 * @param level Level of the envelope of its layer, which the speed of the bow follows
 * @return The sample: the wave at the bridge less BOX_AUDIO_SYNTH_BOW_NOTCH of it a bow position
 * earlier, which notches the harmonics at the multiples of 1 / position
 */
static inline float box_audio_synth_bow_ring(
    box_audio_synth_source_t *src,
    const box_audio_synth_bow_t *b,
    box_audio_synth_string_t *s,
    double inc,
    float level)
{
    const int32_t mask = BOX_AUDIO_SYNTH_STRING_FRAMES - 1;
    float xb, xn, xo, rb, rn, v, dv, t, r, j = 0, contact, m;
    double db, dn;
    if (inc != s->inc || b->pressure_play != s->pressure) {
        box_audio_synth_bow_move(s, inc, b->pressure_play);
    }

    db = (double)b->position_play * s->loop;
    db = db > BOX_AUDIO_SYNTH_BOW_DELAY_MIN ? db : BOX_AUDIO_SYNTH_BOW_DELAY_MIN;
    dn = s->loop - db;
    dn = dn > BOX_AUDIO_SYNTH_BOW_DELAY_MIN ? (dn < BOX_AUDIO_SYNTH_BOW_DELAY_MAX ? dn :
        BOX_AUDIO_SYNTH_BOW_DELAY_MAX) : BOX_AUDIO_SYNTH_BOW_DELAY_MIN;
    xb = box_audio_synth_bow_read(s->bridge, s->write, db);
    xn = box_audio_synth_bow_read(s->line, s->write, dn);
    xo = xb - BOX_AUDIO_SYNTH_BOW_NOTCH *
        box_audio_synth_bow_read(s->bridge, s->write, db + db);
    s->lp = box_audio_clean(s->lp + s->a * (xb - s->lp));
    rb = -s->gain * s->lp;
    rn = -xn;
    contact = level * (1.0f / BOX_AUDIO_SYNTH_BOW_LIFT);
    contact = contact < 1 ? contact : 1.0f;
    if (contact > 0) {
        v = BOX_AUDIO_SYNTH_BOW_VELOCITY * b->speed_play * level;
        dv = v - (rb + rn);
        r = BOX_AUDIO_SYNTH_BOW_SLIP;
        if (v > 0) {
            t = dv * b->slope / v;
            t = (t < 0 ? -t : t) + 0.75f;
            t *= t;
            r = 1.0f / (t * t);
            r = r < BOX_AUDIO_SYNTH_BOW_STICK ? (r > BOX_AUDIO_SYNTH_BOW_SLIP ? r :
                BOX_AUDIO_SYNTH_BOW_SLIP) : BOX_AUDIO_SYNTH_BOW_STICK;
        }

        j = contact * dv * r;
        if (b->noise > 0) {
            j += b->noise * v * box_audio_synth_noise(&src->rng);
        }

        s->bowed = true;
    }

    s->line[s->write] = box_audio_clean(rb + j);
    s->bridge[s->write] = box_audio_clean(rn + j);
    s->write = (s->write + 1) & mask;
    m = xb < 0 ? -xb : xb;
    s->peak = m > s->peak ? m : s->peak;
    if (-- s->window <= 0) {
        if (!s->bowed && s->peak < BOX_AUDIO_SYNTH_STRING_SILENCE) {
            s->ringing = false;
            src->ringing --;
            src->bowing --;
        }

        s->peak = 0;
        s->bowed = false;
        s->window = (int32_t)s->period + 2;
    }

    return box_audio_dc_next(&s->dc, xo, b->dc) * BOX_AUDIO_SYNTH_BOW_LEVEL;
}

/** Render a frame of the string of a bowed layer of one voice and advance it, its bow and its
 * oscillator, while it rings. Its sample is not times the level of its envelope, which is the
 * speed of its bow instead.
 *
 * @param src The source
 * @param part The part of the string
 * @param inc Turns of the frame: the inc of its oscillator, or that times its vibrato
 * @param level Level of the envelope of the layer
 * @return The sample, 0 once the string is silent
 */
static inline float box_audio_synth_bowed_next(
    box_audio_synth_source_t *src,
    box_audio_synth_part_t *part,
    double inc,
    float level)
{
    box_audio_synth_string_t *s = part->string;
    float x = 0;
    if (s->ringing) {
        box_audio_synth_bow_next(&part->bow);
        x = box_audio_synth_bow_ring(src, &part->bow, s, inc, level);
        box_audio_synth_osc_advance(&part->osc, inc);
    }

    return x;
}

/** Whether a source with strings plays on: while one of its envelopes plays, unless every
 * layer is a string and all of them, those of every unison voice, are silent, and while a
 * string without an envelope or a bowed string rings.
 *
 * @param src The source, with strings
 * @param envs Whether one of its envelopes plays
 * @return Whether it plays on
 */
static inline bool box_audio_synth_strings_play(
    const box_audio_synth_source_t *src,
    bool envs)
{
    bool result = envs || src->ringing_free > 0 || src->bowing > 0;
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
        float level = levels[o->env];
        if (o->wave == BOX_AUDIO_SYNTH_BOWED) {
            x = box_audio_synth_bowed_next(src, part, inc, level);
            level = 1;
        } else if (o->wave == BOX_AUDIO_SYNTH_PLUCK) {
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

        mix += x * o->level * o->tremolo * level;
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

        if (o->wave == BOX_AUDIO_SYNTH_BOWED) {
            x = box_audio_synth_bowed_next(src, src->part[l], inc, levels[o->env]);
            level = lfo ? o->tremolo : 1.0f;
        } else if (o->wave == BOX_AUDIO_SYNTH_PLUCK) {
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

/** Render a frame of the strings of the unison voices of a bowed layer and advance them, each
 * at the detune of its voice and with its gains, and its bow and its oscillator while one rings.
 *
 * @param src The source
 * @param part The part of the layer, with more than one voice
 * @param inc Turns of the frame of its oscillator: its inc, or that times its vibrato
 * @param level Level of the envelope of the layer, the speed of its bows
 * @param left Receives the left sample
 * @param right Receives the right sample
 */
static inline void box_audio_synth_bows_next(
    box_audio_synth_source_t *src,
    box_audio_synth_part_t *part,
    double inc,
    float level,
    float *left,
    float *right)
{
    box_audio_synth_unison_t *u = &part->unison;
    box_audio_synth_string_t *s;
    float l = 0, r = 0, x;
    bool rang = false;
    double dt;
    int32_t k;
    box_audio_synth_bow_next(&part->bow);
    for (s = part->string, k = 0; s; s = s->next, k ++) {
        if (s->ringing) {
            dt = inc * u->detune[k];
            if (src->drifts) {
                dt *= u->drift[k];
            }

            dt = dt < src->inc_max ? dt : src->inc_max;
            x = box_audio_synth_bow_ring(src, &part->bow, s, dt, level);
            l += x * u->left[k];
            r += x * u->right[k];
            rang = true;
        }
    }

    if (rang) {
        box_audio_synth_osc_advance(&part->osc, inc);
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
            if (o->wave == BOX_AUDIO_SYNTH_BOWED) {
                box_audio_synth_bows_next(src, part, inc, levels[o->env], &xl, &xr);
                level = lfo ? o->tremolo : 1.0f;
            } else if (part->string) {
                box_audio_synth_strings_next(src, part, inc, &xl, &xr);
            } else {
                box_audio_synth_unison_next(src, part, shift, inc, &xl, &xr, &sub);
            }

            x = 0.5f * (xl + xr);
            o->fb2 = o->fb1;
            o->fb1 = o->sub != 0 ? x - sub : x;
        } else {
            if (o->wave == BOX_AUDIO_SYNTH_BOWED) {
                x = box_audio_synth_bowed_next(src, part, inc, levels[o->env]);
                level = lfo ? o->tremolo : 1.0f;
            } else if (o->wave == BOX_AUDIO_SYNTH_PLUCK) {
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
    float f = (float)(src->comb_delay - (double)d), y, m;
    float fm = f - 1, fm2 = f - 2, fp = f + 1;
    float r = -f * fm * fm2 * (1.0f / 6) * c->line[(at + 1) & mask] +
        fp * fm * fm2 * 0.5f * c->line[at & mask] -
        fp * f * fm2 * 0.5f * c->line[(at - 1) & mask] +
        fp * f * fm * (1.0f / 6) * c->line[(at - 2) & mask];
    c->damp = box_audio_clean(c->damp + (float)src->comb_a * (r - c->damp));
    y = box_audio_clean(x + src->comb_feedback * c->damp);
    c->line[c->write] = y;
    c->write = (c->write + 1) & mask;
    y *= src->comb_gain;
    m = y < 0 ? -y : y;
    c->peak = m > c->peak ? m : c->peak;

    return y;
}

/** Whether the comb filter of a source rings on once its envelopes have ended, with no more
 * input: until the output of a period of every channel falls under
 * BOX_AUDIO_SYNTH_STRING_SILENCE, as a string does.
 *
 * @param src The source, with a comb filter
 * @return Whether it still rings
 */
static inline bool box_audio_synth_comb_rings(
    box_audio_synth_source_t *src)
{
    box_audio_synth_comb_t *r = src->comb[1];
    if (-- src->comb_window <= 0) {
        float peak = src->comb[0]->peak;
        if (r) {
            peak = r->peak > peak ? r->peak : peak;
            r->peak = 0;
        }

        src->comb_ringing = peak >= BOX_AUDIO_SYNTH_STRING_SILENCE;
        src->comb[0]->peak = 0;
        src->comb_window = (int32_t)src->comb_delay + 2;
    }

    return src->comb_ringing;
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
        bool playing = box_audio_synth_envs_next(src, levels), tail;
        if (src->strings) {
            playing = box_audio_synth_strings_play(src, playing);
        }

        tail = false;
        if (!playing && src->comb[0] && !src->loop) {
            tail = box_audio_synth_comb_rings(src);
            playing = tail;
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

        if (src->routes) {
            box_audio_synth_route_envs_next(src);
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
            if (tail) {
                l = 0;
                r = 0;
            }

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
        if (src->panned) {
            out_l[i] = box_audio_clean(l * gain * src->pan_l);
            out_r[i] = box_audio_clean((stereo ? r : l) * gain * src->pan_r);
        } else {
            out_l[i] = box_audio_clean(l * gain);
            if (stereo) {
                out_r[i] = box_audio_clean(r * gain);
            }
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
        bool playing = box_audio_synth_envs_next(src, levels), tail;
        if (src->strings) {
            playing = box_audio_synth_strings_play(src, playing);
        }

        tail = false;
        if (!playing && src->comb[0] && !src->loop) {
            tail = box_audio_synth_comb_rings(src);
            playing = tail;
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

        if (src->routes) {
            box_audio_synth_route_envs_next(src);
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
                float x, level = levels[o->env];
                if (o->wave == BOX_AUDIO_SYNTH_BOWED) {
                    x = box_audio_synth_bowed_next(src, src->part[l], o->inc, level);
                    level = 1;
                } else {
                    x = o->wave == BOX_AUDIO_SYNTH_PLUCK ?
                        box_audio_synth_string_next(src, src->part[l], o->inc) :
                        box_audio_synth_osc_next(o, &src->part[l]->color, &src->rng);
                }

                mix += x * o->level * level;
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
            y = box_audio_synth_comb_next(src, src->comb[0], tail ? 0.0f : mix);
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

        if (src->panned) {
            core->src_r[i] = box_audio_clean(y * src->pan_r);
            y = y * src->pan_l;
        }

        out[i] = box_audio_clean(y);
    }

done:
    return i;
}
