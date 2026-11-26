/**
 * @file RenderOriginFx.c
 * @brief Tests that effects drawn relative to the render origin look as they did without one,
 * with the GPU backend: worlds render the same scene far from the origin for the same frames,
 * one of them with WorldOrigin.render, and their images match within a small tolerance.
 *
 * The scenes are seen into a 96x64 image around the absolute X of FX_X.
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
#define FX_WIDTH (96)

/** Height in pixels of the images */
#define FX_HEIGHT (64)

/** Seconds of a frame */
#define FX_DELTA (1.0f / 60.0f)

/** X of the content */
#define FX_X (3000.0f)

/** X of the far content of the soft particle case, whole units so the f32 positions of the
 * scene are exact */
#define FX_FAR_X (1000000.0f)

/** Largest channel difference between two images that place the same geometry, rounded
 * differently */
#define FX_CLOSE_DIFF (4)

/** Pixels of two images of grass that may differ by more than FX_CLOSE_DIFF, a hundredth of
 * them: those a blade edge passes within the rounding of its place */
#define FX_EDGE_PIXELS (FX_WIDTH * FX_HEIGHT / 100)

/** Largest difference of the means of the channels of two images of grass */
#define FX_EDGE_MEAN (0.1)

/** Worlds a test renders side by side at most */
#define FX_WORLDS (4)

/** Create a headless world that renders on the main thread, with an ambient light and a sun.
 *
 * @return The world
 */
static ecs_world_t* fx_world(void)
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

    return world;
}

/** Create a camera above and behind a point, looking down at it, and a view of it.
 *
 * @param world The world
 * @param parent Parent of the camera, 0 for none
 * @param x X of the camera
 * @return The camera
 */
static ecs_entity_t fx_camera(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x)
{
    ecs_entity_t camera = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, camera, Position, {x, 6, 12});
    ecs_set(world, camera, Rotation, {-0.45f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 200});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {30, 40, 60, 255}};
    ecs_modified(world, view, View);

    return camera;
}

/** Create an emitter of red particles with a fixed seed.
 *
 * @param world The world
 * @param parent Parent of the emitter, 0 for none
 * @param x X of the emitter
 * @param ride ParticleEmitter.ride
 * @return The emitter
 */
static ecs_entity_t fx_emitter(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    bool ride)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, 1, 4});
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->max = 500;
    p->rate = 200;
    p->lifetime = (ParticleRange){1, 1};
    p->speed = (ParticleRange){2, 2};
    p->size = (ParticleRange){0.5f, 0.5f};
    p->spread = 20;
    p->seed = 7;
    p->color = (rgb){255, 20, 20};
    p->ride = ride;
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Create a world of world space particles over a white floor at FX_X, seen by a camera.
 *
 * @return The world
 */
static ecs_world_t* fx_particle_scene(void)
{
    ecs_world_t *world = fx_world();
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {FX_X, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    fx_camera(world, 0, FX_X);
    fx_emitter(world, 0, FX_X, false);

    return world;
}

/** Create a world of soft particles kept in the space of their emitter just over a white floor
 * at a point, which they fade into as they near it, seen by a camera turned about Y so the X of
 * the point counts in the view depth.
 *
 * @param x X of the content
 * @return The world
 */
static ecs_world_t* fx_soft_scene(
    float x)
{
    ecs_world_t *world = fx_world();
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {x, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t camera = fx_camera(world, 0, x);
    ecs_set(world, camera, Rotation, {-0.45f, 0.3f, 0});
    ecs_entity_t e = fx_emitter(world, 0, x, false);
    ecs_set(world, e, Position, {x - 2, 0.25f, 4});
    ecs_get_mut(world, e, ParticleEmitter)->local = true;
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, ParticleAppearance, {.softness = 1});

    return world;
}

/** Create a world of a ship at FX_X: a mover with the camera and an emitter that rides it as
 * children, over the clear color only.
 *
 * @param ride ParticleEmitter.ride of the emitter
 * @param ship Receives the ship
 * @return The world
 */
static ecs_world_t* fx_ship_scene(
    bool ride,
    ecs_entity_t *ship)
{
    ecs_world_t *world = fx_world();
    *ship = ecs_new(world);
    ecs_add(world, *ship, DynamicTransform);
    ecs_set(world, *ship, Position, {FX_X, 0, 0});
    fx_camera(world, *ship, 0);
    fx_emitter(world, *ship, 0, ride);

    return world;
}

/** Turn on the shadows of every view of a world.
 *
 * @param world The world
 */
static void fx_shadows(
    ecs_world_t *world)
{
    ecs_entity_t views[FX_WORLDS];
    int32_t count = 0, i;
    ecs_iter_t it = ecs_each(world, View);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count && count < FX_WORLDS; i ++) {
            views[count ++] = it.entities[i];
        }
    }

    for (i = 0; i < count; i ++) {
        View *v = ecs_get_mut(world, views[i], View);
        v->shadows.enabled = true;
        ecs_modified(world, views[i], View);
    }
}

/** Create a world of a meadow of Grass on a brown floor at FX_X, in a wind with gusts, seen by
 * a camera, the blades casting shadows into the cascades of the view when it has them.
 *
 * @param shadows Whether the view has shadows
 * @param camera Receives the camera, NULL for none
 * @return The world
 */
static ecs_world_t* fx_grass_scene(
    bool shadows,
    ecs_entity_t *camera)
{
    ecs_world_t *world = fx_world();
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {FX_X, -0.1f, 0});
    ecs_set(world, floor, Box, {200, 0.2f, 200});
    ecs_set(world, floor, Rgb, {90, 60, 40});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    ecs_entity_t meadow = ecs_new(world);
    ecs_set(world, meadow, Position, {FX_X, 0, 0});
    Grass *g = ecs_ensure(world, meadow, Grass);
    g->density = 48;
    g->min_width = 0.05f;
    g->max_width = 0.09f;
    g->lod[0] = 8;
    g->lod[1] = 20;
    g->lod[2] = 40;
    g->seed = 3;
    g->shadow_distance = 12;
    ecs_modified(world, meadow, Grass);
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 8, .gust = 0.5f,
        .gust_scale = 10});

    ecs_entity_t c = fx_camera(world, 0, FX_X);
    if (shadows) {
        fx_shadows(world);
    }

    if (camera) {
        *camera = c;
    }

    return world;
}

/** Check that an image shows the grass: a share of its pixels green.
 *
 * @param px The image
 */
static void fx_expect_grass(
    const unsigned char *px)
{
    int32_t green = 0, i;
    for (i = 0; i < FX_WIDTH * FX_HEIGHT; i ++) {
        const unsigned char *p = &px[i * 4];
        green += p[1] > p[0] + 8 && p[1] > p[2] + 8;
    }

    test_assert(green * 4 > FX_WIDTH * FX_HEIGHT);
}

/** Check that two images of grass match: every pixel to within FX_CLOSE_DIFF in every channel
 * but at most FX_EDGE_PIXELS, where the edge of a blade about a pixel wide passes the center of
 * the pixel within the rounding of its place, and the mean of the channels to within
 * FX_EDGE_MEAN.
 *
 * @param a The first image
 * @param b The second image
 */
static void fx_expect_edges(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t edges = 0, i, c;
    double sum = 0;
    for (i = 0; i < FX_WIDTH * FX_HEIGHT; i ++) {
        int32_t worst = 0;
        for (c = 0; c < 3; c ++) {
            int32_t d = (int)a[i * 4 + c] - (int)b[i * 4 + c];
            worst = abs(d) > worst ? abs(d) : worst;
            sum += d;
        }

        edges += worst > FX_CLOSE_DIFF;
    }

    double mean = fabs(sum) / (FX_WIDTH * FX_HEIGHT * 3);
    if (edges > FX_EDGE_PIXELS || mean > FX_EDGE_MEAN) {
        printf("%d pixels differ, mean difference %.3f\n", edges, mean);
    }

    test_assert(edges <= FX_EDGE_PIXELS);
    test_assert(mean <= FX_EDGE_MEAN);
}

/** Draw a world relative to a render origin that follows an entity, without shifts of the
 * world origin.
 *
 * @param world The world
 * @param follow The entity the render origin follows, 0 for the camera
 */
static void fx_render_follow(
    ecs_world_t *world,
    ecs_entity_t follow)
{
    ecs_singleton_set(world, WorldOrigin, {.enabled = false, .render = true,
        .render_follow = follow});
}

/** Render the next frame into an image file of the temporary folder and load it.
 *
 * @param world The world
 * @param file Image file
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* fx_frame(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = FX_WIDTH, .height = FX_HEIGHT,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, FX_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *result = stbi_load(path, &w, &h, &n, 4);
    test_assert(result != NULL);
    test_int(w, FX_WIDTH);
    test_int(h, FX_HEIGHT);
    ecs_os_free(path);

    return result;
}

/** Render the next frame of every world, each image replacing the last one of its world.
 *
 * @param worlds The worlds
 * @param count Worlds, at most FX_WORLDS
 * @param px The last image of each world, NULL for none, receives the new ones
 */
static void fx_step(
    ecs_world_t **worlds,
    int32_t count,
    unsigned char **px)
{
    static const char *files[FX_WORLDS] = { "fx0.png", "fx1.png", "fx2.png", "fx3.png" };
    int32_t w;
    test_assert(count <= FX_WORLDS);
    for (w = 0; w < count; w ++) {
        stbi_image_free(px[w]);
        px[w] = fx_frame(worlds[w], files[w]);
    }
}

/** Get the largest channel difference between two images.
 *
 * @param a The first image
 * @param b The second image
 * @return The difference, 0 to 255
 */
static int32_t fx_worst(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = 0, i;
    for (i = 0; i < FX_WIDTH * FX_HEIGHT * 4; i ++) {
        int32_t d = abs((int)a[i] - (int)b[i]);
        worst = d > worst ? d : worst;
    }

    return worst;
}

/** Check that two images match to within FX_CLOSE_DIFF in every channel.
 *
 * @param a The first image
 * @param b The second image
 */
static void fx_expect_close(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t worst = fx_worst(a, b);
    if (worst > FX_CLOSE_DIFF) {
        printf("worst channel difference %d\n", worst);
    }

    test_assert(worst <= FX_CLOSE_DIFF);
}

/** Check that an image shows the red particles.
 *
 * @param px The image
 */
static void fx_expect_particles(
    const unsigned char *px)
{
    int32_t red = 0, i;
    for (i = 0; i < FX_WIDTH * FX_HEIGHT; i ++) {
        red += px[i * 4] > 150 && px[i * 4 + 1] < 100 && px[i * 4 + 2] < 100;
    }

    test_assert(red > 8);
}

/** Free the images and the worlds of a test.
 *
 * @param worlds The worlds
 * @param count Worlds
 * @param px The last image of each world
 */
static void fx_free(
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

void RenderOriginFx_particles_render_follow_same_image(void) {
    // render/world_origin
    /* World space particles at X 3000 drawn relative to a render origin that follows the
     * camera match those drawn without one, every frame. */
    ecs_world_t *worlds[2] = { fx_particle_scene(), fx_particle_scene() };
    unsigned char *px[2] = {0};
    fx_render_follow(worlds[1], 0);
    for (int32_t i = 0; i < 24; i ++) {
        fx_step(worlds, 2, px);
        fx_expect_close(px[0], px[1]);
    }

    fx_expect_particles(px[0]);
    fx_free(worlds, 2, px);
}

void RenderOriginFx_particles_shift_same_image(void) {
    // render/world_origin
    /* A shift of the world origin halfway moves the live world space particles with the rest
     * of the world, drawn relative to a render origin that follows the camera. */
    ecs_world_t *worlds[2] = { fx_particle_scene(), fx_particle_scene() };
    unsigned char *px[2] = {0};
    fx_render_follow(worlds[0], 0);
    fx_render_follow(worlds[1], 0);
    for (int32_t i = 0; i < 24; i ++) {
        if (i == 12) {
            box_world_origin_shift(worlds[1], 0, (double[3]){ 2816, 0, -256 });
        }

        fx_step(worlds, 2, px);
        fx_expect_close(px[0], px[1]);
    }

    test_int(ecs_singleton_get(worlds[1], WorldOriginStatus)->shifts, 1);
    fx_expect_particles(px[0]);
    fx_free(worlds, 2, px);
}

void RenderOriginFx_particles_ride_moves_with_mover(void) {
    // render/world_origin
    /* The camera rides a ship with an emitter that rides it too: the ship standing still, the
     * ship moving and turning, and the same drawn relative to a render origin that follows the
     * ship show the same particles; a world space emitter on the moving ship leaves them
     * behind. */
    ecs_entity_t ships[4];
    ecs_world_t *worlds[4];
    unsigned char *px[4] = {0};
    int32_t w;
    for (w = 0; w < 4; w ++) {
        worlds[w] = fx_ship_scene(w != 3, &ships[w]);
    }

    for (w = 1; w < 4; w ++) {
        ecs_set(worlds[w], ships[w], Velocity, {0, 0, -6});
    }

    fx_render_follow(worlds[2], ships[2]);
    int32_t behind = 0;
    for (int32_t i = 0; i < 30; i ++) {
        if (i == 12) {
            for (w = 1; w < 4; w ++) {
                ecs_set(worlds[w], ships[w], Rotation, {0, 0.8f, 0});
            }
        }

        fx_step(worlds, 4, px);
        fx_expect_close(px[0], px[1]);
        fx_expect_close(px[0], px[2]);
        int32_t d = fx_worst(px[0], px[3]);
        behind = d > behind ? d : behind;
    }

    fx_expect_particles(px[0]);
    test_assert(ecs_get(worlds[1], ships[1], Position)->z < -2.5f);
    test_assert(behind > 64);
    fx_free(worlds, 4, px);
}


void RenderOriginFx_grass_render_follow_same_image(void) {
    // render/world_origin
    /* A meadow of grass at X 3000 that bends in a gusty wind and casts shadows, drawn relative
     * to a render origin that follows the camera, matches the one drawn without it, every
     * frame. */
    ecs_world_t *worlds[2] = { fx_grass_scene(true, NULL), fx_grass_scene(true, NULL) };
    unsigned char *px[2] = {0};
    fx_render_follow(worlds[1], 0);
    for (int32_t i = 0; i < 16; i ++) {
        fx_step(worlds, 2, px);
        fx_expect_edges(px[0], px[1]);
    }

    fx_expect_grass(px[0]);
    fx_free(worlds, 2, px);
}

void RenderOriginFx_grass_shift_same_image(void) {
    // render/world_origin
    /* A shift of the world origin halfway, by no whole number of cells, leaves the blades,
     * their clumps and colors and the gusts of the wind where they were: the meadow matches
     * the one that never shifts, both drawn relative to a render origin that follows the
     * camera. The view has no shadows: the cascades snap to the texels of the local frame, so
     * their edges move by up to a texel across a shift. */
    ecs_world_t *worlds[2] = { fx_grass_scene(false, NULL), fx_grass_scene(false, NULL) };
    unsigned char *px[2] = {0};
    fx_render_follow(worlds[0], 0);
    fx_render_follow(worlds[1], 0);
    for (int32_t i = 0; i < 24; i ++) {
        if (i == 12) {
            box_world_origin_shift(worlds[1], 0, (double[3]){ 2816.375, 0, -255.625 });
        }

        fx_step(worlds, 2, px);
        fx_expect_edges(px[0], px[1]);
    }

    test_int(ecs_singleton_get(worlds[1], WorldOriginStatus)->shifts, 1);
    fx_expect_grass(px[0]);
    fx_free(worlds, 2, px);
}

void RenderOriginFx_grass_render_follow_moving_continuous(void) {
    // render/world_origin
    /* The camera flies over the meadow, the render origin following it and so moving every
     * frame: every frame matches the same flight drawn without a render origin. */
    ecs_entity_t cameras[2];
    ecs_world_t *worlds[2] = {
        fx_grass_scene(true, &cameras[0]), fx_grass_scene(true, &cameras[1])
    };
    unsigned char *px[2] = {0};
    fx_render_follow(worlds[1], 0);
    for (int32_t w = 0; w < 2; w ++) {
        ecs_set(worlds[w], cameras[w], Velocity, {7, 0, -5});
    }

    for (int32_t i = 0; i < 30; i ++) {
        fx_step(worlds, 2, px);
        fx_expect_edges(px[0], px[1]);
    }

    test_assert(ecs_get(worlds[1], cameras[1], Position)->x > FX_X + 3.0f);
    fx_expect_grass(px[0]);
    fx_free(worlds, 2, px);
}

void RenderOriginFx_particles_soft_far_same_image(void) {
    // render/world_origin
    /* Soft particles kept in the space of their emitter a million units from the local origin,
     * fading into the floor they rise from, drawn relative to a render origin that follows the
     * camera: the depth they fade by keeps its precision, and the image matches the same scene
     * near the local origin, every frame. */
    ecs_world_t *worlds[2] = { fx_soft_scene(0), fx_soft_scene(FX_FAR_X) };
    unsigned char *px[2] = {0};
    fx_render_follow(worlds[1], 0);
    for (int32_t i = 0; i < 16; i ++) {
        fx_step(worlds, 2, px);
        fx_expect_close(px[0], px[1]);
    }

    fx_expect_particles(px[0]);
    fx_free(worlds, 2, px);
}
