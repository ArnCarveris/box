/**
 * @file Terrain.c
 * @brief Benchmark for an unbounded Terrain of 6 levels of detail around a camera that flies
 * over it, rendered headless on the main thread with shadows: frame cost and memory.
 *
 * The heights are ridged noise in a texture graph made in code, so the bench needs no asset
 * files. The camera moves 2 m per frame, so tiles are made and dropped as it flies.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#include "bench_memory.h"

/** Meters the camera moves per frame */
#define BENCH_TERRAIN_SPEED (2.0f)

/** Frames the terrain gets to make its tiles before the measure */
#define BENCH_TERRAIN_SETTLE (600)

void Terrain_unbounded_6_levels(
    bench_t *b)
{
    // terrain/terrain_tiles
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_add(world, WorldOrigin);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 120, 0});
    ecs_set(world, camera, Rotation, {-0.3f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 6000});
    ecs_add_id(world, camera, WorldFocus);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {120, 150, 200, 255}}, .lighting.enabled = true,
        .culling = {.enabled = true, .min_dimension_px = 1},
        .shadows = {.enabled = true, .cascades = 4, .resolution = 2048,
            .max_distance = 400, .split_lambda = 0.7f, .depth_bias = 2,
            .normal_offset = 2, .filter_size = 3, .fade = 0.2f, .blend = 0.1f}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.6f, 2.3f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});

    ecs_entity_t terrain = ecs_new(world);
    ecs_entity_t noise = ecs_new_w_pair(world, EcsChildOf, terrain);
    ecs_set(world, noise, TexNoise, {.frequency = 0.004f, .octaves = 6, .lacunarity = 2,
        .gain = 0.5f, .seed = 11, .fractal = NoiseRidged, .offset = 1, .channels = 1});
    ecs_entity_t height = ecs_new_w_pair(world, EcsChildOf, terrain);
    ecs_set(world, height, TexMultiply, {.a = noise, .b_value = {150, 150, 150, 150}});
    ecs_set(world, terrain, Terrain, {.height = height, .tile_size = 32, .resolution = 65,
        .levels = 6, .min_height = -10, .max_height = 300, .budget = 16});
    ecs_set(world, terrain, Rgb, {110, 130, 80});
    ecs_set(world, terrain, PbrMaterial, {.roughness = 1});

    int32_t i;
    float x = 0;
    for (i = 0; i < BENCH_TERRAIN_SETTLE; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
        if (status && !status->pending && status->drawn) {
            break;
        }
    }

    box_render_gpu_stats_t before, after;
    uint32_t word = 0;
    double frames = 0;
    box_render_gpu_stats(world, &before);
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        x += BENCH_TERRAIN_SPEED;
        ecs_set(world, camera, Position, {x, 120, 0});
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    box_render_gpu_stats(world, &after);
    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    bench_counter(b, "frames", frames);
    bench_counter(b, "cull_gpu_ns", (double)(after.cull_ns - before.cull_ns));
    bench_counter(b, "tiles_drawn", status ? (double)status->drawn : 0);
    bench_counter(b, "tiles_pending", status ? (double)status->pending : 0);
    bench_counter(b, "tiles_created", status ? (double)status->created : 0);
    bench_counter(b, "terrain_gpu_bytes", status ? (double)status->gpu_bytes : 0);
    box_render_draws_stats_t stats = {0};
    box_render_draws_stats(world, &stats);
    bench_counter(b, "batches", (double)stats.batches);
    bench_render_memory(b, &stats);
    ecs_fini(world);
}
