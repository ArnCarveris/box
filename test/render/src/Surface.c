/**
 * @file Surface.c
 * @brief Tests for the size and status of surfaces and for surfaces that render side by side.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Surface_default_size(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {0});
    ecs_progress(world, 0);

    const SurfaceStatus *status = ecs_get(world, e, SurfaceStatus);
    test_assert(status != NULL);
    test_int(status->width, BOX_SURFACE_WIDTH);
    test_int(status->height, BOX_SURFACE_HEIGHT);
    test_int(status->render_width, BOX_SURFACE_WIDTH);
    test_int(status->render_height, BOX_SURFACE_HEIGHT);
    test_bool(status->window, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void Surface_status_written(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .resolution_scale = 2});
    test_assert(!ecs_has(world, e, SurfaceStatus));
    ecs_progress(world, 0);

    const SurfaceStatus *status = ecs_get(world, e, SurfaceStatus);
    test_assert(status != NULL);
    test_int(status->width, 64);
    test_int(status->height, 32);
    test_int(status->render_width, 32);
    test_int(status->render_height, 16);
    test_bool(status->window, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void Surface_two_surfaces(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Surface, {.width = 64, .height = 32, .write_to_file = "first.png"});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Surface, {.width = 16, .height = 8, .write_to_file = "second.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    const char *dir = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *path_a = flecs_asprintf("%s/first.png", dir);
    char *path_b = flecs_asprintf("%s/second.png", dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path_a, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 32);
    test_int(px[2], 255);
    stbi_image_free(px);
    px = stbi_load(path_b, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 16);
    test_int(h, 8);
    test_int(px[2], 255);

    stbi_image_free(px);
    ecs_os_free(path_a);
    ecs_os_free(path_b);
    ecs_fini(world);
}
