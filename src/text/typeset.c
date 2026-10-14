/**
 * @file text/typeset.c
 * @brief Lays out a Text: kerning, line breaks between words, alignment and its bounds.
 *
 * Glyphs advance along a pen in logical pixels; a word that passes the width moves to the next
 * line, a word wider than the width breaks inside. The block is centered on the origin, y up.
 */

#include "text.h"

/** State of the layout of one string */
typedef struct box_text_run_t {
    box_text_state_t *s;    /**< The registry, its placed and lines vectors are filled */
    float wrap;             /**< Width lines break at, 0 for newlines only */
    float pen;              /**< Pen x of the line */
    float line_end;         /**< Right edge of the last glyph of the line */
    float word_x;           /**< Pen x of the first glyph of the current word */
    float word_base;        /**< Right edge of the line before the current word */
    int32_t line;           /**< Line being filled */
    int32_t line_start;     /**< First placed glyph of the line */
    int32_t word_start;     /**< First placed glyph of the current word */
    int32_t prev;           /**< Glyph index before the pen, -1 at the start of a line */
    bool in_word;           /**< The last character was not a space */
} box_text_run_t;

/** Read the code point of a UTF-8 sequence and move past it.
 *
 * A byte that starts no valid sequence is read as itself.
 *
 * @param str The string, advanced past the sequence
 * @return The code point
 */
static int32_t box_text_utf8(
    const char **str)
{
    const uint8_t *p = (const uint8_t*)*str;
    int32_t result = p[0], extra = 0, i;
    if (result >= 0xF0) {
        result &= 0x07;
        extra = 3;
    } else if (result >= 0xE0) {
        result &= 0x0F;
        extra = 2;
    } else if (result >= 0xC0) {
        result &= 0x1F;
        extra = 1;
    }

    for (i = 1; i <= extra; i ++) {
        if ((p[i] & 0xC0) != 0x80) {
            result = p[0];
            extra = 0;
            break;
        }

        result = (result << 6) | (p[i] & 0x3F);
    }

    *str = (const char*)(p + extra + 1);

    return result;
}

/** End the line being filled at a width and start the next one.
 *
 * @param run The run
 * @param width Width of the line
 * @param start First placed glyph of the next line
 */
static void box_text_break(
    box_text_run_t *run,
    float width,
    int32_t start)
{
    *ecs_vec_append_t(NULL, &run->s->lines, float) = width;
    run->line ++;
    run->line_start = start;
}

/** Make room for a glyph that passes the width: move its word to the next line, then break
 * inside a word that still does not fit.
 *
 * @param run The run
 * @param x Pen x of the glyph, moved with it
 * @param advance Advance of the glyph
 */
static void box_text_wrap(
    box_text_run_t *run,
    float *x,
    float advance)
{
    int32_t i, count = ecs_vec_count(&run->s->placed);
    box_text_placed_t *placed = ecs_vec_first(&run->s->placed);
    if (run->word_start > run->line_start) {
        box_text_break(run, run->word_base, run->word_start);
        for (i = run->word_start; i < count; i ++) {
            placed[i].x -= run->word_x;
            placed[i].line = run->line;
        }

        *x -= run->word_x;
        run->line_end = count > run->word_start ? run->line_end - run->word_x : 0.0f;
        run->word_x = 0.0f;
        run->word_base = 0.0f;
    }

    if (*x + advance > run->wrap && count > run->line_start) {
        box_text_break(run, run->line_end, count);
        run->word_start = count;
        run->word_x = 0.0f;
        run->word_base = 0.0f;
        run->line_end = 0.0f;
        *x = 0.0f;
    }
}

/** Advance the run over one code point.
 *
 * @param world The world
 * @param run The run
 * @param font The font, its face loaded
 * @param codepoint The code point
 * @param px Logical pixels per font unit
 */
static void box_text_char(
    ecs_world_t *world,
    box_text_run_t *run,
    box_text_font_t *font,
    int32_t codepoint,
    float px)
{
    int32_t count = ecs_vec_count(&run->s->placed);
    if (codepoint == '\n') {
        box_text_break(run, run->line_end, count);
        run->pen = 0.0f;
        run->line_end = 0.0f;
        run->word_start = count;
        run->prev = -1;
        run->in_word = false;
        goto done;
    }

    if (codepoint == '\r') {
        goto done;
    }

    int32_t glyph = box_text_face_glyph(font->face, codepoint == '\t' ? ' ' : codepoint);
    float advance = box_text_face_advance(font->face, glyph) * px;
    float x = run->pen;
    if (run->prev >= 0) {
        x += box_text_face_kern(font->face, run->prev, glyph) * px;
    }

    run->prev = glyph;
    if (codepoint == ' ' || codepoint == '\t') {
        run->pen = x + advance;
        run->in_word = false;
        goto done;
    }

    if (!run->in_word) {
        run->word_start = count;
        run->word_x = x;
        run->word_base = run->line_end;
        run->in_word = true;
    }

    if (run->wrap > 0.0f && x + advance > run->wrap && count > run->line_start) {
        box_text_wrap(run, &x, advance);
    }

    box_text_placed_t *p = ecs_vec_append_t(NULL, &run->s->placed, box_text_placed_t);
    p->x = x;
    p->line = run->line;
    p->glyph = box_text_font_glyph(world, run->s, font, glyph);
    run->pen = x + advance;
    run->line_end = run->pen;
done:
    return;
}

/** Turn the placed glyphs into quads: align the lines and center the block on the origin.
 *
 * @param run The finished run
 * @param font The font
 * @param text The Text
 * @param metrics Ascent, descent below the baseline and line height in logical pixels
 * @param out Receives the quads
 * @param bounds Receives the size of the block
 */
static void box_text_emit(
    const box_text_run_t *run,
    const box_text_font_t *font,
    const Text *text,
    const float *metrics,
    TextGlyphs *out,
    TextBounds *bounds)
{
    const box_text_placed_t *placed = ecs_vec_first(&run->s->placed);
    const float *widths = ecs_vec_first(&run->s->lines);
    int32_t i, lines = ecs_vec_count(&run->s->lines);
    float width = 0.0f, size = text->size;
    float k = size / (float)BOX_TEXT_EM;
    for (i = 0; i < lines; i ++) {
        width = widths[i] > width ? widths[i] : width;
    }

    float height = (float)lines * metrics[2];
    float lead = (metrics[2] - metrics[0] - metrics[1]) * 0.5f;
    bounds->width = width;
    bounds->height = height;
    for (i = 0; i < ecs_vec_count(&run->s->placed); i ++) {
        const box_text_atlas_glyph_t *g = placed[i].glyph;
        if (!g || g->page < 0) {
            continue;
        }

        const box_text_page_t *page = *ecs_vec_get_t(&font->pages, box_text_page_t*, g->page);
        float free = width - widths[placed[i].line];
        float indent = text->align == AlignCenter ? free * 0.5f
            : (text->align == AlignRight ? free : 0.0f);
        float left = -width * 0.5f + indent + placed[i].x + (float)g->left * k;
        float baseline = height * 0.5f - (float)placed[i].line * metrics[2] - lead - metrics[0];
        float top = baseline - (float)g->top * k;
        float p = (float)page->size;
        box_text_glyph_t *q = ecs_vec_append_t(NULL, &out->glyphs, box_text_glyph_t);
        q->width = (float)g->width * k;
        q->height = (float)g->height * k;
        q->x = left + q->width * 0.5f;
        q->y = top - q->height * 0.5f;
        q->uv[0] = (float)g->width / p;
        q->uv[1] = (float)g->height / p;
        q->uv[2] = (float)g->x / p;
        q->uv[3] = (float)g->y / p;
        q->range = BOX_TEXT_RANGE / p;
        q->page = page->handle;
    }
}

void box_text_layout(
    ecs_world_t *world,
    box_text_state_t *s,
    ecs_entity_t e,
    const Text *text)
{
    TextGlyphs *glyphs = ecs_get_mut(world, e, TextGlyphs);
    const TextBounds *prev = ecs_get(world, e, TextBounds);
    TextBounds bounds = {0};
    float ascent, descent, gap, metrics[3];
    if (!glyphs) {
        goto done;
    }

    ecs_vec_clear(&glyphs->glyphs);
    box_text_font_t *font = text->font ? box_text_font_of(world, s, text->font) : NULL;
    const char *str = text->value;
    if (!font || !font->face || !str || !str[0]) {
        goto write;
    }

    float units = box_text_face_metrics(font->face, &ascent, &descent, &gap);
    float px = text->size / units;
    metrics[0] = ascent * px;
    metrics[1] = -descent * px;
    metrics[2] = text->line_height > 0.0f
        ? text->line_height
        : (ascent - descent + gap) * px;
    box_text_run_t run = {
        .s = s,
        .wrap = text->width > 0.0f ? text->width : 0.0f,
        .prev = -1
    };
    ecs_vec_clear(&s->placed);
    ecs_vec_clear(&s->lines);
    while (*str) {
        box_text_char(world, &run, font, box_text_utf8(&str), px);
    }

    box_text_break(&run, run.line_end, ecs_vec_count(&s->placed));
    box_text_emit(&run, font, text, metrics, glyphs, &bounds);
write:
    ecs_modified(world, e, TextGlyphs);
    if (!prev || prev->width != bounds.width || prev->height != bounds.height) {
        ecs_set_ptr(world, e, TextBounds, &bounds);
    }

done:
    return;
}

/** Check whether a Text lays out again: its font asks for it, or every Text does.
 *
 * @param world The world
 * @param s The registry
 * @param text The Text
 * @param all Every Text lays out again
 * @return Whether the Text lays out again
 */
static bool box_text_stale(
    const ecs_world_t *world,
    box_text_state_t *s,
    const Text *text,
    bool all)
{
    ecs_entity_t e = text->font;
    bool result = all;
    if (!e || !ecs_is_alive(world, e) || !ecs_has(world, e, Font)) {
        e = s->default_font;
    }

    const box_text_font_t *font = ecs_map_get_deref(&s->fonts, box_text_font_t, e);
    if (font && font->relayout) {
        result = true;
    }

    return result;
}

/** Check whether a font of the registry asks for its texts to lay out again.
 *
 * @param s The registry
 * @return Whether one does
 */
static bool box_text_any_stale(
    box_text_state_t *s)
{
    bool result = s->relayout;
    ecs_map_iter_t it = ecs_map_iter(&s->fonts);
    while (ecs_map_next(&it)) {
        box_text_font_t *font = ecs_map_ptr(&it);
        result |= font->relayout;
    }

    return result;
}

/** Forget which fonts asked for their texts to lay out again.
 *
 * @param s The registry
 */
static void box_text_settle(
    box_text_state_t *s)
{
    ecs_map_iter_t it = ecs_map_iter(&s->fonts);
    while (ecs_map_next(&it)) {
        box_text_font_t *font = ecs_map_ptr(&it);
        font->relayout = false;
    }

    s->relayout = false;
}

void box_text_relayout(
    ecs_world_t *world,
    box_text_state_t *s,
    bool all)
{
    int32_t i, pass;
    for (pass = 0; pass < 4 && (all || box_text_any_stale(s)); pass ++) {
        bool every = all || s->relayout;
        ecs_vec_clear(&s->texts);
        ecs_iter_t it = ecs_query_iter(world, s->query);
        while (ecs_query_next(&it)) {
            const Text *texts = ecs_field(&it, Text, 0);
            for (i = 0; i < it.count; i ++) {
                if (box_text_stale(world, s, &texts[i], every)) {
                    *ecs_vec_append_t(NULL, &s->texts, ecs_entity_t) = it.entities[i];
                }
            }
        }

        box_text_settle(s);
        all = false;
        for (i = 0; i < ecs_vec_count(&s->texts); i ++) {
            ecs_entity_t e = *ecs_vec_get_t(&s->texts, ecs_entity_t, i);
            const Text *text = ecs_get(world, e, Text);
            if (text) {
                box_text_layout(world, s, e, text);
            }
        }
    }
}
