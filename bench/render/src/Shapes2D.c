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
#include "bench_memory.h"

/** Static 2D shapes of the scene */
#define BENCH_SHAPES2D_STATIC (100000)

/** Dynamic 2D shapes of the scene */
#define BENCH_SHAPES2D_DYNAMIC (10000)

/** Shapes per row of the grid */
#define BENCH_SHAPES2D_ROW (300)

/** Panels of the UI tree, each with an OrderedChildren list of rows */
#define BENCH_SHAPES2D_PANELS (100)

/** Rows of a panel of the UI tree, each a background with an icon */
#define BENCH_SHAPES2D_ROWS (50)

/** Clip panels of the clipped scene */
#define BENCH_SHAPES2D_CLIPS (4)

/** 2D shapes under the clip panels of the clipped scene */
#define BENCH_SHAPES2D_CLIPPED (1000)

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
    bench_render_memory(b, &stats);
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
    bench_render_memory(b, &stats);
    box_render_sync(world);
    ecs_fini(world);
    ecs_os_free(shapes);
}

void Shapes2D_ui_tree_idle(
    bench_t *b)
{
    // ui/screen
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
    ecs_set(world, view, View, {.camera = camera});
    int32_t p, r;
    for (p = 0; p < BENCH_SHAPES2D_PANELS; p ++) {
        ecs_entity_t panel = ecs_new(world);
        ecs_add_id(world, panel, EcsOrderedChildren);
        ecs_set(world, panel, Position, {(float)(p % 10) * 120, (float)(p / 10) * 220, 0});
        ecs_set(world, panel, Rect, {110, 210});
        ecs_set(world, panel, Rgb, {40, 44, 56});
        for (r = 0; r < BENCH_SHAPES2D_ROWS; r ++) {
            ecs_entity_t row = ecs_new_w_pair(world, EcsChildOf, panel);
            ecs_set(world, row, Position, {0, (float)r * 4 - 100, 0});
            ecs_set(world, row, Rect, {100, 3});
            ecs_set(world, row, Rgb, {60, 66, 84});
            ecs_entity_t icon = ecs_new_w_pair(world, EcsChildOf, row);
            ecs_set(world, icon, Position, {-45, 0, 0});
            ecs_set(world, icon, Circle, {1.5f});
            ecs_set(world, icon, Rgb, {250, 190, 60});
        }
    }

    for (p = 0; p < 4; p ++) {
        ecs_progress(world, 0);
    }

    box_render_draws_stats_t stats = {0};
    bench_set_items(b, BENCH_SHAPES2D_PANELS * (1 + BENCH_SHAPES2D_ROWS * 2));
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
    }

    bench_counter(b, "batches", (double)stats.batches);
    bench_render_memory(b, &stats);
    box_render_sync(world);
    ecs_fini(world);
}

void Shapes2D_ui_tree_add_remove(
    bench_t *b)
{
    // ui/screen
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
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t *panels = ecs_os_malloc_n(ecs_entity_t, BENCH_SHAPES2D_PANELS);
    int32_t p, r;
    for (p = 0; p < BENCH_SHAPES2D_PANELS; p ++) {
        ecs_entity_t panel = panels[p] = ecs_new(world);
        ecs_add_id(world, panel, EcsOrderedChildren);
        ecs_set(world, panel, Position, {(float)(p % 10) * 120, (float)(p / 10) * 220, 0});
        ecs_set(world, panel, Rect, {110, 210});
        ecs_set(world, panel, Rgb, {40, 44, 56});
        for (r = 0; r < BENCH_SHAPES2D_ROWS; r ++) {
            ecs_entity_t row = ecs_new_w_pair(world, EcsChildOf, panel);
            ecs_set(world, row, Position, {0, (float)r * 4 - 100, 0});
            ecs_set(world, row, Rect, {100, 3});
            ecs_set(world, row, Rgb, {60, 66, 84});
            ecs_entity_t icon = ecs_new_w_pair(world, EcsChildOf, row);
            ecs_set(world, icon, Position, {-45, 0, 0});
            ecs_set(world, icon, Circle, {1.5f});
            ecs_set(world, icon, Rgb, {250, 190, 60});
        }
    }

    for (p = 0; p < 4; p ++) {
        ecs_progress(world, 0);
    }

    box_render_draws_stats_t stats = {0};
    ecs_entity_t added = 0;
    int32_t frame = 0;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        if (added) {
            ecs_delete(world, added);
        }

        added = ecs_new_w_pair(world, EcsChildOf, panels[(frame * 37) % BENCH_SHAPES2D_PANELS]);
        ecs_set(world, added, Position, {0, 100, 0});
        ecs_set(world, added, Rect, {100, 3});
        ecs_set(world, added, Rgb, {230, 80, 70});
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
        frame ++;
    }

    bench_counter(b, "batches", (double)stats.batches);
    bench_render_memory(b, &stats);
    box_render_sync(world);
    ecs_fini(world);
    ecs_os_free(panels);
}

void Shapes2D_clipped_1000_moving_clips(
    bench_t *b)
{
    // ui/clip
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
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t clips[BENCH_SHAPES2D_CLIPS];
    int32_t c, i;
    for (c = 0; c < BENCH_SHAPES2D_CLIPS; c ++) {
        ecs_entity_t clip = clips[c] = ecs_new(world);
        ecs_add_id(world, clip, ClipChildren);
        ecs_set(world, clip, Position, {(float)c * 220, 0, 0});
        ecs_set(world, clip, Rect, {200, 200});
        ecs_set(world, clip, Border, {.radius = 16});
        ecs_set(world, clip, Rgb, {40, 44, 56});
        for (i = 0; i < BENCH_SHAPES2D_CLIPPED / BENCH_SHAPES2D_CLIPS; i ++) {
            ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, clip);
            ecs_set(world, e, Position,
                {(float)(i % 16) * 16 - 120, (float)(i / 16) * 16 - 120, 0});
            if (i % 2) {
                ecs_set(world, e, Rect, {14, 14});
            } else {
                ecs_set(world, e, Circle, {7});
            }

            ecs_set(world, e, Rgb, {250, 190, 60});
        }
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    box_render_draws_stats_t stats = {0};
    int32_t frame = 0;
    bench_set_items(b, BENCH_SHAPES2D_CLIPPED);
    while (bench_iter(b)) {
        c = frame % BENCH_SHAPES2D_CLIPS;
        ecs_set(world, clips[c], Position, {(float)c * 220, (float)(frame % 8), 0});
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes + stats.clip_bytes);
        frame ++;
    }

    bench_counter(b, "batches", (double)stats.batches);
    bench_counter(b, "clips", (double)stats.clips);
    bench_counter(b, "clip_bytes", (double)stats.clip_bytes);
    bench_render_memory(b, &stats);
    box_render_sync(world);
    ecs_fini(world);
}
