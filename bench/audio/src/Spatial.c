/**
 * @file Spatial.c
 * @brief Benchmarks for the spatial sources of box.audio: 1000 looping sources, most of them
 * beyond max_distance, through the null output, one item per source per frame.
 *
 * Each iteration runs one frame of a world with box. The frames are short (a microsecond), so
 * the null output renders next to nothing and the time is the time of the frame, the
 * AudioSync of the module included; sources_0 is the same world without sources. The
 * filtered cases give the sources a distance filter.
 *
 * The core cases render 32 looping voices of the core in quanta, with and without the
 * low-pass of BOX_AUDIO_PARAM_CUTOFF, one item per voice per frame, so their difference is the
 * cost of the filter per voice.
 */

#include <box.h>
#include <output.h>
#include <core.h>
#include <bake_bench.h>
#include <math.h>

/** Sources of the benchmarks */
#define BENCH_SOURCES 1000

/** Sources within max_distance of the listener */
#define BENCH_NEAR 32

/** Delta of a frame, short so the null output renders next to nothing */
#define BENCH_DT (1e-6f)

/** Voices of the core cases */
#define BENCH_VOICES 32

/** Frames of the sounds of the core cases */
#define BENCH_SOUND 48000

/** Create a world with a listener, a looping sound and sources.
 *
 * @param count Sources to create
 * @param sources Receives the sources, may be NULL
 * @param cutoff AudioSpatial.cutoff of the sources, 0 for no filter
 * @return The world
 */
static ecs_world_t* bench_world(
    int32_t count,
    ecs_entity_t *sources,
    float cutoff)
{
    static float samples[4800];
    ecs_world_t *world = ecs_init();
    int32_t i;
    ECS_IMPORT(world, Box);
    box_audio_output_open(world, &(box_audio_output_desc_t){ .headless = true });
    for (i = 0; i < 4800; i ++) {
        samples[i] = 0.01f * (float)(i % 100) / 100.0f;
    }

    ecs_entity_t sound = box_audio_sound_from_pcm(world, 0, samples, 4800, 1, 48000);
    ecs_entity_t listener = ecs_entity(world, { .name = "listener" });
    ecs_set(world, listener, Position, {0, 0, 0});
    ecs_add(world, listener, AudioListener);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_new(world);
        float d = i < BENCH_NEAR ? 2.0f + (float)i : 100.0f + (float)i;
        ecs_set(world, e, Position, {d * 0.6f, 0, -d * 0.8f});
        ecs_set(world, e, AudioSpatial, {1, 50, 1, cutoff});
        ecs_set(world, e, AudioSource, {
            .sound = sound, .volume = 0.5f, .pitch = 1, .loop = true, .playing = true });
        if (sources) {
            sources[i] = e;
        }
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, BENCH_DT);
    }

    return world;
}

/** Run frames of a world with 1000 sources.
 *
 * @param b The benchmark
 * @param listener Move the listener every frame
 * @param moved Sources to move every frame
 * @param cutoff AudioSpatial.cutoff of the sources, 0 for no filter
 */
static void bench_spatial(
    bench_t *b,
    bool listener,
    int32_t moved,
    float cutoff)
{
    static ecs_entity_t sources[BENCH_SOURCES];
    ecs_world_t *world = bench_world(BENCH_SOURCES, sources, cutoff);
    ecs_entity_t ears = ecs_lookup(world, "listener");
    float x = 0;
    int32_t i;
    bench_set_items(b, BENCH_SOURCES);
    while (bench_iter(b)) {
        x = x > 1.0f ? 0 : x + 0.01f;
        if (listener) {
            ecs_set(world, ears, Position, {x, 0, 0});
        }

        for (i = 0; i < moved; i ++) {
            const Position *p = ecs_get(world, sources[i], Position);
            ecs_set(world, sources[i], Position, {p->x, x, p->z});
        }

        ecs_progress(world, BENCH_DT);
        bench_keep(world);
    }

    ecs_fini(world);
}

void Spatial_sources_1000_still(
    bench_t *b)
{
    // audio/spatial
    bench_spatial(b, false, 0, 0);
}

void Spatial_sources_1000_listener_moves(
    bench_t *b)
{
    // audio/spatial
    bench_spatial(b, true, 0, 0);
}

void Spatial_sources_1000_ten_move(
    bench_t *b)
{
    // audio/spatial
    bench_spatial(b, false, 10, 0);
}

void Spatial_sources_0(
    bench_t *b)
{
    // audio/spatial
    ecs_world_t *world = bench_world(0, NULL, 0);
    bench_set_items(b, BENCH_SOURCES);
    while (bench_iter(b)) {
        ecs_progress(world, BENCH_DT);
        bench_keep(world);
    }

    ecs_fini(world);
}

void Spatial_sources_1000_filtered_listener_moves(
    bench_t *b)
{
    // audio/spatial
    bench_spatial(b, true, 0, 500);
}

void Spatial_sources_1000_filtered_ten_move(
    bench_t *b)
{
    // audio/spatial
    bench_spatial(b, false, 10, 500);
}

/** Render quanta of a core with looping voices, filtered or not, until the benchmark ends.
 *
 * @param b The benchmark
 * @param stereo Whether the voices play a stereo sound
 * @param cutoff Cutoff of the voices, 0 for an open filter
 */
static void bench_core_voices(
    bench_t *b,
    bool stereo,
    float cutoff)
{
    static float mono[BENCH_SOUND], pair[BENCH_SOUND * 2];
    static float left[BOX_AUDIO_CORE_QUANTUM], right[BOX_AUDIO_CORE_QUANTUM];
    static uint8_t bytes[4096];
    box_audio_core_desc_t desc = { .sample_rate = 48000 };
    box_audio_core_t *core = box_audio_core_new(&desc);
    box_audio_event_t events[64];
    box_audio_writer_t w;
    int32_t i;
    for (i = 0; i < BENCH_SOUND; i ++) {
        mono[i] = 0.5f * (float)sin(i * 0.031);
        pair[i * 2] = mono[i];
        pair[i * 2 + 1] = 0.4f * (float)sin(i * 0.017);
    }

    box_audio_core_sound(core, 1, stereo ? pair : mono, BENCH_SOUND, stereo ? 2 : 1, 48000);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    for (i = 0; i < BENCH_VOICES; i ++) {
        box_audio_write_play(&w, (uint32_t)i + 1, 1, 0, 0.02f, 0, 1, BOX_AUDIO_PLAY_LOOP, 0);
        if (cutoff > 0) {
            box_audio_write_voice_set(&w, (uint32_t)i + 1, BOX_AUDIO_PARAM_CUTOFF,
                cutoff + 10.0f * (float)i, 0);
        }
    }

    box_audio_core_apply(core, w.data, w.size);
    for (i = 0; i < 64; i ++) {
        box_audio_core_render(core, left, right, BOX_AUDIO_CORE_QUANTUM);
    }

    bench_set_items(b, BOX_AUDIO_CORE_QUANTUM * BENCH_VOICES);
    while (bench_iter(b)) {
        box_audio_core_render(core, left, right, BOX_AUDIO_CORE_QUANTUM);
        box_audio_core_read_events(core, events, 64);
        bench_keep(left);
    }

    box_audio_core_free(core);
}

void Spatial_core_voices_32_open(
    bench_t *b)
{
    // audio/core
    bench_core_voices(b, false, 0);
}

void Spatial_core_voices_32_filtered(
    bench_t *b)
{
    // audio/core
    bench_core_voices(b, false, 800);
}

void Spatial_core_voices_32_stereo_open(
    bench_t *b)
{
    // audio/core
    bench_core_voices(b, true, 0);
}

void Spatial_core_voices_32_stereo_filtered(
    bench_t *b)
{
    // audio/core
    bench_core_voices(b, true, 800);
}
