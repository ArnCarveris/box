/**
 * @file TextCaret.c
 * @brief Caret positions, hit testing and line ranges of a Text match its layout.
 */

#include <box.h>
#include <bake_test.h>
#include <text.h>
#include <math.h>
#include <string.h>

/** A paragraph that wraps into several lines at the widths of the tests */
#define CARET_PARAGRAPH "Box lays out text in rows and columns and wraps long paragraphs " \
    "to the width of the panel that holds them"

/** Logical pixels two computed positions of the same layout may differ by */
#define CARET_EPSILON (0.001f)

/** Create a headless world with box imported.
 *
 * @return The world
 */
static ecs_world_t* caret_world(void)
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
 * @param align Alignment of the lines
 * @return The entity
 */
static ecs_entity_t caret_text(
    ecs_world_t *world,
    const char *value,
    float width,
    Align align)
{
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {
        .value = (char*)value, .size = 16, .width = width, .align = align, .font = font });
    ecs_progress(world, 0);

    return e;
}

/** Get the caret at an offset, which must exist.
 *
 * @param world The world
 * @param e The entity
 * @param offset The byte offset
 * @return The caret
 */
static TextCaret caret_at(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t offset)
{
    TextCaret result = {0};
    test_assert(box_text_caret(world, e, offset, &result));

    return result;
}

/** Hit the middle of the line of the caret at an offset.
 *
 * @param world The world
 * @param e The entity
 * @param offset The byte offset
 * @return The offset the hit returns
 */
static int32_t caret_round_trip(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t offset)
{
    TextCaret c = caret_at(world, e, offset);

    return box_text_hit(world, e, c.x, c.y + c.height * 0.5f);
}

/** Check that every character start of a string survives hit(caret(offset)).
 *
 * @param world The world
 * @param e The entity
 * @param str The string of the Text
 */
static void caret_check_round_trip(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *str)
{
    const char *p = str;
    while (*p) {
        test_int(caret_round_trip(world, e, (int32_t)(p - str)), (int32_t)(p - str));
        box_utf8_next(&p);
    }

    test_int(caret_round_trip(world, e, (int32_t)(p - str)), (int32_t)(p - str));
}

/** What a system that edits a Text sees in the same frame */
typedef struct caret_edit_t {
    ecs_entity_t e;     /**< The Text it edits */
    ecs_entity_t font;  /**< The font of the Text */
    bool found;         /**< box_text_caret returned true */
    TextCaret first;    /**< Caret at offset 0 of the new value */
    TextCaret last;     /**< Caret at the end of the new value */
    int32_t lines;      /**< Line count of the new value */
    int32_t end;        /**< End of the first line of the new value */
    int32_t hit;        /**< Hit right of the new value */
} caret_edit_t;

/** Set a longer value on a Text from a system, deferred, and read its carets at once.
 *
 * @param it The iterator, its context a caret_edit_t
 */
static void caret_edit(
    ecs_iter_t *it)
{
    caret_edit_t *edit = it->ctx;
    if (!edit->found) {
        ecs_set(it->world, edit->e, Text, {.value = "abcdef", .size = 16, .font = edit->font});
        edit->found = box_text_caret(it->world, edit->e, 0, &edit->first);
        box_text_caret(it->world, edit->e, 6, &edit->last);
        edit->lines = box_text_line(it->world, edit->e, 0, NULL, &edit->end);
        edit->hit = box_text_hit(it->world, edit->e, 1000, 0);
    }
}

void TextCaret_carets_grow_to_width(void) {
    ecs_world_t *world = caret_world();
    const char *str = "Hello, caret world";
    ecs_entity_t e = caret_text(world, str, 0, AlignLeft);
    const TextBounds *b = ecs_get(world, e, TextBounds);
    int32_t i, len = (int32_t)strlen(str);
    test_assert(b->width > 0);
    TextCaret first = caret_at(world, e, 0);
    test_flt(first.x, -b->width * 0.5f);
    test_flt(first.y, -b->height * 0.5f);
    test_flt(first.height, b->height);
    test_int(first.line, 0);
    float prev = first.x;
    for (i = 1; i <= len; i ++) {
        TextCaret c = caret_at(world, e, i);
        test_assert(c.x > prev);
        test_int(c.line, 0);
        test_flt(c.y, first.y);
        test_flt(c.height, first.height);
        prev = c.x;
    }

    test_assert(fabsf(prev - first.x - b->width) < CARET_EPSILON);
    ecs_fini(world);
}

void TextCaret_alignment_shifts_carets(void) {
    ecs_world_t *world = caret_world();
    const char *str = "short\nmuch longer line";
    ecs_entity_t l = caret_text(world, str, 0, AlignLeft);
    ecs_entity_t c = caret_text(world, str, 0, AlignCenter);
    ecs_entity_t r = caret_text(world, str, 0, AlignRight);
    float w = ecs_get(world, l, TextBounds)->width;
    test_flt(ecs_get(world, c, TextBounds)->width, w);
    test_flt(ecs_get(world, r, TextBounds)->width, w);
    float short_w = caret_at(world, l, 5).x - caret_at(world, l, 0).x;
    test_assert(short_w > 0 && short_w < w * 0.5f);

    test_flt(caret_at(world, l, 0).x, -w * 0.5f);
    test_assert(fabsf(caret_at(world, c, 0).x - (-short_w * 0.5f)) < CARET_EPSILON);
    test_assert(fabsf(caret_at(world, c, 5).x - short_w * 0.5f) < CARET_EPSILON);
    test_assert(fabsf(caret_at(world, r, 0).x - (w * 0.5f - short_w)) < CARET_EPSILON);
    test_assert(fabsf(caret_at(world, r, 5).x - w * 0.5f) < CARET_EPSILON);

    test_flt(caret_at(world, c, 6).x, -w * 0.5f);
    test_flt(caret_at(world, r, 6).x, -w * 0.5f);
    test_assert(fabsf(caret_at(world, r, (int32_t)strlen(str)).x - w * 0.5f) < CARET_EPSILON);
    ecs_fini(world);
}

void TextCaret_kerning(void) {
    ecs_world_t *world = caret_world();
    const char *pairs[] = { "AV", "VA", "AT", "TA", "To", "Yo", "LT", "Te", "AY", "P." };
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t probe = caret_text(world, "x", 0, AlignLeft);
    test_assert(caret_at(world, probe, 0).height > 0);
    box_text_state_t *s = box_text_state_get(world);
    box_text_font_t *f = box_text_font_of(world, s, font);
    test_assert(f->face != NULL);
    float ascent, descent, gap;
    float px = 16.0f / box_text_face_metrics(f->face, &ascent, &descent, &gap);
    int32_t i, kerned = 0;
    for (i = 0; i < (int32_t)(sizeof(pairs) / sizeof(pairs[0])); i ++) {
        int32_t left = box_text_face_glyph(f->face, pairs[i][0]);
        int32_t right = box_text_face_glyph(f->face, pairs[i][1]);
        float kern = box_text_face_kern(f->face, left, right) * px;
        float advance = box_text_face_advance(f->face, left) * px;
        ecs_entity_t e = caret_text(world, pairs[i], 0, AlignLeft);
        float between = caret_at(world, e, 1).x - caret_at(world, e, 0).x;
        test_assert(fabsf(between - (advance + kern)) < CARET_EPSILON);
        if (kern != 0.0f) {
            ecs_entity_t alone = caret_text(world, (char[]){ pairs[i][0], 0 }, 0, AlignLeft);
            float width = ecs_get(world, alone, TextBounds)->width;
            test_assert(fabsf(between - width) > CARET_EPSILON);
            kerned ++;
        }
    }

    test_assert(kerned > 0);
    ecs_fini(world);
}

void TextCaret_wrapped_lines(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, CARET_PARAGRAPH, 150, AlignLeft);
    const TextBounds *b = ecs_get(world, e, TextBounds);
    int32_t i, len = (int32_t)strlen(CARET_PARAGRAPH);
    int32_t lines = box_text_line(world, e, 0, NULL, NULL);
    test_assert(lines > 2);
    TextCaret first = caret_at(world, e, 0);
    test_flt(b->height, first.height * (float)lines);
    test_int(first.line, 0);
    test_assert(fabsf(first.y - (b->height * 0.5f - first.height)) < CARET_EPSILON);
    int32_t line = 0;
    for (i = 1; i <= len; i ++) {
        TextCaret c = caret_at(world, e, i);
        test_assert(c.line == line || c.line == line + 1);
        line = c.line;
        test_assert(fabsf(c.y - (b->height * 0.5f - (float)(line + 1) * c.height)) < CARET_EPSILON);
        test_assert(c.x >= -b->width * 0.5f);
    }

    test_int(line, lines - 1);
    ecs_fini(world);
}

void TextCaret_wrap_point_on_next_line(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, CARET_PARAGRAPH, 150, AlignLeft);
    float w = ecs_get(world, e, TextBounds)->width;
    int32_t line, start, end;
    int32_t lines = box_text_line(world, e, 0, &start, &end);
    test_assert(lines > 2);
    for (line = 1; line < lines; line ++) {
        box_text_line(world, e, line, &start, &end);
        test_assert(start > 0);
        test_assert(CARET_PARAGRAPH[start - 1] == ' ');
        TextCaret at = caret_at(world, e, start);
        TextCaret before = caret_at(world, e, start - 1);
        test_int(at.line, line);
        test_flt(at.x, -w * 0.5f);
        test_int(before.line, line - 1);
        test_assert(before.x > at.x);
    }

    ecs_fini(world);
}

void TextCaret_line_ranges(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, "one two\nthree", 0, AlignLeft);
    int32_t start, end;
    test_int(box_text_line(world, e, 0, &start, &end), 2);
    test_int(start, 0);
    test_int(end, 7);
    test_int(box_text_line(world, e, 1, &start, &end), 2);
    test_int(start, 8);
    test_int(end, 13);
    test_int(box_text_line(world, e, -3, &start, &end), 2);
    test_int(start, 0);
    test_int(end, 7);
    test_int(box_text_line(world, e, 9, &start, &end), 2);
    test_int(start, 8);
    test_int(end, 13);

    ecs_entity_t p = caret_text(world, CARET_PARAGRAPH, 150, AlignLeft);
    int32_t line, prev_end = 0;
    int32_t lines = box_text_line(world, p, 0, NULL, NULL);
    for (line = 0; line < lines; line ++) {
        box_text_line(world, p, line, &start, &end);
        test_int(start, prev_end);
        test_assert(end > start);
        test_int(caret_at(world, p, start).line, line);
        prev_end = end;
    }

    test_int(prev_end, (int32_t)strlen(CARET_PARAGRAPH));
    ecs_fini(world);
}

void TextCaret_layout_width_wraps(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, {
        .direction = DirectionColumn, .padding = { 10, 10, 10, 10 } });
    ecs_set(world, panel, LayoutItem, { .width = { SizeModeFixed, 200 } });
    ecs_entity_t para = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, para, Text, {.value = CARET_PARAGRAPH, .size = 16, .font = font});
    ecs_set(world, para, LayoutItem, { .width = { SizeModeGrow, 1, .content_min = true } });
    ecs_entity_t ref = caret_text(world, CARET_PARAGRAPH, 180, AlignLeft);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, para, TextBounds)->width, 180);
    int32_t line, start, end, ref_start, ref_end;
    int32_t lines = box_text_line(world, para, 0, NULL, NULL);
    test_assert(lines > 2);
    test_int(box_text_line(world, ref, 0, NULL, NULL), lines);
    for (line = 0; line < lines; line ++) {
        box_text_line(world, para, line, &start, &end);
        box_text_line(world, ref, line, &ref_start, &ref_end);
        test_int(start, ref_start);
        test_int(end, ref_end);
        TextCaret c = caret_at(world, para, start);
        test_int(c.line, line);
        test_flt(c.x, -90);
        while (end > start && CARET_PARAGRAPH[end - 1] == ' ') {
            end --;
        }

        TextCaret last = caret_at(world, para, end);
        test_assert(last.x <= 90 + CARET_EPSILON);
    }

    ecs_fini(world);
}

void TextCaret_newlines(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, "ab\ncd\n\nef", 0, AlignLeft);
    float w = ecs_get(world, e, TextBounds)->width;
    int32_t start, end;
    test_int(box_text_line(world, e, 0, &start, &end), 4);
    test_int(start, 0);
    test_int(end, 2);
    box_text_line(world, e, 1, &start, &end);
    test_int(start, 3);
    test_int(end, 5);
    box_text_line(world, e, 2, &start, &end);
    test_int(start, 6);
    test_int(end, 6);
    box_text_line(world, e, 3, &start, &end);
    test_int(start, 7);
    test_int(end, 9);

    TextCaret top = caret_at(world, e, 0);
    TextCaret nl = caret_at(world, e, 2);
    test_int(nl.line, 0);
    test_assert(nl.x > top.x);
    TextCaret c = caret_at(world, e, 3);
    test_int(c.line, 1);
    test_flt(c.x, -w * 0.5f);
    test_assert(fabsf(c.y - (top.y - top.height)) < CARET_EPSILON);
    TextCaret empty = caret_at(world, e, 6);
    test_int(empty.line, 2);
    test_flt(empty.x, -w * 0.5f);
    test_assert(fabsf(empty.y - (top.y - 2 * top.height)) < CARET_EPSILON);
    TextCaret last = caret_at(world, e, 9);
    test_int(last.line, 3);
    test_assert(fabsf(last.y + ecs_get(world, e, TextBounds)->height * 0.5f) < CARET_EPSILON);
    ecs_fini(world);
}

void TextCaret_trailing_newline(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, "ab\n", 0, AlignLeft);
    const TextBounds *b = ecs_get(world, e, TextBounds);
    TextCaret top = caret_at(world, e, 0);
    test_flt(b->height, 2 * top.height);
    TextCaret c = caret_at(world, e, 3);
    test_int(c.line, 1);
    test_flt(c.x, -b->width * 0.5f);
    test_flt(c.y, -b->height * 0.5f);
    test_int(caret_at(world, e, 2).line, 0);
    int32_t start, end;
    test_int(box_text_line(world, e, 1, &start, &end), 2);
    test_int(start, 3);
    test_int(end, 3);
    test_int(box_text_hit(world, e, 100, -100), 3);
    ecs_fini(world);
}

void TextCaret_crlf_line_range(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, "ab\r\ncd", 0, AlignLeft);
    int32_t start, end;
    test_int(box_text_line(world, e, 0, &start, &end), 2);
    test_int(start, 0);
    test_int(end, 2);
    box_text_line(world, e, 1, &start, &end);
    test_int(start, 4);
    test_int(end, 6);
    test_int(caret_at(world, e, 4).line, 1);
    ecs_fini(world);
}

void TextCaret_empty_string(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {.value = "", .size = 16, .line_height = 20, .font = font});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, e, TextBounds)->height, 0);
    TextCaret c = caret_at(world, e, 0);
    test_flt(c.x, 0);
    test_flt(c.y, -10);
    test_flt(c.height, 20);
    test_int(c.line, 0);
    test_flt(caret_at(world, e, 5).x, 0);
    int32_t start = -1, end = -1;
    test_int(box_text_line(world, e, 0, &start, &end), 1);
    test_int(start, 0);
    test_int(end, 0);
    test_int(box_text_hit(world, e, 50, 50), 0);
    test_int(box_text_hit(world, e, -50, -50), 0);
    ecs_fini(world);
}

void TextCaret_multi_byte(void) {
    ecs_world_t *world = caret_world();
    const char *str = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" "b";
    int32_t starts[] = { 0, 1, 3, 6, 10, 11 };
    ecs_entity_t e = caret_text(world, str, 0, AlignLeft);
    int32_t i;
    float prev = -1e9f;
    for (i = 0; i < 6; i ++) {
        TextCaret c = caret_at(world, e, starts[i]);
        test_assert(c.x > prev);
        prev = c.x;
    }

    for (i = 0; i < 5; i ++) {
        int32_t o;
        float x = caret_at(world, e, starts[i]).x;
        for (o = starts[i] + 1; o < starts[i + 1]; o ++) {
            test_flt(caret_at(world, e, o).x, x);
        }
    }

    caret_check_round_trip(world, e, str);
    ecs_fini(world);
}

void TextCaret_ill_formed_utf8(void) {
    ecs_world_t *world = caret_world();
    const char *str = "a\xE2\x82" "b\xFF\xF0\x9F" "c";
    int32_t starts[] = { 0, 1, 3, 4, 5, 7, 8 };
    ecs_entity_t e = caret_text(world, str, 0, AlignLeft);
    int32_t i;
    float prev = -1e9f;
    for (i = 0; i < 7; i ++) {
        TextCaret c = caret_at(world, e, starts[i]);
        test_assert(c.x > prev);
        prev = c.x;
    }

    test_flt(caret_at(world, e, 2).x, caret_at(world, e, 1).x);
    test_flt(caret_at(world, e, 6).x, caret_at(world, e, 5).x);
    for (i = 0; i < 7; i ++) {
        test_int(caret_round_trip(world, e, starts[i]), starts[i]);
    }

    ecs_fini(world);
}

void TextCaret_offset_inside_character(void) {
    ecs_world_t *world = caret_world();
    const char *str = "\xE2\x82\xAC\xE2\x82\xAC";
    ecs_entity_t e = caret_text(world, str, 0, AlignLeft);
    float x0 = caret_at(world, e, 0).x;
    float x3 = caret_at(world, e, 3).x;
    float x6 = caret_at(world, e, 6).x;
    test_assert(x0 < x3 && x3 < x6);
    test_flt(caret_at(world, e, 1).x, x0);
    test_flt(caret_at(world, e, 2).x, x0);
    test_flt(caret_at(world, e, 4).x, x3);
    test_flt(caret_at(world, e, 5).x, x3);
    float x;
    for (x = x0 - 10; x < x6 + 10; x += 0.5f) {
        int32_t hit = box_text_hit(world, e, x, 0);
        test_assert(hit == 0 || hit == 3 || hit == 6);
    }

    ecs_fini(world);
}

void TextCaret_offset_clamped(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, "abc", 0, AlignLeft);
    float w = ecs_get(world, e, TextBounds)->width;
    test_flt(caret_at(world, e, -5).x, -w * 0.5f);
    test_flt(caret_at(world, e, 100).x, caret_at(world, e, 3).x);
    test_assert(fabsf(caret_at(world, e, 3).x - w * 0.5f) < CARET_EPSILON);
    ecs_fini(world);
}

void TextCaret_hit_outside_block(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, "abc\ndef", 0, AlignLeft);
    const TextBounds *b = ecs_get(world, e, TextBounds);
    float far = b->width + 100, high = b->height + 100;
    test_int(box_text_hit(world, e, -far, b->height * 0.25f), 0);
    test_int(box_text_hit(world, e, far, b->height * 0.25f), 3);
    test_int(box_text_hit(world, e, -far, -b->height * 0.25f), 4);
    test_int(box_text_hit(world, e, far, -b->height * 0.25f), 7);
    test_int(box_text_hit(world, e, -far, high), 0);
    test_int(box_text_hit(world, e, far, high), 3);
    test_int(box_text_hit(world, e, -far, -high), 4);
    test_int(box_text_hit(world, e, far, -high), 7);
    test_int(box_text_hit(world, e, 0, high), box_text_hit(world, e, 0, b->height * 0.25f));
    test_int(box_text_hit(world, e, 0, -high), box_text_hit(world, e, 0, -b->height * 0.25f));
    ecs_fini(world);
}

void TextCaret_hit_halfway(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, "Wim", 0, AlignLeft);
    float x0 = caret_at(world, e, 0).x;
    float x1 = caret_at(world, e, 1).x;
    float x2 = caret_at(world, e, 2).x;
    float half = (x0 + x1) * 0.5f;
    test_int(box_text_hit(world, e, half - 0.01f, 0), 0);
    test_int(box_text_hit(world, e, half, 0), 1);
    test_int(box_text_hit(world, e, half + 0.01f, 0), 1);
    half = (x1 + x2) * 0.5f;
    test_int(box_text_hit(world, e, half - 0.01f, 0), 1);
    test_int(box_text_hit(world, e, half + 0.01f, 0), 2);
    test_int(box_text_hit(world, e, x1, 0), 1);
    test_int(box_text_hit(world, e, x0 + 0.01f, 0), 0);
    ecs_fini(world);
}

void TextCaret_hit_round_trip(void) {
    ecs_world_t *world = caret_world();
    const char *str = "Carets of caf\xC3\xA9 lines, wrapped in a paragraph\n"
        "and after a newline  with two spaces, \xE2\x82\xAC and end";
    ecs_entity_t l = caret_text(world, str, 140, AlignLeft);
    ecs_entity_t c = caret_text(world, str, 140, AlignCenter);
    ecs_entity_t r = caret_text(world, str, 140, AlignRight);
    test_assert(box_text_line(world, l, 0, NULL, NULL) > 3);
    caret_check_round_trip(world, l, str);
    caret_check_round_trip(world, c, str);
    caret_check_round_trip(world, r, str);
    ecs_fini(world);
}

void TextCaret_hit_long_line(void) {
    ecs_world_t *world = caret_world();
    int32_t i, len = 20000;
    char *str = ecs_os_malloc(len + 1);
    for (i = 0; i < len; i ++) {
        str[i] = (char)('a' + (i % 26));
    }

    str[len] = '\0';
    ecs_entity_t e = caret_text(world, str, 0, AlignLeft);
    test_int(box_text_line(world, e, 0, NULL, NULL), 1);
    for (i = 0; i <= len; i += 97) {
        test_int(caret_round_trip(world, e, i), i);
    }

    test_int(caret_round_trip(world, e, len), len);
    ecs_os_free(str);
    ecs_fini(world);
}

void TextCaret_edited_this_frame(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = caret_text(world, "abc", 0, AlignLeft);
    ecs_entity_t ref = caret_text(world, "abcdef", 0, AlignLeft);
    float w = ecs_get(world, ref, TextBounds)->width;
    test_assert(w > ecs_get(world, e, TextBounds)->width);

    caret_edit_t edit = { .e = e, .font = ecs_lookup(world, "box.text.DefaultFont") };
    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = caret_edit,
        .ctx = &edit
    });
    ecs_progress(world, 0);

    test_true(edit.found);
    test_int(edit.lines, 1);
    test_int(edit.end, 6);
    test_assert(fabsf(edit.last.x - w * 0.5f) < CARET_EPSILON);
    test_flt(edit.first.x, -w * 0.5f);
    test_int(edit.hit, 6);
    test_flt(ecs_get(world, e, TextBounds)->width, w);

    int32_t start, end;
    Text *t = ecs_get_mut(world, e, Text);
    ecs_os_free(t->value);
    t->value = ecs_os_strdup("ab\ncd");
    test_int(box_text_line(world, e, 1, &start, &end), 2);
    test_int(start, 3);
    test_int(end, 5);
    test_int(caret_at(world, e, 3).line, 1);
    ecs_fini(world);
}

void TextCaret_without_text(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t e = ecs_new(world);
    TextCaret c = { .x = 7 };
    test_false(box_text_caret(world, e, 0, &c));
    test_flt(c.x, 7);
    test_int(box_text_hit(world, e, 0, 0), 0);
    int32_t start = -1, end = -1;
    test_int(box_text_line(world, e, 0, &start, &end), 0);
    test_int(start, 0);
    test_int(end, 0);

    ecs_entity_t t = caret_text(world, "abc", 0, AlignLeft);
    test_true(box_text_caret(world, t, 0, &c));
    ecs_remove(world, t, Text);
    test_false(box_text_caret(world, t, 0, &c));
    ecs_fini(world);
}

void TextCaret_font_not_loaded(void) {
    ecs_world_t *world = caret_world();
    ecs_entity_t font = ecs_lookup(world, "box.text.DefaultFont");
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {.value = "abc", .size = 16, .font = font});
    TextCaret c;
    test_false(box_text_caret(world, e, 0, &c));
    test_int(box_text_hit(world, e, 0, 0), 0);
    test_int(box_text_line(world, e, 0, NULL, NULL), 0);
    ecs_progress(world, 0);

    test_true(box_text_caret(world, e, 0, &c));
    ecs_set(world, e, Text, {.value = "abc", .size = 16, .font = 0});
    test_false(box_text_caret(world, e, 0, &c));
    test_int(box_text_line(world, e, 0, NULL, NULL), 0);
    ecs_fini(world);
}
