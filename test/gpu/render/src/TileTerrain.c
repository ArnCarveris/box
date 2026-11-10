/**
 * @file TileTerrain.c
 * @brief Tests for drawing a TileTerrain on the GPU backend.
 *
 * A terrain turned to face the camera on the left is compared with a plain quad on the right
 * whose color is the atlas variant the dual cell under pixel 16 of row 16 should draw. The
 * atlases of gen_tile_atlases.py hold one color per variant.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void TileTerrain_corner_variant(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Scene, {.texture_blend = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32,
        .write_to_file = "tiles_corner.png", .resolution_scale = 1});

    ecs_entity_t base_tex = ecs_new(world);
    ecs_set(world, base_tex, TexturePath, {.path = "test/assets/textures/tiles_base.png"});
    ecs_entity_t variants_tex = ecs_new(world);
    ecs_set(world, variants_tex, TexturePath,
        {.path = "test/assets/textures/tiles_variants.png"});
    ecs_entity_t base = ecs_new(world);
    ecs_set(world, base, PbrTextures, {.albedo = base_tex});
    ecs_entity_t road = ecs_new(world);
    ecs_set(world, road, PbrTextures, {.albedo = variants_tex});

    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Position, {-3.46f, 0, 0});
    ecs_set(world, terrain, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, terrain, Rgb, {255, 255, 255});
    ecs_set(world, terrain, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, terrain, TileTerrain, {.size = 1,
        .kinds = BOX_VEC_VIEW(TileKind, {'.', base}, {'#', road}),
        .tiles = "..\n.#"});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {3.46f, 0, 0});
    ecs_set(world, plain, Quad, {3, 3});
    ecs_set(world, plain, Rgb, {136, 127, 128});
    ecs_set(world, plain, PbrMaterial, {.metallic = 0, .roughness = 1});

    int32_t i, c;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    test_int(stats.blend_arrays, 3);
    char *path = flecs_asprintf("%s/tiles_corner.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    test_assert(left[0] > 32);
    for (c = 0; c < 3; c ++) {
        if (abs(left[c] - right[c]) > 3) {
            printf("channel %d left %d right %d\n", c, left[c], right[c]);
        }

        test_assert(abs(left[c] - right[c]) <= 3);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_fini(world);
}

void TileTerrain_kind_order(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Scene, {.texture_blend = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32,
        .write_to_file = "tiles_order.png", .resolution_scale = 1});

    ecs_entity_t base_tex = ecs_new(world);
    ecs_set(world, base_tex, TexturePath, {.path = "test/assets/textures/tiles_base.png"});
    ecs_entity_t variants_tex = ecs_new(world);
    ecs_set(world, variants_tex, TexturePath,
        {.path = "test/assets/textures/tiles_variants.png"});
    ecs_entity_t base = ecs_new(world);
    ecs_set(world, base, PbrTextures, {.albedo = base_tex});
    ecs_entity_t grass = ecs_new(world);
    ecs_set(world, grass, PbrTextures, {.albedo = variants_tex});

    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Position, {-3.46f, 0, 0});
    ecs_set(world, terrain, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, terrain, Rgb, {255, 255, 255});
    ecs_set(world, terrain, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, terrain, TileTerrain, {.size = 1,
        .kinds = BOX_VEC_VIEW(TileKind, {'#', base}, {'.', grass}),
        .tiles = "..\n.#"});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {3.46f, 0, 0});
    ecs_set(world, plain, Quad, {3, 3});
    ecs_set(world, plain, Rgb, {120, 143, 128});
    ecs_set(world, plain, PbrMaterial, {.metallic = 0, .roughness = 1});

    int32_t i, c;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/tiles_order.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    test_assert(left[0] > 32);
    for (c = 0; c < 3; c ++) {
        if (abs(left[c] - right[c]) > 3) {
            printf("channel %d left %d right %d\n", c, left[c], right[c]);
        }

        test_assert(abs(left[c] - right[c]) <= 3);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_fini(world);
}

void TileTerrain_whole_tile(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Scene, {.texture_blend = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32,
        .write_to_file = "tiles_whole.png", .resolution_scale = 1});

    ecs_entity_t base_tex = ecs_new(world);
    ecs_set(world, base_tex, TexturePath, {.path = "test/assets/textures/tiles_base.png"});
    ecs_entity_t variants_tex = ecs_new(world);
    ecs_set(world, variants_tex, TexturePath,
        {.path = "test/assets/textures/tiles_variants.png"});
    ecs_entity_t base = ecs_new(world);
    ecs_set(world, base, PbrTextures, {.albedo = base_tex});
    ecs_entity_t road = ecs_new(world);
    ecs_set(world, road, PbrTextures, {.albedo = variants_tex});

    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Position, {-3.46f, 0, 0});
    ecs_set(world, terrain, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, terrain, Rgb, {255, 255, 255});
    ecs_set(world, terrain, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, terrain, TileTerrain, {.size = 1,
        .kinds = BOX_VEC_VIEW(TileKind, {'.', base}, {'#', road}),
        .tiles = "##\n##"});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {3.46f, 0, 0});
    ecs_set(world, plain, Quad, {3, 3});
    ecs_set(world, plain, Rgb, {248, 15, 128});
    ecs_set(world, plain, PbrMaterial, {.metallic = 0, .roughness = 1});

    int32_t i, c;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/tiles_whole.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    test_assert(left[0] > 32);
    for (c = 0; c < 3; c ++) {
        if (abs(left[c] - right[c]) > 3) {
            printf("channel %d left %d right %d\n", c, left[c], right[c]);
        }

        test_assert(abs(left[c] - right[c]) <= 3);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_fini(world);
}

void TileTerrain_extended_variant(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Scene, {.texture_blend = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32,
        .write_to_file = "tiles_extended.png", .resolution_scale = 1});

    ecs_entity_t extended_tex = ecs_new(world);
    ecs_set(world, extended_tex, TexturePath,
        {.path = "test/assets/textures/tiles_extended.png"});
    ecs_entity_t grass = ecs_new(world);
    ecs_set(world, grass, PbrTextures, {.albedo = extended_tex});

    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Position, {-3.96f, 0, 0});
    ecs_set(world, terrain, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, terrain, Rgb, {255, 255, 255});
    ecs_set(world, terrain, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, terrain, TileTerrain, {.size = 1,
        .kinds = BOX_VEC_VIEW(TileKind, {'#', grass}),
        .tiles = "###\n###"});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {3.46f, 0, 0});
    ecs_set(world, plain, Quad, {3, 3});
    ecs_set(world, plain, Rgb, {168, 40, 200});
    ecs_set(world, plain, PbrMaterial, {.metallic = 0, .roughness = 1});

    int32_t i, c;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/tiles_extended.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    test_assert(left[0] > 32);
    for (c = 0; c < 3; c ++) {
        if (abs(left[c] - right[c]) > 3) {
            printf("channel %d left %d right %d\n", c, left[c], right[c]);
        }

        test_assert(abs(left[c] - right[c]) <= 3);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_fini(world);
}

void TileTerrain_alpha_over(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, Scene, {.texture_blend = true});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, light, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32,
        .write_to_file = "tiles_alpha.png", .resolution_scale = 1});

    ecs_entity_t base_tex = ecs_new(world);
    ecs_set(world, base_tex, TexturePath, {.path = "test/assets/textures/tiles_base.png"});
    ecs_entity_t half_tex = ecs_new(world);
    ecs_set(world, half_tex, TexturePath, {.path = "test/assets/textures/tiles_half.png"});
    ecs_entity_t base = ecs_new(world);
    ecs_set(world, base, PbrTextures, {.albedo = base_tex});
    ecs_entity_t mud = ecs_new(world);
    ecs_set(world, mud, PbrTextures, {.albedo = half_tex});

    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Position, {-3.46f, 0, 0});
    ecs_set(world, terrain, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, terrain, Rgb, {255, 255, 255});
    ecs_set(world, terrain, PbrMaterial, {.metallic = 0, .roughness = 1});
    ecs_set(world, terrain, TileTerrain, {.size = 1,
        .kinds = BOX_VEC_VIEW(TileKind, {'.', base}, {'#', mud}),
        .tiles = "..\n.#"});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {3.46f, 0, 0});
    ecs_set(world, plain, Quad, {3, 3});
    ecs_set(world, plain, Rgb, {152, 51, 51});
    ecs_set(world, plain, PbrMaterial, {.metallic = 0, .roughness = 1});

    int32_t i, c;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.errors, 0);
    char *path = flecs_asprintf("%s/tiles_alpha.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    test_assert(left[0] > 32);
    for (c = 0; c < 3; c ++) {
        if (abs(left[c] - right[c]) > 3) {
            printf("channel %d left %d right %d\n", c, left[c], right[c]);
        }

        test_assert(abs(left[c] - right[c]) <= 3);
    }

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_fini(world);
}
