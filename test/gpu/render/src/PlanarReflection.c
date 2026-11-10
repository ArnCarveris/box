/**
 * @file PlanarReflection.c
 * @brief Tests for the planar reflection pass: the mirrored image a view renders about a
 * PlanarReflection, read back from the reflection texture. Shapes above the plane appear
 * mirrored where the view would see their mirror image, shapes below it and the mirror's own
 * shape do not, a camera behind the plane renders none, one-sided shapes keep their front faces
 * and removing the component frees the image.
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

/** Edge of the square surface */
#define PR_SIZE (64)

/** Create a headless world that renders on the main thread, without ambient light and with a
 * point light of intensity 0 in place of the default sun.
 *
 * @return The world
 */
static ecs_world_t* pr_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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
    ecs_set(world, scene, Scene, {0});
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
