/**
 * @file ui/text.c
 * @brief Measures a string, wraps it to a width and emits its glyphs as quads.
 *
 * The layout pass measures a text node while it sizes every other node, so the
 * element is as large as the lines it holds and the row or the column above it
 * places it like any other box. Once the pass placed everything, the build
 * gives every text node one child per visible glyph: a rectangle the size of
 * the bitmap, an Image that names the region of the atlas it shows and the
 * material of that atlas, which is the same 2D shape path a picture takes. A
 * build is skipped when the fingerprint of the string, the font, the box and
 * the color matches the one the last build drew, so text that stands still
 * costs one comparison and no entity ever changes.
 */

#include "ui_internal.h"
#include <math.h>

/** Seed of the fingerprint of a build, the FNV-1a offset basis */
#define BOX_UI_HASH_SEED (14695981039346656037ULL)

/** Multiplier of the fingerprint of a build, the FNV-1a prime */
#define BOX_UI_HASH_PRIME (1099511628211ULL)

/** Fold bytes into a fingerprint.
 *
 * @param hash The fingerprint so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The fingerprint
 */
static uint64_t box_ui_hash(
    uint64_t hash,
    const void *data,
    int32_t size)
{
    const uint8_t *bytes = data;
    int32_t i;
    for (i = 0; i < size; i ++) {
        hash = (hash ^ bytes[i]) * BOX_UI_HASH_PRIME;
    }

    return hash;
}

/** Read the code point a UTF-8 sequence spells and move past it.
 *
 * A byte that starts no valid sequence is read as itself, so a string that is
 * not UTF-8 still lays out instead of stopping the element that holds it.
 *
 * @param str The string, advanced past the sequence
 * @return The code point
 */
static int32_t box_ui_utf8(
    const char **str)
{
    const uint8_t *p = (const uint8_t*)*str;
    int32_t result = p[0];
    int32_t extra = 0;
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

    int32_t i;
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

float box_ui_text_line_height(
    const box_ui_atlas_t *atlas,
    const Text *text)
{
    float result = text->line_height;
    if (result <= 0) {
        result = atlas->ascent + atlas->descent + atlas->line_gap;
    }

    return result;
}

/** State one run of box_ui_text_run keeps while it walks a string */
typedef struct box_ui_run_t {
    ecs_vec_t *quads;   /**< box_ui_quad_t the run fills */
    float wrap;         /**< Width a line is broken at, 0 to break on newlines only */
    float box;          /**< Width the lines are aligned in, 0 for their own width */
    float height;       /**< Distance between two baselines */
    Align align;        /**< Alignment of the lines inside the box */
    float pen;          /**< X the next glyph of the line goes at */
    float widest;       /**< Width of the widest line the run closed */
    float width;        /**< Width of the line up to the end of its last word */
    float word_x;       /**< X the word being filled starts at */
    int32_t line_start; /**< First quad of the line being filled */
    int32_t word_start; /**< First quad of the word being filled */
    int32_t lines;      /**< Lines the run closed */
} box_ui_run_t;

/** X of the left of a line of a width, given the alignment of the run.
 *
 * @param run The run
 * @param width Width of the line
 * @return X of the left of the line
 */
static float box_ui_text_indent(
    const box_ui_run_t *run,
    float width)
{
    float result = 0;
    if (run->box <= 0) {
        goto done;
    }

    if (run->align == AlignCenter) {
        result = (run->box - width) * 0.5f;
    } else if (run->align == AlignRight) {
        result = run->box - width;
    }

done:
    return result;
}

/** Close the line being filled, align its quads and start the next one.
 *
 * @param run The run
 * @param end Quad the line ends before
 * @param width Width of the line
 */
static void box_ui_text_close(
    box_ui_run_t *run,
    int32_t end,
    float width)
{
    float indent = box_ui_text_indent(run, width);
    int32_t i;
    for (i = run->line_start; i < end; i ++) {
        ecs_vec_get_t(run->quads, box_ui_quad_t, i)->x += indent;
    }

    if (width > run->widest) {
        run->widest = width;
    }

    run->lines ++;
    run->line_start = end;
}

/** Move the word being filled to the start of the next line.
 *
 * @param run The run
 */
static void box_ui_text_move(
    box_ui_run_t *run)
{
    int32_t i, count = ecs_vec_count(run->quads);
    for (i = run->word_start; i < count; i ++) {
        box_ui_quad_t *quad = ecs_vec_get_t(run->quads, box_ui_quad_t, i);

        quad->x -= run->word_x;
        quad->y += run->height;
    }
}

/** Width of the line being filled, which drops a run of trailing spaces.
 *
 * @param run The run
 * @return The width
 */
static float box_ui_text_width(
    const box_ui_run_t *run)
{
    return ecs_vec_count(run->quads) > run->word_start ? run->pen : run->width;
}

/** Start a new line of the run, which a newline in the value asks for.
 *
 * @param run The run
 */
static void box_ui_text_newline(
    box_ui_run_t *run)
{
    int32_t count = ecs_vec_count(run->quads);

    box_ui_text_close(run, count, box_ui_text_width(run));
    run->pen = 0;
    run->width = 0;
    run->word_x = 0;
    run->word_start = count;
}

void box_ui_text_run(
    ecs_world_t *world,
    box_ui_atlas_t *atlas,
    const Text *text,
    float wrap,
    float box,
    box_ui_extent_t *out)
{
    UiFonts *fonts = box_ui_fonts(world);
    box_ui_run_t run = {
        .quads = &fonts->quads,
        .wrap = wrap,
        .box = box,
        .height = box_ui_text_line_height(atlas, text),
        .align = text->align
    };

    const char *cur = text->value ? text->value : "";
    const char *value = cur;
    ecs_vec_clear(&fonts->quads);
    while (cur[0] && !atlas->failed) {
        const char *byte = cur;
        int32_t cp = box_ui_utf8(&cur);
        const box_ui_glyph_t *glyph = NULL;
        if (cp == '\n') {
            box_ui_text_newline(&run);
            continue;
        }

        if (cp == ' ' || cp == '\t') {
            glyph = box_ui_atlas_glyph(world, atlas, ' ');
            if (ecs_vec_count(&fonts->quads) > run.word_start) {
                run.width = run.pen;
            }

            run.pen += glyph ? glyph->advance * (cp == '\t' ? 4.0f : 1.0f) : 0;
            run.word_start = ecs_vec_count(&fonts->quads);
            run.word_x = run.pen;
            continue;
        }

        glyph = box_ui_atlas_glyph(world, atlas, cp);
        if (!glyph) {
            continue;
        }

        if (glyph->width > 0 && glyph->height > 0) {
            box_ui_quad_t *quad = ecs_vec_append_t(NULL, &fonts->quads, box_ui_quad_t);

            quad->glyph = glyph;
            quad->x = run.pen + glyph->bearing_x;
            quad->y = (float)run.lines * run.height + atlas->ascent + glyph->top;
            quad->byte = (int32_t)(byte - value);
        }

        run.pen += glyph->advance;
        if (wrap > 0 && run.pen > wrap && run.word_start > run.line_start) {
            float carried = run.pen - run.word_x;

            box_ui_text_move(&run);
            box_ui_text_close(&run, run.word_start, run.width);
            run.pen = carried;
            run.width = carried;
            run.word_x = 0;
        }
    }

    box_ui_text_close(&run, ecs_vec_count(&fonts->quads), box_ui_text_width(&run));
    out->lines = run.lines;
    out->width = run.widest;
    out->height = (float)run.lines * run.height;
}

/** Width a text element wraps its lines at, from its Layout or its Rect.
 *
 * @param p The place of the node
 * @return The width, 0 when the element does not wrap
 */
static float box_ui_text_wrap_width(
    const UiPlace *p)
{
    float result = 0;
    if (p->has_layout && p->layout.width > 0) {
        result = p->layout.width - p->layout.padding * 2;
    } else if (p->fill_width > 0) {
        result = p->fill_width;
    } else if (p->has_rect && p->width > 0) {
        result = p->width;
    }

    return result;
}

void box_ui_text_measure(
    ecs_world_t *world,
    UiPlace *p)
{
    const Text *text = ecs_get(world, p->e, Text);
    if (!text) {
        goto done;
    }

    p->has_text = true;
    p->layout.direction = DirectionNone;

    box_ui_atlas_t *atlas = box_ui_atlas_of(world, p->e);
    box_ui_extent_t extent;
    box_ui_text_run(world, atlas, text, box_ui_text_wrap_width(p), 0, &extent);

    TextImpl *impl = ecs_get_mut(world, p->e, TextImpl);
    if (impl) {
        impl->width = extent.width;
        impl->height = extent.height;
    }

    p->content_width = extent.width;
    p->content_height = extent.height;
    if (p->fill_width <= 0 && (!p->has_layout || p->layout.width <= 0)) {
        p->width = extent.width;
    }

    if (p->fill_height <= 0 && (!p->has_layout || p->layout.height <= 0)) {
        p->height = extent.height;
    }

done:
    return;
}

/** Fingerprint of everything a build of one element draws.
 *
 * @param p The place of the node
 * @param text The Text of the element
 * @param atlas The atlas of the element
 * @param color The color the glyphs are drawn in
 * @return The fingerprint
 */
static uint64_t box_ui_text_print(
    const UiPlace *p,
    const Text *text,
    const box_ui_atlas_t *atlas,
    Rgb color,
    const TerminalColors *terminal)
{
    const char *value = text->value ? text->value : "";
    float box[3] = { p->width, p->height, box_ui_text_wrap_width(p) };
    uint64_t hash = box_ui_hash(BOX_UI_HASH_SEED, value, (int32_t)strlen(value));

    hash = box_ui_hash(hash, &text->line_height, (int32_t)sizeof(float));
    hash = box_ui_hash(hash, &text->align, (int32_t)sizeof(Align));
    hash = box_ui_hash(hash, &atlas, (int32_t)sizeof(box_ui_atlas_t*));
    hash = box_ui_hash(hash, &atlas->epoch, (int32_t)sizeof(uint32_t));
    hash = box_ui_hash(hash, box, (int32_t)sizeof(box));
    hash = box_ui_hash(hash, &p->scroll, (int32_t)sizeof(Scroll));
    hash = box_ui_hash(hash, &color, (int32_t)sizeof(Rgb));
    if (terminal) {
        hash = box_ui_hash(hash, terminal, (int32_t)sizeof(TerminalColors));
    }

    return hash;
}

/** Move a coordinate of a tree onto the device pixel grid of its view.
 *
 * @param value The coordinate in logical pixels
 * @param device Device pixels one logical pixel covers
 * @return The coordinate of the nearest device pixel
 */
static float box_ui_text_snap(
    float value,
    float device)
{
    return roundf(value * device) / device;
}

/** Give one glyph element the rectangle, the region and the place of its quad.
 *
 * The bitmap of a glyph holds one texel per device pixel, so the quad is as
 * wide as its bitmap divided by the device scale and its top left corner is
 * snapped to a device pixel: the quad then covers whole pixels and samples one
 * texel per pixel, which is what keeps the glyph as sharp as it was rasterized.
 *
 * @param world The world
 * @param e The glyph element
 * @param p The place of the text node
 * @param atlas The atlas the glyph comes from
 * @param quad The quad the layout placed
 */
static void box_ui_text_quad(
    ecs_world_t *world,
    ecs_entity_t e,
    const UiPlace *p,
    const box_ui_atlas_t *atlas,
    const box_ui_quad_t *quad)
{
    const box_ui_glyph_t *glyph = quad->glyph;
    float edge = (float)atlas->edge;
    float device = atlas->device > 0 ? atlas->device : 1.0f;
    float width = (float)glyph->width / device;
    float height = (float)glyph->height / device;
    float scroll_x = p->has_scroll && p->scroll.x != OverflowVisible ?
        p->scroll.offset.x : 0;
    float scroll_y = p->has_scroll && p->scroll.y != OverflowVisible ?
        p->scroll.offset.y : 0;
    float left = box_ui_text_snap(
        p->x - p->width * 0.5f + quad->x - scroll_x, device);
    float top = box_ui_text_snap(
        p->y + p->height * 0.5f - quad->y + scroll_y, device);
    float x = left + width * 0.5f;
    float y = top - height * 0.5f;
    Image region = {
        .u0 = (float)glyph->x / edge,
        .v0 = 1.0f - (float)(glyph->y + glyph->height) / edge,
        .u1 = (float)(glyph->x + glyph->width) / edge,
        .v1 = 1.0f - (float)glyph->y / edge
    };

    Position *pos = ecs_get_mut(world, e, Position);
    if (!pos) {
        ecs_set(world, e, Position, { x - p->x, y - p->y, 0 });
    } else if (pos->x != x - p->x || pos->y != y - p->y) {
        pos->x = x - p->x;
        pos->y = y - p->y;
        ecs_modified(world, e, Position);
    }

    Rect *rect = ecs_get_mut(world, e, Rect);
    const Image *shown = ecs_get(world, e, Image);
    bool same = rect && shown && rect->width == width && rect->height == height &&
        shown->u0 == region.u0 && shown->v0 == region.v0 &&
        shown->u1 == region.u1 && shown->v1 == region.v1;
    if (!same) {
        if (rect) {
            rect->width = width;
            rect->height = height;
        } else {
            ecs_set(world, e, Rect, { width, height });
        }

        ecs_set_ptr(world, e, Image, &region);
    }

    Bounds *bounds = ecs_get_mut(world, e, Bounds);
    if (!bounds) {
        ecs_set(world, e, Bounds, { x, y, width, height });
    } else {
        bounds->x = x;
        bounds->y = y;
        bounds->width = width;
        bounds->height = height;
    }
}

/** Collect the glyph elements a former build left under a text node.
 *
 * @param world The world
 * @param e The text node
 * @param out Vector of ecs_entity_t that receives the glyph elements
 */
static void box_ui_text_reuse(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_vec_t *out)
{
    ecs_vec_clear(out);
    box_hierarchy_collect(world, e, out);

    int32_t i, kept = 0, count = ecs_vec_count(out);
    for (i = 0; i < count; i ++) {
        ecs_entity_t child = ecs_vec_get_t(out, ecs_entity_t, i)[0];
        if (ecs_has_id(world, child, UiGlyph)) {
            ecs_vec_get_t(out, ecs_entity_t, kept)[0] = child;
            kept ++;
        }
    }

    ecs_vec_set_count_t(NULL, out, ecs_entity_t, kept);
}

/** Give one text node the glyph elements of the quads the run placed.
 *
 * @param world The world
 * @param p The place of the text node
 * @param atlas The atlas of the element
 * @param color The color the glyphs are drawn in
 */
static void box_ui_text_element(
    ecs_world_t *world,
    const UiPlace *p,
    box_ui_atlas_t *atlas,
    Rgb color)
{
    UiState *state = box_ui_state(world);
    ecs_vec_t *have = &state->nodes;
    int32_t want = ecs_vec_count(&box_ui_fonts(world)->quads);

    box_ui_text_reuse(world, p->e, have);
    int32_t i, count = ecs_vec_count(have);
    for (i = want; i < count; i ++) {
        ecs_delete(world, ecs_vec_get_t(have, ecs_entity_t, i)[0]);
    }

    for (i = 0; i < want; i ++) {
        const box_ui_quad_t *quad = ecs_vec_get_t(
            &box_ui_fonts(world)->quads, box_ui_quad_t, i);
        ecs_entity_t e;

        if (i < count) {
            e = ecs_vec_get_t(have, ecs_entity_t, i)[0];
        } else {
            e = ecs_new_w_pair(world, EcsChildOf, p->e);
            ecs_add_id(world, e, UiGlyph);
            ecs_add_id(world, e, UiQuad);
            ecs_add_pair(world, e, EcsIsA, atlas->prefab);
        }

        Rgb glyph_color = box_ui_console_color(
            world, p->e, quad->byte, color);
        const Emissive *shown = ecs_get(world, e, Emissive);
        if (!shown || shown->strength != 1.0f || shown->color.r != glyph_color.r ||
            shown->color.g != glyph_color.g || shown->color.b != glyph_color.b)
        {
            ecs_set(world, e, Emissive, {
                .strength = 1.0f, .color = glyph_color });
        }

        box_ui_text_quad(world, e, p, atlas, quad);
    }

    if (want != count) {
        box_ui_state(world)->respawn = true;
    }
}

void box_ui_text_build(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    if (!state->texts) {
        goto done;
    }

    box_ui_atlas_upload(world);

    int32_t i, count = ecs_vec_count(&state->places);
    for (i = 0; i < count; i ++) {
        UiPlace place = ecs_vec_get_t(&box_ui_state(world)->places, UiPlace, i)[0];
        if (!place.has_text) {
            continue;
        }

        const Text *text = ecs_get(world, place.e, Text);
        box_ui_atlas_t *atlas = box_ui_atlas_of(world, place.e);
        const Rgb *rgb = ecs_get(world, place.e, Rgb);
        const TerminalColors *terminal = ecs_get(world, place.e, TerminalColors);
        Rgb color = rgb ? rgb[0] : (Rgb){ 255, 255, 255 };
        if (!text || atlas->failed || !atlas->prefab) {
            continue;
        }

        uint64_t hash = box_ui_text_print(&place, text, atlas, color, terminal);
        const TextImpl *impl = ecs_get(world, place.e, TextImpl);
        if (impl && impl->hash == hash) {
            continue;
        }

        box_ui_extent_t extent;
        box_ui_text_run(world, atlas, text, box_ui_text_wrap_width(&place),
            place.width, &extent);
        box_ui_text_element(world, &place, atlas, color);

        TextImpl *written = ecs_get_mut(world, place.e, TextImpl);
        if (written) {
            written->hash = hash;
        }
    }

done:
    return;
}

/** Text observer: count the elements so a world without text pays nothing.
 *
 * @param it Iterator over the elements that gained or lost their string
 */
static void box_ui_text_on_tree(
    ecs_iter_t *it)
{
    UiState *state = box_ui_state(it->world);

    state->texts += it->event == EcsOnAdd ? it->count : -it->count;
}

/** Text observer: an element that lost its string loses its glyphs with it.
 *
 * @param it Iterator over the elements that lost their string
 */
static void box_ui_text_on_remove(
    ecs_iter_t *it)
{
    if (ecs_is_fini(it->world) || ecs_should_quit(it->world)) {
        goto done;
    }

    if (!it->other_table || !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    ecs_vec_t glyphs;
    ecs_vec_init_t(NULL, &glyphs, ecs_entity_t, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_ui_text_reuse(it->world, it->entities[i], &glyphs);
        int32_t g, count = ecs_vec_count(&glyphs);
        for (g = 0; g < count; g ++) {
            ecs_delete(it->stage, ecs_vec_get_t(&glyphs, ecs_entity_t, g)[0]);
        }
    }

    ecs_vec_fini_t(NULL, &glyphs, ecs_entity_t);
done:
    return;
}

void box_ui_text_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Text), .inout = EcsInOutNone }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_ui_text_on_tree
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Text), .inout = EcsInOutNone }},
        .events = { EcsOnRemove },
        .callback = box_ui_text_on_remove
    });
}
