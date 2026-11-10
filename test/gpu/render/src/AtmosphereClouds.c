/**
 * @file AtmosphereClouds.c
 * @brief Tests for the CloudLayers the sky pass of a view composites over the sky of the
 * Atmosphere of its scene.
 *
 * The layers read single channel images of test/assets/textures. Pixels are compared as the PNG
 * holds them, display encoded.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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

/** Seconds of a frame */
#define DT (1.0f / 60.0f)

/** Clear color of the views */
#define CLEAR ((rgba){10, 200, 20, 255})

/** 8 x 8 of 255: every value is fully covered */
#define CLOUD_WHITE_PNG "test/assets/textures/cloud_white.png"

/** 64 x 64, the rows above the middle 0 and the rows below 255 */
#define CLOUD_SPLIT_PNG "test/assets/textures/cookie_split.png"

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
 * @param elevation Elevation of the sun in degrees
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    float elevation)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_add(world, scene, Atmosphere);
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_set(world, sun, Rotation, {glm_rad(elevation), 0, 0});
    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->sun = sun;
    a->sun_disk_intensity = 0;
    ecs_modified(world, scene, Atmosphere);

    return scene;
}

/** Add a camera at a height that looks along -z, pitched up, and a view of it that clears to
 * CLEAR, to a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param y Height of the camera
 * @param fov Vertical field of view in degrees
 * @param pitch Angle above the horizon the camera looks at in degrees
 * @param lit Whether the view is lit
 * @return The camera
 */
static ecs_entity_t camera_new(
    ecs_world_t *world,
    ecs_entity_t scene,
    float y,
    float fov,
    float pitch,
    bool lit)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, y, 0});
    ecs_set(world, camera, Rotation, {glm_rad(pitch), 0, 0});
    ecs_set(world, camera, Camera, {.fov = fov, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = CLEAR};
    v->lighting.enabled = lit;
    v->lighting.atmosphere = true;
    ecs_modified(world, view, View);

    return camera;
}

/** Create a single channel texture and run frames until it is loaded.
 *
 * @param world The world
 * @param path Image file
 * @return The texture entity
 */
static ecs_entity_t cloud_texture(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t tex = ecs_new(world);
    int32_t i;
    ecs_set(world, tex, TexturePath, {.path = ECS_CONST_CAST(char*, path),
        .single_channel = true});
    for (i = 0; i < 200; i ++) {
        const TextureStatus *s = ecs_get(world, tex, TextureStatus);
        if (s && s->state != TextureLoading) {
            break;
        }

        ecs_progress(world, DT);
        box_render_sync(world);
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);

    return tex;
}

/** Add a layer to a scene: a texture at an altitude, a hard step at threshold 0.5.
 *
 * @param world The world
 * @param scene The scene
 * @param texture The texture
 * @param altitude World Y of the layer
 * @param density Opacity of fully covered clouds
 * @return The entity of the layer
 */
static ecs_entity_t cloud_layer(
    ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t texture,
    float altitude,
    float density)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, e, CloudLayer);
    CloudLayer *c = ecs_get_mut(world, e, CloudLayer);
    c->texture = texture;
    c->altitude = altitude;
    c->threshold = 0.5f;
    c->softness = 0;
    c->density = density;
    ecs_modified(world, e, CloudLayer);

    return e;
}

/** Render frames of a world into a WIDTH x HEIGHT image.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @param frames Frames to run, at least 1
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* capture(
    ecs_world_t *world,
    const char *file,
    int32_t frames)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    int32_t frame;
    int w = 0, h = 0, n = 0;
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = WIDTH, .height = HEIGHT,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    for (frame = 0; frame < frames; frame ++) {
        ecs_progress(world, DT);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, WIDTH);
    test_int(h, HEIGHT);
    ecs_os_free(path);

    return result;
}

/** Render frames of a world into an image and delete the world.
 *
 * @param world The world, deleted
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* render(
    ecs_world_t *world)
{
    unsigned char *result = capture(world, "clouds.png", FRAMES);
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

/** Get the largest difference of the channels of two images.
 *
 * @param a The first image
 * @param b The second image
 * @return The difference
 */
static int32_t image_difference(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i, c, result = 0;
    for (i = 0; i < WIDTH * HEIGHT; i ++) {
        for (c = 0; c < 3; c ++) {
            result = glm_max(result, abs((int32_t)a[i * 4 + c] - (int32_t)b[i * 4 + c]));
        }
    }

    return result;
}

/** Get the sum of the channels of a pixel.
 *
 * @param pixels The image
 * @param x Column
 * @param y Row
 * @return The sum
 */
static int32_t brightness(
    const unsigned char *pixels,
    int32_t x,
    int32_t y)
{
    int32_t c[3];
    pixel(pixels, x, y, c);

    return c[0] + c[1] + c[2];
}

/** Find the row of a column where the brightness changes the most from the row above.
 *
 * @param pixels The image
 * @param x Column
 * @return The row, -1 when no change is above 30
 */
static int32_t edge_row(
    const unsigned char *pixels,
    int32_t x)
{
    int32_t y, best = 30, result = -1;
    for (y = 1; y < HEIGHT; y ++) {
        int32_t d = abs(brightness(pixels, x, y) - brightness(pixels, x, y - 1));
        if (d > best) {
            best = d;
            result = y;
        }
    }

    return result;
}

/** Render a view that looks up at 60 degrees, with the sun at an elevation and an optional white
 * layer 1000 units up.
 *
 * @param elevation Elevation of the sun in degrees
 * @param layer Whether the scene has the layer
 * @param density Its density
 * @return The pixels
 */
static unsigned char* render_up(
    float elevation,
    bool layer,
    float density)
{
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, elevation);
    ecs_entity_t tex = cloud_texture(world, CLOUD_WHITE_PNG);
    camera_new(world, scene, 0, 60, 60, true);
    if (layer) {
        cloud_layer(world, scene, tex, 1000, density);
    }

    return render(world);
}

void AtmosphereClouds_white_layer_covers_sky(void) {
    // atmosphere/clouds
    unsigned char *sky = render_up(60, false, 1);
    unsigned char *cloud = render_up(60, true, 1);
    int32_t x, y;
    for (y = 0; y < HEIGHT; y += 8) {
        for (x = 0; x < WIDTH; x += 8) {
            int32_t s[3], c[3];
            pixel(sky, x, y, s);
            pixel(cloud, x, y, c);
            test_assert(difference(s, c) > 20);
            /* Grayer than the blue sky: the blue share of the color drops */
            test_assert(c[2] * (s[0] + s[1] + s[2]) < s[2] * (c[0] + c[1] + c[2]));
        }
    }

    stbi_image_free(sky);
    stbi_image_free(cloud);
}

void AtmosphereClouds_density_zero_leaves_sky(void) {
    // atmosphere/clouds
    unsigned char *sky = render_up(60, false, 1);
    unsigned char *cloud = render_up(60, true, 0);
    test_assert(image_difference(sky, cloud) <= 1);
    stbi_image_free(sky);
    stbi_image_free(cloud);
}

void AtmosphereClouds_geometry_unchanged(void) {
    // atmosphere/clouds
    unsigned char *images[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = world_new();
        ecs_entity_t scene = scene_new(world, 60);
        ecs_entity_t tex = cloud_texture(world, CLOUD_WHITE_PNG);
        camera_new(world, scene, 0, 60, 20, false);
        ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, box, Position, {0, 0.73f, -2});
        ecs_set(world, box, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, box, Rgb, {40, 60, 220});
        if (i) {
            cloud_layer(world, scene, tex, 1000, 1);
        }

        images[i] = render(world);
    }

    int32_t box_plain[3], box_cloud[3], corner_plain[3], corner_cloud[3];
    pixel(images[0], WIDTH / 2, HEIGHT / 2, box_plain);
    pixel(images[1], WIDTH / 2, HEIGHT / 2, box_cloud);
    pixel(images[0], 2, 2, corner_plain);
    pixel(images[1], 2, 2, corner_cloud);
    test_assert(difference(box_plain, (int32_t[3]){40, 60, 220}) <= 2);
    test_assert(difference(box_plain, box_cloud) <= 1);
    test_assert(difference(corner_plain, corner_cloud) > 20);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

void AtmosphereClouds_velocity_moves_pattern(void) {
    // atmosphere/clouds
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 60);
    ecs_entity_t tex = cloud_texture(world, CLOUD_SPLIT_PNG);
    camera_new(world, scene, 0, 60, 90, true);
    /* 400 units repeat, the edge between the halves at v = 0.5 is overhead; the 115 units the
     * view spans 100 units up hold no other edge */
    ecs_entity_t e = cloud_layer(world, scene, tex, 100, 1);
    CloudLayer *c = ecs_get_mut(world, e, CloudLayer);
    c->size = 400;
    c->offset[1] = 200;
    c->velocity[1] = 24;
    ecs_modified(world, e, CloudLayer);

    unsigned char *before = capture(world, "clouds_before.png", 2);
    unsigned char *after = capture(world, "clouds_after.png", 30);
    int32_t r0 = edge_row(before, WIDTH / 2), r1 = edge_row(after, WIDTH / 2);
    test_assert(r0 != -1);
    test_assert(r1 != -1);

    /* 30 frames at 24 units a second move it 12 units; a row spans 2 tan(30) 100 / 48 units */
    float row = 2.0f * tanf(glm_rad(30.0f)) * 100.0f / (float)HEIGHT;
    float rows = 12.0f / row;
    test_assert(fabsf((float)abs(r1 - r0) - rows) <= 1.5f);

    stbi_image_free(before);
    stbi_image_free(after);
    ecs_fini(world);
}

/** Render a view from a height that looks down at 60 degrees, with an optional white layer 1500
 * units up.
 *
 * @param y Height of the camera
 * @param layer Whether the scene has the layer
 * @return The pixels
 */
static unsigned char* render_down(
    float y,
    bool layer)
{
    ecs_world_t *world = world_new();
    ecs_entity_t scene = scene_new(world, 60);
    ecs_entity_t tex = cloud_texture(world, CLOUD_WHITE_PNG);
    camera_new(world, scene, y, 60, -60, true);
    if (layer) {
        cloud_layer(world, scene, tex, 1500, 1);
    }

    return render(world);
}

void AtmosphereClouds_camera_above_sees_layer_below(void) {
    // atmosphere/clouds
    unsigned char *above_sky = render_down(2500, false);
    unsigned char *above_cloud = render_down(2500, true);
    unsigned char *below_sky = render_down(1000, false);
    unsigned char *below_cloud = render_down(1000, true);
    int32_t s[3], c[3];
    pixel(above_sky, WIDTH / 2, HEIGHT / 2, s);
    pixel(above_cloud, WIDTH / 2, HEIGHT / 2, c);
    test_assert(difference(s, c) > 20);
    /* Seen from the top, lit by the sun above: brighter than the haze below the horizon */
    test_assert(c[0] + c[1] + c[2] > s[0] + s[1] + s[2]);

    /* A camera below the layer looking down does not see it */
    test_assert(image_difference(below_sky, below_cloud) <= 1);

    stbi_image_free(above_sky);
    stbi_image_free(above_cloud);
    stbi_image_free(below_sky);
    stbi_image_free(below_cloud);
}

void AtmosphereClouds_sunset_warmer_than_noon(void) {
    // atmosphere/clouds
    unsigned char *noon = render_up(60, true, 1);
    unsigned char *sunset = render_up(3, true, 1);
    int32_t n[3], s[3];
    pixel(noon, WIDTH / 2, HEIGHT / 2, n);
    pixel(sunset, WIDTH / 2, HEIGHT / 2, s);
    /* Red over blue grows toward the sunset */
    test_assert(s[0] * glm_max(n[2], 1) > n[0] * glm_max(s[2], 1));
    test_assert(s[0] > s[2]);
    stbi_image_free(noon);
    stbi_image_free(sunset);
}
