/**
 * @file Cull.c
 * @brief Benchmarks for the frustum and occlusion cull passes of views on the GPU.
 *
 * Headless worlds render about 100k static boxes into a 1280x720 offscreen surface on the main
 * thread and wait for the GPU after every frame, so a case measures a whole frame.
 *
 * The offscreen cases see a tenth of the boxes; the city hides most of its street boxes.
 * The instructions counter is what the process retired over the measured frames (macOS).
 */

#include <box.h>
#include <bake_bench.h>
#include <string.h>
#include <unistd.h>
#include "render.h"
#ifdef __APPLE__
#include <libproc.h>
#endif

/** Get the instructions the process retired so far.
 *
 * @return The instructions, 0 where the platform does not count them
 */
static uint64_t bench_cull_instructions(void)
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

/** Run the measured frames of a world, waiting for the GPU after each, and add the counters.
 *
 * The cull_gpu_ns counter is the GPU time of the cull passes of the views read back over the
 * frames (timestamp queries), cull_passes the passes it covers.
 *
 * @param b The benchmark
 * @param world The world
 * @param items Items of a frame
 */
static void bench_cull_frames(
    bench_t *b,
    ecs_world_t *world,
    int64_t items)
{
    box_render_gpu_stats_t before, after;
    uint32_t word = 0;
    double frames = 0;
    uint64_t start = bench_cull_instructions();
    box_render_gpu_stats(world, &before);
    bench_set_items(b, items);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    box_render_gpu_stats(world, &after);
    bench_counter(b, "instructions", (double)(bench_cull_instructions() - start));
    bench_counter(b, "frames", frames);
    bench_counter(b, "cull_gpu_ns", (double)(after.cull_ns - before.cull_ns));
    bench_counter(b, "cull_passes", (double)(after.cull_passes - before.cull_passes));
}

/** Boxes along x */
#define BENCH_CULL_X (400)

/** Boxes along z */
#define BENCH_CULL_Z (250)

/** Render frames of 100k boxes lit by a sun.
 *
 * @param b The benchmark
 * @param culled Whether the view culls
 * @param fov Vertical field of view of the camera in degrees
 * @param shadows Whether the view renders shadows
 */
static void bench_cull(
    bench_t *b,
    bool culled,
    float fov,
    bool shadows)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 60, 160});
    ecs_set(world, camera, Rotation, {-0.4f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = fov, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {24, 28, 36, 255}}, .lighting.enabled = true,
        .culling = {.enabled = culled, .occlusion = culled, .min_dimension_px = 1},
        .shadows = {.enabled = shadows, .cascades = 4, .resolution = 2048,
            .max_distance = 200, .split_lambda = 0.7f, .depth_bias = 2,
            .normal_offset = 1.5f, .filter_size = 3, .fade = 0.2f}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {204, 0.2f, 129});
    ecs_set(world, floor, Rgb, {90, 96, 104});
    int32_t x, z;
    for (x = 0; x < BENCH_CULL_X; x ++) {
        for (z = 0; z < BENCH_CULL_Z; z ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)x * 0.5f - 99.75f,
                0.3f + (float)(x % 5) * 0.1f, (float)z * 0.5f - 62.25f});
            ecs_set(world, e, Box, {0.35f, 0.6f + (float)(x % 5) * 0.2f, 0.35f});
            ecs_set(world, e, Rgb, {200, 120, 80});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_cull_frames(b, world, BENCH_CULL_X * BENCH_CULL_Z);
    ecs_fini(world);
}

/** Blocks of the city along each axis */
#define BENCH_CITY (50)

/** Render frames of a city whose blocks hide most of the boxes in its streets.
 *
 * @param b The benchmark
 * @param frustum_only Whether the view culls by frustum only, without occlusion
 */
static void bench_city(
    bench_t *b,
    bool frustum_only)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 70, 190});
    ecs_set(world, camera, Rotation, {-0.4f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {150, 180, 220, 255}}, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = !frustum_only, .min_dimension_px = 1}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {0, -0.1f, 0});
    ecs_set(world, ground, Box, {250, 0.2f, 250});
    int32_t x, z, i;
    for (x = 0; x < BENCH_CITY; x ++) {
        for (z = 0; z < BENCH_CITY; z ++) {
            float h = 12 + (float)((x * 7 + z * 3) % 5) * 3;
            ecs_entity_t block = ecs_new(world);
            ecs_set(world, block, Position, {(float)x * 5 - 122.5f, h * 0.5f,
                (float)z * 5 - 122.5f});
            ecs_set(world, block, Box, {3.6f, h, 3.6f});
            for (i = 0; i < 40; i ++) {
                float t = (float)(i % 10) * 0.42f;
                float side = (float)(i / 10 % 2) * 0.4f;
                ecs_entity_t e = ecs_new(world);
                ecs_set(world, e, Position, {
                    (float)x * 5 - 124.6f + (i < 20 ? t : side),
                    0.15f,
                    (float)z * 5 - 124.6f + (i < 20 ? side : 0.8f + t)});
                ecs_set(world, e, Box, {0.3f, 0.3f, 0.3f});
                ecs_set(world, e, Rgb, {250, 120, 60});
            }
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_cull_frames(b, world, BENCH_CITY * BENCH_CITY * 41);
    ecs_fini(world);
}

void Cull_offscreen_100k(
    bench_t *b)
{
    // culling/frustum
    bench_cull(b, true, 15, false);
}

void Cull_offscreen_100k_unculled(
    bench_t *b)
{
    // culling/frustum
    bench_cull(b, false, 15, false);
}

void Cull_shadows_100k(
    bench_t *b)
{
    // culling/frustum
    bench_cull(b, true, 50, true);
}

void Cull_occluded_city_100k(
    bench_t *b)
{
    // culling/hiz
    bench_city(b, false);
}

void Cull_occluded_city_100k_frustum_only(
    bench_t *b)
{
    // culling/hiz
    bench_city(b, true);
}

/** Meshes of the many batch case, one batch each */
#define BENCH_CULL_BATCHES (400)

void Cull_many_batches_shadows(
    bench_t *b)
{
    // render_fixes/deterministic_cull
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 40, 90});
    ecs_set(world, camera, Rotation, {-0.4f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {24, 28, 36, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    int32_t i;
    for (i = 0; i < BENCH_CULL_BATCHES * 50; i ++) {
        int32_t mesh = i % BENCH_CULL_BATCHES;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 200) * 0.6f - 60, 0.5f,
            -(float)(i / 200) * 0.6f});
        ecs_set(world, e, RoundedBox, {0.4f, 0.4f + (float)mesh / 512.0f, 0.4f, 0.05f, 2});
        ecs_set(world, e, Rgb, {200, 120, 80});
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_cull_frames(b, world, BENCH_CULL_BATCHES * 50);
    ecs_fini(world);
}

void Cull_town(
    bench_t *b)
{
    // render_fixes/deterministic_cull
    char root[1024];
    int32_t len = (int32_t)(strstr(__FILE__, "bench/render/src/") - __FILE__);
    ecs_os_snprintf(root, sizeof(root), "%.*s", len, __FILE__);
    if (len > 0) {
        bench_keep((uint64_t)chdir(root));
    }

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_script_run_file(world, "etc/scenes/town.flecs");
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    bench_cull_frames(b, world, 1);
    ecs_fini(world);
}
