/**
 * @file Draws.c
 * @brief Tests that shapes draw with their transform and color, checked on PNG captures.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Draws_transform_and_color(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "xf.png"});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {-2, 1, 0});
    ecs_set(world, a, Box, {2, 1, 1});
    ecs_set(world, a, Rgb, {0, 0, 0});
    ecs_set(world, a, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, a, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {2, -1, 0});
    ecs_set(world, b, Scale, {1, 2, 1});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_set(world, b, Rgb, {0, 0, 0});
    ecs_set(world, b, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, b, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/xf.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *a_left = &px[(12 * 32 + 4) * 4];
    unsigned char *a_right = &px[(12 * 32 + 11) * 4];
    unsigned char *a_outside = &px[(12 * 32 + 2) * 4];
    unsigned char *a_above = &px[(9 * 32 + 8) * 4];
    test_assert(a_left[0] > 200 && a_left[1] < 40);
    test_assert(a_right[0] > 200 && a_right[1] < 40);
    test_assert(a_outside[0] < 40);
    test_assert(a_above[0] < 40);
    unsigned char *b_low = &px[(23 * 32 + 24) * 4];
    unsigned char *b_high = &px[(17 * 32 + 24) * 4];
    unsigned char *b_side = &px[(20 * 32 + 27) * 4];
    test_assert(b_low[1] > 200 && b_low[0] < 40);
    test_assert(b_high[1] > 200 && b_high[0] < 40);
    test_assert(b_side[1] < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Draws_replaced_shape_renders_new_mesh(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "swap.png"});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {2, 2, 2});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/swap.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(12 * 32 + 19) * 4] > 200);
    stbi_image_free(px);

    ecs_delete(world, e);
    ecs_progress(world, 0);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Sphere, {.segments = 24, .smooth = true, .radius = 1});
    ecs_set(world, s, Rgb, {0, 0, 0});
    ecs_set(world, s, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, s, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(12 * 32 + 19) * 4] < 40);
    test_assert(px[(16 * 32 + 16) * 4] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Draws_many_instances_all_drawn(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 16});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .write_to_file = "many.png"});
    int32_t i;
    for (i = 0; i < 256; i ++) {
        if (i == 64) {
            ecs_progress(world, 0);
            ecs_progress(world, 0);
        }

        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {-7.5f + (float)(i % 16), -7.5f + (float)(i / 16), 0});
        ecs_set(world, e, Box, {0.5f, 0.5f, 0.5f});
        ecs_set(world, e, Rgb, {0, 0, 0});
        ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 0, 0}});
    }

    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/many.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t missing = 0;
    for (i = 0; i < 256; i ++) {
        int32_t col = 2 + 4 * (i % 16);
        int32_t row = 61 - 4 * (i / 16);
        missing += px[(row * 64 + col) * 4] < 200;
    }

    test_int(missing, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
