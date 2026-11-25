/**
 * @file RenderOriginPlanar.c
 * @brief Tests that the planar reflections draw relative to the render origin, with the GPU
 * backend: a mirror floor reflects a few emissive boxes far from the local origin, the render
 * origin following the camera (WorldOrigin.render, the world origin never shifting), and the
 * image matches the same scene at the local origin.
 *
 * Far from the local origin the mirrored view moved to the render origin in f32 is coarse (half
 * a unit at the far offset of the still case), so the mirror image would be off by pixels;
 * built from the mirrored camera in f64 it keeps its precision. A camera moving by small steps
 * keeps the mirror image where it is at the local origin instead of making it jitter.
 *
 * The scenes have no light but the emission of the boxes and are seen into a 96x96 image.
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
#define ROP_SIZE (96)

/** Seconds of a frame */
#define ROP_DELTA (1.0f / 60.0f)

/** Local X and Z of the far scene of the still case, whole units so the f32 positions of the
 * scene are exact */
#define ROP_FAR_X (6000000.0f)
#define ROP_FAR_Z (-6000000.0f)

/** Local X and Z of the far scene of the moving case, where the moves of its camera are exact
 * in f32 */
#define ROP_MOVE_X (100000.0f)
#define ROP_MOVE_Z (-100000.0f)

/** Move of the camera per frame of the moving case, exact in f32 at ROP_MOVE_X */
#define ROP_STEP (1.0f / 128.0f)

/** Frames of the still and the moving case */
#define ROP_FRAMES (4)
#define ROP_MOVE_FRAMES (16)

/** Channel difference above which a pixel counts as different, and the pixels that may, per
 * thousand */
#define ROP_PIXEL_DIFF (12)
#define ROP_DIFF_PERMILLE (10)

/** Channel difference above which a pixel counts as far off, and the pixels that may be, per
 * ten thousand */
#define ROP_MAX_DIFF (48)
#define ROP_FAR_PER_10K (10)

/** Pixels per thousand the reflection shows on the floor, at least */
#define ROP_MIRROR_PERMILLE (20)

/** Worlds a test renders side by side */
#define ROP_WORLDS (3)

/** Add a box that shows an emission only.
 *
 * @param world The world
 * @param x X of its center
 * @param y Y of its center
 * @param z Z of its center
 * @param size Edge of the box
 * @param r Red of the emission
 * @param g Green of the emission
 * @param b Blue of the emission
 */
static void rop_box(
    ecs_world_t *world,
    float x,
    float y,
    float z,
    float size,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_set(world, e, Box, {size, size, size});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {r, g, b}});
}

/** Create a headless world that renders on the main thread, without ambient light and with a
 * point light of intensity 0 in place of the default sun, its default scene with
 * Scene.planar_reflections: a floor of white metal on the plane y = 0 around a point,
 * optionally a mirror, a pattern of emissive boxes above it and a camera behind them that
 * looks down at the floor with a lit view.
 *
 * @param x X of the scene
 * @param z Z of the scene
 * @param planar Whether the floor has a PlanarReflection
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* rop_world(
    float x,
    float z,
    bool planar,
    ecs_entity_t *camera)
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
    ecs_set(world, light, Position, {x + 1000, 1000, z + 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {x, 0, z});
    ecs_set(world, floor, Box, {40, 0.02f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 0});
    if (planar) {
        ecs_set(world, floor, PlanarReflection, {.resolution = 1, .clip_offset = 0.02f,
            .distortion = 0});
    }

    rop_box(world, x - 1.5f, 1.5f, z, 1, 255, 0, 0);
    rop_box(world, x, 1, z - 1, 1, 0, 255, 0);
    rop_box(world, x + 1.5f, 2, z, 1, 0, 0, 255);
    rop_box(world, x - 0.5f, 3, z - 2, 0.5f, 255, 255, 0);
    rop_box(world, x + 0.5f, 0.75f, z + 1, 0.5f, 0, 255, 255);

    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {x, 3, z + 8});
    ecs_set(world, *camera, Rotation, {-0.35f, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = *camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting = {.enabled = true}});

    return world;
}

/** Draw a world relative to a render origin that follows its camera, without shifts of the
 * world origin.
 *
 * @param world The world
 */
static void rop_render_follow(
    ecs_world_t *world)
{
    ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = true});
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* rop_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ROP_SIZE, .height = ROP_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ROP_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ROP_SIZE);
    test_int(h, ROP_SIZE);
    ecs_os_free(path);

    return result;
}

/** Render the next frame of every world, each image replacing the last one of its world.
 *
 * @param worlds The worlds
 * @param count Worlds, at most ROP_WORLDS
 * @param px The last image of each world, NULL for none, receives the new ones
 */
static void rop_step(
    ecs_world_t **worlds,
    int32_t count,
    unsigned char **px)
{
    static const char *files[ROP_WORLDS] = { "rop0.png", "rop1.png", "rop2.png" };
    int32_t w;
    test_assert(count <= ROP_WORLDS);
    for (w = 0; w < count; w ++) {
        stbi_image_free(px[w]);
        px[w] = rop_frame(worlds[w], files[w]);
    }
}

/** Get the largest channel difference of a pixel of two images.
 *
 * @param a The first image
 * @param b The second image
 * @param i The pixel
 * @return The difference, 0 to 255
 */
static int32_t rop_pixel_diff(
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

/** Check that two images of the scene match: at most ROP_DIFF_PERMILLE of the pixels differ by
 * more than ROP_PIXEL_DIFF, at most ROP_FAR_PER_10K by more than ROP_MAX_DIFF.
 *
 * @param a The first image
 * @param b The second image
 */
static void rop_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, far = 0, i;
    for (i = 0; i < ROP_SIZE * ROP_SIZE; i ++) {
        int32_t d = rop_pixel_diff(a, b, i);
        worst = d > worst ? d : worst;
        differ += d > ROP_PIXEL_DIFF;
        far += d > ROP_MAX_DIFF;
    }

    if (far * 10000 > ROP_FAR_PER_10K * ROP_SIZE * ROP_SIZE ||
        differ * 1000 > ROP_DIFF_PERMILLE * ROP_SIZE * ROP_SIZE)
    {
        printf("worst channel difference %d, %d pixels differ, %d far\n", worst, differ, far);
    }

    test_assert(far * 10000 <= ROP_FAR_PER_10K * ROP_SIZE * ROP_SIZE);
    test_assert(differ * 1000 <= ROP_DIFF_PERMILLE * ROP_SIZE * ROP_SIZE);
}

/** Check that the reflection shows: the image with the mirror differs from the one without at
 * ROP_MIRROR_PERMILLE of the pixels at least.
 *
 * @param mirrored The image with the mirror
 * @param plain The image without it
 */
static void rop_expect_mirror(
    const unsigned char *mirrored,
    const unsigned char *plain)
{
    int32_t shown = 0, i;
    for (i = 0; i < ROP_SIZE * ROP_SIZE; i ++) {
        shown += rop_pixel_diff(mirrored, plain, i) > 40;
    }

    if (shown * 1000 < ROP_MIRROR_PERMILLE * ROP_SIZE * ROP_SIZE) {
        printf("%d pixels show the reflection\n", shown);
    }

    test_assert(shown * 1000 >= ROP_MIRROR_PERMILLE * ROP_SIZE * ROP_SIZE);
}

/** Free the images and the worlds of a test.
 *
 * @param worlds The worlds
 * @param count Worlds
 * @param px The last image of each world
 */
static void rop_free(
    ecs_world_t **worlds,
    int32_t count,
    unsigned char **px)
{
    int32_t w;
    for (w = 0; w < count; w ++) {
        stbi_image_free(px[w]);
        ecs_fini(worlds[w]);
    }
}

void RenderOriginPlanar_far_same_image(void) {
    // render/world_origin
    /* A mirror floor reflects emissive boxes six million units from the local origin, drawn
     * relative to a render origin that follows the camera: the image matches the same scene
     * at the local origin, and the reflection shows. */
    ecs_entity_t cameras[ROP_WORLDS];
    ecs_world_t *worlds[ROP_WORLDS] = {
        rop_world(0, 0, true, &cameras[0]),
        rop_world(ROP_FAR_X, ROP_FAR_Z, true, &cameras[1]),
        rop_world(0, 0, false, &cameras[2])
    };
    unsigned char *px[ROP_WORLDS] = {0};
    rop_render_follow(worlds[1]);
    for (int32_t i = 0; i < ROP_FRAMES; i ++) {
        rop_step(worlds, ROP_WORLDS, px);
    }

    rop_expect_mirror(px[0], px[2]);
    rop_expect_same(px[0], px[1]);
    rop_free(worlds, ROP_WORLDS, px);
}

void RenderOriginPlanar_far_moving_no_jitter(void) {
    // render/world_origin
    /* The camera moves by small steps a hundred thousand units from the local origin, the
     * render origin following it: every frame matches the same moves at the local origin, so
     * the mirror image does not jitter. */
    ecs_entity_t cameras[2];
    ecs_world_t *worlds[2] = {
        rop_world(0, 0, true, &cameras[0]),
        rop_world(ROP_MOVE_X, ROP_MOVE_Z, true, &cameras[1])
    };
    unsigned char *px[2] = {0};
    float origin[2][2] = { {0, 0}, {ROP_MOVE_X, ROP_MOVE_Z} };
    int32_t w;
    rop_render_follow(worlds[1]);
    for (int32_t i = 0; i < ROP_MOVE_FRAMES; i ++) {
        float d = ROP_STEP * (float)i;
        for (w = 0; w < 2; w ++) {
            ecs_set(worlds[w], cameras[w], Position,
                {origin[w][0] + d, 3, origin[w][1] + 8 - d});
        }

        rop_step(worlds, 2, px);
        rop_expect_same(px[0], px[1]);
    }

    rop_free(worlds, 2, px);
}
