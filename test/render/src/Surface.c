/**
 * @file Surface.c
 * @brief Tests for the targets a Surface describes in the render packet, on the null backend.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void Surface_create_offscreen_target(void) {
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

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->kind, BOX_RENDER_TARGET);
    test_int(r->op, BOX_RENDER_CREATE);
    test_assert(r->handle != 0);
    test_int(r->size, sizeof(box_render_target_desc_t));
    const box_render_target_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_assert(desc->layer == NULL);
    test_int(desc->width, 64);
    test_int(desc->height, 32);
    test_int(desc->render_width, 64);
    test_int(desc->render_height, 32);
    test_bool(desc->vsync, false);

    test_int(ecs_vec_count(&null->last.targets), 1);
    const box_render_target_t *t = ecs_vec_first(&null->last.targets);
    test_uint(t->handle, r->handle);
    test_int(t->capture_size, 0);
    test_flt(t->clear[0], 0);
    test_flt(t->clear[1], 0);
    test_flt(t->clear[2], 0);
    test_flt(t->clear[3], 1);

    ecs_fini(world);
    box_render_null_free(null);
}

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
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    const box_render_target_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_int(desc->width, BOX_SURFACE_WIDTH);
    test_int(desc->height, BOX_SURFACE_HEIGHT);
    test_int(desc->render_width, BOX_SURFACE_WIDTH);
    test_int(desc->render_height, BOX_SURFACE_HEIGHT);

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

void Surface_two_surfaces_two_targets(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Surface, {.width = 64, .height = 32});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Surface, {.width = 16, .height = 8});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 2);
    test_int(ecs_vec_count(&null->last.targets), 2);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    const box_render_target_t *t = ecs_vec_first(&null->last.targets);
    test_assert(r[0].handle != r[1].handle);
    test_uint(t[0].handle, r[0].handle);
    test_uint(t[1].handle, r[1].handle);
    const box_render_target_desc_t *da = box_render_packet_data(&null->last, r[0].data);
    const box_render_target_desc_t *db = box_render_packet_data(&null->last, r[1].data);
    test_int(da->width + db->width, 80);
    test_int(da->height + db->height, 40);

    ecs_fini(world);
    box_render_null_free(null);
}

void Surface_target_every_frame(void) {
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
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(null->packets, 3);
    test_int(null->targets, 3);
    test_int(null->resources, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Surface_no_surface_no_target(void) {
    // renderer/device
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(null->packets, 2);
    test_int(null->targets, 0);
    test_int(null->resources, 0);

    ecs_fini(world);
    box_render_null_free(null);
}
