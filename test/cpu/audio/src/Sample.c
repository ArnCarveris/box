/**
 * @file Sample.c
 * @brief Sound files of box.audio: WAV and FLAC decoded on a job, the rate of the file played
 * at the rate of the output, failed loads, the mix down, loops, sharing and deletes.
 *
 * The files are in test/assets/audio, written by make_sounds.py next to them. Jobs run inline,
 * so a file set before a frame decodes at the end of it and plays from the next one.
 */

#include "world.h"
#include "../../../../src/jobs/jobs.h"
#include <stdio.h>
#include <stdlib.h>

/** Folder of the sound files, from the repository root */
#define SAMPLE_DIR "test/assets/audio/"

/** Create a world whose jobs run inline at the end of each frame.
 *
 * @return The world
 */
static ecs_world_t* sample_world(void) {
    ecs_world_t *world = audio_world();
    test_int(box_jobs_set_workers(world, 0), 0);

    return world;
}

/** Create a sound entity with a SoundFile.
 *
 * @param world The world
 * @param name Name, NULL for none
 * @param path The file
 * @return The sound
 */
static ecs_entity_t sample_file(
    ecs_world_t *world,
    const char *name,
    const char *path)
{
    ecs_entity_t e = name ? ecs_entity(world, { .name = name }) : ecs_new(world);
    ecs_set(world, e, SoundFile, { .path = ECS_CONST_CAST(char*, path) });

    return e;
}

/** Check that a channel of a decoded sound is a sine at 48000 Hz.
 *
 * @param world The world
 * @param sound The sound
 * @param channel The channel
 * @param frequency Hz
 * @param level Peak level
 * @param tolerance Largest difference of a sample
 */
static void sample_expect_sine(
    ecs_world_t *world,
    ecs_entity_t sound,
    int32_t channel,
    double frequency,
    float level,
    float tolerance)
{
    int32_t frames = 0, channels = 0, i;
    float rate = 0, worst = 0;
    const float *data = box_audio_sample_frames(world, sound, &frames, &channels, &rate);
    test_assert(data != NULL);
    test_assert(channel < channels);
    for (i = 0; i < frames; i ++) {
        float x = (float)(level * sin(2 * AUDIO_PI * frequency * i / rate));
        float d = fabsf(data[i * channels + channel] - x);
        worst = d > worst ? d : worst;
    }

    test_assert(worst < tolerance);
}

/** Get the first frame of the tap whose left or right sample is not 0.
 *
 * @param from Frame to start at
 * @return The frame, audio_tap_count when every one is 0
 */
static int32_t sample_first_sound(
    int32_t from)
{
    int32_t i;
    for (i = from; i < audio_tap_count; i ++) {
        if (audio_tap_l[i] != 0 || audio_tap_r[i] != 0) {
            break;
        }
    }

    return i;
}

/** Check a sound file that fails: one error that names it, a failed status, and a looping
 * sound that keeps playing.
 *
 * @param file The file name in SAMPLE_DIR
 */
static void sample_expect_failure(
    const char *file)
{
    char path[256];
    ecs_world_t *world = sample_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 3);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    snprintf(path, sizeof(path), SAMPLE_DIR "%s", file);
    audio_log_start();
    ecs_entity_t s = sample_file(world, "bad", path);
    ecs_entity_t src = audio_source(world, s, 1.0f, true, 0);
    test_bool(box_audio_play(world, s, 1.0f), true);
    audio_run(world, 5);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, file) != NULL);

    const SoundStatus *status = ecs_get(world, s, SoundStatus);
    test_assert(status != NULL);
    test_int(status->state, SoundFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, file) != NULL);
    test_int(status->frames, 0);
    test_bool(ecs_get(world, src, AudioSource)->playing, true);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    ecs_delete(world, s);
    audio_run(world, 2);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(box_audio_sample_count(world), 0);
    ecs_fini(world);
}

void Sample_wav_16(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "sine_16.wav");
    test_int(ecs_get(world, s, SoundStatus)->state, SoundLoading);
    audio_run(world, 2);

    const SoundStatus *status = ecs_get(world, s, SoundStatus);
    test_int(status->state, SoundReady);
    test_int(status->frames, 12000);
    test_int(status->channels, 1);
    test_flt(status->sample_rate, 48000);
    test_assert(status->error == NULL);
    sample_expect_sine(world, s, 0, 1000, 0.5f, 0.001f);
    ecs_fini(world);
}

void Sample_wav_float(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "sine_float.wav");
    audio_run(world, 2);

    const SoundStatus *status = ecs_get(world, s, SoundStatus);
    test_int(status->state, SoundReady);
    test_int(status->frames, 12000);
    test_int(status->channels, 1);
    sample_expect_sine(world, s, 0, 1000, 0.5f, 0.00001f);
    ecs_fini(world);
}

void Sample_flac(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "sine.flac");
    audio_run(world, 2);

    const SoundStatus *status = ecs_get(world, s, SoundStatus);
    test_int(status->state, SoundReady);
    test_int(status->frames, 12000);
    test_int(status->channels, 1);
    test_flt(status->sample_rate, 48000);
    sample_expect_sine(world, s, 0, 1000, 0.5f, 0.001f);
    ecs_fini(world);
}

void Sample_mono(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "sine_16.wav");
    audio_source(world, s, 1.0f, true, 0);
    audio_run(world, 12);

    int32_t from = audio_tap_count - 4800, i;
    test_int(ecs_get(world, s, SoundStatus)->channels, 1);
    for (i = from; i < audio_tap_count; i ++) {
        test_flt(audio_tap_l[i], audio_tap_r[i]);
    }

    test_assert(fabs(audio_frequency(audio_tap_l, from, audio_tap_count) - 1000) < 2);
    test_assert(fabsf(audio_peak(audio_tap_l, from, audio_tap_count) - 0.5f) < 0.01f);
    ecs_fini(world);
}

void Sample_stereo(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "stereo.wav");
    audio_run(world, 2);

    const SoundStatus *status = ecs_get(world, s, SoundStatus);
    test_int(status->state, SoundReady);
    test_int(status->channels, 2);
    test_int(status->frames, 12000);
    sample_expect_sine(world, s, 0, 500, 0.5f, 0.001f);
    sample_expect_sine(world, s, 1, 1000, 0.25f, 0.001f);

    audio_source(world, s, 1.0f, true, 0);
    audio_run(world, 12);
    int32_t from = audio_tap_count - 4800;
    test_assert(fabs(audio_frequency(audio_tap_l, from, audio_tap_count) - 500) < 2);
    test_assert(fabs(audio_frequency(audio_tap_r, from, audio_tap_count) - 1000) < 2);
    test_assert(fabsf(audio_peak(audio_tap_l, from, audio_tap_count) - 0.5f) < 0.01f);
    test_assert(fabsf(audio_peak(audio_tap_r, from, audio_tap_count) - 0.25f) < 0.01f);
    ecs_fini(world);
}

void Sample_rate_44100_on_48000(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "tone_44100.wav");
    audio_run(world, 2);
    test_int(ecs_get(world, s, SoundStatus)->state, SoundReady);
    ecs_entity_t src = audio_source(world, s, 1.0f, false, 0);
    audio_run(world, 75);

    const SoundStatus *status = ecs_get(world, s, SoundStatus);
    test_int(status->frames, 44100);
    test_flt(status->sample_rate, 44100);
    test_bool(ecs_get(world, src, AudioSource)->playing, false);

    int32_t first = sample_first_sound(0), last = audio_tap_count - 1;
    while (last > first && audio_tap_l[last] == 0) {
        last --;
    }

    test_assert(first < audio_tap_count);
    test_assert(abs((last - first) - AUDIO_RATE) < AUDIO_RATE / 200);
    double f = audio_frequency(audio_tap_l, first + 4800, first + 4800 + 24000);
    test_assert(fabs(f - 440) < 1);
    test_assert(fabsf(audio_peak(audio_tap_l, first + 4800, first + 28800) - 0.25f) < 0.01f);
    ecs_fini(world);
}

void Sample_missing_file(void) {
    // audio/sample
    sample_expect_failure("missing.wav");
}

void Sample_corrupt_file(void) {
    // audio/sample
    sample_expect_failure("corrupt.wav");
}

void Sample_empty_file(void) {
    // audio/sample
    sample_expect_failure("empty.wav");
}

void Sample_multichannel(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "surround.wav");
    audio_run(world, 2);

    int32_t frames = 0, channels = 0, i;
    float rate = 0;
    const float *data = box_audio_sample_frames(world, s, &frames, &channels, &rate);
    test_assert(data != NULL);
    test_int(channels, 2);
    test_int(frames, 4800);
    test_int(ecs_get(world, s, SoundStatus)->channels, 2);

    for (i = 0; i < frames; i ++) {
        test_assert(fabsf(data[i * 2] - (0.4f + 0.2f + 0.1f) / 3) < 0.001f);
        test_assert(fabsf(data[i * 2 + 1] - (-0.4f + 0.2f - 0.1f) / 3) < 0.001f);
    }

    ecs_fini(world);
}

void Sample_loop_seam(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "loop.wav");
    ecs_entity_t src = audio_source(world, s, 1.0f, true, 0);
    audio_run(world, 30);

    int32_t first = sample_first_sound(0);
    test_assert(audio_tap_count - first > 4800 * 4);
    test_assert(audio_max_step(audio_tap_l, first + AUDIO_RAMP, audio_tap_count) < 0.0315f);
    test_assert(fabs(audio_frequency(audio_tap_l, first + AUDIO_RAMP, audio_tap_count) - 480)
        < 0.5);
    test_assert(fabsf(audio_peak(audio_tap_l, first + AUDIO_RAMP, audio_tap_count) - 0.5f) <
        0.01f);
    test_bool(ecs_get(world, src, AudioSource)->playing, true);
    ecs_fini(world);
}

void Sample_shared(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t a = sample_file(world, "a", SAMPLE_DIR "sine_16.wav");
    ecs_entity_t b = sample_file(world, "b", SAMPLE_DIR "sine_16.wav");
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, SoundFile, {
        .path = ECS_CONST_CAST(char*, SAMPLE_DIR "sine_16.wav") });
    ecs_entity_t i1 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t i2 = ecs_new_w_pair(world, EcsIsA, prefab);
    test_int(box_audio_sample_count(world), 1);
    test_int(box_audio_module_stats(world).decodes, 1);

    audio_source(world, a, 0.25f, true, 0);
    audio_source(world, a, 0.25f, true, 0);
    audio_source(world, b, 0.25f, true, 0);
    audio_source(world, i1, 0.25f, true, 0);
    audio_run(world, 4);
    test_int(ecs_get(world, i2, SoundStatus)->state, SoundReady);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 4);
    test_int(box_audio_module_sounds(world), 1);
    test_int(box_audio_module_stats(world).decodes, 1);

    int32_t frames, channels;
    float rate;
    const float *data = box_audio_sample_frames(world, a, &frames, &channels, &rate);
    test_assert(data != NULL);
    test_assert(box_audio_sample_frames(world, b, &frames, &channels, &rate) == data);
    test_assert(box_audio_sample_frames(world, i1, &frames, &channels, &rate) == data);
    test_assert(box_audio_sample_frames(world, i2, &frames, &channels, &rate) == data);

    ecs_delete(world, a);
    ecs_delete(world, b);
    ecs_delete(world, i1);
    audio_run(world, 3);
    test_int(box_audio_sample_count(world), 1);
    test_int(box_audio_module_sounds(world), 1);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    ecs_delete(world, i2);
    audio_run(world, 3);
    test_int(box_audio_sample_count(world), 0);
    test_int(box_audio_module_sounds(world), 0);
    ecs_fini(world);
}

void Sample_delete_while_playing(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "surround.wav");
    audio_source(world, s, 1.0f, true, 0);
    audio_run(world, 5);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.7f / 3) < 0.001f);

    test_bool(box_audio_play(world, s, 1.0f), true);
    audio_run(world, 1);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 2);
    test_assert(fabsf(audio_level(AUDIO_FRAME / 2) - 1.4f / 3) < 0.001f);
    int32_t at = audio_tap_count;

    ecs_delete(world, s);
    test_int(box_audio_sample_count(world), 0);
    audio_run(world, 3);
    test_assert(audio_tap_l[at] > 0.4f);
    test_assert(audio_max_step(audio_tap_l, at, audio_tap_count) < 0.01f);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);

    test_int(box_audio_module_sounds(world), 0);
    ecs_fini(world);
}

void Sample_not_ready(void) {
    // audio/sample
    ecs_world_t *world = sample_world();
    ecs_entity_t s = sample_file(world, "s", SAMPLE_DIR "surround.wav");
    ecs_entity_t looped = audio_source(world, s, 1.0f, true, 0);
    ecs_entity_t once = audio_source(world, s, 1.0f, false, 0);
    test_bool(box_audio_play(world, s, 1.0f), true);
    audio_run(world, 1);
    test_int(ecs_get(world, s, SoundStatus)->state, SoundLoading);
    test_bool(ecs_get(world, once, AudioSource)->playing, false);
    test_int(box_audio_module_stats(world).not_ready, 2);
    test_flt(audio_level(AUDIO_FRAME), 0.0f);

    audio_run(world, 3);
    test_int(ecs_get(world, s, SoundStatus)->state, SoundReady);
    test_bool(ecs_get(world, looped, AudioSource)->playing, true);
    test_bool(ecs_get(world, once, AudioSource)->playing, false);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.7f / 3) < 0.001f);
    test_int(box_audio_module_stats(world).not_ready, 2);

    test_bool(box_audio_play(world, s, 1.0f), true);
    audio_run(world, 1);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 2);
    ecs_fini(world);
}
