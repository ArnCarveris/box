/**
 * @file TextSpans.c
 * @brief Colored byte ranges of a Text resolve per glyph without a new layout.
 */

#include <box.h>
#include <bake_test.h>
#include <text.h>
#include <string.h>

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
    *count += it->count;
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
        .query.terms = {{ .id = ecs_id(TextGlyphs), .src.id = e }},
        .events = { EcsOnSet },
        .callback = spans_count_layouts,
        .ctx = count
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
