/**
 * @file Anchor.c
 * @brief Tests for pinning an element to an edge or the centre of its parent.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

void Anchor_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
}

void Anchor_teardown(void)
{
    ecs_fini(w);
}

void Anchor_pins_to_the_edges_of_the_parent(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    float x[] = {-80, 0, 80};
    float y[] = {40, 0, -40};
    int32_t h, v;

    for (h = 0; h < 3; h ++) {
        for (v = 0; v < 3; v ++) {
            ecs_set(w, a, Anchor, {.h = (Align)h, .v = (VAlign)v});
            box_ui_update(w);

            test_flt(ecs_get(w, a, Bounds)->x, x[h]);
            test_flt(ecs_get(w, a, Bounds)->y, y[v]);
            test_flt(ecs_get(w, a, Position)->x, x[h]);
            test_flt(ecs_get(w, a, Position)->y, y[v]);
        }
    }
}

void Anchor_offset_moves_toward_the_inside(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_set(w, a, Anchor, {.h = AlignLeft, .v = VAlignTop, .x = 5, .y = 5});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {40, 20});
    ecs_set(w, b, Anchor, {.h = AlignRight, .v = VAlignBottom, .x = 5, .y = 5});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -75);
    test_flt(ecs_get(w, a, Bounds)->y, 35);
    test_flt(ecs_get(w, b, Bounds)->x, 75);
    test_flt(ecs_get(w, b, Bounds)->y, -35);
}

void Anchor_offset_in_the_centre_follows_the_axes(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_set(w, a, Anchor, {.h = AlignCenter, .v = VAlignMiddle,
        .x = 5, .y = 7});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, 5);
    test_flt(ecs_get(w, a, Bounds)->y, 7);
}

void Anchor_uses_the_padding_of_the_parent(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.width = 200, .height = 100, .padding = 10});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_set(w, a, Anchor, {.h = AlignLeft, .v = VAlignTop});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -70);
    test_flt(ecs_get(w, a, Bounds)->y, 30);
}

void Anchor_anchored_child_takes_no_room_in_a_row(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t badge = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, badge, Rect, {60, 30});
    ecs_set(w, badge, Anchor, {.h = AlignRight, .v = VAlignBottom});
    ecs_entity_t c = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, c, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -80);
    test_flt(ecs_get(w, c, Bounds)->x, -40);
    test_flt(ecs_get(w, badge, Bounds)->x, 70);
    test_flt(ecs_get(w, badge, Bounds)->y, -35);
}

void Anchor_anchored_child_does_not_size_its_parent(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t badge = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, badge, Rect, {200, 200});
    ecs_set(w, badge, Anchor, {.h = AlignRight, .v = VAlignTop});
    box_ui_update(w);

    test_flt(ecs_get(w, root, Bounds)->width, 40);
    test_flt(ecs_get(w, root, Bounds)->height, 20);
}

void Anchor_anchor_of_a_nested_element_follows_its_parent(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {400, 200});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Position, {50, -20, 0});
    ecs_set(w, panel, Rect, {100, 60});
    ecs_entity_t dot = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, dot, Rect, {10, 10});
    ecs_set(w, dot, Anchor, {.h = AlignRight, .v = VAlignTop});
    box_ui_update(w);

    test_flt(ecs_get(w, dot, Bounds)->x, 95);
    test_flt(ecs_get(w, dot, Bounds)->y, 5);
    test_flt(ecs_get(w, dot, Position)->x, 45);
    test_flt(ecs_get(w, dot, Position)->y, 25);
}

void Anchor_changed_anchor_lays_out_the_tree_again(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_set(w, a, Anchor, {.h = AlignLeft, .v = VAlignTop});
    box_ui_update(w);
    test_flt(ecs_get(w, a, Bounds)->x, -80);

    ecs_set(w, a, Anchor, {.h = AlignRight, .v = VAlignTop});
    test_bool(ecs_get(w, root, UiImpl)->relayout, true);
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, 80);
}

void Anchor_removed_anchor_returns_the_child_to_the_row(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_set(w, a, Anchor, {.h = AlignRight, .v = VAlignBottom});
    box_ui_update(w);
    test_flt(ecs_get(w, a, Bounds)->x, 80);

    ecs_remove(w, a, Anchor);
    test_bool(ecs_get(w, root, UiImpl)->relayout, true);
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -80);
    test_flt(ecs_get(w, a, Bounds)->y, 40);
}
