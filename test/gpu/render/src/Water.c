/**
 * @file Water.c
 * @brief Tests for how a Water surface shades over what lies below it, checked on PNG captures.
 *
 * The cases look straight down from a camera 10 units above the water plane y = 0 into a square
 * capture of WATER_SIZE pixels, with a 60 degree field of view: +x runs to the right of the
 * image, the row of z = 0 is the middle one. Most cases have a gray slope below the water that
 * rises along +x as y = -1 + x / 4, so the water is shallow toward the shore at x = 4 and deep
 * toward -x; past the shore the slope is above the water and hides it.
 *
 * The scene is lit by a white ambient only; a point light of intensity 0 out of reach replaces
 * the default sun.
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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Edge of the square captures */
#define WATER_SIZE (64)

/** Height of the camera above the water */
#define WATER_EYE (10.0f)

/** Create a headless world that renders on the main thread, with every Scene feature but water
 * as asked, a white ambient and no sun.
 *
 * @param water Whether the scene enables Water (Scene.water)
 * @param clear Clear color of the view, display encoded
 * @return The world
 */
static ecs_world_t* water_world(
    bool water,
    rgba clear)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true,
        .fresnel = true, .water = water});
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
    ecs_set(world, camera, Position, {0, WATER_EYE, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = clear},
        .lighting = {.enabled = true}});

    return world;
}

/** Add the gray slope y = -1 + x / 4 below the water, a quad turned about z.
 *
 * @param world The world
 * @return The slope
 */
static ecs_entity_t water_slope(
    ecs_world_t *world)
{
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, -1, 0});
    ecs_set(world, parent, Rotation, {0, 0, atanf(0.25f)});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {30, 30});
    ecs_set(world, e, Rgb, {128, 128, 128});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});

    return e;
}

/** Add a box of black albedo that shows its emission.
 *
 * @param world The world
 * @param x X of its center
 * @param y Y of its center
 * @param size Edge of the box
 * @param r Red of the emission
 * @param g Green of the emission
 * @param b Blue of the emission
 * @return The box
 */
static ecs_entity_t water_glow(
    ecs_world_t *world,
    float x,
    float y,
    float size,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, 0});
    ecs_set(world, e, Box, {size, size, size});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {r, g, b}});

    return e;
}

/** Get the Water of the cases: no foam, no refraction, a body that hides what is behind it
 * (visibility 0.001), a white absorption, a green shallow and a blue deep color of depth 3.
 *
 * @return The Water
 */
static Water water_default(void)
{
    return (Water){
        .shallow_color = {0, 255, 0},
        .deep_color = {0, 0, 255},
        .depth = 3,
        .visibility = 0.001f,
        .absorption = {255, 255, 255},
        .refraction = 0,
        .foam_color = {255, 255, 255},
        .foam_width = 0,
        .foam_texture = 0,
        .foam_scale = 1,
        .ripple_scale = 0,
        .ripple_velocity = {0, 0}
    };
}

/** Add the water plane y = 0, a 12x12 quad facing up that fills the capture.
 *
 * @param world The world
 * @param water Its Water
 * @return The water
 */
static ecs_entity_t water_plane(
    ecs_world_t *world,
    const Water *water)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {12, 12});
    ecs_set(world, e, Rgb, {255, 0, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.3f});
    ecs_set_ptr(world, e, Water, water);

    return e;
}

/** Run frames without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void water_frames(
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

/** Write an image into the temporary folder of the world and load it as a texture.
 *
 * @param world The world
 * @param name File name
 * @param px RGBA pixels, size by size
 * @param size Width and height
 * @return The texture, loaded
 */
static ecs_entity_t water_texture(
    ecs_world_t *world,
    const char *name,
    const uint8_t *px,
    int32_t size)
{
    char *file = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    test_assert(stbi_write_png(file, size, size, 4, px, size * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    ecs_os_free(file);

    return tex;
}

/** Create a normal map of one tilt toward +U everywhere: (0.5, 0, 0.866) in tangent space.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t water_tilt(
    ecs_world_t *world)
{
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 8 * 8; i ++) {
        px[i * 4 + 0] = 191;
        px[i * 4 + 1] = 128;
        px[i * 4 + 2] = 238;
        px[i * 4 + 3] = 255;
    }

    return water_texture(world, "water_tilt.png", px, 8);
}

/** Render frames into a capture and load it.
 *
 * @param world The world
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* water_render(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = WATER_SIZE, .height = WATER_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    water_frames(world, 4);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, WATER_SIZE);
    test_int(h, WATER_SIZE);
    ecs_os_free(path);

    return px;
}

/** Get the column of the middle row a point of the plane z = 0 projects to.
 *
 * @param x X of the point
 * @param y Y of the point
 * @return The column
 */
static int32_t water_col(
    float x,
    float y)
{
    float half = (WATER_EYE - y) * tanf(GLM_PIf / 6.0f);

    return (int32_t)floorf((x / half * 0.5f + 0.5f) * (float)WATER_SIZE);
}

/** Get a pixel of the middle row of a capture.
 *
 * @param px The pixels
 * @param col Column
 * @return The pixel, RGBA
 */
static const unsigned char* water_px(
    const unsigned char *px,
    int32_t col)
{
    test_assert(col >= 0 && col < WATER_SIZE);

    return &px[((WATER_SIZE / 2) * WATER_SIZE + col) * 4];
}

void Water_shallow_closer_to_shallow_color(void) {
    ecs_world_t *world = water_world(true, (rgba){0, 0, 0, 255});
    water_slope(world);
    Water w = water_default();
    water_plane(world, &w);
    unsigned char *px = water_render(world, "water_shallow.png");

    // x = 3.5 is 0.125 deep, x = -5 is 2.25 deep
    const unsigned char *shallow = water_px(px, water_col(3.5f, 0));
    const unsigned char *deep = water_px(px, water_col(-5, 0));
    printf("shallow %d %d %d, deep %d %d %d\n", shallow[0], shallow[1], shallow[2],
        deep[0], deep[1], deep[2]);
    test_assert(shallow[1] > shallow[2] + 40);
    test_assert(deep[2] > deep[1] + 40);
    test_assert(shallow[1] > deep[1]);
    test_assert(deep[2] > shallow[2]);

    stbi_image_free(px);
    ecs_fini(world);
}

void Water_object_fades_with_depth(void) {
    ecs_world_t *world = water_world(true, (rgba){0, 0, 0, 255});
    water_slope(world);

    // Red glowing boxes on the slope: the top of the first 0.05 below the water, of the
    // second 1.6
    water_glow(world, 2.6f, -0.35f, 0.6f, 255, 0, 0);
    water_glow(world, -3.6f, -1.9f, 0.6f, 255, 0, 0);
    Water w = water_default();
    w.shallow_color = (Rgb){10, 30, 40};
    w.deep_color = (Rgb){10, 30, 40};
    w.visibility = 2;
    water_plane(world, &w);
    unsigned char *px = water_render(world, "water_fades.png");

    const unsigned char *near_ = water_px(px, water_col(2.6f, -0.05f));
    const unsigned char *far_ = water_px(px, water_col(-3.6f, -1.6f));
    const unsigned char *none = water_px(px, water_col(0, 0));
    printf("near %d %d %d, far %d %d %d, none %d %d %d\n", near_[0], near_[1], near_[2],
        far_[0], far_[1], far_[2], none[0], none[1], none[2]);
    test_assert(near_[0] > 150);
    test_assert(near_[0] > far_[0] + 60);
    test_assert(near_[0] > none[0] + 100);

    stbi_image_free(px);
    ecs_fini(world);
}

/** Render the slope made white under clear water of a black body and an absorption.
 *
 * @param absorption Water.absorption
 * @param file Name of the PNG
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* water_absorption_render(
    Rgb absorption,
    const char *file)
{
    ecs_world_t *world = water_world(true, (rgba){0, 0, 0, 255});
    ecs_entity_t slope = water_slope(world);
    ecs_set(world, slope, Rgb, {255, 255, 255});
    Water w = water_default();
    w.shallow_color = (Rgb){0, 0, 0};
    w.deep_color = (Rgb){0, 0, 0};
    w.visibility = 8;
    w.absorption = absorption;
    ecs_entity_t plane = water_plane(world, &w);

    // Smooth water, so the blur of the snapshot does not mix the depths
    ecs_set(world, plane, PbrMaterial, {.metallic = 0, .roughness = 0.05f});
    unsigned char *px = water_render(world, file);
    unsigned char *copy = ecs_os_malloc(WATER_SIZE * WATER_SIZE * 4);
    memcpy(copy, px, WATER_SIZE * WATER_SIZE * 4);
    stbi_image_free(px);
    ecs_fini(world);

    return copy;
}

void Water_absorption_tints_with_depth(void) {
    // A blue absorption turns the white floor blue, more where the water is deeper
    unsigned char *px = water_absorption_render((Rgb){40, 120, 255}, "water_absorption.png");

    // x = 3.5 is 0.125 deep, x = -5 is 2.25 deep
    const unsigned char *shallow = water_px(px, water_col(3.5f, 0));
    const unsigned char *deep = water_px(px, water_col(-5, 0));
    printf("absorbed shallow %d %d %d, deep %d %d %d\n", shallow[0], shallow[1], shallow[2],
        deep[0], deep[1], deep[2]);
    test_assert(shallow[2] > 60);
    test_assert(deep[2] > 30);
    test_assert(deep[2] > deep[1]);
    test_assert(deep[1] > deep[0]);
    test_assert(deep[2] > deep[0] + 25);

    // The tint grows with the depth: the blue leads the red more relative to the brightness
    float shallow_ratio = (float)shallow[0] / (float)(shallow[2] > 0 ? shallow[2] : 1);
    float deep_ratio = (float)deep[0] / (float)(deep[2] > 0 ? deep[2] : 1);
    printf("red over blue shallow %f, deep %f\n", shallow_ratio, deep_ratio);
    test_assert(shallow_ratio > 0.85f);
    test_assert(deep_ratio < shallow_ratio - 0.2f);

    ecs_os_free(px);
}

void Water_absorption_white_keeps_colors(void) {
    // A white absorption keeps the white floor gray at every depth
    unsigned char *px = water_absorption_render((Rgb){255, 255, 255},
        "water_absorption_white.png");
    int32_t cols[] = {water_col(3.5f, 0), water_col(0, 0), water_col(-5, 0)};
    int32_t i;
    for (i = 0; i < 3; i ++) {
        const unsigned char *p = water_px(px, cols[i]);
        printf("white absorption %d: %d %d %d\n", cols[i], p[0], p[1], p[2]);
        test_assert(abs(p[0] - p[2]) <= 3);
        test_assert(abs(p[1] - p[2]) <= 3);
    }

    test_assert(water_px(px, cols[0])[0] > water_px(px, cols[2])[0]);

    ecs_os_free(px);
}

void Water_foam_at_shore(void) {
    ecs_world_t *world = water_world(true, (rgba){0, 0, 0, 255});
    water_slope(world);
    Water w = water_default();
    w.shallow_color = (Rgb){0, 0, 80};
    w.deep_color = (Rgb){0, 0, 80};
    w.foam_width = 0.5f;
    ecs_entity_t e = water_plane(world, &w);
    unsigned char *px = water_render(world, "water_foam.png");

    // x = 3.5 is 0.125 deep, 0.75 of the foam band; x = 0 is 1 deep, past it
    const unsigned char *shore = water_px(px, water_col(3.5f, 0));
    const unsigned char *open = water_px(px, water_col(0, 0));
    printf("foam %d %d %d, open %d %d %d\n", shore[0], shore[1], shore[2],
        open[0], open[1], open[2]);
    test_assert(shore[0] > 120);
    test_assert(shore[1] > 120);
    test_assert(open[0] < 40);
    stbi_image_free(px);

    // A foam width of 0 has no foam
    w.foam_width = 0;
    ecs_set_ptr(world, e, Water, &w);
    px = water_render(world, "water_no_foam.png");
    shore = water_px(px, water_col(3.5f, 0));
    printf("no foam %d %d %d\n", shore[0], shore[1], shore[2]);
    test_assert(shore[0] < 40);
    test_assert(shore[1] < 40);

    stbi_image_free(px);
    ecs_fini(world);
}

void Water_foam_texture_breaks_up(void) {
    ecs_world_t *world = water_world(true, (rgba){0, 0, 0, 255});
    water_slope(world);

    // A black foam texture cuts the whole band: no foam
    uint8_t black[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 8 * 8; i ++) {
        black[i * 4 + 0] = black[i * 4 + 1] = black[i * 4 + 2] = 0;
        black[i * 4 + 3] = 255;
    }

    Water w = water_default();
    w.shallow_color = (Rgb){0, 0, 80};
    w.deep_color = (Rgb){0, 0, 80};
    w.foam_width = 0.5f;
    w.foam_texture = water_texture(world, "water_foam_black.png", black, 8);
    water_plane(world, &w);
    unsigned char *px = water_render(world, "water_foam_texture.png");
    const unsigned char *shore = water_px(px, water_col(3.5f, 0));
    printf("textured foam %d %d %d\n", shore[0], shore[1], shore[2]);
    test_assert(shore[0] < 60);

    stbi_image_free(px);
    ecs_fini(world);
}

void Water_sky_counts_as_deep(void) {
    // Nothing below the water: the red clear color must not show, the body is deep
    ecs_world_t *world = water_world(true, (rgba){255, 0, 0, 255});
    Water w = water_default();
    w.visibility = 2;
    w.foam_width = 0.5f;
    water_plane(world, &w);
    unsigned char *px = water_render(world, "water_sky.png");

    const unsigned char *p = water_px(px, WATER_SIZE / 2);
    printf("sky %d %d %d\n", p[0], p[1], p[2]);
    test_assert(p[0] < 40);
    test_assert(p[2] > p[1] + 40);
    test_assert(p[2] > 80);

    stbi_image_free(px);
    ecs_fini(world);
}

void Water_above_not_leaked(void) {
    ecs_world_t *world = water_world(true, (rgba){0, 0, 0, 255});

    // A black floor 3 below clear water, a red box above the water and a green one below it
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -3, 0});
    ecs_set(world, floor, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, floor, Quad, {30, 30});
    ecs_set(world, floor, Rgb, {0, 0, 0});
    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 1});
    water_glow(world, 2, 1, 1, 255, 0, 0);
    water_glow(world, -2, -2.5f, 1, 0, 255, 0);

    Water w = water_default();
    w.shallow_color = (Rgb){0, 0, 0};
    w.deep_color = (Rgb){0, 0, 0};
    w.visibility = 1000;
    w.refraction = 0.3f;
    ecs_entity_t e = water_plane(world, &w);
    ecs_set(world, e, PbrTextures, {.normal = water_tilt(world)});

    // Smooth water, so the blur of the snapshot does not spread the boxes
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.05f});
    unsigned char *px = water_render(world, "water_leak.png");

    // The normal map moves the screen point by about 10 pixels along x: the water next to the
    // green box shows it moved, the water next to the red box above does not show it
    int32_t red_left = water_col(1.5f, 0.5f), red_right = water_col(2.5f, 0.5f);
    int32_t green_left = water_col(-2.5f, -2), green_right = water_col(-1.5f, -2);
    int32_t col, green = 0;
    for (col = green_left - 8; col < green_left; col ++) {
        int32_t g = water_px(px, col)[1];
        green = g > green ? g : green;
    }

    for (col = green_right + 1; col < green_right + 8; col ++) {
        int32_t g = water_px(px, col)[1];
        green = g > green ? g : green;
    }

    printf("refracted green %d\n", green);
    test_assert(green > 60);
    for (col = red_left - 9; col < red_left - 1; col ++) {
        printf("left of red %d: %d %d %d\n", col, water_px(px, col)[0], water_px(px, col)[1],
            water_px(px, col)[2]);
        test_assert(water_px(px, col)[0] < 60);
    }

    for (col = red_right + 2; col < red_right + 10 && col < WATER_SIZE; col ++) {
        printf("right of red %d: %d %d %d\n", col, water_px(px, col)[0], water_px(px, col)[1],
            water_px(px, col)[2]);
        test_assert(water_px(px, col)[0] < 60);
    }

    stbi_image_free(px);
    ecs_fini(world);
}

void Water_off_draws_without_water(void) {
    // Without Scene.water the plane draws its own magenta material over the slope
    ecs_world_t *world = water_world(false, (rgba){0, 0, 0, 255});
    water_slope(world);
    Water w = water_default();
    w.foam_width = 0.5f;
    water_plane(world, &w);
    unsigned char *px = water_render(world, "water_off.png");

    const unsigned char *shallow = water_px(px, water_col(3.5f, 0));
    const unsigned char *deep = water_px(px, water_col(-5, 0));
    printf("off shallow %d %d %d, deep %d %d %d\n", shallow[0], shallow[1], shallow[2],
        deep[0], deep[1], deep[2]);
    test_assert(shallow[0] > 100 && shallow[2] > 100 && shallow[1] < 60);
    test_assert(deep[0] > 100 && deep[2] > 100 && deep[1] < 60);

    stbi_image_free(px);
    ecs_fini(world);
}

void Water_off_equals_without_water(void) {
    // Without Scene.water a plane with Water draws exactly like the plane without it
    ecs_world_t *world = water_world(false, (rgba){0, 0, 0, 255});
    water_slope(world);
    Water w = water_default();
    w.foam_width = 0.5f;
    w.ripple_scale = 2;
    w.ripple_velocity[0] = 0.25f;
    water_plane(world, &w);
    unsigned char *with = water_render(world, "water_off_with.png");
    ecs_fini(world);

    world = water_world(false, (rgba){0, 0, 0, 255});
    water_slope(world);
    ecs_entity_t plane = water_plane(world, &w);
    ecs_remove(world, plane, Water);
    unsigned char *without = water_render(world, "water_off_without.png");
    test_int(memcmp(with, without, WATER_SIZE * WATER_SIZE * 4), 0);

    stbi_image_free(with);
    stbi_image_free(without);
    ecs_fini(world);
}

/** Run a frame of 1/60 s and get the bytes it uploaded.
 *
 * @param world The world
 * @return RenderStatus.upload_bytes of the frame
 */
static int64_t water_upload_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    return ecs_singleton_get(world, RenderStatus)->upload_bytes;
}

void Water_still_uploads_nothing(void) {
    // A still Water uploads nothing on an idle frame, moving ripples upload every frame
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, Scene, {.water = true, .transmission = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, WATER_EYE, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    Water w = water_default();
    ecs_entity_t plane = water_plane(world, &w);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        water_upload_frame(world);
    }

    for (i = 0; i < 3; i ++) {
        test_int(water_upload_frame(world), 0);
    }

    /* Moving ripples upload the record of the plane every frame */
    Water *value = ecs_get_mut(world, plane, Water);
    value->ripple_scale = 2;
    value->ripple_velocity[0] = 0.25f;
    ecs_modified(world, plane, Water);
    water_upload_frame(world);
    water_upload_frame(world);
    for (i = 0; i < 3; i ++) {
        test_assert(water_upload_frame(world) > 0);
    }

    /* Still again, it uploads nothing on an idle frame */
    value = ecs_get_mut(world, plane, Water);
    value->ripple_scale = 0;
    ecs_modified(world, plane, Water);
    water_upload_frame(world);
    water_upload_frame(world);
    for (i = 0; i < 3; i ++) {
        test_int(water_upload_frame(world), 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}
