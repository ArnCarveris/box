/**
 * @file Tint.c
 * @brief Tests for how Tint mixes the base color and composes down ChildOf and Parent trees.
 *
 * Every case renders unlit, so a pixel holds the resolved base color. Boxes stand 6 units in
 * front of the camera in a 64x32 capture: world x -4.5, -1.5, 1.5 and 4.5 land on pixel
 * columns 11, 25, 39 and 53 of row 16.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Tint_mix_by_strength(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {0, 255, 0});
    ecs_entity_t none = ecs_new(world);
    ecs_set(world, none, Position, {-4.5f, 0, 0});
    ecs_set(world, none, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, none, Rgb, {200, 100, 50});
    ecs_set(world, none, Tint, {0, 0, 255, 0});
    ecs_entity_t half = ecs_new(world);
    ecs_set(world, half, Position, {-1.5f, 0, 0});
    ecs_set(world, half, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, half, Rgb, {200, 100, 50});
    ecs_set(world, half, Tint, {0, 0, 255, 128});
    ecs_entity_t full = ecs_new(world);
    ecs_set(world, full, Position, {1.5f, 0, 0});
    ecs_set(world, full, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, full, Rgb, {200, 100, 50});
    ecs_set(world, full, Tint, {0, 0, 255, 255});
    ecs_entity_t black = ecs_new(world);
    ecs_set(world, black, Position, {4.5f, 0, 0});
    ecs_set(world, black, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, black, Rgb, {200, 100, 50});
    ecs_set(world, black, Tint, {0, 0, 0, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "mix.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/mix.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *p0 = &px[(16 * 64 + 11) * 4];
    unsigned char *p1 = &px[(16 * 64 + 25) * 4];
    unsigned char *p2 = &px[(16 * 64 + 39) * 4];
    unsigned char *p3 = &px[(16 * 64 + 53) * 4];
    test_assert(abs((int)p0[0] - 200) <= 2);
    test_assert(abs((int)p0[1] - 100) <= 2);
    test_assert(abs((int)p0[2] - 50) <= 2);
    test_assert(abs((int)p1[0] - 100) <= 2);
    test_assert(abs((int)p1[1] - 50) <= 2);
    test_assert(abs((int)p1[2] - 153) <= 2);
    test_assert(p2[0] <= 2);
    test_assert(p2[1] <= 2);
    test_assert(p2[2] >= 253);
    test_assert(p3[0] <= 2);
    test_assert(p3[1] <= 2);
    test_assert(p3[2] <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_compose_childof(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t reference = ecs_new(world);
    ecs_set(world, reference, Position, {-4.5f, 0, 0});
    ecs_set(world, reference, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, reference, Rgb, {128, 192, 255});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Tint, {0, 0, 255, 128});
    ecs_entity_t near_ = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, near_, Position, {-1.5f, 0, 0});
    ecs_set(world, near_, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, near_, Rgb, {128, 192, 255});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Tint, {0, 0, 0, 128});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Position, {1.5f, 0, 0});
    ecs_set(world, grandchild, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, grandchild, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "childof.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/childof.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *plain = &px[(16 * 64 + 11) * 4];
    unsigned char *blue = &px[(16 * 64 + 25) * 4];
    unsigned char *dark = &px[(16 * 64 + 39) * 4];
    test_assert(abs((int)plain[0] - 128) <= 2);
    test_assert(abs((int)plain[1] - 192) <= 2);
    test_assert(abs((int)plain[2] - 255) <= 2);
    test_assert(abs((int)blue[0] - 64) <= 2);
    test_assert(abs((int)blue[1] - 96) <= 2);
    test_assert(abs((int)blue[2] - 255) <= 2);
    test_assert(abs((int)dark[0] - 32) <= 2);
    test_assert(abs((int)dark[1] - 48) <= 2);
    test_assert(abs((int)dark[2] - 127) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_compose_parent(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Tint, {0, 0, 255, 128});
    ecs_entity_t near_ = ecs_new(world);
    ecs_set(world, near_, EcsParent, {parent});
    ecs_set(world, near_, Position, {-1.5f, 0, 0});
    ecs_set(world, near_, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, near_, Rgb, {128, 192, 255});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, EcsParent, {parent});
    ecs_set(world, child, Tint, {0, 0, 0, 128});
    ecs_entity_t grandchild = ecs_new(world);
    ecs_set(world, grandchild, EcsParent, {child});
    ecs_set(world, grandchild, Position, {1.5f, 0, 0});
    ecs_set(world, grandchild, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, grandchild, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "parent.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/parent.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *blue = &px[(16 * 64 + 25) * 4];
    unsigned char *dark = &px[(16 * 64 + 39) * 4];
    test_assert(abs((int)blue[0] - 64) <= 2);
    test_assert(abs((int)blue[1] - 96) <= 2);
    test_assert(abs((int)blue[2] - 255) <= 2);
    test_assert(abs((int)dark[0] - 32) <= 2);
    test_assert(abs((int)dark[1] - 48) <= 2);
    test_assert(abs((int)dark[2] - 127) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_compose_mixed(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Tint, {0, 0, 255, 128});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, a, Tint, {0, 0, 0, 128});
    ecs_entity_t a_shape = ecs_new(world);
    ecs_set(world, a_shape, EcsParent, {a});
    ecs_set(world, a_shape, Position, {-1.5f, 0, 0});
    ecs_set(world, a_shape, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, a_shape, Rgb, {128, 192, 255});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, EcsParent, {root});
    ecs_set(world, b, Tint, {0, 0, 0, 128});
    ecs_entity_t b_shape = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, b_shape, Position, {1.5f, 0, 0});
    ecs_set(world, b_shape, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, b_shape, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "mixed.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/mixed.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *pa = &px[(16 * 64 + 25) * 4];
    unsigned char *pb = &px[(16 * 64 + 39) * 4];
    test_assert(abs((int)pa[0] - 32) <= 2);
    test_assert(abs((int)pa[1] - 48) <= 2);
    test_assert(abs((int)pa[2] - 127) <= 2);
    test_assert(abs((int)pb[0] - 32) <= 2);
    test_assert(abs((int)pb[1] - 48) <= 2);
    test_assert(abs((int)pb[2] - 127) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_own_tint_after_ancestors(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Tint, {0, 0, 255, 128});
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, shape, Position, {-1.5f, 0, 0});
    ecs_set(world, shape, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, shape, Rgb, {255, 255, 255});
    ecs_set(world, shape, Tint, {0, 0, 0, 128});
    ecs_entity_t below = ecs_new_w_pair(world, EcsChildOf, shape);
    ecs_set(world, below, Position, {3, 0, 0});
    ecs_set(world, below, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, below, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "own.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/own.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *own = &px[(16 * 64 + 25) * 4];
    unsigned char *child = &px[(16 * 64 + 39) * 4];
    test_assert(abs((int)own[0] - 64) <= 2);
    test_assert(abs((int)own[1] - 64) <= 2);
    test_assert(abs((int)own[2] - 127) <= 2);
    test_assert(abs((int)child[0] - 64) <= 2);
    test_assert(abs((int)child[1] - 64) <= 2);
    test_assert(abs((int)child[2] - 127) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_change_updates_descendants(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, Tint, {0, 0, 0, 128});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Box, {2, 2, 0.1f});
    ecs_set(world, grandchild, Position, {0, 0, 0});
    ecs_set(world, grandchild, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "change.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/change.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 64) <= 2);
    test_assert(abs((int)c[1] - 96) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);
    stbi_image_free(px);

    ecs_set(world, root, Tint, {0, 0, 255, 128});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 32) <= 2);
    test_assert(abs((int)c[1] - 48) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);
    stbi_image_free(px);

    ecs_set(world, root, Tint, {255, 0, 0, 255});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 127) <= 2);
    test_assert(c[1] <= 2);
    test_assert(c[2] <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_remove_tint(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Tint, {0, 0, 255, 128});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, EcsParent, {root});
    ecs_set(world, child, Tint, {0, 0, 0, 128});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Box, {2, 2, 0.1f});
    ecs_set(world, grandchild, Position, {0, 0, 0});
    ecs_set(world, grandchild, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "remove.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/remove.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 32) <= 2);
    test_assert(abs((int)c[1] - 48) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);
    stbi_image_free(px);

    ecs_remove(world, child, Tint);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 64) <= 2);
    test_assert(abs((int)c[1] - 96) <= 2);
    test_assert(abs((int)c[2] - 255) <= 2);
    stbi_image_free(px);

    ecs_remove(world, root, Tint);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 128) <= 2);
    test_assert(abs((int)c[1] - 192) <= 2);
    test_assert(abs((int)c[2] - 255) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_reparent_childof(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t blue = ecs_new(world);
    ecs_set(world, blue, Tint, {0, 0, 255, 128});
    ecs_entity_t plain = ecs_new(world);
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, blue);
    ecs_set(world, group, Tint, {0, 0, 0, 128});
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, shape, Box, {2, 2, 0.1f});
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "move.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/move.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 32) <= 2);
    test_assert(abs((int)c[1] - 48) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);
    stbi_image_free(px);

    ecs_add_pair(world, group, EcsChildOf, plain);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 64) <= 2);
    test_assert(abs((int)c[1] - 96) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);
    stbi_image_free(px);

    ecs_add_pair(world, shape, EcsChildOf, blue);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 64) <= 2);
    test_assert(abs((int)c[1] - 96) <= 2);
    test_assert(abs((int)c[2] - 255) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_reparent_parent(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t blue = ecs_new(world);
    ecs_set(world, blue, Tint, {0, 0, 255, 128});
    ecs_entity_t black = ecs_new(world);
    ecs_set(world, black, Tint, {0, 0, 0, 128});
    ecs_entity_t group = ecs_new(world);
    ecs_set(world, group, EcsParent, {blue});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, EcsParent, {group});
    ecs_set(world, shape, Box, {2, 2, 0.1f});
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "move.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/move.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 64) <= 2);
    test_assert(abs((int)c[1] - 96) <= 2);
    test_assert(abs((int)c[2] - 255) <= 2);
    stbi_image_free(px);

    ecs_set(world, group, EcsParent, {black});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 64) <= 2);
    test_assert(abs((int)c[1] - 96) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);
    stbi_image_free(px);

    ecs_set(world, black, EcsParent, {blue});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 32) <= 2);
    test_assert(abs((int)c[1] - 48) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_leave_hierarchy(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Tint, {0, 0, 0, 128});
    ecs_entity_t left = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, left, Position, {-1.5f, 0, 0});
    ecs_set(world, left, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, left, Rgb, {128, 192, 255});
    ecs_entity_t right = ecs_new(world);
    ecs_set(world, right, EcsParent, {root});
    ecs_set(world, right, Position, {1.5f, 0, 0});
    ecs_set(world, right, Box, {1.2f, 1.2f, 0.1f});
    ecs_set(world, right, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "leave.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/leave.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *l = &px[(16 * 64 + 25) * 4];
    unsigned char *r = &px[(16 * 64 + 39) * 4];
    test_assert(abs((int)l[1] - 96) <= 2);
    test_assert(abs((int)r[1] - 96) <= 2);
    stbi_image_free(px);

    ecs_remove_pair(world, left, EcsChildOf, root);
    ecs_remove(world, right, EcsParent);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    l = &px[(16 * 64 + 25) * 4];
    r = &px[(16 * 64 + 39) * 4];
    test_assert(abs((int)l[0] - 128) <= 2);
    test_assert(abs((int)l[1] - 192) <= 2);
    test_assert(abs((int)l[2] - 255) <= 2);
    test_assert(abs((int)r[0] - 128) <= 2);
    test_assert(abs((int)r[1] - 192) <= 2);
    test_assert(abs((int)r[2] - 255) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_dynamic_material(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Tint, {0, 0, 255, 128});
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_add(world, shape, DynamicMaterial);
    ecs_set(world, shape, Box, {2, 2, 0.1f});
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Rgb, {128, 192, 255});
    ecs_set(world, shape, Tint, {0, 0, 0, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "dyn.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/dyn.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 64) <= 2);
    test_assert(abs((int)c[1] - 96) <= 2);
    test_assert(abs((int)c[2] - 255) <= 2);
    stbi_image_free(px);

    Tint *tint = ecs_get_mut(world, shape, Tint);
    tint->a = 128;
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 32) <= 2);
    test_assert(abs((int)c[1] - 48) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);
    stbi_image_free(px);

    ecs_set(world, root, Tint, {255, 0, 0, 255});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 127) <= 2);
    test_assert(c[1] <= 2);
    test_assert(c[2] <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_dynamic_material_inherited(void) {
    // materials/tint
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
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicMaterial);
    ecs_set(world, root, Tint, {0, 0, 255, 0});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, group, Tint, {0, 0, 0, 128});
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, shape, Box, {2, 2, 0.1f});
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Rgb, {128, 192, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "dyninh.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/dyninh.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 64) <= 2);
    test_assert(abs((int)c[1] - 96) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);
    stbi_image_free(px);

    Tint *tint = ecs_get_mut(world, root, Tint);
    tint->a = 128;
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 32) <= 2);
    test_assert(abs((int)c[1] - 48) <= 2);
    test_assert(abs((int)c[2] - 127) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Tint_keeps_opacity(void) {
    // materials/tint
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {0, 255, 0});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Opacity, {0.5f});
    ecs_set(world, root, Tint, {255, 0, 0, 255});
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, shape, Box, {2, 2, 0.1f});
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Rgb, {0, 0, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "opacity.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/opacity.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 120);
    test_assert(c[1] > 120);
    test_assert(c[2] <= 4);
    stbi_image_free(px);

    ecs_remove(world, root, Opacity);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] >= 253);
    test_assert(c[1] <= 2);
    test_assert(c[2] <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
