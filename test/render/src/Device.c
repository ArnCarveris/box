/**
 * @file Device.c
 * @brief Tests for the webgpu device and the GPU targets of surfaces in headless worlds.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void Device_surface_creates_device(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_bool(stats.ready, true);
    test_bool(stats.failed, false);
    test_int(stats.created, 1);
    test_int(stats.targets, 1);
    test_int(stats.configured, 1);
    test_int(stats.frames, 1);
    test_int(stats.errors, 0);
    test_int(stats.width, 64);
    test_int(stats.height, 32);
    test_int(stats.render_width, 64);
    test_int(stats.render_height, 32);

    ecs_fini(world);
}

void Device_no_surface_no_device(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), false);
    test_bool(stats.ready, false);

    ecs_fini(world);
}

void Device_own_backend_kept(void) {
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
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), false);
    test_int(null->targets, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Device_resize_reconfigures(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);
    ecs_set(world, e, Surface, {.width = 200, .height = 100});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.created, 1);
    test_int(stats.configured, 2);
    test_int(stats.targets, 1);
    test_int(stats.frames, 2);
    test_int(stats.errors, 0);
    test_int(stats.width, 200);
    test_int(stats.height, 100);

    ecs_fini(world);
}

void Device_resolution_scale_reconfigures(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .resolution_scale = 2});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.configured, 2);
    test_int(stats.width, 64);
    test_int(stats.height, 32);
    test_int(stats.render_width, 32);
    test_int(stats.render_height, 16);
    test_int(stats.frames, 2);
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void Device_unchanged_not_reconfigured(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.configured, 1);
    test_int(stats.frames, 3);

    ecs_fini(world);
}

void Device_remove_releases_target(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);
    ecs_remove(world, e, Surface);
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.created, 1);
    test_int(stats.destroyed, 1);
    test_int(stats.targets, 0);
    test_int(stats.frames, 1);
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void Device_main_thread_renders(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, RenderSettings, {.main_thread = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);

    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_bool(stats.ready, true);
    test_int(stats.frames, 1);
    test_int(stats.errors, 0);
    test_bool(ecs_singleton_get(world, RenderStatus)->threaded, false);

    ecs_fini(world);
}
