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
#include "draw.h"

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

/** The red box of shape_mid_transition moves to x = 2 while the GPU backend applies compile
 * outcomes after the frame, the way the browser does.
 *
 * The box shares its batch with the floor, so the frame that ends the transition scatters its
 * slot, the first scatter of the world: it sends the scatter shader on demand, whose module
 * arrives only with the next packet. The box still ends at x = 2.
 */
void Transition_shape_ends_before_scatter_shader_compiles(void) {
    // engine_core/transition
    ecs_world_t *world = trans_world();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {-2, 0.5f, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {255, 0, 0});
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = trans_capture(world, "transition_scatter_0.png");
    test_assert(trans_channel(px, 11, 32, 1) < TRANS_DARK);
    stbi_image_free(px);

    box_render_gpu_stats_t before;
    test_assert(box_render_gpu_stats(world, &before));
    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_NEXT_PACKET), true);
    ecs_set(world, e, Position, {2, 0.5f, 0});
    int32_t i;
    for (i = 1; i <= 5; i ++) {
        char file[64];
        snprintf(file, sizeof(file), "transition_scatter_%d.png", i);
        stbi_image_free(trans_capture(world, file));
    }

    px = trans_capture(world, "transition_scatter_6.png");
    test_assert(trans_channel(px, 53, 32, 1) < TRANS_DARK);
    test_assert(trans_channel(px, 53, 32, 0) > TRANS_LIT);
    test_assert(trans_channel(px, 32, 32, 1) > TRANS_LIT);
    test_assert(trans_channel(px, 11, 32, 1) > TRANS_LIT);
    stbi_image_free(px);

    box_render_gpu_stats_t after;
    test_assert(box_render_gpu_stats(world, &after));
    test_assert(after.scattered > before.scattered);
    trans_no_errors(world);
    ecs_fini(world);
}

/** Create the world of the clear coat case: a lit view without ambient light or sun, the camera
 * 6 units in front of a rough dark 4x4 quad with a coat of strength 1, and a point light 3 units
 * in front of the quad, whose coat shows its highlight at the center of the image.
 *
 * @param roughness Roughness of the coat
 * @param quad Receives the quad
 * @return The world
 */
static ecs_world_t* trans_coat_world(
    float roughness,
    ecs_entity_t *quad)
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
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Quad, {4, 4});
    ecs_set(world, e, Rgb, {40, 40, 40});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, e, ClearCoat, {.strength = 1, .roughness = roughness});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 3});
    ecs_set(world, light, PointLight, {.intensity = 5, .range = 20});
    *quad = e;

    return world;
}

/** Get the brightest pixel of an image as the sum of its channels.
 *
 * @param px The pixels
 * @return The largest channel sum
 */
static int32_t trans_peak(
    const unsigned char *px)
{
    int32_t best = 0, x, y;
    for (y = 0; y < TRANS_SIZE; y ++) {
        for (x = 0; x < TRANS_SIZE; x ++) {
            int32_t sum = trans_channel(px, x, y, 0) + trans_channel(px, x, y, 1) +
                trans_channel(px, x, y, 2);
            best = sum > best ? sum : best;
        }
    }

    return best;
}

/** A coated quad under a point light changes the roughness of its coat from 0 to 1 over a
 * second: mid transition its highlight peaks between the peaks of the two end values, and at
 * the end it peaks as a quad that has the rough coat from the start.
 */
void Transition_clear_coat_mid_transition(void) {
    // materials/pbr
    ecs_entity_t e = 0, still = 0;
    ecs_world_t *rough = trans_coat_world(1, &still);
    stbi_image_free(trans_capture(rough, "transition_coat_rough_0.png"));
    unsigned char *px = trans_capture(rough, "transition_coat_rough_1.png");
    int32_t end_peak = trans_peak(px);
    stbi_image_free(px);
    trans_no_errors(rough);
    ecs_fini(rough);

    ecs_world_t *world = trans_coat_world(0, &e);
    box_transition_set(world, e, ecs_id(ClearCoat), &(TransitionSpec){ .duration = 1.0f });
    stbi_image_free(trans_capture(world, "transition_coat_0.png"));
    px = trans_capture(world, "transition_coat_1.png");
    int32_t start_peak = trans_peak(px);
    stbi_image_free(px);
    test_assert(start_peak > end_peak + 60);

    ecs_set(world, e, ClearCoat, {.strength = 1, .roughness = 1});
    px = trans_capture(world, "transition_coat_2.png");
    test_assert(trans_peak(px) > end_peak + 60);
    stbi_image_free(px);

    stbi_image_free(trans_capture(world, "transition_coat_3.png"));
    px = trans_capture(world, "transition_coat_4.png");
    test_flt(ecs_get(world, e, ClearCoat)->roughness, 1);
    int32_t mid_peak = trans_peak(px);
    test_assert(mid_peak < start_peak - 20);
    test_assert(mid_peak > end_peak + 20);
    stbi_image_free(px);

    stbi_image_free(trans_capture(world, "transition_coat_5.png"));
    px = trans_capture(world, "transition_coat_6.png");
    test_assert(abs(trans_peak(px) - end_peak) <= 3);
    stbi_image_free(px);

    trans_no_errors(world);
    ecs_fini(world);
}

/** Convert a display encoded byte to linear light.
 *
 * @param c The byte
 * @return The linear value
 */
static float trans_lin(
    int32_t c)
{
    float v = (float)c / 255.0f;

    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

/** Convert linear light to a display encoded byte.
 *
 * @param lin The linear value
 * @return The byte
 */
static int32_t trans_srgb(
    float lin)
{
    lin = lin < 0.0f ? 0.0f : (lin > 1.0f ? 1.0f : lin);
    float v = lin <= 0.0031308f ? lin * 12.92f : 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;

    return (int32_t)(v * 255.0f + 0.5f);
}

/** Check the center pixel of an image against expected channels.
 *
 * @param px The pixels
 * @param want Expected red, green and blue
 * @param tolerance Largest difference per channel
 */
static void trans_center_is(
    const unsigned char *px,
    const int32_t want[3],
    int32_t tolerance)
{
    int32_t mid = TRANS_SIZE / 2, c;
    for (c = 0; c < 3; c ++) {
        int32_t got = trans_channel(px, mid, mid, c);
        if (abs(got - want[c]) > tolerance) {
            printf("channel %d: got %d, want %d\n", c, got, want[c]);
        }

        test_assert(abs(got - want[c]) <= tolerance);
    }
}

/** Check that a center channel is clearly between two values.
 *
 * @param px The pixels
 * @param channel 0 red, 1 green, 2 blue
 * @param a One end
 * @param b The other end
 */
static void trans_center_between(
    const unsigned char *px,
    int32_t channel,
    int32_t a,
    int32_t b)
{
    int32_t mid = TRANS_SIZE / 2, got = trans_channel(px, mid, mid, channel);
    int32_t lo = a < b ? a : b, hi = a < b ? b : a;
    if (got < lo + 20 || got > hi - 20) {
        printf("channel %d: got %d, want between %d and %d\n", channel, got, lo, hi);
    }

    test_assert(got >= lo + 20);
    test_assert(got <= hi - 20);
}

/** Check that a shape is still drawn from the same slot of a transmissive batch.
 *
 * @param world The world
 * @param e The shape
 * @param batch Its batch
 * @param slot Its slot
 */
static void trans_same_batch(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t batch,
    int32_t slot)
{
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    test_assert(ds != NULL);
    test_int(ds->batch, batch);
    test_int(ds->slot, slot);
    test_assert(box_draws_batch(box_draws_state(world), batch)->key.transmissive);
}

/** A smooth white glass slab 4 units in front of the camera, of index of refraction 1 and
 * thickness and attenuation distance 1, over an emissive wall 10 units away in a lit view
 * without light. Its Transmission transitions twice over a second each:
 *
 * - transmission_factor 0 to 1: the unlit glass shows black at the start and the wall at the
 *   end, sampled from the opaque snapshot.
 * - attenuation_color white to red: the transmitted wall keeps its color at the start and is
 *   absorbed down to the wall times the color at the end.
 *
 * Mid transition the center pixel is between the two ends on the channels that change, the
 * game thread sees the target from the start, and the glass keeps its slot of its batch.
 */
void Transition_transmission_mid_transition(void) {
    // engine_core/transition
    const int32_t bg[3] = {200, 220, 230}, red[3] = {255, 60, 60};
    const int32_t black[3] = {0, 0, 0};
    int32_t tinted[3], c;
    for (c = 0; c < 3; c ++) {
        tinted[c] = trans_srgb(trans_lin(bg[c]) * trans_lin(red[c]));
    }

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1000, 1000, 1000});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 0.1f});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting = {.enabled = true}});

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -10});
    ecs_set(world, wall, Box, {40, 40, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1,
        .color = {(uint8_t)bg[0], (uint8_t)bg[1], (uint8_t)bg[2]}});

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, -4});
    ecs_set(world, e, Box, {3, 3, 0.2f});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 0});
    ecs_set(world, e, Transmission, {.transmission_factor = 0, .ior = 1,
        .thickness_factor = 1, .attenuation_distance = 1,
        .attenuation_color = {255, 255, 255}});
    box_transition_set(world, e, ecs_id(Transmission), &(TransitionSpec){ .duration = 1.0f });

    stbi_image_free(trans_capture(world, "transition_transmission_0.png"));
    unsigned char *px = trans_capture(world, "transition_transmission_1.png");
    test_assert(abs(trans_channel(px, 2, 2, 0) - bg[0]) <= 2);
    test_assert(abs(trans_channel(px, 2, 2, 1) - bg[1]) <= 2);
    test_assert(abs(trans_channel(px, 2, 2, 2) - bg[2]) <= 2);
    trans_center_is(px, black, 3);
    stbi_image_free(px);
    int32_t batch = ecs_get(world, e, DrawSlot)->batch;
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;
    trans_same_batch(world, e, batch, slot);

    /* transmission_factor 0 to 1 */
    ecs_set(world, e, Transmission, {.transmission_factor = 1, .ior = 1,
        .thickness_factor = 1, .attenuation_distance = 1,
        .attenuation_color = {255, 255, 255}});
    px = trans_capture(world, "transition_transmission_2.png");
    test_flt(ecs_get(world, e, Transmission)->transmission_factor, 1);
    trans_center_is(px, black, 3);
    stbi_image_free(px);
    trans_same_batch(world, e, batch, slot);

    stbi_image_free(trans_capture(world, "transition_transmission_3.png"));
    trans_same_batch(world, e, batch, slot);
    px = trans_capture(world, "transition_transmission_4.png");
    test_flt(ecs_get(world, e, Transmission)->transmission_factor, 1);
    for (c = 0; c < 3; c ++) {
        trans_center_between(px, c, black[c], bg[c]);
    }

    stbi_image_free(px);
    trans_same_batch(world, e, batch, slot);

    stbi_image_free(trans_capture(world, "transition_transmission_5.png"));
    px = trans_capture(world, "transition_transmission_6.png");
    trans_center_is(px, bg, 3);
    stbi_image_free(px);
    trans_same_batch(world, e, batch, slot);

    /* attenuation_color white to red */
    ecs_set(world, e, Transmission, {.transmission_factor = 1, .ior = 1,
        .thickness_factor = 1, .attenuation_distance = 1,
        .attenuation_color = {(uint8_t)red[0], (uint8_t)red[1], (uint8_t)red[2]}});
    px = trans_capture(world, "transition_transmission_7.png");
    test_int(ecs_get(world, e, Transmission)->attenuation_color.g, red[1]);
    trans_center_is(px, bg, 3);
    stbi_image_free(px);
    trans_same_batch(world, e, batch, slot);

    stbi_image_free(trans_capture(world, "transition_transmission_8.png"));
    trans_same_batch(world, e, batch, slot);
    px = trans_capture(world, "transition_transmission_9.png");
    test_int(ecs_get(world, e, Transmission)->attenuation_color.g, red[1]);
    int32_t mid = TRANS_SIZE / 2;
    test_assert(abs(trans_channel(px, mid, mid, 0) - bg[0]) <= 3);
    trans_center_between(px, 1, bg[1], tinted[1]);
    trans_center_between(px, 2, bg[2], tinted[2]);
    stbi_image_free(px);
    trans_same_batch(world, e, batch, slot);

    stbi_image_free(trans_capture(world, "transition_transmission_10.png"));
    px = trans_capture(world, "transition_transmission_11.png");
    test_int(ecs_get(world, e, Transmission)->attenuation_color.b, red[2]);
    trans_center_is(px, tinted, 3);
    stbi_image_free(px);
    trans_same_batch(world, e, batch, slot);

    trans_no_errors(world);
    ecs_fini(world);
}

/** Column of the center of the left box of the shared slot cases */
#define TRANS_LEFT (16)

/** Column of the center of the right box of the shared slot cases */
#define TRANS_RIGHT (48)

/** Create the world of the shared slot cases: the view of mesh_color_mid_transition with two
 * 1.5 unit boxes at x = -1.5 and x = 1.5, centered on about columns 16 and 48 of row 32.
 *
 * @param left Color of the left box
 * @param right Color of the right box
 * @param a Receives the left box
 * @param b Receives the right box
 * @return The world
 */
static ecs_world_t* trans_pair_world(
    Rgb left,
    Rgb right,
    ecs_entity_t *a,
    ecs_entity_t *b)
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

    *a = ecs_new(world);
    ecs_set(world, *a, Position, {-1.5f, 0, 0});
    ecs_set(world, *a, Box, {1.5f, 1.5f, 1.5f});
    ecs_set_ptr(world, *a, Rgb, &left);
    *b = ecs_new(world);
    ecs_set(world, *b, Position, {1.5f, 0, 0});
    ecs_set(world, *b, Box, {1.5f, 1.5f, 1.5f});
    ecs_set_ptr(world, *b, Rgb, &right);

    return world;
}

/** Get the material slot of a shape.
 *
 * @param world The world
 * @param e The shape
 * @return Its MaterialId, UINT32_MAX when it has none
 */
static uint32_t trans_slot(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const MaterialId *id = ecs_get(world, e, MaterialId);

    return id ? id->value : UINT32_MAX;
}

/** Check that a box is drawn red: no green, and red well above blue.
 *
 * @param px The pixels
 * @param x Column of the center of the box
 */
static void trans_is_red(
    const unsigned char *px,
    int32_t x)
{
    int32_t r = trans_channel(px, x, 32, 0), g = trans_channel(px, x, 32, 1);
    int32_t b = trans_channel(px, x, 32, 2);
    if (!(g < TRANS_DARK && r > b + 60)) {
        printf("column %d: got %d %d %d, want red\n", x, r, g, b);
    }

    test_assert(g < TRANS_DARK);
    test_assert(r > b + 60);
}

/** Check that a box is drawn blue: no green, and blue well above red.
 *
 * @param px The pixels
 * @param x Column of the center of the box
 */
static void trans_is_blue(
    const unsigned char *px,
    int32_t x)
{
    int32_t r = trans_channel(px, x, 32, 0), g = trans_channel(px, x, 32, 1);
    int32_t b = trans_channel(px, x, 32, 2);
    if (!(g < TRANS_DARK && b > r + 60)) {
        printf("column %d: got %d %d %d, want blue\n", x, r, g, b);
    }

    test_assert(g < TRANS_DARK);
    test_assert(b > r + 60);
}

/** Run the frames of a red to blue or blue to red transition of the box at column x, which
 * starts on the frame of the change, and check every frame up to two frames after its end.
 *
 * The frames render at progress 0, 1/4, 1/2, 3/4 and then at the target. The box keeps the
 * start color on the first frame, the channel of the start color only falls and the one of the
 * target only rises from frame to frame, the middle frame is in between, and from the frame the
 * transition ends the box has the target color. No frame has green, which the white default
 * material would show.
 *
 * @param world The world
 * @param x Column of the center of the transitioning box
 * @param to_blue Whether the box goes from red to blue, else from blue to red
 * @param check Called on every frame to check the other box
 * @param name Prefix of the image files
 */
static void trans_pair_frames(
    ecs_world_t *world,
    int32_t x,
    bool to_blue,
    void(*check)(const unsigned char*),
    const char *name)
{
    int32_t from = to_blue ? 0 : 2, to = to_blue ? 2 : 0;
    int32_t prev_from = 255, prev_to = 0, i;
    for (i = 1; i <= 7; i ++) {
        char file[64];
        snprintf(file, sizeof(file), "%s_%d.png", name, i);
        unsigned char *px = trans_capture(world, file);
        int32_t f = trans_channel(px, x, 32, from), t = trans_channel(px, x, 32, to);
        int32_t g = trans_channel(px, x, 32, 1);
        printf("%s frame %d: from %d, to %d, green %d\n", name, i, f, t, g);
        test_assert(g < TRANS_DARK);
        test_assert(f <= prev_from + 3);
        test_assert(t >= prev_to - 3);
        if (i == 1) {
            if (to_blue) {
                trans_is_red(px, x);
            } else {
                trans_is_blue(px, x);
            }
        } else if (i == 3) {
            test_assert(f > 40);
            test_assert(t > 40);
            test_assert(abs(f - t) < 40);
        } else if (i >= 5) {
            if (to_blue) {
                trans_is_blue(px, x);
            } else {
                trans_is_red(px, x);
            }
        }

        check(px);
        prev_from = f;
        prev_to = t;
        stbi_image_free(px);
    }
}

/** Check that the right box is red.
 *
 * @param px The pixels
 */
static void trans_right_red(
    const unsigned char *px)
{
    trans_is_red(px, TRANS_RIGHT);
}

/** Two red boxes share a material slot, and only the left one has a transition on Rgb. It
 * changes to blue: it moves to a private slot when the transition starts and to the shared slot
 * of blue when it ends, and neither switch shows as a frame of another color. The right box
 * keeps its slot and stays red in every frame.
 */
void Transition_mesh_color_shared_slot_transition(void) {
    // gpu_instance_data/material_dedup
    ecs_entity_t a, b;
    ecs_world_t *world = trans_pair_world(
        (Rgb){255, 0, 0}, (Rgb){255, 0, 0}, &a, &b);
    box_transition_set(world, a, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = trans_capture(world, "transition_shared_0.png");
    trans_is_red(px, TRANS_LEFT);
    trans_is_red(px, TRANS_RIGHT);
    stbi_image_free(px);
    uint32_t shared = trans_slot(world, b);
    test_assert(shared != UINT32_MAX);
    test_uint(trans_slot(world, a), shared);

    ecs_set(world, a, Rgb, {0, 0, 255});
    trans_pair_frames(world, TRANS_LEFT, true, trans_right_red, "transition_shared");
    test_uint(trans_slot(world, b), shared);
    test_assert(trans_slot(world, a) != shared);
    test_assert(trans_slot(world, a) != UINT32_MAX);

    trans_no_errors(world);
    ecs_fini(world);
}

/** A blue box with a transition on Rgb next to a red box changes to red: when the transition
 * ends it moves to the shared slot of the red box, with no frame of another color, and the red
 * box stays red in every frame.
 */
void Transition_mesh_color_shared_slot_transition_back(void) {
    // gpu_instance_data/material_dedup
    ecs_entity_t a, b;
    ecs_world_t *world = trans_pair_world(
        (Rgb){0, 0, 255}, (Rgb){255, 0, 0}, &a, &b);
    box_transition_set(world, a, ecs_id(Rgb), &(TransitionSpec){ .duration = 1.0f });
    unsigned char *px = trans_capture(world, "transition_shared_back_0.png");
    trans_is_blue(px, TRANS_LEFT);
    trans_is_red(px, TRANS_RIGHT);
    stbi_image_free(px);
    uint32_t red = trans_slot(world, b);
    test_assert(red != UINT32_MAX);
    test_assert(trans_slot(world, a) != red);

    ecs_set(world, a, Rgb, {255, 0, 0});
    trans_pair_frames(world, TRANS_LEFT, false, trans_right_red, "transition_shared_back");
    test_uint(trans_slot(world, b), red);
    test_uint(trans_slot(world, a), red);

    trans_no_errors(world);
    ecs_fini(world);
}
