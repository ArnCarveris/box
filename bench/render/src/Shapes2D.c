/**
 * @file Shapes2D.c
 * @brief Benchmarks for the extract of the 2D shapes.
 *
 * Rects, circles and lines with borders run frames against a backend that drops the packets,
 * so a case measures the main thread: resolving changed shapes and filling their instances.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Static 2D shapes of the scene */
#define BENCH_SHAPES2D_STATIC (100000)

/** Dynamic 2D shapes of the scene */
#define BENCH_SHAPES2D_DYNAMIC (10000)

/** Shapes per row of the grid */
#define BENCH_SHAPES2D_ROW (300)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_shapes2d_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

void Shapes2D_dynamic_material(
    bench_t *b)
{
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_shapes2d_drop };
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    int32_t i;
    for (i = 0; i < BENCH_SHAPES2D_STATIC + BENCH_SHAPES2D_DYNAMIC; i ++) {
        bool dynamic = i >= BENCH_SHAPES2D_STATIC;
        ecs_entity_t e = ecs_new(world);
        if (dynamic) {
            ecs_add(world, e, DynamicMaterial);
        }

        ecs_set(world, e, Position,
            {(float)(i % BENCH_SHAPES2D_ROW) * 4, (float)(i / BENCH_SHAPES2D_ROW) * 4, 0});
        if (i % 3 == 0) {
            ecs_set(world, e, Rect, {3, 2});
        } else if (i % 3 == 1) {
            ecs_set(world, e, Circle, {1.5f});
        } else {
            ecs_set(world, e, Line, {.x1 = -1, .y1 = -1, .x2 = 1, .y2 = 1, .width = 0.5f});
        }

        ecs_set(world, e, Border, {.width = 0.25f, .radius = 0.5f, .color = {255, 200, 0, 255}});
        ecs_set(world, e, Rgb, {(uint8_t)i, 100, 50});
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    box_render_draws_stats_t stats = {0};
    bench_set_items(b, BENCH_SHAPES2D_DYNAMIC);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
    }

    bench_counter(b, "batches", (double)stats.batches);
    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    box_render_sync(world);
    ecs_fini(world);
}

void Shapes2D_changed_1pct(
    bench_t *b)
{
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_shapes2d_drop };
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t *shapes = ecs_os_malloc_n(ecs_entity_t, BENCH_SHAPES2D_STATIC);
    int32_t i;
    for (i = 0; i < BENCH_SHAPES2D_STATIC; i ++) {
        ecs_entity_t e = shapes[i] = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_SHAPES2D_ROW) * 4, (float)(i / BENCH_SHAPES2D_ROW) * 4, 0});
        if (i % 2) {
            ecs_set(world, e, Rect, {3, 2});
        } else {
            ecs_set(world, e, Circle, {1.5f});
        }

        ecs_set(world, e, Border, {.width = 0.25f, .radius = 0.5f, .color = {255, 200, 0, 255}});
        ecs_set(world, e, Rgb, {(uint8_t)i, 100, 50});
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    box_render_draws_stats_t stats = {0};
    int32_t frame = 0;
    bench_set_items(b, BENCH_SHAPES2D_STATIC / 100);
    while (bench_iter(b)) {
        for (i = 0; i < BENCH_SHAPES2D_STATIC / 100; i ++) {
            ecs_entity_t e = shapes[(frame * 7919 + i * 101) % BENCH_SHAPES2D_STATIC];
            ecs_set(world, e, Border, {
                .width = 0.25f + (float)(frame % 4) * 0.05f,
                .radius = 0.5f,
                .color = {255, 200, 0, 255}
            });
        }

        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
        frame ++;
    }

    bench_counter(b, "batches", (double)stats.batches);
    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    box_render_sync(world);
    ecs_fini(world);
    ecs_os_free(shapes);
}
