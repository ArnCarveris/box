/**
 * @file RenderOriginMover.c
 * @brief Tests that the shape of a mover draws relative to the render origin like the shapes of
 * its descendants, with the GPU backend: a WorldCell root, a mover, has a box of its own and a
 * child box touching it, far from the local origin, the render origin following the camera
 * (WorldOrigin.render, the world origin never shifting), and the image matches the same scene
 * at the local origin.
 *
 * The world origin of the far scene is moved by a fraction of a unit, so the corner of the cell
 * relative to the local origin is not a value of f32: the Position of the root rounds it to a
 * quarter of a unit, while the mover table holds it exactly in f64. A shape of the mover drawn
 * from its own world transform would be off by that rounding against its child, which rides the
 * mover table; drawn through the mover table it touches its child as at the local origin.
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
#define ROM_SIZE (96)

/** Seconds of a frame */
#define ROM_DELTA (1.0f / 60.0f)

/** Cell along X of the root of the far scene, two million units from the local origin, where
 * f32 positions are quarters of a unit */
#define ROM_FAR_CELL (513)

/** Distance the world origin of the far scene moves along X, which leaves the corner of the
 * cell 0.12 above the quarter its Position rounds it down to, away from the child, so a shape
 * of the mover placed from its Position opens a gap at the seam */
#define ROM_FAR_SHIFT (0.13)

/** Frames of a test */
#define ROM_FRAMES (4)

/** Channel difference above which a pixel counts as different, and the pixels that may, per
 * thousand, as the other RenderOrigin tests allow */
#define ROM_PIXEL_DIFF (12)
#define ROM_DIFF_PERMILLE (10)

/** Channel difference above which a pixel counts as far off, and the pixels that may be, per
 * ten thousand */
#define ROM_MAX_DIFF (48)
#define ROM_FAR_PER_10K (10)

/** Pixels per thousand the box of the mover and the box of its child cover, at least */
#define ROM_CONTENT_PERMILLE (40)

/** Worlds a test renders side by side */
#define ROM_WORLDS (2)

/** Create a headless world that renders on the main thread, with an ambient light, a sun, a
 * WorldCell root at a cell along X with a red box of its own and a blue child box touching it
 * along X, and a camera with a lit view.
 *
 * @param cell The cell of the root along X
 * @param root Receives the root
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* rom_world(
    int32_t cell,
    ecs_entity_t *root,
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.3f});
    ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = true});

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    *root = ecs_new(world);
    ecs_set(world, *root, WorldCell, {cell, 0, 0});
    ecs_set(world, *root, Box, {1, 1, 1});
    ecs_set(world, *root, Rgb, {220, 60, 60});
    ecs_set(world, *root, PbrMaterial, {.roughness = 1});

    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, *root);
    ecs_set(world, child, Position, {1, 0, 0});
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_set(world, child, Rgb, {60, 60, 220});
    ecs_set(world, child, PbrMaterial, {.roughness = 1});

    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {0.5f, 1, 2.5f});
    ecs_set(world, *camera, Rotation, {-0.3f, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 40, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = *camera;
    v->clear = (ClearSettings){.enabled = true, .color = {30, 40, 60, 255}};
    v->lighting.enabled = true;
    ecs_modified(world, view, View);

    return world;
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* rom_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ROM_SIZE, .height = ROM_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ROM_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ROM_SIZE);
    test_int(h, ROM_SIZE);
    ecs_os_free(path);

    return result;
}

/** Render the next frame of every world, each image replacing the last one of its world.
 *
 * @param worlds The worlds
 * @param px The last image of each world, NULL for none, receives the new ones
 */
static void rom_step(
    ecs_world_t **worlds,
    unsigned char **px)
{
    static const char *files[ROM_WORLDS] = { "rom0.png", "rom1.png" };
    int32_t w;
    for (w = 0; w < ROM_WORLDS; w ++) {
        stbi_image_free(px[w]);
        px[w] = rom_frame(worlds[w], files[w]);
    }
}

/** Get the largest channel difference of a pixel of two images.
 *
 * @param a The first image
 * @param b The second image
 * @param i The pixel
 * @return The difference, 0 to 255
 */
static int32_t rom_pixel_diff(
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

/** Check that two images of the scene match: at most ROM_DIFF_PERMILLE of the pixels differ by
 * more than ROM_PIXEL_DIFF, at most ROM_FAR_PER_10K by more than ROM_MAX_DIFF.
 *
 * @param a The first image
 * @param b The second image
 */
static void rom_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, far = 0, i;
    for (i = 0; i < ROM_SIZE * ROM_SIZE; i ++) {
        int32_t d = rom_pixel_diff(a, b, i);
        worst = d > worst ? d : worst;
        differ += d > ROM_PIXEL_DIFF;
        far += d > ROM_MAX_DIFF;
    }

    if (far * 10000 > ROM_FAR_PER_10K * ROM_SIZE * ROM_SIZE ||
        differ * 1000 > ROM_DIFF_PERMILLE * ROM_SIZE * ROM_SIZE)
    {
        printf("worst channel difference %d, %d pixels differ, %d far\n", worst, differ, far);
    }

    test_assert(far * 10000 <= ROM_FAR_PER_10K * ROM_SIZE * ROM_SIZE);
    test_assert(differ * 1000 <= ROM_DIFF_PERMILLE * ROM_SIZE * ROM_SIZE);
}

/** Check that the box of the mover and the box of its child show: the reddish and the bluish
 * pixels each cover ROM_CONTENT_PERMILLE of the image at least.
 *
 * @param px The image
 */
static void rom_expect_content(
    const unsigned char *px)
{
    int32_t red = 0, blue = 0, i;
    for (i = 0; i < ROM_SIZE * ROM_SIZE; i ++) {
        int32_t r = px[i * 4], b = px[i * 4 + 2];
        red += r > b + 40;
        blue += b > r + 40 && px[i * 4 + 1] + 20 < b;
    }

    if (red * 1000 < ROM_CONTENT_PERMILLE * ROM_SIZE * ROM_SIZE ||
        blue * 1000 < ROM_CONTENT_PERMILLE * ROM_SIZE * ROM_SIZE)
    {
        printf("%d red pixels, %d blue pixels\n", red, blue);
    }

    test_assert(red * 1000 >= ROM_CONTENT_PERMILLE * ROM_SIZE * ROM_SIZE);
    test_assert(blue * 1000 >= ROM_CONTENT_PERMILLE * ROM_SIZE * ROM_SIZE);
}

void RenderOriginMover_far_same_image(void) {
    // render/world_origin
    /* A WorldCell root with a box of its own and a child box touching it, two million units
     * from the local origin, its corner off the f32 values there by the shift of the world
     * origin, drawn relative to a render origin that follows the camera: the image matches the
     * same scene at the local origin, the box of the mover touching the box of its child. */
    ecs_entity_t roots[ROM_WORLDS], cameras[ROM_WORLDS];
    ecs_world_t *worlds[ROM_WORLDS] = {
        rom_world(0, &roots[0], &cameras[0]),
        rom_world(ROM_FAR_CELL, &roots[1], &cameras[1])
    };
    unsigned char *px[ROM_WORLDS] = {0};
    int32_t i;
    box_world_origin_shift(worlds[1], 0, (double[3]){ ROM_FAR_SHIFT, 0, 0 });
    rom_step(worlds, px);

    /* The camera of the far scene stands above the seam from the Position of the root, the
     * corner rounded to f32; the camera of the near scene stands where that is from the exact
     * corner */
    double corner = (double)ROM_FAR_CELL * BOX_WORLD_CELL_SIZE - ROM_FAR_SHIFT;
    float at = ecs_get(worlds[1], roots[1], Position)->x;
    test_assert((double)at != corner);
    ecs_set(worlds[1], cameras[1], Position, {at + 0.5f, 1, 2.5f});
    ecs_set(worlds[0], cameras[0], Position, {(float)((double)at - corner) + 0.5f, 1, 2.5f});
    for (i = 0; i < ROM_FRAMES; i ++) {
        rom_step(worlds, px);
    }

    rom_expect_content(px[0]);
    rom_expect_same(px[0], px[1]);
    for (i = 0; i < ROM_WORLDS; i ++) {
        stbi_image_free(px[i]);
        ecs_fini(worlds[i]);
    }
}
