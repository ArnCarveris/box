/**
 * @file SynthWavetable.c
 * @brief Wavetable layers of the synth, measured on offline renders of a core with the built-in
 * tables of the main thread: the sine and the saw frames, the mix between two frames, the
 * aliasing of steady notes and of a sweep over the keyboard, the position under an LFO and an
 * envelope, unison in stereo, phase modulation, hard sync and sub, the clamping of the fields,
 * a released or deleted table that a voice plays, a layer without a table, and a script.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Sound id of the table and of the patch of the renders */
#define WT_TABLE 2
#define WT_PATCH 1

/** Frames skipped before a measure, past the attack */
#define WT_SKIP 2400

/** Frames of a steady note the aliasing is measured over, half a second: a whole number of
 * periods of every even frequency in Hz */
#define WT_ALIAS 24000

/** Energy of the aliasing a note may have over its fundamental, in dB. The mip levels and the
 * interpolation of the core keep a saw at about -68 dB in the worst octave, so the margin is
 * about 8 dB */
#define WT_ALIAS_MAX (-60.0)

/** Frames of a window of the sweep */
#define WT_WINDOW 2048

/** Frames of a window of the harmonics of a note of 200 Hz, 10 of its periods at AUDIO_RATE */
#define WT_CENTROID 2400

/** Rate at which a master of 220 Hz has a period of exactly 200 frames */
#define WT_SYNC_RATE 44000

/** Samples of the renders a test compares */
static float wt_a_l[AUDIO_FRAMES];
static float wt_a_r[AUDIO_FRAMES];
static float wt_b_l[AUDIO_FRAMES];

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void wt_define(
    box_audio_core_t *core,
    uint32_t sound,
    const box_audio_synth_patch_t *p)
{
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, sound, p));
    box_audio_core_apply(core, w.data, w.size);
}

/** Release a sound of a core with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 */
static void wt_release(
    box_audio_core_t *core,
    uint32_t sound)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_release(&w, sound));
    box_audio_core_apply(core, w.data, w.size);
}

/** Build a built-in table and give it to a core as a sound.
 *
 * @param core The core
 * @param id Id of the sound
 * @param kind A WavetableKind
 * @return The floats of the table, which the caller frees after the core
 */
static float* wt_table(
    box_audio_core_t *core,
    uint32_t id,
    int32_t kind)
{
    int32_t floats = 0;
    float *data = box_audio_wavetable_kind(kind, &floats);
    test_assert(data != NULL);
    test_int(floats, BOX_AUDIO_TABLE_FLOATS(box_audio_wavetable_frames(kind)));
    test_assert(box_audio_core_sound(core, id, data, floats, 1, 0));

    return data;
}

/** A patch of one wavetable layer at a steady level for a second.
 *
 * @param frequency Hz
 * @param position Frame of the table
 * @return The patch
 */
static box_audio_synth_patch_t wt_patch(
    float frequency,
    float position)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 7;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 1, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_WAVETABLE, .frequency = frequency, .level = 0.5f,
        .table = WT_TABLE, .position = position };

    return p;
}

/** Render a patch in a new core at a rate that has a table of a kind as WT_TABLE into audio_l
 * and audio_r.
 *
 * @param p The patch
 * @param kind A WavetableKind
 * @param rate Frames per second
 * @param frames Frames to render, at most AUDIO_FRAMES
 */
static void wt_render_at(
    const box_audio_synth_patch_t *p,
    int32_t kind,
    float rate,
    int32_t frames)
{
    box_audio_core_desc_t desc = { .sample_rate = rate };
    box_audio_core_t *core = box_audio_core_new(&desc);
    float *data;
    test_assert(core != NULL);
    data = wt_table(core, WT_TABLE, kind);
    wt_define(core, WT_PATCH, p);
    audio_play(core, 1, WT_PATCH, 0, 1, 0, 1, 0);
    audio_render(core, 0, frames);
    box_audio_core_free(core);
    ecs_os_free(data);
}

/** Render a patch for a second at AUDIO_RATE with a table of a kind.
 *
 * @param p The patch
 * @param kind A WavetableKind
 */
static void wt_render(
    const box_audio_synth_patch_t *p,
    int32_t kind)
{
    wt_render_at(p, kind, AUDIO_RATE, AUDIO_FRAMES);
}

/** Largest difference of two ranges of samples.
 *
 * @param a Samples
 * @param b Samples
 * @param from First frame
 * @param to End frame
 * @return The difference
 */
static double wt_diff(
    const float *a,
    const float *b,
    int32_t from,
    int32_t to)
{
    double result = 0, d;
    int32_t i;
    for (i = from; i < to; i ++) {
        d = fabs((double)a[i] - (double)b[i]);
        result = d > result ? d : result;
    }

    return result;
}

/** Whether every sample of a range is finite.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return Whether they are
 */
static bool wt_finite(
    const float *x,
    int32_t from,
    int32_t to)
{
    bool result = true;
    int32_t i;
    for (i = from; i < to && result; i ++) {
        result = isfinite(x[i]);
    }

    return result;
}

/** Energy of the component of a range at a frequency of an exact bin, with a Goertzel filter.
 *
 * @param x Samples
 * @param n Number of samples
 * @param frequency Hz, a whole number of periods in n samples
 * @param rate Frames per second
 * @return The sum of the squares of the component
 */
static double wt_bin_energy(
    const float *x,
    int32_t n,
    double frequency,
    double rate)
{
    double c = 2 * cos(2 * AUDIO_PI * frequency / rate), s0, s1 = 0, s2 = 0;
    int32_t i;
    for (i = 0; i < n; i ++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return 2 * (s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

/** Energy of a steady note of the saw frame of WavetableBasic outside its harmonics and DC
 * over the energy of its fundamental. A harmonic at the Nyquist frequency counts half its
 * bin, the energy it has.
 *
 * @param frequency Hz, even, so WT_ALIAS frames hold a whole number of periods
 * @return The ratio in dB
 */
static double wt_alias(
    int32_t frequency)
{
    box_audio_synth_patch_t p = wt_patch((float)frequency, 2);
    const float *x = audio_l + WT_SKIP;
    double total = 0, mean = 0, harmonics = 0, fundamental = 0, alias, e;
    int32_t i, k;
    wt_render_at(&p, WavetableBasic, AUDIO_RATE, WT_SKIP + WT_ALIAS);
    for (i = 0; i < WT_ALIAS; i ++) {
        total += (double)x[i] * x[i];
        mean += x[i];
    }

    mean /= WT_ALIAS;
    for (k = 1; k * frequency <= AUDIO_RATE / 2; k ++) {
        e = wt_bin_energy(x, WT_ALIAS, (double)(k * frequency), AUDIO_RATE);
        e = k * frequency * 2 == AUDIO_RATE ? e / 2 : e;
        harmonics += e;
        fundamental = k == 1 ? e : fundamental;
    }

    alias = total - harmonics - mean * mean * WT_ALIAS;
    alias = alias > 1e-30 ? alias : 1e-30;

    return 10 * log10(alias / fundamental);
}

/** Highest harmonic of 200 Hz in a window of a render that is over -40 dB of the
 * fundamental, up to the 60th.
 *
 * @param x Samples of the window, WT_CENTROID of them
 * @return The harmonic, 1 for a sine
 */
static int32_t wt_harmonics(
    const float *x)
{
    double e1 = wt_bin_energy(x, WT_CENTROID, 200.0, AUDIO_RATE);
    int32_t k, result = 1;
    for (k = 2; k <= 60; k ++) {
        if (wt_bin_energy(x, WT_CENTROID, k * 200.0, AUDIO_RATE) > 1e-4 * e1) {
            result = k;
        }
    }

    return result;
}

/** Position 0 of WavetableBasic is the sine of a SynthSine layer of the same note. */
void SynthWavetable_sine_frame(void) {
    // audio/synth
    box_audio_synth_patch_t p = wt_patch(440, 0);
    double diff;
    wt_render(&p, WavetableBasic);
    memcpy(wt_a_l, audio_l, sizeof(wt_a_l));
    p.layer[0].wave = BOX_AUDIO_SYNTH_SINE;
    p.layer[0].table = 0;
    wt_render(&p, WavetableBasic);
    diff = wt_diff(wt_a_l, audio_l, 0, AUDIO_FRAMES);
    printf("position 0 of WavetableBasic differs from a sine by %g\n", diff);
    test_assert(audio_peak(wt_a_l, 0, AUDIO_FRAMES) > 0.45f);
    test_assert(diff < 2e-3);
}

/** Position 2 of WavetableBasic has the harmonics of a saw, each at 1/n of the fundamental,
 * up to the harmonics of its mip level. */
void SynthWavetable_saw_harmonics(void) {
    // audio/synth
    box_audio_synth_patch_t p = wt_patch(440, 2);
    const float *x = audio_l + WT_SKIP;
    int32_t n = 43200, k;
    double e1, ek, ratio;
    wt_render(&p, WavetableBasic);
    e1 = wt_bin_energy(x, n, 440, AUDIO_RATE);
    test_assert(e1 > 0.01);
    for (k = 2; k <= 30; k ++) {
        ek = wt_bin_energy(x, n, 440.0 * k, AUDIO_RATE);
        ratio = sqrt(ek / e1) * k;
        if (ratio < 0.97 || ratio > 1.03) {
            printf("harmonic %d of the saw frame at %g of 1/n\n", k, ratio);
        }

        test_assert(ratio > 0.97 && ratio < 1.03);
    }
}

/** Position 2.5 renders the mix of frames 2 and 3, half of each. */
void SynthWavetable_position_mix(void) {
    // audio/synth
    box_audio_synth_patch_t p = wt_patch(330, 2);
    double diff = 0, d;
    int32_t i;
    wt_render(&p, WavetableBasic);
    memcpy(wt_a_l, audio_l, sizeof(wt_a_l));
    p.layer[0].position = 3;
    wt_render(&p, WavetableBasic);
    memcpy(wt_b_l, audio_l, sizeof(wt_b_l));
    p.layer[0].position = 2.5f;
    wt_render(&p, WavetableBasic);
    for (i = 0; i < AUDIO_FRAMES; i ++) {
        d = fabs(0.5 * ((double)wt_a_l[i] + (double)wt_b_l[i]) - (double)audio_l[i]);
        diff = d > diff ? d : diff;
    }

    printf("position 2.5 differs from the mix of 2 and 3 by %g\n", diff);
    test_assert(diff < 1e-5);
    test_assert(wt_diff(wt_a_l, audio_l, 0, AUDIO_FRAMES) > 0.1);
    test_assert(wt_diff(wt_b_l, audio_l, 0, AUDIO_FRAMES) > 0.1);
}

/** Steady notes of the saw frame from 50 Hz to 15 kHz at 48 kHz, and on either side of the
 * boundary of each mip level, keep the energy that folds back from over Nyquist under
 * WT_ALIAS_MAX of the fundamental. */
void SynthWavetable_alias_steady(void) {
    // audio/synth
    double worst = -1000, a, f;
    int32_t worst_f = 0, i, k, frequency;
    for (i = 0; i < 24 + 16; i ++) {
        if (i < 24) {
            f = 50.0 * pow(15000.0 / 50.0, (double)i / 23.0);
        } else {
            k = (i - 24) / 2 + 1;
            f = 46.875 * (double)(1 << k) * ((i - 24) % 2 ? 1.01 : 0.99);
        }

        frequency = 2 * (int32_t)(f * 0.5 + 0.5);
        a = wt_alias(frequency);
        if (a > worst) {
            worst = a;
            worst_f = frequency;
        }

        test_assert(a < WT_ALIAS_MAX);
    }

    printf("aliasing of the saw frame at most %.1f dB, at %d Hz\n", worst, worst_f);
}

/** A sweep of the saw frame from 50 Hz to 15 kHz over a second at 48 kHz: in windows of
 * WT_WINDOW frames, the energy under half the fundamental, where only what folds back from
 * over Nyquist falls, stays under WT_ALIAS_MAX of the fundamental. */
void SynthWavetable_alias_sweep(void) {
    // audio/synth
    static float window[WT_WINDOW];
    box_audio_synth_patch_t p = wt_patch(50, 2);
    double worst = -1000, lo, hi, fund, alias, a, wk, bin = (double)AUDIO_RATE / WT_WINDOW;
    int32_t at, i, k, windows = 0;
    p.layer[0].frequency_end = 15000;
    p.layer[0].sweep = 1;
    wt_render(&p, WavetableBasic);
    test_assert(wt_finite(audio_l, 0, AUDIO_FRAMES));
    for (at = WT_WINDOW; at + WT_WINDOW <= AUDIO_FRAMES; at += WT_WINDOW) {
        lo = 50.0 * pow(300.0, (double)at / AUDIO_RATE);
        hi = 50.0 * pow(300.0, (double)(at + WT_WINDOW) / AUDIO_RATE);
        if (0.45 * lo < 8 * bin) {
            continue;
        }

        for (i = 0; i < WT_WINDOW; i ++) {
            double t = 2 * AUDIO_PI * (double)i / WT_WINDOW;
            wk = 0.35875 - 0.48829 * cos(t) + 0.14128 * cos(2 * t) - 0.01168 * cos(3 * t);
            window[i] = (float)(audio_l[at + i] * wk);
        }

        fund = 0;
        alias = 0;
        for (k = 4; k * bin < 1.1 * hi && k < WT_WINDOW / 2; k ++) {
            double e = wt_bin_energy(window, WT_WINDOW, k * bin, AUDIO_RATE);
            if (k * bin < 0.45 * lo) {
                alias += e;
            } else if (k * bin > 0.9 * lo) {
                fund += e;
            }
        }

        a = 10 * log10((alias > 1e-30 ? alias : 1e-30) / fund);
        worst = a > worst ? a : worst;
        windows ++;
        test_assert(a < WT_ALIAS_MAX);
    }

    printf("aliasing of the sweep of the saw frame at most %.1f dB over %d windows\n", worst,
        windows);
    test_assert(windows > 10);
}

/** An LFO on the position of WavetableHarmonics moves the spectrum of a note over time,
 * from a sine to all 32 harmonics, and it stands without it. */
void SynthWavetable_lfo_position(void) {
    // audio/synth
    box_audio_synth_patch_t p = wt_patch(200, 0);
    int32_t lo = 1000, hi = -1, h, w;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 1,
        .position = 31 };
    wt_render(&p, WavetableHarmonics);
    test_assert(wt_finite(audio_l, 0, AUDIO_FRAMES));
    for (w = 0; w < 18; w ++) {
        h = wt_harmonics(audio_l + WT_SKIP + w * WT_CENTROID);
        lo = h < lo ? h : lo;
        hi = h > hi ? h : hi;
    }

    printf("harmonics under an LFO of the position from %d to %d\n", lo, hi);
    test_assert(lo <= 2);
    test_assert(hi >= 25);

    p.lfo[0].position = 0;
    wt_render(&p, WavetableHarmonics);
    for (w = 0; w < 18; w ++) {
        test_int(wt_harmonics(audio_l + WT_SKIP + w * WT_CENTROID), 1);
    }
}

/** The envelope on the position of WavetableHarmonics opens the spectrum at the attack and
 * closes it as the level decays, on the envelope of the root and on the own one of a layer. */
void SynthWavetable_envelope_position(void) {
    // audio/synth
    box_audio_synth_patch_t p = wt_patch(200, 0);
    int32_t early, late, own;
    for (own = 0; own < 2; own ++) {
        box_audio_synth_envelope_t env = {
            .attack = 0.001f, .decay = 0.8f, .sustain = 0, .release = 0.01f };
        if (own) {
            p.layer[0].has_envelope = 1;
            p.layer[0].envelope = env;
        } else {
            p.envelope = env;
        }

        p.layer[0].position_envelope = 31;
        wt_render(&p, WavetableHarmonics);
        early = wt_harmonics(audio_l + WT_SKIP);
        late = wt_harmonics(audio_l + 30000);
        printf("harmonics under the envelope of the position: %d early, %d late\n", early,
            late);
        test_assert(early >= 20);
        test_assert(late <= 12);
        test_assert(late >= 2);
    }
}

/** Unison voices with width render a wavetable layer in stereo, the copies detuned and
 * spread, under an LFO and the envelope of the position too. */
void SynthWavetable_unison_stereo(void) {
    // audio/synth
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = wt_patch(220, 2);
    float *data = wt_table(core, WT_TABLE, WavetableBasic);
    double cutoff;
    bool unison = false;
    p.layer[0].voices = 5;
    p.layer[0].detune = 25;
    p.layer[0].width = 1;
    p.layer[0].position_envelope = 0.5f;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 3,
        .position = 0.5f };
    wt_define(core, WT_PATCH, &p);
    audio_play(core, 1, WT_PATCH, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(audio_synth_cutoff(core, 1, &cutoff, &unison));
    test_assert(unison);
    test_assert(wt_finite(audio_l, 0, AUDIO_FRAMES / 2));
    test_assert(wt_finite(audio_r, 0, AUDIO_FRAMES / 2));
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES / 2) > 0.1f);
    test_assert(audio_peak(audio_r, 0, AUDIO_FRAMES / 2) > 0.1f);
    test_assert(wt_diff(audio_l, audio_r, WT_SKIP, AUDIO_FRAMES / 2) > 0.05);
    box_audio_core_free(core);
    ecs_os_free(data);
}

/** A wavetable layer is a carrier of phase modulation and a modulator of another layer, hard
 * synced it repeats every period of its master, and it takes a sub. */
void SynthWavetable_fm_sync_sub(void) {
    // audio/synth
    box_audio_synth_patch_t p = wt_patch(220, 2), q;
    double d, lag = 0;
    int32_t i;
    wt_render(&p, WavetableBasic);
    memcpy(wt_a_l, audio_l, sizeof(wt_a_l));

    q = p;
    q.layers = 2;
    q.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 440,
        .level = 1, .modulates = 1, .modulation = BOX_AUDIO_SYNTH_PHASE, .index = 2 };
    wt_render(&q, WavetableBasic);
    test_assert(wt_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(wt_diff(wt_a_l, audio_l, WT_SKIP, AUDIO_FRAMES) > 0.05);

    q = p;
    q.layers = 2;
    q.layer[0].wave = BOX_AUDIO_SYNTH_SINE;
    q.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_WAVETABLE,
        .frequency = 330, .level = 1, .modulates = 1, .modulation = BOX_AUDIO_SYNTH_PHASE,
        .index = 1.5f, .table = WT_TABLE, .position = 3 };
    wt_render(&q, WavetableBasic);
    memcpy(wt_b_l, audio_l, sizeof(wt_b_l));
    q.layer[1].wave = BOX_AUDIO_SYNTH_SINE;
    wt_render(&q, WavetableBasic);
    test_assert(wt_finite(wt_b_l, 0, AUDIO_FRAMES));
    test_assert(wt_diff(wt_b_l, audio_l, WT_SKIP, AUDIO_FRAMES) > 0.05);

    q = p;
    q.layer[0].sync = 2.5f;
    wt_render_at(&q, WavetableBasic, WT_SYNC_RATE, WT_SYNC_RATE / 2);
    for (i = WT_SKIP; i < WT_SYNC_RATE / 2 - 200; i ++) {
        d = fabs((double)audio_l[i] - (double)audio_l[i + 200]);
        lag = d > lag ? d : lag;
    }

    printf("wavetable synced 2.5 at 220 Hz: %g over a master period\n", lag);
    test_assert(lag < 1e-3);
    test_assert(audio_peak(audio_l, 0, WT_SYNC_RATE / 2) > 0.1f);

    q = p;
    q.layer[0].sub = 0.5f;
    wt_render(&q, WavetableBasic);
    test_assert(wt_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(wt_diff(wt_a_l, audio_l, WT_SKIP, AUDIO_FRAMES) > 0.05);
}

/** A wavetable layer without a table, or whose sound is not a table, renders bit identical
 * to a sine. */
void SynthWavetable_no_table_sine(void) {
    // audio/synth
    box_audio_core_t *core;
    box_audio_synth_patch_t p = wt_patch(440, 2);
    p.layer[0].wave = BOX_AUDIO_SYNTH_SINE;
    p.layer[0].table = 0;
    p.layer[0].position = 0;
    wt_render(&p, WavetableBasic);
    memcpy(wt_a_l, audio_l, sizeof(wt_a_l));

    p.layer[0].wave = BOX_AUDIO_SYNTH_WAVETABLE;
    p.layer[0].position = 2;
    wt_render(&p, WavetableBasic);
    test_assert(memcmp(wt_a_l, audio_l, sizeof(wt_a_l)) == 0);

    p.layer[0].table = 3;
    core = audio_new();
    audio_sine(audio_sound_a, 4800, 100, 0.5f);
    test_assert(box_audio_core_sound(core, 3, audio_sound_a, 4800, 1, 0));
    wt_define(core, WT_PATCH, &p);
    audio_play(core, 1, WT_PATCH, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    test_assert(memcmp(wt_a_l, audio_l, sizeof(wt_a_l)) == 0);
}

/** A position past the last frame plays the last frame, a negative or NaN one the first, a
 * NaN envelope none, and an LFO deeper than the most frames as deep as them. */
void SynthWavetable_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p = wt_patch(330, 3);
    wt_render(&p, WavetableBasic);
    memcpy(wt_a_l, audio_l, sizeof(wt_a_l));
    p.layer[0].position = 10;
    wt_render(&p, WavetableBasic);
    test_assert(memcmp(wt_a_l, audio_l, sizeof(wt_a_l)) == 0);
    p.layer[0].position = 3;
    p.layer[0].position_envelope = 100;
    wt_render(&p, WavetableBasic);
    test_assert(memcmp(wt_a_l, audio_l, sizeof(wt_a_l)) == 0);

    p = wt_patch(330, 0);
    wt_render(&p, WavetableBasic);
    memcpy(wt_a_l, audio_l, sizeof(wt_a_l));
    p.layer[0].position = -2;
    wt_render(&p, WavetableBasic);
    test_assert(memcmp(wt_a_l, audio_l, sizeof(wt_a_l)) == 0);
    p.layer[0].position = NAN;
    p.layer[0].position_envelope = NAN;
    wt_render(&p, WavetableBasic);
    test_assert(memcmp(wt_a_l, audio_l, sizeof(wt_a_l)) == 0);

    p = wt_patch(330, 1);
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2,
        .position = BOX_AUDIO_TABLE_FRAMES_MAX };
    wt_render(&p, WavetableBasic);
    memcpy(wt_a_l, audio_l, sizeof(wt_a_l));
    p.lfo[0].position = 1e6f;
    wt_render(&p, WavetableBasic);
    test_assert(memcmp(wt_a_l, audio_l, sizeof(wt_a_l)) == 0);
    test_assert(wt_finite(audio_l, 0, AUDIO_FRAMES));
}

/** A table released while a voice plays it stays until the voice ends: the voice renders bit
 * identical to one without the release, the core frees the sound only after its end, and a
 * new play of the patch plays a sine. */
void SynthWavetable_release_while_playing(void) {
    // audio/synth
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = wt_patch(330, 2);
    float *data = wt_table(core, WT_TABLE, WavetableBasic);
    box_audio_event_t last;
    uint64_t plain;
    wt_define(core, WT_PATCH, &p);
    audio_play(core, 1, WT_PATCH, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    plain = audio_hash(audio_l, AUDIO_FRAMES);
    box_audio_core_free(core);
    ecs_os_free(data);

    core = audio_new();
    data = wt_table(core, WT_TABLE, WavetableBasic);
    wt_define(core, WT_PATCH, &p);
    audio_play(core, 1, WT_PATCH, 0, 1, 0, 1, 0);
    audio_render(core, 0, 94 * BOX_AUDIO_CORE_QUANTUM);
    wt_release(core, WT_TABLE);
    audio_render(core, 94 * BOX_AUDIO_CORE_QUANTUM, 188 * BOX_AUDIO_CORE_QUANTUM);
    test_int(audio_events(core, BOX_AUDIO_EVENT_SOUND_FREE, NULL), 0);
    test_int(box_audio_core_voice_count(core), 1);
    audio_render(core, 282 * BOX_AUDIO_CORE_QUANTUM,
        AUDIO_FRAMES - 282 * BOX_AUDIO_CORE_QUANTUM);
    test_assert(audio_hash(audio_l, AUDIO_FRAMES) == plain);

    audio_render(core, 0, 4800);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_SOUND_FREE, &last), 1);
    test_int(last.id, WT_TABLE);

    audio_play(core, 2, WT_PATCH, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    test_assert(audio_peak(audio_l, 0, 4800) > 0.4f);
    test_assert(wt_finite(audio_l, 0, 4800));
    box_audio_core_free(core);
    ecs_os_free(data);
}

/** Two renders of a patch of wavetable layers with LFOs, unison and modulation are bit
 * identical. */
void SynthWavetable_deterministic(void) {
    // audio/synth
    box_audio_synth_patch_t p = wt_patch(180, 1);
    p.layers = 2;
    p.layer[0].voices = 3;
    p.layer[0].detune = 15;
    p.layer[0].width = 0.7f;
    p.layer[0].position_envelope = 1.5f;
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_WAVETABLE,
        .frequency = 360, .level = 0.5f, .modulates = 1, .modulation = BOX_AUDIO_SYNTH_PHASE,
        .index = 0.5f, .table = WT_TABLE, .position = 7.5f };
    p.lfos = 2;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_RANDOM, .rate = 4,
        .position = 5 };
    p.lfo[1] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 1,
        .layer = 2, .position = 6, .pitch = 20 };
    wt_render(&p, WavetablePulse);
    memcpy(wt_a_l, audio_l, sizeof(wt_a_l));
    memcpy(wt_a_r, audio_r, sizeof(wt_a_r));
    wt_render(&p, WavetablePulse);
    test_assert(memcmp(wt_a_l, audio_l, sizeof(wt_a_l)) == 0);
    test_assert(memcmp(wt_a_r, audio_r, sizeof(wt_a_r)) == 0);
    test_assert(wt_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0.1f);
}

/** The built-in tables have their frames, a table of 64 frames takes under about 1 MB, the
 * core takes a table as one of its sounds by its header and not a sound of other frames, and
 * the top level of the saw frame is a sine at the amplitude of the fundamental of the saw,
 * raised by under 2 % for the gain of the interpolator. */
void SynthWavetable_layout(void) {
    // audio/synth
    float sine[BOX_AUDIO_TABLE_LENGTH_MIN];
    const float *top;
    double amplitude;
    int32_t floats = 0, i, offset;
    float *data;
    test_int(box_audio_wavetable_frames(WavetableBasic), 4);
    test_int(box_audio_wavetable_frames(WavetablePulse), 16);
    test_int(box_audio_wavetable_frames(WavetableHarmonics), 32);
    test_assert(BOX_AUDIO_TABLE_FLOATS(64) * (int32_t)sizeof(float) < 1100000);
    offset = 0;
    for (i = 0; i < BOX_AUDIO_TABLE_LEVELS; i ++) {
        offset += BOX_AUDIO_TABLE_LEVEL_LENGTH(i);
    }

    test_int(offset, BOX_AUDIO_TABLE_FRAME_FLOATS);
    test_int(BOX_AUDIO_TABLE_LEVEL_LENGTH(0), 2048);

    data = box_audio_wavetable_kind(WavetableBasic, &floats);
    test_int(audio_table_frames(data, floats, 1), 4);
    test_int(audio_table_frames(data, floats, 2), 0);
    test_int(audio_table_frames(data, floats - 1, 1), 0);
    audio_sine(audio_sound_a, 4800, 100, 0.5f);
    test_int(audio_table_frames(audio_sound_a, 4800, 1), 0);

    top = data + BOX_AUDIO_TABLE_HEADER + 4 * (BOX_AUDIO_TABLE_FRAME_FLOATS -
        BOX_AUDIO_TABLE_LENGTH_MIN) + 2 * BOX_AUDIO_TABLE_LENGTH_MIN;
    amplitude = -top[BOX_AUDIO_TABLE_LENGTH_MIN / 4];
    test_assert(amplitude > 2 / AUDIO_PI && amplitude < 1.02 * 2 / AUDIO_PI);
    for (i = 0; i < BOX_AUDIO_TABLE_LENGTH_MIN; i ++) {
        sine[i] = (float)(-amplitude * sin(2 * AUDIO_PI * i / BOX_AUDIO_TABLE_LENGTH_MIN));
        test_assert(fabs(top[i] - sine[i]) < 1e-5);
    }

    ecs_os_free(data);
}

/** test/assets/audio/synth_wavetable.flecs compiles the table, the position, the envelope and
 * the LFO into the patch, which names the sound of the table, and the sound plays. */
void SynthWavetable_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_wavetable.flecs");
    ecs_entity_t morph, harmonics;
    const box_audio_synth_patch_t *p;
    const AudioWavetable *w;
    int32_t start;
    audio_log_start();
    test_assert(ecs_script_run(world, "wavetable", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);
    audio_log_stop();
    test_int(audio_log_errors, 0);

    morph = ecs_lookup(world, "Morph");
    harmonics = ecs_lookup(world, "HarmonicsTable");
    test_assert(morph != 0 && harmonics != 0);
    test_int(ecs_get(world, harmonics, Wavetable)->kind, WavetableHarmonics);
    w = ecs_get(world, harmonics, AudioWavetable);
    test_assert(w != NULL && w->id != 0);
    p = &ecs_get(world, morph, AudioSynth)->patch;
    test_int(p->layers, 1);
    test_int(p->layer[0].wave, BOX_AUDIO_SYNTH_WAVETABLE);
    test_int(p->layer[0].table, w->id);
    test_flt(p->layer[0].position, 2);
    test_flt(p->layer[0].position_envelope, 20);
    test_int(p->lfos, 1);
    test_flt(p->lfo[0].position, 6);

    start = audio_tap_count;
    test_assert(box_audio_play(world, morph, 1.0f));
    audio_run(world, 30);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(wt_finite(audio_tap_l, start, audio_tap_count));
    ecs_fini(world);
}

/** A wavetable layer without a SynthTable, or whose table is not an entity with a Wavetable,
 * plays a sine and logs one error that names the sound. */
void SynthWavetable_missing_table(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    ecs_entity_t bare, plain;
    int32_t start;
    audio_log_start();
    test_assert(ecs_script_run(world, "bare",
        "using box.audio\n"
        "prefab Bare {\n"
        "  SynthSound: {}\n"
        "  SynthEnvelope: {attack: 0.005, sustain: 1, hold: 0.2, release: 0.05}\n"
        "  SynthOscillator: {wave: SynthWavetable, frequency: 220, level: 0.3}\n"
        "}\n", NULL) == 0);
    audio_run(world, 2);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "Bare") != NULL);
    test_assert(strstr(audio_log_last, "sine") != NULL);
    bare = ecs_lookup(world, "Bare");
    test_int(ecs_get(world, bare, AudioSynth)->patch.layer[0].table, 0);
    start = audio_tap_count;
    test_assert(box_audio_play(world, bare, 1.0f));
    audio_run(world, 4);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.2f);

    audio_log_start();
    test_assert(ecs_script_run(world, "plain",
        "using box.audio\n"
        "prefab NotATable {}\n"
        "prefab Plain {\n"
        "  SynthSound: {}\n"
        "  SynthEnvelope: {attack: 0.005, sustain: 1, hold: 0.2, release: 0.05}\n"
        "  SynthOscillator: {wave: SynthWavetable, frequency: 220, level: 0.3}\n"
        "  SynthTable: {table: NotATable, position: 1}\n"
        "}\n", NULL) == 0);
    audio_run(world, 2);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "Plain") != NULL);
    plain = ecs_lookup(world, "Plain");
    test_int(ecs_get(world, plain, AudioSynth)->patch.layer[0].table, 0);
    ecs_fini(world);
}

/** Deleting a Wavetable while a voice plays it neither crashes nor stops the voice, which
 * plays to its end; then the core frees the table, and the patch plays a sine. A changed
 * Wavetable gets a new sound, which the patch takes, while the voice of the old one plays on. */
void SynthWavetable_delete_while_playing(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    ecs_entity_t pad, table;
    uint32_t id, id2;
    int32_t sounds, start;
    test_assert(ecs_script_run(world, "pad",
        "using box.audio\n"
        "prefab PulseTable { Wavetable: {kind: WavetablePulse} }\n"
        "prefab Pad {\n"
        "  SynthSound: {}\n"
        "  SynthEnvelope: {attack: 0.01, sustain: 1, hold: 0.5, release: 0.05}\n"
        "  SynthOscillator: {wave: SynthWavetable, frequency: 220, level: 0.3}\n"
        "  SynthTable: {table: PulseTable, position: 4, envelope: 8}\n"
        "  SynthLfo: {rate: 3, position: 3}\n"
        "}\n", NULL) == 0);
    audio_run(world, 2);
    pad = ecs_lookup(world, "Pad");
    table = ecs_lookup(world, "PulseTable");
    id = ecs_get(world, table, AudioWavetable)->id;
    test_assert(id != 0);
    test_int(ecs_get(world, pad, AudioSynth)->patch.layer[0].table, id);

    test_assert(box_audio_play(world, pad, 1.0f));
    audio_run(world, 6);
    ecs_set(world, table, Wavetable, { .kind = WavetableHarmonics });
    audio_run(world, 2);
    id2 = ecs_get(world, table, AudioWavetable)->id;
    test_assert(id2 != 0 && id2 != id);
    test_int(ecs_get(world, pad, AudioSynth)->patch.layer[0].table, id2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    test_assert(box_audio_play(world, pad, 1.0f));
    audio_run(world, 6);
    sounds = box_audio_module_sounds(world);
    start = audio_tap_count;
    ecs_delete(world, table);
    audio_run(world, 6);
    test_int(ecs_get(world, pad, AudioSynth)->patch.layer[0].table, 0);
    test_assert(ecs_singleton_get(world, AudioStatus)->voices >= 1);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(wt_finite(audio_tap_l, start, audio_tap_count));

    audio_run(world, 60);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_assert(box_audio_module_sounds(world) <= sounds - 2);

    start = audio_tap_count;
    test_assert(box_audio_play(world, pad, 1.0f));
    audio_run(world, 6);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    ecs_fini(world);
}
