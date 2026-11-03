/**
 * @file box/text.h
 * @brief Text components: fonts loaded on the job system and strings drawn as glyph quads.
 *
 * Glyphs come from a multi channel signed distance field atlas, so a Text stays crisp at any
 * scale and distance; the engine measures every Text into TextBounds for the layout.
 */

#ifndef BOX_TEXT_H
#define BOX_TEXT_H

#include "layout.h"

#ifdef BOX_TEXT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** A TrueType or OpenType font, loaded on the job system once a Text uses it.
 *
 * A new path loads the font again, and so does a change of its file that FileWatch (box/app.h)
 * sees; the previous font stays in use until the new one is ready.
 *
 * Atlas pages beyond atlas_pages that no Text shows are released, least recently used first, and
 * their glyphs drawn again on next use; only shown pages pass it. A new Font keeps 4, 0 none.
 */
ECS_STRUCT(Font, {
    char *path;          /**< Font file, relative to the working directory or an ancestor of it */
    int32_t atlas_pages; /**< Atlas pages kept before an idle page is cleared for new glyphs */
});

/** Progress of the load of a Font */
ECS_ENUM(FontState, {
    FontLoading = 0, /**< A load is in flight */
    FontReady = 1,   /**< The last load succeeded */
    FontFailed = 2   /**< The last load failed, error says why */
});

/** Outcome of the loads of a Font, written by the engine and read only for applications */
ECS_STRUCT(FontStatus, {
    FontState state;  /**< Progress of the last requested load */
    int32_t revision; /**< Loads that succeeded, the font changed when it increments */
    char *error;      /**< Why the last load failed, NULL unless state is FontFailed */
});

/** How the lines of a Text in a layout follow the width the layout gives it */
ECS_ENUM(TextWrap, {
    TextWrapWords = 0,    /**< Lines break between words at that width and align within it */
    TextWrapNewlines = 1  /**< The layout keeps the lines of Text.width, as Clay's wrap none */
});

/** A string drawn as one quad per glyph in the local XY plane of the entity, facing +Z.
 *
 * Lengths are logical pixels, the block of lines is centered on the origin, y up, and measures
 * TextBounds. Lines break at newlines and at a width between words, inside a wider word. In a
 * layout a Text fits its lines at Text.width and narrows to its widest word in a narrower parent.
 *
 * Color, opacity, emission and tint come from Rgb, Opacity, Emissive, Tint and BlendMode.
 */
ECS_STRUCT(Text, {
    char *value;        /**< The string, UTF-8 */
    float size;         /**< Em size in logical pixels, default 16 */
    float line_height;  /**< Logical pixels between two baselines, 0 for the spacing of the font */
    float width;        /**< Width lines wrap at, 0 to break at newlines only */
    Align align;        /**< Alignment of the lines inside the block */
    ecs_entity_t font;  /**< Entity with a Font, default box.text.DefaultFont, 0 for none */
    TextWrap wrap;      /**< How the lines follow the width of a layout, TextWrapWords */
    int32_t hang;       /**< Byte of the first line whose caret x the lines a width wraps start
                         *   at, as a hanging indent; 0 for the left edge, ignored past half
                         *   the width; the caret after spaces at the end past it too */
});

/** A byte range of Text.value drawn in a color or a style of its own */
ECS_STRUCT(TextSpan, {
    int32_t start;   /**< Byte offset of the first byte of the range */
    int32_t end;     /**< Byte offset after the range */
    rgba color;      /**< Display encoded color, its alpha multiplies the opacity of the text */
    bool bold;       /**< Glyphs are drawn bold, synthesized from the regular glyph */
    bool italic;     /**< Glyphs lean to the right, synthesized from the regular glyph */
    bool keep_color; /**< The span leaves the color alone, color is ignored */
});

/** Colored and styled byte ranges of the Text of the entity.
 *
 * A glyph whose first byte falls in a span takes its color, a later span without keep_color
 * winning, ranges clipped to the string; outside them it keeps the entity color. Styles add up:
 * a glyph is bold or italic if any span over its first byte is, synthesized from the glyph.
 *
 * A color only span changes no layout, a styled one does: TextBounds, wrapping and carets follow
 * the wider advance of bold glyphs. The lean of italic glyphs is not in TextBounds, a caret sits
 * on the advance without it.
 */
ECS_STRUCT(TextSpans, {
    ecs_vec(TextSpan) spans; /**< The spans, TextSpan, later ones over earlier ones */
});

/** Size of the block of lines of a Text, in logical pixels, written by the engine.
 *
 * The width is the widest line without trailing spaces, or the width a layout wraps the lines
 * in when that is wider. The height is the line count times the line height. Both are 0 for an
 * empty string, without a font or while the font is not loaded.
 */
ECS_STRUCT(TextBounds, {
    float width;  /**< Width of the block */
    float height; /**< Height of the lines */
});

/** The caret line at a byte offset of a Text, in the local space of the entity.
 *
 * Logical pixels, y up, in the block of lines TextBounds measures, centered on the origin.
 */
ECS_STRUCT(TextCaret, {
    float x;      /**< Between the glyphs around the offset: the kerned advance, not the ink */
    float y;      /**< Bottom of the line of the caret */
    float height; /**< Line height of the line */
    int32_t line; /**< Line of the caret, 0 for the first */
});

/** Get the caret line at a byte offset of a Text, from the layout that draws it.
 *
 * An offset inside a character moves to its start; offsets clamp to 0 and the string length.
 * At a wrap point the caret starts the next line, after a trailing newline an empty line, and
 * after spaces at the end that pass the width an empty line at the hang indent.
 *
 * A Text whose value, parameters or span styles changed since its last layout is laid out again
 * first.
 *
 * @param world The world
 * @param text The entity with the Text
 * @param offset Byte offset in Text.value
 * @param caret Receives the caret line
 * @return False while the font is not loaded or when the entity has no Text
 */
bool box_text_caret(
    ecs_world_t *world,
    ecs_entity_t text,
    int32_t offset,
    TextCaret *caret);

/** Get the caret offset of a Text closest to a point in the local space of the entity.
 *
 * The line comes from y, clamped to the first and last line, then the nearest caret of that
 * line from x: a point left of the halfway point of a glyph is before it, else after it.
 *
 * @param world The world
 * @param text The entity with the Text
 * @param x X in logical pixels
 * @param y Y in logical pixels, up
 * @return The byte offset of the caret, 0 while the font is not loaded or without a Text
 */
int32_t box_text_hit(
    ecs_world_t *world,
    ecs_entity_t text,
    float x,
    float y);

/** Get the byte range of a line of a Text, without the newline that ends it.
 *
 * A wrapped line ends where the next one starts. The line clamps to the lines of the Text. The
 * line count includes the line of the caret after spaces at the end that pass the width, which
 * starts and ends at the string length.
 *
 * @param world The world
 * @param text The entity with the Text
 * @param line The line, 0 for the first
 * @param start Receives the offset of the first character of the line, may be NULL
 * @param end Receives the offset after its last character, may be NULL
 * @return The line count, 0 while the font is not loaded or without a Text
 */
int32_t box_text_line(
    ecs_world_t *world,
    ecs_entity_t text,
    int32_t line,
    int32_t *start,
    int32_t *end);

/** Break a Text into its lines without an entity, as it lays out outside a layout: at newlines
 * and between words at Text.width, inside a word wider than the width, with its hang indent.
 *
 * Glyphs are only measured, not added to the atlas. An empty string has one line. Spaces at the
 * end that pass the width add the empty line of the caret after them, see Text; spaces before a
 * newline or a word add none, the carets between spaces stay on their line past the width.
 *
 * @param world The world
 * @param text The Text, its font loaded when no other Text requested it before
 * @param starts Receives the int32_t byte offset of the first character of every line, an
 * initialized vector that is cleared first, NULL to only count
 * @return The line count, 0 while the font is not loaded or for a Text without font
 */
int32_t box_text_break_lines(
    ecs_world_t *world,
    const Text *text,
    ecs_vec_t *starts);

/** Break a Text into its lines without an entity, with the styles of spans: as
 * box_text_break_lines, a bold glyph advancing as in the layout of a Text with these spans.
 *
 * @param world The world
 * @param text The Text, its font loaded when no other Text requested it before
 * @param spans The spans of the Text, NULL for none
 * @param starts Receives the int32_t byte offset of the first character of every line, an
 * initialized vector that is cleared first, NULL to only count
 * @return The line count, 0 while the font is not loaded or for a Text without font
 */
int32_t box_text_break_lines_spans(
    ecs_world_t *world,
    const Text *text,
    const TextSpans *spans,
    ecs_vec_t *starts);

/** Register the box.text module.
 *
 * @param world The world
 */
void BoxTextImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
