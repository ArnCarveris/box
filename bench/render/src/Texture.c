/**
 * @file Texture.c
 * @brief Benchmarks for the extract of textured batches.
 *
 * Shapes spread over many PbrTextures combinations run frames against a backend that drops
 * the packets, so a case measures the main thread: the texture handles every batch resolves.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Static textured shapes of the scene */
#define BENCH_TEXTURE_STATIC (100000)

/** Dynamic textured shapes of the scene */
#define BENCH_TEXTURE_DYNAMIC (10000)

/** Texture entities the shapes pick their layers from */
#define BENCH_TEXTURE_COUNT (16)

/** Distinct PbrTextures combinations, each a batch of its own */
#define BENCH_TEXTURE_SETS (256)

/** Shapes per row of the grid */
#define BENCH_TEXTURE_ROW (300)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_texture_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

void Texture_textured_batches(
    bench_t *b)
{
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_texture_drop };
    box_render_set_backend(world, &backend);
    char *file = flecs_asprintf("%s/bench.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[16 * 16 * 4];
    ecs_os_memset(px, 128, ECS_SIZEOF(px));
    stbi_write_png(file, 16, 16, 4, px, 16 * 4);
    ecs_entity_t textures[BENCH_TEXTURE_COUNT];
    int32_t i;
    for (i = 0; i < BENCH_TEXTURE_COUNT; i ++) {
        textures[i] = ecs_new(world);
        ecs_set(world, textures[i], TexturePath, {.path = file});
    }

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    for (i = 0; i < BENCH_TEXTURE_STATIC + BENCH_TEXTURE_DYNAMIC; i ++) {
        int32_t set = i % BENCH_TEXTURE_SETS;
        bool dynamic = i >= BENCH_TEXTURE_STATIC;
        ecs_entity_t e = ecs_new(world);
        if (dynamic) {
            ecs_add(world, e, DynamicMaterial);
        }

        ecs_set(world, e, Position,
            {(float)(i % BENCH_TEXTURE_ROW), dynamic ? 2.0f : 0.0f,
                (float)(i / BENCH_TEXTURE_ROW)});
        ecs_set(world, e, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, e, Rgb, {200, 100, 50});
        ecs_set(world, e, PbrTextures, {
            .albedo = textures[set % BENCH_TEXTURE_COUNT],
            .emissive = textures[(set / BENCH_TEXTURE_COUNT) % BENCH_TEXTURE_COUNT],
            .roughness = textures[(set + 3) % BENCH_TEXTURE_COUNT],
            .normal = textures[(set + 7) % BENCH_TEXTURE_COUNT]
        });
        ecs_set(world, e, TextureTransform, {.scale_x = 2, .scale_y = 2});
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    box_render_draws_stats_t stats = {0};
    bench_set_items(b, BENCH_TEXTURE_STATIC + BENCH_TEXTURE_DYNAMIC);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
    }

    bench_counter(b, "batches", (double)stats.batches);
    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    box_render_sync(world);
    ecs_fini(world);
    ecs_os_free(file);
}
