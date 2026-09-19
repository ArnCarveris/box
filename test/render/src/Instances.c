/**
 * @file Instances.c
 * @brief Tests that the renderer draws instance counts past its old buffer cap.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer.h"

void Instances_static_1m_renders_a_frame(void)
{
    // culling/instance_cap
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 900, -900});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});

    int32_t side = 1000;
    int32_t i;
    for (i = 0; i < 1000000; i ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {(float)(i % side) * 2.0f - (float)side,
            0, (float)(i / side) * 2.0f - (float)side});
        ecs_set(w, e, Box, {1, 1, 1});
    }

    box_renderer_desc_t desc = { .width = 128, .height = 128 };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);

    box_packet_t *p = box_renderer_acquire(r);
    test_assert(p != NULL);
    p->width = 128;
    p->height = 128;
    box_render_extract(w, p);
    test_int(p->static_updates.count, 1000000);
    box_renderer_submit(r, p);

    test_int(box_renderer_frames(r), 1);
    test_int(box_renderer_static_count(r, 0), 1000000);
    test_int(box_renderer_errors(r), 0);
    box_renderer_destroy(r);
    ecs_fini(w);
}
