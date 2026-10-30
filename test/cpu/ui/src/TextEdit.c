/**
 * @file TextEdit.c
 * @brief The TextEdit text field driven through the terminal input commands: typing, editing,
 * caret and word moves, selections by key and pointer, the clipboard, password and read only
 * fields, events, scrolling, KeyCapture and the focus.
 */

#include <box.h>
#include <bake_test.h>
#include <stdarg.h>
#include <string.h>

#ifdef __APPLE__
/** Modifier key of word moves */
#define EDIT_WORD "AltLeft"
/** Modifier key of the shortcuts */
#define EDIT_CMD "SuperLeft"
#else
/** Modifier key of word moves */
#define EDIT_WORD "CtrlLeft"
/** Modifier key of the shortcuts */
#define EDIT_CMD "CtrlLeft"
#endif

/** Seconds of a frame, a fixed step so the frame clock of clicks and the blink is exact */
#define EDIT_DT (1.0f / 60.0f)

/** Frames after which a press no longer counts toward a double click, 0.5 s at 60 fps */
#define EDIT_CLICK_FRAMES (40)

/** Modules, an action bound to H, a screen UI scene with a FocusScope form of two fields and
 * a probe template that counts the changes and submits of its target, and a template that
 * mirrors the value of a TextEdit.
 *
 * The form is a column at the center of the screen, [640, 360]: field above other, both 200
 * points wide unless the scene gives field another LayoutItem. */
#define EDIT_PRELUDE \
    "using flecs.script\n" \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.layout\n" \
    "using box.picking\n" \
    "using box.input\n" \
    "using box.ui\n" \
    "actions {\n" \
    "  hide { Action: {}\n" \
    "    Binding: {key: H}\n" \
    "  }\n" \
    "}\n" \
    "struct Shown(value: string)\n" \
    "template Mirror {\n" \
    "  prop target: entity = 0\n" \
    "  Shown: {value: target[TextEdit].value}\n" \
    "}\n" \
    "template EditProbe {\n" \
    "  prop target: entity = 0\n" \
    "  mut changes: i32 = 0\n" \
    "  mut submits: i32 = 0\n" \
    "  mut widget: entity = 0\n" \
    "  async {\n" \
    "    while true {\n" \
    "      const e = await on.change(target)\n" \
    "      changes = changes + 1\n" \
    "      widget = e.widget\n" \
    "    }\n" \
    "  }\n" \
    "  async {\n" \
    "    while true {\n" \
    "      const e = await on.submit(target)\n" \
    "      submits = submits + 1\n" \
    "      widget = e.widget\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "hud {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "}\n" \
    "hud_view {\n" \
    "  View: {camera: hud.camera}\n" \
    "}\n"

/** The muts of EditProbe */
typedef struct {
    int32_t changes;     /**< Times on.change resolved */
    int32_t submits;     /**< Times on.submit resolved */
    ecs_entity_t widget; /**< Widget of the last event */
} edit_probe_t;

/** The parts the engine built inside a TextEdit */
typedef struct {
    ecs_entity_t content;   /**< The ClipChildren element */
    ecs_entity_t text;      /**< The Text */
    ecs_entity_t selection; /**< The selection Rect */
    ecs_entity_t caret;     /**< The caret Rect */
} edit_parts_t;

/** Create a headless world with a form whose first field has the given components, and run
 * frames until the font is loaded and the form is laid out.
 *
 * @param field Script code inside the first field, such as its TextEdit
 * @return The world
 */
static ecs_world_t* edit_world(
    const char *field)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *code = flecs_asprintf("%s"
        "hud {\n"
        "  form {\n"
        "    Layout: {direction: Column, spacing: 20}\n"
        "    FocusScope\n"
        "    field {\n"
        "      LayoutItem: {width: {mode: Fixed, value: 200}}\n"
        "      %s\n"
        "    }\n"
        "    other {\n"
        "      LayoutItem: {width: {mode: Fixed, value: 200}}\n"
        "      TextEdit: {value: \"other\"}\n"
        "    }\n"
        "  }\n"
        "}\n"
        "probe { EditProbe: {target: hud.form.field} }\n"
        "mirror { Mirror: {target: hud.form.field} }\n",
        EDIT_PRELUDE, field);
    test_assert(ecs_script(world, { .code = code }) != 0);
    ecs_os_free(code);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, EDIT_DT);
    }

    return world;
}

/** Run a terminal command and the frames until the input queue is drained, plus one.
 *
 * @param world The world
 * @param s The terminal session
 * @param fmt Format of the command
 * @param ... Arguments of the format
 */
static void edit_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *fmt,
    ...)
{
    va_list args;
    va_start(args, fmt);
    char *cmd = flecs_vasprintf(fmt, args);
    va_end(args);
    bool error = true;
    char *out = box_command_run(world, s, cmd, &error);
    if (error) {
        printf("command '%s' failed: %s\n", cmd, out);
    }

    test_bool(error, false);
    ecs_os_free(out);
    ecs_os_free(cmd);
    while (box_input_pending(world)) {
        ecs_progress(world, EDIT_DT);
    }

    ecs_progress(world, EDIT_DT);
}

/** Press a key while a modifier is held.
 *
 * @param world The world
 * @param s The terminal session
 * @param mod The modifier key
 * @param key The key
 */
static void edit_chord(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *mod,
    const char *key)
{
    edit_run(world, s, "key_down %s", mod);
    edit_run(world, s, "key_press %s", key);
    edit_run(world, s, "key_up %s", mod);
}

/** Press a key a number of times.
 *
 * @param world The world
 * @param s The terminal session
 * @param key The key
 * @param count Number of presses
 */
static void edit_keys(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *key,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        edit_run(world, s, "key_press %s", key);
    }
}

/** Run frames so that the next press does not count toward a double click.
 *
 * @param world The world
 */
static void edit_wait(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < EDIT_CLICK_FRAMES; i ++) {
        ecs_progress(world, EDIT_DT);
    }
}

/** Get an entity by path.
 *
 * @param world The world
 * @param path The path
 * @return The entity
 */
static ecs_entity_t edit_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the first field.
 *
 * @param world The world
 * @return The entity
 */
static ecs_entity_t edit_field(
    ecs_world_t *world)
{
    return edit_e(world, "hud.form.field");
}

/** Get the parts of a TextEdit.
 *
 * @param world The world
 * @param e The entity
 * @return The parts, which must all exist
 */
static edit_parts_t edit_parts(
    ecs_world_t *world,
    ecs_entity_t e)
{
    edit_parts_t result = {0};
    int32_t i;
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            if (ecs_has_id(world, it.entities[i], ClipChildren)) {
                result.content = it.entities[i];
            }
        }
    }

    test_assert(result.content != 0);
    it = ecs_children(world, result.content);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t c = it.entities[i];
            const Position *p = ecs_get(world, c, Position);
            if (ecs_has(world, c, Text)) {
                result.text = c;
            } else if (p && p->z < 2) {
                result.selection = c;
            } else {
                result.caret = c;
            }
        }
    }

    test_assert(result.text != 0);
    test_assert(result.selection != 0);
    test_assert(result.caret != 0);

    return result;
}

/** Get the status of the first field.
 *
 * @param world The world
 * @return Its TextEditStatus
 */
static TextEditStatus edit_status(
    ecs_world_t *world)
{
    const TextEditStatus *result = ecs_get(world, edit_field(world), TextEditStatus);
    test_assert(result != NULL);

    return *result;
}

/** Get the value of the first field.
 *
 * @param world The world
 * @return Its TextEdit.value, never NULL
 */
static const char* edit_value(
    ecs_world_t *world)
{
    const TextEdit *te = ecs_get(world, edit_field(world), TextEdit);
    test_assert(te != NULL);

    return te->value ? te->value : "";
}

/** Check the caret and the anchor of the first field.
 *
 * @param world The world
 * @param anchor The expected anchor
 * @param caret The expected caret
 */
static void edit_expect(
    ecs_world_t *world,
    int32_t anchor,
    int32_t caret)
{
    TextEditStatus st = edit_status(world);
    if (st.anchor != anchor || st.caret != caret) {
        printf("expected anchor %d caret %d, got anchor %d caret %d\n",
            anchor, caret, st.anchor, st.caret);
    }

    test_int(st.anchor, anchor);
    test_int(st.caret, caret);
}

/** Get the muts of the probe.
 *
 * @param world The world
 * @return The muts
 */
static const edit_probe_t* edit_probe(
    ecs_world_t *world)
{
    const edit_probe_t *result = ecs_get_id(
        world, edit_e(world, "probe"), edit_e(world, "EditProbe.mut"));
    test_assert(result != NULL);

    return result;
}

/** Get the value the Mirror template of the mirror entity read from the first field.
 *
 * @param world The world
 * @return The value of its Shown component
 */
static const char* edit_shown(
    ecs_world_t *world)
{
    char *const *result = ecs_get_id(world, edit_e(world, "mirror"), edit_e(world, "Shown"));
    test_assert(result != NULL);

    return result[0] ? result[0] : "";
}

/** Set the value of the first field from C, as an application does.
 *
 * @param world The world
 * @param value The new value
 */
static void edit_set(
    ecs_world_t *world,
    const char *value)
{
    ecs_entity_t e = edit_field(world);
    TextEdit *te = ecs_get_mut(world, e, TextEdit);
    test_assert(te != NULL);
    ecs_os_free(te->value);
    te->value = ecs_os_strdup(value);
    ecs_modified(world, e, TextEdit);
    ecs_progress(world, EDIT_DT);
}

/** Get the window point of a byte offset of what the first field shows, on its middle line.
 *
 * @param world The world
 * @param offset Byte offset in the shown text
 * @param dx Points added to the x of the caret there
 * @param out Receives the point as [x, y]
 * @param size Bytes of out
 */
static void edit_point(
    ecs_world_t *world,
    int32_t offset,
    float dx,
    char *out,
    size_t size)
{
    edit_parts_t parts = edit_parts(world, edit_field(world));
    const WorldTransform *t = ecs_get(world, parts.content, WorldTransform);
    const Position *p = ecs_get(world, parts.text, Position);
    TextCaret c;
    test_assert(t != NULL);
    test_assert(p != NULL);
    test_assert(box_text_caret(world, parts.text, offset, &c));
    float x = 640 + t->m[3][0] + p->x + c.x + dx;
    float y = 360 - t->m[3][1];
    ecs_os_snprintf(out, (ecs_size_t)size, "[%.2f, %.2f]", (double)x, (double)y);
}

/** Get the x of the caret at a byte offset of the shown text in the text part.
 *
 * @param world The world
 * @param offset The byte offset
 * @return The x of the caret
 */
static float edit_caret_x(
    ecs_world_t *world,
    int32_t offset)
{
    edit_parts_t parts = edit_parts(world, edit_field(world));
    TextCaret c;
    test_assert(box_text_caret(world, parts.text, offset, &c));

    return c.x;
}

/** Click the first field at a byte offset of what it shows.
 *
 * @param world The world
 * @param s The terminal session
 * @param offset Byte offset in the shown text
 * @param dx Points added to the x of the caret there
 */
static void edit_click(
    ecs_world_t *world,
    ecs_entity_t s,
    int32_t offset,
    float dx)
{
    char point[64];
    edit_point(world, offset, dx, point, sizeof(point));
    edit_run(world, s, "mouse_click left %s", point);
}

/** Focus the first field with a click past its text, which puts the caret at the end.
 *
 * @param world The world
 * @param s The terminal session
 */
static void edit_focus(
    ecs_world_t *world,
    ecs_entity_t s)
{
    ecs_entity_t e = edit_field(world);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    edit_run(world, s, "mouse_click left [%.2f, %.2f]",
        (double)(640 + t->m[3][0] + ecs_get(world, e, LayoutBounds)->width * 0.5f - 2),
        (double)(360 - t->m[3][1]));
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, edit_field(world));
    edit_wait(world);
}

/** Get the size of a Rect part.
 *
 * @param world The world
 * @param e The part
 * @return Its Rect
 */
static Rect edit_rect(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const Rect *r = ecs_get(world, e, Rect);
    test_assert(r != NULL);

    return *r;
}

void TextEdit_adds_focus_and_parts(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t e = edit_field(world);
    test_assert(ecs_has_id(world, e, Focusable));
    test_assert(ecs_has_id(world, e, Interactive));
    test_assert(ecs_has_id(world, e, KeyCapture));
    test_assert(ecs_has(world, e, TextEditStatus));
    test_assert(ecs_has(world, e, Layout));
    test_int(ecs_get(world, e, Cursor)->shape, CursorText);
    const TextEdit *te = ecs_get(world, e, TextEdit);
    test_flt(te->size, 16);
    test_uint(te->font, ecs_lookup(world, "box.text.DefaultFont"));

    edit_parts_t parts = edit_parts(world, e);
    TextCaret c;
    test_assert(box_text_caret(world, parts.text, 0, &c));
    test_str(ecs_get(world, parts.text, Text)->value, "hello");
    test_flt(ecs_get(world, e, LayoutBounds)->width, 200);
    test_flt(ecs_get(world, e, LayoutBounds)->height, c.height);
    test_flt(ecs_get(world, parts.content, LayoutBounds)->width, 200);
    test_flt(ecs_get(world, parts.content, LayoutBounds)->height, c.height);

    const UiTheme *theme = ecs_get(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    const Rgb *color = ecs_get(world, parts.text, Rgb);
    test_int(color->r, theme->text.r);
    test_int(color->g, theme->text.g);
    color = ecs_get(world, parts.selection, Rgb);
    test_int(color->r, theme->selection.r);
    color = ecs_get(world, parts.caret, Rgb);
    test_int(color->r, theme->caret.r);

    char point[64];
    edit_point(world, 2, 0, point, sizeof(point));
    edit_run(world, s, "mouse_move %s", point);
    test_uint(ecs_singleton_get(world, PickStatus)->target, e);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, e);
    test_int(ecs_singleton_get(world, PickStatus)->shape, CursorText);

    ecs_remove(world, e, TextEdit);
    ecs_progress(world, EDIT_DT);
    test_assert(!ecs_has_id(world, e, Focusable));
    test_assert(!ecs_has_id(world, e, KeyCapture));
    test_assert(!ecs_has(world, e, TextEditStatus));
    test_assert(!ecs_has(world, e, Cursor));
    test_assert(!ecs_has(world, e, Layout));
    test_assert(!ecs_is_alive(world, parts.content));
    test_assert(!ecs_is_alive(world, parts.text));

    ecs_fini(world);
}

void TextEdit_type_text(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_expect(world, 0, 0);
    edit_run(world, s, "key_type hello");
    test_str(edit_value(world), "hello");
    edit_expect(world, 5, 5);
    test_str(ecs_get(world, edit_parts(world, edit_field(world)).text, Text)->value, "hello");
    test_str(edit_shown(world), "hello");
    test_str(ecs_get(world, edit_e(world, "hud.form.other"), TextEdit)->value, "other");
    ecs_fini(world);
}

void TextEdit_insert_middle(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"held\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_expect(world, 4, 4);
    edit_keys(world, s, "Left", 1);
    edit_run(world, s, "key_type \"lo wor\"");
    test_str(edit_value(world), "hello word");
    edit_expect(world, 9, 9);
    ecs_fini(world);
}

void TextEdit_replace_selection(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_keys(world, s, "Home", 1);
    edit_run(world, s, "key_down ShiftLeft");
    edit_keys(world, s, "Right", 5);
    edit_run(world, s, "key_up ShiftLeft");
    edit_expect(world, 0, 5);
    edit_run(world, s, "key_type bye");
    test_str(edit_value(world), "bye world");
    edit_expect(world, 3, 3);
    test_int(edit_probe(world)->changes, 3);
    ecs_fini(world);
}

void TextEdit_max_length(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {max_length: 5}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_run(world, s, "key_type abcdefg");
    test_str(edit_value(world), "abcde");
    edit_expect(world, 5, 5);
    test_int(edit_probe(world)->changes, 5);

    edit_keys(world, s, "Home", 1);
    edit_keys(world, s, "Right", 1);
    edit_run(world, s, "key_down ShiftLeft");
    edit_keys(world, s, "Right", 2);
    edit_run(world, s, "key_up ShiftLeft");
    box_input_paste(world, "XYZW");
    edit_run(world, s, "input_wait");
    test_str(edit_value(world), "aXYde");
    edit_expect(world, 3, 3);

    edit_set(world, "");
    box_input_paste(world, "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" "abc");
    edit_run(world, s, "input_wait");
    test_str(edit_value(world), "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" "ab");
    edit_set(world, "abc");
    edit_keys(world, s, "End", 1);
    box_input_paste(world, "\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80");
    edit_run(world, s, "input_wait");
    test_str(edit_value(world), "abc\xF0\x9F\x98\x80\xF0\x9F\x98\x80");
    edit_expect(world, 11, 11);
    ecs_fini(world);
}

void TextEdit_control_characters(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    box_input_paste(world, "a\tb\nc\r\nd\x01" "e\x7f" "f\rg\x1b");
    edit_run(world, s, "input_wait");
    test_str(edit_value(world), "a b c def g");
    edit_expect(world, 11, 11);
    test_int(edit_probe(world)->changes, 1);

    box_input_paste(world, "\x01\x02");
    edit_run(world, s, "input_wait");
    test_str(edit_value(world), "a b c def g");
    test_int(edit_probe(world)->changes, 1);

    edit_set(world, "x\ny");
    test_str(ecs_get(world, edit_parts(world, edit_field(world)).text, Text)->value, "x y");
    ecs_fini(world);
}

void TextEdit_multi_byte(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_set(world, "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" "b");
    edit_keys(world, s, "Home", 1);
    int32_t stops[] = {1, 3, 6, 10, 11, 11};
    int32_t i;
    for (i = 0; i < 6; i ++) {
        edit_keys(world, s, "Right", 1);
        edit_expect(world, stops[i], stops[i]);
    }

    for (i = 4; i >= 0; i --) {
        edit_keys(world, s, "Left", 1);
        edit_expect(world, i ? stops[i - 1] : 0, i ? stops[i - 1] : 0);
    }

    edit_keys(world, s, "End", 1);
    edit_keys(world, s, "Left", 1);
    edit_keys(world, s, "Backspace", 1);
    test_str(edit_value(world), "a\xC3\xA9\xE2\x82\xAC" "b");
    edit_expect(world, 6, 6);
    edit_keys(world, s, "Home", 1);
    edit_keys(world, s, "Right", 1);
    edit_keys(world, s, "Delete", 1);
    test_str(edit_value(world), "a\xE2\x82\xAC" "b");
    edit_run(world, s, "key_type \xC3\xBC");
    test_str(edit_value(world), "a\xC3\xBC\xE2\x82\xAC" "b");
    edit_expect(world, 3, 3);
    ecs_fini(world);
}

void TextEdit_ill_formed(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_set(world, "a\xE2\x82" "b\xF0\x9F" "c\xC0\xAF");
    edit_keys(world, s, "Home", 1);
    int32_t stops[] = {1, 3, 4, 6, 7, 8, 9};
    int32_t i;
    for (i = 0; i < 7; i ++) {
        edit_keys(world, s, "Right", 1);
        edit_expect(world, stops[i], stops[i]);
    }

    edit_keys(world, s, "Left", 3);
    edit_expect(world, 6, 6);
    edit_keys(world, s, "Backspace", 1);
    test_str(edit_value(world), "a\xE2\x82" "bc\xC0\xAF");
    edit_expect(world, 4, 4);
    edit_keys(world, s, "Home", 1);
    edit_keys(world, s, "Right", 1);
    edit_keys(world, s, "Delete", 1);
    test_str(edit_value(world), "abc\xC0\xAF");

    edit_set(world, "\xE2\x82\xAC\xE2\x82\xAC");
    edit_keys(world, s, "End", 1);
    edit_set(world, "\xE2\x82\xAC" "ab");
    edit_expect(world, 5, 5);
    edit_set(world, "a\xE2\x82\xAC");
    edit_expect(world, 4, 4);
    edit_keys(world, s, "Left", 1);
    edit_set(world, "\xE2\x82\xAC\xE2\x82\xAC");
    edit_expect(world, 0, 0);
    ecs_fini(world);
}

void TextEdit_caret_moves(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"abc\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_keys(world, s, "Home", 1);
    edit_expect(world, 0, 0);
    edit_keys(world, s, "Right", 1);
    edit_expect(world, 1, 1);
    edit_keys(world, s, "Right", 1);
    edit_expect(world, 2, 2);
    edit_keys(world, s, "Left", 1);
    edit_expect(world, 1, 1);
    edit_keys(world, s, "Right", 5);
    edit_expect(world, 3, 3);
    edit_keys(world, s, "Left", 5);
    edit_expect(world, 0, 0);
    edit_run(world, s, "key_down Right");
    edit_run(world, s, "key_repeat Right 2");
    edit_run(world, s, "key_up Right");
    edit_expect(world, 3, 3);
    test_int(edit_probe(world)->changes, 0);
    test_str(edit_value(world), "abc");
    ecs_fini(world);
}

void TextEdit_word_moves(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello, big  world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_keys(world, s, "Home", 1);
    int32_t right[] = {5, 6, 10, 17, 17};
    int32_t left[] = {12, 7, 5, 0, 0};
    int32_t i;
    for (i = 0; i < 5; i ++) {
        edit_chord(world, s, EDIT_WORD, "Right");
        edit_expect(world, right[i], right[i]);
    }

    for (i = 0; i < 5; i ++) {
        edit_chord(world, s, EDIT_WORD, "Left");
        edit_expect(world, left[i], left[i]);
    }

    edit_run(world, s, "key_down ShiftLeft");
    edit_chord(world, s, EDIT_WORD, "Right");
    edit_run(world, s, "key_up ShiftLeft");
    edit_expect(world, 0, 5);

    edit_keys(world, s, "End", 1);
    edit_chord(world, s, EDIT_WORD, "Backspace");
    test_str(edit_value(world), "hello, big  ");
    edit_expect(world, 12, 12);
    edit_keys(world, s, "Home", 1);
    edit_chord(world, s, EDIT_WORD, "Delete");
    test_str(edit_value(world), ", big  ");
    edit_chord(world, s, EDIT_WORD, "Delete");
    test_str(edit_value(world), " big  ");

    edit_set(world, "\xC3\xBC" "ber stra\xC3\x9F" "e!");
    edit_keys(world, s, "Home", 1);
    edit_chord(world, s, EDIT_WORD, "Right");
    edit_expect(world, 5, 5);
    edit_chord(world, s, EDIT_WORD, "Right");
    edit_expect(world, 13, 13);
    edit_chord(world, s, EDIT_WORD, "Right");
    edit_expect(world, 14, 14);
    ecs_fini(world);
}

void TextEdit_home_end(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"abc def\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_keys(world, s, "Home", 1);
    edit_expect(world, 0, 0);
    edit_keys(world, s, "End", 1);
    edit_expect(world, 7, 7);
    edit_keys(world, s, "Up", 1);
    edit_expect(world, 0, 0);
    edit_keys(world, s, "Down", 1);
    edit_expect(world, 7, 7);
    edit_run(world, s, "key_down ShiftLeft");
    edit_keys(world, s, "Home", 1);
    edit_expect(world, 7, 0);
    edit_keys(world, s, "Down", 1);
    edit_expect(world, 7, 7);
    edit_run(world, s, "key_up ShiftLeft");
#ifdef __APPLE__
    edit_chord(world, s, "SuperLeft", "Left");
    edit_expect(world, 0, 0);
    edit_chord(world, s, "SuperLeft", "Right");
    edit_expect(world, 7, 7);
#endif
    ecs_fini(world);
}

void TextEdit_shift_selection(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"abcdef\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_keys(world, s, "Home", 1);
    edit_run(world, s, "key_down ShiftLeft");
    edit_keys(world, s, "Right", 3);
    edit_expect(world, 0, 3);
    edit_keys(world, s, "Left", 1);
    edit_expect(world, 0, 2);
    edit_run(world, s, "key_up ShiftLeft");
    edit_keys(world, s, "Right", 1);
    edit_expect(world, 2, 2);
    edit_run(world, s, "key_down ShiftRight");
    edit_keys(world, s, "End", 1);
    edit_expect(world, 2, 6);
    edit_run(world, s, "key_up ShiftRight");
    edit_keys(world, s, "Left", 1);
    edit_expect(world, 2, 2);

    edit_run(world, s, "key_down ShiftLeft");
    edit_keys(world, s, "Left", 2);
    edit_run(world, s, "key_up ShiftLeft");
    edit_expect(world, 2, 0);
    edit_parts_t parts = edit_parts(world, edit_field(world));
    Rect sel = edit_rect(world, parts.selection);
    test_flt(sel.width, edit_caret_x(world, 2) - edit_caret_x(world, 0));
    test_assert(sel.height > 0);
    edit_keys(world, s, "Delete", 1);
    test_str(edit_value(world), "cdef");
    edit_expect(world, 0, 0);
    test_flt(edit_rect(world, parts.selection).width, 0);
    ecs_fini(world);
}

void TextEdit_select_all(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_keys(world, s, "Home", 1);
    edit_chord(world, s, EDIT_CMD, "A");
    edit_expect(world, 0, 11);
    test_str(edit_value(world), "hello world");
    edit_run(world, s, "key_type x");
    test_str(edit_value(world), "x");
    edit_expect(world, 1, 1);
    test_int(edit_probe(world)->changes, 1);
    edit_keys(world, s, "A", 1);
    edit_expect(world, 1, 1);
    ecs_fini(world);
}

void TextEdit_backspace_repeat(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"abcdef\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_run(world, s, "key_down Backspace");
    test_str(edit_value(world), "abcde");
    edit_run(world, s, "key_repeat Backspace 3");
    test_str(edit_value(world), "ab");
    edit_run(world, s, "key_up Backspace");
    test_str(edit_value(world), "ab");
    edit_expect(world, 2, 2);
    test_int(edit_probe(world)->changes, 4);
    edit_keys(world, s, "Backspace", 3);
    test_str(edit_value(world), "");
    test_int(edit_probe(world)->changes, 6);
    edit_keys(world, s, "Delete", 1);
    test_int(edit_probe(world)->changes, 6);
    ecs_fini(world);
}

void TextEdit_click_placement(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    float w = edit_caret_x(world, 2) - edit_caret_x(world, 1);
    edit_click(world, s, 0, 1);
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, edit_field(world));
    edit_expect(world, 0, 0);
    edit_wait(world);
    edit_click(world, s, 3, 0);
    edit_expect(world, 3, 3);
    edit_wait(world);
    edit_click(world, s, 5, 0);
    edit_expect(world, 5, 5);
    edit_wait(world);
    edit_click(world, s, 1, w * 0.3f);
    edit_expect(world, 1, 1);
    edit_wait(world);
    edit_click(world, s, 1, w * 0.7f);
    edit_expect(world, 2, 2);
    edit_wait(world);
    edit_click(world, s, 5, 60);
    edit_expect(world, 5, 5);
    edit_wait(world);
    edit_click(world, s, 0, 0.3f);
    edit_expect(world, 0, 0);
    test_int(edit_probe(world)->changes, 0);
    ecs_fini(world);
}

void TextEdit_drag_select(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char from[64], to[64];
    edit_point(world, 0, 0, from, sizeof(from));
    edit_point(world, 5, 0, to, sizeof(to));
    edit_run(world, s, "mouse_drag left %s %s", from, to);
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, edit_field(world));
    edit_expect(world, 0, 5);
    edit_wait(world);
    edit_point(world, 11, 0, from, sizeof(from));
    edit_point(world, 6, 0, to, sizeof(to));
    edit_run(world, s, "mouse_drag left %s %s", from, to);
    edit_expect(world, 11, 6);
    edit_run(world, s, "key_type X");
    test_str(edit_value(world), "hello X");
    ecs_fini(world);
}

void TextEdit_drag_scrolls(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world(
        "TextEdit: {value: \"the quick brown fox jumps over\"}\n"
        "LayoutItem: {width: {mode: Fixed, value: 60}}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char from[64];
    const WorldTransform *t = ecs_get(world, edit_field(world), WorldTransform);
    float x = 640 + t->m[3][0], y = 360 - t->m[3][1];
    int32_t i, last = 0;
    edit_point(world, 1, 0, from, sizeof(from));
    edit_run(world, s, "mouse_move %s", from);
    edit_run(world, s, "mouse_button left true");
    edit_expect(world, 1, 1);
    test_flt(edit_status(world).scroll, 0);
    edit_run(world, s, "mouse_move [%.2f, %.2f]", (double)(x + 90), (double)y);
    for (i = 0; i < 120 && edit_status(world).caret < 30; i ++) {
        test_assert(edit_status(world).caret >= last);
        last = edit_status(world).caret;
        ecs_progress(world, EDIT_DT);
    }

    edit_expect(world, 1, 30);
    test_assert(edit_status(world).scroll > 0);
    edit_run(world, s, "mouse_move [%.2f, %.2f]", (double)(x - 90), (double)y);
    for (i = 0; i < 120 && edit_status(world).caret > 0; i ++) {
        ecs_progress(world, EDIT_DT);
    }

    edit_expect(world, 1, 0);
    test_flt(edit_status(world).scroll, 0);
    edit_run(world, s, "mouse_button left false");
    edit_run(world, s, "mouse_move [%.2f, %.2f]", (double)(x + 90), (double)y);
    edit_expect(world, 1, 0);
    ecs_fini(world);
}

void TextEdit_double_click_word(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello big  world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    float w = edit_caret_x(world, 8) - edit_caret_x(world, 7);
    edit_click(world, s, 7, w * 0.5f);
    edit_expect(world, 7, 7);
    edit_click(world, s, 7, w * 0.5f);
    edit_expect(world, 6, 9);
    edit_wait(world);
    edit_click(world, s, 7, w * 0.5f);
    edit_expect(world, 7, 7);
    edit_wait(world);

    w = edit_caret_x(world, 10) - edit_caret_x(world, 9);
    edit_click(world, s, 9, w * 0.6f);
    edit_click(world, s, 9, w * 0.6f);
    edit_expect(world, 9, 11);
    edit_wait(world);

    w = edit_caret_x(world, 3) - edit_caret_x(world, 2);
    edit_click(world, s, 3, -w * 0.2f);
    edit_click(world, s, 3, -w * 0.2f);
    edit_expect(world, 0, 5);
    edit_wait(world);

    char from[64], to[64];
    edit_click(world, s, 7, 0.5f);
    edit_point(world, 7, 0.5f, from, sizeof(from));
    edit_point(world, 13, 0, to, sizeof(to));
    edit_run(world, s, "mouse_drag left %s %s", from, to);
    edit_expect(world, 6, 16);
    ecs_fini(world);
}

void TextEdit_triple_click_all(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello big world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_click(world, s, 7, 1);
    edit_click(world, s, 7, 1);
    edit_click(world, s, 7, 1);
    edit_expect(world, 0, 15);
    edit_click(world, s, 7, 1);
    edit_expect(world, 7, 7);
    edit_run(world, s, "key_type X");
    test_str(edit_value(world), "hello bXig world");
    ecs_fini(world);
}

void TextEdit_slow_clicks(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello big world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_click(world, s, 7, 1);
    edit_wait(world);
    edit_click(world, s, 7, 1);
    edit_expect(world, 7, 7);
    edit_click(world, s, 1, 1);
    edit_click(world, s, 7, 1);
    edit_expect(world, 7, 7);
    edit_click(world, s, 7, 1);
    edit_expect(world, 6, 9);
    ecs_fini(world);
}

void TextEdit_shift_click(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_click(world, s, 2, 0);
    edit_expect(world, 2, 2);
    edit_run(world, s, "key_down ShiftLeft");
    edit_click(world, s, 8, 0);
    edit_expect(world, 2, 8);
    edit_click(world, s, 1, 0);
    edit_expect(world, 2, 1);
    edit_click(world, s, 1, 0);
    edit_expect(world, 2, 1);
    edit_run(world, s, "key_up ShiftLeft");
    edit_wait(world);
    edit_click(world, s, 4, 0);
    edit_expect(world, 4, 4);
    ecs_fini(world);
}

void TextEdit_copy_paste(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_run(world, s, "key_down ShiftLeft");
    edit_keys(world, s, "Left", 5);
    edit_run(world, s, "key_up ShiftLeft");
    edit_chord(world, s, EDIT_CMD, "C");
    test_str(box_input_clipboard_get(world), "world");
    test_str(edit_value(world), "hello world");
    edit_keys(world, s, "End", 1);
    edit_chord(world, s, EDIT_CMD, "V");
    test_str(edit_value(world), "hello worldworld");
    edit_expect(world, 16, 16);
    test_int(edit_probe(world)->changes, 1);

    edit_run(world, s, "clipboard_set \"big \"");
    edit_keys(world, s, "Home", 1);
    edit_keys(world, s, "Right", 6);
    edit_chord(world, s, EDIT_CMD, "V");
    test_str(edit_value(world), "hello big worldworld");
    edit_expect(world, 10, 10);

    edit_chord(world, s, EDIT_CMD, "A");
    edit_chord(world, s, EDIT_CMD, "C");
    bool error = true;
    char *out = box_command_run(world, s, "clipboard_get", &error);
    test_bool(error, false);
    test_str(out, "hello big worldworld");
    ecs_os_free(out);
    edit_chord(world, s, EDIT_CMD, "V");
    test_str(edit_value(world), "hello big worldworld");
    test_int(edit_probe(world)->changes, 3);
    ecs_fini(world);
}

void TextEdit_cut(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"hello world\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_run(world, s, "clipboard_set keep");
    edit_chord(world, s, EDIT_CMD, "X");
    test_str(edit_value(world), "hello world");
    test_str(box_input_clipboard_get(world), "keep");
    edit_keys(world, s, "Home", 1);
    edit_run(world, s, "key_down ShiftLeft");
    edit_chord(world, s, EDIT_WORD, "Right");
    edit_keys(world, s, "Right", 1);
    edit_run(world, s, "key_up ShiftLeft");
    edit_chord(world, s, EDIT_CMD, "X");
    test_str(edit_value(world), "world");
    test_str(box_input_clipboard_get(world), "hello ");
    edit_expect(world, 0, 0);
    test_int(edit_probe(world)->changes, 1);
    edit_keys(world, s, "End", 1);
    edit_chord(world, s, EDIT_CMD, "V");
    test_str(edit_value(world), "worldhello ");
    ecs_fini(world);
}

void TextEdit_password(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {password: true}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_parts_t parts = edit_parts(world, edit_field(world));
    edit_focus(world, s);
    edit_run(world, s, "key_type s\xC3\xA9" "cret");
    test_str(edit_value(world), "s\xC3\xA9" "cret");
    edit_expect(world, 7, 7);
    test_str(ecs_get(world, parts.text, Text)->value,
        "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");

    edit_keys(world, s, "Left", 1);
    edit_expect(world, 6, 6);
    float x = edit_caret_x(world, 15) - edit_caret_x(world, 0);
    const Position *caret = ecs_get(world, parts.caret, Position);
    const Position *text = ecs_get(world, parts.text, Position);
    test_flt(caret->x, text->x + edit_caret_x(world, 0) + x);

    edit_run(world, s, "clipboard_set keep");
    edit_chord(world, s, EDIT_CMD, "A");
    edit_chord(world, s, EDIT_CMD, "C");
    test_str(box_input_clipboard_get(world), "keep");
    edit_chord(world, s, EDIT_CMD, "X");
    test_str(edit_value(world), "s\xC3\xA9" "cret");
    edit_keys(world, s, "End", 1);
    edit_chord(world, s, EDIT_WORD, "Left");
    edit_expect(world, 0, 0);
    edit_chord(world, s, EDIT_WORD, "Right");
    edit_expect(world, 7, 7);
    edit_chord(world, s, EDIT_CMD, "V");
    test_str(edit_value(world), "s\xC3\xA9" "cretkeep");

    edit_wait(world);
    char point[64];
    edit_point(world, 3, 1, point, sizeof(point));
    edit_run(world, s, "mouse_click left %s", point);
    edit_expect(world, 1, 1);
    edit_run(world, s, "mouse_click left %s", point);
    edit_expect(world, 0, 11);
    ecs_fini(world);
}

void TextEdit_read_only(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"fixed text\", read_only: true}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_expect(world, 10, 10);
    edit_run(world, s, "key_type x");
    edit_keys(world, s, "Backspace", 1);
    edit_keys(world, s, "Home", 1);
    edit_keys(world, s, "Delete", 1);
    edit_run(world, s, "clipboard_set keep");
    edit_chord(world, s, EDIT_CMD, "V");
    test_str(edit_value(world), "fixed text");
    edit_expect(world, 0, 0);

    edit_run(world, s, "key_down ShiftLeft");
    edit_chord(world, s, EDIT_WORD, "Right");
    edit_run(world, s, "key_up ShiftLeft");
    edit_expect(world, 0, 5);
    edit_chord(world, s, EDIT_CMD, "X");
    test_str(edit_value(world), "fixed text");
    test_str(box_input_clipboard_get(world), "keep");
    edit_chord(world, s, EDIT_CMD, "C");
    test_str(box_input_clipboard_get(world), "fixed");
    edit_chord(world, s, EDIT_CMD, "A");
    edit_expect(world, 0, 10);
    test_int(edit_probe(world)->changes, 0);
    ecs_fini(world);
}

void TextEdit_enter_submits(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_run(world, s, "key_type hi");
    edit_keys(world, s, "Enter", 1);
    test_int(edit_probe(world)->submits, 1);
    test_int(edit_probe(world)->changes, 2);
    test_uint(edit_probe(world)->widget, edit_field(world));
    test_str(edit_value(world), "hi");
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, edit_field(world));
    edit_keys(world, s, "KpEnter", 1);
    test_int(edit_probe(world)->submits, 2);
    edit_run(world, s, "key_down Enter");
    edit_run(world, s, "key_repeat Enter 3");
    edit_run(world, s, "key_up Enter");
    test_int(edit_probe(world)->submits, 3);
    test_int(edit_probe(world)->changes, 2);
    ecs_fini(world);
}

void TextEdit_change_per_edit(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_run(world, s, "key_type abc");
    test_int(edit_probe(world)->changes, 3);
    test_uint(edit_probe(world)->widget, edit_field(world));
    edit_keys(world, s, "Left", 2);
    edit_keys(world, s, "Home", 1);
    edit_keys(world, s, "Backspace", 1);
    test_int(edit_probe(world)->changes, 3);
    edit_keys(world, s, "Delete", 1);
    test_int(edit_probe(world)->changes, 4);
    test_str(edit_shown(world), "bc");
    edit_chord(world, s, EDIT_CMD, "A");
    edit_run(world, s, "key_type z");
    test_int(edit_probe(world)->changes, 5);
    box_input_paste(world, "pasted");
    edit_run(world, s, "input_wait");
    test_int(edit_probe(world)->changes, 6);
    test_str(edit_value(world), "zpasted");
    test_str(edit_shown(world), "zpasted");
    ecs_fini(world);
}

void TextEdit_set_value_no_change(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_focus(world, s);
    edit_run(world, s, "key_type hello");
    test_int(edit_probe(world)->changes, 5);
    edit_set(world, "hey");
    edit_expect(world, 3, 3);
    test_str(ecs_get(world, edit_parts(world, edit_field(world)).text, Text)->value, "hey");
    edit_set(world, "a much longer text");
    edit_expect(world, 3, 3);
    edit_run(world, s, "key_down ShiftLeft");
    edit_keys(world, s, "End", 1);
    edit_run(world, s, "key_up ShiftLeft");
    edit_expect(world, 3, 18);
    edit_set(world, "abcd");
    edit_expect(world, 3, 4);

    test_assert(ecs_script_run(world, "set_value",
        "using box.ui\n"
        "hud {\n"
        "  form {\n"
        "    field {\n"
        "      TextEdit: {value: \"xy\"}\n"
        "    }\n"
        "  }\n"
        "}\n", NULL) == 0);
    ecs_progress(world, EDIT_DT);
    test_str(edit_value(world), "xy");
    edit_expect(world, 2, 2);
    test_int(edit_probe(world)->changes, 5);
    test_str(edit_shown(world), "xy");
    edit_run(world, s, "key_type z");
    test_str(edit_value(world), "xyz");
    test_int(edit_probe(world)->changes, 6);
    ecs_fini(world);
}

void TextEdit_scroll_keeps_caret(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world(
        "TextEdit: {}\n"
        "LayoutItem: {width: {mode: Fixed, value: 60}}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_parts_t parts = edit_parts(world, edit_field(world));
    int32_t i;
    edit_focus(world, s);
    const char *text = "abcdefghijklmnopqrst";
    for (i = 0; text[i]; i ++) {
        edit_run(world, s, "key_type %c", text[i]);
        float x = ecs_get(world, parts.caret, Position)->x;
        test_assert(x >= -30 && x <= 30);
    }

    TextEditStatus st = edit_status(world);
    float end = edit_caret_x(world, 20) - edit_caret_x(world, 0);
    test_assert(end > 60);
    test_assert(st.scroll > 0);
    test_assert(st.scroll <= end);
    test_flt(ecs_get(world, parts.text, Position)->x, -30 - st.scroll - edit_caret_x(world, 0));

    for (i = 0; i < 20; i ++) {
        edit_keys(world, s, "Left", 1);
        float x = ecs_get(world, parts.caret, Position)->x;
        test_assert(x >= -30 && x <= 30);
        test_assert(edit_status(world).scroll <= st.scroll);
    }

    test_flt(edit_status(world).scroll, 0);
    test_flt(ecs_get(world, parts.caret, Position)->x, -30);
    edit_keys(world, s, "End", 1);
    test_flt(edit_status(world).scroll, st.scroll);
    edit_keys(world, s, "Home", 1);
    test_flt(edit_status(world).scroll, 0);
    edit_set(world, "abc");
    edit_keys(world, s, "End", 1);
    test_flt(edit_status(world).scroll, 0);
    ecs_fini(world);
}

void TextEdit_key_capture_binding(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t hide = edit_e(world, "actions.hide");
    int32_t i, presses = 0;
    edit_focus(world, s);
    ecs_os_free(box_command_run(world, s, "key_type hhh", NULL));
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, EDIT_DT);
        presses += ecs_get(world, hide, ActionState)->pressed;
    }

    test_str(edit_value(world), "hhh");
    test_int(presses, 0);
    edit_keys(world, s, "Escape", 1);
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, 0);
    ecs_os_free(box_command_run(world, s, "key_press H", NULL));
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, EDIT_DT);
        presses += ecs_get(world, hide, ActionState)->pressed;
    }

    test_int(presses, 1);
    test_str(edit_value(world), "hhh");
    ecs_fini(world);
}

void TextEdit_escape_blurs(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"abc\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_parts_t parts = edit_parts(world, edit_field(world));
    test_bool(edit_status(world).focused, false);
    test_flt(edit_rect(world, parts.caret).width, 0);
    edit_focus(world, s);
    edit_run(world, s, "key_down ShiftLeft");
    edit_keys(world, s, "Left", 2);
    edit_run(world, s, "key_up ShiftLeft");
    test_bool(edit_status(world).focused, true);
    test_assert(edit_rect(world, parts.caret).width > 0);
    test_assert(edit_rect(world, parts.caret).height > 0);
    test_assert(edit_rect(world, parts.selection).width > 0);

    edit_keys(world, s, "Escape", 1);
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, 0);
    test_bool(edit_status(world).focused, false);
    test_flt(edit_rect(world, parts.caret).width, 0);
    test_flt(edit_rect(world, parts.selection).width, 0);
    edit_expect(world, 3, 1);
    edit_run(world, s, "key_type x");
    test_str(edit_value(world), "abc");
    ecs_fini(world);
}

void TextEdit_click_elsewhere_blurs(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"abc\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_parts_t parts = edit_parts(world, edit_field(world));
    edit_focus(world, s);
    edit_chord(world, s, EDIT_CMD, "A");
    test_assert(edit_rect(world, parts.selection).width > 0);
    edit_run(world, s, "mouse_click left [1200, 50]");
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, 0);
    test_bool(edit_status(world).focused, false);
    test_flt(edit_rect(world, parts.caret).width, 0);
    test_flt(edit_rect(world, parts.selection).width, 0);
    edit_expect(world, 0, 3);
    ecs_fini(world);
}

void TextEdit_tab_between_fields(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t other = edit_e(world, "hud.form.other");
    edit_focus(world, s);
    edit_run(world, s, "key_type a");
    edit_keys(world, s, "Tab", 1);
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, other);
    test_bool(edit_status(world).focused, false);
    test_bool(ecs_get(world, other, TextEditStatus)->focused, true);
    edit_run(world, s, "key_type b");
    test_str(ecs_get(world, other, TextEdit)->value, "bother");
    edit_keys(world, s, "Tab", 1);
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, edit_field(world));
    edit_run(world, s, "key_type c");
    test_str(edit_value(world), "ac");
    ecs_fini(world);
}

void TextEdit_placeholder(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {placeholder: \"Name\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_parts_t parts = edit_parts(world, edit_field(world));
    const UiTheme *theme = ecs_get(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    test_str(ecs_get(world, parts.text, Text)->value, "Name");
    test_int(ecs_get(world, parts.text, Rgb)->r, theme->text_muted.r);
    edit_focus(world, s);
    edit_expect(world, 0, 0);
    test_str(ecs_get(world, parts.text, Text)->value, "Name");
    test_flt(ecs_get(world, parts.caret, Position)->x, -100);
    edit_run(world, s, "key_type a");
    test_str(ecs_get(world, parts.text, Text)->value, "a");
    test_int(ecs_get(world, parts.text, Rgb)->r, theme->text.r);
    edit_keys(world, s, "Backspace", 1);
    test_str(ecs_get(world, parts.text, Text)->value, "Name");
    test_int(ecs_get(world, parts.text, Rgb)->r, theme->text_muted.r);
    ecs_fini(world);
}

void TextEdit_caret_blink(void) {
    // ui/text_edit
    ecs_world_t *world = edit_world("TextEdit: {value: \"abc\"}");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    edit_parts_t parts = edit_parts(world, edit_field(world));
    int32_t i, on = 0, off = 0, toggles = 0, first_off = -1;
    bool was = true;
    edit_focus(world, s);
    edit_keys(world, s, "Left", 1);
    test_assert(edit_rect(world, parts.caret).width > 0);
    for (i = 0; i < 120; i ++) {
        ecs_progress(world, EDIT_DT);
        bool shown = edit_rect(world, parts.caret).width > 0;
        on += shown;
        off += !shown;
        toggles += shown != was;
        first_off = first_off < 0 && !shown ? i : first_off;
        was = shown;
    }

    test_assert(first_off >= 25 && first_off <= 30);
    test_assert(toggles >= 3 && toggles <= 4);
    test_assert(on >= 55 && on <= 65);
    test_assert(off >= 55 && off <= 65);
    test_bool(edit_status(world).focused, true);
    for (i = 0; i < 120 && edit_rect(world, parts.caret).width > 0; i ++) {
        ecs_progress(world, EDIT_DT);
    }

    test_flt(edit_rect(world, parts.caret).width, 0);
    edit_keys(world, s, "Left", 1);
    test_assert(edit_rect(world, parts.caret).width > 0);
    for (i = 0; i < 120 && edit_rect(world, parts.caret).width > 0; i ++) {
        ecs_progress(world, EDIT_DT);
    }

    edit_run(world, s, "key_type x");
    test_assert(edit_rect(world, parts.caret).width > 0);

    UiTheme *theme = ecs_get_mut(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    theme->caret_blink = 0;
    ecs_modified(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    on = off = 0;
    for (i = 0; i < 120; i ++) {
        ecs_progress(world, EDIT_DT);
        on += edit_rect(world, parts.caret).width > 0;
        off += edit_rect(world, parts.caret).width == 0;
    }

    test_int(on, 120);
    test_int(off, 0);
    ecs_fini(world);
}

void TextEdit_word_move_function(void) {
    // ui/text_edit
    const char *str = "hello, big  world";
    test_int(box_ui_word_move(str, 0, true), 5);
    test_int(box_ui_word_move(str, 5, true), 6);
    test_int(box_ui_word_move(str, 6, true), 10);
    test_int(box_ui_word_move(str, 10, true), 17);
    test_int(box_ui_word_move(str, 17, true), 17);
    test_int(box_ui_word_move(str, 17, false), 12);
    test_int(box_ui_word_move(str, 12, false), 7);
    test_int(box_ui_word_move(str, 9, false), 7);
    test_int(box_ui_word_move(str, 7, false), 5);
    test_int(box_ui_word_move(str, 5, false), 0);
    test_int(box_ui_word_move(str, 0, false), 0);
    test_int(box_ui_word_move(str, 100, false), 12);
    test_int(box_ui_word_move("a--b", 0, true), 1);
    test_int(box_ui_word_move("a--b", 1, true), 3);
    test_int(box_ui_word_move("x_y", 0, true), 1);
    test_int(box_ui_word_move("\xC3\xBC" "ber \xE6\x97\xA5\xE6\x9C\xAC!", 0, true), 5);
    test_int(box_ui_word_move("\xC3\xBC" "ber \xE6\x97\xA5\xE6\x9C\xAC!", 5, true), 12);
    test_int(box_ui_word_move("\xC3\xBC" "ber \xE6\x97\xA5\xE6\x9C\xAC!", 2, false), 0);
    test_int(box_ui_word_move("ab\xE2\x82\xAC" "cd", 3, true), 7);
    test_int(box_ui_word_move("ab\xE2\x82\xAC" "cd", 3, false), 0);
    test_int(box_ui_word_move("a\xE2\x82 b", 0, true), 3);
    test_int(box_ui_word_move("   ", 0, true), 3);
    test_int(box_ui_word_move("", 0, true), 0);
    test_int(box_ui_word_move(NULL, 0, false), 0);
}
