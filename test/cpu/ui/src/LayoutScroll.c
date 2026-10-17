/**
 * @file LayoutScroll.c
 * @brief LayoutScroll shifts the children of an element and LayoutContent measures them.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** A paragraph much wider than the views of the tests */
#define SCROLL_PARAGRAPH "Box lays out text in rows and columns and wraps long paragraphs " \
    "to the width of the panel that holds them"

/** Create a scroll view of a fixed size.
 *
 * @param world The world
 * @param direction Direction of its layout
 * @param width Its width
 * @param height Its height
 * @param scroll Its LayoutScroll
 * @return The view
 */
static ecs_entity_t scroll_view(
    ecs_world_t *world,
    Direction direction,
    float width,
    float height,
    LayoutScroll scroll)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Layout, { .direction = direction });
    ecs_set(world, result, LayoutItem, {
        .width = { SizeModeFixed, width }, .height = { SizeModeFixed, height } });
    ecs_set_ptr(world, result, LayoutScroll, &scroll);
    return result;
}

/** Create a child with a Rect.
 *
 * @param world The world
 * @param parent The parent
 * @param width Width of the Rect
 * @param height Height of the Rect
 * @return The child
 */
static ecs_entity_t scroll_rect(
    ecs_world_t *world,
    ecs_entity_t parent,
    float width,
    float height)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, result, Rect, { width, height });
    return result;
}

/** Create a child that grows on x without a content minimum, whose content is 80 wide.
 *
 * @param world The world
 * @param parent The parent
 * @return The child
 */
static ecs_entity_t scroll_grow_child(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, result, Layout, { .direction = DirectionRow });
    ecs_set(world, result, LayoutItem, { .width = { SizeModeGrow, 1 } });
    scroll_rect(world, result, 80, 10);
    return result;
}

void LayoutScroll_offset_shifts_column(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .y = 10, .vertical = true });
    ecs_entity_t a = scroll_rect(world, v, 50, 40);
    ecs_entity_t b = scroll_rect(world, v, 50, 40);
    ecs_entity_t c = scroll_rect(world, v, 50, 40);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -25);
    test_flt(ecs_get(world, a, Position)->y, 40);
    test_flt(ecs_get(world, b, Position)->y, 0);
    test_flt(ecs_get(world, c, Position)->y, -40);
    const LayoutContent *content = ecs_get(world, v, LayoutContent);
    test_assert(content != NULL);
    test_flt(content->height, 120);
    test_flt(content->max_y, 20);
    test_flt(content->y, 10);
    test_flt(ecs_get(world, v, LayoutBounds)->height, 100);

    ecs_fini(world);
}

void LayoutScroll_offset_shifts_row(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionRow, 100, 100,
        (LayoutScroll){ .x = 15, .horizontal = true });
    ecs_entity_t a = scroll_rect(world, v, 40, 20);
    ecs_entity_t b = scroll_rect(world, v, 40, 20);
    ecs_entity_t c = scroll_rect(world, v, 40, 20);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->x, -45);
    test_flt(ecs_get(world, b, Position)->x, -5);
    test_flt(ecs_get(world, c, Position)->x, 35);
    test_flt(ecs_get(world, a, Position)->y, 40);
    const LayoutContent *content = ecs_get(world, v, LayoutContent);
    test_flt(content->width, 120);
    test_flt(content->max_x, 20);
    test_flt(content->x, 15);

    ecs_fini(world);
}

void LayoutScroll_content_column(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .vertical = true });
    ecs_set(world, v, Layout, { .direction = DirectionColumn, .spacing = 5 });
    scroll_rect(world, v, 30, 40);
    scroll_rect(world, v, 60, 50);
    scroll_rect(world, v, 20, 30);
    ecs_progress(world, 0);

    const LayoutContent *content = ecs_get(world, v, LayoutContent);
    test_flt(content->width, 60);
    test_flt(content->height, 130);
    test_flt(content->max_x, 0);
    test_flt(content->max_y, 30);
    test_flt(content->x, 0);
    test_flt(content->y, 0);

    ecs_fini(world);
}

void LayoutScroll_content_row(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionRow, 100, 50,
        (LayoutScroll){ .horizontal = true });
    ecs_set(world, v, Layout, { .direction = DirectionRow, .spacing = 10 });
    scroll_rect(world, v, 40, 20);
    scroll_rect(world, v, 50, 30);
    scroll_rect(world, v, 30, 10);
    ecs_progress(world, 0);

    const LayoutContent *content = ecs_get(world, v, LayoutContent);
    test_flt(content->width, 140);
    test_flt(content->height, 30);
    test_flt(content->max_x, 40);
    test_flt(content->max_y, 0);

    ecs_fini(world);
}

void LayoutScroll_content_padding(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .vertical = true });
    ecs_set(world, v, Layout, {
        .direction = DirectionColumn,
        .padding = { .top = 10, .bottom = 20, .left = 5, .right = 15 } });
    ecs_entity_t a = scroll_rect(world, v, 50, 60);
    ecs_entity_t b = scroll_rect(world, v, 50, 60);
    ecs_progress(world, 0);

    const LayoutContent *content = ecs_get(world, v, LayoutContent);
    test_flt(content->width, 70);
    test_flt(content->height, 150);
    test_flt(content->max_y, 50);
    test_flt(ecs_get(world, a, Position)->x, -20);
    test_flt(ecs_get(world, a, Position)->y, 10);

    ecs_set(world, v, LayoutScroll, { .y = 50, .vertical = true });
    ecs_progress(world, 0);

    /* Scrolled to the end, the bottom padding is in view under the last child */
    test_flt(ecs_get(world, a, Position)->y, 60);
    test_flt(ecs_get(world, b, Position)->y, 0);
    test_flt(ecs_get(world, b, Position)->y - 30, -50 + 20);

    ecs_fini(world);
}

void LayoutScroll_content_wrapping_text(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t v = scroll_view(world, DirectionColumn, 200, 50,
        (LayoutScroll){ .vertical = true });
    ecs_set(world, v, Layout, {
        .direction = DirectionColumn, .padding = { 10, 10, 10, 10 } });
    ecs_entity_t para = ecs_new_w_pair(world, EcsChildOf, v);
    ecs_set(world, para, Text, {.value = SCROLL_PARAGRAPH, .size = 16, .font = font});
    ecs_set(world, para, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    const TextBounds *tb = ecs_get(world, para, TextBounds);
    const LayoutContent *content = ecs_get(world, v, LayoutContent);
    test_assert(tb->height > 30);
    test_flt(ecs_get(world, para, LayoutBounds)->width, 180);
    test_flt(content->width, 200);
    test_flt(content->height, tb->height + 20);
    test_flt(content->max_x, 0);
    test_flt(content->max_y, tb->height + 20 - 50);
    test_flt(ecs_get(world, v, LayoutBounds)->height, 50);

    ecs_fini(world);
}

void LayoutScroll_content_fits(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .y = 10, .vertical = true });
    ecs_entity_t a = scroll_rect(world, v, 50, 40);
    ecs_progress(world, 0);

    const LayoutContent *content = ecs_get(world, v, LayoutContent);
    test_flt(content->width, 50);
    test_flt(content->height, 40);
    test_flt(content->max_x, 0);
    test_flt(content->max_y, 0);
    test_flt(content->y, 0);
    test_flt(ecs_get(world, a, Position)->y, 30);

    ecs_fini(world);
}

void LayoutScroll_clamp_offset(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .y = 1000, .vertical = true });
    ecs_entity_t a = scroll_rect(world, v, 50, 80);
    scroll_rect(world, v, 50, 80);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, v, LayoutContent)->y, 60);
    test_flt(ecs_get(world, a, Position)->y, 70);

    ecs_set(world, v, LayoutScroll, { .y = -5, .vertical = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, v, LayoutContent)->y, 0);
    test_flt(ecs_get(world, a, Position)->y, 10);

    ecs_set(world, v, LayoutScroll, { .y = 2000, .vertical = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, v, LayoutContent)->y, 60);
    test_flt(ecs_get(world, a, Position)->y, 70);

    ecs_fini(world);
}

void LayoutScroll_clamp_not_written_back(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .y = 1000, .x = -7, .vertical = true });
    scroll_rect(world, v, 50, 80);
    scroll_rect(world, v, 50, 80);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, v, LayoutScroll)->y, 1000);
    test_flt(ecs_get(world, v, LayoutScroll)->x, -7);
    test_flt(ecs_get(world, v, LayoutContent)->y, 60);
    test_flt(ecs_get(world, v, LayoutContent)->x, 0);

    ecs_fini(world);
}

void LayoutScroll_not_narrowed_along_scroll_axis(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t plain = scroll_view(world, DirectionRow, 100, 50, (LayoutScroll){0});
    ecs_remove(world, plain, LayoutScroll);
    ecs_entity_t pa = scroll_grow_child(world, plain);
    ecs_entity_t pb = scroll_grow_child(world, plain);
    ecs_entity_t v = scroll_view(world, DirectionRow, 100, 50,
        (LayoutScroll){ .horizontal = true });
    ecs_entity_t a = scroll_grow_child(world, v);
    ecs_entity_t b = scroll_grow_child(world, v);
    ecs_progress(world, 0);

    /* Without scrolling the children share the row */
    test_flt(ecs_get(world, pa, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, pb, LayoutBounds)->width, 50);

    /* Scrolling on x, they keep their fit width */
    test_flt(ecs_get(world, a, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, b, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, a, Position)->x, -10);
    test_flt(ecs_get(world, b, Position)->x, 70);
    test_flt(ecs_get(world, v, LayoutContent)->width, 160);
    test_flt(ecs_get(world, v, LayoutContent)->max_x, 60);
    test_flt(ecs_get(world, v, LayoutBounds)->width, 100);

    ecs_fini(world);
}

void LayoutScroll_grow_across_scroll_axis(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .vertical = true });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, v);
    ecs_set(world, a, Layout, { .direction = DirectionRow });
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeGrow, 1 }, .height = { SizeModeGrow, 1 } });
    scroll_rect(world, a, 30, 30);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 30);
    test_flt(ecs_get(world, a, Position)->y, 35);

    ecs_fini(world);
}

void LayoutScroll_scroll_change_moves_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .vertical = true });
    ecs_entity_t a = scroll_rect(world, v, 50, 80);
    ecs_entity_t b = scroll_rect(world, v, 50, 80);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->y, 10);
    test_flt(ecs_get(world, b, Position)->y, -70);

    ecs_set(world, v, LayoutScroll, { .y = 15, .vertical = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->y, 25);
    test_flt(ecs_get(world, b, Position)->y, -55);
    test_flt(ecs_get(world, a, Position)->x, -25);
    test_flt(ecs_get(world, v, LayoutContent)->y, 15);

    ecs_set(world, v, LayoutScroll, { .y = 5, .vertical = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->y, 15);
    test_flt(ecs_get(world, b, Position)->y, -65);

    ecs_fini(world);
}

void LayoutScroll_scroll_change_does_not_remeasure(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionColumn, 100, 100,
        (LayoutScroll){ .vertical = true });
    ecs_entity_t a = scroll_rect(world, v, 50, 80);
    ecs_entity_t b = scroll_rect(world, v, 50, 80);
    ecs_progress(world, 0);

    /* A change no observer sees: a layout that measures again picks it up */
    Rect *r = ecs_get_mut(world, a, Rect);
    r->width = 99;
    r->height = 99;

    ecs_set(world, v, LayoutScroll, { .y = 10, .vertical = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, a, LayoutBounds)->height, 80);
    test_flt(ecs_get(world, a, Position)->y, 20);
    test_flt(ecs_get(world, b, Position)->y, -60);
    test_flt(ecs_get(world, v, LayoutContent)->height, 160);

    ecs_modified(world, v, Layout);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->height, 99);
    test_flt(ecs_get(world, v, LayoutContent)->height, 179);
    test_flt(ecs_get(world, a, Position)->y, 50 - 49.5f + 10);

    ecs_fini(world);
}

void LayoutScroll_axes_change_lays_out_again(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionRow, 100, 50, (LayoutScroll){0});
    ecs_entity_t a = scroll_grow_child(world, v);
    scroll_grow_child(world, v);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, v, LayoutContent)->width, 100);
    test_flt(ecs_get(world, v, LayoutContent)->max_x, 0);

    ecs_set(world, v, LayoutScroll, { .x = 20, .horizontal = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, a, Position)->x, -30);
    test_flt(ecs_get(world, v, LayoutContent)->max_x, 60);

    ecs_fini(world);
}

void LayoutScroll_remove_scroll_lays_out_again(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionRow, 100, 50,
        (LayoutScroll){ .x = 20, .horizontal = true });
    ecs_entity_t a = scroll_grow_child(world, v);
    scroll_grow_child(world, v);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 80);
    test_flt(ecs_get(world, a, Position)->x, -30);

    ecs_remove(world, v, LayoutScroll);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, LayoutBounds)->width, 50);
    test_flt(ecs_get(world, a, Position)->x, -25);

    ecs_fini(world);
}

void LayoutScroll_direction_none_keeps_free_children(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t v = scroll_view(world, DirectionNone, 100, 100,
        (LayoutScroll){ .y = 10, .vertical = true });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, v);
    ecs_set(world, a, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 150 },
        .v = VAlignTop });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, v);
    ecs_set(world, b, Position, {5, 5});
    ecs_set(world, b, Rect, {10, 10});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, v, LayoutContent)->height, 150);
    test_flt(ecs_get(world, v, LayoutContent)->max_y, 50);
    test_flt(ecs_get(world, a, Position)->y, -15);
    test_flt(ecs_get(world, b, Position)->x, 5);
    test_flt(ecs_get(world, b, Position)->y, 5);

    ecs_set(world, v, LayoutScroll, { .y = 30, .vertical = true });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, Position)->y, 5);
    test_flt(ecs_get(world, b, Position)->y, 5);

    ecs_fini(world);
}

void LayoutScroll_script(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *code =
        "using box.layout\n"
        "using box.geometry\n"
        "view {\n"
        "  Layout: {direction: Column}\n"
        "  LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 100}}\n"
        "  LayoutScroll: {y: 10, vertical: true}\n"
        "  a { Rect: {50, 80} }\n"
        "  b { Rect: {50, 80} }\n"
        "}\n";
    test_assert(ecs_script_run(world, "scroll", code, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t view = ecs_lookup(world, "view");
    ecs_entity_t a = ecs_lookup(world, "view.a");
    const LayoutContent *content = ecs_get(world, view, LayoutContent);
    test_assert(content != NULL);
    test_flt(content->height, 160);
    test_flt(content->max_y, 60);
    test_flt(content->y, 10);
    test_flt(ecs_get(world, a, Position)->y, 20);

    ecs_fini(world);
}
