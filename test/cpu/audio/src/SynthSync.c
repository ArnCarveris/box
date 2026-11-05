/**
 * @file SynthSync.c
 * @brief Hard sync of synth layers, measured on offline renders of a core: the period of the
 * master, a ratio of 1 as no sync, the aliasing of the restart, LFOs of the ratio, unison
 * voices that each sync to their own master, phase modulation of a synced layer, the waves that
 * ignore it, the clamping of the fields, and a script.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Rate at which a master of 220 Hz has a period of exactly 200 frames */
#define SYNC_RATE 44000

/** Frames of the period of a master of 220 Hz at SYNC_RATE */
#define SYNC_PERIOD 200

/** Frames skipped before a measure, past the attack */
#define SYNC_SKIP 2048

/** Frames of the aliasing check, 0.8 seconds at 48 kHz, a whole number of periods of 1 kHz */
#define SYNC_ALIAS_48000 38400

/** Frames of the aliasing check, 0.8 seconds at 44.1 kHz, a whole number of periods of 1 kHz */
#define SYNC_ALIAS_44100 35280

/** Frames of a window of the centroid, 10 periods of 200 Hz at AUDIO_RATE */
#define SYNC_WINDOW 2400

/** Windows of the centroid in a second at AUDIO_RATE */
#define SYNC_WINDOWS 18

/** Samples of the renders a test compares */
static float sync_a_l[AUDIO_FRAMES];
static float sync_a_r[AUDIO_FRAMES];

/** Create a core at a rate.
 *
 * @param rate Frames per second
 * @return The core
 */
static box_audio_core_t* sync_core(
    float rate)
{
    box_audio_core_desc_t desc = { .sample_rate = rate };
    box_audio_core_t *core = box_audio_core_new(&desc);
    test_assert(core != NULL);

    return core;
}

/** Give a sound id of a core a patch with a single command.
 *
 * @param core The core
 * @param sound Id of the sound
 * @param p The patch
 */
static void sync_define(
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

/** A patch of one oscillator at a steady level for a second, hard synced at a ratio.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz of the master
 * @param sync Ratio of the sync, 0 for none
 * @return The patch
 */
static box_audio_synth_patch_t sync_patch(
    uint32_t wave,
    float frequency,
    float sync)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = 7;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 1, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = wave, .frequency = frequency, .level = 0.5f, .sync = sync };

    return p;
}

/** Render a patch at a rate in a new core into audio_l and audio_r.
 *
 * @param p The patch
 * @param rate Frames per second
 * @param frames Frames to render, at most AUDIO_FRAMES
 */
static void sync_render(
    const box_audio_synth_patch_t *p,
    float rate,
    int32_t frames)
{
    box_audio_core_t *core = sync_core(rate);
    sync_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, frames);
    box_audio_core_free(core);
}

/** Render a patch at AUDIO_RATE into sync_a_l and sync_a_r.
 *
 * @param p The patch
 */
static void sync_render_a(
    const box_audio_synth_patch_t *p)
{
    sync_render(p, AUDIO_RATE, AUDIO_FRAMES);
    memcpy(sync_a_l, audio_l, sizeof(sync_a_l));
    memcpy(sync_a_r, audio_r, sizeof(sync_a_r));
}

/** Whether the last render is the same as the one in sync_a_l and sync_a_r, bit for bit.
 *
 * @return Whether it is
 */
static bool sync_same_a(void)
{
    return memcmp(sync_a_l, audio_l, sizeof(sync_a_l)) == 0 &&
        memcmp(sync_a_r, audio_r, sizeof(sync_a_r)) == 0;
}

/** Largest difference of a range of samples and the same range a lag later.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame, a lag before the end of x
 * @param lag Frames of the lag
 * @return The difference
 */
static double sync_lag_diff(
    const float *x,
    int32_t from,
    int32_t to,
    int32_t lag)
{
    double result = 0, d;
    int32_t i;
    for (i = from; i < to; i ++) {
        d = fabs((double)x[i] - (double)x[i + lag]);
        result = d > result ? d : result;
    }

    return result;
}

/** Largest difference of two ranges of samples.
 *
 * @param a Samples
 * @param b Samples
 * @param from First frame
 * @param to End frame
 * @return The difference
 */
static double sync_diff(
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
static bool sync_finite(
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
static double sync_bin_energy(
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

/** Energy of a synced oscillator at a master of 1 kHz and a ratio of 3.7 outside the
 * harmonics of the master and DC, over the energy of its fundamental or of its strongest
 * harmonic. A harmonic at the Nyquist frequency counts half its bin, the energy it has.
 *
 * @param wave A box_audio_synth_wave_t
 * @param rate Frames per second
 * @param n Frames measured, a whole number of periods of 1 kHz
 * @param strongest Whether over the strongest harmonic instead of the fundamental
 * @return The ratio in dB
 */
static double sync_alias(
    uint32_t wave,
    float rate,
    int32_t n,
    bool strongest)
{
    box_audio_synth_patch_t p = sync_patch(wave, 1000, 3.7f);
    const float *x = audio_l + SYNC_SKIP;
    double total = 0, mean = 0, harmonics = 0, top = 0, alias, e;
    int32_t i, k;
    sync_render(&p, rate, SYNC_SKIP + n);
    for (i = 0; i < n; i ++) {
        total += (double)x[i] * x[i];
        mean += x[i];
    }

    mean /= n;
    for (k = 1; k * 1000 <= (int32_t)rate / 2; k ++) {
        e = sync_bin_energy(x, n, k * 1000.0, rate);
        e = k * 2000 == (int32_t)rate ? e / 2 : e;
        harmonics += e;
        top = k == 1 || (strongest && e > top) ? e : top;
    }

    alias = total - harmonics - mean * mean * n;
    alias = alias > 1e-30 ? alias : 1e-30;

    return 10 * log10(alias / top);
}

/** Spectral centroid of a window of a render, in harmonics of a master of 200 Hz.
 *
 * @param x Samples of the window, SYNC_WINDOW of them
 * @return The centroid
 */
static double sync_centroid(
    const float *x)
{
    double sum = 0, weighted = 0, e;
    int32_t k;
    for (k = 1; k * 200 < 20000; k ++) {
        e = sync_bin_energy(x, SYNC_WINDOW, k * 200.0, AUDIO_RATE);
        sum += e;
        weighted += k * e;
    }

    return weighted / sum;
}

/** Range of the centroid over the windows of the last render of a second.
 *
 * @return The highest centroid minus the lowest
 */
static double sync_centroid_range(void)
{
    double lo = 1e9, hi = -1e9, c;
    int32_t w;
    for (w = 0; w < SYNC_WINDOWS; w ++) {
        c = sync_centroid(audio_l + SYNC_SKIP + w * SYNC_WINDOW);
        lo = c < lo ? c : lo;
        hi = c > hi ? c : hi;
    }

    return hi - lo;
}

/** A saw synced at a master of 220 Hz and a ratio of 2.5 repeats every period of the master,
 * 200 frames at 44 kHz, and not at the period of its wave alone, 80 frames. */
void SynthSync_period_master(void) {
    // audio/synth
    box_audio_synth_patch_t p = sync_patch(BOX_AUDIO_SYNTH_SAW, 220, 2.5f);
    double master, wave, plain;
    sync_render(&p, SYNC_RATE, SYNC_RATE / 2);
    master = sync_lag_diff(audio_l, SYNC_SKIP, SYNC_RATE / 2 - SYNC_PERIOD, SYNC_PERIOD);
    wave = sync_lag_diff(audio_l, SYNC_SKIP, SYNC_RATE / 2 - SYNC_PERIOD, 80);
    test_assert(sync_finite(audio_l, 0, SYNC_RATE / 2));
    p.layer[0].sync = 0;
    p.layer[0].frequency = 550;
    memcpy(sync_a_l, audio_l, sizeof(float) * (SYNC_RATE / 2));
    sync_render(&p, SYNC_RATE, SYNC_RATE / 2);
    plain = sync_diff(sync_a_l, audio_l, SYNC_SKIP, SYNC_RATE / 2);
    printf("sync 2.5 at 220 Hz: %g over a master period, %g over 80 frames, %g from a saw at "
        "550 Hz\n", master, wave, plain);
    test_assert(master < 1e-3);
    test_assert(wave > 0.1);
    test_assert(plain > 0.1);
}

/** A ratio of 1 renders bit identical to no sync, on a mono patch with phase modulation, a
 * glide and LFOs (one of them on the ratio of every synced layer), and on a wide unison
 * patch. */
void SynthSync_ratio_one_identical(void) {
    // audio/synth
    box_audio_synth_patch_t p = {0};
    p.layers = 3;
    p.seed = 3;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.1f, .sustain = 0.6f, .hold = 0.5f, .release = 0.1f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 3000;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .frequency_end = 220, .sweep = 0.3f, .level = 0.3f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_TRIANGLE, .frequency = 220,
        .level = 0.3f };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 330,
        .level = 1, .modulates = 2, .index = 1.5f, .feedback = 0.3f };
    p.lfos = 2;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5,
        .pitch = 20, .sync = 12 };
    p.lfo[1] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 2,
        .pulse_width = 0.2f };
    sync_render_a(&p);
    p.layer[0].sync = 1;
    p.layer[1].sync = 1;
    p.layer[2].sync = 1;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    test_assert(sync_same_a());

    p.layer[0].sync = 0;
    p.layer[1].sync = 0;
    p.layer[2].sync = 0;
    p.layer[0].voices = 5;
    p.layer[0].detune = 30;
    p.layer[0].width = 1;
    sync_render_a(&p);
    p.layer[0].sync = 1;
    p.layer[1].sync = 1;
    p.layer[2].sync = 1;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    test_assert(sync_same_a());
    test_assert(sync_diff(audio_l, audio_r, 0, AUDIO_FRAMES) > 0.01);
}

/** At a master of 1 kHz and a ratio of 3.7 at 48 kHz the energy off the harmonics of the
 * master is at least 40 dB under the fundamental: the master has a whole number of frames,
 * 48, so the aliases fold onto its harmonics and only an error of the period would show,
 * such as a restart off the wrap of the master. alias_44100 measures the aliases. */
void SynthSync_alias_48000(void) {
    // audio/synth
    const uint32_t waves[] = { BOX_AUDIO_SYNTH_SAW, BOX_AUDIO_SYNTH_SQUARE,
        BOX_AUDIO_SYNTH_TRIANGLE, BOX_AUDIO_SYNTH_SINE };
    double alias;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        alias = sync_alias(waves[i], 48000, SYNC_ALIAS_48000, false);
        printf("wave %u synced 3.7 at 1 kHz at 48 kHz: %.2f dB off its harmonics\n",
            waves[i], alias);
        test_assert(alias < -40);
    }
}

/** At a master of 1 kHz and a ratio of 3.7 at 44.1 kHz, where the master is 44.1 frames and
 * the aliases fall between its harmonics, the energy off them is under the strongest harmonic
 * (near the ratio, the fundamental of a sync is weak) by the margins the polyBLEP and polyBLAMP
 * of the restart meet: 20 dB for the saw and the square, 30 dB for the triangle, 35 dB for the
 * sine. The aliases of a polyBLEP saw alone at 3.7 kHz are about 22 dB under it; without the
 * correction of the restart the synced saw is about 6 dB under its strongest harmonic, the
 * sine about 22 dB. */
void SynthSync_alias_44100(void) {
    // audio/synth
    const uint32_t waves[] = { BOX_AUDIO_SYNTH_SAW, BOX_AUDIO_SYNTH_SQUARE,
        BOX_AUDIO_SYNTH_TRIANGLE, BOX_AUDIO_SYNTH_SINE };
    const double margins[] = { -20, -20, -30, -35 };
    double alias;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        alias = sync_alias(waves[i], 44100, SYNC_ALIAS_44100, true);
        printf("wave %u synced 3.7 at 1 kHz at 44.1 kHz: %.2f dB off its harmonics\n",
            waves[i], alias);
        test_assert(alias < margins[i]);
    }
}

/** An LFO of the root on the ratio of a synced saw moves its spectral centroid over time; the
 * same saw without the LFO keeps it. */
void SynthSync_lfo_spectrum(void) {
    // audio/synth
    box_audio_synth_patch_t p = sync_patch(BOX_AUDIO_SYNTH_SAW, 200, 2);
    double steady, swept;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    steady = sync_centroid_range();
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 1,
        .sync = 24 };
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    swept = sync_centroid_range();
    printf("centroid of sync 2 at 200 Hz: range %f steady, %f under an LFO of 24 semitones\n",
        steady, swept);
    test_assert(sync_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(steady < 0.1);
    test_assert(swept > 2);
}

/** An LFO on the ratio of a layer syncs that layer from a ratio of 1, so a layer without sync
 * changes its spectrum over time; the same LFO on the root leaves a layer without sync as it
 * is. */
void SynthSync_lfo_layer_syncs(void) {
    // audio/synth
    box_audio_synth_patch_t p = sync_patch(BOX_AUDIO_SYNTH_SAW, 200, 0);
    double swept;
    sync_render_a(&p);
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_TRIANGLE, .rate = 1,
        .sync = 24 };
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    test_assert(sync_same_a());
    p.lfo[0].layer = 1;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    swept = sync_centroid_range();
    printf("centroid of a saw at 200 Hz under a layer LFO of the ratio: range %f\n", swept);
    test_assert(sync_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(swept > 2);
}

/** Unison voices without detune each sync to a master at the same frequency, so the wide
 * render repeats every period of the master on both channels; with a detune each syncs to
 * its own master and it does not. */
void SynthSync_unison_own_master(void) {
    // audio/synth
    box_audio_synth_patch_t p = sync_patch(BOX_AUDIO_SYNTH_SAW, 220, 2.5f);
    double left, right, detuned;
    p.layer[0].voices = 3;
    p.layer[0].width = 1;
    sync_render(&p, SYNC_RATE, SYNC_RATE / 2);
    left = sync_lag_diff(audio_l, SYNC_SKIP, SYNC_RATE / 2 - SYNC_PERIOD, SYNC_PERIOD);
    right = sync_lag_diff(audio_r, SYNC_SKIP, SYNC_RATE / 2 - SYNC_PERIOD, SYNC_PERIOD);
    test_assert(sync_diff(audio_l, audio_r, SYNC_SKIP, SYNC_RATE / 2) > 0.01);
    p.layer[0].detune = 30;
    sync_render(&p, SYNC_RATE, SYNC_RATE / 2);
    detuned = sync_lag_diff(audio_l, SYNC_SKIP, SYNC_RATE / 2 - SYNC_PERIOD, SYNC_PERIOD);
    printf("unison sync 2.5 at 220 Hz over a master period: %g left, %g right, %g detuned\n",
        left, right, detuned);
    test_assert(left < 1e-3);
    test_assert(right < 1e-3);
    test_assert(detuned > 0.05);
}

/** A synced unison layer in stereo, under an LFO of its ratio: both channels play, differ,
 * stay finite and differ from the same layer without sync. */
void SynthSync_unison_stereo(void) {
    // audio/synth
    box_audio_synth_patch_t p = sync_patch(BOX_AUDIO_SYNTH_SQUARE, 150, 0);
    p.layer[0].voices = 5;
    p.layer[0].detune = 25;
    p.layer[0].width = 1;
    p.layer[0].pulse_width = 0.3f;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 3,
        .sync = 12 };
    sync_render_a(&p);
    p.layer[0].sync = 3.3f;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    test_assert(sync_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(sync_finite(audio_r, 0, AUDIO_FRAMES));
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0.05f);
    test_assert(audio_peak(audio_r, 0, AUDIO_FRAMES) > 0.05f);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) < 2);
    test_assert(sync_diff(audio_l, audio_r, SYNC_SKIP, AUDIO_FRAMES) > 0.01);
    test_assert(sync_diff(audio_l, sync_a_l, SYNC_SKIP, AUDIO_FRAMES) > 0.05);
    test_assert(sync_diff(audio_r, sync_a_r, SYNC_SKIP, AUDIO_FRAMES) > 0.05);
}

/** A modulator of index 0 leaves a synced carrier bit identical to the carrier alone; with an
 * index its phase modulation adds to the synced phase and changes it, mono and in stereo. */
void SynthSync_fm_adds(void) {
    // audio/synth
    box_audio_synth_patch_t p = sync_patch(BOX_AUDIO_SYNTH_SINE, 220, 3);
    int32_t wide;
    for (wide = 0; wide < 2; wide ++) {
        p.layers = 1;
        p.layer[0].voices = wide ? 3 : 0;
        p.layer[0].width = wide ? 1.0f : 0.0f;
        sync_render_a(&p);
        p.layers = 2;
        p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 330,
            .level = 1, .modulates = 1, .index = 0 };
        sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
        test_assert(sync_same_a());
        p.layer[1].index = 2;
        sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
        test_assert(sync_finite(audio_l, 0, AUDIO_FRAMES));
        test_assert(sync_diff(audio_l, sync_a_l, SYNC_SKIP, AUDIO_FRAMES) > 0.05);
    }
}

/** Noise and pluck layers ignore the ratio and its LFO: they render bit identical to the same
 * layers without sync. */
void SynthSync_ignored_noise_pluck(void) {
    // audio/synth
    const uint32_t waves[] = { BOX_AUDIO_SYNTH_NOISE, BOX_AUDIO_SYNTH_PLUCK };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        box_audio_synth_patch_t p = sync_patch(waves[i], 220, 0);
        sync_render_a(&p);
        p.layer[0].sync = 4;
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 3,
            .layer = 1, .sync = 12 };
        sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
        p.layer[0].sync = 0;
        memcpy(sync_a_r, audio_l, sizeof(sync_a_r));
        sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
        test_assert(memcmp(audio_l, sync_a_r, sizeof(sync_a_r)) == 0);
    }
}

/** A ratio over 16 renders as 16, a negative or NaN one as none, and the depth of an LFO over
 * 48 semitones as 48. */
void SynthSync_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p = sync_patch(BOX_AUDIO_SYNTH_SAW, 220, 16);
    sync_render_a(&p);
    p.layer[0].sync = 100;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    test_assert(sync_same_a());

    p.layer[0].sync = 0;
    sync_render_a(&p);
    p.layer[0].sync = -3;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    test_assert(sync_same_a());
    p.layer[0].sync = NAN;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    test_assert(sync_same_a());

    p.layer[0].sync = 2;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2,
        .sync = 48 };
    sync_render_a(&p);
    p.lfo[0].sync = 1000;
    sync_render(&p, AUDIO_RATE, AUDIO_FRAMES);
    test_assert(sync_same_a());
    test_assert(sync_finite(audio_l, 0, AUDIO_FRAMES));
}

/** test/assets/audio/synth_sync.flecs compiles the ratio of the layer and the depth of the
 * LFO, the default is no sync, and the sound plays. */
void SynthSync_script(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_sync.flecs");
    ecs_entity_t lead, plain;
    const box_audio_synth_patch_t *p;
    int32_t start;
    test_assert(ecs_script_run(world, "sync", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);

    lead = ecs_lookup(world, "SyncLead");
    plain = ecs_lookup(world, "Plain");
    test_assert(lead != 0 && plain != 0);
    test_flt(ecs_get(world, plain, SynthOscillator)->sync, 0);
    test_flt(ecs_get(world, lead, SynthOscillator)->sync, 2.5f);
    test_flt(ecs_get(world, lead, SynthLfo)->sync, 12);
    p = &ecs_get(world, lead, AudioSynth)->patch;
    test_int(p->layers, 1);
    test_flt(p->layer[0].sync, 2.5f);
    test_int(p->lfos, 1);
    test_flt(p->lfo[0].sync, 12);
    p = &ecs_get(world, plain, AudioSynth)->patch;
    test_flt(p->layer[0].sync, 0);

    audio_run(world, 1);
    start = audio_tap_count;
    test_assert(box_audio_play(world, lead, 1.0f));
    audio_run(world, 4);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(sync_finite(audio_tap_l, start, audio_tap_count));
    ecs_fini(world);
}
