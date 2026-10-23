/**
 * @file Material.c
 * @brief Tests for how material components shade, blend and update, checked on PNG captures.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include "render.h"
#include "draw.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Material_default_material(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Box, {2, 2, 2});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "default.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/default.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 60);
    test_assert(c[1] > 60);
    test_assert(c[2] > 60);
    test_assert(abs((int)c[0] - (int)c[1]) < 40);
    test_assert(abs((int)c[1] - (int)c[2]) < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_base_color(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rgb, {255, 0, 128});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "base.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/base.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 60);
    test_assert(c[0] > c[2] + 20);
    test_assert(c[2] > c[1] + 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_pbr_material(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {-1.5f, 0, 0});
    ecs_set(world, plain, Box, {2, 2, 2});
    ecs_set(world, plain, Rgb, {128, 128, 128});
    ecs_entity_t metal = ecs_new(world);
    ecs_set(world, metal, Position, {1.5f, 0, 0});
    ecs_set(world, metal, Box, {2, 2, 2});
    ecs_set(world, metal, Rgb, {128, 128, 128});
    ecs_set(world, metal, PbrMaterial, {.metallic = 1, .roughness = 0.25f});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "pbr.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/pbr.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, diff = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *a = &px[(16 * 64 + 25) * 4];
    unsigned char *b = &px[(16 * 64 + 39) * 4];
    for (i = 0; i < 3; i ++) {
        diff += abs((int)a[i] - (int)b[i]);
    }

    test_assert(diff > 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_pbr_material_defaults(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, PbrMaterial);
    ecs_add(world, e, Opacity);
    const PbrMaterial *pbr = ecs_get(world, e, PbrMaterial);
    test_flt(pbr->metallic, 0);
    test_flt(pbr->roughness, 0.5f);
    test_flt(ecs_get(world, e, Opacity)->value, 1);

    ecs_fini(world);
}

void Material_emissive(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t dim = ecs_new(world);
    ecs_set(world, dim, Position, {-1.5f, 0, 0});
    ecs_set(world, dim, Box, {2, 2, 0.1f});
    ecs_set(world, dim, Rgb, {0, 0, 0});
    ecs_set(world, dim, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, dim, Emissive, {.strength = 1, .color = {128, 0, 0}});
    ecs_entity_t bright = ecs_new(world);
    ecs_set(world, bright, Position, {1.5f, 0, 0});
    ecs_set(world, bright, Box, {2, 2, 0.1f});
    ecs_set(world, bright, Rgb, {0, 0, 0});
    ecs_set(world, bright, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, bright, Emissive, {.strength = 2, .color = {128, 0, 0}});
    ecs_entity_t color = ecs_new(world);
    ecs_set(world, color, Position, {0, 0, -1});
    ecs_set(world, color, Box, {20, 20, 0.1f});
    ecs_set(world, color, Rgb, {0, 0, 0});
    ecs_set(world, color, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, color, Emissive, {.strength = 1, .color = {255, 0, 128}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "emissive.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/emissive.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *a = &px[(16 * 64 + 25) * 4];
    unsigned char *b = &px[(16 * 64 + 39) * 4];
    unsigned char *c = &px[(16 * 64 + 2) * 4];
    test_assert(a[0] > 100);
    test_assert(b[0] > a[0] + 20);
    test_assert(a[1] < 30 && b[1] < 30);
    test_assert(c[0] > 240);
    test_assert(c[1] < 30);
    test_assert(c[2] > 110 && c[2] < 150);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_opacity_hierarchy(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Opacity, {0.5f});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, child, Position, {-1.5f, 0, 0});
    ecs_set(world, child, Box, {2, 2, 0.1f});
    ecs_set(world, child, Rgb, {0, 0, 0});
    ecs_set(world, child, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, child, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_set(world, child, Opacity, {0.5f});
    ecs_entity_t parented = ecs_new(world);
    ecs_set(world, parented, EcsParent, {root});
    ecs_set(world, parented, Position, {1.5f, 0, 0});
    ecs_set(world, parented, Box, {2, 2, 0.1f});
    ecs_set(world, parented, Rgb, {0, 0, 0});
    ecs_set(world, parented, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, parented, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "fade.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/fade.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *quarter = &px[(16 * 64 + 25) * 4];
    unsigned char *half = &px[(16 * 64 + 39) * 4];
    test_assert(quarter[2] > 40);
    test_assert(half[2] > quarter[2] + 20);
    test_assert(quarter[0] > half[0] + 20);
    test_assert(half[0] > 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_parent_opacity_updates_children(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, Box, {2, 2, 0.1f});
    ecs_set(world, child, Rgb, {0, 0, 0});
    ecs_set(world, child, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, child, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "parent.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/parent.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] < 40);
    test_assert(px[(16 * 64 + 32) * 4 + 2] > 200);
    stbi_image_free(px);

    ecs_set(world, root, Opacity, {0.25f});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 150);
    test_assert(px[(16 * 64 + 32) * 4 + 2] > 60);
    stbi_image_free(px);

    ecs_remove(world, root, Opacity);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] < 40);
    test_assert(px[(16 * 64 + 32) * 4 + 2] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_blend_mode_change(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t glow = ecs_new(world);
    ecs_set(world, glow, Box, {2, 2, 0.1f});
    ecs_set(world, glow, Rgb, {0, 0, 0});
    ecs_set(world, glow, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, glow, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_set(world, glow, BlendMode, {Additive});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "mode.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/mode.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 240);
    test_assert(px[(16 * 64 + 32) * 4 + 1] > 240);
    stbi_image_free(px);

    ecs_set(world, glow, BlendMode, {Opaque});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] < 40);
    test_assert(px[(16 * 64 + 32) * 4 + 1] > 240);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_back_face_mode_removed(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, quad, Quad, {2, 2});
    ecs_set(world, quad, Rgb, {0, 0, 0});
    ecs_set(world, quad, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, quad, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, quad, BackFaceMode, {BackFacePlain});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "face.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/face.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 240);
    stbi_image_free(px);

    ecs_remove(world, quad, BackFaceMode);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(px[(16 * 64 + 32) * 4], 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_blended_after_opaque_back_to_front(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t near = ecs_new(world);
    ecs_set(world, near, Position, {0, 0, 1});
    ecs_set(world, near, Box, {2, 2, 0.1f});
    ecs_set(world, near, Rgb, {0, 0, 0});
    ecs_set(world, near, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, near, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_set(world, near, Opacity, {0.5f});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {0, 0, -1});
    ecs_set(world, far, Sphere, {.segments = 24, .smooth = true, .radius = 1});
    ecs_set(world, far, Rgb, {0, 0, 0});
    ecs_set(world, far, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, far, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_set(world, far, Opacity, {0.5f});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -3});
    ecs_set(world, wall, Box, {40, 40, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "sort.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/sort.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(c[1] > c[2] + 20);
    test_assert(c[0] > 40);
    test_assert(c[2] > 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_blended_sorted_per_view(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t front = ecs_new(world);
    ecs_set(world, front, Position, {0, 0, 6});
    ecs_set(world, front, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Position, {0, 0, -6});
    ecs_set(world, back, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, back, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t root_front = ecs_new(world);
    ecs_entity_t root_back = ecs_new(world);
    ecs_entity_t v1 = ecs_new_w_pair(world, EcsChildOf, root_front);
    ecs_set(world, v1, View, {
        .camera = front, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t v2 = ecs_new_w_pair(world, EcsChildOf, root_back);
    ecs_set(world, v2, View, {
        .camera = back, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t green = ecs_new(world);
    ecs_set(world, green, Position, {0, 0, 0.5f});
    ecs_set(world, green, Box, {2, 2, 0.1f});
    ecs_set(world, green, Rgb, {0, 0, 0});
    ecs_set(world, green, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, green, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_set(world, green, Opacity, {0.5f});
    ecs_entity_t blue = ecs_new(world);
    ecs_set(world, blue, Position, {0, 0, -0.5f});
    ecs_set(world, blue, Box, {2, 2, 0.1f});
    ecs_set(world, blue, Rgb, {0, 0, 0});
    ecs_set(world, blue, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, blue, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_set(world, blue, Opacity, {0.5f});
    ecs_entity_t s1 = ecs_new(world);
    ecs_set(world, s1, Surface, {
        .width = 32, .height = 32, .view_root = root_front, .write_to_file = "front.png",
            .resolution_scale = 1});
    ecs_entity_t s2 = ecs_new(world);
    ecs_set(world, s2, Surface, {
        .width = 32, .height = 32, .view_root = root_back, .write_to_file = "back.png",
            .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    const char *dir = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *path = flecs_asprintf("%s/front.png", dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 32 + 16) * 4];
    test_assert(c[1] > c[2] + 20);
    stbi_image_free(px);
    ecs_os_free(path);

    path = flecs_asprintf("%s/back.png", dir);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 32 + 16) * 4];
    test_assert(c[2] > c[1] + 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_blended_scene_filter(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Scene, {0});
    ecs_entity_t other = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, other, Position, {1.5f, 0, 0});
    ecs_set(world, other, Box, {2, 2, 0.1f});
    ecs_set(world, other, Rgb, {0, 0, 0});
    ecs_set(world, other, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, other, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_set(world, other, Opacity, {0.5f});
    ecs_entity_t mine = ecs_new(world);
    ecs_set(world, mine, Position, {-1.5f, 0, 0});
    ecs_set(world, mine, Box, {2, 2, 0.1f});
    ecs_set(world, mine, Rgb, {0, 0, 0});
    ecs_set(world, mine, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, mine, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_set(world, mine, Opacity, {0.5f});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "filter.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/filter.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *shown = &px[(16 * 64 + 25) * 4];
    unsigned char *hidden = &px[(16 * 64 + 39) * 4];
    test_assert(shown[2] > 100);
    test_int(hidden[0], 255);
    test_int(hidden[2], 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_change_uploads_only_changed(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[20];
    int32_t i;
    for (i = 0; i < 20; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    int64_t full = ecs_singleton_get(world, RenderStatus)->upload_bytes;
    test_assert(full > 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);

    ecs_set(world, shapes[7], PbrMaterial, {.metallic = 0.75f, .roughness = 0.1f});
    ecs_progress(world, 0);
    int64_t changed = ecs_singleton_get(world, RenderStatus)->upload_bytes;
    test_assert(changed > 0);
    test_assert(changed < full / 5);

    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_dynamic_material_per_frame(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t box = ecs_new(world);
    ecs_add(world, box, DynamicMaterial);
    ecs_set(world, box, Box, {2, 2, 0.1f});
    ecs_set(world, box, Rgb, {0, 0, 0});
    ecs_set(world, box, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, box, Emissive, {.strength = 0, .color = {255, 255, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "dynmat.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/dynmat.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] < 40);
    stbi_image_free(px);

    Emissive *emissive = ecs_get_mut(world, box, Emissive);
    emissive->strength = 1;
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 240);
    test_assert(px[(16 * 64 + 32) * 4 + 1] > 240);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_dynamic_transform_keeps_material(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 8, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_set(world, a, Position, {-2, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_set(world, a, Rgb, {0, 0, 0});
    ecs_set(world, a, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, a, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t b = ecs_new(world);
    ecs_add(world, b, DynamicTransform);
    ecs_add(world, b, DynamicMaterial);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_set(world, b, Rgb, {0, 0, 0});
    ecs_set(world, b, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, b, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 32, .write_to_file = "keep.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_get_mut(world, a, Position)->y = 2;
    ecs_get_mut(world, b, Position)->y = 2;
    ecs_get_mut(world, b, Emissive)->color = (Rgb){0, 255, 0};
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/keep.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *pa = &px[(8 * 32 + 8) * 4];
    unsigned char *pb = &px[(8 * 32 + 24) * 4];
    test_assert(pa[0] > 200 && pa[1] < 40);
    test_assert(pb[1] > 200 && pb[0] < 40);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    stbi_image_free(px);

    ecs_set(world, a, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    pa = &px[(8 * 32 + 8) * 4];
    test_assert(pa[2] > 200 && pa[0] < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_dynamic_opacity_blends(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicMaterial);
    ecs_set(world, parent, Opacity, {1});
    ecs_entity_t pane = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, pane, Box, {2, 2, 0.1f});
    ecs_set(world, pane, Rgb, {0, 0, 0});
    ecs_set(world, pane, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, pane, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "dynfade.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/dynfade.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] < 40);
    stbi_image_free(px);

    ecs_get_mut(world, parent, Opacity)->value = 0.5f;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 150);
    test_assert(px[(16 * 64 + 32) * 4 + 2] > 150);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_blended_no_alloc_steady_state(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_enable(world, ecs_lookup(world, "flecs.stats"), false);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t i;
    for (i = 0; i < 200; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 10), 0, -(float)((i * 7) % 50)});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Opacity, {0.5f});
    }

    for (i = 0; i < 10; i ++) {
        ecs_set(world, camera, Position, {0, 0, (float)i});
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    for (i = 0; i < 50; i ++) {
        ecs_set(world, camera, Position, {0, 0, (float)(i % 10)});
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after - before, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Material_gpu_alpha_blend(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t pane = ecs_new(world);
    ecs_set(world, pane, Position, {-1.5f, 0, 0});
    ecs_set(world, pane, Box, {2, 2, 0.1f});
    ecs_set(world, pane, Rgb, {0, 0, 0});
    ecs_set(world, pane, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, pane, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_set(world, pane, Opacity, {0.5f});
    ecs_entity_t glow = ecs_new(world);
    ecs_set(world, glow, Position, {1.5f, 0, 0});
    ecs_set(world, glow, Box, {2, 2, 0.1f});
    ecs_set(world, glow, Rgb, {0, 0, 0});
    ecs_set(world, glow, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, glow, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_set(world, glow, BlendMode, {Additive});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "blend.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/blend.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 32);
    unsigned char *red = &px[(16 * 64 + 2) * 4];
    unsigned char *mixed = &px[(16 * 64 + 25) * 4];
    unsigned char *added = &px[(16 * 64 + 39) * 4];
    test_assert(red[0] > 240);
    test_assert(red[2] < 40);
    test_assert(mixed[0] > 150 && mixed[0] < 220);
    test_assert(mixed[2] > 150 && mixed[2] < 220);
    test_assert(mixed[1] < 40);
    test_assert(added[0] > 240);
    test_assert(added[1] > 240);
    test_assert(added[2] < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_gpu_back_face_plain(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t culled = ecs_new(world);
    ecs_set(world, culled, Position, {-1.5f, 0, 0});
    ecs_set(world, culled, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, culled, Quad, {2, 2});
    ecs_set(world, culled, Rgb, {0, 0, 0});
    ecs_set(world, culled, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, culled, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {1.5f, 0, 0});
    ecs_set(world, plain, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, plain, Quad, {2, 2});
    ecs_set(world, plain, Rgb, {0, 0, 0});
    ecs_set(world, plain, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, plain, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_set(world, plain, BackFaceMode, {BackFacePlain});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "back.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/back.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *left = &px[(16 * 64 + 25) * 4];
    unsigned char *right = &px[(16 * 64 + 39) * 4];
    test_int(left[0], 0);
    test_int(left[1], 0);
    test_int(left[2], 0);
    test_assert(right[0] > 240);
    test_assert(right[1] > 240);
    test_assert(right[2] > 240);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Material_default_roughness_without_material(void) {
    // materials/pbr
    box_render_instance_t inst = {0};
    box_draw_material_t material = {0};
    box_draws_material(&inst, &material);
    test_flt(inst.material[0], 0);
    test_flt(inst.material[1], 1);

    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.3f };
    material.pbr = &pbr;
    box_draws_material(&inst, &material);
    test_flt(inst.material[0], 0.25f);
    test_flt(inst.material[1], 0.3f);
}

void Material_default_roughness_metallic_only(void) {
    // materials/pbr
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "materials",
        "using box.material\n"
        "added {}\n"
        "metal { PbrMaterial: {metallic: 1} }\n"
        "rough { PbrMaterial: {roughness: 0.3} }\n", NULL) == 0);

    ecs_entity_t added = ecs_lookup(world, "added");
    ecs_add(world, added, PbrMaterial);
    const PbrMaterial *pbr = ecs_get(world, added, PbrMaterial);
    test_assert(pbr != NULL);
    test_flt(pbr->metallic, 0);
    test_flt(pbr->roughness, 1);

    pbr = ecs_get(world, ecs_lookup(world, "metal"), PbrMaterial);
    test_assert(pbr != NULL);
    test_flt(pbr->metallic, 1);
    test_flt(pbr->roughness, 1);

    pbr = ecs_get(world, ecs_lookup(world, "rough"), PbrMaterial);
    test_assert(pbr != NULL);
    test_flt(pbr->metallic, 0);
    test_flt(pbr->roughness, 0.3f);

    ecs_fini(world);
}

void Material_default_material_sphere_matte(void) {
    // materials/pbr
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {-1.5f, 0, 0});
    ecs_set(world, plain, Sphere, {.segments = 64, .smooth = true, .radius = 1.2f});
    ecs_set(world, plain, Rgb, {64, 128, 32});
    ecs_entity_t glossy = ecs_new(world);
    ecs_set(world, glossy, Position, {1.5f, 0, 0});
    ecs_set(world, glossy, Sphere, {.segments = 64, .smooth = true, .radius = 1.2f});
    ecs_set(world, glossy, Rgb, {64, 128, 32});
    ecs_set(world, glossy, PbrMaterial, {.roughness = 0.3f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 128, .height = 64, .write_to_file = "matte.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/matte.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, x, y;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 128);
    test_int(h, 64);

    float albedo_rg = powf((64.0f / 255.0f + 0.055f) / 1.055f, 2.4f) /
        powf((128.0f / 255.0f + 0.055f) / 1.055f, 2.4f);
    float albedo_bg = powf((32.0f / 255.0f + 0.055f) / 1.055f, 2.4f) /
        powf((128.0f / 255.0f + 0.055f) / 1.055f, 2.4f);
    int half;
    for (half = 0; half < 2; half ++) {
        unsigned char *peak = NULL;
        int best = -1;
        for (y = 0; y < h; y ++) {
            for (x = half * 64; x < half * 64 + 64; x ++) {
                unsigned char *c = &px[(y * w + x) * 4];
                int sum = c[0] + c[1] + c[2];
                if (sum > best) {
                    best = sum;
                    peak = c;
                }
            }
        }

        test_assert(peak != NULL);
        test_assert(peak[1] > 40);
        float lin[3];
        int i;
        for (i = 0; i < 3; i ++) {
            float v = (float)peak[i] / 255.0f;
            lin[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
        }

        float rg = lin[0] / lin[1];
        float bg = lin[2] / lin[1];
        if (half == 0) {
            test_assert(rg < albedo_rg * 1.5f);
            test_assert(bg < albedo_bg * 3.0f);
        } else {
            test_assert(rg > albedo_rg * 2.0f);
            test_assert(bg > albedo_bg * 4.0f);
        }
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
