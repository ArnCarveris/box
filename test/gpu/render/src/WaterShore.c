/**
 * @file WaterShore.c
 * @brief Tests for Water.shore_period and shore_reach: lines of foam roll in toward the shore and
 * wash back, checked on PNG captures taken at fixed render times.
 *
 * The cases look straight down from a camera 10 units above the water plane y = 0 into a square
 * capture of SHORE_SIZE pixels, with a 60 degree field of view: +x runs to the right of the image,
 * the row of z = 0 is the middle one. A gray slope below the water rises along +x as
 * y = -1 + x / 4, so the water is about 1 deep at x = 0 and meets the shore at x = 4. The body of
 * the water is a dark blue that hides the slope, the foam white, so the red of a pixel measures
 * its foam.
 *
 * Every frame advances the time by SHORE_DT, a power of two, so the render time of a capture is
 * exact and the captures of two worlds a whole number of periods apart see the same phase.
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
#define SHORE_SIZE (64)

/** Height of the camera above the water */
#define SHORE_EYE (10.0f)

/** Seconds of a frame */
#define SHORE_DT (0.25f)

/** Seconds between two waves of the cases */
#define SHORE_PERIOD (2.0f)

/** Rows of the strip measured above and below the middle row */
#define SHORE_ROWS (8)

/** Create a headless world that renders on the main thread with a white ambient and no sun, a
 * gray slope below the water and a camera looking straight down.
 *
 * @return The world
 */
static ecs_world_t* shore_world(void)
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
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255},
        .ground = {255, 255, 255}, .intensity = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, SHORE_EYE, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    /* The slope y = -1 + x / 4, a quad turned about z */
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, -1, 0});
    ecs_set(world, parent, Rotation, {0, 0, atanf(0.25f)});
    ecs_entity_t slope = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, slope, Position, {0, 0, 0});
    ecs_set(world, slope, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, slope, Quad, {30, 30});
    ecs_set(world, slope, Rgb, {128, 128, 128});
    ecs_set(world, slope, PbrMaterial, {.metallic = 0, .roughness = 1});

    return world;
}

/** Get the Water of the cases: a dark blue body that hides what is behind it, no foam band, no
 * refraction, white foam, and shore waves of the period and reach.
 *
 * @param period Water.shore_period
 * @param reach Water.shore_reach
 * @return The Water
 */
static Water shore_water(
    float period,
    float reach)
{
    return (Water){
        .shallow_color = {0, 0, 80},
        .deep_color = {0, 0, 80},
        .depth = 3,
        .visibility = 0.001f,
        .absorption = {255, 255, 255},
        .refraction = 0,
        .foam_color = {255, 255, 255},
        .foam_width = 0,
        .foam_texture = 0,
        .foam_scale = 1,
        .shore_period = period,
        .shore_reach = reach
    };
}

/** Run frames of SHORE_DT without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void shore_frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t before, after;
    bool started = box_render_gpu_stats(world, &before);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, SHORE_DT);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &after), true);
    if (started) {
        test_int(after.errors, before.errors);
    } else {
        test_int(after.errors, 0);
    }
}

/** Render a world with a water plane of a Water into a capture after a number of frames, load
 * it and free the world.
 *
 * @param water The Water of the plane
 * @param skip Frames of SHORE_DT run before the last 4, of which the last is captured
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* shore_render(
    const Water *water,
    int32_t skip,
    const char *file)
{
    ecs_world_t *world = shore_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {12, 12});
    ecs_set(world, e, Rgb, {255, 0, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.3f});
    ecs_set_ptr(world, e, Water, water);

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = SHORE_SIZE, .height = SHORE_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    shore_frames(world, skip + 4);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, SHORE_SIZE);
    test_int(h, SHORE_SIZE);
    ecs_os_free(path);
    ecs_fini(world);

    return px;
}

/** Get the column a point of the water plane at an x projects to.
 *
 * @param x X of the point
 * @return The column
 */
static int32_t shore_col(
    float x)
{
    float half = SHORE_EYE * tanf(GLM_PIf / 6.0f);

    return (int32_t)floorf((x / half * 0.5f + 0.5f) * (float)SHORE_SIZE);
}

/** Get the red of every pixel of the strip of rows around the middle one between two x, the
 * foam of the water there.
 *
 * @param px The capture
 * @param x0 X of the first column
 * @param x1 X of the last column
 * @param out Receives the reds, row by row
 * @return The number of pixels
 */
static int32_t shore_strip(
    const unsigned char *px,
    float x0,
    float x1,
    float *out)
{
    int32_t c0 = shore_col(x0), c1 = shore_col(x1), x, y, count = 0;
    test_assert(c0 >= 0 && c1 < SHORE_SIZE && c0 < c1);
    for (y = SHORE_SIZE / 2 - SHORE_ROWS; y <= SHORE_SIZE / 2 + SHORE_ROWS; y ++) {
        for (x = c0; x <= c1; x ++) {
            out[count ++] = (float)px[(y * SHORE_SIZE + x) * 4];
        }
    }

    return count;
}

/** Get the mean absolute difference of the foam of two captures over a strip.
 *
 * @param a The first capture
 * @param b The second capture
 * @param x0 X of the first column
 * @param x1 X of the last column
 * @return The mean difference of the red
 */
static float shore_difference(
    const unsigned char *a,
    const unsigned char *b,
    float x0,
    float x1)
{
    float ra[SHORE_SIZE * SHORE_SIZE], rb[SHORE_SIZE * SHORE_SIZE];
    int32_t n = shore_strip(a, x0, x1, ra), i;
    test_int(shore_strip(b, x0, x1, rb), n);
    double sum = 0;
    for (i = 0; i < n; i ++) {
        sum += fabs((double)ra[i] - (double)rb[i]);
    }

    return (float)(sum / n);
}

/** Get the mean foam of a capture over a strip: how much redder it is than a capture of the
 * same water without shore waves, whose body reflects some of the white ambient.
 *
 * @param px The capture
 * @param plain The capture without shore waves
 * @param x0 X of the first column
 * @param x1 X of the last column
 * @return The mean red above that of plain
 */
static float shore_coverage(
    const unsigned char *px,
    const unsigned char *plain,
    float x0,
    float x1)
{
    float r[SHORE_SIZE * SHORE_SIZE], rp[SHORE_SIZE * SHORE_SIZE];
    int32_t n = shore_strip(px, x0, x1, r), i;
    test_int(shore_strip(plain, x0, x1, rp), n);
    double sum = 0;
    for (i = 0; i < n; i ++) {
        sum += r[i] > rp[i] ? r[i] - rp[i] : 0;
    }

    return (float)(sum / n);
}

/** Get the mean absolute difference of two captures per channel over the whole image.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The mean difference of red, green and blue
 */
static float shore_image_difference(
    const unsigned char *a,
    const unsigned char *b)
{
    double sum = 0;
    int32_t i, c;
    for (i = 0; i < SHORE_SIZE * SHORE_SIZE; i ++) {
        for (c = 0; c < 3; c ++) {
            sum += fabs((double)a[i * 4 + c] - (double)b[i * 4 + c]);
        }
    }

    return (float)(sum / (SHORE_SIZE * SHORE_SIZE * 3));
}

void WaterShore_waves_move(void) {
    /* With a reach of 1 the strip from x = 0.5 to 3.7 lies inside it: a quarter of a period
     * later its foam has changed, a whole period later it is the same again */
    Water w = shore_water(SHORE_PERIOD, 1.0f);
    int32_t quarter = (int32_t)(SHORE_PERIOD / SHORE_DT / 4);
    int32_t whole = (int32_t)(SHORE_PERIOD / SHORE_DT);
    Water still = shore_water(0, 1.0f);
    unsigned char *plain = shore_render(&still, 0, "shore_move_plain.png");
    unsigned char *a = shore_render(&w, 0, "shore_move_a.png");
    unsigned char *b = shore_render(&w, quarter, "shore_move_b.png");
    unsigned char *c = shore_render(&w, whole, "shore_move_c.png");
    unsigned char *d = shore_render(&w, 2 * quarter, "shore_move_d.png");

    float ab = shore_difference(a, b, 0.5f, 3.7f);
    float ad = shore_difference(a, d, 0.5f, 3.7f);
    float ac = shore_difference(a, c, 0.5f, 3.7f);
    printf("foam coverage %f %f %f %f, quarter period %f, half %f, whole %f\n",
        shore_coverage(a, plain, 0.5f, 3.7f), shore_coverage(b, plain, 0.5f, 3.7f),
        shore_coverage(d, plain, 0.5f, 3.7f), shore_coverage(c, plain, 0.5f, 3.7f), ab, ad, ac);
    test_assert(ab > 5);
    test_assert(ad > 5);
    test_assert(ac < 0.5f);

    /* Some foam shows inside the reach in every capture */
    test_assert(shore_coverage(a, plain, 0.5f, 3.7f) > 5);
    test_assert(shore_coverage(b, plain, 0.5f, 3.7f) > 5);
    test_assert(shore_coverage(d, plain, 0.5f, 3.7f) > 5);

    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
    stbi_image_free(d);
    stbi_image_free(plain);
}

void WaterShore_beyond_reach_clear(void) {
    /* With a reach of 0.3 the water deeper than about 0.45 (x below 2) shows no foam at any
     * phase, while the shallows show some at one of them at least */
    Water w = shore_water(SHORE_PERIOD, 0.3f);
    int32_t step = (int32_t)(SHORE_PERIOD / SHORE_DT / 4);
    Water still = shore_water(0, 0.3f);
    unsigned char *plain = shore_render(&still, 0, "shore_reach_plain.png");
    float shallow = 0;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        char file[64];
        snprintf(file, sizeof(file), "shore_reach_%d.png", (int)i);
        unsigned char *px = shore_render(&w, i * step, file);
        float deep = shore_coverage(px, plain, -4.0f, 1.5f);
        float shallows = shore_coverage(px, plain, 3.0f, 3.8f);
        printf("phase %d: deep foam %f, shallow foam %f\n", (int)i, deep, shallows);
        test_assert(deep < 2);
        shallow = shallows > shallow ? shallows : shallow;
        stbi_image_free(px);
    }

    test_assert(shallow > 20);
    stbi_image_free(plain);
}

void WaterShore_zero_period_unchanged(void) {
    /* A shore_period of 0 draws exactly as before: whatever the reach and the time, the same as
     * a Water without shore waves */
    Water plain = shore_water(0, 0.3f);
    Water reach = shore_water(0, 2.0f);
    unsigned char *a = shore_render(&plain, 0, "shore_zero_a.png");
    unsigned char *b = shore_render(&reach, 3, "shore_zero_b.png");

    float d = shore_image_difference(a, b);
    printf("shore period 0 difference %f\n", d);
    test_assert(d == 0);

    stbi_image_free(a);
    stbi_image_free(b);
}
