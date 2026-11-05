/**
 * @file TransitionBorder.c
 * @brief Tests that a transition of the Border of a 2D shape blends its width and color with the
 * GPU backend: halfway through, the border renders as a shape set up at the halfway border, and
 * at the end as a shape set up at the target.
 *
 * The world advances a quarter second per frame and the transitions take one second, Linear.
 * The worlds are made one after the other, so no two render at once.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge in pixels of the images */
#define TBO_SIZE (64)

/** Seconds every frame advances the world */
#define TBO_DELTA (0.25f)

/** Largest difference of a channel between a frame in mid transition and the scene set up in
 * that state: the antialiased edges may round apart */
#define TBO_TOLERANCE (4)

/** Captures after a change that render at progress 1/2 */
#define TBO_MID (3)

/** Captures after a change that render at the target */
#define TBO_END (5)

/** The border a case starts at: thin and grey */
static const Border tbo_from = { .width = 2, .radius = 4, .color = {128, 128, 128, 255} };

/** The border a case changes to: thick and blue */
static const Border tbo_to = { .width = 8, .radius = 4, .color = {0, 0, 255, 255} };

/** Halfway between them, the channels rounded as the resolver rounds them */
static const Border tbo_mid = { .width = 5, .radius = 4, .color = {64, 64, 192, 255} };

/** Create a headless world with an ortho camera of one unit per pixel and a white clear in a
 * 64x64 image.
 *
 * @return The world
 */
static ecs_world_t* tbo_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {255, 255, 255, 255}}});

    return world;
}

/** Add a red 40x40 rect with a Border at the center.
 *
 * @param world The world
 * @param border The border
 * @return The rect
 */
static ecs_entity_t tbo_rect(
    ecs_world_t *world,
    const Border *border)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {40, 40});
    ecs_set(world, e, Rgb, {255, 0, 0});
    ecs_set_ptr(world, e, Border, border);

    return e;
}

/** Render a frame that advances the world by TBO_DELTA into an image file and load it.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a TBO_SIZE square image, freed with stbi_image_free
 */
static unsigned char* tbo_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = TBO_SIZE, .height = TBO_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, TBO_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, TBO_SIZE);
    test_int(h, TBO_SIZE);
    ecs_os_free(path);

    return px;
}

/** Render a number of frames and keep the last.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @param count Frames to render, at least 1
 * @return The pixels of the last frame, freed with stbi_image_free
 */
static unsigned char* tbo_capture_n(
    ecs_world_t *world,
    const char *file,
    int32_t count)
{
    int32_t i;
    for (i = 1; i < count; i ++) {
        stbi_image_free(tbo_capture(world, file));
    }

    return tbo_capture(world, file);
}

/** Get the largest difference of a channel between two captures.
 *
 * @param a The first capture
 * @param b The second capture
 * @return The difference
 */
static int32_t tbo_diff(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i, result = 0;
    for (i = 0; i < TBO_SIZE * TBO_SIZE * 4; i ++) {
        if ((i & 3) == 3) {
            continue;
        }

        int32_t d = abs((int32_t)a[i] - (int32_t)b[i]);
        result = d > result ? d : result;
    }

    return result;
}

/** Check that a world has no device errors and free it.
 *
 * @param world The world
 */
static void tbo_fini(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    ecs_fini(world);
}

/** Render a scene of one bordered rect without a transition.
 *
 * @param file Image file in the temporary folder
 * @param border The border
 * @param shadow The BoxShadow of the rect, NULL for none
 * @return The pixels, freed with stbi_image_free
 */
static unsigned char* tbo_reference(
    const char *file,
    const Border *border,
    const BoxShadow *shadow)
{
    ecs_world_t *world = tbo_world();
    ecs_entity_t e = tbo_rect(world, border);
    if (shadow) {
        ecs_set_ptr(world, e, BoxShadow, shadow);
    }

    unsigned char *result = tbo_capture_n(world, file, 2);
    tbo_fini(world);

    return result;
}

/** Run a Border transition of a rect from tbo_from to tbo_to and compare it halfway with a rect
 * set up at tbo_mid and at the end with one set up at tbo_to.
 *
 * @param name Prefix of the image files
 * @param shadow The BoxShadow of the rect, NULL for none
 */
static void tbo_case(
    const char *name,
    const BoxShadow *shadow)
{
    char *file = flecs_asprintf("%s.png", name);
    char *ref = flecs_asprintf("%s_ref.png", name);
    ecs_world_t *world = tbo_world();
    ecs_entity_t e = tbo_rect(world, &tbo_from);
    if (shadow) {
        ecs_set_ptr(world, e, BoxShadow, shadow);
    }

    box_transition_set(world, e, ecs_id(Border), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *start = tbo_capture_n(world, file, 2);
    ecs_set_ptr(world, e, Border, &tbo_to);
    unsigned char *moving = tbo_capture_n(world, file, TBO_MID);
    test_assert(ecs_has_id(world, e, TransitionMaterial));
    unsigned char *end = tbo_capture_n(world, file, TBO_END - TBO_MID);
    test_assert(!ecs_has_id(world, e, TransitionMaterial));
    tbo_fini(world);

    unsigned char *at_mid = tbo_reference(ref, &tbo_mid, shadow);
    unsigned char *at_end = tbo_reference(ref, &tbo_to, shadow);
    test_assert(tbo_diff(moving, start) > 20);
    test_assert(tbo_diff(moving, at_end) > 20);
    test_assert(tbo_diff(moving, at_mid) <= TBO_TOLERANCE);
    test_int(tbo_diff(end, at_end), 0);
    stbi_image_free(start);
    stbi_image_free(moving);
    stbi_image_free(end);
    stbi_image_free(at_mid);
    stbi_image_free(at_end);
    ecs_os_free(file);
    ecs_os_free(ref);
}

/** A bordered rect grows its border and turns it from grey to blue: halfway through it is the
 * rect set up at the halfway border, and at the end the one set up at the target.
 */
void TransitionBorder_width_color_mid_transition(void) {
    // ui/border
    tbo_case("tbo_border", NULL);
}

/** The same with a BoxShadow under the rect, which follows the rendered border.
 */
void TransitionBorder_with_box_shadow(void) {
    // ui/border
    const BoxShadow shadow = { .offset_x = 4, .offset_y = -4, .blur = 4,
        .color = {0, 0, 0, 160} };
    tbo_case("tbo_border_shadow", &shadow);
}
