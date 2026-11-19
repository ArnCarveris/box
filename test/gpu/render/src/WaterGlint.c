/**
 * @file WaterGlint.c
 * @brief Tests for the specular antialiasing of a Water: the slope variance of the WaterWaves
 * that fade out with distance widens its roughness, checked on PNG captures.
 *
 * Without the widening, waves that fade out leave far water flat, so the same Water without
 * WaterWaves shows how far water looked before: one sharp sun highlight.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the square captures */
#define GLINT_SIZE (128)

/** Sum of red, green and blue above which a pixel belongs to the sun highlight */
#define GLINT_BRIGHT (600.0f)

/** Create a headless world that renders on the main thread with no ambient light, a sun of a
 * pitch and a camera at a position and pitch, both looking along -z.
 *
 * @param sun_pitch Pitch of the sun, the angle its light comes down at, radians
 * @param eye Height of the camera above the water
 * @param pitch Pitch of the camera, negative looks down, radians
 * @return The world
 */
static ecs_world_t* glint_world(
    float sun_pitch,
    float eye,
    float pitch)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.reflections = true, .environment = true,
        .transmission = true, .fresnel = true, .water = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {0, 0, 0}, .ground = {0, 0, 0},
        .intensity = 0});

    /* The light travels along local +Z: the pitch turns it down, toward +z, so the sun stands
     * in front of the camera */
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {sun_pitch, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, eye, 0});
    ecs_set(world, camera, Rotation, {pitch, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 5000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Add the water plane y = 0, a large quad facing up with a smooth (roughness 0.04) Water of a
 * dark body that hides what is behind it, without foam or refraction.
 *
 * @param world The world
 * @return The plane
 */
static ecs_entity_t glint_plane(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {4000, 4000});
    ecs_set(world, e, Rgb, {20, 20, 20});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.04f});
    ecs_set(world, e, Water, {
        .shallow_color = {20, 20, 20},
        .deep_color = {20, 20, 20},
        .depth = 3,
        .visibility = 0.001f,
        .absorption = {255, 255, 255},
        .refraction = 0,
        .foam_color = {255, 255, 255},
        .foam_width = 0,
        .foam_scale = 1
    });

    return e;
}

/** Run frames of 1/60 s without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void glint_frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t before, after;
    bool started = box_render_gpu_stats(world, &before);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &after), true);
    if (started) {
        test_int(after.errors, before.errors);
    } else {
        test_int(after.errors, 0);
    }
}

/** Render frames into a capture and load it.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* glint_render(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = GLINT_SIZE, .height = GLINT_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    glint_frames(world, 4);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, GLINT_SIZE);
    test_int(h, GLINT_SIZE);
    ecs_os_free(path);

    return px;
}

/** Count the pixels of the sun highlight in a band of rows.
 *
 * @param px The pixels
 * @param first First row
 * @param last Last row, included
 * @return Pixels whose red, green and blue sum above GLINT_BRIGHT
 */
static int32_t glint_bright(
    const unsigned char *px,
    int32_t first,
    int32_t last)
{
    int32_t x, y, count = 0;
    for (y = first; y <= last; y ++) {
        for (x = 0; x < GLINT_SIZE; x ++) {
            const unsigned char *p = &px[(y * GLINT_SIZE + x) * 4];
            if ((float)p[0] + (float)p[1] + (float)p[2] > GLINT_BRIGHT) {
                count ++;
            }
        }
    }

    return count;
}

/** Get the mean absolute difference of two captures per channel.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The mean difference of red, green and blue
 */
static float glint_difference(
    const unsigned char *a,
    const unsigned char *b)
{
    double sum = 0;
    int32_t i;
    for (i = 0; i < GLINT_SIZE * GLINT_SIZE; i ++) {
        int32_t c;
        for (c = 0; c < 3; c ++) {
            sum += fabs((double)a[i * 4 + c] - (double)b[i * 4 + c]);
        }
    }

    return (float)(sum / (GLINT_SIZE * GLINT_SIZE * 3));
}

/** Get the WaterWaves of a Water seen from afar: short steep waves toward the sun and across.
 *
 * @return The waves
 */
static WaterWaves glint_far_waves(void)
{
    WaterWaves result = { .detail = 1 };
    result.waves[0] = (WaterWave){ .direction = {0, 1}, .wavelength = 0.5f,
        .amplitude = 0.025f, .steepness = 0, .speed = 1e-4f };
    result.waves[1] = (WaterWave){ .direction = {0.6f, 0.8f}, .wavelength = 0.35f,
        .amplitude = 0.015f, .steepness = 0, .speed = 1e-4f };

    return result;
}

void WaterGlint_far_glitter_wider(void) {
    /* A camera 2 units above the water looks 5 degrees down toward a sun 5 degrees above the
     * horizon: the highlight lies about 23 units away, near the middle row. Rows 50..80 show
     * water farther than about 8 units, where the waves are shorter than 2 pixels along the
     * view and fade out */
    float low = glm_rad(5.0f);
    ecs_world_t *world = glint_world(low, 2, -low);
    glint_plane(world);
    unsigned char *flat = glint_render(world, "glint_far_flat.png");
    ecs_fini(world);

    world = glint_world(low, 2, -low);
    ecs_entity_t plane = glint_plane(world);
    WaterWaves w = glint_far_waves();
    ecs_set_ptr(world, plane, WaterWaves, &w);
    unsigned char *waved = glint_render(world, "glint_far_waved.png");

    /* The faded waves widen the roughness: a broad glitter band instead of a sharp dot */
    int32_t flat_count = glint_bright(flat, 50, 80);
    int32_t waved_count = glint_bright(waved, 50, 80);
    printf("bright pixels far: flat %d, waved %d\n", flat_count, waved_count);
    test_assert(waved_count > flat_count * 2);
    test_assert(waved_count > flat_count + 20);

    stbi_image_free(flat);
    stbi_image_free(waved);
    ecs_fini(world);
}

void WaterGlint_near_unchanged(void) {
    /* A camera 3 units above the water looks straight down at the highlight of a sun almost
     * overhead; waves of 2 units, about 74 pixels, are resolved everywhere and add no
     * roughness, so gentle ones (slope 0.003) look like the flat water they barely tilt. A
     * widened roughness would about double the highlight */
    float overhead = GLM_PI_2f - 0.1f;
    ecs_world_t *world = glint_world(overhead, 3, -GLM_PI_2f);
    glint_plane(world);
    unsigned char *flat = glint_render(world, "glint_near_flat.png");
    ecs_fini(world);

    world = glint_world(overhead, 3, -GLM_PI_2f);
    ecs_entity_t plane = glint_plane(world);
    WaterWaves w = { .detail = 1 };
    w.waves[0] = (WaterWave){ .direction = {1, 0}, .wavelength = 2,
        .amplitude = 0.003f / (2 * GLM_PIf / 2), .steepness = 0, .speed = 1e-4f };
    ecs_set_ptr(world, plane, WaterWaves, &w);
    unsigned char *waved = glint_render(world, "glint_near_waved.png");

    int32_t flat_count = glint_bright(flat, 0, GLINT_SIZE - 1);
    int32_t waved_count = glint_bright(waved, 0, GLINT_SIZE - 1);
    float d = glint_difference(flat, waved);
    printf("bright pixels near: flat %d, waved %d, difference %f\n",
        flat_count, waved_count, d);
    test_assert(flat_count > 0);
    test_assert(abs(waved_count - flat_count) <= flat_count / 4 + 4);
    test_assert(d < 2);

    stbi_image_free(flat);
    stbi_image_free(waved);
    ecs_fini(world);
}
