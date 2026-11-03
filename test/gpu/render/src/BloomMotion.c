/**
 * @file BloomMotion.c
 * @brief Tests that the glow of Bloom stays the same as small and thin features move by
 * fractions of a pixel, at display sizes from 1280x720 to 3840x2160.
 *
 * Every case renders a grid of emissive dots and bars of 1 to 24 pixels under an orthographic
 * camera that moves a fraction of a pixel every frame, once with the default Bloom and once
 * without, and compares the glow of the frames.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Frames of a case, each a subpixel camera step further */
#define MOTION_FRAMES (8)

/** Columns of the grid of features */
#define MOTION_COLS (8)

/** Rows of the grid of features */
#define MOTION_ROWS (4)

/** Features, one in every cell of the grid */
#define MOTION_FEATURES (MOTION_COLS * MOTION_ROWS)

/** Emissive strength of the features, above the firefly clamp */
#define MOTION_STRENGTH (16.0f)

/** Exposure of the captures, which keeps the green and blue channels of the features below 1 */
#define MOTION_EXPOSURE (0.25f)

/** Green and blue of the emissive color, a linear 1/8 and 1/64 of its red */
#define MOTION_GREEN (99)
#define MOTION_BLUE (33)

/** Highest spread of the glow of the whole image across the frames, of its mean */
#define MOTION_TOTAL (0.02)

/** Highest change of the glow around a feature from one frame to the next, of its mean */
#define MOTION_LOCAL (0.02)

/** Highest difference of the glow around a feature with the previous frame, pixel by pixel
 * after shifting it as far as the feature moved, of the glow around it */
#define MOTION_SHAPE (0.05)

/** Glow of one frame, in linear light in the scale of the red channel */
typedef struct motion_frame_t {
    double source;                  /**< Light of the features without bloom */
    double total;                   /**< Glow of the image */
    double near[MOTION_FEATURES];   /**< Glow close to every feature */
    double wide[MOTION_FEATURES];   /**< Glow further out around every feature */
    double shape[MOTION_FEATURES];  /**< Difference with the previous frame, of the glow */
    int32_t x[MOTION_FEATURES];     /**< Column of the first pixel of every feature */
    int32_t y[MOTION_FEATURES];     /**< Row of the first pixel of every feature */
} motion_frame_t;

/** A case: the world, its view and the measures of its frames */
typedef struct motion_t {
    ecs_world_t *world;             /**< The world */
    ecs_entity_t camera;            /**< The orthographic camera */
    ecs_entity_t view;              /**< The view, Bloom its first effect */
    int32_t width;                  /**< Render width in pixels */
    int32_t height;                 /**< Render height in pixels */
    float pixel;                    /**< World units per pixel */
    float channel[3];               /**< Linear red, green and blue of the emissive color */
    double near;                    /**< Width of the near gaussian in pixels */
    double wide;                    /**< Width of the wide gaussian in pixels */
    float *glow;                    /**< Glow of every pixel of the current frame */
    float *previous;                /**< Glow of every pixel of the previous frame */
    motion_frame_t frames[MOTION_FRAMES]; /**< Measures of every frame */
} motion_t;

/** Decode a stored 8 bit sRGB channel to linear.
 *
 * @param c The stored value
 * @return The linear channel
 */
static float motion_linear(
    unsigned char c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/** Create the world of a case: the camera, the view with Bloom and a pinned exposure, the
 * features and the surface.
 *
 * @param m The case
 * @param width Width of the surface in points
 * @param height Height of the surface in points
 * @param ratio Pixels per point
 */
static void motion_init(
    motion_t *m,
    int32_t width,
    int32_t height,
    float ratio)
{
    ecs_world_t *world = m->world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    float scale = 100.0f * (float)width / 1280.0f;
    m->width = (int32_t)((float)width * ratio);
    m->height = (int32_t)((float)height * ratio);
    m->pixel = 1.0f / (scale * ratio);
    m->channel[0] = 1;
    m->channel[1] = motion_linear(MOTION_GREEN);
    m->channel[2] = motion_linear(MOTION_BLUE);
    m->near = (double)m->width / 512.0 * 1.5;
    m->wide = m->near * 4;
    m->glow = ecs_os_malloc_n(float, m->width * m->height);
    m->previous = ecs_os_malloc_n(float, m->width * m->height);
    m->camera = ecs_new(world);
    ecs_set(world, m->camera, Camera, {.ortho = true, .ortho_scale = scale, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t bloom = ecs_new(world);
    ecs_add(world, bloom, Bloom);
    ecs_entity_t exposure = ecs_new(world);
    ecs_set(world, exposure, AutoExposure, {.min_exposure = MOTION_EXPOSURE,
        .max_exposure = MOTION_EXPOSURE, .min_brightness = 0.1f, .max_brightness = 0.3f,
        .min_log_luma = -8, .max_log_luma = 4, .speed_up = 3, .speed_down = 1,
        .high_percentile = 0.95f});
    m->view = ecs_new(world);
    View value = {.camera = m->camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 2);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = bloom };
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = exposure };
    ecs_set_ptr(world, m->view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);

    int32_t i;
    for (i = 0; i < MOTION_FEATURES; i ++) {
        int32_t col = i % MOTION_COLS, row = i / MOTION_COLS;
        float x = ((float)col + 0.5f) * (float)m->width / MOTION_COLS - (float)m->width * 0.5f;
        float y = (float)m->height * 0.5f - ((float)row + 0.5f) * (float)m->height / MOTION_ROWS;
        float sx = 2, sy = 2;
        switch (i % 4) {
        case 1: sx = 24; sy = 1; break;
        case 2: sx = 1; sy = 24; break;
        case 3: sx = 3; sy = 3; break;
        default: break;
        }

        x += 0.137f * (float)i;
        y += 0.071f * (float)i;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {x * m->pixel, y * m->pixel, 0});
        ecs_set(world, e, Box, {sx * m->pixel, sy * m->pixel, 0.05f});
        ecs_set(world, e, Rgb, {0, 0, 0});
        ecs_set(world, e, Emissive, {.strength = MOTION_STRENGTH,
            .color = {255, MOTION_GREEN, MOTION_BLUE}});
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = width, .height = height, .resolution_scale = 1,
        .pixel_ratio = ratio, .write_to_file = "motion.png"});
}

/** Release a case.
 *
 * @param m The case
 */
static void motion_fini(
    motion_t *m)
{
    ecs_os_free(m->glow);
    ecs_os_free(m->previous);
    ecs_fini(m->world);
}

/** Render a frame with or without bloom and load its capture as linear light.
 *
 * @param m The case
 * @param bloom Whether bloom runs
 * @return The channels of every pixel, -1 where it clipped, freed with ecs_os_free
 */
static float* motion_render(
    motion_t *m,
    bool bloom)
{
    float linear[256];
    View *v = ecs_get_mut(m->world, m->view, View);
    ecs_vec_first_t(&v->effects, ViewEffect)->enabled = bloom;
    ecs_modified(m->world, m->view, View);
    ecs_progress(m->world, 1.0f / 60);
    box_render_sync(m->world);

    const char *tmp = ecs_singleton_get(m->world, AppStatus)->tmp_dir;
    char *path = flecs_asprintf("%s/motion.png", tmp);
    int w = 0, h = 0, n = 0, i, c;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, m->width);
    test_int(h, m->height);
    for (i = 0; i < 256; i ++) {
        linear[i] = i < 255 ? motion_linear((unsigned char)i) : -1.0f;
    }

    float *result = ecs_os_malloc_n(float, w * h * 3);
    for (i = 0; i < w * h; i ++) {
        for (c = 0; c < 3; c ++) {
            result[i * 3 + c] = linear[px[(size_t)i * 4 + (size_t)c]];
        }
    }

    stbi_image_free(px);
    ecs_os_free(path);

    return result;
}

/** Sum the glow around a point, weighted by a gaussian.
 *
 * @param m The case
 * @param cx Column of the point, in pixels from the left edge
 * @param cy Row of the point, in pixels from the top edge
 * @param sigma Width of the gaussian in pixels
 * @return The weighted glow
 */
static double motion_window(
    const motion_t *m,
    double cx,
    double cy,
    double sigma)
{
    int32_t r = (int32_t)(sigma * 3.0) + 1, x, y;
    int32_t x0 = glm_max((int32_t)cx - r, 0), x1 = glm_min((int32_t)cx + r, m->width - 1);
    int32_t y0 = glm_max((int32_t)cy - r, 0), y1 = glm_min((int32_t)cy + r, m->height - 1);
    double result = 0, k = -0.5 / (sigma * sigma);
    for (y = y0; y <= y1; y ++) {
        double dy = (double)y + 0.5 - cy;
        for (x = x0; x <= x1; x ++) {
            double dx = (double)x + 0.5 - cx;
            result += (double)m->glow[y * m->width + x] * exp((dx * dx + dy * dy) * k);
        }
    }

    return result;
}

/** Get the difference of the glow around a feature with the previous frame, pixel by pixel
 * after shifting the previous frame as far as the feature moved.
 *
 * @param m The case
 * @param f The frame, after the first
 * @param i The feature
 * @param cx Column of the center of the feature
 * @param cy Row of the center of the feature
 * @return The difference of the glow around the feature
 */
static double motion_shape(
    const motion_t *m,
    int32_t f,
    int32_t i,
    double cx,
    double cy)
{
    const motion_frame_t *cur = &m->frames[f], *prev = &m->frames[f - 1];
    int32_t dx = cur->x[i] - prev->x[i], dy = cur->y[i] - prev->y[i];
    int32_t r = (int32_t)(m->near * 3.0), x, y;
    int32_t x0 = glm_max((int32_t)cx - r, 1), x1 = glm_min((int32_t)cx + r, m->width - 2);
    int32_t y0 = glm_max((int32_t)cy - r, 1), y1 = glm_min((int32_t)cy + r, m->height - 2);
    double diff = 0, sum = 0;
    for (y = y0; y <= y1; y ++) {
        for (x = x0; x <= x1; x ++) {
            float a = m->glow[y * m->width + x];
            float b = m->previous[(y - dy) * m->width + x - dx];
            if (a != 0 && b != 0) {
                diff += fabs((double)(a - b));
                sum += (double)a;
            }
        }
    }

    return sum > 0 ? diff / sum : 1;
}

/** Render a frame with and without bloom and measure its glow.
 *
 * The pixels of the features are lit without bloom and left out, the others hold only glow,
 * read from the red channel or, where it clipped, the green one. The glow around a feature is
 * weighted with gaussians centered on the pixels it covers.
 *
 * @param m The case
 * @param f The frame
 */
static void motion_frame(
    motion_t *m,
    int32_t f)
{
    motion_frame_t *mf = &m->frames[f];
    float offset = 0.031f + 0.29f * (float)f;
    ecs_set(m->world, m->camera, Position, {offset * m->pixel, 0.6f * offset * m->pixel, 5});
    float *lit = motion_render(m, true);
    float *dark = motion_render(m, false);
    double cx[MOTION_FEATURES] = {0}, cy[MOTION_FEATURES] = {0}, area[MOTION_FEATURES] = {0};
    int32_t x, y, i;
    for (i = 0; i < MOTION_FEATURES; i ++) {
        mf->x[i] = mf->y[i] = -1;
    }

    for (y = 0; y < m->height; y ++) {
        for (x = 0; x < m->width; x ++) {
            int32_t p = y * m->width + x, c = 0;
            if (dark[p * 3] != 0) {
                i = (y * MOTION_ROWS / m->height) * MOTION_COLS + x * MOTION_COLS / m->width;
                if (mf->x[i] < 0) {
                    mf->x[i] = x;
                    mf->y[i] = y;
                }

                cx[i] += (double)x + 0.5;
                cy[i] += (double)y + 0.5;
                area[i] += 1;
                test_assert(dark[p * 3 + 1] > 0);
                mf->source += (double)(dark[p * 3 + 1] / m->channel[1]);
                m->glow[p] = 0;
                continue;
            }

            if (lit[p * 3] < 0) {
                c = 1;
            }

            test_assert(lit[p * 3 + c] >= 0);
            m->glow[p] = lit[p * 3 + c] / m->channel[c];
            mf->total += (double)m->glow[p];
        }
    }

    for (i = 0; i < MOTION_FEATURES; i ++) {
        test_assert(area[i] > 0);
        mf->near[i] = motion_window(m, cx[i] / area[i], cy[i] / area[i], m->near);
        mf->wide[i] = motion_window(m, cx[i] / area[i], cy[i] / area[i], m->wide);
        if (f) {
            mf->shape[i] = motion_shape(m, f, i, cx[i] / area[i], cy[i] / area[i]);
        }
    }

    float *swap = m->previous;
    m->previous = m->glow;
    m->glow = swap;
    ecs_os_free(dark);
    ecs_os_free(lit);
}

/** Get the largest change of a measure from one frame to the next, of its mean.
 *
 * @param m The case
 * @param offset Offset of the measure in motion_frame_t
 * @return The largest change of the mean, 1 when the mean is not above 0
 */
static double motion_change(
    const motion_t *m,
    size_t offset)
{
    double mean = 0, result = 0;
    int32_t f;
    for (f = 0; f < MOTION_FRAMES; f ++) {
        mean += *(const double*)ECS_OFFSET(&m->frames[f], offset) / MOTION_FRAMES;
    }

    for (f = 1; f < MOTION_FRAMES; f ++) {
        double a = *(const double*)ECS_OFFSET(&m->frames[f], offset);
        double b = *(const double*)ECS_OFFSET(&m->frames[f - 1], offset);
        result = glm_max(result, fabs(a - b) / mean);
    }

    return mean > 0 ? result : 1;
}

/** Render the frames of a case and check that the glow changes little from frame to frame:
 * in total, close to and further out around every feature, and pixel by pixel.
 *
 * The camera is orthographic and the features are axis aligned and a whole number of pixels
 * in size, so every frame rasterizes the same features, shifted by whole pixels, and a glow
 * that moves with them stays the same.
 *
 * Each channel of the emissive color is 8 times the next, so the glow is precise in the red
 * channel while the features still fit the green one.
 *
 * @param width Width of the surface in points
 * @param height Height of the surface in points
 * @param ratio Pixels per point
 */
static void motion_test(
    int32_t width,
    int32_t height,
    float ratio)
{
    motion_t case_ = {0}, *m = &case_;
    double source = 0, total = 0, lo = INFINITY, hi = 0, local = 0, shape = 0;
    int32_t f, i;
    motion_init(m, width, height, ratio);
    for (f = 0; f < MOTION_FRAMES; f ++) {
        motion_frame(m, f);
        source += m->frames[f].source / MOTION_FRAMES;
        total += m->frames[f].total / MOTION_FRAMES;
        lo = glm_min(lo, m->frames[f].total);
        hi = glm_max(hi, m->frames[f].total);
    }

    for (f = 0; f < MOTION_FRAMES; f ++) {
        test_assert(fabs(m->frames[f].source - source) < source * 0.01);
        for (i = 0; f && i < MOTION_FEATURES; i ++) {
            shape = glm_max(shape, m->frames[f].shape[i]);
        }
    }

    for (i = 0; i < MOTION_FEATURES; i ++) {
        local = glm_max(local, motion_change(m,
            offsetof(motion_frame_t, near) + (size_t)i * sizeof(double)));
        local = glm_max(local, motion_change(m,
            offsetof(motion_frame_t, wide) + (size_t)i * sizeof(double)));
    }

    if ((hi - lo) / total > MOTION_TOTAL || local > MOTION_LOCAL || shape > MOTION_SHAPE) {
        printf("%dx%d: glow %f of the features, spread %f, local %f, shape %f\n",
            m->width, m->height, total / source, (hi - lo) / total, local, shape);
    }

    test_assert(total > source * 0.1);
    test_assert((hi - lo) / total <= MOTION_TOTAL);
    test_assert(local <= MOTION_LOCAL);
    test_assert(shape <= MOTION_SHAPE);

    motion_fini(m);
}

void BloomMotion_stable_at_720p(void) {
    // lighting/effects
    motion_test(1280, 720, 1);
}

void BloomMotion_stable_at_1440p_high_dpi(void) {
    // lighting/effects
    motion_test(1280, 720, 2);
}

void BloomMotion_stable_at_2160p(void) {
    // lighting/effects
    motion_test(3840, 2160, 1);
}
