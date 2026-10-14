/**
 * @file Cull.c
 * @brief Benchmarks for the frustum and occlusion cull passes of views on the GPU.
 *
 * Headless worlds render about 100k static boxes into a 1280x720 offscreen surface on the main
 * thread and wait for the GPU after every frame, so a case measures a whole frame.
 *
 * The offscreen cases see a tenth of the boxes; the city hides most of its street boxes.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Boxes along x */
#define BENCH_CULL_X (400)

/** Boxes along z */
#define BENCH_CULL_Z (250)

/** Render frames of 100k boxes lit by a sun.
 *
 * @param b The benchmark
 * @param culled Whether the view culls
 * @param fov Vertical field of view of the camera in degrees
 * @param shadows Whether the view renders shadows
 */
static void bench_cull(
    bench_t *b,
    bool culled,
    float fov,
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
    ecs_set(world, camera, Camera, {.fov = fov});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {24, 28, 36, 255}}, .lighting.enabled = true,
        .culling = {.disabled = !culled, .min_dimension_px = 1},
        .shadows = {.enabled = shadows, .cascades = 4, .resolution = 2048,
            .max_distance = 200, .split_lambda = 0.7f, .depth_bias = 2,
            .normal_offset = 1.5f, .filter_size = 3, .fade = 0.2f}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {-0.7f, 2.5f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {204, 0.2f, 129});
    ecs_set(world, floor, Rgb, {90, 96, 104});
    int32_t x, z;
    for (x = 0; x < BENCH_CULL_X; x ++) {
        for (z = 0; z < BENCH_CULL_Z; z ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)x * 0.5f - 99.75f,
                0.3f + (float)(x % 5) * 0.1f, (float)z * 0.5f - 62.25f});
            ecs_set(world, e, Box, {0.35f, 0.6f + (float)(x % 5) * 0.2f, 0.35f});
            ecs_set(world, e, Rgb, {200, 120, 80});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720});
    uint32_t word = 0;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_set_items(b, BENCH_CULL_X * BENCH_CULL_Z);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    ecs_fini(world);
}

/** Blocks of the city along each axis */
#define BENCH_CITY (50)

/** Render frames of a city whose blocks hide most of the boxes in its streets.
 *
 * @param b The benchmark
 * @param frustum_only Whether the view culls by frustum only, without occlusion
 */
static void bench_city(
    bench_t *b,
    bool frustum_only)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 70, 190});
    ecs_set(world, camera, Rotation, {-0.4f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {150, 180, 220, 255}}, .lighting.enabled = true,
        .culling = {.frustum_only = frustum_only}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {-0.7f, 2.5f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f});
    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {0, -0.1f, 0});
    ecs_set(world, ground, Box, {250, 0.2f, 250});
    int32_t x, z, i;
    for (x = 0; x < BENCH_CITY; x ++) {
        for (z = 0; z < BENCH_CITY; z ++) {
            float h = 12 + (float)((x * 7 + z * 3) % 5) * 3;
            ecs_entity_t block = ecs_new(world);
            ecs_set(world, block, Position, {(float)x * 5 - 122.5f, h * 0.5f,
                (float)z * 5 - 122.5f});
            ecs_set(world, block, Box, {3.6f, h, 3.6f});
            for (i = 0; i < 40; i ++) {
                float t = (float)(i % 10) * 0.42f;
                float side = (float)(i / 10 % 2) * 0.4f;
                ecs_entity_t e = ecs_new(world);
                ecs_set(world, e, Position, {
                    (float)x * 5 - 124.6f + (i < 20 ? t : side),
                    0.15f,
                    (float)z * 5 - 124.6f + (i < 20 ? side : 0.8f + t)});
                ecs_set(world, e, Box, {0.3f, 0.3f, 0.3f});
                ecs_set(world, e, Rgb, {250, 120, 60});
            }
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720});
    uint32_t word = 0;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_set_items(b, BENCH_CITY * BENCH_CITY * 41);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    ecs_fini(world);
}

void Cull_offscreen_100k(
    bench_t *b)
{
    // culling/frustum
    bench_cull(b, true, 15, false);
}

void Cull_offscreen_100k_unculled(
    bench_t *b)
{
    // culling/frustum
    bench_cull(b, false, 15, false);
}

void Cull_shadows_100k(
    bench_t *b)
{
    // culling/frustum
    bench_cull(b, true, 50, true);
}

void Cull_occluded_city_100k(
    bench_t *b)
{
    // culling/hiz
    bench_city(b, false);
}

void Cull_occluded_city_100k_frustum_only(
    bench_t *b)
{
    // culling/hiz
    bench_city(b, true);
}
