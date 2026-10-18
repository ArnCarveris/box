/**
 * @file ui/text_edit.c
 * @brief TextEdit: a single line text field edited per key and pointer event, its private parts
 * (content, text, selection, caret) and the sync that draws them and keeps the caret in view.
 */

#include "ui.h"
#include <math.h>
#include <string.h>

ECS_COMPONENT_DECLARE(TextEditState);

/** Seconds between two presses that count as one double or triple click */
#define BOX_UI_EDIT_CLICK_TIME (0.5)

/** Points two presses of one double or triple click may lie apart */
#define BOX_UI_EDIT_CLICK_DIST (4.0f)

/** Width of the caret in logical pixels */
#define BOX_UI_EDIT_CARET_WIDTH (1.0f)

/** Em size of a new TextEdit */
#define BOX_UI_EDIT_SIZE_DEFAULT (16.0f)

/** Line height of a TextEdit whose font is not loaded yet, in ems */
#define BOX_UI_EDIT_LINE_GUESS (1.25f)

/** The character a password shows for each character, U+2022 */
#define BOX_UI_EDIT_BULLET "\xE2\x80\xA2"

/** Bytes of BOX_UI_EDIT_BULLET */
#define BOX_UI_EDIT_BULLET_SIZE (3)

/** Position z of the parts inside the content element, the draw order of siblings */
#define BOX_UI_EDIT_Z_SELECTION (1.0f)
#define BOX_UI_EDIT_Z_TEXT (2.0f)
#define BOX_UI_EDIT_Z_CARET (3.0f)

/** Word classes of box_ui_char_t */
#define BOX_UI_EDIT_SPACE (0)
#define BOX_UI_EDIT_WORD (1)
#define BOX_UI_EDIT_OTHER (2)

#ifdef __APPLE__
/** macOS moves by word with Alt and takes shortcuts with Cmd */
#define BOX_UI_EDIT_MAC (true)
#else
/** Other platforms move by word and take shortcuts with Ctrl */
#define BOX_UI_EDIT_MAC (false)
#endif

/** A TextEdit being edited by one event */
typedef struct box_ui_edit_t {
    ecs_entity_t e;        /**< The entity */
    const TextEdit *te;    /**< Its TextEdit */
    TextEditState *s;      /**< Its parts */
    TextEditStatus status; /**< Caret and anchor, written back when the edit ends */
    const char *value;     /**< TextEdit.value, never NULL */
    int32_t length;        /**< Bytes of value */
} box_ui_edit_t;

/** Construct TextEdit values with the default size and font.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info, whose hooks ctx is the default font
 */
static void box_ui_text_edit_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    const ecs_entity_t *font = ti->hooks.ctx;
    TextEdit *values = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        values[i] = (TextEdit){ .size = BOX_UI_EDIT_SIZE_DEFAULT, .font = *font };
    }
}

/** Free the context of the TextEdit constructor.
 *
 * @param ctx The default font
 */
static void box_ui_text_edit_ctor_free(
    void *ctx)
{
    ecs_os_free(ctx);
}

/** Get the frame clock, which advances by the delta time of every frame.
 *
 * @param world The world
 * @return Seconds since the world started
 */
static double box_ui_edit_now(
    const ecs_world_t *world)
{
    return (double)ecs_get_world_info(world)->world_time_total;
}

/** Find the start of the character after a byte, as box_utf8_next decodes it.
 *
 * @param str The string
 * @param length Bytes of the string
 * @param at The byte, at the start of a character
 * @return The first byte of the next character, the length at the end
 */
static int32_t box_ui_edit_next(
    const char *str,
    int32_t length,
    int32_t at)
{
    const char *ptr = &str[at];
    if (at < length) {
        box_utf8_next(&ptr);
        at = (int32_t)(ptr - str);
    }

    return at < length ? at : length;
}

/** Find the start of the character before a byte, decoding from the start of the string since
 * only a forward read finds the maximal subparts of an ill formed sequence.
 *
 * @param str The string
 * @param length Bytes of the string
 * @param at The byte, at the start of a character
 * @return The first byte of the character before it, 0 at the start
 */
static int32_t box_ui_edit_prev(
    const char *str,
    int32_t length,
    int32_t at)
{
    int32_t result = 0, next = 0;
    while (next < at) {
        result = next;
        next = box_ui_edit_next(str, length, next);
    }

    return result;
}

/** Move a byte offset to the start of the character it is in, clamped to the string.
 *
 * @param str The string
 * @param length Bytes of the string
 * @param at The byte offset
 * @return The start of the character at or before it, the length past the end
 */
static int32_t box_ui_edit_floor(
    const char *str,
    int32_t length,
    int32_t at)
{
    int32_t result = 0, next = 0;
    if (at >= length) {
        result = length;
    } else {
        while (next <= at) {
            result = next;
            next = box_ui_edit_next(str, length, next);
        }
    }

    return result;
}

/** Count the characters of a byte range.
 *
 * @param str The string
 * @param length Bytes of the string
 * @param from First byte, at the start of a character
 * @param to Byte after the range
 * @return Number of characters
 */
static int32_t box_ui_edit_count(
    const char *str,
    int32_t length,
    int32_t from,
    int32_t to)
{
    int32_t result = 0;
    while (from < to) {
        from = box_ui_edit_next(str, length, from);
        result ++;
    }

    return result;
}

/** Get the word class of a code point: spaces, letters and digits (ASCII and every code point
 * above U+007F), or other characters.
 *
 * @param code The code point
 * @return BOX_UI_EDIT_SPACE, BOX_UI_EDIT_WORD or BOX_UI_EDIT_OTHER
 */
static int32_t box_ui_edit_kind(
    int32_t code)
{
    int32_t result = BOX_UI_EDIT_OTHER;
    if (code <= ' ') {
        result = BOX_UI_EDIT_SPACE;
    } else if (code > 0x7F || (code >= '0' && code <= '9') || (code >= 'a' && code <= 'z') ||
        (code >= 'A' && code <= 'Z'))
    {
        result = BOX_UI_EDIT_WORD;
    }

    return result;
}

/** Decode the characters of a string with their word classes.
 *
 * @param chars Receives the box_ui_char_t of the characters and the end, whose offset is the
 * length of the string
 * @param str The string
 * @return Number of characters
 */
static int32_t box_ui_edit_decode(
    ecs_vec_t *chars,
    const char *str)
{
    const char *p = str;
    int32_t result = 0;
    ecs_vec_clear(chars);
    while (p[0]) {
        box_ui_char_t *c = ecs_vec_append_t(NULL, chars, box_ui_char_t);
        c->offset = (int32_t)(p - str);
        c->kind = box_ui_edit_kind(box_utf8_next(&p));
        result ++;
    }

    ecs_vec_append_t(NULL, chars, box_ui_char_t)[0] =
        (box_ui_char_t){ .offset = (int32_t)(p - str), .kind = BOX_UI_EDIT_SPACE };

    return result;
}

/** Find the character of a decoded string that a byte offset is in.
 *
 * @param chars The decoded characters with the end
 * @param count Number of characters
 * @param at The byte offset
 * @return Its index, count at or past the end
 */
static int32_t box_ui_edit_index(
    const box_ui_char_t *chars,
    int32_t count,
    int32_t at)
{
    int32_t result = 0;
    while (result < count && chars[result + 1].offset <= at) {
        result ++;
    }

    return result;
}

/** Find where a word move goes in a string: past the spaces next to the offset, then past one
 * run of word characters or of other characters. Decodes forward from the start, without
 * allocating, since only a forward read finds the maximal subparts of ill formed sequences.
 *
 * @param str The string
 * @param at The byte offset, taken as the start of the character it is in
 * @param forward Whether the move goes right, to the end of the run
 * @return The byte offset the move ends at
 */
static int32_t box_ui_edit_word(
    const char *str,
    int32_t at,
    bool forward)
{
    const char *p = str, *next = str;
    int32_t kind = -1, prev = -1, start = 0, result = 0;
    while (p[0]) {
        next = p;
        kind = box_ui_edit_kind(box_utf8_next(&next));
        if ((int32_t)(next - str) > at) {
            break;
        }

        start = kind != prev ? (int32_t)(p - str) : start;
        result = kind != BOX_UI_EDIT_SPACE ? start : result;
        prev = kind;
        p = next;
    }

    if (forward) {
        for (prev = BOX_UI_EDIT_SPACE; p[0] && (kind == prev || prev == BOX_UI_EDIT_SPACE);) {
            prev = prev == BOX_UI_EDIT_SPACE ? kind : prev;
            p = next;
            kind = p[0] ? box_ui_edit_kind(box_utf8_next(&next)) : -1;
        }

        result = (int32_t)(p - str);
    }

    return result;
}

int32_t box_ui_word_move(
    const char *text,
    int32_t offset,
    bool forward)
{
    return box_ui_edit_word(text ? text : "", offset, forward);
}

/** Find where a word move of an edit goes; a password moves to its start or end.
 *
 * @param st The module state, whose scratch decodes the value
 * @param ed The edit
 * @param at The byte offset
 * @param forward Whether the move goes right
 * @return The byte offset the move ends at
 */
static int32_t box_ui_edit_word_move(
    box_ui_state_t *st,
    const box_ui_edit_t *ed,
    int32_t at,
    bool forward)
{
    int32_t result = forward ? ed->length : 0;
    (void)st;
    if (!ed->te->password) {
        result = box_ui_edit_word(ed->value, at, forward);
    }

    return result;
}

/** Find the run of one word class around the character at a byte offset, spaces included, the
 * selection of a double click. A password selects everything.
 *
 * @param st The module state
 * @param ed The edit
 * @param at Byte offset of the character, the length for the last one
 * @param range Receives the byte range of the run
 */
static void box_ui_edit_word_at(
    box_ui_state_t *st,
    const box_ui_edit_t *ed,
    int32_t at,
    int32_t *range)
{
    int32_t count = box_ui_edit_decode(&st->edit_chars, ed->value);
    const box_ui_char_t *c = ecs_vec_first_t(&st->edit_chars, box_ui_char_t);
    int32_t i = box_ui_edit_index(c, count, at), first, last;
    i = i >= count && count > 0 ? count - 1 : i;
    first = last = i;
    if (ed->te->password) {
        first = 0;
        last = count;
    } else if (count) {
        while (first > 0 && c[first - 1].kind == c[i].kind) {
            first --;
        }

        while (last < count && c[last].kind == c[i].kind) {
            last ++;
        }
    }

    range[0] = c[first].offset;
    range[1] = c[last].offset;
}

/** Get the offset in the shown text of a byte offset of the value.
 *
 * @param ed The edit
 * @param at Byte offset in the value
 * @return Byte offset in what the Text part shows
 */
static int32_t box_ui_edit_shown(
    const box_ui_edit_t *ed,
    int32_t at)
{
    int32_t result = at;
    if (!ed->length) {
        result = 0;
    } else if (ed->te->password) {
        result = box_ui_edit_count(ed->value, ed->length, 0, at) * BOX_UI_EDIT_BULLET_SIZE;
    }

    return result;
}

/** Get the byte offset of the value at an offset of the shown text.
 *
 * @param ed The edit
 * @param at Byte offset in what the Text part shows
 * @return Byte offset in the value, at the start of a character
 */
static int32_t box_ui_edit_unshown(
    const box_ui_edit_t *ed,
    int32_t at)
{
    int32_t result = 0, chars;
    if (!ed->length) {
        result = 0;
    } else if (ed->te->password) {
        for (chars = at / BOX_UI_EDIT_BULLET_SIZE; chars > 0 && result < ed->length; chars --) {
            result = box_ui_edit_next(ed->value, ed->length, result);
        }
    } else {
        result = box_ui_edit_floor(ed->value, ed->length, at);
    }

    return result;
}

/** Create a part of a TextEdit inside its content element.
 *
 * @param world The world
 * @param content The content element
 * @param z Position z, which orders the parts
 * @return The part
 */
static ecs_entity_t box_ui_edit_part(
    ecs_world_t *world,
    ecs_entity_t content,
    float z)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, content);
    ecs_add_id(world, result, PickThrough);
    ecs_set(world, result, Position, {0, 0, z});

    return result;
}

/** Get the parts of a TextEdit, building them and the Layout and Cursor of the entity first
 * when it has none yet.
 *
 * @param world The world, not deferred
 * @param e The entity with a TextEdit
 * @return Its parts
 */
static TextEditState* box_ui_edit_setup(
    ecs_world_t *world,
    ecs_entity_t e)
{
    TextEditState *result = ecs_get_mut(world, e, TextEditState);
    const TextEdit *te = ecs_get(world, e, TextEdit);
    TextEditState s = {0};
    float size;
    ecs_entity_t font;
    if (result || !te) {
        goto done;
    }

    size = te->size;
    font = te->font;
    if (!ecs_has(world, e, Layout)) {
        ecs_set(world, e, Layout, {0});
        s.added_layout = true;
    }

    if (!ecs_has(world, e, Cursor)) {
        ecs_set(world, e, Cursor, { .shape = CursorText });
        s.added_cursor = true;
    }

    s.content = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_add_id(world, s.content, PickThrough);
    ecs_add_id(world, s.content, ClipChildren);
    ecs_set(world, s.content, Position, {0, 0, 0});
    ecs_set(world, s.content, LayoutItem, {
        .width = { .mode = SizeModeGrow, .value = 1 },
        .height = { .mode = SizeModeFixed, .value = size * BOX_UI_EDIT_LINE_GUESS },
        .v = VAlignMiddle
    });
    s.selection = box_ui_edit_part(world, s.content, BOX_UI_EDIT_Z_SELECTION);
    ecs_set(world, s.selection, Rect, {0, 0});
    s.text = box_ui_edit_part(world, s.content, BOX_UI_EDIT_Z_TEXT);
    ecs_set(world, s.text, Text, { .value = "", .size = size, .font = font });
    s.caret = box_ui_edit_part(world, s.content, BOX_UI_EDIT_Z_CARET);
    ecs_set(world, s.caret, Rect, {0, 0});
    ecs_set_ptr(world, e, TextEditState, &s);
    result = ecs_get_mut(world, e, TextEditState);
done:
    return result;
}

/** Start an edit of a TextEdit, building its parts when it has none.
 *
 * @param world The world, not deferred
 * @param e The entity
 * @param ed Receives the edit
 * @return False when the entity has no TextEdit
 */
static bool box_ui_edit_begin(
    ecs_world_t *world,
    ecs_entity_t e,
    box_ui_edit_t *ed)
{
    const TextEditStatus *status;
    bool result = false;
    ecs_os_zeromem(ed);
    if (!e || !ecs_is_alive(world, e) || !ecs_has(world, e, TextEdit)) {
        goto done;
    }

    ed->s = box_ui_edit_setup(world, e);
    ed->te = ecs_get(world, e, TextEdit);
    status = ecs_get(world, e, TextEditStatus);
    if (!ed->s || !ed->te) {
        goto done;
    }

    ed->e = e;
    ed->status = status ? *status : (TextEditStatus){0};
    ed->value = ed->te->value ? ed->te->value : "";
    ed->length = (int32_t)strlen(ed->value);
    result = true;
done:
    return result;
}

/** Set the color of a part when it differs.
 *
 * @param world The world
 * @param e The part
 * @param color The color
 */
static void box_ui_edit_color(
    ecs_world_t *world,
    ecs_entity_t e,
    Rgb color)
{
    const Rgb *cur = ecs_get(world, e, Rgb);
    if (!cur || cur->r != color.r || cur->g != color.g || cur->b != color.b) {
        ecs_set_ptr(world, e, Rgb, &color);
    }
}

/** Set the place and size of a part when they differ.
 *
 * @param world The world
 * @param e The part
 * @param x Position x
 * @param y Position y
 * @param z Position z
 * @param size Rect of the part, NULL to leave it
 */
static void box_ui_edit_place(
    ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float y,
    float z,
    const Rect *size)
{
    const Position *p = ecs_get(world, e, Position);
    const Rect *r = size ? ecs_get(world, e, Rect) : NULL;
    if (!p || p->x != x || p->y != y || p->z != z) {
        ecs_set(world, e, Position, {x, y, z});
    }

    if (size && (!r || r->width != size->width || r->height != size->height)) {
        ecs_set_ptr(world, e, Rect, size);
    }
}

/** Write what the Text part shows: the placeholder for an empty value, a bullet per character
 * of a password, else the value; control bytes show as spaces so the text stays on one line.
 *
 * @param world The world
 * @param ed The edit
 */
static void box_ui_edit_show(
    ecs_world_t *world,
    const box_ui_edit_t *ed)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    const char *src = ed->length ? ed->value :
        (ed->te->placeholder ? ed->te->placeholder : "");
    const Text *cur = ecs_get(world, ed->s->text, Text);
    char *shown;
    int32_t i, chars;
    if (ed->length && ed->te->password) {
        for (chars = box_ui_edit_count(ed->value, ed->length, 0, ed->length); chars; chars --) {
            ecs_strbuf_appendstrn(&buf, BOX_UI_EDIT_BULLET, BOX_UI_EDIT_BULLET_SIZE);
        }
    } else {
        ecs_strbuf_appendstr(&buf, src);
    }

    shown = ecs_strbuf_get(&buf);
    shown = shown ? shown : ecs_os_strdup("");
    for (i = 0; shown[i]; i ++) {
        if ((unsigned char)shown[i] < 0x20 || shown[i] == 0x7F) {
            shown[i] = ' ';
        }
    }

    if (!cur || !cur->value || strcmp(cur->value, shown) || cur->size != ed->te->size ||
        cur->font != ed->te->font)
    {
        ecs_set(world, ed->s->text, Text, {
            .value = shown, .size = ed->te->size, .font = ed->te->font });
    }

    ecs_os_free(shown);
}

/** Get the place of the pointer of an event in the content element of a TextEdit.
 *
 * @param world The world
 * @param s The parts
 * @param point The point of the event in the scene
 * @param x Receives x in the content element
 * @return False while the content element has no transform
 */
static bool box_ui_edit_local(
    const ecs_world_t *world,
    const TextEditState *s,
    const float *point,
    float *x)
{
    const WorldTransform *t = ecs_get(world, s->content, WorldTransform);
    if (t) {
        mat4 inv;
        vec3 local;
        glm_mat4_inv(ECS_CONST_CAST(vec4*, t->m), inv);
        glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, point), 1.0f, local);
        *x = local[0];
    }

    return t != NULL;
}

/** Get the caret of the value nearest to a place in the content element.
 *
 * @param world The world
 * @param ed The edit
 * @param x X in the content element
 * @param under Receives the character under the place, the one the caret follows when the place
 * is left of it; may be NULL
 * @return Byte offset of the caret in the value
 */
static int32_t box_ui_edit_hit(
    ecs_world_t *world,
    const box_ui_edit_t *ed,
    float x,
    int32_t *under)
{
    const Position *p = ecs_get(world, ed->s->text, Position);
    float tx = x - (p ? p->x : 0);
    int32_t shown = box_text_hit(world, ed->s->text, tx, 0);
    int32_t result = box_ui_edit_unshown(ed, shown);
    TextCaret c;
    if (under) {
        bool left = box_text_caret(world, ed->s->text, shown, &c) && tx < c.x;
        *under = left && result > 0 ? box_ui_edit_prev(ed->value, ed->length, result) : result;
    }

    return result;
}

/** Move the caret of an edit, restarting the blink.
 *
 * @param world The world
 * @param ed The edit
 * @param caret The new caret
 * @param extend Whether the selection follows; else the anchor moves with the caret
 */
static void box_ui_edit_move(
    const ecs_world_t *world,
    box_ui_edit_t *ed,
    int32_t caret,
    bool extend)
{
    ed->status.caret = caret;
    if (!extend) {
        ed->status.anchor = caret;
    }

    ed->s->blink = box_ui_edit_now(world);
}

/** Follow the pointer of a held left press: move the caret to it, by word after a double
 * click, nothing after a triple click.
 *
 * @param world The world
 * @param st The module state
 * @param ed The edit
 * @param x X of the pointer in the content element
 */
static void box_ui_edit_drag(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_ui_edit_t *ed,
    float x)
{
    int32_t under, run[2], hit = box_ui_edit_hit(world, ed, x, &under);
    int32_t caret = ed->status.caret, anchor = ed->status.anchor;
    if (st->edit_clicks == 1) {
        caret = hit;
    } else if (st->edit_clicks == 2 && hit < st->edit_word[0]) {
        box_ui_edit_word_at(st, ed, under, run);
        anchor = st->edit_word[1];
        caret = run[0];
    } else if (st->edit_clicks == 2 && hit > st->edit_word[1]) {
        box_ui_edit_word_at(st, ed, under, run);
        anchor = st->edit_word[0];
        caret = run[1];
    } else if (st->edit_clicks == 2) {
        anchor = st->edit_word[0];
        caret = st->edit_word[1];
    }

    if (caret != ed->status.caret || anchor != ed->status.anchor) {
        box_ui_edit_move(world, ed, caret, true);
        ed->status.anchor = anchor;
    }
}

/** Draw an edit: the shown text, the scroll that keeps the caret in view with a margin, the
 * caret while focused and in the on phase of its blink, the selection while focused, the line
 * height of the content element and TextEditStatus.
 *
 * Takes in a value set from outside first, keeping the caret and the anchor where they fit.
 *
 * @param world The world, not deferred
 * @param st The module state
 * @param ed The edit
 */
static void box_ui_edit_sync(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_ui_edit_t *ed)
{
    const UiTheme *t = st->theme ? ecs_get(world, st->theme, UiTheme) : NULL;
    UiTheme theme = t ? *t : (UiTheme){0};
    TextEditState *s = ed->s;
    TextEditStatus status = ed->status;
    ecs_entity_t e = ed->e;
    bool focused = st->focused == e, placeholder = !ed->length, on;
    double now = box_ui_edit_now(world);
    TextCaret c0, cc, ca, ce;
    const LayoutBounds *lb;
    const LayoutItem *li;
    const TextEditStatus *cur;
    float w, margin, left, scroll, xc, xa, xe, x0, x1, y, size = ed->te->size;
    if (strcmp(ed->value, s->value ? s->value : "")) {
        ecs_os_free(s->value);
        s->value = ecs_os_strdup(ed->value);
    }

    status.caret = box_ui_edit_floor(ed->value, ed->length, status.caret);
    status.anchor = box_ui_edit_floor(ed->value, ed->length, status.anchor);
    ed->status = status;
    if (focused != s->focused) {
        s->focused = focused;
        s->blink = now;
    }

    box_ui_edit_show(world, ed);
    box_ui_edit_color(world, s->text, placeholder ? theme.text_muted : theme.text);
    box_ui_edit_color(world, s->selection, theme.selection);
    box_ui_edit_color(world, s->caret, theme.caret);
    lb = ecs_get(world, s->content, LayoutBounds);
    w = lb ? lb->width : 0;
    if (st->edit_press == e && focused && w > 0 &&
        (st->edit_pointer < -w * 0.5f || st->edit_pointer > w * 0.5f))
    {
        box_ui_edit_drag(world, st, ed, st->edit_pointer);
        status = ed->status;
    }

    if (!box_text_caret(world, s->text, 0, &c0)) {
        box_ui_edit_place(world, s->caret, 0, 0, BOX_UI_EDIT_Z_CARET, &(Rect){0, 0});
        box_ui_edit_place(world, s->selection, 0, 0, BOX_UI_EDIT_Z_SELECTION, &(Rect){0, 0});
        status.scroll = 0;
        goto publish;
    }

    li = ecs_get(world, s->content, LayoutItem);
    if (li && li->height.value != c0.height) {
        LayoutItem item = *li;
        item.height.value = c0.height;
        ecs_set_ptr(world, s->content, LayoutItem, &item);
    }

    box_text_caret(world, s->text, box_ui_edit_shown(ed, status.caret), &cc);
    box_text_caret(world, s->text, box_ui_edit_shown(ed, status.anchor), &ca);
    box_text_caret(world, s->text, box_ui_edit_shown(ed, ed->length), &ce);
    xc = cc.x - c0.x;
    xa = ca.x - c0.x;
    xe = ce.x - c0.x;
    scroll = status.scroll;
    margin = fminf(size * 0.5f, w * 0.25f);
    if (w > 0) {
        if (xc - scroll > w - margin) {
            scroll = xc - w + margin;
        }

        if (xc - scroll < margin) {
            scroll = xc - margin;
        }

        scroll = fminf(scroll, fmaxf(0, xe - w + margin));
        scroll = fmaxf(scroll, 0);
    } else {
        scroll = 0;
    }

    left = -w * 0.5f - scroll;
    y = cc.y + cc.height * 0.5f;
    on = focused && (theme.caret_blink <= 0 ||
        fmod(now - s->blink, 2.0 * (double)theme.caret_blink) < (double)theme.caret_blink);
    x0 = fminf(xc, xa);
    x1 = fmaxf(xc, xa);
    box_ui_edit_place(world, s->text, left - c0.x, 0, BOX_UI_EDIT_Z_TEXT, NULL);
    box_ui_edit_place(world, s->caret, left + xc, y, BOX_UI_EDIT_Z_CARET,
        &(Rect){ on ? BOX_UI_EDIT_CARET_WIDTH : 0, on ? cc.height : 0 });
    box_ui_edit_place(world, s->selection, left + (x0 + x1) * 0.5f, y, BOX_UI_EDIT_Z_SELECTION,
        focused && x1 > x0 ? &(Rect){ x1 - x0, cc.height } : &(Rect){0, 0});
    status.scroll = scroll;
publish:
    status.focused = focused;
    ed->status = status;
    cur = ecs_get(world, e, TextEditStatus);
    if (!cur || ecs_os_memcmp_t(cur, &status, TextEditStatus)) {
        ecs_set_ptr(world, e, TextEditStatus, &status);
    }
}

/** End an edit: write the caret and the anchor and draw the TextEdit.
 *
 * @param world The world
 * @param st The module state
 * @param ed The edit
 */
static void box_ui_edit_end(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_ui_edit_t *ed)
{
    box_ui_edit_t cur;
    if (box_ui_edit_begin(world, ed->e, &cur)) {
        cur.status.caret = ed->status.caret;
        cur.status.anchor = ed->status.anchor;
        box_ui_edit_sync(world, st, &cur);
    }
}

/** Replace a byte range of the value with text, put the caret after it and report a change.
 *
 * @param world The world
 * @param st The module state
 * @param ed The edit, whose pointers are taken again after the write
 * @param from First byte of the range
 * @param to Byte after the range
 * @param text The text
 * @param size Bytes of the text
 */
static void box_ui_edit_replace(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_ui_edit_t *ed,
    int32_t from,
    int32_t to,
    const char *text,
    int32_t size)
{
    int32_t length = ed->length - (to - from) + size;
    char *value = ecs_os_malloc(length + 1);
    UiChangeEvent evt = { .widget = ed->e };
    TextEditStatus status = ed->status;
    TextEdit *te;
    memcpy(value, ed->value, (size_t)from);
    memcpy(&value[from], text, (size_t)size);
    memcpy(&value[from + size], &ed->value[to], (size_t)(ed->length - to));
    value[length] = '\0';
    ecs_os_free(ed->s->value);
    ed->s->value = ecs_os_strdup(value);
    te = ecs_ensure(world, ed->e, TextEdit);
    ecs_os_free(te->value);
    te->value = value;
    status.caret = status.anchor = from + size;
    ecs_modified(world, ed->e, TextEdit);
    if (box_ui_edit_begin(world, ed->e, ed)) {
        ed->status.caret = status.caret;
        ed->status.anchor = status.anchor;
        ed->s->blink = box_ui_edit_now(world);
    }

    box_ui_deliver(world, st, BoxUiWaitChange, evt.widget, ecs_id(UiChangeEvent), &evt);
}

/** Remove the selection of an edit, or the range from one offset to the caret without one.
 *
 * @param world The world
 * @param st The module state
 * @param ed The edit
 * @param to The other end of the range to remove when nothing is selected
 */
static void box_ui_edit_erase(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_ui_edit_t *ed,
    int32_t to)
{
    int32_t a = ed->status.caret, b = ed->status.anchor;
    if (a == b) {
        b = to;
    }

    if (a != b && !ed->te->read_only) {
        box_ui_edit_replace(world, st, ed, a < b ? a : b, a < b ? b : a, "", 0);
    }
}

/** Insert typed or pasted text at the caret, replacing the selection. Control characters are
 * dropped, newlines (CR LF as one) and tabs become spaces, and max_length cuts the text at a
 * character boundary. Nothing happens when no character is left.
 *
 * @param world The world
 * @param st The module state
 * @param e The entity
 * @param text UTF-8 text
 */
static void box_ui_edit_insert(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t e,
    const char *text)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    box_ui_edit_t ed;
    int32_t from, to, room = -1, size = 0;
    char utf8[4], *insert = NULL;
    if (!text[0] || !box_ui_edit_begin(world, e, &ed) || ed.te->read_only) {
        goto done;
    }

    from = ed.status.caret < ed.status.anchor ? ed.status.caret : ed.status.anchor;
    to = ed.status.caret < ed.status.anchor ? ed.status.anchor : ed.status.caret;
    if (ed.te->max_length > 0) {
        room = ed.te->max_length - box_ui_edit_count(ed.value, ed.length, 0, ed.length) +
            box_ui_edit_count(ed.value, ed.length, from, to);
        room = room > 0 ? room : 0;
    }

    while (text[0] && room != 0) {
        int32_t code = box_utf8_next(&text);
        if (code == '\r' && text[0] == '\n') {
            text ++;
        }

        if (code == '\n' || code == '\r' || code == '\t') {
            code = ' ';
        } else if (code < 0x20 || (code >= 0x7F && code <= 0x9F)) {
            continue;
        }

        ecs_strbuf_appendstrn(&buf, utf8, box_utf8_encode(code, utf8));
        room = room > 0 ? room - 1 : room;
    }

    size = ecs_strbuf_written(&buf);
    insert = ecs_strbuf_get(&buf);
    if (size) {
        box_ui_edit_replace(world, st, &ed, from, to, insert, size);
    }

    box_ui_edit_end(world, st, &ed);
done:
    ecs_os_free(insert);
}

void box_ui_text_edit_input(
    ecs_world_t *world,
    box_ui_state_t *st)
{
    ecs_entity_t e = st->focused;
    if (e && ecs_vec_count(&st->text) && ecs_has(world, e, TextEdit)) {
        box_ui_edit_insert(world, st, e, ecs_vec_first_t(&st->text, char));
    }

    if (e && ecs_vec_count(&st->paste) && st->focused == e && ecs_is_alive(world, e) &&
        ecs_has(world, e, TextEdit))
    {
        box_ui_edit_insert(world, st, e, ecs_vec_first_t(&st->paste, char));
    }
}

/** Put the selection of an edit on the clipboard, unless it is a password.
 *
 * @param world The world
 * @param ed The edit
 * @return Whether text was copied
 */
static bool box_ui_edit_copy(
    ecs_world_t *world,
    const box_ui_edit_t *ed)
{
    int32_t a = ed->status.caret, b = ed->status.anchor;
    int32_t from = a < b ? a : b, to = a < b ? b : a;
    bool result = from != to && !ed->te->password;
    if (result) {
        char *text = flecs_asprintf("%.*s", to - from, &ed->value[from]);
        box_input_clipboard_set(world, text);
        ecs_os_free(text);
    }

    return result;
}

void box_ui_text_edit_key(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t e,
    const UiKeyEvent *evt)
{
    box_ui_edit_t ed;
    UiChangeEvent submit = { .widget = e };
    bool word = BOX_UI_EDIT_MAC ? evt->alt : evt->ctrl;
    bool cmd = BOX_UI_EDIT_MAC ? evt->meta : evt->ctrl;
    bool line = BOX_UI_EDIT_MAC && evt->meta;
    bool extend = evt->shift, selected;
    int32_t caret, from, to;
    if (!evt->down || !box_ui_edit_begin(world, e, &ed)) {
        goto done;
    }

    caret = ed.status.caret;
    selected = ed.status.caret != ed.status.anchor;
    from = caret < ed.status.anchor ? caret : ed.status.anchor;
    to = caret < ed.status.anchor ? ed.status.anchor : caret;
    switch (evt->key) {
    case KeyLeft:
        caret = line ? 0 : word ? box_ui_edit_word_move(st, &ed, caret, false) :
            selected && !extend ? from : box_ui_edit_prev(ed.value, ed.length, caret);
        box_ui_edit_move(world, &ed, caret, extend);
        break;
    case KeyRight:
        caret = line ? ed.length : word ? box_ui_edit_word_move(st, &ed, caret, true) :
            selected && !extend ? to : box_ui_edit_next(ed.value, ed.length, caret);
        box_ui_edit_move(world, &ed, caret, extend);
        break;
    case KeyHome:
    case KeyUp:
        box_ui_edit_move(world, &ed, 0, extend);
        break;
    case KeyEnd:
    case KeyDown:
        box_ui_edit_move(world, &ed, ed.length, extend);
        break;
    case KeyBackspace:
        box_ui_edit_erase(world, st, &ed, word ? box_ui_edit_word_move(st, &ed, caret, false) :
            box_ui_edit_prev(ed.value, ed.length, caret));
        break;
    case KeyDelete:
        box_ui_edit_erase(world, st, &ed, word ? box_ui_edit_word_move(st, &ed, caret, true) :
            box_ui_edit_next(ed.value, ed.length, caret));
        break;
    case KeyA:
        if (cmd) {
            box_ui_edit_move(world, &ed, ed.length, false);
            ed.status.anchor = 0;
        }

        break;
    case KeyC:
        if (cmd) {
            box_ui_edit_copy(world, &ed);
        }

        break;
    case KeyX:
        if (cmd && !ed.te->read_only && box_ui_edit_copy(world, &ed)) {
            box_ui_edit_erase(world, st, &ed, caret);
        }

        break;
    case KeyEscape:
        st->edit_press = 0;
        box_ui_focus_set(world, st, 0, false);
        break;
    case KeyEnter:
    case KeyKpEnter:
        if (!evt->repeat) {
            box_ui_deliver(world, st, BoxUiWaitSubmit, e, ecs_id(UiChangeEvent), &submit);
        }

        break;
    default:
        break;
    }

    box_ui_edit_end(world, st, &ed);
done:
    return;
}

/** Start the selection of a left press on a focused TextEdit: the caret at the hit, a word for
 * the second click, everything for the third, the selection extended with shift.
 *
 * @param world The world
 * @param st The module state
 * @param src The press
 * @param pick The pick of the press
 */
static void box_ui_edit_press(
    ecs_world_t *world,
    box_ui_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick)
{
    box_ui_edit_t ed;
    double now = box_ui_edit_now(world);
    float dx = src->screen_x - st->edit_click_at[0], dy = src->screen_y - st->edit_click_at[1];
    int32_t hit, under;
    float x;
    st->edit_press = 0;
    if (!pick->handler || pick->handler != st->focused ||
        !box_ui_edit_begin(world, pick->handler, &ed) ||
        !box_ui_edit_local(world, ed.s, pick->point, &x))
    {
        goto done;
    }

    if (!src->shift && st->edit_click == ed.e &&
        now - st->edit_click_time <= BOX_UI_EDIT_CLICK_TIME &&
        dx * dx + dy * dy <= BOX_UI_EDIT_CLICK_DIST * BOX_UI_EDIT_CLICK_DIST)
    {
        st->edit_clicks = st->edit_clicks % 3 + 1;
    } else {
        st->edit_clicks = 1;
    }

    st->edit_click = ed.e;
    st->edit_click_time = now;
    st->edit_click_at[0] = src->screen_x;
    st->edit_click_at[1] = src->screen_y;
    st->edit_press = ed.e;
    st->edit_pointer = x;
    hit = box_ui_edit_hit(world, &ed, x, &under);
    if (st->edit_clicks == 1) {
        box_ui_edit_move(world, &ed, hit, src->shift);
    } else if (st->edit_clicks == 2) {
        box_ui_edit_word_at(st, &ed, under, st->edit_word);
        box_ui_edit_move(world, &ed, st->edit_word[1], false);
        ed.status.anchor = st->edit_word[0];
    } else {
        box_ui_edit_move(world, &ed, ed.length, false);
        ed.status.anchor = 0;
    }

    box_ui_edit_end(world, st, &ed);
done:
    return;
}

void box_ui_text_edit_pointer(
    ecs_world_t *world,
    box_ui_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick,
    uint32_t pressed,
    uint32_t released)
{
    box_ui_edit_t ed;
    float x;
    if (pressed & 1u) {
        box_ui_edit_press(world, st, src, pick);
    } else if (st->edit_press && (src->buttons & 1u) && st->edit_press == st->focused &&
        box_ui_edit_begin(world, st->edit_press, &ed) &&
        box_ui_edit_local(world, ed.s, pick->point, &x))
    {
        st->edit_pointer = x;
        box_ui_edit_drag(world, st, &ed, x);
        box_ui_edit_end(world, st, &ed);
    }

    if (released & 1u) {
        st->edit_press = 0;
    }
}

/** Remove what TextEdit added to an entity and its parts when the TextEdit is removed, unless
 * the entity is deleted. The changes go through the stage, the entity is mid table move.
 *
 * @param it Iterator of the observer
 */
static void box_ui_text_edit_on_remove(
    ecs_iter_t *it)
{
    bool deleted = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    if (deleted) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const TextEditState *s = ecs_get(it->world, e, TextEditState);
        ecs_remove_id(it->stage, e, Focusable);
        ecs_remove_id(it->stage, e, KeyCapture);
        ecs_remove(it->stage, e, TextEditStatus);
        if (s && s->added_layout) {
            ecs_remove(it->stage, e, Layout);
        }

        if (s && s->added_cursor) {
            ecs_remove(it->stage, e, Cursor);
        }

        if (s && s->content) {
            ecs_delete(it->stage, s->content);
        }

        ecs_remove(it->stage, e, TextEditState);
    }

done:
    return;
}

/** Build the parts of new TextEdits and draw every TextEdit after the layout ran: values set
 * from outside, the caret blink, the drag past an edge, the theme and the width of the layout.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxUiTextEditUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    box_ui_state_t *st = it->ctx;
    ecs_iter_t qit = ecs_query_iter(world, st->edits);
    box_ui_edit_t ed;
    int32_t i;
    ecs_vec_clear(&st->edit_list);
    while (ecs_query_next(&qit)) {
        for (i = 0; i < qit.count; i ++) {
            ecs_vec_append_t(NULL, &st->edit_list, ecs_entity_t)[0] = qit.entities[i];
        }
    }

    for (i = 0; i < ecs_vec_count(&st->edit_list); i ++) {
        if (box_ui_edit_begin(world, ecs_vec_get_t(&st->edit_list, ecs_entity_t, i)[0], &ed)) {
            box_ui_edit_sync(world, st, &ed);
        }
    }
}

void box_ui_text_edit_import(
    ecs_world_t *world,
    box_ui_state_t *st)
{
    ecs_entity_t *font = ecs_os_malloc_t(ecs_entity_t);
    *font = ecs_lookup(world, "box.text.DefaultFont");
    box_owned_hooks(world, TextEdit,
        BOX_OWNED_STR(TextEdit, value), BOX_OWNED_STR(TextEdit, placeholder));
    ecs_set_hooks(world, TextEdit, {
        .ctor = box_ui_text_edit_ctor,
        .ctx = font,
        .ctx_free = box_ui_text_edit_ctor_free
    });
    ecs_set_hooks(world, TextEditStatus, { .ctor = flecs_default_ctor });
    ECS_COMPONENT_DEFINE(world, TextEditState);
    box_owned_hooks(world, TextEditState, BOX_OWNED_STR(TextEditState, value));
    ecs_add_pair(world, ecs_id(TextEdit), EcsWith, Focusable);
    ecs_add_pair(world, ecs_id(TextEdit), EcsWith, KeyCapture);
    ecs_add_pair(world, ecs_id(TextEdit), EcsWith, ecs_id(TextEditStatus));

    st->edits = ecs_query(world, {
        .terms = {{ .id = ecs_id(TextEdit), .inout = EcsInOutNone }},
        .cache_kind = EcsQueryCacheAuto
    });
    ecs_vec_init_t(NULL, &st->edit_list, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &st->edit_chars, box_ui_char_t, 0);
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextEditRemove" }),
        .query.terms = {{ .id = ecs_id(TextEdit) }},
        .events = { EcsOnRemove },
        .callback = box_ui_text_edit_on_remove
    });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TextEditUpdate" }),
        .phase = EcsPostUpdate,
        .run = BoxUiTextEditUpdate,
        .ctx = st,
        .immediate = true
    });
}
