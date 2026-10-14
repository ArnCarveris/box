/**
 * @file Light.c
 * @brief Benchmarks for the light extract: collecting the lights of the scenes every frame.
 *
 * A case runs frames against a backend that drops the packets, so it measures the main thread:
 * gathering 1000 point lights into the light buffer of their scene and uploading it on change.
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
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
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
        ecs_set(world, camera, Camera, {.fov = 60});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
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
