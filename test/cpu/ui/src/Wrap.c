/**
 * @file Wrap.c
 * @brief Text in a layout wraps to the width the layout gives it.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** A paragraph much wider than the panels of the tests */
#define WRAP_PARAGRAPH "Box lays out text in rows and columns and wraps long paragraphs " \
    "to the width of the panel that holds them"

void Wrap_fixed_panel_grows_in_height(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t ref = ecs_new(world);
    ecs_set(world, ref, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, {
        .direction = DirectionColumn, .padding = { 10, 10, 10, 10 } });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 200 } });
    ecs_set(world, panel, Rect, {1, 1});
    ecs_entity_t para = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, para, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_set(world, para, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    const TextBounds *natural = ecs_get(world, ref, TextBounds);
    const TextBounds *wrapped = ecs_get(world, para, TextBounds);
    test_assert(natural->width > 400);
    test_assert(natural->height > 0);
    test_assert(wrapped->width <= 180);
    test_assert(wrapped->width > 90);
    test_assert(wrapped->height >= 3 * natural->height);
    test_flt(ecs_get(world, para, LayoutBounds)->width, 180);
    test_flt(ecs_get(world, para, LayoutBounds)->height, wrapped->height);
    test_flt(ecs_get(world, panel, LayoutBounds)->width, 200);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, wrapped->height + 20);
    test_flt(ecs_get(world, panel, Rect)->height, wrapped->height + 20);
    test_flt(ecs_get(world, para, Position)->y, 0);

    ecs_fini(world);
}

void Wrap_panel_width_change_rewraps(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t ref = ecs_new(world);
    ecs_set(world, ref, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, {
        .direction = DirectionColumn, .padding = { 10, 10, 10, 10 } });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 200 } });
    ecs_entity_t para = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, para, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_set(world, para, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    float natural_width = ecs_get(world, ref, TextBounds)->width;
    float line = ecs_get(world, ref, TextBounds)->height;
    float narrow = ecs_get(world, para, TextBounds)->height;
    test_assert(narrow >= 3 * line);

    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 400 } });
    ecs_progress(world, 0);

    const TextBounds *wide = ecs_get(world, para, TextBounds);
    test_assert(wide->width <= 380);
    test_assert(wide->height < narrow);
    test_assert(wide->height >= 2 * line);
    test_flt(ecs_get(world, para, LayoutBounds)->width, 380);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, wide->height + 20);

    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 2000 } });
    ecs_progress(world, 0);

    test_assert(natural_width < 1980);
    test_flt(ecs_get(world, para, TextBounds)->width, 1980);
    test_flt(ecs_get(world, para, TextBounds)->height, line);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, line + 20);

    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 200 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, para, TextBounds)->height, narrow);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, narrow + 20);

    ecs_fini(world);
}

void Wrap_longest_word_is_minimum(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t word = ecs_new(world);
    ecs_set(world, word, Text, {.value = "wonderful", .size = 16, .font = font});
    ecs_entity_t row = ecs_new(world);
    ecs_set(world, row, Layout, { .direction = DirectionRow });
    ecs_set(world, row, LayoutItem, { .width = { SizeModeFixed, 30 } });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, row);
    ecs_set(world, t, Text, {.value = "a wonderful day", .size = 16, .font = font});
    ecs_set(world, t, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    const TextBounds *w = ecs_get(world, word, TextBounds);
    test_assert(w->width > 30);
    test_assert(fabsf(ecs_get(world, t, LayoutBounds)->width - w->width) < 0.01f);
    test_assert(fabsf(ecs_get(world, t, TextBounds)->width - w->width) < 0.01f);
    test_flt(ecs_get(world, t, TextBounds)->height, 3 * w->height);
    test_flt(ecs_get(world, row, LayoutBounds)->height, 3 * w->height);

    ecs_fini(world);
}

void Wrap_fixed_width_text(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t ref = ecs_new(world);
    ecs_set(world, ref, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_entity_t col = ecs_new(world);
    ecs_set(world, col, Layout, { .direction = DirectionColumn });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, col);
    ecs_set(world, t, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_set(world, t, LayoutItem, { .width = { SizeModeFixed, 150 } });
    ecs_progress(world, 0);

    const TextBounds *natural = ecs_get(world, ref, TextBounds);
    test_assert(ecs_get(world, t, TextBounds)->width <= 150);
    test_assert(ecs_get(world, t, TextBounds)->height >= 3 * natural->height);
    test_flt(ecs_get(world, col, LayoutBounds)->width, 150);
    test_flt(ecs_get(world, col, LayoutBounds)->height, ecs_get(world, t, TextBounds)->height);

    ecs_set(world, t, LayoutItem, { .width = { SizeModeFit } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, t, TextBounds)->width, natural->width);
    test_flt(ecs_get(world, t, TextBounds)->height, natural->height);
    test_flt(ecs_get(world, col, LayoutBounds)->width, natural->width);

    ecs_fini(world);
}

void Wrap_fit_text_wraps_unless_newlines(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t ref = ecs_new(world);
    ecs_set(world, ref, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_entity_t col = ecs_new(world);
    ecs_set(world, col, Layout, { .direction = DirectionColumn });
    ecs_set(world, col, LayoutItem, { .width = { SizeModeFixed, 100 } });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, col);
    ecs_set(world, t, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_entity_t lines = ecs_new(world);
    ecs_set(world, lines, Layout, { .direction = DirectionColumn });
    ecs_set(world, lines, LayoutItem, { .width = { SizeModeFixed, 100 } });
    ecs_entity_t n = ecs_new_w_pair(world, EcsChildOf, lines);
    ecs_set(world, n, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font,
        .wrap = TextWrapNewlines});
    ecs_progress(world, 0);

    const TextBounds *natural = ecs_get(world, ref, TextBounds);
    test_flt(ecs_get(world, t, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, t, TextBounds)->width, 100);
    test_assert(ecs_get(world, t, TextBounds)->height >= 5 * natural->height);
    test_flt(ecs_get(world, col, LayoutBounds)->height, ecs_get(world, t, TextBounds)->height);
    test_flt(ecs_get(world, n, TextBounds)->width, natural->width);
    test_flt(ecs_get(world, n, TextBounds)->height, natural->height);
    test_flt(ecs_get(world, n, LayoutBounds)->width, natural->width);

    ecs_fini(world);
}

void Wrap_text_width_still_applies(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t ref = ecs_new(world);
    ecs_set(world, ref, Text, {
        .value = WRAP_PARAGRAPH, .size = 16, .width = 200, .font = font});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, { .direction = DirectionColumn });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 300 } });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, t, Text, {
        .value = WRAP_PARAGRAPH, .size = 16, .width = 200, .font = font});
    ecs_set(world, t, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    float h200 = ecs_get(world, ref, TextBounds)->height;
    test_assert(ecs_get(world, ref, TextBounds)->width <= 200);
    test_flt(ecs_get(world, t, TextBounds)->height, h200);
    test_flt(ecs_get(world, t, LayoutBounds)->width, 300);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, h200);

    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 120 } });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, t, TextBounds)->width, 120);
    test_assert(ecs_get(world, t, TextBounds)->height > h200);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, ecs_get(world, t, TextBounds)->height);

    ecs_fini(world);
}

void Wrap_value_change_rewraps(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, {
        .direction = DirectionColumn, .padding = { 10, 10, 10, 10 } });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 200 } });
    ecs_entity_t para = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, para, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_progress(world, 0);

    float one = ecs_get(world, para, TextBounds)->height;
    ecs_set(world, para, Text, {.value = WRAP_PARAGRAPH " " WRAP_PARAGRAPH, .size = 16,
        .font = font});
    ecs_progress(world, 0);

    float two = ecs_get(world, para, TextBounds)->height;
    test_assert(two > one * 1.5f);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, two + 20);
    test_flt(ecs_get(world, para, LayoutBounds)->width, 180);

    ecs_set(world, para, Text, {.value = "Hi", .size = 16, .font = font});
    ecs_progress(world, 0);

    float line = ecs_get(world, para, TextBounds)->height;
    test_assert(line < one / 3);
    test_assert(ecs_get(world, para, LayoutBounds)->width < 40);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, line + 20);

    ecs_fini(world);
}

void Wrap_text_removed_drops_its_size(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t col = ecs_new(world);
    ecs_set(world, col, Layout, { .direction = DirectionColumn });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, col);
    ecs_set(world, t, Text, {.value = "Hello world", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    test_assert(ecs_get(world, col, LayoutBounds)->width > 40);

    ecs_remove(world, t, Text);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, col, LayoutBounds)->width, 0);
    test_flt(ecs_get(world, col, LayoutBounds)->height, 0);
    test_flt(ecs_get(world, t, LayoutBounds)->width, 0);

    ecs_fini(world);
}

void Wrap_leaving_the_tree_unwraps(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t ref = ecs_new(world);
    ecs_set(world, ref, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, { .direction = DirectionColumn });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 200 } });
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, a, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, b, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_progress(world, 0);

    const TextBounds *natural = ecs_get(world, ref, TextBounds);
    test_flt(ecs_get(world, a, TextBounds)->width, 200);
    test_flt(ecs_get(world, b, TextBounds)->width, 200);

    ecs_entity_t other = ecs_new(world);
    ecs_add_pair(world, b, EcsChildOf, other);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, b, TextBounds)->width, natural->width);
    test_flt(ecs_get(world, b, TextBounds)->height, natural->height);
    test_flt(ecs_get(world, a, TextBounds)->width, 200);

    ecs_remove(world, panel, Layout);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, a, TextBounds)->width, natural->width);
    test_flt(ecs_get(world, a, TextBounds)->height, natural->height);

    ecs_fini(world);
}

void Wrap_lines_align_in_the_box(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, {
        .direction = DirectionColumn, .padding = { 0, 0, 7, 3 } });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 210 } });
    ecs_entity_t para = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, para, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font,
        .align = AlignLeft});
    ecs_progress(world, 0);

    const TextBounds *b = ecs_get(world, para, TextBounds);
    const Position *p = ecs_get(world, para, Position);
    test_flt(b->width, 200);
    test_flt(p->x - b->width / 2, -105 + 7);

    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 310 } });
    ecs_progress(world, 0);

    b = ecs_get(world, para, TextBounds);
    p = ecs_get(world, para, Position);
    test_flt(b->width, 300);
    test_flt(p->x - b->width / 2, -155 + 7);

    ecs_fini(world);
}

void Wrap_grow_paragraph_in_fit_card(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t ref = ecs_new(world);
    ecs_set(world, ref, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, { .direction = DirectionColumn });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 200 } });
    ecs_entity_t card = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, card, Layout, {
        .direction = DirectionColumn, .padding = { 5, 5, 5, 5 } });
    ecs_set(world, card, Rect, {1, 1});
    ecs_entity_t para = ecs_new_w_pair(world, EcsChildOf, card);
    ecs_set(world, para, Text, {.value = WRAP_PARAGRAPH, .size = 16, .font = font});
    ecs_set(world, para, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    const TextBounds *natural = ecs_get(world, ref, TextBounds);
    const TextBounds *b = ecs_get(world, para, TextBounds);
    test_flt(ecs_get(world, card, LayoutBounds)->width, 200);
    test_flt(ecs_get(world, card, Rect)->width, 200);
    test_flt(ecs_get(world, para, LayoutBounds)->width, 190);
    test_flt(b->width, 190);
    test_assert(b->height >= 4 * natural->height);
    test_flt(ecs_get(world, card, LayoutBounds)->height, b->height + 10);

    ecs_fini(world);
}

void Wrap_script_paragraph(void) {
    // ui_fixes/text_wrap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    const char *code =
        "using box.layout\n"
        "using box.text\n"
        "panel {\n"
        "  Layout: {direction: Column, padding: {5, 5, 5, 5}}\n"
        "  LayoutItem: {width: {mode: Fixed, value: 160}}\n"
        "  para {\n"
        "    Text: {value: \"" WRAP_PARAGRAPH "\"}\n"
        "    LayoutItem: {width: {mode: Grow}}\n"
        "  }\n"
        "}\n";
    test_assert(ecs_script_run(world, "wrap", code, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t panel = ecs_lookup(world, "panel");
    ecs_entity_t para = ecs_lookup(world, "panel.para");
    const TextBounds *b = ecs_get(world, para, TextBounds);
    test_assert(b->width <= 150);
    test_assert(b->height > 50);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, b->height + 10);

    ecs_fini(world);
}

/** Create a world whose default font is loaded, through a Text that uses it.
 *
 * @param font Receives box.text.DefaultFont
 * @return The world
 */
static ecs_world_t* wrap_font_world(
    ecs_entity_t *font)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    *font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t ref = ecs_new(world);
    ecs_set(world, ref, Text, {.value = "loaded", .size = 16, .font = *font});
    ecs_progress(world, 0);

    return world;
}

/** Get the x of the caret before a byte of a Text.
 *
 * @param world The world
 * @param e The entity with the Text
 * @param offset The byte
 * @param line Receives the line of the caret, may be NULL
 * @return The x
 */
static float wrap_caret_x(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t offset,
    int32_t *line)
{
    TextCaret caret = {0};
    test_assert(box_text_caret(world, e, offset, &caret));
    if (line) {
        *line = caret.line;
    }

    return caret.x;
}

void Wrap_break_lines_match_layout(void) {
    // console/wrap
    ecs_entity_t font;
    ecs_world_t *world = wrap_font_world(&font);
    Text look = {.value = WRAP_PARAGRAPH, .size = 16, .width = 200, .font = font};
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Text, &look);
    ecs_progress(world, 0);

    ecs_vec_t starts;
    ecs_vec_init_t(NULL, &starts, int32_t, 0);
    int32_t count = box_text_break_lines(world, &look, &starts);
    test_assert(count >= 3);
    test_int(count, box_text_line(world, e, 0, NULL, NULL));
    test_int(ecs_vec_count(&starts), count);
    for (int32_t i = 0; i < count; i ++) {
        int32_t start = -1;
        box_text_line(world, e, i, &start, NULL);
        test_int(*ecs_vec_get_t(&starts, int32_t, i), start);
    }

    test_int(box_text_break_lines(world, &look, NULL), count);

    look.width = 0;
    test_int(box_text_break_lines(world, &look, &starts), 1);
    test_int(*ecs_vec_get_t(&starts, int32_t, 0), 0);
    look.value = "";
    test_int(box_text_break_lines(world, &look, &starts), 1);
    look.value = "one\ntwo\n";
    test_int(box_text_break_lines(world, &look, &starts), 3);
    test_int(*ecs_vec_get_t(&starts, int32_t, 1), 4);
    test_int(*ecs_vec_get_t(&starts, int32_t, 2), 8);
    look.font = 0;
    test_int(box_text_break_lines(world, &look, &starts), 0);
    test_int(ecs_vec_count(&starts), 0);

    ecs_vec_fini_t(NULL, &starts, int32_t);
    ecs_fini(world);
}

void Wrap_long_word_breaks_inside(void) {
    // console/wrap
    ecs_entity_t font;
    ecs_world_t *world = wrap_font_world(&font);
    char word[201];
    for (int32_t i = 0; i < 200; i ++) {
        word[i] = (char)('a' + i % 26);
    }

    word[200] = '\0';
    Text look = {.value = word, .size = 16, .line_height = 20, .width = 200, .font = font};
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Text, &look);
    ecs_progress(world, 0);

    ecs_vec_t starts;
    ecs_vec_init_t(NULL, &starts, int32_t, 0);
    int32_t count = box_text_break_lines(world, &look, &starts);
    test_assert(count >= 4);
    for (int32_t i = 1; i < count; i ++) {
        test_assert(*ecs_vec_get_t(&starts, int32_t, i) >
            *ecs_vec_get_t(&starts, int32_t, i - 1));
    }

    test_assert(ecs_get(world, e, TextBounds)->width <= 200);
    test_flt(ecs_get(world, e, TextBounds)->height, (float)count * 20);
    test_int(box_text_line(world, e, 0, NULL, NULL), count);

    ecs_vec_fini_t(NULL, &starts, int32_t);
    ecs_fini(world);
}

void Wrap_hang_indents_wrapped_lines(void) {
    // console/wrap
    ecs_entity_t font;
    ecs_world_t *world = wrap_font_world(&font);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {
        .value = "> " WRAP_PARAGRAPH, .size = 16, .width = 200, .font = font, .hang = 2 });
    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, Text, {
        .value = "> " WRAP_PARAGRAPH, .size = 16, .width = 200, .font = font });
    ecs_progress(world, 0);

    int32_t lines = box_text_line(world, e, 0, NULL, NULL);
    test_assert(lines >= 3);
    float left = wrap_caret_x(world, e, 0, NULL);
    float hang = wrap_caret_x(world, e, 2, NULL);
    test_assert(hang > left);
    for (int32_t i = 1; i < lines; i ++) {
        int32_t start, line;
        box_text_line(world, e, i, &start, NULL);
        test_flt(wrap_caret_x(world, e, start, &line), hang);
        test_int(line, i);
    }

    int32_t start;
    box_text_line(world, flat, 1, &start, NULL);
    test_flt(wrap_caret_x(world, flat, start, NULL), wrap_caret_x(world, flat, 0, NULL));
    test_assert(ecs_get(world, e, TextBounds)->width <= 200);

    const Text *t = ecs_get(world, e, Text);
    test_int(box_text_break_lines(world, t, NULL), lines);

    ecs_fini(world);
}

void Wrap_hang_long_word_breaks_at_hang(void) {
    // console/wrap
    ecs_entity_t font;
    ecs_world_t *world = wrap_font_world(&font);
    char value[203] = "  ";
    for (int32_t i = 0; i < 200; i ++) {
        value[2 + i] = (char)('a' + i % 26);
    }

    value[202] = '\0';
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {.value = value, .size = 16, .width = 200, .font = font, .hang = 2});
    ecs_progress(world, 0);

    int32_t lines = box_text_line(world, e, 0, NULL, NULL);
    test_assert(lines >= 4);
    float hang = wrap_caret_x(world, e, 2, NULL);
    test_assert(hang > wrap_caret_x(world, e, 0, NULL));
    for (int32_t i = 1; i < lines; i ++) {
        int32_t start;
        box_text_line(world, e, i, &start, NULL);
        test_flt(wrap_caret_x(world, e, start, NULL), hang);
    }

    test_assert(ecs_get(world, e, TextBounds)->width <= 200);

    ecs_fini(world);
}

void Wrap_hang_past_half_ignored(void) {
    // console/wrap
    ecs_entity_t font;
    ecs_world_t *world = wrap_font_world(&font);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {
        .value = "a fairly long first part " WRAP_PARAGRAPH, .size = 16, .width = 200,
        .font = font, .hang = 22 });
    ecs_progress(world, 0);

    test_assert(wrap_caret_x(world, e, 22, NULL) - wrap_caret_x(world, e, 0, NULL) > 100);
    int32_t start;
    test_assert(box_text_line(world, e, 0, NULL, NULL) >= 2);
    box_text_line(world, e, 1, &start, NULL);
    test_flt(wrap_caret_x(world, e, start, NULL), wrap_caret_x(world, e, 0, NULL));

    ecs_fini(world);
}

void Wrap_hang_not_after_newline(void) {
    // console/wrap
    ecs_entity_t font;
    ecs_world_t *world = wrap_font_world(&font);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {
        .value = "> first\nsecond", .size = 16, .width = 200, .font = font, .hang = 2 });
    ecs_progress(world, 0);

    test_int(box_text_line(world, e, 0, NULL, NULL), 2);
    test_flt(wrap_caret_x(world, e, 8, NULL), wrap_caret_x(world, e, 0, NULL));

    ecs_fini(world);
}
