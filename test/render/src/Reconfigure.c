/**
 * @file Reconfigure.c
 * @brief Tests for target updates and releases after a Surface changes, on the null backend.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void Reconfigure_resize_updates_target(void) {
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
    uint32_t handle = ecs_vec_first_t(&null->last.resources, box_render_resource_t)->handle;

    ecs_set(world, e, Surface, {.width = 128, .height = 96});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->kind, BOX_RENDER_TARGET);
    test_int(r->op, BOX_RENDER_UPDATE);
    test_uint(r->handle, handle);
    const box_render_target_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_int(desc->width, 128);
    test_int(desc->height, 96);
    test_int(desc->render_width, 128);
    test_int(desc->render_height, 96);
    const SurfaceStatus *status = ecs_get(world, e, SurfaceStatus);
    test_int(status->width, 128);
    test_int(status->height, 96);

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
    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);

    ecs_set(world, e, Surface, {.width = 64, .height = 32, .resolution_scale = 4});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->op, BOX_RENDER_UPDATE);
    const box_render_target_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_int(desc->width, 64);
    test_int(desc->height, 32);
    test_int(desc->render_width, 16);
    test_int(desc->render_height, 8);
    const SurfaceStatus *status = ecs_get(world, e, SurfaceStatus);
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
    box_render_sync(world);

    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    const box_render_target_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_int(desc->width, 64);
    test_int(desc->height, 32);
    test_int(desc->render_width, 128);
    test_int(desc->render_height, 64);

    ecs_fini(world);
    box_render_null_free(null);
}

void Reconfigure_vsync_updates_target(void) {
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

    ecs_set(world, e, Surface, {.width = 64, .height = 32, .vsync = true});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->op, BOX_RENDER_UPDATE);
    const box_render_target_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_bool(desc->vsync, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void Reconfigure_unchanged_sends_no_update(void) {
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

    ecs_set(world, e, Surface, {.width = 64, .height = 32});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 0);
    test_int(ecs_vec_count(&null->last.targets), 1);
    test_int(null->resources, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Reconfigure_remove_destroys_target(void) {
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
    uint32_t handle = ecs_vec_first_t(&null->last.resources, box_render_resource_t)->handle;

    ecs_remove(world, e, Surface);
    test_assert(ecs_is_alive(world, e));
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->kind, BOX_RENDER_TARGET);
    test_int(r->op, BOX_RENDER_DESTROY);
    test_uint(r->handle, handle);
    test_int(ecs_vec_count(&null->last.targets), 0);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.resources), 0);
    test_int(null->resources, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Reconfigure_delete_destroys_target(void) {
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
    uint32_t handle = ecs_vec_first_t(&null->last.resources, box_render_resource_t)->handle;

    ecs_delete(world, e);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->op, BOX_RENDER_DESTROY);
    test_uint(r->handle, handle);
    test_int(ecs_vec_count(&null->last.targets), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Reconfigure_set_again_after_remove(void) {
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
    uint32_t handle = ecs_vec_first_t(&null->last.resources, box_render_resource_t)->handle;

    ecs_remove(world, e, Surface);
    ecs_set(world, e, Surface, {.width = 16, .height = 8});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 2);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r[0].op, BOX_RENDER_DESTROY);
    test_uint(r[0].handle, handle);
    test_int(r[1].op, BOX_RENDER_CREATE);
    test_assert(r[1].handle != handle);
    const box_render_target_desc_t *desc = box_render_packet_data(&null->last, r[1].data);
    test_int(desc->width, 16);
    test_int(ecs_vec_count(&null->last.targets), 1);
    test_uint(ecs_vec_first_t(&null->last.targets, box_render_target_t)->handle, r[1].handle);

    ecs_fini(world);
    box_render_null_free(null);
}
