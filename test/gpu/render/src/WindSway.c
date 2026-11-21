/**
 * @file WindSway.c
 * @brief Tests for how the Wind moves the vertices of the shapes with a WindSway, and their
 * shadows, and that it leaves every other shape as it is, checked on PNG captures.
 *
 * The cases look straight down from 12 units above a white floor into a 128x128 capture where a
 * unit is about 9.2 pixels: image right is +x and image down is +z. A sun at 45 degrees casts the
 * shadow of a 3 unit pole at the origin along +z. The wind blows along +x without gusts.
 *
 * Every frame advances the world by SWAY_DT, so the frames are the same every run.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Edge of the captures in pixels */
#define SWAY_SIZE (128)

/** Seconds every frame of the cases advances the world */
#define SWAY_DT (1.0f / 60.0f)

/** Pixels of one unit on the floor: the capture height over the height the camera sees */
#define SWAY_UNIT ((float)SWAY_SIZE / (24.0f * tanf(GLM_PIf / 6.0f)))

/** Create the world of the cases lifted by a height: the camera above the origin looking down, a
 * white floor, a sun at 45 degrees that shadows toward +z, a little ambient light and shadows,
 * in a scene with Scene.wind or without it. The camera and the floor are lifted, so a lifted
 * scene captures as one at height 0.
 *
 * @param wind Whether the scene has Scene.wind
 * @param lift World height the camera and the floor are lifted by
 * @return The world
 */
static ecs_world_t* sway_world_at(
    bool wind,
    float lift)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.2f});
    ecs_singleton_set(world, Scene, {.shader_features.Extended.wind = wind});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 12 + lift, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_4, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f + lift, 0});
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    return world;
}

/** Create the world of the cases at height 0 (sway_world_at).
 *
 * @param wind Whether the scene has Scene.wind
 * @return The world
 */
static ecs_world_t* sway_world(
    bool wind)
{
    return sway_world_at(wind, 0);
}

/** Set the wind of the cases: along +x without gusts.
 *
 * @param world The world
 * @param speed The speed
 */
static void sway_wind(
    ecs_world_t *world,
    float speed)
{
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = speed, .gust = 0,
        .gust_scale = 20});
}

/** Add the 3 unit pole of the cases standing on the floor at the origin.
 *
 * @param world The world
 * @param sway Whether it sways: a bend of 0.05 that is full at its top
 * @return The pole
 */
static ecs_entity_t sway_pole(
    ecs_world_t *world,
    bool sway)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 1.5f, 0});
    ecs_set(world, e, Box, {0.3f, 3, 0.3f});
    ecs_set(world, e, Rgb, {200, 60, 40});
    if (sway) {
        ecs_set(world, e, WindSway, {.bend = 0.05f, .height = 3, .base = 0,
            .flutter_frequency = 2, .flutter_reach = 0.3f});
    }

    return e;
}

/** Add the 3 unit swaying pole of the cases standing on a floor lifted by a height, with a
 * bend of 0.05 that is full at its top. With a local base the base is its foot, 1.5 units below
 * its origin, wherever it stands; without, the base is world Y 0.
 *
 * @param world The world
 * @param lift World height of the foot of the pole
 * @param local Whether the base is measured from the origin of the pole
 * @return The pole
 */
static ecs_entity_t sway_pole_at(
    ecs_world_t *world,
    float lift,
    bool local)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 1.5f + lift, 0});
    ecs_set(world, e, Box, {0.3f, 3, 0.3f});
    ecs_set(world, e, Rgb, {200, 60, 40});
    ecs_set(world, e, WindSway, {.bend = 0.05f, .height = 3, .base = local ? -1.5f : 0,
        .flutter_frequency = 2, .flutter_reach = 0.3f, .local_base = local});

    return e;
}

/** Advance the world by frames of SWAY_DT.
 *
 * @param world The world
 * @param frames Frames
 */
static void sway_frames(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, SWAY_DT);
        box_render_sync(world);
    }
}

/** Render two frames into a PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* sway_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = SWAY_SIZE, .height = SWAY_SIZE,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    sway_frames(world, 2);
    ecs_delete(world, e);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, SWAY_SIZE);
    test_int(h, SWAY_SIZE);

    return px;
}

/** Sum of the channels of a pixel of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Red plus green plus blue
 */
static int sway_sum(
    const unsigned char *px,
    int x,
    int y)
{
    const unsigned char *p = &px[(y * SWAY_SIZE + x) * 4];

    return p[0] + p[1] + p[2];
}

/** Mean column of the shadowed floor pixels in a band of rows: the pixels darker than half the
 * brightest pixel of the band.
 *
 * @param px The pixels
 * @param top First row
 * @param bottom Last row
 * @return The mean column, -1 when no pixel is shadowed
 */
static float sway_dark_column(
    const unsigned char *px,
    int top,
    int bottom)
{
    int x, y, bright = 0, count = 0;
    float sum = 0.0f;
    for (y = top; y <= bottom; y ++) {
        for (x = 0; x < SWAY_SIZE; x ++) {
            bright = sway_sum(px, x, y) > bright ? sway_sum(px, x, y) : bright;
        }
    }

    for (y = top; y <= bottom; y ++) {
        for (x = 0; x < SWAY_SIZE; x ++) {
            if (sway_sum(px, x, y) * 2 < bright) {
                sum += (float)x;
                count ++;
            }
        }
    }

    return count ? sum / (float)count : -1.0f;
}

/** Mean column of the red pixels of the pole seen from above.
 *
 * @param px The pixels
 * @return The mean column, -1 when the pole is not seen
 */
static float sway_pole_column(
    const unsigned char *px)
{
    int x, y, count = 0;
    float sum = 0.0f;
    for (y = 0; y < SWAY_SIZE; y ++) {
        for (x = 0; x < SWAY_SIZE; x ++) {
            const unsigned char *p = &px[(y * SWAY_SIZE + x) * 4];
            if (p[0] > 2 * p[1] + 20 && p[0] > 60) {
                sum += (float)x;
                count ++;
            }
        }
    }

    return count ? sum / (float)count : -1.0f;
}

/** A calm wind draws a swaying pole and its shadow exactly as a pole without a WindSway */
void WindSway_calm_matches_without(void) {
    // materials/wind_sway
    ecs_world_t *world = sway_world(true);
    sway_wind(world, 0);
    ecs_entity_t pole = sway_pole(world, true);
    sway_frames(world, 30);
    unsigned char *calm = sway_render(world, "wind_calm.png");

    ecs_remove(world, pole, WindSway);
    sway_frames(world, 3);
    unsigned char *rigid = sway_render(world, "wind_rigid.png");
    test_assert(!memcmp(calm, rigid, SWAY_SIZE * SWAY_SIZE * 4));

    stbi_image_free(calm);
    stbi_image_free(rigid);
    ecs_fini(world);
}

/** The wind bends the top of the pole downwind, and two times of a fixed delta render different
 * frames */
void WindSway_bends_downwind(void) {
    // materials/wind_sway
    ecs_world_t *world = sway_world(true);
    sway_wind(world, 0);
    sway_pole(world, true);
    sway_frames(world, 3);
    unsigned char *calm = sway_render(world, "wind_bend_calm.png");

    sway_wind(world, 10);
    sway_frames(world, 30);
    unsigned char *a = sway_render(world, "wind_bend_a.png");
    sway_frames(world, 40);
    unsigned char *b = sway_render(world, "wind_bend_b.png");

    /* At full bend the top moves 0.5 units times the sway, 0.45 to 0.55 */
    float rest = sway_pole_column(calm);
    float bent = sway_pole_column(a);
    test_assert(rest > 0.0f && bent > 0.0f);
    test_assert(bent - rest > 0.1f * SWAY_UNIT);
    test_assert(memcmp(a, b, SWAY_SIZE * SWAY_SIZE * 4) != 0);

    stbi_image_free(calm);
    stbi_image_free(a);
    stbi_image_free(b);
    ecs_fini(world);
}

/** The shadow of the top of the pole moves downwind with it: the tip of the shadow, 2 to 3 units
 * along +z, shifts toward +x */
void WindSway_shadow_follows(void) {
    // materials/wind_sway
    ecs_world_t *world = sway_world(true);
    sway_wind(world, 0);
    sway_pole(world, true);
    sway_frames(world, 3);
    unsigned char *calm = sway_render(world, "wind_shadow_calm.png");

    sway_wind(world, 10);
    sway_frames(world, 30);
    unsigned char *windy = sway_render(world, "wind_shadow_windy.png");

    int top = SWAY_SIZE / 2 + (int)(2.2f * SWAY_UNIT);
    int bottom = SWAY_SIZE / 2 + (int)(2.8f * SWAY_UNIT);
    float rest = sway_dark_column(calm, top, bottom);
    float moved = sway_dark_column(windy, top, bottom);
    test_assert(rest > 0.0f && moved > 0.0f);
    test_assert(fabsf(rest - SWAY_SIZE / 2.0f) < 0.3f * SWAY_UNIT);
    test_assert(moved - rest > 0.15f * SWAY_UNIT);

    stbi_image_free(calm);
    stbi_image_free(windy);
    ecs_fini(world);
}

/** A strong wind in a scene with Scene.wind draws a mesh without a WindSway and its shadow
 * exactly as the same scene without Scene.wind */
void WindSway_plain_mesh_unchanged(void) {
    // materials/wind_sway
    ecs_world_t *world = sway_world(true);
    sway_wind(world, 30);
    sway_pole(world, false);
    sway_frames(world, 30);
    unsigned char *wind = sway_render(world, "wind_plain_on.png");

    ecs_singleton_set(world, Scene, {.shader_features.Extended.wind = false});
    sway_frames(world, 3);
    unsigned char *still = sway_render(world, "wind_plain_off.png");
    test_assert(!memcmp(wind, still, SWAY_SIZE * SWAY_SIZE * 4));

    ecs_singleton_remove(world, Wind);
    sway_frames(world, 3);
    unsigned char *none = sway_render(world, "wind_plain_none.png");
    test_assert(!memcmp(none, still, SWAY_SIZE * SWAY_SIZE * 4));

    stbi_image_free(wind);
    stbi_image_free(still);
    stbi_image_free(none);
    ecs_fini(world);
}

/** Without Scene.wind a pole with a WindSway draws as without it in a strong wind */
void WindSway_scene_off_draws_without(void) {
    // materials/wind_sway
    ecs_world_t *world = sway_world(false);
    sway_wind(world, 30);
    ecs_entity_t pole = sway_pole(world, true);
    sway_frames(world, 30);
    unsigned char *off = sway_render(world, "wind_off_sway.png");

    ecs_remove(world, pole, WindSway);
    sway_frames(world, 3);
    unsigned char *rigid = sway_render(world, "wind_off_rigid.png");
    test_assert(!memcmp(off, rigid, SWAY_SIZE * SWAY_SIZE * 4));

    stbi_image_free(off);
    stbi_image_free(rigid);
    ecs_fini(world);
}

/** A gusty wind rolls gusts through the pole: as the gust bands travel past it, the pole leans
 * further and straightens again, so its top moves well beyond the gentle sway around its lean */
void WindSway_gusts_roll(void) {
    // materials/wind_sway
    ecs_world_t *world = sway_world(true);
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 10, .gust = 1,
        .gust_scale = 4});
    sway_pole(world, true);
    sway_frames(world, 3);

    float lo = (float)SWAY_SIZE, hi = 0.0f;
    int32_t i;
    for (i = 0; i < 8; i ++) {
        char name[64];
        snprintf(name, sizeof(name), "wind_gust_%d.png", (int)i);
        sway_frames(world, 20);
        unsigned char *px = sway_render(world, name);
        float column = sway_pole_column(px);
        test_assert(column > 0.0f);
        lo = column < lo ? column : lo;
        hi = column > hi ? column : hi;
        stbi_image_free(px);
    }

    /* At full bend and no gust the top moves 0.5 units; the gusts move it between 0 and twice
     * that, the sway by a tenth of it */
    test_assert(hi - lo > 0.2f * SWAY_UNIT);

    ecs_fini(world);
}

/** How far the wind moves a lifted pole and its shadow: the shift of the pole seen from above
 * and of its shadow at 0.8 to 1.4 units and at 2.2 to 2.8 units along +z (the shadows of those
 * heights of the pole), in pixels, between a calm frame and one after 30 frames of a wind of 10.
 */
typedef struct {
    float pole;
    float middle;
    float tip;
} sway_shift_t;

/** Measure how far the wind moves the pole of the cases standing on a lifted floor.
 *
 * @param lift World height of the floor, the camera and the foot of the pole
 * @param local Whether the base of the pole is measured from its origin
 * @param name Prefix of the file names of the captures
 * @return The shifts
 */
static sway_shift_t sway_shift(
    float lift,
    bool local,
    const char *name)
{
    char calm_name[64], windy_name[64];
    snprintf(calm_name, sizeof(calm_name), "%s_calm.png", name);
    snprintf(windy_name, sizeof(windy_name), "%s_windy.png", name);
    ecs_world_t *world = sway_world_at(true, lift);
    sway_wind(world, 0);
    sway_pole_at(world, lift, local);
    sway_frames(world, 3);
    unsigned char *calm = sway_render(world, calm_name);

    sway_wind(world, 10);
    sway_frames(world, 30);
    unsigned char *windy = sway_render(world, windy_name);

    int mid_top = SWAY_SIZE / 2 + (int)(0.8f * SWAY_UNIT);
    int mid_bottom = SWAY_SIZE / 2 + (int)(1.4f * SWAY_UNIT);
    int tip_top = SWAY_SIZE / 2 + (int)(2.2f * SWAY_UNIT);
    int tip_bottom = SWAY_SIZE / 2 + (int)(2.8f * SWAY_UNIT);
    float pole_rest = sway_pole_column(calm), pole_bent = sway_pole_column(windy);
    float mid_rest = sway_dark_column(calm, mid_top, mid_bottom);
    float mid_bent = sway_dark_column(windy, mid_top, mid_bottom);
    float tip_rest = sway_dark_column(calm, tip_top, tip_bottom);
    float tip_bent = sway_dark_column(windy, tip_top, tip_bottom);
    test_assert(pole_rest > 0.0f && pole_bent > 0.0f);
    test_assert(mid_rest > 0.0f && mid_bent > 0.0f);
    test_assert(tip_rest > 0.0f && tip_bent > 0.0f);

    sway_shift_t result = {
        .pole = pole_bent - pole_rest,
        .middle = mid_bent - mid_rest,
        .tip = tip_bent - tip_rest
    };

    stbi_image_free(calm);
    stbi_image_free(windy);
    ecs_fini(world);

    return result;
}

/** A pole with a local base bends from its foot wherever it stands: lifted with its floor and
 * the camera, it and its shadow move as far as at height 0, in the forward pass and in the
 * shadows; with a world base the lifted pole bends over its whole length */
void WindSway_local_base_any_height(void) {
    // materials/wind_sway
    sway_shift_t ground = sway_shift(0, true, "wind_local_0");
    sway_shift_t high = sway_shift(7.5f, true, "wind_local_high");
    sway_shift_t low = sway_shift(-4.25f, true, "wind_local_low");

    /* The top moves downwind, the middle of the pole far less than its top */
    test_assert(ground.pole > 0.1f * SWAY_UNIT);
    test_assert(ground.tip > 0.15f * SWAY_UNIT);
    test_assert(ground.middle < 0.5f * ground.tip);

    float tolerance = 0.05f * SWAY_UNIT;
    test_assert(fabsf(high.pole - ground.pole) < tolerance);
    test_assert(fabsf(high.middle - ground.middle) < tolerance);
    test_assert(fabsf(high.tip - ground.tip) < tolerance);
    test_assert(fabsf(low.pole - ground.pole) < tolerance);
    test_assert(fabsf(low.middle - ground.middle) < tolerance);
    test_assert(fabsf(low.tip - ground.tip) < tolerance);

    /* At height 0 the local base bends as the world base at the foot */
    sway_shift_t world_ground = sway_shift(0, false, "wind_world_0");
    test_assert(fabsf(world_ground.pole - ground.pole) < tolerance);
    test_assert(fabsf(world_ground.middle - ground.middle) < tolerance);
    test_assert(fabsf(world_ground.tip - ground.tip) < tolerance);

    /* Lifted above its world base the whole pole leans, its middle as far as its top */
    sway_shift_t world_high = sway_shift(7.5f, false, "wind_world_high");
    test_assert(world_high.middle - ground.middle > 0.2f * SWAY_UNIT);
}
