/**
 * @file Capture.c
 * @brief Captures of box.audio: a valid 16 bit stereo WAV of the length asked for, and two
 * runs with fixed deltas write identical files.
 */

#include "world.h"
#include <stdio.h>

/** Read a whole file.
 *
 * @param path The file
 * @param size Receives its size
 * @return The bytes, which the caller frees, NULL when it cannot be read
 */
static uint8_t* capture_read(
    const char *path,
    long *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *result = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        *size = ftell(f);
        fseek(f, 0, SEEK_SET);
        result = ecs_os_malloc((ecs_size_t)*size);
        test_assert(fread(result, 1, (size_t)*size, f) == (size_t)*size);
        fclose(f);
    }

    return result;
}

static uint32_t capture_u32(
    const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static uint16_t capture_u16(
    const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/** Run frames until the capture of a world wrote its file.
 *
 * @param world The world
 */
static void capture_wait(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < 200 && box_audio_capture_pending(world); i ++) {
        audio_run(world, 1);
    }

    test_bool(box_audio_capture_pending(world), false);
}

/** Record a fixed scene: a tone on a bus with a send, a source that changes and plays.
 *
 * @param path The WAV to write
 */
static void capture_scene(
    const char *path)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t music = audio_bus(world, "music", 0, 0.8f);
    ecs_entity_t echo = audio_bus(world, "echo", 0, 0.5f);
    audio_send(world, music, echo, 0.3f, false);
    AudioBus *bus = ecs_ensure(world, music, AudioBus);
    bus->pan = -0.25f;
    ecs_modified(world, music, AudioBus);
    ecs_entity_t tone = audio_tone(world, "tone", 440, 0.3f, AUDIO_RATE);
    ecs_entity_t click = audio_tone(world, "click", 1200, 0.2f, AUDIO_RATE / 20);
    ecs_entity_t src = audio_source(world, tone, 1.0f, true, music);
    audio_run(world, 3);
    test_int(box_audio_capture(world, 1.0f, path), 0);
    audio_run(world, 10);
    ecs_ensure(world, src, AudioSource)->pitch = 1.5f;
    ecs_modified(world, src, AudioSource);
    box_audio_play(world, click, 0.7f);
    capture_wait(world);
    ecs_fini(world);
}

void Capture_valid_wav(void) {
    // audio/capture
    const char *path = "audio_capture_valid.wav";
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 5);
    test_int(box_audio_capture(world, 0.5f, path), 0);
    test_bool(box_audio_capture_pending(world), true);
    audio_log_start();
    test_assert(box_audio_capture(world, 0.5f, "other.wav") != 0);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    capture_wait(world);
    ecs_fini(world);

    long size = 0;
    uint8_t *wav = capture_read(path, &size);
    test_assert(wav != NULL);
    test_int(size, 44 + 24000 * 4);
    test_assert(!memcmp(wav, "RIFF", 4));
    test_uint(capture_u32(wav + 4), (uint32_t)size - 8);
    test_assert(!memcmp(wav + 8, "WAVEfmt ", 8));
    test_uint(capture_u32(wav + 16), 16);
    test_uint(capture_u16(wav + 20), 1);
    test_uint(capture_u16(wav + 22), 2);
    test_uint(capture_u32(wav + 24), 48000);
    test_uint(capture_u32(wav + 28), 48000 * 4);
    test_uint(capture_u16(wav + 32), 4);
    test_uint(capture_u16(wav + 34), 16);
    test_assert(!memcmp(wav + 36, "data", 4));
    test_uint(capture_u32(wav + 40), 24000 * 4);
    int16_t left = (int16_t)capture_u16(wav + 44 + 1000 * 4);
    int16_t right = (int16_t)capture_u16(wav + 44 + 1000 * 4 + 2);
    test_int(left, 16384);
    test_int(right, 16384);
    ecs_os_free(wav);
    remove(path);
}

void Capture_two_runs_identical(void) {
    // audio/capture
    const char *a = "audio_capture_run_a.wav", *b = "audio_capture_run_b.wav";
    capture_scene(a);
    capture_scene(b);

    long size_a = 0, size_b = 0;
    uint8_t *wav_a = capture_read(a, &size_a);
    uint8_t *wav_b = capture_read(b, &size_b);
    test_assert(wav_a != NULL);
    test_assert(wav_b != NULL);
    test_int(size_a, 44 + 48000 * 4);
    test_int(size_a, size_b);
    test_assert(!memcmp(wav_a, wav_b, (size_t)size_a));

    int32_t i, loud = 0;
    for (i = 44; i < size_a; i += 2) {
        int16_t v = (int16_t)capture_u16(wav_a + i);
        loud += v > 3000 || v < -3000;
    }

    test_assert(loud > 1000);
    ecs_os_free(wav_a);
    ecs_os_free(wav_b);
    remove(a);
    remove(b);
}

void Capture_no_output(void) {
    // audio/capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(box_audio_capture(world, 0.5f, "audio_capture_none.wav"), 0);
    audio_log_start();
    audio_run(world, 2);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_bool(box_audio_capture_pending(world), false);
    audio_log_start();
    test_assert(box_audio_capture(world, 0.0f, "x.wav") != 0);
    test_assert(box_audio_capture(world, 100.0f, "x.wav") != 0);
    audio_log_stop();
    test_int(audio_log_errors, 2);
    ecs_fini(world);
}
