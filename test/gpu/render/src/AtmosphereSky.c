/**
 * @file AtmosphereSky.c
 * @brief Tests for the sky the forward pass draws behind the geometry and the aerial
 * perspective of the 3D meshes in a scene with an Atmosphere.
 *
 * Boxes are drawn by unlit views, so a box shows its own color unless the atmosphere between it
 * and the camera changes it. Pixels are compared as the PNG holds them, display encoded.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width of the rendered image */
#define WIDTH (64)

/** Height of the rendered image */
#define HEIGHT (48)

/** Frames a render runs */
#define FRAMES (3)

/** Clear color of the views */
#define CLEAR ((rgba){10, 200, 20, 255})

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

/** Create a scene with the default Atmosphere and a white sun at an elevation.
 *
 * @param world The world
 * @param units_per_km World units in a km
 * @param elevation Elevation of the sun in degrees
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    float units_per_km,
    float elevation)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_add(world, scene, Atmosphere);
    ecs_get_mut(world, scene, Atmosphere)->units_per_km = units_per_km;
    ecs_modified(world, scene, Atmosphere);
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_set(world, sun, Rotation, {glm_rad(elevation), 0, 0});

    return scene;
}

/** Add a camera at a height that looks along -z, pitched up, to a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param y Height of the camera
 * @param fov Vertical field of view in degrees
 * @param pitch Angle above the horizon the camera looks at in degrees
 * @return The camera
 */
static ecs_entity_t camera_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float y,
    float fov,
    float pitch)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, y, 0});
    ecs_set(world, camera, Rotation, {glm_rad(pitch), 0, 0});
    ecs_set(world, camera, Camera, {.fov = fov, .near_ = 0.1f});

    return camera;
}

/** Add a view of a camera that clears to CLEAR, with the View constructor's settings.
 *
 * @param world The world
 * @param camera The camera
 * @param lit Whether the view is lit
 * @param atmosphere Whether the view renders the atmosphere
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t camera,
    bool lit,
    bool atmosphere)
{
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = CLEAR};
    v->lighting.enabled = lit;
    v->lighting.atmosphere = atmosphere;
    ecs_modified(world, view, View);

    return view;
}

/** Add an unlit box to a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param position Center of the box
 * @param size Edge of the box
 * @param color Color of the box
 */
static void box_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    const float position[3],
    float size,
    Rgb color)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Box, {size, size, size});
    ecs_set_ptr(world, e, Rgb, &color);
}

/** Render frames of a world into a WIDTH x HEIGHT image.
 *
 * @param world The world, deleted
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* render(
    ecs_world_t *world)
{
    int32_t frame;
    int w = 0, h = 0, n = 0;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = WIDTH, .height = HEIGHT, .write_to_file = "sky.png",
        .resolution_scale = 1});
    for (frame = 0; frame < FRAMES; frame ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/sky.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, WIDTH);
    test_int(h, HEIGHT);
    ecs_os_free(path);
    ecs_fini(world);

    return result;
}

/** Get a pixel of an image.
 *
 * @param pixels The image
 * @param x Column
 * @param y Row, 0 at the top
 * @param out Receives red, green and blue
 */
static void pixel(
    const unsigned char *pixels,
    int32_t x,
    int32_t y,
    int32_t out[3])
{
    const unsigned char *p = pixels + (y * WIDTH + x) * 4;
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
}

/** Get the largest difference of the channels of two colors.
 *
 * @param a The first color
 * @param b The second color
 * @return The difference
 */
static int32_t difference(
    const int32_t a[3],
    const int32_t b[3])
{
    int32_t c, result = 0;
    for (c = 0; c < 3; c ++) {
        result = glm_max(result, abs(a[c] - b[c]));
    }

    return result;
}

/** Get the sum of the squared differences of the channels of two colors.
 *
 * @param a The first color
 * @param b The second color
 * @return The squared distance
 */
static int32_t distance2(
    const int32_t a[3],
    const int32_t b[3])
{
    int32_t c, result = 0;
    for (c = 0; c < 3; c ++) {
        result += (a[c] - b[c]) * (a[c] - b[c]);
    }

    return result;
}

/** Render two boxes of one color in front of the horizon at noon, one 20 m and one 30 km
 * away, seen by an unlit view.
 *
 * @param atmosphere Whether the view renders the atmosphere
 * @return The pixels, the near box at (16, 24), the far box at (32, 24) and the sky at the
 * horizon at (56, 24)
 */
static unsigned char* render_boxes(
    bool atmosphere)
{
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 100, 90);
    ecs_entity_t camera = camera_new(world, scene, 0, 60, 0);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = CLEAR},
        .lighting = {.atmosphere = atmosphere}});
    box_new(world, scene, (float[3]){-0.75f, 0, -2}, 0.5f, (Rgb){200, 40, 40});
    box_new(world, scene, (float[3]){0, 0, -3000}, 600, (Rgb){200, 40, 40});

    return render(world);
}

void AtmosphereSky_zenith_bluer_and_darker_than_horizon(void) {
    // atmosphere/sky
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 1000, 90);
    ecs_entity_t camera = camera_new(world, scene, 0, 90, 45);
    view_new(world, camera, true, true);
    unsigned char *pixels = render(world);
    int32_t zenith[3], horizon[3], clear[3] = {CLEAR.r, CLEAR.g, CLEAR.b};
    pixel(pixels, WIDTH / 2, 0, zenith);
    pixel(pixels, WIDTH / 2, HEIGHT - 1, horizon);
    test_assert(difference(zenith, clear) > 8);
    test_assert(difference(horizon, clear) > 8);
    test_assert(zenith[0] + zenith[1] + zenith[2] < horizon[0] + horizon[1] + horizon[2]);
    test_assert(zenith[2] * (horizon[0] + horizon[1] + horizon[2]) >
        horizon[2] * (zenith[0] + zenith[1] + zenith[2]));
    test_assert(zenith[2] > zenith[0]);
    stbi_image_free(pixels);
}

void AtmosphereSky_view_without_atmosphere_shows_clear_color(void) {
    // atmosphere/sky
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 1000, 90);
    ecs_entity_t camera = camera_new(world, scene, 0, 90, 45);
    view_new(world, camera, true, false);
    unsigned char *pixels = render(world);
    int32_t i;
    for (i = 0; i < WIDTH * HEIGHT; i ++) {
        test_int(pixels[i * 4], CLEAR.r);
        test_int(pixels[i * 4 + 1], CLEAR.g);
        test_int(pixels[i * 4 + 2], CLEAR.b);
    }

    stbi_image_free(pixels);
}

void AtmosphereSky_far_box_approaches_horizon_color(void) {
    // atmosphere/sky
    unsigned char *sky = render_boxes(true);
    unsigned char *plain = render_boxes(false);
    int32_t near_sky[3], far_sky[3], horizon[3], near_plain[3], far_plain[3];
    pixel(sky, 16, 24, near_sky);
    pixel(sky, 32, 24, far_sky);
    pixel(sky, 56, 24, horizon);
    pixel(plain, 16, 24, near_plain);
    pixel(plain, 32, 24, far_plain);
    test_assert(difference(near_plain, (int32_t[3]){200, 40, 40}) <= 2);
    test_assert(difference(far_plain, (int32_t[3]){200, 40, 40}) <= 2);
    test_assert(difference(near_sky, near_plain) <= 6);
    test_assert(difference(far_sky, far_plain) > 20);
    test_assert(distance2(far_sky, horizon) * 4 < distance2(far_plain, horizon));
    test_assert(distance2(far_sky, horizon) < distance2(near_sky, horizon));
    stbi_image_free(sky);
    stbi_image_free(plain);
}

void AtmosphereSky_sky_does_not_draw_over_geometry(void) {
    // atmosphere/sky
    unsigned char *images[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = world_new();
        ecs_entity_t scene = scene_new(world, 1000, 90);
        ecs_entity_t camera = camera_new(world, scene, 0, 60, 20);
        view_new(world, camera, false, i == 0);
        box_new(world, scene, (float[3]){0, 0.73f, -2}, 0.5f, (Rgb){40, 60, 220});
        images[i] = render(world);
    }

    int32_t box_sky[3], box_plain[3], corner_sky[3], corner_plain[3];
    pixel(images[0], WIDTH / 2, HEIGHT / 2, box_sky);
    pixel(images[1], WIDTH / 2, HEIGHT / 2, box_plain);
    pixel(images[0], 2, 2, corner_sky);
    pixel(images[1], 2, 2, corner_plain);
    test_assert(difference(box_plain, (int32_t[3]){40, 60, 220}) <= 2);
    test_assert(difference(box_sky, box_plain) <= 2);
    test_assert(difference(corner_plain, (int32_t[3]){CLEAR.r, CLEAR.g, CLEAR.b}) == 0);
    test_assert(difference(corner_sky, corner_plain) > 8);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}
