/**
 * @file TextureLinear.c
 * @brief Tests for sampling a TexturePath with linear set in a color layer of the GPU backend.
 *
 * A texel of a linear image is linear light: the 64 of the image reaches the capture as the
 * sRGB encoding of 64 / 255, 137, where the same texel of an sRGB image stays 64.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

void TextureLinear_emissive_layer(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/grey.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i, c;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 64;
        px[i * 4 + 1] = 64;
        px[i * 4 + 2] = 64;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t srgb = ecs_new(world);
    ecs_set(world, srgb, TexturePath, {.path = file});
    ecs_entity_t linear = ecs_new(world);
    ecs_set(world, linear, TexturePath, {.path = file, .linear = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    for (i = 0; i < 2; i ++) {
        ecs_entity_t quad = ecs_new(world);
        ecs_set(world, quad, Position, {i ? 3.0f : -3.0f, 0, 0});
        ecs_set(world, quad, Quad, {6, 6});
        ecs_set(world, quad, Rgb, {0, 0, 0});
        ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
        ecs_set(world, quad, PbrTextures, {.emissive = i ? linear : srgb});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "linear.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/linear.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 12) * 4];
    unsigned char *right = &img[(16 * 64 + 52) * 4];
    for (c = 0; c < 3; c ++) {
        test_assert(abs(left[c] - 64) <= 3);
        test_assert(abs(right[c] - 137) <= 3);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}
