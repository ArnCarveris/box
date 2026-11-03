/**
 * @file text/typeset.c
 * @brief Lays out a Text: kerning, line breaks between words, alignment and its bounds.
 *
 * Glyphs advance along a pen in logical pixels; a word that passes the width moves to the next
 * line, a word wider than the width breaks inside, a wrapped line starting at the hang indent.
 * A Text is measured at Text.width for box.layout, which can wrap it narrower afterwards.
 *
 * Span styles are clipped once per run into sorted ranges. A styled glyph takes the variant
 * face FontVariants names for its style once loaded; what the face lacks is synthesized: a
 * bold glyph advances further, an italic one leans.
 */

#include "text.h"
#include <stdlib.h>

/** Logical pixels a line may pass its width by, so a block wraps at its own measured width */
#define BOX_TEXT_WRAP_SLACK (0.001f)

/** Style combinations of glyphs, indexed by BOX_TEXT_STYLE_ flags */
#define BOX_TEXT_STYLES (4)

/** The face of a style in a run, resolved on the first glyph of the style */
typedef struct box_text_run_face_t {
    box_text_font_t *font;  /**< The face: the font of the Text or a loaded variant */
    float px;               /**< Logical pixels per font unit of the face */
    uint8_t synth;          /**< BOX_TEXT_STYLE_ flags synthesized on the face */
    bool resolved;          /**< The face was resolved */
} box_text_run_face_t;

/** State of the layout of one string */
typedef struct box_text_run_t {
    box_text_state_t *s;    /**< The registry, its placed and lines vectors are filled */
    box_text_font_t *font;  /**< Font of the Text, its face loaded */
    box_text_run_face_t faces[BOX_TEXT_STYLES]; /**< Face per style */
    const box_text_font_t *prev_face; /**< Face of prev */
    float wrap;             /**< Width lines break at, 0 for newlines only */
    float hang;             /**< Pen x the lines a width wraps start at */
    int32_t hang_at;        /**< Byte of the first line whose pen x is the hang */
    bool draw;              /**< Glyphs are drawn, so they are added to the atlas */
    float em;               /**< Em size in logical pixels */
    int32_t style_at;       /**< Style range of the registry at or after the last code point */
    float pen;              /**< Pen x of the line */
    float line_end;         /**< Right edge of the last glyph of the line */
    float word_x;           /**< Pen x of the first glyph of the current word */
    float word_base;        /**< Right edge of the line before the current word */
    int32_t line;           /**< Line being filled */
    int32_t line_start;     /**< First placed glyph of the line */
    int32_t word_start;     /**< First placed glyph of the current word */
    int32_t prev;           /**< Glyph index before the pen, -1 at the start of a line */
    int32_t word_caret;     /**< Caret of the first character of the current word */
    int32_t row_caret;      /**< First caret of the line being filled */
    int32_t cr;             /**< Offset of a carriage return just before, -1 for none */
    float longest;          /**< Width of the widest word so far */
    bool in_word;           /**< The last character was not a space */
} box_text_run_t;

/** End the line being filled at a width and start the next one.
 *
 * @param run The run
 * @param width Width of the line
 * @param start First placed glyph of the next line
 * @param end Byte offset after the line, without its newline
 * @param caret First caret of the next line
 */
static void box_text_break(
    box_text_run_t *run,
    float width,
    int32_t start,
    int32_t end,
    int32_t caret)
{
    *ecs_vec_append_t(NULL, &run->s->lines, float) = width;
    *ecs_vec_append_t(NULL, &run->s->rows, box_text_row_t) = (box_text_row_t){
        .caret = run->row_caret, .end = end };
    run->line ++;
    run->line_start = start;
    run->row_caret = caret;
}

/** Make room for a glyph that passes the width: move its word to the next line, then break
 * inside a word that still does not fit.
 *
 * @param run The run
 * @param x Pen x of the glyph, moved with it
 * @param advance Advance of the glyph
 * @param caret Caret of the glyph
 */
static void box_text_wrap(
    box_text_run_t *run,
    float *x,
    float advance,
    int32_t caret)
{
    int32_t i, count = ecs_vec_count(&run->s->placed);
    box_text_placed_t *placed = ecs_vec_first(&run->s->placed);
    box_text_caret_t *carets = ecs_vec_first(&run->s->carets);
    if (run->word_start > run->line_start) {
        float shift = run->word_x - run->hang;
        box_text_break(run, run->word_base, run->word_start,
            carets[run->word_caret].offset, run->word_caret);
        for (i = run->word_start; i < count; i ++) {
            placed[i].x -= shift;
            placed[i].line = run->line;
        }

        for (i = run->word_caret; i <= caret; i ++) {
            carets[i].x -= shift;
            carets[i].line = run->line;
        }

        *x -= shift;
        run->line_end = count > run->word_start ? run->line_end - shift : 0.0f;
        run->word_x = run->hang;
        run->word_base = 0.0f;
    }

    if (*x + advance > run->wrap + BOX_TEXT_WRAP_SLACK && count > run->line_start) {
        box_text_break(run, run->line_end, count, carets[caret].offset, caret);
        run->word_start = count;
        run->word_caret = caret;
        run->word_x = run->hang;
        run->word_base = 0.0f;
        run->line_end = 0.0f;
        *x = run->hang;
    }
}

/** Get the style of a code point, walking the style ranges of the registry forward: offsets
 * only grow along a run, so a lookup is O(1) amortized.
 *
 * @param run The run
 * @param offset Byte offset of the code point
 * @return The BOX_TEXT_STYLE_ flags of the code point
 */
static uint8_t box_text_style_of(
    box_text_run_t *run,
    int32_t offset)
{
    const box_text_style_t *styles = ecs_vec_first(&run->s->styles);
    int32_t count = ecs_vec_count(&run->s->styles);
    while (run->style_at < count && styles[run->style_at].end <= offset) {
        run->style_at ++;
    }

    return run->style_at < count && styles[run->style_at].start <= offset
        ? styles[run->style_at].flags : 0;
}

/** Get the loaded font of a variant entity, requesting its load when no glyph needed it before.
 *
 * @param world The world
 * @param s The registry
 * @param e The variant entity, 0 for none
 * @return The font, NULL without a variant, for an entity without Font or until it loaded
 */
static box_text_font_t* box_text_variant_of(
    ecs_world_t *world,
    box_text_state_t *s,
    ecs_entity_t e)
{
    box_text_font_t *result = NULL;
    if (!e || !ecs_is_alive(world, e) || !ecs_has(world, e, Font)) {
        goto done;
    }

    result = box_text_font_of(world, s, e);
    if (!result->face) {
        result = NULL;
    }

done:
    return result;
}

/** Get the face of a style of a run, as the table of FontVariants gives it: the first loaded
 * variant for the style, else the font of the Text, and what it lacks synthesized.
 *
 * @param world The world
 * @param run The run
 * @param style BOX_TEXT_STYLE_ flags of the glyph
 * @return The face of the style
 */
static const box_text_run_face_t* box_text_face_of(
    ecs_world_t *world,
    box_text_run_t *run,
    uint8_t style)
{
    box_text_run_face_t *result = &run->faces[style];
    float ascent, descent, gap;
    int32_t i, count = 0;
    if (result->resolved) {
        goto done;
    }

    const FontVariants *v = style ? ecs_get(world, run->font->entity, FontVariants) : NULL;
    ecs_entity_t order[3] = {0};
    uint8_t lacks[3] = {0};
    result->font = run->font;
    result->synth = style;
    result->resolved = true;
    if (!v) {
        count = 0;
    } else if (style == (BOX_TEXT_STYLE_BOLD | BOX_TEXT_STYLE_ITALIC)) {
        order[0] = v->bold_italic;
        order[1] = v->bold;
        lacks[1] = BOX_TEXT_STYLE_ITALIC;
        order[2] = v->italic;
        lacks[2] = BOX_TEXT_STYLE_BOLD;
        count = 3;
    } else {
        order[0] = style == BOX_TEXT_STYLE_BOLD ? v->bold : v->italic;
        count = 1;
    }

    for (i = 0; i < count; i ++) {
        box_text_font_t *face = box_text_variant_of(world, run->s, order[i]);
        if (face) {
            result->font = face;
            result->synth = lacks[i];
            break;
        }
    }

    result->px = run->em / box_text_face_metrics(result->font->face, &ascent, &descent, &gap);
done:
    return result;
}

/** Advance the run over one code point and place the caret before it.
 *
 * @param world The world
 * @param run The run
 * @param codepoint The code point
 * @param offset Byte offset of the code point in the string
 */
static void box_text_char(
    ecs_world_t *world,
    box_text_run_t *run,
    int32_t codepoint,
    int32_t offset)
{
    int32_t count = ecs_vec_count(&run->s->placed);
    int32_t caret = ecs_vec_count(&run->s->carets), cr = run->cr;
    box_text_caret_t *c = ecs_vec_append_t(NULL, &run->s->carets, box_text_caret_t);
    *c = (box_text_caret_t){ .offset = offset, .line = run->line, .x = run->pen };
    run->cr = -1;
    if (codepoint == '\n') {
        box_text_break(run, run->line_end, count, cr >= 0 ? cr : offset, caret + 1);
        run->pen = 0.0f;
        run->line_end = 0.0f;
        run->word_start = count;
        run->prev = -1;
        run->in_word = false;
        goto done;
    }

    if (codepoint == '\r') {
        run->cr = offset;
        goto done;
    }

    const box_text_run_face_t *face = box_text_face_of(world, run, box_text_style_of(run, offset));
    box_text_font_t *font = face->font;
    float px = face->px;
    uint8_t style = face->synth;
    int32_t glyph = box_text_face_glyph(font->face, codepoint == '\t' ? ' ' : codepoint);
    float advance = box_text_face_advance(font->face, glyph) * px;
    if (style & BOX_TEXT_STYLE_BOLD) {
        advance += BOX_TEXT_BOLD_ADVANCE * run->em;
    }

    float x = run->pen;
    if (run->prev >= 0 && run->prev_face == font) {
        x += box_text_face_kern(font->face, run->prev, glyph) * px;
    }

    run->prev = glyph;
    run->prev_face = font;
    c->x = x;
    if (codepoint == ' ' || codepoint == '\t') {
        run->pen = x + advance;
        run->in_word = false;
        goto done;
    }

    if (!run->in_word) {
        run->word_start = count;
        run->word_caret = caret;
        run->word_x = x;
        run->word_base = run->line_end;
        run->in_word = true;
    }

    if (run->wrap > 0.0f && x + advance > run->wrap + BOX_TEXT_WRAP_SLACK &&
        count > run->line_start)
    {
        box_text_wrap(run, &x, advance, caret);
    }

    c->x = x;
    c->line = run->line;

    const box_text_atlas_glyph_t *area = run->draw ?
        box_text_font_glyph(world, run->s, font, glyph) : NULL;
    box_text_placed_t *p = ecs_vec_append_t(NULL, &run->s->placed, box_text_placed_t);
    p->x = x;
    p->line = run->line;
    p->offset = offset;
    p->glyph = area;
    p->font = font;
    p->style = style;
    run->pen = x + advance;
    run->line_end = run->pen;
    if (run->line_end - run->word_x > run->longest) {
        run->longest = run->line_end - run->word_x;
    }

done:
    if (offset == run->hang_at && !c->line && run->wrap > 0.0f) {
        run->hang = c->x <= run->wrap * 0.5f ? c->x : 0.0f;
    }
}

/** Copy the carets and lines of a run into TextGlyphs, aligned and in the local space.
 *
 * @param run The finished run
 * @param text The Text
 * @param width Width of the block
 * @param height Height of the block
 * @param out Receives the carets and lines
 */
static void box_text_emit_carets(
    const box_text_run_t *run,
    const Text *text,
    float width,
    float height,
    TextGlyphs *out)
{
    const box_text_caret_t *carets = ecs_vec_first(&run->s->carets);
    const float *widths = ecs_vec_first(&run->s->lines);
    int32_t i, count = ecs_vec_count(&run->s->carets);
    box_text_caret_t *dst = ecs_vec_grow_t(NULL, &out->carets, box_text_caret_t, count);
    for (i = 0; i < count; i ++) {
        float free = width - widths[carets[i].line];
        float indent = text->align == AlignCenter ? free * 0.5f
            : (text->align == AlignRight ? free : 0.0f);
        dst[i] = carets[i];
        dst[i].x = -width * 0.5f + indent + carets[i].x;
    }

    count = ecs_vec_count(&run->s->rows);
    ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &out->rows, box_text_row_t, count),
        ecs_vec_first(&run->s->rows), box_text_row_t, count);
    out->top = height * 0.5f;
    out->ready = true;
}

/** Turn the placed glyphs into quads: align the lines and center the block on the origin. A
 * glyph takes its page from its face, its baseline from the metrics of the font of the Text.
 *
 * @param run The finished run
 * @param text The Text
 * @param metrics Ascent, descent below the baseline and line height in logical pixels
 * @param box Width the lines align in when it is wider than the widest line
 * @param out Receives the quads
 * @param bounds Receives the size of the block
 */
static void box_text_emit(
    const box_text_run_t *run,
    const Text *text,
    const float *metrics,
    float box,
    TextGlyphs *out,
    TextBounds *bounds)
{
    const box_text_placed_t *placed = ecs_vec_first(&run->s->placed);
    const float *widths = ecs_vec_first(&run->s->lines);
    int32_t i, lines = ecs_vec_count(&run->s->lines);
    float width = box, size = text->size;
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

        const box_text_font_t *font = placed[i].font;
        const box_text_page_t *page = *ecs_vec_get_t(&font->pages, box_text_page_t*, g->page);
        float free = width - widths[placed[i].line];
        float indent = text->align == AlignCenter ? free * 0.5f
            : (text->align == AlignRight ? free : 0.0f);
        float left = -width * 0.5f + indent + placed[i].x + (float)g->left * k;
        float baseline = height * 0.5f - (float)placed[i].line * metrics[2] - lead - metrics[0];
        float top = baseline - (float)g->top * k;
        float p = (float)page->size;
        uint8_t style = placed[i].style;
        box_text_glyph_t *q = ecs_vec_append_t(NULL, &out->glyphs, box_text_glyph_t);
        q->width = (float)g->width * k;
        q->height = (float)g->height * k;
        q->x = left + q->width * 0.5f;
        q->y = top - q->height * 0.5f;
        q->shear = style & BOX_TEXT_STYLE_ITALIC ? BOX_TEXT_ITALIC_SHEAR : 0.0f;
        q->weight = style & BOX_TEXT_STYLE_BOLD ? BOX_TEXT_BOLD_FIELD : 0.0f;
        if (style & BOX_TEXT_STYLE_BOLD) {
            q->x += BOX_TEXT_BOLD_ADVANCE * size * 0.5f;
        }

        if (style & BOX_TEXT_STYLE_ITALIC) {
            q->x += q->shear * (q->y - baseline);
        }

        q->uv[0] = (float)g->width / p;
        q->uv[1] = (float)g->height / p;
        q->uv[2] = (float)g->x / p;
        q->uv[3] = (float)g->y / p;
        q->range = BOX_TEXT_RANGE / p;
        q->page = page->handle;
        q->offset = placed[i].offset;
    }

    box_text_emit_carets(run, text, width, height, out);
}

/** Order two edges of style ranges by their byte.
 *
 * @param a The first edge
 * @param b The second edge
 * @return Negative, 0 or positive as the first byte is before, at or after the second
 */
static int box_text_edge_compare(
    const void *a,
    const void *b)
{
    const box_text_style_t *l = a, *r = b;

    return (l->start > r->start) - (l->start < r->start);
}

/** Get the style flags of a span.
 *
 * @param span The span
 * @return Its BOX_TEXT_STYLE_ flags, 0 for a span that only colors
 */
static uint8_t box_text_span_style(
    const TextSpan *span)
{
    return (uint8_t)((span->bold ? BOX_TEXT_STYLE_BOLD : 0u) |
        (span->italic ? BOX_TEXT_STYLE_ITALIC : 0u));
}

/** Clip the range of a span to a string.
 *
 * @param span The span
 * @param length Byte length of the string
 * @param start Receives the first byte
 * @param end Receives the byte after the range
 * @return Whether the clipped range holds a byte
 */
static bool box_text_span_clip(
    const TextSpan *span,
    int32_t length,
    int32_t *start,
    int32_t *end)
{
    *start = span->start > 0 ? span->start : 0;
    *end = span->end < length ? span->end : length;

    return *start < *end;
}

/** Clip the styled spans of a Text into the sorted style ranges of the registry, the styles of
 * overlapping spans added up and neighbors of the same style merged.
 *
 * @param s The registry
 * @param spans The spans, NULL for none
 * @param length Byte length of the string
 */
static void box_text_styles(
    box_text_state_t *s,
    const TextSpans *spans,
    int32_t length)
{
    const TextSpan *first = spans ? ecs_vec_first(&spans->spans) : NULL;
    int32_t i, start, end, count = first ? ecs_vec_count(&spans->spans) : 0;
    int32_t bold = 0, italic = 0;
    ecs_vec_clear(&s->styles);
    ecs_vec_clear(&s->edges);
    for (i = 0; i < count; i ++) {
        uint8_t flags = box_text_span_style(&first[i]);
        if (!flags || !box_text_span_clip(&first[i], length, &start, &end)) {
            continue;
        }

        *ecs_vec_append_t(NULL, &s->edges, box_text_style_t) = (box_text_style_t){
            .start = start, .end = 1, .flags = flags };
        *ecs_vec_append_t(NULL, &s->edges, box_text_style_t) = (box_text_style_t){
            .start = end, .end = -1, .flags = flags };
    }

    box_text_style_t *edges = ecs_vec_first(&s->edges);
    count = ecs_vec_count(&s->edges);
    if (count > 1) {
        qsort(edges, (size_t)count, sizeof(box_text_style_t), box_text_edge_compare);
    }

    for (i = 0; i < count;) {
        int32_t at = edges[i].start;
        for (; i < count && edges[i].start == at; i ++) {
            bold += edges[i].flags & BOX_TEXT_STYLE_BOLD ? edges[i].end : 0;
            italic += edges[i].flags & BOX_TEXT_STYLE_ITALIC ? edges[i].end : 0;
        }

        uint8_t flags = (uint8_t)((bold > 0 ? BOX_TEXT_STYLE_BOLD : 0u) |
            (italic > 0 ? BOX_TEXT_STYLE_ITALIC : 0u));
        if (!flags || i == count) {
            continue;
        }

        int32_t styles = ecs_vec_count(&s->styles);
        box_text_style_t *last = styles
            ? ecs_vec_get_t(&s->styles, box_text_style_t, styles - 1) : NULL;
        if (last && last->end == at && last->flags == flags) {
            last->end = edges[i].start;
        } else {
            *ecs_vec_append_t(NULL, &s->styles, box_text_style_t) = (box_text_style_t){
                .start = at, .end = edges[i].start, .flags = flags };
        }
    }
}

/** Break a string into lines at a width, filling the placed and lines scratch of the registry.
 *
 * @param world The world
 * @param s The registry
 * @param font The font, its face loaded; styled glyphs take its variants
 * @param str The string
 * @param spans The spans whose styles the glyphs take, NULL for none
 * @param em Em size in logical pixels
 * @param wrap Width lines break at, 0 for newlines only
 * @param hang Byte of the first line whose pen x the lines a width wraps start at
 * @param draw Add the glyphs to the atlas, false to only measure them
 * @return The finished run
 */
static box_text_run_t box_text_run(
    ecs_world_t *world,
    box_text_state_t *s,
    box_text_font_t *font,
    const char *str,
    const TextSpans *spans,
    float em,
    float wrap,
    int32_t hang,
    bool draw)
{
    box_text_run_t result = { .s = s, .font = font, .wrap = wrap, .hang_at = hang,
        .draw = draw, .em = em, .prev = -1, .cr = -1 };
    const char *p = str;
    int32_t last = 0;
    box_text_styles(s, spans, (int32_t)strlen(str));
    ecs_vec_clear(&s->placed);
    ecs_vec_clear(&s->lines);
    ecs_vec_clear(&s->carets);
    ecs_vec_clear(&s->rows);
    while (*p) {
        int32_t offset = (int32_t)(p - str);
        last = box_utf8_next(&p);
        box_text_char(world, &result, last, offset);
    }

    int32_t length = (int32_t)(p - str);
    if (wrap > 0.0f && (last == ' ' || last == '\t') &&
        result.pen > wrap + BOX_TEXT_WRAP_SLACK)
    {
        box_text_break(&result, result.line_end, ecs_vec_count(&s->placed), length,
            ecs_vec_count(&s->carets));
        result.pen = result.hang;
        result.line_end = 0.0f;
    }

    *ecs_vec_append_t(NULL, &s->carets, box_text_caret_t) = (box_text_caret_t){
        .offset = length, .line = result.line, .x = result.pen };
    box_text_break(&result, result.line_end, ecs_vec_count(&s->placed),
        length, ecs_vec_count(&s->carets));

    return result;
}

/** Get the width of the widest line of the lines scratch of the registry.
 *
 * @param s The registry
 * @return The width
 */
static float box_text_widest(
    const box_text_state_t *s)
{
    const float *widths = ecs_vec_first(&s->lines);
    int32_t i, lines = ecs_vec_count(&s->lines);
    float result = 0.0f;
    for (i = 0; i < lines; i ++) {
        result = widths[i] > result ? widths[i] : result;
    }

    return result;
}

/** Give an empty string its one caret, at the origin of one line centered on the origin.
 *
 * @param out Receives the caret and the line, its line height set
 */
static void box_text_empty(
    TextGlyphs *out)
{
    *ecs_vec_append_t(NULL, &out->carets, box_text_caret_t) = (box_text_caret_t){0};
    *ecs_vec_append_t(NULL, &out->rows, box_text_row_t) = (box_text_row_t){0};
    out->top = out->line_height * 0.5f;
    out->ready = true;
}

/** Mix bytes into a 64 bit FNV-1a hash.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash with the bytes mixed in
 */
static uint64_t box_text_hash(
    uint64_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    size_t i;
    for (i = 0; i < size; i ++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }

    return hash;
}

uint64_t box_text_key(
    const Text *text)
{
    const char *str = text->value ? text->value : "";
    float sizes[3] = { text->size, text->line_height, text->width };
    uint64_t ids[4] = {
        (uint64_t)text->align, (uint64_t)text->wrap, text->font, (uint64_t)text->hang };
    uint64_t result = box_text_hash(14695981039346656037ull, str, strlen(str) + 1);
    result = box_text_hash(result, sizes, sizeof(sizes));

    return box_text_hash(result, ids, sizeof(ids));
}

uint64_t box_text_style_key(
    const TextSpans *spans,
    int32_t length)
{
    const TextSpan *first = spans ? ecs_vec_first(&spans->spans) : NULL;
    int32_t i, count = first ? ecs_vec_count(&spans->spans) : 0;
    uint64_t result = 14695981039346656037ull;
    bool styled = false;
    for (i = 0; i < count; i ++) {
        int32_t words[3] = { 0, 0, box_text_span_style(&first[i]) };
        if (!words[2] || !box_text_span_clip(&first[i], length, &words[0], &words[1])) {
            continue;
        }

        result = box_text_hash(result, words, sizeof(words));
        styled = true;
    }

    return !styled ? 0 : (result ? result : 1);
}

bool box_text_current_layout(
    const ecs_world_t *world,
    ecs_entity_t e,
    const Text *text,
    const TextGlyphs *glyphs)
{
    int32_t length = text->value ? (int32_t)strlen(text->value) : 0;

    return glyphs->key == box_text_key(text) &&
        glyphs->style == box_text_style_key(ecs_get(world, e, TextSpans), length);
}

/** Lay out a Text into its TextGlyphs and TextBounds.
 *
 * The lines break at Text.width, which gives the sizes box.layout fits the Text to, and again
 * at the width of the layout when that is narrower; they align within the width of the layout.
 *
 * @param world The world
 * @param s The registry
 * @param e The entity
 * @param text Its Text
 * @param spans The spans whose styles it takes, NULL for none
 * @param measure Measure the Text again, false when only the width of the layout changed
 */
static void box_text_typeset(
    ecs_world_t *world,
    box_text_state_t *s,
    ecs_entity_t e,
    const Text *text,
    const TextSpans *spans,
    bool measure)
{
    TextGlyphs *glyphs = ecs_get_mut(world, e, TextGlyphs);
    const TextBounds *prev = ecs_get(world, e, TextBounds);
    TextBounds bounds = {0};
    float ascent, descent, gap, metrics[3];
    if (!glyphs) {
        goto done;
    }

    float was[3] = { glyphs->natural[0], glyphs->natural[1], glyphs->longest };
    ecs_vec_clear(&glyphs->glyphs);
    ecs_vec_clear(&glyphs->carets);
    ecs_vec_clear(&glyphs->rows);
    glyphs->ready = false;
    glyphs->key = box_text_key(text);
    glyphs->style = box_text_style_key(spans, text->value ? (int32_t)strlen(text->value) : 0);
    if (measure) {
        glyphs->natural[0] = glyphs->natural[1] = glyphs->longest = 0.0f;
    }

    box_text_font_t *font = text->font ? box_text_font_of(world, s, text->font) : NULL;
    const char *str = text->value;
    if (!font || !font->face) {
        goto write;
    }

    float units = box_text_face_metrics(font->face, &ascent, &descent, &gap);
    float px = text->size / units;
    metrics[0] = ascent * px;
    metrics[1] = -descent * px;
    metrics[2] = text->line_height > 0.0f
        ? text->line_height
        : (ascent - descent + gap) * px;
    glyphs->line_height = metrics[2];
    if (!str || !str[0]) {
        box_text_empty(glyphs);
        goto write;
    }

    float wrap = text->width > 0.0f ? text->width : 0.0f;
    float box = text->wrap == TextWrapWords ? glyphs->box : 0.0f;
    bool narrow = !measure && box > 0.0f && box < glyphs->natural[0];
    box_text_run_t run = box_text_run(world, s, font, str, spans, text->size,
        narrow ? box : wrap, text->hang, true);
    if (measure) {
        glyphs->natural[0] = box_text_widest(s);
        glyphs->natural[1] = (float)ecs_vec_count(&s->lines) * metrics[2];
        glyphs->longest = run.longest;
        if (box > 0.0f && box < glyphs->natural[0]) {
            run = box_text_run(world, s, font, str, spans, text->size, box, text->hang, true);
        }
    }

    box_text_emit(&run, text, metrics, box, glyphs, &bounds);
write:
    s->trim = true;
    ecs_modified(world, e, TextGlyphs);
    bool moved = was[0] != glyphs->natural[0] || was[1] != glyphs->natural[1] ||
        was[2] != glyphs->longest;
    if (!prev || prev->width != bounds.width || prev->height != bounds.height || moved) {
        ecs_set_ptr(world, e, TextBounds, &bounds);
    }

done:
    return;
}

void box_text_layout(
    ecs_world_t *world,
    box_text_state_t *s,
    ecs_entity_t e,
    const Text *text)
{
    box_text_typeset(world, s, e, text, ecs_get(world, e, TextSpans), true);
}

void box_text_layout_spans(
    ecs_world_t *world,
    box_text_state_t *s,
    ecs_entity_t e,
    const Text *text,
    const TextSpans *spans)
{
    box_text_typeset(world, s, e, text, spans, true);
}

int32_t box_text_break_lines(
    ecs_world_t *world,
    const Text *text,
    ecs_vec_t *starts)
{
    return box_text_break_lines_spans(world, text, NULL, starts);
}

int32_t box_text_break_lines_spans(
    ecs_world_t *world,
    const Text *text,
    const TextSpans *spans,
    ecs_vec_t *starts)
{
    box_text_state_t *s = box_text_state_get(world);
    box_text_font_t *font = s && text->font ? box_text_font_of(world, s, text->font) : NULL;
    const char *str = text->value ? text->value : "";
    int32_t i, result = 0;
    if (starts) {
        ecs_vec_clear(starts);
    }

    if (!font || !font->face) {
        goto done;
    }

    if (!str[0]) {
        result = 1;
        if (starts) {
            *ecs_vec_append_t(NULL, starts, int32_t) = 0;
        }

        goto done;
    }

    box_text_run(world, s, font, str, spans, text->size,
        text->width > 0.0f ? text->width : 0.0f, text->hang, false);
    const box_text_row_t *rows = ecs_vec_first(&s->rows);
    const box_text_caret_t *carets = ecs_vec_first(&s->carets);
    int32_t caret_count = ecs_vec_count(&s->carets);
    result = ecs_vec_count(&s->rows);
    for (i = 0; starts && i < result; i ++) {
        int32_t at = rows[i].caret < caret_count ? rows[i].caret : caret_count - 1;
        *ecs_vec_append_t(NULL, starts, int32_t) = carets[at].offset;
    }

done:
    return result;
}

bool box_text_measure(
    const ecs_world_t *world,
    ecs_entity_t e,
    float *size,
    float *least,
    bool *wraps)
{
    const TextBounds *b = ecs_get(world, e, TextBounds);
    const Text *text = b ? ecs_get(world, e, Text) : NULL;
    const TextGlyphs *glyphs = text ? ecs_get(world, e, TextGlyphs) : NULL;
    *wraps = glyphs && text->wrap == TextWrapWords;
    if (glyphs) {
        size[0] = glyphs->natural[0];
        size[1] = glyphs->natural[1];
        *least = *wraps ? glyphs->longest : size[0];
    } else if (b) {
        size[0] = *least = b->width;
        size[1] = b->height;
    }

    return b != NULL;
}

float box_text_wrap_to(
    ecs_world_t *world,
    ecs_entity_t e,
    float width)
{
    box_text_state_t *s = box_text_state_get(world);
    TextGlyphs *glyphs = ecs_get_mut(world, e, TextGlyphs);
    const Text *text = ecs_get(world, e, Text);
    float box = width > 0.0f ? width : 0.0f;
    if (s && glyphs && text && glyphs->box != box) {
        glyphs->box = box;
        box_text_typeset(world, s, e, text, ecs_get(world, e, TextSpans), false);
    }

    const TextBounds *b = ecs_get(world, e, TextBounds);

    return b ? b->height : 0.0f;
}

/** Check whether a font asks for its texts to lay out again.
 *
 * @param s The registry
 * @param e The font entity, 0 for none
 * @return Whether the font is in the registry and asks for it
 */
static bool box_text_font_stale(
    box_text_state_t *s,
    ecs_entity_t e)
{
    const box_text_font_t *font = e
        ? ecs_map_get_deref(&s->fonts, box_text_font_t, e)
        : NULL;

    return font && font->relayout;
}

/** Check whether a Text lays out again: its font or a variant of FontVariants on it asks for
 * it, or every Text does.
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

    const FontVariants *v = ecs_get(world, e, FontVariants);
    if (box_text_font_stale(s, e)) {
        result = true;
    } else if (v && (box_text_font_stale(s, v->bold) || box_text_font_stale(s, v->italic) ||
        box_text_font_stale(s, v->bold_italic)))
    {
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
