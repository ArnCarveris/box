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
            .orm = textures[(set + 3) % BENCH_TEXTURE_COUNT],
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

/** Side of the noise texture of the layers of the frame cases in pixels */
#define BENCH_TEXTURE_FRAME_SIZE (512)

/** Side of the mask of the blended frame case in pixels, smooth once filtered over the sphere */
#define BENCH_TEXTURE_MASK_SIZE (16)

/** Write a PNG of random bytes into the temporary folder of a world.
 *
 * @param world The world
 * @param name File name in the temporary folder
 * @param size Side of the image in pixels
 * @param seed Seed of the bytes
 * @return The path, freed by the caller
 */
static char* bench_texture_noise(
    ecs_world_t *world,
    const char *name,
    int32_t size,
    uint32_t seed)
{
    char *file = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    uint8_t *px = ecs_os_malloc(size * size * 4);
    int32_t i;
    for (i = 0; i < size * size * 4; i ++) {
        seed = seed * 1664525u + 1013904223u;
        px[i] = (uint8_t)(seed >> 24);
    }

    stbi_write_png(file, size, size, 4, px, size * 4);
    ecs_os_free(px);

    return file;
}

/** Render frames of a lit sphere that fills a 3840x2160 surface, either with one set of
 * PbrTextures or with a TextureBlend of four layers of PbrTextures under a noise mask, so most
 * fragments weigh several layers. Every frame waits for the GPU, so a case measures a frame.
 * With a contrast above 0 the layers have the noise as their heights.
 *
 * @param b The benchmark
 * @param blended Whether the sphere blends four layers
 * @param contrast TextureBlend.contrast of the blended sphere, 0 for the linear blend
 */
static void bench_texture_frame(
    bench_t *b,
    bool blended,
    float contrast)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    char *file = bench_texture_noise(world, "bench_layer.png", BENCH_TEXTURE_FRAME_SIZE, 1);
    char *mask_file = bench_texture_noise(world, "bench_mask.png", BENCH_TEXTURE_MASK_SIZE, 7);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t mask = ecs_new(world);
    ecs_set(world, mask, TexturePath, {.path = mask_file});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 4.2f});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {110, 150, 210, 255}}, .lighting.enabled = true});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {.radius = 3.0f, .smooth = true, .segments = 64});
    ecs_set(world, e, Rgb, {220, 200, 180});
    ecs_set(world, e, PbrTextures, {.albedo = tex, .orm = tex, .normal = tex, .emissive = tex});
    ecs_set(world, e, Emissive, {.strength = 0.1f, .color = {255, 255, 255}});
    if (blended) {
        TextureBlend blend = { .mask = mask, .contrast = contrast };
        int32_t i;
        for (i = 0; i < 4; i ++) {
            blend.layers[i] = ecs_new(world);
            ecs_set(world, blend.layers[i], PbrTextures, {.albedo = tex, .orm = tex,
                .normal = tex});
            ecs_set(world, blend.layers[i], TextureTransform, {.scale_x = (float)(i + 1),
                .scale_y = (float)(i + 1), .offset_x = 0.25f * (float)i});
            blend.heights[i] = contrast > 0.0f ? tex : 0;
        }

        ecs_set_ptr(world, e, TextureBlend, &blend);
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 3840, .height = 2160, .resolution_scale = 1});
    int32_t frame;
    for (frame = 0; frame < 100; frame ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        const TextureStatus *a = ecs_get(world, tex, TextureStatus);
        const TextureStatus *m = ecs_get(world, mask, TextureStatus);
        if (frame >= 3 && a && a->state != TextureLoading && m && m->state != TextureLoading) {
            break;
        }
    }

    uint32_t word = 0;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    box_render_gpu_stats_t stats = {0};
    if (box_render_gpu_stats(world, &stats)) {
        bench_counter(b, "blend_bytes", (double)stats.blend_bytes);
        bench_counter(b, "blend_arrays", (double)stats.blend_arrays);
        bench_counter(b, "blend_builds", (double)stats.blend_builds);
    }

    ecs_fini(world);
    ecs_os_free(file);
    ecs_os_free(mask_file);
}

void Texture_single_frame(
    bench_t *b)
{
    // materials/texture_blend
    bench_texture_frame(b, false, 0.0f);
}

void Texture_blend_four_frame(
    bench_t *b)
{
    // materials/texture_blend
    bench_texture_frame(b, true, 0.0f);
}

void Texture_blend_four_height_frame(
    bench_t *b)
{
    // materials/texture_blend
    bench_texture_frame(b, true, 16.0f);
}
