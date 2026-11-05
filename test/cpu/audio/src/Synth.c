/**
 * @file Synth.c
 * @brief The synth of the audio core and synth sounds of box.audio, measured on offline
 * renders: frequencies, aliasing, noise, envelopes, the filter, layers, phase and ring
 * modulation, feedback, eight layers, unison and stereo width, and scripts.
 */

#include "world.h"
#include "../../../../src/jobs/jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/** Rate of the aliasing check, at which 5 kHz has an exact bin every 441 frames */
#define SYNTH_ALIAS_RATE 44100

/** Frames of the aliasing check, 0.8 seconds at SYNTH_ALIAS_RATE */
#define SYNTH_ALIAS_FRAMES 35280

/** Gain of a channel at full pan in the core, square root of 2 */
#define SYNTH_SQRT2 1.41421356237309504880f

/** Frames of a millisecond at AUDIO_RATE, the tolerance of a segment of an envelope */
#define SYNTH_MS 48

/** Create a core at a rate.
 *
 * @param rate Frames per second
 * @return The core
 */
static box_audio_core_t* synth_core(
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
static void synth_define(
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

/** A patch of one oscillator at a steady level for a while, then a short release.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @param level Level of the oscillator
 * @param hold Seconds at the level
 * @return The patch
 */
static box_audio_synth_patch_t synth_steady(
    uint32_t wave,
    float frequency,
    float level,
    float hold)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = hold, .release = 0.001f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = wave, .frequency = frequency, .level = level };

    return p;
}

/** Frequency of a range from its upward zero crossings, at a rate.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param rate Frames per second of the samples
 * @return Frequency in Hz
 */
static double synth_frequency(
    const float *x,
    int32_t from,
    int32_t to,
    float rate)
{
    return audio_frequency(x, from, to) * (double)rate / AUDIO_RATE;
}

/** Energy of the component of a range at a frequency of an exact bin, with a Goertzel filter.
 *
 * @param x Samples
 * @param n Number of samples
 * @param frequency Hz, a whole number of periods in n samples
 * @param rate Frames per second
 * @return The sum of the squares of the component
 */
static double synth_bin_energy(
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

/** Amplitude of a sine in a range, from its mean square.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The amplitude
 */
static double synth_amplitude(
    const float *x,
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        sum += (double)x[i] * x[i];
    }

    return sqrt(2 * sum / (to - from));
}

/** Check that every sample of a range is finite.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return Whether they are
 */
static bool synth_finite(
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

/** First frame of a range within a tolerance of a value.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param value The value
 * @return The frame, -1 when none is
 */
static int32_t synth_first(
    const float *x,
    int32_t from,
    int32_t to,
    float value)
{
    int32_t result = -1, i;
    for (i = from; i < to && result < 0; i ++) {
        if (fabsf(x[i] - value) < 1e-4f) {
            result = i;
        }
    }

    return result;
}

/** Last frame of a range that is not silent.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The frame, -1 when all are silent
 */
static int32_t synth_last(
    const float *x,
    int32_t from,
    int32_t to)
{
    int32_t result = -1, i;
    for (i = to - 1; i >= from && result < 0; i --) {
        if (x[i] != 0) {
            result = i;
        }
    }

    return result;
}

/** Render a steady oscillator at a rate and check its frequency.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @param rate Frames per second
 */
static void synth_check_frequency(
    uint32_t wave,
    float frequency,
    float rate)
{
    box_audio_core_t *core = synth_core(rate);
    box_audio_synth_patch_t p = synth_steady(wave, frequency, 0.5f, 1.0f);
    double f;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, (int32_t)(rate * 0.6f));
    f = synth_frequency(audio_l, (int32_t)(rate * 0.1f), (int32_t)(rate * 0.6f), rate);
    if (fabs(f - frequency) > 0.001 * frequency) {
        printf("wave %u at %.0f Hz and %.0f: %f\n", wave, (double)frequency, (double)rate, f);
    }

    test_assert(fabs(f - frequency) <= 0.001 * frequency);
    box_audio_core_free(core);
}

void Synth_frequency_waves(void) {
    // audio/synth
    const float rates[] = { 44100, 48000 };
    const float frequencies[] = { 110, 440, 1234.5f, 5000 };
    uint32_t wave;
    int32_t r, f;
    for (r = 0; r < 2; r ++) {
        for (wave = BOX_AUDIO_SYNTH_SINE; wave <= BOX_AUDIO_SYNTH_TRIANGLE; wave ++) {
            for (f = 0; f < 4; f ++) {
                synth_check_frequency(wave, frequencies[f], rates[r]);
            }
        }
    }
}

void Synth_sweep_target(void) {
    // audio/synth
    const float rates[] = { 44100, 48000 };
    int32_t r;
    for (r = 0; r < 2; r ++) {
        float rate = rates[r];
        box_audio_core_t *core = synth_core(rate);
        box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 200, 0.5f, 1.0f);
        double early, end;
        p.layer[0].frequency_end = 800;
        p.layer[0].sweep = 0.1f;
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, (int32_t)(rate * 0.6f));
        early = synth_frequency(audio_l, 0, (int32_t)(rate * 0.02f), rate);
        end = synth_frequency(audio_l, (int32_t)(rate * 0.1f) + 1, (int32_t)(rate * 0.6f), rate);
        test_assert(early > 190 && early < 260);
        test_assert(fabs(end - 800) < 0.8);
        box_audio_core_free(core);
    }
}

/** Share of the energy of a 5 kHz oscillator at 44100 Hz outside its harmonics and DC.
 *
 * @param wave A box_audio_synth_wave_t
 * @return The share, 0..1
 */
static double synth_alias(
    uint32_t wave)
{
    box_audio_core_t *core = synth_core(SYNTH_ALIAS_RATE);
    box_audio_synth_patch_t p = synth_steady(wave, 5000, 0.5f, 1.0f);
    const float *x = audio_l + 2048;
    double total = 0, mean = 0, harmonics = 0;
    int32_t i, k;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 2048 + SYNTH_ALIAS_FRAMES);
    for (i = 0; i < SYNTH_ALIAS_FRAMES; i ++) {
        total += (double)x[i] * x[i];
        mean += x[i];
    }

    mean /= SYNTH_ALIAS_FRAMES;
    for (k = 1; k * 5000 < SYNTH_ALIAS_RATE / 2; k ++) {
        harmonics += synth_bin_energy(x, SYNTH_ALIAS_FRAMES, k * 5000.0, SYNTH_ALIAS_RATE);
    }

    box_audio_core_free(core);

    return (total - harmonics - mean * mean * SYNTH_ALIAS_FRAMES) / total;
}

void Synth_alias_saw(void) {
    // audio/synth
    double alias = synth_alias(BOX_AUDIO_SYNTH_SAW);
    printf("saw at 5 kHz: %.2f dB outside its harmonics\n", 10 * log10(alias));
    test_assert(alias >= 0);
    test_assert(alias < 0.01);
}

void Synth_alias_square(void) {
    // audio/synth
    double alias = synth_alias(BOX_AUDIO_SYNTH_SQUARE);
    printf("square at 5 kHz: %.2f dB outside its harmonics\n", 10 * log10(alias));
    test_assert(alias >= 0);
    test_assert(alias < 0.01);
}

void Synth_alias_triangle(void) {
    // audio/synth
    double alias = synth_alias(BOX_AUDIO_SYNTH_TRIANGLE);
    printf("triangle at 5 kHz: %.2f dB outside its harmonics\n", 10 * log10(alias));
    test_assert(alias >= 0);
    test_assert(alias < 0.003);
}

/** Render a noise patch with a seed in a new core.
 *
 * @param seed The seed
 * @param out Receives frames samples
 * @param frames Number of frames, at most AUDIO_FRAMES
 */
static void synth_noise(
    uint32_t seed,
    float *out,
    int32_t frames)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_NOISE, 0, 0.5f, 1.0f);
    p.seed = seed;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, frames);
    memcpy(out, audio_l, (size_t)frames * sizeof(float));
    box_audio_core_free(core);
}

void Synth_noise_flat(void) {
    // audio/synth
    enum { N = 1024, BANDS = 8, BLOCKS = 40 };
    double band[BANDS] = {0}, mean = 0;
    int32_t b, k, i, j;
    synth_noise(7, audio_sound_a, AUDIO_FRAMES);
    for (b = 0; b < BLOCKS; b ++) {
        const float *x = audio_sound_a + 1024 + b * N;
        for (k = 1; k < N / 2; k ++) {
            double re = 0, im = 0;
            for (i = 0; i < N; i ++) {
                double a = 2 * AUDIO_PI * k * i / N;
                re += x[i] * cos(a);
                im -= x[i] * sin(a);
            }

            j = k * BANDS / (N / 2);
            band[j] += re * re + im * im;
        }
    }

    for (j = 0; j < BANDS; j ++) {
        mean += band[j] / BANDS;
    }

    for (j = 0; j < BANDS; j ++) {
        printf("band %d: %.3f of the mean\n", j, band[j] / mean);
        test_assert(fabs(band[j] / mean - 1) < 0.15);
    }

    test_assert(audio_peak(audio_sound_a, 1024, AUDIO_FRAMES) <= 0.5f);
    test_assert(audio_peak(audio_sound_a, 1024, AUDIO_FRAMES) > 0.4f);
}

/** The next play of the same patch takes the next count of plays. */
void Synth_noise_seed_differs(void) {
    // audio/synth
    box_audio_core_t *core;
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_NOISE, 0, 0.5f, 1.0f);
    synth_noise(1, audio_sound_a, 4800);
    synth_noise(2, audio_sound_b, 4800);
    test_assert(memcmp(audio_sound_a, audio_sound_b, 4800 * sizeof(float)) != 0);

    core = synth_core(AUDIO_RATE);
    p.seed = 1;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    test_assert(memcmp(audio_l, audio_sound_a, 4800 * sizeof(float)) == 0);
    audio_play(core, 2, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    box_audio_core_free(core);
    test_assert(memcmp(audio_l, audio_sound_a, 4800 * sizeof(float)) != 0);
}

void Synth_noise_seed_identical(void) {
    // audio/synth
    synth_noise(1234, audio_sound_a, AUDIO_FRAMES);
    synth_noise(1234, audio_sound_b, AUDIO_FRAMES);
    test_assert(memcmp(audio_sound_a, audio_sound_b, AUDIO_FRAMES * sizeof(float)) == 0);
}

void Synth_envelope_segments(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0);
    int32_t attack = 480, decay = 960, hold = 4800, release = 2400, at, i;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.02f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 12000);

    at = synth_first(audio_l, 0, 12000, 0.5f);
    test_assert(abs(at - (attack - 1)) <= SYNTH_MS);
    at = synth_first(audio_l, at, 12000, 0.25f);
    test_assert(abs(at - (attack + decay - 1)) <= SYNTH_MS);
    for (i = attack + decay + SYNTH_MS; i < attack + decay + hold - SYNTH_MS; i ++) {
        test_assert(fabsf(audio_l[i] - 0.25f) < 1e-5f);
    }

    at = synth_last(audio_l, 0, 12000);
    test_assert(abs(at - (attack + decay + hold + release - 1)) <= SYNTH_MS);
    test_flt(audio_peak(audio_l, at + 1, 12000), 0);
    test_assert(audio_max_step(audio_l, 0, 12000) < 0.01f);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);
    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);
}

/** The level of the linear envelope as the core made it before curves: a float step per
 * frame, the target on the last frame of a segment.
 *
 * @param levels Receives the level of each frame
 * @param frames Frames of the attack, the decay, the hold and the release
 * @param sustain Sustain level
 * @param count Frames to fill, silent past the release
 */
static void synth_linear_levels(
    float *levels,
    const int32_t *frames,
    float sustain,
    int32_t count)
{
    const float targets[4] = { 1.0f, sustain, sustain, 0.0f };
    float value = 0, step;
    int32_t at = 0, s, k;
    for (s = 0; s < 4; s ++) {
        step = (targets[s] - value) / (float)frames[s];
        for (k = frames[s] - 1; k >= 0 && at < count; k --) {
            value = k ? value + step : targets[s];
            levels[at ++] = value;
        }
    }

    while (at < count) {
        levels[at ++] = 0;
    }
}

/** Share of the way a segment of a curve has gone at its middle.
 *
 * @param curve The curve
 * @return 0.5 for a linear segment, more for a positive curve
 */
static double synth_curve_mid(
    double curve)
{
    return curve == 0 ? 0.5 : 1 / (1 + exp(-curve / 2));
}

/** Curves of 0, and curves too small to bend a segment, render the linear envelope bit
 * identical: the old recurrence, a square of 1 Hz at 0.5 through its first half. */
void Synth_envelope_linear_identical(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0);
    const int32_t frames[4] = { 480, 960, 4800, 2400 };
    int32_t i;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.02f, .sustain = 0.5f, .hold = 0.1f, .release = 0.05f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 12000);
    synth_linear_levels(audio_sound_b, frames, 0.5f, 12000);
    for (i = 1; i < 12000; i ++) {
        test_assert(audio_l[i] == 0.5f * audio_sound_b[i]);
    }

    memcpy(audio_sound_a, audio_l, 12000 * sizeof(float));
    p.envelope.curve = BOX_AUDIO_SYNTH_CURVE_MIN * 0.5f;
    p.envelope.attack_curve = -BOX_AUDIO_SYNTH_CURVE_MIN * 0.5f;
    synth_define(core, 2, &p);
    audio_play(core, 2, 2, 0, 1, 0, 1, 0);
    audio_render(core, 0, 12000);
    test_assert(memcmp(audio_l, audio_sound_a, 12000 * sizeof(float)) == 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 2);
    box_audio_core_free(core);
}

/** A curved decay is below the linear one at its middle by what its curve predicts, falls
 * without a step and reaches the sustain level at the end of the decay. */
void Synth_envelope_curved_decay(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0);
    const float sustain = 0.2f, curve = 4;
    const int32_t attack = 480, decay = 4800, hold = 4800;
    int32_t mid = attack + decay / 2 - 1, end = attack + decay - 1, i;
    double linear, curved;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.01f, .decay = 0.1f,
        .sustain = sustain, .hold = 0.1f, .release = 0.05f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 15000);
    memcpy(audio_sound_a, audio_l, 15000 * sizeof(float));

    p.envelope.curve = curve;
    synth_define(core, 2, &p);
    audio_play(core, 2, 2, 0, 1, 0, 1, 0);
    audio_render(core, 0, 15000);

    linear = 0.5 * (1 - (1 - sustain) * synth_curve_mid(0));
    curved = 0.5 * (1 - (1 - sustain) * synth_curve_mid(curve));
    test_assert(fabs(audio_sound_a[mid] - linear) < 1e-4);
    test_assert(fabs(audio_l[mid] - curved) < 1e-4);
    test_assert(audio_l[mid] < audio_sound_a[mid] - 0.1f);

    /* The attack is still linear, the decay falls all the way and lands on the sustain */
    for (i = 1; i < attack; i ++) {
        test_assert(audio_l[i] == audio_sound_a[i]);
    }

    for (i = attack; i < end; i ++) {
        test_assert(audio_l[i + 1] <= audio_l[i]);
        test_assert(audio_l[i] >= 0.5f * sustain - 1e-6f);
    }

    test_assert(audio_l[end] == 0.5f * sustain);
    for (i = end; i < end + hold; i ++) {
        test_assert(fabsf(audio_l[i] - 0.5f * sustain) < 1e-6f);
    }

    test_assert(audio_max_step(audio_l, 1, 15000) < 0.01f);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 2);
    box_audio_core_free(core);
}

/** A curved release, and a curved attack, still end the voice at the end of its release. */
void Synth_envelope_curved_release_ends(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0);
    const float curve = 6;
    const int32_t attack = 480, decay = 960, hold = 2400, release = 4800;
    int32_t start = attack + decay + hold, end = start + release - 1, at;
    double expected;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.01f, .decay = 0.02f,
        .sustain = 0.5f, .hold = 0.05f, .release = 0.1f, .curve = curve,
        .attack_curve = 3 };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, end);
    test_int(box_audio_core_voice_count(core), 1);
    audio_render(core, end, 12000 - end);

    at = synth_last(audio_l, 0, 12000);
    test_int(at, end - 1);
    test_flt(audio_peak(audio_l, end, 12000), 0);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);

    /* Fast at first: at the middle of the release under half of the linear level */
    expected = 0.25 * (1 - synth_curve_mid(curve));
    test_assert(fabs(audio_l[start + release / 2 - 1] - expected) < 1e-4);
    test_assert(audio_l[start + release / 2 - 1] < 0.125f * 0.5f);

    /* A curved attack is above the linear one at its middle */
    test_assert(fabs(audio_l[attack / 2 - 1] - 0.5 * synth_curve_mid(3)) < 1e-4);
    test_assert(audio_max_step(audio_l, 1, 12000) < 0.02f);
    box_audio_core_free(core);
}

/** The core clamps curves that are NaN or too large either way: the voice stays finite and
 * still ends on time. */
void Synth_envelope_curve_clamped(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 200, 0.5f, 0);
    float bad[3] = { NAN, 1e9f, -1e9f };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        p.envelope = (box_audio_synth_envelope_t){ .attack = 0.005f, .decay = 0.02f,
            .sustain = 0.5f, .hold = 0.02f, .release = 0.03f, .curve = bad[i],
            .attack_curve = bad[i] };
        synth_define(core, 1 + i, &p);
        audio_play(core, 1 + i, 1 + i, 0, 1, 0, 1, 0);
        audio_render(core, 0, 4800);
        test_assert(synth_finite(audio_l, 0, 4800));
        test_assert(audio_peak(audio_l, 0, 4800) <= 0.5f + 1e-4f);
        test_int(box_audio_core_voice_count(core), 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 3);
    box_audio_core_free(core);
}

/** No attack and no release: the edges keep both ends from jumping. */
void Synth_envelope_no_click(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 100, 0.5f, 0);
    int32_t last;

    p.envelope = (box_audio_synth_envelope_t){ .sustain = 1, .hold = 0.05f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    last = synth_last(audio_l, 0, 4800);
    test_assert(last > 2400 && last < 2400 + SYNTH_MS);
    test_assert(fabsf(audio_l[0]) < 0.03f);
    test_assert(fabsf(audio_l[last]) < 0.03f);
    test_assert(audio_max_step(audio_l, 0, 4800) < 0.03f);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);
    box_audio_core_free(core);
}

/** Every slot is free again: 64 more plays steal nothing. */
void Synth_envelope_frees_slot(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SAW, 300, 0.01f, 0);
    uint32_t i;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.001f, .decay = 0.03f };
    synth_define(core, 1, &p);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        audio_play(core, i, 1, 0, 1, 0, 1, 0);
    }

    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    audio_render(core, 0, 2400);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), BOX_AUDIO_VOICE_MAX);

    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        audio_play(core, 100 + i, 1, 0, 1, 0, 1, 0);
    }

    audio_render(core, 0, 128);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, NULL), 0);
    box_audio_core_free(core);
}

void Synth_one_shot_no_release_ends(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_TRIANGLE, 220, 0.5f, 0);
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.01f, .sustain = 1, .hold = 0.1f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    test_int(box_audio_core_voice_count(core), 1);
    audio_render(core, 0, 1200);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);
    box_audio_core_free(core);
}

void Synth_bounded_length(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 50, 0.1f, 1e9f);
    int32_t second;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    for (second = 0; second < (int32_t)BOX_AUDIO_SYNTH_LENGTH_MAX - 1; second ++) {
        audio_render(core, 0, AUDIO_FRAMES);
    }

    test_int(box_audio_core_voice_count(core), 1);
    audio_render(core, 0, AUDIO_FRAMES);
    audio_render(core, 0, AUDIO_FRAMES);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);
    box_audio_core_free(core);
}

/** Gain of the filter of a patch for a sine.
 *
 * @param kind A box_audio_synth_filter_t
 * @param cutoff Hz
 * @param resonance 0..1
 * @param frequency Hz of the sine, a whole number of periods in 9600 frames
 * @return Amplitude out over amplitude in
 */
static double synth_filter_gain(
    uint32_t kind,
    float cutoff,
    float resonance,
    float frequency)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, frequency, 0.25f, 1.0f);
    double result;
    p.filter = kind;
    p.cutoff = cutoff;
    p.resonance = resonance;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 19200);
    result = synth_amplitude(audio_l, 9600, 19200) / 0.25;
    box_audio_core_free(core);

    return result;
}

void Synth_filter_lowpass(void) {
    // audio/synth
    double pass = synth_filter_gain(BOX_AUDIO_SYNTH_LOWPASS, 1000, 0, 250);
    double cut = synth_filter_gain(BOX_AUDIO_SYNTH_LOWPASS, 1000, 0, 1000);
    double octave = synth_filter_gain(BOX_AUDIO_SYNTH_LOWPASS, 1000, 0, 2000);
    printf("lowpass at 1 kHz: %.2f dB at 250 Hz, %.2f dB at 1 kHz, %.2f dB at 2 kHz\n",
        20 * log10(pass), 20 * log10(cut), 20 * log10(octave));
    test_assert(pass > 0.95 && pass < 1.05);
    test_assert(fabs(cut - 0.7071) < 0.05);
    test_assert(octave > 0.18 && octave < 0.3);
}

void Synth_filter_highpass(void) {
    // audio/synth
    double pass = synth_filter_gain(BOX_AUDIO_SYNTH_HIGHPASS, 1000, 0, 4000);
    double cut = synth_filter_gain(BOX_AUDIO_SYNTH_HIGHPASS, 1000, 0, 1000);
    double octave = synth_filter_gain(BOX_AUDIO_SYNTH_HIGHPASS, 1000, 0, 500);
    printf("highpass at 1 kHz: %.2f dB at 4 kHz, %.2f dB at 1 kHz, %.2f dB at 500 Hz\n",
        20 * log10(pass), 20 * log10(cut), 20 * log10(octave));
    test_assert(pass > 0.95 && pass < 1.05);
    test_assert(fabs(cut - 0.7071) < 0.05);
    test_assert(octave > 0.18 && octave < 0.3);
}

void Synth_filter_bandpass(void) {
    // audio/synth
    double center = synth_filter_gain(BOX_AUDIO_SYNTH_BANDPASS, 1000, 0.5f, 1000);
    double above = synth_filter_gain(BOX_AUDIO_SYNTH_BANDPASS, 1000, 0.5f, 2000);
    double below = synth_filter_gain(BOX_AUDIO_SYNTH_BANDPASS, 1000, 0.5f, 500);
    printf("bandpass at 1 kHz: %.2f dB at 1 kHz, %.2f dB at 2 kHz, %.2f dB at 500 Hz\n",
        20 * log10(center), 20 * log10(above), 20 * log10(below));
    test_assert(center > 0.95 && center < 1.05);
    test_assert(above < 0.5);
    test_assert(below < 0.5);
}

/** Render a quiet saw and noise through a filter at full resonance whose cutoff a fast filter
 * envelope moves, and check that it stays stable: a filter that blew up would reach the limiter.
 *
 * @param kind A box_audio_synth_filter_t
 * @param slope A box_audio_synth_slope_t
 * @param cutoff Cutoff of the patch in Hz
 * @param amount Octaves of the filter envelope
 */
static void synth_check_envelope(
    uint32_t kind,
    uint32_t slope,
    float cutoff,
    float amount)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SAW, 110, 0.002f, 0.5f);
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.002f };
    p.filter = kind;
    p.slope = slope;
    p.cutoff = cutoff;
    p.resonance = 1;
    p.filter_amount = amount;
    p.filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.002f, .decay = 0.01f, .sustain = 0.5f, .hold = 0.02f, .release = 0.005f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) < 0.5f);
    box_audio_core_free(core);
}

/** Every kind at both slopes, with a filter envelope that opens past Nyquist, one that closes
 * from it and a cutoff at and above Nyquist; the loop restarts the envelope every 0.5 s. */
void Synth_filter_envelope_stable(void) {
    // audio/synth
    uint32_t kind, slope;
    for (slope = BOX_AUDIO_SYNTH_SLOPE_12; slope <= BOX_AUDIO_SYNTH_SLOPE_24; slope ++) {
        for (kind = BOX_AUDIO_SYNTH_LOWPASS; kind <= BOX_AUDIO_SYNTH_BANDPASS; kind ++) {
            synth_check_envelope(kind, slope, 20, 10);
            synth_check_envelope(kind, slope, 24000, -10);
            synth_check_envelope(kind, slope, AUDIO_RATE / 2, 3);
            synth_check_envelope(kind, slope, AUDIO_RATE, -1);
        }
    }
}

/** Frequency of the saw of synth_render_envelope in Hz: a period of 400 frames */
#define SYNTH_ENVELOPE_SAW 120.0

/** Energy of the harmonics of the saw of synth_render_envelope above a frequency in a range,
 * each measured on its exact bin.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame, a whole number of periods of the saw after from
 * @param above Hz the harmonics are above
 * @return The mean square of the harmonics per frame
 */
static double synth_bright(
    const float *x,
    int32_t from,
    int32_t to,
    double above)
{
    double sum = 0, f;
    for (f = SYNTH_ENVELOPE_SAW; f < AUDIO_RATE / 2; f += SYNTH_ENVELOPE_SAW) {
        if (f > above) {
            sum += synth_bin_energy(x + from, to - from, f, AUDIO_RATE);
        }
    }

    return sum / (to - from);
}

/** Render a steady saw of SYNTH_ENVELOPE_SAW Hz through a lowpass with a filter envelope.
 *
 * @param cutoff Cutoff of the patch in Hz
 * @param amount Octaves of the filter envelope
 * @param env The filter envelope
 */
static void synth_render_envelope(
    float cutoff,
    float amount,
    box_audio_synth_envelope_t env)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SAW, SYNTH_ENVELOPE_SAW, 0.25f,
        0.6f);
    p.envelope.attack = 0.001f;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = cutoff;
    p.filter_amount = amount;
    p.filter_envelope = env;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES / 2));
    box_audio_core_free(core);
}

/** A saw through a lowpass at 200 Hz whose envelope opens it 4 octaves: attack 0 to 4800,
 * decay to 9600, hold at half (800 Hz) to 14400, release to 19200, then back at 200 Hz. The
 * energy of the harmonics above 1 kHz follows the envelope. */
void Synth_filter_envelope_follows(void) {
    // audio/synth
    double start, peak, sustain, release, end;
    synth_render_envelope(200, 4, (box_audio_synth_envelope_t){
        .attack = 0.1f, .decay = 0.1f, .sustain = 0.5f, .hold = 0.1f, .release = 0.1f });
    start = synth_bright(audio_l, 600, 1800, 1000);
    peak = synth_bright(audio_l, 3600, 4800, 1000);
    sustain = synth_bright(audio_l, 10800, 13200, 1000);
    release = synth_bright(audio_l, 16800, 18000, 1000);
    end = synth_bright(audio_l, 20400, 22800, 1000);
    printf("filter envelope: start %.3g, peak %.3g, sustain %.3g, release %.3g, end %.3g\n",
        start, peak, sustain, release, end);
    test_assert(peak > 4 * start);
    test_assert(peak > 2 * sustain);
    test_assert(sustain > 2 * end);
    test_assert(release < sustain);
    test_assert(release > end);
    test_assert(end < 2 * start);
}

/** A lowpass at 4 kHz that an amount of -4 octaves closes to 250 Hz on its sustain, and that is
 * open again once its envelope ended. */
void Synth_filter_envelope_negative(void) {
    // audio/synth
    box_audio_synth_envelope_t env = {
        .attack = 0.02f, .sustain = 1, .hold = 0.2f, .release = 0.02f };
    double open_sustain, open_end, closed_sustain, closed_end;
    synth_render_envelope(4000, 0, env);
    open_sustain = synth_bright(audio_l, 4800, 9600, 1000);
    open_end = synth_bright(audio_l, 16800, 21600, 1000);
    synth_render_envelope(4000, -4, env);
    closed_sustain = synth_bright(audio_l, 4800, 9600, 1000);
    closed_end = synth_bright(audio_l, 16800, 21600, 1000);
    printf("negative amount: %.3g open, %.3g closed, %.3g and %.3g after\n",
        open_sustain, closed_sustain, open_end, closed_end);
    test_assert(closed_sustain < open_sustain / 10);
    test_assert(closed_end > open_end * 0.8 && closed_end < open_end * 1.25);
}

/** Gain of the filter of a patch for a sine, at a slope.
 *
 * @param kind A box_audio_synth_filter_t
 * @param slope A box_audio_synth_slope_t
 * @param cutoff Hz
 * @param frequency Hz of the sine, a whole number of periods in 9600 frames
 * @return Amplitude out over amplitude in
 */
static double synth_slope_gain(
    uint32_t kind,
    uint32_t slope,
    float cutoff,
    float frequency)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, frequency, 0.25f, 1.0f);
    double result;
    p.filter = kind;
    p.slope = slope;
    p.cutoff = cutoff;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 19200);
    result = synth_amplitude(audio_l, 9600, 19200) / 0.25;
    box_audio_core_free(core);

    return result;
}

/** The 24 dB filter attenuates an octave past its cutoff about twice as much in dB as the 12 dB
 * one, and passes its passband as the 12 dB one does. */
void Synth_filter_slope_24(void) {
    // audio/synth
    double low12 = 20 * log10(synth_slope_gain(BOX_AUDIO_SYNTH_LOWPASS,
        BOX_AUDIO_SYNTH_SLOPE_12, 1000, 2000));
    double low24 = 20 * log10(synth_slope_gain(BOX_AUDIO_SYNTH_LOWPASS,
        BOX_AUDIO_SYNTH_SLOPE_24, 1000, 2000));
    double high12 = 20 * log10(synth_slope_gain(BOX_AUDIO_SYNTH_HIGHPASS,
        BOX_AUDIO_SYNTH_SLOPE_12, 1000, 500));
    double high24 = 20 * log10(synth_slope_gain(BOX_AUDIO_SYNTH_HIGHPASS,
        BOX_AUDIO_SYNTH_SLOPE_24, 1000, 500));
    double pass = synth_slope_gain(BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_24, 1000, 250);
    double cut = synth_slope_gain(BOX_AUDIO_SYNTH_LOWPASS, BOX_AUDIO_SYNTH_SLOPE_24, 1000, 1000);
    printf("an octave past 1 kHz: lowpass %.2f dB at 12, %.2f dB at 24; highpass %.2f dB at 12, "
        "%.2f dB at 24; 24 dB lowpass %.2f dB at 250 Hz, %.2f dB at 1 kHz\n",
        low12, low24, high12, high24, 20 * log10(pass), 20 * log10(cut));
    test_assert(low24 / low12 > 1.7 && low24 / low12 < 2.3);
    test_assert(high24 / high12 > 1.7 && high24 / high12 < 2.3);
    test_assert(pass > 0.95 && pass < 1.05);
    test_assert(fabs(cut - 0.7071) < 0.05);
}

void Synth_filter_extreme(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p;
    const float values[] = { NAN, INFINITY, -INFINITY, -1, 0, 1e30f, 24000, 1e-30f };
    int32_t i, k;
    for (i = 0; i < 8; i ++) {
        float v = values[i];
        memset(&p, 0, sizeof(p));
        p.layers = 99;
        p.pitch_variance = v;
        p.level_variance = v;
        p.envelope = (box_audio_synth_envelope_t){ v, v, v, v, v };
        p.filter = 1 + (uint32_t)i % 3;
        p.cutoff = v;
        p.resonance = v;
        p.slope = (uint32_t)i % 3;
        p.filter_amount = v;
        p.filter_envelope = (box_audio_synth_envelope_t){ v, v, v, v, v };
        for (k = 0; k < BOX_AUDIO_SYNTH_LAYERS; k ++) {
            p.layer[k] = (box_audio_synth_layer_t){
                .wave = (uint32_t)(k + i), .has_envelope = (uint32_t)k % 2,
                .frequency = v, .frequency_end = v, .sweep = v, .level = v,
                .envelope = { v, v, v, v, v } };
        }

        synth_define(core, 1 + (uint32_t)i, &p);
        audio_play(core, 1 + (uint32_t)i, 1 + (uint32_t)i, 0, 0.1f, 0, 1, 0);
    }

    p = synth_steady(BOX_AUDIO_SYNTH_SAW, 20000, 0.1f, 1);
    p.filter = BOX_AUDIO_SYNTH_HIGHPASS;
    p.cutoff = AUDIO_RATE / 2;
    p.resonance = 1;
    synth_define(core, 20, &p);
    audio_play(core, 20, 20, 0, 1, 0, 16, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(synth_finite(audio_r, 0, AUDIO_FRAMES));
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    box_audio_core_free(core);
}

void Synth_layers_sum(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 1, 0.2f, 0.4f);
    p.layers = 3;
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 1, .level = 0.3f };
    p.layer[2] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 1, .level = 0.15f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    test_assert(fabsf(audio_l[2400] - 0.65f) < 1e-5f);
    test_assert(fabsf(audio_l[4799] - 0.65f) < 1e-5f);
    box_audio_core_free(core);
}

void Synth_layer_own_envelope(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 1, 0.2f, 0.4f);
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 1, .level = 0.3f, .has_envelope = 1,
        .envelope = { .sustain = 1, .hold = 0.1f } };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_assert(fabsf(audio_l[2400] - 0.5f) < 1e-5f);
    test_assert(fabsf(audio_l[9600] - 0.2f) < 1e-5f);
    test_assert(fabsf(audio_l[19000] - 0.2f) < 1e-5f);
    test_assert(audio_max_step(audio_l, 1, AUDIO_FRAMES / 2) < 0.05f);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 1);
    box_audio_core_free(core);
}

void Synth_pitch_variance(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 1000, 0.5f, 0.2f);
    double f[4];
    int32_t i;
    bool differ = false;
    p.pitch_variance = 0.05f;
    synth_define(core, 1, &p);
    for (i = 0; i < 4; i ++) {
        audio_play(core, (uint32_t)i, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 9600);
        f[i] = audio_frequency(audio_l, 480, 9000);
        test_assert(f[i] >= 949 && f[i] <= 1051);
        differ = differ || (i && fabs(f[i] - f[0]) > 0.5);
    }

    test_assert(differ);
    box_audio_core_free(core);
}

void Synth_voice_pitch(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SAW, 300, 0.5f, 1.0f);
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 2, 0);
    audio_render(core, 0, 9600);
    test_assert(fabs(audio_frequency(audio_l, 480, 9600) - 600) < 0.6);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 0.5f);
    audio_render(core, 0, 9600);
    test_assert(fabs(audio_frequency(audio_l, 480, 9600) - 150) < 0.3);
    box_audio_core_free(core);
}

/** Each pass of 51 ms starts loud again. */
void Synth_loop_restarts(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 1000, 0.5f, 0);
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.001f, .decay = 0.05f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    test_int(box_audio_core_voice_count(core), 1);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 0);

    test_assert(audio_peak(audio_l, 2448 * 4, 2448 * 4 + 200) > 0.3f);
    test_assert(audio_peak(audio_l, 2448 * 4 - 100, 2448 * 4) < 0.05f);
    test_assert(audio_max_step(audio_l, 0, AUDIO_FRAMES / 2) < 0.2f);
    box_audio_core_free(core);
}

/** A sound id that is a synth takes no frames, and frames take no synth.
 * A release frees the id at once; the voice that plays keeps its copy.
 * The id takes frames again.
 */
void Synth_release_patch(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 440, 0.5f, 0.5f);
    box_audio_event_t e;
    uint8_t bytes[64];
    box_audio_writer_t w;
    synth_define(core, 3, &p);

    audio_fill(audio_sound_a, 100, 0.1f);
    test_assert(!box_audio_core_sound(core, 3, audio_sound_a, 100, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 4, audio_sound_a, 100, 1, AUDIO_RATE));
    synth_define(core, 4, &p);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, &e), 1);
    test_int(e.id, BOX_AUDIO_CMD_SYNTH);

    audio_play(core, 1, 3, 0, 1, 0, 1, 0);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_release(&w, 3));
    box_audio_core_apply(core, w.data, w.size);
    test_int(audio_events(core, BOX_AUDIO_EVENT_SOUND_FREE, &e), 1);
    test_int(e.id, 3);
    audio_render(core, 0, 9600);
    test_assert(fabs(audio_frequency(audio_l, 480, 9600) - 440) < 0.5);

    test_assert(box_audio_core_sound(core, 3, audio_sound_a, 100, 1, AUDIO_RATE));
    box_audio_core_free(core);
}

/** Bessel function of the first kind, from its series, for the small orders and arguments of
 * the FM checks.
 *
 * @param n Order, may be negative
 * @param x Argument, 0..10
 * @return J_n(x)
 */
static double synth_bessel(
    int32_t n,
    double x)
{
    double sum = 0, term = 1, sign = 1;
    int32_t k, order = n < 0 ? -n : n;
    for (k = 1; k <= order; k ++) {
        term *= x / 2 / k;
    }

    for (k = 0; k < 40; k ++) {
        sum += term;
        term *= -(x / 2) * (x / 2) / ((k + 1) * (double)(k + 1 + order));
    }

    if (n < 0 && order % 2) {
        sign = -1;
    }

    return sign * sum;
}

/** Amplitude of the component of a range at a frequency of an exact bin.
 *
 * @param x Samples
 * @param n Number of samples
 * @param frequency Hz, a whole number of periods in n samples
 * @param rate Frames per second
 * @return The amplitude
 */
static double synth_bin_amplitude(
    const float *x,
    int32_t n,
    double frequency,
    double rate)
{
    return sqrt(2 * synth_bin_energy(x, n, frequency, rate) / n);
}

/** Sum of the squares of a range.
 *
 * @param x Samples
 * @param n Number of samples
 * @return The energy
 */
static double synth_energy(
    const float *x,
    int32_t n)
{
    double result = 0;
    int32_t i;
    for (i = 0; i < n; i ++) {
        result += (double)x[i] * x[i];
    }

    return result;
}

/** A steady sine carrier with a sine that modulates it from the second layer.
 *
 * @param carrier Hz of the carrier
 * @param modulator Hz of the modulator
 * @param modulation A box_audio_synth_modulation_t
 * @param index Index in radians, or depth of ring modulation
 * @return The patch, with the carrier at 0.5
 */
static box_audio_synth_patch_t synth_pair(
    float carrier,
    float modulator,
    uint32_t modulation,
    float index)
{
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, carrier, 0.5f, 1.0f);
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = modulator, .level = 1, .modulates = 1,
        .modulation = modulation, .index = index };

    return p;
}

/** Render a patch for a second at AUDIO_RATE into audio_l.
 *
 * @param p The patch
 */
static void synth_render_patch(
    const box_audio_synth_patch_t *p)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    synth_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
}

/** Phase modulation at 1:1: the carrier and the modulator at 1 kHz with an index of 2 make the
 * harmonics of 1 kHz, the sidebands below 0 Hz folded onto them with their sign, so harmonic h
 * has J(h - 1) + (-1)^h J(h + 1). The tone repeats every period: the pitch does not drift. */
void Synth_fm_bessel(void) {
    // audio/synth
    enum { FROM = 2400, N = 9600, PERIOD = 48 };
    const double index = 2;
    box_audio_synth_patch_t p = synth_pair(1000, 1000, BOX_AUDIO_SYNTH_PHASE, (float)index);
    const float *x = audio_l + FROM;
    double total, harmonics = 0, a, expected;
    int32_t h, i;
    synth_render_patch(&p);
    total = synth_energy(x, N);
    for (h = 1; h <= 10; h ++) {
        a = synth_bin_amplitude(x, N, 1000.0 * h, AUDIO_RATE) / 0.5;
        expected = fabs(synth_bessel(h - 1, index) + (h % 2 ? -1 : 1) *
            synth_bessel(h + 1, index));
        printf("harmonic %d: %.4f, Bessel %.4f\n", h, a, expected);
        test_assert(fabs(a - expected) < 0.005);
        harmonics += synth_bin_energy(x, N, 1000.0 * h, AUDIO_RATE);
    }

    test_assert(harmonics / total > 0.999);
    for (i = 0; i < N - PERIOD; i ++) {
        test_assert(fabsf(x[i + PERIOD] - x[i]) < 1e-4f);
    }
}

/** Phase modulation of a 6 kHz carrier by 1 kHz with an index of 1.5: sidebands at 6 kHz plus
 * and minus multiples of 1 kHz, each with |J(k)|, and nothing else. */
void Synth_fm_sidebands(void) {
    // audio/synth
    enum { FROM = 2400, N = 9600 };
    const double index = 1.5;
    box_audio_synth_patch_t p = synth_pair(6000, 1000, BOX_AUDIO_SYNTH_PHASE, (float)index);
    const float *x = audio_l + FROM;
    double total, sidebands = 0, a, expected;
    int32_t k;
    synth_render_patch(&p);
    total = synth_energy(x, N);
    for (k = -5; k <= 5; k ++) {
        a = synth_bin_amplitude(x, N, 6000.0 + 1000.0 * k, AUDIO_RATE) / 0.5;
        expected = fabs(synth_bessel(k, index));
        printf("sideband %d: %.4f, Bessel %.4f\n", k, a, expected);
        test_assert(fabs(a - expected) < 0.005);
        sidebands += synth_bin_energy(x, N, 6000.0 + 1000.0 * k, AUDIO_RATE);
    }

    test_assert(sidebands / total > 0.999);
}

/** A modulator with an index of 0 leaves its carrier bit identical to the carrier alone. */
void Synth_fm_index_zero_identical(void) {
    // audio/synth
    box_audio_synth_patch_t alone = synth_steady(BOX_AUDIO_SYNTH_SINE, 440, 0.5f, 1.0f);
    box_audio_synth_patch_t pair = synth_pair(440, 660, BOX_AUDIO_SYNTH_PHASE, 0);
    synth_render_patch(&alone);
    memcpy(audio_sound_a, audio_l, AUDIO_FRAMES * sizeof(float));
    synth_render_patch(&pair);
    test_assert(memcmp(audio_sound_a, audio_l, AUDIO_FRAMES * sizeof(float)) == 0);
}

/** The index follows the envelope of the modulator, a decay of 0.5 s from an index of 3: half
 * way the sidebands are those of an index of 1.5, the high ones fade, and once the envelope
 * ended the carrier is a pure sine. */
void Synth_fm_index_follows_envelope(void) {
    // audio/synth
    enum { N = 960, ATTACK = 48, DECAY = 24000 };
    box_audio_synth_patch_t p = synth_pair(500, 500, BOX_AUDIO_SYNTH_PHASE, 3);
    double high[3] = {0}, a, expected, total, fundamental;
    const int32_t from[3] = { ATTACK, ATTACK + DECAY / 2 - N / 2, ATTACK + DECAY * 3 / 4 };
    int32_t h, w;
    p.envelope.hold = 1.5f;
    p.layer[1].has_envelope = 1;
    p.layer[1].envelope = (box_audio_synth_envelope_t){ .attack = 0.001f, .decay = 0.5f };
    synth_render_patch(&p);
    for (w = 0; w < 3; w ++) {
        for (h = 4; h <= 10; h ++) {
            high[w] += synth_bin_energy(audio_l + from[w], N, 500.0 * h, AUDIO_RATE);
        }

        printf("window %d: %.3e above the third harmonic\n", w, high[w]);
    }

    for (h = 1; h <= 4; h ++) {
        a = synth_bin_amplitude(audio_l + from[1], N, 500.0 * h, AUDIO_RATE) / 0.5;
        expected = fabs(synth_bessel(h - 1, 1.5) + (h % 2 ? -1 : 1) * synth_bessel(h + 1, 1.5));
        printf("half way, harmonic %d: %.4f, Bessel %.4f\n", h, a, expected);
        test_assert(fabs(a - expected) < 0.03);
    }

    test_assert(high[1] < high[0] / 10);
    test_assert(high[2] < high[1] / 10);

    total = synth_energy(audio_l + ATTACK + DECAY + N, N);
    fundamental = synth_bin_energy(audio_l + ATTACK + DECAY + N, N, 500, AUDIO_RATE);
    test_assert(fundamental / total > 0.99999);
}

/** An index of 20 on a 7 kHz carrier and modulator would put strong sidebands far past
 * Nyquist; it is limited to (20000 - 7000) / 7000 - 1, so the sidebands up to 21 kHz are
 * those of that index. */
void Synth_fm_index_limited(void) {
    // audio/synth
    enum { FROM = 2400, N = 4800 };
    const double index = (20000.0 - 7000) / 7000 - 1;
    box_audio_synth_patch_t p = synth_pair(7000, 7000, BOX_AUDIO_SYNTH_PHASE, 20);
    const float *x = audio_l + FROM;
    double a, expected;
    int32_t h;
    synth_render_patch(&p);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    for (h = 1; h <= 3; h ++) {
        a = synth_bin_amplitude(x, N, 7000.0 * h, AUDIO_RATE) / 0.5;
        expected = fabs(synth_bessel(h - 1, index) + (h % 2 ? -1 : 1) *
            synth_bessel(h + 1, index));
        printf("harmonic %d: %.4f, Bessel of %.3f %.4f\n", h, a, index, expected);
        test_assert(fabs(a - expected) < 0.01);
    }
}

/** Share of the energy of a 2 kHz carrier, phase modulated by a 500 Hz sine with an index of
 * 2, at 44100 Hz outside the harmonics of 500 Hz and DC: the aliases of its edges.
 *
 * @param wave A box_audio_synth_wave_t of the carrier
 * @return The share, 0..1
 */
static double synth_alias_fm(
    uint32_t wave)
{
    box_audio_core_t *core = synth_core(SYNTH_ALIAS_RATE);
    box_audio_synth_patch_t p = synth_pair(2000, 500, BOX_AUDIO_SYNTH_PHASE, 2);
    const float *x = audio_l + 2048;
    double total = 0, mean = 0, harmonics = 0;
    int32_t i, k;
    p.layer[0].wave = wave;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 2048 + SYNTH_ALIAS_FRAMES);
    for (i = 0; i < SYNTH_ALIAS_FRAMES; i ++) {
        total += (double)x[i] * x[i];
        mean += x[i];
    }

    mean /= SYNTH_ALIAS_FRAMES;
    for (k = 1; k * 500 < SYNTH_ALIAS_RATE / 2; k ++) {
        harmonics += synth_bin_energy(x, SYNTH_ALIAS_FRAMES, k * 500.0, SYNTH_ALIAS_RATE);
    }

    box_audio_core_free(core);

    return (total - harmonics - mean * mean * SYNTH_ALIAS_FRAMES) / total;
}

/** A saw carrier under phase modulation smooths its edges with the speed of its modulated
 * phase, so it aliases about as little as a plain saw: 0.0014, where the polyBLEP of its
 * unmodulated rate left 0.0039. */
void Synth_fm_alias_saw(void) {
    // audio/synth
    double alias = synth_alias_fm(BOX_AUDIO_SYNTH_SAW);
    printf("saw at 2 kHz by 500 Hz: %.2f dB outside its harmonics\n", 10 * log10(alias));
    test_assert(alias >= 0);
    test_assert(alias < 0.002);
}

/** A square carrier under phase modulation, as Synth_fm_alias_saw: 0.0010, where the
 * unmodulated rate left 0.0023. */
void Synth_fm_alias_square(void) {
    // audio/synth
    double alias = synth_alias_fm(BOX_AUDIO_SYNTH_SQUARE);
    printf("square at 2 kHz by 500 Hz: %.2f dB outside its harmonics\n", 10 * log10(alias));
    test_assert(alias >= 0);
    test_assert(alias < 0.0015);
}

/** Ring modulation of a 1 kHz sine by a 300 Hz sine: energy at 700 and 1300 Hz only, and none
 * at either of them. */
void Synth_ring_sum_difference(void) {
    // audio/synth
    enum { FROM = 2400, N = 4800 };
    box_audio_synth_patch_t p = synth_pair(1000, 300, BOX_AUDIO_SYNTH_RING, 1);
    const float *x = audio_l + FROM;
    double total, difference, sum;
    synth_render_patch(&p);
    total = synth_energy(x, N);
    difference = synth_bin_energy(x, N, 700, AUDIO_RATE);
    sum = synth_bin_energy(x, N, 1300, AUDIO_RATE);
    printf("700 Hz: %.4f, 1300 Hz: %.4f\n", synth_bin_amplitude(x, N, 700, AUDIO_RATE),
        synth_bin_amplitude(x, N, 1300, AUDIO_RATE));
    test_assert(fabs(synth_bin_amplitude(x, N, 700, AUDIO_RATE) - 0.25) < 0.003);
    test_assert(fabs(synth_bin_amplitude(x, N, 1300, AUDIO_RATE) - 0.25) < 0.003);
    test_assert((difference + sum) / total > 0.9999);
    test_assert(synth_bin_energy(x, N, 1000, AUDIO_RATE) / total < 1e-5);
    test_assert(synth_bin_energy(x, N, 300, AUDIO_RATE) / total < 1e-5);
}

/** A modulator that is audible is heard at its level as well; one that is not, is not. */
void Synth_modulator_audible(void) {
    // audio/synth
    enum { FROM = 2400, N = 4800 };
    box_audio_synth_patch_t p = synth_pair(1000, 300, BOX_AUDIO_SYNTH_RING, 1);
    const float *x = audio_l + FROM;
    p.layer[1].level = 0.2f;
    synth_render_patch(&p);
    test_assert(synth_bin_amplitude(x, N, 300, AUDIO_RATE) < 0.001);
    p.layer[1].audible = 1;
    synth_render_patch(&p);
    test_assert(fabs(synth_bin_amplitude(x, N, 300, AUDIO_RATE) - 0.2) < 0.003);
    test_assert(fabs(synth_bin_amplitude(x, N, 700, AUDIO_RATE) - 0.25) < 0.003);
    test_assert(fabs(synth_bin_amplitude(x, N, 1300, AUDIO_RATE) - 0.25) < 0.003);
}

/** Feedback turns a sine into a sawlike tone at the same pitch: harmonics that fall off, and
 * nothing between them. */
void Synth_feedback_harmonics(void) {
    // audio/synth
    enum { FROM = 2400, N = 9600 };
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 500, 0.5f, 1.0f);
    const float *x = audio_l + FROM;
    double e[8], total, harmonics = 0, mean = 0;
    int32_t h, i;
    p.layer[0].feedback = 1.0f;
    synth_render_patch(&p);
    total = synth_energy(x, N);
    for (i = 0; i < N; i ++) {
        mean += x[i];
    }

    mean /= N;
    for (h = 1; h <= 40; h ++) {
        double energy = synth_bin_energy(x, N, 500.0 * h, AUDIO_RATE);
        if (h <= 8) {
            e[h - 1] = energy;
            printf("harmonic %d: %.4f\n", h, synth_bin_amplitude(x, N, 500.0 * h, AUDIO_RATE));
        }

        harmonics += energy;
    }

    test_assert(e[1] > e[0] * 0.01);
    test_assert(e[2] > e[0] * 0.001);
    test_assert(e[1] < e[0] && e[2] < e[1] && e[3] < e[2]);
    printf("harmonics: %.6f of the energy\n", (harmonics + mean * mean * N) / total);
    test_assert((harmonics + mean * mean * N) / total > 0.999);
    test_assert(fabs(audio_frequency(audio_l, FROM, FROM + N) - 500) < 0.5);
}

/** A patch of BOX_AUDIO_SYNTH_LAYERS sines, each at its own frequency and level.
 *
 * @param hold Seconds at the level
 * @return The patch
 */
static box_audio_synth_patch_t synth_octet(
    float hold)
{
    static const float frequencies[BOX_AUDIO_SYNTH_LAYERS] = {
        300, 500, 700, 1100, 1300, 1700, 1900, 2300 };
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, frequencies[0], 0.04f, hold);
    int32_t k;
    p.layers = BOX_AUDIO_SYNTH_LAYERS;
    for (k = 0; k < BOX_AUDIO_SYNTH_LAYERS; k ++) {
        p.layer[k] = (box_audio_synth_layer_t){
            .wave = BOX_AUDIO_SYNTH_SINE, .frequency = frequencies[k],
            .level = 0.04f + 0.01f * (float)k };
    }

    return p;
}

/** A patch of BOX_AUDIO_SYNTH_LAYERS layers renders every one: a Goertzel filter finds each at
 * its own frequency and level, and nothing between them. */
void Synth_eight_layers(void) {
    // audio/synth
    enum { FROM = 2400, N = 24000 };
    box_audio_synth_patch_t p = synth_octet(1.0f);
    const float *x = audio_l + FROM;
    double amplitude, between;
    int32_t k;
    test_int(BOX_AUDIO_SYNTH_LAYERS, 8);
    synth_render_patch(&p);
    for (k = 0; k < BOX_AUDIO_SYNTH_LAYERS; k ++) {
        amplitude = synth_bin_amplitude(x, N, p.layer[k].frequency, AUDIO_RATE);
        printf("layer %d at %.0f Hz: %.5f, level %.2f\n", (int)k,
            (double)p.layer[k].frequency, amplitude, (double)p.layer[k].level);
        test_assert(fabs(amplitude - p.layer[k].level) < 0.002);
    }

    between = synth_bin_amplitude(x, N, 900, AUDIO_RATE) +
        synth_bin_amplitude(x, N, 1500, AUDIO_RATE) + synth_bin_amplitude(x, N, 2100, AUDIO_RATE);
    test_assert(between < 0.001);
}

/** A patch with more than BOX_AUDIO_SYNTH_LAYERS layers plays the first 8 of them. */
void Synth_layers_clamped(void) {
    // audio/synth
    enum { FROM = 2400, N = 24000 };
    box_audio_synth_patch_t p = synth_octet(1.0f);
    const float *x = audio_l + FROM;
    double amplitude;
    int32_t k;
    p.layers = BOX_AUDIO_SYNTH_LAYERS + 1;
    synth_render_patch(&p);
    for (k = 0; k < BOX_AUDIO_SYNTH_LAYERS; k ++) {
        amplitude = synth_bin_amplitude(x, N, p.layer[k].frequency, AUDIO_RATE);
        test_assert(fabs(amplitude - p.layer[k].level) < 0.002);
    }
}

/** A synth command ends after the layers of its patch, and one cut short of them is ignored. */
void Synth_layers_command_size(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, 440, 0.5f, 0.2f);
    box_audio_cmd_header_t header;
    box_audio_event_t e;
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){ .frequency = 660, .level = 0.2f };
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    printf("synth command: %d bytes for 2 layers, %d for %d\n", (int)w.size,
        (int)BOX_AUDIO_CMD_SYNTH_SIZE(BOX_AUDIO_SYNTH_LAYERS), (int)BOX_AUDIO_SYNTH_LAYERS);
    test_int(w.size, BOX_AUDIO_CMD_SYNTH_SIZE(2));
    test_assert(w.size < (int32_t)sizeof(box_audio_cmd_synth_t));
    box_audio_core_apply(core, w.data, w.size);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    test_assert(audio_peak(audio_l, 0, 4800) > 0.5f);

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 2, &p));
    memcpy(&header, bytes, sizeof(header));
    header.size = (uint16_t)BOX_AUDIO_CMD_SYNTH_SIZE(1);
    memcpy(bytes, &header, sizeof(header));
    box_audio_core_apply(core, bytes, BOX_AUDIO_CMD_SYNTH_SIZE(1));
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, &e), 1);
    test_int(e.id, BOX_AUDIO_CMD_SYNTH);
    box_audio_core_free(core);
}

/** Read every event of a core and count them by type.
 *
 * @param core The core
 * @param counts Receives the count of each type, BOX_AUDIO_EVENT_LOST + 1 of them
 * @param stolen Receives the last VOICE_STOLEN, id 0 without one
 */
static void synth_pool_events(
    box_audio_core_t *core,
    int32_t *counts,
    box_audio_event_t *stolen)
{
    box_audio_event_t events[64];
    int32_t n, i;
    memset(counts, 0, sizeof(int32_t) * (BOX_AUDIO_EVENT_LOST + 1));
    memset(stolen, 0, sizeof(*stolen));
    while ((n = box_audio_core_read_events(core, events, 64)) > 0) {
        for (i = 0; i < n; i ++) {
            if (events[i].type >= 0 && events[i].type <= BOX_AUDIO_EVENT_LOST) {
                counts[events[i].type] ++;
            }

            if (events[i].type == BOX_AUDIO_EVENT_VOICE_STOLEN) {
                *stolen = events[i];
            }
        }
    }
}

/** Voices take the state of only the layers their patch has, from a pool of 4 per voice slot:
 * every slot plays 4 layers, a play of 8 layers that finds no room steals the quietest voice,
 * the first of equals, and the layers come back when the voices end. */
void Synth_layers_pool(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t wide = synth_octet(0.05f);
    box_audio_synth_patch_t four = synth_octet(0.05f);
    const int32_t slots = BOX_AUDIO_VOICE_MAX + BOX_AUDIO_VOICE_TAIL_MAX;
    const int32_t fit = 4 * slots / BOX_AUDIO_SYNTH_LAYERS;
    int32_t counts[BOX_AUDIO_EVENT_LOST + 1];
    box_audio_event_t stolen;
    int32_t i;
    four.layers = 4;
    synth_define(core, 1, &wide);
    synth_define(core, 2, &four);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        audio_play(core, (uint32_t)i, 2, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    audio_render(core, 0, 9600);
    test_int(box_audio_core_voice_count(core), 0);
    audio_events(core, 0, NULL);

    for (i = 0; i < fit; i ++) {
        audio_play(core, 100 + (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), fit);
    audio_play(core, 1000, 1, 0, 0.01f, 0, 1, 0);
    synth_pool_events(core, counts, &stolen);
    test_int(counts[BOX_AUDIO_EVENT_IGNORED], 0);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_STOLEN], 1);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_END], 1);
    test_uint(stolen.id, 100);
    test_int(stolen.value, 1);
    test_int(box_audio_core_voice_count(core), fit);
    test_assert(box_audio_core_voice_count(core) <= BOX_AUDIO_VOICE_MAX);
    audio_render(core, 0, 9600);
    test_assert(synth_finite(audio_l, 0, 9600));
    test_int(box_audio_core_voice_count(core), 0);
    audio_events(core, 0, NULL);

    for (i = 0; i < fit; i ++) {
        audio_play(core, 2000 + (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), fit);
    box_audio_core_free(core);
}

/** A play of 8 layers that finds the pool full steals the quietest playing voice of 8 layers,
 * and ends voices that fade out before it steals one that plays, the first of equals. */
void Synth_layers_steal_quietest(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t wide = synth_octet(0.05f);
    const int32_t slots = BOX_AUDIO_VOICE_MAX + BOX_AUDIO_VOICE_TAIL_MAX;
    const int32_t fit = 4 * slots / BOX_AUDIO_SYNTH_LAYERS;
    int32_t counts[BOX_AUDIO_EVENT_LOST + 1];
    box_audio_event_t stolen, e;
    uint8_t bytes[64];
    box_audio_writer_t w;
    int32_t i;
    synth_define(core, 1, &wide);
    for (i = 0; i < fit; i ++) {
        audio_play(core, 100 + (uint32_t)i, 1, 0, i == 7 ? 0.005f : 0.02f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), fit);

    audio_play(core, 1000, 1, 0, 0.02f, 0, 1, 0);
    synth_pool_events(core, counts, &stolen);
    test_int(counts[BOX_AUDIO_EVENT_IGNORED], 0);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_STOLEN], 1);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_END], 1);
    test_uint(stolen.id, 107);
    test_int(stolen.value, 1);
    test_int(box_audio_core_voice_count(core), fit);

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 110, 0));
    test_assert(box_audio_write_stop(&w, 111, 0));
    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_voice_count(core), fit - 2);

    audio_play(core, 1001, 1, 0, 0.02f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_uint(e.id, 110);
    test_int(box_audio_core_voice_count(core), fit - 1);

    audio_play(core, 1002, 1, 0, 0.02f, 0, 1, 0);
    synth_pool_events(core, counts, &stolen);
    test_int(counts[BOX_AUDIO_EVENT_IGNORED], 0);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_STOLEN], 0);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_END], 1);
    test_int(box_audio_core_voice_count(core), fit);

    audio_render(core, 0, 9600);
    test_assert(synth_finite(audio_l, 0, 9600));
    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);
}

/** At the voice cap with the pool full, a play of 8 layers steals one voice for its parts and
 * the cap steals no other: the voice it ends leaves room under the cap. */
void Synth_layers_steal_cap(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t wide = synth_octet(0.05f);
    box_audio_synth_patch_t two = synth_octet(0.05f);
    const int32_t eights = (BOX_AUDIO_VOICE_MAX + BOX_AUDIO_VOICE_TAIL_MAX) * 4 / 8 - 8;
    const int32_t twos = BOX_AUDIO_VOICE_MAX - eights;
    int32_t counts[BOX_AUDIO_EVENT_LOST + 1];
    box_audio_event_t stolen;
    int32_t i;
    two.layers = 2;
    synth_define(core, 1, &wide);
    synth_define(core, 2, &two);
    test_int(eights * 8 + twos * 2, (BOX_AUDIO_VOICE_MAX + BOX_AUDIO_VOICE_TAIL_MAX) * 4);
    for (i = 0; i < twos; i ++) {
        audio_play(core, 100 + (uint32_t)i, 2, 0, 0.02f, 0, 1, 0);
    }

    for (i = 0; i < eights; i ++) {
        audio_play(core, 200 + (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);

    audio_play(core, 1000, 1, 0, 0.02f, 0, 1, 0);
    synth_pool_events(core, counts, &stolen);
    test_int(counts[BOX_AUDIO_EVENT_IGNORED], 0);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_STOLEN], 1);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_END], 1);
    test_uint(stolen.id, 200);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    box_audio_core_free(core);
}

/** Patches of up to 4 layers (filters, sweeps, noise, envelopes, FM, ring, a loop) render bit
 * identical whatever the layers past their count hold. The hashes depend on the compiler. */
void Synth_layers_identical(void) {
    // audio/synth
    box_audio_synth_envelope_t decay = {
        .attack = 0.002f, .decay = 0.5f, .sustain = 0.2f, .hold = 0.3f, .release = 0.1f };
    box_audio_synth_layer_t junk = { .frequency = 500, .level = 1, .modulates = 1, .index = 2 };
    box_audio_synth_patch_t p[4] = {{0}};
    uint64_t h[2];
    int32_t i, run, l;
    p[0].layers = 3;
    p[0].seed = 5;
    p[0].pitch_variance = 0.1f;
    p[0].level_variance = 0.2f;
    p[0].envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.3f, .release = 0.05f,
        .curve = 4, .attack_curve = 1 };
    p[0].filter = BOX_AUDIO_SYNTH_LOWPASS;
    p[0].cutoff = 300;
    p[0].slope = BOX_AUDIO_SYNTH_SLOPE_24;
    p[0].filter_amount = 4.7f;
    p[0].filter_envelope = (box_audio_synth_envelope_t){
        .attack = 0.15f, .sustain = 1, .hold = 0.05f, .release = 0.05f };
    p[0].resonance = 0.7f;
    p[0].layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .frequency_end = 440, .sweep = 0.1f, .level = 0.3f };
    p[0].layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.2f };
    p[0].layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 220,
        .level = 0.2f, .has_envelope = 1, .envelope = { .attack = 0.001f, .decay = 0.02f } };

    p[1].layers = 4;
    p[1].seed = 3;
    p[1].envelope = decay;
    p[1].filter = BOX_AUDIO_SYNTH_BANDPASS;
    p[1].cutoff = 900;
    p[1].resonance = 0.4f;
    p[1].layer[0] = (box_audio_synth_layer_t){ .frequency = 220, .level = 0.4f };
    p[1].layer[1] = (box_audio_synth_layer_t){ .frequency = 308, .level = 1,
        .modulates = 1, .index = 3, .has_envelope = 1, .envelope = decay };
    p[1].layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_TRIANGLE,
        .frequency = 440, .level = 0.2f };
    p[1].layer[3] = (box_audio_synth_layer_t){ .frequency = 440, .level = 0.3f,
        .modulates = 3, .modulation = BOX_AUDIO_SYNTH_RING, .index = 0.7f, .feedback = 0.8f,
        .audible = 1 };

    p[2] = synth_steady(BOX_AUDIO_SYNTH_SINE, 1000, 0.5f, 0.5f);
    p[2].layers = 2;
    p[2].layer[0].feedback = 0.6f;
    p[2].layer[1] = (box_audio_synth_layer_t){ .frequency = 6000, .level = 1,
        .modulates = 1, .index = 2, .frequency_end = 3000, .sweep = 0.4f };

    p[3].layers = 2;
    p[3].seed = 11;
    p[3].level_variance = 0.3f;
    p[3].envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.05f, .sustain = 0.6f, .hold = 0.05f, .release = 0.05f };
    p[3].filter = BOX_AUDIO_SYNTH_HIGHPASS;
    p[3].cutoff = 200;
    p[3].layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 330,
        .level = 0.4f };
    p[3].layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.1f,
        .has_envelope = 1, .envelope = { .attack = 0.001f, .decay = 0.03f } };

    for (i = 0; i < 4; i ++) {
        for (run = 0; run < 2; run ++) {
            box_audio_core_t *core = synth_core(AUDIO_RATE);
            for (l = p[i].layers; run && l < BOX_AUDIO_SYNTH_LAYERS; l ++) {
                p[i].layer[l] = junk;
            }

            synth_define(core, 1, &p[i]);
            audio_play(core, 1, 1, 0, 1, 0, 0.9f, i == 3 ? BOX_AUDIO_PLAY_LOOP : 0);
            audio_render(core, 0, AUDIO_FRAMES / 2);
            audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 1.2f);
            audio_render(core, AUDIO_FRAMES / 2, AUDIO_FRAMES / 2);
            h[run] = audio_hash(audio_l, AUDIO_FRAMES) ^ (audio_hash(audio_r, AUDIO_FRAMES) << 1);
            box_audio_core_free(core);
        }

        printf("patch %d: 0x%016llXull\n", (int)i, (unsigned long long)h[0]);
        test_assert(h[0] == h[1]);
    }
}

/** Create a world and run test/assets/audio/synth.flecs in it.
 *
 * @return The world
 */
static ecs_world_t* synth_world(void)
{
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth.flecs");
    test_assert(ecs_script_run(world, "synth", code, NULL) == 0);
    ecs_os_free(code);

    return world;
}

void Synth_script_parses(void) {
    // audio/synth
    ecs_world_t *world = synth_world();
    ecs_entity_t click = ecs_lookup(world, "UiClick");
    ecs_entity_t noise = ecs_lookup(world, "UiClick.noise");
    ecs_entity_t tone = ecs_lookup(world, "UiClick.tone");
    ecs_entity_t ui = ecs_lookup(world, "ui");
    test_assert(click != 0);
    test_assert(noise != 0);
    test_assert(tone != 0);
    test_assert(ecs_has_id(world, click, EcsPrefab));
    test_int(ecs_get(world, click, AudioOutput)->bus, ui);
    test_flt(ecs_get(world, click, SynthSound)->pitch_variance, 0.05f);
    test_flt(ecs_get(world, click, SynthEnvelope)->attack, 0.001f);
    test_flt(ecs_get(world, click, SynthEnvelope)->decay, 0.03f);
    test_flt(ecs_get(world, click, SynthEnvelope)->sustain, 0);
    test_int(ecs_get(world, noise, SynthOscillator)->wave, SynthNoise);
    test_flt(ecs_get(world, noise, SynthOscillator)->level, 0.6f);
    test_flt(ecs_get(world, noise, SynthOscillator)->frequency, 440);
    test_int(ecs_get(world, tone, SynthOscillator)->wave, SynthSine);
    test_flt(ecs_get(world, tone, SynthOscillator)->frequency, 1800);
    test_flt(ecs_get(world, tone, SynthOscillator)->frequency_end, 900);
    test_flt(ecs_get(world, tone, SynthOscillator)->sweep, 0.03f);
    test_flt(ecs_get(world, tone, SynthOscillator)->level, 0.4f);

    audio_run(world, 1);
    const AudioSynth *synth = ecs_get(world, click, AudioSynth);
    test_assert(synth != NULL);
    test_assert(synth->compiled);
    test_int(synth->patch.layers, 2);
    test_int(synth->patch.layer[0].wave, BOX_AUDIO_SYNTH_NOISE);
    test_int(synth->patch.layer[1].wave, BOX_AUDIO_SYNTH_SINE);
    test_assert(ecs_get(world, click, AudioSoundId)->id != 0);
    ecs_fini(world);
}

void Synth_script_renders(void) {
    // audio/synth
    ecs_world_t *world = synth_world();
    ecs_entity_t click = ecs_lookup(world, "UiClick");
    int32_t start;
    audio_run(world, 2);
    start = audio_tap_count;
    test_assert(box_audio_play(world, click, 1.0f));
    test_assert(ecs_script_run(world, "play",
        "using box.audio\n"
        "const played: bool = box.audio.play(UiClick, 1.0)\n", NULL) == 0);
    audio_run(world, 1);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 2);
    audio_run(world, 4);
    test_assert(audio_peak(audio_tap_l, start, start + 1600) > 0.1f);
    test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
    test_flt(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count), 0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(box_audio_module_stats(world).synths, 2);
    ecs_fini(world);
}

void Synth_script_change_next_play(void) {
    // audio/synth
    ecs_world_t *world = synth_world();
    ecs_entity_t beep = ecs_lookup(world, "Beep");
    test_assert(beep != 0);
    audio_run(world, 1);
    test_assert(box_audio_play(world, beep, 1.0f));
    audio_run(world, 6);
    test_assert(fabs(audio_frequency(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 440) < 1);
    audio_run(world, 12);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    ecs_set(world, beep, SynthOscillator, {
        .wave = SynthSine, .frequency = 880, .level = 0.5f });
    test_assert(box_audio_play(world, beep, 1.0f));
    audio_run(world, 6);
    test_assert(fabs(audio_frequency(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 880) < 2);
    ecs_fini(world);
}

void Synth_script_loop_source(void) {
    // audio/synth
    ecs_world_t *world = synth_world();
    ecs_entity_t beep = ecs_lookup(world, "Beep");
    ecs_entity_t src = audio_source(world, beep, 1.0f, true, 0);
    audio_run(world, 40);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_bool(ecs_get(world, src, AudioSource)->playing, true);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME * 12,
        audio_tap_count) > 0.3f);
    ecs_set(world, src, AudioSource, { .sound = beep, .volume = 1, .pitch = 1, .loop = false,
        .playing = true });
    audio_run(world, 20);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_bool(ecs_get(world, src, AudioSource)->playing, false);
    ecs_fini(world);
}

/** The examples of docs/audio.md compile without errors, play and end: the click within 3
 * frames of AUDIO_DT, the blip within 12, the whoosh within 70, the bell within 155, the
 * electric piano within 100, the metallic hit and the ring within 40 and the pluck within 48. */
void Synth_docs_examples(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_examples.flecs");
    const char *names[] = {
        "UiClick", "UiBlip", "Whoosh", "Bell", "ElectricPiano", "MetalHit", "Pluck", "Ring",
        "Strings" };
    const int32_t frames[] = { 3, 12, 70, 155, 100, 40, 48, 40, 110 };
    int32_t i, start;
    audio_log_start();
    test_assert(ecs_script_run(world, "examples", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);
    test_flt(ecs_get(world, ecs_lookup(world, "Ring"), AudioSynth)->patch.envelope.curve, 5);
    test_int(ecs_get(world, ecs_lookup(world, "Strings"), AudioSynth)->patch.layers,
        BOX_AUDIO_SYNTH_LAYERS);
    for (i = 0; i < 9; i ++) {
        ecs_entity_t sound = ecs_lookup(world, names[i]);
        test_assert(sound != 0);
        test_assert(ecs_get(world, sound, AudioSynth)->compiled);
        start = audio_tap_count;
        test_assert(box_audio_play(world, sound, 1.0f));
        audio_run(world, frames[i]);
        test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
        test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
        test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
        test_flt(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count), 0);
    }

    audio_log_stop();
    test_int(audio_log_errors, 0);
    ecs_fini(world);
}

/** The filter envelope and the slope of a script reach the patch, and a change or a removal of
 * the SynthFilterEnvelope compiles the sound again. */
void Synth_script_filter_envelope(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_examples.flecs");
    ecs_entity_t pluck, whoosh;
    const AudioSynth *synth;
    test_assert(ecs_script_run(world, "examples", code, NULL) == 0);
    ecs_os_free(code);
    pluck = ecs_lookup(world, "Pluck");
    whoosh = ecs_lookup(world, "Whoosh");
    test_assert(pluck != 0);
    test_assert(whoosh != 0);
    test_int(ecs_get(world, pluck, SynthFilter)->slope, SynthSlope24);
    test_flt(ecs_get(world, pluck, SynthFilterEnvelope)->amount, 4);
    test_flt(ecs_get(world, pluck, SynthFilterEnvelope)->decay, 0.25f);
    test_int(ecs_get(world, whoosh, SynthFilter)->slope, SynthSlope12);

    audio_run(world, 1);
    synth = ecs_get(world, pluck, AudioSynth);
    test_assert(synth->compiled);
    test_int(synth->patch.filter, BOX_AUDIO_SYNTH_LOWPASS);
    test_int(synth->patch.slope, BOX_AUDIO_SYNTH_SLOPE_24);
    test_flt(synth->patch.cutoff, 300);
    test_flt(synth->patch.filter_amount, 4);
    test_flt(synth->patch.filter_envelope.attack, 0.002f);
    test_flt(synth->patch.filter_envelope.decay, 0.25f);
    test_flt(synth->patch.filter_envelope.sustain, 0.2f);
    test_flt(synth->patch.filter_envelope.hold, 0.25f);
    test_flt(synth->patch.filter_envelope.release, 0.2f);
    synth = ecs_get(world, whoosh, AudioSynth);
    test_int(synth->patch.slope, BOX_AUDIO_SYNTH_SLOPE_12);
    test_flt(synth->patch.filter_amount, 3.737f);
    test_flt(synth->patch.filter_envelope.attack, 0.5f);

    ecs_set(world, pluck, SynthFilterEnvelope, { .decay = 0.1f, .curve = 3,
        .attack_curve = -2, .amount = -2 });
    audio_run(world, 1);
    synth = ecs_get(world, pluck, AudioSynth);
    test_assert(synth->compiled);
    test_flt(synth->patch.filter_amount, -2);
    test_flt(synth->patch.filter_envelope.decay, 0.1f);
    test_flt(synth->patch.filter_envelope.curve, 3);
    test_flt(synth->patch.filter_envelope.attack_curve, -2);
    ecs_remove(world, pluck, SynthFilterEnvelope);
    audio_run(world, 1);
    synth = ecs_get(world, pluck, AudioSynth);
    test_flt(synth->patch.filter_amount, 0);
    test_flt(synth->patch.filter_envelope.decay, 0);
    ecs_fini(world);
}

/** A sound of test/assets/audio/synth_layers.flecs with 8 layers compiles them all without an
 * error, and one with a ninth logs one error and leaves it out. */
void Synth_too_many_layers(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_layers.flecs");
    const box_audio_synth_patch_t *p;
    ecs_entity_t octet, wide;
    float sum = 0;
    int32_t i, start;
    audio_log_start();
    test_assert(ecs_script_run(world, "layers", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "Wide") != NULL);
    test_assert(strstr(audio_log_last, "9 layers") != NULL);

    octet = ecs_lookup(world, "Octet");
    p = &ecs_get(world, octet, AudioSynth)->patch;
    test_int(p->layers, BOX_AUDIO_SYNTH_LAYERS);
    for (i = 0; i < BOX_AUDIO_SYNTH_LAYERS; i ++) {
        sum += p->layer[i].frequency;
    }

    test_flt(sum, 3600);

    wide = ecs_lookup(world, "Wide");
    p = &ecs_get(world, wide, AudioSynth)->patch;
    test_int(p->layers, BOX_AUDIO_SYNTH_LAYERS);
    sum = 0;
    for (i = 0; i < BOX_AUDIO_SYNTH_LAYERS; i ++) {
        test_assert(p->layer[i].frequency >= 100 && p->layer[i].frequency <= 900);
        sum += p->layer[i].frequency;
    }

    test_assert(sum >= 3600 && sum <= 4400);
    start = audio_tap_count;
    test_assert(box_audio_play(world, octet, 1.0f));
    test_assert(box_audio_play(world, wide, 1.0f));
    audio_run(world, 2);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.1f);
    audio_run(world, 30);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

void Synth_endless_logs_error(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    ecs_entity_t hum;
    audio_log_start();
    test_assert(ecs_script_run(world, "hum",
        "using box.audio\n"
        "prefab Hum {\n"
        "  SynthSound: {}\n"
        "  SynthOscillator: {wave: SynthTriangle, frequency: 110, level: 0.3}\n"
        "}\n", NULL) == 0);
    audio_run(world, 2);
    hum = ecs_lookup(world, "Hum");
    test_assert(box_audio_play(world, hum, 1.0f));
    audio_run(world, 60);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "never end") != NULL);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    audio_run(world, (int32_t)(BOX_AUDIO_SYNTH_ENDLESS * 60) - 50);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

void Synth_deleted_releases(void) {
    // audio/synth
    ecs_world_t *world = synth_world();
    ecs_entity_t beep = ecs_lookup(world, "Beep");
    int32_t sounds;
    audio_run(world, 1);
    sounds = box_audio_module_sounds(world);
    test_assert(sounds >= 2);
    test_assert(box_audio_play(world, beep, 1.0f));
    audio_run(world, 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    ecs_delete(world, beep);
    audio_run(world, 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(box_audio_module_sounds(world), sounds - 1);
    ecs_fini(world);
}

/** Frequency of a range at a frame, from the period between each two upward zero crossings,
 * placed at their middle and interpolated in cents between the two periods around the frame.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param at The frame
 * @return Frequency in Hz, 0 when the range has no periods around the frame
 */
static double synth_frequency_at(
    const float *x,
    int32_t from,
    int32_t to,
    double at)
{
    double prev = -1, mid0 = -1, f0 = 0, result = 0;
    int32_t i;
    for (i = from + 1; i < to && result == 0; i ++) {
        if (x[i - 1] < 0 && x[i] >= 0) {
            double c = i - 1 + x[i - 1] / (double)(x[i - 1] - x[i]);
            if (prev >= 0) {
                double mid = (prev + c) / 2, f = AUDIO_RATE / (c - prev);
                if (mid0 >= 0 && mid0 <= at && mid >= at) {
                    double k = (at - mid0) / (mid - mid0);
                    result = f0 * pow(f / f0, k);
                }

                mid0 = mid;
                f0 = f;
            }

            prev = c;
        }
    }

    return result;
}

/** A sine at a steady level for a second, whose pitch glides over a time.
 *
 * @param frequency Hz
 * @param glide Seconds of a glide
 * @return The patch
 */
static box_audio_synth_patch_t synth_glide_patch(
    float frequency,
    float glide)
{
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SINE, frequency, 0.5f, 1.0f);
    p.glide = glide;

    return p;
}

/** Send a legato command of a note that follows the voice of a handle.
 *
 * @param core The core
 * @param voice New handle of the voice
 * @param from Handle of the voice the note follows
 * @param sound Sound id
 */
static void synth_legato(
    box_audio_core_t *core,
    uint32_t voice,
    uint32_t from,
    uint32_t sound)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato(&w, voice, from, sound, 0, 1, 0, 1, 0));
    box_audio_core_apply(core, w.data, w.size);
}

/** A pitch change of an octave over 0.2 s: the frequency is the geometric mean halfway, a
 * quarter of an octave up at a quarter, and lands on the octave without a step. */
void Synth_glide_geometric_mean(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_glide_patch(300, 0.2f);
    double mid = 300 * sqrt(2.0), quarter = 300 * pow(2.0, 0.25), f;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4864);
    test_assert(fabs(audio_frequency(audio_l, 480, 4864) - 300) < 0.3);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 2);
    audio_render(core, 0, 19200);

    f = synth_frequency_at(audio_l, 0, 19200, 4800);
    printf("halfway: %f Hz, geometric mean %f Hz\n", f, mid);
    test_assert(fabs(f - mid) < 0.003 * mid);
    f = synth_frequency_at(audio_l, 0, 19200, 2400);
    printf("a quarter: %f Hz, expected %f Hz\n", f, quarter);
    test_assert(fabs(f - quarter) < 0.003 * quarter);
    f = synth_frequency_at(audio_l, 0, 19200, 200);
    test_assert(f > 300 && f < 308);
    test_assert(fabs(audio_frequency(audio_l, 9600 + 480, 19200) - 600) < 0.6);
    test_assert(audio_max_step(audio_l, 1, 19200) < 0.05f);
    test_int(box_audio_core_voice_count(core), 1);
    box_audio_core_free(core);
}

/** A glide of 0 keeps the jump of today, bit identical: a sine at 0.5 after its attack is the
 * old phase recurrence, its turns per frame multiplied by the new pitch at the block of the
 * change. */
void Synth_glide_zero_identical(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_glide_patch(300, 0);
    double t = 0, dt = (double)300.0f * (double)1.0f / (double)AUDIO_RATE;
    int32_t edge = (int32_t)(BOX_AUDIO_SYNTH_EDGE * AUDIO_RATE + 0.5f), i;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4864);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 1.5f);
    audio_render(core, 4864, 4864);
    for (i = 0; i < 9728; i ++) {
        if (i == 4864) {
            dt = dt * ((double)1.5f / (double)1.0f);
        }

        if (i >= edge) {
            test_assert(audio_l[i] == box_audio_core_sin(t) * 0.5f);
        }

        t += dt;
        t = t >= 1 ? t - 1 : t;
    }

    test_assert(fabs(audio_frequency(audio_l, 4864 + 10, 9728) - 450) < 0.5);
    box_audio_core_free(core);
}

/** A legato note glides the voice it follows to the new note without a new attack: the level
 * stays at the sustain, which holds for its hold again, one voice plays and it ends with the
 * new handle. */
void Synth_legato_no_retrigger(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t a = synth_glide_patch(300, 0.05f), b = a;
    box_audio_event_t e;
    double amp, low = 1, high = 0;
    int32_t i, at;
    a.envelope = (box_audio_synth_envelope_t){
        .attack = 0.05f, .decay = 0.05f, .sustain = 0.5f, .hold = 0.5f, .release = 0.05f };
    b = a;
    b.layer[0].frequency = 450;
    synth_define(core, 1, &a);
    synth_define(core, 2, &b);
    audio_play(core, 7, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 9600);
    test_assert(fabs(synth_amplitude(audio_l, 6000, 9600) - 0.25) < 0.01);

    synth_legato(core, 8, 7, 2);
    audio_render(core, 0, 24000);
    test_int(box_audio_core_voice_count(core), 1);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 0);
    for (at = 0; at + 960 <= 24000; at += 960) {
        amp = synth_amplitude(audio_l, at, at + 960);
        low = amp < low ? amp : low;
        high = amp > high ? amp : high;
    }

    printf("legato level: %f to %f\n", low, high);
    test_assert(low > 0.24 && high < 0.26);
    test_assert(audio_max_step(audio_l, 1, 24000) < 0.05f);
    test_assert(fabs(audio_frequency(audio_l, 2400 + 480, 24000) - 450) < 0.5);

    audio_render(core, 24000, 24000);
    i = synth_last(audio_l, 0, AUDIO_FRAMES);
    printf("last sample at %d, release ends at 26400\n", i);
    test_assert(abs(i - 26400) <= SYNTH_MS);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_int(e.id, 8);
    box_audio_core_free(core);
}

/** A legato note of a voice that releases starts a new voice with the new handle: the old one
 * releases, ends and reports its end with handle 0. */
void Synth_legato_after_release(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t a = synth_glide_patch(300, 0.05f), b;
    box_audio_event_t e;
    a.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .sustain = 0.5f, .hold = 0.05f, .release = 0.2f };
    b = a;
    b.layer[0].frequency = 450;
    synth_define(core, 1, &a);
    synth_define(core, 2, &b);
    audio_play(core, 7, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4800);
    synth_legato(core, 8, 7, 2);
    audio_render(core, 0, 480);
    test_int(box_audio_core_voice_count(core), 2);
    audio_render(core, 0, 9600);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_int(e.id, 0);
    test_int(box_audio_core_voice_count(core), 1);
    test_assert(fabs(audio_frequency(audio_l, 8000, 9600) - 450) < 0.5);
    audio_render(core, 0, AUDIO_FRAMES);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_int(e.id, 8);
    box_audio_core_free(core);
}

/** A legato note of a looping voice in the release of its pass starts a new voice: the old one
 * stops looping, rings out its release and ends with handle 0, while the new one loops, and a
 * stop of the new handle leaves nothing playing. */
void Synth_legato_after_release_loop(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t a = synth_glide_patch(300, 0.05f), b;
    box_audio_writer_t w;
    uint8_t bytes[64];
    box_audio_event_t e;
    a.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .sustain = 0.5f, .hold = 0.05f, .release = 0.2f };
    b = a;
    b.layer[0].frequency = 450;
    synth_define(core, 1, &a);
    synth_define(core, 2, &b);
    audio_play(core, 7, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 4800);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato(&w, 8, 7, 2, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, 480);
    test_int(box_audio_core_voice_count(core), 2);
    audio_render(core, 0, 19200);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_int(e.id, 0);
    test_int(box_audio_core_voice_count(core), 1);

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 8, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_int(e.id, 8);
    box_audio_core_free(core);
}

/** A legato note that follows a voice that already ended, before its sender read the end,
 * starts a new voice with the new handle, so the end of the old one does not name it. */
void Synth_legato_after_end(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t a = synth_glide_patch(300, 0.05f), b;
    box_audio_event_t e;
    a.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .sustain = 0.5f, .hold = 0.05f, .release = 0.05f };
    b = a;
    b.layer[0].frequency = 450;
    synth_define(core, 1, &a);
    synth_define(core, 2, &b);
    audio_play(core, 7, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 9600);
    test_int(box_audio_core_voice_count(core), 0);
    synth_legato(core, 8, 7, 2);
    audio_render(core, 0, 4800);
    test_int(box_audio_core_voice_count(core), 1);
    test_assert(fabs(synth_amplitude(audio_l, 960, 2400) - 0.25) < 0.01);
    test_assert(fabs(audio_frequency(audio_l, 480, 2400) - 450) < 0.5);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_int(e.id, 7);
    audio_render(core, 0, AUDIO_FRAMES);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_int(e.id, 8);
    box_audio_core_free(core);
}

/** Create a world and run test/assets/audio/synth_glide.flecs in it.
 *
 * @return The world
 */
static ecs_world_t* synth_glide_world(void)
{
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_glide.flecs");
    test_assert(ecs_script_run(world, "glide", code, NULL) == 0);
    ecs_os_free(code);

    return world;
}

/** A change of the pitch of a playing AudioSource glides: two frames into a glide of 0.2 s
 * the frequency is between the two, at the end on the new one. */
void Synth_glide_source_pitch(void) {
    // audio/synth
    ecs_world_t *world = synth_glide_world();
    ecs_entity_t slide = ecs_lookup(world, "Slide");
    ecs_entity_t src;
    double f;
    test_assert(slide != 0);
    audio_run(world, 1);
    test_flt(ecs_get(world, slide, AudioSynth)->patch.glide, 0.2f);
    test_bool(ecs_get(world, slide, SynthGlide)->legato, false);
    src = audio_source(world, slide, 1.0f, false, 0);
    audio_run(world, 10);
    test_assert(fabs(audio_frequency(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 300) < 1);
    ecs_set(world, src, AudioSource, { .sound = slide, .volume = 1, .pitch = 2,
        .playing = true });
    audio_run(world, 3);
    f = audio_frequency(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count);
    printf("during the glide: %f Hz\n", f);
    test_assert(f > 310 && f < 590);
    audio_run(world, 20);
    test_assert(fabs(audio_frequency(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 600) < 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    ecs_fini(world);
}

/** box.audio.play of a legato note while the last note of its channel holds glides that
 * voice: one voice plays, it moves through the frequencies between and ends on the new note. */
void Synth_script_legato(void) {
    // audio/synth
    ecs_world_t *world = synth_glide_world();
    ecs_entity_t low = ecs_lookup(world, "glide_low");
    double f;
    test_assert(low != 0);
    audio_run(world, 1);
    test_flt(ecs_get(world, low, AudioSynth)->patch.glide, 0.1f);
    test_bool(ecs_get(world, low, SynthGlide)->legato, true);
    test_assert(box_audio_play(world, low, 1.0f));
    audio_run(world, 10);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_assert(fabs(audio_frequency(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 300) < 1);
    test_assert(ecs_script_run(world, "play",
        "using box.audio\n"
        "const played: bool = box.audio.play(glide_high, 1.0)\n", NULL) == 0);
    audio_run(world, 2);
    f = audio_frequency(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count);
    printf("during the legato glide: %f Hz\n", f);
    test_assert(f > 305 && f < 445);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    audio_run(world, 10);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_assert(fabs(audio_frequency(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 450) < 1.5);
    test_assert(fabs(synth_amplitude(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 0.4) < 0.02);
    test_assert(audio_max_step(audio_tap_l, audio_tap_count - AUDIO_FRAME * 22,
        audio_tap_count) < 0.05f);
    audio_run(world, 80);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** A new legato sound of an AudioSource that plays glides its voice instead of starting one. */
void Synth_script_legato_source(void) {
    // audio/synth
    ecs_world_t *world = synth_glide_world();
    ecs_entity_t low = ecs_lookup(world, "glide_low");
    ecs_entity_t high = ecs_lookup(world, "glide_high");
    ecs_entity_t src;
    test_assert(low != 0);
    test_assert(high != 0);
    audio_run(world, 1);
    src = audio_source(world, low, 1.0f, false, 0);
    audio_run(world, 10);
    test_assert(fabs(synth_amplitude(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 0.4) < 0.02);
    ecs_set(world, src, AudioSource, { .sound = high, .volume = 1, .pitch = 1,
        .playing = true });
    audio_run(world, 12);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_bool(ecs_get(world, src, AudioSource)->playing, true);
    test_assert(fabs(audio_frequency(audio_tap_l, audio_tap_count - 1600, audio_tap_count)
        - 450) < 1.5);
    test_assert(audio_max_step(audio_tap_l, audio_tap_count - AUDIO_FRAME * 14,
        audio_tap_count) < 0.05f);
    audio_run(world, 80);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_bool(ecs_get(world, src, AudioSource)->playing, false);
    ecs_fini(world);
}

/** The lead of docs/audio.md compiles without errors; three notes a few frames apart play as
 * one voice that ends after the last. */
void Synth_docs_glide_example(void) {
    // audio/synth
    ecs_world_t *world;
    const char *names[] = { "lead_a3", "lead_c4", "lead_e4" };
    int32_t i, start;
    world = synth_glide_world();
    audio_log_start();
    audio_run(world, 1);
    start = audio_tap_count;
    for (i = 0; i < 3; i ++) {
        ecs_entity_t sound = ecs_lookup(world, names[i]);
        test_assert(sound != 0);
        test_assert(ecs_get(world, sound, AudioSynth)->compiled);
        test_bool(ecs_get(world, sound, SynthGlide)->legato, true);
        test_assert(box_audio_play(world, sound, 1.0f));
        audio_run(world, 8);
        test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    }

    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
    audio_run(world, 60);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_flt(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count), 0);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    ecs_fini(world);
}

/** Modulators from test/assets/audio/synth_fm.flecs: a child of a layer modulates it, a
 * grandchild modulates the child, a change of the grandchild compiles the sound again, and a
 * modulator whose parent is not a layer logs one error and is left out. */
void Synth_script_modulators(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_fm.flecs");
    ecs_entity_t bell, orphan, deep;
    const box_audio_synth_patch_t *p;
    int32_t i, bright = -1, ring = -1, tone = -1, start;
    audio_log_start();
    test_assert(ecs_script_run(world, "fm", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "Orphan") != NULL);

    bell = ecs_lookup(world, "FmBell");
    test_assert(bell != 0);
    p = &ecs_get(world, bell, AudioSynth)->patch;
    test_int(p->layers, 4);
    test_flt(p->layer[0].frequency, 500);
    test_int(p->layer[0].modulates, 0);
    for (i = 1; i < 4; i ++) {
        if (p->layer[i].frequency == 700) {
            bright = i;
        } else if (p->layer[i].frequency == 300) {
            ring = i;
        } else if (p->layer[i].frequency == 1000) {
            tone = i;
        }
    }

    test_assert(bright > 0 && ring > 0 && tone > 0);
    test_int(p->layer[bright].modulates, 1);
    test_int(p->layer[bright].modulation, BOX_AUDIO_SYNTH_PHASE);
    test_flt(p->layer[bright].index, 2);
    test_int(p->layer[bright].has_envelope, 1);
    test_int(p->layer[ring].modulates, (uint32_t)bright + 1);
    test_int(p->layer[ring].modulation, BOX_AUDIO_SYNTH_RING);
    test_flt(p->layer[ring].index, 0.5f);
    test_int(p->layer[ring].audible, 0);
    test_assert(ring > bright);
    test_int(p->layer[tone].modulates, 0);
    test_flt(p->layer[tone].feedback, 0.5f);

    orphan = ecs_lookup(world, "Orphan");
    test_int(ecs_get(world, orphan, AudioSynth)->patch.layers, 1);
    test_flt(ecs_get(world, orphan, AudioSynth)->patch.layer[0].frequency, 200);

    deep = ecs_lookup(world, "FmBell.bright.deep");
    test_assert(deep != 0);
    ecs_set(world, deep, SynthOscillator, {
        .wave = SynthSine, .frequency = 300, .level = 1, .modulation = SynthRing,
        .index = 0.25f });
    audio_run(world, 1);
    p = &ecs_get(world, bell, AudioSynth)->patch;
    test_flt(p->layer[ring].index, 0.25f);

    start = audio_tap_count;
    test_assert(box_audio_play(world, bell, 1.0f));
    audio_run(world, 40);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.1f);
    test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** A patch of one oscillator held for a while with one LFO of every layer at a rate, at
 * depth 0 on every destination.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @param level Level of the oscillator
 * @param hold Seconds at the level
 * @param rate Hz of the LFO
 * @return The patch
 */
static box_audio_synth_patch_t synth_lfo_patch(
    uint32_t wave,
    float frequency,
    float level,
    float hold,
    float rate)
{
    box_audio_synth_patch_t p = synth_steady(wave, frequency, level, hold);
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = rate };

    return p;
}

/** Upward zero crossings of a range, interpolated between frames.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param at Receives the frame of each crossing
 * @param max Most crossings to keep
 * @return Number of crossings
 */
static int32_t synth_crossings(
    const float *x,
    int32_t from,
    int32_t to,
    double *at,
    int32_t max)
{
    int32_t count = 0, i;
    for (i = from + 1; i < to && count < max; i ++) {
        if (x[i - 1] < 0 && x[i] >= 0) {
            at[count ++] = i - 1 + x[i - 1] / (double)(x[i - 1] - x[i]);
        }
    }

    return count;
}

/** Energy of a harmonic of a window of frames, from a Goertzel filter.
 *
 * @param x Samples
 * @param center Middle frame of the window
 * @param n Frames of the window, a whole number of periods of the frequency
 * @param frequency Hz
 * @return The energy
 */
static double synth_window_energy(
    const float *x,
    int32_t center,
    int32_t n,
    double frequency)
{
    return synth_bin_energy(x + center - n / 2, n, frequency, AUDIO_RATE);
}

/** A vibrato of 100 cents at 4 Hz after a delay of 0.25 s: the pitch holds 1 kHz during the
 * delay, then the frequency of each period follows 1 kHz times 2 to the cents of the LFO,
 * peaks 100 cents up and down, and its peaks are a period of the LFO apart. */
void Synth_lfo_vibrato(void) {
    // audio/synth
    enum { MAX = 1200 };
    static double at[MAX];
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SINE, 1000, 0.5f, 1.2f, 4);
    const int32_t delay = 12000;
    double hi = -1e9, lo = 1e9, peaks[8], f, mid, t, expected, cents, last = 0, err = 0;
    int32_t count, k, npeaks = 0;
    p.lfo[0].delay = 0.25f;
    p.lfo[0].pitch = 100;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    count = synth_crossings(audio_l, 0, AUDIO_FRAMES, at, MAX);
    test_assert(count > 900);
    for (k = 1; k + 1 < count; k ++) {
        f = AUDIO_RATE / (at[k + 1] - at[k]);
        mid = 0.5 * (at[k] + at[k + 1]);
        if (at[k + 1] < delay) {
            test_assert(fabs(f - 1000) < 0.1);
            continue;
        }

        if (mid < delay + 480) {
            continue;
        }

        t = (mid + 1 - delay) / AUDIO_RATE;
        expected = 1000 * pow(2.0, 100.0 / 1200 * sin(2 * AUDIO_PI * 4 * t));
        err = fabs(f - expected) > err ? fabs(f - expected) : err;
        cents = 1200 * log2(f / 1000);
        hi = cents > hi ? cents : hi;
        lo = cents < lo ? cents : lo;
        if (k > 1 && npeaks < 8 && cents > 99 && mid - last > 4800) {
            peaks[npeaks ++] = mid;
            last = mid;
        }
    }

    printf("vibrato: %.2f to %.2f cents, %.3f Hz from the expected at most\n", lo, hi, err);
    test_assert(err < 1);
    test_assert(fabs(hi - 100) < 3);
    test_assert(fabs(lo + 100) < 3);
    test_assert(npeaks >= 2);
    for (k = 1; k < npeaks; k ++) {
        test_assert(fabs((peaks[k] - peaks[k - 1]) / AUDIO_RATE - 0.25) < 0.005);
    }

    box_audio_core_free(core);
}

/** A tremolo of depth 0.5 at 4 Hz on a steady level: the output dips from the level at the
 * top of the LFO to half of it at its bottom, frame by frame as its formula says. */
void Synth_lfo_tremolo(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0.6f, 4);
    double expected, err = 0;
    float hi, lo;
    int32_t i, at_lo = 0;
    p.lfo[0].amplitude = 0.5f;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 24000);
    hi = audio_l[100];
    lo = audio_l[100];
    for (i = 100; i < 23000; i ++) {
        expected = 0.5 * (1 - 0.5 * 0.5 * (1 - sin(2 * AUDIO_PI * 4 * (i + 1) / AUDIO_RATE)));
        err = fabs(audio_l[i] - expected) > err ? fabs(audio_l[i] - expected) : err;
        hi = audio_l[i] > hi ? audio_l[i] : hi;
        if (audio_l[i] < lo) {
            lo = audio_l[i];
            at_lo = i;
        }
    }

    printf("tremolo: %.4f to %.4f, %.6f from the expected at most\n", (double)lo, (double)hi,
        err);
    test_assert(err < 1e-3);
    test_assert(fabsf(hi - 0.5f) < 1e-3f);
    test_assert(fabsf(lo - 0.25f) < 1e-3f);
    test_assert(abs(at_lo - 9000) < 200);
    box_audio_core_free(core);
}

/** A tremolo of an LFO on one layer moves that layer alone. */
void Synth_lfo_layer(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SQUARE, 1, 0.2f, 0.6f, 4);
    float hi, lo;
    int32_t i;
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 1, .level = 0.3f };
    p.lfo[0].layer = 2;
    p.lfo[0].amplitude = 1;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 24000);
    hi = audio_l[100];
    lo = audio_l[100];
    for (i = 100; i < 23000; i ++) {
        hi = audio_l[i] > hi ? audio_l[i] : hi;
        lo = audio_l[i] < lo ? audio_l[i] : lo;
    }

    test_assert(fabsf(hi - 0.5f) < 1e-3f);
    test_assert(fabsf(lo - 0.2f) < 1e-3f);
    box_audio_core_free(core);
}

/** An LFO comes in after its delay and over its fade: the level holds during the delay, dips
 * less while it fades in, and fully after. */
void Synth_lfo_delay_fade(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0.6f, 10);
    float lo_fade = 1, lo_full = 1;
    int32_t i;
    p.lfo[0].amplitude = 1;
    p.lfo[0].delay = 0.1f;
    p.lfo[0].fade = 0.2f;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 24000);
    for (i = 100; i < 4800; i ++) {
        test_assert(fabsf(audio_l[i] - 0.5f) < 1e-6f);
    }

    for (i = 4800; i < 9600; i ++) {
        lo_fade = audio_l[i] < lo_fade ? audio_l[i] : lo_fade;
    }

    for (i = 14400; i < 23000; i ++) {
        lo_full = audio_l[i] < lo_full ? audio_l[i] : lo_full;
    }

    printf("delay and fade: %.4f while it fades in, %.4f after\n", (double)lo_fade,
        (double)lo_full);
    test_assert(lo_fade > 0.24f);
    test_assert(lo_fade < 0.49f);
    test_assert(lo_full < 0.01f);
    box_audio_core_free(core);
}

/** A slow LFO moves the level without zipper noise: the second difference of a slow tremolo
 * stays at the rounding of a float, far under the steps a control block would leave. */
void Synth_lfo_smooth(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0.6f, 0.5f);
    double d2, worst = 0;
    int32_t i;
    p.lfo[0].amplitude = 1;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 24000);
    for (i = 200; i < 23000; i ++) {
        d2 = fabs((double)audio_l[i + 1] - 2.0 * audio_l[i] + audio_l[i - 1]);
        worst = d2 > worst ? d2 : worst;
    }

    printf("slow tremolo: second difference %.3g at most\n", worst);
    test_assert(worst < 1e-6);
    box_audio_core_free(core);
}

/** A square LFO and a random one at full depth do not click: their steps are smoothed. */
void Synth_lfo_square_no_click(void) {
    // audio/synth
    const uint32_t waves[] = { BOX_AUDIO_SYNTH_LFO_SQUARE, BOX_AUDIO_SYNTH_LFO_SAW,
        BOX_AUDIO_SYNTH_LFO_RANDOM };
    int32_t w;
    for (w = 0; w < 3; w ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0.6f, 8);
        float step, hi = 0, lo = 1;
        int32_t i;
        p.lfo[0].wave = waves[w];
        p.lfo[0].amplitude = 1;
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 24000);
        step = audio_max_step(audio_l, 100, 23000);
        for (i = 100; i < 23000; i ++) {
            hi = audio_l[i] > hi ? audio_l[i] : hi;
            lo = audio_l[i] < lo ? audio_l[i] : lo;
        }

        printf("lfo wave %u: largest step %.5f, %.4f to %.4f\n", waves[w], (double)step,
            (double)lo, (double)hi);
        test_assert(step < 0.01f);
        test_assert(hi - lo > (waves[w] == BOX_AUDIO_SYNTH_LFO_RANDOM ? 0.05f : 0.4f));
        box_audio_core_free(core);
    }
}

/** An LFO of 2 octaves at 2 Hz on the cutoff of a lowpass: the 10th harmonic of a 220 Hz saw
 * is strong at the top of the LFO, when the cutoff is 4 kHz, and weak at its bottom, at
 * 250 Hz, once each period of the LFO. */
void Synth_lfo_cutoff(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SAW, 220, 0.5f, 1.2f, 2);
    double top[2], bottom[2];
    int32_t k;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 1000;
    p.lfo[0].cutoff = 2;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    for (k = 0; k < 2; k ++) {
        top[k] = synth_window_energy(audio_l, 6000 + k * 24000, 2400, 2200);
        bottom[k] = synth_window_energy(audio_l, 18000 + k * 24000, 2400, 2200);
        printf("cutoff lfo, period %d: 2200 Hz %.3g at the top, %.3g at the bottom\n", k,
            top[k], bottom[k]);
        test_assert(top[k] > 100 * bottom[k]);
    }

    test_assert(fabs(top[1] / top[0] - 1) < 0.2);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    box_audio_core_free(core);
}

/** An LFO on the pulse width of a 200 Hz square, from 0.375 by 0.125 at 2 Hz: at the top of
 * the LFO the width is 0.5 and the 2nd harmonic is gone, at its bottom the width is 0.25 and
 * the 2nd harmonic is strong, once each period of the LFO. */
void Synth_lfo_pulse_width(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SQUARE, 200, 0.5f, 1.2f, 2);
    double top[2], bottom[2];
    int32_t k;
    p.layer[0].pulse_width = 0.375f;
    p.lfo[0].pulse_width = 0.125f;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    for (k = 0; k < 2; k ++) {
        top[k] = synth_window_energy(audio_l, 6000 + k * 24000, 2400, 400);
        bottom[k] = synth_window_energy(audio_l, 18000 + k * 24000, 2400, 400);
        printf("pulse width lfo, period %d: 400 Hz %.3g at the top, %.3g at the bottom\n", k,
            top[k], bottom[k]);
        test_assert(bottom[k] > 50 * top[k]);
    }

    test_assert(fabs(bottom[1] / bottom[0] - 1) < 0.2);
    test_assert(audio_max_step(audio_l, 100, AUDIO_FRAMES) < 1.5f);
    box_audio_core_free(core);
}

/** A pulse at a fixed width has the harmonics of its width: none at multiples of 1 / width,
 * the 1st and the 3rd in the ratio of |sin(pi n w)| / n. */
void Synth_pulse_width_harmonics(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 200, 0.5f, 1.0f);
    double h1, h3, h4, expected;
    p.layer[0].pulse_width = 0.25f;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 24000);
    h1 = synth_window_energy(audio_l, 12000, 9600, 200);
    h3 = synth_window_energy(audio_l, 12000, 9600, 600);
    h4 = synth_window_energy(audio_l, 12000, 9600, 800);
    expected = pow(sin(3 * AUDIO_PI * 0.25) / 3 / sin(AUDIO_PI * 0.25), 2);
    printf("pulse 0.25: 3rd over 1st %.4f, expected %.4f, 4th %.3g\n", h3 / h1, expected,
        h4 / h1);
    test_assert(fabs(h3 / h1 / expected - 1) < 0.05);
    test_assert(h4 < 1e-4 * h1);
    box_audio_core_free(core);
}

/** Share of the energy of a 5 kHz pulse at 44100 Hz outside its harmonics and DC.
 *
 * @param width The pulse width
 * @return The share, 0..1
 */
static double synth_alias_pulse(
    float width)
{
    box_audio_core_t *core = synth_core(SYNTH_ALIAS_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 5000, 0.5f, 1.0f);
    const float *x = audio_l + 2048;
    double total = 0, mean = 0, harmonics = 0;
    int32_t i, k;
    p.layer[0].pulse_width = width;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 2048 + SYNTH_ALIAS_FRAMES);
    for (i = 0; i < SYNTH_ALIAS_FRAMES; i ++) {
        total += (double)x[i] * x[i];
        mean += x[i];
    }

    mean /= SYNTH_ALIAS_FRAMES;
    for (k = 1; k * 5000 < SYNTH_ALIAS_RATE / 2; k ++) {
        harmonics += synth_bin_energy(x, SYNTH_ALIAS_FRAMES, k * 5000.0, SYNTH_ALIAS_RATE);
    }

    box_audio_core_free(core);

    return (total - harmonics - mean * mean * SYNTH_ALIAS_FRAMES) / total;
}

/** A pulse is band limited at every width, the narrow ones included. */
void Synth_alias_pulse(void) {
    // audio/synth
    const float widths[] = { 0.1f, 0.25f, 0.4f, 0.75f, 0.95f };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        double alias = synth_alias_pulse(widths[i]);
        printf("pulse %.2f at 5 kHz: %.2f dB outside its harmonics\n", (double)widths[i],
            10 * log10(alias));
        test_assert(alias >= 0);
        test_assert(alias < 0.02);
    }
}

/** A square at a width of 0.5, and a patch whose LFO has no depth, render bit identical to the
 * patch as before: plain layers, noise, a filter envelope and phase modulation. */
void Synth_lfo_zero_identical(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SQUARE, 330, 0.3f, 0.3f);
    int32_t pass;
    p.seed = 11;
    p.layers = 4;
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.1f };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 220,
        .level = 0.2f };
    p.layer[3] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 440,
        .modulates = 3, .index = 2 };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 800;
    p.filter_amount = 2;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.05f, .decay = 0.1f };
    for (pass = 0; pass < 2; pass ++) {
        uint32_t voice = 1 + 2 * (uint32_t)pass;
        p.layers = pass ? 4 : 3;
        p.lfos = 0;
        p.layer[0].pulse_width = 0;
        synth_define(core, 1, &p);
        audio_play(core, voice, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 16000);
        test_int(box_audio_core_voice_count(core), 0);
        memcpy(audio_sound_a, audio_l, 16000 * sizeof(float));

        p.layer[0].pulse_width = 0.5f;
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_RANDOM, .rate = 6,
            .random_phase = 1 };
        synth_define(core, 2, &p);
        audio_play(core, voice + 1, 2, 0, 1, 0, 1, 0);
        audio_render(core, 0, 16000);
        test_int(box_audio_core_voice_count(core), 0);
        test_assert(memcmp(audio_l, audio_sound_a, 16000 * sizeof(float)) == 0);
    }

    box_audio_core_free(core);
}

/** Render a saw through a filter envelope without LFOs, in a core whose voice slot first
 * played a patch with a cutoff LFO to its end, or in a new core.
 *
 * @param before Whether the slot plays the patch with the LFO first
 * @param out Receives 16000 samples
 */
static void synth_lfo_reuse(
    bool before,
    float *out)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SAW, 220, 0.3f, 0.1f, 3);
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 300;
    p.lfo[0].cutoff = 1;
    if (before) {
        synth_define(core, 2, &p);
        audio_play(core, 2, 2, 0, 1, 0, 1, 0);
        audio_render(core, 0, 16000);
        test_int(box_audio_core_voice_count(core), 0);
    }

    p = synth_steady(BOX_AUDIO_SYNTH_SAW, 220, 0.3f, 0.2f);
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 300;
    p.filter_amount = 4;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.05f, .decay = 0.2f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 16000);
    test_int(box_audio_core_voice_count(core), 0);
    memcpy(out, audio_l, 16000 * sizeof(float));
    box_audio_core_free(core);
}

/** A voice slot that played a cutoff LFO keeps nothing of it: a filter envelope without LFOs
 * in it renders bit identical to the one of a new core. */
void Synth_lfo_slot_reuse(void) {
    // audio/synth
    synth_lfo_reuse(false, audio_sound_a);
    synth_lfo_reuse(true, audio_sound_b);
    test_assert(memcmp(audio_sound_a, audio_sound_b, 16000 * sizeof(float)) == 0);
}

/** An LFO with a delay and no fade still comes in over a few ms: a full tremolo that starts
 * at its bottom does not step the level. */
void Synth_lfo_delay_no_fade(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SQUARE, 1, 0.5f, 0.6f, 0.5f);
    float step, worst = 0;
    int32_t i;
    p.lfo[0].amplitude = 1;
    p.lfo[0].delay = 0.1f;
    p.lfo[0].phase = 0.75f;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 24000);
    for (i = 100; i < 23000; i ++) {
        step = fabsf(audio_l[i + 1] - audio_l[i]);
        worst = step > worst ? step : worst;
    }

    printf("delay without fade: %.5f largest step\n", (double)worst);
    test_assert(worst < 0.005f);
    test_assert(audio_l[4800 + 480] < 0.01f);
    box_audio_core_free(core);
}

/** Render a vibrato at a random phase per play of a seed in a new core, the plays before it
 * rendered and dropped.
 *
 * @param seed The seed
 * @param play Count of the play to keep, from 1
 * @param out Receives frames samples
 * @param frames Number of frames
 */
static void synth_lfo_random(
    uint32_t seed,
    int32_t play,
    float *out,
    int32_t frames)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_lfo_patch(BOX_AUDIO_SYNTH_SINE, 500, 0.5f, 0.4f, 3);
    int32_t i;
    p.seed = seed;
    p.lfo[0].random_phase = 1;
    p.lfo[0].pitch = 50;
    synth_define(core, 1, &p);
    for (i = 1; i <= play; i ++) {
        audio_play(core, (uint32_t)i, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, frames);
    }

    memcpy(out, audio_l, (size_t)frames * sizeof(float));
    box_audio_core_free(core);
}

/** A random phase is the same for the same seed and count of plays, and differs with either. */
void Synth_lfo_random_phase_seeded(void) {
    // audio/synth
    static float other[24000];
    synth_lfo_random(21, 1, audio_sound_a, 24000);
    synth_lfo_random(21, 1, audio_sound_b, 24000);
    test_assert(memcmp(audio_sound_a, audio_sound_b, 24000 * sizeof(float)) == 0);
    synth_lfo_random(21, 3, audio_sound_b, 24000);
    synth_lfo_random(21, 3, other, 24000);
    test_assert(memcmp(other, audio_sound_b, 24000 * sizeof(float)) == 0);
    test_assert(memcmp(audio_sound_a, audio_sound_b, 24000 * sizeof(float)) != 0);
    synth_lfo_random(22, 1, audio_sound_b, 24000);
    test_assert(memcmp(audio_sound_a, audio_sound_b, 24000 * sizeof(float)) != 0);
}

/** The examples of LFOs of docs/audio.md, from test/assets/audio/synth_lfo.flecs, compile
 * into their patches without errors, play and end. */
void Synth_docs_lfo_examples(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_lfo.flecs");
    const char *names[] = { "flute_a4", "PwmPad" };
    const int32_t frames[] = { 75, 300 };
    const box_audio_synth_patch_t *p;
    ecs_entity_t flute, pad;
    int32_t i, start;
    audio_log_start();
    test_assert(ecs_script_run(world, "lfo", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);
    flute = ecs_lookup(world, "flute_a4");
    pad = ecs_lookup(world, "PwmPad");
    test_assert(flute != 0);
    test_assert(pad != 0);
    test_flt(ecs_get(world, flute, SynthLfo)->rate, 5.2f);
    test_int(ecs_get(world, flute, SynthLfo)->wave, SynthLfoSine);
    test_bool(ecs_get(world, flute, SynthLfo)->random_phase, true);

    p = &ecs_get(world, flute, AudioSynth)->patch;
    test_int(p->layers, 4);
    test_int(p->lfos, 1);
    test_int(p->lfo[0].layer, 0);
    test_int(p->lfo[0].wave, BOX_AUDIO_SYNTH_LFO_SINE);
    test_flt(p->lfo[0].rate, 5.2f);
    test_flt(p->lfo[0].delay, 0.2f);
    test_flt(p->lfo[0].fade, 0.3f);
    test_flt(p->lfo[0].pitch, 14);
    test_flt(p->lfo[0].amplitude, 0.08f);
    test_int(p->lfo[0].random_phase, 1);
    test_flt(p->layer[0].pulse_width, 0.5f);

    p = &ecs_get(world, pad, AudioSynth)->patch;
    test_int(p->layers, 2);
    test_int(p->lfos, 2);
    test_int(p->lfo[0].layer, 0);
    test_int(p->lfo[0].wave, BOX_AUDIO_SYNTH_LFO_TRIANGLE);
    test_flt(p->lfo[0].pulse_width, 0.3f);
    test_flt(p->lfo[0].cutoff, 0.5f);
    test_int(p->lfo[1].layer, 2);
    test_flt(p->lfo[1].pitch, 6);
    test_flt(p->lfo[1].phase, 0.5f);
    test_flt(p->layer[0].pulse_width, 0.5f);
    test_flt(p->layer[1].pulse_width, 0.3f);

    for (i = 0; i < 2; i ++) {
        ecs_entity_t sound = ecs_lookup(world, names[i]);
        start = audio_tap_count;
        test_assert(box_audio_play(world, sound, 1.0f));
        audio_run(world, frames[i]);
        test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
        test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
        test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
        test_flt(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count), 0);
    }

    audio_log_stop();
    test_int(audio_log_errors, 0);
    ecs_fini(world);
}

/** LFOs of a script: one on a child of the root without an oscillator moves every layer, a
 * third logs one error and is left out, and a change or a removal compiles the sound again. */
void Synth_script_lfo(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    ecs_entity_t wobble;
    const box_audio_synth_patch_t *p;
    audio_log_start();
    test_assert(ecs_script_run(world, "lfos",
        "using box.audio\n"
        "prefab Wobble {\n"
        "  SynthSound: {seed: 4}\n"
        "  SynthEnvelope: {attack: 0.01, decay: 0.1}\n"
        "  SynthFilter: {cutoff: 800}\n"
        "  tone {\n"
        "    SynthOscillator: {wave: SynthSquare, frequency: 220, level: 0.3, pulse_width: 0.2}\n"
        "    SynthLfo: {wave: SynthLfoSquare, rate: 3, pulse_width: 0.1}\n"
        "  }\n"
        "  wah { SynthLfo: {wave: SynthLfoRandom, rate: 7, cutoff: 1.5} }\n"
        "  more { SynthLfo: {rate: 1, pitch: 10} }\n"
        "}\n", NULL) == 0);
    audio_run(world, 1);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "Wobble") != NULL);
    test_assert(strstr(audio_log_last, "LFO") != NULL);

    wobble = ecs_lookup(world, "Wobble");
    p = &ecs_get(world, wobble, AudioSynth)->patch;
    test_int(p->layers, 1);
    test_flt(p->layer[0].pulse_width, 0.2f);
    test_int(p->lfos, 2);
    test_int(p->lfo[0].layer, 1);
    test_int(p->lfo[0].wave, BOX_AUDIO_SYNTH_LFO_SQUARE);
    test_flt(p->lfo[0].pulse_width, 0.1f);
    test_int(p->lfo[1].layer, 0);
    test_int(p->lfo[1].wave, BOX_AUDIO_SYNTH_LFO_RANDOM);
    test_flt(p->lfo[1].cutoff, 1.5f);

    ecs_delete(world, ecs_lookup(world, "Wobble.more"));
    ecs_set(world, ecs_lookup(world, "Wobble.wah"), SynthLfo, {
        .wave = SynthLfoTriangle, .rate = 2, .amplitude = 0.5f });
    audio_run(world, 1);
    p = &ecs_get(world, wobble, AudioSynth)->patch;
    test_int(p->lfos, 2);
    test_int(p->lfo[1].wave, BOX_AUDIO_SYNTH_LFO_TRIANGLE);
    test_flt(p->lfo[1].amplitude, 0.5f);
    test_flt(p->lfo[1].cutoff, 0);

    ecs_remove(world, ecs_lookup(world, "Wobble.tone"), SynthLfo);
    audio_run(world, 1);
    p = &ecs_get(world, wobble, AudioSynth)->patch;
    test_int(p->lfos, 1);
    test_int(p->lfo[0].layer, 0);

    test_assert(box_audio_play(world, wobble, 1.0f));
    audio_run(world, 10);
    test_assert(synth_finite(audio_tap_l, 0, audio_tap_count));
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** Seconds of the renders of the unison cases that measure levels and correlations */
#define SYNTH_UNISON_SECONDS 4

/** A patch of one oscillator with unison voices at a steady level for a while.
 *
 * @param wave A box_audio_synth_wave_t
 * @param frequency Hz
 * @param level Level of the layer
 * @param voices Unison voices
 * @param detune Cents from the lowest voice to the highest
 * @param width Stereo width, 0..1
 * @param hold Seconds at the level
 * @return The patch
 */
static box_audio_synth_patch_t synth_unison(
    uint32_t wave,
    float frequency,
    float level,
    uint32_t voices,
    float detune,
    float width,
    float hold)
{
    box_audio_synth_patch_t p = synth_steady(wave, frequency, level, hold);
    p.layer[0].voices = voices;
    p.layer[0].detune = detune;
    p.layer[0].width = width;

    return p;
}

/** Power of the component of a range at a frequency, through a Hann window, so that a peak
 * leaks little into frequencies a few Hz away.
 *
 * @param x Samples
 * @param n Number of samples
 * @param frequency Hz
 * @param rate Frames per second
 * @return The power, in arbitrary units
 */
static double synth_hann_power(
    const float *x,
    int32_t n,
    double frequency,
    double rate)
{
    double re = 0, im = 0, w, a;
    int32_t i;
    for (i = 0; i < n; i ++) {
        w = 0.5 - 0.5 * cos(2 * AUDIO_PI * i / (n - 1));
        a = 2 * AUDIO_PI * frequency * i / rate;
        re += x[i] * w * cos(a);
        im -= x[i] * w * sin(a);
    }

    return re * re + im * im;
}

/** Statistics of the two channels of a render of SYNTH_UNISON_SECONDS seconds of a patch, from
 * 0.05 seconds on.
 *
 * @param p The patch, which holds that long
 * @param ms_l Receives the mean square of the left channel
 * @param ms_r Receives the mean square of the right channel
 * @param corr Receives the correlation of the channels
 */
static void synth_unison_stats(
    const box_audio_synth_patch_t *p,
    double *ms_l,
    double *ms_r,
    double *corr)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    double ll = 0, rr = 0, lr = 0;
    int32_t s, i, n = 0;
    synth_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    for (s = 0; s < SYNTH_UNISON_SECONDS; s ++) {
        audio_render(core, 0, AUDIO_FRAMES);
        for (i = s ? 0 : AUDIO_RATE / 20; i < AUDIO_FRAMES; i ++) {
            ll += (double)audio_l[i] * audio_l[i];
            rr += (double)audio_r[i] * audio_r[i];
            lr += (double)audio_l[i] * audio_r[i];
            n ++;
        }
    }

    box_audio_core_free(core);
    *ms_l = ll / n;
    *ms_r = rr / n;
    *corr = lr / sqrt(ll * rr);
}

/** The voices of a layer play at its frequency times 2 to the detune of each over 1200: the
 * spectrum has a peak at each, of about the same height, and little between them. */
void Synth_unison_detune_peaks(void) {
    // audio/synth
    const uint32_t voices[] = { 3, 4 };
    const float detune[] = { 100, 60 };
    const int32_t from = 2400, n = AUDIO_FRAMES - 2400;
    int32_t c, k;
    for (c = 0; c < 2; c ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_unison(
            BOX_AUDIO_SYNTH_SINE, 440, 0.5f, voices[c], detune[c], 0, 1.5f);
        double f[BOX_AUDIO_SYNTH_UNISON_MAX], lo = 1e300, hi = 0, between = 0, power;
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        box_audio_core_free(core);
        for (k = 0; k < (int32_t)voices[c]; k ++) {
            double cents = detune[c] * ((double)k / (voices[c] - 1) - 0.5);
            f[k] = 440 * pow(2, cents / 1200);
            power = synth_hann_power(audio_l + from, n, f[k], AUDIO_RATE);
            printf("%u voices over %.0f cents: %.2f Hz at %g\n", voices[c],
                (double)detune[c], f[k], power);
            lo = power < lo ? power : lo;
            hi = power > hi ? power : hi;
        }

        for (k = 0; k + 1 < (int32_t)voices[c]; k ++) {
            power = synth_hann_power(audio_l + from, n, 0.5 * (f[k] + f[k + 1]), AUDIO_RATE);
            between = power > between ? power : between;
        }

        power = synth_hann_power(audio_l + from, n, 440 * pow(2, detune[c] / 1200), AUDIO_RATE);
        between = power > between ? power : between;
        printf("between the peaks: %g\n", between);
        test_assert(hi / lo < 2);
        test_assert(lo > 100 * between);
    }
}

/** A width of 0 renders the same samples on both channels, whatever the voices and detune. */
void Synth_unison_width_zero_mono(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_unison(
        BOX_AUDIO_SYNTH_SAW, 220, 0.3f, 5, 30, 0, 0.8f);
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 2000;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0.1f);
    test_assert(memcmp(audio_l, audio_r, AUDIO_FRAMES * sizeof(float)) == 0);
}

/** The channels of a wide layer correlate as the mean of cos(pi pan / 2) over its voices: 0
 * for 2 voices at width 1, 1/3 for 3, and more at a smaller width. */
void Synth_unison_width_correlation(void) {
    // audio/synth
    const uint32_t voices[] = { 2, 2, 3, 5, 7 };
    const float width[] = { 1, 0.5f, 1, 1, 1 };
    int32_t c, k;
    for (c = 0; c < 5; c ++) {
        box_audio_synth_patch_t p = synth_unison(BOX_AUDIO_SYNTH_SINE, 440, 0.3f, voices[c],
            100, width[c], SYNTH_UNISON_SECONDS + 1);
        double ms_l, ms_r, corr, expected = 0;
        for (k = 0; k < (int32_t)voices[c]; k ++) {
            double pan = width[c] * (2.0 * k / (voices[c] - 1) - 1);
            expected += cos(AUDIO_PI * pan / 2) / voices[c];
        }

        synth_unison_stats(&p, &ms_l, &ms_r, &corr);
        printf("%u voices at width %.1f: correlation %.3f, expected %.3f, levels %.4f %.4f\n",
            voices[c], (double)width[c], corr, expected, sqrt(ms_l), sqrt(ms_r));
        test_assert(fabs(corr - expected) < 0.05);
        test_assert(fabs(sqrt(ms_l / ms_r) - 1) < 0.05);
    }
}

/** The level of a layer stays about the same from 1 to 7 voices, at width 0 and 1. */
void Synth_unison_level_constant(void) {
    // audio/synth
    double base = 0, ms_l, ms_r, corr, ratio;
    uint32_t v;
    int32_t w;
    for (w = 0; w < 2; w ++) {
        for (v = 1; v <= BOX_AUDIO_SYNTH_UNISON_MAX; v ++) {
            box_audio_synth_patch_t p = synth_unison(BOX_AUDIO_SYNTH_SAW, 220, 0.2f, v, 50,
                (float)w, SYNTH_UNISON_SECONDS + 1);
            synth_unison_stats(&p, &ms_l, &ms_r, &corr);
            if (!base) {
                base = ms_l;
            }

            ratio = sqrt(0.5 * (ms_l + ms_r) / base);
            printf("%u voices at width %d: %.2f dB, left %.2f dB, right %.2f dB\n", v, (int)w,
                20 * log10(ratio), 10 * log10(ms_l / base), 10 * log10(ms_r / base));
            test_assert(fabs(20 * log10(ratio)) < 1.0);
            test_assert(fabs(10 * log10(ms_l / base)) < 1.5);
            test_assert(fabs(10 * log10(ms_r / base)) < 1.5);
        }
    }
}

/** Hash both channels of a render of a patch in a new core, and of a second play of it.
 *
 * @param p The patch
 * @param second Receives the hash of the second play
 * @return The hash of the first play
 */
static uint64_t synth_unison_hash(
    const box_audio_synth_patch_t *p,
    uint64_t *second)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    uint64_t result;
    synth_define(core, 1, p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    result = audio_hash(audio_l, AUDIO_FRAMES / 2) ^ (audio_hash(audio_r, AUDIO_FRAMES / 2) << 1);
    audio_play(core, 2, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    *second = audio_hash(audio_l, AUDIO_FRAMES / 2) ^
        (audio_hash(audio_r, AUDIO_FRAMES / 2) << 1);
    box_audio_core_free(core);

    return result;
}

/** The same seed renders bit identical; another seed, and the next play of the same patch,
 * start the voices at other phases. */
void Synth_unison_seed_identical(void) {
    // audio/synth
    box_audio_synth_patch_t p = synth_unison(BOX_AUDIO_SYNTH_SAW, 220, 0.2f, 5, 25, 0.7f, 0.3f);
    uint64_t a, b, c, a2, b2, c2;
    p.layers = 3;
    p.seed = 9;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 1500;
    p.slope = BOX_AUDIO_SYNTH_SLOPE_24;
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.05f,
        .voices = 2, .width = 1 };
    p.layer[2] = (box_audio_synth_layer_t){ .frequency = 330, .level = 0.1f, .feedback = 0.4f,
        .voices = 3, .detune = 10 };
    a = synth_unison_hash(&p, &a2);
    b = synth_unison_hash(&p, &b2);
    p.seed = 10;
    c = synth_unison_hash(&p, &c2);
    printf("seed 9: 0x%016llXull\n", (unsigned long long)a);
    test_assert(a == b);
    test_assert(a2 == b2);
    test_assert(a != a2);
    test_assert(a != c);
}

/** A layer of one voice, or none, renders bit identical to a layer without unison, whatever
 * its detune and width, and stays mono. */
void Synth_unison_one_voice_identical(void) {
    // audio/synth
    box_audio_synth_patch_t p = synth_steady(BOX_AUDIO_SYNTH_SAW, 220, 0.3f, 0.3f);
    uint64_t plain, one, none, second;
    int32_t i;
    p.layers = 2;
    p.seed = 2;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 600;
    p.filter_amount = 2;
    p.filter_envelope = (box_audio_synth_envelope_t){ .attack = 0.05f, .decay = 0.1f };
    p.layer[1] = (box_audio_synth_layer_t){ .frequency = 660, .level = 1, .modulates = 1,
        .index = 1.5f };
    plain = synth_unison_hash(&p, &second);
    for (i = 0; i < 2; i ++) {
        p.layer[i].voices = 1;
        p.layer[i].detune = 50;
        p.layer[i].width = 1;
    }

    one = synth_unison_hash(&p, &second);
    test_assert(memcmp(audio_l, audio_r, (AUDIO_FRAMES / 2) * sizeof(float)) == 0);
    for (i = 0; i < 2; i ++) {
        p.layer[i].voices = 0;
    }

    none = synth_unison_hash(&p, &second);
    test_assert(plain == one);
    test_assert(plain == none);
}

/** Render a wide layer on bus 1 into audio_l and audio_r.
 *
 * @param routes The order
 * @param count Routes in the order
 * @param voice_pan Pan of the voice
 * @param bus_pan Pan of bus 1
 * @param send Send level of bus 1
 */
static void synth_unison_chain(
    const box_audio_route_t *routes,
    int32_t count,
    float voice_pan,
    float bus_pan,
    float send)
{
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_unison(BOX_AUDIO_SYNTH_SAW, 220, 0.15f, 2, 40, 1, 1.0f);
    p.seed = 6;
    synth_define(core, 1, &p);
    audio_order(core, routes, count);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, bus_pan);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND, send);
    audio_play(core, 1, 1, 1, 1, voice_pan, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    box_audio_core_free(core);
}

/** A wide voice is a stereo voice in the mixer: the pan of the voice and of its bus are a
 * balance that keeps the power of its two channels on one side, and a send carries both. */
void Synth_unison_stereo_chain(void) {
    // audio/synth
    static float ref_l[AUDIO_FRAMES / 2], ref_r[AUDIO_FRAMES / 2];
    const box_audio_route_t plain[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    const box_audio_route_t sent[] = {
        AUDIO_ROUTE(1, 0, 2), AUDIO_OUT(2, 0), AUDIO_MASTER };
    const int32_t from = 2 * AUDIO_RAMP, to = AUDIO_FRAMES / 2;
    double ms_l = 0, ms_r = 0, ms = 0, lr = 0;
    float err = 0;
    int32_t i;
    synth_unison_chain(plain, 2, 0, 0, 0);
    memcpy(ref_l, audio_l, sizeof(ref_l));
    memcpy(ref_r, audio_r, sizeof(ref_r));
    for (i = from; i < to; i ++) {
        ms_l += (double)ref_l[i] * ref_l[i];
        ms_r += (double)ref_r[i] * ref_r[i];
        lr += (double)ref_l[i] * ref_r[i];
    }

    test_assert(audio_peak(ref_l, from, to) > 0.05f);
    test_assert(fabs(lr / sqrt(ms_l * ms_r)) < 0.3);

    synth_unison_chain(plain, 2, -1, 0, 0);
    test_assert(audio_peak(audio_r, from, to) < 1e-6f);
    for (i = from; i < to; i ++) {
        float d = fabsf(audio_l[i] - SYNTH_SQRT2 * ref_l[i]);
        err = d > err ? d : err;
        ms += (double)audio_l[i] * audio_l[i];
    }

    printf("voice pan left: error %g, power %.3f of the centered one\n", (double)err,
        ms / (ms_l + ms_r));
    test_assert(err < 1e-5f);
    test_assert(fabs(ms / (ms_l + ms_r) - 1) < 0.1);

    synth_unison_chain(plain, 2, 0, 1, 0);
    test_assert(audio_peak(audio_l, from, to) < 1e-6f);
    err = 0;
    for (i = from; i < to; i ++) {
        float d = fabsf(audio_r[i] - SYNTH_SQRT2 * ref_r[i]);
        err = d > err ? d : err;
    }

    printf("bus pan right: error %g\n", (double)err);
    test_assert(err < 1e-5f);

    synth_unison_chain(sent, 3, 0, 0, 1);
    err = 0;
    for (i = from; i < to; i ++) {
        float dl = fabsf(audio_l[i] - 2 * ref_l[i]);
        float dr = fabsf(audio_r[i] - 2 * ref_r[i]);
        err = dl > err ? dl : err;
        err = dr > err ? dr : err;
    }

    printf("send: error %g\n", (double)err);
    test_assert(err < 1e-5f);
}

/** The pad of docs/audio.md and the sounds of test/assets/audio/synth_unison.flecs compile with
 * their unison, the pad plays wide and ends, the defaults play mono, a change of a SynthUnison
 * compiles the sound again, and a wide sound in space is heard from its side alone. */
void Synth_script_unison(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_unison.flecs");
    ecs_entity_t pad, thick, beep, saw;
    const box_audio_synth_patch_t *p;
    int32_t i, start, found = 0;
    float diff = 0;
    ecs_entity_t ears;
    test_int(box_jobs_set_workers(world, 0), 0);
    audio_log_start();
    test_assert(ecs_script_run(world, "unison", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);

    pad = ecs_lookup(world, "WidePad");
    thick = ecs_lookup(world, "Thick");
    beep = ecs_lookup(world, "WideBeep");
    saw = ecs_lookup(world, "WidePad.saw");
    test_assert(pad != 0 && thick != 0 && beep != 0 && saw != 0);
    test_int(ecs_get(world, thick, SynthUnison)->voices, 3);
    test_flt(ecs_get(world, thick, SynthUnison)->detune, 10);
    test_flt(ecs_get(world, thick, SynthUnison)->width, 0);
    p = &ecs_get(world, pad, AudioSynth)->patch;
    test_int(p->layers, 3);
    for (i = 0; i < 3; i ++) {
        if (p->layer[i].frequency == 220) {
            test_int(p->layer[i].voices, 5);
            test_flt(p->layer[i].detune, 20);
            test_flt(p->layer[i].width, 0.8f);
            found ++;
        } else if (p->layer[i].frequency == 330) {
            test_int(p->layer[i].voices, 3);
            test_flt(p->layer[i].detune, 14);
            test_flt(p->layer[i].width, 1);
            found ++;
        } else if (p->layer[i].frequency == 110) {
            test_int(p->layer[i].voices, 1);
            found ++;
        }
    }

    test_int(found, 3);
    p = &ecs_get(world, thick, AudioSynth)->patch;
    test_int(p->layer[0].voices, 3);
    test_flt(p->layer[0].detune, 10);

    start = audio_tap_count;
    test_assert(box_audio_play(world, pad, 1.0f));
    audio_run(world, 140);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
    test_assert(synth_finite(audio_tap_r, start, audio_tap_count));
    for (i = start; i < audio_tap_count; i ++) {
        float d = fabsf(audio_tap_l[i] - audio_tap_r[i]);
        diff = d > diff ? d : diff;
    }

    test_assert(diff > 0.01f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_flt(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count), 0);

    start = audio_tap_count;
    test_assert(box_audio_play(world, thick, 1.0f));
    audio_run(world, 30);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.1f);
    test_assert(memcmp(audio_tap_l + start, audio_tap_r + start,
        (size_t)(audio_tap_count - start) * sizeof(float)) == 0);

    ecs_set(world, saw, SynthUnison, { .voices = 7, .detune = 30, .width = 1 });
    audio_run(world, 1);
    p = &ecs_get(world, pad, AudioSynth)->patch;
    for (i = 0; i < 3; i ++) {
        if (p->layer[i].frequency == 220) {
            test_int(p->layer[i].voices, 7);
            test_flt(p->layer[i].detune, 30);
        }
    }

    ears = ecs_new(world);
    ecs_set(world, ears, Position, {0, 0, 0});
    ecs_set(world, ears, Rotation, {0, 0, 0});
    ecs_add(world, ears, AudioListener);
    audio_run(world, 2);
    test_bool(box_audio_play_at(world, beep, (vec3){-5, 0, 0}, 1.0f), true);
    start = audio_tap_count;
    audio_run(world, 6);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.2f);
    test_assert(audio_peak(audio_tap_r, start, audio_tap_count) < 1e-4f);
    audio_run(world, 40);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    test_bool(box_audio_play_at(world, beep, (vec3){0, 0, -5}, 1.0f), true);
    start = audio_tap_count;
    audio_run(world, 6);
    diff = 0;
    for (i = start; i < audio_tap_count; i ++) {
        float d = fabsf(audio_tap_l[i] - audio_tap_r[i]);
        diff = d > diff ? d : diff;
    }

    test_assert(diff > 0.05f);
    audio_run(world, 40);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    ecs_fini(world);
}

/** A patch of one pluck string without any envelope, which rings out on its own.
 *
 * @param frequency Hz
 * @param decay Seconds of the fundamental to -60 dB
 * @param brightness Brightness of the loop, 0..1
 * @param seed Seed of the noise of the pluck
 * @return The patch
 */
static box_audio_synth_patch_t synth_pluck(
    float frequency,
    float decay,
    float brightness,
    uint32_t seed)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.seed = seed;
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_PLUCK, .frequency = frequency, .level = 0.5f,
        .decay = decay, .brightness = brightness, .hardness = 0.5f, .rings_free = 1 };

    return p;
}

/** Component of a range at a frequency over a Hann window, the phase relative to frame 0.
 *
 * @param x Samples
 * @param from First frame of the window
 * @param window Frames of the window
 * @param frequency Hz
 * @param rate Frames per second
 * @param re Receives the real part
 * @param im Receives the imaginary part
 */
static void synth_component(
    const float *x,
    int32_t from,
    int32_t window,
    double frequency,
    double rate,
    double *re,
    double *im)
{
    double r = 0, m = 0, w, ph;
    int32_t i;
    for (i = 0; i < window; i ++) {
        w = 0.5 - 0.5 * cos(2 * AUDIO_PI * i / window);
        ph = 2 * AUDIO_PI * frequency * (double)(from + i) / rate;
        r += w * x[from + i] * cos(ph);
        m -= w * x[from + i] * sin(ph);
    }

    *re = r;
    *im = m;
}

/** Frequency of the fundamental of a range near a guess, from the advance of its phase between
 * two Hann windows a gap apart: exact to a small share of a cent, harmonics or not, as long as
 * the guess is within rate / (2 gap) of it.
 *
 * @param x Samples
 * @param from First frame of the first window
 * @param gap Frames between the windows
 * @param window Frames of a window
 * @param guess Hz
 * @param rate Frames per second
 * @return Frequency in Hz
 */
static double synth_pitch(
    const float *x,
    int32_t from,
    int32_t gap,
    int32_t window,
    double guess,
    double rate)
{
    double r0, i0, r1, i1, phase;
    synth_component(x, from, window, guess, rate, &r0, &i0);
    synth_component(x, from + gap, window, guess, rate, &r1, &i1);
    phase = atan2(i1 * r0 - r1 * i0, r1 * r0 + i1 * i0);

    return guess + phase * rate / (2 * AUDIO_PI * gap);
}

/** Time until a render first falls 60 dB under its loudest window, in windows of 10 ms.
 *
 * @param x Samples
 * @param frames Frames of the render
 * @param rate Frames per second
 * @return Seconds, -1 when it never does
 */
static double synth_t60(
    const float *x,
    int32_t frames,
    double rate)
{
    int32_t window = (int32_t)(rate / 100), i, w, count = frames / window;
    double top = 0, result = -1;
    for (w = 0; w < count; w ++) {
        double sum = 0;
        for (i = 0; i < window; i ++) {
            sum += (double)x[w * window + i] * x[w * window + i];
        }

        sum = sqrt(sum / window);
        if (sum > top) {
            top = sum;
        } else if (result < 0 && sum < top * 1e-3) {
            result = (double)(w * window) / rate;
        }
    }

    return result;
}

/** A pluck string plays its pitch within 2 cents from 80 Hz to 2 kHz at 44.1 and 48 kHz: the
 * allpass tunes the fraction of a frame, which a delay line of whole frames would miss by up
 * to 70 cents at 2 kHz. */
void Synth_pluck_pitch(void) {
    // audio/synth
    const float rates[] = { 44100, 48000 };
    const float frequencies[] = { 80, 110, 146.83f, 220, 330, 440, 659.25f, 987.77f, 1500, 2000 };
    int32_t r, i;
    for (r = 0; r < 2; r ++) {
        for (i = 0; i < 10; i ++) {
            box_audio_core_t *core = synth_core(rates[r]);
            box_audio_synth_patch_t p = synth_pluck(frequencies[i], 4, 0.3f, 1);
            int32_t from = (int32_t)(rates[r] * 0.05f), gap = (int32_t)(rates[r] * 0.05f);
            int32_t window = (int32_t)(rates[r] * 0.1f);
            double f, cents;
            synth_define(core, 1, &p);
            audio_play(core, 1, 1, 0, 1, 0, 1, 0);
            audio_render(core, 0, from + gap + window);
            test_assert(synth_finite(audio_l, 0, from + gap + window));
            f = synth_pitch(audio_l, from, gap, window, frequencies[i], rates[r]);
            cents = 1200 * log2(f / frequencies[i]);
            printf("pluck %.2f Hz at %.0f: %.4f Hz, %.3f cents\n", (double)frequencies[i],
                (double)rates[r], f, cents);
            test_assert(fabs(cents) < 2);
            box_audio_core_free(core);
        }
    }
}

/** The -60 dB time of a pluck string follows its decay: twice the decay rings twice as long. */
void Synth_pluck_decay(void) {
    // audio/synth
    const float decays[] = { 0.15f, 0.3f };
    double t[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck(220, decays[i], 0.5f, 1);
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        t[i] = synth_t60(audio_l, AUDIO_FRAMES, AUDIO_RATE);
        printf("pluck decay %.2f s: -60 dB after %.3f s\n", (double)decays[i], t[i]);
        test_assert(t[i] > 0.5 * decays[i] && t[i] < 1.2 * decays[i]);
        box_audio_core_free(core);
    }

    test_assert(t[1] / t[0] > 1.7 && t[1] / t[0] < 2.3);
}

/** A brighter string keeps more energy in its upper harmonics than a dark one: the share of
 * harmonics 5 to 12 of 220 Hz against the fundamental, 0.1 s after the pluck. */
void Synth_pluck_brightness(void) {
    // audio/synth
    const float brightness[] = { 0.1f, 0.9f };
    double share[2];
    int32_t i, k;
    for (i = 0; i < 2; i ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck(220, 2, brightness[i], 1);
        double high = 0, low;
        p.layer[0].hardness = 1;
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 14400);
        low = synth_bin_energy(audio_l + 4800, 9600, 220, AUDIO_RATE);
        for (k = 5; k <= 12; k ++) {
            high += synth_bin_energy(audio_l + 4800, 9600, 220.0 * k, AUDIO_RATE);
        }

        share[i] = high / low;
        printf("pluck brightness %.1f: harmonics 5..12 at %.4f of the fundamental\n",
            (double)brightness[i], share[i]);
        box_audio_core_free(core);
    }

    test_assert(share[1] > 4 * share[0]);
}

/** The same seed plucks bit identical strings, another seed another one. */
void Synth_pluck_seed(void) {
    // audio/synth
    const uint32_t seeds[] = { 3, 3, 4 };
    uint64_t hash[3];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck(330, 1, 0.5f, seeds[i]);
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 9600);
        test_assert(audio_peak(audio_l, 0, 9600) > 0.1f);
        hash[i] = audio_hash(audio_l, 9600);
        box_audio_core_free(core);
    }

    test_assert(hash[0] == hash[1]);
    test_assert(hash[0] != hash[2]);
}

/** A pluck layer of one unison voice, or none, renders bit identical to one without unison,
 * whatever its detune and width, and stays mono; a string next to a wide unison layer rings in
 * the center of the stereo voice and ends it. */
void Synth_pluck_unison(void) {
    // audio/synth
    box_audio_core_t *core;
    box_audio_synth_patch_t p;
    uint64_t hash[3];
    int32_t i;
    float diff = 0;
    for (i = 0; i < 3; i ++) {
        core = synth_core(AUDIO_RATE);
        p = synth_pluck(330, 1, 0.5f, 3);
        if (i > 0) {
            p.layer[0].voices = (uint32_t)(i - 1);
            p.layer[0].detune = 30;
            p.layer[0].width = 1;
        }

        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 9600);
        test_assert(audio_peak(audio_l, 0, 9600) > 0.1f);
        test_assert(memcmp(audio_l, audio_r, 9600 * sizeof(float)) == 0);
        hash[i] = audio_hash(audio_l, 9600);
        box_audio_core_free(core);
    }

    test_assert(hash[0] == hash[1]);
    test_assert(hash[0] == hash[2]);

    core = synth_core(AUDIO_RATE);
    p = synth_pluck(220, 0.1f, 0.5f, 3);
    p.layers = 2;
    p.layer[1] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110, .level = 0.2f, .has_envelope = 1,
        .envelope = { .attack = 0.001f, .sustain = 1, .hold = 0.05f, .release = 0.05f },
        .voices = 3, .detune = 20, .width = 1 };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(synth_finite(audio_r, 0, AUDIO_FRAMES));
    test_assert(audio_peak(audio_l, 0, 4800) > 0.1f);
    for (i = 0; i < 4800; i ++) {
        float d = fabsf(audio_l[i] - audio_r[i]);
        diff = d > diff ? d : diff;
    }

    test_assert(diff > 0.01f);
    test_assert(synth_last(audio_l, 0, AUDIO_FRAMES) < (int32_t)(0.5 * AUDIO_RATE));
    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);
}

/** A string ends its voice once it is silent: without an envelope, and with an envelope that
 * would hold for seconds. */
void Synth_pluck_ends(void) {
    // audio/synth
    int32_t i, last;
    for (i = 0; i < 2; i ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck(440, 0.1f, 0.5f, 1);
        if (i == 1) {
            p.layer[0].rings_free = 0;
            p.envelope = (box_audio_synth_envelope_t){
                .attack = 0.001f, .sustain = 1, .hold = 5, .release = 0.1f };
        }

        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        last = synth_last(audio_l, 0, AUDIO_FRAMES);
        printf("pluck %d: last sound at %.3f s\n", (int)i, (double)last / AUDIO_RATE);
        test_assert(last > (int32_t)(0.08 * AUDIO_RATE));
        test_assert(last < (int32_t)(0.3 * AUDIO_RATE));
        test_int(box_audio_core_voice_count(core), 0);
        box_audio_core_free(core);
    }
}

/** A string follows the pitch of its voice: a glide moves it to the new pitch, and a jump
 * lands it there within 2 cents. */
void Synth_pluck_glide(void) {
    // audio/synth
    int32_t i;
    for (i = 0; i < 2; i ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck(220, 4, 0.3f, 1);
        double f, cents;
        p.glide = i ? 0.05f : 0;
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 4800);
        audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 1.5f);
        audio_render(core, 0, 4800 + 2400 + 4800);
        test_assert(synth_finite(audio_l, 0, 12000));
        f = synth_pitch(audio_l, 4800, 2400, 4800, 330, AUDIO_RATE);
        cents = 1200 * log2(f / 330);
        printf("pluck glide %d: %.4f Hz, %.3f cents from 330\n", (int)i, f, cents);
        test_assert(fabs(cents) < 2);
        test_int(box_audio_core_voice_count(core), 1);
        box_audio_core_free(core);
    }
}

/** Strings come from a pool of BOX_AUDIO_SYNTH_STRINGS: a play that finds none free is
 * ignored, and the strings come back when the voices end. */
void Synth_pluck_pool(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_pluck(440, 0.05f, 0.5f, 1);
    int32_t i;
    synth_define(core, 1, &p);
    for (i = 0; i < BOX_AUDIO_SYNTH_STRINGS; i ++) {
        audio_play(core, (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_SYNTH_STRINGS);
    audio_play(core, 1000, 1, 0, 0.01f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 1);
    audio_render(core, 0, AUDIO_FRAMES);
    test_int(box_audio_core_voice_count(core), 0);
    audio_events(core, 0, NULL);
    for (i = 0; i < BOX_AUDIO_SYNTH_STRINGS; i ++) {
        audio_play(core, 2000 + (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_SYNTH_STRINGS);
    box_audio_core_free(core);
}

/** A pluck patch of one layer with unison voices.
 *
 * @param frequency Hz
 * @param decay Seconds of the fundamental to -60 dB
 * @param voices Unison voices, a string each
 * @param detune Cents from the lowest string to the highest
 * @param width Stereo width, 0..1
 * @param seed Seed of the noise of the plucks
 * @return The patch
 */
static box_audio_synth_patch_t synth_pluck_unison(
    float frequency,
    float decay,
    uint32_t voices,
    float detune,
    float width,
    uint32_t seed)
{
    box_audio_synth_patch_t p = synth_pluck(frequency, decay, 0.3f, seed);
    p.layer[0].voices = voices;
    p.layer[0].detune = detune;
    p.layer[0].width = width;

    return p;
}

/** The strings of a unison pluck ring at the detuned frequencies: the spectrum has a peak at
 * each, against little between them, where a single string has none. */
void Synth_pluck_unison_peaks(void) {
    // audio/synth
    const uint32_t voices[] = { 3, 5 };
    const int32_t from = 2400, n = AUDIO_FRAMES - 2400;
    int32_t c, k;
    for (c = 0; c < 2; c ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck_unison(440, 4, voices[c], 100, 0, 7);
        double f[BOX_AUDIO_SYNTH_UNISON_MAX], lo = 1e300, hi = 0, between = 0, power, single;
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        box_audio_core_free(core);
        test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
        for (k = 0; k < (int32_t)voices[c]; k ++) {
            double cents = 100 * ((double)k / (voices[c] - 1) - 0.5);
            f[k] = 440 * pow(2, cents / 1200);
            power = synth_hann_power(audio_l + from, n, f[k], AUDIO_RATE);
            printf("%u strings over 100 cents: %.2f Hz at %g\n", voices[c], f[k], power);
            lo = power < lo ? power : lo;
            hi = power > hi ? power : hi;
        }

        for (k = 0; k + 1 < (int32_t)voices[c]; k ++) {
            power = synth_hann_power(audio_l + from, n, 0.5 * (f[k] + f[k + 1]), AUDIO_RATE);
            between = power > between ? power : between;
        }

        printf("between the peaks: %g\n", between);
        test_assert(hi / lo < 20);
        test_assert(lo > 20 * between);

        core = synth_core(AUDIO_RATE);
        p = synth_pluck_unison(440, 4, 1, 100, 0, 7);
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        box_audio_core_free(core);
        single = synth_hann_power(audio_l + from, n, f[0], AUDIO_RATE);
        printf("a single string at %.2f Hz: %g\n", f[0], single);
        test_assert(lo > 20 * single);
    }
}

/** A unison pluck at width 0 renders the same samples on both channels; at width 1 the two
 * strings of 2 voices sit one on each channel and decorrelate them, and each is plucked with
 * its own noise, so two strings of the same pitch differ too. */
void Synth_pluck_unison_width(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_pluck_unison(330, 2, 4, 30, 0, 5);
    double ll = 0, rr = 0, lr = 0, corr;
    int32_t i;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0.05f);
    test_assert(memcmp(audio_l, audio_r, AUDIO_FRAMES * sizeof(float)) == 0);

    core = synth_core(AUDIO_RATE);
    p = synth_pluck_unison(330, 2, 2, 50, 1, 5);
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    box_audio_core_free(core);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(synth_finite(audio_r, 0, AUDIO_FRAMES));
    for (i = AUDIO_RATE / 100; i < AUDIO_FRAMES; i ++) {
        ll += (double)audio_l[i] * audio_l[i];
        rr += (double)audio_r[i] * audio_r[i];
        lr += (double)audio_l[i] * audio_r[i];
    }

    corr = lr / sqrt(ll * rr);
    printf("2 strings at width 1: correlation %.3f, levels %.4f %.4f\n", corr, sqrt(ll),
        sqrt(rr));
    test_assert(ll > 0 && rr > 0);
    test_assert(fabs(corr) < 0.2);

    core = synth_core(AUDIO_RATE);
    p = synth_pluck_unison(330, 2, 2, 0, 1, 5);
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 9600);
    box_audio_core_free(core);
    test_assert(audio_peak(audio_l, 0, 9600) > 0.05f);
    test_assert(audio_peak(audio_r, 0, 9600) > 0.05f);
    test_assert(memcmp(audio_l, audio_r, 9600 * sizeof(float)) != 0);
}

/** A unison pluck takes a string of the pool for each voice: 8 plays of 4 voices take all
 * BOX_AUDIO_SYNTH_STRINGS, so a play of 4 voices and one of a single string are ignored.
 *
 * With 28 taken one of 5 voices is ignored and one of 4 fits. The strings come back when the
 * voices end, the last of their strings silent, so the same plays fit again. */
void Synth_pluck_unison_pool(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t four = synth_pluck_unison(440, 0.05f, 4, 20, 1, 1);
    box_audio_synth_patch_t five = synth_pluck_unison(440, 0.05f, 5, 20, 1, 1);
    box_audio_synth_patch_t one = synth_pluck(440, 0.05f, 0.5f, 1);
    int32_t i, plays = BOX_AUDIO_SYNTH_STRINGS / 4;
    synth_define(core, 1, &four);
    synth_define(core, 2, &five);
    synth_define(core, 3, &one);
    for (i = 0; i < plays; i ++) {
        audio_play(core, (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), plays);
    audio_play(core, 1000, 1, 0, 0.01f, 0, 1, 0);
    audio_play(core, 1001, 3, 0, 0.01f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 2);
    test_int(box_audio_core_voice_count(core), plays);
    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    test_assert(synth_finite(audio_r, 0, AUDIO_FRAMES));
    test_int(box_audio_core_voice_count(core), 0);
    audio_events(core, 0, NULL);

    for (i = 0; i < plays - 1; i ++) {
        audio_play(core, 2000 + (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    audio_play(core, 3000, 2, 0, 0.01f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 1);
    audio_play(core, 3001, 1, 0, 0.01f, 0, 1, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), plays);
    audio_render(core, 0, AUDIO_FRAMES);
    test_int(box_audio_core_voice_count(core), 0);
    audio_events(core, 0, NULL);

    for (i = 0; i < plays; i ++) {
        audio_play(core, 4000 + (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    test_int(box_audio_core_voice_count(core), plays);
    box_audio_core_free(core);
}

/** The same seed renders a wide unison pluck bit identical; another seed, and the next play of
 * the same patch, pluck the strings with other noise. */
void Synth_pluck_unison_seed(void) {
    // audio/synth
    box_audio_synth_patch_t p = synth_pluck_unison(262, 1, 3, 20, 0.8f, 9);
    uint64_t a, b, c, a2, b2, c2;
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 3000;
    a = synth_unison_hash(&p, &a2);
    b = synth_unison_hash(&p, &b2);
    p.seed = 10;
    c = synth_unison_hash(&p, &c2);
    printf("pluck unison seed 9: 0x%016llXull\n", (unsigned long long)a);
    test_assert(a == b);
    test_assert(a2 == b2);
    test_assert(a != a2);
    test_assert(a != c);
}

/** Fill a core to the voice cap with pluck voices that take every string and sine voices.
 *
 * @param core The core
 */
static void synth_pluck_cap_fill(
    box_audio_core_t *core)
{
    box_audio_synth_patch_t pluck = synth_pluck(440, 0.5f, 0.5f, 1);
    box_audio_synth_patch_t sine = synth_steady(BOX_AUDIO_SYNTH_SINE, 440, 0.5f, 0.5f);
    int32_t i;
    synth_define(core, 1, &pluck);
    synth_define(core, 2, &sine);
    for (i = 0; i < BOX_AUDIO_SYNTH_STRINGS; i ++) {
        audio_play(core, 100 + (uint32_t)i, 1, 0, 0.01f, 0, 1, 0);
    }

    for (i = BOX_AUDIO_SYNTH_STRINGS; i < BOX_AUDIO_VOICE_MAX; i ++) {
        audio_play(core, 100 + (uint32_t)i, 2, 0, 0.02f, 0, 1, 0);
    }
}

/** A pluck play rejected for its strings at the voice cap leaves every voice as it was: the cap
 * steals no voice for it, no voice ends, and the core renders bit identical to one that never
 * got the play. A play that fits still steals at the cap.
 *
 * With 8 layers at most and every synth voice reclaimable, no synth play is rejected for parts,
 * so this is the rejection that remains. */
void Synth_pluck_pool_cap(void) {
    // audio/synth
    static float expect[4800];
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_core_t *ref = synth_core(AUDIO_RATE);
    int32_t counts[BOX_AUDIO_EVENT_LOST + 1];
    box_audio_event_t stolen;
    synth_pluck_cap_fill(core);
    synth_pluck_cap_fill(ref);
    synth_pool_events(core, counts, &stolen);
    test_int(counts[BOX_AUDIO_EVENT_IGNORED], 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);

    audio_play(core, 1000, 1, 0, 0.01f, 0, 1, 0);
    synth_pool_events(core, counts, &stolen);
    test_int(counts[BOX_AUDIO_EVENT_IGNORED], 1);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_STOLEN], 0);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_END], 0);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);

    audio_play(core, 1001, 2, 0, 0.02f, 0, 1, 0);
    synth_pool_events(core, counts, &stolen);
    test_int(counts[BOX_AUDIO_EVENT_IGNORED], 0);
    test_int(counts[BOX_AUDIO_EVENT_VOICE_STOLEN], 1);
    test_uint(stolen.id, 100);
    test_int(stolen.value, 1);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);

    audio_play(ref, 1001, 2, 0, 0.02f, 0, 1, 0);
    audio_render(ref, 0, 4800);
    memcpy(expect, audio_l, sizeof(expect));
    audio_render(core, 0, 4800);
    test_assert(memcmp(expect, audio_l, sizeof(expect)) == 0);
    test_int(box_audio_core_voice_count(core), box_audio_core_voice_count(ref));
    box_audio_core_free(core);
    box_audio_core_free(ref);
}

/** A SynthPluck layer takes its SynthString, or the defaults without one, needs no envelope,
 * and a frequency under BOX_AUDIO_SYNTH_PLUCK_LOWEST is clamped with one warning. */
void Synth_script_pluck(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    const box_audio_synth_patch_t *p;
    ecs_entity_t low;
    int32_t start;
    audio_log_start();
    test_assert(ecs_script_run(world, "pluck",
        "using box.audio\n"
        "prefab Plain {\n"
        "  SynthSound: {}\n"
        "  SynthOscillator: {wave: SynthPluck, frequency: 220, level: 0.5}\n"
        "}\n"
        "prefab Low {\n"
        "  SynthSound: {}\n"
        "  SynthOscillator: {wave: SynthPluck, frequency: 30, level: 0.5}\n"
        "  SynthString: {decay: 0.2, brightness: 0.25, hardness: 0.3}\n"
        "}\n", NULL) == 0);
    audio_run(world, 2);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_int(audio_log_warnings, 1);

    p = &ecs_get(world, ecs_lookup(world, "Plain"), AudioSynth)->patch;
    test_int(p->layers, 1);
    test_int(p->layer[0].wave, BOX_AUDIO_SYNTH_PLUCK);
    test_int(p->layer[0].rings_free, 1);
    test_flt(p->layer[0].decay, 2);
    test_flt(p->layer[0].brightness, 0.5f);
    test_flt(p->layer[0].hardness, 0.8f);

    low = ecs_lookup(world, "Low");
    p = &ecs_get(world, low, AudioSynth)->patch;
    test_flt(p->layer[0].frequency, BOX_AUDIO_SYNTH_PLUCK_LOWEST);
    test_flt(p->layer[0].decay, 0.2f);
    test_flt(p->layer[0].brightness, 0.25f);
    test_flt(p->layer[0].hardness, 0.3f);

    ecs_set(world, low, SynthString, { .decay = 0.1f, .brightness = 0.5f, .hardness = 1 });
    audio_run(world, 1);
    test_flt(ecs_get(world, low, AudioSynth)->patch.layer[0].decay, 0.1f);

    start = audio_tap_count;
    test_assert(box_audio_play(world, low, 1.0f));
    audio_run(world, 30);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.1f);
    test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** The guitar, the 12 string guitar and the koto of docs/audio.md and the harp of the Mossvale
 * score compile without errors, play and end on their own.
 *
 * The guitars, the 12 string with a pair of strings in unison, end within 280 frames of
 * AUDIO_DT, the koto and two notes of the harp within 230. */
void Synth_docs_pluck_examples(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_pluck.flecs");
    const char *names[] = { "Koto", "hp_c3", "hp_a4", "Guitar", "TwelveString" };
    const box_audio_synth_patch_t *p;
    int32_t i, start;
    audio_log_start();
    test_assert(ecs_script_run(world, "pluck", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);
    for (i = 0; i < 5; i ++) {
        ecs_entity_t sound = ecs_lookup(world, names[i]);
        test_assert(sound != 0);
        test_assert(ecs_get(world, sound, AudioSynth)->compiled);
    }

    p = &ecs_get(world, ecs_lookup(world, "TwelveString"), AudioSynth)->patch;
    test_int(p->layers, 2);
    test_int(p->layer[0].wave, BOX_AUDIO_SYNTH_PLUCK);
    test_int(p->layer[0].voices, 2);
    test_flt(p->layer[0].detune, 6);
    test_flt(p->layer[0].width, 0.5f);
    test_int(p->layer[1].voices, 1);

    start = audio_tap_count;
    test_assert(box_audio_play(world, ecs_lookup(world, "Guitar"), 1.0f));
    test_assert(box_audio_play(world, ecs_lookup(world, "TwelveString"), 1.0f));
    audio_run(world, 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 2);
    audio_run(world, 278);
    test_assert(audio_peak(audio_tap_r, start, audio_tap_count) > 0.05f);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    start = audio_tap_count;
    for (i = 0; i < 3; i ++) {
        test_assert(box_audio_play(world, ecs_lookup(world, names[i]), 1.0f));
    }

    audio_run(world, 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 3);
    audio_run(world, 228);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(synth_finite(audio_tap_l, start, audio_tap_count));
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_flt(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count), 0);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    ecs_fini(world);
}

/** Cents of the fundamental of a render near a frequency, measured around a frame from the
 * advance of its phase over 10 ms.
 *
 * @param x Samples
 * @param center Frame in the middle of the measure
 * @param frequency Hz the cents are from
 * @return Cents
 */
static double synth_cents_at(
    const float *x,
    int32_t center,
    double frequency)
{
    int32_t gap = AUDIO_RATE / 100, window = AUDIO_RATE / 50;
    double f = synth_pitch(x, center - (gap + window) / 2, gap, window, frequency, AUDIO_RATE);

    return 1200 * log2(f / frequency);
}

/** LFOs move a layer of unison voices in stereo: a tremolo scales both channels of the same
 * render by its dip, so the voices keep their phases and detune, and a vibrato moves the pitch
 * of every voice by its cents at the top and the bottom of the wave. */
void Synth_lfo_unison(void) {
    // audio/synth
    enum { FRAMES = 24000 };
    static float plain_l[FRAMES], plain_r[FRAMES];
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_unison(BOX_AUDIO_SYNTH_SAW, 220, 0.3f, 5, 20, 1, 0.6f);
    double expected, dip, err = 0, top, bottom;
    float diff = 0;
    int32_t i;
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, FRAMES);
    memcpy(plain_l, audio_l, sizeof(plain_l));
    memcpy(plain_r, audio_r, sizeof(plain_r));
    for (i = 0; i < FRAMES; i ++) {
        float d = fabsf(plain_l[i] - plain_r[i]);
        diff = d > diff ? d : diff;
    }

    test_assert(diff > 0.05f);
    box_audio_core_free(core);

    core = synth_core(AUDIO_RATE);
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 4, .amplitude = 0.5f };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, FRAMES);
    for (i = 100; i < FRAMES - 1000; i ++) {
        dip = 1 - 0.5 * 0.5 * (1 - sin(2 * AUDIO_PI * 4 * (i + 1) / AUDIO_RATE));
        expected = plain_l[i] * dip;
        err = fabs(audio_l[i] - expected) > err ? fabs(audio_l[i] - expected) : err;
        expected = plain_r[i] * dip;
        err = fabs(audio_r[i] - expected) > err ? fabs(audio_r[i] - expected) : err;
    }

    printf("unison tremolo: %.6f from the expected at most\n", err);
    test_assert(err < 2e-3);
    box_audio_core_free(core);

    core = synth_core(AUDIO_RATE);
    p = synth_unison(BOX_AUDIO_SYNTH_SINE, 400, 0.3f, 3, 0, 0.5f, 0.6f);
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2, .pitch = 100 };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, FRAMES);
    test_assert(synth_finite(audio_l, 0, FRAMES));
    top = synth_cents_at(audio_l, AUDIO_RATE / 8, 400);
    bottom = synth_cents_at(audio_r, AUDIO_RATE * 3 / 8, 400);
    printf("unison vibrato: %.2f cents at the top, %.2f at the bottom\n", top, bottom);
    test_assert(fabs(top - 100) < 5);
    test_assert(fabs(bottom + 100) < 5);
    box_audio_core_free(core);
}

/** An LFO moves a pluck string: its pitch swings by the cents of a vibrato at the top and the
 * bottom of the wave, and the string still rings out and ends the voice. */
void Synth_lfo_pluck(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t p = synth_pluck(220, 1, 0.3f, 1);
    double top, bottom, back;
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){
        .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 2, .pitch = 50 };
    synth_define(core, 1, &p);
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    top = synth_cents_at(audio_l, AUDIO_RATE / 8, 220);
    bottom = synth_cents_at(audio_l, AUDIO_RATE * 3 / 8, 220);
    back = synth_cents_at(audio_l, AUDIO_RATE * 5 / 8, 220);
    printf("pluck vibrato: %.2f, %.2f and %.2f cents\n", top, bottom, back);
    test_assert(fabs(top - 50) < 4);
    test_assert(fabs(bottom + 50) < 4);
    test_assert(fabs(back - 50) < 4);
    audio_render(core, 0, AUDIO_FRAMES);
    test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
    test_int(box_audio_core_voice_count(core), 0);
    box_audio_core_free(core);
}

/** Swing of a series of cents measured a step of frames apart from a frame on, at a rate: the
 * amplitude of its component at the rate, its mean left out.
 *
 * @param cents The series
 * @param count Points of the series
 * @param from Frame of the first point
 * @param step Frames between the points
 * @param rate Hz
 * @return Cents either way
 */
static double synth_swing(
    const double *cents,
    int32_t count,
    int32_t from,
    int32_t step,
    double rate)
{
    double mean = 0, re = 0, im = 0, ph;
    int32_t i;
    for (i = 0; i < count; i ++) {
        mean += cents[i];
    }

    mean /= count;
    for (i = 0; i < count; i ++) {
        ph = 2 * AUDIO_PI * rate * (double)(from + i * step) / AUDIO_RATE;
        re += (cents[i] - mean) * cos(ph);
        im += (cents[i] - mean) * sin(ph);
    }

    return 2 * sqrt(re * re + im * im) / count;
}

/** Energy of a range above 8 kHz: the components from 8 kHz to the Nyquist frequency every
 * 100 Hz over Hann windows of 20 ms, half a window apart.
 *
 * @param x Samples
 * @param from First frame
 * @param frames Frames of the range, at least a window
 * @return The energy
 */
static double synth_high(
    const float *x,
    int32_t from,
    int32_t frames)
{
    const int32_t window = AUDIO_RATE / 50;
    double sum = 0, re, im, f;
    int32_t at;
    for (at = from; at + window <= from + frames; at += window / 2) {
        for (f = 8000; f < AUDIO_RATE / 2; f += 100) {
            synth_component(x, at, window, f, AUDIO_RATE, &re, &im);
            sum += re * re + im * im;
        }
    }

    return sum;
}

/** A vibrato moves a pluck string by its depth at its rate: the cents of the string over
 * windows of 30 ms swing at the rate of the LFO, more than at any other rate, and by the swing
 * of a sine oscillator under the same LFO measured the same way, within 10 %. */
void Synth_pluck_vibrato_rate(void) {
    // audio/synth
    enum { POINTS = 161, STEP = 240, FROM = 4800 };
    static double cents[2][POINTS];
    double depth[2], top = 0, top_rate = 0, swing, r;
    int32_t k, i;
    for (k = 0; k < 2; k ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p;
        if (k) {
            p = synth_lfo_patch(BOX_AUDIO_SYNTH_SINE, 220, 0.5f, 1.2f, 5);
        } else {
            p = synth_pluck(220, 4, 0.3f, 1);
            p.lfos = 1;
            p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5 };
        }

        p.lfo[0].pitch = 30;
        synth_define(core, 1, &p);
        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
        for (i = 0; i < POINTS; i ++) {
            cents[k][i] = synth_cents_at(audio_l, FROM + i * STEP, 220);
        }

        depth[k] = synth_swing(cents[k], POINTS, FROM, STEP, 5);
        box_audio_core_free(core);
    }

    for (r = 2; r <= 10; r += 0.5) {
        swing = synth_swing(cents[0], POINTS, FROM, STEP, r);
        if (swing > top) {
            top = swing;
            top_rate = r;
        }
    }

    printf("pluck vibrato: %.2f cents at 5 Hz, the sine %.2f, the largest swing at %.1f Hz\n",
        depth[0], depth[1], top_rate);
    test_assert(fabs(top_rate - 5) < 0.01);
    test_assert(depth[1] > 0.8 * 30 && depth[1] < 1.05 * 30);
    test_assert(fabs(depth[0] - depth[1]) < 0.1 * depth[1]);
}

/** A string crosses whole frames of its delay line smoothly: a glide of an octave over 20 ms
 * has under 0.4 and a vibrato of a semitone at 8 Hz under 0.05 of the energy above 8 kHz of
 * the same renders in which the read point jumps at each crossing.
 *
 * The glide keeps some, as its allpass sweeps its whole range every 9 frames. */
void Synth_pluck_crossing_smooth(void) {
    // audio/synth
    double glide[2], vibrato[2];
    int32_t r;
    for (r = 0; r < 2; r ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck(220, 4, 0.3f, 1);
        p.glide = 0.02f;
        synth_define(core, 1, &p);
        if (r) {
            audio_strings_rough(core);
        }

        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, 4800);
        audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 2);
        audio_render(core, 4800, 4800);
        test_assert(synth_finite(audio_l, 0, 9600));
        glide[r] = synth_high(audio_l, 4320, 2400);
        box_audio_core_free(core);

        core = synth_core(AUDIO_RATE);
        p = synth_pluck(220, 4, 0.3f, 1);
        p.lfos = 1;
        p.lfo[0] = (box_audio_synth_lfo_t){
            .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 8, .pitch = 100 };
        synth_define(core, 1, &p);
        if (r) {
            audio_strings_rough(core);
        }

        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
        vibrato[r] = synth_high(audio_l, 4800, 38400);
        box_audio_core_free(core);
    }

    printf("pluck crossings above 8 kHz: glide %.3g smooth, %.3g rough; vibrato %.3g smooth, "
        "%.3g rough\n", glide[0], glide[1], vibrato[0], vibrato[1]);
    test_assert(glide[0] < 0.4 * glide[1]);
    test_assert(vibrato[0] < 0.05 * vibrato[1]);
}

/** A string whose pitch does not move renders bit identical under an LFO without a pitch
 * depth, under a pitch LFO before its delay, and with the crossfade switched off. */
void Synth_pluck_steady_identical(void) {
    // audio/synth
    static float plain[AUDIO_FRAMES];
    int32_t k;
    for (k = 0; k < 4; k ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck(330, 2, 0.5f, 7);
        if (k == 1) {
            p.lfos = 1;
            p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5 };
        } else if (k == 2) {
            p.lfos = 1;
            p.lfo[0] = (box_audio_synth_lfo_t){
                .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5, .pitch = 20, .delay = 0.5f };
        }

        synth_define(core, 1, &p);
        if (k == 3) {
            audio_strings_rough(core);
        }

        audio_play(core, 1, 1, 0, 1, 0, 1, 0);
        audio_render(core, 0, AUDIO_FRAMES);
        test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
        if (k == 0) {
            memcpy(plain, audio_l, sizeof(plain));
        } else {
            int32_t n = k == 2 ? AUDIO_FRAMES / 2 - 100 : AUDIO_FRAMES;
            test_assert(memcmp(plain, audio_l, sizeof(float) * (size_t)n) == 0);
        }

        box_audio_core_free(core);
    }
}

/** The cost of 32 strings, one per voice, under a vibrato against the same strings without
 * one, in ns per string per frame. It prints the cost; the strings all ring. */
void Synth_pluck_vibrato_cost(void) {
    // audio/synth
    double ns[2];
    int32_t k, i;
    for (k = 0; k < 2; k ++) {
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        box_audio_synth_patch_t p = synth_pluck(110, 4, 0.5f, 1);
        clock_t start;
        if (k) {
            p.lfos = 1;
            p.lfo[0] = (box_audio_synth_lfo_t){
                .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5.5f, .pitch = 25 };
        }

        synth_define(core, 1, &p);
        for (i = 0; i < BOX_AUDIO_SYNTH_STRINGS; i ++) {
            audio_play(core, 1 + (uint32_t)i, 1, 0, 0.02f, 0, 1 + 0.07f * (float)i, 0);
        }

        test_int(box_audio_core_voice_count(core), BOX_AUDIO_SYNTH_STRINGS);
        start = clock();
        audio_render(core, 0, AUDIO_FRAMES);
        ns[k] = (double)(clock() - start) / CLOCKS_PER_SEC * 1e9 /
            ((double)AUDIO_FRAMES * BOX_AUDIO_SYNTH_STRINGS);
        test_assert(synth_finite(audio_l, 0, AUDIO_FRAMES));
        test_int(box_audio_core_voice_count(core), BOX_AUDIO_SYNTH_STRINGS);
        box_audio_core_free(core);
    }

    printf("32 strings: %.1f ns per string and frame steady, %.1f under vibrato\n", ns[0], ns[1]);
}

/** SynthFilter has a drive of 0 by default. */
void Synth_filter_drive_default(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    const SynthFilter *f;
    ecs_add(world, e, SynthFilter);
    f = ecs_get(world, e, SynthFilter);
    test_assert(f != NULL);
    test_flt(f->drive, 0);
    test_flt(f->cutoff, 1000);
    ecs_fini(world);
}
