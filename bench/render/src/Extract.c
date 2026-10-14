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
        ecs_singleton_set(world, LightBake, {.probe_spacing = 2, .bounds_padding = 1});
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

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_draws_stats_t stats = {0};
    bench_set_items(b, BENCH_STATIC + BENCH_DYNAMIC);
    while (bench_iter(b)) {
        y += 0.01f;
        for (i = 0; i < changed; i ++) {
            ecs_entity_t e = shapes[next];
            next = (next + 1) % BENCH_STATIC;
            ecs_set(world, e, Position, {(float)(next % BENCH_ROW), y, 0});
        }

        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
    }

    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    bench_counter(b, "scattered", (double)stats.scattered);
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
