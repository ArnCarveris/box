/**
 * @file TimeOfDayInvalidation.c
 * @brief Tests for the light and color thresholds that keep the light bake and the reflection
 * probes from rebaking on every small change of the sun, with the GPU backend.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Frames a scene gets to converge */
#define CONVERGE_FRAMES (2000)

/** Elevation of the sun in degrees the scenes start with */
#define ELEVATION (50.0f)

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

/** Turn a directional light to shine down from an elevation, yaw 0.
 *
 * @param world The world
 * @param light The light
 * @param elevation Elevation of the direction toward the light in degrees
 */
static void sun_turn(
    ecs_world_t *world,
    ecs_entity_t light,
    float elevation)
{
    ecs_set(world, light, Rotation, {glm_rad(elevation), 0, 0});
}

/** Create a scene with a LightBake and a ReflectionBake with the same thresholds, a static floor
 * and block, a white sun at ELEVATION, a camera with a baked view and a surface.
 *
 * @param world The world
 * @param light Light threshold of both bakes
 * @param color Color threshold of both bakes
 * @param atmosphere Whether the scene root has an Atmosphere that the view and both bakes see
 * @param sun Receives the sun
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    float light,
    float color,
    bool atmosphere,
    ecs_entity_t *sun)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_entity_t source = atmosphere ? scene : 0;
    ecs_add(world, scene, Scene);
    if (atmosphere) {
        ecs_add(world, scene, Atmosphere);
    }

    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 1, .environment = source,
        .light_threshold = light, .color_threshold = color});
    ecs_set(world, scene, ReflectionBake, {.resolution = 16, .budget_ms = 1,
        .scene_probe = true, .scene_probe_height = 2, .environment = source,
        .light_threshold = light, .color_threshold = color});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {8, 0.2f, 4});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, block, Position, {0, 0.5f, 0});
    ecs_set(world, block, Box, {1, 1, 1});
    *sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, *sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_set(world, *sun, Rgb, {255, 255, 255});
    sun_turn(world, *sun, ELEVATION);
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true, .lighting.environment = {.source = source, .intensity = 1}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    return scene;
}

/** Render a frame and wait until the GPU rendered it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Check whether the light bake and the reflection probes of a scene converged.
 *
 * @param world The world
 * @param scene The scene
 * @return Whether both statuses are converged
 */
static bool converged(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    return ecs_get(world, scene, LightBakeStatus)->converged &&
        ecs_get(world, scene, ReflectionBakeStatus)->converged;
}

/** Run frames until the light bake and the reflection probes of a scene converged.
 *
 * @param world The world
 * @param scene The scene
 * @return Whether they converged within CONVERGE_FRAMES frames
 */
static bool converge(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    int32_t i;
    for (i = 0; i < CONVERGE_FRAMES && !converged(world, scene); i ++) {
        frame(world);
    }

    return converged(world, scene);
}

/** Check that the backend reported no error.
 *
 * @param world The world
 */
static void no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

void TimeOfDayInvalidation_small_rotation_keeps_bakes_converged(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = world_new();
    ecs_entity_t sun;
    ecs_entity_t scene = scene_new(world, 2, 0.05f, false, &sun);
    test_bool(converge(world, scene), true);
    int32_t baked = ecs_get(world, scene, LightBakeStatus)->baked;
    int32_t captured = ecs_get(world, scene, ReflectionBakeStatus)->captured;
    test_assert(baked > 0);
    test_assert(captured > 0);

    sun_turn(world, sun, ELEVATION + 1.5f);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        frame(world);
        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
        test_int(ecs_get(world, scene, LightBakeStatus)->baked, baked);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
        test_int(ecs_get(world, scene, ReflectionBakeStatus)->captured, captured);
    }

    no_errors(world);
    ecs_fini(world);
}

void TimeOfDayInvalidation_large_rotation_requeues_bakes(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = world_new();
    ecs_entity_t sun;
    ecs_entity_t scene = scene_new(world, 2, 0.05f, false, &sun);
    test_bool(converge(world, scene), true);
    int32_t baked = ecs_get(world, scene, LightBakeStatus)->baked;

    sun_turn(world, sun, ELEVATION + 3);
    frame(world);
    const LightBakeStatus *light = ecs_get(world, scene, LightBakeStatus);
    test_bool(light->converged, false);
    test_int(light->baked, 0);
    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, false);

    test_bool(converge(world, scene), true);
    test_int(ecs_get(world, scene, LightBakeStatus)->baked, baked);
    no_errors(world);
    ecs_fini(world);
}

void TimeOfDayInvalidation_small_rotations_add_up_requeue_once(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = world_new();
    ecs_entity_t sun;
    ecs_entity_t scene = scene_new(world, 2, 0.05f, false, &sun);
    test_bool(converge(world, scene), true);

    int32_t step, requeues = 0, at = -1;
    for (step = 1; step <= 5; step ++) {
        sun_turn(world, sun, ELEVATION + 0.8f * (float)step);
        frame(world);
        bool light = ecs_get(world, scene, LightBakeStatus)->converged;
        bool reflect = ecs_get(world, scene, ReflectionBakeStatus)->converged;
        test_bool(light, reflect);
        if (!light) {
            requeues ++;
            at = step;
        }

        test_bool(converge(world, scene), true);
    }

    test_int(requeues, 1);
    test_int(at, 3);
    no_errors(world);
    ecs_fini(world);
}

void TimeOfDayInvalidation_zero_threshold_requeues_every_change(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = world_new();
    ecs_entity_t sun;
    ecs_entity_t scene = scene_new(world, 0, 0, false, &sun);
    test_bool(converge(world, scene), true);

    int32_t step;
    for (step = 1; step <= 2; step ++) {
        sun_turn(world, sun, ELEVATION + 0.1f * (float)step);
        frame(world);
        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, false);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, false);
        test_bool(converge(world, scene), true);
    }

    ecs_set(world, sun, DirectionalLight, {.intensity = 1.01f * GLM_PIf});
    frame(world);
    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, false);
    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, false);
    test_bool(converge(world, scene), true);

    no_errors(world);
    ecs_fini(world);
}

void TimeOfDayInvalidation_held_sun_ignores_environment_versions(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = world_new();
    ecs_entity_t sun;
    ecs_entity_t scene = scene_new(world, 2, 0.05f, true, &sun);
    int32_t i, version = 0;
    for (i = 0; i < CONVERGE_FRAMES; i ++) {
        frame(world);
        int32_t now = ecs_get(world, scene, EnvironmentStatus)->version;
        if (now && now == version && converged(world, scene)) {
            break;
        }

        version = now;
    }

    test_bool(converged(world, scene), true);
    test_assert(version > 0);

    sun_turn(world, sun, ELEVATION + 1.5f);
    bool rendered = false;
    for (i = 0; i < 60; i ++) {
        frame(world);
        rendered |= ecs_get(world, scene, EnvironmentStatus)->version > version;
        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
        test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
    }

    test_assert(rendered);
    no_errors(world);
    ecs_fini(world);
}
