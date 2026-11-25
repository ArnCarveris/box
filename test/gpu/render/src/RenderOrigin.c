/**
 * @file RenderOrigin.c
 * @brief Tests that the render origin does not change the rendered image, with the GPU backend:
 * two worlds render the same scene for the same frames with another render origin, or one of
 * them far from the origin of the world, and their last images match within a small tolerance.
 *
 * The scene is a white floor with boxes around x = 3000 under a sun with shadows, seen from a
 * camera above and behind them into a 96x64 image. The camera is the focus the render origin
 * follows, unless a WorldFocus entity holds it elsewhere.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width in pixels of the images */
#define ORIGIN_WIDTH (96)

/** Height in pixels of the images */
#define ORIGIN_HEIGHT (64)

/** Seconds of a frame */
#define ORIGIN_DELTA (1.0f / 60.0f)

/** X of the content */
#define ORIGIN_X (3000.0f)

/** Largest channel difference of a pixel between the images, edges of shapes and shadows
 * rasterize differently relative to another render origin */
#define ORIGIN_MAX_DIFF (64)

/** Pixels that may differ by more than 4 in a channel, per thousand */
#define ORIGIN_DIFF_PERMILLE (10)

/** Create a headless world that renders on the main thread with the scene: the floor, three
 * boxes, a sun with shadows and a camera with a view, all moved by an offset.
 *
 * @param offset Added to the positions of the scene, 0 for none
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* origin_scene(
    double offset,
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.3f});

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {ORIGIN_X + offset, -0.1f, offset});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    for (int32_t i = 0; i < 3; i ++) {
        ecs_entity_t box = ecs_new(world);
        ecs_set(world, box, Position, {ORIGIN_X - 4 + 4 * i + offset, 1, -2 * i + offset});
        ecs_set(world, box, Rotation, {0, 0.3f * (float)i, 0});
        ecs_set(world, box, Box, {1.5f, 2, 1.5f});
        ecs_set(world, box, Rgb, {(uint8_t)(80 * i + 60), 120, 200});
    }

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {ORIGIN_X + offset, 6, 12 + offset});
    ecs_set(world, *camera, Rotation, {-0.45f, 0, 0});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = *camera;
    v->clear = (ClearSettings){.enabled = true, .color = {30, 40, 60, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);

    return world;
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* origin_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = ORIGIN_WIDTH, .height = ORIGIN_HEIGHT,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, ORIGIN_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, ORIGIN_WIDTH);
    test_int(h, ORIGIN_HEIGHT);
    ecs_os_free(path);

    return result;
}

/** Check that two images match within the tolerance of another render origin: no channel differs
 * by more than ORIGIN_MAX_DIFF and few pixels by more than 4.
 *
 * @param a The first image
 * @param b The second image
 */
static void origin_expect_same(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, differ = 0, i, c;
    for (i = 0; i < ORIGIN_WIDTH * ORIGIN_HEIGHT; i ++) {
        int32_t d = 0;
        for (c = 0; c < 3; c ++) {
            int32_t delta = abs((int)a[i * 4 + c] - (int)b[i * 4 + c]);
            d = delta > d ? delta : d;
        }

        worst = d > worst ? d : worst;
        differ += d > 4;
    }

    if (worst > ORIGIN_MAX_DIFF || differ * 1000 > ORIGIN_DIFF_PERMILLE * ORIGIN_WIDTH *
        ORIGIN_HEIGHT)
    {
        printf("worst channel difference %d, %d pixels differ\n", worst, differ);
    }

    test_assert(worst <= ORIGIN_MAX_DIFF);
    test_assert(differ * 1000 <= ORIGIN_DIFF_PERMILLE * ORIGIN_WIDTH * ORIGIN_HEIGHT);
}

/** Check that an image is not a plain clear: the scene was drawn.
 *
 * @param px The image
 */
static void origin_expect_content(
    const unsigned char *px)
{
    int32_t lit = 0, i;
    for (i = 0; i < ORIGIN_WIDTH * ORIGIN_HEIGHT; i ++) {
        lit += px[i * 4] > 100 && px[i * 4 + 1] > 100;
    }

    test_assert(lit > ORIGIN_WIDTH * ORIGIN_HEIGHT / 8);
}

/** Check that an image shows the red particles.
 *
 * @param px The image
 */
static void origin_expect_particles(
    const unsigned char *px)
{
    int32_t red = 0, i;
    for (i = 0; i < ORIGIN_WIDTH * ORIGIN_HEIGHT; i ++) {
        red += px[i * 4] > 150 && px[i * 4 + 1] < 100 && px[i * 4 + 2] < 100;
    }

    test_assert(red > 8);
}

void RenderOrigin_follows_camera(void) {
    // render/render_origin
    ecs_entity_t camera;
    double origin[3];
    ecs_world_t *world = origin_scene(0, &camera);
    stbi_image_free(origin_frame(world, "a.png"));
    box_render_origin(world, origin);
    test_assert(origin[0] == 3072 && origin[1] == 0 && origin[2] == 0);
    test_flt(ecs_get(world, camera, Position)->x, ORIGIN_X);

    ecs_set(world, camera, Position, {ORIGIN_X + 1000, 6, 12});
    stbi_image_free(origin_frame(world, "a.png"));
    box_render_origin(world, origin);
    test_assert(origin[0] == 3072);

    ecs_set(world, camera, Position, {ORIGIN_X + 1200, 6, -1500});
    stbi_image_free(origin_frame(world, "a.png"));
    box_render_origin(world, origin);
    test_assert(origin[0] == 4096 && origin[2] == -1024);
    test_flt(ecs_get(world, camera, Position)->x, ORIGIN_X + 1200);
    ecs_fini(world);
}

void RenderOrigin_origin_same_image(void) {
    // render/render_origin
    ecs_entity_t camera_a, camera_b;
    double origin[3];
    ecs_world_t *a = origin_scene(0, &camera_a);
    ecs_world_t *b = origin_scene(0, &camera_b);
    ecs_entity_t focus = ecs_new(a);
    ecs_set(a, focus, Position, {0, 0, 0});
    ecs_add(a, focus, WorldFocus);
    unsigned char *pa = NULL, *pb = NULL;
    for (int32_t i = 0; i < 3; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
    }

    box_render_origin(a, origin);
    test_assert(origin[0] == 0 && origin[2] == 0);
    box_render_origin(b, origin);
    test_assert(origin[0] == 3072 && origin[2] == 0);
    origin_expect_content(pa);
    origin_expect_same(pa, pb);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

void RenderOrigin_move_same_image(void) {
    // render/render_origin
    ecs_entity_t camera_a, camera_b;
    double origin[3];
    ecs_world_t *a = origin_scene(0, &camera_a);
    ecs_world_t *b = origin_scene(0, &camera_b);
    ecs_entity_t focus = ecs_new(b);
    ecs_set(b, focus, Position, {ORIGIN_X, 0, 0});
    ecs_add(b, focus, WorldFocus);
    for (int32_t i = 0; i < 3; i ++) {
        stbi_image_free(origin_frame(a, "a.png"));
        stbi_image_free(origin_frame(b, "b.png"));
    }

    ecs_set(b, focus, Position, {ORIGIN_X + 5000, 0, -3000});
    unsigned char *pa = NULL, *pb = NULL;
    for (int32_t i = 0; i < 3; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
    }

    box_render_origin(b, origin);
    test_assert(origin[0] == 8192 && origin[2] == -3072);
    test_flt(ecs_get(b, camera_b, Position)->x, ORIGIN_X);
    origin_expect_content(pa);
    origin_expect_same(pa, pb);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

void RenderOrigin_far_same_image(void) {
    // render/render_origin
    ecs_entity_t camera_a, camera_b;
    double origin[3];
    ecs_world_t *a = origin_scene(0, &camera_a);
    ecs_world_t *b = origin_scene(1048576.0 * 4, &camera_b);
    unsigned char *pa = NULL, *pb = NULL;
    for (int32_t i = 0; i < 3; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
    }

    box_render_origin(b, origin);
    test_assert(origin[0] == 3072 + 1048576.0 * 4 && origin[2] == 1048576.0 * 4);
    test_assert(ecs_get(b, camera_b, Position)->z == 12 + 1048576.0 * 4);
    origin_expect_content(pa);
    origin_expect_same(pa, pb);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

void RenderOrigin_particles_same_image(void) {
    // render/render_origin
    ecs_entity_t camera_a, camera_b;
    ecs_world_t *a = origin_scene(0, &camera_a);
    ecs_world_t *b = origin_scene(0, &camera_b);
    ecs_world_t *worlds[2] = { a, b };
    ecs_entity_t focus = ecs_new(b);
    ecs_set(b, focus, Position, {ORIGIN_X, 0, 0});
    ecs_add(b, focus, WorldFocus);
    for (int32_t w = 0; w < 2; w ++) {
        ecs_entity_t emitter = ecs_new(worlds[w]);
        ecs_set(worlds[w], emitter, Position, {ORIGIN_X, 1, 4});
        ecs_add(worlds[w], emitter, ParticleEmitter);
        ParticleEmitter *p = ecs_get_mut(worlds[w], emitter, ParticleEmitter);
        p->max = 500;
        p->rate = 200;
        p->lifetime = (ParticleRange){1, 1};
        p->speed = (ParticleRange){2, 2};
        p->size = (ParticleRange){0.5f, 0.5f};
        p->spread = 20;
        p->seed = 7;
        p->color = (rgb){255, 20, 20};
        ecs_modified(worlds[w], emitter, ParticleEmitter);
    }

    unsigned char *pa = NULL, *pb = NULL;
    for (int32_t i = 0; i < 24; i ++) {
        if (i == 12) {
            ecs_set(b, focus, Position, {ORIGIN_X + 2816, 0, 0});
        }

        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
    }

    origin_expect_content(pa);
    origin_expect_particles(pb);
    origin_expect_same(pa, pb);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}
