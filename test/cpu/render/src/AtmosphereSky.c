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

/** Create a white directional light in a scene that shines down from an elevation.
 *
 * @param world The world
 * @param scene The scene
 * @param elevation Elevation of the direction toward the light in degrees
 * @param yaw Yaw of the light in degrees
 * @param intensity Intensity of the light
 * @return The light
 */
static ecs_entity_t light_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float elevation,
    float yaw,
    float intensity)
{
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, light, DirectionalLight, {.intensity = intensity});
    ecs_set(world, light, Rgb, {255, 255, 255});
    ecs_set(world, light, Rotation, {glm_rad(elevation), glm_rad(yaw), 0});

    return light;
}

/** Create a world that renders with the null backend, with a scene that has a sun and a moon
 * at elevations.
 *
 * @param null Receives the backend
 * @param sun_elevation Elevation of the sun in degrees
 * @param moon_elevation Elevation of the moon in degrees, opposite the sun
 * @param view Receives the view
 * @return The world
 */
static ecs_world_t* world_sun_moon(
    box_render_null_t **null,
    float sun_elevation,
    float moon_elevation,
    ecs_entity_t *view)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t scene = scene_new(world, view);
    ecs_entity_t sun = light_new(world, scene, sun_elevation, 0, 1);
    ecs_entity_t moon = light_new(world, scene, moon_elevation, 180, 0.5f);
    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->sun = sun;
    a->moon = moon;
    ecs_modified(world, scene, Atmosphere);

    return world;
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

void AtmosphereSky_fog_atmosphere_defaults(void) {
    // atmosphere/fog
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "fog",
        "using box.effects\n"
        "fog { HeightFog: {} }\n"
        "off { HeightFog: {atmosphere: false} }\n", NULL) == 0);

    const HeightFog *fog = ecs_get(world, ecs_lookup(world, "fog"), HeightFog);
    test_assert(fog != NULL);
    test_bool(fog->atmosphere, true);
    test_flt(fog->density, 0.1f);
    fog = ecs_get(world, ecs_lookup(world, "off"), HeightFog);
    test_bool(fog->atmosphere, false);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, HeightFog);
    test_bool(ecs_get(world, added, HeightFog)->atmosphere, true);

    HeightFog resolved;
    box_render_fog_settings(ecs_get(world, added, HeightFog), &resolved);
    test_bool(resolved.atmosphere, true);

    ecs_fini(world);
}

void AtmosphereSky_packet_view_fog_light_is_sun_by_day(void) {
    // atmosphere/fog
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = world_sun_moon(&null, 30, 20, &view);

    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->atmosphere != 0);
    test_assert(fabsf(v->atmos.fog_light[0]) < 1e-4f);
    test_assert(fabsf(v->atmos.fog_light[1] - sinf(glm_rad(30))) < 1e-4f);
    test_assert(fabsf(v->atmos.fog_light[1] - v->atmos.sun[1]) < 1e-5f);
    test_assert(fabsf(v->atmos.fog_light[2] - v->atmos.sun[2]) < 1e-5f);
    test_flt(v->atmos.fog_light[3], 1.0f);
    test_flt(v->atmos.fog_moon[3], 0.0f);
    test_flt(v->atmos.fog_moon[0], 0.0f);

    ecs_fini(world);
    box_render_null_free(null);
}

void AtmosphereSky_packet_view_fog_light_is_moon_by_night(void) {
    // atmosphere/fog
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = world_sun_moon(&null, -20, 30, &view);

    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->atmosphere != 0);
    test_assert(fabsf(v->atmos.fog_light[1] - sinf(glm_rad(30))) < 1e-4f);
    test_assert(v->atmos.fog_light[2] * v->atmos.sun[2] < 0);
    test_flt(v->atmos.fog_light[3], 1.0f);
    test_flt(v->atmos.fog_moon[3], 1.0f);
    test_assert(v->atmos.fog_moon[0] > 0);
    test_assert(v->atmos.fog_moon[0] < 0.5f);
    test_assert(v->atmos.fog_moon[0] > v->atmos.fog_moon[2]);

    ecs_fini(world);
    box_render_null_free(null);
}

void AtmosphereSky_packet_view_fog_light_without_lights(void) {
    // atmosphere/fog
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t view;
    scene_new(world, &view);
    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->atmosphere != 0);
    test_assert(fabsf(v->atmos.fog_light[0] - v->atmos.sun[0]) < 1e-5f);
    test_assert(fabsf(v->atmos.fog_light[1] - v->atmos.sun[1]) < 1e-5f);
    test_assert(v->atmos.fog_light[1] > 0);
    test_assert(v->atmos.fog_light[3] > 0.99f);
    test_flt(v->atmos.fog_moon[3], 0.0f);

    ecs_fini(world);
    box_render_null_free(null);
}

void AtmosphereSky_packet_view_moon_disk(void) {
    // atmosphere/moon
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = world_sun_moon(&null, -20, 30, &view);

    const box_render_view_t *v = frame_view(world, null);
    ecs_entity_t scene = ecs_get_parent(world, ecs_get(world, view, View)->camera);
    const AtmosphereStatus *status = ecs_get(world, scene, AtmosphereStatus);
    test_assert(v->atmosphere != 0);
    test_assert(fabsf(v->atmos.moon[1] - sinf(glm_rad(30))) < 1e-4f);
    test_assert(v->atmos.moon[2] * v->atmos.sun[2] < 0);
    test_assert(status->moon_illumination > 0.9f);
    test_flt(v->atmos.moon[3], status->moon_illumination);
    test_assert(fabsf(v->atmos.moon_color[0] - 0.5f) < 1e-5f);
    test_assert(fabsf(v->atmos.moon_color[2] - 0.5f) < 1e-5f);
    test_flt(v->atmos.moon_disk[0], cosf(glm_rad(0.259f)));
    test_flt(v->atmos.moon_disk[1], sinf(glm_rad(0.259f)));
    test_flt(v->atmos.moon_disk[2], 0.12f);
    test_flt(v->atmos.moon_disk[3], 0.0f);

    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->moon_radius = 2;
    a->moon_disk_intensity = 3;
    a->moon_albedo = 0.2f;
    a->earthshine = 0.1f;
    ecs_modified(world, scene, Atmosphere);
    v = frame_view(world, null);
    test_flt(v->atmos.moon_disk[0], cosf(glm_rad(2.0f)));
    test_flt(v->atmos.moon_disk[1], sinf(glm_rad(2.0f)));
    test_assert(fabsf(v->atmos.moon_disk[2] - 0.6f) < 1e-6f);
    test_flt(v->atmos.moon_disk[3], 0.1f);

    ecs_fini(world);
    box_render_null_free(null);
}

void AtmosphereSky_packet_view_without_moon(void) {
    // atmosphere/moon
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t view;
    ecs_entity_t scene = scene_new(world, &view);
    light_new(world, scene, 30, 0, 1);
    const box_render_view_t *v = frame_view(world, null);
    test_assert(v->atmosphere != 0);
    test_flt(v->atmos.moon[0], 0.0f);
    test_flt(v->atmos.moon[1], -1.0f);
    test_flt(v->atmos.moon[2], 0.0f);
    test_flt(v->atmos.moon[3], 0.0f);
    test_flt(v->atmos.moon_color[0], 0.0f);
    test_flt(v->atmos.moon_color[2], 0.0f);

    ecs_fini(world);
    box_render_null_free(null);
}
