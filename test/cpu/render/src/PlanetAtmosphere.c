/**
 * @file PlanetAtmosphere.c
 * @brief Tests for PlanetAtmosphere: its defaults, its status, its LUTs and the shells the views
 * of the packets the null backend receives draw.
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

/** Create a scene root.
 *
 * @param world The world
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);

    return scene;
}

/** Create a planet with the default PlanetAtmosphere in a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param x World X of the center
 * @return The planet
 */
static ecs_entity_t planet_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x)
{
    ecs_entity_t planet = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, planet, Position, {x, 0, 0});
    ecs_add(world, planet, PlanetAtmosphere);

    return planet;
}

/** Create a white directional light in a scene that shines down from an elevation.
 *
 * @param world The world
 * @param scene The scene
 * @param elevation Elevation of the direction toward the light in degrees
 * @param intensity Intensity of the light
 * @return The light
 */
static ecs_entity_t light_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float elevation,
    float intensity)
{
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, light, DirectionalLight, {.intensity = intensity});
    ecs_set(world, light, Rgb, {255, 255, 255});
    ecs_set(world, light, Rotation, {glm_rad(elevation), 0, 0});

    return light;
}

/** Create a view whose camera is in a scene, looking at the origin from +Z.
 *
 * @param world The world
 * @param scene The scene of the camera
 * @param z World Z of the camera
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float z)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 0, z});
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

/** Get a shell a view of the last packet draws.
 *
 * @param null The backend
 * @param v The view
 * @param index Index of the shell in the shells of the view
 * @return The shell
 */
static const box_render_planet_atmos_t* view_shell(
    box_render_null_t *null,
    const box_render_view_t *v,
    int32_t index)
{
    test_assert(index < v->planet_count);
    test_assert(v->planet_first + index < ecs_vec_count(&null->last.planet_atmospheres));

    return ecs_vec_get_t(&null->last.planet_atmospheres, box_render_planet_atmos_t,
        v->planet_first + index);
}

/** Run a frame and get the status of a planet.
 *
 * @param world The world
 * @param e The entity with PlanetAtmosphere
 * @return The status
 */
static const PlanetAtmosphereStatus* frame_status(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_progress(world, 0);
    const PlanetAtmosphereStatus *result = ecs_get(world, e, PlanetAtmosphereStatus);
    test_assert(result != NULL);

    return result;
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

/** Set a PlanetAtmosphere with an invalid value and check that its status has an error and
 * that the view draws no shell.
 *
 * @param value The values
 */
static void expect_error(
    const PlanetAtmosphere *value)
{
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0);
    light_new(world, scene, 30, 1);
    view_new(world, scene, 10);
    ecs_set_ptr(world, planet, PlanetAtmosphere, value);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 0);
    const PlanetAtmosphereStatus *status = ecs_get(world, planet, PlanetAtmosphereStatus);
    test_assert(status != NULL);
    test_assert(status->error != NULL);
    test_int(status->lut_version, 0);
    ecs_fini(world);
    box_render_null_free(null);
}

/** Get the default PlanetAtmosphere values.
 *
 * @param out Receives the values
 */
static void planet_defaults(
    PlanetAtmosphere *out)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, PlanetAtmosphere);
    *out = *ecs_get(world, e, PlanetAtmosphere);
    ecs_fini(world);
}

void PlanetAtmosphere_defaults(void) {
    // planet_atmosphere/component
    PlanetAtmosphere p;
    planet_defaults(&p);
    test_uint(p.sun, 0);
    test_flt(p.radius, 1.0f);
    test_flt(p.planet_radius, 6360.0f);
    test_flt(p.thickness, 100.0f);
    test_flt(p.rayleigh.scale_height, 8.0f);
    test_flt(p.rayleigh.scattering_scale, 1.0f);
    test_flt(p.mie.scale_height, 1.2f);
    test_flt(p.mie.anisotropy, 0.8f);
    test_flt(p.mie.scattering_scale, 1.0f);
    test_int(p.mie.tint.r, 255);
    test_int(p.mie.tint.g, 255);
    test_int(p.mie.tint.b, 255);
    test_flt(p.ozone_scale, 1.0f);
    test_int(p.ground_albedo.r, 77);
    test_int(p.ground_albedo.g, 77);
    test_int(p.ground_albedo.b, 77);
    test_flt(p.intensity, 1.0f);
}

void PlanetAtmosphere_script_defaults(void) {
    // planet_atmosphere/component
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "planet_atmosphere_defaults",
        "using box.atmosphere\n"
        "planet {\n"
        "  PlanetAtmosphere: {radius: 2, rayleigh: {scale_height: 4}}\n"
        "}\n", NULL) == 0);
    ecs_entity_t planet = ecs_lookup(world, "planet");
    test_assert(planet != 0);
    const PlanetAtmosphere *p = ecs_get(world, planet, PlanetAtmosphere);
    test_assert(p != NULL);
    test_flt(p->radius, 2.0f);
    test_flt(p->planet_radius, 6360.0f);
    test_flt(p->thickness, 100.0f);
    test_flt(p->rayleigh.scale_height, 4.0f);
    test_flt(p->rayleigh.scattering_scale, 1.0f);
    test_flt(p->mie.anisotropy, 0.8f);
    test_flt(p->intensity, 1.0f);
    ecs_fini(world);
}

void PlanetAtmosphere_status_follows_component(void) {
    // planet_atmosphere/status
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t planet = ecs_new(world);
    ecs_add(world, planet, PlanetAtmosphere);
    test_assert(ecs_has(world, planet, PlanetAtmosphereStatus));
    const PlanetAtmosphereStatus *status = ecs_get(world, planet, PlanetAtmosphereStatus);
    test_int(status->lut_version, 0);
    test_assert(status->error == NULL);
    ecs_remove(world, planet, PlanetAtmosphere);
    test_assert(!ecs_has(world, planet, PlanetAtmosphereStatus));
    ecs_fini(world);
}

void PlanetAtmosphere_invalid_values_set_error(void) {
    // planet_atmosphere/status
    PlanetAtmosphere d, p;
    planet_defaults(&d);

    p = d; p.radius = 0;
    expect_error(&p);
    p = d; p.radius = -1;
    expect_error(&p);
    p = d; p.planet_radius = 0;
    expect_error(&p);
    p = d; p.thickness = 0;
    expect_error(&p);
    p = d; p.thickness = -10;
    expect_error(&p);
    p = d; p.rayleigh.scale_height = 0;
    expect_error(&p);
    p = d; p.mie.scale_height = -1;
    expect_error(&p);
    p = d; p.rayleigh.scattering_scale = -1;
    expect_error(&p);
    p = d; p.mie.anisotropy = 1;
    expect_error(&p);
    p = d; p.radius = NAN;
    expect_error(&p);
}

void PlanetAtmosphere_error_clears_when_valid(void) {
    // planet_atmosphere/status
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_get_mut(world, planet, PlanetAtmosphere)->thickness = 0;
    ecs_modified(world, planet, PlanetAtmosphere);
    const PlanetAtmosphereStatus *status = frame_status(world, planet);
    test_assert(status->error != NULL);
    test_int(status->lut_version, 0);

    ecs_get_mut(world, planet, PlanetAtmosphere)->thickness = 60;
    ecs_modified(world, planet, PlanetAtmosphere);
    status = frame_status(world, planet);
    test_assert(status->error == NULL);
    test_int(status->lut_version, 1);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_sun_not_attenuated(void) {
    // planet_atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    planet_new(world, scene, 0);
    ecs_entity_t sun = light_new(world, scene, 30, 2);
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_near(v->lights.sun[1], 0.5f, 1e-4f);
    test_near(v->lights.sun_color[0], 2, 1e-5f);
    test_near(v->lights.sun_color[1], 2, 1e-5f);
    test_near(v->lights.sun_color[2], 2, 1e-5f);
    const box_render_light_t *l = view_light(world, scene, sun);
    test_near(l->color[0], 2, 1e-5f);
    test_near(l->color[2], 2, 1e-5f);
    test_flt(ecs_get(world, sun, DirectionalLight)->intensity, 2);
    test_int(ecs_get(world, sun, Rgb)->b, 255);

    test_int(v->planet_count, 1);
    const box_render_planet_atmos_t *shell = view_shell(null, v, 0);
    test_near(shell->sun[1], 0.5f, 1e-4f);
    test_near(shell->sun_color[0], 2, 1e-5f);
    test_near(shell->sun_color[0], shell->sun_color[2], 1e-5f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_no_environment(void) {
    // planet_atmosphere/component
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0);
    light_new(world, scene, 30, 1);
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_assert(!ecs_has(world, planet, Environment));
    test_assert(!ecs_has(world, scene, Environment));
    test_assert(!ecs_has(world, planet, AtmosphereStatus));
    test_uint(v->atmosphere, 0);
    test_uint(v->environment.handle, 0);
    test_int(v->planet_count, 1);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_next_to_atmosphere(void) {
    // planet_atmosphere/component
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, 30, 2);
    ecs_add(world, scene, Atmosphere);
    ecs_get_mut(world, scene, Atmosphere)->sun = sun;
    ecs_modified(world, scene, Atmosphere);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_set(world, planet, PlanetAtmosphere, {
        .radius = 1,
        .planet_radius = 3000,
        .thickness = 80,
        .rayleigh = {8, 1},
        .mie = {1.2f, 0.8f, 1, {255, 128, 64}},
        .ozone_scale = 1,
        .ground_albedo = {77, 77, 77},
        .intensity = 1
    });
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);

    const AtmosphereStatus *as = ecs_get(world, scene, AtmosphereStatus);
    test_assert(as->error == NULL);
    test_int(as->lut_version, 1);
    test_assert(as->sun_transmittance[2] < 1.0f && as->sun_transmittance[2] > 0.5f);
    const PlanetAtmosphereStatus *ps = ecs_get(world, planet, PlanetAtmosphereStatus);
    test_assert(ps->error == NULL);
    test_int(ps->lut_version, 1);
    test_assert(ecs_has(world, scene, Environment));
    test_assert(!ecs_has(world, planet, Environment));

    test_near(v->lights.sun_color[2], 2 * as->sun_transmittance[2], 1e-4f);
    test_assert(v->atmosphere != 0);
    test_int(v->planet_count, 1);
    const box_render_planet_atmos_t *shell = view_shell(null, v, 0);
    test_assert(shell->atmosphere != 0);
    test_assert(shell->atmosphere != v->atmosphere);
    test_near(shell->sun_color[2], 2, 1e-5f);
    test_near(shell->medium.radii[0], 3000, 1e-3f);
    test_near(shell->medium.radii[1], 3080, 1e-3f);
    test_near(shell->center[3], 3000, 1e-2f);
    test_assert(shell->medium.mie_scattering[0] > shell->medium.mie_scattering[2]);

    ecs_remove(world, planet, PlanetAtmosphere);
    v = frame_view(world, null, scene);
    test_assert(v->atmosphere != 0);
    test_int(v->planet_count, 0);
    test_int(ecs_get(world, scene, AtmosphereStatus)->lut_version, 1);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_lut_version_follows_medium(void) {
    // planet_atmosphere/luts
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t sun = light_new(world, scene, 30, 1);
    view_new(world, scene, 10);
    test_int(frame_status(world, planet)->lut_version, 1);
    test_int(frame_status(world, planet)->lut_version, 1);

    ecs_get_mut(world, planet, PlanetAtmosphere)->intensity = 3;
    ecs_modified(world, planet, PlanetAtmosphere);
    test_int(frame_status(world, planet)->lut_version, 1);

    ecs_get_mut(world, planet, PlanetAtmosphere)->sun = sun;
    ecs_modified(world, planet, PlanetAtmosphere);
    test_int(frame_status(world, planet)->lut_version, 1);

    ecs_get_mut(world, planet, PlanetAtmosphere)->mie.anisotropy = 0.5f;
    ecs_modified(world, planet, PlanetAtmosphere);
    test_int(frame_status(world, planet)->lut_version, 1);

    ecs_get_mut(world, planet, PlanetAtmosphere)->radius = 2;
    ecs_modified(world, planet, PlanetAtmosphere);
    test_int(frame_status(world, planet)->lut_version, 1);

    ecs_set(world, planet, Position, {0, 1, 0});
    ecs_set(world, sun, Rotation, {glm_rad(10.0f), 0, 0});
    test_int(frame_status(world, planet)->lut_version, 1);

    ecs_get_mut(world, planet, PlanetAtmosphere)->rayleigh.scattering_scale = 2;
    ecs_modified(world, planet, PlanetAtmosphere);
    test_int(frame_status(world, planet)->lut_version, 2);
    test_int(frame_status(world, planet)->lut_version, 2);

    ecs_get_mut(world, planet, PlanetAtmosphere)->thickness = 60;
    ecs_modified(world, planet, PlanetAtmosphere);
    test_int(frame_status(world, planet)->lut_version, 3);

    ecs_get_mut(world, planet, PlanetAtmosphere)->ground_albedo = (rgb){200, 100, 50};
    ecs_modified(world, planet, PlanetAtmosphere);
    test_int(frame_status(world, planet)->lut_version, 4);

    ecs_get_mut(world, planet, PlanetAtmosphere)->mie.tint = (rgb){255, 100, 50};
    ecs_modified(world, planet, PlanetAtmosphere);
    test_int(frame_status(world, planet)->lut_version, 5);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_view_gets_shell(void) {
    // planet_atmosphere/views
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_entity_t sun = light_new(world, scene, 30, 1);
    light_new(world, scene, 60, 4);
    ecs_set(world, planet, PlanetAtmosphere, {
        .sun = sun,
        .radius = 2,
        .planet_radius = 6000,
        .thickness = 60,
        .rayleigh = {8, 1},
        .mie = {1.2f, 0.7f, 1, {255, 255, 255}},
        .ozone_scale = 0,
        .ground_albedo = {77, 77, 77},
        .intensity = 3
    });
    ecs_set(world, planet, Position, {0.5f, 0, 0});
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    const box_render_planet_atmos_t *shell = view_shell(null, v, 0);
    test_uint(shell->entity, planet);
    test_assert(shell->atmosphere != 0);
    test_near(shell->center[0], 0.5f, 1e-5f);
    test_near(shell->center[1], 0, 1e-5f);
    test_near(shell->center[3], 3000, 1e-2f);
    test_near(shell->medium.radii[0], 6000, 1e-3f);
    test_near(shell->medium.radii[1], 6060, 1e-3f);
    test_near(shell->medium.ozone[1], 0, 1e-9f);
    test_near(shell->sun[1], 0.5f, 1e-4f);
    test_near(shell->sun[3], 0.7f, 1e-6f);
    test_near(shell->sun_color[0], 1, 1e-5f);
    test_near(shell->sun_color[3], 3, 1e-6f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_first_light_is_default_sun(void) {
    // planet_atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    planet_new(world, scene, 0);
    light_new(world, scene, 30, 2);
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    const box_render_planet_atmos_t *shell = view_shell(null, v, 0);
    test_near(shell->sun[1], 0.5f, 1e-4f);
    test_near(shell->sun_color[1], 2, 1e-5f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_no_light_no_sun(void) {
    // planet_atmosphere/lights
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    planet_new(world, scene, 0);
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    const box_render_planet_atmos_t *shell = view_shell(null, v, 0);
    test_near(shell->sun[1], -1, 1e-6f);
    test_near(shell->sun_color[0], 0, 1e-9f);
    test_near(shell->sun_color[3], 1, 1e-6f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_other_scene_not_drawn(void) {
    // planet_atmosphere/views
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t other = scene_new(world);
    planet_new(world, other, 0);
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_behind_camera_not_drawn(void) {
    // planet_atmosphere/views
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_set(world, planet, Position, {0, 0, 30});
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_shells_back_to_front(void) {
    // planet_atmosphere/views
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t near_ = planet_new(world, scene, 0);
    ecs_entity_t far_ = planet_new(world, scene, 0);
    ecs_set(world, near_, Position, {0, 0, 2});
    ecs_set(world, far_, Position, {0, 0, -20});
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 2);
    test_uint(view_shell(null, v, 0)->entity, far_);
    test_uint(view_shell(null, v, 1)->entity, near_);
    test_assert(view_shell(null, v, 0)->atmosphere != view_shell(null, v, 1)->atmosphere);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_removed_frees_luts(void) {
    // planet_atmosphere/luts
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0);
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    uint32_t handle = view_shell(null, v, 0)->atmosphere;
    ecs_delete(world, planet);
    v = frame_view(world, null, scene);
    test_int(v->planet_count, 0);
    bool destroyed = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_ATMOSPHERE && r->op == BOX_RENDER_DESTROY &&
            r->handle == handle)
        {
            destroyed = true;
        }
    }

    test_assert(destroyed);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_scaled_parent_scales_shell(void) {
    // planet_atmosphere/scale
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t parent = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, parent, Scale, {2, 2, 2});
    ecs_entity_t planet = planet_new(world, parent, 0.25f);
    ecs_set(world, planet, PlanetAtmosphere, {
        .radius = 2,
        .planet_radius = 6000,
        .thickness = 60,
        .rayleigh = {8, 1},
        .mie = {1.2f, 0.8f, 1, {255, 255, 255}},
        .ozone_scale = 1,
        .ground_albedo = {77, 77, 77},
        .intensity = 1
    });
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    const box_render_planet_atmos_t *shell = view_shell(null, v, 0);
    test_uint(shell->entity, planet);
    test_near(shell->center[0], 0.5f, 1e-5f);
    test_near(shell->center[1], 0, 1e-5f);
    test_near(shell->center[2], 0, 1e-5f);
    test_near(shell->center[3], 1500, 1e-2f);
    test_near(shell->medium.radii[0], 6000, 1e-3f);
    test_near(shell->medium.radii[1], 6060, 1e-3f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_scaled_planet_scales_shell(void) {
    // planet_atmosphere/scale
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0.5f);
    ecs_set(world, planet, Scale, {3, 3, 3});
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    const box_render_planet_atmos_t *shell = view_shell(null, v, 0);
    test_near(shell->center[0], 0.5f, 1e-5f);
    test_near(shell->center[3], 6360.0f / 3.0f, 1e-2f);
    test_near(shell->medium.radii[0], 6360, 1e-2f);

    ecs_set(world, planet, Scale, {1, 1, 1});
    v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    shell = view_shell(null, v, 0);
    test_assert(shell->center[3] == 6360.0f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_non_uniform_scale_uses_mean(void) {
    // planet_atmosphere/scale
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t planet = planet_new(world, scene, 0);
    ecs_set(world, planet, Scale, {1, 2, 4});
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    test_near(view_shell(null, v, 0)->center[3], 3180, 1e-1f);
    ecs_fini(world);
    box_render_null_free(null);
}

void PlanetAtmosphere_scaled_planet_culling(void) {
    // planet_atmosphere/scale
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);

    /* The frustum of the view at the depth of the planets is about 5.77 high above the axis.
     * The top of the shell of a planet of radius 1 is at about 1.016. */
    ecs_entity_t outside = planet_new(world, scene, 0);
    ecs_set(world, outside, Position, {0, 7.5f, 0});
    ecs_entity_t inside = planet_new(world, scene, 0);
    ecs_set(world, inside, Position, {0, -6.6f, 0});
    view_new(world, scene, 10);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    test_uint(view_shell(null, v, 0)->entity, inside);

    /* Grown by 4 the outside planet reaches into the frustum, shrunk by 2 the inside planet
     * leaves it */
    ecs_set(world, outside, Scale, {4, 4, 4});
    ecs_set(world, inside, Scale, {0.5f, 0.5f, 0.5f});
    v = frame_view(world, null, scene);
    test_int(v->planet_count, 1);
    test_uint(view_shell(null, v, 0)->entity, outside);

    /* A planet flattened to a scale of 0 is not drawn */
    ecs_set(world, outside, Scale, {0, 0, 0});
    v = frame_view(world, null, scene);
    test_int(v->planet_count, 0);
    ecs_fini(world);
    box_render_null_free(null);
}
