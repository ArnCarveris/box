/**
 * @file RenderOriginShadow.c
 * @brief Tests that the sun shadow cascades render and are looked up relative to the render
 * origin, with the GPU backend: a box casts its shadow on a ground plane far from the local
 * origin, the render origin following the camera (WorldOrigin.render, the world origin never
 * shifting), and the image matches the same scene at the local origin.
 *
 * Far from the local origin a local position in f32 is coarse (half a unit at the far offset of
 * the still case), so a shadow placed and looked up there would be blocky and off by pixels;
 * relative to the render origin it keeps its precision. The cascades stay snapped to the texels
 * of the local frame, so a camera moving by less than a texel leaves the shadow edges where they
 * are, as at the local origin.
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
#define ROS_SIZE (96)

/** Seconds of a frame */
#define ROS_DELTA (1.0f / 60.0f)

/** Local X and Z of the far scene of the still case, whole units so the f32 positions of the
 * scene are exact */
#define ROS_FAR_X (6000000.0f)
#define ROS_FAR_Z (-6000000.0f)

/** Local X and Z of the far scene of the moving case, where the moves of its camera are exact
 * in f32 */
#define ROS_MOVE_X (100000.0f)
#define ROS_MOVE_Z (-100000.0f)

/** Move of the camera per frame of the moving case, less than a texel of the first cascade and
 * exact in f32 at ROS_MOVE_X */
#define ROS_STEP (1.0f / 128.0f)

/** Frames of the still and the moving case */
#define ROS_FRAMES (4)
#define ROS_MOVE_FRAMES (16)

/** Channel difference above which a pixel counts as different, and the pixels that may, per
 * thousand: the cascades of the two scenes snap to the texels of their own local frame, so the
 * shadow edges may be a fraction of a texel apart */
#define ROS_PIXEL_DIFF (12)
#define ROS_DIFF_PERMILLE (10)

/** Channel difference above which a pixel counts as far off, and the pixels that may be, per
 * ten thousand */
#define ROS_MAX_DIFF (48)
#define ROS_FAR_PER_10K (10)

/** Pixels per thousand the shadow darkens, at least */
#define ROS_SHADOW_PERMILLE (20)

/** Worlds a test renders side by side */
#define ROS_WORLDS (3)

/** Create a headless world that renders on the main thread, with an ambient light, a sun, a
 * ground plane and a box standing on it around a point, and a camera above and behind the box
 * with a lit view.
 *
 * @param x X of the scene
 * @param z Z of the scene
 * @param shadows Whether the view renders shadows
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* ros_world(
    float x,
    float z,
    bool shadows,
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.3f});

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {x, -0.25f, z});
    ecs_set(world, ground, Box, {40, 0.5f, 40});
    ecs_set(world, ground, Rgb, {255, 255, 255});
    ecs_set(world, ground, PbrMaterial, {.roughness = 1});

    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {x, 1, z});
    ecs_set(world, caster, Box, {2, 2, 2});
    ecs_set(world, caster, Rgb, {200, 60, 60});
    ecs_set(world, caster, PbrMaterial, {.roughness = 1});

    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {x, 8, z + 6});
    ecs_set(world, *camera, Rotation, {-0.95f, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = *camera;
    v->clear = (ClearSettings){.enabled = true, .color = {30, 40, 60, 255}};
    v->lighting.enabled = true;
    v->shadows.enabled = shadows;
    ecs_modified(world, view, View);

    return world;
}

/** Draw a world relative to a render origin that follows its camera, without shifts of the
 * world origin.
 *
 * @param world The world
 */
static void ros_render_follow(
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
static unsigned char* ros_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ROS_SIZE, .height = ROS_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ROS_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ROS_SIZE);
    test_int(h, ROS_SIZE);
    ecs_os_free(path);

    return result;
}

/** Render the next frame of every world, each image replacing the last one of its world.
 *
 * @param worlds The worlds
 * @param count Worlds, at most ROS_WORLDS
 * @param px The last image of each world, NULL for none, receives the new ones
 */
static void ros_step(
    ecs_world_t **worlds,
    int32_t count,
    unsigned char **px)
{
    static const char *files[ROS_WORLDS] = { "ros0.png", "ros1.png", "ros2.png" };
    int32_t w;
    test_assert(count <= ROS_WORLDS);
    for (w = 0; w < count; w ++) {
        stbi_image_free(px[w]);
        px[w] = ros_frame(worlds[w], files[w]);
    }
}

/** Get the largest channel difference of a pixel of two images.
 *
 * @param a The first image
 * @param b The second image
 * @param i The pixel
 * @return The difference, 0 to 255
 */
static int32_t ros_pixel_diff(
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

/** Check that two images of the shadowed scene match: at most ROS_DIFF_PERMILLE of the pixels
 * differ by more than ROS_PIXEL_DIFF, at most ROS_FAR_PER_10K by more than ROS_MAX_DIFF.
 *
 * @param a The first image
 * @param b The second image
 */
static void ros_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, far = 0, i;
    for (i = 0; i < ROS_SIZE * ROS_SIZE; i ++) {
        int32_t d = ros_pixel_diff(a, b, i);
        worst = d > worst ? d : worst;
        differ += d > ROS_PIXEL_DIFF;
        far += d > ROS_MAX_DIFF;
    }

    if (far * 10000 > ROS_FAR_PER_10K * ROS_SIZE * ROS_SIZE ||
        differ * 1000 > ROS_DIFF_PERMILLE * ROS_SIZE * ROS_SIZE)
    {
        printf("worst channel difference %d, %d pixels differ, %d far\n", worst, differ, far);
    }

    test_assert(far * 10000 <= ROS_FAR_PER_10K * ROS_SIZE * ROS_SIZE);
    test_assert(differ * 1000 <= ROS_DIFF_PERMILLE * ROS_SIZE * ROS_SIZE);
}

/** Check that the shadow shows: the image with shadows is darker than the one without at
 * ROS_SHADOW_PERMILLE of the pixels at least.
 *
 * @param shadowed The image with shadows
 * @param lit The image without shadows
 */
static void ros_expect_shadow(
    const unsigned char *shadowed,
    const unsigned char *lit)
{
    int32_t dark = 0, i;
    for (i = 0; i < ROS_SIZE * ROS_SIZE; i ++) {
        dark += (int)lit[i * 4 + 1] - (int)shadowed[i * 4 + 1] > 40;
    }

    if (dark * 1000 < ROS_SHADOW_PERMILLE * ROS_SIZE * ROS_SIZE) {
        printf("%d pixels darkened by the shadow\n", dark);
    }

    test_assert(dark * 1000 >= ROS_SHADOW_PERMILLE * ROS_SIZE * ROS_SIZE);
}

/** Free the images and the worlds of a test.
 *
 * @param worlds The worlds
 * @param count Worlds
 * @param px The last image of each world
 */
static void ros_free(
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

void RenderOriginShadow_far_same_image(void) {
    // render/world_origin
    /* A box casts its shadow on the ground six million units from the local origin, drawn
     * relative to a render origin that follows the camera: the image matches the same scene
     * at the local origin, and the shadow shows. */
    ecs_entity_t cameras[ROS_WORLDS];
    ecs_world_t *worlds[ROS_WORLDS] = {
        ros_world(0, 0, true, &cameras[0]),
        ros_world(ROS_FAR_X, ROS_FAR_Z, true, &cameras[1]),
        ros_world(0, 0, false, &cameras[2])
    };
    unsigned char *px[ROS_WORLDS] = {0};
    ros_render_follow(worlds[1]);
    for (int32_t i = 0; i < ROS_FRAMES; i ++) {
        ros_step(worlds, ROS_WORLDS, px);
    }

    ros_expect_shadow(px[0], px[2]);
    ros_expect_same(px[0], px[1]);
    ros_free(worlds, ROS_WORLDS, px);
}

void RenderOriginShadow_far_moving_no_swim(void) {
    // render/world_origin
    /* The camera moves by less than a texel of the first cascade every frame, a hundred
     * thousand units from the local origin, the render origin following it, the cascades
     * rendering at intervals so most frames sample cascades kept from an earlier frame: every
     * frame matches the same moves at the local origin, so the shadow edges stay where the
     * texels of the local frame put them instead of moving with the camera. */
    ecs_entity_t cameras[2];
    ecs_world_t *worlds[2] = {
        ros_world(0, 0, true, &cameras[0]),
        ros_world(ROS_MOVE_X, ROS_MOVE_Z, true, &cameras[1])
    };
    unsigned char *px[2] = {0};
    float origin[2][2] = { {0, 0}, {ROS_MOVE_X, ROS_MOVE_Z} };
    int32_t w;
    ros_render_follow(worlds[1]);
    for (w = 0; w < 2; w ++) {
        ecs_iter_t it = ecs_each(worlds[w], View);
        ecs_entity_t view = 0;
        while (ecs_each_next(&it)) {
            view = it.count ? it.entities[0] : view;
        }

        View *v = ecs_get_mut(worlds[w], view, View);
        v->shadows.interval[0] = 1;
        v->shadows.interval[1] = 2;
        v->shadows.interval[2] = 3;
        v->shadows.interval[3] = 4;
        ecs_modified(worlds[w], view, View);
    }

    for (int32_t i = 0; i < ROS_MOVE_FRAMES; i ++) {
        float d = ROS_STEP * (float)i;
        for (w = 0; w < 2; w ++) {
            ecs_set(worlds[w], cameras[w], Position,
                {origin[w][0] + d, 8, origin[w][1] + 6 - d});
        }

        ros_step(worlds, 2, px);
        ros_expect_same(px[0], px[1]);
    }

    ros_free(worlds, 2, px);
}
