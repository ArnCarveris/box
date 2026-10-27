/**
 * @file Blended.c
 * @brief Benchmark for the draws of many blended instances of one batch on the GPU.
 *
 * 10000 half transparent boxes of one mesh render into a 320x180 headless surface while the
 * camera moves, waiting for the GPU every frame; instructions (macOS) are those the process
 * retired over the measured frames, frames the frames they ran.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif

/** Blended boxes of the scene */
#define BENCH_BLENDED_COUNT (10000)

/** Boxes per row of the grid */
#define BENCH_BLENDED_ROW (100)

/** Get the instructions the process retired so far.
 *
 * @return The instructions, 0 where the platform does not count them
 */
static uint64_t bench_blended_instructions(void)
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

/** Render frames of the blended boxes.
 *
 * @param b The benchmark
 * @param lit Whether the view is lit and culls by frustum and occlusion, as the View ctor sets
 */
static void bench_blended(
    bench_t *b,
    bool lit)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {50, 30, 120});
    ecs_set(world, camera, Rotation, {-0.3f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {30, 30, 40, 255}}, .lighting.enabled = lit,
        .culling = {.enabled = lit, .occlusion = lit, .min_dimension_px = 1}});
    if (lit) {
        ecs_entity_t sun = ecs_new(world);
        ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
        ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    }

    int32_t i;
    for (i = 0; i < BENCH_BLENDED_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % BENCH_BLENDED_ROW),
            (float)((i * 7) % 13) * 0.5f, -(float)(i / BENCH_BLENDED_ROW)});
        ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
        ecs_set(world, e, Rgb, {(uint8_t)(i % 256), 120, 200});
        ecs_set(world, e, Opacity, {0.5f});
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 320, .height = 180, .resolution_scale = 1});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    uint32_t word = 0;
    float x = 50;
    double frames = 0;
    uint64_t start = bench_blended_instructions();
    bench_set_items(b, BENCH_BLENDED_COUNT);
    while (bench_iter(b)) {
        x += 0.05f;
        ecs_set(world, camera, Position, {x, 30, 120});
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    bench_counter(b, "instructions", (double)(bench_blended_instructions() - start));
    bench_counter(b, "frames", frames);
    ecs_fini(world);
}

void Blended_gpu_10k(
    bench_t *b)
{
    // render_fixes/blended_draws
    bench_blended(b, false);
}

void Blended_gpu_10k_lit_culled(
    bench_t *b)
{
    // render_fixes/blended_draws
    bench_blended(b, true);
}
