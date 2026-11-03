/**
 * @file text/caret.c
 * @brief Caret positions, hit testing and line ranges of a Text, from the layout that draws it.
 *
 * The layout keeps the caret before every character and the byte range of every line in
 * TextGlyphs; a Text that changed since its last layout is laid out again before a lookup.
 */

#include "text.h"
#include <math.h>

/** Get the laid out carets of a Text, laying the Text out again when it changed since.
 *
 * @param world The world
 * @param e The entity
 * @return The glyphs of the Text, NULL without a Text or while its font is not loaded
 */
static const TextGlyphs* box_text_current(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_text_state_t *s = box_text_state_get(world);
    const Text *text = s ? ecs_get(world, e, Text) : NULL;
    const TextGlyphs *result = text ? ecs_get(world, e, TextGlyphs) : NULL;
    if (result && !box_text_current_layout(world, e, text, result)) {
        box_text_layout(world, s, e, text);
        result = ecs_get(world, e, TextGlyphs);
    }

    if (result && (!result->ready || !ecs_vec_count(&result->rows))) {
        result = NULL;
    }

    return result;
}

/** Find the caret at or before a byte offset.
 *
 * @param glyphs The laid out glyphs, ready
 * @param offset The byte offset
 * @return Index of the last caret whose offset is not past the offset
 */
static int32_t box_text_caret_find(
    const TextGlyphs *glyphs,
    int32_t offset)
{
    const box_text_caret_t *carets = ecs_vec_first(&glyphs->carets);
    int32_t low = 0, high = ecs_vec_count(&glyphs->carets) - 1;
    while (low < high) {
        int32_t mid = low + (high - low + 1) / 2;
        if (carets[mid].offset <= offset) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }

    return low;
}

/** Get the carets of a line.
 *
 * @param glyphs The laid out glyphs, ready
 * @param line The line, clamped to the lines
 * @param first Receives the index of the first caret of the line
 * @param last Receives the index after its last caret
 * @return The clamped line
 */
static int32_t box_text_row_carets(
    const TextGlyphs *glyphs,
    int32_t line,
    int32_t *first,
    int32_t *last)
{
    const box_text_row_t *rows = ecs_vec_first(&glyphs->rows);
    int32_t count = ecs_vec_count(&glyphs->rows);
    int32_t result = line < 0 ? 0 : (line >= count ? count - 1 : line);
    *first = rows[result].caret;
    *last = result + 1 < count ? rows[result + 1].caret : ecs_vec_count(&glyphs->carets);

    return result;
}

bool box_text_caret(
    ecs_world_t *world,
    ecs_entity_t text,
    int32_t offset,
    TextCaret *caret)
{
    const TextGlyphs *glyphs = box_text_current(world, text);
    if (!glyphs) {
        goto done;
    }

    const box_text_caret_t *c = ecs_vec_get_t(&glyphs->carets, box_text_caret_t,
        box_text_caret_find(glyphs, offset));
    caret->x = c->x;
    caret->y = glyphs->top - (float)(c->line + 1) * glyphs->line_height;
    caret->height = glyphs->line_height;
    caret->line = c->line;
done:
    return glyphs != NULL;
}

int32_t box_text_hit(
    ecs_world_t *world,
    ecs_entity_t text,
    float x,
    float y)
{
    const TextGlyphs *glyphs = box_text_current(world, text);
    int32_t first, last, result = 0;
    if (!glyphs) {
        goto done;
    }

    int32_t rows = ecs_vec_count(&glyphs->rows);
    float row = glyphs->line_height > 0.0f
        ? floorf((glyphs->top - y) / glyphs->line_height) : 0.0f;
    int32_t line = !(row >= 0.0f) ? 0 : (row >= (float)rows ? rows - 1 : (int32_t)row);
    box_text_row_carets(glyphs, line, &first, &last);
    const box_text_caret_t *carets = ecs_vec_first(&glyphs->carets);
    int32_t low = first, high = last;
    while (low < high) {
        int32_t mid = low + (high - low) / 2;
        if (carets[mid].x < x) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    if (low == last) {
        low = last - 1;
    } else if (low > first && x < (carets[low - 1].x + carets[low].x) * 0.5f) {
        low --;
    }

    result = carets[low].offset;
done:
    return result;
}

int32_t box_text_line(
    ecs_world_t *world,
    ecs_entity_t text,
    int32_t line,
    int32_t *start,
    int32_t *end)
{
    const TextGlyphs *glyphs = box_text_current(world, text);
    int32_t first, last, from = 0, to = 0, result = 0;
    if (glyphs) {
        line = box_text_row_carets(glyphs, line, &first, &last);
        from = ecs_vec_get_t(&glyphs->carets, box_text_caret_t, first)->offset;
        to = ecs_vec_get_t(&glyphs->rows, box_text_row_t, line)->end;
        result = ecs_vec_count(&glyphs->rows);
    }

    if (start) {
        *start = from;
    }

    if (end) {
        *end = to;
    }

    return result;
}
