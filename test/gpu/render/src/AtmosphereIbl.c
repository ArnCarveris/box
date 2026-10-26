/**
 * @file AtmosphereIbl.c
 * @brief Tests for the Atmosphere as an environment source: spheres lit only by its cubemap,
 * the throttle of its renders and the light bake that sees it.
 *
 * The sky lives in one scene and the sphere in another without lights, whose view takes the
 * entity with the Atmosphere as its LightSettings.environment.source, so the sun of the sky
 * lights the sphere only through the environment.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "environment.h"
#include "bake.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the rendered image */
#define SIZE (64)

/** Frames a wait renders at most */
#define WAIT_FRAMES (500)

/** Create a headless world that renders with the GPU on the main thread.
 *
 * @return The world
 */
static ecs_world_t* ibl_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    return world;
}

/** Turn a directional light to shine down from an elevation and a yaw.
 *
 * @param world The world
 * @param light The light
 * @param elevation Elevation of the direction toward the light in degrees
 * @param yaw Yaw of the light in degrees
 */
static void sun_turn(
    ecs_world_t *world,
    ecs_entity_t light,
    float elevation,
    float yaw)
{
    ecs_set(world, light, Rotation, {glm_rad(elevation), glm_rad(yaw), 0});
}

/** Create a scene with the default Atmosphere and a white sun.
 *
 * @param world The world
 * @param elevation Elevation of the sun in degrees
 * @param yaw Yaw of the sun in degrees
 * @param sun Receives the sun
 * @return The scene, the environment source
 */
static ecs_entity_t sky_new(
    ecs_world_t *world,
    float elevation,
    float yaw,
    ecs_entity_t *sun)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_add(world, scene, Atmosphere);
    *sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, *sun, DirectionalLight, {.intensity = 1});
    ecs_set(world, *sun, Rgb, {255, 255, 255});
    sun_turn(world, *sun, elevation, yaw);

    return scene;
}

/** Create a scene without light with a white diffuse sphere at the origin, seen by a view lit
 * only by an environment source, that renders into a surface.
 *
 * @param world The world
 * @param source The environment source
 * @param file Name of the image the surface writes
 * @return The scene
 */
static ecs_entity_t sphere_new(
    ecs_world_t *world,
    ecs_entity_t source,
    const char *file)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_entity_t sphere = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sphere, Sphere, {.segments = 48, .smooth = true, .radius = 1});
    ecs_set(world, sphere, Rgb, {255, 255, 255});
    ecs_set(world, sphere, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 0, 3.2f});
    ecs_set(world, camera, Camera, {.fov = 45, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true, .environment = {.source = source, .intensity = 1}}
    });
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = SIZE, .height = SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return scene;
}

/** Render a frame and wait until the GPU rendered it.
 *
 * @param world The world
 */
static void ibl_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Get what the GPU did with the cubemap of a source.
 *
 * @param world The world
 * @param source The source
 * @param out Receives the state
 * @return Whether the cubemap exists
 */
static bool ibl_state(
    ecs_world_t *world,
    ecs_entity_t source,
    box_render_environment_state_t *out)
{
    uint32_t handle = box_render_environment_find(world, source);

    return handle && box_render_gpu_environment_state(world, handle, out);
}

/** Render frames until the cubemap of a source was processed for the version of its
 * EnvironmentStatus.
 *
 * @param world The world
 * @param source The source
 * @return Whether it was processed within WAIT_FRAMES frames
 */
static bool ibl_wait(
    ecs_world_t *world,
    ecs_entity_t source)
{
    box_render_environment_state_t state;
    bool result = false;
    int32_t frame;
    for (frame = 0; frame < WAIT_FRAMES && !result; frame ++) {
        ibl_frame(world);
        int32_t version = ecs_get(world, source, EnvironmentStatus)->version;
        result = ibl_state(world, source, &state) && version && state.processed == version;
    }

    return result;
}

/** Get the EnvironmentStatus.version of a source.
 *
 * @param world The world
 * @param source The source
 * @return The version
 */
static int32_t ibl_version(
    ecs_world_t *world,
    ecs_entity_t source)
{
    return ecs_get(world, source, EnvironmentStatus)->version;
}

/** Check that the backend reported no error.
 *
 * @param world The world
 */
static void ibl_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

/** Load the image a surface wrote.
 *
 * @param world The world
 * @param file Name of the image
 * @return The RGBA pixels, SIZE by SIZE
 */
static unsigned char* ibl_image(
    ecs_world_t *world,
    const char *file)
{
    int w = 0, h = 0, n = 0;
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, SIZE);
    test_int(h, SIZE);
    ecs_os_free(path);

    return result;
}

/** Get the mean color of the pixels of a square of an image.
 *
 * @param pixels The image
 * @param x Column of the center of the square
 * @param y Row of the center of the square
 * @param out Receives red, green and blue, display encoded
 */
static void ibl_patch(
    const unsigned char *pixels,
    int32_t x,
    int32_t y,
    float out[3])
{
    int32_t i, j, c;
    out[0] = out[1] = out[2] = 0;
    for (j = y - 2; j <= y + 2; j ++) {
        for (i = x - 2; i <= x + 2; i ++) {
            for (c = 0; c < 3; c ++) {
                out[c] += (float)pixels[(j * SIZE + i) * 4 + c] / 25.0f;
            }
        }
    }
}

/** Sum the channels of a color.
 *
 * @param c The color
 * @return The sum
 */
static float ibl_sum(
    const float c[3])
{
    return c[0] + c[1] + c[2];
}

/* With the default ground_albedo and a high sun, the sunlit ground lights the underside of the
 * sphere about as much as the clear sky lights its top (the sky holds 6 to 9% of the sun's
 * irradiance, the ground reflects 7.4% of it), so the ground is darkened here. */
void AtmosphereIbl_noon_sphere_bluish_brighter_on_top(void) {
    // atmosphere/environment
    ecs_world_t *world = ibl_world();
    ecs_entity_t sun;
    ecs_entity_t sky = sky_new(world, 60, 30, &sun);
    ecs_get_mut(world, sky, Atmosphere)->ground_albedo = (rgb){40, 40, 40};
    ecs_modified(world, sky, Atmosphere);
    sphere_new(world, sky, "noon.png");
    test_assert(ibl_wait(world, sky));
    ibl_frame(world);
    ibl_frame(world);
    ibl_no_errors(world);

    unsigned char *pixels = ibl_image(world, "noon.png");
    float top[3], bottom[3], center[3];
    ibl_patch(pixels, SIZE / 2, SIZE / 2 - 18, top);
    ibl_patch(pixels, SIZE / 2, SIZE / 2 + 18, bottom);
    ibl_patch(pixels, SIZE / 2, SIZE / 2, center);
    printf("noon top %.0f %.0f %.0f, center %.0f %.0f %.0f, bottom %.0f %.0f %.0f\n",
        (double)top[0], (double)top[1], (double)top[2], (double)center[0],
        (double)center[1], (double)center[2], (double)bottom[0], (double)bottom[1],
        (double)bottom[2]);
    test_assert(ibl_sum(top) > 30);
    test_assert(ibl_sum(bottom) > 15);
    test_assert(top[2] > top[0] + 10);
    test_assert(center[2] > center[0]);
    test_assert(ibl_sum(top) > ibl_sum(bottom) * 1.2f);

    stbi_image_free(pixels);
    ecs_fini(world);
}

void AtmosphereIbl_sunset_side_toward_sun_warmer(void) {
    // atmosphere/environment
    ecs_world_t *world = ibl_world();
    ecs_entity_t sun;
    ecs_entity_t sky = sky_new(world, 3, 90, &sun);
    sphere_new(world, sky, "sunset.png");
    test_assert(ibl_wait(world, sky));
    ibl_frame(world);
    ibl_frame(world);
    ibl_no_errors(world);

    float dir[3], color[3];
    test_assert(box_render_lights_atmos_sun(box_render_lights_state(world), sky, dir, color));
    test_assert(fabsf(dir[0]) > 0.9f);
    unsigned char *pixels = ibl_image(world, "sunset.png");
    float toward[3], away[3];
    int32_t side = dir[0] > 0 ? 1 : -1;
    ibl_patch(pixels, SIZE / 2 + side * 18, SIZE / 2, toward);
    ibl_patch(pixels, SIZE / 2 - side * 18, SIZE / 2, away);
    printf("sunset toward %.0f %.0f %.0f, away %.0f %.0f %.0f\n", (double)toward[0],
        (double)toward[1], (double)toward[2], (double)away[0], (double)away[1],
        (double)away[2]);
    test_assert(ibl_sum(toward) > 6);
    test_assert(toward[0] / glm_max(toward[2], 1.0f) > away[0] / glm_max(away[2], 1.0f) * 1.1f);

    stbi_image_free(pixels);
    ecs_fini(world);
}

void AtmosphereIbl_version_follows_throttle(void) {
    // atmosphere/environment
    ecs_world_t *world = ibl_world();
    ecs_entity_t sun;
    ecs_entity_t sky = sky_new(world, 45, 0, &sun);
    sphere_new(world, sky, "version.png");
    box_render_environment_state_t state;
    int32_t i;
    test_assert(ibl_wait(world, sky));
    int32_t version = ibl_version(world, sky);
    test_assert(ibl_state(world, sky, &state));
    int32_t runs = state.runs;
    for (i = 0; i < 10; i ++) {
        ibl_frame(world);
        test_int(ibl_version(world, sky), version);
    }

    test_assert(ibl_state(world, sky, &state));
    test_int(state.runs, runs);
    test_int(state.written, version);

    sun_turn(world, sun, 46, 0);
    test_assert(ibl_wait(world, sky));
    test_int(ibl_version(world, sky), version + 1);
    test_assert(ibl_state(world, sky, &state));
    test_int(state.runs, runs + 1);

    sun_turn(world, sun, 46.2f, 0);
    for (i = 0; i < 5; i ++) {
        ibl_frame(world);
        test_int(ibl_version(world, sky), version + 1);
    }

    test_assert(ibl_state(world, sky, &state));
    test_int(state.runs, runs + 1);
    ibl_no_errors(world);
    ecs_fini(world);
}

/** Get the hemisphere SH the forward pass shades with as a + b y, as the probes of a volume
 * without captures hold it.
 *
 * @param sky Linear ambient light from above
 * @param ground Linear ambient light from below
 * @param out Receives the coefficients
 */
static void ibl_hemisphere(
    const float sky[3],
    const float ground[3],
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    double band0 = GLM_PI * 2.0 * sqrt(GLM_PI);
    double band1 = GLM_PI * 2.0 * sqrt(GLM_PI / 3.0);
    int32_t c;
    ecs_os_memset(out, 0, ECS_SIZEOF(float) * BOX_RENDER_SH_COEFFICIENTS * 4);
    for (c = 0; c < 3; c ++) {
        out[0][c] = (float)((sky[c] + ground[c]) * 0.5 * band0);
        out[1][c] = (float)((sky[c] - ground[c]) * 0.5 * band1);
    }
}

/** Get the largest difference of two SH over the largest magnitude of the first one.
 *
 * @param a The first SH
 * @param b The second SH
 * @return The relative difference
 */
static float ibl_sh_error(
    float a[BOX_RENDER_SH_COEFFICIENTS][4],
    float b[BOX_RENDER_SH_COEFFICIENTS][4])
{
    float largest = 0, diff = 0;
    int32_t k, c;
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        for (c = 0; c < 3; c ++) {
            largest = glm_max(largest, fabsf(a[k][c]));
            diff = glm_max(diff, fabsf(a[k][c] - b[k][c]));
        }
    }

    return diff / glm_max(largest, 1e-6f);
}

void AtmosphereIbl_bake_open_probe_matches_environment(void) {
    // environment/bake
    ecs_world_t *world = ibl_world();
    ecs_entity_t sun;
    ecs_entity_t sky = sky_new(world, 50, 20, &sun);
    ecs_entity_t scene = sphere_new(world, sky, "bake.png");
    ecs_set(world, scene, AmbientLight, {.sky = {255, 0, 0}, .ground = {0, 255, 0},
        .intensity = 1});
    ecs_entity_t marker = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, marker, Position, {0, 0, -6});
    ecs_set(world, marker, Box, {0.2f, 0.2f, 0.2f});
    ecs_add(world, marker, NoBake);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounces = 1, .capture_size = 16,
        .budget_ms = 2, .bounds_padding = 1, .environment = sky});
    int32_t frame;
    for (frame = 0; frame < WAIT_FRAMES; frame ++) {
        ibl_frame(world);
        const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
        int32_t version = ibl_version(world, sky);
        box_render_environment_state_t state;
        if (status->converged && version && ibl_state(world, sky, &state) &&
            state.processed == version && frame > 4)
        {
            break;
        }
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    ibl_no_errors(world);

    const box_bake_volume_t *v = box_bake_find(box_bake_state(world), scene);
    test_assert(v != NULL);
    test_assert(v->handle != 0);
    test_assert(v->probes > 0);
    float probe[BOX_RENDER_SH_COEFFICIENTS][4], env[BOX_RENDER_SH_COEFFICIENTS][4];
    float hemisphere[BOX_RENDER_SH_COEFFICIENTS][4];
    uint32_t far = 0;
    float distance = -1;
    int32_t i;
    for (i = 0; i < v->probes; i ++) {
        vec3 pos;
        box_bake_probe_position(v, (uint32_t)i, pos);
        float d = glm_vec3_norm(pos);
        if (box_bake_valid(v, (uint32_t)i) && d > distance) {
            distance = d;
            far = (uint32_t)i;
        }
    }

    test_assert(distance > 1.5f);
    test_assert(box_render_gpu_read_volume_sh(world, v->handle, (uint32_t)v->display, far,
        probe));
    test_assert(box_render_gpu_read_environment_sh(world,
        box_render_environment_find(world, sky), env));
    ibl_hemisphere((float[3]){1, 0, 0}, (float[3]){0, 1, 0}, hemisphere);
    float to_env = ibl_sh_error(env, probe);
    float to_hemisphere = ibl_sh_error(hemisphere, probe);
    printf("open probe: %.4f from the environment SH, %.4f from the hemisphere SH\n",
        (double)to_env, (double)to_hemisphere);
    test_assert(env[0][2] > 0);
    test_assert(to_env < 0.05f);
    test_assert(to_hemisphere > 0.5f);

    ecs_fini(world);
}

/* Above the horizon the environment holds the sky the views draw: a view that draws the
 * atmosphere and one that draws the environment as its background match, the disk off. */
void AtmosphereIbl_background_matches_sky(void) {
    // atmosphere/environment
    unsigned char *images[2];
    int32_t i, largest = 0, sum = 0;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ibl_world();
        ecs_entity_t sun;
        ecs_entity_t sky = sky_new(world, 45, 30, &sun);
        ecs_get_mut(world, sky, Atmosphere)->sun_disk_intensity = 0;
        ecs_modified(world, sky, Atmosphere);
        ecs_entity_t scene = sky;
        if (i == 1) {
            scene = ecs_new(world);
            ecs_add(world, scene, Scene);
        }

        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Rotation, {glm_rad(55), glm_rad(40), 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {
            .camera = camera,
            .clear = {.enabled = true, .color = {0, 0, 0, 255}},
            .lighting = {.enabled = true, .atmosphere = true, .environment = {
                .source = i ? sky : 0, .intensity = 1, .background = true}}
        });
        ecs_entity_t surface = ecs_new(world);
        ecs_set(world, surface, Surface, {.width = SIZE, .height = SIZE,
            .write_to_file = "background.png", .resolution_scale = 1});
        test_assert(ibl_wait(world, sky));
        ibl_frame(world);
        ibl_frame(world);
        ibl_no_errors(world);
        images[i] = ibl_image(world, "background.png");
        ecs_fini(world);
    }

    for (i = 0; i < SIZE * SIZE * 4; i ++) {
        if (i % 4 != 3) {
            int32_t d = abs((int32_t)images[0][i] - (int32_t)images[1][i]);
            largest = d > largest ? d : largest;
            sum += images[0][i];
        }
    }

    printf("background against sky: largest difference %d, mean %.1f\n", largest,
        (double)sum / (SIZE * SIZE * 3));
    test_assert(sum > SIZE * SIZE * 3 * 20);
    test_assert(largest <= 4);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

void AtmosphereIbl_reflection_capture_sees_environment(void) {
    // environment/bake
    ecs_world_t *world = ibl_world();
    ecs_entity_t sun;
    ecs_entity_t sky = sky_new(world, 30, 60, &sun);
    ecs_set(world, sky, Environment, {.resolution = 16, .filter = EnvironmentFilterBox});
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, AmbientLight, {.sky = {255, 0, 0}, .ground = {0, 255, 0},
        .intensity = 1});
    ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_entity_t marker = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, marker, Box, {0.2f, 0.2f, 0.2f});
    ecs_add(world, marker, NoBake);
    ecs_set(world, scene, ReflectionBake, {.resolution = 16, .budget_ms = 1,
        .scene_probe = true, .scene_probe_height = 2, .environment = sky});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 0, 3});
    ecs_set(world, camera, Camera, {.fov = 45, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting = {.enabled = true,
        .reflections = true}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = SIZE, .height = SIZE, .resolution_scale = 1});
    int32_t frame;
    for (frame = 0; frame < WAIT_FRAMES; frame ++) {
        ibl_frame(world);
        int32_t version = ibl_version(world, sky);
        box_render_environment_state_t state;
        if (ecs_get(world, scene, ReflectionBakeStatus)->converged && version &&
            ibl_state(world, sky, &state) && state.processed == version && frame > 4)
        {
            break;
        }
    }

    test_bool(ecs_get(world, scene, ReflectionBakeStatus)->converged, true);
    ibl_no_errors(world);
    int32_t i, count = 0, slot = -1;
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(world, scene,
        &count);
    for (i = 0; i < count; i ++) {
        if (probes[i].scene) {
            slot = (int32_t)probes[i].slot;
        }
    }

    test_assert(slot >= 0);
    float captured[16 * 16 * 4], env[16 * 16 * 4];
    uint32_t face;
    float largest = 0, diff = 0;
    for (face = 0; face < 6; face ++) {
        test_assert(box_render_gpu_read_reflection(world,
            box_render_reflect_texture(world, scene), (uint32_t)slot, face, captured,
            16 * 16 * 4));
        test_assert(box_render_gpu_read_environment(world,
            box_render_environment_find(world, sky), face, env, 16 * 16 * 4));
        for (i = 0; i < 16 * 16 * 4; i ++) {
            if (i % 4 != 3) {
                largest = glm_max(largest, env[i]);
                diff = glm_max(diff, fabsf(env[i] - captured[i]));
            }
        }
    }

    printf("reflection capture against environment: largest %.4f, difference %.5f\n",
        (double)largest, (double)diff);
    test_assert(largest > 0.01f);
    test_assert(diff <= largest * 0.01f);
    ecs_fini(world);
}
