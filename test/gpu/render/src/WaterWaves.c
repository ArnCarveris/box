/**
 * @file WaterWaves.c
 * @brief Tests for the travelling waves of a Water (WaterWaves, WaterFlow), checked on PNG
 * captures.
 *
 * The cases look straight down from a camera 10 units above the water plane y = 0 into a square
 * capture of WAVES_SIZE pixels, with a 60 degree field of view: +x runs to the right of the
 * image, every row shows the same range of x. The water hides everything behind it and has a
 * light gray body, lit only by a sun that comes down from +x at 45 degrees, so its brightness
 * follows the slope of the waves along x.
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
#define WAVES_SIZE (128)

/** Height of the camera above the water */
#define WAVES_EYE (10.0f)

/** Columns left out on each side of a row */
#define WAVES_MARGIN (8)

/** Create a headless world that renders on the main thread with a sun from +x at 45 degrees
 * and no ambient light.
 *
 * @param water Whether the scene enables Water (Scene.water)
 * @return The world
 */
static ecs_world_t* waves_world(
    bool water)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.reflections = true, .environment = true,
        .shader_features.Extended.transmission = true, .shader_features.Extended.fresnel = true,
        .shader_features.Extended.water = water});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {0, 0, 0}, .ground = {0, 0, 0},
        .intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {GLM_PI_4f, GLM_PI_2f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, WAVES_EYE, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Add the water plane y = 0, a 12x12 quad facing up that fills the capture, with a Water of a
 * light gray body that hides what is behind it, without foam or refraction.
 *
 * @param world The world
 * @param water Whether the plane has a Water
 * @return The plane
 */
static ecs_entity_t waves_plane(
    ecs_world_t *world,
    bool water)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {12, 12});
    ecs_set(world, e, Rgb, {200, 200, 200});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.6f});
    if (water) {
        ecs_set(world, e, Water, {
            .shallow_color = {200, 200, 200},
            .deep_color = {200, 200, 200},
            .depth = 3,
            .visibility = 0.001f,
            .absorption = {255, 255, 255},
            .refraction = 0,
            .foam_color = {255, 255, 255},
            .foam_width = 0,
            .foam_scale = 1
        });
    }

    return e;
}

/** Get WaterWaves of one wave along +x.
 *
 * @param wavelength World units between crests
 * @param speed World units per second the crests travel
 * @return The waves
 */
static WaterWaves waves_one(
    float wavelength,
    float speed)
{
    WaterWaves result = { .detail = 1 };
    result.waves[0] = (WaterWave){ .direction = {1, 0}, .wavelength = wavelength,
        .amplitude = 0.08f * wavelength, .steepness = 0, .speed = speed };

    return result;
}

/** Run frames of 1/60 s without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void waves_frames(
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

/** Add a surface that writes a capture every frame.
 *
 * @param world The world
 * @param file Name of the PNG
 */
static void waves_surface(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = WAVES_SIZE, .height = WAVES_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
}

/** Load the capture a surface wrote last.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* waves_load(
    ecs_world_t *world,
    const char *file)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, WAVES_SIZE);
    test_int(h, WAVES_SIZE);
    ecs_os_free(path);

    return px;
}

/** Render frames into a capture and load it.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* waves_render(
    ecs_world_t *world,
    const char *file)
{
    waves_surface(world, file);
    waves_frames(world, 4);

    return waves_load(world, file);
}

/** Get the brightness of a row of a capture, the sum of red, green and blue per column.
 *
 * @param px The pixels
 * @param row The row
 * @param out Receives WAVES_SIZE values
 */
static void waves_row(
    const unsigned char *px,
    int32_t row,
    float *out)
{
    int32_t x;
    for (x = 0; x < WAVES_SIZE; x ++) {
        const unsigned char *p = &px[(row * WAVES_SIZE + x) * 4];
        out[x] = (float)p[0] + (float)p[1] + (float)p[2];
    }
}

/** Get the pixels per world unit of the plane y = 0.
 *
 * @return The pixels
 */
static float waves_scale(void)
{
    float half = WAVES_EYE * tanf(GLM_PIf / 6.0f);

    return (float)WAVES_SIZE / (2.0f * half);
}

/** Get the mean difference of a row and another row moved by a number of columns: b[i] against
 * a[i - shift], over the columns inside the margins.
 *
 * @param a The first row
 * @param b The second row
 * @param shift Columns b is moved by
 * @return The mean absolute difference
 */
static float waves_shifted(
    const float *a,
    const float *b,
    int32_t shift)
{
    float sum = 0;
    int32_t i, count = 0;
    for (i = WAVES_MARGIN; i < WAVES_SIZE - WAVES_MARGIN; i ++) {
        int32_t j = i - shift;
        if (j < WAVES_MARGIN || j >= WAVES_SIZE - WAVES_MARGIN) {
            continue;
        }

        sum += fabsf(b[i] - a[j]);
        count ++;
    }

    return count ? sum / (float)count : 1e9f;
}

/** Find the shift of a periodic row against another, within half a period either way.
 *
 * @param a The first row
 * @param b The second row
 * @param period Columns of a period
 * @return The columns b is moved by against a
 */
static int32_t waves_shift(
    const float *a,
    const float *b,
    float period)
{
    int32_t s, best = 0, half = (int32_t)(period * 0.5f);
    float least = 1e9f;
    for (s = -half; s <= half; s ++) {
        float d = waves_shifted(a, b, s);
        if (d < least) {
            least = d;
            best = s;
        }
    }

    return best;
}

/** Get the range of the brightness of a row inside the margins.
 *
 * @param row The row
 * @return Its largest minus its smallest value
 */
static float waves_range(
    const float *row)
{
    float lo = 1e9f, hi = -1e9f;
    int32_t i;
    for (i = WAVES_MARGIN; i < WAVES_SIZE - WAVES_MARGIN; i ++) {
        lo = row[i] < lo ? row[i] : lo;
        hi = row[i] > hi ? row[i] : hi;
    }

    return hi - lo;
}

void WaterWaves_periodic_along_direction(void) {
    ecs_world_t *world = waves_world(true);
    ecs_entity_t plane = waves_plane(world, true);
    WaterWaves w = waves_one(3, 1e-4f);
    ecs_set_ptr(world, plane, WaterWaves, &w);
    unsigned char *px = waves_render(world, "waves_periodic.png");
    float row[WAVES_SIZE], other[WAVES_SIZE];
    waves_row(px, WAVES_SIZE / 2, row);
    waves_row(px, WAVES_SIZE / 2 + 24, other);

    /* The brightness follows the slope of the wave along x */
    float range = waves_range(row);
    float period = 3.0f * waves_scale();
    float at_period = waves_shifted(row, row, (int32_t)roundf(period));
    float at_half = waves_shifted(row, row, (int32_t)roundf(period * 0.5f));
    printf("range %f, period %f px: same %f, half %f\n", range, period, at_period, at_half);
    test_assert(range > 60);
    test_assert(at_period < at_half * 0.25f);
    test_assert(at_half > range * 0.3f);

    /* The crests run along z: the rows match */
    float across = waves_shifted(row, other, 0);
    printf("rows apart %f\n", across);
    test_assert(across < range * 0.15f);

    stbi_image_free(px);
    ecs_fini(world);
}

void WaterWaves_moves_over_time(void) {
    ecs_world_t *world = waves_world(true);
    ecs_entity_t plane = waves_plane(world, true);
    WaterWaves w = waves_one(4, 1);
    ecs_set_ptr(world, plane, WaterWaves, &w);
    waves_surface(world, "waves_time.png");
    waves_frames(world, 4);
    unsigned char *a = waves_load(world, "waves_time.png");

    /* Half a second at 1 unit per second moves the crests half a unit along +x */
    waves_frames(world, 30);
    unsigned char *b = waves_load(world, "waves_time.png");
    float ra[WAVES_SIZE], rb[WAVES_SIZE];
    waves_row(a, WAVES_SIZE / 2, ra);
    waves_row(b, WAVES_SIZE / 2, rb);
    float period = 4.0f * waves_scale();
    int32_t shift = waves_shift(ra, rb, period);
    float expected = 0.5f * waves_scale();
    printf("moved %d px, expected %f\n", shift, expected);
    test_assert(fabsf((float)shift - expected) <= 1.5f);
    test_assert(waves_shifted(ra, rb, 0) > waves_range(ra) * 0.1f);

    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(world);
}

void WaterWaves_flow_moves_pattern(void) {
    ecs_world_t *world = waves_world(true);
    ecs_entity_t plane = waves_plane(world, true);
    WaterWaves w = waves_one(4, 1e-4f);
    ecs_set_ptr(world, plane, WaterWaves, &w);
    waves_surface(world, "waves_still.png");
    waves_frames(world, 4);
    unsigned char *a = waves_load(world, "waves_still.png");

    /* Without a flow the crests (almost) stand still */
    waves_frames(world, 30);
    unsigned char *b = waves_load(world, "waves_still.png");
    float ra[WAVES_SIZE], rb[WAVES_SIZE];
    waves_row(a, WAVES_SIZE / 2, ra);
    waves_row(b, WAVES_SIZE / 2, rb);
    float period = 4.0f * waves_scale();
    int32_t shift = waves_shift(ra, rb, period);
    printf("still moved %d px\n", shift);
    test_int(shift, 0);

    /* A flow of -1.5 units per second along x carries them by velocity times time */
    ecs_set(world, plane, WaterFlow, {{-1.5f, 0}});
    waves_frames(world, 4);
    unsigned char *c = waves_load(world, "waves_still.png");
    waves_frames(world, 30);
    unsigned char *d = waves_load(world, "waves_still.png");
    float rc[WAVES_SIZE], rd[WAVES_SIZE];
    waves_row(c, WAVES_SIZE / 2, rc);
    waves_row(d, WAVES_SIZE / 2, rd);
    shift = waves_shift(rc, rd, period);
    float expected = -1.5f * 0.5f * waves_scale();
    printf("flow moved %d px, expected %f\n", shift, expected);
    test_assert(fabsf((float)shift - expected) <= 1.5f);

    /* A flow across the waves does not move them */
    ecs_set(world, plane, WaterFlow, {{0, 2}});
    waves_frames(world, 4);
    unsigned char *e = waves_load(world, "waves_still.png");
    waves_frames(world, 30);
    unsigned char *f = waves_load(world, "waves_still.png");
    float re[WAVES_SIZE], rf[WAVES_SIZE];
    waves_row(e, WAVES_SIZE / 2, re);
    waves_row(f, WAVES_SIZE / 2, rf);
    shift = waves_shift(re, rf, period);
    printf("across moved %d px\n", shift);
    test_int(shift, 0);

    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
    stbi_image_free(d);
    stbi_image_free(e);
    stbi_image_free(f);
    ecs_fini(world);
}

void WaterWaves_far_waves_fade(void) {
    /* A wavelength below 2 pixels fades out: the water looks flat */
    ecs_world_t *world = waves_world(true);
    ecs_entity_t plane = waves_plane(world, true);
    WaterWaves w = waves_one(1.5f / waves_scale(), 1e-4f);
    ecs_set_ptr(world, plane, WaterWaves, &w);
    unsigned char *tiny = waves_render(world, "waves_tiny.png");
    ecs_fini(world);

    world = waves_world(true);
    waves_plane(world, true);
    unsigned char *flat = waves_render(world, "waves_flat.png");
    float rt[WAVES_SIZE], rf[WAVES_SIZE];
    waves_row(tiny, WAVES_SIZE / 2, rt);
    waves_row(flat, WAVES_SIZE / 2, rf);
    float d = waves_shifted(rf, rt, 0);
    printf("tiny against flat %f, range %f\n", d, waves_range(rt));
    test_assert(d < 3);

    stbi_image_free(tiny);
    stbi_image_free(flat);
    ecs_fini(world);
}

void WaterWaves_without_waves_unchanged(void) {
    /* A Water that lost its WaterWaves draws exactly as one that never had them */
    ecs_world_t *world = waves_world(true);
    waves_plane(world, true);
    unsigned char *plain = waves_render(world, "waves_plain.png");
    ecs_fini(world);

    world = waves_world(true);
    ecs_entity_t plane = waves_plane(world, true);
    WaterWaves w = waves_one(3, 1);
    ecs_set_ptr(world, plane, WaterWaves, &w);
    ecs_set(world, plane, WaterFlow, {{1, 0}});
    waves_surface(world, "waves_removed.png");
    waves_frames(world, 4);
    unsigned char *waved = waves_load(world, "waves_removed.png");
    test_assert(memcmp(plain, waved, WAVES_SIZE * WAVES_SIZE * 4) != 0);
    ecs_remove(world, plane, WaterWaves);
    waves_frames(world, 4);
    unsigned char *removed = waves_load(world, "waves_removed.png");
    test_int(memcmp(plain, removed, WAVES_SIZE * WAVES_SIZE * 4), 0);

    stbi_image_free(plain);
    stbi_image_free(waved);
    stbi_image_free(removed);
    ecs_fini(world);
}

void WaterWaves_ignored_without_water(void) {
    /* WaterWaves on a plane without Water, or in a scene without Scene.water, do nothing */
    ecs_world_t *world = waves_world(true);
    waves_plane(world, false);
    unsigned char *plain = waves_render(world, "waves_no_water.png");
    ecs_fini(world);

    world = waves_world(true);
    ecs_entity_t plane = waves_plane(world, false);
    WaterWaves w = waves_one(3, 1);
    ecs_set_ptr(world, plane, WaterWaves, &w);
    unsigned char *waved = waves_render(world, "waves_no_water_waved.png");
    test_int(memcmp(plain, waved, WAVES_SIZE * WAVES_SIZE * 4), 0);
    ecs_fini(world);

    world = waves_world(false);
    waves_plane(world, true);
    unsigned char *off = waves_render(world, "waves_off.png");
    ecs_fini(world);

    world = waves_world(false);
    plane = waves_plane(world, true);
    ecs_set_ptr(world, plane, WaterWaves, &w);
    unsigned char *off_waved = waves_render(world, "waves_off_waved.png");
    test_int(memcmp(off, off_waved, WAVES_SIZE * WAVES_SIZE * 4), 0);

    stbi_image_free(plain);
    stbi_image_free(waved);
    stbi_image_free(off);
    stbi_image_free(off_waved);
    ecs_fini(world);
}
