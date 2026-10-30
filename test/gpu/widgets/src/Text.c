/**
 * @file Text.c
 * @brief The Text section of the widget gallery driven through the command API: text fields by
 * click and typing, the password, disabled and read only fields, Tab between them, the submit
 * label, muts set from the terminal, the theme and a capture of a field with a selection.
 */

#include "gallery.h"
#include "render.h"
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** File the window writes for the capture */
#define TEXT_CAPTURE "text_selection.png"

/** Get a private part the engine built inside the TextEdit of a gallery field.
 *
 * @param world The world
 * @param path Path of the field element, the one with the TextEdit
 * @param caret Whether to get the caret Rect, else the Text
 * @return The part
 */
static ecs_entity_t text_part_of(
    ecs_world_t *world,
    const char *path,
    bool caret)
{
    ecs_entity_t field = ecs_lookup(world, path), content = 0, result = 0;
    int32_t i;
    test_assert(field != 0);
    ecs_iter_t it = ecs_children(world, field);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            content = ecs_has_id(world, it.entities[i], ClipChildren) ? it.entities[i] : content;
        }
    }

    test_assert(content != 0);
    it = ecs_children(world, content);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t c = it.entities[i];
            const Position *p = ecs_get(world, c, Position);
            bool match = caret ? !ecs_has(world, c, Text) && p && p->z > 2 :
                ecs_has(world, c, Text);
            result = match ? c : result;
        }
    }

    test_assert(result != 0);

    return result;
}

/** Get the private Text part the engine built inside the TextEdit of a gallery field.
 *
 * @param world The world
 * @param path Path of the field element, the one with the TextEdit
 * @return The Text part
 */
static ecs_entity_t text_part(
    ecs_world_t *world,
    const char *path)
{
    return text_part_of(world, path, false);
}

/** Check a member of the TextInput mut of a field of the Text panel.
 *
 * @param world The world
 * @param s The terminal session
 * @param name Name of the TextInput, or a path below the panel
 * @param member Member of the mut
 * @param expect The value as the get command prints it
 */
static void text_expect_mut(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *name,
    const char *member,
    const char *expect)
{
    char *line = flecs_asprintf("get " TEXT ".%s box.widgets.TextInput.mut.%s", name, member);
    gallery_expect(world, s, line, expect);
    ecs_os_free(line);
}

/** Check the entity with the keyboard focus.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Its path as the command prints it, "#0" for none
 */
static void text_expect_focus(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path)
{
    char *focused = gallery_focused(world, s);
    test_str(focused, path);
    ecs_os_free(focused);
}

/** Type text and run the input.
 *
 * @param world The world
 * @param s The terminal session
 * @param text The text, quoted as the key_type command takes it
 */
static void text_type(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *text)
{
    ecs_os_free(gallery_run(world, s, "key_type %s", text));
    gallery_settle(world, s);
}

/** Press a key while the control key (command on macOS) is held.
 *
 * @param world The world
 * @param s The terminal session
 * @param key The key
 */
static void text_shortcut(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *key)
{
#ifdef __APPLE__
    const char *mod = "SuperLeft";
#else
    const char *mod = "CtrlLeft";
#endif
    ecs_os_free(gallery_run(world, s, "key_down %s", mod));
    gallery_key(world, s, key);
    ecs_os_free(gallery_run(world, s, "key_up %s", mod));
    gallery_settle(world, s);
}

void Text_scene_loads(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_expect(world, s, "logs warning 100", "");
    gallery_expect(world, s, "get " TEXT ".heading box.text.Text.value", "\"Text\"");
    gallery_expect(world, s, "get " TEXT ".name.caption box.text.Text.value", "\"Name\"");
    gallery_expect(world, s, "get " TEXT ".name.field box.ui.TextEdit.placeholder",
        "\"Your name\"");
    gallery_expect(world, s, "get " TEXT ".name.field box.ui.TextEdit.size", "14");
    gallery_expect(world, s, "get " TEXT ".secret.field box.ui.TextEdit.password", "true");
    gallery_expect(world, s, "get " TEXT ".fixed.field box.ui.TextEdit.read_only", "true");
    gallery_expect(world, s, "get " TEXT ".fixed.field box.ui.TextEdit.value", "\"Copy me\"");
    test_bool(ecs_has_id(world, ecs_lookup(world, TEXT ".name.field"), Focusable), true);
    test_bool(ecs_has(world, ecs_lookup(world, TEXT ".off.field"), TextEdit), false);
    test_bool(ecs_has_id(world, ecs_lookup(world, TEXT ".off.field"), Focusable), false);
    gallery_expect(world, s, "get " TEXT ".off.field.shown box.text.Text.value",
        "\"Not editable\"");
    gallery_expect(world, s, "get " TEXT ".off box.material.Opacity",
        "{\"Opacity\":{\"value\":0.4499999881}}");
    gallery_expect(world, s, "get " TEXT ".submit.echo box.text.Text.value", "\"Submitted: \"");
    text_expect_mut(world, s, "fixed", "current", "\"Copy me\"");
    text_expect_mut(world, s, "name", "current", "\"\"");

    ecs_entity_t name = text_part(world, TEXT ".name.field");
    test_str(ecs_get(world, name, Text)->value, "Your name");
    const LayoutBounds *b = ecs_get(world, ecs_lookup(world, TEXT ".name.field"), LayoutBounds);
    test_flt(b->width, 248);
    test_flt(b->height, 28);
    gallery_hover(world, s, TEXT ".name.field", 0, 0, TEXT ".name.field");
    gallery_expect(world, s, "get $ box.picking.PickStatus.target",
        "\"" TEXT ".name.field\"");
    gallery_expect(world, s, "get $ box.picking.PickStatus.shape", "\"CursorText\"");
    text_expect_mut(world, s, "name", "hover", "true");

    ecs_fini(world);
}

void Text_type_name(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_click(world, s, TEXT ".name.field", 0, 0, TEXT ".name.field");
    text_expect_focus(world, s, TEXT ".name.field");
    text_expect_mut(world, s, "name", "focused", "true");
    gallery_expect(world, s, "get " TEXT ".name.field box.geometry.Border.color",
        "{\"r\":120, \"g\":172, \"b\":240, \"a\":255}");
    text_type(world, s, "Ada");
    text_expect_mut(world, s, "name", "current", "\"Ada\"");
    gallery_expect(world, s, "get " TEXT ".name.field box.ui.TextEdit.value", "\"Ada\"");
    test_str(ecs_get(world, text_part(world, TEXT ".name.field"), Text)->value, "Ada");
    test_int(gallery_changes(world, s), 3);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"name: Ada\"");
    gallery_expect(world, s, "get gallery.hud.root.header.status box.text.Text.value",
        "\"Last change: name: Ada\"");

    gallery_key(world, s, "Backspace");
    text_expect_mut(world, s, "name", "current", "\"Ad\"");
    test_int(gallery_changes(world, s), 4);
    gallery_key(world, s, "Escape");
    text_expect_focus(world, s, "#0");
    text_expect_mut(world, s, "name", "focused", "false");
    gallery_expect(world, s, "get " TEXT ".name.field box.geometry.Border.color",
        "{\"r\":142, \"g\":149, \"b\":160, \"a\":255}");

    ecs_fini(world);
}

void Text_password(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_click(world, s, TEXT ".secret.field", 0, 0, TEXT ".secret.field");
    text_expect_focus(world, s, TEXT ".secret.field");
    text_type(world, s, "hunter2");
    text_expect_mut(world, s, "secret", "current", "\"hunter2\"");
    test_str(ecs_get(world, text_part(world, TEXT ".secret.field"), Text)->value,
        "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"secret\"");
    gallery_expect(world, s, "clipboard_set keep", "");
    text_shortcut(world, s, "A");
    text_shortcut(world, s, "C");
    gallery_expect(world, s, "clipboard_get", "keep");
    text_shortcut(world, s, "X");
    text_expect_mut(world, s, "secret", "current", "\"hunter2\"");

    ecs_fini(world);
}

void Text_disabled(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_click(world, s, TEXT ".name.field", 0, 0, TEXT ".name.field");
    gallery_click(world, s, TEXT ".off.field", 0, 0, PAGE);
    text_expect_focus(world, s, "#0");
    text_type(world, s, "x");
    text_expect_mut(world, s, "off", "current", "\"Not editable\"");
    text_expect_mut(world, s, "off", "hover", "false");
    test_int(gallery_changes(world, s), 0);

    ecs_fini(world);
}

void Text_read_only(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_click(world, s, TEXT ".fixed.field", 0, 0, TEXT ".fixed.field");
    text_expect_focus(world, s, TEXT ".fixed.field");
    text_type(world, s, "x");
    gallery_key(world, s, "Backspace");
    text_expect_mut(world, s, "fixed", "current", "\"Copy me\"");
    text_shortcut(world, s, "A");
    text_shortcut(world, s, "C");
    gallery_expect(world, s, "clipboard_get", "Copy me");
    text_shortcut(world, s, "X");
    gallery_expect(world, s, "get " TEXT ".fixed.field box.ui.TextEdit.value", "\"Copy me\"");
    test_int(gallery_changes(world, s), 0);

    ecs_fini(world);
}

void Text_tab_between(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_click(world, s, TEXT ".name.field", 0, 0, TEXT ".name.field");
    text_type(world, s, "a");
    gallery_key(world, s, "Tab");
    text_expect_focus(world, s, TEXT ".secret.field");
    text_type(world, s, "b");
    gallery_key(world, s, "Tab");
    text_expect_focus(world, s, TEXT ".fixed.field");
    gallery_key(world, s, "Tab");
    text_expect_focus(world, s, TEXT ".submit.message.field");
    text_type(world, s, "c");
    ecs_os_free(gallery_run(world, s, "key_down ShiftLeft"));
    gallery_key(world, s, "Tab");
    ecs_os_free(gallery_run(world, s, "key_up ShiftLeft"));
    gallery_settle(world, s);
    text_expect_focus(world, s, TEXT ".fixed.field");

    text_expect_mut(world, s, "name", "current", "\"a\"");
    text_expect_mut(world, s, "secret", "current", "\"b\"");
    text_expect_mut(world, s, "submit.message", "current", "\"c\"");
    test_int(gallery_changes(world, s), 3);
    gallery_expect(world, s, "get " TEXT ".fixed.field box.geometry.Border.width", "2");

    ecs_fini(world);
}

void Text_submit_label(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_click(world, s, TEXT ".submit.message.field", 0, 0, TEXT ".submit.message.field");
    text_type(world, s, "\"hello there\"");
    gallery_expect(world, s, "get gallery Gallery.mut.submitted", "\"\"");
    gallery_key(world, s, "Enter");
    gallery_expect(world, s, "get gallery Gallery.mut.submitted", "\"hello there\"");
    gallery_expect(world, s, "get " TEXT ".submit.echo box.text.Text.value",
        "\"Submitted: hello there\"");
    text_expect_focus(world, s, TEXT ".submit.message.field");

    text_shortcut(world, s, "A");
    text_type(world, s, "bye");
    gallery_expect(world, s, "get " TEXT ".submit.echo box.text.Text.value",
        "\"Submitted: hello there\"");
    gallery_key(world, s, "KpEnter");
    gallery_expect(world, s, "get " TEXT ".submit.echo box.text.Text.value", "\"Submitted: bye\"");

    gallery_click(world, s, TEXT ".name.field", 0, 0, TEXT ".name.field");
    text_type(world, s, "x");
    gallery_key(world, s, "Enter");
    gallery_expect(world, s, "get gallery Gallery.mut.submitted", "\"bye\"");

    ecs_fini(world);
}

void Text_set_mut_no_change(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    ecs_os_free(gallery_run(world, s, "set " TEXT ".name box.widgets.TextInput.mut "
        "{current: \"Bob\"}"));
    gallery_settle(world, s);
    gallery_expect(world, s, "get " TEXT ".name.field box.ui.TextEdit.value", "\"Bob\"");
    test_str(ecs_get(world, text_part(world, TEXT ".name.field"), Text)->value, "Bob");
    test_int(gallery_changes(world, s), 0);

    gallery_click(world, s, TEXT ".name.field", 0.9f, 0, TEXT ".name.field");
    text_type(world, s, "by");
    text_expect_mut(world, s, "name", "current", "\"Bobby\"");
    test_int(gallery_changes(world, s), 2);

    ecs_fini(world);
}

void Text_light_theme(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    ecs_entity_t theme = ecs_lookup(world, "box.ui.theme");
    ecs_entity_t light = ecs_lookup(world, "box.widgets.LightTheme");
    ecs_entity_t fixed = text_part(world, TEXT ".fixed.field");
    ecs_entity_t name = text_part(world, TEXT ".name.field");

    gallery_expect(world, s, "get " TEXT ".name.field box.material.Rgb",
        "{\"Rgb\":{\"r\":38, \"g\":41, \"b\":48}}");
    test_int(ecs_get(world, fixed, Rgb)->r, 214);
    test_int(ecs_get(world, name, Rgb)->r, 142);
    ecs_value_t v = ecs_const_var_get(world, light);
    test_assert(v.type == ecs_id(UiTheme));
    ecs_set_id(world, theme, ecs_id(UiTheme), sizeof(UiTheme), v.ptr);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    gallery_expect(world, s, "get " TEXT ".name.field box.material.Rgb",
        "{\"Rgb\":{\"r\":222, \"g\":226, \"b\":233}}");
    gallery_expect(world, s, "get " TEXT ".name.field box.geometry.Border.color",
        "{\"r\":190, \"g\":196, \"b\":206, \"a\":255}");
    gallery_expect(world, s, "get " TEXT ".name.caption box.material.Rgb",
        "{\"Rgb\":{\"r\":102, \"g\":110, \"b\":122}}");
    test_int(ecs_get(world, fixed, Rgb)->r, 33);
    test_int(ecs_get(world, name, Rgb)->r, 102);
    gallery_click(world, s, TEXT ".name.field", 0, 0, TEXT ".name.field");
    gallery_expect(world, s, "get " TEXT ".name.field box.geometry.Border.color",
        "{\"r\":38, \"g\":110, \"b\":220, \"a\":255}");
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Text_capture_selection(void) {
    // widgets/text
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    ecs_entity_t window = ecs_lookup(world, "window");
    int32_t i, w = 0, h = 0, n = 0, selected = 0, caret = 0;

    UiTheme *theme = ecs_get_mut(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    theme->caret_blink = 0;
    ecs_modified(world, ecs_lookup(world, "box.ui.theme"), UiTheme);
    gallery_click(world, s, TEXT ".name.field", 0, 0, TEXT ".name.field");
    text_type(world, s, "\"Grace Hopper\"");
    ecs_os_free(gallery_run(world, s, "key_down ShiftLeft"));
    for (i = 0; i < 6; i ++) {
        gallery_key(world, s, "Left");
    }

    ecs_os_free(gallery_run(world, s, "key_up ShiftLeft"));
    gallery_settle(world, s);
    gallery_expect(world, s, "get " TEXT ".name.field box.ui.TextEditStatus",
        "{\"TextEditStatus\":{\"caret\":6, \"anchor\":12, \"scroll\":0, \"focused\":true}}");

    /* The capture reads pixels: render the window at its full size, which the gallery of the
     * tests divides */
    Surface *surface = ecs_get_mut(world, window, Surface);
    surface->write_to_file = ecs_os_strdup(TEXT_CAPTURE);
    surface->resolution_scale = 1.0f;
    ecs_modified(world, window, Surface);
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir,
        TEXT_CAPTURE);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    test_int(w, ss->width);
    test_int(h, ss->height);

    const WorldTransform *t = ecs_get(world, ecs_lookup(world, TEXT ".name.field"),
        WorldTransform);
    const LayoutBounds *b = ecs_get(world, ecs_lookup(world, TEXT ".name.field"), LayoutBounds);
    float scale = (float)w / (float)ss->point_width;
    int32_t x0 = (int32_t)(((float)ss->point_width * 0.5f + t->m[3][0] - b->width * 0.5f) * scale);
    int32_t x1 = (int32_t)(((float)ss->point_width * 0.5f + t->m[3][0] + b->width * 0.5f) * scale);
    int32_t y = (int32_t)(((float)ss->point_height * 0.5f - t->m[3][1] - 6) * scale);
    for (i = x0; i < x1; i ++) {
        const unsigned char *c = &px[(y * w + i) * 4];
        selected += abs(c[0] - 60) < 12 && abs(c[1] - 104) < 12 && abs(c[2] - 84) < 12;
        caret += c[0] > 200 && c[1] > 200 && c[2] > 200;
    }

    const WorldTransform *ct = ecs_get(world, text_part_of(world, TEXT ".name.field", true),
        WorldTransform);
    const Rect *cr = ecs_get(world, text_part_of(world, TEXT ".name.field", true), Rect);
    test_assert(ct != NULL);
    test_assert(cr != NULL);
    test_assert(cr->width > 0);
    int32_t cx = (int32_t)(((float)ss->point_width * 0.5f + ct->m[3][0]) * scale);
    int32_t cy = (int32_t)(((float)ss->point_height * 0.5f - ct->m[3][1]) * scale);
    int32_t half = (int32_t)(cr->height * 0.5f * scale) - 1, r;
    for (r = cy - half; r <= cy + half; r ++) {
        int32_t best = 0, d;
        for (d = -1; d <= 1; d ++) {
            const unsigned char *c = &px[(r * w + cx + d) * 4];
            best = c[0] + c[1] + c[2] > best ? c[0] + c[1] + c[2] : best;
        }

        caret += best > 3 * 150;
    }

    test_assert(selected > 20);
    test_int(caret, 2 * half + 1);
    stbi_image_free(px);
    ecs_os_free(path);

    ecs_fini(world);
}
