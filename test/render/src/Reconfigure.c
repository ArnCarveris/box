/**
 * @file Reconfigure.c
 * @brief Tests that a changed, removed or deleted Surface reconfigures what is rendered.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Reconfigure_resize_updates_status(void) {
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
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_set(world, e, Surface, {.width = 128, .height = 96, .resolution_scale = 1});
    ecs_progress(world, 0);

    const SurfaceStatus *status = ecs_get(world, e, SurfaceStatus);
    test_int(status->width, 128);
    test_int(status->height, 96);
    test_int(status->render_width, 128);
    test_int(status->render_height, 96);

    ecs_fini(world);
    box_render_null_free(null);
}

void Reconfigure_resolution_scale_updates_render_size(void) {
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
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .resolution_scale = 4});
    ecs_progress(world, 0);

    const SurfaceStatus *status = ecs_get(world, e, SurfaceStatus);
    test_int(status->width, 64);
    test_int(status->height, 32);
    test_int(status->render_width, 16);
    test_int(status->render_height, 8);

    ecs_fini(world);
    box_render_null_free(null);
}

void Reconfigure_fractional_resolution_scale(void) {
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
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .resolution_scale = 0.5f});
    ecs_progress(world, 0);

    const SurfaceStatus *status = ecs_get(world, e, SurfaceStatus);
    test_int(status->width, 64);
    test_int(status->height, 32);
    test_int(status->render_width, 128);
    test_int(status->render_height, 64);

    ecs_fini(world);
    box_render_null_free(null);
}

void Reconfigure_remove_stops_rendering(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 8, .height = 8, .write_to_file = "removed.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/removed.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    fclose(f);
    test_int(remove(path), 0);

    ecs_remove(world, e, Surface);
    test_assert(ecs_is_alive(world, e));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(fopen(path, "rb") == NULL);

    ecs_os_free(path);
    ecs_fini(world);
}

void Reconfigure_delete_stops_rendering(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 8, .height = 8, .write_to_file = "deleted.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/deleted.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    fclose(f);
    test_int(remove(path), 0);

    ecs_delete(world, e);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_assert(fopen(path, "rb") == NULL);

    ecs_os_free(path);
    ecs_fini(world);
}

void Reconfigure_set_again_after_remove(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "again.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_remove(world, e, Surface);
    ecs_set(world, e, Surface, {.width = 16, .height = 8, .write_to_file = "again.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/again.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 16);
    test_int(h, 8);
    test_int(ecs_get(world, e, SurfaceStatus)->width, 16);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
