/**
 * @file ReflectionShading.c
 * @brief Tests for the indirect specular light the forward pass takes from reflection probes.
 *
 * Most scenes look at a mirror ball, a white metal sphere of the smallest roughness, so the
 * center pixel shows what the probes reflect straight back toward the camera.
 *
 * The probes capture the inside of small enclosures of emissive walls, wrapped in black walls
 * that keep their light in; with no ambient and no light, a ball that samples none is black.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the probe cubemaps in texels */
#define RESOLUTION (32)

/** Frames a render runs, the captures land in the first */
#define FRAMES (3)

/** Create a headless world that renders with the GPU on the main thread.
 *
 * @return The world
 */
static ecs_world_t* world_new(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    return world;
}

/** Create a dark scene: no ambient light and a directional light of intensity 0, so the
 * scene has no default sun.
 *
 * @param world The world
 * @param bake The ReflectionBake of the scene, NULL for none
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    const ReflectionBake *bake)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    if (bake) {
        ecs_set_ptr(world, scene, ReflectionBake, bake);
    }

    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});

    return scene;
}

/** Add a static box to a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param position Center of the box
 * @param size Size of the box
 * @param color Base color
 * @param emissive Emission, strength 0 for none
 */
static void box_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float position[3],
    const float size[3],
    Rgb color,
    Emissive emissive)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Box, {size[0], size[1], size[2]});
    ecs_set_ptr(world, e, Rgb, &color);
    if (emissive.strength > 0) {
        ecs_set_ptr(world, e, Emissive, &emissive);
    }
}

/** Add six emissive walls around a point, wrapped in six black walls that keep their light
 * from the rest of the scene.
 *
 * @param world The world
 * @param scene The scene
 * @param x Position of the center along x
 * @param color Emission color of the inner walls
 */
static void enclosure_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x,
    Rgb color)
{
    int32_t i, shell;
    for (shell = 0; shell < 2; shell ++) {
        float distance = shell ? 1.4f : 1.1f, edge = shell ? 3.2f : 2.6f;
        for (i = 0; i < 6; i ++) {
            int32_t axis = i / 2;
            float position[3] = {x, 0, 0}, size[3] = {edge, edge, edge};
            position[axis] += i % 2 ? -distance : distance;
            size[axis] = 0.2f;
            box_new(world, scene, position, size, (Rgb){0, 0, 0}, shell
                ? (Emissive){0}
                : (Emissive){.strength = 1, .color = color});
        }
    }
}

/** Add a reflection probe centered on the origin that captures from a point along x.
 *
 * @param world The world
 * @param scene The scene
 * @param capture Capture point along x, the center of an enclosure
 * @param priority Priority of the probe
 * @param extent Half size of its influence box on every axis
 * @param blend Fade distance at the edges of its influence box
 * @return The probe
 */
static ecs_entity_t probe_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float capture,
    int32_t priority,
    float extent,
    float blend)
{
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 0, 0});
    ecs_set(world, probe, ReflectionProbe, {
        .extent = {extent, extent, extent},
        .blend = blend,
        .priority = priority,
        .offset = {capture, 0, 0}
    });

    return probe;
}

/** Add a mirror ball of radius 1 to a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param x Position of the center along x
 * @param y Position of the center along y
 */
static void mirror_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x,
    float y)
{
    ecs_entity_t ball = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, ball, Position, {x, y, 0});
    ecs_set(world, ball, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    ecs_set(world, ball, Rgb, {255, 255, 255});
    ecs_set(world, ball, PbrMaterial, {.metallic = 1, .roughness = 0});
}

/** Add a camera looking along -z from 4 units in front of a point and a view of it.
 *
 * @param world The world
 * @param scene The scene of the camera
 * @param x Position of the point along x
 * @param y Position of the point along y
 * @param lighting Light settings of the view
 */
static void view_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float x,
    float y,
    LightSettings lighting)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {x, y, 4});
    ecs_set(world, camera, Camera, {.fov = 30, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting = lighting,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
}

/** Render frames of a world into a 64x48 image.
 *
 * @param world The world, deleted
 * @param w Receives the width
 * @param h Receives the height
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* render(
    ecs_world_t *world,
    int *w,
    int *h)
{
    int32_t frame;
    int n = 0;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "shade.png",
        .resolution_scale = 1});
    for (frame = 0; frame < FRAMES; frame ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/shade.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    unsigned char *result = stbi_load(path, w, h, &n, 4);
    test_assert(result != NULL);
    test_int(*w, 64);
    test_int(*h, 48);
    ecs_os_free(path);
    ecs_fini(world);

    return result;
}

/** Get the center pixel of an image, the middle of the mirror ball.
 *
 * @param pixels The image
 * @param out Receives red, green and blue
 */
static void center(
    const unsigned char *pixels,
    int out[3])
{
    const unsigned char *p = pixels + (24 * 64 + 32) * 4;
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
}

/** Check that two images are identical and free them.
 *
 * @param a The first image
 * @param b The second image
 */
static void expect_same(
    unsigned char *a,
    unsigned char *b)
{
    test_int(memcmp(a, b, 64 * 48 * 4), 0);
    stbi_image_free(a);
    stbi_image_free(b);
}

/** Render the lit scene of the identity tests: a floor, a rough and a smooth metal ball, a
 * point light and the default ambient and sun.
 *
 * @param bake The ReflectionBake of the scene, NULL for none
 * @param probe Whether the scene has a ReflectionProbe
 * @param lighting Light settings of the view
 * @return The pixels
 */
static unsigned char* render_lit(
    const ReflectionBake *bake,
    bool probe,
    LightSettings lighting)
{
    ecs_world_t *world = world_new();
    int w = 0, h = 0;
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    if (bake) {
        ecs_set_ptr(world, scene, ReflectionBake, bake);
    }

    if (probe) {
        probe_new(world, scene, 0, 0, 5, 1);
    }

    box_new(world, scene, (float[3]){0, -1.1f, 0}, (float[3]){10, 0.2f, 10},
        (Rgb){200, 200, 200}, (Emissive){0});
    ecs_entity_t smooth = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, smooth, Position, {-1.2f, 0, 0});
    ecs_set(world, smooth, Sphere, {.segments = 24, .smooth = true, .radius = 1});
    ecs_set(world, smooth, Rgb, {230, 200, 150});
    ecs_set(world, smooth, PbrMaterial, {.metallic = 1, .roughness = 0.1f});
    ecs_entity_t rough = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, rough, Position, {1.2f, 0, 0});
    ecs_set(world, rough, Sphere, {.segments = 24, .smooth = true, .radius = 1});
    ecs_set(world, rough, Rgb, {150, 180, 230});
    ecs_set(world, rough, PbrMaterial, {.metallic = 1, .roughness = 0.7f});
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, light, Position, {2, 3, 2});
    ecs_set(world, light, PointLight, {.intensity = 4 * GLM_PIf, .range = 12});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting = lighting,
        .clear = {.enabled = true, .color = {10, 10, 20, 255}}});

    return render(world, &w, &h);
}

void ReflectionShading_scene_without_probes_renders_as_before(void) {
    // reflection_probes/shading
    LightSettings off = {.enabled = true, .baked = true};
    LightSettings on = {.enabled = true, .baked = true, .reflections = true};
    ReflectionBake no_probes = {.resolution = RESOLUTION, .budget_ms = 1, .scene_probe = false};
    unsigned char *before = render_lit(NULL, false, off);
    expect_same(render_lit(NULL, false, on), render_lit(NULL, false, off));
    expect_same(before, render_lit(&no_probes, false, on));
}

void ReflectionShading_view_without_reflections_renders_as_before(void) {
    // reflection_probes/shading
    LightSettings off = {.enabled = true, .baked = true};
    ReflectionBake bake = {.resolution = RESOLUTION, .budget_ms = 1, .scene_probe = true,
        .scene_probe_height = 2};
    expect_same(render_lit(NULL, false, off), render_lit(&bake, true, off));
}

void ReflectionShading_probes_ready_after_capture(void) {
    // reflection_probes/shading
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = RESOLUTION, .budget_ms = 1, .scene_probe = true, .scene_probe_height = 1});
    enclosure_new(world, scene, 6, (Rgb){255, 0, 0});
    probe_new(world, scene, 6, 0, 3, 0);
    mirror_new(world, scene, 0, 0);
    view_new(world, scene, 0, 0, (LightSettings){.enabled = true, .reflections = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .resolution_scale = 1});

    int32_t i, frame, count = 0;
    for (frame = 0; frame < 50; frame ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        if (ecs_get(world, scene, ReflectionBakeStatus)->converged) {
            break;
        }
    }

    test_assert(frame < 50);
    const box_render_reflection_probe_t *probes = box_render_reflect_probes(
        world, scene, &count);
    test_int(count, 2);
    for (i = 0; i < count; i ++) {
        test_int(probes[i].ready, 1);
    }

    ecs_fini(world);
}

void ReflectionShading_mirror_reflects_probe(void) {
    // reflection_probes/shading
    int i, w = 0, h = 0, c[2][3];
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = world_new();
        ecs_entity_t scene = scene_new(world, &(ReflectionBake){
            .resolution = RESOLUTION, .budget_ms = 1, .scene_probe = false});
        enclosure_new(world, scene, 6, (Rgb){255, 0, 0});
        probe_new(world, scene, 6, 0, 3, 0);
        mirror_new(world, scene, 0, 0);
        view_new(world, scene, 0, 0, (LightSettings){.enabled = true, .reflections = i == 1});
        unsigned char *pixels = render(world, &w, &h);
        center(pixels, c[i]);
        stbi_image_free(pixels);
    }

    test_assert(c[0][0] < 16);
    test_assert(c[1][0] > 128);
    test_assert(c[1][1] < 32);
    test_assert(c[1][2] < 32);
}

void ReflectionShading_higher_priority_probe_covers_lower(void) {
    // reflection_probes/shading
    int i, w = 0, h = 0, c[2][3];
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = world_new();
        ecs_entity_t scene = scene_new(world, &(ReflectionBake){
            .resolution = RESOLUTION, .budget_ms = 1, .scene_probe = false});
        enclosure_new(world, scene, 6, (Rgb){255, 0, 0});
        enclosure_new(world, scene, -6, (Rgb){0, 255, 0});
        probe_new(world, scene, 6, i == 0 ? 1 : 0, 3, 0);
        probe_new(world, scene, -6, i == 0 ? 0 : 1, 3, 0);
        mirror_new(world, scene, 0, 0);
        view_new(world, scene, 0, 0, (LightSettings){.enabled = true, .reflections = true});
        unsigned char *pixels = render(world, &w, &h);
        center(pixels, c[i]);
        stbi_image_free(pixels);
    }

    test_assert(c[0][0] > 128);
    test_assert(c[0][1] < 32);
    test_assert(c[1][1] > 128);
    test_assert(c[1][0] < 32);
}

/* The center pixel of the ball is 1 unit inside the faces of the red probe along z and 2 along
 * x and y, so with a blend of 4 it weighs 1/4 and the green probe below it gets the other 3/4. */
void ReflectionShading_blend_mixes_overlapping_probes(void) {
    // reflection_probes/shading
    int w = 0, h = 0, c[3];
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, &(ReflectionBake){
        .resolution = RESOLUTION, .budget_ms = 1, .scene_probe = false});
    enclosure_new(world, scene, 6, (Rgb){255, 0, 0});
    enclosure_new(world, scene, -6, (Rgb){0, 255, 0});
    probe_new(world, scene, 6, 1, 2, 4);
    probe_new(world, scene, -6, 0, 3, 0);
    mirror_new(world, scene, 0, 0);
    view_new(world, scene, 0, 0, (LightSettings){.enabled = true, .reflections = true});
    unsigned char *pixels = render(world, &w, &h);
    center(pixels, c);
    stbi_image_free(pixels);

    test_assert(c[0] > 96);
    test_assert(c[1] > 160);
    test_assert(c[1] > c[0]);
    test_assert(c[2] < 32);
}

/* A red emissive room holds the ball and the scene probe. Without ReflectionProbes the scene
 * probe reflects the room; without the scene probe the blue hemisphere ambient is reflected. */
void ReflectionShading_scene_probe_fills_outside_probes(void) {
    // reflection_probes/shading
    int i, w = 0, h = 0, c[2][3];
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = world_new();
        ecs_entity_t scene = scene_new(world, &(ReflectionBake){
            .resolution = RESOLUTION, .budget_ms = 1, .scene_probe = i == 0,
            .scene_probe_height = 5.1f});
        ecs_set(world, scene, AmbientLight, {
            .sky = {0, 0, 255}, .ground = {0, 0, 255}, .intensity = 1});
        int32_t k;
        for (k = 0; k < 6; k ++) {
            int32_t axis = k / 2;
            float position[3] = {0, 0, 0}, size[3] = {10.2f, 10.2f, 10.2f};
            position[axis] += k % 2 ? -5 : 5;
            size[axis] = 0.2f;
            box_new(world, scene, position, size, (Rgb){0, 0, 0},
                (Emissive){.strength = 1, .color = {255, 0, 0}});
        }

        mirror_new(world, scene, 0, -2);
        view_new(world, scene, 0, -2, (LightSettings){.enabled = true, .reflections = true});
        unsigned char *pixels = render(world, &w, &h);
        center(pixels, c[i]);
        stbi_image_free(pixels);
    }

    test_assert(c[0][0] > 128);
    test_assert(c[0][0] > c[0][2]);
    test_assert(c[1][2] > 128);
    test_assert(c[1][0] < 16);
}

/* A room probe captures from the room center; the back wall behind the camera is red left of
 * x = 1.5 and green right of it. The ball at x = 3 reflects the back wall straight back along
 * +z: an infinitely distant environment samples +z from the center, the red part, while the
 * projection box finds where the ray meets the wall, at x = 3 in the green part. */
void ReflectionShading_box_projection_corrects_parallax(void) {
    // reflection_probes/shading
    int i, w = 0, h = 0, c[2][3];
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = world_new();
        ecs_entity_t scene = scene_new(world, &(ReflectionBake){
            .resolution = RESOLUTION, .budget_ms = 1, .scene_probe = false});
        int32_t k;
        for (k = 0; k < 6; k ++) {
            int32_t axis = k / 2;
            float position[3] = {0, 0, 0}, size[3] = {10.2f, 10.2f, 10.2f};
            if (k == 4) {
                continue;
            }

            position[axis] += k % 2 ? -5 : 5;
            size[axis] = 0.2f;
            box_new(world, scene, position, size, (Rgb){0, 0, 0}, (Emissive){0});
        }

        box_new(world, scene, (float[3]){-1.75f, 0, 5}, (float[3]){6.5f, 10.2f, 0.2f},
            (Rgb){0, 0, 0}, (Emissive){.strength = 1, .color = {255, 0, 0}});
        box_new(world, scene, (float[3]){3.25f, 0, 5}, (float[3]){3.5f, 10.2f, 0.2f},
            (Rgb){0, 0, 0}, (Emissive){.strength = 1, .color = {0, 255, 0}});
        ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, probe, Position, {0, 0, 0});
        ecs_set(world, probe, ReflectionProbe, {
            .extent = {5, 5, 5},
            .projection = {i ? 4.9f : 0, i ? 4.9f : 0, i ? 4.9f : 0}
        });
        mirror_new(world, scene, 3, 0);
        view_new(world, scene, 3, 0, (LightSettings){.enabled = true, .reflections = true});
        unsigned char *pixels = render(world, &w, &h);
        center(pixels, c[i]);
        stbi_image_free(pixels);
    }

    test_assert(c[0][0] > 128);
    test_assert(c[0][1] < 64);
    test_assert(c[1][1] > 128);
    test_assert(c[1][0] < 64);
}

/* The irradiance grid of a LightBake sees no light at the ball, while the probe captured a
 * bright red enclosure: normalized by the grid the reflection goes dark, without baked
 * lighting it stays red. */
void ReflectionShading_grid_darkens_dim_fragment(void) {
    // reflection_probes/shading
    int i, w = 0, h = 0, c[2][3];
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = world_new();
        ecs_entity_t scene = scene_new(world, &(ReflectionBake){
            .resolution = RESOLUTION, .budget_ms = 1, .scene_probe = false});
        ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounces = 0, .capture_size = 8,
            .budget_ms = 2, .bounds_padding = 1, .cache = ""});
        enclosure_new(world, scene, 6, (Rgb){255, 0, 0});
        probe_new(world, scene, 6, 0, 3, 0);
        mirror_new(world, scene, 0, 0);
        view_new(world, scene, 0, 0,
            (LightSettings){.enabled = true, .baked = i == 0, .reflections = true});
        unsigned char *pixels = render(world, &w, &h);
        center(pixels, c[i]);
        stbi_image_free(pixels);
    }

    test_assert(c[0][0] < 32);
    test_assert(c[1][0] > 128);
}
