/**
 * @file Layout.c
 * @brief Tests for the boxes the layout pass computes for the nodes of a tree.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

void Layout_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
}

void Layout_teardown(void)
{
    ecs_fini(w);
}

void Layout_every_node_gets_bounds(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Position, {10, 20, 0});
    ecs_set(w, panel, Rect, {40, 30});
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, label, Position, {-5, 0, 0});
    ecs_set(w, label, Rect, {10, 10});
    box_ui_update(w);

    test_assert(ecs_has(w, root, Bounds));
    test_flt(ecs_get(w, root, Bounds)->x, 0);
    test_flt(ecs_get(w, root, Bounds)->y, 0);
    test_flt(ecs_get(w, root, Bounds)->width, 200);
    test_flt(ecs_get(w, root, Bounds)->height, 100);
    test_flt(ecs_get(w, panel, Bounds)->x, 10);
    test_flt(ecs_get(w, panel, Bounds)->y, 20);
    test_flt(ecs_get(w, panel, Bounds)->width, 40);
    test_flt(ecs_get(w, label, Bounds)->x, 5);
    test_flt(ecs_get(w, label, Bounds)->y, 20);
    test_flt(ecs_get(w, label, Bounds)->height, 10);
}

void Layout_row_places_children_left_to_right(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {60, 30});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -80);
    test_flt(ecs_get(w, a, Bounds)->y, 40);
    test_flt(ecs_get(w, b, Bounds)->x, -30);
    test_flt(ecs_get(w, b, Bounds)->y, 35);
    test_flt(ecs_get(w, a, Position)->x, -80);
    test_flt(ecs_get(w, a, Position)->y, 40);
    test_flt(ecs_get(w, b, Position)->x, -30);
}

void Layout_column_places_children_top_to_bottom(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionColumn,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {60, 30});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -80);
    test_flt(ecs_get(w, a, Bounds)->y, 40);
    test_flt(ecs_get(w, b, Bounds)->x, -70);
    test_flt(ecs_get(w, b, Bounds)->y, 15);
}

void Layout_padding_insets_the_children(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100, .padding = 10});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -70);
    test_flt(ecs_get(w, a, Bounds)->y, 30);
}

void Layout_spacing_separates_the_children(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100, .spacing = 8});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {40, 20});
    box_ui_update(w);

    const Bounds *first = ecs_get(w, a, Bounds);
    const Bounds *second = ecs_get(w, b, Bounds);
    test_flt(first->x, -80);
    test_flt(second->x, -32);
    test_flt((second->x - second->width / 2) - (first->x + first->width / 2), 8);
}

void Layout_row_grows_to_fit_its_children(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .padding = 5, .spacing = 4});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {60, 30});
    box_ui_update(w);

    test_flt(ecs_get(w, root, Bounds)->width, 114);
    test_flt(ecs_get(w, root, Bounds)->height, 40);
    test_flt(ecs_get(w, a, Bounds)->x, -32);
    test_flt(ecs_get(w, a, Bounds)->y, 5);
    test_flt(ecs_get(w, b, Bounds)->x, 22);
    test_flt(ecs_get(w, b, Bounds)->y, 0);
}

void Layout_column_grows_to_fit_its_children(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionColumn,
        .padding = 5, .spacing = 4});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {60, 30});
    box_ui_update(w);

    test_flt(ecs_get(w, root, Bounds)->width, 70);
    test_flt(ecs_get(w, root, Bounds)->height, 64);
    test_flt(ecs_get(w, a, Bounds)->y, 17);
    test_flt(ecs_get(w, b, Bounds)->y, -12);
}

void Layout_layout_resizes_the_rect_of_its_element(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {0, 0});
    ecs_set(w, root, Layout, {.direction = DirectionRow, .padding = 5});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, root, Rect)->width, 50);
    test_flt(ecs_get(w, root, Rect)->height, 30);
    test_flt(ecs_get(w, root, Bounds)->width, 50);
}

void Layout_rect_gives_the_size_when_the_layout_has_none(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {200, 100});
    ecs_set(w, root, Layout, {.direction = DirectionRow});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, root, Bounds)->width, 200);
    test_flt(ecs_get(w, root, Bounds)->height, 100);
    test_flt(ecs_get(w, root, Rect)->width, 200);
    test_flt(ecs_get(w, a, Bounds)->x, -80);
}

void Layout_row_aligns_its_children_on_both_axes(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {60, 30});
    float start[] = {-100, -50, 0};
    float first_y[] = {40, 0, -40};
    float second_y[] = {35, 0, -35};
    int32_t h, v;

    for (h = 0; h < 3; h ++) {
        for (v = 0; v < 3; v ++) {
            ecs_set(w, root, Layout, {.direction = DirectionRow,
                .width = 200, .height = 100, .h = (Align)h, .v = (VAlign)v});
            box_ui_update(w);

            test_flt(ecs_get(w, a, Bounds)->x, start[h] + 20);
            test_flt(ecs_get(w, a, Bounds)->y, first_y[v]);
            test_flt(ecs_get(w, b, Bounds)->x, start[h] + 70);
            test_flt(ecs_get(w, b, Bounds)->y, second_y[v]);
        }
    }
}

void Layout_column_aligns_its_children_on_both_axes(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {60, 30});
    float start[] = {50, 25, 0};
    float first_x[] = {-80, 0, 80};
    float second_x[] = {-70, 0, 70};
    int32_t h, v;

    for (h = 0; h < 3; h ++) {
        for (v = 0; v < 3; v ++) {
            ecs_set(w, root, Layout, {.direction = DirectionColumn,
                .width = 200, .height = 100, .h = (Align)h, .v = (VAlign)v});
            box_ui_update(w);

            test_flt(ecs_get(w, a, Bounds)->x, first_x[h]);
            test_flt(ecs_get(w, a, Bounds)->y, start[v] - 10);
            test_flt(ecs_get(w, b, Bounds)->x, second_x[h]);
            test_flt(ecs_get(w, b, Bounds)->y, start[v] - 35);
        }
    }
}

void Layout_nested_rows_and_columns(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionColumn,
        .padding = 10, .spacing = 5});
    ecs_entity_t row = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, row, Layout, {.direction = DirectionRow, .spacing = 4});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, a, Rect, {30, 10});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, b, Rect, {20, 40});
    ecs_entity_t foot = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, foot, Rect, {50, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, root, Bounds)->width, 74);
    test_flt(ecs_get(w, root, Bounds)->height, 85);
    test_flt(ecs_get(w, row, Bounds)->width, 54);
    test_flt(ecs_get(w, row, Bounds)->height, 40);
    test_flt(ecs_get(w, row, Bounds)->x, 0);
    test_flt(ecs_get(w, row, Bounds)->y, 12.5);
    test_flt(ecs_get(w, a, Bounds)->x, -12);
    test_flt(ecs_get(w, a, Bounds)->y, 27.5);
    test_flt(ecs_get(w, a, Position)->y, 15);
    test_flt(ecs_get(w, b, Bounds)->x, 17);
    test_flt(ecs_get(w, b, Bounds)->y, 12.5);
    test_flt(ecs_get(w, foot, Bounds)->x, -2);
    test_flt(ecs_get(w, foot, Bounds)->y, -22.5);
}

void Layout_direction_none_keeps_the_authored_position(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Position, {10, 20, 0});
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Position)->x, 10);
    test_flt(ecs_get(w, a, Position)->y, 20);
    test_flt(ecs_get(w, a, Bounds)->x, 10);
    test_flt(ecs_get(w, a, Bounds)->y, 20);
}

void Layout_root_position_offsets_the_tree(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Position, {100, -50, 0});
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, root, Bounds)->x, 100);
    test_flt(ecs_get(w, root, Bounds)->y, -50);
    test_flt(ecs_get(w, a, Bounds)->x, 20);
    test_flt(ecs_get(w, a, Bounds)->y, -10);
    test_flt(ecs_get(w, a, Position)->x, -80);
    test_flt(ecs_get(w, a, Position)->y, 40);
}

void Layout_changed_child_lays_out_the_tree_again(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {60, 30});
    box_ui_update(w);
    test_flt(ecs_get(w, b, Bounds)->x, -30);

    ecs_set(w, a, Rect, {100, 20});
    test_bool(ecs_get(w, root, UiImpl)->relayout, true);
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -50);
    test_flt(ecs_get(w, b, Bounds)->x, 30);
}

void Layout_changed_layout_lays_out_the_tree_again(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);
    test_flt(ecs_get(w, a, Bounds)->x, -80);

    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100, .h = AlignRight});
    test_bool(ecs_get(w, root, UiImpl)->relayout, true);
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, 80);
}

void Layout_node_added_later_is_laid_out(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {60, 30});
    box_ui_update(w);

    test_assert(ecs_has(w, b, Bounds));
    test_flt(ecs_get(w, b, Bounds)->x, -30);
    test_flt(ecs_get(w, b, Position)->x, -30);
}

void Layout_tree_that_did_not_change_is_not_laid_out(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_bool(ecs_get(w, root, UiImpl)->dirty, false);
    test_bool(ecs_get(w, root, UiImpl)->relayout, false);

    Bounds *bounds = ecs_get_mut(w, a, Bounds);
    bounds->x = 1234;
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, 1234);
}

void Layout_node_that_leaves_the_tree_loses_its_bounds(void)
{
    // ui/layout
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);
    test_assert(ecs_has(w, a, Bounds));

    ecs_remove_pair(w, a, EcsChildOf, root);
    box_ui_update(w);

    test_assert(!ecs_has(w, a, Bounds));
}

void Layout_moved_leaf_lays_out_its_subtree_only(void)
{
    // ui/layout_subtree
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow,
        .width = 400, .height = 200});
    ecs_entity_t left = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, left, Layout, {.direction = DirectionColumn,
        .width = 100, .height = 100, .v = VAlignTop});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, left);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, left);
    ecs_set(w, b, Rect, {40, 20});
    ecs_entity_t right = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, right, Layout, {.direction = DirectionColumn,
        .width = 100, .height = 100, .v = VAlignTop});
    ecs_entity_t c = ecs_new_w_pair(w, EcsChildOf, right);
    ecs_set(w, c, Rect, {40, 20});
    box_ui_update(w);

    float y = ecs_get(w, b, Bounds)->y;
    ecs_get_mut(w, c, Bounds)->x = 1234;
    ecs_set(w, a, Rect, {40, 40});
    box_ui_update(w);

    test_flt(ecs_get(w, b, Bounds)->y, y - 20);
    test_flt(ecs_get(w, left, Bounds)->height, 100);
    test_flt(ecs_get(w, c, Bounds)->x, 1234);
}

void Layout_content_sized_ancestors_are_laid_out_again(void)
{
    // ui/layout_subtree
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionColumn,
        .width = 400, .height = 200, .h = AlignCenter});
    ecs_entity_t outer = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, outer, Layout, {.direction = DirectionColumn});
    ecs_entity_t inner = ecs_new_w_pair(w, EcsChildOf, outer);
    ecs_set(w, inner, Layout, {.direction = DirectionColumn});
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, inner);
    ecs_set(w, leaf, Rect, {40, 20});
    ecs_entity_t other = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, other, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, inner, Bounds)->height, 20);
    ecs_get_mut(w, other, Bounds)->x = 1234;
    ecs_set(w, leaf, Rect, {40, 60});
    box_ui_update(w);

    test_flt(ecs_get(w, inner, Bounds)->height, 60);
    test_flt(ecs_get(w, outer, Bounds)->height, 60);
    test_flt(ecs_get(w, root, Bounds)->height, 200);
    test_flt(ecs_get(w, other, Bounds)->x, 0);
}

void Layout_changed_leaf_keeps_the_places_of_the_tree(void)
{
    // ui/layout_subtree
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {400, 200});
    ecs_entity_t first = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, first, Rect, {100, 100});
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, first);
    ecs_set(w, leaf, Rect, {40, 20});
    ecs_entity_t second = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, second, Rect, {100, 100});
    ecs_entity_t last = ecs_new_w_pair(w, EcsChildOf, second);
    ecs_set(w, last, Rect, {40, 20});
    box_ui_update(w);

    test_int(ecs_get(w, last, UiNode)->order, 4);
    ecs_get_mut(w, last, RenderInstance)->depth = 5;
    ecs_set(w, leaf, Rect, {60, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, leaf, Bounds)->width, 60);
    test_int(ecs_get(w, last, UiNode)->order, 4);
    test_flt(ecs_get(w, last, RenderInstance)->depth, 5);
}

void Layout_structural_change_numbers_the_tree_again(void)
{
    // ui/layout_subtree
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {400, 200});
    ecs_entity_t first = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, first, Layout, {.direction = DirectionNone});
    ecs_set(w, first, Rect, {100, 100});
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, first);
    ecs_set(w, leaf, Rect, {40, 20});
    ecs_entity_t second = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, second, Rect, {100, 100});
    ecs_entity_t last = ecs_new_w_pair(w, EcsChildOf, second);
    ecs_set(w, last, Rect, {40, 20});
    box_ui_update(w);

    ecs_get_mut(w, last, RenderInstance)->depth = 5;
    ecs_entity_t added = ecs_new_w_pair(w, EcsChildOf, first);
    ecs_set(w, added, Rect, {40, 20});
    box_ui_update(w);

    test_int(ecs_get(w, added, UiNode)->order, 3);
    test_int(ecs_get(w, last, UiNode)->order, 5);
    test_flt(ecs_get(w, last, RenderInstance)->depth, -5 * BOX_UI_STEP);
}

void Layout_fill_sizes_an_element_as_a_fraction_of_its_parent(void)
{
    // ore_engine/console_pane
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {400, 200});
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Layout, {.width = 300, .height = 200, .padding = 10});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Fill, {.width = 0.5f, .height = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, child, Bounds)->width, 140);
    test_flt(ecs_get(w, child, Bounds)->height, 180);
}

void Layout_fill_of_the_root_follows_the_view(void)
{
    // ore_engine/console_pane
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 960;
    surface->height = 540;
    surface->point_width = 960;
    surface->point_height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Fill, {.width = 0.5f, .height = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, panel, Bounds)->width, 480);
    test_flt(ecs_get(w, panel, Bounds)->height, 540);
}

void Layout_content_sized_ancestors_follow_a_grandchild_that_appears_later(void)
{
    // ore_engine/ui_canonical
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Layout, {.direction = DirectionColumn, .padding = 16});
    ecs_set(w, panel, Rect, {0, 0});
    ecs_entity_t row = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, row, Layout, {.direction = DirectionRow, .spacing = 10});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_progress(w, 0);
    test_flt(ecs_get(w, panel, Bounds)->width, 42);

    ecs_set(w, a, Rect, {120, 28});
    ecs_set(w, b, Rect, {120, 28});
    ecs_progress(w, 0);
    ecs_progress(w, 0);

    test_flt(ecs_get(w, row, Bounds)->width, 250);
    test_flt(ecs_get(w, panel, Bounds)->width, 282);
    test_flt(ecs_get(w, panel, Rect)->width, 282);
    test_flt(ecs_get(w, panel, Bounds)->height, 60);
}

void Layout_padding_sides_inset_the_children(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow, .width = 200, .height = 100,
        .padding_left = 10, .padding_top = 5});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -70);
    test_flt(ecs_get(w, a, Bounds)->y, 35);
}

void Layout_padding_side_of_zero_takes_the_padding(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow, .width = 200, .height = 100,
        .padding = 8, .padding_left = 20});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->x, -60);
    test_flt(ecs_get(w, a, Bounds)->y, 32);
}

void Layout_content_sized_box_adds_the_padding_of_each_side(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t row = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, row, Layout, {.direction = DirectionRow, .padding_left = 3,
        .padding_right = 7, .padding_top = 2, .padding_bottom = 4});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, row, Bounds)->width, 50);
    test_flt(ecs_get(w, row, Bounds)->height, 26);
    test_flt(ecs_get(w, a, Bounds)->x - ecs_get(w, row, Bounds)->x, -2);
    test_flt(ecs_get(w, a, Bounds)->y - ecs_get(w, row, Bounds)->y, 1);
}

void Layout_min_width_widens_a_content_sized_box(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t row = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, row, Layout, {.direction = DirectionRow, .min_width = 100,
        .min_height = 50});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, a, Rect, {40, 20});
    box_ui_update(w);

    test_flt(ecs_get(w, row, Bounds)->width, 100);
    test_flt(ecs_get(w, row, Bounds)->height, 50);
}

void Layout_max_width_narrows_an_explicit_box(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t box = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, box, Layout, {.width = 300, .height = 200, .max_width = 120,
        .max_height = 80});
    ecs_set(w, box, Rect, {0, 0});
    box_ui_update(w);

    test_flt(ecs_get(w, box, Bounds)->width, 120);
    test_flt(ecs_get(w, box, Bounds)->height, 80);
    test_flt(ecs_get(w, box, Rect)->width, 120);
}

void Layout_max_height_bounds_a_content_sized_column(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t column = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, column, Layout, {.direction = DirectionColumn, .max_height = 60});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, column);
    ecs_set(w, a, Rect, {40, 50});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, column);
    ecs_set(w, b, Rect, {40, 50});
    box_ui_update(w);

    test_flt(ecs_get(w, column, Bounds)->height, 60);
}

void Layout_grow_takes_the_room_a_row_leaves(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow, .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {0, 20});
    ecs_set(w, b, Grow, {.width = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, b, Bounds)->width, 160);
    test_flt(ecs_get(w, b, Bounds)->height, 20);
    test_flt(ecs_get(w, b, Bounds)->x, 20);
}

void Layout_grow_weights_share_the_room(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow, .width = 300, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {0, 10});
    ecs_set(w, a, Grow, {.width = 1});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {0, 10});
    ecs_set(w, b, Grow, {.width = 2});
    ecs_entity_t c = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, c, Rect, {60, 10});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->width, 80);
    test_flt(ecs_get(w, b, Bounds)->width, 160);
    test_flt(ecs_get(w, c, Bounds)->x, 120);
}

void Layout_grow_stops_at_the_max_width(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow, .width = 300, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Layout, {.max_width = 50, .height = 10});
    ecs_set(w, a, Grow, {.width = 1});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Layout, {.height = 10});
    ecs_set(w, b, Grow, {.width = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->width, 50);
    test_flt(ecs_get(w, b, Bounds)->width, 250);
}

void Layout_grow_height_takes_the_room_a_column_leaves(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionColumn, .width = 100,
        .height = 200, .spacing = 10});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {10, 50});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {10, 0});
    ecs_set(w, b, Grow, {.height = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, b, Bounds)->height, 140);
    test_flt(ecs_get(w, b, Bounds)->y, -30);
    test_flt(ecs_get(w, b, Rect)->height, 140);
}

void Layout_grow_stretches_across_a_column(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionColumn, .width = 200,
        .height = 100, .padding = 10});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {0, 30});
    ecs_set(w, a, Grow, {.width = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, a, Bounds)->width, 180);
    test_flt(ecs_get(w, a, Bounds)->height, 30);
    test_flt(ecs_get(w, a, Bounds)->x, 0);
}

void Layout_grow_in_a_content_sized_row_takes_no_room(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t row = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, row, Layout, {.direction = DirectionRow});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, b, Layout, {.width = 30, .height = 20});
    ecs_set(w, b, Grow, {.width = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, row, Bounds)->width, 70);
    test_flt(ecs_get(w, b, Bounds)->width, 30);
}

void Layout_grow_follows_a_sibling_that_changes(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionRow, .width = 200, .height = 100});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {0, 20});
    ecs_set(w, b, Grow, {.width = 1});
    box_ui_update(w);
    test_flt(ecs_get(w, b, Rect)->width, 160);

    ecs_set(w, a, Rect, {60, 20});
    box_ui_update(w);
    test_flt(ecs_get(w, b, Bounds)->width, 140);
    test_flt(ecs_get(w, b, Rect)->width, 140);
    test_flt(ecs_get(w, b, Rect)->height, 20);

    box_ui_update(w);
    test_flt(ecs_get(w, b, Bounds)->width, 140);
}

void Layout_grow_of_a_nested_row_follows_its_parent(void)
{
    // ore_engine/layout_sizing
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Layout, {.direction = DirectionColumn, .width = 300, .height = 100});
    ecs_entity_t row = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, row, Layout, {.direction = DirectionRow});
    ecs_set(w, row, Grow, {.width = 1});
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, a, Rect, {40, 20});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, row);
    ecs_set(w, b, Rect, {0, 20});
    ecs_set(w, b, Grow, {.width = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, row, Bounds)->width, 300);
    test_flt(ecs_get(w, b, Bounds)->width, 260);

    ecs_set(w, a, Rect, {100, 20});
    box_ui_update(w);
    test_flt(ecs_get(w, b, Bounds)->width, 200);
}

void Layout_anchored_grow_fills_the_view_of_a_root_without_a_box(void)
{
    // ore_engine/layout_sizing
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 960;
    surface->height = 540;
    surface->point_width = 960;
    surface->point_height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t overlay = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, overlay, Anchor, {.h = AlignCenter, .v = VAlignMiddle});
    ecs_set(w, overlay, Rect, {0, 0});
    ecs_set(w, overlay, Grow, {.width = 1, .height = 1});
    box_ui_update(w);

    test_flt(ecs_get(w, overlay, Bounds)->width, 960);
    test_flt(ecs_get(w, overlay, Bounds)->height, 540);
}
