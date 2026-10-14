/**
 * @file Layout.c
 * @brief Layout rules of DESIGN.md: fit, grow, rows, columns, anchors and ownership.
 */

#include <box.h>
#include <bake_test.h>

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

    test_flt(ecs_get(world, a, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, a, Position)->x, -50);
    test_flt(ecs_get(world, b, Position)->x, 50);

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
    ecs_set(world, b, LayoutItem, { .height = { SizeModeGrow } });
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
        .width = { SizeModeFixed, 50 }, .height = { SizeModeGrow } });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, LayoutItem, {
        .width = { SizeModeFixed, 50 }, .height = { SizeModeGrow, .max = 30 } });
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
        .width = { SizeModeGrow }, .height = { SizeModeFixed, 10 } });
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
        .width = { SizeModeGrow }, .height = { SizeModeGrow } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 180);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 80);
    test_flt(ecs_get(world, a, Rect)->width, 180);
    test_flt(ecs_get(world, a, Rect)->height, 80);
    test_flt(ecs_get(world, a, Position)->x, 0);
    test_flt(ecs_get(world, a, Position)->y, 0);

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
    ecs_set(world, b, LayoutItem, { .width = { SizeModeGrow }, .height = { SizeModeGrow } });
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
