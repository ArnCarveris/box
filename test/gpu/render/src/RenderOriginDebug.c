/**
 * @file RenderOriginDebug.c
 * @brief Tests that the debug passes of a view draw relative to the render origin, with the GPU
 * backend: the grid and the boxes of the spatial index far from the local origin, the render
 * origin following the camera (WorldOrigin.render, the world origin never shifting), match the
 * same scene near the local origin.
 *
 * Far from the local origin a local position in f32 is coarse (a sixteenth of a unit at the far
 * offset), so a ray of the grid cast from the near plane there, or a line placed there, would be
 * off by pixels or lost; relative to the render origin they keep their precision, and the lines
 * of the grid take the render origin wrapped to their period, so they fall where they fall near
 * the local origin.
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
#define ROD_SIZE (96)

/** Seconds of a frame */
#define ROD_DELTA (1.0f / 60.0f)

/** Local X and Z of the near scene, whole multiples of the major cells of the grid, far enough
 * from the axes of the grid that its fade hides them */
#define ROD_NEAR_X (100.0f)
#define ROD_NEAR_Z (-100.0f)

/** Local X and Z of the far scene, whole multiples of the major cells of the grid, so the f32
 * positions of the scene are exact */
#define ROD_FAR_X (1000000.0f)
#define ROD_FAR_Z (-1000000.0f)

/** Frames of a case */
#define ROD_FRAMES (3)

/** Channel difference above which a pixel counts as different, and the pixels that may, per
 * thousand */
#define ROD_PIXEL_DIFF (12)
#define ROD_DIFF_PERMILLE (10)

/** Pixels per thousand the lines of a debug pass cover, at least */
#define ROD_LINES_PERMILLE (20)

/** Worlds a test renders side by side */
#define ROD_WORLDS (2)

/** Grid settings of the cases: white minor lines every unit, green major lines every 5, faded
 * out before the axes of the near scene */
static const GridSettings rod_grid = {
    .enabled = true, .cell_size = 1, .major_every = 5, .fade_distance = 40,
    .line_width = 1, .color = {255, 255, 255, 255}, .major_color = {0, 255, 0, 255}
};

/** Create a headless world that renders with the GPU on the main thread, without lights, and a
 * camera at a point looking down at the plane y = 0 with a view that clears to black. Far
 * scenes draw relative to a render origin that follows the camera.
 *
 * @param x X of the camera
 * @param z Z of the camera
 * @param follow Whether the world draws relative to a render origin that follows the camera
 * @param out_view Receives the view
 * @return The world
 */
static ecs_world_t* rod_world(
    float x,
    float z,
    bool follow,
    ecs_entity_t *out_view)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {x, 3, z});
    ecs_set(world, camera, Rotation, {-0.6f, 0.3f, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    *out_view = ecs_new(world);
    ecs_set(world, *out_view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* rod_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ROD_SIZE, .height = ROD_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ROD_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ROD_SIZE);
    test_int(h, ROD_SIZE);
    ecs_os_free(path);

    return result;
}

/** Render ROD_FRAMES frames of every world, keeping the last image of each.
 *
 * @param worlds The worlds
 * @param px Receives the last image of each world
 */
static void rod_render(
    ecs_world_t **worlds,
    unsigned char **px)
{
    static const char *files[ROD_WORLDS] = { "rod0.png", "rod1.png" };
    int32_t i, w;
    for (i = 0; i < ROD_FRAMES; i ++) {
        for (w = 0; w < ROD_WORLDS; w ++) {
            stbi_image_free(px[w]);
            px[w] = rod_frame(worlds[w], files[w]);
        }
    }
}

/** Get the largest channel difference of a pixel of two images.
 *
 * @param a The first image
 * @param b The second image
 * @param i The pixel
 * @return The difference, 0 to 255
 */
static int32_t rod_pixel_diff(
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

/** Check that two images match: at most ROD_DIFF_PERMILLE of the pixels differ by more than
 * ROD_PIXEL_DIFF.
 *
 * @param a The first image
 * @param b The second image
 */
static void rod_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, i;
    for (i = 0; i < ROD_SIZE * ROD_SIZE; i ++) {
        int32_t d = rod_pixel_diff(a, b, i);
        worst = d > worst ? d : worst;
        differ += d > ROD_PIXEL_DIFF;
    }

    if (differ * 1000 > ROD_DIFF_PERMILLE * ROD_SIZE * ROD_SIZE) {
        printf("worst channel difference %d, %d pixels differ\n", worst, differ);
    }

    test_assert(differ * 1000 <= ROD_DIFF_PERMILLE * ROD_SIZE * ROD_SIZE);
}

/** Check that the lines of a debug pass show: at least ROD_LINES_PERMILLE of the pixels are
 * lit over the black clear.
 *
 * @param px The image
 */
static void rod_expect_lines(
    const unsigned char *px)
{
    int32_t lit = 0, i;
    for (i = 0; i < ROD_SIZE * ROD_SIZE; i ++) {
        lit += px[i * 4] > 40 || px[i * 4 + 1] > 40 || px[i * 4 + 2] > 40;
    }

    test_assert(lit * 1000 >= ROD_LINES_PERMILLE * ROD_SIZE * ROD_SIZE);
}

/** Free the images and the worlds of a test.
 *
 * @param worlds The worlds
 * @param px The last image of each world
 */
static void rod_free(
    ecs_world_t **worlds,
    unsigned char **px)
{
    int32_t w;
    for (w = 0; w < ROD_WORLDS; w ++) {
        stbi_image_free(px[w]);
        ecs_fini(worlds[w]);
    }
}

void RenderOriginDebug_grid_far_same_image(void) {
    // debug/grid, render/world_origin
    /* The grid seen a million units from the local origin, drawn relative to a render origin
     * that follows the camera, matches the grid seen near the local origin: its rays keep their
     * precision and its lines fall on the same whole units. */
    ecs_entity_t views[ROD_WORLDS];
    ecs_world_t *worlds[ROD_WORLDS] = {
        rod_world(ROD_NEAR_X, ROD_NEAR_Z, false, &views[0]),
        rod_world(ROD_FAR_X, ROD_FAR_Z, true, &views[1])
    };
    unsigned char *px[ROD_WORLDS] = {0};
    int32_t w;
    for (w = 0; w < ROD_WORLDS; w ++) {
        ecs_get_mut(worlds[w], views[w], View)->debug.grid = rod_grid;
        ecs_modified(worlds[w], views[w], View);
    }

    rod_render(worlds, px);
    rod_expect_lines(px[0]);
    rod_expect_same(px[0], px[1]);
    rod_free(worlds, px);
}

void RenderOriginDebug_bvh_far_same_image(void) {
    // debug/bvh, render/world_origin
    /* The boxes of the static tree around boxes a million units from the local origin, drawn
     * relative to a render origin that follows the camera, match those near the local
     * origin. */
    static const float boxes[3][3] = { {0, 0, -3}, {-2, 0, -5}, {2, 0, -6} };
    float at[ROD_WORLDS][2] = { {ROD_NEAR_X, ROD_NEAR_Z}, {ROD_FAR_X, ROD_FAR_Z} };
    ecs_entity_t views[ROD_WORLDS];
    ecs_world_t *worlds[ROD_WORLDS] = {
        rod_world(at[0][0], at[0][1], false, &views[0]),
        rod_world(at[1][0], at[1][1], true, &views[1])
    };
    unsigned char *px[ROD_WORLDS] = {0};
    int32_t w, b;
    for (w = 0; w < ROD_WORLDS; w ++) {
        ecs_get_mut(worlds[w], views[w], View)->debug.bvh = (BvhDebug){
            .static_tree = true, .leaves = true};
        ecs_modified(worlds[w], views[w], View);
        for (b = 0; b < 3; b ++) {
            ecs_entity_t e = ecs_new(worlds[w]);
            ecs_set(worlds[w], e, Position,
                {at[w][0] + boxes[b][0], boxes[b][1], at[w][1] + boxes[b][2]});
            ecs_set(worlds[w], e, Box, {2, 1, 2});
            ecs_set(worlds[w], e, Rgb, {0, 0, 0});
        }
    }

    rod_render(worlds, px);
    rod_expect_lines(px[0]);
    rod_expect_same(px[0], px[1]);
    rod_free(worlds, px);
}
