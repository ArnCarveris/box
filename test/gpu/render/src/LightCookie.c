/**
 * @file LightCookie.c
 * @brief Tests for the LightCookie of the first directional light: the remap of its stored
 * values, its plane and light projections, its tiling and wind, and what it leaves alone (the
 * ambient light and the probe captures).
 *
 * An orthographic camera above a white floor on y = 0, lit straight down at pi / 2, sees 8 units
 * in 64x64 pixels: column c is x = (c + 0.5) / 8 - 4, row r is z = (r + 0.5) / 8 - 4.
 *
 * The cookies are grayscale PNGs (gen_cookie_textures.py) loaded single channel: split is black
 * above its middle row, so size 8 and offset {0, 4} darken the floor at z < 0, rows 0 to 31.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Uniform 128 */
#define COOKIE_GRAY_PNG "test/assets/textures/cookie_gray.png"

/** Uniform 0 */
#define COOKIE_BLACK_PNG "test/assets/textures/cookie_black.png"

/** 256 columns whose gray is the column */
#define COOKIE_RAMP_PNG "test/assets/textures/cookie_ramp.png"

/** Black top half, white bottom half */
#define COOKIE_SPLIT_PNG "test/assets/textures/cookie_split.png"

/** Stripes 2 rows high, alternately 0 and 255 */
#define COOKIE_STRIPES_PNG "test/assets/textures/cookie_stripes.png"

/** Stripes 2 columns wide, alternately 0 and 255 */
#define COOKIE_COLUMNS_PNG "test/assets/textures/cookie_columns.png"

/** Seamless fbm noise, half of its texels below 128 */
#define COOKIE_NOISE_PNG "test/assets/textures/cloud_noise_256.png"

/** Edge of the captures in pixels */
#define SIZE (64)

/** Pixels per world unit of the captures */
#define PIXELS (8)

/** Seconds every frame advances */
#define DT (1.0f / 60.0f)

/** Create a headless world with a white rough floor on y = 0, a sun, an ambient light and a
 * camera looking straight down at the floor.
 *
 * @param ambient Intensity of the default AmbientLight colors, 0 for none
 * @param sun Receives the sun, which shines straight down at irradiance pi / 2
 * @return The world
 */
static ecs_world_t* cookie_world(
    float ambient,
    ecs_entity_t *sun)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_add(world, AmbientLight);
    ecs_singleton_get_mut(world, AmbientLight)->intensity = ambient;
    ecs_singleton_modified(world, AmbientLight);

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    *sun = ecs_new(world);
    ecs_set(world, *sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, *sun, DirectionalLight, {.intensity = 0.5f * GLM_PIf});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 8, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});

    return world;
}

/** Find the camera of a world.
 *
 * @param world The world
 * @return The camera
 */
static ecs_entity_t cookie_camera(
    ecs_world_t *world)
{
    ecs_entity_t camera = 0;
    ecs_query_t *q = ecs_query(world, { .terms = {{ ecs_id(Camera) }} });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            camera = it.entities[i];
        }
    }

    ecs_query_fini(q);
    test_assert(camera != 0);

    return camera;
}

/** Create a headless world with a white rough wall on z = 0 facing the camera, lit by a low
 * sun 10 degrees above the horizon behind the camera, without ambient light.
 *
 * The camera looks along -Z and sees the same 8 units in 64x64 pixels as over the floor:
 * column c is x = (c + 0.5) / 8 - 4, row r is y = 4 - (r + 0.5) / 8.
 *
 * @param sun Receives the sun
 * @return The world
 */
static ecs_world_t* wall_world(
    ecs_entity_t *sun)
{
    ecs_world_t *world = cookie_world(0, sun);
    ecs_delete_with(world, ecs_id(Box));

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -0.1f});
    ecs_set(world, wall, Box, {40, 40, 0.2f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});

    ecs_set(world, *sun, Rotation, {(float)GLM_PI - glm_rad(10), 0, 0});

    ecs_entity_t camera = cookie_camera(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Rotation, {0, 0, 0});

    return world;
}

/** Create a single channel texture and run frames until it is loaded.
 *
 * @param world The world
 * @param path Image file
 * @return The texture entity
 */
static ecs_entity_t cookie_texture(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t tex = ecs_new(world);
    int32_t i;
    ecs_set(world, tex, TexturePath, {.path = ECS_CONST_CAST(char*, path),
        .single_channel = true});
    for (i = 0; i < 200; i ++) {
        const TextureStatus *s = ecs_get(world, tex, TextureStatus);
        if (s && s->state != TextureLoading) {
            break;
        }

        ecs_progress(world, DT);
        box_render_sync(world);
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);

    return tex;
}

/** Set a cookie on a light: the defaults with a texture, size, offset and projection.
 *
 * @param world The world
 * @param light The light
 * @param texture The texture, 0 for none
 * @param size World units the texture repeats over
 * @param offset_z Offset along world Z or the light's Y
 * @return The cookie, to change further before ecs_modified
 */
static LightCookie* cookie_set(
    ecs_world_t *world,
    ecs_entity_t light,
    ecs_entity_t texture,
    float size,
    float offset_z)
{
    ecs_add(world, light, LightCookie);
    LightCookie *c = ecs_get_mut(world, light, LightCookie);
    c->texture = texture;
    c->size = size;
    c->height = 5;
    c->offset[1] = offset_z;

    return c;
}

/** Render frames into an image file and load the pixels of the last one.
 *
 * @param world The world
 * @param file Image file in the temporary folder
 * @param frames Frames to run, at least 1
 * @return RGBA pixels, freed with stbi_image_free
 */
static unsigned char* cookie_capture(
    ecs_world_t *world,
    const char *file,
    int32_t frames)
{
    ecs_entity_t e = ecs_lookup(world, "capture");
    int32_t i;
    if (!e) {
        e = ecs_entity(world, { .name = "capture" });
    }

    ecs_set(world, e, Surface, {.width = SIZE, .height = SIZE,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, DT);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, SIZE);
    test_int(h, SIZE);
    ecs_os_free(path);

    return px;
}

/** Get the linear light of the green channel of a pixel.
 *
 * @param px The pixels
 * @param col Column
 * @param row Row
 * @return The decoded channel, 0 to 1
 */
static float lum(
    const unsigned char *px,
    int32_t col,
    int32_t row)
{
    float c = (float)px[(row * SIZE + col) * 4 + 1] / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Get the largest difference of a channel between two images.
 *
 * @param a The first image
 * @param b The second image
 * @return The difference
 */
static int32_t largest_difference(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i, result = 0;
    for (i = 0; i < SIZE * SIZE * 4; i ++) {
        int32_t d = abs((int32_t)a[i] - (int32_t)b[i]);
        result = d > result ? d : result;
    }

    return result;
}

/** Find the first row of a column, from row 12 down, lit at least half as much as the brightest
 * pixel of the column.
 *
 * @param px The pixels
 * @param col The column
 * @return The row, -1 for none
 */
static int32_t lit_row(
    const unsigned char *px,
    int32_t col)
{
    float top = 0;
    int32_t r;
    for (r = 0; r < SIZE; r ++) {
        top = glm_max(top, lum(px, col, r));
    }

    int32_t result = -1;
    for (r = 12; r < SIZE && result == -1; r ++) {
        if (lum(px, col, r) >= 0.5f * top) {
            result = r;
        }
    }

    return result;
}

/** Find the row of a column where it turns from dark to lit, going down the image and around.
 *
 * @param px The pixels
 * @param col The column
 * @return The first lit row after a dark one, -1 for none
 */
static int32_t rising_row(
    const unsigned char *px,
    int32_t col)
{
    float top = 0;
    int32_t r;
    for (r = 0; r < SIZE; r ++) {
        top = glm_max(top, lum(px, col, r));
    }

    int32_t result = -1;
    for (r = 0; r < SIZE && result == -1; r ++) {
        bool before = lum(px, col, (r + SIZE - 1) % SIZE) >= 0.5f * top;
        if (!before && lum(px, col, r) >= 0.5f * top) {
            result = r;
        }
    }

    return result;
}

void LightCookie_default_settings(void) {
    // lighting/light_cookie
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t sun = ecs_new(world);
    ecs_add(world, sun, DirectionalLight);
    ecs_add(world, sun, LightCookie);
    const LightCookie *c = ecs_get(world, sun, LightCookie);
    test_assert(c != NULL);
    test_uint(c->texture, 0);
    test_int(c->projection, LightCookieProjectionPlane);
    test_flt(c->size, 4000);
    test_flt(c->height, 1500);
    test_flt(c->offset[0], 0);
    test_flt(c->offset[1], 0);
    test_flt(c->velocity[0], 0);
    test_flt(c->velocity[1], 0);
    test_flt(c->threshold, 0.5f);
    test_flt(c->softness, 0.5f);
    test_flt(c->strength, 1);

    test_assert(ecs_script_run(world, "cookie",
        "using box.light\n"
        "moon { DirectionalLight: {}\n LightCookie: {projection: Light, size: 8} }\n",
        NULL) == 0);
    c = ecs_get(world, ecs_lookup(world, "moon"), LightCookie);
    test_int(c->projection, LightCookieProjectionLight);
    test_flt(c->size, 8);
    test_flt(c->height, 1500);
    test_flt(c->strength, 1);

    ecs_fini(world);
}

void LightCookie_no_texture_no_effect(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(1, &sun);
    unsigned char *none = cookie_capture(world, "cookie_none.png", 2);
    cookie_set(world, sun, 0, 8, 0);
    ecs_modified(world, sun, LightCookie);
    unsigned char *empty = cookie_capture(world, "cookie_empty.png", 2);

    test_assert(lum(none, 32, 32) > 0.2f);
    test_assert(largest_difference(none, empty) <= 1);

    stbi_image_free(none);
    stbi_image_free(empty);
    ecs_fini(world);
}

void LightCookie_uniform_half(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(1, &sun);
    ecs_entity_t gray = cookie_texture(world, COOKIE_GRAY_PNG);
    unsigned char *none = cookie_capture(world, "half_none.png", 2);
    cookie_set(world, sun, gray, 8, 0);
    ecs_modified(world, sun, LightCookie);
    unsigned char *half = cookie_capture(world, "half_gray.png", 2);
    ecs_remove(world, sun, LightCookie);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    unsigned char *ambient = cookie_capture(world, "half_ambient.png", 2);

    float a = lum(ambient, 32, 32), n = lum(none, 32, 32), h = lum(half, 32, 32);
    float expected = a + (128.0f / 255.0f) * (n - a);
    test_assert(a > 0.01f);
    test_assert(n - a > 0.2f);
    test_assert(fabsf(h - expected) < 0.015f);

    stbi_image_free(none);
    stbi_image_free(half);
    stbi_image_free(ambient);
    ecs_fini(world);
}

void LightCookie_ambient_unaffected(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(1, &sun);
    ecs_entity_t black = cookie_texture(world, COOKIE_BLACK_PNG);
    cookie_set(world, sun, black, 8, 0);
    ecs_modified(world, sun, LightCookie);
    unsigned char *covered = cookie_capture(world, "ambient_black.png", 2);
    ecs_remove(world, sun, LightCookie);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    unsigned char *ambient = cookie_capture(world, "ambient_only.png", 2);

    test_assert(lum(ambient, 32, 32) > 0.01f);
    test_assert(largest_difference(covered, ambient) <= 2);

    stbi_image_free(covered);
    stbi_image_free(ambient);
    ecs_fini(world);
}

void LightCookie_strength_mixes(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(1, &sun);
    ecs_entity_t black = cookie_texture(world, COOKIE_BLACK_PNG);
    unsigned char *none = cookie_capture(world, "strength_none.png", 2);
    LightCookie *c = cookie_set(world, sun, black, 8, 0);
    c->strength = 0;
    ecs_modified(world, sun, LightCookie);
    unsigned char *zero = cookie_capture(world, "strength_zero.png", 2);
    ecs_get_mut(world, sun, LightCookie)->strength = 1;
    ecs_modified(world, sun, LightCookie);
    unsigned char *full = cookie_capture(world, "strength_full.png", 2);
    ecs_get_mut(world, sun, LightCookie)->strength = 0.5f;
    ecs_modified(world, sun, LightCookie);
    unsigned char *half = cookie_capture(world, "strength_half.png", 2);

    float n = lum(none, 32, 32), f = lum(full, 32, 32), h = lum(half, 32, 32);
    test_assert(largest_difference(none, zero) <= 1);
    test_assert(n - f > 0.2f);
    test_assert(fabsf(h - 0.5f * (n + f)) < 0.015f);

    stbi_image_free(none);
    stbi_image_free(zero);
    stbi_image_free(full);
    stbi_image_free(half);
    ecs_fini(world);
}

/** The ramp texture reads (x + 4) / 8. Softness 0 gives two flat levels split at x = 0: columns
 * 4 and 20 (x = -3.4, -1.4) dark, 44 and 60 (x = 1.6, 3.6) equally lit.
 *
 * Softness 0.25 ramps over x = -2 to 2: x = -1.4 is at 0.15 of the light, x = 1.6 at 0.9, and
 * the outer columns keep the two levels.
 */
void LightCookie_threshold_softness(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(0, &sun);
    ecs_entity_t ramp = cookie_texture(world, COOKIE_RAMP_PNG);
    LightCookie *c = cookie_set(world, sun, ramp, 8, 0);
    c->offset[0] = 4;
    c->softness = 0;
    ecs_modified(world, sun, LightCookie);
    unsigned char *hard = cookie_capture(world, "threshold_hard.png", 2);
    ecs_get_mut(world, sun, LightCookie)->softness = 0.25f;
    ecs_modified(world, sun, LightCookie);
    unsigned char *soft = cookie_capture(world, "threshold_soft.png", 2);

    float lit = lum(hard, 60, 32);
    test_assert(lit > 0.2f);
    test_assert(lum(hard, 4, 32) < 0.005f);
    test_assert(lum(hard, 20, 32) < 0.005f);
    test_assert(fabsf(lum(hard, 44, 32) - lit) < 0.01f);

    test_assert(lum(soft, 4, 32) < 0.005f);
    test_assert(fabsf(lum(soft, 60, 32) - lit) < 0.01f);
    test_assert(fabsf(lum(soft, 20, 32) / lit - 0.15f) < 0.06f);
    test_assert(fabsf(lum(soft, 44, 32) / lit - 0.9f) < 0.06f);

    stbi_image_free(hard);
    stbi_image_free(soft);
    ecs_fini(world);
}

/** With size 2 positions 16 pixels apart get the same value, and the step across the wrap at
 * x = 0, between columns 31 and 32, is no larger than the steps between other columns.
 */
void LightCookie_tiles_repeat(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(0, &sun);
    ecs_entity_t noise = cookie_texture(world, COOKIE_NOISE_PNG);
    cookie_set(world, sun, noise, 2, 0);
    ecs_modified(world, sun, LightCookie);
    unsigned char *px = cookie_capture(world, "tiles.png", 2);
    int32_t r, c, largest = 0;
    float seam = 0, steps = 0;

    for (r = 0; r < SIZE; r ++) {
        for (c = 0; c + 2 * PIXELS < SIZE; c ++) {
            int32_t d = abs((int32_t)px[(r * SIZE + c) * 4 + 1] -
                (int32_t)px[(r * SIZE + c + 2 * PIXELS) * 4 + 1]);
            largest = d > largest ? d : largest;
        }
    }

    test_assert(largest <= 3);

    for (r = 0; r < SIZE; r ++) {
        seam += fabsf(lum(px, 32, r) - lum(px, 31, r));
        for (c = 0; c + 1 < SIZE; c ++) {
            steps += fabsf(lum(px, c + 1, r) - lum(px, c, r));
        }
    }

    steps /= (float)(SIZE - 1);
    test_assert(steps > 0);
    test_assert(seam <= 3.0f * steps + 0.02f * SIZE);

    stbi_image_free(px);
    ecs_fini(world);
}

/** Tilting the sun by atan(0.25) toward -Z shifts the pattern of a plane 4 units up by 1 unit,
 * 8 rows, toward +Z. A plane below the floor leaves it as without a cookie.
 */
void LightCookie_plane_projection(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(0, &sun);
    ecs_entity_t split = cookie_texture(world, COOKIE_SPLIT_PNG);
    LightCookie *c = cookie_set(world, sun, split, 8, 4);
    c->height = 4;
    c->softness = 0;
    ecs_modified(world, sun, LightCookie);
    unsigned char *straight = cookie_capture(world, "plane_straight.png", 2);

    float tilt = atanf(0.25f);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2 - tilt, 0, 0});
    unsigned char *tilted = cookie_capture(world, "plane_tilted.png", 2);

    int32_t edge = lit_row(straight, 32), shifted = lit_row(tilted, 32);
    test_assert(abs(edge - 32) <= 1);
    test_assert(abs(shifted - edge - PIXELS) <= 1);
    test_assert(lum(straight, 32, 20) < 0.005f);

    ecs_get_mut(world, sun, LightCookie)->height = -1;
    ecs_modified(world, sun, LightCookie);
    unsigned char *below = cookie_capture(world, "plane_below.png", 2);
    ecs_remove(world, sun, LightCookie);
    unsigned char *none = cookie_capture(world, "plane_none.png", 2);
    test_assert(lum(none, 32, 20) > 0.2f);
    test_assert(largest_difference(below, none) <= 1);

    stbi_image_free(straight);
    stbi_image_free(tilted);
    stbi_image_free(below);
    stbi_image_free(none);
    ecs_fini(world);
}

/** The local Y axis of the sun pitched straight down is world +Z, so the top of the image is dark.
 * Rolled a quarter turn about its axis its Y axis is world -X: the right half is dark instead.
 * The plane projection ignores the roll.
 */
void LightCookie_light_projection(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(0, &sun);
    ecs_entity_t split = cookie_texture(world, COOKIE_SPLIT_PNG);
    LightCookie *c = cookie_set(world, sun, split, 8, 4);
    c->projection = LightCookieProjectionLight;
    c->softness = 0;
    ecs_modified(world, sun, LightCookie);

    unsigned char *px = cookie_capture(world, "light_straight.png", 2);
    test_assert(lum(px, 32, 16) < 0.005f);
    test_assert(lum(px, 32, 48) > 0.2f);
    test_assert(fabsf(lum(px, 16, 48) - lum(px, 48, 48)) < 0.01f);

    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, (float)GLM_PI_2});
    unsigned char *rolled = cookie_capture(world, "light_rolled.png", 2);
    test_assert(lum(rolled, 48, 32) < 0.005f);
    test_assert(lum(rolled, 16, 32) > 0.2f);
    test_assert(fabsf(lum(rolled, 16, 16) - lum(rolled, 16, 48)) < 0.01f);

    ecs_get_mut(world, sun, LightCookie)->projection = LightCookieProjectionPlane;
    ecs_modified(world, sun, LightCookie);
    unsigned char *plane = cookie_capture(world, "light_plane.png", 2);
    test_assert(lum(plane, 32, 16) < 0.005f);
    test_assert(lum(plane, 32, 48) > 0.2f);

    stbi_image_free(px);
    stbi_image_free(rolled);
    stbi_image_free(plane);
    ecs_fini(world);
}

/** 30 frames of 1/60 s at 2 units a second move the pattern 1 unit, 8 rows, toward -Z; the
 * offset stays within the size after hours of simulated time.
 */
void LightCookie_velocity_moves(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(0, &sun);
    ecs_entity_t split = cookie_texture(world, COOKIE_SPLIT_PNG);
    LightCookie *c = cookie_set(world, sun, split, 8, 4);
    c->softness = 0;
    c->velocity[1] = 2;
    ecs_modified(world, sun, LightCookie);
    unsigned char *before = cookie_capture(world, "wind_before.png", 2);

    unsigned char *after = cookie_capture(world, "wind_after.png", 30);
    int32_t r0 = rising_row(before, 32), r1 = rising_row(after, 32);
    test_assert(r0 != -1);
    test_assert(r1 != -1);
    test_assert(abs((r0 - r1 + SIZE) % SIZE - PIXELS) <= 1);

    LightCookie wind = *ecs_get(world, sun, LightCookie);
    wind.size = 4000;
    wind.offset[0] = 123.5f;
    wind.velocity[0] = 13.75f;
    wind.velocity[1] = -5.25f;
    float out[2];
    double times[3] = {36000.0, 1e7, 36000.0 + 1.0 / 60.0};
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_render_cookie_offset(&wind, times[i], out);
        test_assert(out[0] >= 0 && out[0] < 4000);
        test_assert(out[1] >= 0 && out[1] < 4000);
    }

    box_render_cookie_offset(&wind, 36000.0, out);
    test_assert(fabsf(out[0] - (float)fmod(123.5 + 13.75 * 36000.0, 4000.0)) < 1e-3f);
    test_assert(fabsf(out[1] - (float)(fmod(4.0 - 5.25 * 36000.0, 4000.0) + 4000.0)) < 1e-3f);

    stbi_image_free(before);
    stbi_image_free(after);
    ecs_fini(world);
}

/** A lit particle faces the camera and the sun above it: lit without a cookie, without direct
 * light under the black one.
 */
void LightCookie_particles_lit_by_cookie(void) {
    // lighting/light_cookie
    unsigned char *images[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_entity_t sun;
        ecs_world_t *world = cookie_world(0, &sun);
        ecs_delete_with(world, ecs_id(Box));
        ecs_entity_t black = cookie_texture(world, COOKIE_BLACK_PNG);
        if (i) {
            cookie_set(world, sun, black, 8, 0);
            ecs_modified(world, sun, LightCookie);
        }

        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, 0});
        ecs_add(world, e, ParticleEmitter);
        ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
        p->rate = 60;
        p->max = 1;
        p->lifetime = (ParticleRange){100, 100};
        p->size = (ParticleRange){2, 2};
        p->direction[0] = 0;
        p->direction[1] = 0;
        p->direction[2] = 0;
        ecs_modified(world, e, ParticleEmitter);
        ecs_set(world, e, ParticleAppearance, {.lit = 1});
        images[i] = cookie_capture(world, "particle.png", 4);
        ecs_fini(world);
    }

    test_assert(lum(images[0], 32, 32) > 0.1f);
    test_assert(lum(images[1], 32, 32) < 0.005f);

    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

/** The sun shines from behind the wall down onto the floor in front of it, so the wall facing
 * the camera has only the light the floor bounces, which the captures bake: a black moving
 * cookie on the sun leaves it unchanged.
 */
void LightCookie_bake_ignores_cookie(void) {
    // lighting/light_cookie
    unsigned char *images[2];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_add(world, scene, Scene);
        ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
            .capture_size = 16, .budget_ms = 2, .bounces = 1});
        ecs_set(world, scene, AmbientLight, {.intensity = 0});
        ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {8, 0.2f, 8});
        ecs_set(world, floor, Rgb, {255, 255, 255});
        ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, wall, Position, {0, 3, -2});
        ecs_set(world, wall, Box, {8, 6, 0.2f});
        ecs_set(world, wall, Rgb, {255, 255, 255});

        ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, sun, Rotation, {(float)GLM_PI_2 - 0.3f, 0, 0});
        ecs_set(world, sun, DirectionalLight, {.intensity = 2 * GLM_PIf});
        ecs_entity_t black = cookie_texture(world, COOKIE_BLACK_PNG);
        if (i) {
            cookie_set(world, sun, black, 8, 0);
            ecs_get_mut(world, sun, LightCookie)->velocity[0] = 3;
            ecs_modified(world, sun, LightCookie);
        }

        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 1.5f, 2});
        ecs_set(world, camera, Camera, {.fov = 40, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "bake.png",
            .resolution_scale = 1});
        int32_t frames = 0;
        while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
            ecs_progress(world, DT);
            box_render_sync(world);
            frames ++;
        }

        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
        ecs_progress(world, DT);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/bake.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int w = 0, h = 0, n = 0;
        images[i] = stbi_load(path, &w, &h, &n, 4);
        test_assert(images[i] != NULL);
        test_int(w, 64);
        test_int(h, 48);
        ecs_os_free(path);
        ecs_fini(world);
    }

    int32_t j, largest = 0;
    for (j = 0; j < 64 * 48 * 4; j ++) {
        int32_t d = abs((int32_t)images[0][j] - (int32_t)images[1][j]);
        largest = d > largest ? d : largest;
    }

    int32_t center = (24 * 64 + 32) * 4;
    test_assert(images[0][center + 1] > 20);
    test_assert(largest <= 1);

    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

/** The view covers exactly one tile of the noise with threshold 0.5 and softness 0: about half
 * of the floor is at the level of the ambient light alone, as half of the stored texels are
 * below 128.
 */
void LightCookie_noise_coverage(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(1, &sun);
    ecs_entity_t noise = cookie_texture(world, COOKIE_NOISE_PNG);
    LightCookie *c = cookie_set(world, sun, noise, 8, 0);
    c->threshold = 0.5f;
    c->softness = 0;
    c->strength = 1;
    ecs_modified(world, sun, LightCookie);
    unsigned char *px = cookie_capture(world, "coverage.png", 2);
    ecs_remove(world, sun, LightCookie);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});
    unsigned char *ambient = cookie_capture(world, "coverage_ambient.png", 2);

    int32_t i, dark = 0;
    for (i = 0; i < SIZE * SIZE; i ++) {
        dark += abs((int32_t)px[i * 4 + 1] - (int32_t)ambient[i * 4 + 1]) <= 2;
    }

    float coverage = (float)dark / (float)(SIZE * SIZE);
    test_assert(lum(ambient, 32, 32) > 0.01f);
    test_assert(coverage > 0.45f);
    test_assert(coverage < 0.55f);

    stbi_image_free(px);
    stbi_image_free(ambient);
    ecs_fini(world);
}

/** The wall under the low sun moves the plane projection 1 / tan(10 degrees), about 5.7 units,
 * per unit up the wall: a pixel, 1/8 unit, spans 2.8 periods of the stripes of size 4.
 *
 * The default remap passes the stripes through, so every pixel of the wall gets the light of the
 * mean of the cookie over it, a half. The level of a horizontal surface aliases instead.
 */
void LightCookie_steep_surface_lod(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = wall_world(&sun);
    ecs_entity_t stripes = cookie_texture(world, COOKIE_STRIPES_PNG);
    unsigned char *none = cookie_capture(world, "steep_none.png", 2);
    LightCookie *c = cookie_set(world, sun, stripes, 4, 0);
    c->height = 50;
    ecs_modified(world, sun, LightCookie);
    unsigned char *px = cookie_capture(world, "steep_stripes.png", 2);

    int32_t r, col, count = 0;
    float sum = 0, sum2 = 0, worst = 0;
    for (r = 8; r < SIZE - 8; r ++) {
        for (col = 8; col < SIZE - 8; col ++) {
            float n = lum(none, col, r);
            test_assert(n > 0.2f);
            float ratio = lum(px, col, r) / n;
            sum += ratio;
            sum2 += ratio * ratio;
            worst = glm_max(worst, fabsf(ratio - 0.5f));
            count ++;
        }
    }

    float mean = sum / (float)count;
    float variance = sum2 / (float)count - mean * mean;
    test_assert(fabsf(mean - 0.5f) < 0.05f);
    test_assert(worst < 0.12f);
    test_assert(variance < 0.002f);

    stbi_image_free(none);
    stbi_image_free(px);
    ecs_fini(world);
}

/** The light projection of the same wall reads the stripes along the light's Y axis, which is
 * 0.98 times the height on the wall: with size 16 a stripe is 4 pixels high.
 *
 * A texel is larger than a pixel, so the stripes stay sharp. Scaling the level by the stretch of
 * the plane projection would blur them toward a half.
 */
void LightCookie_light_projection_lod_unchanged(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = wall_world(&sun);
    ecs_entity_t stripes = cookie_texture(world, COOKIE_STRIPES_PNG);
    unsigned char *none = cookie_capture(world, "light_lod_none.png", 2);
    LightCookie *c = cookie_set(world, sun, stripes, 16, 0);
    c->projection = LightCookieProjectionLight;
    ecs_modified(world, sun, LightCookie);
    unsigned char *px = cookie_capture(world, "light_lod_stripes.png", 2);

    int32_t r;
    float low = 1, high = 0;
    for (r = 8; r < SIZE - 8; r ++) {
        float n = lum(none, 32, r);
        test_assert(n > 0.2f);
        float ratio = lum(px, 32, r) / n;
        low = glm_min(low, ratio);
        high = glm_max(high, ratio);
        test_assert(fabsf(lum(px, 16, r) - lum(px, 48, r)) < 0.01f);
    }

    test_assert(low < 0.1f);
    test_assert(high > 0.9f);

    stbi_image_free(none);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A perspective camera 1 unit above a wide floor looks along -Z, 5 degrees down, with a field
 * of view of 8 degrees: the top rows see the floor 1 to 3 degrees below the horizon, 20 to 55
 * units away.
 *
 * A pixel there covers 0.04 to 0.08 units across the view and 1 to 3 units along it, while the
 * stripes of size 8 repeat every half unit along Z.
 *
 * The sun is straight up and the default remap passes the stripes through. A footprint that
 * follows the pixel across the view picks the finest level, where the rows land on the stripes
 * at arbitrary phases; the footprint along the view averages them to a half in every row.
 */
void LightCookie_grazing_view_lod(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = cookie_world(0, &sun);
    ecs_delete_with(world, ecs_id(Box));

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {1000, 0.2f, 1000});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});

    ecs_entity_t camera = cookie_camera(world);
    ecs_set(world, camera, Position, {0, 1, 0});
    ecs_set(world, camera, Rotation, {-glm_rad(5), 0, 0});
    ecs_set(world, camera, Camera, {.fov = 8, .near_ = 0.1f, .far_ = 1000});

    ecs_entity_t stripes = cookie_texture(world, COOKIE_STRIPES_PNG);
    unsigned char *none = cookie_capture(world, "grazing_none.png", 2);
    LightCookie *c = cookie_set(world, sun, stripes, 8, 0);
    c->height = 50;
    ecs_modified(world, sun, LightCookie);
    unsigned char *px = cookie_capture(world, "grazing_stripes.png", 2);

    int32_t r, col, count = 0;
    float sum = 0, sum2 = 0;
    for (r = 2; r < 16; r ++) {
        for (col = 8; col < SIZE - 8; col ++) {
            float n = lum(none, col, r);
            test_assert(n > 0.02f);
            float ratio = lum(px, col, r) / n;
            sum += ratio;
            sum2 += ratio * ratio;
            count ++;
        }
    }

    float mean = sum / (float)count;
    float variance = sum2 / (float)count - mean * mean;
    test_assert(fabsf(mean - 0.5f) < 0.1f);
    test_assert(variance < 0.005f);

    stbi_image_free(none);
    stbi_image_free(px);
    ecs_fini(world);
}

/** The wall of steep_surface_lod under the sun 10 degrees up behind the camera, whose
 * horizontal direction is the wall's normal.
 *
 * A step up the wall moves the plane projection along Z, along the columns of the cookie, and a
 * step across it moves it along X: with size 8 a texel is a pixel and the columns repeat every
 * 4 pixels.
 *
 * The footprint is long only up the wall, so neighboring columns keep the contrast of the
 * stripes. The isotropic bound of the stretch, 6.8, would blur them to a half.
 */
void LightCookie_steep_surface_keeps_horizontal_detail(void) {
    // lighting/light_cookie
    ecs_entity_t sun;
    ecs_world_t *world = wall_world(&sun);
    ecs_entity_t columns = cookie_texture(world, COOKIE_COLUMNS_PNG);
    unsigned char *none = cookie_capture(world, "horizontal_none.png", 2);
    LightCookie *c = cookie_set(world, sun, columns, 8, 0);
    c->height = 50;
    ecs_modified(world, sun, LightCookie);
    unsigned char *px = cookie_capture(world, "horizontal_columns.png", 2);

    int32_t r, col, count = 0;
    float steps = 0, low = 1, high = 0;
    for (r = 8; r < SIZE - 8; r ++) {
        for (col = 8; col < SIZE - 8; col ++) {
            float n = lum(none, col, r);
            float next = lum(none, col + 1, r);
            test_assert(n > 0.2f);
            test_assert(next > 0.2f);
            float ratio = lum(px, col, r) / n;
            steps += fabsf(lum(px, col + 1, r) / next - ratio);
            low = glm_min(low, ratio);
            high = glm_max(high, ratio);
            count ++;
        }
    }

    steps /= (float)count;
    test_assert(steps > 0.25f);
    test_assert(low < 0.2f);
    test_assert(high > 0.8f);

    stbi_image_free(none);
    stbi_image_free(px);
    ecs_fini(world);
}
