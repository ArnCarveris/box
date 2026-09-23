/**
 * @file DrawCost.c
 * @brief Tests for the GPU commands a UI tree costs the frame it is drawn in.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer.h"

/** Frames a test renders, enough for the read back of a counted frame to arrive */
#define DRAW_COST_FRAMES (8)

/** Edge of the offscreen target the tests render into */
#define DRAW_COST_SIZE (256)

void DrawCost_sorted_pass_sets_its_pipeline_once(void)
{
    // ui/ui_draw_cost
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = DRAW_COST_SIZE;
    s->height = DRAW_COST_SIZE;
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("abcdefghij")});

    box_renderer_desc_t desc = { .width = DRAW_COST_SIZE, .height = DRAW_COST_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);
    box_renderer_set_counters(r, BOX_COUNT_FRAME | BOX_COUNT_PIPELINES);

    int32_t i;
    for (i = 0; i < DRAW_COST_FRAMES; i ++) {
        ecs_progress(w, 0);
        box_packet_t *p = box_renderer_acquire(r);
        p->width = DRAW_COST_SIZE;
        p->height = DRAW_COST_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
    }

    box_counters_t *c = ecs_os_calloc_t(box_counters_t);
    test_bool(box_renderer_counters(r, c), true);
    const box_count_pipeline_t *blended = NULL;
    for (i = 0; i < (int32_t)c->pipeline_count; i ++) {
        if (!ecs_os_strcmp(c->pipelines[i].name, "blended")) {
            blended = &c->pipelines[i];
        }
    }

    test_assert(blended != NULL);
    test_assert(blended->draws > 1);
    test_int(blended->sets, 1);
    test_int(box_renderer_errors(r), 0);
    ecs_os_free(c);
    box_renderer_destroy(r);
    ecs_fini(w);
}

void DrawCost_changing_element_keeps_the_scene_shadows_cached(void)
{
    // ui/ui_draw_cost
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = DRAW_COST_SIZE;
    s->height = DRAW_COST_SIZE;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 4, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 55});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true, .shadows = {.enabled = true}});
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0.5f, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {40, 20});

    box_renderer_desc_t desc = { .width = DRAW_COST_SIZE, .height = DRAW_COST_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    test_assert(r != NULL);

    box_renderer_stats_t stats = {0};
    int32_t i;
    for (i = 0; i < DRAW_COST_FRAMES; i ++) {
        ecs_set(w, panel, Rect, {40.0f + (float)i, 20});
        ecs_progress(w, 0);
        box_packet_t *p = box_renderer_acquire(r);
        p->width = DRAW_COST_SIZE;
        p->height = DRAW_COST_SIZE;
        box_render_extract(w, p);
        box_renderer_submit(r, p);
        box_renderer_stats(r, &stats);
    }

    test_int(stats.shadow_rebuilds, 0);
    test_int(box_renderer_errors(r), 0);
    box_renderer_destroy(r);
    ecs_fini(w);
}
