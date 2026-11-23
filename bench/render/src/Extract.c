/**
 * @file Extract.c
 * @brief Benchmarks for the extract and upload of instanced batches.
 *
 * A world with 100k static and 10k dynamic shapes runs frames against a backend that drops
 * the packets, so a case measures the main thread: transforms, change tracking and extract.
 * The baked case adds a LightBake whose probe volume stays idle.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#include "bench_memory.h"
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif

/** Get the instructions the process retired so far.
 *
 * @return The instructions, 0 where the platform does not count them
 */
static uint64_t bench_extract_instructions(void)
{
    uint64_t result = 0;
#ifdef __APPLE__
    struct rusage_info_v4 info;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t*)&info)) {
        result = info.ri_instructions;
    }
#endif

    return result;
}

/** Static shapes of the scene */
#define BENCH_STATIC (100000)

/** Dynamic shapes of the scene */
#define BENCH_DYNAMIC (10000)

/** Shapes per row of the grid */
#define BENCH_ROW (300)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_render_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Run frames of a scene with static and dynamic shapes, moving a share of the static ones.
 *
 * @param b The benchmark
 * @param changed Static shapes moved before every frame
 * @param baked The scene has a LightBake, so its probe volume is kept every frame
 */
static void bench_render_extract(
    bench_t *b,
    int32_t changed,
    bool baked)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    if (baked) {
        ecs_singleton_set(world, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
            .compute = true});
    }

    box_render_backend_t backend = { .render = bench_render_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t *shapes = ecs_os_malloc_n(ecs_entity_t, BENCH_STATIC);
    int32_t i, next = 0;
    float y = 0;
    for (i = 0; i < BENCH_STATIC; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position,
            {(float)(i % BENCH_ROW), 0, (float)(i / BENCH_ROW)});
        ecs_set(world, shapes[i], Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, shapes[i], Rgb, {200, 100, 50});
    }

    for (i = 0; i < BENCH_DYNAMIC; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position, {(float)(i % BENCH_ROW), 2, (float)(i / BENCH_ROW)});
        ecs_set(world, e, Sphere, {.segments = 8, .radius = 0.25f});
    }

    box_render_draws_stats_t stats = {0};
    ecs_progress(world, 0);
    box_render_draws_stats(world, &stats);
    int64_t setup_material_bytes = stats.material_bytes;
    int64_t setup_upload_bytes = stats.upload_bytes;
    ecs_progress(world, 0);
    int64_t observers = 0, sets = 0;
    double frames = 0;
    uint64_t start = bench_extract_instructions();
    bench_set_items(b, BENCH_STATIC + BENCH_DYNAMIC);
    while (bench_iter(b)) {
        int64_t before = ecs_get_world_info(world)->observers_ran_total;
        y += 0.01f;
        for (i = 0; i < changed; i ++) {
            ecs_entity_t e = shapes[next];
            next = (next + 1) % BENCH_STATIC;
            ecs_set(world, e, Position, {(float)(next % BENCH_ROW), y, 0});
        }

        observers += ecs_get_world_info(world)->observers_ran_total - before;
        sets += changed;

        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
        frames ++;
    }

    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    bench_counter(b, "instructions", (double)(bench_extract_instructions() - start));
    bench_counter(b, "frames", frames);
    bench_counter(b, "scattered", (double)stats.scattered);
    bench_counter(b, "setup_material_bytes", (double)setup_material_bytes);
    bench_counter(b, "setup_upload_bytes", (double)setup_upload_bytes);
    bench_render_memory(b, &stats);
    if (sets) {
        bench_counter(b, "observers_per_set", (double)observers / (double)sets);
    }

    if (baked) {
        bench_counter(b, "probes",
            (double)ecs_get(world, ecs_id(LightBake), LightBakeStatus)->probes);
    }

    box_render_sync(world);
    ecs_fini(world);
    ecs_os_free(shapes);
}

void Extract_unchanged(
    bench_t *b)
{
    // renderer/instancing
    bench_render_extract(b, 0, false);
}

void Extract_changed_1pct(
    bench_t *b)
{
    // renderer/instancing
    bench_render_extract(b, BENCH_STATIC / 100, false);
}

void Extract_baked_idle(
    bench_t *b)
{
    // baked_lighting/probe_volume
    bench_render_extract(b, 0, true);
}

/** Ships of the fade scene */
#define BENCH_FADE_SHIPS (4)

/** Levels of the hierarchy of a ship */
#define BENCH_FADE_DEPTH (12)

/** Shapes on each level of a ship */
#define BENCH_FADE_WIDTH (340)

/** Run frames of ships of deep hierarchies (BENCH_FADE_DEPTH levels of BENCH_FADE_WIDTH shapes,
 * about 4k shapes each) whose root Opacity changes every frame, as a fade does.
 *
 * The scene has blend modes, so a fade below 1 draws the shapes blended. Some levels have
 * DynamicMaterial, NoShadow or LightFixture, which the shapes below inherit through ChildOf.
 *
 * @param b The benchmark
 * @param blend The fade crosses 1 every frame, so the shapes move between the opaque and the
 * blended batches; otherwise it stays below 1 and the shapes keep their batches
 */
static void bench_render_fade(
    bench_t *b,
    bool blend)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.blend_modes = true});
    box_render_backend_t backend = { .render = bench_render_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t roots[BENCH_FADE_SHIPS];
    int32_t s, l, w;
    for (s = 0; s < BENCH_FADE_SHIPS; s ++) {
        ecs_entity_t parent = roots[s] = ecs_new(world);
        ecs_set(world, parent, Position, {(float)s * 50.0f, 0, 0});
        for (l = 0; l < BENCH_FADE_DEPTH; l ++) {
            ecs_entity_t first = 0;
            for (w = 0; w < BENCH_FADE_WIDTH; w ++) {
                ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
                ecs_set(world, e, Position, {(float)(w % 20), (float)l, (float)(w / 20)});
                ecs_set(world, e, Box, {0.5f, 0.5f, 0.5f});
                ecs_set(world, e, Rgb, {200, 100, 50});
                first = first ? first : e;
            }

            if (l == 3) {
                ecs_add_id(world, first, NoShadow);
            } else if (l == 6) {
                ecs_add_id(world, first, LightFixture);
            } else if (l == 9) {
                ecs_add_id(world, first, DynamicMaterial);
            }

            parent = first;
        }
    }

    box_render_draws_stats_t stats = {0};
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    double frames = 0, processed = 0;
    int32_t frame = 0;
    bench_set_items(b, BENCH_FADE_SHIPS * BENCH_FADE_DEPTH * BENCH_FADE_WIDTH);
    while (bench_iter(b)) {
        float opacity = blend ? ((frame & 1) ? 1.0f : 0.5f) :
            0.25f + 0.5f * (float)(frame % 64) / 64.0f;
        for (s = 0; s < BENCH_FADE_SHIPS; s ++) {
            ecs_set(world, roots[s], Opacity, {opacity});
        }

        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
        processed += (double)stats.processed;
        frames ++;
        frame ++;
    }

    bench_counter(b, "frames", frames);
    if (frames) {
        bench_counter(b, "processed_per_frame", processed / frames);
    }

    box_render_sync(world);
    ecs_fini(world);
}

void Extract_fade_hierarchy(
    bench_t *b)
{
    // renderer/instancing
    bench_render_fade(b, false);
}

void Extract_fade_hierarchy_blend(
    bench_t *b)
{
    // renderer/instancing
    bench_render_fade(b, true);
}
