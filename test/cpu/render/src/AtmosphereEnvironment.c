/**
 * @file AtmosphereEnvironment.c
 * @brief Tests for the Atmosphere as an environment source: the Environment it adds, the
 * throttled renders its packets carry and the environment settings of the bakes.
 */

#include <box.h>
#include <bake_test.h>
#include "environment.h"

/** Create a world that renders into a null backend.
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

/** Turn a directional light to shine down from an elevation.
 *
 * @param world The world
 * @param light The light
 * @param elevation Elevation of the direction toward the light in degrees
 */
static void light_turn(
    ecs_world_t *world,
    ecs_entity_t light,
    float elevation)
{
    ecs_set(world, light, Rotation, {glm_rad(elevation), glm_rad(30.0f), 0});
}

/** Create a white directional light in a scene that shines down from an elevation, the sun of
 * the Atmosphere of the scene when that has none.
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
    light_turn(world, light, elevation);
    const Atmosphere *a = ecs_get(world, scene, Atmosphere);
    if (a && !a->sun) {
        ecs_get_mut(world, scene, Atmosphere)->sun = light;
        ecs_modified(world, scene, Atmosphere);
    }

    return light;
}

/** Create a lit view whose camera is in a scene, lit by an environment source.
 *
 * @param world The world
 * @param scene The scene of the camera
 * @param source The environment source
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t source)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->lighting.environment.source = source;
    ecs_modified(world, view, View);

    return view;
}

/** Run a frame and wait until the backend received it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Get the EnvironmentStatus.version of a source.
 *
 * @param world The world
 * @param e The source
 * @return The version
 */
static int32_t version_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const EnvironmentStatus *status = ecs_get(world, e, EnvironmentStatus);
    test_assert(status != NULL);

    return status->version;
}

/** Count the renders of the sky of an atmosphere into a cubemap in the last packet.
 *
 * @param null The backend
 * @param out Receives the last one, may be NULL
 * @return The renders
 */
static int32_t renders(
    box_render_null_t *null,
    box_render_atmosphere_env_t *out)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_ATMOSPHERE_ENVIRONMENT) {
            test_int(r->op, BOX_RENDER_UPDATE);
            test_assert(r->size >= sizeof(box_render_atmosphere_env_t));
            if (out) {
                *out = *(const box_render_atmosphere_env_t*)box_render_packet_data(
                    &null->last, r->data);
            }

            result ++;
        }
    }

    return result;
}

/** Find the last fill or create of a probe volume in the last packet.
 *
 * @param null The backend
 * @param out Receives its description
 * @return Whether the packet fills a volume
 */
static bool volume_fill(
    box_render_null_t *null,
    box_render_volume_desc_t *out)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        const box_render_volume_desc_t *desc = r->kind == BOX_RENDER_VOLUME &&
            r->op != BOX_RENDER_DESTROY ? box_render_packet_data(&null->last, r->data) : NULL;
        if (desc && (r->op == BOX_RENDER_CREATE || desc->op == BOX_RENDER_VOLUME_FILL)) {
            *out = *desc;
            result = true;
        }
    }

    return result;
}

void AtmosphereEnvironment_adds_environment(void) {
    // atmosphere/environment
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scene = scene_new(world);

    const Environment *env = ecs_get(world, scene, Environment);
    test_assert(env != NULL);
    test_int(env->resolution, 128);
    test_int(env->filter, EnvironmentFilterBox);
    const EnvironmentStatus *status = ecs_get(world, scene, EnvironmentStatus);
    test_assert(status != NULL);
    test_int(status->version, 0);

    ecs_fini(world);
}

void AtmosphereEnvironment_singleton_adds_environment(void) {
    // atmosphere/environment
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, Atmosphere);

    const Environment *env = ecs_get(world, ecs_id(Atmosphere), Environment);
    test_assert(env != NULL);
    test_int(env->resolution, 128);
    test_int(env->filter, EnvironmentFilterBox);
    test_assert(ecs_has(world, ecs_id(Atmosphere), EnvironmentStatus));

    ecs_fini(world);
}

void AtmosphereEnvironment_keeps_authored_environment(void) {
    // atmosphere/environment
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, Environment, {.resolution = 64, .filter = EnvironmentFilterGgx});
    ecs_add(world, scene, Atmosphere);

    const Environment *env = ecs_get(world, scene, Environment);
    test_int(env->resolution, 64);
    test_int(env->filter, EnvironmentFilterGgx);

    ecs_fini(world);
}

void AtmosphereEnvironment_authored_after_atmosphere_wins(void) {
    // atmosphere/environment
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scene = scene_new(world);
    ecs_set(world, scene, Environment, {.resolution = 256, .filter = EnvironmentFilterGgx});

    const Environment *env = ecs_get(world, scene, Environment);
    test_int(env->resolution, 256);
    test_int(env->filter, EnvironmentFilterGgx);

    ecs_fini(world);
}

void AtmosphereEnvironment_script_authored_environment_wins(void) {
    // atmosphere/environment
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "atmospheres",
        "using box.view\n"
        "using box.light\n"
        "using box.atmosphere\n"
        "plain {\n"
        "  Scene: {}\n"
        "  Atmosphere: {}\n"
        "}\n"
        "after {\n"
        "  Scene: {}\n"
        "  Atmosphere: {}\n"
        "  Environment: {resolution: 512, filter: Ggx}\n"
        "}\n"
        "before {\n"
        "  Scene: {}\n"
        "  Environment: {resolution: 32, filter: Ggx}\n"
        "  Atmosphere: {}\n"
        "}\n", NULL) == 0);

    const Environment *env = ecs_get(world, ecs_lookup(world, "plain"), Environment);
    test_assert(env != NULL);
    test_int(env->resolution, 128);
    test_int(env->filter, EnvironmentFilterBox);
    env = ecs_get(world, ecs_lookup(world, "after"), Environment);
    test_int(env->resolution, 512);
    test_int(env->filter, EnvironmentFilterGgx);
    env = ecs_get(world, ecs_lookup(world, "before"), Environment);
    test_int(env->resolution, 32);
    test_int(env->filter, EnvironmentFilterGgx);

    ecs_fini(world);
}

void AtmosphereEnvironment_light_bake_environment_default(void) {
    // environment/bake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "bakes",
        "using box.light\n"
        "scripted { Bake: {} }\n", NULL) == 0);
    const LightBake *bake = ecs_get(world, ecs_lookup(world, "scripted"), LightBake);
    test_assert(bake != NULL);
    test_uint(bake->environment, 0);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, LightBake);
    test_uint(ecs_get(world, e, LightBake)->environment, 0);

    ecs_entity_t source = ecs_new(world);
    ecs_set(world, e, LightBake, {.environment = source, .compute = true});
    test_uint(ecs_get(world, e, LightBake)->environment, source);

    ecs_fini(world);
}

void AtmosphereEnvironment_reflection_bake_environment_default(void) {
    // environment/bake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "bakes",
        "using box.light\n"
        "scripted { ReflectionBake: {} }\n", NULL) == 0);
    const ReflectionBake *bake = ecs_get(world, ecs_lookup(world, "scripted"), ReflectionBake);
    test_assert(bake != NULL);
    test_uint(bake->environment, 0);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ReflectionBake);
    test_uint(ecs_get(world, e, ReflectionBake)->environment, 0);

    ecs_entity_t source = ecs_new(world);
    ecs_set(world, e, ReflectionBake, {.environment = source});
    test_uint(ecs_get(world, e, ReflectionBake)->environment, source);

    ecs_fini(world);
}

void AtmosphereEnvironment_renders_once_without_changes(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    light_new(world, scene, 60, 1);
    ecs_entity_t view = view_new(world, scene, scene);
    box_render_atmosphere_env_t desc;
    int32_t i;

    frame(world);
    test_int(renders(null, &desc), 1);
    test_int(version_of(world, scene), 1);
    test_int(desc.version, 1);
    test_uint(desc.atmosphere, box_render_atmosphere_handle(world, scene));
    test_assert(desc.sun[1] > 0.8f);
    test_assert(desc.sun_color[0] > 0);
    test_flt(desc.moon_color[0], 0);
    test_flt(desc.altitude, 0.5f);

    const box_render_view_t *v = NULL;
    for (i = 0; i < ecs_vec_count(&null->last.views); i ++) {
        const box_render_view_t *pv = ecs_vec_get_t(&null->last.views, box_render_view_t, i);
        if (pv->view == view) {
            v = pv;
        }
    }

    test_assert(v != NULL);
    test_assert(v->environment.handle != 0);
    test_uint(v->environment.handle, box_render_environment_find(world, scene));

    for (i = 0; i < 10; i ++) {
        frame(world);
        test_int(renders(null, NULL), 0);
    }

    test_int(version_of(world, scene), 1);
    ecs_fini(world);
}

void AtmosphereEnvironment_sun_move_past_angle_renders(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, 60, 1);
    frame(world);
    frame(world);
    test_int(version_of(world, scene), 1);

    light_turn(world, sun, 61);
    frame(world);
    test_int(renders(null, NULL), 1);
    test_int(version_of(world, scene), 2);
    frame(world);
    test_int(renders(null, NULL), 0);
    test_int(version_of(world, scene), 2);

    ecs_fini(world);
}

void AtmosphereEnvironment_small_sun_moves_accumulate(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, 60, 1);
    frame(world);
    test_int(version_of(world, scene), 1);

    light_turn(world, sun, 60.3f);
    frame(world);
    test_int(renders(null, NULL), 0);
    test_int(version_of(world, scene), 1);

    light_turn(world, sun, 60.6f);
    frame(world);
    test_int(renders(null, NULL), 1);
    test_int(version_of(world, scene), 2);

    ecs_fini(world);
}

void AtmosphereEnvironment_update_angle_follows_setting(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, 60, 1);
    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->ibl_update_angle = 5;
    ecs_modified(world, scene, Atmosphere);
    frame(world);
    test_int(version_of(world, scene), 1);

    light_turn(world, sun, 63);
    frame(world);
    test_int(version_of(world, scene), 1);

    light_turn(world, sun, 66);
    frame(world);
    test_int(version_of(world, scene), 2);

    ecs_fini(world);
}

void AtmosphereEnvironment_color_change_threshold(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, 60, 10);
    frame(world);
    test_int(version_of(world, scene), 1);

    ecs_set(world, sun, DirectionalLight, {.intensity = 10.1f});
    frame(world);
    test_int(renders(null, NULL), 0);
    test_int(version_of(world, scene), 1);

    ecs_set(world, sun, DirectionalLight, {.intensity = 10.5f});
    frame(world);
    test_int(renders(null, NULL), 1);
    test_int(version_of(world, scene), 2);

    ecs_set(world, sun, Rgb, {255, 240, 255});
    frame(world);
    test_int(renders(null, NULL), 1);
    test_int(version_of(world, scene), 3);

    ecs_fini(world);
}

void AtmosphereEnvironment_medium_change_renders(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    light_new(world, scene, 60, 1);
    frame(world);
    test_int(version_of(world, scene), 1);

    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->ibl_update_angle = 1;
    a->sun_disk_intensity = 4;
    ecs_modified(world, scene, Atmosphere);
    frame(world);
    test_int(renders(null, NULL), 0);
    test_int(version_of(world, scene), 1);

    a = ecs_get_mut(world, scene, Atmosphere);
    a->rayleigh.scattering_scale = 2;
    ecs_modified(world, scene, Atmosphere);
    frame(world);
    test_int(renders(null, NULL), 1);
    test_int(version_of(world, scene), 2);

    a = ecs_get_mut(world, scene, Atmosphere);
    a->ground_altitude = 2;
    ecs_modified(world, scene, Atmosphere);
    frame(world);
    test_int(renders(null, NULL), 1);
    test_int(version_of(world, scene), 3);

    ecs_fini(world);
}

void AtmosphereEnvironment_moon_scatters_into_environment(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, -30, 1);
    ecs_entity_t moon = light_new(world, scene, 40, 0.01f);
    ecs_set(world, scene, Atmosphere, {
        .sun = sun, .moon = moon, .planet_radius = 6360, .thickness = 100,
        .ground_altitude = 0.5f, .units_per_km = 1000,
        .rayleigh = {.scale_height = 8, .scattering_scale = 1},
        .mie = {.scale_height = 1.2f, .anisotropy = 0.8f, .scattering_scale = 1},
        .ibl_update_angle = 0.5f
    });
    box_render_atmosphere_env_t desc;
    frame(world);
    test_int(renders(null, &desc), 1);
    test_assert(desc.moon[1] > 0.5f);
    test_assert(desc.moon_color[0] > 0);
    test_assert(desc.sun[1] < 0);

    ecs_set(world, moon, DirectionalLight, {.intensity = 0.0105f});
    frame(world);
    test_int(renders(null, NULL), 1);

    ecs_fini(world);
}

void AtmosphereEnvironment_invalid_resolution_renders_nothing(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    light_new(world, scene, 60, 1);
    ecs_set(world, scene, Environment, {.resolution = 100, .filter = EnvironmentFilterBox});
    frame(world);
    test_int(renders(null, NULL), 0);
    test_int(version_of(world, scene), 0);

    ecs_set(world, scene, Environment, {.resolution = 64, .filter = EnvironmentFilterBox});
    frame(world);
    test_int(renders(null, NULL), 1);
    test_int(version_of(world, scene), 1);

    ecs_fini(world);
}

void AtmosphereEnvironment_resolution_change_renders_again(void) {
    // atmosphere/environment
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    light_new(world, scene, 60, 1);
    frame(world);
    frame(world);
    test_int(version_of(world, scene), 1);

    ecs_set(world, scene, Environment, {.resolution = 64, .filter = EnvironmentFilterBox});
    frame(world);
    test_int(renders(null, NULL), 1);
    test_int(version_of(world, scene), 2);

    ecs_set(world, scene, Environment, {.resolution = 64, .filter = EnvironmentFilterGgx});
    frame(world);
    test_int(renders(null, NULL), 0);
    test_int(version_of(world, scene), 2);

    ecs_fini(world);
}

void AtmosphereEnvironment_bake_fills_with_environment(void) {
    // environment/bake
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, 60, 1);
    ecs_set(world, scene, LightBake, {
        .probe_spacing = 2, .bounces = 1, .capture_size = 8, .budget_ms = 1,
        .bounds_padding = 1, .environment = scene, .compute = true
    });
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Position, {0, 0, 0});
    view_new(world, scene, scene);
    box_render_volume_desc_t desc;
    int32_t i, fills = 0;
    uint32_t environment = 0;
    for (i = 0; i < 3; i ++) {
        frame(world);
        if (volume_fill(null, &desc)) {
            fills ++;
            environment = desc.environment;
        }
    }

    test_assert(fills >= 1);
    test_assert(environment != 0);
    test_uint(environment, box_render_environment_find(world, scene));
    frame(world);
    test_assert(!volume_fill(null, &desc));

    light_turn(world, sun, 70);
    fills = 0;
    for (i = 0; i < 2; i ++) {
        frame(world);
        if (volume_fill(null, &desc)) {
            fills ++;
            test_uint(desc.environment, environment);
        }
    }

    test_int(fills, 1);
    ecs_fini(world);
}

void AtmosphereEnvironment_bake_without_environment_uses_hemisphere(void) {
    // environment/bake
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world);
    ecs_entity_t sun = light_new(world, scene, 60, 1);
    ecs_set(world, scene, LightBake, {
        .probe_spacing = 2, .bounces = 1, .capture_size = 8, .budget_ms = 1,
        .bounds_padding = 1, .compute = true
    });
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Position, {0, 0, 0});
    view_new(world, scene, scene);
    box_render_volume_desc_t desc;
    int32_t i, fills = 0;
    for (i = 0; i < 3; i ++) {
        frame(world);
        if (volume_fill(null, &desc)) {
            fills ++;
            test_uint(desc.environment, 0);
        }
    }

    test_int(fills, 1);
    light_turn(world, sun, 70);
    frame(world);
    frame(world);
    test_assert(!volume_fill(null, &desc));

    ecs_fini(world);
}
