/**
 * @file Shape2DDisplay.c
 * @brief Tests that the world 2D shapes of a tone mapped view show their authored colors, as
 * display values that the exposure and the tone map of the view do not change, with the GPU
 * backend.
 *
 * A camera of a dark scene with no light looks at a Rect in the middle of the image. Its view
 * scales the image by a fixed exposure of 4 (an AutoExposure clamped to it) and tone maps it
 * with PBR Neutral, as a dark room of the portal bunker does.
 *
 * The scenes are seen into a 64x64 image.
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
#define S2D_SIZE (64)

/** Seconds of a frame */
#define S2D_DELTA (1.0f / 60.0f)

/** Frames rendered before an image is checked: the first one has no adapted exposure yet */
#define S2D_FRAMES (4)

/** Distance of the Rect in front of the camera */
#define S2D_DISTANCE (8.0f)

/** Size of the Rect */
#define S2D_RECT (4.0f)

/** The fixed exposure of the view */
#define S2D_EXPOSURE (4.0f)

/** Fog of the room of the fogged scene: color and density */
#define S2D_FOG_COLOR (0.5f)
#define S2D_FOG_DENSITY (0.1f)

/** Largest channel difference between the Rect and its authored color in clear air */
#define S2D_CLOSE (6)

/** Least red of the cyan Rect through the fog: the fog scatters light in front of it */
#define S2D_FOG_RED (40)

/** Least green of the cyan Rect through the fog: a bright stroke stays bright */
#define S2D_FOG_GREEN (200)

/** The authored color of the Rect in clear air: a mid teal */
static const Rgb s2d_teal = {20, 120, 160};

/** The authored color of the Rect in the fog: a vivid cyan stroke */
static const Rgb s2d_cyan = {0, 255, 255};

/** Create a headless world that renders on the main thread: a dark scene without ambient, a
 * Rect in front of the camera and a view that scales by a fixed exposure and tone maps.
 *
 * @param color The color of the Rect
 * @param fog Whether the camera and the Rect are in a fogged VisArea
 * @return The world
 */
static ecs_world_t* s2d_world(
    Rgb color,
    bool fog)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});

    if (fog) {
        char *script = flecs_asprintf(
            "using box.portal\n"
            "room {\n"
            "  VisArea: {shape: [{-10, -12}, {10, -12}, {10, 4}, {-10, 4}], y: -5,\n"
            "    height: 10, ambient: {0, 0, 0}, fog: {%f, %f, %f, %f}}\n"
            "}\n",
            (double)S2D_FOG_COLOR, (double)S2D_FOG_COLOR, (double)S2D_FOG_COLOR,
            (double)S2D_FOG_DENSITY);
        test_int(ecs_script_run(world, "room", script, NULL), 0);
        ecs_os_free(script);
    }

    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, -S2D_DISTANCE});
    ecs_set(world, rect, Rect, {S2D_RECT, S2D_RECT});
    ecs_set_ptr(world, rect, Rgb, &color);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});

    ecs_entity_t exposure = ecs_new(world);
    ecs_set(world, exposure, AutoExposure, {
        .min_brightness = 0.1f,
        .max_brightness = 0.3f,
        .min_log_luma = -8,
        .max_log_luma = 4,
        .speed_up = 3,
        .speed_down = 1,
        .low_percentile = 0.5f,
        .high_percentile = 0.95f,
        .min_exposure = S2D_EXPOSURE,
        .max_exposure = S2D_EXPOSURE
    });
    ecs_entity_t tonemap = ecs_new(world);
    ecs_set(world, tonemap, Tonemap, {.auto_exposure = exposure});

    ecs_entity_t view = ecs_new(world);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}};
    value.lighting.enabled = true;
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);

    return world;
}

/** Render S2D_FRAMES frames into an image file of the temporary folder and get the pixel in
 * the middle of the last one.
 *
 * @param world The world
 * @param file Image file
 * @param out Receives the red, green and blue of the pixel
 */
static void s2d_center(
    ecs_world_t *world,
    const char *file,
    int32_t out[3])
{
    ecs_entity_t e = ecs_entity(world, { .name = "capture" });
    ecs_set(world, e, Surface, {.width = S2D_SIZE, .height = S2D_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    for (int32_t i = 0; i < S2D_FRAMES; i ++) {
        ecs_progress(world, S2D_DELTA);
        box_render_sync(world);
    }

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, S2D_SIZE);
    test_int(h, S2D_SIZE);
    const unsigned char *center = &px[((S2D_SIZE / 2) * S2D_SIZE + S2D_SIZE / 2) * 4];
    out[0] = center[0];
    out[1] = center[1];
    out[2] = center[2];
    printf("%s: %d %d %d\n", file, out[0], out[1], out[2]);
    stbi_image_free(px);
    ecs_os_free(path);
}

void Shape2DDisplay_authored_color_tonemapped(void) {
    // ui/shapes_2d
    /* A teal Rect seen by a view that scales the image by 4 and tone maps it shows its
     * authored color, not the color lifted by the exposure and flattened by the curve. */
    ecs_world_t *world = s2d_world(s2d_teal, false);
    int32_t c[3];
    s2d_center(world, "s2d_teal.png", c);
    test_assert(abs(c[0] - s2d_teal.r) <= S2D_CLOSE);
    test_assert(abs(c[1] - s2d_teal.g) <= S2D_CLOSE);
    test_assert(abs(c[2] - s2d_teal.b) <= S2D_CLOSE);
    ecs_fini(world);
}

void Shape2DDisplay_fogged_tonemapped(void) {
    // ui/shapes_2d
    /* A cyan Rect in a fogged room seen by the same view still fades into the fog: the fog
     * scatters light in front of it, so its red rises, while its bright green stays bright. */
    ecs_world_t *world = s2d_world(s2d_cyan, true);
    int32_t c[3];
    s2d_center(world, "s2d_fog.png", c);
    test_assert(c[0] >= S2D_FOG_RED);
    test_assert(c[1] >= S2D_FOG_GREEN);
    test_assert(c[2] >= S2D_FOG_GREEN);
    ecs_fini(world);
}
