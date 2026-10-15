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
 * Changing the path loads the font again; the previous font stays in use until the new one
 * is ready. The engine writes FontStatus.
 */
ECS_STRUCT(Font, {
    char *path; /**< Font file, relative to the working directory or an ancestor of it */
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
 * Lengths are logical pixels. The block of lines is centered on the origin, y up, and
 * measures TextBounds. Lines break at newlines and, with a width, between words. In a layout a
 * Text fits its lines at Text.width and narrows to its widest word in a narrower parent.
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
