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

/** Fonts the typing case spreads its glyphs over, so it never runs out of new ones */
#define BENCH_TEXT_FONTS (4)

/** Bytes of atlas texture data the backend received */
static uint64_t bench_text_atlas_bytes;

/** Accept a packet without rendering it, counting the bytes of its texture data.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_text_count(
    void *ctx,
    const box_render_packet_t *packet)
{
    int32_t i;
    (void)ctx;
    for (i = 0; i < ecs_vec_count(&packet->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &packet->resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_TEXTURE && r->op != BOX_RENDER_DESTROY) {
            bench_text_atlas_bytes += r->size;
        }
    }
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
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t *texts = ecs_os_malloc_n(ecs_entity_t, BENCH_TEXT_COUNT);
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    int32_t i, frame = 0;
    for (i = 0; i < BENCH_TEXT_COUNT; i ++) {
        ecs_entity_t e = texts[i] = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_TEXT_ROW) * 120, (float)(i / BENCH_TEXT_ROW) * 20, 0});
        ecs_set(world, e, Text, {.value = "Label number", .size = 14, .width = 60, .font = font});
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
                .width = 60,
                .font = font
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
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t *texts = ecs_os_malloc_n(ecs_entity_t, BENCH_TEXT_COUNT);
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    int32_t i, frame = 0;
    for (i = 0; i < BENCH_TEXT_COUNT; i ++) {
        ecs_entity_t e = texts[i] = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_TEXT_ROW) * 120, (float)(i / BENCH_TEXT_ROW) * 20, 0});
        ecs_set(world, e, Text, {.value = "Score 0000", .size = 14, .font = font});
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
            ecs_set(world, e, Text, {.value = value, .size = 14, .font = font});
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

void Text_type_new_glyphs(
    bench_t *b)
{
    // ui_fixes/atlas_upload
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_text_count };
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    const Font *def = ecs_get(world, ecs_lookup(world, "box.text.DefaultFont"), Font);
    char *path = ecs_os_strdup(def->path);
    ecs_entity_t fonts[BENCH_TEXT_FONTS], texts[BENCH_TEXT_FONTS];
    int32_t i, typed = 0;
    for (i = 0; i < BENCH_TEXT_FONTS; i ++) {
        fonts[i] = ecs_new(world);
        ecs_set(world, fonts[i], Font, {.path = path});
        texts[i] = ecs_new(world);
        ecs_set(world, texts[i], Position, {(float)i * 40, 0, 0});
        ecs_set(world, texts[i], Text, {.value = "a", .size = 24, .font = fonts[i]});
        ecs_set(world, texts[i], Rgb, {255, 255, 255});
    }

    for (i = 0; i < 8; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    bench_text_atlas_bytes = 0;
    char value[8];
    while (bench_iter(b)) {
        int32_t step = typed % 317, f = (typed / 317) % BENCH_TEXT_FONTS;
        uint32_t cp = (uint32_t)(step < 94 ? 0x21 + step : 0xA1 + step - 94);
        if (step == 0 && typed >= 317 * BENCH_TEXT_FONTS) {
            bench_pause(b);
            ecs_delete(world, fonts[f]);
            fonts[f] = ecs_new(world);
            ecs_set(world, fonts[f], Font, {.path = path});
            ecs_set(world, texts[f], Text, {.value = "a", .size = 24, .font = fonts[f]});
            ecs_progress(world, 0);
            box_render_sync(world);
            bench_resume(b);
        }

        if (cp < 0x80) {
            value[0] = (char)cp;
            value[1] = 0;
        } else {
            value[0] = (char)(0xC0 | (cp >> 6));
            value[1] = (char)(0x80 | (cp & 0x3F));
            value[2] = 0;
        }

        ecs_set(world, texts[f], Text, {.value = value, .size = 24, .font = fonts[f]});
        ecs_progress(world, 0);
        typed ++;
    }

    box_render_sync(world);
    bench_counter(b, "glyphs", (double)typed);
    bench_counter(b, "atlas_bytes_per_glyph",
        typed ? (double)bench_text_atlas_bytes / (double)typed : 0.0);
    ecs_fini(world);
    ecs_os_free(path);
}
