/**
 * @file Hit.c
 * @brief Tests for resolving a point against the rectangles of a UI tree.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

void Hit_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
}

void Hit_teardown(void)
{
    ecs_fini(w);
}

void Hit_element_under_the_point_is_found(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Position, {50, 20, 0});
    ecs_set(w, panel, Rect, {40, 30});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 50, 20), panel);
    test_uint(box_ui_hit(w, 0, 31, 6), panel);
    test_uint(box_ui_hit(w, 0, 0, 0), root);
}

void Hit_point_outside_every_element_hits_nothing(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 101, 0), 0);
    test_uint(box_ui_hit(w, 0, 0, 51), 0);
    test_uint(box_ui_hit(w, 0, -400, -400), 0);
}

void Hit_deepest_element_under_the_point_wins(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {100, 60});
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, label, Rect, {20, 10});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 0, 0), label);
    test_uint(box_ui_hit(w, 0, 20, 0), panel);
    test_uint(box_ui_hit(w, 0, 80, 0), root);
}

void Hit_later_sibling_wins_where_two_overlap(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Position, {-10, 0, 0});
    ecs_set(w, a, Rect, {60, 40});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Position, {10, 0, 0});
    ecs_set(w, b, Rect, {60, 40});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 0, 0), b);
    test_uint(box_ui_hit(w, 0, -35, 0), a);
    test_uint(box_ui_hit(w, 0, 35, 0), b);
}

void Hit_element_without_a_rectangle_is_not_hit(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t empty = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, empty, Position, {0, 0, 0});
    box_ui_update(w);

    test_flt(ecs_get(w, empty, Bounds)->width, 0);
    test_uint(box_ui_hit(w, 0, 0, 0), root);
}

void Hit_rotated_element_follows_its_corners(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t bar = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, bar, Rect, {100, 20});
    ecs_set(w, bar, Rotation, {0, 0, 45.0f * 3.14159265f / 180.0f});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 0, 0), bar);
    test_uint(box_ui_hit(w, 0, 35, 35), bar);
    test_uint(box_ui_hit(w, 0, 49, 0), 0);
}

void Hit_scaled_element_covers_its_scaled_rectangle(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {40, 30});
    ecs_set(w, panel, Scale, {2, 2, 1});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 35, 0), panel);
    test_uint(box_ui_hit(w, 0, 45, 0), 0);
}

void Hit_element_of_another_view_is_not_hit(void)
{
    // ui/ui_events
    ecs_entity_t first = ecs_new(w);
    ecs_add(w, first, Ui);
    ecs_set(w, first, Rect, {200, 100});
    ecs_entity_t second = ecs_new(w);
    ecs_add(w, second, Ui);
    ecs_set(w, second, Rect, {200, 100});
    box_ui_update(w);

    ecs_entity_t view = ecs_get(w, first, UiImpl)->view;
    ecs_entity_t other = ecs_get(w, second, UiImpl)->view;
    test_assert(view != 0);
    test_assert(other != view);
    test_uint(box_ui_hit(w, view, 0, 0), first);
    test_uint(box_ui_hit(w, other, 0, 0), second);
}

void Hit_clipped_child_of_a_transparent_subtree_is_not_hit(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t inside = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, inside, Position, {50, 0, 0});
    ecs_set(w, inside, Rect, {40, 20});
    ecs_entity_t outside = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, outside, Position, {150, 0, 0});
    ecs_set(w, outside, Rect, {80, 40});
    box_ui_update(w);

    test_assert(ecs_has(w, panel, GroupImpl));
    test_uint(box_ui_hit(w, 0, 50, 0), inside);
    test_uint(box_ui_hit(w, 0, 150, 0), 0);
}

void Hit_glyphs_of_a_label_are_not_hit(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, label, Text, {.value = ecs_os_strdup("Hello")});
    box_ui_update(w);

    const Bounds *b = ecs_get(w, label, Bounds);
    test_assert(b->width > 0);
    test_int(ecs_count_id(w, UiGlyph), 5);
    test_uint(box_ui_hit(w, 0, b->x, b->y), label);
}

void Hit_element_that_left_the_tree_is_not_hit(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {40, 30});
    box_ui_update(w);
    test_uint(box_ui_hit(w, 0, 0, 0), panel);

    ecs_remove_pair(w, panel, EcsChildOf, EcsWildcard);
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 0, 0), root);
}

void Hit_moved_element_is_hit_where_it_went(void)
{
    // ui/ui_events
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Position, {0, 0, 0});
    ecs_set(w, panel, Rect, {40, 30});
    box_ui_update(w);
    test_uint(box_ui_hit(w, 0, 0, 0), panel);

    ecs_set(w, panel, Position, {100, 0, 0});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 0, 0), 0);
    test_uint(box_ui_hit(w, 0, 100, 0), panel);
}

void Hit_world_without_a_tree_hits_nothing(void)
{
    // ui/ui_events
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Rect, {200, 100});
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 0, 0), 0);
}

void Hit_a_hit_through_element_hands_the_point_to_its_parent(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {100, 50});
    ecs_entity_t caption = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, caption, Rect, {40, 20});
    ecs_add_id(w, caption, HitThrough);
    box_ui_update(w);

    test_uint(box_ui_hit(w, 0, 0, 0), panel);

    ecs_remove_id(w, caption, HitThrough);
    box_ui_update(w);
    test_uint(box_ui_hit(w, 0, 0, 0), caption);
}
