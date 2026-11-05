/**
 * @file ReflectionInvalidation.c
 * @brief Tests for the captures again of reflection probes after a change, with the GPU backend.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Create a headless world that renders with the GPU.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Create a scene with a ReflectionBake without scene probe, a static floor, a camera with a
 * view, a surface and two small probes at x = -1.5 and x = 1.5.
 *
 * @param world The world
 * @param a Receives the probe at x = -1.5
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    ecs_entity_t *a)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, ReflectionBake, {.resolution = 16, .budget_ms = 1});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {4, 0.2f, 4});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 48, .resolution_scale = 1});
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, probe, Position, {i ? 1.5f : -1.5f, 1, 0});
        ecs_set(world, probe, ReflectionProbe, {
            .extent = {0.4f, 0.4f, 0.4f}, .blend = 0.1f, .projection = {0.4f, 0.4f, 0.4f}});
        if (!i) {
            *a = probe;
        }
    }

    return scene;
}

/** Run frames until the reflection probes of a scene converged, and its light bake when it
 * has one.
 *
 * @param world The world
 * @param scene The scene
 * @return Whether they converged within 60 frames
 */
static bool converge(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    int32_t frame;
    bool result = false;
    for (frame = 0; !result && frame < 60; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        const LightBakeStatus *light = ecs_get(world, scene, LightBakeStatus);
        result = ecs_get(world, scene, ReflectionBakeStatus)->converged &&
            (!light || light->converged);
    }

    return result;
}

/** Add a small static box just below the probe at x = -1.5, which reaches its boxes only.
 *
 * @param world The world
 * @param scene The scene
 */
static void shape_near_a(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, shape, Position, {-1.5f, 0.5f, 0});
    ecs_set(world, shape, Box, {0.2f, 0.2f, 0.2f});
}

void ReflectionInvalidation_static_change_recaptures_reached_probe(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = world_new();
    ecs_entity_t a;
    ecs_entity_t scene = scene_new(world, &a);
    test_bool(converge(world, scene), true);
    test_int(ecs_get(world, scene, ReflectionBakeStatus)->captured, 2);

    shape_near_a(world, scene);
    ecs_progress(world, 0);
    box_render_sync(world);
    const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
    test_bool(status->converged, false);
    test_int(status->captured, 1);
    test_int(status->resident, 2);

    test_bool(converge(world, scene), true);
    test_int(ecs_get(world, scene, ReflectionBakeStatus)->captured, 2);

    int32_t frame;
    for (frame = 0; frame < 5; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
    }

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void ReflectionInvalidation_light_bake_pass_recaptures_every_probe(void) {
    // reflection_probes/invalidation
    ecs_world_t *world = world_new();
    ecs_entity_t a;
    ecs_entity_t scene = scene_new(world, &a);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 1, .compute = true});
    test_bool(converge(world, scene), true);
    test_int(ecs_get(world, scene, ReflectionBakeStatus)->captured, 2);

    shape_near_a(world, scene);
    ecs_progress(world, 0);
    box_render_sync(world);
    const ReflectionBakeStatus *status = ecs_get(world, scene, ReflectionBakeStatus);
    test_bool(status->converged, false);
    test_int(status->captured, 0);

    test_bool(converge(world, scene), true);
    test_int(ecs_get(world, scene, ReflectionBakeStatus)->captured, 2);

    int32_t frame;
    for (frame = 0; frame < 5; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    }

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    ecs_fini(world);
}
