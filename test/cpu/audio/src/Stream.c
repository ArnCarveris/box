/**
 * @file Stream.c
 * @brief Music streams of box.audio: an Ogg Vorbis file decoded ahead in chunks into the ring of
 * a stream of the core, seamless loops, the end, underruns, path changes, stops and deletes.
 *
 * The files are written by test/assets/audio/make_ogg.py: tone.ogg holds 12000 frames of 480 Hz
 * at 0.5 and 48000 Hz, 120 whole periods, tone_960.ogg 6000 frames of 960 Hz at 0.25. Jobs run
 * inline, so a stream set before a frame decodes at the end of it and starts in the next one.
 */

#include "world.h"
#include "../../../../src/jobs/jobs.h"
#include <stdlib.h>

/** Folder of the files, from the repository root */
#define STREAM_DIR "test/assets/audio/"

/** The tone of 480 Hz */
#define STREAM_TONE STREAM_DIR "tone.ogg"

/** Frames of tone.ogg */
#define STREAM_TONE_FRAMES 12000

/** The tone of 960 Hz */
#define STREAM_OTHER STREAM_DIR "tone_960.ogg"

/** Largest difference of a decoded sample from the tone */
#define STREAM_TOLERANCE 0.01f

/** Largest step between two samples of the tone of 480 Hz at 0.5, with room for the loss */
#define STREAM_STEP 0.036f

/** Calls of the counting allocator */
static int32_t stream_allocs;

/** Chunks of a sine for the tests of the core, continuous from one to the next */
static float stream_chunks[4][BOX_AUDIO_STREAM_CHUNK];

/** Create a world whose jobs run inline at the end of each frame.
 *
 * @return The world
 */
static ecs_world_t* stream_world(void) {
    ecs_world_t *world = audio_world();
    test_int(box_jobs_set_workers(world, 0), 0);

    return world;
}

/** Create an entity with an AudioStream that keeps the defaults but its path and loop.
 *
 * @param world The world
 * @param name Name, NULL for none
 * @param path The file
 * @param loop Whether it loops
 * @return The entity
 */
static ecs_entity_t stream_new(
    ecs_world_t *world,
    const char *name,
    const char *path,
    bool loop)
{
    ecs_entity_t e = name ? ecs_entity(world, { .name = name }) : ecs_new(world);
    AudioStream *s = ecs_ensure(world, e, AudioStream);
    test_flt(s->volume, 1.0f);
    test_bool(s->loop, true);
    test_bool(s->playing, true);
    s->path = ecs_os_strdup(path);
    s->loop = loop;
    ecs_modified(world, e, AudioStream);

    return e;
}

/** Set the path of an AudioStream.
 *
 * @param world The world
 * @param e The entity
 * @param path The file
 */
static void stream_set_path(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *path)
{
    AudioStream *s = ecs_get_mut(world, e, AudioStream);
    test_assert(s != NULL);
    ecs_os_free(s->path);
    s->path = ecs_os_strdup(path);
    ecs_modified(world, e, AudioStream);
}

/** Set whether an AudioStream plays.
 *
 * @param world The world
 * @param e The entity
 * @param playing Whether it plays
 */
static void stream_set_playing(
    ecs_world_t *world,
    ecs_entity_t e,
    bool playing)
{
    AudioStream *s = ecs_get_mut(world, e, AudioStream);
    test_assert(s != NULL);
    s->playing = playing;
    ecs_modified(world, e, AudioStream);
}

/** Get the status of a stream.
 *
 * @param world The world
 * @param e The entity
 * @return The status
 */
static const AudioStreamStatus* stream_status(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const AudioStreamStatus *result = ecs_get(world, e, AudioStreamStatus);
    test_assert(result != NULL);

    return result;
}

/** Run frames one at a time until a stream plays.
 *
 * @param world The world
 * @param e The entity
 * @return Frame of the tap at which its voice started
 */
static int32_t stream_start(
    ecs_world_t *world,
    ecs_entity_t e)
{
    int32_t i, result = -1;
    for (i = 0; i < 10 && result < 0; i ++) {
        int32_t at = audio_tap_count;
        audio_run(world, 1);
        if (stream_status(world, e)->state == AudioStreamPlaying) {
            result = at;
        }
    }

    test_assert(result >= 0);

    return result;
}

/** The sample of the tone of tone.ogg at a frame.
 *
 * @param frame The frame
 * @return The sample
 */
static float stream_tone(
    int32_t frame)
{
    return (float)(0.5 * sin(2 * AUDIO_PI * 480.0 * (frame % 100) / AUDIO_RATE));
}

/** Check that the tap plays the tone of tone.ogg from a frame on, continuing over its loops.
 *
 * @param at Frame of the tap at which the stream started
 * @param from First frame after at to check
 * @param to End frame after at
 */
static void stream_expect_tone(
    int32_t at,
    int32_t from,
    int32_t to)
{
    float worst = 0;
    int32_t i;
    test_assert(at + to <= audio_tap_count);
    for (i = from; i < to; i ++) {
        float d = fabsf(audio_tap_l[at + i] - stream_tone(i));
        worst = d > worst ? d : worst;
        test_flt(audio_tap_l[at + i], audio_tap_r[at + i]);
    }

    test_assert(worst < STREAM_TOLERANCE);
}

/** Get the chunks the output holds for its streams.
 *
 * @param world The world
 * @return Number of chunks
 */
static int32_t stream_output_chunks(
    ecs_world_t *world)
{
    box_audio_transport_t *t = box_audio_output_transport(world);
    box_audio_transport_stats_t stats = {0};
    test_assert(t != NULL);
    t->stats(t, &stats);

    return stats.chunks;
}

/** Check a file that fails: one error that names it, a failed status, and a looping sound
 * that keeps playing.
 *
 * @param file The file name in STREAM_DIR
 */
static void stream_expect_failure(
    const char *file)
{
    char path[256];
    ecs_world_t *world = stream_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 3);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);

    snprintf(path, sizeof(path), STREAM_DIR "%s", file);
    audio_log_start();
    ecs_entity_t e = stream_new(world, "music", path, true);
    audio_run(world, 5);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, file) != NULL);

    const AudioStreamStatus *status = stream_status(world, e);
    test_int(status->state, AudioStreamFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, file) != NULL);
    test_int(status->frames, 0);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    ecs_delete(world, e);
    audio_run(world, 2);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(box_audio_music_count(world), 0);
    ecs_fini(world);
}

/** Fill the chunks of the core tests with a sine of 480 Hz at 0.5, continuous over them. */
static void stream_fill_chunks(void) {
    int32_t c, i;
    for (c = 0; c < 4; c ++) {
        for (i = 0; i < BOX_AUDIO_STREAM_CHUNK; i ++) {
            stream_chunks[c][i] = stream_tone(c * BOX_AUDIO_STREAM_CHUNK + i);
        }
    }
}

/** Open stream 0 of a core for voice 1, mono at the rate of the tests.
 *
 * @param core The core
 */
static void stream_open(
    box_audio_core_t *core)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stream(&w, 1, 0, 0, 1.0f, 1, AUDIO_RATE));
    box_audio_core_apply(core, w.data, w.size);
}

/** Allocator that counts its calls.
 *
 * @param size Bytes
 * @param ctx Unused
 * @return The memory
 */
static void* stream_alloc(
    size_t size,
    void *ctx)
{
    (void)ctx;
    stream_allocs ++;

    return malloc(size);
}

/** Free of the counting allocator.
 *
 * @param ptr The memory
 * @param ctx Unused
 */
static void stream_free(
    void *ptr,
    void *ctx)
{
    (void)ctx;
    free(ptr);
}

void Stream_decode_matches(void) {
    // audio/stream
    ecs_world_t *world = stream_world();
    ecs_entity_t e = stream_new(world, "music", STREAM_TONE, true);
    test_int(stream_status(world, e)->state, AudioStreamLoading);
    int32_t at = stream_start(world, e);
    audio_run(world, 5);

    const AudioStreamStatus *status = stream_status(world, e);
    test_int(status->state, AudioStreamPlaying);
    test_int(status->frames, STREAM_TONE_FRAMES);
    test_int(status->channels, 1);
    test_flt(status->sample_rate, 48000);
    test_int(status->underruns, 0);
    test_assert(status->error == NULL);
    test_assert(status->buffered > 1.0f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);

    int32_t played = audio_tap_count - at;
    test_assert(played >= 4800);
    stream_expect_tone(at, AUDIO_RAMP + 1, played);
    test_assert(fabs(audio_frequency(audio_tap_l, at + AUDIO_RAMP, audio_tap_count) - 480) < 0.5);
    test_assert(fabsf(audio_peak(audio_tap_l, at + AUDIO_RAMP, audio_tap_count) - 0.5f) < 0.01f);
    ecs_fini(world);
}

void Stream_loop_seamless(void) {
    // audio/stream
    ecs_world_t *world = stream_world();
    ecs_entity_t e = stream_new(world, "music", STREAM_TONE, true);
    int32_t at = stream_start(world, e);
    audio_run(world, 75);

    int32_t played = audio_tap_count - at;
    test_assert(played > STREAM_TONE_FRAMES * 4);
    stream_expect_tone(at, AUDIO_RAMP + 1, played);
    test_assert(audio_max_step(audio_tap_l, at + AUDIO_RAMP, audio_tap_count) < STREAM_STEP);
    test_assert(fabs(audio_frequency(audio_tap_l, at + AUDIO_RAMP, audio_tap_count) - 480) < 0.1);

    const AudioStreamStatus *status = stream_status(world, e);
    test_int(status->state, AudioStreamPlaying);
    test_int(status->frames, STREAM_TONE_FRAMES);
    test_int(status->underruns, 0);
    test_assert(status->position >= 0 && status->position < 0.25f);
    test_bool(ecs_get(world, e, AudioStream)->playing, true);
    test_assert(stream_output_chunks(world) <= BOX_AUDIO_STREAM_CHUNKS);
    ecs_fini(world);
}

void Stream_no_loop_ends(void) {
    // audio/stream
    ecs_world_t *world = stream_world();
    ecs_entity_t e = stream_new(world, "music", STREAM_TONE, false);
    int32_t at = stream_start(world, e), i;
    audio_run(world, 30);

    const AudioStreamStatus *status = stream_status(world, e);
    test_int(status->state, AudioStreamEnded);
    test_bool(ecs_get(world, e, AudioStream)->playing, false);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(status->underruns, 0);

    stream_expect_tone(at, AUDIO_RAMP + 1, STREAM_TONE_FRAMES);
    test_assert(fabsf(audio_tap_l[at + STREAM_TONE_FRAMES - 1]) > 0.005f);
    for (i = at + STREAM_TONE_FRAMES; i < audio_tap_count; i ++) {
        test_flt(audio_tap_l[i], 0.0f);
    }

    audio_run(world, 3);
    test_int(stream_status(world, e)->state, AudioStreamEnded);
    test_int(stream_output_chunks(world), 0);

    stream_set_playing(world, e, true);
    at = stream_start(world, e);
    audio_run(world, 3);
    stream_expect_tone(at, AUDIO_RAMP + 1, audio_tap_count - at);
    ecs_fini(world);
}

void Stream_underrun_silent(void) {
    // audio/stream
    box_audio_core_t *core = audio_new();
    box_audio_stream_status_t status;
    int32_t c = BOX_AUDIO_STREAM_CHUNK, i;
    stream_fill_chunks();
    stream_open(core);
    test_assert(box_audio_core_stream_chunk(core, 0, stream_chunks[0], c, false));
    test_assert(box_audio_core_stream_chunk(core, 0, stream_chunks[1], c, false));

    audio_render(core, 0, c * 3);
    box_audio_core_stream_status(core, 0, &status);
    test_int(status.voice, 1);
    test_int(status.open, 1);
    test_int(status.underruns, 1);
    test_int(status.starved, 1);
    test_int(status.consumed, 1);
    test_int(box_audio_core_voice_count(core), 1);
    for (i = AUDIO_RAMP + 1; i < c * 2 - 4 * AUDIO_RAMP; i ++) {
        test_assert(fabsf(audio_l[i] - stream_tone(i)) < 1e-5f);
    }

    test_assert(audio_max_step(audio_l, 0, c * 3) < STREAM_STEP);
    test_assert(audio_peak(audio_l, c * 2, c * 3) == 0);

    test_assert(box_audio_core_stream_chunk(core, 0, stream_chunks[2], c, false));
    test_assert(box_audio_core_stream_chunk(core, 0, stream_chunks[3], c, true));
    audio_render(core, 0, c * 3);
    box_audio_core_stream_status(core, 0, &status);
    test_int(status.underruns, 1);
    test_int(status.starved, 0);
    test_assert(audio_max_step(audio_l, 1, c * 2) < STREAM_STEP);
    test_assert(fabsf(audio_peak(audio_l, AUDIO_RAMP * 2, c) - 0.5f) < 0.01f);
    // The 3 frames held at the underrun play before the 2 new chunks, none is skipped
    test_assert(audio_l[c * 2 + 2] != 0);
    test_assert(audio_peak(audio_l, c * 2 + 3, c * 3) == 0);

    box_audio_core_stream_status(core, 0, &status);
    test_int(status.open, 0);
    test_int(status.ended, 1);
    test_int(box_audio_core_voice_count(core), 0);
    test_assert(!box_audio_core_stream_chunk(core, 0, stream_chunks[0], c, false));
    box_audio_core_free(core);
}

void Stream_change_path(void) {
    // audio/stream
    ecs_world_t *world = stream_world();
    ecs_entity_t e = stream_new(world, "music", STREAM_TONE, true);
    stream_start(world, e);
    audio_run(world, 10);
    test_int(box_audio_music_count(world), 1);

    audio_log_start();
    int32_t at = audio_tap_count;
    stream_set_path(world, e, STREAM_OTHER);
    audio_run(world, 1);
    test_int(box_audio_music_count(world), 2);
    audio_run(world, 20);
    audio_log_stop();
    test_int(audio_log_errors, 0);

    test_assert(audio_tap_l[at] != 0 || audio_tap_l[at + 1] != 0);
    test_assert(audio_max_step(audio_tap_l, at, audio_tap_count) < STREAM_STEP);
    int32_t from = audio_tap_count - 4800;
    test_assert(fabs(audio_frequency(audio_tap_l, from, audio_tap_count) - 960) < 1);
    test_assert(fabsf(audio_peak(audio_tap_l, from, audio_tap_count) - 0.25f) < 0.01f);

    const AudioStreamStatus *status = stream_status(world, e);
    test_int(status->state, AudioStreamPlaying);
    test_int(status->frames, 6000);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_int(box_audio_music_count(world), 1);
    test_assert(stream_output_chunks(world) <= BOX_AUDIO_STREAM_CHUNKS);
    ecs_fini(world);
}

void Stream_stop_resume(void) {
    // audio/stream
    ecs_world_t *world = stream_world();
    ecs_entity_t e = stream_new(world, "music", STREAM_TONE, true);
    stream_start(world, e);
    audio_run(world, 10);

    int32_t at = audio_tap_count;
    stream_set_playing(world, e, false);
    audio_run(world, 3);
    test_int(stream_status(world, e)->state, AudioStreamStopped);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_assert(audio_tap_l[at] != 0 || audio_tap_l[at + 1] != 0);
    test_assert(audio_max_step(audio_tap_l, at, audio_tap_count) < STREAM_STEP);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count) == 0);
    test_int(stream_output_chunks(world), 0);
    float position = stream_status(world, e)->position;
    test_assert(position >= 0 && position < 0.25f);

    for (int32_t i = 0; i < 3; i ++) {
        stream_set_playing(world, e, true);
        at = stream_start(world, e);
        audio_run(world, 6);
        test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
        test_assert(audio_max_step(audio_tap_l, at, audio_tap_count) < STREAM_STEP);
        test_assert(fabs(audio_frequency(audio_tap_l, at + AUDIO_RAMP, audio_tap_count) - 480)
            < 0.5);
        stream_set_playing(world, e, false);
        audio_run(world, 3);
    }

    test_int(box_audio_music_count(world), 1);
    test_int(stream_output_chunks(world), 0);
    test_int(box_audio_module_stats(world).music_starts, 4);
    test_int(stream_status(world, e)->underruns, 0);
    ecs_fini(world);
}

void Stream_delete_frees(void) {
    // audio/stream
    ecs_world_t *world = stream_world();
    ecs_entity_t e = stream_new(world, "music", STREAM_TONE, true);
    stream_start(world, e);
    audio_run(world, 10);
    test_assert(stream_output_chunks(world) > 0);

    int32_t at = audio_tap_count;
    ecs_delete(world, e);
    audio_run(world, 3);
    test_assert(audio_tap_l[at] != 0 || audio_tap_l[at + 1] != 0);
    test_assert(audio_max_step(audio_tap_l, at, audio_tap_count) < STREAM_STEP);
    test_assert(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME, audio_tap_count) == 0);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(box_audio_music_count(world), 0);
    test_int(stream_output_chunks(world), 0);

    e = stream_new(world, "music2", STREAM_TONE, true);
    ecs_delete(world, e);
    audio_run(world, 3);
    test_int(box_audio_music_count(world), 0);
    test_int(stream_output_chunks(world), 0);
    ecs_fini(world);
}

void Stream_missing_file(void) {
    // audio/stream
    stream_expect_failure("missing.ogg");
}

void Stream_corrupt_file(void) {
    // audio/stream
    stream_expect_failure("corrupt.ogg");
}

void Stream_second_stream_errors(void) {
    // audio/stream
    ecs_world_t *world = stream_world();
    ecs_entity_t a = stream_new(world, "first", STREAM_TONE, true);
    int32_t at = stream_start(world, a);

    audio_log_start();
    ecs_entity_t b = stream_new(world, "second", STREAM_OTHER, true);
    audio_run(world, 5);
    stream_set_playing(world, b, true);
    audio_run(world, 5);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "second") != NULL);

    const AudioStreamStatus *status = stream_status(world, b);
    test_int(status->state, AudioStreamFailed);
    test_assert(status->error != NULL);
    test_int(stream_status(world, a)->state, AudioStreamPlaying);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    test_int(box_audio_music_count(world), 1);
    stream_expect_tone(at, AUDIO_RAMP + 1, audio_tap_count - at);
    ecs_fini(world);
}

void Stream_no_alloc(void) {
    // audio/stream
    box_audio_core_desc_t desc = {
        .sample_rate = AUDIO_RATE, .alloc = stream_alloc, .free = stream_free
    };
    box_audio_stream_status_t status;
    int32_t c = BOX_AUDIO_STREAM_CHUNK, i;
    stream_allocs = 0;
    box_audio_core_t *core = box_audio_core_new(&desc);
    test_assert(core != NULL);
    test_int(stream_allocs, 1);
    stream_fill_chunks();

    for (i = 0; i < 2; i ++) {
        uint8_t bytes[64];
        box_audio_writer_t w;
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        test_assert(box_audio_write_stream(&w, (uint32_t)(10 + i), (uint32_t)i, 0, 1.0f,
            1, i ? 44100.0f : AUDIO_RATE));
        box_audio_core_apply(core, w.data, w.size);
        audio_render(core, 0, 1000);
        test_assert(box_audio_core_stream_chunk(core, (uint32_t)i, stream_chunks[0], c, false));
        audio_render(core, 0, c * 2);
        test_assert(box_audio_core_stream_chunk(core, (uint32_t)i, stream_chunks[1], c, false));
        test_assert(box_audio_core_stream_chunk(core, (uint32_t)i, stream_chunks[2], c, true));
        audio_render(core, 0, c * 3);
        box_audio_core_stream_status(core, (uint32_t)i, &status);
        test_int(status.open, 0);
        test_int(status.ended, 1);
        test_int(status.underruns, 1);
        audio_events(core, 0, NULL);
    }

    test_int(stream_allocs, 1);
    box_audio_core_free(core);
    test_int(stream_allocs, 1);
}
