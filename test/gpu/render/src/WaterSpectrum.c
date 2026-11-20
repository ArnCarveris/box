/**
 * @file WaterSpectrum.c
 * @brief Tests for the spectra of WaterSpectrum on the GPU: the inverse FFT of the spectrum
 * shader (box_render_gpu_spectrum_fft), the slopes read back from a spectrum
 * (box_render_gpu_read_spectrum) and PNG captures of a Water with a spectrum.
 *
 * The cases look straight down from a camera 10 units above the water plane y = 0 into a square
 * capture of SPECTRUM_SIZE pixels, lit only by a sun that comes down from +x at 45 degrees, so the
 * brightness of the water follows the slope of its waves.
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
#define SPECTRUM_SIZE (128)

/** Seconds of a frame, exact in binary so the render time adds up exactly */
#define SPECTRUM_DT (1.0f / 64)

/** 2 pi */
#define TAU (6.283185307179586)

/** Create a headless world that renders on the main thread with Scene.water, a sun from +x at
 * 45 degrees, no ambient light and a camera above the origin looking down.
 *
 * @param water Whether the scene enables Water (Scene.water)
 * @return The world
 */
static ecs_world_t* spectrum_world(
    bool water)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.reflections = true, .environment = true,
        .transmission = true, .fresnel = true, .water = water});
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
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Add the water plane y = 0, a 12x12 quad facing up that fills the capture, with a Water of a
 * light gray body that hides what is behind it, without foam or refraction, and a WaterSpectrum.
 *
 * @param world The world
 * @param wind_speed WaterSpectrum.wind_speed
 * @return The plane
 */
static ecs_entity_t spectrum_plane(
    ecs_world_t *world,
    float wind_speed)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {12, 12});
    ecs_set(world, e, Rgb, {200, 200, 200});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.3f});
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
    ecs_set(world, e, WaterSpectrum, {.wind_speed = wind_speed, .wind_direction = {1, 0},
        .size = 16, .cascades = 2, .resolution = 128, .directionality = 0.8f,
        .amplitude = 1, .seed = 3, .detail = 0});

    return e;
}

/** Run frames of SPECTRUM_DT without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void spectrum_frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t before, after;
    bool started = box_render_gpu_stats(world, &before);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, SPECTRUM_DT);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &after), true);
    if (started) {
        test_int(after.errors, before.errors);
    } else {
        test_int(after.errors, 0);
    }
}

/** Add a surface that writes a capture every frame, or one that only renders.
 *
 * @param world The world
 * @param file Name of the PNG, or NULL to write none
 */
static void spectrum_surface(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = SPECTRUM_SIZE, .height = SPECTRUM_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
}

/** Load the capture a surface wrote last.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* spectrum_load(
    ecs_world_t *world,
    const char *file)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, SPECTRUM_SIZE);
    test_int(h, SPECTRUM_SIZE);
    ecs_os_free(path);

    return px;
}

/** Get the mean absolute difference of the red, green and blue of two captures, and the largest.
 *
 * @param a The first capture
 * @param b The second capture
 * @param most Receives the largest difference of a channel
 * @return The mean difference per channel
 */
static float spectrum_difference(
    const unsigned char *a,
    const unsigned char *b,
    int32_t *most)
{
    double sum = 0;
    int32_t i, c;
    *most = 0;
    for (i = 0; i < SPECTRUM_SIZE * SPECTRUM_SIZE; i ++) {
        for (c = 0; c < 3; c ++) {
            int32_t d = abs((int32_t)a[i * 4 + c] - (int32_t)b[i * 4 + c]);
            sum += d;
            *most = d > *most ? d : *most;
        }
    }

    return (float)(sum / (SPECTRUM_SIZE * SPECTRUM_SIZE * 3));
}

/** Get the range of the brightness of a capture, the sum of red, green and blue per pixel.
 *
 * @param px The capture
 * @return Its largest minus its smallest value
 */
static int32_t spectrum_range(
    const unsigned char *px)
{
    int32_t lo = 1 << 20, hi = 0, i;
    for (i = 0; i < SPECTRUM_SIZE * SPECTRUM_SIZE; i ++) {
        int32_t v = px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2];
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }

    return hi - lo;
}

/** The statistics of the slopes of a cascade read back from the GPU */
typedef struct spectrum_stats_t {
    double mean[2];     /**< Mean slope along X and Z */
    double variance;    /**< Variance of the slope along X plus that along Z */
    double height;      /**< Variance of the height */
    int32_t resolution; /**< Texels per side */
} spectrum_stats_t;

/** Read a cascade of the spectrum of a Water and get the statistics of its slopes.
 *
 * @param world The world
 * @param water The entity that owns the WaterSpectrum
 * @param cascade The cascade
 * @return The statistics
 */
static spectrum_stats_t spectrum_read(
    ecs_world_t *world,
    ecs_entity_t water,
    int32_t cascade)
{
    spectrum_stats_t result = {0};
    int32_t count = 256 * 256 * 4, i, n;
    float *texels = ecs_os_malloc_n(float, count);
    test_bool(box_render_gpu_read_spectrum(world, water, cascade, &result.resolution, texels,
        count), true);
    n = result.resolution * result.resolution;
    test_assert(n > 0);
    double sx = 0, sz = 0, sxx = 0, szz = 0, h = 0, hh = 0;
    for (i = 0; i < n; i ++) {
        const float *t = &texels[i * 4];
        test_assert(isfinite(t[0]) && isfinite(t[1]) && isfinite(t[2]) && isfinite(t[3]));
        sx += t[0];
        sz += t[1];
        sxx += (double)t[0] * t[0];
        szz += (double)t[1] * t[1];
        h += t[2];
        hh += (double)t[2] * t[2];
    }

    result.mean[0] = sx / n;
    result.mean[1] = sz / n;
    result.variance = sxx / n - result.mean[0] * result.mean[0] +
        szz / n - result.mean[1] * result.mean[1];
    result.height = hh / n - (h / n) * (h / n);
    ecs_os_free(texels);

    return result;
}

void WaterSpectrum_fft_single_frequency(void) {
    ecs_world_t *world = spectrum_world(true);
    spectrum_plane(world, 4);
    spectrum_surface(world, NULL);
    spectrum_frames(world, 4);

    /* One frequency of 3 cycles along X in the first value and one of 5 cycles along Z with half
     * the amplitude in the second: cosine and sine along X, a cosine along Z */
    int32_t n = 64, x, z;
    float *in = ecs_os_calloc_n(float, n * n * 4);
    float *out = ecs_os_calloc_n(float, n * n * 4);
    in[(0 * n + 3) * 4 + 0] = 1.0f;
    in[(5 * n + 0) * 4 + 2] = 0.5f;
    test_bool(box_render_gpu_spectrum_fft(world, n, in, out), true);

    float worst = 0;
    for (z = 0; z < n; z ++) {
        for (x = 0; x < n; x ++) {
            const float *t = &out[(z * n + x) * 4];
            float re = (float)cos(TAU * 3 * x / n), im = (float)sin(TAU * 3 * x / n);
            float h = 0.5f * (float)cos(TAU * 5 * z / n);
            float e = fabsf(t[0] - re);
            e = fmaxf(e, fabsf(t[1] - im));
            e = fmaxf(e, fabsf(t[2] - h));
            e = fmaxf(e, fabsf(t[3] - 1.0f));
            worst = fmaxf(worst, e);
        }
    }

    printf("largest error %f\n", worst);
    test_assert(worst < 4e-3f);

    /* The largest resolution too: a frequency of -1 cycles along X and 2 along Z */
    n = 256;
    ecs_os_free(in);
    ecs_os_free(out);
    in = ecs_os_calloc_n(float, n * n * 4);
    out = ecs_os_calloc_n(float, n * n * 4);
    in[(2 * n + (n - 1)) * 4 + 0] = 1.0f;
    test_bool(box_render_gpu_spectrum_fft(world, n, in, out), true);
    worst = 0;
    for (z = 0; z < n; z ++) {
        for (x = 0; x < n; x ++) {
            const float *t = &out[(z * n + x) * 4];
            double a = TAU * (-x + 2 * z) / n;
            worst = fmaxf(worst, fabsf(t[0] - (float)cos(a)));
            worst = fmaxf(worst, fabsf(t[1] - (float)sin(a)));
            worst = fmaxf(worst, fabsf(t[2]));
        }
    }

    printf("largest error at 256 %f\n", worst);
    test_assert(worst < 4e-3f);

    ecs_os_free(in);
    ecs_os_free(out);
    ecs_fini(world);
}

void WaterSpectrum_slopes_mean_zero(void) {
    ecs_world_t *world = spectrum_world(true);
    ecs_entity_t plane = spectrum_plane(world, 4);
    spectrum_surface(world, NULL);
    spectrum_frames(world, 4);

    int32_t c;
    for (c = 0; c < 2; c ++) {
        spectrum_stats_t s = spectrum_read(world, plane, c);
        test_int(s.resolution, 128);
        printf("cascade %d: mean %f %f, slope variance %f, height variance %f\n", c,
            s.mean[0], s.mean[1], s.variance, s.height);
        test_assert(s.variance > 1e-6);
        test_assert(fabs(s.mean[0]) < 0.05 * sqrt(s.variance) + 1e-4);
        test_assert(fabs(s.mean[1]) < 0.05 * sqrt(s.variance) + 1e-4);

        /* The variance of the slopes is about what the spectrum expects */
        box_render_water_spectrum_t r = {0};
        box_render_spectrum_params(ecs_get(world, plane, WaterSpectrum), &r);
        double expect = box_render_spectrum_variance(&r, c, true);
        printf("expected slope variance %f\n", expect);
        test_assert(s.variance > 0.3 * expect);
        test_assert(s.variance < 3.0 * expect);
    }

    ecs_fini(world);
}

void WaterSpectrum_variance_grows_with_wind(void) {
    ecs_world_t *world = spectrum_world(true);
    ecs_entity_t plane = spectrum_plane(world, 3);
    spectrum_surface(world, NULL);
    spectrum_frames(world, 4);
    spectrum_stats_t calm = spectrum_read(world, plane, 0);

    WaterSpectrum *s = ecs_ensure(world, plane, WaterSpectrum);
    s->wind_speed = 9;
    ecs_modified(world, plane, WaterSpectrum);
    spectrum_frames(world, 4);
    spectrum_stats_t windy = spectrum_read(world, plane, 0);
    printf("slope variance %f at 3 m/s, %f at 9 m/s; height variance %f, %f\n",
        calm.variance, windy.variance, calm.height, windy.height);
    test_assert(windy.variance > calm.variance * 1.2);
    test_assert(windy.height > calm.height * 2);

    ecs_fini(world);
}

void WaterSpectrum_changes_over_time(void) {
    ecs_world_t *world = spectrum_world(true);
    spectrum_plane(world, 6);
    spectrum_surface(world, "spectrum_time.png");
    spectrum_frames(world, 4);
    unsigned char *a = spectrum_load(world, "spectrum_time.png");
    spectrum_frames(world, 16);
    unsigned char *b = spectrum_load(world, "spectrum_time.png");

    /* The waves shade the water and move */
    int32_t most;
    float mean = spectrum_difference(a, b, &most);
    printf("range %d, mean difference %f, largest %d\n", spectrum_range(a), mean, most);
    test_assert(spectrum_range(a) > 30);
    test_assert(mean > 2.0f);

    /* The same a period later: run up to the time of the first capture plus the period */
    ecs_progress(world, (float)(BOX_RENDER_SPECTRUM_PERIOD - 20 * SPECTRUM_DT));
    box_render_sync(world);
    spectrum_frames(world, 4);
    unsigned char *c = spectrum_load(world, "spectrum_time.png");
    mean = spectrum_difference(a, c, &most);
    printf("a period later: mean difference %f, largest %d\n", mean, most);
    test_assert(mean < 0.5f);
    test_assert(most <= 4);

    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
    ecs_fini(world);
}

void WaterSpectrum_flat_without_wind(void) {
    ecs_world_t *world = spectrum_world(true);
    spectrum_plane(world, 0);
    spectrum_surface(world, "spectrum_calm.png");
    spectrum_frames(world, 4);
    unsigned char *a = spectrum_load(world, "spectrum_calm.png");
    spectrum_frames(world, 16);
    unsigned char *b = spectrum_load(world, "spectrum_calm.png");

    /* Without wind the water stays still */
    int32_t most;
    float mean = spectrum_difference(a, b, &most);
    printf("calm: mean difference %f, largest %d\n", mean, most);
    test_assert(mean < 0.5f);

    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(world);
}

void WaterSpectrum_ignored_without_water(void) {
    ecs_world_t *world = spectrum_world(false);
    ecs_entity_t plane = spectrum_plane(world, 4);
    spectrum_surface(world, NULL);
    spectrum_frames(world, 4);

    /* A scene without Scene.water evolves no spectrum */
    int32_t resolution = 0;
    test_bool(box_render_gpu_read_spectrum(world, plane, 0, &resolution, NULL, 0), false);

    ecs_fini(world);
}
