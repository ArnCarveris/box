/**
 * @file TextureScroll.c
 * @brief Tests for how a TextureScroll moves the textures of a mesh, checked on PNG captures.
 *
 * The cases look at a 4x4 quad facing the camera, 6 units in front of it, in a 128x96 capture
 * where a unit is about 13.9 pixels. The U direction of a Quad runs along +x, so a growing U
 * offset moves the texture toward -x in the image.
 *
 * Every frame advances the world by SCROLL_DT, so the time between two captures is known.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "draw.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Width of the captures */
#define SCROLL_W (128)

/** Height of the captures */
#define SCROLL_H (96)

/** Seconds every frame of the cases advances the world */
#define SCROLL_DT (1.0f / 60.0f)

/** Pixels of one unit at the quad: the capture height over the height the camera sees */
#define SCROLL_UNIT ((float)SCROLL_H / (12.0f * tanf(GLM_PIf / 6.0f)))

/** Create the world of the cases: a camera 6 units in front of the origin, no ambient light and
 * a directional light of intensity 0, so there is no default sun, in a scene with
 * Scene.texture_scroll.
 *
 * @param lit Whether the view is lit
 * @return The world
 */
static ecs_world_t* scroll_world(
    bool lit)
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = lit});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_scroll = true});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, DirectionalLight, {.intensity = 0});

    return world;
}

/** Advance the world by frames of SCROLL_DT.
 *
 * @param world The world
 * @param frames Frames
 */
static void scroll_frames(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, SCROLL_DT);
        box_render_sync(world);
    }
}

/** Write an image into the temporary folder of the world and load it as a texture.
 *
 * @param world The world
 * @param name File name
 * @param px RGBA pixels, size by size
 * @param size Width and height
 * @return The texture, loaded
 */
static ecs_entity_t scroll_texture(
    ecs_world_t *world,
    const char *name,
    const uint8_t *px,
    int32_t size)
{
    char *file = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    test_assert(stbi_write_png(file, size, size, 4, px, size * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        scroll_frames(world, 1);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    ecs_os_free(file);

    return tex;
}

/** Create a texture whose left half (along U) is black and right half white.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t scroll_halves(
    ecs_world_t *world)
{
    uint8_t px[32 * 32 * 4];
    int32_t x, y;
    for (y = 0; y < 32; y ++) {
        for (x = 0; x < 32; x ++) {
            uint8_t *p = &px[(y * 32 + x) * 4];
            p[0] = p[1] = p[2] = x < 16 ? 0 : 255;
            p[3] = 255;
        }
    }

    return scroll_texture(world, "scroll_halves.png", px, 32);
}

/** Create a tangent space normal map of bumps along U, four per texture width.
 *
 * @param world The world
 * @return The texture
 */
static ecs_entity_t scroll_bumps(
    ecs_world_t *world)
{
    uint8_t px[32 * 32 * 4];
    int32_t x, y;
    for (y = 0; y < 32; y ++) {
        for (x = 0; x < 32; x ++) {
            vec3 n = { 0.8f * sinf((float)x * GLM_PIf / 4.0f), 0.0f, 1.0f };
            glm_vec3_normalize(n);
            uint8_t *p = &px[(y * 32 + x) * 4];
            p[0] = (uint8_t)roundf((n[0] * 0.5f + 0.5f) * 255.0f);
            p[1] = (uint8_t)roundf((n[1] * 0.5f + 0.5f) * 255.0f);
            p[2] = (uint8_t)roundf((n[2] * 0.5f + 0.5f) * 255.0f);
            p[3] = 255;
        }
    }

    return scroll_texture(world, "scroll_bumps.png", px, 32);
}

/** Render two frames into a 128x96 PNG and load it.
 *
 * @param world The world
 * @param name File name of the capture
 * @return The pixels, RGBA, freed with stbi_image_free
 */
static unsigned char* scroll_render(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = SCROLL_W, .height = SCROLL_H,
        .write_to_file = ECS_CONST_CAST(char*, name), .resolution_scale = 1});
    scroll_frames(world, 2);
    ecs_delete(world, e);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, SCROLL_W);
    test_int(h, SCROLL_H);

    return px;
}

/** Sum of the channels of a pixel of a capture.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row
 * @return Red plus green plus blue
 */
static int scroll_sum(
    const unsigned char *px,
    int x,
    int y)
{
    const unsigned char *p = &px[(y * SCROLL_W + x) * 4];

    return p[0] + p[1] + p[2];
}

/** Find the columns of the middle row where a capture turns from dark to bright, inside the
 * quad.
 *
 * @param px The pixels
 * @param out Receives the columns
 * @param max Capacity of out
 * @return Number of columns found
 */
static int scroll_rises(
    const unsigned char *px,
    int *out,
    int max)
{
    int x, count = 0;
    for (x = 40; x < 88 && count < max; x ++) {
        if (scroll_sum(px, x - 1, 48) < 384 && scroll_sum(px, x, 48) >= 384) {
            out[count ++] = x;
        }
    }

    return count;
}

/** Add the 4x4 quad of the cases on the origin, white, matte, with a TextureTransform scale of
 * 2, so the texture repeats twice across it.
 *
 * @param world The world
 * @return The quad
 */
static ecs_entity_t scroll_quad(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Quad, {4, 4});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, e, TextureTransform, {2, 2, 0, 0});

    return e;
}

/** Check that every rise of a capture moved by a number of pixels toward -x in a later one,
 * modulo the period of the texture across the quad.
 *
 * @param a The first capture
 * @param b The later capture
 * @param shift Pixels the texture moved toward -x
 */
static void scroll_expect_shift(
    const unsigned char *a,
    const unsigned char *b,
    float shift)
{
    float period = 2.0f * SCROLL_UNIT;
    int ra[8], rb[8], i, j;
    int na = scroll_rises(a, ra, 8);
    int nb = scroll_rises(b, rb, 8);
    test_assert(na >= 1);
    test_assert(nb >= 1);
    for (i = 0; i < na; i ++) {
        float best = 1e9f;
        for (j = 0; j < nb; j ++) {
            float d = (float)(ra[i] - rb[j]) - shift;
            d -= period * roundf(d / period);
            best = fabsf(d) < best ? fabsf(d) : best;
        }

        test_assert(best <= 1.5f);
    }
}

void TextureScroll_shifts_checker(void) {
    // materials/texture_scroll
    ecs_world_t *world = scroll_world(false);
    ecs_entity_t tex = scroll_halves(world);
    ecs_entity_t quad = scroll_quad(world);
    ecs_set(world, quad, PbrTextures, {.albedo = tex});
    ecs_set(world, quad, TextureScroll, {.velocity = {0.5f, 0}});
    unsigned char *a = scroll_render(world, "scroll_a.png");
    scroll_frames(world, 18);
    unsigned char *b = scroll_render(world, "scroll_b.png");

    /* 20 frames at 0.5 texture widths per second move a texture width of half the quad by a
     * sixth of it */
    float shift = 0.5f * 20.0f * SCROLL_DT * 2.0f * SCROLL_UNIT;
    test_assert(memcmp(a, b, SCROLL_W * SCROLL_H * 4) != 0);
    scroll_expect_shift(a, b, shift);
    stbi_image_free(a);
    stbi_image_free(b);

    ecs_remove(world, quad, TextureScroll);
    scroll_frames(world, 2);
    a = scroll_render(world, "scroll_still_a.png");
    scroll_frames(world, 18);
    b = scroll_render(world, "scroll_still_b.png");
    test_int(memcmp(a, b, SCROLL_W * SCROLL_H * 4), 0);
    stbi_image_free(a);
    stbi_image_free(b);

    ecs_fini(world);
}

void TextureScroll_adds_to_texture_transform(void) {
    // materials/texture_scroll
    ecs_world_t *world = scroll_world(false);
    ecs_entity_t tex = scroll_halves(world);
    ecs_entity_t quad = scroll_quad(world);
    ecs_set(world, quad, PbrTextures, {.albedo = tex});
    ecs_set(world, quad, TextureScroll, {.velocity = {0.5f, 0}});
    unsigned char *a = scroll_render(world, "scroll_offset_a.png");

    /* A quarter of a texture width more offset during 20 frames: 1/6 + 1/4 of it */
    ecs_set(world, quad, TextureTransform, {2, 2, 0.25f, 0});
    scroll_frames(world, 18);
    unsigned char *b = scroll_render(world, "scroll_offset_b.png");
    float shift = (0.5f * 20.0f * SCROLL_DT + 0.25f) * 2.0f * SCROLL_UNIT;
    scroll_expect_shift(a, b, shift);
    stbi_image_free(a);
    stbi_image_free(b);

    ecs_fini(world);
}

/** Render a lit or unlit quad with the halves as albedo and the bumps as normal map, whose
 * normal map scrolls, twice 20 frames apart.
 *
 * @param lit Whether the view is lit
 * @param a Receives the first capture
 * @param b Receives the later capture
 */
static void scroll_normal_pair(
    bool lit,
    unsigned char **a,
    unsigned char **b)
{
    ecs_world_t *world = scroll_world(lit);
    ecs_entity_t halves = scroll_halves(world);
    ecs_entity_t bumps = scroll_bumps(world);
    ecs_entity_t quad = scroll_quad(world);
    ecs_set(world, quad, PbrTextures, {.albedo = halves, .normal = bumps});
    ecs_set(world, quad, TextureScroll, {.normal_velocity = {0.25f, 0}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 3});
    ecs_set(world, light, PointLight, {.intensity = 20, .range = 20});
    *a = scroll_render(world, lit ? "scroll_lit_a.png" : "scroll_unlit_a.png");
    scroll_frames(world, 18);
    *b = scroll_render(world, lit ? "scroll_lit_b.png" : "scroll_unlit_b.png");
    ecs_fini(world);
}

void TextureScroll_normal_velocity_moves_normal_only(void) {
    // materials/texture_scroll
    unsigned char *a, *b;
    scroll_normal_pair(false, &a, &b);
    int ra[8];
    test_assert(scroll_rises(a, ra, 8) >= 1);
    test_int(memcmp(a, b, SCROLL_W * SCROLL_H * 4), 0);
    stbi_image_free(a);
    stbi_image_free(b);

    scroll_normal_pair(true, &a, &b);
    int x, changed = 0;
    for (x = 40; x < 88; x ++) {
        changed += abs(scroll_sum(a, x, 48) - scroll_sum(b, x, 48)) > 12;
    }

    test_assert(changed >= 4);
    stbi_image_free(a);
    stbi_image_free(b);
}

void TextureScroll_ignored_by_2d(void) {
    // materials/texture_scroll
    ecs_world_t *world = scroll_world(false);
    ecs_entity_t tex = scroll_halves(world);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {4, 4});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, PbrTextures, {.albedo = tex});
    ecs_set(world, e, TextureScroll, {.velocity = {0.5f, 0}});
    unsigned char *a = scroll_render(world, "scroll_2d_a.png");
    scroll_frames(world, 18);
    unsigned char *b = scroll_render(world, "scroll_2d_b.png");
    test_int(memcmp(a, b, SCROLL_W * SCROLL_H * 4), 0);
    stbi_image_free(a);
    stbi_image_free(b);

    ecs_fini(world);
}

void TextureScroll_flag_off_draws_without(void) {
    // materials/texture_scroll
    ecs_world_t *world = scroll_world(true);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.texture_scroll = false});
    ecs_entity_t halves = scroll_halves(world);
    ecs_entity_t bumps = scroll_bumps(world);
    ecs_entity_t quad = scroll_quad(world);
    ecs_set(world, quad, PbrTextures, {.albedo = halves, .normal = bumps});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 3});
    ecs_set(world, light, PointLight, {.intensity = 20, .range = 20});
    unsigned char *without = scroll_render(world, "scroll_off_without.png");

    /* A scene without Scene.texture_scroll draws the shape as without its TextureScroll: no
     * UV scroll and no normal scroll, however many frames pass */
    ecs_set(world, quad, TextureScroll, {.velocity = {0.5f, 0}, .normal_velocity = {0.25f, 0}});
    scroll_frames(world, 2);
    unsigned char *a = scroll_render(world, "scroll_off_a.png");
    scroll_frames(world, 18);
    unsigned char *b = scroll_render(world, "scroll_off_b.png");
    int ra[8];
    test_assert(scroll_rises(a, ra, 8) >= 1);
    test_int(memcmp(a, without, SCROLL_W * SCROLL_H * 4), 0);
    test_int(memcmp(a, b, SCROLL_W * SCROLL_H * 4), 0);

    /* No material is packed per frame for it */
    box_render_draws_stats_t stats;
    scroll_frames(world, 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.material_bytes, 0);
    stbi_image_free(without);
    stbi_image_free(a);
    stbi_image_free(b);

    ecs_fini(world);
}
