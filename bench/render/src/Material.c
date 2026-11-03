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
#include "bench_memory.h"
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif

/** Get the instructions the process retired so far.
 *
 * @return The instructions, 0 where the platform does not count them
 */
static uint64_t bench_material_instructions(void)
{
    uint64_t result = 0;
#ifdef __APPLE__
    struct rusage_info_v4 info;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t*)&info)) {
        result = info.ri_instructions;
    }
#endif

    return result;
}

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
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
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

    box_render_draws_stats_t stats = {0};
    ecs_progress(world, 0);
    box_render_draws_stats(world, &stats);
    int64_t setup_material_bytes = stats.material_bytes;
    ecs_progress(world, 0);
    double frames = 0;
    uint64_t start = bench_material_instructions();
    bench_set_items(b, BENCH_MATERIAL_SHAPES);
    while (bench_iter(b)) {
        x += 0.01f;
        ecs_set(world, camera, Position, {x, 10, -20});
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
        frames ++;
    }

    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    bench_counter(b, "material_bytes", (double)stats.material_bytes);
    bench_counter(b, "setup_material_bytes", (double)setup_material_bytes);
    bench_render_memory(b, &stats);
    box_render_sync(world);
    bench_counter(b, "instructions", (double)(bench_material_instructions() - start));
    bench_counter(b, "frames", frames);
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

void Material_tint_subtree(
    bench_t *b)
{
    // materials/tint
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_material_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, -20});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    int32_t i;
    for (i = 0; i < BENCH_MATERIAL_STATIC; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_MATERIAL_ROW), 0, (float)(i / BENCH_MATERIAL_ROW)});
        ecs_set(world, e, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, e, Rgb, {200, 100, 50});
    }

    ecs_entity_t root = ecs_new(world);
    ecs_entity_t group = 0;
    for (i = 0; i < BENCH_MATERIAL_SHAPES; i ++) {
        if (!(i % 100)) {
            group = ecs_new_w_pair(world, EcsChildOf, root);
            ecs_set(world, group, Tint, {0, 0, 0, 64});
        }

        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, group);
        ecs_set(world, e, Position,
            {(float)(i % BENCH_MATERIAL_ROW), 2, (float)(i / BENCH_MATERIAL_ROW)});
        ecs_set(world, e, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, e, Rgb, {50, 100, 200});
    }

    box_render_draws_stats_t stats = {0};
    ecs_progress(world, 0);
    box_render_draws_stats(world, &stats);
    int64_t setup_material_bytes = stats.material_bytes;
    ecs_progress(world, 0);
    uint8_t strength = 0;
    double frames = 0;
    uint64_t start = bench_material_instructions();
    bench_set_items(b, BENCH_MATERIAL_SHAPES);
    while (bench_iter(b)) {
        strength ++;
        ecs_set(world, root, Tint, {0, 0, 255, strength});
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.processed);
        frames ++;
    }

    bench_counter(b, "processed", (double)stats.processed);
    bench_counter(b, "instructions", (double)(bench_material_instructions() - start));
    bench_counter(b, "frames", frames);
    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    bench_counter(b, "setup_material_bytes", (double)setup_material_bytes);
    bench_render_memory(b, &stats);
    box_render_sync(world);
    ecs_fini(world);
}

void Material_dynamic_material_dynamic_parents(
    bench_t *b)
{
    // core_fixes/unspread_delete_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_material_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t parent = 0;
    int32_t i;
    float x = 0;
    for (i = 0; i < BENCH_MATERIAL_SHAPES; i ++) {
        if (!(i % 100)) {
            parent = ecs_new(world);
            ecs_add(world, parent, DynamicTransform);
            ecs_add(world, parent, DynamicMaterial);
            ecs_set(world, parent, Position, {0, 2, (float)(i / 100)});
        }

        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
        ecs_set(world, e, Position, {(float)(i % 100), 0, 0});
        ecs_set(world, e, Sphere, {.segments = 8, .radius = 0.25f});
        ecs_set(world, e, Rgb, {50, 100, 200});
        ecs_set(world, e, PbrMaterial, {.metallic = 0.5f, .roughness = 0.3f});
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
    bench_render_memory(b, &stats);
    box_render_sync(world);
    ecs_fini(world);
}
