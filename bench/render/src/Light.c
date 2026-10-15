/**
 * @file Light.c
 * @brief Benchmarks for the light extract: collecting the lights of the scenes every frame.
 *
 * Most cases run frames against a backend that drops the packets, so they measure the main
 * thread: gathering 1000 point lights into the light buffer of their scene and uploading it on
 * change. The cluster case renders the lights on the GPU and waits for every frame.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Point lights of the scene */
#define BENCH_LIGHTS (1000)

/** Lights per row of the grid */
#define BENCH_LIGHTS_ROW (40)

/** Scenes the lights of the scene case are spread over */
#define BENCH_LIGHT_SCENES (10)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_light_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Run frames of a scene with point lights under one parent.
 *
 * @param b The benchmark
 * @param moving Whether the parent moves every frame, so every light changes
 */
static void bench_light(
    bench_t *b,
    bool moving)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_light_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    int32_t i;
    float x = 0;
    for (i = 0; i < BENCH_LIGHTS; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_LIGHTS_ROW) * 2, 1, (float)(i / BENCH_LIGHTS_ROW) * 2});
        ecs_set(world, e, PointLight, {.intensity = 2, .range = 3});
        ecs_set(world, e, Rgb, {255, 200, 100});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_lights_stats_t stats = {0};
    bench_set_items(b, BENCH_LIGHTS);
    while (bench_iter(b)) {
        if (moving) {
            x += 0.01f;
            ecs_set(world, parent, Position, {x, 0, 0});
        }

        ecs_progress(world, 0);
        box_render_lights_stats(world, &stats);
        bench_keep(stats.upload_bytes);
    }

    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    box_render_sync(world);
    ecs_fini(world);
}

/** Run frames of scenes whose lights hang under a group, half of them in Parent storage.
 *
 * @param b The benchmark
 */
static void bench_light_scenes(
    bench_t *b)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_light_drop };
    box_render_set_backend(world, &backend);
    int32_t i, s;
    for (s = 0; s < BENCH_LIGHT_SCENES; s ++) {
        ecs_entity_t scene = ecs_new(world);
        ecs_set(world, scene, Scene, {0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
            .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
        ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, group, Position, {0, 0, 0});
        for (i = 0; i < BENCH_LIGHTS / BENCH_LIGHT_SCENES; i ++) {
            ecs_entity_t e = i % 2
                ? ecs_new_w_parent(world, group, NULL)
                : ecs_new_w_pair(world, EcsChildOf, group);
            ecs_set(world, e, Position,
                {(float)(i % BENCH_LIGHTS_ROW) * 2, 1, (float)(i / BENCH_LIGHTS_ROW) * 2});
            ecs_set(world, e, PointLight, {.intensity = 2, .range = 3});
            ecs_set(world, e, Rgb, {255, 200, 100});
        }
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_lights_stats_t stats = {0};
    bench_set_items(b, BENCH_LIGHTS);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_lights_stats(world, &stats);
        bench_keep(stats.lights);
    }

    bench_counter(b, "scenes", (double)stats.scenes);
    box_render_sync(world);
    ecs_fini(world);
}

/** Render frames of a floor lit by point and spot lights, through the cluster pass.
 *
 * @param b The benchmark
 */
static void bench_light_clustered(
    bench_t *b)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {40, 20, 70});
    ecs_set(world, camera, Rotation, {-0.4f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {40, -0.1f, 25});
    ecs_set(world, floor, Box, {100, 0.2f, 70});
    ecs_set(world, floor, Rgb, {200, 200, 200});
    int32_t i;
    for (i = 0; i < BENCH_LIGHTS; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_LIGHTS_ROW) * 2, 1, (float)(i / BENCH_LIGHTS_ROW) * 2});
        if (i % 10) {
            ecs_set(world, e, PointLight, {.intensity = 2, .range = 3});
        } else {
            ecs_set(world, e, Rotation, {-1.2f, 0, 0});
            ecs_set(world, e, SpotLight, {.intensity = 4, .range = 8, .inner_angle = 20,
                .outer_angle = 30});
        }

        ecs_set(world, e, Rgb, {255, 200, 100});
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    uint32_t word = 0;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_set_items(b, BENCH_LIGHTS);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    ecs_fini(world);
}

void Light_static_1000(
    bench_t *b)
{
    // lighting/lights
    bench_light(b, false);
}

void Light_moving_1000(
    bench_t *b)
{
    // lighting/lights
    bench_light(b, true);
}

void Light_scenes_1000(
    bench_t *b)
{
    // scenes/isolation
    bench_light_scenes(b);
}

void Light_clustered_1000(
    bench_t *b)
{
    // lighting_fixes/cluster_overflow
    bench_light_clustered(b);
}
