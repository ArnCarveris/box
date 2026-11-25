/**
 * @file RenderOriginImpostor.c
 * @brief Tests that the impostor cards draw, are lit and cast their sun shadow relative to the
 * render origin, with the GPU backend: the card of a tower stands on a ground plane far from the
 * local origin and casts its shadow on it, the render origin following the camera
 * (WorldOrigin.render, the world origin never shifting), and the image matches the same scene
 * at the local origin.
 *
 * Far from the local origin a local position in f32 is coarse (a sixteenth of a unit at the far
 * offset of the still case), so a card placed there, its shadow drawn there and the point its
 * own shadow is looked up at would be off by pixels; relative to the render origin they keep
 * their precision. The cascades stay snapped to the texels of the local frame, so a camera
 * moving by less than a texel leaves the shadow of the card where it is, as at the local origin.
 *
 * The prefab is Tower of test/assets/impostor/draw.flecs. The scenes are seen into a 96x96
 * image.
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
#define ROI_SIZE (96)

/** Seconds of a frame */
#define ROI_DELTA (1.0f / 60.0f)

/** Local X and Z of the far scene of the still case, whole units so the f32 positions of the
 * scene are exact */
#define ROI_FAR_X (1000000.0f)
#define ROI_FAR_Z (-1000000.0f)

/** Local X and Z of the far scene of the moving case, where the moves of its camera are exact
 * in f32 */
#define ROI_MOVE_X (100000.0f)
#define ROI_MOVE_Z (-100000.0f)

/** Move of the camera per frame of the moving case, less than a texel of the first cascade and
 * exact in f32 at ROI_MOVE_X */
#define ROI_STEP (1.0f / 128.0f)

/** Frames of the still and the moving case */
#define ROI_FRAMES (4)
#define ROI_MOVE_FRAMES (16)

/** Frames a world runs at most for the bake of its impostor */
#define ROI_BAKE_FRAMES (400)

/** Views per axis and pixels of a view of the bakes */
#define ROI_IMPOSTOR_FRAMES (5)
#define ROI_IMPOSTOR_RES (64)

/** Distance from the camera beyond which the card draws in place of the tower, nearer than the
 * camera of the scenes */
#define ROI_IMPOSTOR_DISTANCE (5.0f)

/** Channel difference above which a pixel counts as different, and the pixels that may, per
 * thousand: the cascades of the two scenes snap to the texels of their own local frame, so the
 * shadow edges may be a fraction of a texel apart */
#define ROI_PIXEL_DIFF (12)
#define ROI_DIFF_PERMILLE (10)

/** Channel difference above which a pixel counts as far off, and the pixels that may be, per
 * ten thousand */
#define ROI_MAX_DIFF (48)
#define ROI_FAR_PER_10K (10)

/** Pixels per thousand the shadow of the card darkens, at least */
#define ROI_SHADOW_PERMILLE (20)

/** Worlds a test renders side by side */
#define ROI_WORLDS (3)

/** Create a headless world that renders with the GPU on the main thread, with an ambient light,
 * a sun, a ground plane and an instance of Tower standing on it around a point, drawn as its
 * card, and a camera above and behind it with a lit view. Runs frames until the impostor of
 * Tower is baked, then draws relative to a render origin that follows the camera with follow.
 *
 * @param x X of the scene
 * @param z Z of the scene
 * @param shadows Whether the view renders shadows
 * @param follow Whether the world draws relative to a render origin that follows the camera
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* roi_world(
    float x,
    float z,
    bool shadows,
    bool follow,
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.alpha_cutout = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    box_render_use_gpu(world);
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.3f});

    char *file = box_app_locate("test/assets/impostor/draw.flecs");
    test_assert(file != NULL);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "impostor_script" }),
        .filename = file
    }) != 0);
    ecs_os_free(file);

    ecs_entity_t tower = ecs_lookup(world, "Tower");
    test_assert(tower != 0);
    ecs_set(world, tower, Impostor, {
        .frames = ROI_IMPOSTOR_FRAMES,
        .resolution = ROI_IMPOSTOR_RES,
        .hemisphere = true,
        .distance = ROI_IMPOSTOR_DISTANCE,
        .fade = 1
    });

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.9f, 0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {x, -0.25f, z});
    ecs_set(world, ground, Box, {40, 0.5f, 40});
    ecs_set(world, ground, Rgb, {255, 255, 255});
    ecs_set(world, ground, PbrMaterial, {.roughness = 1});

    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, tower);
    ecs_set(world, inst, Position, {x, 3, z});

    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {x, 12, z + 12});
    ecs_set(world, *camera, Rotation, {-0.75f, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = *camera,
        .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {30, 40, 60, 255}},
        .lighting.enabled = true,
        .shadows = {.enabled = shadows, .cascades = 2, .resolution = 1024,
            .max_distance = 60, .split_lambda = 0.5f, .depth_bias = 2, .normal_offset = 2,
            .filter_size = 3, .fade = 0.1f, .blend = 0.1f}});

    const ImpostorStatus *s = NULL;
    int32_t i;
    for (i = 0; i < ROI_BAKE_FRAMES; i ++) {
        ecs_progress(world, 0);
        s = ecs_get(world, tower, ImpostorStatus);
        if (s && s->ready) {
            break;
        }
    }

    test_assert(s != NULL);
    test_bool(s->ready, true);
    box_render_sync(world);

    /* The bake renders the copy of Tower at the origin of a scene of its own, which a render
     * origin far from it would place coarsely: the far scene follows the camera once baked */
    if (follow) {
        ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = true});
    }

    return world;
}

/** Find the view of a world.
 *
 * @param world The world
 * @return The view
 */
static ecs_entity_t roi_view(
    ecs_world_t *world)
{
    ecs_iter_t it = ecs_each(world, View);
    ecs_entity_t result = 0;
    while (ecs_each_next(&it)) {
        result = it.count ? it.entities[0] : result;
    }

    test_assert(result != 0);
    return result;
}

/** Render the next frame into an image file of the temporary folder and load it; the frame
 * draws the impostor as its card.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* roi_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ROI_SIZE, .height = ROI_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ROI_DELTA);
    box_render_sync(world);

    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.impostors, 1);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ROI_SIZE);
    test_int(h, ROI_SIZE);
    ecs_os_free(path);

    return result;
}

/** Render the next frame of every world, each image replacing the last one of its world.
 *
 * @param worlds The worlds
 * @param count Worlds, at most ROI_WORLDS
 * @param px The last image of each world, NULL for none, receives the new ones
 */
static void roi_step(
    ecs_world_t **worlds,
    int32_t count,
    unsigned char **px)
{
    static const char *files[ROI_WORLDS] = { "roi0.png", "roi1.png", "roi2.png" };
    int32_t w;
    test_assert(count <= ROI_WORLDS);
    for (w = 0; w < count; w ++) {
        stbi_image_free(px[w]);
        px[w] = roi_frame(worlds[w], files[w]);
    }
}

/** Get the largest channel difference of a pixel of two images.
 *
 * @param a The first image
 * @param b The second image
 * @param i The pixel
 * @return The difference, 0 to 255
 */
static int32_t roi_pixel_diff(
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

/** Check that two images of the scene match: at most ROI_DIFF_PERMILLE of the pixels differ by
 * more than ROI_PIXEL_DIFF, at most ROI_FAR_PER_10K by more than ROI_MAX_DIFF.
 *
 * @param a The first image
 * @param b The second image
 */
static void roi_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, far = 0, i;
    for (i = 0; i < ROI_SIZE * ROI_SIZE; i ++) {
        int32_t d = roi_pixel_diff(a, b, i);
        worst = d > worst ? d : worst;
        differ += d > ROI_PIXEL_DIFF;
        far += d > ROI_MAX_DIFF;
    }

    if (far * 10000 > ROI_FAR_PER_10K * ROI_SIZE * ROI_SIZE ||
        differ * 1000 > ROI_DIFF_PERMILLE * ROI_SIZE * ROI_SIZE)
    {
        printf("worst channel difference %d, %d pixels differ, %d far\n", worst, differ, far);
    }

    test_assert(far * 10000 <= ROI_FAR_PER_10K * ROI_SIZE * ROI_SIZE);
    test_assert(differ * 1000 <= ROI_DIFF_PERMILLE * ROI_SIZE * ROI_SIZE);
}

/** Check that the shadow of the card shows: the image with shadows is darker than the one
 * without at ROI_SHADOW_PERMILLE of the pixels at least.
 *
 * @param shadowed The image with shadows
 * @param lit The image without shadows
 */
static void roi_expect_shadow(
    const unsigned char *shadowed,
    const unsigned char *lit)
{
    int32_t dark = 0, i;
    for (i = 0; i < ROI_SIZE * ROI_SIZE; i ++) {
        dark += (int)lit[i * 4 + 1] - (int)shadowed[i * 4 + 1] > 40;
    }

    if (dark * 1000 < ROI_SHADOW_PERMILLE * ROI_SIZE * ROI_SIZE) {
        printf("%d pixels darkened by the shadow\n", dark);
    }

    test_assert(dark * 1000 >= ROI_SHADOW_PERMILLE * ROI_SIZE * ROI_SIZE);
}

/** Free the images and the worlds of a test.
 *
 * @param worlds The worlds
 * @param count Worlds
 * @param px The last image of each world
 */
static void roi_free(
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

void RenderOriginImpostor_far_same_image(void) {
    // render/world_origin
    /* The card of a tower casts its shadow on the ground a million units from the local origin,
     * drawn relative to a render origin that follows the camera: the image matches the same
     * scene at the local origin, and the shadow shows. */
    ecs_entity_t cameras[ROI_WORLDS];
    ecs_world_t *worlds[ROI_WORLDS] = {
        roi_world(0, 0, true, false, &cameras[0]),
        roi_world(ROI_FAR_X, ROI_FAR_Z, true, true, &cameras[1]),
        roi_world(0, 0, false, false, &cameras[2])
    };
    unsigned char *px[ROI_WORLDS] = {0};
    for (int32_t i = 0; i < ROI_FRAMES; i ++) {
        roi_step(worlds, ROI_WORLDS, px);
    }

    roi_expect_shadow(px[0], px[2]);
    roi_expect_same(px[0], px[1]);
    roi_free(worlds, ROI_WORLDS, px);
}

void RenderOriginImpostor_far_moving_no_swim(void) {
    // render/world_origin
    /* The camera moves by less than a texel of the first cascade every frame, a hundred
     * thousand units from the local origin, the render origin following it, the cascades
     * rendering at intervals so most frames sample cascades kept from an earlier frame: every
     * frame matches the same moves at the local origin, so the card and its shadow stay where
     * they are instead of moving with the camera. */
    ecs_entity_t cameras[2];
    ecs_world_t *worlds[2] = {
        roi_world(0, 0, true, false, &cameras[0]),
        roi_world(ROI_MOVE_X, ROI_MOVE_Z, true, true, &cameras[1])
    };
    unsigned char *px[2] = {0};
    float origin[2][2] = { {0, 0}, {ROI_MOVE_X, ROI_MOVE_Z} };
    int32_t w;
    for (w = 0; w < 2; w ++) {
        ecs_entity_t view = roi_view(worlds[w]);
        View *v = ecs_get_mut(worlds[w], view, View);
        v->shadows.interval[0] = 1;
        v->shadows.interval[1] = 2;
        ecs_modified(worlds[w], view, View);
    }

    for (int32_t i = 0; i < ROI_MOVE_FRAMES; i ++) {
        float d = ROI_STEP * (float)i;
        for (w = 0; w < 2; w ++) {
            ecs_set(worlds[w], cameras[w], Position,
                {origin[w][0] + d, 12, origin[w][1] + 12 - d});
        }

        roi_step(worlds, 2, px);
        roi_expect_same(px[0], px[1]);
    }

    roi_free(worlds, 2, px);
}
