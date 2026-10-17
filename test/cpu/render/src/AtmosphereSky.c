/**
 * @file AtmosphereSky.c
 * @brief Tests for LightSettings.atmosphere and what the views of a packet carry for the sky
 * and the aerial perspective of the forward pass.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Create a scene root with the default Atmosphere and a view whose camera is in it.
 *
 * @param world The world
 * @param view Receives the view
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    ecs_entity_t *view)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_add(world, scene, Atmosphere);
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    *view = ecs_new(world);
    ecs_add(world, *view, View);
    ecs_get_mut(world, *view, View)->camera = camera;
    ecs_modified(world, *view, View);

    return scene;
}

/** Run a frame and get the only view of the packet the backend received.
 *
 * @param world The world
 * @param null The backend
 * @return The view
 */
static const box_render_view_t* frame_view(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.views), 1);

    return ecs_vec_first(&null->last.views);
}

void AtmosphereSky_view_atmosphere_defaults(void) {
    // atmosphere/sky
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "view",
        "using box.view\n"
        "view { View: {} }\n"
        "off { View: {lighting: {enabled: true, atmosphere: false}} }\n", NULL) == 0);

    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_bool(view->lighting.atmosphere, true);
    view = ecs_get(world, ecs_lookup(world, "off"), View);
    test_bool(view->lighting.enabled, true);
    test_bool(view->lighting.reflections, true);
    test_bool(view->lighting.atmosphere, false);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, View, {.lighting.enabled = true});
    test_bool(ecs_get(world, literal, View)->lighting.atmosphere, false);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, View);
    test_bool(ecs_get(world, added, View)->lighting.atmosphere, true);

    ecs_fini(world);
}

void AtmosphereSky_packet_view_follows_atmosphere(void) {
    // atmosphere/sky
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t view;
    ecs_entity_t scene = scene_new(world, &view);
    ecs_get_mut(world, scene, Atmosphere)->units_per_km = 500;
    ecs_modified(world, scene, Atmosphere);

    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->atmosphere != 0);
    test_bool(v->atmos.sky, true);
    test_flt(v->atmos.km_per_unit, 1.0f / 500.0f);
    test_flt(v->atmos.aerial_distance, 32.0f);

    ecs_get_mut(world, view, View)->lighting.atmosphere = false;
    ecs_modified(world, view, View);
    v = frame_view(world, null);
    test_assert(v->atmosphere != 0);
    test_bool(v->atmos.sky, false);

    ecs_remove(world, scene, Atmosphere);
    ecs_get_mut(world, view, View)->lighting.atmosphere = true;
    ecs_modified(world, view, View);
    v = frame_view(world, null);
    test_uint(v->atmosphere, 0);
    test_bool(v->atmos.sky, false);

    ecs_fini(world);
}

void AtmosphereSky_packet_view_sun_disk(void) {
    // atmosphere/sun_disk
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t view;
    ecs_entity_t scene = scene_new(world, &view);
    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->atmosphere != 0);
    test_flt(v->atmos.sun_disk[0], cosf(glm_rad(0.266f)));
    test_flt(v->atmos.sun_disk[1], sinf(glm_rad(0.266f)));
    test_flt(v->atmos.sun_disk[2], 1.0f);

    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->sun_radius = 2;
    a->sun_disk_intensity = 0;
    ecs_modified(world, scene, Atmosphere);
    v = frame_view(world, null);
    test_flt(v->atmos.sun_disk[0], cosf(glm_rad(2.0f)));
    test_flt(v->atmos.sun_disk[1], sinf(glm_rad(2.0f)));
    test_flt(v->atmos.sun_disk[2], 0.0f);

    ecs_fini(world);
}
