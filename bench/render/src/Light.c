/**
 * @file Light.c
 * @brief Benchmarks for the light extract: collecting the lights of the scenes every frame.
 *
 * Most cases drop the packets, so they measure gathering 1000 point lights into the light
 * buffer of their scene on the main thread. The cluster cases render on the GPU and wait for
 * every frame; the crowded ones render a tiny image, so the cluster pass is most of its work.
 *
 * The baked cases have LightBake: all Mixed lights share the view list with the captures,
 * cycling modes puts the lights into a view and three capture lists.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Point lights of the scene */
#define BENCH_LIGHTS (1000)

/** Lights per row of the grid */
#define BENCH_LIGHTS_ROW (40)

/** Lights per axis of the grid of the crowded cluster cases */
#define BENCH_CROWD_AXIS (8)

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
 * @param modes 0 without LightBake, 1 with LightBake (that bakes nothing) and Mixed lights, 3
 * with LightBake and lights that cycle through the Mixed, Realtime and Baked modes
 */
static void bench_light(
    bench_t *b,
    bool moving,
    int32_t modes)
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
    if (modes) {
        ecs_singleton_set(world, LightBake, {.probe_spacing = 2, .bounces = 0,
            .capture_size = 8, .budget_ms = 0});
    }

    int32_t i;
    float x = 0;
    for (i = 0; i < BENCH_LIGHTS; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_LIGHTS_ROW) * 2, 1, (float)(i / BENCH_LIGHTS_ROW) * 2});
        ecs_set(world, e, PointLight, {.intensity = 2, .range = 3,
            .bake = modes ? (LightBakeMode)(i % modes) : LightBakeModeMixed});
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

/** Render frames of a tiny image whose view frustum is crowded with point or spot lights.
 *
 * The lights fill a grid in front of the camera with ranges that overlap many clusters; the
 * spot lights have narrow cones, so the cone test lists them in a small part of those clusters.
 *
 * @param b The benchmark
 * @param spots Whether the lights are spot lights
 */
static void bench_light_crowded(
    bench_t *b,
    bool spots)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -40});
    ecs_set(world, wall, Box, {80, 50, 0.2f});
    ecs_set(world, wall, Rgb, {200, 200, 200});
    int32_t i, count = BENCH_CROWD_AXIS * BENCH_CROWD_AXIS * BENCH_CROWD_AXIS;
    for (i = 0; i < count; i ++) {
        int32_t x = i % BENCH_CROWD_AXIS, y = (i / BENCH_CROWD_AXIS) % BENCH_CROWD_AXIS;
        int32_t z = i / (BENCH_CROWD_AXIS * BENCH_CROWD_AXIS);
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)x * 2.0f - 7.0f, (float)y - 3.5f, -4.0f - (float)z * 4.0f});
        if (spots) {
            ecs_set(world, e, SpotLight, {.intensity = 1, .range = 8, .inner_angle = 15,
                .outer_angle = 20});
        } else {
            ecs_set(world, e, PointLight, {.intensity = 1, .range = 8});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 18, .resolution_scale = 1});
    uint32_t word = 0;
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_set_items(b, count);
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
    bench_light(b, false, 0);
}

void Light_moving_1000(
    bench_t *b)
{
    // lighting/lights
    bench_light(b, true, 0);
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

void Light_crowded_points_512(
    bench_t *b)
{
    // lighting_fixes/cluster_overflow
    bench_light_crowded(b, false);
}

void Light_crowded_spots_512(
    bench_t *b)
{
    // lighting_fixes/cluster_overflow
    bench_light_crowded(b, true);
}

void Light_moving_modes_1000(
    bench_t *b)
{
    // baked_lighting/light_modes
    bench_light(b, true, 3);
}

void Light_moving_mixed_1000(
    bench_t *b)
{
    // baked_lighting/light_modes
    bench_light(b, true, 1);
}
