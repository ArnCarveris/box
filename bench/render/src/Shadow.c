/**
 * @file Shadow.c
 * @brief Benchmarks for the cost of the cascaded shadow pass on the GPU.
 *
 * A headless world renders 100k static boxes on a floor into a 1280x720 offscreen surface on
 * the main thread, and waits for the GPU after every frame by reading back a word of the
 * first instance buffer, so a case measures a whole frame: extract, shadow and forward pass.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Boxes along x */
#define BENCH_SHADOW_X (400)

/** Boxes along z */
#define BENCH_SHADOW_Z (250)

/** Render frames of 100k boxes lit by a sun, with or without shadows.
 *
 * @param b The benchmark
 * @param shadows Whether the view renders shadows
 */
static void bench_shadow(
    bench_t *b,
    bool shadows)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 60, 160});
    ecs_set(world, camera, Rotation, {-0.4f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {24, 28, 36, 255}}, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .shadows = {.enabled = shadows, .cascades = 4, .resolution = 2048,
            .max_distance = 200, .split_lambda = 0.7f, .depth_bias = 2,
            .normal_offset = 1.5f, .filter_size = 3, .fade = 0.2f}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {204, 0.2f, 129});
    ecs_set(world, floor, Rgb, {90, 96, 104});
    int32_t x, z;
    for (x = 0; x < BENCH_SHADOW_X; x ++) {
        for (z = 0; z < BENCH_SHADOW_Z; z ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)x * 0.5f - 99.75f,
                0.3f + (float)(x % 5) * 0.1f, (float)z * 0.5f - 62.25f});
            ecs_set(world, e, Box, {0.35f, 0.6f + (float)(x % 5) * 0.2f, 0.35f});
            ecs_set(world, e, Rgb, {200, 120, 80});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    uint32_t word = 0;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_set_items(b, BENCH_SHADOW_X * BENCH_SHADOW_Z);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    ecs_fini(world);
}

void Shadow_off_100k(
    bench_t *b)
{
    // lighting/shadows
    bench_shadow(b, false);
}

void Shadow_on_100k(
    bench_t *b)
{
    // lighting/shadows
    bench_shadow(b, true);
}
