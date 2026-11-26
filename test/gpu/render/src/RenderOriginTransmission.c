/**
 * @file RenderOriginTransmission.c
 * @brief Tests that a transmissive surface finds the point of the opaque snapshot it shows
 * relative to the render origin, with the GPU backend: a turned glass slab in front of a wall of
 * red and green stripes far from the local origin, the render origin following the camera
 * (WorldOrigin.render, the world origin never shifting), matches the same scene near the local
 * origin.
 *
 * Far from the local origin a local position in f32 is coarse (a sixteenth of a unit at the far
 * offset), so the exit point of a refracted ray projected there would sample the snapshot pixels
 * away from where it should, moving the stripes seen through the glass; relative to the render
 * origin it keeps its precision.
 *
 * The scenes are seen into a 96x96 image.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width and height in pixels of the images */
#define ROT_SIZE (96)

/** Seconds of a frame */
#define ROT_DELTA (1.0f / 60.0f)

/** Local X and Z of the far scene, whole units so the f32 positions of the scene are exact */
#define ROT_FAR_X (1000000.0f)
#define ROT_FAR_Z (-1000000.0f)

/** Frames of a case */
#define ROT_FRAMES (3)

/** Stripes of the wall behind the glass, and the width of each */
#define ROT_STRIPES (16)
#define ROT_STRIPE (0.5f)

/** Channel difference above which a pixel counts as different, and the pixels that may, per
 * thousand: the refracted ray turns from the view vector of the local frame, so the stripe edges
 * seen through the glass may be a small fraction of a pixel apart */
#define ROT_PIXEL_DIFF (32)
#define ROT_DIFF_PERMILLE (10)

/** Worlds a test renders side by side */
#define ROT_WORLDS (2)

/** Create a headless world that renders with the GPU on the main thread with transmission on,
 * without lights, a wall of red and green stripes that show their emission, a smooth glass slab
 * turned in front of it and a camera looking at both down -Z. Far scenes draw relative to a
 * render origin that follows the camera.
 *
 * @param x X of the scene
 * @param z Z of the scene
 * @param follow Whether the world draws relative to a render origin that follows the camera
 * @return The world
 */
static ecs_world_t* rot_world(
    float x,
    float z,
    bool follow)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.transmission = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {x + 1000, 1000, z + 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});
    if (follow) {
        ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = true});
    }

    int32_t i;
    for (i = 0; i < ROT_STRIPES; i ++) {
        float left = -0.5f * ROT_STRIPES * ROT_STRIPE + (float)i * ROT_STRIPE;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {x + left + 0.5f * ROT_STRIPE, 0, z - 8});
        ecs_set(world, e, Box, {ROT_STRIPE, 8, 0.1f});
        ecs_set(world, e, Rgb, {0, 0, 0});
        ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, e, Emissive, {.strength = 1,
            .color = (i & 1) ? (Rgb){0, 255, 0} : (Rgb){255, 0, 0}});
    }

    ecs_entity_t glass = ecs_new(world);
    ecs_set(world, glass, Position, {x, 0, z - 3});
    ecs_set(world, glass, Rotation, {0, 0.5f, 0});
    ecs_set(world, glass, Box, {3, 3, 0.5f});
    ecs_set(world, glass, Rgb, {255, 255, 255});
    ecs_set(world, glass, PbrMaterial, {.metallic = 0, .roughness = 0});
    ecs_set(world, glass, Transmission, {.transmission_factor = 1, .ior = 1.5f,
        .thickness_factor = 0.1f, .attenuation_distance = 1e10f,
        .attenuation_color = {255, 255, 255}});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {x, 0, z});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true, .depth_prepass = true}});

    return world;
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* rot_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ROT_SIZE, .height = ROT_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ROT_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ROT_SIZE);
    test_int(h, ROT_SIZE);
    ecs_os_free(path);

    return result;
}

/** Get the largest channel difference of a pixel of two images.
 *
 * @param a The first image
 * @param b The second image
 * @param i The pixel
 * @return The difference, 0 to 255
 */
static int32_t rot_pixel_diff(
    const unsigned char *a,
    const unsigned char *b,
    int32_t i)
{
    int32_t result = 0, c;
    for (c = 0; c < 3; c ++) {
        int32_t d = abs((int)a[i * 4 + c] - (int)b[i * 4 + c]);
        result = d > result ? d : result;
    }

    return result;
}

/** Check that two images match: at most ROT_DIFF_PERMILLE of the pixels differ by more than
 * ROT_PIXEL_DIFF.
 *
 * @param a The first image
 * @param b The second image
 */
static void rot_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, i;
    for (i = 0; i < ROT_SIZE * ROT_SIZE; i ++) {
        int32_t d = rot_pixel_diff(a, b, i);
        worst = d > worst ? d : worst;
        differ += d > ROT_PIXEL_DIFF;
    }

    if (differ * 1000 > ROT_DIFF_PERMILLE * ROT_SIZE * ROT_SIZE) {
        printf("worst channel difference %d, %d pixels differ\n", worst, differ);
    }

    test_assert(differ * 1000 <= ROT_DIFF_PERMILLE * ROT_SIZE * ROT_SIZE);
}

/** Check that the stripes show through the glass: the middle row holds both red and green.
 *
 * @param px The image
 */
static void rot_expect_stripes(
    const unsigned char *px)
{
    int32_t red = 0, green = 0, x;
    for (x = 0; x < ROT_SIZE; x ++) {
        const unsigned char *p = &px[((ROT_SIZE / 2) * ROT_SIZE + x) * 4];
        red += p[0] > 100 && p[1] < 60;
        green += p[1] > 100 && p[0] < 60;
    }

    test_assert(red > 8);
    test_assert(green > 8);
}

void RenderOriginTransmission_far_same_image(void) {
    // materials/transmission, render/world_origin
    /* The stripes seen through a turned glass slab a million units from the local origin,
     * drawn relative to a render origin that follows the camera, match those seen near the
     * local origin. */
    static const char *files[ROT_WORLDS] = { "rot0.png", "rot1.png" };
    ecs_world_t *worlds[ROT_WORLDS] = {
        rot_world(0, 0, false),
        rot_world(ROT_FAR_X, ROT_FAR_Z, true)
    };
    unsigned char *px[ROT_WORLDS] = {0};
    int32_t i, w;
    for (i = 0; i < ROT_FRAMES; i ++) {
        for (w = 0; w < ROT_WORLDS; w ++) {
            stbi_image_free(px[w]);
            px[w] = rot_frame(worlds[w], files[w]);
        }
    }

    rot_expect_stripes(px[0]);
    rot_expect_same(px[0], px[1]);
    for (w = 0; w < ROT_WORLDS; w ++) {
        stbi_image_free(px[w]);
        ecs_fini(worlds[w]);
    }
}
