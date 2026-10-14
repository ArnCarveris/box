/**
 * @file Texture.c
 * @brief Tests for loading textures from files and sampling them in PBR texture layers.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

void Texture_load_png(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/load.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[8 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(file, 8, 4, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_assert(status != NULL);
    test_int(status->state, TextureLoading);
    test_int(status->revision, 0);
    test_assert(!ecs_has(world, tex, Texture));

    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureReady);
    test_int(status->revision, 1);
    test_assert(status->error == NULL);
    const Texture *t = ecs_get(world, tex, Texture);
    test_assert(t != NULL);
    test_int(t->width, 8);
    test_int(t->height, 4);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_requested_size(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/size.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[8 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(file, 8, 4, 4, px, 8 * 4) != 0);

    ecs_entity_t both = ecs_new(world);
    ecs_set(world, both, TexturePath, {.path = file, .width = 5, .height = 7});
    ecs_entity_t width = ecs_new(world);
    ecs_set(world, width, TexturePath, {.path = file, .width = 16});
    ecs_entity_t height = ecs_new(world);
    ecs_set(world, height, TexturePath, {.path = file, .height = 2});
    int32_t frames = 0;
    while ((ecs_get(world, both, TextureStatus)->state == TextureLoading ||
        ecs_get(world, width, TextureStatus)->state == TextureLoading ||
        ecs_get(world, height, TextureStatus)->state == TextureLoading) && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, both, Texture)->width, 5);
    test_int(ecs_get(world, both, Texture)->height, 7);
    test_int(ecs_get(world, width, Texture)->width, 16);
    test_int(ecs_get(world, width, Texture)->height, 8);
    test_int(ecs_get(world, height, Texture)->width, 4);
    test_int(ecs_get(world, height, Texture)->height, 2);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_load_svg(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/icon.svg", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(file, "w");
    test_assert(f != NULL);
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
        "<rect width=\"40\" height=\"20\" fill=\"#ff0000\"/></svg>", f);
    fclose(f);

    ecs_entity_t native = ecs_new(world);
    ecs_set(world, native, TexturePath, {.path = file});
    ecs_entity_t scaled = ecs_new(world);
    ecs_set(world, scaled, TexturePath, {.path = file, .width = 200});
    int32_t frames = 0;
    while ((ecs_get(world, native, TextureStatus)->state == TextureLoading ||
        ecs_get(world, scaled, TextureStatus)->state == TextureLoading) && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, native, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, native, Texture)->width, 40);
    test_int(ecs_get(world, native, Texture)->height, 20);
    test_int(ecs_get(world, scaled, TextureStatus)->state, TextureReady);
    test_int(ecs_get(world, scaled, Texture)->width, 200);
    test_int(ecs_get(world, scaled, Texture)->height, 100);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_missing_file_fails(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_log_set_level(-4);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = "no/such/texture.png"});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureFailed);
    test_int(status->revision, 0);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "no/such/texture.png") != NULL);
    test_assert(!ecs_has(world, tex, Texture));

    ecs_fini(world);
}

void Texture_invalid_image_fails(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/broken.png", tmp);
    char *svg = flecs_asprintf("%s/broken.svg", tmp);
    FILE *f = fopen(png, "w");
    test_assert(f != NULL);
    fputs("this is not an image", f);
    fclose(f);
    f = fopen(svg, "w");
    test_assert(f != NULL);
    fputs("<svg width=", f);
    fclose(f);

    ecs_log_set_level(-4);
    ecs_entity_t raster = ecs_new(world);
    ecs_set(world, raster, TexturePath, {.path = png});
    ecs_entity_t vector = ecs_new(world);
    ecs_set(world, vector, TexturePath, {.path = svg});
    int32_t frames = 0;
    while ((ecs_get(world, raster, TextureStatus)->state == TextureLoading ||
        ecs_get(world, vector, TextureStatus)->state == TextureLoading) && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    const TextureStatus *status = ecs_get(world, raster, TextureStatus);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "broken.png") != NULL);
    status = ecs_get(world, vector, TextureStatus);
    test_int(status->state, TextureFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "broken.svg") != NULL);
    test_assert(!ecs_has(world, raster, Texture));
    test_assert(!ecs_has(world, vector, Texture));

    ecs_os_free(png);
    ecs_os_free(svg);
    ecs_fini(world);
}

void Texture_reload_on_path_change(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *first = flecs_asprintf("%s/first.png", tmp);
    char *second = flecs_asprintf("%s/second.png", tmp);
    uint8_t px[8 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(first, 8, 4, 4, px, 8 * 4) != 0);
    test_assert(stbi_write_png(second, 2, 2, 4, px, 2 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = first});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    test_int(ecs_get(world, tex, Texture)->width, 8);

    ecs_set(world, tex, TexturePath, {.path = second});
    test_int(ecs_get(world, tex, TextureStatus)->state, TextureLoading);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_int(ecs_get(world, tex, Texture)->width, 2);
    test_int(ecs_get(world, tex, Texture)->height, 2);

    ecs_os_free(first);
    ecs_os_free(second);
    ecs_fini(world);
}

void Texture_failed_reload_keeps_image(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/keep.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[8 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(file, 8, 4, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, TextureStatus)->state, TextureReady);

    ecs_log_set_level(-4);
    ecs_set(world, tex, TexturePath, {.path = "no/such/file.png"});
    frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    const TextureStatus *status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureFailed);
    test_int(status->revision, 1);
    test_assert(strstr(status->error, "no/such/file.png") != NULL);
    test_int(ecs_get(world, tex, Texture)->width, 8);
    test_int(ecs_get(world, tex, Texture)->height, 4);

    ecs_set(world, tex, TexturePath, {.path = file});
    frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    status = ecs_get(world, tex, TextureStatus);
    test_int(status->state, TextureReady);
    test_int(status->revision, 2);
    test_assert(status->error == NULL);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_remove_path(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/remove.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t px[4 * 4 * 4];
    memset(px, 200, sizeof(px));
    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t pending = ecs_new(world);
    ecs_set(world, pending, TexturePath, {.path = file});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_assert(ecs_has(world, tex, Texture));
    ecs_remove(world, tex, TexturePath);
    test_assert(!ecs_has(world, tex, Texture));
    test_assert(!ecs_has(world, tex, TextureStatus));

    ecs_set(world, pending, TexturePath, {.path = file, .width = 2});
    ecs_remove(world, pending, TexturePath);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, pending, TextureStatus));
    test_assert(!ecs_has(world, tex, Texture));

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_texture_quality_caps_size(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *file = flecs_asprintf("%s/large.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t *px = ecs_os_calloc(1024 * 256 * 4);
    test_assert(stbi_write_png(file, 1024, 256, 4, px, 1024 * 4) != 0);
    ecs_os_free(px);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 32, .height = 32, .texture_quality = TextureQualityLow,
        .resolution_scale = 1});
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t small = ecs_new(world);
    ecs_set(world, small, TexturePath, {.path = file, .width = 128});
    int32_t frames = 0;
    while ((ecs_get(world, tex, TextureStatus)->state == TextureLoading ||
        ecs_get(world, small, TextureStatus)->state == TextureLoading) && frames < 100)
    {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, Texture)->width, 512);
    test_int(ecs_get(world, tex, Texture)->height, 128);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 1);
    test_int(ecs_get(world, small, Texture)->width, 128);
    test_int(ecs_get(world, small, Texture)->height, 32);

    Surface *surface = ecs_get_mut(world, s, Surface);
    surface->texture_quality = TextureQualityMedium;
    ecs_modified(world, s, Surface);
    ecs_progress(world, 0);
    frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(ecs_get(world, tex, Texture)->width, 1024);
    test_int(ecs_get(world, tex, Texture)->height, 256);
    test_int(ecs_get(world, tex, TextureStatus)->revision, 2);
    test_int(ecs_get(world, small, Texture)->width, 128);

    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_emissive_layer(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {0, 3.2f, 0});
    ecs_set(world, plain, Quad, {12, 0.4f});
    ecs_set(world, plain, Rgb, {0, 0, 0});
    ecs_set(world, plain, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, plain, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "emissive.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/emissive.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 12) * 4];
    unsigned char *right = &img[(16 * 64 + 52) * 4];
    unsigned char *top = &img[(1 * 64 + 32) * 4];
    test_assert(left[0] > 240);
    test_assert(left[1] < 20);
    test_assert(right[0] < 20);
    test_assert(right[1] > 240);
    test_assert(top[2] > 240);
    test_assert(top[0] < 20);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_albedo_layer(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/blue.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 20;
        px[i * 4 + 1] = 40;
        px[i * 4 + 2] = 255;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t textured = ecs_new(world);
    ecs_set(world, textured, Position, {-3, 0, 0});
    ecs_set(world, textured, Quad, {5, 5});
    ecs_set(world, textured, Rgb, {255, 255, 255});
    ecs_set(world, textured, PbrMaterial, {.roughness = 1});
    ecs_set(world, textured, PbrTextures, {.albedo = tex});
    ecs_entity_t tinted = ecs_new(world);
    ecs_set(world, tinted, Position, {3, 0, 0});
    ecs_set(world, tinted, Quad, {5, 5});
    ecs_set(world, tinted, Rgb, {255, 0, 0});
    ecs_set(world, tinted, PbrMaterial, {.roughness = 1});
    ecs_set(world, tinted, PbrTextures, {.albedo = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "albedo.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/albedo.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 18) * 4];
    unsigned char *right = &img[(16 * 64 + 46) * 4];
    test_assert(left[2] > 80);
    test_assert(left[2] > left[0] + 50);
    test_assert(left[2] > left[1] + 40);
    test_assert(right[0] < 40);
    test_assert(right[1] < 40);
    test_assert(right[2] < 40);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_wrap_repeat(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_set(world, quad, TextureTransform, {.scale_x = 2, .scale_y = 1});
    ecs_set(world, quad, TextureWrap, {WrapRepeat});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "repeat.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/repeat.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 10) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 24) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 1] < 20);
    test_assert(img[(16 * 64 + 52) * 4 + 1] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_wrap_clamp(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_set(world, quad, TextureTransform, {.scale_x = 2, .scale_y = 1});
    ecs_set(world, quad, TextureWrap, {WrapClamp});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "clamp.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/clamp.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 10) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 24) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 0] < 20);
    test_assert(img[(16 * 64 + 52) * 4 + 1] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_wrap_mirror(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_set(world, quad, TextureTransform, {.scale_x = 2, .scale_y = 1});
    ecs_set(world, quad, TextureWrap, {WrapMirror});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "mirror.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/mirror.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 10) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 24) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 38) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 52) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 52) * 4 + 1] < 20);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_transform_change(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        bool left = (i % 8) < 4;
        px[i * 4 + 0] = left ? 255 : 0;
        px[i * 4 + 1] = left ? 0 : 255;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t still = ecs_new(world);
    ecs_set(world, still, Position, {0, 1.6f, 0});
    ecs_set(world, still, Quad, {12, 3});
    ecs_set(world, still, Rgb, {0, 0, 0});
    ecs_set(world, still, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, still, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, still, PbrTextures, {.emissive = tex});
    ecs_entity_t moving = ecs_new(world);
    ecs_add(world, moving, DynamicMaterial);
    ecs_set(world, moving, Position, {0, -1.6f, 0});
    ecs_set(world, moving, Quad, {12, 3});
    ecs_set(world, moving, Rgb, {0, 0, 0});
    ecs_set(world, moving, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, moving, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, moving, PbrTextures, {.emissive = tex});
    ecs_set(world, moving, TextureTransform, {.scale_x = 1, .scale_y = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "offset.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/offset.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(9 * 64 + 16) * 4 + 0] > 240);
    test_assert(img[(9 * 64 + 48) * 4 + 1] > 240);
    test_assert(img[(23 * 64 + 16) * 4 + 0] > 240);
    test_assert(img[(23 * 64 + 48) * 4 + 1] > 240);
    stbi_image_free(img);

    ecs_set(world, still, TextureTransform, {.scale_x = 1, .scale_y = 1, .offset_x = 0.5f});
    TextureTransform *uv = ecs_get_mut(world, moving, TextureTransform);
    uv->offset_x = 0.5f;
    ecs_progress(world, 0);
    box_render_sync(world);

    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(9 * 64 + 16) * 4 + 1] > 240);
    test_assert(img[(9 * 64 + 48) * 4 + 0] > 240);
    test_assert(img[(23 * 64 + 16) * 4 + 1] > 240);
    test_assert(img[(23 * 64 + 48) * 4 + 0] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_anisotropy(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/stripes.png", tmp);
    uint8_t px[8 * 8 * 4];
    int32_t i, x, y;
    for (i = 0; i < 64; i ++) {
        uint8_t v = (i % 8) < 4 ? 255 : 0;
        px[i * 4 + 0] = v;
        px[i * 4 + 1] = v;
        px[i * 4 + 2] = v;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 8, 4, px, 8 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0.5f, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, 0, -100});
    ecs_set(world, floor, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, floor, Quad, {20, 200});
    ecs_set(world, floor, Rgb, {0, 0, 0});
    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, floor, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, floor, PbrTextures, {.emissive = tex});
    ecs_set(world, floor, TextureTransform, {.scale_x = 10, .scale_y = 100});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "aniso.png",
        .anisotropy = AnisotropyOff, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/aniso.png", tmp);
    int w = 0, h = 0, n = 0;
    int32_t contrast[2] = {0, 0};
    for (i = 0; i < 2; i ++) {
        if (i) {
            Surface *s = ecs_get_mut(world, e, Surface);
            s->anisotropy = AnisotropyUltra;
            ecs_modified(world, e, Surface);
            ecs_progress(world, 0);
            box_render_sync(world);
        }

        unsigned char *img = stbi_load(path, &w, &h, &n, 4);
        test_assert(img != NULL);
        for (y = 17; y < 19; y ++) {
            int32_t lo = 255, hi = 0;
            for (x = 16; x < 48; x ++) {
                int32_t v = img[(y * 64 + x) * 4 + 1];
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }

            contrast[i] = hi - lo > contrast[i] ? hi - lo : contrast[i];
        }

        stbi_image_free(img);
    }

    test_assert(contrast[1] > contrast[0] + 40);

    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_normal_layer(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *toward = flecs_asprintf("%s/toward.png", tmp);
    char *away = flecs_asprintf("%s/away.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 204;
        px[i * 4 + 1] = 128;
        px[i * 4 + 2] = 230;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(toward, 4, 4, 4, px, 4 * 4) != 0);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 51;
    }

    test_assert(stbi_write_png(away, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t lit = ecs_new(world);
    ecs_set(world, lit, TexturePath, {.path = toward});
    ecs_entity_t dark = ecs_new(world);
    ecs_set(world, dark, TexturePath, {.path = away});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {-3.5f, 0, 0});
    ecs_set(world, a, Quad, {6, 6});
    ecs_set(world, a, Rgb, {200, 200, 200});
    ecs_set(world, a, PbrMaterial, {.roughness = 1});
    ecs_set(world, a, PbrTextures, {.normal = lit});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {3.5f, 0, 0});
    ecs_set(world, b, Quad, {6, 6});
    ecs_set(world, b, Rgb, {200, 200, 200});
    ecs_set(world, b, PbrMaterial, {.roughness = 1});
    ecs_set(world, b, PbrTextures, {.normal = dark});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "normal.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/normal.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 16) * 4];
    unsigned char *right = &img[(16 * 64 + 48) * 4];
    test_assert(left[1] > right[1] + 40);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(toward);
    ecs_os_free(away);
    ecs_fini(world);
}

void Texture_gpu_reload_changes_image(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *red = flecs_asprintf("%s/red.png", tmp);
    char *green = flecs_asprintf("%s/green.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(red, 4, 4, 4, px, 4 * 4) != 0);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 0;
        px[i * 4 + 1] = 255;
    }

    test_assert(stbi_write_png(green, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = red});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "reload.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/reload.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 32) * 4 + 1] < 20);
    stbi_image_free(img);

    ecs_set(world, tex, TexturePath, {.path = green});
    int32_t frames = 0;
    while (ecs_get(world, tex, TextureStatus)->state == TextureLoading && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] < 20);
    test_assert(img[(16 * 64 + 32) * 4 + 1] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(red);
    ecs_os_free(green);
    ecs_fini(world);
}

void Texture_gpu_deleted_texture_unbinds(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/red.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "unbind.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/unbind.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 1] < 20);
    stbi_image_free(img);

    ecs_delete(world, tex);
    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 32) * 4 + 1] > 240);
    test_assert(img[(16 * 64 + 32) * 4 + 2] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Texture_gpu_inherited_from_prefab(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *red = flecs_asprintf("%s/red.png", tmp);
    char *green = flecs_asprintf("%s/green.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(red, 4, 4, 4, px, 4 * 4) != 0);
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = 0;
        px[i * 4 + 1] = 255;
    }

    test_assert(stbi_write_png(green, 4, 4, 4, px, 4 * 4) != 0);

    ecs_entity_t red_tex = ecs_new(world);
    ecs_set(world, red_tex, TexturePath, {.path = red});
    ecs_entity_t green_tex = ecs_new(world);
    ecs_set(world, green_tex, TexturePath, {.path = green});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, PbrTextures, {.emissive = red_tex});
    ecs_entity_t quad = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    test_assert(!ecs_owns(world, quad, PbrTextures));
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "prefab.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/prefab.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] > 240);
    test_assert(img[(16 * 64 + 32) * 4 + 1] < 20);
    stbi_image_free(img);

    ecs_set(world, prefab, PbrTextures, {.emissive = green_tex});
    ecs_progress(world, 0);
    box_render_sync(world);
    img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    test_assert(img[(16 * 64 + 32) * 4 + 0] < 20);
    test_assert(img[(16 * 64 + 32) * 4 + 1] > 240);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(red);
    ecs_os_free(green);
    ecs_fini(world);
}

void Texture_gpu_svg_colors(void) {
    // materials/textures
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/halves.svg", tmp);
    FILE *f = fopen(file, "w");
    test_assert(f != NULL);
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<rect width=\"4\" height=\"8\" fill=\"#0000ff\"/>"
        "<rect x=\"4\" width=\"4\" height=\"8\" fill=\"#ffff00\"/></svg>", f);
    fclose(f);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file, .width = 64, .height = 64});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Quad, {12, 6});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, PbrTextures, {.emissive = tex});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "svg.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, tex, Texture)->width, 64);
    char *path = flecs_asprintf("%s/svg.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    test_assert(img != NULL);
    unsigned char *left = &img[(16 * 64 + 12) * 4];
    unsigned char *right = &img[(16 * 64 + 52) * 4];
    test_assert(left[0] < 20);
    test_assert(left[1] < 20);
    test_assert(left[2] > 240);
    test_assert(right[0] > 240);
    test_assert(right[1] > 240);
    test_assert(right[2] < 20);

    stbi_image_free(img);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}
