/**
 * @file WaterScatter.c
 * @brief Tests for Water.scatter: the sunlight scattered inside the body glows through the
 * crests of the WaterWaves seen against a low sun, checked on PNG captures.
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
#define SCATTER_SIZE (128)

/** First row of the captures that shows water well below the horizon */
#define SCATTER_FIRST_ROW (40)

/** Create a headless world that renders on the main thread with a dim ambient light, a sun 6
 * degrees above the horizon and a camera 2 units above the water looking 20 degrees down along
 * -z.
 *
 * @param behind Whether the sun stands behind the camera instead of in front of it
 * @return The world
 */
static ecs_world_t* scatter_world(
    bool behind)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.reflections = true, .environment = true,
        .shader_features.Extended.transmission = true, .shader_features.Extended.fresnel = true,
        .shader_features.Extended.water = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {40, 50, 60}, .ground = {40, 50, 60},
        .intensity = 0.2f});

    /* The light travels along local +Z: a small pitch turns it down toward +z, so the sun
     * stands in front of the camera; pi minus it turns it toward -z, behind the camera */
    float low = glm_rad(6.0f);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {behind ? GLM_PIf - low : low, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, 0});
    ecs_set(world, camera, Rotation, {-glm_rad(20.0f), 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 5000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Add the water plane y = 0: a large quad facing up with a rough (0.3) Water of a dark deep
 * body without foam or refraction, under a swell travelling toward the camera.
 *
 * @param world The world
 * @param water The Water, its colors and scatter
 * @return The plane
 */
static ecs_entity_t scatter_plane(
    ecs_world_t *world,
    const Water *water)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {4000, 4000});
    ecs_set(world, e, Rgb, {20, 20, 20});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.3f});
    ecs_set_ptr(world, e, Water, water);

    WaterWaves w = { .detail = 1 };
    w.waves[0] = (WaterWave){ .direction = {0, 1}, .wavelength = 3,
        .amplitude = 0.15f, .steepness = 0.5f, .speed = 1e-4f };
    w.waves[1] = (WaterWave){ .direction = {0.6f, 0.8f}, .wavelength = 2,
        .amplitude = 0.06f, .steepness = 0.3f, .speed = 1e-4f };
    ecs_set_ptr(world, e, WaterWaves, &w);

    return e;
}

/** Get a dark deep Water that hides what is behind it, with a scatter.
 *
 * @param scatter Water.scatter
 * @return The Water
 */
static Water scatter_water(
    float scatter)
{
    return (Water){
        .shallow_color = {8, 30, 45},
        .deep_color = {8, 30, 45},
        .depth = 3,
        .visibility = 0.001f,
        .absorption = {255, 255, 255},
        .refraction = 0,
        .foam_color = {255, 255, 255},
        .foam_width = 0,
        .foam_scale = 1,
        .scatter_color = {40, 200, 170},
        .scatter = scatter
    };
}

/** Run frames of 1/60 s without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void scatter_frames(
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

/** Render a world with a Water plane into a capture, load it and free the world.
 *
 * @param behind Whether the sun stands behind the camera
 * @param water The Water of the plane
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* scatter_render(
    bool behind,
    const Water *water,
    const char *file)
{
    ecs_world_t *world = scatter_world(behind);
    scatter_plane(world, water);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = SCATTER_SIZE, .height = SCATTER_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    scatter_frames(world, 4);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, SCATTER_SIZE);
    test_int(h, SCATTER_SIZE);
    ecs_os_free(path);
    ecs_fini(world);

    return px;
}

/** Get the mean of a channel of b minus a over the water rows.
 *
 * @param a The first capture
 * @param b The second capture
 * @param c The channel, 0 red, 1 green, 2 blue
 * @return The mean signed difference
 */
static float scatter_gain(
    const unsigned char *a,
    const unsigned char *b,
    int32_t c)
{
    double sum = 0;
    int32_t x, y;
    for (y = SCATTER_FIRST_ROW; y < SCATTER_SIZE; y ++) {
        for (x = 0; x < SCATTER_SIZE; x ++) {
            int32_t i = (y * SCATTER_SIZE + x) * 4 + c;
            sum += (double)b[i] - (double)a[i];
        }
    }

    return (float)(sum / ((SCATTER_SIZE - SCATTER_FIRST_ROW) * SCATTER_SIZE));
}

/** Get the largest increase of green from a to b of a pixel of the water rows.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The largest increase
 */
static int32_t scatter_peak(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t x, y, peak = 0;
    for (y = SCATTER_FIRST_ROW; y < SCATTER_SIZE; y ++) {
        for (x = 0; x < SCATTER_SIZE; x ++) {
            int32_t i = (y * SCATTER_SIZE + x) * 4 + 1;
            int32_t d = (int32_t)b[i] - (int32_t)a[i];
            peak = d > peak ? d : peak;
        }
    }

    return peak;
}

/** Get the mean absolute difference of two captures per channel over the whole image.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The mean difference of red, green and blue
 */
static float scatter_difference(
    const unsigned char *a,
    const unsigned char *b)
{
    double sum = 0;
    int32_t i;
    for (i = 0; i < SCATTER_SIZE * SCATTER_SIZE; i ++) {
        int32_t c;
        for (c = 0; c < 3; c ++) {
            sum += fabs((double)a[i * 4 + c] - (double)b[i * 4 + c]);
        }
    }

    return (float)(sum / (SCATTER_SIZE * SCATTER_SIZE * 3));
}

void WaterScatter_crests_glow_toward_sun(void) {
    /* Looking toward a sun 6 degrees above the horizon, the crests of the swell glow turquoise
     * with scatter 1: green rises more than red, and some crest pixels rise a lot */
    Water none = scatter_water(0);
    Water lit = scatter_water(1);
    unsigned char *a = scatter_render(false, &none, "scatter_toward_none.png");
    unsigned char *b = scatter_render(false, &lit, "scatter_toward_lit.png");

    float green = scatter_gain(a, b, 1);
    float red = scatter_gain(a, b, 0);
    int32_t peak = scatter_peak(a, b);
    printf("toward the sun: green gain %f, red gain %f, peak %d\n", green, red, peak);
    test_assert(green > 3);
    test_assert(green > red * 2);
    test_assert(peak > 20);

    stbi_image_free(a);
    stbi_image_free(b);
}

void WaterScatter_sun_behind_small(void) {
    /* With the sun behind the camera no crest is seen against it: scatter 1 adds only the
     * faint glow of the faces turned to the viewer, much less than toward the sun */
    Water none = scatter_water(0);
    Water lit = scatter_water(1);
    unsigned char *a = scatter_render(true, &none, "scatter_behind_none.png");
    unsigned char *b = scatter_render(true, &lit, "scatter_behind_lit.png");
    unsigned char *c = scatter_render(false, &none, "scatter_front_none.png");
    unsigned char *d = scatter_render(false, &lit, "scatter_front_lit.png");

    float behind = scatter_gain(a, b, 1);
    float toward = scatter_gain(c, d, 1);
    printf("green gain: sun behind %f, toward the sun %f\n", behind, toward);
    test_assert(behind >= -0.5f);
    test_assert(behind < 3);
    test_assert(behind * 3 < toward);

    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
    stbi_image_free(d);
}

void WaterScatter_zero_unchanged(void) {
    /* A scatter of 0 draws exactly as a Water without one, whatever its scatter color */
    Water plain = scatter_water(0);
    plain.scatter_color = (Rgb){0, 0, 0};
    Water colored = scatter_water(0);
    colored.scatter_color = (Rgb){255, 0, 255};
    unsigned char *a = scatter_render(false, &plain, "scatter_zero_plain.png");
    unsigned char *b = scatter_render(false, &colored, "scatter_zero_colored.png");

    float d = scatter_difference(a, b);
    printf("scatter 0 difference %f\n", d);
    test_assert(d == 0);

    stbi_image_free(a);
    stbi_image_free(b);
}

void WaterScatter_without_waves_faint(void) {
    /* Flat water has no crests: toward the sun scatter 1 adds only the faint glow of the faces
     * turned to the viewer */
    Water none = scatter_water(0);
    Water lit = scatter_water(1);
    ecs_world_t *world;
    unsigned char *px[2];
    const Water *waters[2] = { &none, &lit };
    const char *files[2] = { "scatter_flat_none.png", "scatter_flat_lit.png" };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        world = scatter_world(false);
        ecs_entity_t plane = scatter_plane(world, waters[i]);
        ecs_remove(world, plane, WaterWaves);
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = SCATTER_SIZE, .height = SCATTER_SIZE,
            .write_to_file = ECS_CONST_CAST(char*, files[i]), .resolution_scale = 1});
        scatter_frames(world, 4);
        char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir,
            files[i]);
        int w = 0, h = 0, n = 0;
        px[i] = stbi_load(path, &w, &h, &n, 4);
        test_assert(px[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    float green = scatter_gain(px[0], px[1], 1);
    printf("flat water green gain %f\n", green);
    test_assert(green >= -0.5f);
    test_assert(green < 3);

    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}
