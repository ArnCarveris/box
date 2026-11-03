/**
 * @file TextSpans.c
 * @brief Colored byte ranges of a Text resolve per glyph without a new layout, styled ones
 * lay the Text out again.
 */

#include <box.h>
#include <bake_test.h>
#include <text.h>
#include <string.h>
#include <math.h>

/** a, e acute of 2 bytes, euro sign of 3 bytes and b: glyphs at offsets 0, 1, 3 and 6 */
#define SPANS_MULTI_BYTE "a\xC3\xA9\xE2\x82\xAC" "b"

/** A string that wraps at a width of 60, its second "green" at bytes 19 to 24 */
#define SPANS_PARAGRAPH "red green blue red green blue"

/** Create a headless world with box imported.
 *
 * @return The world
 */
static ecs_world_t* spans_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    return world;
}

/** Create a Text with the default font, laid out by one frame.
 *
 * @param world The world
 * @param value The string
 * @param width Width the lines wrap at, 0 for none
 * @return The entity
 */
static ecs_entity_t spans_text(
    ecs_world_t *world,
    const char *value,
    float width)
{
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, { .value = (char*)value, .size = 16, .width = width, .font = font });
    ecs_progress(world, 0);

    return e;
}

/** Replace the TextSpans of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param spans The spans
 * @param count Number of spans
 */
static void spans_set(
    ecs_world_t *world,
    ecs_entity_t e,
    const TextSpan *spans,
    int32_t count)
{
    TextSpans *ts = ecs_ensure(world, e, TextSpans);
    int32_t i;
    ecs_vec_clear(&ts->spans);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &ts->spans, TextSpan) = spans[i];
    }

    ecs_modified(world, e, TextSpans);
}

/** Get the glyphs of a laid out Text, which must have a layout.
 *
 * @param world The world
 * @param e The entity
 * @param count Receives the number of glyphs
 * @return The glyphs
 */
static const box_text_glyph_t* spans_glyphs(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t *count)
{
    const TextGlyphs *tg = ecs_get(world, e, TextGlyphs);
    test_assert(tg != NULL);
    test_assert(tg->ready);
    *count = ecs_vec_count(&tg->glyphs);

    return ecs_vec_first(&tg->glyphs);
}

/** Get the span color of a glyph of a Text.
 *
 * @param world The world
 * @param e The entity
 * @param glyph Index of the glyph
 * @return The color, NULL for the color of the entity
 */
static const rgba* spans_color(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t glyph)
{
    int32_t count = 0;
    const box_text_glyph_t *glyphs = spans_glyphs(world, e, &count);
    const Text *text = ecs_get(world, e, Text);
    test_assert(glyph < count);

    return box_text_span_color(ecs_get(world, e, TextSpans),
        (int32_t)strlen(text->value), glyphs[glyph].offset);
}

/** Check that a glyph takes a color.
 *
 * @param world The world
 * @param e The entity
 * @param glyph Index of the glyph
 * @param r Red
 * @param g Green
 * @param b Blue
 * @param a Alpha
 */
static void spans_expect(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t glyph,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a)
{
    const rgba *c = spans_color(world, e, glyph);
    test_assert(c != NULL);
    test_int(c->r, r);
    test_int(c->g, g);
    test_int(c->b, b);
    test_int(c->a, a);
}

/** Count the TextGlyphs set events of an entity.
 *
 * @param it The iterator of the observer
 */
static void spans_count_layouts(
    ecs_iter_t *it)
{
    int32_t *count = it->ctx;
    ecs_entity_t e = (ecs_entity_t)(uintptr_t)it->callback_ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        *count += it->entities[i] == e;
    }
}

/** Observe the layouts of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param count Incremented by every layout
 */
static void spans_watch_layouts(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t *count)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(TextGlyphs) }},
        .events = { EcsOnSet },
        .callback = spans_count_layouts,
        .ctx = count,
        .callback_ctx = (void*)(uintptr_t)e
    });
}

void TextSpans_glyph_to_color(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, "ab cd", 0);
    int32_t count = 0;
    const box_text_glyph_t *glyphs = spans_glyphs(world, e, &count);
    test_int(count, 4);
    test_int(glyphs[0].offset, 0);
    test_int(glyphs[1].offset, 1);
    test_int(glyphs[2].offset, 3);
    test_int(glyphs[3].offset, 4);

    spans_set(world, e, (TextSpan[]){
        { 0, 2, { 255, 0, 0, 255 } },
        { 3, 5, { 0, 255, 0, 128 } }
    }, 2);
    spans_expect(world, e, 0, 255, 0, 0, 255);
    spans_expect(world, e, 1, 255, 0, 0, 255);
    spans_expect(world, e, 2, 0, 255, 0, 128);
    spans_expect(world, e, 3, 0, 255, 0, 128);

    ecs_fini(world);
}

void TextSpans_outside_spans_keep_entity_color(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, "abcd", 0);
    test_assert(spans_color(world, e, 0) == NULL);

    spans_set(world, e, (TextSpan[]){{ 1, 3, { 10, 20, 30, 255 } }}, 1);
    test_assert(spans_color(world, e, 0) == NULL);
    spans_expect(world, e, 1, 10, 20, 30, 255);
    spans_expect(world, e, 2, 10, 20, 30, 255);
    test_assert(spans_color(world, e, 3) == NULL);

    spans_set(world, e, NULL, 0);
    test_assert(spans_color(world, e, 1) == NULL);

    ecs_fini(world);
}

void TextSpans_later_span_wins(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, "abcdef", 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 6, { 255, 0, 0, 255 } },
        { 2, 4, { 0, 255, 0, 255 } },
        { 3, 5, { 0, 0, 255, 255 } }
    }, 3);
    spans_expect(world, e, 0, 255, 0, 0, 255);
    spans_expect(world, e, 1, 255, 0, 0, 255);
    spans_expect(world, e, 2, 0, 255, 0, 255);
    spans_expect(world, e, 3, 0, 0, 255, 255);
    spans_expect(world, e, 4, 0, 0, 255, 255);
    spans_expect(world, e, 5, 255, 0, 0, 255);

    spans_set(world, e, (TextSpan[]){
        { 3, 5, { 0, 0, 255, 255 } },
        { 0, 6, { 255, 0, 0, 255 } }
    }, 2);
    spans_expect(world, e, 3, 255, 0, 0, 255);

    ecs_fini(world);
}

void TextSpans_ranges_clipped(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, "abcd", 0);
    spans_set(world, e, (TextSpan[]){
        { -10, 1, { 255, 0, 0, 255 } },
        { 3, 1000, { 0, 255, 0, 255 } },
        { 4, 8, { 0, 0, 255, 255 } }
    }, 3);
    spans_expect(world, e, 0, 255, 0, 0, 255);
    test_assert(spans_color(world, e, 1) == NULL);
    test_assert(spans_color(world, e, 2) == NULL);
    spans_expect(world, e, 3, 0, 255, 0, 255);

    test_assert(box_text_span_color(ecs_get(world, e, TextSpans), 4, 4) == NULL);
    test_assert(box_text_span_color(ecs_get(world, e, TextSpans), 4, -1) == NULL);
    test_assert(box_text_span_color(NULL, 4, 0) == NULL);

    ecs_fini(world);
}

void TextSpans_empty_and_inverted_ranges(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, "abcd", 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 4, { 255, 0, 0, 255 } },
        { 1, 1, { 0, 255, 0, 255 } },
        { 3, 2, { 0, 0, 255, 255 } }
    }, 3);
    spans_expect(world, e, 0, 255, 0, 0, 255);
    spans_expect(world, e, 1, 255, 0, 0, 255);
    spans_expect(world, e, 2, 255, 0, 0, 255);
    spans_expect(world, e, 3, 255, 0, 0, 255);

    ecs_fini(world);
}

void TextSpans_multi_byte(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, SPANS_MULTI_BYTE, 0);
    int32_t count = 0;
    const box_text_glyph_t *glyphs = spans_glyphs(world, e, &count);
    test_int(count, 4);
    test_int(glyphs[0].offset, 0);
    test_int(glyphs[1].offset, 1);
    test_int(glyphs[2].offset, 3);
    test_int(glyphs[3].offset, 6);

    spans_set(world, e, (TextSpan[]){
        { 1, 3, { 255, 0, 0, 255 } },
        { 3, 6, { 0, 255, 0, 255 } }
    }, 2);
    test_assert(spans_color(world, e, 0) == NULL);
    spans_expect(world, e, 1, 255, 0, 0, 255);
    spans_expect(world, e, 2, 0, 255, 0, 255);
    test_assert(spans_color(world, e, 3) == NULL);

    ecs_fini(world);
}

void TextSpans_span_starts_inside_character(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, SPANS_MULTI_BYTE, 0);
    spans_set(world, e, (TextSpan[]){{ 2, 4, { 0, 0, 255, 255 } }}, 1);
    test_assert(spans_color(world, e, 0) == NULL);
    test_assert(spans_color(world, e, 1) == NULL);
    spans_expect(world, e, 2, 0, 0, 255, 255);
    test_assert(spans_color(world, e, 3) == NULL);

    spans_set(world, e, (TextSpan[]){{ 4, 6, { 0, 0, 255, 255 } }}, 1);
    test_assert(spans_color(world, e, 2) == NULL);

    ecs_fini(world);
}

void TextSpans_wrapped_offsets(void) {
    ecs_world_t *world = spans_world();
    const char *str = SPANS_PARAGRAPH;
    ecs_entity_t e = spans_text(world, str, 60);
    test_assert(box_text_line(world, e, 0, NULL, NULL) > 1);

    int32_t i, count = 0;
    const box_text_glyph_t *glyphs = spans_glyphs(world, e, &count);
    int32_t letters = 0;
    for (i = 0; str[i]; i ++) {
        letters += str[i] != ' ';
    }

    test_int(count, letters);
    for (i = 0; i < count; i ++) {
        test_assert(str[glyphs[i].offset] != ' ');
        test_assert(i == 0 || glyphs[i].offset > glyphs[i - 1].offset);
    }

    spans_set(world, e, (TextSpan[]){{ 19, 24, { 0, 255, 0, 255 } }}, 1);
    for (i = 0; i < count; i ++) {
        bool in = glyphs[i].offset >= 19 && glyphs[i].offset < 24;
        test_bool(spans_color(world, e, i) != NULL, in);
    }

    ecs_fini(world);
}

void TextSpans_span_change_no_relayout(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, "two words", 0);
    int32_t layouts = 0;
    spans_watch_layouts(world, e, &layouts);
    const TextGlyphs *tg = ecs_get(world, e, TextGlyphs);
    uint64_t key = tg->key;
    const void *first = ecs_vec_first(&tg->glyphs);
    TextBounds bounds = *ecs_get(world, e, TextBounds);

    spans_set(world, e, (TextSpan[]){{ 0, 3, { 255, 0, 0, 255 } }}, 1);
    ecs_progress(world, 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 3, { 0, 255, 0, 255 } },
        { 4, 9, { 0, 0, 255, 128 } }
    }, 2);
    ecs_progress(world, 0);

    test_int(layouts, 0);
    tg = ecs_get(world, e, TextGlyphs);
    test_assert(tg->key == key);
    test_assert(ecs_vec_first(&tg->glyphs) == first);
    test_flt(ecs_get(world, e, TextBounds)->width, bounds.width);
    test_flt(ecs_get(world, e, TextBounds)->height, bounds.height);
    spans_expect(world, e, 0, 0, 255, 0, 255);
    spans_expect(world, e, 3, 0, 0, 255, 128);

    ecs_fini(world);
}

void TextSpans_remove_spans_no_relayout(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, "two words", 0);
    spans_set(world, e, (TextSpan[]){{ 0, 9, { 255, 0, 0, 255 } }}, 1);
    ecs_progress(world, 0);
    int32_t layouts = 0;
    spans_watch_layouts(world, e, &layouts);

    ecs_remove(world, e, TextSpans);
    ecs_progress(world, 0);

    test_int(layouts, 0);
    test_assert(spans_color(world, e, 0) == NULL);

    ecs_fini(world);
}

void TextSpans_script_sets_spans(void) {
    ecs_world_t *world = spans_world();
    test_int(ecs_script_run(world, "spans",
        "using box.text\n"
        "label {\n"
        "  Text: {value: \"let x\", size: 16}\n"
        "  TextSpans: {spans: [{start: 0, end: 3, color: {200, 100, 50, 255}}, "
        "{start: 4, end: 5, color: {1, 2, 3, 4}}]}\n"
        "}\n", NULL), 0);
    ecs_progress(world, 0);

    ecs_entity_t e = ecs_lookup(world, "label");
    test_assert(e != 0);
    const TextSpans *ts = ecs_get(world, e, TextSpans);
    test_assert(ts != NULL);
    test_int(ecs_vec_count(&ts->spans), 2);
    spans_expect(world, e, 0, 200, 100, 50, 255);
    spans_expect(world, e, 2, 200, 100, 50, 255);
    spans_expect(world, e, 3, 1, 2, 3, 4);

    char *json = ecs_ptr_to_json(world, ecs_id(TextSpans), ts);
    test_assert(json != NULL);
    test_assert(strstr(json, "\"start\":4") != NULL);
    ecs_os_free(json);

    ecs_fini(world);
}

void TextSpans_copy_owns_spans(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t e = spans_text(world, "abcd", 0);
    spans_set(world, e, (TextSpan[]){{ 0, 2, { 255, 0, 0, 255 } }}, 1);
    ecs_entity_t copy = ecs_clone(world, 0, e, true);
    ecs_progress(world, 0);

    spans_set(world, e, (TextSpan[]){{ 2, 4, { 0, 255, 0, 255 } }}, 1);
    const TextSpans *ts = ecs_get(world, copy, TextSpans);
    test_assert(ts != NULL);
    test_int(ecs_vec_count(&ts->spans), 1);
    spans_expect(world, copy, 0, 255, 0, 0, 255);
    test_assert(spans_color(world, copy, 2) == NULL);

    ecs_delete(world, e);
    ecs_progress(world, 0);
    spans_expect(world, copy, 1, 255, 0, 0, 255);

    ecs_fini(world);
}

/** Font file of the tests of styled spans, in the test assets */
#define SPANS_FONT "test/assets/fonts/B612-Regular.ttf"

/** Em size of the styled texts of the tests */
#define SPANS_SIZE (16.0f)

/** Logical pixels a bold glyph of SPANS_SIZE advances further, BOX_TEXT_BOLD_ADVANCE ems */
#define SPANS_BOLD (BOX_TEXT_BOLD_ADVANCE * SPANS_SIZE)

/** Logical pixels two lengths may differ by */
#define SPANS_EPSILON (0.01f)

/** Create a font of the test assets.
 *
 * @param world The world
 * @return The font entity
 */
static ecs_entity_t spans_font(
    ecs_world_t *world)
{
    ecs_entity_t font = ecs_new(world);
    ecs_set(world, font, Font, { .path = SPANS_FONT, .atlas_pages = 4 });

    return font;
}

/** Create a Text with a font of the test assets, laid out by one frame.
 *
 * @param world The world
 * @param parent Parent of the Text, 0 for none
 * @param font The font entity
 * @param value The string
 * @param width Width the lines wrap at, 0 for none
 * @return The entity
 */
static ecs_entity_t spans_styled(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t font,
    const char *value,
    float width)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Text, {
        .value = (char*)value, .size = SPANS_SIZE, .width = width, .font = font });
    ecs_progress(world, 0);

    return e;
}

/** Create a row layout of a fixed width, 0 to fit its content.
 *
 * @param world The world
 * @param width Width of the row, 0 to fit
 * @return The row
 */
static ecs_entity_t spans_row(
    ecs_world_t *world,
    float width)
{
    ecs_entity_t row = ecs_new(world);
    ecs_set(world, row, Layout, { .direction = DirectionRow });
    if (width > 0.0f) {
        ecs_set(world, row, LayoutItem, { .width = { SizeModeFixed, width } });
    }

    return row;
}

/** Get the caret of a byte offset of a Text, which must have one.
 *
 * @param world The world
 * @param e The entity
 * @param offset The byte offset
 * @return The caret
 */
static TextCaret spans_caret(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t offset)
{
    TextCaret result = {0};
    test_assert(box_text_caret(world, e, offset, &result));

    return result;
}

/** Check that two lengths are equal within SPANS_EPSILON.
 *
 * @param a The first length
 * @param b The second length
 */
static void spans_near(
    float a,
    float b)
{
    test_assert(fabsf(a - b) < SPANS_EPSILON);
}

/** Check the style of a glyph of a Text.
 *
 * @param world The world
 * @param e The entity
 * @param glyph Index of the glyph
 * @param bold Whether the glyph is bold
 * @param italic Whether the glyph is italic
 */
static void spans_style_expect(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t glyph,
    bool bold,
    bool italic)
{
    int32_t count = 0;
    const box_text_glyph_t *glyphs = spans_glyphs(world, e, &count);
    test_assert(glyph < count);
    test_bool(glyphs[glyph].weight > 0.0f, bold);
    test_bool(glyphs[glyph].shear > 0.0f, italic);
    if (bold) {
        test_flt(glyphs[glyph].weight, BOX_TEXT_BOLD_FIELD);
    }

    if (italic) {
        test_flt(glyphs[glyph].shear, BOX_TEXT_ITALIC_SHEAR);
    }
}

void TextSpans_color_only_no_layout(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "two words", 0);
    int32_t layouts = 0;
    spans_watch_layouts(world, e, &layouts);
    const TextGlyphs *tg = ecs_get(world, e, TextGlyphs);
    uint64_t key = tg->key;
    const void *first = ecs_vec_first(&tg->glyphs);
    TextBounds bounds = *ecs_get(world, e, TextBounds);
    test_assert(tg->style == 0);

    spans_set(world, e, (TextSpan[]){
        { 0, 3, { 255, 0, 0, 255 }, .bold = false, .italic = false }}, 1);
    ecs_progress(world, 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 3, { 0, 255, 0, 255 }, .bold = false },
        { 4, 9, { 0, 0, 255, 255 }, .keep_color = true }
    }, 2);
    ecs_progress(world, 0);

    test_int(layouts, 0);
    tg = ecs_get(world, e, TextGlyphs);
    test_assert(tg->key == key);
    test_assert(tg->style == 0);
    test_assert(ecs_vec_first(&tg->glyphs) == first);
    test_flt(ecs_get(world, e, TextBounds)->width, bounds.width);
    test_flt(ecs_get(world, e, TextBounds)->height, bounds.height);
    spans_expect(world, e, 0, 0, 255, 0, 255);
    test_assert(spans_color(world, e, 3) == NULL);
    spans_style_expect(world, e, 0, false, false);

    ecs_fini(world);
}

void TextSpans_bold_grows_bounds(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abc def", 0);
    ecs_entity_t row = spans_row(world, 0);
    ecs_entity_t child = spans_styled(world, row, font, "abc def", 0);
    TextBounds regular = *ecs_get(world, e, TextBounds);
    float laid = ecs_get(world, child, LayoutBounds)->width;
    test_assert(regular.width > 0);
    spans_near(ecs_get(world, child, TextBounds)->width, regular.width);

    TextSpan bold = { 0, 3, {0}, .bold = true, .keep_color = true };
    spans_set(world, e, &bold, 1);
    spans_set(world, child, &bold, 1);
    ecs_progress(world, 0);

    const TextBounds *b = ecs_get(world, e, TextBounds);
    spans_near(b->width, regular.width + 3 * SPANS_BOLD);
    test_flt(b->height, regular.height);
    spans_near(ecs_get(world, child, TextBounds)->width, regular.width + 3 * SPANS_BOLD);
    spans_near(ecs_get(world, child, LayoutBounds)->width, laid + 3 * SPANS_BOLD);
    spans_style_expect(world, e, 0, true, false);
    spans_style_expect(world, e, 2, true, false);
    spans_style_expect(world, e, 3, false, false);
    test_assert(spans_color(world, e, 0) == NULL);

    ecs_fini(world);
}

void TextSpans_bold_wraps_earlier(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t ref = spans_styled(world, 0, font, "aaaa bbbb", 0);
    float width = ecs_get(world, ref, TextBounds)->width + 1.0f;

    ecs_entity_t e = spans_styled(world, 0, font, "aaaa bbbb", width);
    ecs_entity_t row = spans_row(world, width);
    ecs_entity_t child = spans_styled(world, row, font, "aaaa bbbb", 0);
    ecs_set(world, child, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_progress(world, 0);
    test_int(box_text_line(world, e, 0, NULL, NULL), 1);
    test_int(box_text_line(world, child, 0, NULL, NULL), 1);
    float line = ecs_get(world, e, TextBounds)->height;

    TextSpan bold = { 0, 9, {0}, .bold = true, .keep_color = true };
    spans_set(world, e, &bold, 1);
    spans_set(world, child, &bold, 1);
    ecs_progress(world, 0);

    int32_t start = -1, end = -1;
    test_int(box_text_line(world, e, 1, &start, &end), 2);
    test_int(start, 5);
    test_int(end, 9);
    test_flt(ecs_get(world, e, TextBounds)->height, 2 * line);
    test_int(box_text_line(world, child, 1, &start, &end), 2);
    test_int(start, 5);
    test_flt(ecs_get(world, child, TextBounds)->height, 2 * line);
    spans_near(ecs_get(world, child, LayoutBounds)->height, 2 * line);

    ecs_fini(world);
}

void TextSpans_bold_moves_carets(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abcd", 0);
    ecs_entity_t row = spans_row(world, 0);
    ecs_entity_t child = spans_styled(world, row, font, "abcd", 0);
    ecs_entity_t texts[2] = { e, child };
    float regular[2][5];
    int32_t t, i;
    for (t = 0; t < 2; t ++) {
        for (i = 0; i <= 4; i ++) {
            regular[t][i] = spans_caret(world, texts[t], i).x;
        }
    }

    TextSpan bold = { 1, 3, {0}, .bold = true, .keep_color = true };
    spans_set(world, e, &bold, 1);
    spans_set(world, child, &bold, 1);
    ecs_progress(world, 0);

    for (t = 0; t < 2; t ++) {
        float x[5];
        for (i = 0; i <= 4; i ++) {
            x[i] = spans_caret(world, texts[t], i).x;
        }

        spans_near(x[1] - x[0], regular[t][1] - regular[t][0]);
        spans_near(x[2] - x[1], regular[t][2] - regular[t][1] + SPANS_BOLD);
        spans_near(x[3] - x[2], regular[t][3] - regular[t][2] + SPANS_BOLD);
        spans_near(x[4] - x[3], regular[t][4] - regular[t][3]);
        spans_near(x[4] - x[0], regular[t][4] - regular[t][0] + 2 * SPANS_BOLD);
    }

    ecs_fini(world);
}

void TextSpans_bold_hit_test(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abcdef", 0);
    ecs_entity_t row = spans_row(world, 0);
    ecs_entity_t child = spans_styled(world, row, font, "abcdef", 0);
    TextSpan bold = { 0, 3, {0}, .bold = true, .keep_color = true };
    spans_set(world, e, &bold, 1);
    spans_set(world, child, &bold, 1);
    ecs_progress(world, 0);

    ecs_entity_t texts[2] = { e, child };
    int32_t t, i;
    for (t = 0; t < 2; t ++) {
        for (i = 0; i < 6; i ++) {
            TextCaret a = spans_caret(world, texts[t], i);
            TextCaret b = spans_caret(world, texts[t], i + 1);
            float y = a.y + a.height * 0.5f;
            test_int(box_text_hit(world, texts[t], a.x + (b.x - a.x) * 0.25f, y), i);
            test_int(box_text_hit(world, texts[t], a.x + (b.x - a.x) * 0.75f, y), i + 1);
        }
    }

    ecs_fini(world);
}

void TextSpans_italic_same_bounds(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "italic text", 0);
    TextBounds regular = *ecs_get(world, e, TextBounds);
    float end = spans_caret(world, e, 11).x;
    int32_t count = 0;
    spans_glyphs(world, e, &count);

    spans_set(world, e, (TextSpan[]){
        { 0, 11, {0}, .italic = true, .keep_color = true }}, 1);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, e, TextBounds)->width, regular.width);
    test_flt(ecs_get(world, e, TextBounds)->height, regular.height);
    test_flt(spans_caret(world, e, 11).x, end);
    int32_t i, now = 0;
    const box_text_glyph_t *glyphs = spans_glyphs(world, e, &now);
    test_int(now, count);
    for (i = 0; i < now; i ++) {
        test_flt(glyphs[i].shear, BOX_TEXT_ITALIC_SHEAR);
        test_flt(glyphs[i].weight, 0.0f);
    }

    ecs_fini(world);
}

void TextSpans_style_change_lays_out(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abcdef", 0);
    float regular = ecs_get(world, e, TextBounds)->width;
    int32_t layouts = 0;
    spans_watch_layouts(world, e, &layouts);

    spans_set(world, e, (TextSpan[]){
        { 0, 2, { 255, 0, 0, 255 }, .bold = true }}, 1);
    ecs_progress(world, 0);
    test_assert(layouts > 0);
    test_assert(ecs_get(world, e, TextGlyphs)->style != 0);
    spans_near(ecs_get(world, e, TextBounds)->width, regular + 2 * SPANS_BOLD);
    spans_expect(world, e, 0, 255, 0, 0, 255);

    layouts = 0;
    spans_set(world, e, (TextSpan[]){
        { 0, 5, { 255, 0, 0, 255 }, .bold = true }}, 1);
    ecs_progress(world, 0);
    test_assert(layouts > 0);
    spans_near(ecs_get(world, e, TextBounds)->width, regular + 5 * SPANS_BOLD);

    layouts = 0;
    spans_set(world, e, (TextSpan[]){
        { 0, 5, { 0, 0, 255, 255 }, .bold = true }}, 1);
    ecs_progress(world, 0);
    test_int(layouts, 0);
    spans_expect(world, e, 0, 0, 0, 255, 255);

    spans_set(world, e, (TextSpan[]){
        { 0, 5, { 0, 0, 255, 255 }, .bold = false }}, 1);
    ecs_progress(world, 0);
    test_assert(layouts > 0);
    test_assert(ecs_get(world, e, TextGlyphs)->style == 0);
    spans_near(ecs_get(world, e, TextBounds)->width, regular);

    ecs_fini(world);
}

void TextSpans_style_removed_lays_out(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abcdef", 0);
    float regular = ecs_get(world, e, TextBounds)->width;
    float end = spans_caret(world, e, 6).x;
    spans_set(world, e, (TextSpan[]){{ 0, 6, {0}, .bold = true, .keep_color = true }}, 1);
    ecs_progress(world, 0);
    spans_near(ecs_get(world, e, TextBounds)->width, regular + 6 * SPANS_BOLD);
    int32_t layouts = 0;
    spans_watch_layouts(world, e, &layouts);

    ecs_remove(world, e, TextSpans);
    ecs_progress(world, 0);

    test_assert(layouts > 0);
    test_assert(ecs_get(world, e, TextGlyphs)->style == 0);
    spans_near(ecs_get(world, e, TextBounds)->width, regular);
    spans_near(spans_caret(world, e, 6).x, end);
    spans_style_expect(world, e, 0, false, false);

    ecs_fini(world);
}

void TextSpans_keep_color(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abcd", 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 4, { 255, 0, 0, 255 } },
        { 0, 2, { 0, 255, 0, 255 }, .bold = true, .keep_color = true }
    }, 2);
    ecs_progress(world, 0);
    spans_expect(world, e, 0, 255, 0, 0, 255);
    spans_expect(world, e, 1, 255, 0, 0, 255);
    spans_expect(world, e, 2, 255, 0, 0, 255);
    spans_style_expect(world, e, 0, true, false);
    spans_style_expect(world, e, 2, false, false);

    spans_set(world, e, (TextSpan[]){
        { 0, 4, { 255, 0, 0, 255 } },
        { 0, 2, { 0, 0, 255, 255 }, .bold = true }
    }, 2);
    ecs_progress(world, 0);
    spans_expect(world, e, 0, 0, 0, 255, 255);
    spans_expect(world, e, 2, 255, 0, 0, 255);
    spans_style_expect(world, e, 0, true, false);

    spans_set(world, e, (TextSpan[]){{ 0, 4, { 1, 2, 3, 4 }, .keep_color = true }}, 1);
    ecs_progress(world, 0);
    test_assert(spans_color(world, e, 0) == NULL);

    ecs_fini(world);
}

void TextSpans_overlap_styles_add(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abcdef", 0);
    spans_set(world, e, (TextSpan[]){
        { 0, 4, {0}, .bold = true, .keep_color = true },
        { 2, 6, {0}, .italic = true, .keep_color = true }
    }, 2);
    ecs_progress(world, 0);
    spans_style_expect(world, e, 0, true, false);
    spans_style_expect(world, e, 1, true, false);
    spans_style_expect(world, e, 2, true, true);
    spans_style_expect(world, e, 3, true, true);
    spans_style_expect(world, e, 4, false, true);
    spans_style_expect(world, e, 5, false, true);

    spans_set(world, e, (TextSpan[]){
        { 1, 3, {0}, .bold = true, .keep_color = true },
        { 1, 3, {0}, .italic = true, .keep_color = true },
        { 0, 6, {0}, .keep_color = true }
    }, 3);
    ecs_progress(world, 0);
    spans_style_expect(world, e, 0, false, false);
    spans_style_expect(world, e, 1, true, true);
    spans_style_expect(world, e, 2, true, true);
    spans_style_expect(world, e, 3, false, false);

    ecs_fini(world);
}

void TextSpans_spans_clip_to_string(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abcdef", 0);
    ecs_entity_t clipped = spans_styled(world, 0, font, "abcdef", 0);
    spans_set(world, e, (TextSpan[]){
        { -5, 2, {0}, .bold = true, .keep_color = true },
        { 4, 100, {0}, .italic = true, .keep_color = true },
        { 6, 10, {0}, .bold = true, .keep_color = true }
    }, 3);
    spans_set(world, clipped, (TextSpan[]){
        { 0, 2, {0}, .bold = true, .keep_color = true },
        { 4, 6, {0}, .italic = true, .keep_color = true }
    }, 2);
    ecs_progress(world, 0);

    spans_style_expect(world, e, 0, true, false);
    spans_style_expect(world, e, 1, true, false);
    spans_style_expect(world, e, 2, false, false);
    spans_style_expect(world, e, 3, false, false);
    spans_style_expect(world, e, 4, false, true);
    spans_style_expect(world, e, 5, false, true);
    test_assert(ecs_get(world, e, TextGlyphs)->style ==
        ecs_get(world, clipped, TextGlyphs)->style);
    test_flt(ecs_get(world, e, TextBounds)->width,
        ecs_get(world, clipped, TextBounds)->width);
    test_assert(box_text_style_key(ecs_get(world, e, TextSpans), 6) ==
        box_text_style_key(ecs_get(world, clipped, TextSpans), 6));

    ecs_fini(world);
}

void TextSpans_span_mid_utf8(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, SPANS_MULTI_BYTE, 0);
    float regular = ecs_get(world, e, TextBounds)->width;
    spans_set(world, e, (TextSpan[]){{ 2, 4, {0}, .bold = true, .keep_color = true }}, 1);
    ecs_progress(world, 0);
    spans_style_expect(world, e, 0, false, false);
    spans_style_expect(world, e, 1, false, false);
    spans_style_expect(world, e, 2, true, false);
    spans_style_expect(world, e, 3, false, false);
    spans_near(ecs_get(world, e, TextBounds)->width, regular + SPANS_BOLD);

    spans_set(world, e, (TextSpan[]){{ 4, 6, {0}, .bold = true, .keep_color = true }}, 1);
    ecs_progress(world, 0);
    spans_style_expect(world, e, 2, false, false);
    spans_style_expect(world, e, 3, false, false);
    spans_near(ecs_get(world, e, TextBounds)->width, regular);

    ecs_fini(world);
}

void TextSpans_empty_span(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    ecs_entity_t e = spans_styled(world, 0, font, "abcd", 0);
    TextBounds bounds = *ecs_get(world, e, TextBounds);
    int32_t layouts = 0;
    spans_watch_layouts(world, e, &layouts);

    spans_set(world, e, (TextSpan[]){
        { 1, 1, {0}, .bold = true, .keep_color = true },
        { 3, 2, {0}, .bold = true, .italic = true, .keep_color = true },
        { 4, 8, {0}, .italic = true, .keep_color = true }
    }, 3);
    ecs_progress(world, 0);

    test_int(layouts, 0);
    test_assert(ecs_get(world, e, TextGlyphs)->style == 0);
    test_assert(box_text_style_key(ecs_get(world, e, TextSpans), 4) == 0);
    test_flt(ecs_get(world, e, TextBounds)->width, bounds.width);
    spans_style_expect(world, e, 0, false, false);
    spans_style_expect(world, e, 1, false, false);
    spans_style_expect(world, e, 2, false, false);
    spans_style_expect(world, e, 3, false, false);

    ecs_fini(world);
}

void TextSpans_break_lines_spans(void) {
    ecs_world_t *world = spans_world();
    ecs_entity_t font = spans_font(world);
    const char *str = SPANS_PARAGRAPH;
    ecs_entity_t ref = spans_styled(world, 0, font, str, 0);
    float width = ecs_get(world, ref, TextBounds)->width * 0.5f;
    ecs_entity_t e = spans_styled(world, 0, font, str, width);
    spans_set(world, e, (TextSpan[]){
        { 0, (int32_t)strlen(str), {0}, .bold = true, .keep_color = true }}, 1);
    ecs_progress(world, 0);

    ecs_vec_t starts;
    ecs_vec_init_t(NULL, &starts, int32_t, 0);
    const Text *text = ecs_get(world, e, Text);
    const TextSpans *spans = ecs_get(world, e, TextSpans);
    int32_t lines = box_text_line(world, e, 0, NULL, NULL);
    int32_t plain = box_text_break_lines(world, text, NULL);
    test_assert(lines > 1);
    test_int(box_text_break_lines_spans(world, text, spans, &starts), lines);
    test_int(ecs_vec_count(&starts), lines);
    test_assert(plain <= lines);
    test_int(box_text_break_lines_spans(world, text, NULL, NULL), plain);

    int32_t i;
    for (i = 0; i < lines; i ++) {
        int32_t start = -1;
        box_text_line(world, e, i, &start, NULL);
        test_int(*ecs_vec_get_t(&starts, int32_t, i), start);
    }

    ecs_vec_fini_t(NULL, &starts, int32_t);
    ecs_fini(world);
}

void TextSpans_from_script(void) {
    ecs_world_t *world = spans_world();
    test_int(ecs_script_run(world, "styles",
        "using box.text\n"
        "assets_font {\n"
        "  Font: {path: \"" SPANS_FONT "\"}\n"
        "}\n"
        "plain {\n"
        "  Text: {value: \"bold text\", size: 16, font: assets_font}\n"
        "}\n"
        "label {\n"
        "  Text: {value: \"bold text\", size: 16, font: assets_font}\n"
        "  TextSpans: {spans: [{start: 0, end: 4, bold: true, keep_color: true}]}\n"
        "}\n", NULL), 0);
    ecs_progress(world, 0);

    ecs_entity_t e = ecs_lookup(world, "label");
    ecs_entity_t plain = ecs_lookup(world, "plain");
    test_assert(e != 0);
    test_assert(plain != 0);
    const TextSpans *ts = ecs_get(world, e, TextSpans);
    test_assert(ts != NULL);
    test_int(ecs_vec_count(&ts->spans), 1);
    const TextSpan *span = ecs_vec_first(&ts->spans);
    test_bool(span->bold, true);
    test_bool(span->italic, false);
    test_bool(span->keep_color, true);

    spans_style_expect(world, e, 0, true, false);
    spans_style_expect(world, e, 3, true, false);
    spans_style_expect(world, e, 4, false, false);
    test_assert(spans_color(world, e, 0) == NULL);
    spans_near(ecs_get(world, e, TextBounds)->width,
        ecs_get(world, plain, TextBounds)->width + 4 * SPANS_BOLD);

    char *json = ecs_ptr_to_json(world, ecs_id(TextSpans), ts);
    test_assert(json != NULL);
    test_assert(strstr(json, "\"bold\":true") != NULL);
    test_assert(strstr(json, "\"keep_color\":true") != NULL);
    ecs_os_free(json);

    ecs_fini(world);
}
