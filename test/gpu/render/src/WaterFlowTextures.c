/**
 * @file WaterFlowTextures.c
 * @brief Tests for WaterFlow.textures: the foam texture of a Water moving with the flow, checked
 * on PNG captures.
 *
 * The cases look straight down from a camera 10 units above the water plane y = 0 into a square
 * capture of FLOW_SIZE pixels, with a 60 degree field of view: +x runs to the right of the
 * image, +z down. A gray floor half a unit below the water keeps all of it inside a foam band of
 * 1, so the foam texture alone decides where the white foam covers the dark blue body. The scene
 * is lit by a white ambient only.
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
#define FLOW_SIZE (64)

/** Edge of the foam texture */
#define FLOW_TEXTURE (16)

/** Seconds of a frame */
#define FLOW_DT (1.0f / 60)

/** Create a headless world that renders on the main thread, with Scene.water, a white ambient,
 * no sun and a camera 10 units above the origin looking down.
 *
 * @return The world
 */
static ecs_world_t* flow_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.reflections = true, .environment = true,
        .shader_features.Extended.transmission = true, .shader_features.Extended.triplanar = true,
        .shader_features.Extended.texture_scroll = true, .shader_features.Extended.water = true});
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
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    /* The floor half a unit below the water */
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.5f, 0});
    ecs_set(world, floor, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, floor, Quad, {30, 30});
    ecs_set(world, floor, Rgb, {128, 128, 128});
    ecs_set(world, floor, PbrMaterial, {.metallic = 0, .roughness = 1});

    return world;
}

/** Run frames of FLOW_DT without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void flow_frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t before, after;
    bool started = box_render_gpu_stats(world, &before);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, FLOW_DT);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &after), true);
    if (started) {
        test_int(after.errors, before.errors);
    } else {
        test_int(after.errors, 0);
    }
}

/** Create the foam texture: smooth blobs, the red 0.5 + 0.5 sin(u) sin(v) over one period per
 * texture on both axes, written into the temporary folder of the world and loaded.
 *
 * @param world The world
 * @return The texture, loaded
 */
static ecs_entity_t flow_foam_texture(
    ecs_world_t *world)
{
    uint8_t px[FLOW_TEXTURE * FLOW_TEXTURE * 4];
    int32_t x, y;
    for (y = 0; y < FLOW_TEXTURE; y ++) {
        for (x = 0; x < FLOW_TEXTURE; x ++) {
            float u = (x + 0.5f) / FLOW_TEXTURE * 2 * GLM_PIf;
            float v = (y + 0.5f) / FLOW_TEXTURE * 2 * GLM_PIf;
            uint8_t r = (uint8_t)(127.5f + 127.5f * sinf(u) * sinf(v));
            uint8_t *p = &px[(y * FLOW_TEXTURE + x) * 4];
            p[0] = p[1] = p[2] = r;
            p[3] = 255;
        }
    }

    char *file = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir,
        "water_flow_foam.png");
    test_assert(stbi_write_png(file, FLOW_TEXTURE, FLOW_TEXTURE, 4, px,
        FLOW_TEXTURE * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, FLOW_DT);
        box_render_sync(world);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    ecs_os_free(file);

    return tex;
}

/** Get the Water of the cases: a dark blue body that hides the floor, a white foam over a band
 * of 1 broken up by the foam texture, no refraction.
 *
 * @param foam The foam texture
 * @param scale Its foam_scale
 * @return The Water
 */
static Water flow_water(
    ecs_entity_t foam,
    float scale)
{
    return (Water){
        .shallow_color = {0, 0, 80},
        .deep_color = {0, 0, 80},
        .depth = 3,
        .visibility = 0.001f,
        .absorption = {255, 255, 255},
        .foam_color = {255, 255, 255},
        .foam_width = 1,
        .foam_texture = foam,
        .foam_scale = scale
    };
}

/** Add a Water quad at y = 0 facing up.
 *
 * @param world The world
 * @param x World X of its center
 * @param width World units along X
 * @param depth World units along Z
 * @param water Its Water
 * @param vx WaterFlow velocity X
 * @param vz WaterFlow velocity Z
 * @param textures WaterFlow.textures
 * @return The quad
 */
static ecs_entity_t flow_quad(
    ecs_world_t *world,
    float x,
    float width,
    float depth,
    const Water *water,
    float vx,
    float vz,
    bool textures)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Rotation, {-GLM_PI_2f, 0, 0});
    ecs_set(world, e, Quad, {width, depth});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0.3f});
    ecs_set_ptr(world, e, Water, water);
    ecs_set(world, e, WaterFlow, {.velocity = {vx, vz}, .textures = textures});

    return e;
}

/** Run frames, render a capture and load it. The capture surface is deleted afterwards.
 *
 * @param world The world
 * @param file Name of the PNG
 * @param frames Frames to run before the capture is read
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* flow_render(
    ecs_world_t *world,
    const char *file,
    int32_t frames)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = FLOW_SIZE, .height = FLOW_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    flow_frames(world, frames);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, FLOW_SIZE);
    test_int(h, FLOW_SIZE);
    ecs_os_free(path);
    ecs_delete(world, e);

    return px;
}

/** Get the mean absolute difference of the red of two captures.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The mean difference, 0..255
 */
static double flow_difference(
    const unsigned char *a,
    const unsigned char *b)
{
    double sum = 0;
    int32_t i;
    for (i = 0; i < FLOW_SIZE * FLOW_SIZE; i ++) {
        sum += abs((int)a[i * 4] - (int)b[i * 4]);
    }

    return sum / (FLOW_SIZE * FLOW_SIZE);
}

/** Estimate the columns the pattern of a capture moved by in a second one: with b(x) close to
 * a(x - d) for a small d, b - a = -d da/dx, so d is the least squares fit over every pixel with
 * a neighbour on both sides.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The columns, positive when the pattern moved to the right (+x)
 */
static double flow_motion(
    const unsigned char *a,
    const unsigned char *b)
{
    double num = 0, den = 0;
    int32_t row, col;
    for (row = 0; row < FLOW_SIZE; row ++) {
        for (col = 1; col < FLOW_SIZE - 1; col ++) {
            int32_t i = row * FLOW_SIZE + col;
            double g = 0.5 * ((double)a[(i + 1) * 4] - (double)a[(i - 1) * 4]);
            num += ((double)b[i * 4] - (double)a[i * 4]) * g;
            den += g * g;
        }
    }

    return den > 0 ? -num / den : 0;
}

/** Get the mean motion (flow_motion) of the foam pattern of a flowing Water with textures over
 * a few pairs of captures two frames apart, spread over a period of the flow mapping.
 *
 * @param vx WaterFlow velocity X
 * @return The mean columns per pair
 */
static double flow_drift(
    float vx)
{
    ecs_world_t *world = flow_world();
    ecs_entity_t foam = flow_foam_texture(world);
    Water w = flow_water(foam, 2);
    flow_quad(world, 0, 16, 16, &w, vx, 0, true);

    double sum = 0;
    int32_t i;
    for (i = 0; i < 6; i ++) {
        unsigned char *a = flow_render(world, "water_drift_a.png", 8);
        unsigned char *b = flow_render(world, "water_drift_b.png", 2);
        sum += flow_motion(a, b);
        stbi_image_free(a);
        stbi_image_free(b);
    }

    ecs_fini(world);

    return sum / 6;
}

/** Get the mean absolute difference of the red of two neighbouring columns of a capture over
 * all rows.
 *
 * @param px The capture
 * @param col The left column; the right one is col + 1
 * @return The mean difference, 0..255
 */
static double flow_column_step(
    const unsigned char *px,
    int32_t col)
{
    double sum = 0;
    int32_t row;
    test_assert(col >= 0 && col + 1 < FLOW_SIZE);
    for (row = 0; row < FLOW_SIZE; row ++) {
        sum += abs((int)px[(row * FLOW_SIZE + col) * 4] -
            (int)px[(row * FLOW_SIZE + col + 1) * 4]);
    }

    return sum / FLOW_SIZE;
}

/** Get the mean of flow_column_step over the columns of a capture, but those within 2 of the
 * middle, where the seam of the cases lies.
 *
 * @param px The capture
 * @return The mean difference, 0..255
 */
static double flow_column_steps(
    const unsigned char *px)
{
    double sum = 0;
    int32_t col, count = 0;
    for (col = 0; col < FLOW_SIZE - 1; col ++) {
        if (abs(col - (FLOW_SIZE / 2 - 1)) <= 2) {
            continue;
        }

        sum += flow_column_step(px, col);
        count ++;
    }

    return sum / count;
}

/** Get the mean red of a capture.
 *
 * @param px The capture
 * @return The mean, 0..255
 */
static double flow_mean(
    const unsigned char *px)
{
    double sum = 0;
    int32_t i;
    for (i = 0; i < FLOW_SIZE * FLOW_SIZE; i ++) {
        sum += px[i * 4];
    }

    return sum / (FLOW_SIZE * FLOW_SIZE);
}

void WaterFlowTextures_foam_moves_with_flow(void) {
    ecs_world_t *world = flow_world();
    ecs_entity_t foam = flow_foam_texture(world);
    Water w = flow_water(foam, 2);
    flow_quad(world, 0, 16, 16, &w, 2, 0, true);

    /* The foam pattern covers part of the water: some white, some blue */
    unsigned char *a = flow_render(world, "water_flow_a.png", 4);
    double mean = flow_mean(a);
    printf("flowing mean red %.1f\n", mean);
    test_assert(mean > 20 && mean < 235);

    /* A quarter of a second later the pattern moved */
    unsigned char *b = flow_render(world, "water_flow_b.png", 15);
    unsigned char *c = flow_render(world, "water_flow_c.png", 15);
    double ab = flow_difference(a, b), bc = flow_difference(b, c);
    printf("flowing differences %.2f %.2f\n", ab, bc);
    test_assert(ab > 2);
    test_assert(bc > 2);


    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
    ecs_fini(world);

    /* It moves downstream: 6 units per second move it about a pixel in two frames */
    double east = flow_drift(6), west = flow_drift(-6);
    printf("drift east %.3f west %.3f\n", east, west);
    test_assert(east > 0.3);
    test_assert(west < -0.3);
}

void WaterFlowTextures_foam_still_without_textures(void) {
    ecs_world_t *world = flow_world();
    ecs_entity_t foam = flow_foam_texture(world);
    Water w = flow_water(foam, 2);
    flow_quad(world, 0, 16, 16, &w, 2, 0, false);

    /* The same flowing Water without textures keeps its foam pattern still */
    unsigned char *a = flow_render(world, "water_still_a.png", 4);
    double mean = flow_mean(a);
    printf("still mean red %.1f\n", mean);
    test_assert(mean > 20 && mean < 235);
    unsigned char *b = flow_render(world, "water_still_b.png", 15);
    unsigned char *c = flow_render(world, "water_still_c.png", 15);
    double ab = flow_difference(a, b), bc = flow_difference(b, c);
    printf("still differences %.2f %.2f\n", ab, bc);
    test_assert(ab < 0.5);
    test_assert(bc < 0.5);

    stbi_image_free(a);
    stbi_image_free(b);
    stbi_image_free(c);
    ecs_fini(world);
}

/** Render the seam case: two Water quads that meet at x = 0, the left one flowing east, the
 * right one south, both with flowing textures projected from the world (MappingWorld) so their
 * texture space is the same, with a coarse foam pattern (a period of 8 units), with or without
 * a field of WaterRipples over both. Returns the largest ratio over a few captures of the step
 * across the seam to the mean step between other columns.
 *
 * @param field Whether a field of WaterRipples covers the quads
 * @return The ratio
 */
static double flow_seam_ratio(
    bool field)
{
    ecs_world_t *world = flow_world();
    ecs_entity_t foam = flow_foam_texture(world);
    Water w = flow_water(foam, 0.25f);
    ecs_entity_t left = flow_quad(world, -4, 8, 16, &w, 3, 0, true);
    ecs_entity_t right = flow_quad(world, 4, 8, 16, &w, 0, 3, true);
    ecs_set(world, left, TextureMapping, {.mode = MappingWorld, .size = 2});
    ecs_set(world, right, TextureMapping, {.mode = MappingWorld, .size = 2});
    if (field) {
        /* The Water of the field has no shape of its own */
        ecs_entity_t pond = ecs_new(world);
        ecs_set(world, pond, Position, {0, 0, 0});
        ecs_add(world, pond, Water);
        ecs_set(world, pond, WaterRipples, {.size = 24, .resolution = 128,
            .wave_speed = 1, .strength = 1});
    }

    double worst = 0;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        char file[64];
        snprintf(file, sizeof(file), "water_flow_seam_%d_%d.png", field ? 1 : 0, i);
        unsigned char *px = flow_render(world, file, i ? 13 : 4);
        double seam = flow_column_step(px, FLOW_SIZE / 2 - 1);
        double other = flow_column_steps(px);
        double ratio = seam / (other + 1);
        printf("%s seam %.2f, others %.2f\n", field ? "field" : "no field", seam, other);
        if (ratio > worst) {
            worst = ratio;
        }

        stbi_image_free(px);
    }

    ecs_fini(world);

    return worst;
}

void WaterFlowTextures_no_seam_in_field(void) {
    /* Inside the field the velocity blends across the seam, so the pattern steps across it no
     * more than between other columns */
    double field = flow_seam_ratio(true);
    printf("seam ratio with a field %.2f\n", field);
    test_assert(field < 1.2);

    /* Each quad flowing on its own breaks the pattern at the seam */
    double alone = flow_seam_ratio(false);
    printf("seam ratio without a field %.2f\n", alone);
    test_assert(alone > 1.5 * field);
}
