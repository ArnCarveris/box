/**
 * @file CullReadback.c
 * @brief Tests for the order in which the cull counters of the frames reach CullStatus.
 *
 * Mapped counter copies are held and then published newest first, the order in which copies
 * that finish mapping out of order arrive; CullStatus must never step back to an older frame.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void CullReadback_older_frame_never_replaces_newer(void) {
    // render_fixes/cull_readback_order
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .culling = {.enabled = true,
        .min_dimension_px = 1}});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    uint32_t word = 0;
    int32_t i;
    for (i = 0; i < 50 && !ecs_has(world, view, CullStatus); i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
    }

    test_assert(ecs_has(world, view, CullStatus));
    test_bool(box_render_gpu_readback_delivery(world, BOX_RENDER_READBACK_HOLD), true);
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
    }

    int64_t before = ecs_get(world, view, CullStatus)->frame;
    box_render_gpu_readback_delivery(world, BOX_RENDER_READBACK_NEWEST_FIRST);
    int64_t last = before;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        int64_t frame = ecs_get(world, view, CullStatus)->frame;
        test_assert(frame >= last);
        if (i == 1) {
            test_assert(frame >= before + 3);
        }

        last = frame;
    }

    test_int(ecs_get(world, view, CullStatus)->visible, 1);

    ecs_fini(world);
}

void CullReadback_read_off_render_thread_aborts(void) {
    // render_fixes/render_minor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = false});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Position, {0, 0, -5});
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    test_bool(ecs_get(world, ecs_id(RenderStatus), RenderStatus)->threaded, true);

    uint32_t word = 0;
    ecs_os_api.abort_ = test_abort;
    ecs_log_set_level(-5);
    test_expect_abort();
    box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
}
