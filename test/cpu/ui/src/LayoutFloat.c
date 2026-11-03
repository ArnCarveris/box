/**
 * @file LayoutFloat.c
 * @brief LayoutFloat places an element against an anchor, out of the flow of its parent.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "draw.h"

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

/** The reproduction of a float in a clip: a 100 by 100 ClipChildren column at the origin with
 * an 80 by 300 column below its top left corner, which spans -50..30 on x and -250..50 on y. Its
 * child overlay, 10 by 50 against its bottom right corner, lies below the bottom of outer. The
 * argument is appended to the LayoutFloat of overlay. */
#define FLOAT_CLIP_REPRO(fl) \
    "using box.layout\n" \
    "using box.geometry\n" \
    "using box.transform\n" \
    "outer {\n" \
    "  Position: {0, 0, 0}\n" \
    "  Layout: {direction: Column}\n" \
    "  LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 100}}\n" \
    "  ClipChildren\n" \
    "  inner {\n" \
    "    Layout: {direction: Column}\n" \
    "    LayoutItem: {width: {mode: Fixed, value: 80}, height: {mode: Fixed, value: 300}}\n" \
    "    overlay {\n" \
    "      LayoutFloat: {h: Right, v: Bottom, anchor_h: Right, anchor_v: Bottom" fl "}\n" \
    "      LayoutItem: {width: {mode: Fixed, value: 10}, height: {mode: Fixed, value: 50}}\n" \
    "      Rect: {1, 1}\n" \
    "    }\n" \
    "  }\n" \
    "}\n"

/** Nested clips: outer as in FLOAT_CLIP_REPRO, inner a ClipChildren column of the same place
 * with a content child that fills it. Its first child overlay, 20 points wide, takes the height
 * of inner and sits 5 points right of its right edge: it spans 15..35 on x, the part right of
 * 30 outside inner, and -250..50 on y, the part below -50 outside outer. The argument is appended
 * to the LayoutFloat of overlay. */
#define FLOAT_CLIP_NESTED(fl) \
    "using box.layout\n" \
    "using box.geometry\n" \
    "using box.transform\n" \
    "outer {\n" \
    "  Position: {0, 0, 0}\n" \
    "  Layout: {direction: Column}\n" \
    "  LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 100}}\n" \
    "  ClipChildren\n" \
    "  inner {\n" \
    "    Layout: {direction: Column}\n" \
    "    LayoutItem: {width: {mode: Fixed, value: 80}, height: {mode: Fixed, value: 300}}\n" \
    "    ClipChildren\n" \
    "    overlay {\n" \
    "      LayoutFloat: {h: Right, v: Middle, anchor_h: Right, anchor_v: Middle, x: 5" fl "}\n" \
    "      LayoutItem: {width: {mode: Fixed, value: 20}, height: {mode: Grow}}\n" \
    "      Rect: {1, 1}\n" \
    "    }\n" \
    "    content {\n" \
    "      LayoutItem: {width: {mode: Fixed, value: 80}, height: {mode: Fixed, value: 300}}\n" \
    "      Rect: {1, 1}\n" \
    "    }\n" \
    "  }\n" \
    "}\n"

/** Run a script in a new world and run frames, so the layout and the transforms settle.
 *
 * @param code The script
 * @return The world
 */
static ecs_world_t* float_clip_world(
    const char *code)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "clip", code, NULL) == 0);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    return world;
}

/** Check whether the clips around an element keep a world point on its plane.
 *
 * @param world The world
 * @param path Path of the element
 * @param x World x of the point
 * @param y World y of the point
 * @return Whether no ClipChildren ancestor clips the point away
 */
static bool float_clip_keeps(
    ecs_world_t *world,
    const char *path,
    float x,
    float y)
{
    ecs_entity_t e = ecs_lookup(world, path);
    test_assert(e != 0);
    float point[3] = { x, y, 0 };

    return box_render_clip_contains(world, e, point);
}

/** Get the world position of the center of an element.
 *
 * @param world The world
 * @param path Path of the element
 * @param out Receives the position
 */
static void float_clip_center(
    ecs_world_t *world,
    const char *path,
    float out[3])
{
    const WorldTransform *t = ecs_get(world, ecs_lookup(world, path), WorldTransform);
    test_assert(t != NULL);
    out[0] = t->m[3][0];
    out[1] = t->m[3][1];
    out[2] = t->m[3][2];
}

void LayoutFloat_clip_overlay_clipped(void) {
    ecs_world_t *world = float_clip_world(FLOAT_CLIP_REPRO(", clip: true"));
    ecs_entity_t overlay = ecs_lookup(world, "outer.inner.overlay");
    test_assert(overlay != 0);
    test_bool(ecs_get(world, overlay, LayoutFloat)->clip, true);

    /* Placed as a float, against the bottom right corner of inner, below the bottom of outer */
    float center[3];
    float_clip_center(world, "outer.inner.overlay", center);
    test_flt(center[0], 25);
    test_flt(center[1], -225);

    /* The clip of outer, an ancestor of its parent, clips it away */
    test_bool(box_render_clip_contains(world, overlay, center), false);
    test_bool(box_render_clip_hides(world, overlay), true);

    ecs_fini(world);
}

void LayoutFloat_float_escapes_clips(void) {
    ecs_world_t *world = float_clip_world(FLOAT_CLIP_REPRO(""));
    ecs_entity_t overlay = ecs_lookup(world, "outer.inner.overlay");
    test_assert(overlay != 0);
    test_bool(ecs_get(world, overlay, LayoutFloat)->clip, false);

    /* A float without clip, such as a dropdown list, leaves the clips of its ancestors */
    float center[3];
    float_clip_center(world, "outer.inner.overlay", center);
    test_flt(center[1], -225);
    test_bool(box_render_clip_contains(world, overlay, center), true);
    test_bool(box_render_clip_hides(world, overlay), false);

    ecs_fini(world);
}

void LayoutFloat_clip_overlay_nested_clips(void) {
    ecs_world_t *world = float_clip_world(FLOAT_CLIP_NESTED(", clip: true"));
    ecs_entity_t overlay = ecs_lookup(world, "outer.inner.overlay");
    ecs_entity_t content = ecs_lookup(world, "outer.inner.content");
    test_assert(overlay != 0 && content != 0);

    /* Out of the flow: the first child, it does not move the content, and it takes the height
     * of its anchor */
    test_flt(ecs_get(world, overlay, Position)->x, 35);
    test_flt(ecs_get(world, overlay, Position)->y, 0);
    test_flt(ecs_get(world, overlay, LayoutBounds)->height, 300);
    test_flt(ecs_get(world, content, Position)->x, 0);
    test_flt(ecs_get(world, content, Position)->y, 0);

    /* Inside both clips it is kept, outside either one it is not */
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 20, 0), true);
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 32, 0), false);
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 20, -80), false);
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 32, -80), false);
    test_bool(box_render_clip_hides(world, overlay), false);

    /* Its descendants are clipped the same way */
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, overlay);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Rect, {4, 4});
    ecs_progress(world, 0);
    float point[3] = { 32, 0, 0 };
    test_bool(box_render_clip_contains(world, child, point), false);
    point[0] = 20;
    test_bool(box_render_clip_contains(world, child, point), true);

    /* The flag set at runtime takes effect: without it the float leaves both clips */
    ecs_set(world, overlay, LayoutFloat, {
        .h = AlignRight, .v = VAlignMiddle, .anchor_h = AlignRight, .anchor_v = VAlignMiddle,
        .x = 5 });
    ecs_progress(world, 0);
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 32, -80), true);
    test_bool(box_render_clip_contains(world, child, (float[3]){ 32, 0, 0 }), true);

    ecs_fini(world);
}

void LayoutFloat_float_escapes_nested_clips(void) {
    ecs_world_t *world = float_clip_world(FLOAT_CLIP_NESTED(""));
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 20, 0), true);
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 32, 0), true);
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 20, -80), true);
    test_bool(float_clip_keeps(world, "outer.inner.overlay", 32, -80), true);

    ecs_fini(world);
}

void LayoutFloat_clip_overlay_inside_float(void) {
    /* A clipped overlay in a dropdown list is clipped by the list, not by the clips the list
     * leaves */
    ecs_world_t *world = float_clip_world(
        "using box.layout\n"
        "using box.geometry\n"
        "using box.transform\n"
        "outer {\n"
        "  Position: {0, 0, 0}\n"
        "  Layout: {direction: Column}\n"
        "  LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 100}}\n"
        "  ClipChildren\n"
        "  list {\n"
        "    LayoutFloat: {h: Left, v: Top, anchor_h: Left, anchor_v: Bottom}\n"
        "    Layout: {direction: Column}\n"
        "    LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 200}}\n"
        "    ClipChildren\n"
        "    bar {\n"
        "      LayoutFloat: {h: Right, v: Top, anchor_h: Right, anchor_v: Top, x: 5, clip: true}\n"
        "      LayoutItem: {width: {mode: Fixed, value: 20}, height: {mode: Fixed, value: 50}}\n"
        "      Rect: {1, 1}\n"
        "    }\n"
        "  }\n"
        "}\n");

    /* The list spans -50..50 on x and -250..-50 on y, below outer; bar spans 35..55 on x */
    float center[3];
    float_clip_center(world, "outer.list", center);
    test_flt(center[0], 0);
    test_flt(center[1], -150);
    test_bool(float_clip_keeps(world, "outer.list.bar", 40, -60), true);
    test_bool(float_clip_keeps(world, "outer.list.bar", 52, -60), false);

    ecs_fini(world);
}

/** The reproduction of a float whose anchor scrolls out: scroll, a 100 by 100 ClipChildren
 * scroll view at the origin, holds anchor, 100 by 40 at its top (10..50 on y), and 300 points of
 * filler. The child list of anchor, 80 by 50, hangs below it: it spans -50..30 on x and -40..10
 * on y, centered on -10, -15, before a scroll. The argument is appended to the LayoutFloat of
 * list. */
#define FLOAT_HIDE_REPRO(fl) \
    "using box.layout\n" \
    "using box.geometry\n" \
    "using box.transform\n" \
    "scroll {\n" \
    "  Position: {0, 0, 0}\n" \
    "  Layout: {direction: Column}\n" \
    "  LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 100}}\n" \
    "  LayoutScroll: {vertical: true}\n" \
    "  ClipChildren\n" \
    "  anchor {\n" \
    "    Layout: {}\n" \
    "    LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 40}}\n" \
    "    Rect: {1, 1}\n" \
    "    list {\n" \
    "      LayoutFloat: {anchor_v: Bottom, v: Top" fl "}\n" \
    "      Rect: {80, 50}\n" \
    "      item {\n" \
    "        Position: {0, 0, 0}\n" \
    "        Rect: {10, 10}\n" \
    "      }\n" \
    "    }\n" \
    "  }\n" \
    "  filler {\n" \
    "    LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 300}}\n" \
    "    Rect: {1, 1}\n" \
    "  }\n" \
    "}\n"

/** Set the vertical scroll offset of the scroll view of FLOAT_HIDE_REPRO and run one frame.
 *
 * @param world The world
 * @param y The offset
 */
static void float_hide_scroll(
    ecs_world_t *world,
    float y)
{
    ecs_set(world, ecs_lookup(world, "scroll"), LayoutScroll, { .y = y, .vertical = true });
    ecs_progress(world, 0);
}

/** Check whether the clips keep the center of an element of FLOAT_HIDE_REPRO, as picking
 * checks it.
 *
 * @param world The world
 * @param path Path of the element
 * @return Whether a hit at its center is kept
 */
static bool float_hide_keeps_center(
    ecs_world_t *world,
    const char *path)
{
    float center[3];
    float_clip_center(world, path, center);

    return box_render_clip_contains(world, ecs_lookup(world, path), center);
}

void LayoutFloat_hide_clipped_anchor_scrolled_out(void) {
    ecs_world_t *world = float_clip_world(FLOAT_HIDE_REPRO(", hide_clipped: true"));
    ecs_entity_t list = ecs_lookup(world, "scroll.anchor.list");
    ecs_entity_t item = ecs_lookup(world, "scroll.anchor.list.item");
    test_assert(list != 0 && item != 0);
    test_bool(ecs_get(world, list, LayoutFloat)->hide_clipped, true);

    /* The anchor is in view: the list is shown, past the bottom of the scroll view too */
    float center[3];
    float_clip_center(world, "scroll.anchor.list", center);
    test_flt(center[0], -10);
    test_flt(center[1], -15);
    test_bool(box_render_float_hidden(world, list), false);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), true);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list.item"), true);
    test_bool(box_render_clip_hides(world, list), false);
    float below[3] = { 0, -35, 0 };
    test_bool(box_render_clip_contains(world, list, below), true);

    /* Scrolled out, in the frame of the scroll: the list follows its anchor but hides, its
     * descendants with it */
    float_hide_scroll(world, 200);
    float_clip_center(world, "scroll.anchor", center);
    test_flt(center[1], 230);
    test_bool(box_render_clip_contains(world, ecs_lookup(world, "scroll.anchor"), center), false);
    float_clip_center(world, "scroll.anchor.list", center);
    test_flt(center[1], 185);
    test_bool(box_render_float_hidden(world, list), true);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), false);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list.item"), false);
    test_bool(box_render_clip_hides(world, list), true);
    test_bool(box_render_clip_hides(world, item), true);

    /* Partly in view (40..80 on y), the anchor keeps the list shown */
    float_hide_scroll(world, 30);
    test_bool(box_render_float_hidden(world, list), false);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), true);

    /* Scrolled back in, shown again */
    float_hide_scroll(world, 200);
    test_bool(box_render_float_hidden(world, list), true);
    float_hide_scroll(world, 0);
    test_bool(box_render_float_hidden(world, list), false);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), true);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list.item"), true);

    ecs_fini(world);
}

void LayoutFloat_no_hide_without_flag(void) {
    ecs_world_t *world = float_clip_world(FLOAT_HIDE_REPRO(""));
    ecs_entity_t list = ecs_lookup(world, "scroll.anchor.list");
    test_assert(list != 0);

    /* Without hide_clipped the list stays shown while its anchor is scrolled out */
    float_hide_scroll(world, 200);
    test_bool(box_render_float_hidden(world, list), false);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), true);

    /* The flag set at runtime takes effect, and cleared again it shows the list */
    ecs_set(world, list, LayoutFloat, {
        .v = VAlignTop, .anchor_v = VAlignBottom, .hide_clipped = true });
    ecs_progress(world, 0);
    test_bool(box_render_float_hidden(world, list), true);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), false);

    ecs_set(world, list, LayoutFloat, { .v = VAlignTop, .anchor_v = VAlignBottom });
    ecs_progress(world, 0);
    test_bool(box_render_float_hidden(world, list), false);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), true);

    ecs_fini(world);
}

void LayoutFloat_hide_clipped_clip_overlay_ignored(void) {
    /* A clipped overlay stays clipped by its ancestors and does not take hide_clipped */
    ecs_world_t *world = float_clip_world(FLOAT_HIDE_REPRO(", clip: true, hide_clipped: true"));
    ecs_entity_t list = ecs_lookup(world, "scroll.anchor.list");
    test_assert(list != 0);
    test_bool(box_render_float_hidden(world, list), false);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), true);

    /* Scrolled out with its anchor, the scroll view clips it as any other child */
    float_hide_scroll(world, 200);
    test_bool(box_render_float_hidden(world, list), false);
    test_bool(float_hide_keeps_center(world, "scroll.anchor.list"), false);
    float inside[3] = { -10, 45, 0 };
    test_bool(box_render_clip_contains(world, list, inside), true);

    ecs_fini(world);
}

void LayoutFloat_hide_clipped_not_drawn(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    test_assert(ecs_script_run(world, "hide", FLOAT_HIDE_REPRO(", hide_clipped: true"),
        NULL) == 0);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    test_assert(system != 0);
    box_render_draws_t *d = ecs_system_get(world, system)->ctx;
    ecs_entity_t list = ecs_lookup(world, "scroll.anchor.list");
    ecs_entity_t item = ecs_lookup(world, "scroll.anchor.list.item");
    test_assert(list != 0 && item != 0);

    /* The list and its descendants take the clip of the list, which leaves the clip of the
     * scroll view, and whose area holds everything while the anchor is in view */
    const DrawSlot *slot = ecs_get(world, list, DrawSlot);
    test_assert(slot != NULL);
    int32_t clip = slot->clip;
    test_assert(clip > 0);
    test_int(ecs_get(world, item, DrawSlot)->clip, clip);
    test_assert(clip != ecs_get(world, ecs_lookup(world, "scroll.filler"), DrawSlot)->clip);
    const box_render_clip_t *record = ecs_vec_get_t(&d->clips.records, box_render_clip_t, clip);
    test_assert(record->area[0] > 1e6f && record->area[1] > 1e6f);
    test_flt(record->area[3], 0);

    /* Scrolled out, the area is empty in the frame of the scroll, so nothing of it draws */
    float_hide_scroll(world, 200);
    test_int(ecs_get(world, list, DrawSlot)->clip, clip);
    record = ecs_vec_get_t(&d->clips.records, box_render_clip_t, clip);
    test_flt(record->area[0], 0);
    test_flt(record->area[1], 0);

    /* Back in view, it draws again */
    float_hide_scroll(world, 0);
    record = ecs_vec_get_t(&d->clips.records, box_render_clip_t, clip);
    test_assert(record->area[0] > 1e6f && record->area[1] > 1e6f);

    /* Without the flag the list no longer has a clip */
    ecs_set(world, list, LayoutFloat, { .v = VAlignTop, .anchor_v = VAlignBottom });
    ecs_progress(world, 0);
    test_int(ecs_get(world, list, DrawSlot)->clip, 0);
    test_int(ecs_get(world, item, DrawSlot)->clip, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

static char float_warnings[4096];
static int32_t float_warning_count;

static void LayoutFloatCaptureLog(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -2) {
        float_warning_count ++;
        if (strlen(float_warnings) + strlen(msg) + 2 < sizeof(float_warnings)) {
            strcat(float_warnings, msg);
            strcat(float_warnings, "\n");
        }
    }
}

void LayoutFloat_warn_parent_without_layout(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t root = ecs_entity(world, { .name = "menu" });
    ecs_set(world, root, Layout, { .direction = DirectionColumn });
    ecs_set(world, root, LayoutItem, {
        .width = { SizeModeFixed, 400 }, .height = { SizeModeFixed, 400 } });
    ecs_entity_t button = ecs_entity(world, { .name = "button", .parent = root });
    ecs_set(world, button, Rect, {1, 1});
    ecs_set(world, button, LayoutItem, {
        .width = { SizeModeFixed, 100 }, .height = { SizeModeFixed, 20 } });
    ecs_entity_t list = ecs_entity(world, { .name = "list", .parent = button });
    ecs_set(world, list, Position, {3, 4, 0});
    ecs_set(world, list, Rect, {1, 1});
    ecs_set(world, list, LayoutItem, {
        .width = { SizeModeFixed, 80 }, .height = { SizeModeFixed, 50 } });
    ecs_set(world, list, LayoutFloat, {
        .h = AlignLeft, .v = VAlignTop, .anchor_h = AlignLeft, .anchor_v = VAlignBottom });

    float_warnings[0] = '\0';
    float_warning_count = 0;
    int32_t level = ecs_log_set_level(-1);
    bool colors = ecs_log_enable_colors(false);
    ecs_os_api_log_t real_log = ecs_os_api.log_;
    ecs_os_api.log_ = LayoutFloatCaptureLog;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_os_api.log_ = real_log;
    ecs_log_set_level(level);
    ecs_log_enable_colors(colors);

    test_int(float_warning_count, 1);
    test_assert(strstr(float_warnings,
        "layout: 'menu.button.list' has a LayoutFloat but its parent 'menu.button' has no "
        "Layout, so the float is ignored") != NULL);
    test_flt(ecs_get(world, list, Position)->x, 3);
    test_flt(ecs_get(world, list, Position)->y, 4);

    ecs_fini(world);
}

void LayoutFloat_warn_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t root = ecs_entity(world, { .name = "popup" });
    ecs_set(world, root, Layout, { .direction = DirectionColumn });
    ecs_set(world, root, LayoutItem, {
        .width = { SizeModeFixed, 80 }, .height = { SizeModeFixed, 50 } });
    ecs_set(world, root, LayoutFloat, { .anchor_v = VAlignBottom });

    float_warnings[0] = '\0';
    float_warning_count = 0;
    int32_t level = ecs_log_set_level(-1);
    bool colors = ecs_log_enable_colors(false);
    ecs_os_api_log_t real_log = ecs_os_api.log_;
    ecs_os_api.log_ = LayoutFloatCaptureLog;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_os_api.log_ = real_log;
    ecs_log_set_level(level);
    ecs_log_enable_colors(colors);

    test_int(float_warning_count, 1);
    test_assert(strstr(float_warnings, "'popup' has a LayoutFloat") != NULL);
    test_assert(strstr(float_warnings, "the float is ignored") != NULL);

    ecs_fini(world);
}
