/**
 * @file LayoutFloat.c
 * @brief LayoutFloat places an element against an anchor, out of the flow of its parent.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Create the root of the tests: a 400 by 400 column.
 *
 * @param world The world
 * @param h Alignment of its children on x
 * @param v Alignment of its children on y
 * @return The root
 */
static ecs_entity_t float_root(
    ecs_world_t *world,
    Align h,
    VAlign v)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Layout, { .direction = DirectionColumn, .h = h, .v = v });
    ecs_set(world, result, LayoutItem, {
        .width = { SizeModeFixed, 400 }, .height = { SizeModeFixed, 400 } });
    return result;
}

/** Create an element with a Layout and a fixed size.
 *
 * @param world The world
 * @param parent The parent
 * @param width The width
 * @param height The height
 * @return The element
 */
static ecs_entity_t float_box(
    ecs_world_t *world,
    ecs_entity_t parent,
    float width,
    float height)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, result, Layout, { .direction = DirectionColumn });
    ecs_set(world, result, LayoutItem, {
        .width = { SizeModeFixed, width }, .height = { SizeModeFixed, height } });
    return result;
}

/** Create an 80 by 50 floating element.
 *
 * @param world The world
 * @param parent The parent
 * @param fl Its LayoutFloat
 * @return The element
 */
static ecs_entity_t float_list(
    ecs_world_t *world,
    ecs_entity_t parent,
    LayoutFloat fl)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, result, Rect, {80, 50});
    ecs_set_ptr(world, result, LayoutFloat, &fl);
    return result;
}

/** The LayoutFloat of a dropdown list, below the left edge of its anchor */
#define FLOAT_DROPDOWN (LayoutFloat){ \
    .h = AlignLeft, .v = VAlignTop, .anchor_h = AlignLeft, .anchor_v = VAlignBottom }

void LayoutFloat_not_in_parent_fit(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionColumn });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {50, 20});
    ecs_entity_t f = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, f, Rect, {200, 200});
    ecs_set(world, f, LayoutFloat, {0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, p, LayoutBounds)->height, 20);
    test_flt(ecs_get(world, f, LayoutBounds)->width, 200);
    test_flt(ecs_get(world, f, LayoutBounds)->height, 200);
    test_flt(ecs_get(world, a, Position)->y, 0);

    ecs_fini(world);
}

void LayoutFloat_not_in_flow(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionColumn, .spacing = 10 });
    ecs_set(world, p, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 100 } });
    ecs_entity_t f = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, f, Rect, {30, 30});
    ecs_set(world, f, LayoutFloat, {0});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {50, 20});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, b, Rect, {50, 20});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->y, 40);
    test_flt(ecs_get(world, b, Position)->y, 10);
    test_flt(ecs_get(world, a, Position)->x, -25);

    /* The zero LayoutFloat puts the top left corners together */
    test_flt(ecs_get(world, f, Position)->x, -35);
    test_flt(ecs_get(world, f, Position)->y, 35);

    ecs_fini(world);
}

void LayoutFloat_dropdown(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l = float_list(world, b, FLOAT_DROPDOWN);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b, Position)->x, -150);
    test_flt(ecs_get(world, b, Position)->y, 190);
    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, -35);
    test_flt(ecs_get(world, l, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, l, LayoutBounds)->height, 50);
    test_flt(ecs_get(world, b, LayoutBounds)->height, 20);

    ecs_fini(world);
}

void LayoutFloat_anchor_points(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l = float_list(world, b, (LayoutFloat){0});
    int32_t h, v, ah, av;
    for (h = 0; h < 3; h ++) {
        for (v = 0; v < 3; v ++) {
            for (ah = 0; ah < 3; ah ++) {
                for (av = 0; av < 3; av ++) {
                    ecs_set(world, l, LayoutFloat, {
                        .h = (Align)h, .v = (VAlign)v,
                        .anchor_h = (Align)ah, .anchor_v = (VAlign)av,
                        .x = 3, .y = -4 });
                    ecs_progress(world, 0);

                    float x = (float)(ah - 1) * 50 + 3 - (float)(h - 1) * 40;
                    float y = (float)(1 - av) * 10 - 4 - (float)(1 - v) * 25;
                    test_flt(ecs_get(world, l, Position)->x, x);
                    test_flt(ecs_get(world, l, Position)->y, y);
                }
            }
        }
    }

    ecs_fini(world);
}

void LayoutFloat_offset(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.x = 5;
    fl.y = -2;
    ecs_entity_t l = float_list(world, b, fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, -5);
    test_flt(ecs_get(world, l, Position)->y, -37);

    ecs_fini(world);
}

void LayoutFloat_anchor_sibling(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b1 = float_box(world, r, 100, 20);
    ecs_entity_t b2 = float_box(world, r, 60, 30);
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.anchor = b2;
    ecs_entity_t l = float_list(world, b1, fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b2, Position)->x, -170);
    test_flt(ecs_get(world, b2, Position)->y, 165);
    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, -65);

    ecs_fini(world);
}

void LayoutFloat_deleted_anchor_is_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b1 = float_box(world, r, 100, 20);
    ecs_entity_t b2 = float_box(world, r, 60, 30);
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.anchor = b2;
    ecs_entity_t l = float_list(world, b1, fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->y, -65);

    ecs_delete(world, b2);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, -35);

    ecs_fini(world);
}

void LayoutFloat_fixed_size(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, l, LayoutItem, {
        .width = { SizeModeFixed, 120 }, .height = { SizeModeFixed, 30 } });
    ecs_set_ptr(world, l, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, LayoutBounds)->width, 120);
    test_flt(ecs_get(world, l, LayoutBounds)->height, 30);
    test_flt(ecs_get(world, l, Position)->x, 10);
    test_flt(ecs_get(world, l, Position)->y, -25);

    ecs_fini(world);
}

void LayoutFloat_fit_size(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, l, Layout, { .direction = DirectionColumn });
    ecs_set_ptr(world, l, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_entity_t c1 = ecs_new_w_pair(world, EcsChildOf, l);
    ecs_set(world, c1, Rect, {40, 10});
    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, l);
    ecs_set(world, c2, Rect, {60, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, LayoutBounds)->width, 60);
    test_flt(ecs_get(world, l, LayoutBounds)->height, 20);
    test_flt(ecs_get(world, l, Position)->x, -20);
    test_flt(ecs_get(world, l, Position)->y, -20);
    test_flt(ecs_get(world, c1, Position)->x, -10);
    test_flt(ecs_get(world, c1, Position)->y, 5);
    test_flt(ecs_get(world, c2, Position)->y, -5);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 100);

    ecs_fini(world);
}

void LayoutFloat_grow_width_of_anchor(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b1 = float_box(world, r, 100, 20);
    ecs_entity_t b2 = float_box(world, r, 60, 30);
    ecs_entity_t l1 = ecs_new_w_pair(world, EcsChildOf, b1);
    ecs_set(world, l1, Layout, { .direction = DirectionColumn });
    ecs_set(world, l1, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeFixed, 50 } });
    ecs_set_ptr(world, l1, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_entity_t row = ecs_new_w_pair(world, EcsChildOf, l1);
    ecs_set(world, row, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeFixed, 10 } });
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.anchor = b2;
    ecs_entity_t l2 = ecs_new_w_pair(world, EcsChildOf, b1);
    ecs_set(world, l2, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeFixed, 50 } });
    ecs_set_ptr(world, l2, LayoutFloat, &fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l1, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, l1, LayoutBounds)->height, 50);
    test_flt(ecs_get(world, l1, Position)->x, 0);
    test_flt(ecs_get(world, row, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, l2, LayoutBounds)->width, 60);
    test_flt(ecs_get(world, l2, Position)->x, -20);
    test_flt(ecs_get(world, b1, LayoutBounds)->width, 100);

    ecs_fini(world);
}

void LayoutFloat_grow_clamped(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t narrow = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, narrow, LayoutItem, {
        .width = { SizeModeGrow, 1, .max = 70 }, .height = { SizeModeFixed, 10 } });
    ecs_set_ptr(world, narrow, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_entity_t wide = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, wide, LayoutItem, {
        .width = { SizeModeGrow, 1, .min = 150 }, .height = { SizeModeFixed, 10 } });
    ecs_set_ptr(world, wide, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_entity_t content = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, content, Layout, { .direction = DirectionRow });
    ecs_set(world, content, LayoutItem, {
        .width = { SizeModeGrow, 1, .content_min = true }, .height = { SizeModeFixed, 10 } });
    ecs_set_ptr(world, content, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, content);
    ecs_set(world, inner, Rect, {130, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, narrow, LayoutBounds)->width, 70);
    test_flt(ecs_get(world, wide, LayoutBounds)->width, 150);
    test_flt(ecs_get(world, content, LayoutBounds)->width, 130);

    ecs_fini(world);
}

void LayoutFloat_flip_bottom(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignBottom);
    ecs_entity_t b = float_box(world, r, 100, 20);
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.y = -2;
    fl.flip = true;
    ecs_entity_t l = float_list(world, b, fl);
    ecs_progress(world, 0);

    /* Below the button it passes the bottom of the root, so it opens upward */
    test_flt(ecs_get(world, b, Position)->y, -190);
    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, 37);

    ecs_fini(world);
}

void LayoutFloat_flip_kept_when_it_fits(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.flip = true;
    ecs_entity_t l = float_list(world, b, fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, -35);

    /* Moved to the bottom, it flips in the same frame */
    ecs_set(world, r, Layout, { .direction = DirectionColumn, .v = VAlignBottom });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->y, 35);

    ecs_fini(world);
}

void LayoutFloat_no_flip_without_flag(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignRight, VAlignBottom);
    ecs_entity_t b = float_box(world, r, 100, 20);
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.anchor_h = AlignRight;
    ecs_entity_t l = float_list(world, b, fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, 90);
    test_flt(ecs_get(world, l, Position)->y, -35);

    ecs_fini(world);
}

void LayoutFloat_flip_right_edge(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignRight, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l = float_list(world, b, (LayoutFloat){
        .h = AlignLeft, .v = VAlignTop, .anchor_h = AlignRight, .anchor_v = VAlignTop,
        .flip = true });
    ecs_progress(world, 0);

    /* Right of the button it passes the right edge, so it shifts left to stay inside */
    test_flt(ecs_get(world, b, Position)->x, 150);
    test_flt(ecs_get(world, l, Position)->x, 10);
    test_flt(ecs_get(world, l, Position)->y, -15);

    ecs_fini(world);
}

void LayoutFloat_flip_left_edge(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l = float_list(world, b, (LayoutFloat){
        .h = AlignRight, .v = VAlignTop, .anchor_h = AlignLeft, .anchor_v = VAlignTop,
        .flip = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, -15);

    ecs_fini(world);
}

void LayoutFloat_flip_against_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    /* The list passes the bottom of the container of the button but stays in the root */
    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t c = float_box(world, r, 200, 100);
    ecs_set(world, c, Layout, { .direction = DirectionColumn, .v = VAlignBottom });
    ecs_entity_t b = float_box(world, c, 100, 20);
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.flip = true;
    ecs_entity_t l = float_list(world, b, fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b, Position)->y, -40);
    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, -35);

    ecs_fini(world);
}

void LayoutFloat_follows_moving_anchor(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_set(world, r, Layout, { .direction = DirectionNone });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, r);
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 60 }, .height = { SizeModeFixed, 30 }, .x = 10 });
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.anchor = a;
    ecs_entity_t l = float_list(world, r, fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -160);
    test_flt(ecs_get(world, a, Position)->y, 185);
    test_flt(ecs_get(world, l, Position)->x, -150);
    test_flt(ecs_get(world, l, Position)->y, 145);

    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 60 }, .height = { SizeModeFixed, 30 }, .x = 50, .y = -20 });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, -110);
    test_flt(ecs_get(world, l, Position)->y, 125);

    ecs_fini(world);
}

void LayoutFloat_follows_scrolling_anchor(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t s = float_box(world, r, 200, 100);
    ecs_set(world, s, LayoutScroll, { .vertical = true });
    ecs_entity_t rows[5];
    int32_t i;
    for (i = 0; i < 5; i ++) {
        rows[i] = ecs_new_w_pair(world, EcsChildOf, s);
        ecs_set(world, rows[i], Rect, {200, 40});
    }

    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.anchor = rows[1];
    ecs_entity_t l = float_list(world, r, fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, s, Position)->y, 150);
    test_flt(ecs_get(world, rows[1], Position)->y, -10);
    test_flt(ecs_get(world, l, Position)->x, -160);
    test_flt(ecs_get(world, l, Position)->y, 95);

    ecs_set(world, s, LayoutScroll, { .y = 30, .vertical = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, rows[1], Position)->y, 20);
    test_flt(ecs_get(world, l, Position)->x, -160);
    test_flt(ecs_get(world, l, Position)->y, 125);

    ecs_fini(world);
}

void LayoutFloat_follows_resizing_anchor(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l = float_list(world, b, (LayoutFloat){
        .h = AlignLeft, .v = VAlignTop, .anchor_h = AlignRight, .anchor_v = VAlignBottom });
    ecs_entity_t g = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, g, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeFixed, 10 } });
    ecs_set_ptr(world, g, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, 90);
    test_flt(ecs_get(world, g, LayoutBounds)->width, 100);

    ecs_set(world, b, LayoutItem, {
        .width = { SizeModeFixed, 140 }, .height = { SizeModeFixed, 30 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b, Position)->x, -130);
    test_flt(ecs_get(world, l, Position)->x, 110);
    test_flt(ecs_get(world, l, Position)->y, -40);
    test_flt(ecs_get(world, g, LayoutBounds)->width, 140);
    test_flt(ecs_get(world, g, Position)->x, 0);

    ecs_fini(world);
}

void LayoutFloat_anchor_in_other_tree(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_set(world, r, Layout, { .direction = DirectionNone });
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {50, 60});
    ecs_set(world, other, Layout, { .direction = DirectionRow });
    ecs_set(world, other, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 40 } });
    ecs_entity_t l = ecs_new_w_pair(world, EcsChildOf, r);
    ecs_set(world, l, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeFixed, 30 } });
    LayoutFloat fl = FLOAT_DROPDOWN;
    fl.anchor = other;
    ecs_set_ptr(world, l, LayoutFloat, &fl);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, l, Position)->x, 50);
    test_flt(ecs_get(world, l, Position)->y, 25);

    ecs_set(world, other, LayoutItem, {
        .width = { SizeModeFixed, 160 }, .height = { SizeModeFixed, 40 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, LayoutBounds)->width, 160);
    test_flt(ecs_get(world, l, Position)->x, 50);

    ecs_set(world, other, Position, {70, 80});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, 70);
    test_flt(ecs_get(world, l, Position)->y, 45);

    ecs_fini(world);
}

void LayoutFloat_float_child_of_scroll_not_scrolled(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Layout, { .direction = DirectionColumn });
    ecs_set(world, s, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 100 } });
    ecs_set(world, s, LayoutScroll, { .vertical = true });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, s);
    ecs_set(world, a, Rect, {100, 80});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, s);
    ecs_set(world, b, Rect, {100, 80});
    ecs_entity_t l = float_list(world, s, (LayoutFloat){0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, 25);
    test_flt(ecs_get(world, s, LayoutContent)->height, 160);

    ecs_set(world, s, LayoutScroll, { .y = 10, .vertical = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->y, 20);
    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, 25);

    ecs_fini(world);
}

void LayoutFloat_nested_float(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l1 = float_box(world, b, 80, 50);
    ecs_set_ptr(world, l1, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_entity_t l2 = float_list(world, l1, (LayoutFloat){
        .h = AlignLeft, .v = VAlignTop, .anchor_h = AlignRight, .anchor_v = VAlignTop });
    ecs_set(world, l2, Rect, {60, 40});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l1, Position)->x, -10);
    test_flt(ecs_get(world, l1, Position)->y, -35);
    test_flt(ecs_get(world, l2, Position)->x, 70);
    test_flt(ecs_get(world, l2, Position)->y, 5);
    test_flt(ecs_get(world, l1, LayoutBounds)->width, 80);

    ecs_fini(world);
}

void LayoutFloat_remove_float_rejoins_flow(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_new(world);
    ecs_set(world, p, Layout, { .direction = DirectionColumn });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, a, Rect, {50, 20});
    ecs_entity_t f = ecs_new_w_pair(world, EcsChildOf, p);
    ecs_set(world, f, Rect, {30, 30});
    ecs_set(world, f, LayoutFloat, {0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->height, 20);

    ecs_remove(world, f, LayoutFloat);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, p, LayoutBounds)->height, 50);
    test_flt(ecs_get(world, a, Position)->y, 15);
    test_flt(ecs_get(world, f, Position)->x, -10);
    test_flt(ecs_get(world, f, Position)->y, -10);

    ecs_fini(world);
}

void LayoutFloat_keeps_z(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t r = float_root(world, AlignLeft, VAlignTop);
    ecs_entity_t b = float_box(world, r, 100, 20);
    ecs_entity_t l = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, l, Position, {0, 0, 5});
    ecs_set(world, l, Rect, {80, 50});
    ecs_set_ptr(world, l, LayoutFloat, &FLOAT_DROPDOWN);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, l, Position)->x, -10);
    test_flt(ecs_get(world, l, Position)->y, -35);
    test_flt(ecs_get(world, l, Position)->z, 5);

    ecs_fini(world);
}

void LayoutFloat_script(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *code =
        "using box.layout\n"
        "using box.geometry\n"
        "root {\n"
        "  Layout: {direction: Column, v: Bottom}\n"
        "  LayoutItem: {width: {mode: Fixed, value: 400}, height: {mode: Fixed, value: 400}}\n"
        "  button {\n"
        "    Layout: {}\n"
        "    LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 20}}\n"
        "    list {\n"
        "      Rect: {80, 50}\n"
        "      LayoutFloat: {h: Left, v: Top, anchor_h: Left, anchor_v: Bottom, flip: true}\n"
        "    }\n"
        "  }\n"
        "}\n";
    test_assert(ecs_script_run(world, "float", code, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t root = ecs_lookup(world, "root");
    ecs_entity_t list = ecs_lookup(world, "root.button.list");
    test_flt(ecs_get(world, root, LayoutBounds)->height, 400);
    test_flt(ecs_get(world, list, Position)->x, -10);
    test_flt(ecs_get(world, list, Position)->y, 35);

    ecs_fini(world);
}
