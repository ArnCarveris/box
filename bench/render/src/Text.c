/**
 * @file Text.c
 * @brief Benchmarks for laying out texts and bringing their glyphs into the 2D batches.
 *
 * Labels of a dozen glyphs run frames against a backend that drops the packets, so a case
 * measures the main thread: kerning, line breaks, bounds and the glyph instances.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Texts of the scene */
#define BENCH_TEXT_COUNT (10000)

/** Texts per row of the grid */
#define BENCH_TEXT_ROW (100)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_text_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

void Text_layout_10k(
    bench_t *b)
{
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_text_drop };
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t *texts = ecs_os_malloc_n(ecs_entity_t, BENCH_TEXT_COUNT);
    int32_t i, frame = 0;
    for (i = 0; i < BENCH_TEXT_COUNT; i ++) {
        ecs_entity_t e = texts[i] = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_TEXT_ROW) * 120, (float)(i / BENCH_TEXT_ROW) * 20, 0});
        ecs_set(world, e, Text, {.value = "Label number", .size = 14, .width = 60});
        ecs_set(world, e, Rgb, {255, 255, 255});
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    box_render_draws_stats_t stats = {0};
    bench_set_items(b, BENCH_TEXT_COUNT);
    while (bench_iter(b)) {
        for (i = 0; i < BENCH_TEXT_COUNT; i ++) {
            ecs_set(world, texts[i], Text, {
                .value = frame % 2 ? "Label number" : "Other label",
                .size = 14,
                .width = 60
            });
        }

        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
        frame ++;
    }

    bench_counter(b, "batches", (double)stats.batches);
    bench_counter(b, "instances", (double)stats.instances);
    box_render_sync(world);
    ecs_fini(world);
    ecs_os_free(texts);
}

void Text_changed_1pct(
    bench_t *b)
{
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_text_drop };
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t *texts = ecs_os_malloc_n(ecs_entity_t, BENCH_TEXT_COUNT);
    int32_t i, frame = 0;
    for (i = 0; i < BENCH_TEXT_COUNT; i ++) {
        ecs_entity_t e = texts[i] = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_TEXT_ROW) * 120, (float)(i / BENCH_TEXT_ROW) * 20, 0});
        ecs_set(world, e, Text, {.value = "Score 0000", .size = 14});
        ecs_set(world, e, Rgb, {255, 255, 255});
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    box_render_draws_stats_t stats = {0};
    char value[32];
    bench_set_items(b, BENCH_TEXT_COUNT / 100);
    while (bench_iter(b)) {
        for (i = 0; i < BENCH_TEXT_COUNT / 100; i ++) {
            ecs_entity_t e = texts[(frame * 7919 + i * 101) % BENCH_TEXT_COUNT];
            ecs_os_snprintf(value, ECS_SIZEOF(value), "Score %04d", (frame * 37 + i) % 10000);
            ecs_set(world, e, Text, {.value = value, .size = 14});
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
    ecs_os_free(texts);
}
