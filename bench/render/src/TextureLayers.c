/**
 * @file TextureLayers.c
 * @brief Benchmarks of the cost of TextureLayers for the shapes without them: a grid of lit
 * boxes without layers and a few boxes with layers whose amount animates every frame, rendered
 * with Scene.texture_layers off (the layers ignored) and on, which must cost the same frame time
 * and draw the plain shapes with the same draws; and the frame of a grid with layers.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#include "draw.h"

/** Boxes of the grid without layers */
#define BENCH_LAYERS_PLAIN (4096)

/** Boxes with layers next to the grid in the cases that have them */
#define BENCH_LAYERS_LAYERED (16)

/** Boxes per row of the grid */
#define BENCH_LAYERS_ROW (64)

/** Width and height of the surface the frames render into */
#define BENCH_LAYERS_SIZE (1920)

/** What a case renders */
typedef enum bench_layers_case_t {
    BENCH_LAYERS_OFF,       /**< The plain grid and the layered boxes, Scene.texture_layers
                             *   off */
    BENCH_LAYERS_ON,        /**< The plain grid and the layered boxes animating, the flag on */
    BENCH_LAYERS_ALL        /**< Every box of the grid with two layers, the flag on */
} bench_layers_case_t;

/** Render frames of a grid of boxes that fills a surface, every frame waiting for the GPU so a
 * case measures a frame, and report the extract batches and the draws per frame.
 *
 * @param b The benchmark
 * @param which What the case renders
 */
static void bench_layers_frame(
    bench_t *b,
    bench_layers_case_t which)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, Scene, {
        .shader_features.Extended.texture_layers = which != BENCH_LAYERS_OFF});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 60, 70});
    ecs_set(world, camera, Rotation, {-0.7f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {110, 150, 210, 255}}, .lighting.enabled = true});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.8f, 0.5f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});

    ecs_entity_t snow = ecs_new(world);
    ecs_set(world, snow, TextureLayer, {.amount = 0.5f, .direction = {0, 1, 0}, .facing = 0.6f,
        .contrast = 8, .albedo = LayerMix, .roughness = LayerMix});
    ecs_set(world, snow, Rgb, {250, 250, 255});
    ecs_entity_t wet = ecs_new(world);
    ecs_set(world, wet, TextureLayer, {.amount = 1, .contrast = 8, .albedo = LayerMultiply,
        .roughness = LayerMultiply, .normal = 0.3f, .porosity = 1});
    ecs_set(world, wet, Rgb, {153, 153, 153});
    ecs_set(world, wet, PbrMaterial, {.roughness = 0.3f});

    int32_t i;
    for (i = 0; i < BENCH_LAYERS_PLAIN; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % BENCH_LAYERS_ROW) * 1.5f - 48.0f, 0,
            (float)(i / BENCH_LAYERS_ROW) * 1.5f - 48.0f});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {(uint8_t)(100 + i % 100), 120, 140});
        if (which == BENCH_LAYERS_ALL) {
            ecs_set(world, e, TextureLayers, {.layers = {wet, snow}});
        }
    }

    for (i = 0; which != BENCH_LAYERS_ALL && i < BENCH_LAYERS_LAYERED; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 1.5f - 12.0f, 0, 52.0f});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, TextureLayers, {.layers = {wet, snow}});
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = BENCH_LAYERS_SIZE, .height = BENCH_LAYERS_SIZE,
        .resolution_scale = 1});
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    box_render_gpu_stats_t before = {0}, after = {0};
    box_render_gpu_stats(world, &before);
    uint32_t word = 0;
    int64_t frames = 0;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        TextureLayer *l = ecs_get_mut(world, snow, TextureLayer);
        l->amount = 0.5f + 0.5f * sinf((float)frames * 0.1f);
        ecs_modified(world, snow, TextureLayer);

        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    if (box_render_gpu_stats(world, &after) && frames) {
        bench_counter(b, "draws_per_frame", (double)(after.draws - before.draws) /
            (double)frames);
        bench_counter(b, "pipelines", (double)after.pipelines);
    }

    const box_render_draws_t *d = box_draws_state(world);
    if (d) {
        bench_counter(b, "batches", (double)d->stats.batches);
    }

    ecs_fini(world);
}

void TextureLayers_plain_off_frame(
    bench_t *b)
{
    // materials/texture_layers
    bench_layers_frame(b, BENCH_LAYERS_OFF);
}

void TextureLayers_plain_on_animating_frame(
    bench_t *b)
{
    // materials/texture_layers
    bench_layers_frame(b, BENCH_LAYERS_ON);
}

void TextureLayers_all_layered_frame(
    bench_t *b)
{
    // materials/texture_layers
    bench_layers_frame(b, BENCH_LAYERS_ALL);
}
