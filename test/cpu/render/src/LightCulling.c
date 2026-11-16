/**
 * @file LightCulling.c
 * @brief Tests for the culling of point and spot lights no view of their scene sees: a range
 * sphere outside every frustum or too small on screen is not collected.
 *
 * The cases render into the null backend with a camera at the origin looking down -Z.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void LightCulling_behind_camera_culled(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 50});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = 2});

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 1);
    test_int(stats.lights, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void LightCulling_in_view_kept(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, -10});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = 2});

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 0);
    test_int(stats.lights, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void LightCulling_range_reaches_view_kept(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 3});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = 5});

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 0);
    test_int(stats.lights, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void LightCulling_spot_behind_camera_culled(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 50});
    ecs_set(world, light, SpotLight, {.intensity = 1, .range = 2, .outer_angle = 30});

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 1);
    test_int(stats.lights, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void LightCulling_far_small_light_culled(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, SurfaceStatus, {.render_width = 1280, .render_height = 720});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {0, 0, -10000});
    ecs_set(world, far, PointLight, {.intensity = 1, .range = 1});
    ecs_entity_t near = ecs_new(world);
    ecs_set(world, near, Position, {0, 0, -100});
    ecs_set(world, near, PointLight, {.intensity = 1, .range = 1});

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 1);
    test_int(stats.lights, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void LightCulling_far_light_kept_without_surface_size(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, -10000});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = 1});

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 0);
    test_int(stats.lights, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void LightCulling_reflection_probe_keeps_lights(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t probe = ecs_new(world);
    ecs_set(world, probe, Position, {0, 0, 50});
    ecs_set(world, probe, ReflectionProbe, {0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 50});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = 2});

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 0);
    test_int(stats.lights, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void LightCulling_child_of_moving_parent_culled(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t ship = ecs_new(world);
    ecs_set(world, ship, Position, {0, 0, -10});
    ecs_add(world, ship, DynamicTransform);
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, ship);
    ecs_set(world, light, Position, {0, 1, 0});
    ecs_set(world, light, PointLight, {.intensity = 1, .range = 2});

    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 0);
    test_int(stats.lights, 1);

    ecs_set(world, ship, Position, {0, 0, 50});
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_lights_stats(world, &stats);
    test_int(stats.culled, 1);
    test_int(stats.lights, 0);

    ecs_fini(world);
    box_render_null_free(null);
}
