/**
 * @file PlanarReflection.c
 * @brief Tests for the planar reflection pass: the mirrored image a view renders about a
 * PlanarReflection, read back from the reflection texture. Shapes above the plane appear
 * mirrored where the view would see their mirror image, shapes below it and the mirror's own
 * shape do not, a camera behind the plane renders none, one-sided shapes keep their front faces
 * and removing the component frees the image.
 *
 * The shading cases check the PNG captures: the shape of the plane shows the image as its
 * indirect specular light, blurred by its roughness and faded to the probes when rough, Water
 * reflects it moved by its normal map, a scene without Scene.planar_reflections draws as
 * without the component and a plane over the limit shows the probes.
 *
 * The camera stands at (0, 2, 10) and looks along -Z with a 60 degree field of view into a
 * square surface of PR_SIZE pixels; the mirror is the plane y = 0 facing +Y. Shapes are black
 * and show their emission, the scene has no other light.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Edge of the square surface */
#define PR_SIZE (64)

/** Create a headless world that renders on the main thread, without ambient light and with a
 * point light of intensity 0 in place of the default sun, its default scene with
 * Scene.planar_reflections.
 *
 * @return The world
 */
static ecs_world_t* pr_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.planar_reflections = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.sky = {0, 0, 0}, .ground = {0, 0, 0},
        .intensity = 0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});

    return world;
}

/** Add a camera at a position that looks along -Z and a lit view of it that clears to black.
 *
 * @param world The world
 * @param y Height of the camera
 * @return The view
 */
static ecs_entity_t pr_view(
    ecs_world_t *world,
    float y)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, y, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return view;
}

/** Add a surface that renders the views into a PNG.
 *
 * @param world The world
 * @param file Name of the PNG in the temporary directory
 * @return The surface
 */
static ecs_entity_t pr_surface(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = PR_SIZE, .height = PR_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return e;
}

/** Make a shape black so it shows its emission only.
 *
 * @param world The world
 * @param e The shape
 * @param r Red of the emission
 * @param g Green of the emission
 * @param b Blue of the emission
 */
static void pr_emissive(
    ecs_world_t *world,
    ecs_entity_t e,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {r, g, b}});
}

/** Add a box that shows a red emission.
 *
 * @param world The world
 * @param x X of its center
 * @param y Y of its center
 * @param z Z of its center
 * @param size Edge of the box
 * @return The box
 */
static ecs_entity_t pr_red_box(
    ecs_world_t *world,
    float x,
    float y,
    float z,
    float size)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_set(world, e, Box, {size, size, size});
    pr_emissive(world, e, 255, 0, 0);

    return e;
}

/** Add a mirror plane y = 0 facing +Y without a shape, at full resolution.
 *
 * @param world The world
 * @return The mirror
 */
static ecs_entity_t pr_mirror(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, PlanarReflection, {.resolution = 1, .clip_offset = 0.02f});

    return e;
}

/** Run frames without new device errors.
 *
 * @param world The world
 * @param count Frames to run
 */
static void pr_frames(
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

/** Read the reflection image of the nearest plane of a view, PR_SIZE square.
 *
 * @param world The world
 * @param view The view
 * @return The texels, RGBA, freed with ecs_os_free
 */
static float* pr_read(
    ecs_world_t *world,
    ecs_entity_t view)
{
    int32_t count = PR_SIZE * PR_SIZE * 4, width = 0, height = 0;
    float *out = ecs_os_calloc_n(float, count);
    test_bool(box_render_gpu_read_planar(world, view, 0, &width, &height, out, count), true);
    test_int(width, PR_SIZE);
    test_int(height, PR_SIZE);

    return out;
}

/** Get the pixel of the view a world point projects to, y down.
 *
 * @param p The point
 * @param x Receives the column
 * @param y Receives the row
 */
static void pr_project(
    const float *p,
    int32_t *x,
    int32_t *y)
{
    Camera c = {.fov = 60, .near_ = 0.1f, .far_ = 1000};
    mat4 proj, view, m;
    vec4 in = {p[0], p[1], p[2], 1}, clip;
    box_camera_projection(&c, PR_SIZE, PR_SIZE, proj);
    glm_translate_make(view, (vec3){0, -2, -10});
    glm_mat4_mul(proj, view, m);
    glm_mat4_mulv(m, in, clip);
    *x = (int32_t)floorf((clip[0] / clip[3] * 0.5f + 0.5f) * PR_SIZE);
    *y = (int32_t)floorf((0.5f - clip[1] / clip[3] * 0.5f) * PR_SIZE);
}

/** Get a texel of a reflection image.
 *
 * @param texels The texels
 * @param x Column
 * @param y Row, 0 at the top
 * @return The texel, RGBA
 */
static const float* pr_texel(
    const float *texels,
    int32_t x,
    int32_t y)
{
    test_assert(x >= 0 && x < PR_SIZE && y >= 0 && y < PR_SIZE);

    return &texels[(y * PR_SIZE + x) * 4];
}

/** Check whether a texel is red: red above 0.5, green and blue below 0.2.
 *
 * @param t The texel
 * @return Whether it is red
 */
static bool pr_red(
    const float *t)
{
    return t[0] > 0.5f && t[1] < 0.2f && t[2] < 0.2f;
}

/** Count the texels of an image with a channel above a value.
 *
 * @param texels The texels
 * @param channel The channel
 * @param above The value
 * @return Texels whose channel is above it
 */
static int32_t pr_count(
    const float *texels,
    int32_t channel,
    float above)
{
    int32_t i, result = 0;
    for (i = 0; i < PR_SIZE * PR_SIZE; i ++) {
        result += texels[i * 4 + channel] > above;
    }

    return result;
}

void PlanarReflection_box_mirrored(void) {
    ecs_world_t *world = pr_world();
    ecs_entity_t view = pr_view(world, 2);
    pr_mirror(world);
    pr_red_box(world, 0, 1.5f, 0, 2);
    pr_surface(world, "planar_box_mirrored.png");
    pr_frames(world, 3);

    float *texels = pr_read(world, view);
    int32_t mx, my, ux, uy;
    pr_project((float[]){0, -1.5f, 0}, &mx, &my);
    pr_project((float[]){0, 1.5f, 0}, &ux, &uy);
    printf("mirrored at %d %d: %f %f %f\n", mx, my, pr_texel(texels, mx, my)[0],
        pr_texel(texels, mx, my)[1], pr_texel(texels, mx, my)[2]);
    test_assert(pr_red(pr_texel(texels, mx, my)));
    test_assert(pr_red(pr_texel(texels, mx - 2, my)));
    test_assert(pr_red(pr_texel(texels, mx + 2, my)));

    // Where the view sees the box itself the reflection shows the clear color
    test_assert(!pr_red(pr_texel(texels, ux, uy)));
    test_assert(pr_texel(texels, ux, uy)[0] < 0.1f);

    // The mirror image is below the horizon of the view, the corners above it are empty
    test_assert(pr_texel(texels, 1, 1)[0] < 0.1f);
    test_assert(pr_texel(texels, PR_SIZE - 2, 1)[0] < 0.1f);

    ecs_os_free(texels);
    ecs_fini(world);
}

void PlanarReflection_below_plane_hidden(void) {
    ecs_world_t *world = pr_world();
    ecs_entity_t view = pr_view(world, 2);
    pr_mirror(world);
    pr_red_box(world, 0, -1.5f, 0, 2);
    pr_surface(world, "planar_below_plane.png");
    pr_frames(world, 3);

    float *texels = pr_read(world, view);
    int32_t mx, my;
    pr_project((float[]){0, 1.5f, 0}, &mx, &my);
    test_assert(!pr_red(pr_texel(texels, mx, my)));
    test_int(pr_count(texels, 0, 0.1f), 0);

    ecs_os_free(texels);
    ecs_fini(world);
}

void PlanarReflection_own_shape_hidden(void) {
    ecs_world_t *world = pr_world();
    ecs_entity_t view = pr_view(world, 2);

    // A green box around the plane: its upper half is in front of it, and its mirror image
    // would hide the red box behind it
    ecs_entity_t mirror = ecs_new(world);
    ecs_set(world, mirror, Position, {0, 0, 0});
    ecs_set(world, mirror, Box, {6, 2, 6});
    pr_emissive(world, mirror, 0, 255, 0);
    ecs_set(world, mirror, PlanarReflection, {.resolution = 1, .clip_offset = 0.02f});
    pr_red_box(world, 0, 2, -4, 1.5f);
    pr_surface(world, "planar_own_shape.png");
    pr_frames(world, 3);

    float *texels = pr_read(world, view);
    int32_t mx, my;
    pr_project((float[]){0, -2, -4}, &mx, &my);
    test_int(pr_count(texels, 1, 0.2f), 0);
    test_assert(pr_red(pr_texel(texels, mx, my)));

    ecs_os_free(texels);
    ecs_fini(world);
}

void PlanarReflection_camera_behind_plane(void) {
    ecs_world_t *world = pr_world();
    ecs_entity_t view = pr_view(world, -2);
    ecs_entity_t above = pr_view(world, 2);
    pr_mirror(world);
    pr_red_box(world, 0, 1.5f, 0, 2);
    pr_surface(world, "planar_camera_behind.png");
    pr_frames(world, 3);

    test_bool(box_render_gpu_read_planar(world, view, 0, NULL, NULL, NULL, 0), false);
    test_bool(box_render_gpu_read_planar(world, view, 1, NULL, NULL, NULL, 0), false);
    test_bool(box_render_gpu_read_planar(world, above, 0, NULL, NULL, NULL, 0), true);

    ecs_fini(world);
}

void PlanarReflection_one_sided_winding(void) {
    ecs_world_t *world = pr_world();
    ecs_entity_t view = pr_view(world, 2);
    pr_mirror(world);

    // A quad facing the camera, culled from behind
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Position, {0, 1.5f, 0});
    ecs_set(world, quad, Quad, {2, 2});
    pr_emissive(world, quad, 255, 0, 0);
    pr_surface(world, "planar_one_sided.png");
    pr_frames(world, 3);

    float *texels = pr_read(world, view);
    int32_t mx, my;
    pr_project((float[]){0, -1.5f, 0}, &mx, &my);
    test_assert(pr_red(pr_texel(texels, mx, my)));

    // Turned away from the camera it shows nothing, in the view and in the reflection
    ecs_set(world, quad, Rotation, {0, GLM_PIf, 0});
    pr_frames(world, 3);
    ecs_os_free(texels);
    texels = pr_read(world, view);
    test_int(pr_count(texels, 0, 0.1f), 0);

    ecs_os_free(texels);
    ecs_fini(world);
}

void PlanarReflection_remove_frees_texture(void) {
    ecs_world_t *world = pr_world();
    ecs_entity_t view = pr_view(world, 2);
    ecs_entity_t mirror = ecs_new(world);
    ecs_set(world, mirror, Position, {0, 0, 0});
    ecs_add(world, mirror, PlanarReflection);
    pr_red_box(world, 0, 1.5f, 0, 2);
    pr_surface(world, "planar_remove.png");
    pr_frames(world, 3);

    int32_t width = 0, height = 0;
    test_bool(box_render_gpu_read_planar(world, view, 0, &width, &height, NULL, 0), true);
    test_int(width, PR_SIZE / 2);
    test_int(height, PR_SIZE / 2);

    ecs_remove(world, mirror, PlanarReflection);
    pr_frames(world, 3);
    test_bool(box_render_gpu_read_planar(world, view, 0, NULL, NULL, NULL, 0), false);

    // Set again, it renders again; deleting the view frees it
    ecs_add(world, mirror, PlanarReflection);
    pr_frames(world, 3);
    test_bool(box_render_gpu_read_planar(world, view, 0, NULL, NULL, NULL, 0), true);
    ecs_delete(world, view);
    pr_frames(world, 3);
    test_bool(box_render_gpu_read_planar(world, view, 0, NULL, NULL, NULL, 0), false);

    ecs_fini(world);
}

void PlanarReflection_sky_mirrored(void) {
    ecs_world_t *world = pr_world();
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {.planar_reflections = true});
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, Rotation, {1.3f, 0.5f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_add(world, scene, Atmosphere);
    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->units_per_km = 100;
    a->sun = sun;
    ecs_modified(world, scene, Atmosphere);
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    ecs_entity_t mirror = pr_mirror(world);
    ecs_add_pair(world, mirror, EcsChildOf, scene);
    pr_surface(world, "planar_sky.png");
    pr_frames(world, 3);

    // Below the horizon the reflection shows the sky above it: bright at the horizon and bluer
    // toward the zenith, without a seam where the oblique near plane would bend its rays
    float *texels = pr_read(world, view);
    float previous = 0;
    int32_t y;
    for (y = PR_SIZE / 2 + 2; y < PR_SIZE; y ++) {
        const float *t = pr_texel(texels, PR_SIZE / 4, y);
        float ratio = t[2] / t[0];
        test_assert(t[0] > 0.01f && t[2] > t[0]);
        if (previous) {
            test_assert(ratio > previous - 0.05f && ratio < previous + 0.4f);
        }

        previous = ratio;
    }

    test_assert(pr_texel(texels, PR_SIZE / 4, PR_SIZE - 1)[2] /
        pr_texel(texels, PR_SIZE / 4, PR_SIZE - 1)[0] >
        pr_texel(texels, PR_SIZE / 4, PR_SIZE / 2 + 2)[2] /
        pr_texel(texels, PR_SIZE / 4, PR_SIZE / 2 + 2)[0] + 0.5f);

    ecs_os_free(texels);
    ecs_fini(world);
}

void PlanarReflection_water_hidden(void) {
    ecs_world_t *world = pr_world();
    ecs_singleton_set(world, Scene, {.shader_features.Extended.transmission = true,
        .shader_features.Extended.water = true, .planar_reflections = true});
    ecs_entity_t view = pr_view(world, 2);
    pr_mirror(world);

    // A green Water box above the plane, not the owner of the PlanarReflection, in front of the
    // mirror image of a red box behind it
    ecs_entity_t water = ecs_new(world);
    ecs_set(world, water, Position, {0, 1.5f, 0});
    ecs_set(world, water, Box, {2, 2, 2});
    pr_emissive(world, water, 0, 255, 0);
    ecs_add(world, water, Water);
    Water *w = ecs_get_mut(world, water, Water);
    w->shallow_color = (Rgb){0, 255, 0};
    w->deep_color = (Rgb){0, 255, 0};
    ecs_modified(world, water, Water);
    pr_red_box(world, 0, 2, -4, 1.5f);
    pr_surface(world, "planar_water_hidden.png");
    pr_frames(world, 3);

    float *texels = pr_read(world, view);
    int32_t mx, my, wx, wy;
    pr_project((float[]){0, -2, -4}, &mx, &my);
    pr_project((float[]){0, -1.5f, 0}, &wx, &wy);
    test_int(pr_count(texels, 1, 0.2f), 0);
    test_assert(pr_red(pr_texel(texels, mx, my)));
    test_assert(pr_texel(texels, wx, wy)[1] < 0.1f);

    ecs_os_free(texels);
    ecs_fini(world);
}

/** Render frames into a capture of PR_SIZE square and load it.
 *
 * @param world The world
 * @param file Name of the PNG in the temporary directory
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* pr_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t surface = pr_surface(world, file);
    pr_frames(world, 3);
    ecs_delete(world, surface);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, PR_SIZE);
    test_int(h, PR_SIZE);

    return px;
}

/** Get a pixel of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row, 0 at the top
 * @return The pixel, RGBA
 */
static const unsigned char* pr_px(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    test_assert(x >= 0 && x < PR_SIZE && y >= 0 && y < PR_SIZE);

    return &px[(y * PR_SIZE + x) * 4];
}

/** Get the largest difference of a channel between two captures over a rectangle.
 *
 * @param a The first capture
 * @param b The second capture
 * @param x0 First column
 * @param y0 First row
 * @param x1 Column past the last
 * @param y1 Row past the last
 * @return The largest difference of a color channel
 */
static int32_t pr_diff(
    const unsigned char *a,
    const unsigned char *b,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    int32_t x, y, c, result = 0;
    for (y = y0; y < y1; y ++) {
        for (x = x0; x < x1; x ++) {
            for (c = 0; c < 3; c ++) {
                int32_t d = abs((int32_t)pr_px(a, x, y)[c] - (int32_t)pr_px(b, x, y)[c]);
                result = d > result ? d : result;
            }
        }
    }

    return result;
}

/** Add a floor of white metal whose top lies on the plane y = 0, 40 units square, optionally
 * with a PlanarReflection at full resolution and no distortion.
 *
 * @param world The world
 * @param roughness Its roughness
 * @param planar Whether it has a PlanarReflection
 * @return The floor
 */
static ecs_entity_t pr_floor(
    ecs_world_t *world,
    float roughness,
    bool planar)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {40, 0.02f, 40});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = roughness});
    if (planar) {
        ecs_set(world, e, PlanarReflection, {.resolution = 1, .clip_offset = 0.02f,
            .distortion = 0});
    }

    return e;
}

/** Count the pixels of a row of a capture with a red between two values, exclusive.
 *
 * @param px The capture
 * @param y The row
 * @param lo The lower bound
 * @param hi The upper bound
 * @return The pixels
 */
static int32_t pr_row_between(
    const unsigned char *px,
    int32_t y,
    int32_t lo,
    int32_t hi)
{
    int32_t x, result = 0;
    for (x = 0; x < PR_SIZE; x ++) {
        int32_t r = pr_px(px, x, y)[0];
        result += r > lo && r < hi;
    }

    return result;
}

/** Get the red weighted mean column of a row of a capture, -1 without red.
 *
 * @param px The capture
 * @param y The row
 * @return The column
 */
static float pr_row_center(
    const unsigned char *px,
    int32_t y)
{
    float sum = 0, weight = 0;
    int32_t x;
    for (x = 0; x < PR_SIZE; x ++) {
        const unsigned char *p = pr_px(px, x, y);
        float r = (float)p[0] - (float)(p[1] > p[2] ? p[1] : p[2]);
        if (r > 20) {
            sum += r * (float)x;
            weight += r;
        }
    }

    return weight > 0 ? sum / weight : -1.0f;
}

void PlanarReflection_shading_mirror_floor(void) {
    ecs_world_t *world = pr_world();
    pr_view(world, 2);
    ecs_entity_t floor = pr_floor(world, 0, true);
    pr_red_box(world, 0, 1.5f, 0, 2);
    unsigned char *px = pr_capture(world, "planar_shading_mirror.png");

    // The floor shows the box mirrored below it: the floor has no other light to reflect
    int32_t mx, my;
    pr_project((float[]){0, -1.5f, 0}, &mx, &my);
    const unsigned char *p = pr_px(px, mx, my);
    printf("mirrored %d %d: %d %d %d\n", mx, my, p[0], p[1], p[2]);
    test_assert(p[0] > 150 && p[1] < 50 && p[2] < 50);

    // Beside the mirror image the floor reflects nothing
    test_assert(pr_px(px, 2, PR_SIZE - 2)[0] < 30);

    // Without the component the floor is black there
    ecs_remove(world, floor, PlanarReflection);
    stbi_image_free(px);
    px = pr_capture(world, "planar_shading_mirror_without.png");
    test_assert(pr_px(px, mx, my)[0] < 30);

    stbi_image_free(px);
    ecs_fini(world);
}

void PlanarReflection_shading_roughness_blurs(void) {
    ecs_world_t *world = pr_world();
    pr_view(world, 2);
    ecs_entity_t floor = pr_floor(world, 0, true);
    pr_red_box(world, 0, 1.5f, 0, 2);
    unsigned char *sharp = pr_capture(world, "planar_shading_sharp.png");

    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 0.4f});
    unsigned char *rough = pr_capture(world, "planar_shading_rough.png");

    // The mirror image keeps its red in the middle, but its edges spread over more pixels
    int32_t mx, my;
    pr_project((float[]){0, -1.5f, 0}, &mx, &my);
    int32_t sharp_edge = pr_row_between(sharp, my, 24, 140);
    int32_t rough_edge = pr_row_between(rough, my, 24, 140);
    printf("edge pixels sharp %d rough %d, middle %d\n", sharp_edge, rough_edge,
        pr_px(rough, mx, my)[0]);
    test_assert(pr_px(rough, mx, my)[0] > 60);
    test_assert(rough_edge > sharp_edge + 2);

    stbi_image_free(sharp);
    stbi_image_free(rough);
    ecs_fini(world);
}

void PlanarReflection_shading_rough_matches_probe(void) {
    ecs_world_t *world = pr_world();
    ecs_singleton_set(world, AmbientLight, {.sky = {40, 80, 255}, .ground = {40, 40, 40},
        .intensity = 1});
    pr_view(world, 2);
    ecs_entity_t floor = pr_floor(world, 1, true);
    pr_red_box(world, 0, 1.5f, 0, 2);
    unsigned char *with = pr_capture(world, "planar_shading_rough_with.png");

    ecs_remove(world, floor, PlanarReflection);
    unsigned char *without = pr_capture(world, "planar_shading_rough_without.png");

    // At roughness 1 the floor shows the ambient it reflects without the image
    int32_t mx, my;
    pr_project((float[]){0, -1.5f, 0}, &mx, &my);
    printf("rough floor %d %d %d, without %d %d %d\n", pr_px(with, mx, my)[0],
        pr_px(with, mx, my)[1], pr_px(with, mx, my)[2], pr_px(without, mx, my)[0],
        pr_px(without, mx, my)[1], pr_px(without, mx, my)[2]);
    test_assert(pr_px(with, mx, my)[2] > 20);
    test_assert(pr_diff(with, without, 0, PR_SIZE / 2 + 2, PR_SIZE, PR_SIZE) <= 3);

    stbi_image_free(with);
    stbi_image_free(without);
    ecs_fini(world);
}

/** Create a normal map of one tilt toward +U everywhere: (0.5, 0, 0.866) in tangent space.
 *
 * @param world The world
 * @return The texture, loaded
 */
static ecs_entity_t pr_tilt(
    ecs_world_t *world)
{
    uint8_t px[8 * 8 * 4];
    int32_t i, frames = 0;
    for (i = 0; i < 8 * 8; i ++) {
        px[i * 4 + 0] = 191;
        px[i * 4 + 1] = 128;
        px[i * 4 + 2] = 238;
        px[i * 4 + 3] = 255;
    }

    char *file = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir,
        "planar_tilt.png");
    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    ecs_os_free(file);

    return tex;
}

void PlanarReflection_shading_water(void) {
    ecs_world_t *world = pr_world();
    ecs_singleton_set(world, Scene, {.shader_features.Extended.transmission = true,
        .shader_features.Extended.water = true, .planar_reflections = true});
    pr_view(world, 2);

    // A black, opaque bodied Water plane that reflects a bright red box above it, turned about
    // Y so the U direction of its normal map has a part across the screen
    ecs_entity_t water = ecs_new(world);
    ecs_set(world, water, Position, {0, 0, 0});
    ecs_set(world, water, Rotation, {0, GLM_PI_4f, 0});
    ecs_set(world, water, Box, {40, 0.02f, 40});
    ecs_set(world, water, Rgb, {255, 255, 255});
    ecs_set(world, water, PbrMaterial, {.metallic = 0, .roughness = 0.05f});
    ecs_set(world, water, Water, {.shallow_color = {0, 0, 0}, .deep_color = {0, 0, 0},
        .depth = 1, .visibility = 0.001f, .absorption = {255, 255, 255}, .foam_scale = 1});
    ecs_set(world, water, PlanarReflection, {.resolution = 1, .clip_offset = 0.02f,
        .distortion = 0.2f});
    ecs_entity_t box = pr_red_box(world, 0, 1.5f, 0, 2);
    ecs_set(world, box, Emissive, {.strength = 6, .color = {255, 0, 0}});
    unsigned char *flat = pr_capture(world, "planar_shading_water_flat.png");

    int32_t mx, my;
    pr_project((float[]){0, -1.5f, 0}, &mx, &my);
    const unsigned char *p = pr_px(flat, mx, my);
    printf("water mirrored %d %d: %d %d %d\n", mx, my, p[0], p[1], p[2]);
    test_assert(p[0] > 60 && p[0] > p[1] + 40 && p[0] > p[2] + 40);

    // A normal map tilted toward +U moves the mirror image sideways by about the distortion
    // times the tilt across the screen, 0.2 * 0.5 * 0.7 of the image height
    ecs_set(world, water, PbrTextures, {.normal = pr_tilt(world)});
    unsigned char *tilted = pr_capture(world, "planar_shading_water_tilted.png");
    float flat_center = pr_row_center(flat, my);
    float tilted_center = pr_row_center(tilted, my);
    printf("water mirror center flat %f tilted %f\n", flat_center, tilted_center);
    test_assert(flat_center >= 0 && tilted_center >= 0);
    test_assert(fabsf(tilted_center - flat_center) > 2.0f);

    // Without the component the water reflects no red
    ecs_remove(world, water, PlanarReflection);
    unsigned char *without = pr_capture(world, "planar_shading_water_without.png");
    test_assert(pr_px(without, mx, my)[0] < 40);

    stbi_image_free(flat);
    stbi_image_free(tilted);
    stbi_image_free(without);
    ecs_fini(world);
}

void PlanarReflection_shading_feature_off(void) {
    ecs_world_t *world = pr_world();
    ecs_singleton_set(world, Scene, {0});
    ecs_singleton_set(world, AmbientLight, {.sky = {40, 80, 255}, .ground = {40, 40, 40},
        .intensity = 1});
    ecs_entity_t view = pr_view(world, 2);
    ecs_entity_t floor = pr_floor(world, 0.2f, true);
    pr_red_box(world, 0, 1.5f, 0, 2);
    unsigned char *with = pr_capture(world, "planar_shading_off_with.png");

    // No reflection pass runs
    test_bool(box_render_gpu_read_planar(world, view, 0, NULL, NULL, NULL, 0), false);

    ecs_remove(world, floor, PlanarReflection);
    unsigned char *without = pr_capture(world, "planar_shading_off_without.png");
    test_int(pr_diff(with, without, 0, 0, PR_SIZE, PR_SIZE), 0);

    stbi_image_free(with);
    stbi_image_free(without);
    ecs_fini(world);
}

void PlanarReflection_shading_over_limit(void) {
    ecs_world_t *world = pr_world();
    ecs_singleton_set(world, AmbientLight, {.sky = {40, 80, 255}, .ground = {40, 40, 40},
        .intensity = 1});
    ecs_entity_t view = pr_view(world, 2);

    // Three floor strips side by side, each lower than the one before: the camera is nearest
    // to the planes of the first two, which render, and farthest from the third
    ecs_entity_t strips[3];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        strips[i] = ecs_new(world);
        ecs_set(world, strips[i], Position, {-4.0f + 4.0f * (float)i, -0.3f * (float)i, 0});
        ecs_set(world, strips[i], Box, {3.8f, 0.02f, 40});
        ecs_set(world, strips[i], Rgb, {255, 255, 255});
        ecs_set(world, strips[i], PbrMaterial, {.metallic = 1, .roughness = 0.1f});
        ecs_set(world, strips[i], PlanarReflection, {.resolution = 1, .clip_offset = 0.02f,
            .distortion = 0});
    }

    pr_red_box(world, 4, 1.5f, -2, 1.5f);
    unsigned char *with = pr_capture(world, "planar_shading_limit_with.png");
    test_bool(box_render_gpu_read_planar(world, view, 0, NULL, NULL, NULL, 0), true);
    test_bool(box_render_gpu_read_planar(world, view, 1, NULL, NULL, NULL, 0), true);
    test_bool(box_render_gpu_read_planar(world, view, 2, NULL, NULL, NULL, 0), false);

    // The third strip shows the probes, as it does without the component
    ecs_remove(world, strips[2], PlanarReflection);
    unsigned char *without = pr_capture(world, "planar_shading_limit_without.png");
    int32_t x0, y0, x1, y1, near_y, far_y;
    pr_project((float[]){2.6f, -0.6f, 0}, &x0, &near_y);
    pr_project((float[]){5.4f, -0.6f, -2}, &x1, &far_y);
    x0 = glm_imin(glm_imax(x0 + 1, 0), PR_SIZE);
    x1 = glm_imin(glm_imax(x1, 0), PR_SIZE);
    y0 = glm_imin(glm_imax(far_y, 0), PR_SIZE);
    y1 = glm_imin(glm_imax(near_y, 0), PR_SIZE);
    printf("third strip %d..%d x %d..%d\n", x0, x1, y0, y1);
    test_assert(x1 > x0 && y1 > y0);
    test_assert(pr_px(with, (x0 + x1) / 2, (y0 + y1) / 2)[2] > 20);
    test_assert(pr_diff(with, without, x0, y0, x1, y1) <= 3);

    stbi_image_free(with);
    stbi_image_free(without);
    ecs_fini(world);
}
