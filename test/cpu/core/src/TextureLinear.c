/**
 * @file TextureLinear.c
 * @brief Tests for TexturePath.linear: images whose color channels hold linear light are
 * encoded to sRGB on load, so the color layers that decode sRGB read the stored values.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <math.h>
#include "texture.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Create a headless world, which has a temporary folder and waits for its texture loads.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Get a file in the temporary folder of a world.
 *
 * @param world The world
 * @param name Name of the file
 * @return The path, freed by the caller
 */
static char* tmp_file(
    ecs_world_t *world,
    const char *name)
{
    return flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, name);
}

/** Write a PNG of two pixels: an opaque one and a translucent one.
 *
 * @param path The file
 */
static void write_png(
    const char *path)
{
    uint8_t px[8] = { 128, 0, 255, 255, 64, 200, 10, 128 };
    test_assert(stbi_write_png(path, 2, 1, 4, px, 8) != 0);
}

/** Write an SVG of 4 by 4 pixels filled with the grey 128.
 *
 * @param path The file
 */
static void write_svg(
    const char *path)
{
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\" "
        "viewBox=\"0 0 4 4\"><rect width=\"4\" height=\"4\" fill=\"#808080\"/></svg>", f);
    fclose(f);
}

/** Run frames until a texture is no longer loading.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The status
 */
static const TextureStatus* wait_texture(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    int32_t frames = 0;
    ecs_progress(world, 0);
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    return ecs_get(world, tex, TextureStatus);
}

/** Get the decoded pixels of a texture entity.
 *
 * @param world The world
 * @param tex The texture entity
 * @return The RGBA8 rows, NULL without an image
 */
static const uint8_t* pixels_of(
    ecs_world_t *world,
    ecs_entity_t tex)
{
    box_textures_t *t = box_textures_get(world);
    test_assert(t != NULL);
    box_texture_t *entry = ecs_map_get_deref(&t->textures, box_texture_t, tex);
    test_assert(entry != NULL);

    return entry->pixels;
}

/** Decode an sRGB byte to linear light.
 *
 * @param value The byte
 * @return The linear value from 0 to 1
 */
static double srgb_decode(
    uint8_t value)
{
    double c = (double)value / 255.0;

    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

void TextureLinear_encode_table(void) {
    // oreelse/linear_colors
    uint8_t px[256 * 4];
    int32_t i;
    for (i = 0; i < 256; i ++) {
        px[i * 4 + 0] = (uint8_t)i;
        px[i * 4 + 1] = (uint8_t)i;
        px[i * 4 + 2] = (uint8_t)i;
        px[i * 4 + 3] = (uint8_t)(255 - i);
    }

    box_texture_encode_linear(px, 256);
    test_int(px[0], 0);
    test_int(px[1 * 4], 13);
    test_int(px[10 * 4], 56);
    test_int(px[64 * 4], 137);
    test_int(px[128 * 4], 188);
    test_int(px[200 * 4], 229);
    test_int(px[255 * 4], 255);
    for (i = 0; i < 256; i ++) {
        test_int(px[i * 4 + 1], px[i * 4]);
        test_int(px[i * 4 + 2], px[i * 4]);
        test_int(px[i * 4 + 3], 255 - i);
        test_assert(fabs(srgb_decode(px[i * 4]) - (double)i / 255.0) < 0.0045);
        if (i) {
            test_assert(px[i * 4] >= px[(i - 1) * 4]);
        }
    }
}

void TextureLinear_default_off(void) {
    // oreelse/linear_colors
    ecs_world_t *world = world_new();
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, { .path = "test/integration/visual/assets/checker.png" });
    test_bool(ecs_get(world, tex, TexturePath)->linear, false);

    const char *code =
        "using box.texture\n"
        "plain { Path: {path: \"test/integration/visual/assets/checker.png\"} }\n"
        "lin { Path: {path: \"test/integration/visual/assets/checker.png\", "
        "linear: true} }\n";
    test_assert(ecs_script_run(world, "linear", code, NULL) == 0);
    test_bool(ecs_get(world, ecs_lookup(world, "plain"), TexturePath)->linear, false);
    test_bool(ecs_get(world, ecs_lookup(world, "lin"), TexturePath)->linear, true);

    ecs_fini(world);
}

void TextureLinear_png_encoded(void) {
    // oreelse/linear_colors
    ecs_world_t *world = world_new();
    char *file = tmp_file(world, "linear.png");
    write_png(file);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, { .path = file, .linear = true });
    test_int(wait_texture(world, tex)->state, TextureReady);
    const uint8_t *px = pixels_of(world, tex);
    test_assert(px != NULL);
    test_int(px[0], 188);
    test_int(px[1], 0);
    test_int(px[2], 255);
    test_int(px[3], 255);
    test_int(px[4], 137);
    test_int(px[5], 229);
    test_int(px[6], 56);
    test_int(px[7], 128);

    ecs_os_free(file);
    ecs_fini(world);
}

void TextureLinear_png_off_kept(void) {
    // oreelse/linear_colors
    ecs_world_t *world = world_new();
    char *file = tmp_file(world, "kept.png");
    write_png(file);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, { .path = file });
    test_int(wait_texture(world, tex)->state, TextureReady);
    const uint8_t *px = pixels_of(world, tex);
    test_assert(px != NULL);
    test_int(px[0], 128);
    test_int(px[1], 0);
    test_int(px[2], 255);
    test_int(px[3], 255);
    test_int(px[4], 64);
    test_int(px[5], 200);
    test_int(px[6], 10);
    test_int(px[7], 128);

    ecs_os_free(file);
    ecs_fini(world);
}

void TextureLinear_svg_encoded(void) {
    // oreelse/linear_colors
    ecs_world_t *world = world_new();
    char *file = tmp_file(world, "linear.svg");
    write_svg(file);

    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, TexturePath, { .path = file, .width = 4, .height = 4 });
    ecs_entity_t lin = ecs_new(world);
    ecs_set(world, lin, TexturePath, {
        .path = file, .width = 4, .height = 4, .linear = true });
    test_int(wait_texture(world, plain)->state, TextureReady);
    test_int(wait_texture(world, lin)->state, TextureReady);

    const uint8_t *a = pixels_of(world, plain);
    const uint8_t *b = pixels_of(world, lin);
    test_assert(a != NULL && b != NULL);
    int32_t i;
    for (i = 0; i < 16; i ++) {
        test_int(a[i * 4], 128);
        test_int(a[i * 4 + 3], 255);
        test_int(b[i * 4], 188);
        test_int(b[i * 4 + 1], 188);
        test_int(b[i * 4 + 2], 188);
        test_int(b[i * 4 + 3], 255);
    }

    ecs_os_free(file);
    ecs_fini(world);
}

void TextureLinear_change_reloads(void) {
    // oreelse/linear_colors
    ecs_world_t *world = world_new();
    char *file = tmp_file(world, "toggle.png");
    write_png(file);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, { .path = file });
    test_int(wait_texture(world, tex)->state, TextureReady);
    int32_t revision = ecs_get(world, tex, TextureStatus)->revision;
    test_int(pixels_of(world, tex)[0], 128);

    ecs_set(world, tex, TexturePath, { .path = file, .linear = true });
    test_int(wait_texture(world, tex)->state, TextureReady);
    test_int(ecs_get(world, tex, TextureStatus)->revision, revision + 1);
    test_int(pixels_of(world, tex)[0], 188);

    ecs_os_free(file);
    ecs_fini(world);
}
