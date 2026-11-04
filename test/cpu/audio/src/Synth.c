/**
 * @file Synth.c
 * @brief The synth of the audio core and synth sounds of box.audio, measured on offline
 * renders: frequencies, aliasing, noise, envelopes, the filter, layers, phase and ring
 * modulation, feedback, eight layers and scripts.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Rate of the aliasing check, at which 5 kHz has an exact bin every 441 frames */
#define SYNTH_ALIAS_RATE 44100

/** Frames of the aliasing check, 0.8 seconds at SYNTH_ALIAS_RATE */
#define SYNTH_ALIAS_FRAMES 35280

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
    uint8_t bytes[1024];
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
    uint8_t bytes[1024];
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

/** Voices take the state of only the layers their patch has, from a pool of 4 per voice slot:
 * every slot plays 4 layers, a play of 8 layers that finds no room is ignored, and the layers
 * come back when the voices end. */
void Synth_layers_pool(void) {
    // audio/synth
    box_audio_core_t *core = synth_core(AUDIO_RATE);
    box_audio_synth_patch_t wide = synth_octet(0.05f);
    box_audio_synth_patch_t four = synth_octet(0.05f);
    const int32_t slots = BOX_AUDIO_VOICE_MAX + BOX_AUDIO_VOICE_TAIL_MAX;
    const int32_t fit = 4 * slots / BOX_AUDIO_SYNTH_LAYERS;
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
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 1);
    test_int(box_audio_core_voice_count(core), fit);
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

/** FNV-1a hash of the bits of samples.
 *
 * @param x Samples
 * @param n Number of samples
 * @return The hash
 */
static uint64_t synth_hash(
    const float *x,
    int32_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t bits;
    int32_t i, b;
    for (i = 0; i < n; i ++) {
        memcpy(&bits, &x[i], sizeof(bits));
        for (b = 0; b < 4; b ++) {
            h ^= (bits >> (8 * b)) & 0xFF;
            h *= 0x100000001B3ull;
        }
    }

    return h;
}

/** Hashes of the renders of Synth_layers_identical by the synth of 4 layers, before the layers
 * became parts of the core */
static const uint64_t synth_golden[4] = { 0, 0, 0, 0 };

/** Patches of up to 4 layers render bit identical to the synth of 4 layers: filters with an
 * envelope at both slopes, sweeps, noise with variances, own envelopes, phase and ring
 * modulation with feedback, a change of pitch and the restarts of a loop. */
void Synth_layers_identical(void) {
    // audio/synth
    box_audio_synth_envelope_t decay = {
        .attack = 0.002f, .decay = 0.5f, .sustain = 0.2f, .hold = 0.3f, .release = 0.1f };
    box_audio_synth_patch_t p[4] = {{0}};
    uint64_t hash;
    int32_t i;
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
        box_audio_core_t *core = synth_core(AUDIO_RATE);
        synth_define(core, 1, &p[i]);
        audio_play(core, 1, 1, 0, 1, 0, 0.9f, i == 3 ? BOX_AUDIO_PLAY_LOOP : 0);
        audio_render(core, 0, AUDIO_FRAMES / 2);
        audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 1.2f);
        audio_render(core, AUDIO_FRAMES / 2, AUDIO_FRAMES / 2);
        hash = synth_hash(audio_l, AUDIO_FRAMES) ^ (synth_hash(audio_r, AUDIO_FRAMES) << 1);
        printf("patch %d: 0x%016llXull\n", (int)i, (unsigned long long)hash);
        test_assert(hash == synth_golden[i]);
        box_audio_core_free(core);
    }
}

/** Read a file of the repository.
 *
 * @param file Path from the root of the repository
 * @return The contents, which the caller frees
 */
static char* synth_read(
    const char *file)
{
    char *path = box_app_locate(file);
    test_assert(path != NULL);
    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *result = ecs_os_malloc((ecs_size_t)size + 1);
    test_assert(fread(result, 1, (size_t)size, f) == (size_t)size);
    result[size] = '\0';
    fclose(f);
    ecs_os_free(path);

    return result;
}

/** Create a world and run test/assets/audio/synth.flecs in it.
 *
 * @return The world
 */
static ecs_world_t* synth_world(void)
{
    ecs_world_t *world = audio_world();
    char *code = synth_read("test/assets/audio/synth.flecs");
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
    char *code = synth_read("test/assets/audio/synth_examples.flecs");
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
    char *code = synth_read("test/assets/audio/synth_examples.flecs");
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
    char *code = synth_read("test/assets/audio/synth_layers.flecs");
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

/** Modulators from test/assets/audio/synth_fm.flecs: a child of a layer modulates it, a
 * grandchild modulates the child, a change of the grandchild compiles the sound again, and a
 * modulator whose parent is not a layer logs one error and is left out. */
void Synth_script_modulators(void) {
    // audio/synth
    ecs_world_t *world = audio_world();
    char *code = synth_read("test/assets/audio/synth_fm.flecs");
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
