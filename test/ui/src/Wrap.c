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

    test_flt(ecs_get(world, para, TextBounds)->width, natural_width);
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

void Wrap_fit_text_keeps_its_lines(void) {
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
    ecs_progress(world, 0);

    const TextBounds *natural = ecs_get(world, ref, TextBounds);
    test_flt(ecs_get(world, t, TextBounds)->width, natural->width);
    test_flt(ecs_get(world, t, TextBounds)->height, natural->height);
    test_flt(ecs_get(world, t, LayoutBounds)->width, natural->width);

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
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, { .direction = DirectionColumn });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 300 } });
    ecs_entity_t t = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, t, Text, {
        .value = WRAP_PARAGRAPH, .size = 16, .width = 200, .font = font});
    ecs_set(world, t, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);

    float h200 = ecs_get(world, t, TextBounds)->height;
    test_assert(ecs_get(world, t, TextBounds)->width <= 200);
    test_flt(ecs_get(world, t, LayoutBounds)->width, 300);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, h200);

    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 120 } });
    ecs_progress(world, 0);

    test_assert(ecs_get(world, t, TextBounds)->width <= 120);
    test_assert(ecs_get(world, t, TextBounds)->height > h200);
    test_flt(ecs_get(world, panel, LayoutBounds)->height, ecs_get(world, t, TextBounds)->height);

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
