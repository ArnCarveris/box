/**
 * @file Atmosphere.c
 * @brief Tests for Atmosphere, its status, the CPU transmittance and the attenuated lights.
 *
 * The transmittance is observed through AtmosphereStatus, the attenuated lights through the
 * views of the packets the null backend receives.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "bake.h"

/** Check that two values are within a tolerance of each other.
 *
 * @param a The first value
 * @param b The second value
 * @param eps The tolerance
 */
static void test_near(
    float a,
    float b,
    float eps)
{
    if (fabsf(a - b) > eps) {
        printf("%f != %f\n", (double)a, (double)b);
    }

    test_assert(fabsf(a - b) <= eps);
}

/** Create a world with Box and the null backend.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Create a scene root with the default Atmosphere.
 *
 * @param world The world
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_add(world, scene, Atmosphere);

    return scene;
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

/** Create a view whose camera is in a scene.
 *
 * @param world The world
 * @param scene The scene of the camera
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});

    return view;
}

/** Run a frame and get the view of a scene in the packet the backend received.
 *
 * @param world The world
 * @param null The backend
 * @param scene The scene of the view
 * @return The view
 */
static const box_render_view_t* frame_view(
    ecs_world_t *world,
    box_render_null_t *null,
    ecs_entity_t scene)
{
    const box_render_view_t *result = NULL;
    int32_t i;
    ecs_progress(world, 0);
    box_render_sync(world);
    for (i = 0; i < ecs_vec_count(&null->last.views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, i);
        if (v->scene == scene) {
            result = v;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Run a frame and get the status of an atmosphere.
 *
 * @param world The world
 * @param e The entity with Atmosphere
 * @return The status
 */
static const AtmosphereStatus* frame_status(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_progress(world, 0);
    const AtmosphereStatus *result = ecs_get(world, e, AtmosphereStatus);
    test_assert(result != NULL);

    return result;
}

/** Get the sun transmittance of a scene whose Atmosphere is set, with a sun at an elevation.
 *
 * @param a The atmosphere
 * @param elevation Elevation of the sun in degrees
 * @param out Receives the transmittance
 */
static void sun_transmittance(
    const Atmosphere *a,
    float elevation,
    float *out)
{
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_set_ptr(world, scene, Atmosphere, a);
    light_new(world, scene, elevation, 0, 1);
    const AtmosphereStatus *status = frame_status(world, scene);
    test_assert(status->error == NULL);
    glm_vec3_copy(ECS_CONST_CAST(float*, status->sun_transmittance), out);
    ecs_fini(world);
    box_render_null_free(null);
}

/** Get the default Atmosphere values.
 *
 * @param out Receives the values
 */
static void atmosphere_defaults(
    Atmosphere *out)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Atmosphere);
    *out = *ecs_get(world, e, Atmosphere);
    ecs_fini(world);
}

/** Find a light of the view list of a scene by its entity.
 *
 * @param world The world
 * @param scene The scene
 * @param e The light entity
 * @return The light
 */
static const box_render_light_t* view_light(
    ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t e)
{
    box_bake_light_set_t set;
    const box_render_light_t *result = NULL;
    int32_t i;
    box_bake_view_lights(box_render_lights_state(world), scene, &set);
    for (i = 0; i < set.count; i ++) {
        if (set.entities[i] == e) {
            result = &set.lights[i];
        }
    }

    test_assert(result != NULL);

    return result;
}

void Atmosphere_status_follows_atmosphere(void) {
    // atmosphere/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    test_assert(!ecs_has(world, scene, AtmosphereStatus));
    ecs_add(world, scene, Atmosphere);
    test_assert(ecs_has(world, scene, AtmosphereStatus));
    test_assert(ecs_get(world, scene, AtmosphereStatus)->error == NULL);
    test_int(ecs_get(world, scene, AtmosphereStatus)->lut_version, 0);

    ecs_remove(world, scene, Atmosphere);
    test_assert(!ecs_has(world, scene, AtmosphereStatus));

    ecs_fini(world);
}

void Atmosphere_singleton_has_status(void) {
    // atmosphere/settings
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_singleton_add(world, Atmosphere);
    test_assert(ecs_has(world, ecs_id(Atmosphere), AtmosphereStatus));
    test_flt(ecs_singleton_get(world, Atmosphere)->planet_radius, 6360);

    const AtmosphereStatus *status = frame_status(world, ecs_id(Atmosphere));
    test_assert(status->error == NULL);
    test_assert(status->sun_transmittance[0] > 0.5f);
    test_assert(status->sun_transmittance[2] > 0.5f);

    ecs_singleton_remove(world, Atmosphere);
    test_assert(!ecs_has(world, ecs_id(Atmosphere), AtmosphereStatus));

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_script_defaults(void) {
    // atmosphere/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.view\n"
        "using box.atmosphere\n"
        "scene {\n Scene: {}\n Atmosphere: {}\n}\n"
        "custom {\n Scene: {}\n Atmosphere: {planet_radius: 3000, rayleigh: {scale_height: 4}, "
        "mie: {tint: {255, 0, 0}}, ozone_scale: 0, sun_radius: 1}\n}\n", NULL) == 0);

    ecs_entity_t scene = ecs_lookup(world, "scene");
    const Atmosphere *a = ecs_get(world, scene, Atmosphere);
    test_assert(a != NULL);
    test_assert(ecs_has(world, scene, AtmosphereStatus));
    test_uint(a->sun, 0);
    test_uint(a->moon, 0);
    test_flt(a->planet_radius, 6360);
    test_flt(a->thickness, 100);
    test_flt(a->ground_altitude, 0.5f);
    test_flt(a->sea_level, 0);
    test_flt(a->units_per_km, 1000);
    test_flt(a->rayleigh.scale_height, 8);
    test_flt(a->rayleigh.scattering_scale, 1);
    test_flt(a->mie.scale_height, 1.2f);
    test_flt(a->mie.anisotropy, 0.8f);
    test_flt(a->mie.scattering_scale, 1);
    test_int(a->mie.tint.r, 255);
    test_int(a->mie.tint.g, 255);
    test_int(a->mie.tint.b, 255);
    test_flt(a->ozone_scale, 1);
    test_int(a->ground_albedo.r, 77);
    test_int(a->ground_albedo.g, 77);
    test_int(a->ground_albedo.b, 77);
    test_int(a->night_tint.r, 70);
    test_int(a->night_tint.g, 110);
    test_int(a->night_tint.b, 230);
    test_flt(a->night_intensity, 0.003f);
    test_flt(a->sun_disk_intensity, 1);
    test_flt(a->sun_radius, 0.266f);
    test_flt(a->moon_disk_intensity, 1);
    test_flt(a->moon_radius, 0.259f);
    test_flt(a->moon_albedo, 0.12f);
    test_flt(a->earthshine, 0);
    test_flt(a->aerial_distance, 32);
    test_flt(a->aerial_intensity, 1);
    test_flt(a->ibl_update_angle, 0.5f);

    a = ecs_get(world, ecs_lookup(world, "custom"), Atmosphere);
    test_assert(a != NULL);
    test_flt(a->planet_radius, 3000);
    test_flt(a->thickness, 100);
    test_flt(a->rayleigh.scale_height, 4);
    test_flt(a->rayleigh.scattering_scale, 1);
    test_flt(a->mie.scale_height, 1.2f);
    test_int(a->mie.tint.r, 255);
    test_int(a->mie.tint.g, 0);
    test_int(a->mie.tint.b, 0);
    test_flt(a->ozone_scale, 0);
    test_flt(a->sun_radius, 1);
    test_flt(a->moon_radius, 0.259f);

    ecs_fini(world);
}

void Atmosphere_add_defaults(void) {
    // atmosphere/settings
    Atmosphere a;
    atmosphere_defaults(&a);
    test_uint(a.sun, 0);
    test_uint(a.moon, 0);
    test_flt(a.planet_radius, 6360);
    test_flt(a.thickness, 100);
    test_flt(a.ground_altitude, 0.5f);
    test_flt(a.sea_level, 0);
    test_flt(a.units_per_km, 1000);
    test_flt(a.rayleigh.scale_height, 8);
    test_flt(a.rayleigh.scattering_scale, 1);
    test_flt(a.mie.scale_height, 1.2f);
    test_flt(a.mie.anisotropy, 0.8f);
    test_flt(a.mie.scattering_scale, 1);
    test_int(a.mie.tint.r, 255);
    test_int(a.mie.tint.g, 255);
    test_int(a.mie.tint.b, 255);
    test_flt(a.ozone_scale, 1);
    test_int(a.ground_albedo.r, 77);
    test_int(a.night_tint.r, 70);
    test_int(a.night_tint.g, 110);
    test_int(a.night_tint.b, 230);
    test_flt(a.night_intensity, 0.003f);
    test_flt(a.sun_disk_intensity, 1);
    test_flt(a.sun_radius, 0.266f);
    test_flt(a.moon_disk_intensity, 1);
    test_flt(a.moon_radius, 0.259f);
    test_flt(a.moon_albedo, 0.12f);
    test_flt(a.earthshine, 0);
    test_flt(a.aerial_distance, 32);
    test_flt(a.aerial_intensity, 1);
    test_flt(a.ibl_update_angle, 0.5f);
}

void Atmosphere_zero_is_literal(void) {
    // atmosphere/settings
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, Atmosphere, {0});
    const Atmosphere *a = ecs_get(world, scene, Atmosphere);
    test_flt(a->planet_radius, 0);
    test_flt(a->thickness, 0);
    test_flt(a->ground_altitude, 0);
    test_flt(a->units_per_km, 0);
    test_flt(a->rayleigh.scale_height, 0);
    test_flt(a->rayleigh.scattering_scale, 0);
    test_flt(a->mie.anisotropy, 0);
    test_int(a->mie.tint.r, 0);
    test_flt(a->ozone_scale, 0);
    test_flt(a->sun_radius, 0);
    test_flt(a->ibl_update_angle, 0);
    test_assert(frame_status(world, scene)->error != NULL);

    Atmosphere zeros;
    atmosphere_defaults(&zeros);
    zeros.ground_altitude = 0;
    zeros.rayleigh.scattering_scale = 0;
    zeros.mie.scattering_scale = 0;
    zeros.mie.anisotropy = 0;
    zeros.ozone_scale = 0;
    zeros.sun_radius = 0;
    zeros.aerial_distance = 0;
    ecs_set_ptr(world, scene, Atmosphere, &zeros);
    a = ecs_get(world, scene, Atmosphere);
    test_flt(a->rayleigh.scattering_scale, 0);
    test_flt(a->ozone_scale, 0);
    const AtmosphereStatus *status = frame_status(world, scene);
    test_assert(status->error == NULL);
    test_flt(status->sun_transmittance[0], 1);
    test_flt(status->sun_transmittance[1], 1);
    test_flt(status->sun_transmittance[2], 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_invalid_values_set_error(void) {
    // atmosphere/settings
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t valid = scene_new(world);
    ecs_entity_t radius = scene_new(world);
    ecs_get_mut(world, radius, Atmosphere)->planet_radius = -1;
    ecs_modified(world, radius, Atmosphere);
    ecs_entity_t sun_radius = scene_new(world);
    ecs_get_mut(world, sun_radius, Atmosphere)->sun_radius = -1;
    ecs_modified(world, sun_radius, Atmosphere);
    ecs_entity_t thickness = scene_new(world);
    ecs_get_mut(world, thickness, Atmosphere)->thickness = 0;
    ecs_modified(world, thickness, Atmosphere);
    ecs_entity_t units = scene_new(world);
    ecs_get_mut(world, units, Atmosphere)->units_per_km = 0;
    ecs_modified(world, units, Atmosphere);
    light_new(world, thickness, 30, 0, 1);

    const AtmosphereStatus *status = frame_status(world, valid);
    test_assert(status->error == NULL);
    status = ecs_get(world, radius, AtmosphereStatus);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "planet_radius") != NULL);
    status = ecs_get(world, sun_radius, AtmosphereStatus);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "sun_radius") != NULL);
    status = ecs_get(world, units, AtmosphereStatus);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "units_per_km") != NULL);
    status = ecs_get(world, thickness, AtmosphereStatus);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "thickness") != NULL);
    test_flt(status->sun_transmittance[0], 0);
    test_flt(status->sun_transmittance[1], 0);
    test_flt(status->sun_transmittance[2], 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_error_clears_when_valid(void) {
    // atmosphere/settings
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t scene = scene_new(world);
    light_new(world, scene, 30, 0, 1);
    ecs_get_mut(world, scene, Atmosphere)->thickness = 0;
    ecs_modified(world, scene, Atmosphere);
    test_assert(frame_status(world, scene)->error != NULL);

    ecs_get_mut(world, scene, Atmosphere)->thickness = 100;
    ecs_modified(world, scene, Atmosphere);
    const AtmosphereStatus *status = frame_status(world, scene);
    test_assert(status->error == NULL);
    test_assert(status->sun_transmittance[0] > 0.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_zenith_transmittance_near_white(void) {
    // atmosphere/transmittance
    Atmosphere a;
    float t[3];
    atmosphere_defaults(&a);
    sun_transmittance(&a, 90, t);
    test_assert(t[0] > 0.9f && t[0] <= 1.0f);
    test_assert(t[1] > 0.8f && t[1] <= 1.0f);
    test_assert(t[2] > 0.7f && t[2] <= 1.0f);
    test_assert(t[0] > t[2]);
}

void Atmosphere_low_sun_transmittance_reddish(void) {
    // atmosphere/transmittance
    Atmosphere a;
    float t[3], zenith[3];
    atmosphere_defaults(&a);
    sun_transmittance(&a, 2, t);
    sun_transmittance(&a, 90, zenith);
    test_assert(t[0] > t[1]);
    test_assert(t[1] > t[2]);
    test_assert(t[2] > 0);
    test_assert(t[0] < zenith[0]);
    test_assert(t[2] < 0.5f * zenith[2]);
}

void Atmosphere_no_rayleigh_transmittance(void) {
    // atmosphere/transmittance
    Atmosphere a;
    float with[3], without[3];
    atmosphere_defaults(&a);
    sun_transmittance(&a, 10, with);
    a.rayleigh.scattering_scale = 0;
    sun_transmittance(&a, 10, without);
    test_assert(without[0] > with[0]);
    test_assert(without[1] > with[1]);
    test_assert(without[2] > with[2]);
    test_assert(without[2] - with[2] > without[1] - with[1]);
    test_assert(without[1] - with[1] > without[0] - with[0]);
    test_assert(without[2] > without[1]);
}

void Atmosphere_no_ozone_transmittance(void) {
    // atmosphere/transmittance
    Atmosphere a;
    float with[3], without[3];
    atmosphere_defaults(&a);
    sun_transmittance(&a, 10, with);
    a.ozone_scale = 0;
    sun_transmittance(&a, 10, without);
    test_assert(without[0] > with[0]);
    test_assert(without[1] > with[1]);
    test_assert(without[2] >= with[2]);
    test_assert(without[1] - with[1] > without[0] - with[0]);
    test_assert(without[0] - with[0] > without[2] - with[2]);
}

void Atmosphere_sun_light_attenuated(void) {
    // atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, 30, 0, 2);
    view_new(world, scene);
    const box_render_view_t *v = frame_view(world, null, scene);
    const AtmosphereStatus *status = ecs_get(world, scene, AtmosphereStatus);
    const float *t = status->sun_transmittance;
    test_assert(t[0] > t[1] && t[1] > t[2] && t[2] > 0.5f && t[0] < 1.0f);
    test_near(v->lights.sun[1], 0.5f, 1e-4f);
    test_near(v->lights.sun_color[0], 2 * GLM_PIf * t[0], 1e-4f);
    test_near(v->lights.sun_color[1], 2 * GLM_PIf * t[1], 1e-4f);
    test_near(v->lights.sun_color[2], 2 * GLM_PIf * t[2], 1e-4f);

    test_flt(ecs_get(world, sun, DirectionalLight)->intensity, 2);
    test_int(ecs_get(world, sun, Rgb)->r, 255);
    test_int(ecs_get(world, sun, Rgb)->b, 255);

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_sun_below_horizon_is_zero(void) {
    // atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, -10, 0, 1);
    view_new(world, scene);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->lights.directional, 1);
    test_flt(v->lights.sun_color[0], 0);
    test_flt(v->lights.sun_color[1], 0);
    test_flt(v->lights.sun_color[2], 0);
    const AtmosphereStatus *status = ecs_get(world, scene, AtmosphereStatus);
    test_flt(status->sun_transmittance[0], 0);
    test_flt(status->sun_transmittance[2], 0);

    ecs_set(world, sun, Rotation, {glm_rad(-0.5f), 0, 0});
    v = frame_view(world, null, scene);
    test_flt(v->lights.sun_color[0], 0);
    test_flt(v->lights.sun_color[1], 0);
    test_flt(v->lights.sun_color[2], 0);
    test_assert(ecs_get(world, scene, AtmosphereStatus)->sun_transmittance[0] > 0);

    ecs_set(world, sun, Rotation, {0, 0, 0});
    v = frame_view(world, null, scene);
    status = ecs_get(world, scene, AtmosphereStatus);
    test_near(v->lights.sun_color[0], GLM_PIf * 0.5f * status->sun_transmittance[0], 1e-4f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_explicit_sun_and_moon_attenuated(void) {
    // atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t scene = scene_new(world);
    ecs_entity_t other = light_new(world, scene, 60, 0, 1);
    ecs_entity_t sun = light_new(world, scene, 30, 0, 1);
    ecs_entity_t moon = light_new(world, scene, 30, 180, 0.5f);
    ecs_get_mut(world, scene, Atmosphere)->sun = sun;
    ecs_get_mut(world, scene, Atmosphere)->moon = moon;
    ecs_modified(world, scene, Atmosphere);

    const AtmosphereStatus *status = frame_status(world, scene);
    test_assert(status->error == NULL);
    test_near(status->moon_illumination, 0.75f, 1e-4f);
    const float *ts = status->sun_transmittance, *tm = status->moon_transmittance;
    test_assert(ts[2] > 0 && ts[2] < 1);
    test_near(tm[0], ts[0], 1e-5f);
    test_near(tm[2], ts[2], 1e-5f);

    const box_render_light_t *l = view_light(world, scene, other);
    test_near(l->color[0], GLM_PIf, 1e-5f);
    test_near(l->color[2], GLM_PIf, 1e-5f);
    l = view_light(world, scene, sun);
    test_near(l->color[0], GLM_PIf * ts[0], 1e-4f);
    test_near(l->color[2], GLM_PIf * ts[2], 1e-4f);
    l = view_light(world, scene, moon);
    test_near(l->color[0], 0.5f * GLM_PIf * tm[0], 1e-4f);
    test_near(l->color[2], 0.5f * GLM_PIf * tm[2], 1e-4f);

    ecs_get_mut(world, scene, Atmosphere)->moon = 0;
    ecs_modified(world, scene, Atmosphere);
    status = frame_status(world, scene);
    test_flt(status->moon_illumination, 0);
    test_flt(status->moon_transmittance[0], 0);
    l = view_light(world, scene, moon);
    test_near(l->color[0], 0.5f * GLM_PIf, 1e-5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_default_sun_attenuated(void) {
    // atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t scene = scene_new(world);
    view_new(world, scene);
    const box_render_view_t *v = frame_view(world, null, scene);
    const float *t = ecs_get(world, scene, AtmosphereStatus)->sun_transmittance;
    test_assert(t[0] > t[2] && t[2] > 0.5f && t[0] < 1.0f);
    test_assert(v->lights.buffer != BOX_RENDER_BUFFER_LIGHT_FIRST);
    test_int(v->lights.directional, 1);
    test_int(v->lights.count, 1);
    test_near(v->lights.sun_color[0], GLM_PIf * 1.0f * t[0], 1e-4f);
    test_near(v->lights.sun_color[1], GLM_PIf * 0.96f * t[1], 1e-4f);
    test_near(v->lights.sun_color[2], GLM_PIf * 0.9f * t[2], 1e-4f);

    ecs_remove(world, scene, Atmosphere);
    v = frame_view(world, null, scene);
    test_assert(v->lights.buffer == BOX_RENDER_BUFFER_LIGHT_FIRST);
    test_near(v->lights.sun_color[0], GLM_PIf, 1e-5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_invalid_atmosphere_leaves_lights(void) {
    // atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t scene = scene_new(world);
    ecs_get_mut(world, scene, Atmosphere)->units_per_km = 0;
    ecs_modified(world, scene, Atmosphere);
    light_new(world, scene, 30, 0, 2);
    view_new(world, scene);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_assert(ecs_get(world, scene, AtmosphereStatus)->error != NULL);
    test_near(v->lights.sun_color[0], 2 * GLM_PIf, 1e-5f);
    test_near(v->lights.sun_color[2], 2 * GLM_PIf, 1e-5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Atmosphere_scene_without_atmosphere_unchanged(void) {
    // atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t sky = scene_new(world);
    ecs_entity_t plain = ecs_new(world);
    ecs_add(world, plain, Scene);
    light_new(world, sky, 30, 0, 1);
    light_new(world, plain, 30, 0, 1);
    view_new(world, sky);
    view_new(world, plain);
    const box_render_view_t *v = frame_view(world, null, plain);
    test_near(v->lights.sun_color[0], GLM_PIf, 1e-5f);
    test_near(v->lights.sun_color[1], GLM_PIf, 1e-5f);
    test_near(v->lights.sun_color[2], GLM_PIf, 1e-5f);
    v = frame_view(world, null, sky);
    test_assert(v->lights.sun_color[2] < 0.9f * GLM_PIf);

    ecs_fini(world);
    box_render_null_free(null);
}
