/**
 * @file Lod.c
 * @brief Tests for the LOD settings of a view and the thresholds they extract into.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Lod_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    packet.clear[3] = 1.0f;
}

void Lod_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Lod_defaults_of_new_settings(void)
{
    // lod/screen_space_error
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, LodSettings);
    const LodSettings *s = ecs_get(w, e, LodSettings);
    test_bool(s->enabled, true);
    test_flt(s->quality, BOX_VIEW_LOD_QUALITY);
    test_flt(s->error_px, BOX_VIEW_LOD_ERROR);
    test_flt(s->shadow_error_px, BOX_VIEW_LOD_SHADOW_ERROR);
    test_flt(s->hysteresis, BOX_VIEW_LOD_HYSTERESIS);
}

void Lod_a_view_without_settings_takes_the_defaults(void)
{
    // lod/screen_space_error
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->lod_enabled, 1);
    test_flt(v->lod_error_px, BOX_VIEW_LOD_ERROR);
    test_flt(v->lod_shadow_error_px, BOX_VIEW_LOD_SHADOW_ERROR);
    test_flt(v->lod_hysteresis, BOX_VIEW_LOD_HYSTERESIS);
}

void Lod_settings_of_a_view_override_the_singleton(void)
{
    // lod/screen_space_error
    ecs_singleton_set(w, LodSettings, {.enabled = true, .quality = 1, .error_px = 8,
        .shadow_error_px = 24, .hysteresis = 0.5f});
    ecs_entity_t cam_a = ecs_new(w);
    ecs_set(w, cam_a, Position, {0, 0, -5});
    ecs_set(w, cam_a, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(w);
    ecs_add(w, a, View);
    View *a_v = ecs_get_mut(w, a, View);
    a_v->camera = cam_a;
    a_v->priority = 0;
    ecs_modified(w, a, View);
    ecs_entity_t cam_b = ecs_new(w);
    ecs_set(w, cam_b, Position, {0, 0, -8});
    ecs_set(w, cam_b, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_add(w, b, View);
    View *b_v = ecs_get_mut(w, b, View);
    b_v->camera = cam_b;
    b_v->priority = 1;
    ecs_modified(w, b, View);
    ecs_set(w, a, LodSettings, {.enabled = true, .quality = 1, .error_px = 1,
        .shadow_error_px = 3, .hysteresis = 0.1f});
    box_render_extract(w, &packet);
    box_view_t *first = box_vec_at(&packet.views, 0);
    box_view_t *second = box_vec_at(&packet.views, 1);
    test_flt(first->lod_error_px, 1.0f);
    test_flt(first->lod_shadow_error_px, 3.0f);
    test_flt(first->lod_hysteresis, 0.1f);
    test_flt(second->lod_error_px, 8.0f);
    test_flt(second->lod_shadow_error_px, 24.0f);
    test_flt(second->lod_hysteresis, 0.5f);
}

void Lod_settings_fall_back_to_the_singleton(void)
{
    // lod/screen_space_error
    ecs_singleton_set(w, LodSettings, {.enabled = true, .quality = 1, .error_px = 8,
        .shadow_error_px = 24, .hysteresis = 0.5f});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_flt(v->lod_error_px, 8.0f);
    test_flt(v->lod_shadow_error_px, 24.0f);
    test_flt(v->lod_hysteresis, 0.5f);
}

void Lod_quality_scales_both_thresholds(void)
{
    // lod/screen_space_error
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    ecs_set(w, e, LodSettings, {.enabled = true, .quality = 2, .error_px = 3,
        .shadow_error_px = 9, .hysteresis = 0.25f});
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_flt(v->lod_error_px, 1.5f);
    test_flt(v->lod_shadow_error_px, 4.5f);
}

void Lod_a_view_can_turn_selection_off(void)
{
    // lod/screen_space_error
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    ecs_set(w, e, LodSettings, {.enabled = false});
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->lod_enabled, 0);
    test_flt(v->lod_error_px, BOX_VIEW_LOD_ERROR);
}

void Lod_the_shadow_threshold_selects_a_coarser_level(void)
{
    // lod/screen_space_error
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    float levels[4] = {0.0f, 1.5f, 5.0f, 30.0f};
    box_lod_select_desc_t primary = { .error_px = levels, .count = 4, .current = -1,
        .threshold = v->lod_error_px, .hysteresis = v->lod_hysteresis };
    box_lod_select_desc_t shadow = { .error_px = levels, .count = 4, .current = -1,
        .threshold = v->lod_shadow_error_px, .hysteresis = v->lod_hysteresis };

    test_int(box_lod_select(&primary), 1);
    test_int(box_lod_select(&shadow), 2);
}
