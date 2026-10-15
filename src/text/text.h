/**
 * @file text/text.h
 * @brief Fonts, glyph atlases and the laid out glyphs of the box.text sources and the renderer.
 *
 * Fonts load and glyph fields generate on the job system; the atlas pages are RGBA8 images the
 * render extract sends to the backend, and TextGlyphs holds the quads of a laid out Text.
 */

#ifndef BOX_TEXT_INTERNAL_H
#define BOX_TEXT_INTERNAL_H

#include <box.h>

/** Atlas texels per em of a glyph field */
#define BOX_TEXT_EM (40)

/** Texels of distance the 0 to 1 of a field channel spans */
#define BOX_TEXT_RANGE (4.0f)

/** Texels a glyph field reaches beyond the outline of the glyph */
#define BOX_TEXT_PAD (3)

/** Texels left empty between two glyphs of a page */
#define BOX_TEXT_SPACING (4)

/** Width and height of a new atlas page */
#define BOX_TEXT_PAGE_MIN (256)

/** Width and height a page grows to before the next glyph starts a new page */
#define BOX_TEXT_PAGE_MAX (1024)

/** Most passes box_text_atlas_place makes over the pages before a glyph is drawn as missing */
#define BOX_TEXT_PLACE_TRIES (8)

/** First backend texture handle of atlas pages, the handles below belong to box.texture */
#define BOX_TEXT_PAGE_FIRST (0x40000000u)

/** Em size of a constructed Text, in logical pixels */
#define BOX_TEXT_SIZE_DEFAULT (16.0f)

/** Font file of box.text.DefaultFont */
#define BOX_TEXT_DEFAULT_FONT "etc/fonts/B612-Regular.ttf"

/** Seconds a headless frame waits at most for the font loads and glyph fields in flight */
#define BOX_TEXT_HEADLESS_WAIT (10.0)

/** A parsed font file, shared by the main thread and the jobs that read its outlines */
typedef struct box_text_face_t box_text_face_t;

/** Quad of one glyph of a laid out Text */
typedef struct box_text_glyph_t {
    float x;        /**< Center x in the local space of the entity, logical pixels */
    float y;        /**< Center y in the local space of the entity, logical pixels */
    float width;    /**< Width in logical pixels */
    float height;   /**< Height in logical pixels */
    float uv[4];    /**< Area of the page: u scale, v scale, u offset, v offset */
    float range;    /**< Distance range of the field in texture coordinates of the page */
    uint32_t page;  /**< Backend texture handle of the atlas page */
} box_text_glyph_t;

/** Glyph quads of a Text, written by the layout and read by the renderer */
typedef struct TextGlyphs {
    ecs_vec_t glyphs;   /**< box_text_glyph_t, in the order of the string */
    float box;          /**< Width box.layout gives the Text, 0 outside a layout */
    float natural[2];   /**< Width and height of the block wrapped at Text.width only */
    float longest;      /**< Width of the widest word, the narrowest the block wraps to */
} TextGlyphs;

/** Component id of TextGlyphs */
extern ECS_COMPONENT_DECLARE(TextGlyphs);

/** An RGBA8 page of a glyph atlas: RGB the multi channel field, A the true distance */
typedef struct box_text_page_t {
    uint32_t handle;    /**< Backend texture handle */
    int32_t size;       /**< Width and height in texels */
    uint8_t *pixels;    /**< RGBA8 rows */
    int32_t pen_x;      /**< Column the next glyph of the shelf goes at */
    int32_t pen_y;      /**< Row of the shelf being filled */
    int32_t shelf;      /**< Height of the shelf being filled */
    bool dirty;         /**< The backend does not have the current pixels */
} box_text_page_t;

/** Where the field of a glyph is in the atlas of its font */
typedef struct box_text_atlas_glyph_t {
    int32_t page;   /**< Index of the page in the font, -1 for a glyph without outline */
    int32_t x;      /**< Column of the area on the page */
    int32_t y;      /**< Row of the area on the page */
    int32_t width;  /**< Columns of the area */
    int32_t height; /**< Rows of the area */
    int32_t left;   /**< Texels from the pen to the left edge of the area */
    int32_t top;    /**< Texels from the baseline down to the top edge of the area */
    bool ready;     /**< The field is on the page */
} box_text_atlas_glyph_t;

/** A glyph of a string placed on its line, before the lines are aligned */
typedef struct box_text_placed_t {
    float x;                                /**< Pen x of the glyph on its line */
    int32_t line;                           /**< Line of the glyph */
    const box_text_atlas_glyph_t *glyph;    /**< Atlas area of the glyph */
} box_text_placed_t;

/** Counter of the jobs in flight, shared by the registry and its jobs */
typedef struct box_text_sync_t {
    ecs_os_mutex_t lock;    /**< Protects working */
    int32_t working;        /**< Jobs whose work did not finish */
    int32_t refs;           /**< The registry and every job, changed on the main thread */
} box_text_sync_t;

/** A font entity of the registry */
typedef struct box_text_font_t {
    ecs_entity_t entity;    /**< The font entity */
    uint32_t generation;    /**< Bumped by every load request, older results are dropped */
    uint32_t epoch;         /**< Bumped when the atlas is dropped, older glyph fields are too */
    box_text_face_t *face;  /**< The loaded face, NULL before the first load succeeded */
    ecs_map_t glyphs;       /**< Glyph index to box_text_atlas_glyph_t* */
    ecs_vec_t pages;        /**< box_text_page_t*, the atlas of the face */
    bool requested;         /**< A Text uses the font, so the font loads */
    bool relayout;          /**< A page grew, the texts of the font lay out again */
    bool oversized;         /**< A glyph of the face did not fit a page, which was logged */
    bool malformed;         /**< A composite glyph of the face nests too deep, which was logged */
} box_text_font_t;

/** Main thread state of the fonts of a world */
typedef struct box_text_state_t {
    ecs_map_t fonts;            /**< Font entity to box_text_font_t* */
    ecs_vec_t released;         /**< uint32_t, page handles the backend should destroy */
    ecs_vec_t placed;           /**< Scratch of the layout: box_text_placed_t of a string */
    ecs_vec_t lines;            /**< Scratch of the layout: float width per line */
    ecs_vec_t texts;            /**< Scratch: entities to lay out again */
    uint32_t next_page;         /**< Next page handle */
    box_text_sync_t *sync;      /**< Jobs in flight */
    ecs_query_t *query;         /**< Entities with a Text */
    ecs_entity_t default_font;  /**< box.text.DefaultFont */
    bool relayout;              /**< A font was removed, every Text lays out again */
} box_text_state_t;

/** Singleton that holds the font registry of a world */
typedef struct TextRegistry {
    box_text_state_t *state; /**< The registry */
} TextRegistry;

/** Component id of TextRegistry */
extern ECS_COMPONENT_DECLARE(TextRegistry);

/** Get the font registry of a world.
 *
 * @param world The world
 * @return The registry, NULL when box.text is not imported
 */
box_text_state_t* box_text_state_get(
    const ecs_world_t *world);

/** Get the font a Text uses, requesting its load when no Text used it before.
 *
 * @param world The world
 * @param s The registry
 * @param font The font entity of the Text, an entity without Font for the default font
 * @return The font, its face NULL until it loaded
 */
box_text_font_t* box_text_font_of(
    ecs_world_t *world,
    box_text_state_t *s,
    ecs_entity_t font);

/** Get the atlas area of a glyph, placing it and submitting its field when it is new.
 *
 * @param world The world
 * @param s The registry
 * @param font The font, its face loaded
 * @param glyph The glyph index
 * @return The area
 */
const box_text_atlas_glyph_t* box_text_font_glyph(
    ecs_world_t *world,
    box_text_state_t *s,
    box_text_font_t *font,
    int32_t glyph);

/** Load the face of a font entity again, after its Font changed.
 *
 * @param world The world
 * @param s The registry
 * @param font The font
 */
void box_text_font_load(
    ecs_world_t *world,
    box_text_state_t *s,
    box_text_font_t *font);

/** Drop the atlas of a font: its glyphs and pages, releasing the page handles.
 *
 * @param s The registry
 * @param font The font
 */
void box_text_font_clear(
    box_text_state_t *s,
    box_text_font_t *font);

/** Free a font and its atlas.
 *
 * @param s The registry
 * @param font The font
 */
void box_text_font_free(
    box_text_state_t *s,
    box_text_font_t *font);

/** Get the vertical metrics of a face in font units.
 *
 * @param face The face
 * @param ascent Receives the height above the baseline
 * @param descent Receives the depth below the baseline, negative
 * @param gap Receives the gap between two lines
 * @return Font units per em
 */
float box_text_face_metrics(
    const box_text_face_t *face,
    float *ascent,
    float *descent,
    float *gap);

/** Map a code point to the glyph index of a face.
 *
 * @param face The face
 * @param codepoint The code point
 * @return The glyph index, 0 for the missing glyph
 */
int32_t box_text_face_glyph(
    const box_text_face_t *face,
    int32_t codepoint);

/** Get the advance of a glyph in font units.
 *
 * @param face The face
 * @param glyph The glyph index
 * @return The advance
 */
float box_text_face_advance(
    const box_text_face_t *face,
    int32_t glyph);

/** Get the kerning between two glyphs in font units.
 *
 * @param face The face
 * @param left The glyph index on the left
 * @param right The glyph index on the right
 * @return The adjustment of the advance of the left glyph
 */
float box_text_face_kern(
    const box_text_face_t *face,
    int32_t left,
    int32_t right);

/** Lay out a Text into its TextGlyphs and TextBounds.
 *
 * @param world The world
 * @param s The registry
 * @param e The entity
 * @param text Its Text
 */
void box_text_layout(
    ecs_world_t *world,
    box_text_state_t *s,
    ecs_entity_t e,
    const Text *text);

/** Get the sizes box.layout fits a text element to, the measure hook of the layout.
 *
 * A Text gives the size of its lines at Text.width and its widest word, an element with only
 * TextBounds gives that size.
 *
 * @param world The world
 * @param e The entity
 * @param size Receives the width and height of the lines at Text.width
 * @param least Receives the narrowest width the lines wrap to
 * @param wraps Receives whether the element is a Text that takes the width of the layout
 * @return False when the entity has no TextBounds
 */
bool box_text_measure(
    const ecs_world_t *world,
    ecs_entity_t e,
    float *size,
    float *least,
    bool *wraps);

/** Give a Text the width box.layout resolved, laying it out again on a change.
 *
 * Lines break at the width when it is narrower than the lines at Text.width, and align within
 * it. A width of 0 lays the Text out on its own again.
 *
 * @param world The world
 * @param e The entity, which has a Text
 * @param width The width
 * @return The height of the block
 */
float box_text_wrap_to(
    ecs_world_t *world,
    ecs_entity_t e,
    float width);

/** Lay out every Text again whose font is one of the fonts that ask for it.
 *
 * @param world The world
 * @param s The registry
 * @param all Lay out every Text, whatever its font
 */
void box_text_relayout(
    ecs_world_t *world,
    box_text_state_t *s,
    bool all);

#endif
