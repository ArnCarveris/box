/**
 * @file TextureSingleChannel.c
 * @brief Tests for sampling a TexturePath with single_channel set on the GPU backend.
 *
 * A single channel texture is R8Unorm: a layer samples it as (r, 0, 0, 1) with r as stored,
 * so an albedo of the red 128 of an RGB image renders like an RGBA albedo of (128, 0, 0)
 * loaded as linear, whose red the color layer also reads as 128 / 255.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** RGB PNG of 16 by 16 pixels of (128, 200, 50) */
#define MASK_RGB_PNG "test/assets/textures/mask_rgb.png"

/** RGB PNG of 16 by 16 pixels of (128, 0, 0) */
#define MASK_RED_PNG "test/assets/textures/mask_red.png"

/** Grayscale PNG of 4 by 2 pixels */
#define GRAY_PNG "test/assets/textures/gray.png"

/** Create a headless world.
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

void TextureSingleChannel_albedo_red(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    int32_t i, c;
    ecs_entity_t single = ecs_new(world);
    ecs_set(world, single, TexturePath, {.path = MASK_RGB_PNG, .single_channel = true});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, TexturePath, {.path = MASK_RED_PNG, .linear = true});
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
        ecs_set(world, quad, Rgb, {255, 255, 255});
        ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, quad, PbrTextures, {.albedo = i ? red : single});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "single.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, single, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, single, Texture)->width, 16);
    test_int(ecs_get(world, single, Texture)->height, 16);

    char *path = flecs_asprintf("%s/single.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 12) * 4];
    unsigned char *right = &img[(16 * 64 + 52) * 4];
    test_assert(left[0] > 8);
    test_assert(left[1] <= 3);
    test_assert(left[2] <= 3);
    for (c = 0; c < 3; c ++) {
        test_assert(abs(left[c] - right[c]) <= 3);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_fini(world);
}

void TextureSingleChannel_quality_caps(void) {
    // materials/textures
    ecs_world_t *world = world_new();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .texture_quality = (TextureQuality)2});
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = GRAY_PNG, .single_channel = true});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, tex, Texture)->width, 2);
    test_int(ecs_get(world, tex, Texture)->height, 1);

    ecs_fini(world);
}
