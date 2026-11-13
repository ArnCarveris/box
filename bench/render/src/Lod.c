/**
 * @file Lod.c
 * @brief Benchmarks for the levels of detail of round shapes (LodSettings): spheres and tori
 * spread over distance, drawn with and without levels.
 *
 * A headless world renders 20k shapes lit by a sun with two shadow cascades into a 1280x720
 * offscreen surface on the main thread and waits for the GPU after every frame, so a case
 * measures a whole frame. The triangles counters are what the view and its cascades drew in
 * the last measured frame, read back from the indirect draw arguments.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Shapes along x */
#define BENCH_LOD_X (40)

/** Shapes along z, away from the camera */
#define BENCH_LOD_Z (500)

/** Render frames of spheres and tori over a field that reaches 500 units from the camera.
 *
 * @param b The benchmark
 * @param lod Whether the view draws with three levels of detail
 */
static void bench_lod(
    bench_t *b,
    bool lod)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 6, 10});
    ecs_set(world, camera, Rotation, {-0.08f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {24, 28, 36, 255}}, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .shadows = {.enabled = true, .cascades = 2, .resolution = 2048,
            .max_distance = 120, .split_lambda = 0.7f, .depth_bias = 2,
            .normal_offset = 2, .filter_size = 3, .fade = 0.2f, .blend = 0.1f},
        .lod = {.levels = lod ? 3 : 0, .distances = {0, 30, 90}}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, -250});
    ecs_set(world, floor, Box, {100, 0.2f, 520});
    ecs_set(world, floor, Rgb, {90, 96, 104});
    int32_t x, z, i;
    for (x = 0; x < BENCH_LOD_X; x ++) {
        for (z = 0; z < BENCH_LOD_Z; z ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)x * 2.0f - 39.0f, 0.6f, -(float)z});
            if ((x + z) % 2) {
                ecs_set(world, e, Sphere, {.segments = 48, .smooth = true, .radius = 0.5f});
                ecs_set(world, e, Rgb, {200, 120, 80});
            } else {
                ecs_set(world, e, Torus, {.segments = 48, .rings = 24, .smooth = true,
                    .radius = 0.4f, .tube = 0.15f});
                ecs_set(world, e, Rgb, {80, 140, 200});
            }
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    double frames = 0;
    bench_set_items(b, BENCH_LOD_X * BENCH_LOD_Z);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    box_render_cull_stats_t stats = {0};
    box_render_gpu_cull_stats(world, view, &stats);
    bench_counter(b, "frames", frames);
    bench_counter(b, "visible", (double)stats.visible);
    bench_counter(b, "triangles", (double)(stats.indices / 3));
    bench_counter(b, "caster_triangles", (double)(stats.casters_indices / 3));
    bench_counter(b, "level_0", (double)(stats.levels[0] + stats.casters_levels[0]));
    bench_counter(b, "level_1", (double)(stats.levels[1] + stats.casters_levels[1]));
    bench_counter(b, "level_2", (double)(stats.levels[2] + stats.casters_levels[2]));

    ecs_fini(world);
}

void Lod_spheres_tori_off(
    bench_t *b)
{
    bench_lod(b, false);
}

void Lod_spheres_tori_on(
    bench_t *b)
{
    bench_lod(b, true);
}
