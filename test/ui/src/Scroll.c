/**
 * @file Scroll.c
 * @brief Tests for the overflow modes of an element, its clip and its offset.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Scroll_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 960;
    packet.height = 540;
    packet.clear[3] = 1.0f;
}

void Scroll_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scroll_visible_axis_does_not_clip(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .x = OverflowVisible, .y = OverflowVisible });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    test_assert(!ecs_has(w, item, RenderClip));
    test_flt(ecs_get(w, item, Bounds)->y, -150);
}

void Scroll_visible_axis_keeps_the_offset_at_zero(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowVisible, .offset = {0, 60} });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.y, 0);
    test_flt(ecs_get(w, item, Bounds)->y, -150);
}

void Scroll_hidden_clips_the_children_to_the_box(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowHidden });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    const RenderClip *clip = ecs_get(w, item, RenderClip);
    test_assert(clip != NULL);
    test_flt(clip->min[1], -50);
    test_flt(clip->max[1], 50);
    test_assert(clip->min[0] < -1e6f);
    test_assert(clip->max[0] > 1e6f);
}

void Scroll_hidden_takes_no_wheel(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowHidden });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    ecs_add_id(w, item, Hovered);
    Input *in = ecs_singleton_ensure(w, Input);
    in->mouse.scroll_delta.y = -1;
    ecs_singleton_modified(w, Input);
    box_ui_scroll_update(w);
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.y, 0);
}

void Scroll_scroll_clips_and_takes_the_wheel(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowScroll });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    test_assert(ecs_has(w, item, RenderClip));
    ecs_add_id(w, item, Hovered);
    Input *in = ecs_singleton_ensure(w, Input);
    in->mouse.scroll_delta.y = -1;
    ecs_singleton_modified(w, Input);
    box_ui_scroll_update(w);
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.y, BOX_UI_SCROLL_STEP);
}

void Scroll_auto_scrolls_when_the_content_is_larger(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowAuto });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    test_flt(ecs_get(w, list, ScrollImpl)->content_height, 400);
    test_flt(ecs_get(w, list, ScrollImpl)->max_y, 300);
    test_assert(ecs_has(w, item, RenderClip));
}

void Scroll_auto_does_not_scroll_when_the_content_fits(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 400 });
    ecs_set(w, list, Scroll, { .y = OverflowAuto });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 100});
    box_ui_update(w);

    test_flt(ecs_get(w, list, ScrollImpl)->max_y, 0);
    ecs_add_id(w, item, Hovered);
    Input *in = ecs_singleton_ensure(w, Input);
    in->mouse.scroll_delta.y = -1;
    ecs_singleton_modified(w, Input);
    box_ui_scroll_update(w);
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.y, 0);
}

void Scroll_offset_is_clamped_to_the_content(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowAuto, .offset = {0, 5000} });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.y, 300);
}

void Scroll_offset_below_zero_is_clamped_away(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowAuto, .offset = {0, -80} });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.y, 0);
}

void Scroll_offset_moves_the_children_up(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowAuto });
    ecs_entity_t first = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, first, Rect, {200, 100});
    ecs_entity_t second = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, second, Rect, {200, 100});
    box_ui_update(w);

    test_flt(ecs_get(w, first, Bounds)->y, 0);
    ecs_set(w, list, Scroll, { .y = OverflowAuto, .offset = {0, 100} });
    box_ui_update(w);

    test_flt(ecs_get(w, first, Bounds)->y, 100);
    test_flt(ecs_get(w, second, Bounds)->y, 0);
}

void Scroll_offset_moves_the_children_left(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionRow, .width = 100,
        .height = 100 });
    ecs_set(w, list, Scroll, { .x = OverflowAuto, .offset = {80, 0} });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {400, 100});
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.x, 80);
    test_flt(ecs_get(w, item, Bounds)->x, 70);

    const RenderClip *clip = ecs_get(w, item, RenderClip);
    test_assert(clip != NULL);
    test_flt(clip->min[0], -50);
    test_flt(clip->max[0], 50);
}

void Scroll_anchored_child_is_not_moved_by_the_offset(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowAuto });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    ecs_entity_t badge = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, badge, Rect, {20, 20});
    ecs_set(w, badge, Anchor, { .h = AlignRight, .v = VAlignTop });
    box_ui_update(w);
    ecs_set(w, list, Scroll, { .y = OverflowAuto, .offset = {0, 100} });
    box_ui_update(w);

    test_flt(ecs_get(w, badge, Bounds)->y, 40);
}

void Scroll_nested_clips_intersect(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t outer = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, outer, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, outer, Scroll, { .y = OverflowHidden });
    ecs_entity_t inner = ecs_new_w_pair(w, EcsChildOf, outer);
    ecs_set(w, inner, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 400 });
    ecs_set(w, inner, Scroll, { .x = OverflowHidden });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, inner);
    ecs_set(w, item, Rect, {400, 400});
    box_ui_update(w);

    const RenderClip *clip = ecs_get(w, item, RenderClip);
    test_assert(clip != NULL);
    test_flt(clip->min[1], -50);
    test_flt(clip->max[1], 50);
    test_flt(clip->min[0], -100);
    test_flt(clip->max[0], 100);
}

void Scroll_wheel_moves_the_hovered_element(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowAuto });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    ecs_add_id(w, list, Hovered);
    Input *in = ecs_singleton_ensure(w, Input);
    in->mouse.scroll_delta.y = -2;
    ecs_singleton_modified(w, Input);
    box_ui_scroll_update(w);
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.y, 2 * BOX_UI_SCROLL_STEP);
}

void Scroll_wheel_leaves_an_unhovered_element_alone(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowAuto });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    Input *in = ecs_singleton_ensure(w, Input);
    in->mouse.scroll_delta.y = -2;
    ecs_singleton_modified(w, Input);
    box_ui_scroll_update(w);
    box_ui_update(w);

    test_flt(ecs_get(w, list, Scroll)->offset.y, 0);
}

void Scroll_wheel_moves_the_innermost_scrollable_ancestor(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t outer = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, outer, Layout, { .direction = DirectionColumn, .width = 400,
        .height = 200 });
    ecs_set(w, outer, Scroll, { .y = OverflowAuto });
    ecs_entity_t inner = ecs_new_w_pair(w, EcsChildOf, outer);
    ecs_set(w, inner, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, inner, Scroll, { .y = OverflowAuto });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, inner);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    ecs_add_id(w, item, Hovered);
    Input *in = ecs_singleton_ensure(w, Input);
    in->mouse.scroll_delta.y = -1;
    ecs_singleton_modified(w, Input);
    box_ui_scroll_update(w);
    box_ui_update(w);

    test_flt(ecs_get(w, inner, Scroll)->offset.y, BOX_UI_SCROLL_STEP);
    test_flt(ecs_get(w, outer, Scroll)->offset.y, 0);
}

void Scroll_wheel_passes_through_an_element_that_cannot_scroll(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t outer = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, outer, Layout, { .direction = DirectionColumn, .width = 400,
        .height = 100 });
    ecs_set(w, outer, Scroll, { .y = OverflowAuto });
    ecs_entity_t inner = ecs_new_w_pair(w, EcsChildOf, outer);
    ecs_set(w, inner, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 400 });
    ecs_set(w, inner, Scroll, { .y = OverflowAuto });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, inner);
    ecs_set(w, item, Rect, {200, 100});
    box_ui_update(w);

    ecs_add_id(w, item, Hovered);
    Input *in = ecs_singleton_ensure(w, Input);
    in->mouse.scroll_delta.y = -1;
    ecs_singleton_modified(w, Input);
    box_ui_scroll_update(w);
    box_ui_update(w);

    test_flt(ecs_get(w, inner, Scroll)->offset.y, 0);
    test_flt(ecs_get(w, outer, Scroll)->offset.y, BOX_UI_SCROLL_STEP);
}

void Scroll_clip_reaches_the_instance_of_the_packet(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowHidden });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);
    box_render_extract(w, &packet);

    bool seen = false;
    uint32_t i;
    for (i = 0; i < packet.static_updates.count; i ++) {
        box_static_update_t *update = box_vec_at(&packet.static_updates, i);
        if (!box_instance_clipped(&update->data)) {
            continue;
        }

        test_flt(update->data.clip[1], -50);
        test_flt(update->data.clip[3], 50);
        seen = true;
    }

    test_bool(seen, true);
}

void Scroll_removing_scroll_drops_the_clip(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowHidden });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    test_assert(ecs_has(w, item, RenderClip));
    ecs_remove(w, list, Scroll);
    box_ui_update(w);

    test_assert(!ecs_has(w, item, RenderClip));
}

void Scroll_glyphs_of_a_scrolled_text_are_clipped(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t log = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, log, Layout, { .width = 200, .height = 30 });
    ecs_set(w, log, Scroll, { .y = OverflowAuto });
    ecs_set(w, log, Text, { .value = ecs_os_strdup("one\ntwo\nthree\nfour") });
    box_ui_update(w);

    int32_t glyphs = 0, clipped = 0;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            glyphs ++;
            clipped += ecs_has(w, it.entities[i], RenderClip) ? 1 : 0;
        }
    }

    test_assert(glyphs > 0);
    test_int(clipped, glyphs);
}

void Scroll_offset_moves_the_glyphs_of_a_text(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t log = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, log, Layout, { .width = 200, .height = 30 });
    ecs_set(w, log, Scroll, { .y = OverflowAuto });
    ecs_set(w, log, Text, { .value = ecs_os_strdup("one\ntwo\nthree\nfour") });
    box_ui_update(w);

    float was = ecs_get(w, log, Bounds)->y;
    ecs_iter_t it = ecs_each_id(w, UiGlyph);
    float first = 0;
    bool found = false;
    while (!found && ecs_each_next(&it)) {
        if (it.count) {
            first = ecs_get(w, it.entities[0], Bounds)->y;
            found = true;
        }
    }

    if (found) {
        ecs_iter_fini(&it);
    }

    test_bool(found, true);
    test_flt(ecs_get(w, log, Bounds)->y, was);

    float max = ecs_get(w, log, ScrollImpl)->max_y;
    test_assert(max > 0);
    ecs_set(w, log, Scroll, { .y = OverflowAuto, .offset = {0, max} });
    box_ui_update(w);

    it = ecs_each_id(w, UiGlyph);
    float moved = 0;
    found = false;
    while (!found && ecs_each_next(&it)) {
        if (it.count) {
            moved = ecs_get(w, it.entities[0], Bounds)->y;
            found = true;
        }
    }

    if (found) {
        ecs_iter_fini(&it);
    }

    test_assert(fabsf(moved - first - max) < 1.0f);
}

void Scroll_log_that_grows_stays_at_the_end(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t log = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, log, Layout, { .width = 200, .height = 30 });
    ecs_set(w, log, Scroll, { .y = OverflowAuto });
    ecs_set(w, log, Text, { .value = ecs_os_strdup("one") });
    box_ui_update(w);

    test_flt(ecs_get(w, log, Scroll)->offset.y, 0);
    ecs_set(w, log, Text, {
        .value = ecs_os_strdup("one\ntwo\nthree\nfour\nfive") });
    box_ui_update(w);

    float max = ecs_get(w, log, ScrollImpl)->max_y;
    test_assert(max > 0);
    test_flt(ecs_get(w, log, Scroll)->offset.y, max);
}

void Scroll_log_that_grows_stays_put_after_a_scroll_up(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t log = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, log, Layout, { .width = 200, .height = 30 });
    ecs_set(w, log, Scroll, { .y = OverflowAuto });
    ecs_set(w, log, Text, { .value = ecs_os_strdup("one") });
    box_ui_update(w);
    ecs_set(w, log, Text, {
        .value = ecs_os_strdup("one\ntwo\nthree\nfour\nfive") });
    box_ui_update(w);

    ecs_add_id(w, log, Hovered);
    Input *in = ecs_singleton_ensure(w, Input);
    in->mouse.scroll_delta.y = 1;
    ecs_singleton_modified(w, Input);
    box_ui_scroll_update(w);
    box_ui_update(w);

    float scrolled = ecs_get(w, log, Scroll)->offset.y;
    test_assert(scrolled < ecs_get(w, log, ScrollImpl)->max_y);
    ecs_set(w, log, Text, {
        .value = ecs_os_strdup("one\ntwo\nthree\nfour\nfive\nsix\nseven") });
    box_ui_update(w);

    test_flt(ecs_get(w, log, Scroll)->offset.y, scrolled);
}

void Scroll_hit_test_skips_a_clipped_node(void)
{
    // ore_engine/ui_scroll
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t list = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, list, Layout, { .direction = DirectionColumn, .width = 200,
        .height = 100 });
    ecs_set(w, list, Scroll, { .y = OverflowHidden });
    ecs_entity_t item = ecs_new_w_pair(w, EcsChildOf, list);
    ecs_set(w, item, Rect, {200, 400});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 0, 0), item);
    test_uint(box_ui_hit(w, 0, 0, -120), 0);
}
