/**
 * @file Blended.c
 * @brief Benchmark for the draws of many blended instances of one batch on the GPU.
 *
 * 10000 half transparent boxes of one mesh render into a 320x180 headless surface while the
 * camera moves, waiting for the GPU every frame; instructions (macOS) are those the process
 * retired over the measured frames, frames the frames they ran.
 *
 * The transmission cases put glass panes over 100 blended boxes in a 1280x720 surface: none,
 * one pane in front of them, or a pane between every group of them up to the budget of
 * snapshots of a view (BOX_GPU_TRANSMISSION_SNAPSHOTS). Their time per frame against the case
 * without glass is the GPU time of the snapshots; snapshots counts those recorded per frame.
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

/** Blended boxes of the transmission cases */
#define BENCH_BLENDED_GLASS_COUNT (100)

/** Snapshots of transmission a view takes at most in a frame, BOX_GPU_TRANSMISSION_SNAPSHOTS of
 * src/render/gpu.h */
#define BENCH_BLENDED_SNAPSHOTS (4)

/** Render frames of 100 blended boxes behind glass panes.
 *
 * The boxes stand in BENCH_BLENDED_SNAPSHOTS groups at increasing depth. With layers panes the
 * last pane stands in front of every group and each other one between two groups, so every
 * pane follows blended draws and takes a snapshot.
 *
 * @param b The benchmark
 * @param layers Glass panes, 0 for none
 */
static void bench_blended_glass(
    bench_t *b,
    int32_t layers)
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
        .color = {30, 30, 40, 255}}, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -30});
    ecs_set(world, wall, Box, {80, 50, 1});
    ecs_set(world, wall, Rgb, {120, 110, 100});

    int32_t i, group = BENCH_BLENDED_GLASS_COUNT / BENCH_BLENDED_SNAPSHOTS;
    for (i = 0; i < BENCH_BLENDED_GLASS_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        int32_t g = i / group, k = i % group;
        ecs_set(world, e, Position, {(float)(k % 5) * 3.0f - 6.0f,
            (float)(k / 5) * 2.4f - 4.8f, -24.0f + (float)g * 4.0f});
        ecs_set(world, e, Box, {1.5f, 1.5f, 0.2f});
        ecs_set(world, e, Rgb, {(uint8_t)(i * 37 % 256), 120, 200});
        ecs_set(world, e, Opacity, {0.5f});
    }

    for (i = 0; i < layers; i ++) {
        ecs_entity_t e = ecs_new(world);
        float z = layers == 1 ? -8.0f :
            -22.0f + (float)(i + BENCH_BLENDED_SNAPSHOTS - layers) * 4.0f;
        ecs_set(world, e, Position, {0, 0, z});
        ecs_set(world, e, Box, {18, 14, 0.1f});
        ecs_set(world, e, Rgb, {255, 255, 255});
        ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.2f});
        ecs_set(world, e, Transmission, {.transmission_factor = 1, .ior = 1.5f,
            .thickness_factor = 0.1f, .attenuation_distance = 1e10f,
            .attenuation_color = {255, 255, 255}});
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    box_render_gpu_stats_t before, after;
    uint32_t word = 0;
    double frames = 0;
    box_render_gpu_stats(world, &before);
    bench_set_items(b, BENCH_BLENDED_GLASS_COUNT);
    while (bench_iter(b)) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    box_render_gpu_stats(world, &after);
    bench_counter(b, "frames", frames);
    bench_counter(b, "snapshots", frames ? (double)(after.transmission_snapshots -
        before.transmission_snapshots) / frames : 0);
    ecs_fini(world);
}

void Blended_gpu_100_no_glass(
    bench_t *b)
{
    // render_quality/transmission_blended
    bench_blended_glass(b, 0);
}

void Blended_gpu_100_glass_one_layer(
    bench_t *b)
{
    // render_quality/transmission_blended
    bench_blended_glass(b, 1);
}

void Blended_gpu_100_glass_budget_layers(
    bench_t *b)
{
    // render_quality/transmission_blended
    bench_blended_glass(b, BENCH_BLENDED_SNAPSHOTS);
}
