/**
 * @file Effects.c
 * @brief The particle effects of the bunny hopper scene as they render: dust at the bunny's feet
 * after a landing and sparkles brighter than the sky.
 *
 * Every test renders the same frames twice, once with the effects and once with the effect
 * templates of BunnyGame cleared, so the pixels that differ are the particles.
 */

#include <bunnyhopper_game.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Path of the game scene in the repository */
#define FX_SCENE "etc/bunnyhopper/bunnyhopper.flecs"

/** Path of this file in the repository, which locates the repository root */
#define FX_SOURCE "test/gpu/bunnyhopper/src/"

/** Size of the window of the scene */
#define WINDOW_WIDTH (1280)
#define WINDOW_HEIGHT (1024)

/** Fixed delta of a frame */
#define GAME_DT (1.0f / 60.0f)

/** Channel difference above which a pixel shows a particle */
#define FX_DIFF (24)

/** X below which carrots are deleted and counted */
#define DESPAWN_X (-8.0f)

/** A capture: RGBA pixels */
typedef struct image_t {
    unsigned char *px; /**< The pixels, owned */
    int w;             /**< Width */
    int h;             /**< Height */
} image_t;

/** Get the path of a file of the repository.
 *
 * @param file Path of the file in the repository
 * @return The absolute path, owned by the caller
 */
static char* repo_path(
    const char *file)
{
    const char *source = strstr(__FILE__, FX_SOURCE);
    int32_t root = source ? (int32_t)(source - __FILE__) : 0;

    return flecs_asprintf("%.*s%s", root, __FILE__, file);
}

/** Step the world by frames of the fixed delta.
 *
 * @param world The world
 * @param frames Number of frames
 */
static void step(
    ecs_world_t *world,
    int32_t frames)
{
    for (int32_t i = 0; i < frames; i ++) {
        ecs_progress(world, GAME_DT);
    }
}

/** Load the game scene headless with its Surface, stop new carrots and run its first frames.
 *
 * @param effects Whether to keep the effect templates of BunnyGame, else they are cleared
 * @return The world
 */
static ecs_world_t* fx_new(
    bool effects)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, Bunnyhopper);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = repo_path(FX_SCENE);
    ecs_entity_t script = ecs_script(world, { .filename = path });
    ecs_os_free(path);
    test_assert(script != 0);
    const EcsScript *s = ecs_get(world, script, EcsScript);
    test_assert(s != NULL);
    test_assert(s->error == NULL);

    BunnyGame *g = ecs_singleton_get_mut(world, BunnyGame);
    test_assert(g->dust != 0);
    test_assert(g->sparkle != 0);
    g->next_spawn_distance = 1e9f;
    if (!effects) {
        g->dust = 0;
        g->dust_high = 0;
        g->hit = 0;
        g->sparkle = 0;
    }

    ecs_singleton_modified(world, BunnyGame);
    step(world, 2);

    return world;
}

/** Get the bunny.
 *
 * @param world The world
 * @return The player entity
 */
static ecs_entity_t player(
    ecs_world_t *world)
{
    const BunnyGame *g = ecs_singleton_get(world, BunnyGame);
    test_assert(g != NULL);
    test_assert(g->player != 0);

    return g->player;
}

/** Capture the window in the next frames and load the capture.
 *
 * @param world The world
 * @param name File name of the capture in the temporary folder
 * @return The capture
 */
static image_t capture(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t window = ecs_lookup(world, "window");
    test_assert(window != 0);
    Surface *s = ecs_get_mut(world, window, Surface);
    ecs_os_strset(&s->write_to_file, name);
    ecs_modified(world, window, Surface);
    step(world, 2);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    image_t result = {0};
    int n = 0;
    result.px = stbi_load(path, &result.w, &result.h, &n, 4);
    ecs_os_free(path);
    test_assert(result.px != NULL);
    test_int(result.w, WINDOW_WIDTH);
    test_int(result.h, WINDOW_HEIGHT);

    return result;
}

/** Get the luminance of a pixel of display encoded values.
 *
 * @param c The pixel
 * @return Its luminance, 0 to 255
 */
static float luminance(
    const unsigned char *c)
{
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

/** Check whether a pixel differs between two captures.
 *
 * @param a A capture
 * @param b The other capture
 * @param i Index of the pixel
 * @return Whether a channel differs by more than FX_DIFF
 */
static bool differs(
    const image_t *a,
    const image_t *b,
    int32_t i)
{
    bool result = false;
    for (int32_t c = 0; c < 3; c ++) {
        result |= abs((int)a->px[i * 4 + c] - (int)b->px[i * 4 + c]) > FX_DIFF;
    }

    return result;
}

/** Project a world point onto the window with the camera of the scene.
 *
 * @param world The world
 * @param x X of the point
 * @param y Y of the point
 * @param out Receives the pixel, x right and y down
 */
static void project(
    ecs_world_t *world,
    float x,
    float y,
    float out[2])
{
    ecs_entity_t camera = ecs_lookup(world, "camera");
    test_assert(camera != 0);
    const Camera *c = ecs_get(world, camera, Camera);
    const WorldTransform *t = ecs_get(world, camera, WorldTransform);
    test_assert(c != NULL);
    test_assert(t != NULL);

    mat4 transform, view, proj, view_proj;
    vec4 point = { x, y, 0.0f, 1.0f }, clip;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, t->m), transform);
    glm_mat4_inv(transform, view);
    box_camera_projection(c, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, proj);
    glm_mat4_mul(proj, view, view_proj);
    glm_mat4_mulv(view_proj, point, clip);
    test_assert(clip[3] > 0.0f);
    out[0] = (clip[0] / clip[3] * 0.5f + 0.5f) * (float)WINDOW_WIDTH;
    out[1] = (0.5f - clip[1] / clip[3] * 0.5f) * (float)WINDOW_HEIGHT;
}

/** Land the bunny in the next frame, hard, and run frames after the landing.
 *
 * @param world The world
 * @param frames Frames after the landing
 */
static void land(
    ecs_world_t *world,
    int32_t frames)
{
    ecs_entity_t p = player(world);
    ecs_set(world, p, Position, { -7.0f, 0.3f, 0.0f });
    ecs_set(world, p, BunnyJump, { .v = -18.0f, .jumps = 2 });
    step(world, 1);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 0);
    step(world, frames);
}

void Effects_dust_near_feet(void) {
    // bunnyhopper/effects
    image_t images[2];
    float lo[2], hi[2];
    for (int32_t run = 0; run < 2; run ++) {
        ecs_world_t *world = fx_new(run == 0);
        land(world, 8);
        images[run] = capture(world, "dust.png");

        project(world, -10.0f, 1.5f, lo);
        project(world, -6.0f, -0.2f, hi);
        ecs_fini(world);
    }

    int32_t count = 0;
    float cx = 0.0f, cy = 0.0f;
    for (int32_t i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT; i ++) {
        if (differs(&images[0], &images[1], i)) {
            count ++;
            cx += (float)(i % WINDOW_WIDTH);
            cy += (float)(i / WINDOW_WIDTH);
        }
    }

    test_assert(count > 50);
    cx /= (float)count;
    cy /= (float)count;
    test_assert(cx > lo[0] && cx < hi[0]);
    test_assert(cy > lo[1] && cy < hi[1]);

    float dust = 0.0f, track = 0.0f;
    int32_t pale = 0;
    for (int32_t i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT; i ++) {
        if (differs(&images[0], &images[1], i)) {
            dust += luminance(&images[0].px[i * 4]);
            track += luminance(&images[1].px[i * 4]);
            pale += images[0].px[i * 4] >= images[0].px[i * 4 + 2];
        }
    }

    test_assert(dust > track);
    test_assert(pale > count / 2);

    stbi_image_free(images[0].px);
    stbi_image_free(images[1].px);
}

void Effects_sparkle_brighter_than_sky(void) {
    // bunnyhopper/effects
    image_t images[2];
    for (int32_t run = 0; run < 2; run ++) {
        ecs_world_t *world = fx_new(run == 0);
        const BunnyGame *g = ecs_singleton_get(world, BunnyGame);
        ecs_entity_t c = ecs_new_w_pair(world, EcsIsA, ecs_lookup(world, "SmallCarrot"));
        ecs_add_pair(world, c, EcsChildOf, g->carrot_parent);
        ecs_set(world, c, Position, { DESPAWN_X + 0.1f, 0.0f, 0.0f });
        step(world, 2);
        test_assert(!ecs_is_alive(world, c));
        step(world, 8);
        images[run] = capture(world, "sparkle.png");
        ecs_fini(world);
    }

    float sparkle = 0.0f, sky = 0.0f;
    int32_t count = 0;
    for (int32_t i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT; i ++) {
        if (differs(&images[0], &images[1], i)) {
            sparkle = fmaxf(sparkle, luminance(&images[0].px[i * 4]));
            count ++;
        }
    }

    for (int32_t i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT / 5; i ++) {
        const unsigned char *px = &images[1].px[i * 4];
        if (px[2] > px[0] + 20 && px[2] > px[1]) {
            sky = fmaxf(sky, luminance(px));
        }
    }

    test_assert(count > 20);
    test_assert(sparkle > sky + 20.0f);

    stbi_image_free(images[0].px);
    stbi_image_free(images[1].px);
}
