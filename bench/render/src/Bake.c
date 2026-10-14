/**
 * @file Bake.c
 * @brief Benchmarks for the per frame cost of a scene whose probe volume is baked.
 *
 * A headless world renders 2500 static boxes into a 1280x720 offscreen surface on the main
 * thread and waits for the GPU after every frame; a headless bake converges in the first frame.
 *
 * A converged bake must capture nothing and cost what a never baked volume (budget_ms 0) does.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Boxes along each axis */
#define BENCH_BAKE_ROW (50)

/** Render frames of a scene with or without a probe volume.
 *
 * @param b The benchmark
 * @param baked Whether the scene has a LightBake
 * @param budget Milliseconds per frame the bake may spend, 0 never bakes
 */
static void bench_bake(
    bench_t *b,
    bool baked,
    float budget)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    if (baked) {
        ecs_singleton_set(world, LightBake, {.probe_spacing = 4, .bounds_padding = 1,
            .capture_size = 16, .budget_ms = budget, .bounces = 1});
    }

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 30, 80});
    ecs_set(world, camera, Rotation, {-0.4f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {24, 28, 36, 255}}, .lighting.enabled = true, .lighting.baked = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.9f, 2.4f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {102, 0.2f, 102});
    ecs_set(world, floor, Rgb, {90, 96, 104});
    int32_t x, z, i;
    for (x = 0; x < BENCH_BAKE_ROW; x ++) {
        for (z = 0; z < BENCH_BAKE_ROW; z ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)x * 2 - 49, 0.5f, (float)z * 2 - 49});
            ecs_set(world, e, Box, {0.8f, 1, 0.8f});
            ecs_set(world, e, Rgb, {200, 120, 80});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t before, after;
    uint32_t word = 0;
    box_render_gpu_stats(world, &before);
    bench_set_items(b, BENCH_BAKE_ROW * BENCH_BAKE_ROW);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    box_render_gpu_stats(world, &after);
    bench_counter(b, "captured", (double)(after.probes - before.probes));
    if (baked) {
        const LightBakeStatus *status = ecs_get(world, ecs_id(LightBake), LightBakeStatus);
        bench_counter(b, "probes", (double)status->probes);
        bench_counter(b, "converged", status->converged ? 1.0 : 0.0);
    }

    ecs_fini(world);
}

void Bake_none(
    bench_t *b)
{
    // baked_lighting/probe_capture
    bench_bake(b, false, 0);
}

void Bake_unbaked(
    bench_t *b)
{
    // baked_lighting/probe_capture
    bench_bake(b, true, 0);
}

void Bake_converged(
    bench_t *b)
{
    // baked_lighting/probe_capture
    bench_bake(b, true, 2);
}
