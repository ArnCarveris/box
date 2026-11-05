/**
 * @file SynthWavetableAsset.c
 * @brief Wavetables loaded from a sound: the saw and the square frames of
 * test/assets/audio/wavetable_saw_square.wav, the frames of another frame_size, a table whose
 * sound is not ready that plays the fallback and then the table, and a sound that is too short.
 */

#include "world.h"
#include "../../../../src/jobs/jobs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** The script of the tables of the file */
#define WTA_SCRIPT "test/assets/audio/synth_wavetable_asset.flecs"

/** The table of 8 frames of 2048 samples from a saw to a square */
#define WTA_FILE "test/assets/audio/wavetable_saw_square.wav"

/** Frames of a window of the harmonics of a note of 200 Hz, 10 of its periods at AUDIO_RATE */
#define WTA_WINDOW 2400

/** Create a world whose jobs run inline at the end of each frame, so a sound file decodes over
 * a few frames.
 *
 * @return The world
 */
static ecs_world_t* wta_world(void) {
    ecs_world_t *world = audio_world();
    test_int(box_jobs_set_workers(world, 0), 0);

    return world;
}

/** Run the script of the tables of the file in a world.
 *
 * @param world The world
 */
static void wta_script(
    ecs_world_t *world)
{
    char *code = audio_read(WTA_SCRIPT);
    test_assert(ecs_script_run(world, "wavetable_asset", code, NULL) == 0);
    ecs_os_free(code);
}

/** Amplitude of harmonic k of a period, from its DFT.
 *
 * @param x Samples of the period
 * @param len Samples
 * @param k The harmonic
 * @return The amplitude
 */
static double wta_harmonic(
    const float *x,
    int32_t len,
    int32_t k)
{
    double re = 0, im = 0;
    int32_t t;
    for (t = 0; t < len; t ++) {
        re += x[t] * cos(2 * AUDIO_PI * k * t / len);
        im += x[t] * sin(2 * AUDIO_PI * k * t / len);
    }

    return 2 * sqrt(re * re + im * im) / len;
}

/** Energy of the component of a range at a frequency of an exact bin, with a Goertzel filter.
 *
 * @param x Samples
 * @param n Number of samples
 * @param frequency Hz, a whole number of periods in n samples
 * @return The sum of the squares of the component
 */
static double wta_bin_energy(
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

/** Highest harmonic of 200 Hz in a window of the tap that is over -40 dB of the fundamental,
 * up to the 30th.
 *
 * @param from First frame of the window in the tap, WTA_WINDOW frames
 * @return The harmonic, 1 for a sine
 */
static int32_t wta_harmonics(
    int32_t from)
{
    const float *x = audio_tap_l + from;
    double e1 = wta_bin_energy(x, WTA_WINDOW, 200.0);
    int32_t k, result = 1;
    test_assert(e1 > 1e-4);
    for (k = 2; k <= 30; k ++) {
        if (wta_bin_energy(x, WTA_WINDOW, k * 200.0) > 1e-4 * e1) {
            result = k;
        }
    }

    return result;
}

/** The test table loads as 8 frames: level 0 of frame 0 has the harmonics of a saw, each at 1/n
 * of the fundamental, and frame 7 those of a square, the odd ones at 1/n and no even ones; the
 * table is normalized to a peak of 1, and a synth sound that names it plays it. */
void SynthWavetableAsset_saw_to_square(void) {
    // audio/synth
    ecs_world_t *world = wta_world();
    ecs_entity_t file, table, morph;
    const AudioWavetable *w;
    const float *frames;
    float *data, rate = 0, peak = 0;
    int32_t count = 0, channels = 0, table_frames = 0, floats = 0, k, t, start;
    double h1, ratio;
    audio_log_start();
    wta_script(world);
    audio_run(world, 4);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_int(audio_log_warnings, 0);

    file = ecs_lookup(world, "SawSquareFile");
    table = ecs_lookup(world, "SawSquare");
    morph = ecs_lookup(world, "Morph");
    test_assert(file != 0 && table != 0 && morph != 0);
    test_int(ecs_get(world, table, Wavetable)->frame_size, 2048);
    w = ecs_get(world, table, AudioWavetable);
    test_assert(w->id != 0);
    test_int(w->frames, 8);
    test_int(ecs_get(world, morph, AudioSynth)->patch.layer[0].table, w->id);

    frames = box_audio_sample_frames(world, file, &count, &channels, &rate);
    test_assert(frames != NULL);
    test_int(count, 8 * 2048);
    test_int(channels, 1);
    data = box_audio_wavetable_load(frames, count, channels, 2048, &table_frames, &floats);
    test_assert(data != NULL);
    test_int(table_frames, 8);
    test_int(floats, BOX_AUDIO_TABLE_FLOATS(8));
    test_int(audio_table_frames(data, floats, 1), 8);

    for (t = 0; t < 8 * BOX_AUDIO_TABLE_LENGTH; t ++) {
        float v = fabsf(data[BOX_AUDIO_TABLE_HEADER + t]);
        peak = v > peak ? v : peak;
    }

    test_assert(peak > 0.97f && peak < 1.03f);

    const float *saw = data + BOX_AUDIO_TABLE_HEADER;
    const float *square = data + BOX_AUDIO_TABLE_HEADER + 7 * BOX_AUDIO_TABLE_LENGTH;
    h1 = wta_harmonic(saw, BOX_AUDIO_TABLE_LENGTH, 1);
    test_assert(h1 > 0.1);
    for (k = 2; k <= 30; k ++) {
        ratio = wta_harmonic(saw, BOX_AUDIO_TABLE_LENGTH, k) * k / h1;
        if (ratio < 0.97 || ratio > 1.03) {
            printf("harmonic %d of frame 0 at %g of 1/n\n", k, ratio);
        }

        test_assert(ratio > 0.97 && ratio < 1.03);
    }

    h1 = wta_harmonic(square, BOX_AUDIO_TABLE_LENGTH, 1);
    test_assert(h1 > 0.1);
    for (k = 2; k <= 30; k ++) {
        ratio = wta_harmonic(square, BOX_AUDIO_TABLE_LENGTH, k) * k / h1;
        if (k % 2) {
            if (ratio < 0.97 || ratio > 1.03) {
                printf("harmonic %d of frame 7 at %g of 1/n\n", k, ratio);
            }

            test_assert(ratio > 0.97 && ratio < 1.03);
        } else {
            test_assert(ratio < 0.01);
        }
    }

    ecs_os_free(data);

    start = audio_tap_count;
    test_assert(box_audio_play(world, morph, 1.0f));
    audio_run(world, 8);
    test_assert(audio_peak(audio_tap_l, start, audio_tap_count) > 0.05f);
    test_assert(wta_harmonics(start + 2 * AUDIO_FRAME) > 10);
    ecs_fini(world);
}

/** frame_size 1024 cuts the same file into twice the frames; frame_size 0 is the default and
 * the others are clamped to 64..4096. */
void SynthWavetableAsset_frame_size(void) {
    // audio/synth
    ecs_world_t *world = wta_world();
    ecs_entity_t table, half;
    const AudioWavetable *w, *wh;
    test_int(box_audio_wavetable_frame_size(0), 2048);
    test_int(box_audio_wavetable_frame_size(10), 64);
    test_int(box_audio_wavetable_frame_size(1024), 1024);
    test_int(box_audio_wavetable_frame_size(100000), 4096);

    audio_log_start();
    wta_script(world);
    audio_run(world, 4);
    audio_log_stop();
    test_int(audio_log_errors, 0);

    table = ecs_lookup(world, "SawSquare");
    half = ecs_lookup(world, "SawSquareHalf");
    test_assert(table != 0 && half != 0);
    w = ecs_get(world, table, AudioWavetable);
    wh = ecs_get(world, half, AudioWavetable);
    test_assert(w->id != 0 && wh->id != 0 && w->id != wh->id);
    test_int(w->frames, 8);
    test_int(wh->frames, 16);

    ecs_set(world, half, Wavetable, { .sound = ecs_lookup(world, "SawSquareFile"),
        .frame_size = 4096 });
    audio_run(world, 2);
    test_int(ecs_get(world, half, AudioWavetable)->frames, 4);
    ecs_fini(world);
}

/** A table whose sound still decodes has no frames, so its layers play the sine of the
 * fallback; once the sound is ready the table is built, the synth sound takes it, and the
 * next play has the harmonics of the saw of frame 0.
 *
 * The decode completes at the start of the second frame, so the first play is queued before
 * the first frame, while the sound loads. */
void SynthWavetableAsset_not_ready_fallback(void) {
    // audio/synth
    ecs_world_t *world = wta_world();
    ecs_entity_t file, table, morph;
    const AudioWavetable *w;
    int32_t start;
    audio_log_start();
    wta_script(world);
    file = ecs_lookup(world, "SawSquareFile");
    table = ecs_lookup(world, "SawSquare");
    morph = ecs_lookup(world, "Morph");
    test_assert(file != 0 && table != 0 && morph != 0);

    start = audio_tap_count;
    test_assert(box_audio_play(world, morph, 1.0f));
    audio_run(world, 1);
    test_int(ecs_get(world, file, SoundStatus)->state, SoundLoading);
    w = ecs_get(world, table, AudioWavetable);
    test_int(w->id, 0);
    test_int(w->frames, 0);
    test_int(ecs_get(world, morph, AudioSynth)->patch.layer[0].table, 0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    audio_run(world, 6);
    test_int(wta_harmonics(start + 2 * AUDIO_FRAME), 1);

    audio_run(world, 20);
    test_int(ecs_get(world, file, SoundStatus)->state, SoundReady);
    w = ecs_get(world, table, AudioWavetable);
    test_assert(w->id != 0);
    test_int(w->frames, 8);
    test_int(ecs_get(world, morph, AudioSynth)->patch.layer[0].table, w->id);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    start = audio_tap_count;
    test_assert(box_audio_play(world, morph, 1.0f));
    audio_run(world, 6);
    test_assert(wta_harmonics(start + 2 * AUDIO_FRAME) > 10);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    ecs_fini(world);
}

/** A sound shorter than one frame logs one error that names the Wavetable, which has no
 * frames and whose layers play a sine; a longer sound set later builds the table. */
void SynthWavetableAsset_too_short(void) {
    // audio/synth
    ecs_world_t *world = wta_world();
    ecs_entity_t sound, table, pad;
    const AudioWavetable *w;
    int32_t start;
    audio_log_start();
    sound = audio_tone(world, "Short", 200, 0.5f, 1000);
    test_assert(ecs_script_run(world, "short",
        "using box.audio\n"
        "prefab ShortTable { Wavetable: {sound: Short} }\n"
        "prefab Pad {\n"
        "  SynthSound: {}\n"
        "  SynthEnvelope: {attack: 0.005, sustain: 1, hold: 0.2, release: 0.01}\n"
        "  SynthOscillator: {wave: SynthWavetable, frequency: 200, level: 0.3}\n"
        "  SynthTable: {table: ShortTable}\n"
        "}\n", NULL) == 0);
    audio_run(world, 4);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "ShortTable") != NULL);

    table = ecs_lookup(world, "ShortTable");
    pad = ecs_lookup(world, "Pad");
    w = ecs_get(world, table, AudioWavetable);
    test_int(w->id, 0);
    test_int(w->frames, 0);
    test_int(ecs_get(world, pad, AudioSynth)->patch.layer[0].table, 0);

    start = audio_tap_count;
    test_assert(box_audio_play(world, pad, 1.0f));
    audio_run(world, 6);
    test_int(wta_harmonics(start + 2 * AUDIO_FRAME), 1);

    audio_log_start();
    audio_run(world, 20);
    audio_log_stop();
    test_int(audio_log_errors, 0);

    sound = audio_tone(world, "Short", 200, 0.5f, 4096);
    test_assert(sound != 0);
    audio_run(world, 2);
    w = ecs_get(world, table, AudioWavetable);
    test_assert(w->id != 0);
    test_int(w->frames, 2);
    test_int(ecs_get(world, pad, AudioSynth)->patch.layer[0].table, w->id);
    ecs_fini(world);
}
