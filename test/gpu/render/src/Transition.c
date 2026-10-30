/**
 * @file Transition.c
 * @brief Tests that transform and material transitions render, with the GPU backend: a shape
 * and the shadow of a caster are drawn where the transition is and a mesh in the color it is
 * at, not at the target the game thread sees.
 *
 * The camera looks straight down from 4 units above a white floor into a 64x64 image with a 90
 * degree camera: world x = 0 lands on column 32 and a world unit on the floor is 8 pixels. The
 * world advances a quarter second per frame and the transitions take one second, Linear.
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
#define TRANS_SIZE (64)

/** Seconds every frame advances the world */
#define TRANS_DELTA (0.25f)

/** Channel at most of a dark or red pixel */
#define TRANS_DARK (60)

/** Channel at least of a lit white pixel */
#define TRANS_LIT (100)

/** Create a headless world on the main thread without ambient light, with a white rough floor
 * on y = 0, a spot light 10 units above the origin shining down, and the camera.
 *
 * @return The world
 */
static ecs_world_t* trans_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 4, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 90, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);

    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 10, 0});
    ecs_set(world, light, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, light, SpotLight, {
        .intensity = 60 * GLM_PIf, .range = 20, .inner_angle = 40, .outer_angle = 45,
        .shadows = true, .shadow_resolution = 512
    });

    return world;
}

/** Render a frame that advances the world by TRANS_DELTA into an image file and load it.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @return RGBA pixels of a TRANS_SIZE square image, freed with stbi_image_free
 */
static unsigned char* trans_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = TRANS_SIZE, .height = TRANS_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, TRANS_DELTA);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, TRANS_SIZE);
    test_int(h, TRANS_SIZE);
    ecs_os_free(path);

    return px;
}

/** Get a channel of a pixel.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row
 * @param channel 0 red, 1 green, 2 blue
 * @return The channel
 */
static int32_t trans_channel(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    int32_t channel)
{
    return px[(y * TRANS_SIZE + x) * 4 + channel];
}

/** Check that a world has no device errors.
 *
 * @param world The world
 */
static void trans_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
}

/** A red unit box on the floor moves from x = -2 to x = 2. Its top is 3 units below the
 * camera, so x = -2, 0 and 2 land on about columns 11, 32 and 53; the green channel tells the
 * red box from the white floor.
 */
void Transition_shape_mid_transition(void) {
    // engine_core/transition
    ecs_world_t *world = trans_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {-2, 0.5f, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {255, 0, 0});
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = trans_capture(world, "transition_shape_0.png");
    test_assert(trans_channel(px, 11, 32, 1) < TRANS_DARK);
    test_assert(trans_channel(px, 32, 32, 1) > TRANS_LIT);
    stbi_image_free(px);

    ecs_set(world, e, Position, {2, 0.5f, 0});
    px = trans_capture(world, "transition_shape_1.png");
    test_assert(trans_channel(px, 11, 32, 1) < TRANS_DARK);
    test_assert(trans_channel(px, 32, 32, 1) > TRANS_LIT);
    stbi_image_free(px);

    stbi_image_free(trans_capture(world, "transition_shape_2.png"));
    px = trans_capture(world, "transition_shape_3.png");
    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 2);
    test_assert(trans_channel(px, 32, 32, 1) < TRANS_DARK);
    test_assert(trans_channel(px, 32, 32, 0) > TRANS_LIT);
    test_assert(trans_channel(px, 11, 32, 1) > TRANS_LIT);
    test_assert(trans_channel(px, 53, 32, 1) > TRANS_LIT);
    stbi_image_free(px);

    stbi_image_free(trans_capture(world, "transition_shape_4.png"));
    px = trans_capture(world, "transition_shape_5.png");
    test_assert(trans_channel(px, 53, 32, 1) < TRANS_DARK);
    test_assert(trans_channel(px, 32, 32, 1) > TRANS_LIT);
    test_assert(trans_channel(px, 11, 32, 1) > TRANS_LIT);
    stbi_image_free(px);

    trans_no_errors(world);
    ecs_fini(world);
}

/** A 1x1 slab 6 units above the floor, behind the camera, moves from x = -1.2 to x = 1.2.
 * The light 10 units up casts its shadow 2.5 times as far out and as wide: centered on
 * x = -3, 0 and 3 (columns 8, 32 and 56), 20 pixels wide.
 *
 * The shadow atlas renders the tile again while the caster moves and after it stops.
 */
void Transition_shadow_mid_transition(void) {
    // engine_core/transition
    ecs_world_t *world = trans_world();
    ecs_entity_t slab = ecs_new(world);
    ecs_set(world, slab, Position, {-1.2f, 6, 0});
    ecs_set(world, slab, Box, {1, 0.1f, 1});
    box_transition_set(world, slab, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = trans_capture(world, "transition_shadow_0.png");
    test_assert(trans_channel(px, 8, 32, 0) < TRANS_DARK);
    test_assert(trans_channel(px, 32, 32, 0) > TRANS_LIT);
    stbi_image_free(px);

    ecs_set(world, slab, Position, {1.2f, 6, 0});
    px = trans_capture(world, "transition_shadow_1.png");
    test_assert(trans_channel(px, 8, 32, 0) < TRANS_DARK);
    test_assert(trans_channel(px, 32, 32, 0) > TRANS_LIT);
    test_assert(trans_channel(px, 56, 32, 0) > TRANS_LIT);
    stbi_image_free(px);

    stbi_image_free(trans_capture(world, "transition_shadow_2.png"));
    px = trans_capture(world, "transition_shadow_3.png");
    test_assert(trans_channel(px, 32, 32, 0) < TRANS_DARK);
    test_assert(trans_channel(px, 8, 32, 0) > TRANS_LIT);
    test_assert(trans_channel(px, 56, 32, 0) > TRANS_LIT);
    stbi_image_free(px);

    stbi_image_free(trans_capture(world, "transition_shadow_4.png"));
    px = trans_capture(world, "transition_shadow_5.png");
    test_assert(trans_channel(px, 56, 32, 0) < TRANS_DARK);
    test_assert(trans_channel(px, 32, 32, 0) > TRANS_LIT);
    test_assert(trans_channel(px, 8, 32, 0) > TRANS_LIT);
    stbi_image_free(px);

    px = trans_capture(world, "transition_shadow_6.png");
    test_assert(trans_channel(px, 56, 32, 0) < TRANS_DARK);
    test_assert(trans_channel(px, 32, 32, 0) > TRANS_LIT);
    stbi_image_free(px);

    trans_no_errors(world);
    ecs_fini(world);
}

/** A box in front of the camera, with the default lighting, changes its Rgb from red to blue
 * over a second.
 *
 * The frames render at progress 0, 1/4, 1/2 and 3/4 and then at the target: the red and blue
 * channels of its center pixel cross over while the game thread sees blue from the start.
 */
void Transition_mesh_color_mid_transition(void) {
    // engine_core/transition
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
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {2, 2, 2});
    ecs_set(world, e, Rgb, {255, 0, 0});
    box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = trans_capture(world, "transition_color_0.png");
    int32_t mid = TRANS_SIZE / 2;
    test_assert(trans_channel(px, mid, mid, 0) > trans_channel(px, mid, mid, 2) + 60);
    stbi_image_free(px);

    ecs_set(world, e, Rgb, {0, 0, 255});
    px = trans_capture(world, "transition_color_1.png");
    test_assert(trans_channel(px, mid, mid, 0) > trans_channel(px, mid, mid, 2) + 60);
    stbi_image_free(px);

    stbi_image_free(trans_capture(world, "transition_color_2.png"));
    px = trans_capture(world, "transition_color_3.png");
    test_int(ecs_get(world, e, Rgb)->r, 0);
    test_int(ecs_get(world, e, Rgb)->b, 255);
    int32_t r = trans_channel(px, mid, mid, 0), b = trans_channel(px, mid, mid, 2);
    test_assert(r > 40);
    test_assert(b > 40);
    test_assert(abs(r - b) < 40);
    test_assert(trans_channel(px, mid, mid, 1) < TRANS_DARK);
    stbi_image_free(px);

    stbi_image_free(trans_capture(world, "transition_color_4.png"));
    px = trans_capture(world, "transition_color_5.png");
    test_assert(trans_channel(px, mid, mid, 2) > trans_channel(px, mid, mid, 0) + 60);
    stbi_image_free(px);

    trans_no_errors(world);
    ecs_fini(world);
}
