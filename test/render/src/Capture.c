/**
 * @file Capture.c
 * @brief Tests for PNG captures of surfaces written by the webgpu backend in headless worlds.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Errors logged since the counter was installed */
static int capture_errors = 0;

/** Count logged errors.
 *
 * @param level Level of the message
 * @param file File that logged it
 * @param line Line that logged it
 * @param msg The message
 */
static void capture_count_errors(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    if (level == -3) {
        capture_errors ++;
    }
}

void Capture_cleared_surface(void) {
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
        .camera = camera, .clear = {.enabled = true, .color = {255, 128, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "clear.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/clear.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 32);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] != 255 || px[i * 4 + 1] != 128 || px[i * 4 + 2] != 0 ||
            px[i * 4 + 3] != 255;
    }

    test_int(wrong, 0);
    box_render_gpu_stats_t stats;
    box_render_gpu_stats(world, &stats);
    test_int(stats.captures, 1);
    test_int(stats.errors, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Capture_no_clearing_view_is_black(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.color = {255, 255, 255, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .write_to_file = "black.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/black.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 16);
    test_int(h, 16);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] || px[i * 4 + 1] || px[i * 4 + 2] || px[i * 4 + 3] != 255;
    }

    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Capture_resolution_scale(void) {
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
        .camera = camera, .clear = {.enabled = true, .color = {20, 200, 90, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {
        .width = 64, .height = 32, .resolution_scale = 4, .write_to_file = "scaled.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scaled.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 32);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] != 20 || px[i * 4 + 1] != 200 || px[i * 4 + 2] != 90 ||
            px[i * 4 + 3] != 255;
    }

    test_int(wrong, 0);
    box_render_gpu_stats_t stats;
    box_render_gpu_stats(world, &stats);
    test_int(stats.render_width, 16);
    test_int(stats.render_height, 8);
    test_int(stats.errors, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Capture_after_resize(void) {
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
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "resize.png"});
    ecs_progress(world, 0);
    ecs_set(world, e, Surface, {.width = 48, .height = 40, .write_to_file = "resize.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resize.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 48);
    test_int(h, 40);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] || px[i * 4 + 1] || px[i * 4 + 2] != 255 || px[i * 4 + 3] != 255;
    }

    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Capture_absolute_path(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/absolute.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    test_assert(path[0] == '/');
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 8, .height = 4, .write_to_file = path});
    ecs_progress(world, 0);
    box_render_sync(world);

    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 8);
    test_int(h, 4);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Capture_view_root_selects_clear(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 255, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {
        .width = 8, .height = 8, .view_root = root, .write_to_file = "root.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/root.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] || px[i * 4 + 1] != 255 || px[i * 4 + 2] || px[i * 4 + 3] != 255;
    }

    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Capture_every_frame(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 8, .height = 8, .write_to_file = "frames.png"});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.frames, 3);
    test_int(stats.captures, 3);
    test_int(stats.errors, 0);

    ecs_set(world, e, Surface, {.width = 8, .height = 8});
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_gpu_stats(world, &stats);
    test_int(stats.frames, 4);
    test_int(stats.captures, 3);

    ecs_fini(world);
}

void Capture_main_thread(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, RenderSettings, {.main_thread = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 255, 255, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 16, .write_to_file = "main.png"});
    ecs_progress(world, 0);

    char *path = flecs_asprintf("%s/main.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 32);
    test_int(h, 16);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] != 255 || px[i * 4 + 1] != 255 || px[i * 4 + 2] != 255 ||
            px[i * 4 + 3] != 255;
    }

    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Capture_relative_path_written_before_exit(void) {
    // renderer/capture_path
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 255, 255, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 256, .height = 256, .write_to_file = "exit.png"});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_os_api_log_t log = ecs_os_api.log_;
    capture_errors = 0;
    ecs_os_api.log_ = capture_count_errors;
    ecs_fini(world);
    ecs_os_api.log_ = log;
    test_int(capture_errors, 0);
}
