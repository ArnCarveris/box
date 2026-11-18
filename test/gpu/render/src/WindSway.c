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

/** Create the world of the cases: the camera above the origin looking down, a white floor, a sun
 * at 45 degrees that shadows toward +z, a little ambient light and shadows, in a scene with
 * Scene.wind or without it.
 *
 * @param wind Whether the scene has Scene.wind
 * @return The world
 */
static ecs_world_t* sway_world(
    bool wind)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0.2f});
    ecs_singleton_set(world, Scene, {.wind = wind});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 12, 0});
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
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    return world;
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

    /* At full bend the top moves 0.5 units times the sway, 0.375 to 0.625 */
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

    ecs_singleton_set(world, Scene, {.wind = false});
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
