/**
 * @file Layout.c
 * @brief Layout rules of DESIGN.md: fit, grow, rows, columns, anchors and ownership.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Layout_row_places_children(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 300 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {50, 20});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {30, 40});
    ecs_progress(world, 0);

    const Position *pa = ecs_get(world, a, Position);
    const Position *pb = ecs_get(world, b, Position);
    test_assert(pa != NULL);
    test_assert(pb != NULL);
    test_flt(pa->x, -125);
    test_flt(pa->y, 40);
    test_flt(pb->x, -85);
    test_flt(pb->y, 30);
    const LayoutBounds *lb = ecs_get(world, p, LayoutBounds);
    test_flt(lb->width, 300);
    test_flt(lb->height, 100);

    ecs_fini(world);
}

void Layout_column_places_children(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionColumn });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 300 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {50, 20});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {30, 40});
    ecs_progress(world, 0);

    const Position *pa = ecs_get(world, a, Position);
    const Position *pb = ecs_get(world, b, Position);
    test_flt(pa->x, -25);
    test_flt(pa->y, 140);
    test_flt(pb->x, -35);
    test_flt(pb->y, 110);

    ecs_fini(world);
}

void Layout_ordered_children(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, c, Rect, {10, 10});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_add(world, a, Position);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {10, 10});
    ecs_set(world, b, Rgb, {255, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, c, Position)->x, -10);
    test_flt(ecs_get(world, a, Position)->x, 0);
    test_flt(ecs_get(world, b, Position)->x, 10);

    ecs_entity_t order[] = { b, c, a };
    ecs_set_child_order(world, p, order, 3);
    ecs_modified(world, p, Layout);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b, Position)->x, -10);
    test_flt(ecs_get(world, c, Position)->x, 0);
    test_flt(ecs_get(world, a, Position)->x, 10);

    ecs_fini(world);
}

void Layout_ordered_children_added_late(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_set(world, a, Rgb, {255, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {10, 10});
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, c, Rect, {10, 10});
    ecs_set(world, c, Rgb, {0, 255, 0});
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, d, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -15);
    test_flt(ecs_get(world, b, Position)->x, -5);
    test_flt(ecs_get(world, c, Position)->x, 5);
    test_flt(ecs_get(world, d, Position)->x, 15);

    ecs_entity_t order[] = { d, c, b, a };
    ecs_set_child_order(world, p, order, 4);
    ecs_modified(world, p, Layout);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, d, Position)->x, -15);
    test_flt(ecs_get(world, a, Position)->x, 15);

    ecs_fini(world);
}

void Layout_ordered_children_recycled_ids(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t x = ecs_new(world);
    ecs_entity_t y = ecs_new(world);
    ecs_entity_t z = ecs_new(world);
    ecs_delete(world, x);
    ecs_delete(world, y);
    ecs_delete(world, z);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {10, 10});
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, c, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -10);
    test_flt(ecs_get(world, b, Position)->x, 0);
    test_flt(ecs_get(world, c, Position)->x, 10);

    ecs_fini(world);
}

void Layout_row_spacing(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow, .spacing = 5 });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {10, 10});
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, c, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 40);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 10);
    test_flt(ecs_get(world, a, Position)->x, -15);
    test_flt(ecs_get(world, b, Position)->x, 0);
    test_flt(ecs_get(world, c, Position)->x, 15);

    ecs_fini(world);
}

void Layout_column_spacing(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionColumn, .spacing = 4 });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {10, 20});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 10);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 34);
    test_flt(ecs_get(world, a, Position)->y, 12);
    test_flt(ecs_get(world, b, Position)->y, -7);

    ecs_fini(world);
}

void Layout_padding(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow,
        .padding = { .top = 1, .bottom = 3, .left = 5, .right = 7 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 20});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 22);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 24);
    test_flt(ecs_get(world, a, Position)->x, -1);
    test_flt(ecs_get(world, a, Position)->y, 1);

    ecs_fini(world);
}

void Layout_fit_content(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow, .spacing = 2 });
    ecs_set(world, p, Rect, {1, 1});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 20});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {30, 6});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 42);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 20);
    test_flt(ecs_get(world, p, Rect)->width, 42);
    test_flt(ecs_get(world, p, Rect)->height, 20);

    ecs_fini(world);
}

void Layout_fit_rect(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {40, 30});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {16, 12});
    ecs_set(world, b, LayoutItem, {0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 40);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 30);
    test_flt(ecs_get(world, a, Rect)->width, 40);
    test_flt(ecs_get(world, a, Rect)->height, 30);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 16);
    test_flt(ecs_get(world, b, LayoutBounds)->height, 12);
    test_flt(ecs_get(world, b, Rect)->width, 16);
    test_flt(ecs_get(world, b, Rect)->height, 12);

    ecs_fini(world);
}

void Layout_fit_circle(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Circle, {7});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {10, 4});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 14);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 14);
    test_flt(ecs_get(world, p, LayoutBounds)->width, 24);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 14);
    test_flt(ecs_get(world, a, Position)->x, -5);
    test_flt(ecs_get(world, b, Position)->x, 7);

    ecs_fini(world);
}

void Layout_fit_text(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionColumn });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, TextBounds, {80, 16});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {20, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 16);
    test_flt(ecs_get(world, p, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 26);

    ecs_set(world, a, TextBounds, {100, 20});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, p, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 30);
    test_flt(ecs_get(world, b, Position)->x, -40);
    test_flt(ecs_get(world, b, Position)->y, -10);

    ecs_fini(world);
}

void Layout_fit_without_item(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 300 }, .height = { SizeModeFixed, 200 } });
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, inner, Layout, { .direction = DirectionColumn, .padding = { 2, 2, 2, 2 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_set(world, a, Rect, {30, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, inner, LayoutBounds)->width, 34);
    test_flt(ecs_get(world, inner, LayoutBounds)->height, 14);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 0);
    test_flt(ecs_get(world, b, LayoutBounds)->height, 0);

    ecs_fini(world);
}

void Layout_fixed_size(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 120 }, .height = { SizeModeFixed, 80 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 120);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 80);
    test_flt(ecs_get(world, a, Rect)->width, 120);
    test_flt(ecs_get(world, a, Rect)->height, 80);
    test_flt(ecs_get(world, p, LayoutBounds)->width, 120);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 80);

    ecs_fini(world);
}

void Layout_min_max(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, { .width = { SizeModeFit, .min = 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFit, .min = 30 }, .height = { SizeModeFit, .max = 5 } });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, {
        .width = { SizeModeFixed, 100, .max = 60 }, .height = { SizeModeFixed, 4, .min = 8 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 30);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 5);
    test_flt(ecs_get(world, a, Rect)->width, 10);
    test_flt(ecs_get(world, a, Rect)->height, 10);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 60);
    test_flt(ecs_get(world, b, LayoutBounds)->height, 8);
    test_flt(ecs_get(world, p, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 8);

    ecs_set(world, p, LayoutItem, { .width = { SizeModeFit, .max = 50 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 50);

    ecs_fini(world);
}

void Layout_grow_weighted(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 300 }, .height = { SizeModeFixed, 50 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 60 }, .height = { SizeModeFixed, 10 } });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, { .width = { SizeModeGrow, 1 } });
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, c, LayoutItem, { .width = { SizeModeGrow, 2 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, c, LayoutBounds)->width, 160);
    test_flt(ecs_get(world, a, Position)->x, -120);
    test_flt(ecs_get(world, b, Position)->x, -50);
    test_flt(ecs_get(world, c, Position)->x, 70);

    ecs_fini(world);
}

void Layout_grow_zero_weight(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 50 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, LayoutItem, { .width = { SizeModeGrow, 0 } });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, { .width = { SizeModeGrow, 1 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 0);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 200);
    test_flt(ecs_get(world, a, Position)->x, -100);
    test_flt(ecs_get(world, b, Position)->x, 0);

    ecs_fini(world);
}

void Layout_grow_min_max(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 300 }, .height = { SizeModeFixed, 50 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, LayoutItem, { .width = { SizeModeGrow, 1, .max = 50 } });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, { .width = { SizeModeGrow, 1 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 250);

    ecs_set(world, a, LayoutItem, { .width = { SizeModeGrow, 1, .min = 200 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 200);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, a, Position)->x, -50);
    test_flt(ecs_get(world, b, Position)->x, 100);

    ecs_fini(world);
}

void Layout_grow_column(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionColumn, .spacing = 10 });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 200 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, LayoutItem, { .height = { SizeModeFixed, 50 } });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, { .height = { SizeModeGrow, 1 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b, LayoutBounds)->height, 140);
    test_flt(ecs_get(world, a, Position)->y, 75);
    test_flt(ecs_get(world, b, Position)->y, -30);

    ecs_fini(world);
}

void Layout_grow_cross_axis(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow,
        .padding = { .top = 10, .bottom = 20, .left = 4, .right = 6 } });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 50 }, .height = { SizeModeGrow, 1 } });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, {
        .width = { SizeModeFixed, 50 }, .height = { SizeModeGrow, 1, .max = 30 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->height, 70);
    test_flt(ecs_get(world, a, Position)->y, 5);
    test_flt(ecs_get(world, b, LayoutBounds)->height, 30);
    test_flt(ecs_get(world, b, Position)->y, 25);

    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, Layout, { .direction = DirectionColumn });
    ecs_set(world, c, LayoutItem, {
        .width = { SizeModeFixed, 80 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_set(world, d, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeFixed, 10 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, d, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, d, Position)->x, 0);

    ecs_fini(world);
}

void Layout_grow_direction_none(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .padding = { 10, 10, 10, 10 } });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {1, 1});
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeGrow, 1 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 180);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 80);
    test_flt(ecs_get(world, a, Rect)->width, 180);
    test_flt(ecs_get(world, a, Rect)->height, 80);
    test_flt(ecs_get(world, a, Position)->x, 0);
    test_flt(ecs_get(world, a, Position)->y, 0);

    ecs_fini(world);
}

void Layout_grow_fit_column_widest(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionColumn, .spacing = 4 });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Layout, { .direction = DirectionRow, .padding = { 5, 5, 5, 5 } });
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeGrow, 1 } });
    ecs_set(world, a, Rect, {1, 1});
    ecs_entity_t ta = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, ta, Rect, {40, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Layout, { .direction = DirectionRow, .padding = { 5, 5, 5, 5 } });
    ecs_set(world, b, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeGrow, 1 } });
    ecs_set(world, b, Rect, {1, 1});
    ecs_entity_t tb = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, tb, Rect, {80, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 90);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 44);
    test_flt(ecs_get(world, a, LayoutBounds)->width, 90);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 20);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 90);
    test_flt(ecs_get(world, b, LayoutBounds)->height, 20);
    test_flt(ecs_get(world, a, Rect)->width, 90);
    test_flt(ecs_get(world, a, Position)->x, 0);
    test_flt(ecs_get(world, a, Position)->y, 12);
    test_flt(ecs_get(world, b, Position)->y, -12);
    test_flt(ecs_get(world, ta, Position)->x, -20);

    ecs_fini(world);
}

void Layout_grow_content_is_minimum(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 50 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Layout, { .direction = DirectionRow });
    ecs_set(world, a, LayoutItem, { .width = { SizeModeGrow, 3, .content_min = true } });
    ecs_entity_t ca = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, ca, Rect, {40, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Layout, { .direction = DirectionRow });
    ecs_set(world, b, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_entity_t cb = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, cb, Rect, {80, 10});

    ecs_entity_t q = ecs_new(world);
    ecs_set(world, q, Layout, { .direction = DirectionRow, .spacing = 2 });
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, q);
    ecs_set(world, c, Layout, { .direction = DirectionRow });
    ecs_set(world, c, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_entity_t cc = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_set(world, cc, Rect, {40, 10});
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, q);
    ecs_set(world, d, Layout, { .direction = DirectionRow });
    ecs_set(world, d, LayoutItem, { .width = { SizeModeGrow, 3, .content_min = true } });
    ecs_entity_t cd = ecs_new_w_pair(world, EcsChildOf, d);
    ecs_set(world, cd, Rect, {80, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 120);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, a, Position)->x, -40);
    test_flt(ecs_get(world, b, Position)->x, 60);
    test_flt(ecs_get(world, q, LayoutBounds)->width, 122);
    test_flt(ecs_get(world, q, LayoutBounds)->height, 10);
    test_flt(ecs_get(world, c, LayoutBounds)->width, 40);
    test_flt(ecs_get(world, d, LayoutBounds)->width, 80);

    ecs_fini(world);
}

void Layout_grow_never_below_text(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t hello = ecs_new(world);
    ecs_set(world, hello, Text, {.value = "Hello", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t world_word = ecs_new(world);
    ecs_set(world, world_word, Text, {.value = "world", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 50 } });
    ecs_entity_t f = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, f, LayoutItem, {
        .width = { SizeModeFixed, 80 }, .height = { SizeModeFixed, 10 } });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, t, Text, {.value = "Hello world", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, t, LayoutItem, {
        .width = { SizeModeGrow, 1, .content_min = true },
        .height = { SizeModeGrow, 1, .content_min = true } });

    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, Layout, { .direction = DirectionColumn });
    ecs_set(world, c, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 5 } });
    ecs_entity_t u = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_set(world, u, Text, {.value = "Hello world", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, u, LayoutItem, { .height = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    const TextBounds *tt = ecs_get(world, t, TextBounds);
    test_assert(tt != NULL);
    float widest = fmaxf(ecs_get(world, hello, TextBounds)->width,
        ecs_get(world, world_word, TextBounds)->width);
    test_assert(fabsf(ecs_get(world, t, LayoutBounds)->width - widest) < 0.01f);
    test_assert(fabsf(tt->width - widest) < 0.01f);
    test_flt(tt->height, 2 * ecs_get(world, hello, TextBounds)->height);
    test_flt(ecs_get(world, t, LayoutBounds)->height, 50);
    const TextBounds *tu = ecs_get(world, u, TextBounds);
    test_assert(tu->height > 5);
    test_flt(ecs_get(world, u, LayoutBounds)->height, tu->height);

    ecs_fini(world);
}

void Layout_grow_without_content_min(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 50 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Layout, { .direction = DirectionRow });
    ecs_set(world, a, LayoutItem, { .width = { SizeModeGrow, 3 } });
    ecs_entity_t ca = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, ca, Rect, {40, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Layout, { .direction = DirectionRow });
    ecs_set(world, b, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeGrow, 1 } });
    ecs_entity_t cb = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, cb, Rect, {80, 70});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 150);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, b, LayoutBounds)->height, 50);

    ecs_fini(world);
}

void Layout_grow_cross_axis_keeps_content(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 5 } });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, t, Text, {.value = "Hello world", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, t, LayoutItem, { .height = { SizeModeGrow, 1, .content_min = true } });

    ecs_entity_t q = ecs_new(world);
    ecs_set(world, q, Layout, { .direction = DirectionNone });
    ecs_set(world, q, LayoutItem, {
        .width = { SizeModeFixed, 10 }, .height = { SizeModeFixed, 5 } });
    ecs_entity_t u = ecs_new_w_pair(world, EcsChildOf, q);
    ecs_set(world, u, Text, {.value = "Hello world", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, u, LayoutItem, {
        .width = { SizeModeGrow, 1, .content_min = true },
        .height = { SizeModeGrow, 1, .content_min = true } });

    ecs_entity_t r = ecs_new(world);
    ecs_set(world, r, Layout, { .direction = DirectionRow });
    ecs_set(world, r, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 40 } });
    ecs_entity_t v = ecs_new_w_pair(world, EcsChildOf, r);
    ecs_set(world, v, Text, {.value = "Hello world", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, v, LayoutItem, { .height = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    const TextBounds *tt = ecs_get(world, t, TextBounds);
    test_assert(tt != NULL);
    test_assert(tt->height > 5);
    test_assert(tt->width > 10);
    test_flt(ecs_get(world, t, LayoutBounds)->height, tt->height);
    const TextBounds *tu = ecs_get(world, u, TextBounds);
    test_flt(tu->height, 2 * tt->height);
    test_assert(fabsf(ecs_get(world, u, LayoutBounds)->width - tu->width) < 0.01f);
    test_flt(ecs_get(world, u, LayoutBounds)->height, tu->height);
    test_flt(ecs_get(world, v, LayoutBounds)->height, 40);

    ecs_fini(world);
}

void Layout_grow_zero_weight_keeps_content(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 300 }, .height = { SizeModeFixed, 50 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Layout, { .direction = DirectionRow });
    ecs_set(world, a, LayoutItem, { .width = { SizeModeGrow, 0, .content_min = true } });
    ecs_entity_t ca = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, ca, Rect, {40, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 40);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 260);
    test_flt(ecs_get(world, a, Position)->x, -130);
    test_flt(ecs_get(world, b, Position)->x, 20);

    ecs_set(world, a, LayoutItem, { .width = { SizeModeGrow, 0 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 0);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 300);

    ecs_fini(world);
}

void Layout_grow_script_keeps_content(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *code =
        "using box.layout\n"
        "using box.geometry\n"
        "row {\n"
        "  Layout: {direction: Row}\n"
        "  LayoutItem: {width: {mode: Fixed, value: 50}, height: {mode: Fixed, value: 20}}\n"
        "  button {\n"
        "    Layout: {direction: Row}\n"
        "    LayoutItem: {width: {mode: Grow}}\n"
        "    label { Rect: {80, 10} }\n"
        "  }\n"
        "  spacer { LayoutItem: {width: {mode: Grow}} }\n"
        "}\n";
    test_assert(ecs_script_run(world, "grow", code, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t button = ecs_lookup(world, "row.button");
    test_flt(ecs_get(world, button, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, ecs_lookup(world, "row.spacer"), LayoutBounds)->width, 0);

    ecs_fini(world);
}

void Layout_grow_opted_out_descendant_keeps_content(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 20 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Layout, { .direction = DirectionColumn });
    ecs_set(world, a, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, b, Layout, { .direction = DirectionRow });
    ecs_set(world, b, LayoutItem, { .width = { SizeModeGrow, 1 } });
    ecs_entity_t rb = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, rb, Rect, {80, 10});
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, c, Layout, { .direction = DirectionRow });
    ecs_set(world, c, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_entity_t rc = ecs_new_w_pair(world, EcsChildOf, c);
    ecs_set(world, rc, Rect, {80, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, c, LayoutBounds)->width, 80);

    ecs_fini(world);
}

void Layout_grow_follows_shrinking_parent(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 210 }, .height = { SizeModeFixed, 50 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {1, 1});
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeGrow, 1 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Rect)->width, 210);
    test_flt(ecs_get(world, a, Rect)->height, 50);

    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 30 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 30);
    test_flt(ecs_get(world, a, Rect)->width, 100);
    test_flt(ecs_get(world, a, Rect)->height, 30);

    ecs_fini(world);
}

void Layout_row_align(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, {
        .direction = DirectionRow, .h = AlignCenter, .v = VAlignMiddle });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {20, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {40, 30});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -20);
    test_flt(ecs_get(world, a, Position)->y, 0);
    test_flt(ecs_get(world, b, Position)->x, 10);
    test_flt(ecs_get(world, b, Position)->y, 0);

    ecs_set(world, p, Layout, {
        .direction = DirectionRow, .h = AlignRight, .v = VAlignBottom });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 50);
    test_flt(ecs_get(world, a, Position)->y, -45);
    test_flt(ecs_get(world, b, Position)->x, 80);
    test_flt(ecs_get(world, b, Position)->y, -35);

    ecs_fini(world);
}

void Layout_column_align(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, {
        .direction = DirectionColumn, .h = AlignCenter, .v = VAlignMiddle });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 200 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {20, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {40, 30});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 0);
    test_flt(ecs_get(world, a, Position)->y, 15);
    test_flt(ecs_get(world, b, Position)->x, 0);
    test_flt(ecs_get(world, b, Position)->y, -5);

    ecs_set(world, p, Layout, {
        .direction = DirectionColumn, .h = AlignRight, .v = VAlignBottom });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 40);
    test_flt(ecs_get(world, a, Position)->y, -65);
    test_flt(ecs_get(world, b, Position)->x, 30);
    test_flt(ecs_get(world, b, Position)->y, -85);

    ecs_fini(world);
}

void Layout_row_ignores_item_placement(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 10 },
        .h = AlignRight, .v = VAlignBottom, .x = 50, .y = 50 });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -90);
    test_flt(ecs_get(world, a, Position)->y, 45);

    ecs_fini(world);
}

void Layout_none_anchors(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .padding = { 10, 10, 10, 10 } });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 200 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 10 },
        .h = AlignLeft, .v = VAlignTop, .x = 5, .y = -5 });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 10 },
        .h = AlignCenter, .v = VAlignMiddle, .x = 3, .y = 4 });
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, c, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 10 },
        .h = AlignRight, .v = VAlignBottom, .x = -5, .y = 5 });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -75);
    test_flt(ecs_get(world, a, Position)->y, 30);
    test_flt(ecs_get(world, b, Position)->x, 3);
    test_flt(ecs_get(world, b, Position)->y, 4);
    test_flt(ecs_get(world, c, Position)->x, 75);
    test_flt(ecs_get(world, c, Position)->y, -30);

    ecs_fini(world);
}

void Layout_none_keeps_position(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, {0});
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_set(world, a, Position, {12, -7, 2});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 12);
    test_flt(ecs_get(world, a, Position)->y, -7);
    test_flt(ecs_get(world, a, Position)->z, 2);
    test_flt(ecs_get(world, a, LayoutBounds)->width, 10);

    ecs_set(world, a, Position, {20, 30, 2});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 20);
    test_flt(ecs_get(world, a, Position)->y, 30);

    ecs_fini(world);
}

void Layout_none_fit_content(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .padding = { 2, 2, 2, 2 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 10 },
        .h = AlignCenter, .v = VAlignMiddle });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 24);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 14);

    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {6, 4});
    ecs_set(world, b, Position, {10, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 30);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 14);

    ecs_fini(world);
}

void Layout_none_refits_moved_child(void) {
    // ui_fixes/layout_observer
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .padding = { 2, 2, 2, 2 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {6, 4});
    ecs_set(world, a, Position, {10, 0, 0});
    ecs_entity_t q = ecs_new(world);
    ecs_set(world, q, Layout, { .padding = { 2, 2, 2, 2 } });
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, EcsParent, {q});
    ecs_set(world, b, Rect, {6, 4});
    ecs_set(world, b, Position, {0, 5, 0});
    ecs_entity_t r = ecs_new(world);
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, r);
    ecs_set(world, c, Rect, {6, 4});
    ecs_set(world, c, Position, {3, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 30);
    test_flt(ecs_get(world, q, LayoutBounds)->height, 18);

    ecs_set(world, a, Position, {20, 0, 0});
    ecs_set(world, b, Position, {0, 10, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, q, LayoutBounds)->height, 28);
    test_flt(ecs_get(world, a, Position)->x, 20);

    ecs_set(world, r, Layout, { .padding = { 1, 1, 1, 1 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, r, LayoutBounds)->width, 14);

    ecs_set(world, c, Position, {5, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, r, LayoutBounds)->width, 18);

    ecs_fini(world);
}

void Layout_none_refits_query_write(void) {
    // ui_fixes/layout_observer
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .padding = { 2, 2, 2, 2 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {6, 4});
    ecs_set(world, a, Position, {10, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 30);

    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(Position) }, { .id = ecs_id(Rect), .inout = EcsInOutNone }}
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        Position *pos = ecs_field(&it, Position, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            pos[i].x = 20;
        }
    }

    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 50);

    ecs_query_fini(q);
    ecs_fini(world);
}

void Layout_child_leaving_tree_drops_layout_tag(void) {
    // ui_fixes/layout_observer
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_lookup(world, "box.layout.LayoutChild");
    test_assert(tag != 0);
    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .padding = { 2, 2, 2, 2 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {6, 4});
    ecs_set(world, a, Position, {10, 0, 0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, EcsParent, {p});
    ecs_set(world, b, Rect, {6, 4});
    ecs_set(world, b, Position, {0, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 30);

    ecs_entity_t scene = ecs_new(world);
    ecs_add_pair(world, a, EcsChildOf, scene);
    ecs_set(world, b, EcsParent, {scene});
    ecs_progress(world, 0);

    test_assert(!ecs_has_id(world, a, tag));
    test_assert(!ecs_has_id(world, b, tag));
    test_flt(ecs_get(world, p, LayoutBounds)->width, 4);

    ecs_add_pair(world, a, EcsChildOf, p);
    ecs_progress(world, 0);
    ecs_set(world, a, Position, {20, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 50);

    ecs_fini(world);
}

void Layout_position_keeps_z(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_set(world, a, Position, {100, 100, 3});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -5);
    test_flt(ecs_get(world, a, Position)->y, 0);
    test_flt(ecs_get(world, a, Position)->z, 3);
    test_assert(ecs_get(world, b, Position) != NULL);
    test_flt(ecs_get(world, b, Position)->x, 5);
    test_flt(ecs_get(world, b, Position)->z, 0);

    ecs_fini(world);
}

void Layout_position_owned(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 0);

    ecs_set(world, a, Position, {999, 999, 1});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 0);
    test_flt(ecs_get(world, a, Position)->y, 0);
    test_flt(ecs_get(world, a, Position)->z, 1);

    ecs_fini(world);
}

void Layout_rect_written(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow, .padding = { 5, 5, 5, 5 } });
    ecs_set(world, p, LayoutItem, { .width = { SizeModeFixed, 210 } });
    ecs_set(world, p, Rect, {1, 1});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {30, 20});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {1, 1});
    ecs_set(world, b, LayoutItem, { .width = { SizeModeGrow, 1 }, .height = { SizeModeGrow, 1 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, Rect)->width, 210);
    test_flt(ecs_get(world, p, Rect)->height, 30);
    test_flt(ecs_get(world, a, Rect)->width, 30);
    test_flt(ecs_get(world, a, Rect)->height, 20);
    test_flt(ecs_get(world, b, Rect)->width, 170);
    test_flt(ecs_get(world, b, Rect)->height, 20);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 170);

    ecs_fini(world);
}

void Layout_rect_input_change(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b, Position)->x, 5);

    ecs_set(world, a, Rect, {30, 40});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 30);
    test_flt(ecs_get(world, p, LayoutBounds)->width, 40);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 40);
    test_flt(ecs_get(world, a, Position)->x, -5);
    test_flt(ecs_get(world, b, Position)->x, 15);
    test_flt(ecs_get(world, b, Position)->y, 15);

    ecs_fini(world);
}

void Layout_nested(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t col = ecs_new(world);
    ecs_set(world, col, Layout, {
        .direction = DirectionColumn, .spacing = 10, .padding = { 5, 5, 5, 5 } });
    ecs_entity_t r1 = ecs_new_w_pair(world, EcsChildOf, col);
    ecs_set(world, r1, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, r1);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, r1);
    ecs_set(world, b, Rect, {10, 10});
    ecs_entity_t r2 = ecs_new_w_pair(world, EcsChildOf, col);
    ecs_set(world, r2, Layout, { .direction = DirectionRow });
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, r2);
    ecs_set(world, c, Rect, {30, 20});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, col, LayoutBounds)->width, 40);
    test_flt(ecs_get(world, col, LayoutBounds)->height, 50);
    test_flt(ecs_get(world, r1, LayoutBounds)->width, 20);
    test_flt(ecs_get(world, r1, Position)->x, -5);
    test_flt(ecs_get(world, r1, Position)->y, 15);
    test_flt(ecs_get(world, r2, Position)->x, 0);
    test_flt(ecs_get(world, r2, Position)->y, -10);
    test_flt(ecs_get(world, a, Position)->x, -5);
    test_flt(ecs_get(world, a, Position)->y, 0);
    test_flt(ecs_get(world, b, Position)->x, 5);
    test_flt(ecs_get(world, c, Position)->x, 0);

    ecs_set(world, c, Rect, {50, 20});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, col, LayoutBounds)->width, 60);
    test_flt(ecs_get(world, r1, Position)->x, -15);

    ecs_fini(world);
}

void Layout_transform_same_frame(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Position, {100, 50, 0});
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {30, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, Position)->x, 100);
    const WorldTransform *wt = ecs_get(world, b, WorldTransform);
    test_assert(wt != NULL);
    test_flt(wt->m[3][0], 105);
    test_flt(wt->m[3][1], 50);

    ecs_set(world, a, Rect, {50, 10});
    ecs_progress(world, 0);

    wt = ecs_get(world, b, WorldTransform);
    test_flt(wt->m[3][0], 125);

    ecs_fini(world);
}

void Layout_same_in_every_scene(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, Scale, {0.01f, 0.01f, 0.01f});
    ecs_entity_t p = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, p, Layout, { .direction = DirectionRow, .spacing = 4 });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {20, 10});

    ecs_entity_t q = ecs_new(world);
    ecs_set(world, q, Layout, { .direction = DirectionRow, .spacing = 4 });
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, q);
    ecs_set(world, c, Rect, {10, 10});
    ecs_entity_t d = ecs_new_w_pair(world, EcsChildOf, q);
    ecs_set(world, d, Rect, {20, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 34);
    test_flt(ecs_get(world, q, LayoutBounds)->width, 34);
    test_flt(ecs_get(world, a, Position)->x, ecs_get(world, c, Position)->x);
    test_flt(ecs_get(world, b, Position)->x, ecs_get(world, d, Position)->x);
    test_flt(ecs_get(world, b, Position)->x, 7);

    ecs_fini(world);
}

void Layout_parent_component(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, EcsParent, {p});
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, EcsParent, {p});
    ecs_set(world, b, Rect, {20, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 30);
    test_flt(ecs_get(world, a, Position)->x, -10);
    test_flt(ecs_get(world, b, Position)->x, 5);

    ecs_delete(world, a);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 20);
    test_flt(ecs_get(world, b, Position)->x, 0);

    ecs_fini(world);
}

void Layout_parent_reparent(void) {
    // ui_fixes/parent_reparent
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r1 = ecs_new(world);
    ecs_set(world, r1, Layout, { .direction = DirectionRow });
    ecs_entity_t r2 = ecs_new(world);
    ecs_set(world, r2, Layout, { .direction = DirectionRow });
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, EcsParent, {r1});
    ecs_set(world, c, Rect, {10, 10});
    ecs_entity_t d = ecs_new(world);
    ecs_set(world, d, EcsParent, {r1});
    ecs_set(world, d, Rect, {20, 10});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, EcsParent, {r2});
    ecs_set(world, e, Rect, {30, 12});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, r1, LayoutBounds)->width, 30);
    test_flt(ecs_get(world, r2, LayoutBounds)->width, 30);

    ecs_set(world, c, EcsParent, {r2});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, r1, LayoutBounds)->width, 20);
    test_flt(ecs_get(world, r1, LayoutBounds)->height, 10);
    test_flt(ecs_get(world, r2, LayoutBounds)->width, 40);
    test_flt(ecs_get(world, r2, LayoutBounds)->height, 12);
    test_flt(ecs_get(world, d, Position)->x, 0);
    test_flt(ecs_get(world, e, Position)->x, -5);
    test_flt(ecs_get(world, c, Position)->x, 15);

    ecs_fini(world);
}

void Layout_parent_reparent_to_plain_parent(void) {
    // ui_fixes/parent_reparent
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t row = ecs_new(world);
    ecs_set(world, row, Layout, { .direction = DirectionRow });
    ecs_entity_t plain = ecs_new(world);
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, EcsParent, {row});
    ecs_set(world, c, Rect, {10, 10});
    ecs_entity_t d = ecs_new(world);
    ecs_set(world, d, EcsParent, {row});
    ecs_set(world, d, Rect, {20, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, row, LayoutBounds)->width, 30);

    ecs_set(world, c, EcsParent, {plain});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, row, LayoutBounds)->width, 20);
    test_flt(ecs_get(world, d, Position)->x, 0);

    ecs_fini(world);
}

void Layout_parent_reparent_to_deleted_parent(void) {
    // ui_fixes/parent_reparent
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r1 = ecs_new(world);
    ecs_set(world, r1, Layout, { .direction = DirectionRow });
    ecs_entity_t r2 = ecs_new(world);
    ecs_set(world, r2, Layout, { .direction = DirectionRow });
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, EcsParent, {r1});
    ecs_set(world, c, Rect, {10, 10});
    ecs_entity_t d = ecs_new(world);
    ecs_set(world, d, EcsParent, {r1});
    ecs_set(world, d, Rect, {20, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, r1, LayoutBounds)->width, 30);

    ecs_readonly_begin(world, false);
    ecs_world_t *stage = ecs_get_stage(world, 0);
    ecs_set(stage, c, EcsParent, {r2});
    ecs_delete(stage, r2);
    ecs_readonly_end(world);
    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, c));
    test_flt(ecs_get(world, r1, LayoutBounds)->width, 20);
    test_flt(ecs_get(world, d, Position)->x, 0);

    ecs_fini(world);
}

void Layout_parent_reparent_instance_child(void) {
    // ui_fixes/parent_reparent
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t pa = ecs_new_w_parent(world, prefab, "a");
    ecs_set(world, pa, Rect, {10, 10});
    ecs_entity_t pb = ecs_new_w_parent(world, prefab, "b");
    ecs_set(world, pb, Rect, {20, 10});
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Layout, { .direction = DirectionRow });
    ecs_entity_t r2 = ecs_new(world);
    ecs_set(world, r2, Layout, { .direction = DirectionRow });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, inst, LayoutBounds)->width, 30);
    ecs_entity_t a = 0;
    ecs_iter_t it = ecs_children(world, inst);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            const Rect *r = ecs_get(world, it.entities[i], Rect);
            a = r && r->width == 10 ? it.entities[i] : a;
        }
    }

    test_assert(a != 0);

    ecs_set(world, a, EcsParent, {r2});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, inst, LayoutBounds)->width, 20);
    test_flt(ecs_get(world, r2, LayoutBounds)->width, 10);

    ecs_fini(world);
}

void Layout_prefab_layout(void) {
    // ui_fixes/prefab_layout_abort
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Layout, { .direction = DirectionRow, .spacing = 2 });
    ecs_entity_t pa = ecs_new_w_parent(world, prefab, "a");
    ecs_set(world, pa, Rect, {10, 10});
    ecs_entity_t pb = ecs_new_w_parent(world, prefab, "b");
    ecs_set(world, pb, Layout, { .direction = DirectionColumn });
    ecs_entity_t pc = ecs_new_w_parent(world, pb, "c");
    ecs_set(world, pc, Rect, {20, 12});
    ecs_entity_t i1 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_progress(world, 0);
    ecs_entity_t i2 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, i1, LayoutBounds)->width, 32);
    test_flt(ecs_get(world, i1, LayoutBounds)->height, 12);
    test_flt(ecs_get(world, i2, LayoutBounds)->width, 32);
    test_assert(!ecs_has(world, pa, Position));
    test_assert(!ecs_has(world, pb, Position));

    ecs_entity_t a = 0, b = 0;
    ecs_iter_t it = ecs_children(world, i1);
    while (ecs_children_next(&it)) {
        for (int32_t c = 0; c < it.count; c ++) {
            a = ecs_has(world, it.entities[c], Rect) ? it.entities[c] : a;
            b = ecs_has(world, it.entities[c], Layout) ? it.entities[c] : b;
        }
    }

    test_assert(a != 0);
    test_assert(b != 0);
    test_flt(ecs_get(world, a, Position)->x, -11);
    test_flt(ecs_get(world, b, Position)->x, 6);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 20);

    ecs_set(world, pa, Rect, {14, 10});
    ecs_set(world, pc, Rect, {22, 12});
    ecs_set(world, a, Rect, {30, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, i1, LayoutBounds)->width, 52);
    test_assert(!ecs_has(world, pa, Position));

    ecs_entity_t variant = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, variant, EcsIsA, prefab);
    ecs_set(world, variant, Layout, { .direction = DirectionColumn, .spacing = 2 });
    ecs_entity_t i3 = ecs_new_w_pair(world, EcsIsA, variant);
    ecs_progress(world, 0);

    const LayoutBounds *b3 = ecs_get(world, i3, LayoutBounds);
    test_assert(b3 != NULL);
    test_assert(ecs_owns(world, i3, LayoutBounds));
    test_flt(b3->width, 22);
    test_flt(b3->height, 24);

    ecs_fini(world);
}

void Layout_prefab_child_of_tree(void) {
    // ui_fixes/prefab_layout_abort
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = ecs_new(world);
    ecs_set(world, r, Layout, { .direction = DirectionRow, .spacing = 2 });
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, r);
    ecs_set(world, c, Rect, {10, 10});
    ecs_entity_t py = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, py, EcsChildOf, r);
    ecs_set(world, py, Rect, {5, 5});
    ecs_entity_t px = ecs_new_w_parent(world, r, "px");
    ecs_add_id(world, px, EcsPrefab);
    ecs_set(world, px, Rect, {7, 7});
    ecs_entity_t iy = ecs_new_w_pair(world, EcsIsA, py);
    ecs_entity_t ix = ecs_new_w_pair(world, EcsIsA, px);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, r, LayoutBounds)->width, 10);
    test_flt(ecs_get(world, r, LayoutBounds)->height, 10);
    test_flt(ecs_get(world, c, Position)->x, 0);
    test_assert(!ecs_has(world, py, Position));
    test_assert(!ecs_has(world, px, Position));
    test_assert(ecs_is_alive(world, iy));
    test_assert(ecs_is_alive(world, ix));

    ecs_set(world, c, Rect, {12, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, r, LayoutBounds)->width, 12);
    test_assert(!ecs_has(world, py, Position));

    ecs_fini(world);
}

void Layout_prefab_childof_and_slot(void) {
    // ui_fixes/prefab_layout_abort
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pb = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, pb, Layout, { .direction = DirectionColumn, .spacing = 1 });
    ecs_entity_t pb1 = ecs_new_w_pair(world, EcsChildOf, pb);
    ecs_set(world, pb1, Rect, {8, 3});
    ecs_entity_t pb2 = ecs_new_w_pair(world, EcsChildOf, pb);
    ecs_set(world, pb2, Rect, {6, 3});
    ecs_entity_t pa = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, pa, Layout, { .direction = DirectionRow, .spacing = 2 });
    ecs_entity_t pa1 = ecs_new_w_pair(world, EcsChildOf, pa);
    ecs_set(world, pa1, Rect, {4, 4});
    ecs_entity_t slot = ecs_new_w_pair(world, EcsIsA, pb);
    ecs_add_pair(world, slot, EcsChildOf, pa);
    ecs_entity_t ib = ecs_new_w_pair(world, EcsIsA, pb);
    ecs_entity_t ia = ecs_new_w_pair(world, EcsIsA, pa);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, ib, LayoutBounds)->width, 8);
    test_flt(ecs_get(world, ib, LayoutBounds)->height, 7);
    test_flt(ecs_get(world, ia, LayoutBounds)->width, 14);
    test_flt(ecs_get(world, ia, LayoutBounds)->height, 7);
    test_flt(ecs_get(world, pa, LayoutBounds)->width, 0);
    test_flt(ecs_get(world, pb, LayoutBounds)->width, 0);
    test_assert(!ecs_has(world, pb1, Position));
    test_assert(!ecs_has(world, pa1, Position));
    test_assert(!ecs_has(world, slot, Position));

    ecs_fini(world);
}

void Layout_add_remove_child(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 10);

    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {20, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 30);
    test_flt(ecs_get(world, a, Position)->x, -10);
    test_flt(ecs_get(world, b, Position)->x, 5);

    ecs_delete(world, a);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 20);
    test_flt(ecs_get(world, b, Position)->x, 0);

    ecs_fini(world);
}

void Layout_reparent(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t q = ecs_new(world);
    ecs_set(world, q, Layout, { .direction = DirectionColumn });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {20, 20});
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, q);
    ecs_set(world, c, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 30);
    test_flt(ecs_get(world, q, LayoutBounds)->height, 10);

    ecs_add_pair(world, b, EcsChildOf, q);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 10);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 10);
    test_flt(ecs_get(world, q, LayoutBounds)->width, 20);
    test_flt(ecs_get(world, q, LayoutBounds)->height, 30);
    test_flt(ecs_get(world, a, Position)->x, 0);
    test_flt(ecs_get(world, b, Position)->x, 0);
    test_flt(ecs_get(world, b, Position)->y, -5);

    ecs_fini(world);
}

void Layout_unchanged_tree_not_laid_out(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 0);

    Position *pos = ecs_get_mut(world, a, Position);
    pos->x = 999;
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 999);

    ecs_modified(world, p, Layout);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, 0);

    ecs_fini(world);
}

void Layout_root_item(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {7, 8, 9});
    ecs_set(world, e, Rect, {1, 1});
    ecs_set(world, e, LayoutItem, {
        .width = { SizeModeFixed, 50 }, .height = { SizeModeFixed, 40 }, .x = 30, .y = 30 });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, e, Rect)->width, 50);
    test_flt(ecs_get(world, e, Rect)->height, 40);
    test_flt(ecs_get(world, e, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, e, Position)->x, 7);
    test_flt(ecs_get(world, e, Position)->y, 8);
    test_flt(ecs_get(world, e, Position)->z, 9);

    ecs_fini(world);
}

void Layout_script(void) {
    // ui/layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *code =
        "using box.layout\n"
        "using box.geometry\n"
        "panel {\n"
        "  Layout: {direction: Column, spacing: 2, padding: {4, 4, 4, 4}}\n"
        "  Rect: {1, 1}\n"
        "  title { Rect: {60, 10} }\n"
        "  body {\n"
        "    Layout: {direction: Row, spacing: 2}\n"
        "    LayoutItem: {width: {mode: Grow}}\n"
        "    left { Rect: {10, 20} }\n"
        "    right { Rect: {10, 20} }\n"
        "  }\n"
        "}\n";
    test_assert(ecs_script_run(world, "layout", code, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t panel = ecs_lookup(world, "panel");
    ecs_entity_t body = ecs_lookup(world, "panel.body");
    ecs_entity_t right = ecs_lookup(world, "panel.body.right");
    test_flt(ecs_get(world, panel, Rect)->width, 68);
    test_flt(ecs_get(world, panel, Rect)->height, 40);
    test_flt(ecs_get(world, body, LayoutBounds)->width, 60);
    test_flt(ecs_get(world, body, Position)->y, -6);
    test_flt(ecs_get(world, right, Position)->x, -13);

    ecs_fini(world);
}

void Layout_module_child_fini(void) {
    // ui/fini_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t m = ecs_entity(world, { .name = "game" });
    ecs_add_id(world, m, EcsModule);
    ecs_entity_t p = ecs_entity(world, { .name = "panel", .parent = m });
    ecs_set(world, p, Layout, { .direction = DirectionRow });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 300 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {50, 20});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {30, 40});
    ecs_progress(world, 0);

    const Position *pa = ecs_get(world, a, Position);
    test_assert(pa != NULL);
    test_flt(pa->x, -125);

    test_int(ecs_fini(world), 0);
}

void Layout_module_child_text_fini(void) {
    // ui/fini_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t m = ecs_entity(world, { .name = "game" });
    ecs_add_id(world, m, EcsModule);
    ecs_entity_t p = ecs_entity(world, { .name = "panel", .parent = m });
    ecs_set(world, p, Layout, { .direction = DirectionColumn });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, t, Text, {.value = "Hello", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    const LayoutBounds *lb = ecs_get(world, p, LayoutBounds);
    test_assert(lb != NULL);
    test_assert(lb->width > 0);

    test_int(ecs_fini(world), 0);
}
