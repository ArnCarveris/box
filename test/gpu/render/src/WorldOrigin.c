/**
 * @file WorldOrigin.c
 * @brief Tests that a shift of the world origin does not change the rendered image, with the
 * GPU backend: two worlds render the same scene far from the origin for the same frames, one of
 * them shifting, and their last images match within a small tolerance.
 *
 * The scene is a white floor with boxes around x = 3000 under a sun with shadows, seen from a
 * camera above and behind them into a 96x64 image.
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
 * rasterize differently far from the origin */
#define ORIGIN_MAX_DIFF (64)

/** Pixels that may differ by more than 4 in a channel, per thousand */
#define ORIGIN_DIFF_PERMILLE (10)

/** Largest channel difference between two images of one world that place the same geometry,
 * with and without a render origin */
#define ORIGIN_CLOSE_DIFF (4)

/** Create a headless world that renders on the main thread with the scene: the floor, three
 * boxes, a sun with shadows and a camera with a view.
 *
 * @param camera Receives the camera
 * @param cell Whether the floor and boxes are children of a WorldCell root at cell 1 0 0
 * @param root Receives the WorldCell root, 0 without one; NULL to ignore it
 * @return The world
 */
static ecs_world_t* origin_scene_in(
    ecs_entity_t *camera,
    bool cell,
    ecs_entity_t *root)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.3f});

    ecs_entity_t parent = 0;
    float x = ORIGIN_X;
    if (cell) {
        parent = ecs_new(world);
        ecs_set(world, parent, WorldCell, {1, 0, 0});
        x -= (float)BOX_WORLD_CELL_SIZE;
    }

    if (root) {
        *root = parent;
    }

    ecs_entity_t floor = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, floor, Position, {x, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    for (int32_t i = 0; i < 3; i ++) {
        ecs_entity_t box = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
        ecs_set(world, box, Position, {x - 4 + 4 * (float)i, 1, -2 * (float)i});
        ecs_set(world, box, Rotation, {0, 0.3f * (float)i, 0});
        ecs_set(world, box, Box, {1.5f, 2, 1.5f});
        ecs_set(world, box, Rgb, {(uint8_t)(80 * i + 60), 120, 200});
    }

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    *camera = ecs_new(world);
    ecs_set(world, *camera, Position, {ORIGIN_X, 6, 12});
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

/** Create the scene of the tests, its content at the absolute X of ORIGIN_X.
 *
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* origin_scene(
    ecs_entity_t *camera)
{
    return origin_scene_in(camera, false, NULL);
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

/** Check that two images match within the tolerance of a shift: no channel differs by more
 * than ORIGIN_MAX_DIFF and few pixels by more than 4.
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

void WorldOrigin_shift_same_image(void) {
    // render/world_origin
    ecs_entity_t camera_a, camera_b;
    ecs_world_t *a = origin_scene(&camera_a);
    ecs_world_t *b = origin_scene(&camera_b);
    for (int32_t i = 0; i < 3; i ++) {
        stbi_image_free(origin_frame(a, "a.png"));
        stbi_image_free(origin_frame(b, "b.png"));
    }

    box_world_origin_shift(b, 0, (double[3]){ 2816, 0, -256 });
    test_flt(ecs_get(b, camera_b, Position)->x, ORIGIN_X - 2816);
    test_flt(ecs_get(b, camera_b, Position)->z, 268);
    unsigned char *pa = NULL, *pb = NULL;
    for (int32_t i = 0; i < 3; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
    }

    origin_expect_content(pa);
    origin_expect_same(pa, pb);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

void WorldOrigin_rebase_same_image(void) {
    // render/world_origin
    ecs_entity_t camera_a, camera_b;
    ecs_world_t *a = origin_scene(&camera_a);
    ecs_world_t *b = origin_scene(&camera_b);
    ecs_singleton_set(b, WorldOrigin, {.threshold = 2048, .step = 256, .enabled = true});
    ecs_add(a, camera_a, WorldFocus);
    ecs_add(b, camera_b, WorldFocus);
    ecs_set(a, camera_a, Velocity, {0, 0, -6});
    ecs_set(b, camera_b, Velocity, {0, 0, -6});
    unsigned char *pa = NULL, *pb = NULL;
    for (int32_t i = 0; i < 6; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
    }

    test_int(ecs_singleton_get(b, WorldOriginStatus)->shifts, 1);
    test_assert(ecs_singleton_get(b, WorldOrigin)->x == 3072);
    origin_expect_content(pa);
    origin_expect_same(pa, pb);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

void WorldOrigin_particles_same_image(void) {
    // render/world_origin
    ecs_entity_t camera_a, camera_b;
    ecs_world_t *a = origin_scene(&camera_a);
    ecs_world_t *b = origin_scene(&camera_b);
    ecs_world_t *worlds[2] = { a, b };
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
            box_world_origin_shift(b, 0, (double[3]){ 2816, 0, 0 });
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

void WorldOrigin_cell_same_image(void) {
    // render/world_origin
    ecs_entity_t camera_a, camera_b;
    ecs_world_t *a = origin_scene(&camera_a);
    ecs_world_t *b = origin_scene_in(&camera_b, true, NULL);
    unsigned char *pa = NULL, *pb = NULL;
    for (int32_t i = 0; i < 3; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
    }

    origin_expect_content(pa);
    origin_expect_same(pa, pb);

    box_world_origin_shift(b, 0, (double[3]){ 2816, 0, -256 });
    test_int(ecs_singleton_get(b, WorldOriginStatus)->written, 3);
    for (int32_t i = 0; i < 3; i ++) {
        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
    }

    origin_expect_same(pa, pb);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}

/** Create a world with a moving ship: a mover root at the absolute X of ORIGIN_X with three
 * boxes and the camera as children, moving along -Z at 6 units a second over a white floor
 * with a row of pillars, under a sun with shadows.
 *
 * @param ship Receives the ship
 * @return The world
 */
static ecs_world_t* origin_ship_scene(
    ecs_entity_t *ship)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.3f});

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {ORIGIN_X, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    for (int32_t i = 0; i < 6; i ++) {
        ecs_entity_t pillar = ecs_new(world);
        ecs_set(world, pillar, Position, {ORIGIN_X + 6, 1, 4 - 5 * (float)i});
        ecs_set(world, pillar, Box, {0.6f, 2, 0.6f});
        ecs_set(world, pillar, Rgb, {220, 200, 60});
    }

    *ship = ecs_new(world);
    ecs_add(world, *ship, DynamicTransform);
    ecs_set(world, *ship, Position, {ORIGIN_X, 0, 0});
    ecs_set(world, *ship, Velocity, {0, 0, -6});
    for (int32_t i = 0; i < 3; i ++) {
        ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, *ship);
        ecs_set(world, box, Position, {-4 + 4 * (float)i, 1, -2 * (float)i});
        ecs_set(world, box, Rotation, {0, 0.3f * (float)i, 0});
        ecs_set(world, box, Box, {1.5f, 2, 1.5f});
        ecs_set(world, box, Rgb, {(uint8_t)(80 * i + 60), 120, 200});
    }

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, *ship);
    ecs_set(world, camera, Position, {0, 6, 12});
    ecs_set(world, camera, Rotation, {-0.45f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {30, 40, 60, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);

    return world;
}

/** Make a world draw relative to a render origin that follows an entity, with a world origin
 * that shifts by whole meters every meter its focus moves, so it shifts while the ship moves.
 *
 * @param world The world
 * @param follow The entity the render origin follows, 0 for the camera
 * @param focus The focus of the world origin
 */
static void origin_render_follow(
    ecs_world_t *world,
    ecs_entity_t follow,
    ecs_entity_t focus)
{
    ecs_singleton_set(world, WorldOrigin, {.threshold = 1, .step = 1, .enabled = true,
        .render = true, .render_follow = follow});
    ecs_add(world, focus, WorldFocus);
}

/** Check that two images match to within ORIGIN_CLOSE_DIFF in every channel: they place the same
 * geometry, only rounded differently.
 *
 * @param a The first image
 * @param b The second image
 */
static void origin_expect_close(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, i;
    for (i = 0; i < ORIGIN_WIDTH * ORIGIN_HEIGHT * 4; i ++) {
        int32_t d = abs((int)a[i] - (int)b[i]);
        worst = d > worst ? d : worst;
    }

    if (worst > ORIGIN_CLOSE_DIFF) {
        printf("worst channel difference %d\n", worst);
    }

    test_assert(worst <= ORIGIN_CLOSE_DIFF);
}

void WorldOrigin_render_follow_same_image(void) {
    // render/world_origin
    /* The render origin follows the ship, or the camera on it, every frame while the world
     * origin shifts by whole meters under them: every frame, those of the shifts included,
     * matches the same world drawn without a render origin. */
    ecs_entity_t ships[3];
    ecs_world_t *worlds[3];
    const char *files[3] = { "plain.png", "ship.png", "camera.png" };
    for (int32_t w = 0; w < 3; w ++) {
        worlds[w] = origin_ship_scene(&ships[w]);
    }

    origin_render_follow(worlds[0], 0, ships[0]);
    WorldOrigin *o = ecs_singleton_get_mut(worlds[0], WorldOrigin);
    o->render = false;
    ecs_singleton_modified(worlds[0], WorldOrigin);
    origin_render_follow(worlds[1], ships[1], ships[1]);
    origin_render_follow(worlds[2], 0, ships[2]);
    unsigned char *px[3] = {0};
    for (int32_t i = 0; i < 24; i ++) {
        for (int32_t w = 0; w < 3; w ++) {
            stbi_image_free(px[w]);
            px[w] = origin_frame(worlds[w], files[w]);
        }

        origin_expect_close(px[0], px[1]);
        origin_expect_close(px[0], px[2]);
    }

    origin_expect_content(px[0]);
    for (int32_t w = 0; w < 3; w ++) {
        test_assert(ecs_singleton_get(worlds[w], WorldOriginStatus)->shifts >= 3);
        test_assert(ecs_singleton_get(worlds[w], WorldOrigin)->x == 3000);
    }

    double z = ecs_singleton_get(worlds[1], WorldOrigin)->z;
    test_assert(z + (double)ecs_get(worlds[1], ships[1], Position)->z < -2.0);
    for (int32_t w = 0; w < 3; w ++) {
        stbi_image_free(px[w]);
        ecs_fini(worlds[w]);
    }
}

void WorldOrigin_render_follow_cell_same_image(void) {
    // render/world_origin
    /* The render origin follows a WorldCell root at its exact cell, across a shift. */
    ecs_entity_t camera_a, camera_b, root;
    ecs_world_t *a = origin_scene(&camera_a);
    ecs_world_t *b = origin_scene_in(&camera_b, true, &root);
    ecs_singleton_set(b, WorldOrigin, {.enabled = false, .render = true, .render_follow = root});
    unsigned char *pa = NULL, *pb = NULL;
    for (int32_t i = 0; i < 6; i ++) {
        if (i == 3) {
            box_world_origin_shift(b, 0, (double[3]){ 2816, 0, -256 });
        }

        stbi_image_free(pa);
        stbi_image_free(pb);
        pa = origin_frame(a, "a.png");
        pb = origin_frame(b, "b.png");
        if (i > 0) {
            origin_expect_same(pa, pb);
        }
    }

    origin_expect_content(pa);
    stbi_image_free(pa);
    stbi_image_free(pb);
    ecs_fini(a);
    ecs_fini(b);
}
