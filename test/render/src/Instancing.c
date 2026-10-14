/**
 * @file Instancing.c
 * @brief Instanced shapes: what renders after adds, removes and changes, and what uploads.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Instancing_added_shape_rendered(void) {
    // renderer/instancing
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
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "add.png",
        .resolution_scale = 1});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {-3.0f + 2.0f * (float)i, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {0, 0, 0});
        ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 0, 0}});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t added = ecs_new(world);
    ecs_set(world, added, Position, {0, 2.5f, 0});
    ecs_set(world, added, Box, {1, 1, 1});
    ecs_set(world, added, Rgb, {0, 0, 0});
    ecs_set(world, added, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, added, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(ecs_singleton_get(world, RenderStatus)->upload_bytes > 0);

    char *path = flecs_asprintf("%s/add.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *green = &px[(6 * 32 + 16) * 4];
    test_assert(green[1] > 200);
    test_assert(green[0] < 40);
    for (i = 0; i < 4; i ++) {
        unsigned char *red = &px[(16 * 32 + 4 + 8 * i) * 4];
        test_assert(red[0] > 200);
        test_assert(red[1] < 40);
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Instancing_removed_shape_disappears(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 2, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 40, .height = 8, .write_to_file = "rm.png",
        .resolution_scale = 1});
    ecs_entity_t shapes[10];
    int32_t i;
    for (i = 0; i < 10; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {-4.5f + (float)i, 0, 0});
        ecs_set(world, shapes[i], Box, {0.8f, 0.8f, 0.8f});
        ecs_set(world, shapes[i], Rgb, {0, 0, 0});
        ecs_set(world, shapes[i], PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, shapes[i], Emissive, {.strength = 1, .color = {255, 0, 0}});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_delete(world, shapes[3]);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/rm.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 10; i ++) {
        unsigned char *c = &px[(4 * 40 + 2 + 4 * i) * 4];
        if (i == 3) {
            test_assert(c[0] < 40);
        } else {
            test_assert(c[0] > 200);
        }
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Instancing_shape_change_renders_new_mesh(void) {
    // renderer/instancing
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
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "mesh.png",
        .resolution_scale = 1});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {-2, 0, 0});
    ecs_set(world, a, Box, {2, 2, 2});
    ecs_set(world, a, Rgb, {0, 0, 0});
    ecs_set(world, a, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, a, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_set(world, b, Rgb, {0, 0, 0});
    ecs_set(world, b, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, b, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/mesh.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(12 * 32 + 11) * 4] > 200);
    test_assert(px[(12 * 32 + 27) * 4] < 40);
    stbi_image_free(px);

    ecs_remove(world, a, Box);
    ecs_set(world, a, Sphere, {.segments = 24, .smooth = true, .radius = 1});
    ecs_set(world, b, Box, {2, 2, 2});
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(12 * 32 + 11) * 4] < 40);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(12 * 32 + 27) * 4] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Instancing_scene_change_updates_render(void) {
    // renderer/instancing
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
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png",
        .resolution_scale = 1});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Scene, {0});
    ecs_entity_t group = ecs_new(world);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, a, Position, {-2, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_set(world, a, Rgb, {0, 0, 0});
    ecs_set(world, a, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, a, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_set(world, b, Rgb, {0, 0, 0});
    ecs_set(world, b, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, b, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4] > 200);
    stbi_image_free(px);

    ecs_add_pair(world, b, EcsChildOf, root);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4] < 40);
    stbi_image_free(px);

    ecs_set(world, group, Scene, {0});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    test_assert(px[(16 * 32 + 24) * 4] < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Instancing_unchanged_uploads_nothing(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t i;
    for (i = 0; i < 100; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {255, 0, 0});
    }

    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, RenderStatus)->upload_bytes > 0);

    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_changed_uploads_only_changes(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[100];
    int32_t i;
    for (i = 0; i < 100; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)i, 0, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    int64_t full = ecs_singleton_get(world, RenderStatus)->upload_bytes;
    test_assert(full > 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);

    ecs_set(world, shapes[42], Position, {1, 2, 3});
    ecs_set(world, shapes[17], Rgb, {0, 255, 0});
    ecs_progress(world, 0);
    int64_t changed = ecs_singleton_get(world, RenderStatus)->upload_bytes;
    test_assert(changed > 0);
    test_assert(changed < full / 10);

    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_parent_move_updates_children(void) {
    // renderer/instancing
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
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "move.png",
        .resolution_scale = 1});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, -2, 0});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, parent);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, i < 2 ? parent : group);
        ecs_set(world, c, Position, {-3.0f + 2.0f * (float)i, 0, 0});
        ecs_set(world, c, Box, {1, 1, 1});
        ecs_set(world, c, Rgb, {0, 0, 0});
        ecs_set(world, c, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, c, Emissive, {.strength = 1, .color = {255, 0, 0}});
    }

    for (i = 0; i < 60; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {100.0f + (float)i, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_set(world, parent, Position, {0, 2, 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/move.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 4; i ++) {
        test_assert(px[(8 * 32 + 4 + 8 * i) * 4] > 200);
        test_assert(px[(24 * 32 + 4 + 8 * i) * 4] < 40);
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Instancing_dynamic_uploads_every_frame(void) {
    // renderer/instancing
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
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "dyn.png",
        .resolution_scale = 1});
    int32_t i;
    for (i = 0; i < 50; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {100.0f + (float)i, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_entity_t mover = 0;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position, {-3.0f + 2.0f * (float)i, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {0, 0, 0});
        ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 0, 0}});
        mover = e;
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, RenderStatus)->upload_bytes > 0);
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, RenderStatus)->upload_bytes > 0);

    Position *pos = ecs_get_mut(world, mover, Position);
    pos->y = 2;
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/dyn.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 32 + 28) * 4] > 200);
    test_assert(px[(16 * 32 + 28) * 4] < 40);
    test_assert(px[(16 * 32 + 4) * 4] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Instancing_dynamic_material_descendants(void) {
    // renderer/instancing
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
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "dmat.png",
        .resolution_scale = 1});
    ecs_entity_t parent = ecs_new(world);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, a, Position, {-2, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_set(world, a, Rgb, {0, 0, 0});
    ecs_set(world, a, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, a, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    ecs_add(world, parent, DynamicMaterial);
    ecs_progress(world, 0);
    Emissive *emissive = ecs_get_mut(world, a, Emissive);
    emissive->color = (Rgb){0, 255, 0};
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/dmat.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 32 + 8) * 4];
    test_assert(c[1] > 200);
    test_assert(c[0] < 40);
    stbi_image_free(px);

    ecs_remove(world, parent, DynamicMaterial);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);

    ecs_os_free(path);
    ecs_fini(world);
}

void Instancing_dynamic_tag_toggles_uploads(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);

    ecs_add(world, a, DynamicTransform);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, RenderStatus)->upload_bytes > 0);
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, RenderStatus)->upload_bytes > 0);

    ecs_remove(world, a, DynamicTransform);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->upload_bytes, 0);
    test_assert(b != 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_no_alloc_steady_state(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t shapes[1000];
    int32_t i;
    for (i = 0; i < 1000; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)i, 0, 0});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    for (i = 0; i < 100; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position, {(float)i, 1, 0});
        ecs_set(world, e, Sphere, {.segments = 8, .radius = 1});
    }

    for (i = 0; i < 10; i ++) {
        ecs_set(world, shapes[i % 4], Position, {(float)i, 2, 0});
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    for (i = 0; i < 100; i ++) {
        ecs_set(world, shapes[i % 4], Position, {(float)i, 3, 0});
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    test_int(after - before, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Instancing_gpu_changes_rendered(void) {
    // renderer/instancing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 40, .height = 40, .write_to_file = "grid.png",
        .resolution_scale = 1});
    ecs_entity_t shapes[100];
    int32_t i;
    for (i = 0; i < 100; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {
            -4.5f + (float)(i % 10), -4.5f + (float)(i / 10), 0});
        ecs_set(world, shapes[i], Box, {0.6f, 0.6f, 0.6f});
        ecs_set(world, shapes[i], Rgb, {0, 0, 0});
        ecs_set(world, shapes[i], PbrMaterial, {.metallic = 1, .roughness = 1});
        ecs_set(world, shapes[i], Emissive, {.strength = 1, .color = {255, 0, 0}});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_set(world, shapes[7], Position, {-2.5f, 20, 0});
    ecs_set(world, shapes[50], Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_delete(world, shapes[3]);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/grid.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t wrong = 0;
    for (i = 0; i < 100; i ++) {
        int32_t col = 2 + 4 * (i % 10);
        int32_t row = 37 - 4 * (i / 10);
        const unsigned char *c = &px[(row * 40 + col) * 4];
        bool red = c[0] > 200 && c[1] < 40;
        bool green = c[1] > 200 && c[0] < 40;
        bool dark = c[0] < 40 && c[1] < 40;
        if (i == 3 || i == 7) {
            wrong += !dark;
        } else if (i == 50) {
            wrong += !green;
        } else {
            wrong += !red;
        }
    }

    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
