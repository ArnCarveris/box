/**
 * @file Spatial.c
 * @brief Benchmarks for the spatial sources of box.audio: 1000 looping sources, most of them
 * beyond max_distance, through the null output, one item per source per frame; and 300
 * emitters, one item per emitter per frame.
 *
 * Each iteration runs one frame of a world with box. The frames are short (a microsecond), so
 * the null output renders next to nothing and the time is the time of the frame, the
 * AudioSync of the module included; sources_0 is the same world without sources.
 *
 * emitters_300_idle runs emitters that are never due. emitters_300_playing runs frames of 1/60
 * s in which a few play a short silent sound; minus emitters_0_playing, the same world without
 * emitters, it is the cost of an emitter on the main thread.
 */

#include <box.h>
#include <output.h>
#include <bake_bench.h>

/** Sources of the benchmarks */
#define BENCH_SOURCES 1000

/** Sources within max_distance of the listener */
#define BENCH_NEAR 32

/** Delta of a frame, short so the null output renders next to nothing */
#define BENCH_DT (1e-6f)

/** Create a world with a listener, a looping sound and sources.
 *
 * @param count Sources to create
 * @param sources Receives the sources, may be NULL
 * @return The world
 */
static ecs_world_t* bench_world(
    int32_t count,
    ecs_entity_t *sources)
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
        ecs_set(world, e, AudioSpatial, {1, 50, 1});
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
 */
static void bench_spatial(
    bench_t *b,
    bool listener,
    int32_t moved)
{
    static ecs_entity_t sources[BENCH_SOURCES];
    ecs_world_t *world = bench_world(BENCH_SOURCES, sources);
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
    bench_spatial(b, false, 0);
}

void Spatial_sources_1000_listener_moves(
    bench_t *b)
{
    // audio/spatial
    bench_spatial(b, true, 0);
}

void Spatial_sources_1000_ten_move(
    bench_t *b)
{
    // audio/spatial
    bench_spatial(b, false, 10);
}

void Spatial_sources_0(
    bench_t *b)
{
    // audio/spatial
    ecs_world_t *world = bench_world(0, NULL);
    bench_set_items(b, BENCH_SOURCES);
    while (bench_iter(b)) {
        ecs_progress(world, BENCH_DT);
        bench_keep(world);
    }

    ecs_fini(world);
}

/** Emitters of the benchmarks */
#define BENCH_EMITTERS 300

/** Create a world with a listener, a short silent sound and emitters on spheres around it.
 *
 * @param count Emitters to create
 * @param min interval_min
 * @param max interval_max
 * @return The world
 */
static ecs_world_t* bench_emitter_world(
    int32_t count,
    float min,
    float max)
{
    static float silence[48];
    ecs_world_t *world = ecs_init();
    int32_t i;
    ECS_IMPORT(world, Box);
    box_audio_output_open(world, &(box_audio_output_desc_t){ .headless = true });
    ecs_entity_t sound = box_audio_sound_from_pcm(world, 0, silence, 48, 1, 48000);
    ecs_entity_t listener = ecs_entity(world, { .name = "listener" });
    ecs_set(world, listener, Position, {0, 0, 0});
    ecs_add(world, listener, AudioListener);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_new(world);
        float d = 2.0f + (float)(i % 40);
        ecs_set(world, e, Position, {d * 0.6f, 0, -d * 0.8f});
        AudioEmitter *em = ecs_ensure(world, e, AudioEmitter);
        em->sound = sound;
        em->interval_min = min;
        em->interval_max = max;
        em->shape = AudioEmitterSphere;
        em->radius = 3;
        em->volume_min = 0.5f;
        em->pitch_min = 0.9f;
        em->pitch_max = 1.1f;
        em->max_active = 2;
        ecs_modified(world, e, AudioEmitter);
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, BENCH_DT);
    }

    return world;
}

/** Run frames of a world with emitters.
 *
 * @param b The benchmark
 * @param count Emitters to create
 * @param dt Delta of a frame
 */
static void bench_emitters(
    bench_t *b,
    int32_t count,
    float dt)
{
    ecs_world_t *world = bench_emitter_world(count, dt > BENCH_DT ? 0.5f : 1.0f,
        dt > BENCH_DT ? 2.0f : 4.0f);
    bench_set_items(b, BENCH_EMITTERS);
    while (bench_iter(b)) {
        ecs_progress(world, dt);
        bench_keep(world);
    }

    ecs_fini(world);
}

void Spatial_emitters_300_idle(
    bench_t *b)
{
    // audio/emitter
    bench_emitters(b, BENCH_EMITTERS, BENCH_DT);
}

void Spatial_emitters_300_playing(
    bench_t *b)
{
    // audio/emitter
    bench_emitters(b, BENCH_EMITTERS, 1.0f / 60.0f);
}

void Spatial_emitters_0_playing(
    bench_t *b)
{
    // audio/emitter
    bench_emitters(b, 0, 1.0f / 60.0f);
}
