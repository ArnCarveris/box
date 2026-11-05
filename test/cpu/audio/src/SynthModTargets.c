/**
 * @file SynthModTargets.c
 * @brief The targets of the modulation matrix of the newer synth features: the ratio of hard
 * sync, the position of a wavetable layer and the vowel of a formant filter, on the mono and
 * the stereo render paths, routes to targets that do not apply, routes at an amount of 0 that
 * render as without them, and a script.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Sound id of the table and of the patch of the renders */
#define MT_TABLE 2
#define MT_PATCH 1

/** Frames skipped before a measure, past the attack */
#define MT_SKIP 2400

/** Frames of a window of the spectrum, 10 periods of a note of 200 Hz at AUDIO_RATE */
#define MT_WINDOW 2400

/** Windows of a render of a second */
#define MT_WINDOWS 18

/** Hz of the saw of the formant renders */
#define MT_SAW 110.0

/** Frames that hold a whole number of periods of MT_SAW at AUDIO_RATE: 11 */
#define MT_PERIODS 4800

/** Harmonics of the saw that the spectral envelope looks at, up to 3.3 kHz */
#define MT_HARMONICS 30

/** Samples of the renders a test compares */
static float mt_a_l[AUDIO_FRAMES];
static float mt_a_r[AUDIO_FRAMES];

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void mt_define(
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

/** Render a second of a patch at a velocity in a new core into audio_l and audio_r, with a
 * built-in table as MT_TABLE.
 *
 * @param p The patch
 * @param kind A WavetableKind, -1 for no table
 * @param velocity Velocity of the note
 */
static void mt_render(
    const box_audio_synth_patch_t *p,
    int32_t kind,
    float velocity)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    float *data = NULL;
    int32_t floats = 0;
    uint8_t bytes[64];
    box_audio_writer_t w;
    test_assert(core != NULL);
    if (kind >= 0) {
        data = box_audio_wavetable_kind(kind, &floats);
        test_assert(data != NULL);
        test_assert(box_audio_core_sound(core, MT_TABLE, data, floats, 1, 0));
    }

    mt_define(core, MT_PATCH, p);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play_priority(&w, 1, MT_PATCH, 0, 1, 0, 1, velocity,
        0, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    if (data) {
        ecs_os_free(data);
    }
}

/** Keep the last render in mt_a_l and mt_a_r. */
static void mt_keep(void)
{
    memcpy(mt_a_l, audio_l, sizeof(mt_a_l));
    memcpy(mt_a_r, audio_r, sizeof(mt_a_r));
}

/** Whether the last render is the same as the one in mt_a_l and mt_a_r, bit for bit.
 *
 * @return Whether it is
 */
static bool mt_same(void)
{
    return memcmp(mt_a_l, audio_l, sizeof(mt_a_l)) == 0 &&
        memcmp(mt_a_r, audio_r, sizeof(mt_a_r)) == 0;
}

/** Whether a range of samples is finite.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return Whether every sample is finite
 */
static bool mt_finite(
    const float *x,
    int32_t from,
    int32_t to)
{
    int32_t i;
    for (i = from; i < to; i ++) {
        if (!isfinite(x[i])) {
            return false;
        }
    }

    return true;
}

/** Energy of the component of a range at a frequency of an exact bin, with a Goertzel filter.
 *
 * @param x Samples
 * @param n Number of samples
 * @param frequency Hz, a whole number of periods in n samples
 * @return The sum of the squares of the component
 */
static double mt_bin_energy(
    const float *x,
    int32_t n,
    double frequency)
{
    double c = 2 * cos(2 * AUDIO_PI * frequency / AUDIO_RATE), s0, s1 = 0, s2 = 0;
    int32_t i;
    for (i = 0; i < n; i ++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return 2 * (s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

/** Spectral centroid of a window of a render, in harmonics of a master of 200 Hz.
 *
 * @param x Samples of the window, MT_WINDOW of them
 * @return The centroid
 */
static double mt_centroid(
    const float *x)
{
    double sum = 0, weighted = 0, e;
    int32_t k;
    for (k = 1; k * 200 < 20000; k ++) {
        e = mt_bin_energy(x, MT_WINDOW, k * 200.0);
        sum += e;
        weighted += k * e;
    }

    return weighted / sum;
}

/** Range of the centroid over the windows of a channel of the last render.
 *
 * @param x Samples of the render, a second of them
 * @return The highest centroid minus the lowest
 */
static double mt_centroid_range(
    const float *x)
{
    double lo = 1e9, hi = -1e9, c;
    int32_t w;
    for (w = 0; w < MT_WINDOWS; w ++) {
        c = mt_centroid(x + MT_SKIP + w * MT_WINDOW);
        lo = c < lo ? c : lo;
        hi = c > hi ? c : hi;
    }

    return hi - lo;
}

/** Highest harmonic of 200 Hz in a window of a render that is over -40 dB of the
 * fundamental, up to the 60th.
 *
 * @param x Samples of the window, MT_WINDOW of them
 * @return The harmonic, 1 for a sine
 */
static int32_t mt_harmonics(
    const float *x)
{
    double e1 = mt_bin_energy(x, MT_WINDOW, 200.0);
    int32_t k, result = 1;
    for (k = 2; k <= 60; k ++) {
        if (mt_bin_energy(x, MT_WINDOW, k * 200.0) > 1e-4 * e1) {
            result = k;
        }
    }

    return result;
}

/** A patch of one layer at a steady level for a second.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @return The patch
 */
static box_audio_synth_patch_t mt_patch(
    uint32_t wave,
    float frequency)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 7;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 1, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = wave, .frequency = frequency,
        .level = 0.5f };

    return p;
}

/** A patch of a saw at MT_SAW Hz held for a second through a formant filter at a vowel.
 *
 * @param vowel The vowel, 0..4
 * @return The patch
 */
static box_audio_synth_patch_t mt_formant(
    float vowel)
{
    box_audio_synth_patch_t p = mt_patch(BOX_AUDIO_SYNTH_SAW, (float)MT_SAW);
    p.layer[0].level = 0.05f;
    p.filter = BOX_AUDIO_SYNTH_FORMANT;
    p.cutoff = 1000;
    p.vowel = vowel;

    return p;
}

/** Make the layers of a patch three unison voices without detune and with width, so it
 * renders on the stereo path with the spectrum of a single voice.
 *
 * @param p The patch
 */
static void mt_wide(
    box_audio_synth_patch_t *p)
{
    uint32_t l;
    for (l = 0; l < p->layers; l ++) {
        p->layer[l].voices = 3;
        p->layer[l].detune = 0;
        p->layer[l].width = 0.5f;
    }
}

/** Add a route to a patch.
 *
 * @param p The patch
 * @param source A box_audio_synth_source_kind_t
 * @param target A box_audio_synth_target_t
 * @param layer 1 + the layer it moves, 0 for every layer
 * @param amount The amount
 */
static void mt_route(
    box_audio_synth_patch_t *p,
    uint32_t source,
    uint32_t target,
    uint32_t layer,
    float amount)
{
    p->route[p->routes ++] = (box_audio_synth_route_t){
        .source = source, .target = target, .layer = layer, .amount = amount };
}

/** Amplitude of a frequency in a range of samples at AUDIO_RATE, from its DFT bin.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param frequency Hz
 * @return Amplitude
 */
static double mt_amplitude(
    const float *x,
    int32_t from,
    int32_t to,
    double frequency)
{
    double re = 0, im = 0, w = 2 * AUDIO_PI * frequency / AUDIO_RATE;
    int32_t i;
    for (i = from; i < to; i ++) {
        re += (double)x[i] * cos(w * (double)(i - from));
        im += (double)x[i] * sin(w * (double)(i - from));
    }

    return 2 * sqrt(re * re + im * im) / (double)(to - from);
}

/** Prominence of a local maximum of the dB of the harmonics: how far it stands above the
 * higher of the lowest points on each side before a higher harmonic, or the end.
 *
 * @param db dB of harmonics 1..MT_HARMONICS + 1
 * @param h The harmonic of the maximum
 * @return Its prominence in dB
 */
static double mt_prominence(
    const double *db,
    int32_t h)
{
    double left = db[h], right = db[h];
    int32_t i;
    for (i = h - 1; i >= 1 && db[i] <= db[h]; i --) {
        left = db[i] < left ? db[i] : left;
    }

    for (i = h + 1; i <= MT_HARMONICS + 1 && db[i] <= db[h]; i ++) {
        right = db[i] < right ? db[i] : right;
    }

    return db[h] - (left > right ? left : right);
}

/** The first two spectral peaks of a saw at MT_SAW through a filter, as in the SynthFormant
 * suite: the first two local maxima of the amplitudes of its harmonics times their number
 * within 30 dB of the highest and more than 6 dB prominent, refined by parabola.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame, from plus a multiple of MT_PERIODS
 * @param f1 Receives the first peak in Hz
 * @param f2 Receives the second peak in Hz, 0 for none
 */
static void mt_peaks(
    const float *x,
    int32_t from,
    int32_t to,
    double *f1,
    double *f2)
{
    double db[MT_HARMONICS + 2], top = -1000, a, b, c, d, f;
    int32_t h, found = 0;
    for (h = 1; h <= MT_HARMONICS + 1; h ++) {
        db[h] = 20 * log10(mt_amplitude(x, from, to, MT_SAW * h) * h + 1e-12);
        top = db[h] > top ? db[h] : top;
    }

    *f1 = 0;
    *f2 = 0;
    for (h = 2; h <= MT_HARMONICS && found < 2; h ++) {
        if (db[h] > db[h - 1] && db[h] >= db[h + 1] && db[h] > top - 30 &&
            mt_prominence(db, h) > 6)
        {
            a = db[h - 1];
            b = db[h];
            c = db[h + 1];
            d = a - 2 * b + c;
            d = d < 0 ? 0.5 * (a - c) / d : 0.0;
            f = ((double)h + d) * MT_SAW;
            if (found == 0) {
                *f1 = f;
            } else {
                *f2 = f;
            }

            found ++;
        }
    }
}

/** Check that a peak is within 10% of a frequency.
 *
 * @param peak The peak in Hz
 * @param frequency The formant in Hz
 */
static void mt_near(
    double peak,
    double frequency)
{
    test_assert(peak > frequency * 0.9 && peak < frequency * 1.1);
}

/* The cases without a world call no flecs function that would set up the OS API the tables
 * are built with. */
void SynthModTargets_setup(void) {
    ecs_os_set_api_defaults();
}

/** Render a saw synced at a ratio of 2 with the mod envelope routed to its ratio, on a route
 * of the root or of the layer, and check that its spectral centroid sweeps, while the same saw
 * without the route keeps it.
 *
 * @param wide Whether the layer renders on the stereo path
 * @param layer 1 for a route of the layer, 0 for one of the root
 */
static void mt_sync_sweep(
    bool wide,
    uint32_t layer)
{
    box_audio_synth_patch_t p = mt_patch(BOX_AUDIO_SYNTH_SAW, 200);
    double steady, swept;
    p.layer[0].sync = 2;
    if (wide) {
        mt_wide(&p);
    }

    mt_render(&p, -1, 1);
    steady = mt_centroid_range(audio_l);
    p.mod_envelope = (box_audio_synth_envelope_t){
        .attack = 0.3f, .decay = 0.3f, .sustain = 0, .release = 0.05f };
    mt_route(&p, BOX_AUDIO_SYNTH_FROM_MOD_ENV, BOX_AUDIO_SYNTH_TO_SYNC, layer, 24);
    mt_render(&p, -1, 1);
    swept = mt_centroid_range(audio_l);
    printf("mod envelope to sync, %s, route of %s: centroid range %f steady, %f swept\n",
        wide ? "wide" : "mono", layer ? "the layer" : "the root", steady, swept);
    test_assert(mt_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(steady < 0.1);
    test_assert(swept > 2);
    if (wide) {
        test_assert(mt_finite(audio_r, 0, AUDIO_FRAMES));
        test_assert(mt_centroid_range(audio_r) > 2);
    }
}

/** The mod envelope routed to the ratio of hard sync sweeps the spectrum of a synced saw, from
 * a route of the root and of the layer, on the mono and the stereo paths. */
void SynthModTargets_mod_envelope_sync(void) {
    // audio/synth
    mt_sync_sweep(false, 0);
    mt_sync_sweep(false, 1);
    mt_sync_sweep(true, 0);
}

/** Render a WavetableHarmonics layer at position 0 with velocity routed to its position by 31
 * frames, and check how many harmonics it has at a velocity.
 *
 * @param wide Whether the layer renders on the stereo path
 * @param layer 1 for a route of the layer, 0 for one of the root
 * @param velocity Velocity of the note
 * @return The highest harmonic, the same in every window
 */
static int32_t mt_velocity_position(
    bool wide,
    uint32_t layer,
    float velocity)
{
    box_audio_synth_patch_t p = mt_patch(BOX_AUDIO_SYNTH_WAVETABLE, 200);
    int32_t result, w;
    p.layer[0].table = MT_TABLE;
    if (wide) {
        mt_wide(&p);
    }

    mt_route(&p, BOX_AUDIO_SYNTH_FROM_VELOCITY, BOX_AUDIO_SYNTH_TO_POSITION, layer, 31);
    mt_render(&p, WavetableHarmonics, velocity);
    test_assert(mt_finite(audio_l, 0, AUDIO_FRAMES));
    result = mt_harmonics(audio_l + MT_SKIP);
    for (w = 1; w < MT_WINDOWS; w ++) {
        test_int(mt_harmonics(audio_l + MT_SKIP + w * MT_WINDOW), result);
    }

    if (wide) {
        test_int(mt_harmonics(audio_r + MT_SKIP), result);
    }

    printf("velocity %.2f to position, %s, route of %s: %d harmonics\n", (double)velocity,
        wide ? "wide" : "mono", layer ? "the layer" : "the root", (int)result);

    return result;
}

/** Velocity routed to the position of a wavetable layer picks a brighter frame of
 * WavetableHarmonics at a high velocity: a sine at velocity 0, a few harmonics at 0.1 and
 * nearly all 32 at 1, the moved position clamped to the last frame at 1. */
void SynthModTargets_velocity_position(void) {
    // audio/synth
    int32_t soft, mid, loud;
    soft = mt_velocity_position(false, 1, 0);
    mid = mt_velocity_position(false, 1, 0.1f);
    loud = mt_velocity_position(false, 1, 1);
    test_int(soft, 1);
    test_assert(mid >= 3 && mid <= 6);
    test_assert(loud >= 25);
    test_int(mt_velocity_position(false, 0, 1), loud);
    test_assert(mt_velocity_position(true, 1, 0) <= 2);
    test_assert(mt_velocity_position(true, 1, 1) >= 25);
}

/** Render a saw through a formant filter at vowel 0 with LFO 1, a sine of 0.5 Hz from its
 * top without depths of its own, routed to the vowel by 2 vowels, and check that the peaks
 * start at i and reach a, clamped, from 0.5 s on.
 *
 * @param wide Whether the layer renders on the stereo path
 * @param layer 1 for a route of the layer, 0 for one of the root
 */
static void mt_lfo_vowel(
    bool wide,
    uint32_t layer)
{
    box_audio_synth_patch_t p = mt_formant(0);
    double e1, e2, l1, l2;
    if (wide) {
        p.layer[0].voices = 2;
        p.layer[0].detune = 0;
        p.layer[0].width = 1;
    }

    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 0.5f, .phase = 0.25f };
    mt_route(&p, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_VOWEL, layer, 2);
    mt_render(&p, -1, 1);
    test_assert(mt_finite(audio_l, 0, AUDIO_FRAMES));
    mt_peaks(audio_l, 0, MT_PERIODS, &e1, &e2);
    mt_peaks(audio_l, 28800, 43200, &l1, &l2);
    printf("lfo 1 to vowel, %s, route of %s: peaks at %.1f and %.1f Hz, then %.1f and "
        "%.1f Hz\n", wide ? "wide" : "mono", layer ? "the layer" : "the root", e1, e2, l1, l2);
    test_assert(e1 < 400);
    test_assert(e2 > 1600);
    mt_near(l1, 650);
    mt_near(l2, 1080);
    if (wide) {
        mt_peaks(audio_r, 0, MT_PERIODS, &e1, &e2);
        test_assert(e1 < 400);
        test_assert(e2 > 1600);
    }
}

/** LFO 1 routed to the vowel of a formant filter moves its peaks, from a route of the root and
 * of a layer, which moves the vowel of the sound as the cutoff, on the mono and the stereo
 * paths. */
void SynthModTargets_lfo1_vowel(void) {
    // audio/synth
    mt_lfo_vowel(false, 0);
    mt_lfo_vowel(false, 1);
    mt_lfo_vowel(true, 0);
}

/** Routes to targets that do not apply do nothing: the render is bit for bit the one without
 * them. A route to the ratio of sync on a saw without sync, of the root and of the layer; one
 * to the position on a saw that is not a wavetable; one to the vowel without a filter and with
 * a lowpass; on the mono and the stereo paths. */
void SynthModTargets_not_applied(void) {
    // audio/synth
    box_audio_synth_patch_t p, q;
    int32_t wide;
    for (wide = 0; wide < 2; wide ++) {
        p = mt_patch(BOX_AUDIO_SYNTH_SAW, 200);
        p.mod_envelope = (box_audio_synth_envelope_t){
            .attack = 0.3f, .decay = 0.3f, .sustain = 0, .release = 0.05f };
        if (wide) {
            mt_wide(&p);
        }

        mt_render(&p, -1, 1);
        mt_keep();
        q = p;
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_MOD_ENV, BOX_AUDIO_SYNTH_TO_SYNC, 0, 24);
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_MOD_ENV, BOX_AUDIO_SYNTH_TO_SYNC, 1, 24);
        mt_render(&q, -1, 1);
        test_assert(mt_same());

        q = p;
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_VELOCITY, BOX_AUDIO_SYNTH_TO_POSITION, 0, 31);
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_MOD_ENV, BOX_AUDIO_SYNTH_TO_POSITION, 1, 31);
        mt_render(&q, -1, 1);
        test_assert(mt_same());

        q = p;
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_MOD_ENV, BOX_AUDIO_SYNTH_TO_VOWEL, 0, 4);
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_VELOCITY, BOX_AUDIO_SYNTH_TO_VOWEL, 1, -4);
        mt_render(&q, -1, 1);
        test_assert(mt_same());

        p.filter = BOX_AUDIO_SYNTH_LOWPASS;
        p.cutoff = 800;
        p.resonance = 0.3f;
        mt_render(&p, -1, 1);
        mt_keep();
        q = p;
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_MOD_ENV, BOX_AUDIO_SYNTH_TO_VOWEL, 0, 4);
        mt_render(&q, -1, 1);
        test_assert(mt_same());
        printf("routes that do not apply, %s: identical\n", wide ? "wide" : "mono");
    }
}

/** Routes to the new targets at an amount of 0 render bit for bit as the patch without
 * them: a synced saw, a wavetable layer and a formant filter, each with its own LFO on the
 * target, on the mono and the stereo paths. */
void SynthModTargets_defaults_identical(void) {
    // audio/synth
    box_audio_synth_patch_t p, q;
    int32_t wide;
    for (wide = 0; wide < 2; wide ++) {
        p = mt_patch(BOX_AUDIO_SYNTH_SAW, 200);
        p.layer[0].sync = 2;
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 1,
            .hard_sync = 12 };
        if (wide) {
            mt_wide(&p);
        }

        mt_render(&p, -1, 1);
        mt_keep();
        q = p;
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_SYNC, 0, 0);
        mt_render(&q, -1, 1);
        test_assert(mt_same());

        p = mt_patch(BOX_AUDIO_SYNTH_WAVETABLE, 200);
        p.layer[0].table = MT_TABLE;
        p.layer[0].position = 4;
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 1,
            .position = 8 };
        if (wide) {
            mt_wide(&p);
        }

        mt_render(&p, WavetableHarmonics, 1);
        mt_keep();
        q = p;
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_POSITION, 1, 0);
        mt_render(&q, WavetableHarmonics, 1);
        test_assert(mt_same());

        p = mt_formant(1);
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2,
            .vowel = 1 };
        if (wide) {
            mt_wide(&p);
        }

        mt_render(&p, -1, 1);
        mt_keep();
        q = p;
        mt_route(&q, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_VOWEL, 0, 0);
        mt_render(&q, -1, 1);
        test_assert(mt_same());
        printf("routes at an amount of 0, %s: identical\n", wide ? "wide" : "mono");
    }
}

/** The new targets follow the core in the order of the enums, and their amounts are clamped
 * to the ranges of their targets: past the range they render as at its end, NaN as 0, and a
 * target past the last does nothing. */
void SynthModTargets_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p, q;
    test_int(SynthTargetHardSync, BOX_AUDIO_SYNTH_TO_SYNC);
    test_int(SynthTargetPosition, BOX_AUDIO_SYNTH_TO_POSITION);
    test_int(SynthTargetVowel, BOX_AUDIO_SYNTH_TO_VOWEL);
    test_int(BOX_AUDIO_SYNTH_TARGETS, SynthTargetVowel + 1);

    p = mt_patch(BOX_AUDIO_SYNTH_SAW, 200);
    p.layer[0].sync = 1.5f;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2 };
    q = p;
    mt_route(&p, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_SYNC, 0,
        BOX_AUDIO_SYNTH_SYNC_SEMITONES);
    mt_render(&p, -1, 1);
    mt_keep();
    test_assert(mt_finite(audio_l, 0, AUDIO_FRAMES));
    mt_route(&q, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_SYNC, 0, 1000);
    mt_render(&q, -1, 1);
    test_assert(mt_same());

    p = mt_patch(BOX_AUDIO_SYNTH_WAVETABLE, 200);
    p.layer[0].table = MT_TABLE;
    p.layer[0].position = 16;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2 };
    q = p;
    mt_route(&p, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_POSITION, 0,
        -(float)BOX_AUDIO_TABLE_FRAMES_MAX);
    mt_render(&p, WavetableHarmonics, 1);
    mt_keep();
    test_assert(mt_finite(audio_l, 0, AUDIO_FRAMES));
    mt_route(&q, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_POSITION, 0, -1000);
    mt_render(&q, WavetableHarmonics, 1);
    test_assert(mt_same());

    p = mt_formant(1);
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2 };
    q = p;
    mt_route(&p, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_VOWEL, 0, 0);
    mt_render(&p, -1, 1);
    mt_keep();
    mt_route(&q, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TO_VOWEL, 0, NAN);
    mt_render(&q, -1, 1);
    test_assert(mt_same());
    q.route[0].amount = 0;
    mt_route(&q, BOX_AUDIO_SYNTH_FROM_LFO1, BOX_AUDIO_SYNTH_TARGETS, 0, 1);
    mt_render(&q, -1, 1);
    test_assert(mt_same());
}

/** A script routes the new targets from SynthModRoute children of the root and of a layer:
 * they reach the patch with their targets and layers. */
void SynthModTargets_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_mod_targets.flecs");
    const AudioSynth *synth;
    ecs_entity_t lead;
    uint32_t i, found = 0;
    test_assert(ecs_script_run(world, "targets", code, NULL) == 0);
    ecs_os_free(code);
    lead = ecs_lookup(world, "targets_lead");
    test_assert(lead != 0);
    audio_run(world, 1);
    synth = ecs_get(world, lead, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.routes, 3);
    for (i = 0; i < synth->patch.routes; i ++) {
        const box_audio_synth_route_t *r = &synth->patch.route[i];
        if (r->target == BOX_AUDIO_SYNTH_TO_SYNC) {
            test_int(r->source, BOX_AUDIO_SYNTH_FROM_MOD_ENV);
            test_int(r->layer, 0);
            test_flt(r->amount, 12);
            found |= 1;
        } else if (r->target == BOX_AUDIO_SYNTH_TO_VOWEL) {
            test_int(r->source, BOX_AUDIO_SYNTH_FROM_LFO1);
            test_int(r->layer, 0);
            test_flt(r->amount, 1.5f);
            found |= 2;
        } else {
            test_int(r->target, BOX_AUDIO_SYNTH_TO_POSITION);
            test_int(r->source, BOX_AUDIO_SYNTH_FROM_VELOCITY);
            test_int(r->layer, 2);
            test_flt(r->amount, 3);
            found |= 4;
        }
    }

    test_int(found, 7);
    ecs_fini(world);
}
