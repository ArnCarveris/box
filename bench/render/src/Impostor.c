/**
 * @file Impostor.c
 * @brief Benchmarks for the impostor cards (ImpostorDraw): 10k instances of a tree prefab drawn
 * as cards beyond Impostor.distance, against the same instances drawn as their hierarchy.
 *
 * Headless worlds render a 100 by 100 grid of trees, a trunk and two crowns each, lit by a sun
 * with shadows, into a 1280x720 offscreen surface on the main thread and wait for the GPU after
 * every frame, so a case measures a whole frame. The camera sees every tree from beyond 60.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#include "bench_memory.h"

/** Trees along each axis */
#define BENCH_IMPOSTOR_GRID (100)

/** Meters between two trees */
#define BENCH_IMPOSTOR_SPACING (4.0f)

/** Frames a case runs at most for the bake */
#define BENCH_IMPOSTOR_BAKE_FRAMES (600)

/** Create the tree prefab: a trunk and two crowns.
 *
 * @param world The world
 * @return The prefab
 */
static ecs_entity_t bench_impostor_tree(
    ecs_world_t *world)
{
    ecs_entity_t result = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t trunk = ecs_new_w_pair(world, EcsChildOf, result);
    ecs_add_id(world, trunk, EcsPrefab);
    ecs_set(world, trunk, Position, {0, 1.5f, 0});
    ecs_set(world, trunk, Cylinder, {.radius = 0.25f, .length = 3, .segments = 12,
        .smooth = true});
    ecs_set(world, trunk, Rgb, {110, 80, 50});
    ecs_entity_t low = ecs_new_w_pair(world, EcsChildOf, result);
    ecs_add_id(world, low, EcsPrefab);
    ecs_set(world, low, Position, {0, 3.5f, 0});
    ecs_set(world, low, Cone, {.radius = 1.6f, .length = 3, .segments = 16, .smooth = true});
    ecs_set(world, low, Rgb, {50, 120, 60});
    ecs_entity_t high = ecs_new_w_pair(world, EcsChildOf, result);
    ecs_add_id(world, high, EcsPrefab);
    ecs_set(world, high, Position, {0, 5.5f, 0});
    ecs_set(world, high, Cone, {.radius = 1.1f, .length = 2.5f, .segments = 16, .smooth = true});
    ecs_set(world, high, Rgb, {60, 140, 70});

    return result;
}

/** Render frames of 10k trees, as impostor cards or as their hierarchy.
 *
 * The counters are the frames, the GPU time of the cull passes, the impostor instances and
 * batches of the extract and the memory of the instances.
 *
 * @param b The benchmark
 * @param impostor Whether the prefab has an Impostor whose cards draw every tree
 */
static void bench_impostor(
    bench_t *b,
    bool impostor)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true});
    ecs_singleton_set(world, ImpostorBudget, {.frames = 64});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 90, 260});
    ecs_set(world, camera, Rotation, {-0.45f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {150, 180, 220, 255}}, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .shadows = {.enabled = true, .cascades = 4, .resolution = 2048,
            .max_distance = 400, .split_lambda = 0.7f, .depth_bias = 2,
            .normal_offset = 2, .filter_size = 3, .fade = 0.2f, .blend = 0.1f}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {0, -0.1f, 0});
    ecs_set(world, ground, Box, {420, 0.2f, 420});
    ecs_set(world, ground, Rgb, {90, 110, 70});

    ecs_entity_t tree = bench_impostor_tree(world);
    if (impostor) {
        ecs_set(world, tree, Impostor, {.frames = 8, .resolution = 128, .hemisphere = true,
            .distance = 60, .fade = 5});
    }

    int32_t x, z, i;
    float half = (float)(BENCH_IMPOSTOR_GRID - 1) * BENCH_IMPOSTOR_SPACING * 0.5f;
    for (x = 0; x < BENCH_IMPOSTOR_GRID; x ++) {
        for (z = 0; z < BENCH_IMPOSTOR_GRID; z ++) {
            ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, tree);
            ecs_set(world, e, Position, {(float)x * BENCH_IMPOSTOR_SPACING - half, 0,
                (float)z * BENCH_IMPOSTOR_SPACING - half});
            ecs_set(world, e, Rotation, {0, (float)((x * 7 + z * 13) % 31) * 0.2f, 0});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    for (i = 0; impostor && i < BENCH_IMPOSTOR_BAKE_FRAMES; i ++) {
        const ImpostorStatus *s = ecs_get(world, tree, ImpostorStatus);
        if (s && s->ready) {
            break;
        }

        ecs_progress(world, 0);
        box_render_sync(world);
    }

    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t before, after;
    uint32_t word = 0;
    double frames = 0;
    box_render_gpu_stats(world, &before);
    bench_set_items(b, BENCH_IMPOSTOR_GRID * BENCH_IMPOSTOR_GRID);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    box_render_gpu_stats(world, &after);
    box_render_draws_stats_t stats = {0};
    box_render_draws_stats(world, &stats);
    bench_counter(b, "frames", frames);
    bench_counter(b, "cull_gpu_ns", (double)(after.cull_ns - before.cull_ns));
    bench_counter(b, "impostors", (double)stats.impostors);
    bench_counter(b, "impostor_batches", (double)stats.impostor_batches);
    bench_counter(b, "batches", (double)stats.batches);
    bench_render_memory(b, &stats);
    ecs_fini(world);
}

void Impostor_cards_10k(
    bench_t *b)
{
    bench_impostor(b, true);
}

void Impostor_full_10k(
    bench_t *b)
{
    bench_impostor(b, false);
}
