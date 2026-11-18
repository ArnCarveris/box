/**
 * @file WaterCaustics.c
 * @brief Tests for the caustics of a Water (WaterCaustics), checked on PNG captures.
 *
 * The cases look straight down from a camera 10 units above the water plane y = 0 into a square
 * capture of CAUSTICS_SIZE pixels, with a 60 degree field of view: +x runs to the right of the
 * image. A flat gray floor lies under clear water, lit by a dim ambient light and a sun that
 * comes down from +x at 45 degrees; a board above the water at the right edge of the view can
 * cast its shadow on the right half of the floor. The water is smooth, so the floor seen
 * through it is not blurred.
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
#define CAUSTICS_SIZE (128)

/** Height of the camera above the water */
#define CAUSTICS_EYE (10.0f)

/** Columns of the lit part of the floor, left of the shadow of the board */
#define CAUSTICS_LIT_FIRST (12)
#define CAUSTICS_LIT_LAST (54)

/** Columns of the part of the floor in the shadow of the board */
#define CAUSTICS_SHADOW_FIRST (76)
#define CAUSTICS_SHADOW_LAST (96)

/** Rows the statistics are taken over */
#define CAUSTICS_ROW_FIRST (16)
#define CAUSTICS_ROW_LAST (112)

/** Create a headless world that renders on the main thread with a sun from +x at 45 degrees, a
 * dim ambient light, a view with shadows and a gray floor at a depth below the water plane.
 *
 * @param floor_depth Depth of the top of the floor below y = 0
 * @param board Whether a board high above the water shades the right half of the floor
 * @return The world
 */
static ecs_world_t* caustics_world(
    float floor_depth,
    bool board)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.transmission = true, .water = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {255, 255, 255}, .ground = {255, 255, 255},
        .intensity = 0.15f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {GLM_PI_4f, -GLM_PI_2f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 2.0f});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, CAUSTICS_EYE, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->lighting.enabled = true;
    v->shadows.enabled = true;
    ecs_modified(world, view, View);

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -floor_depth - 0.1f, 0});
    ecs_set(world, floor, Box, {30, 0.2f, 30});
    ecs_set(world, floor, Rgb, {150, 150, 150});
    ecs_set(world, floor, PbrMaterial, {.metallic = 0, .roughness = 1});

    if (board) {
        /* x 3.5..7.5 at y 3, at the right edge of the view; its shadow falls on x 0..4 of
         * the floor */
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {5.5f, 3, 0});
        ecs_set(world, e, Box, {4, 0.2f, 30});
        ecs_set(world, e, Rgb, {100, 100, 100});
    }

    return world;
}

/** Add the water plane y = 0, a smooth 20x20 quad facing up that fills the capture, with a Water
 * so clear that the floor shows through almost untouched, without foam or refraction.
 *
 * @param world The world
 * @return The plane
 */
static ecs_entity_t caustics_plane(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {20, 20});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.05f});
    ecs_set(world, e, Water, {
        .shallow_color = {150, 150, 150},
        .deep_color = {150, 150, 150},
        .depth = 100,
        .visibility = 200,
        .absorption = {255, 255, 255},
        .refraction = 0,
        .foam_color = {255, 255, 255},
        .foam_width = 0,
        .foam_scale = 1
    });

    return e;
}

/** Render frames of 1/60 s into a capture without new device errors and load it.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* caustics_render(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = CAUSTICS_SIZE, .height = CAUSTICS_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    box_render_gpu_stats_t before, after;
    bool started = box_render_gpu_stats(world, &before);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &after), true);
    if (started) {
        test_int(after.errors, before.errors);
    } else {
        test_int(after.errors, 0);
    }

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, CAUSTICS_SIZE);
    test_int(h, CAUSTICS_SIZE);
    ecs_os_free(path);

    return px;
}

/** Brightness of a rectangle of a capture: the mean and the standard deviation of the sum of
 * red, green and blue.
 */
typedef struct caustics_stats_t {
    float mean;     /**< Mean brightness */
    float spread;   /**< Standard deviation of the brightness */
} caustics_stats_t;

/** Get the brightness of the columns of a capture over the rows the statistics are taken over.
 *
 * @param px The pixels
 * @param first First column
 * @param last Last column
 * @return The statistics
 */
static caustics_stats_t caustics_stats(
    const unsigned char *px,
    int32_t first,
    int32_t last)
{
    double sum = 0, squares = 0;
    int32_t x, y, count = 0;
    for (y = CAUSTICS_ROW_FIRST; y <= CAUSTICS_ROW_LAST; y ++) {
        for (x = first; x <= last; x ++) {
            const unsigned char *p = &px[(y * CAUSTICS_SIZE + x) * 4];
            double v = (double)p[0] + (double)p[1] + (double)p[2];
            sum += v;
            squares += v * v;
            count ++;
        }
    }

    double mean = sum / count;
    double variance = squares / count - mean * mean;

    return (caustics_stats_t){
        .mean = (float)mean, .spread = (float)sqrt(variance > 0 ? variance : 0) };
}

/** Get the largest difference of a channel of two captures.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The difference, 0..255
 */
static int32_t caustics_difference(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i, result = 0;
    for (i = 0; i < CAUSTICS_SIZE * CAUSTICS_SIZE * 4; i ++) {
        int32_t d = abs((int32_t)a[i] - (int32_t)b[i]);
        result = d > result ? d : result;
    }

    return result;
}

void WaterCaustics_lit_floor_varies(void) {
    ecs_world_t *world = caustics_world(0.5f, false);
    caustics_plane(world);
    unsigned char *plain = caustics_render(world, "caustics_plain.png");
    ecs_fini(world);

    world = caustics_world(0.5f, false);
    ecs_entity_t plane = caustics_plane(world);
    ecs_set(world, plane, WaterCaustics, {.strength = 1, .scale = 2, .speed = 0.5f,
        .depth = 2});
    unsigned char *lit = caustics_render(world, "caustics_lit.png");

    /* The floor under plain water is lit evenly; with caustics its brightness varies clearly,
     * around about the same mean */
    caustics_stats_t a = caustics_stats(plain, CAUSTICS_LIT_FIRST, CAUSTICS_LIT_LAST);
    caustics_stats_t b = caustics_stats(lit, CAUSTICS_LIT_FIRST, CAUSTICS_LIT_LAST);
    printf("plain mean %f spread %f, caustics mean %f spread %f\n",
        a.mean, a.spread, b.mean, b.spread);
    test_assert(a.mean > 60);
    test_assert(b.spread > a.spread * 3 + 12);
    test_assert(fabsf(b.mean - a.mean) < a.mean * 0.25f);

    stbi_image_free(plain);
    stbi_image_free(lit);
    ecs_fini(world);
}

void WaterCaustics_none_in_shadow(void) {
    ecs_world_t *world = caustics_world(0.5f, true);
    caustics_plane(world);
    unsigned char *plain = caustics_render(world, "caustics_shadow_plain.png");
    ecs_fini(world);

    world = caustics_world(0.5f, true);
    ecs_entity_t plane = caustics_plane(world);
    ecs_set(world, plane, WaterCaustics, {.strength = 1, .scale = 2, .speed = 0.5f,
        .depth = 2});
    unsigned char *lit = caustics_render(world, "caustics_shadow.png");

    /* The board shades the right part of the floor: darker than the left */
    caustics_stats_t sun = caustics_stats(plain, CAUSTICS_LIT_FIRST, CAUSTICS_LIT_LAST);
    caustics_stats_t shade = caustics_stats(plain, CAUSTICS_SHADOW_FIRST,
        CAUSTICS_SHADOW_LAST);
    printf("sunlit mean %f, shaded mean %f\n", sun.mean, shade.mean);
    test_assert(shade.mean < sun.mean * 0.6f);

    /* The sunlit part shows the caustics, the shaded part none */
    caustics_stats_t lit_sun = caustics_stats(lit, CAUSTICS_LIT_FIRST, CAUSTICS_LIT_LAST);
    caustics_stats_t lit_shade = caustics_stats(lit, CAUSTICS_SHADOW_FIRST,
        CAUSTICS_SHADOW_LAST);
    printf("caustics sunlit spread %f, shaded spread %f (plain %f), shaded mean %f\n",
        lit_sun.spread, lit_shade.spread, shade.spread, lit_shade.mean);
    test_assert(lit_sun.spread > sun.spread * 3 + 12);
    test_assert(lit_shade.spread < shade.spread + 2);
    test_assert(fabsf(lit_shade.mean - shade.mean) < 2);

    stbi_image_free(plain);
    stbi_image_free(lit);
    ecs_fini(world);
}

void WaterCaustics_strength_zero_unchanged(void) {
    ecs_world_t *world = caustics_world(0.5f, false);
    caustics_plane(world);
    unsigned char *plain = caustics_render(world, "caustics_zero_plain.png");
    ecs_fini(world);

    /* A strength of 0 renders exactly as no WaterCaustics */
    world = caustics_world(0.5f, false);
    ecs_entity_t plane = caustics_plane(world);
    ecs_set(world, plane, WaterCaustics, {.strength = 0, .scale = 2, .speed = 0.5f,
        .depth = 2});
    ecs_set(world, plane, WaterFlow, {{1, 0}});
    unsigned char *zero = caustics_render(world, "caustics_zero.png");
    test_int(memcmp(plain, zero, CAUSTICS_SIZE * CAUSTICS_SIZE * 4), 0);

    stbi_image_free(plain);
    stbi_image_free(zero);
    ecs_fini(world);
}

void WaterCaustics_deep_floor_unchanged(void) {
    ecs_world_t *world = caustics_world(3, false);
    caustics_plane(world);
    unsigned char *plain = caustics_render(world, "caustics_deep_plain.png");
    ecs_fini(world);

    /* A floor deeper than the depth of the caustics shows none */
    world = caustics_world(3, false);
    ecs_entity_t plane = caustics_plane(world);
    ecs_set(world, plane, WaterCaustics, {.strength = 1, .scale = 2, .speed = 0.5f,
        .depth = 2});
    unsigned char *deep = caustics_render(world, "caustics_deep.png");
    int32_t d = caustics_difference(plain, deep);
    caustics_stats_t s = caustics_stats(deep, CAUSTICS_LIT_FIRST, CAUSTICS_LIT_LAST);
    printf("deep difference %d, spread %f\n", d, s.spread);
    test_assert(d <= 1);

    stbi_image_free(plain);
    stbi_image_free(deep);
    ecs_fini(world);
}

void WaterCaustics_move_over_time(void) {
    ecs_world_t *world = caustics_world(0.5f, false);
    ecs_entity_t plane = caustics_plane(world);
    ecs_set(world, plane, WaterCaustics, {.strength = 1, .scale = 2, .speed = 0.5f,
        .depth = 2});
    unsigned char *a = caustics_render(world, "caustics_time_a.png");

    /* Half a second later the pattern changed */
    int32_t i;
    for (i = 0; i < 30; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    unsigned char *b = caustics_render(world, "caustics_time_b.png");
    int32_t d = caustics_difference(a, b);
    printf("difference after half a second %d\n", d);
    test_assert(d > 20);

    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(world);
}
