/**
 * @file RenderOriginFog.c
 * @brief Tests that the froxel grid of the fog volumes is lit relative to the render origin,
 * with the GPU backend: a roof shades the sun's light in an unbounded fog over a ground plane
 * far from the local origin, the render origin following the camera (WorldOrigin.render, the
 * world origin never shifting), and the image matches the same scene at the local origin.
 *
 * Far from the local origin a local position in f32 is coarse (a sixteenth of a unit at the far
 * offset of the still case), so the rays of the grid, whose two points are a tenth of a unit
 * apart at the near plane, and the positions at which the fog looks up the sun's shadow would
 * be off; relative to the render origin they keep their precision. The cascades stay snapped to
 * the texels of the local frame, so a camera moving by less than a texel leaves the shafts
 * where they are, as at the local origin.
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
#define ROF_SIZE (96)

/** Seconds of a frame */
#define ROF_DELTA (1.0f / 60.0f)

/** Local X and Z of the far scene of the still case, whole units so the f32 positions of the
 * scene are exact */
#define ROF_FAR_X (1000000.0f)
#define ROF_FAR_Z (-1000000.0f)

/** Local X and Z of the far scene of the moving case, where the moves of its camera are exact
 * in f32 */
#define ROF_MOVE_X (100000.0f)
#define ROF_MOVE_Z (-100000.0f)

/** Move of the camera per frame of the moving case, less than a texel of the first cascade and
 * exact in f32 at ROF_MOVE_X */
#define ROF_STEP (1.0f / 128.0f)

/** Frames of the still and the moving case */
#define ROF_FRAMES (8)
#define ROF_MOVE_FRAMES (16)

/** Channel difference above which a pixel counts as different, and the pixels that may, per
 * thousand: the cascades of the two scenes snap to the texels of their own local frame, so the
 * edges of the shafts may be a fraction of a texel apart */
#define ROF_PIXEL_DIFF (12)
#define ROF_DIFF_PERMILLE (10)

/** Channel difference above which a pixel counts as far off, and the pixels that may be, per
 * ten thousand */
#define ROF_MAX_DIFF (48)
#define ROF_FAR_PER_10K (10)

/** Pixels per thousand the shadow of the roof darkens, at least */
#define ROF_SHADOW_PERMILLE (20)

/** Worlds a test renders side by side */
#define ROF_WORLDS (3)

/** Shadow settings of the views: two cascades over 60 units */
static const ShadowSettings rof_shadows = {
    .enabled = true, .cascades = 2, .resolution = 1024, .max_distance = 60,
    .split_lambda = 0.7f, .depth_bias = 2, .normal_offset = 2, .filter_size = 3,
    .fade = 0.2f, .blend = 0.1f
};

/** Create a headless world that renders on the main thread, with an ambient light, a sun, a
 * ground plane in an unbounded fog around a point, a roof over half of it, above the image, and
 * a camera looking at the ground with a lit view that renders the fog and shadows.
 *
 * @param x X of the scene
 * @param z Z of the scene
 * @param roof Whether the roof is there
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* rof_world(
    float x,
    float z,
    bool roof,
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.1f});

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.3f, 0.5f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {x, -0.25f, z});
    ecs_set(world, ground, Box, {40, 0.5f, 40});
    ecs_set(world, ground, Rgb, {40, 40, 40});
    ecs_set(world, ground, PbrMaterial, {.roughness = 1});

    if (roof) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {x - 4, 12, z});
        ecs_set(world, e, Box, {8, 0.5f, 30});
    }

    ecs_entity_t fog = ecs_new(world);
    ecs_set(world, fog, Position, {x, 0, z});
    ecs_add(world, fog, FogVolume);
    FogVolume *f = ecs_get_mut(world, fog, FogVolume);
    f->shape = FogVolumeUnbounded;
    f->density = 0.08f;
    f->asymmetry = 0;
    f->ambient_inscatter = 0;
    ecs_modified(world, fog, FogVolume);

    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {x, 3, z + 14});
    ecs_set(world, *camera, Rotation, {-0.2f, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = *camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->lighting.enabled = true;
    v->lighting.fog_volumes = true;
    v->lighting.fog_distance = 40;
    v->shadows = rof_shadows;
    ecs_modified(world, view, View);

    return world;
}

/** Draw a world relative to a render origin that follows its camera, without shifts of the
 * world origin.
 *
 * @param world The world
 */
static void rof_render_follow(
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
static unsigned char* rof_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ROF_SIZE, .height = ROF_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ROF_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ROF_SIZE);
    test_int(h, ROF_SIZE);
    ecs_os_free(path);

    return result;
}

/** Render the next frame of every world, each image replacing the last one of its world.
 *
 * @param worlds The worlds
 * @param count Worlds, at most ROF_WORLDS
 * @param px The last image of each world, NULL for none, receives the new ones
 */
static void rof_step(
    ecs_world_t **worlds,
    int32_t count,
    unsigned char **px)
{
    static const char *files[ROF_WORLDS] = { "rof0.png", "rof1.png", "rof2.png" };
    int32_t w;
    test_assert(count <= ROF_WORLDS);
    for (w = 0; w < count; w ++) {
        stbi_image_free(px[w]);
        px[w] = rof_frame(worlds[w], files[w]);
    }
}

/** Get the largest channel difference of a pixel of two images.
 *
 * @param a The first image
 * @param b The second image
 * @param i The pixel
 * @return The difference, 0 to 255
 */
static int32_t rof_pixel_diff(
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

/** Check that two images of the fog match: at most ROF_DIFF_PERMILLE of the pixels differ by
 * more than ROF_PIXEL_DIFF, at most ROF_FAR_PER_10K by more than ROF_MAX_DIFF.
 *
 * @param a The first image
 * @param b The second image
 */
static void rof_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, far = 0, i;
    for (i = 0; i < ROF_SIZE * ROF_SIZE; i ++) {
        int32_t d = rof_pixel_diff(a, b, i);
        worst = d > worst ? d : worst;
        differ += d > ROF_PIXEL_DIFF;
        far += d > ROF_MAX_DIFF;
    }

    if (far * 10000 > ROF_FAR_PER_10K * ROF_SIZE * ROF_SIZE ||
        differ * 1000 > ROF_DIFF_PERMILLE * ROF_SIZE * ROF_SIZE)
    {
        printf("worst channel difference %d, %d pixels differ, %d far\n", worst, differ, far);
    }

    test_assert(far * 10000 <= ROF_FAR_PER_10K * ROF_SIZE * ROF_SIZE);
    test_assert(differ * 1000 <= ROF_DIFF_PERMILLE * ROF_SIZE * ROF_SIZE);
}

/** Check that the shadow of the roof shows in the fog: the image with the roof is darker than
 * the one without at ROF_SHADOW_PERMILLE of the pixels at least.
 *
 * @param shaded The image with the roof
 * @param open The image without it
 */
static void rof_expect_shadow(
    const unsigned char *shaded,
    const unsigned char *open)
{
    int32_t dark = 0, i;
    for (i = 0; i < ROF_SIZE * ROF_SIZE; i ++) {
        dark += (int)open[i * 4 + 1] - (int)shaded[i * 4 + 1] > 20;
    }

    if (dark * 1000 < ROF_SHADOW_PERMILLE * ROF_SIZE * ROF_SIZE) {
        printf("%d pixels darkened by the shadow\n", dark);
    }

    test_assert(dark * 1000 >= ROF_SHADOW_PERMILLE * ROF_SIZE * ROF_SIZE);
}

/** Free the images and the worlds of a test.
 *
 * @param worlds The worlds
 * @param count Worlds
 * @param px The last image of each world
 */
static void rof_free(
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

void RenderOriginFog_far_same_image(void) {
    // render/world_origin
    /* A roof shades the sun's light in an unbounded fog a million units from the local origin,
     * the fog lit relative to a render origin that follows the camera: the image matches the
     * same scene at the local origin, and the shadow of the roof shows in the fog. */
    ecs_entity_t cameras[ROF_WORLDS];
    ecs_world_t *worlds[ROF_WORLDS] = {
        rof_world(0, 0, true, &cameras[0]),
        rof_world(ROF_FAR_X, ROF_FAR_Z, true, &cameras[1]),
        rof_world(0, 0, false, &cameras[2])
    };
    unsigned char *px[ROF_WORLDS] = {0};
    rof_render_follow(worlds[1]);
    for (int32_t i = 0; i < ROF_FRAMES; i ++) {
        rof_step(worlds, ROF_WORLDS, px);
    }

    rof_expect_shadow(px[0], px[2]);
    rof_expect_same(px[0], px[1]);
    rof_free(worlds, ROF_WORLDS, px);
}

void RenderOriginFog_far_moving_no_swim(void) {
    // render/world_origin
    /* The camera moves by less than a texel of the first cascade every frame, a hundred
     * thousand units from the local origin, the render origin following it: every frame
     * matches the same moves at the local origin, so the shafts in the fog stay where the
     * texels of the local frame put them instead of moving with the camera. */
    ecs_entity_t cameras[2];
    ecs_world_t *worlds[2] = {
        rof_world(0, 0, true, &cameras[0]),
        rof_world(ROF_MOVE_X, ROF_MOVE_Z, true, &cameras[1])
    };
    unsigned char *px[2] = {0};
    float origin[2][2] = { {0, 0}, {ROF_MOVE_X, ROF_MOVE_Z} };
    int32_t w;
    rof_render_follow(worlds[1]);
    for (int32_t i = 0; i < ROF_MOVE_FRAMES; i ++) {
        float d = ROF_STEP * (float)i;
        for (w = 0; w < 2; w ++) {
            ecs_set(worlds[w], cameras[w], Position,
                {origin[w][0] + d, 3, origin[w][1] + 14 - d});
        }

        rof_step(worlds, 2, px);
        rof_expect_same(px[0], px[1]);
    }

    rof_free(worlds, 2, px);
}
