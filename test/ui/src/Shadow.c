/**
 * @file Shadow.c
 * @brief Tests for BoxShadow: the quad it draws, its size, its place and its depth.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

void Shadow_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
}

void Shadow_teardown(void)
{
    ecs_fini(w);
}

void Shadow_element_gets_a_quad(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, BoxShadow, {.blur = 8, .color = {0, 0, 0, 128}});
    box_ui_update(w);

    const ShadowImpl *impl = ecs_get(w, panel, ShadowImpl);
    test_assert(impl != NULL);
    test_assert(impl->quad != 0);
    test_assert(ecs_has_id(w, impl->quad, UiShadow));
    test_assert(ecs_has_id(w, impl->quad, UiQuad));
    test_uint(box_hierarchy_parent(w, impl->quad), panel);
}

void Shadow_quad_covers_the_box_plus_the_spread_and_the_blur(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, BoxShadow, {.blur = 8, .spread = 4, .color = {0, 0, 0, 128}});
    box_ui_update(w);

    ecs_entity_t quad = ecs_get(w, panel, ShadowImpl)->quad;
    const Rect *rect = ecs_get(w, quad, Rect);
    test_assert(rect != NULL);
    test_flt(rect->width, 224);
    test_flt(rect->height, 124);
}

void Shadow_quad_is_offset_from_its_element(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, BoxShadow, {.x = 6, .y = 10, .color = {0, 0, 0, 128}});
    box_ui_update(w);

    ecs_entity_t quad = ecs_get(w, panel, ShadowImpl)->quad;
    const Position *pos = ecs_get(w, quad, Position);
    test_assert(pos != NULL);
    test_flt(pos->x, 6);
    test_flt(pos->y, -10);
}

void Shadow_quad_is_drawn_behind_its_element(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {400, 300});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, BoxShadow, {.blur = 8, .color = {0, 0, 0, 128}});
    box_ui_update(w);

    ecs_entity_t quad = ecs_get(w, panel, ShadowImpl)->quad;
    float root_depth = ecs_get(w, root, RenderInstance)->depth;
    float panel_depth = ecs_get(w, panel, RenderInstance)->depth;
    float quad_depth = ecs_get(w, quad, RenderInstance)->depth;

    test_assert(quad_depth > panel_depth);
    test_assert(quad_depth < root_depth);
    test_flt(quad_depth, -(1.0f - BOX_UI_SHADOW_STEP) * BOX_UI_STEP);
}

void Shadow_quad_takes_the_radii_of_the_element(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Border, {.radius = 12});
    ecs_set(w, panel, BoxShadow, {.blur = 6, .spread = 2, .color = {0, 0, 0, 128}});
    box_ui_update(w);

    ecs_entity_t quad = ecs_get(w, panel, ShadowImpl)->quad;
    const RenderShape2D *shape = ecs_get(w, quad, RenderShape2D);
    test_assert(shape != NULL);
    test_assert(ecs_has_id(w, quad, RenderSoftShape));
    test_flt(shape->radii[0], 20);
    test_flt(shape->radii[2], 20);
    test_flt(shape->widths[0], 6);
    test_flt(shape->widths[3], 6);
}

void Shadow_transparent_shadow_draws_no_quad(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, BoxShadow, {.blur = 8, .color = {0, 0, 0, 0}});
    box_ui_update(w);

    test_uint(ecs_get(w, panel, ShadowImpl)->quad, 0);
}

void Shadow_quad_takes_no_room_in_a_row(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow, .spacing = 10});
    ecs_entity_t first = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, first, Rect, {100, 40});
    ecs_set(w, first, BoxShadow, {.blur = 20, .spread = 20, .color = {0, 0, 0, 200}});
    ecs_entity_t second = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, second, Rect, {60, 40});
    box_ui_update(w);

    test_flt(ecs_get(w, root, Bounds)->width, 170);
    test_flt(ecs_get(w, second, Bounds)->x - ecs_get(w, first, Bounds)->x, 90);
}

void Shadow_removing_the_shadow_drops_the_quad(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, BoxShadow, {.blur = 8, .color = {0, 0, 0, 128}});
    box_ui_update(w);
    ecs_entity_t quad = ecs_get(w, panel, ShadowImpl)->quad;
    test_assert(ecs_is_alive(w, quad));

    ecs_remove(w, panel, BoxShadow);
    box_ui_update(w);

    test_assert(!ecs_is_alive(w, quad));
    test_assert(!ecs_has(w, panel, ShadowImpl));
}

void Shadow_quad_extracts_as_a_soft_shape(void)
{
    // ui/ui_visuals
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 960;
    packet.height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, BoxShadow, {.blur = 8, .color = {0, 0, 0, 128}});
    box_ui_update(w);
    box_render_extract(w, &packet);

    ecs_entity_t quad = ecs_get(w, panel, ShadowImpl)->quad;
    const RenderInstance *inst = ecs_get(w, quad, RenderInstance);
    test_assert(inst != NULL);
    test_bool(inst->transparent, true);

    bool seen = false;
    uint32_t i;
    for (i = 0; i < packet.transparent.count; i ++) {
        box_transparent_t *entry = box_vec_at(&packet.transparent, i);
        if (box_instance_soft(&entry->data)) {
            test_bool(box_instance_shape(&entry->data), true);
            test_uint(entry->blend, BOX_BLEND_ALPHA);
            seen = true;
        }
    }

    test_bool(seen, true);
    box_packet_fini(&packet);
}
