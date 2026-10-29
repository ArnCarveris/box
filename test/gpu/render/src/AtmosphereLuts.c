/**
 * @file AtmosphereLuts.c
 * @brief Tests for the atmosphere LUTs: when they are built, the resources they create and the
 * transmittance LUT against the CPU transmittance of AtmosphereStatus.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Largest difference of a transmittance read from the LUT and the CPU transmittance */
#define TRANSMITTANCE_TOLERANCE (0.01f)

/** Create a headless world that renders with the GPU on the main thread.
 *
 * @return The world
 */
static ecs_world_t* world_new(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    return world;
}

/** Create a white sun in a scene at an elevation.
 *
 * @param world The world
 * @param scene The scene
 * @param elevation Elevation of the direction toward the sun in degrees
 * @return The sun
 */
static ecs_entity_t sun_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float elevation)
{
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_set(world, sun, Rotation, {glm_rad(elevation), 0, 0});

    return sun;
}

/** Create a camera in a scene with a view under the scene root.
 *
 * @param world The world
 * @param scene The scene of the camera
 * @return The camera
 */
static ecs_entity_t camera_new(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});

    return camera;
}

/** Create a scene with a sun, a camera and a surface that renders the views of the scene.
 *
 * @param world The world
 * @param atmosphere Whether the scene has the default Atmosphere
 * @param elevation Elevation of the sun in degrees
 * @param sun Receives the sun, NULL when not needed
 * @param camera Receives the camera, NULL when not needed
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    bool atmosphere,
    float elevation,
    ecs_entity_t *sun,
    ecs_entity_t *camera)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    if (atmosphere) {
        ecs_add(world, scene, Atmosphere);
    }

    ecs_entity_t s = sun_new(world, scene, elevation);
    if (atmosphere) {
        ecs_get_mut(world, scene, Atmosphere)->sun = s;
        ecs_modified(world, scene, Atmosphere);
    }

    ecs_entity_t c = camera_new(world, scene);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 48, .resolution_scale = 1,
        .view_root = scene});
    if (sun) {
        *sun = s;
    }

    if (camera) {
        *camera = c;
    }

    return scene;
}

/** Run frames, each rendered before the next one starts.
 *
 * @param world The world
 * @param count Number of frames
 */
static void frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }
}

/** Get the lut_version of the atmosphere of a scene.
 *
 * @param world The world
 * @param scene The scene
 * @return The version
 */
static int32_t lut_version(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    const AtmosphereStatus *status = ecs_get(world, scene, AtmosphereStatus);
    test_assert(status != NULL);
    test_assert(status->error == NULL);

    return status->lut_version;
}

/** Get the counters of the GPU backend.
 *
 * @param world The world
 * @return The counters
 */
static box_render_gpu_stats_t gpu_stats(
    ecs_world_t *world)
{
    box_render_gpu_stats_t result = {0};
    test_assert(box_render_gpu_stats(world, &result));
    test_assert(result.ready);

    return result;
}

/** Sample the transmittance LUT read back from the GPU as the linear clamp sampler does.
 *
 * @param texels The LUT, four floats per texel row by row
 * @param uv The texture coordinates
 * @param out Receives the rgb transmittance
 */
static void sample_lut(
    const float *texels,
    const float *uv,
    float *out)
{
    int32_t w = BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH, h = BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT;
    float x = glm_clamp(uv[0] * (float)w - 0.5f, 0.0f, (float)(w - 1));
    float y = glm_clamp(uv[1] * (float)h - 0.5f, 0.0f, (float)(h - 1));
    int32_t x0 = (int32_t)x, y0 = (int32_t)y;
    int32_t x1 = glm_min(x0 + 1, w - 1), y1 = glm_min(y0 + 1, h - 1);
    float fx = x - (float)x0, fy = y - (float)y0;
    int32_t c;
    for (c = 0; c < 3; c ++) {
        float a = texels[(y0 * w + x0) * 4 + c] * (1 - fx) + texels[(y0 * w + x1) * 4 + c] * fx;
        float b = texels[(y1 * w + x0) * 4 + c] * (1 - fx) + texels[(y1 * w + x1) * 4 + c] * fx;
        out[c] = a * (1 - fy) + b * fy;
    }
}

/** Check that the transmittance LUT of a scene whose sun is at an elevation holds the CPU
 * transmittance toward the sun from the ground altitude.
 *
 * @param elevation Elevation of the sun in degrees
 */
static void transmittance_matches(
    float elevation)
{
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, true, elevation, NULL, NULL);
    float *texels = ecs_os_malloc_n(float,
        BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH * BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT * 4);
    float uv[2], gpu[3];
    int32_t c;
    frames(world, 3);
    const Atmosphere *a = ecs_get(world, scene, Atmosphere);
    const AtmosphereStatus *status = ecs_get(world, scene, AtmosphereStatus);
    test_assert(status->error == NULL);
    test_assert(box_render_gpu_read_atmosphere(world, box_render_atmosphere_handle(world, scene),
        BOX_RENDER_ATMOS_TRANSMITTANCE, texels,
        BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH * BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT * 4));
    box_render_atmos_transmittance_uv(a->planet_radius, a->planet_radius + a->thickness,
        a->planet_radius + a->ground_altitude, sinf(glm_rad(elevation)), uv);
    sample_lut(texels, uv, gpu);
    for (c = 0; c < 3; c ++) {
        test_assert(status->sun_transmittance[c] > 0);
        if (fabsf(gpu[c] - status->sun_transmittance[c]) > TRANSMITTANCE_TOLERANCE) {
            printf("channel %d: lut %f, cpu %f\n", c, (double)gpu[c],
                (double)status->sun_transmittance[c]);
        }

        test_assert(fabsf(gpu[c] - status->sun_transmittance[c]) <= TRANSMITTANCE_TOLERANCE);
    }

    ecs_os_free(texels);
    ecs_fini(world);
}

void AtmosphereLuts_version_steady_without_changes(void) {
    // atmosphere/luts
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, true, 30, NULL, NULL);
    frames(world, 3);
    int32_t version = lut_version(world, scene);
    int32_t builds = gpu_stats(world).atmosphere_builds;
    test_assert(version >= 1);
    test_assert(builds >= 1);
    frames(world, 5);
    test_int(lut_version(world, scene), version);
    test_int(gpu_stats(world).atmosphere_builds, builds);
    ecs_fini(world);
}

void AtmosphereLuts_version_changes_with_medium(void) {
    // atmosphere/luts
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, true, 30, NULL, NULL);
    frames(world, 3);
    int32_t version = lut_version(world, scene);
    int32_t builds = gpu_stats(world).atmosphere_builds;

    ecs_get_mut(world, scene, Atmosphere)->rayleigh.scattering_scale = 2;
    ecs_modified(world, scene, Atmosphere);
    frames(world, 1);
    test_int(lut_version(world, scene), version + 1);
    test_int(gpu_stats(world).atmosphere_builds, builds + 1);

    ecs_get_mut(world, scene, Atmosphere)->ground_albedo = (rgb){200, 200, 200};
    ecs_modified(world, scene, Atmosphere);
    frames(world, 1);
    test_int(lut_version(world, scene), version + 2);

    ecs_get_mut(world, scene, Atmosphere)->mie.tint = (rgb){255, 128, 128};
    ecs_modified(world, scene, Atmosphere);
    frames(world, 1);
    test_int(lut_version(world, scene), version + 3);

    ecs_get_mut(world, scene, Atmosphere)->night_intensity = 1;
    ecs_modified(world, scene, Atmosphere);
    frames(world, 2);
    test_int(lut_version(world, scene), version + 3);
    test_int(gpu_stats(world).atmosphere_builds, builds + 3);
    ecs_fini(world);
}

void AtmosphereLuts_version_kept_when_sun_moves(void) {
    // atmosphere/luts
    ecs_world_t *world = world_new();
    ecs_entity_t sun;
    ecs_entity_t scene = scene_new(world, true, 30, &sun, NULL);
    frames(world, 3);
    int32_t version = lut_version(world, scene);
    int32_t builds = gpu_stats(world).atmosphere_builds;
    float transmittance = ecs_get(world, scene, AtmosphereStatus)->sun_transmittance[2];

    ecs_set(world, sun, Rotation, {glm_rad(5), glm_rad(40), 0});
    frames(world, 2);
    test_assert(ecs_get(world, scene, AtmosphereStatus)->sun_transmittance[2] < transmittance);
    test_int(lut_version(world, scene), version);
    test_int(gpu_stats(world).atmosphere_builds, builds);
    ecs_fini(world);
}

void AtmosphereLuts_version_kept_when_camera_moves(void) {
    // atmosphere/luts
    ecs_world_t *world = world_new();
    ecs_entity_t camera;
    ecs_entity_t scene = scene_new(world, true, 30, NULL, &camera);
    frames(world, 3);
    int32_t version = lut_version(world, scene);
    int32_t builds = gpu_stats(world).atmosphere_builds;

    ecs_set(world, camera, Position, {100, 3000, -50});
    ecs_set(world, camera, Rotation, {0.3f, 1.2f, 0});
    frames(world, 2);
    test_int(lut_version(world, scene), version);
    test_int(gpu_stats(world).atmosphere_builds, builds);
    ecs_fini(world);
}

void AtmosphereLuts_scene_without_atmosphere_has_no_resources(void) {
    // atmosphere/luts
    ecs_world_t *world = world_new();
    ecs_entity_t plain = scene_new(world, false, 30, NULL, NULL);
    frames(world, 3);
    box_render_gpu_stats_t stats = gpu_stats(world);
    ecs_entity_t shader = ecs_lookup(world, "box.shader.builtin.atmosphere");
    test_assert(shader != 0);
    test_assert(!ecs_has(world, shader, ShaderStatus));
    test_int(stats.atmosphere_luts, 0);
    test_int(stats.atmosphere_views, 0);
    test_int(stats.atmosphere_builds, 0);
    test_uint(box_render_atmosphere_handle(world, plain), 0);

    ecs_entity_t sky = scene_new(world, true, 30, NULL, NULL);
    frames(world, 3);
    stats = gpu_stats(world);
    test_assert(ecs_get(world, shader, ShaderStatus)->ok);
    test_int(stats.atmosphere_luts, 1);
    test_int(stats.atmosphere_views, 1);
    test_assert(box_render_atmosphere_handle(world, sky) != 0);
    test_uint(box_render_atmosphere_handle(world, plain), 0);
    ecs_fini(world);
}

void AtmosphereLuts_removed_atmosphere_releases_resources(void) {
    // atmosphere/luts
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, true, 30, NULL, NULL);
    frames(world, 3);
    box_render_gpu_stats_t stats = gpu_stats(world);
    test_int(stats.atmosphere_luts, 1);
    test_int(stats.atmosphere_views, 1);

    ecs_remove(world, scene, Atmosphere);
    frames(world, 2);
    stats = gpu_stats(world);
    test_int(stats.atmosphere_luts, 0);
    test_int(stats.atmosphere_views, 0);
    test_uint(box_render_atmosphere_handle(world, scene), 0);
    ecs_fini(world);
}

void AtmosphereLuts_views_of_a_scene_get_own_volumes(void) {
    // atmosphere/luts
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, true, 30, NULL, NULL);
    ecs_entity_t camera = camera_new(world, scene);
    ecs_set(world, camera, Position, {0, 500, 0});
    ecs_set(world, camera, Rotation, {-0.5f, 2.0f, 0});
    frames(world, 3);
    box_render_gpu_stats_t stats = gpu_stats(world);
    test_int(stats.atmosphere_luts, 1);
    test_int(stats.atmosphere_views, 2);
    ecs_fini(world);
}

void AtmosphereLuts_transmittance_matches_cpu_at_zenith(void) {
    // atmosphere/luts
    transmittance_matches(90);
}

void AtmosphereLuts_transmittance_matches_cpu_near_horizon(void) {
    // atmosphere/luts
    transmittance_matches(3);
}
