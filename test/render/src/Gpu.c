/**
 * @file Gpu.c
 * @brief Tests that the renderer times the passes of a frame when it is asked to.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer.h"

/** Frames a test renders, enough for a resolve to arrive */
#define GPU_FRAMES (8)

/** Edge of the offscreen target the tests render into */
#define GPU_SIZE (128)

void Gpu_timings_are_off_until_they_are_asked_for(void)
{
    // monitoring/gpu_timings
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 55});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0.5f, 0});
    ecs_set(w, e, Box, {1, 1, 1});

    box_renderer_desc_t desc = { .width = GPU_SIZE, .height = GPU_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);

    int32_t i;
    for (i = 0; i < GPU_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        test_assert(p != NULL);
        p->width = GPU_SIZE;
        p->height = GPU_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_renderer_gpu_t gpu;
    box_renderer_gpu_timings(r, &gpu);
    test_int(gpu.count, 0);
    test_flt(gpu.frame_ms, 0.0);
    test_int(box_renderer_errors(r), 0);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void Gpu_enabled_timings_report_the_passes_of_a_frame(void)
{
    // monitoring/gpu_timings
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 55});
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0.5f, 0});
    ecs_set(w, e, Box, {1, 1, 1});

    box_renderer_desc_t desc = { .width = GPU_SIZE, .height = GPU_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_gpu_timings(r, true);

    int32_t i;
    for (i = 0; i < GPU_FRAMES; i ++) {
        box_packet_t *p = box_renderer_acquire(r);
        test_assert(p != NULL);
        p->width = GPU_SIZE;
        p->height = GPU_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_renderer_gpu_t gpu;
    box_renderer_gpu_timings(r, &gpu);
    test_int(box_renderer_errors(r), 0);
    if (gpu.supported) {
        test_assert(gpu.count > 0);
        test_assert(gpu.frame_ms >= 0.0);
        bool opaque = false;
        uint32_t pass;
        for (pass = 0; pass < gpu.count; pass ++) {
            opaque |= !ecs_os_strcmp(gpu.passes[pass].name, "opaque");
        }

        test_bool(opaque, true);
    } else {
        test_int(gpu.count, 0);
    }

    box_renderer_destroy(r);
    ecs_fini(w);
}

void Gpu_disabled_timings_stop_the_queries(void)
{
    // monitoring/gpu_timings
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 55});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0.5f, 0});
    ecs_set(w, e, Box, {1, 1, 1});

    box_renderer_desc_t desc = { .width = GPU_SIZE, .height = GPU_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_gpu_timings(r, true);

    int32_t i;
    for (i = 0; i < GPU_FRAMES * 2; i ++) {
        if (i == GPU_FRAMES) {
            box_renderer_set_gpu_timings(r, false);
        }

        box_packet_t *p = box_renderer_acquire(r);
        test_assert(p != NULL);
        p->width = GPU_SIZE;
        p->height = GPU_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    test_int(box_renderer_frames(r), GPU_FRAMES * 2);
    test_int(box_renderer_errors(r), 0);
    box_renderer_destroy(r);
    ecs_fini(w);
}
