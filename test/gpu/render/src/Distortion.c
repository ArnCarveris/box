/**
 * @file Distortion.c
 * @brief Tests for how a Distortion shape shows the scene behind it, checked on PNG captures.
 *
 * The cases look down -z from an orthographic camera at z = 10 with a visible height of
 * DIST_HEIGHT into a square capture of DIST_SIZE pixels, so a world unit is DIST_SIZE /
 * DIST_HEIGHT pixels at every depth. Behind, at z = -5, white emissive stripes half a unit wide
 * repeat every unit along x over a black clear color; in front, at z = 0, a thin box covers the
 * middle of the capture. Without lights only the emission and the snapshot show.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the square captures */
#define DIST_SIZE (64)

/** Visible height of the camera in world units */
#define DIST_HEIGHT (16.0f)

/** Pixels per world unit */
#define DIST_PX ((int32_t)(DIST_SIZE / DIST_HEIGHT))

/** Edge of the box in front of the stripes, world units */
#define DIST_BOX (8.0f)

/** Difference of a channel above which two pixels differ */
#define DIST_THRESHOLD (24)

/** What the box in front of the stripes has */
typedef enum dist_kind_t {
    DIST_NONE = 0,      /**< No box */
    DIST_GLASS,         /**< Transmission of factor 1 and an ior of 1, no Distortion */
    DIST_ALONE,         /**< Distortion without Transmission */
    DIST_GLASS_DISTORT  /**< Transmission as DIST_GLASS and a Distortion */
} dist_kind_t;

/** Create a headless world that renders on the main thread with an orthographic camera, no
 * light and Scene.distortion as asked.
 *
 * @param distortion Whether the scene enables Distortion (Scene.distortion)
 * @return The world
 */
static ecs_world_t* dist_world(
    bool distortion)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true,
        .shader_features.Extended.blend_modes = true, .shader_features.Extended.transmission = true,
        .shader_features.Extended.fresnel = true,
        .shader_features.Extended.distortion = distortion});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = DIST_HEIGHT,
        .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Add the stripes behind the box: white emissive boxes half a unit wide every unit along x,
 * from x = -12 to 12, children of a parent.
 *
 * @param world The world
 * @param parent ChildOf parent
 */
static void dist_stripes(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    int32_t i;
    for (i = -12; i <= 12; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
        ecs_set(world, e, Position, {(float)i, 0, -5});
        ecs_set(world, e, Box, {0.5f, 30, 0.1f});
        ecs_set(world, e, Rgb, {0, 0, 0});
        ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 255, 255}});
    }
}

/** Add the box in front of the stripes, a child of a parent.
 *
 * @param world The world
 * @param parent ChildOf parent
 * @param kind What it has
 * @param distortion Its Distortion, for the kinds with one
 * @return The box, 0 for DIST_NONE
 */
static ecs_entity_t dist_box(
    ecs_world_t *world,
    ecs_entity_t parent,
    dist_kind_t kind,
    const Distortion *distortion)
{
    ecs_entity_t e = 0;
    if (kind == DIST_NONE) {
        goto done;
    }

    e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {DIST_BOX, DIST_BOX, 0.2f});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0});
    if (kind == DIST_GLASS || kind == DIST_GLASS_DISTORT) {
        ecs_set(world, e, Transmission, {.transmission_factor = 1, .ior = 1,
            .thickness_factor = 0, .attenuation_distance = 1e10f,
            .attenuation_color = {255, 255, 255}});
    }

    if (kind == DIST_ALONE || kind == DIST_GLASS_DISTORT) {
        ecs_set_ptr(world, e, Distortion, distortion);
    }
done:
    return e;
}

/** Create a scene: the stripes and the box under one parent at the origin.
 *
 * @param distortion Whether the scene enables Distortion
 * @param kind What the box has
 * @param d Its Distortion, for the kinds with one
 * @param parent Receives the parent, NULL for none
 * @return The world
 */
static ecs_world_t* dist_scene(
    bool distortion,
    dist_kind_t kind,
    const Distortion *d,
    ecs_entity_t *parent)
{
    ecs_world_t *world = dist_world(distortion);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {0, 0, 0});
    dist_stripes(world, root);
    dist_box(world, root, kind, d);
    if (parent) {
        *parent = root;
    }

    return world;
}

/** Run frames without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void dist_frames(
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

/** Load a capture of the temporary directory.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* dist_load(
    ecs_world_t *world,
    const char *file)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, DIST_SIZE);
    test_int(h, DIST_SIZE);
    ecs_os_free(path);

    return px;
}

/** Add a surface that writes every frame into a capture.
 *
 * @param world The world
 * @param file Name of the PNG
 */
static void dist_surface(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = DIST_SIZE, .height = DIST_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
}

/** Render frames into a capture and load it.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* dist_render(
    ecs_world_t *world,
    const char *file)
{
    dist_surface(world, file);
    dist_frames(world, 4);

    return dist_load(world, file);
}

/** Render a scene in a fresh world into a capture.
 *
 * @param distortion Whether the scene enables Distortion
 * @param kind What the box has
 * @param d Its Distortion, for the kinds with one
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* dist_capture(
    bool distortion,
    dist_kind_t kind,
    const Distortion *d,
    const char *file)
{
    ecs_world_t *world = dist_scene(distortion, kind, d, NULL);
    unsigned char *px = dist_render(world, file);
    ecs_fini(world);

    return px;
}

/** Compare a square of two captures, the second moved by a number of columns.
 *
 * @param a The first capture
 * @param b The second capture
 * @param lo First row and column of the square in a
 * @param hi Row and column past the square in a
 * @param shift Columns the square is moved by in b
 * @param max Receives the largest difference of a channel
 * @return Pixels with a channel that differs by more than DIST_THRESHOLD
 */
static int32_t dist_diff(
    const unsigned char *a,
    const unsigned char *b,
    int32_t lo,
    int32_t hi,
    int32_t shift,
    int32_t *max)
{
    int32_t x, y, c, result = 0;
    *max = 0;
    for (y = lo; y < hi; y ++) {
        for (x = lo; x < hi; x ++) {
            const unsigned char *pa = &a[(y * DIST_SIZE + x) * 4];
            const unsigned char *pb = &b[(y * DIST_SIZE + x + shift) * 4];
            bool differs = false;
            for (c = 0; c < 3; c ++) {
                int32_t d = abs((int32_t)pa[c] - (int32_t)pb[c]);
                *max = d > *max ? d : *max;
                differs = differs || d > DIST_THRESHOLD;
            }

            result += differs;
        }
    }

    return result;
}

/** First row and column of the square inside the box the cases compare */
#define DIST_LO (DIST_SIZE / 2 - (int32_t)(DIST_BOX * 0.5f) * DIST_PX + 2)

/** Row and column past that square */
#define DIST_HI (DIST_SIZE / 2 + (int32_t)(DIST_BOX * 0.5f) * DIST_PX - 2)

/** The Distortion of the cases: an offset of up to 5% of the image height, about three pixels,
 * over features of one unit */
static const Distortion dist_haze = { .strength = 0.05f, .scale = 1, .speed = 0 };

void Distortion_moves_stripes(void) {
    unsigned char *haze = dist_capture(true, DIST_ALONE, &dist_haze, "distortion_haze.png");
    unsigned char *stripes = dist_capture(true, DIST_NONE, NULL, "distortion_stripes.png");

    /* The distorted box bends the stripes behind it */
    int32_t max = 0;
    int32_t moved = dist_diff(stripes, haze, DIST_LO, DIST_HI, 0, &max);
    printf("haze against stripes: %d pixels differ, max %d\n", moved, max);
    test_assert(moved > (DIST_HI - DIST_LO) * (DIST_HI - DIST_LO) / 10);
    test_assert(max > 100);

    /* Outside the box nothing changes */
    int32_t outside = 0, x, y, c;
    for (y = 0; y < DIST_SIZE; y ++) {
        for (x = 0; x < 8; x ++) {
            for (c = 0; c < 3; c ++) {
                int32_t d = abs((int32_t)haze[(y * DIST_SIZE + x) * 4 + c] -
                    (int32_t)stripes[(y * DIST_SIZE + x) * 4 + c]);
                outside += d > DIST_THRESHOLD;
            }
        }
    }

    test_int(outside, 0);
    stbi_image_free(haze);
    stbi_image_free(stripes);
}

void Distortion_without_scene_transmission(void) {
    ecs_world_t *world = dist_scene(true, DIST_ALONE, &dist_haze, NULL);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true,
        .shader_features.Extended.blend_modes = true, .shader_features.Extended.fresnel = true,
        .shader_features.Extended.distortion = true});
    unsigned char *haze = dist_render(world, "distortion_alone.png");
    ecs_fini(world);
    unsigned char *stripes = dist_capture(true, DIST_NONE, NULL, "distortion_stripes.png");

    /* Scene.distortion alone takes the snapshot and bends it */
    int32_t max = 0;
    int32_t moved = dist_diff(stripes, haze, DIST_LO, DIST_HI, 0, &max);
    printf("without Scene.transmission: %d pixels differ, max %d\n", moved, max);
    test_assert(moved > (DIST_HI - DIST_LO) * (DIST_HI - DIST_LO) / 10);
    test_assert(max > 100);
    stbi_image_free(haze);
    stbi_image_free(stripes);
}

void Distortion_with_transmission_moves_stripes(void) {
    unsigned char *glass = dist_capture(true, DIST_GLASS, NULL, "distortion_glass.png");
    unsigned char *haze = dist_capture(true, DIST_GLASS_DISTORT, &dist_haze,
        "distortion_glass_haze.png");
    int32_t max = 0;
    int32_t moved = dist_diff(glass, haze, DIST_LO, DIST_HI, 0, &max);
    printf("distorted glass against glass: %d pixels differ, max %d\n", moved, max);
    test_assert(moved > (DIST_HI - DIST_LO) * (DIST_HI - DIST_LO) / 10);
    stbi_image_free(glass);
    stbi_image_free(haze);
}

void Distortion_zero_strength_matches(void) {
    Distortion zero = dist_haze;
    zero.strength = 0;
    unsigned char *stripes = dist_capture(true, DIST_NONE, NULL, "distortion_stripes.png");
    unsigned char *glass = dist_capture(true, DIST_GLASS, NULL, "distortion_glass.png");
    unsigned char *alone = dist_capture(true, DIST_ALONE, &zero, "distortion_zero.png");
    unsigned char *both = dist_capture(true, DIST_GLASS_DISTORT, &zero,
        "distortion_glass_zero.png");

    /* Without Transmission the box shows the sharp snapshot, so without light it disappears */
    int32_t max = 0;
    int32_t differ = dist_diff(stripes, alone, 0, DIST_SIZE, 0, &max);
    printf("zero strength alone against stripes: %d pixels differ, max %d\n", differ, max);
    test_int(differ, 0);
    test_assert(max <= 3);
    differ = dist_diff(glass, both, 0, DIST_SIZE, 0, &max);
    printf("zero strength glass against glass: %d pixels differ, max %d\n", differ, max);
    test_int(differ, 0);
    test_assert(max <= 1);
    stbi_image_free(stripes);
    stbi_image_free(glass);
    stbi_image_free(alone);
    stbi_image_free(both);
}

void Distortion_off_matches_without_distortion(void) {
    unsigned char *glass = dist_capture(false, DIST_GLASS, NULL, "distortion_off_glass.png");
    unsigned char *haze = dist_capture(false, DIST_GLASS_DISTORT, &dist_haze,
        "distortion_off_haze.png");
    int32_t max = 0;
    int32_t differ = dist_diff(glass, haze, 0, DIST_SIZE, 0, &max);
    printf("off: %d pixels differ, max %d\n", differ, max);
    test_int(differ, 0);
    test_assert(max <= 1);
    stbi_image_free(glass);
    stbi_image_free(haze);
}

void Distortion_changes_over_time(void) {
    Distortion moving = dist_haze;
    moving.speed = 1;
    ecs_world_t *world = dist_scene(true, DIST_ALONE, &moving, NULL);
    dist_surface(world, "distortion_time.png");
    dist_frames(world, 4);
    unsigned char *a = dist_load(world, "distortion_time.png");

    /* A third of a turn later the pattern has changed */
    dist_frames(world, 20);
    unsigned char *b = dist_load(world, "distortion_time.png");
    int32_t max = 0;
    int32_t differ = dist_diff(a, b, DIST_LO, DIST_HI, 0, &max);
    printf("over time: %d pixels differ, max %d\n", differ, max);
    test_assert(differ > (DIST_HI - DIST_LO) * (DIST_HI - DIST_LO) / 20);
    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(world);
}

void Distortion_still_without_speed(void) {
    ecs_world_t *world = dist_scene(true, DIST_ALONE, &dist_haze, NULL);
    dist_surface(world, "distortion_still.png");
    dist_frames(world, 4);
    unsigned char *a = dist_load(world, "distortion_still.png");
    dist_frames(world, 20);
    unsigned char *b = dist_load(world, "distortion_still.png");
    int32_t max = 0;
    int32_t differ = dist_diff(a, b, 0, DIST_SIZE, 0, &max);
    printf("still: %d pixels differ, max %d\n", differ, max);
    test_int(differ, 0);
    test_assert(max <= 1);
    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(world);
}

void Distortion_moves_with_shape(void) {
    ecs_entity_t parent;
    ecs_world_t *world = dist_scene(true, DIST_ALONE, &dist_haze, &parent);
    dist_surface(world, "distortion_move.png");
    dist_frames(world, 4);
    unsigned char *a = dist_load(world, "distortion_move.png");

    /* The stripes and the box move two units to the right: the image moves with them,
     * pattern included */
    ecs_set(world, parent, Position, {2, 0, 0});
    dist_frames(world, 4);
    unsigned char *b = dist_load(world, "distortion_move.png");
    int32_t shift = 2 * DIST_PX;
    int32_t max = 0;
    int32_t differ = dist_diff(a, b, DIST_LO, DIST_HI - shift, shift, &max);
    printf("moved: %d pixels differ, max %d\n", differ, max);
    test_assert(differ <= (DIST_HI - DIST_LO) * (DIST_HI - DIST_LO) / 50);

    /* Unmoved, the distorted image is not the shifted one */
    int32_t unmoved = dist_diff(a, b, DIST_LO, DIST_HI - shift, 0, &max);
    printf("not shifted: %d pixels differ, max %d\n", unmoved, max);
    test_assert(unmoved > differ);
    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(world);
}
