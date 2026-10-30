/**
 * @file TransitionTintOpacity.c
 * @brief Tests that Tint and Opacity transitions render with the GPU backend, through the
 * hierarchy: a child box in the tint and the opacity its parent is at, not at the target the
 * game thread sees.
 *
 * A box 6 units in front of the camera fills the center of a 64x64 image on a black clear. The
 * world advances a quarter second per frame and the transitions take one second, Linear, so
 * the frames after a change render at progress 0, 1/4, 1/2 and 3/4 and then at the target.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge in pixels of the images */
#define TTO_SIZE (64)

/** Seconds every frame advances the world */
#define TTO_DELTA (0.25f)

/** Channel at most of a dark pixel */
#define TTO_DARK (20)

/** Create a headless world with a camera looking at the origin from 6 units away, a black
 * clear, and a parent without a shape that has a red 2x2x2 box as its child.
 *
 * @param out Receives the world
 * @param child Receives the box
 * @return The parent
 */
static ecs_entity_t tto_world(
    ecs_world_t **out,
    ecs_entity_t *child)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    *child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, *child, Position, {0, 0, 0});
    ecs_set(world, *child, Box, {2, 2, 2});
    ecs_set(world, *child, Rgb, {255, 0, 0});
    *out = world;

    return parent;
}

/** Render a frame that advances the world by TTO_DELTA into an image file and load it.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a TTO_SIZE square image, freed with stbi_image_free
 */
static unsigned char* tto_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = TTO_SIZE, .height = TTO_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, TTO_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, TTO_SIZE);
    test_int(h, TTO_SIZE);
    ecs_os_free(path);

    return px;
}

/** Get a channel of the center pixel.
 *
 * @param px The pixels
 * @param channel 0 red, 1 green, 2 blue
 * @return The channel
 */
static int32_t tto_center(
    const unsigned char *px,
    int32_t channel)
{
    int32_t mid = TTO_SIZE / 2;

    return px[(mid * TTO_SIZE + mid) * 4 + channel];
}

/** Check that a world has no device errors.
 *
 * @param world The world
 */
static void tto_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

/** The parent tints its red child toward blue, from a strength of 0 to 255: red and blue of
 * the center pixel cross over, while the game thread sees the full tint from the start.
 */
void TransitionTintOpacity_tint_mid_transition(void) {
    // engine_core/transition
    ecs_world_t *world;
    ecs_entity_t child;
    ecs_entity_t parent = tto_world(&world, &child);
    ecs_set(world, parent, Tint, {0, 0, 255, 0});
    box_transition_set(world, parent, ecs_id(Tint), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = tto_capture(world, "transition_tint_0.png");
    test_assert(tto_center(px, 0) > tto_center(px, 2) + 60);
    stbi_image_free(px);

    ecs_set(world, parent, Tint, {0, 0, 255, 255});
    px = tto_capture(world, "transition_tint_1.png");
    test_assert(tto_center(px, 0) > tto_center(px, 2) + 60);
    stbi_image_free(px);

    stbi_image_free(tto_capture(world, "transition_tint_2.png"));
    px = tto_capture(world, "transition_tint_3.png");
    test_int(ecs_get(world, parent, Tint)->a, 255);
    int32_t r = tto_center(px, 0), b = tto_center(px, 2);
    test_assert(r > 40);
    test_assert(b > 40);
    test_assert(abs(r - b) < 40);
    stbi_image_free(px);

    stbi_image_free(tto_capture(world, "transition_tint_4.png"));
    px = tto_capture(world, "transition_tint_5.png");
    test_assert(tto_center(px, 2) > tto_center(px, 0) + 60);
    stbi_image_free(px);

    tto_no_errors(world);
    ecs_fini(world);
}

/** The parent fades its red child out over the black clear, from opacity 1 to 0: the center
 * pixel is red, then dimmer in mid transition, then black, while the game thread sees 0 from
 * the start. The child draws blended during the transition.
 */
void TransitionTintOpacity_opacity_mid_transition(void) {
    // engine_core/transition
    ecs_world_t *world;
    ecs_entity_t child;
    ecs_entity_t parent = tto_world(&world, &child);
    ecs_set(world, parent, Opacity, {1.0f});
    box_transition_set(world, parent, ecs_id(Opacity), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = tto_capture(world, "transition_opacity_0.png");
    int32_t full = tto_center(px, 0);
    test_assert(full > 60);
    stbi_image_free(px);

    ecs_set(world, parent, Opacity, {0.0f});
    px = tto_capture(world, "transition_opacity_1.png");
    test_assert(abs(tto_center(px, 0) - full) < 10);
    stbi_image_free(px);

    stbi_image_free(tto_capture(world, "transition_opacity_2.png"));
    px = tto_capture(world, "transition_opacity_3.png");
    test_flt(ecs_get(world, parent, Opacity)->value, 0);
    int32_t mid = tto_center(px, 0);
    test_assert(mid > TTO_DARK);
    test_assert(mid < full - 15);
    stbi_image_free(px);

    stbi_image_free(tto_capture(world, "transition_opacity_4.png"));
    px = tto_capture(world, "transition_opacity_5.png");
    test_assert(tto_center(px, 0) < TTO_DARK);
    stbi_image_free(px);

    tto_no_errors(world);
    ecs_fini(world);
}
