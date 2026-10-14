/**
 * @file Material.c
 * @brief Benchmarks for the material data of instances and the sort of blended instances.
 *
 * A case runs frames against a backend that drops the packets, so it measures the main thread:
 * the per frame material fill of DynamicMaterial shapes and the back to front sort per view.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Static opaque shapes of the scene */
#define BENCH_MATERIAL_STATIC (100000)

/** Dynamic material or blended shapes of the scene */
#define BENCH_MATERIAL_SHAPES (10000)

/** Shapes per row of the grid */
#define BENCH_MATERIAL_ROW (300)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_material_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Run frames of a scene with static opaque shapes and shapes with a material.
 *
 * @param b The benchmark
 * @param dynamic Whether the material shapes have DynamicMaterial, else they are blended
 */
static void bench_material(
    bench_t *b,
    bool dynamic)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_material_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t i;
    float x = 0;
    for (i = 0; i < BENCH_MATERIAL_STATIC; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_MATERIAL_ROW), 0, (float)(i / BENCH_MATERIAL_ROW)});
        ecs_set(world, e, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, e, Rgb, {200, 100, 50});
    }

    for (i = 0; i < BENCH_MATERIAL_SHAPES; i ++) {
        ecs_entity_t e = ecs_new(world);
        if (dynamic) {
            ecs_add(world, e, DynamicMaterial);
        }

        ecs_set(world, e, Position,
            {(float)(i % BENCH_MATERIAL_ROW), 2, (float)(i / BENCH_MATERIAL_ROW)});
        ecs_set(world, e, Sphere, {.segments = 8, .radius = 0.25f});
        ecs_set(world, e, Rgb, {50, 100, 200});
        ecs_set(world, e, PbrMaterial, {.metallic = 0.5f, .roughness = 0.3f});
        ecs_set(world, e, Emissive, {.strength = 0.5f, .color = {255, 200, 100}});
        ecs_set(world, e, Opacity, {dynamic ? 1.0f : 0.5f});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_draws_stats_t stats = {0};
    bench_set_items(b, BENCH_MATERIAL_SHAPES);
    while (bench_iter(b)) {
        x += 0.01f;
        ecs_set(world, camera, Position, {x, 10, -20});
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
    }

    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    box_render_sync(world);
    ecs_fini(world);
}

void Material_dynamic_material(
    bench_t *b)
{
    // materials/pbr
    bench_material(b, true);
}

void Material_blended_sort(
    bench_t *b)
{
    // materials/pbr
    bench_material(b, false);
}
