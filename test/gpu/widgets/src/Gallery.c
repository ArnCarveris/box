/**
 * @file Gallery.c
 * @brief The whole widget gallery driven through the command API: its sections, the theme
 * switch, the form that drives the 3D preview by pointer and by keyboard only, the cursor shapes
 * and a resize of the window.
 */

#include "gallery.h"
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Paths of the widgets of the form */
#define CONTROLS FORM ".body.controls"
#define NAME CONTROLS ".name"
#define SHAPE CONTROLS ".shape_row.shape"
#define RED CONTROLS ".red"
#define GREEN CONTROLS ".green"
#define BLUE CONTROLS ".blue"
#define SIZE CONTROLS ".size_row.size"
#define SHADOW CONTROLS ".shadow"
#define LIGHT CONTROLS ".light_row.light"
#define APPLY CONTROLS ".form_actions.apply"
#define RESET CONTROLS ".form_actions.reset"

/** Paths of the 3D preview the form drives */
#define PREVIEW "gallery.preview"
#define PREVIEW_VIEW "gallery.preview_views.view"

/** Width of the thumb of a slider, the knob center stays thumb / 2 from either edge */
#define GALLERY_THUMB (16.0f)

/** Place of the plus button across a stepper of the default width */
#define GALLERY_PLUS (0.75f)

/** Check a window coordinate, which the transforms of the layout round a little.
 *
 * @param v The coordinate
 * @param expect The coordinate it must be
 */
static void gallery_expect_at(
    float v,
    float expect)
{
    if (fabsf(v - expect) >= 0.01f) {
        printf("coordinate %f, expected %f\n", (double)v, (double)expect);
    }

    test_assert(fabsf(v - expect) < 0.01f);
}

/** Check a member of the mut of a widget.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the widget
 * @param tmpl Template of the widget, such as Slider
 * @param member Member of the mut
 * @param expect The value as the get command prints it
 */
static void gallery_expect_mut(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    const char *tmpl,
    const char *member,
    const char *expect)
{
    char *line = flecs_asprintf("get %s box.widgets.%s.mut.%s", path, tmpl, member);
    gallery_expect(world, s, line, expect);
    ecs_os_free(line);
}

/** Frames to step after the input queue is drained: the widgets react in the frames that
 * handle the input, picking reports a pointer move a frame later. */
#define GALLERY_STEP (0)

/** Run the input commands queued so far and the frames the widgets react in.
 *
 * @param world The world
 * @param s The terminal session
 */
static void gallery_step(
    ecs_world_t *world,
    ecs_entity_t s)
{
    gallery_settle_frames(world, s, GALLERY_STEP);
}

/** Press a key and run the input.
 *
 * @param world The world
 * @param s The terminal session
 * @param key The key
 */
static void gallery_press(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *key)
{
    ecs_os_free(gallery_run(world, s, "key_press %s", key));
    gallery_step(world, s);
}

/** Scroll a widget into view and click a place in it.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the widget
 * @param fx Place across the width, as gallery_point
 * @param fy Place across the height, as gallery_point
 */
static void gallery_tap(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    float fx,
    float fy)
{
    char point[64];
    gallery_reveal(world, s, path);
    gallery_point(world, path, fx, fy, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_click left %s", point));
    gallery_step(world, s);
}

/** Scroll a widget into view, move the pointer to a place in it and check the handler under it.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the widget
 * @param fx Place across the width, as gallery_point
 * @param fy Place across the height, as gallery_point
 * @param handler Path of the handler PickStatus must report there, "#0" for none
 */
static void gallery_over(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    float fx,
    float fy,
    const char *handler)
{
    char point[64];
    gallery_reveal(world, s, path);
    gallery_point(world, path, fx, fy, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle_frames(world, s, 1);
    char *expect = flecs_asprintf("\"%s\"", handler);
    gallery_expect(world, s, "get $ box.picking.PickStatus.handler", expect);
    ecs_os_free(expect);
}

/** Type text and run the input.
 *
 * @param world The world
 * @param s The terminal session
 * @param text The text, quoted as the key_type command takes it
 */
static void gallery_type(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *text)
{
    ecs_os_free(gallery_run(world, s, "key_type %s", text));
    gallery_step(world, s);
}

/** Press a key while the control key (command on macOS) is held.
 *
 * @param world The world
 * @param s The terminal session
 * @param key The key
 */
static void gallery_shortcut(
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
    gallery_press(world, s, key);
    ecs_os_free(gallery_run(world, s, "key_up %s", mod));
    gallery_step(world, s);
}

/** Press Tab and check the entity that gets the focus.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the entity
 */
static void gallery_tab_expect(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path)
{
    gallery_press(world, s, "Tab");
    gallery_expect_focus(world, s, path);
}

/** Click the track of a slider of the form at a ratio of its range.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the slider
 * @param ratio Place in the range, 0 for min and 1 for max
 */
static void gallery_slider_click(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    float ratio)
{
    gallery_reveal(world, s, path);
    const LayoutBounds *b = ecs_get(world, ecs_lookup(world, path), LayoutBounds);
    test_assert(b != NULL);
    float fy = 1.0f - GALLERY_THUMB / b->height;
    float fx = (-b->width * 0.5f + GALLERY_THUMB * 0.5f + ratio * (b->width - GALLERY_THUMB)) /
        (b->width * 0.5f);
    gallery_tap(world, s, path, fx, fy);
}

/** Check the shape the pointer cursor shows over a place of an element: the one PickStatus
 * reports and the one box.input shows.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the element
 * @param fx Place across the width, as gallery_point
 * @param fy Place across the height, as gallery_point
 * @param handler Path of the handler PickStatus must report there, "#0" for none
 * @param shape The shape
 */
static void gallery_expect_cursor(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    float fx,
    float fy,
    const char *handler,
    PointerCursor shape)
{
    gallery_over(world, s, path, fx, fy, handler);
    test_int(ecs_singleton_get(world, PickStatus)->shape, shape);
    test_int(box_input_cursor_get(world), shape);
}

/** Check the shape the pointer cursor shows at the place it is at.
 *
 * @param world The world
 * @param s The terminal session
 * @param handler Path of the handler PickStatus must report there, "#0" for none
 * @param shape The shape
 */
static void gallery_expect_cursor_here(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *handler,
    PointerCursor shape)
{
    gallery_settle_frames(world, s, 1);
    char *expect = flecs_asprintf("\"%s\"", handler);
    gallery_expect(world, s, "get $ box.picking.PickStatus.handler", expect);
    ecs_os_free(expect);
    test_int(ecs_singleton_get(world, PickStatus)->shape, shape);
    test_int(box_input_cursor_get(world), shape);
}

/** Check that the preview shows the defaults of the form: a box named Widget of size 1 in the
 * day light that casts a shadow.
 *
 * @param world The world
 * @param s The terminal session
 */
static void gallery_expect_preview_defaults(
    ecs_world_t *world,
    ecs_entity_t s)
{
    ecs_entity_t shape = ecs_lookup(world, PREVIEW ".shape");
    test_assert(shape != 0);
    gallery_expect(world, s, "get " PREVIEW ".label box.text.Text.value", "\"Widget\"");
    test_bool(ecs_has(world, shape, Box), true);
    test_bool(ecs_has(world, shape, Sphere), false);
    test_bool(ecs_has(world, shape, Torus), false);
    test_bool(ecs_has_id(world, shape, NoShadow), false);
    gallery_expect(world, s, "get " PREVIEW ".shape box.material.Rgb",
        "{\"Rgb\":{\"r\":230, \"g\":140, \"b\":60}}");
    gallery_expect(world, s, "get " PREVIEW ".shape box.transform.Scale",
        "{\"Scale\":{\"x\":1, \"y\":1, \"z\":1}}");
    gallery_expect(world, s, "get " PREVIEW ".sun box.material.Rgb",
        "{\"Rgb\":{\"r\":255, \"g\":246, \"b\":230}}");
    gallery_expect(world, s, "get " PREVIEW_VIEW " box.view.View.clear.color",
        "{\"r\":150, \"g\":186, \"b\":226, \"a\":255}");
}

/** Check that the widgets of the form show the defaults.
 *
 * @param world The world
 * @param s The terminal session
 */
static void gallery_expect_form_defaults(
    ecs_world_t *world,
    ecs_entity_t s)
{
    gallery_expect_mut(world, s, NAME, "TextInput", "current", "\"Widget\"");
    gallery_expect(world, s, "get " NAME ".field box.ui.TextEdit.value", "\"Widget\"");
    gallery_expect_mut(world, s, SHAPE, "Dropdown", "current", "0");
    gallery_expect(world, s, "get " SHAPE ".caption box.text.Text.value", "\"Box\"");
    gallery_expect_mut(world, s, RED, "Slider", "current", "230");
    gallery_expect_mut(world, s, GREEN, "Slider", "current", "140");
    gallery_expect_mut(world, s, BLUE, "Slider", "current", "60");
    gallery_expect(world, s, "get " RED ".head.amount box.text.Text.value", "\"230\"");
    gallery_expect_mut(world, s, SIZE, "Stepper", "current", "1");
    gallery_expect(world, s, "get " SIZE ".amount.digits box.text.Text.value", "\"1.00\"");
    gallery_expect_mut(world, s, SHADOW, "Checkbox", "current", "true");
    gallery_expect_mut(world, s, LIGHT, "RadioGroup", "current", "\"Day\"");
}

void Gallery_scene_loads(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *sections[] = { BASICS, TEXT, CONTAINERS, FORM };
    const char *titles[] = { "\"Basics\"", "\"Text\"", "\"Containers\"", "\"Form\"" };
    float page[4], r[4];
    int32_t i;

    gallery_expect(world, s, "logs warning 100", "");
    gallery_expect(world, s, "get " HEADER ".title box.text.Text.value", "\"Widgets\"");
    gallery_expect(world, s, "get " HEADER ".theme.caption box.text.Text.value", "\"Dark\"");
    gallery_expect_mut(world, s, HEADER ".theme", "Dropdown", "current", "0");
    gallery_expect(world, s, "get gallery.preview_surface box.surface.Surface.texture",
        "\"preview_texture\"");
    gallery_expect(world, s, "get " FORM ".body.preview.image box.geometry.Image.texture",
        "\"preview_texture\"");
    gallery_expect_focus(world, s, "#0");
    gallery_expect_form_defaults(world, s);
    gallery_expect_preview_defaults(world, s);

    /* At 1280x800 the sections do not fit below each other: the page scrolls each into view */
    const LayoutContent *c = ecs_get(world, ecs_lookup(world, PAGE), LayoutContent);
    test_assert(c != NULL);
    test_assert(c->max_y > 0);
    test_flt(c->max_x, 0);
    for (i = 0; i < 4; i ++) {
        char *line = flecs_asprintf("get %s.heading box.text.Text.value", sections[i]);
        gallery_expect(world, s, line, titles[i]);
        ecs_os_free(line);
        gallery_reveal(world, s, sections[i]);
        gallery_rect(world, PAGE, page);
        gallery_rect(world, sections[i], r);
        test_assert(r[0] >= page[0] - 0.5f);
        test_assert(r[2] <= page[2] + 0.5f);
        test_assert(r[1] >= page[1] - 0.5f);
        test_assert(r[3] <= page[3] + 0.5f);
    }

    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Gallery_theme_switch(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_expect(world, s, "get " BASICS ".buttons.save box.material.Rgb",
        "{\"Rgb\":{\"r\":98, \"g\":168, \"b\":122}}");
    gallery_expect(world, s, "get " TEXT ".name.field box.material.Rgb",
        "{\"Rgb\":{\"r\":38, \"g\":41, \"b\":48}}");
    gallery_expect(world, s, "get " BASICS " box.material.Rgb",
        "{\"Rgb\":{\"r\":46, \"g\":50, \"b\":58}}");
    gallery_expect(world, s, "get " FORM " box.material.Rgb",
        "{\"Rgb\":{\"r\":46, \"g\":50, \"b\":58}}");

    /* A click opens the theme dropdown, a click on Light picks it */
    gallery_tap(world, s, HEADER ".theme", 0, 0);
    gallery_expect_mut(world, s, HEADER ".theme", "Dropdown", "open", "true");
    gallery_tap(world, s, HEADER ".theme.list.option_1", 0, 0);
    gallery_expect_mut(world, s, HEADER ".theme", "Dropdown", "current", "1");
    gallery_expect(world, s, "get " HEADER ".theme.caption box.text.Text.value", "\"Light\"");
    gallery_expect(world, s, "get box.ui.theme box.ui.Theme.background",
        "{\"r\":232, \"g\":235, \"b\":240}");
    gallery_expect(world, s, "get " BASICS ".buttons.save box.material.Rgb",
        "{\"Rgb\":{\"r\":44, \"g\":128, \"b\":82}}");
    gallery_expect(world, s, "get " TEXT ".name.field box.material.Rgb",
        "{\"Rgb\":{\"r\":222, \"g\":226, \"b\":233}}");
    gallery_expect(world, s, "get " BASICS " box.material.Rgb",
        "{\"Rgb\":{\"r\":250, \"g\":251, \"b\":253}}");
    gallery_expect(world, s, "get " FORM " box.material.Rgb",
        "{\"Rgb\":{\"r\":250, \"g\":251, \"b\":253}}");
    gallery_expect(world, s, "get " HEADER ".title box.material.Rgb",
        "{\"Rgb\":{\"r\":33, \"g\":37, \"b\":43}}");
    gallery_expect(world, s, "get gallery.backdrop_view box.view.View.clear.color",
        "{\"r\":232, \"g\":235, \"b\":240, \"a\":255}");
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"theme: 1\"");

    /* The keys pick Dark again: Down opens the list at Light, Up highlights Dark, Enter */
    gallery_press(world, s, "Down");
    gallery_expect_mut(world, s, HEADER ".theme", "Dropdown", "open", "true");
    gallery_press(world, s, "Up");
    gallery_press(world, s, "Enter");
    gallery_expect_mut(world, s, HEADER ".theme", "Dropdown", "current", "0");
    gallery_expect_mut(world, s, HEADER ".theme", "Dropdown", "open", "false");
    gallery_expect(world, s, "get " BASICS ".buttons.save box.material.Rgb",
        "{\"Rgb\":{\"r\":98, \"g\":168, \"b\":122}}");
    gallery_expect(world, s, "get " TEXT ".name.field box.material.Rgb",
        "{\"Rgb\":{\"r\":38, \"g\":41, \"b\":48}}");
    gallery_expect(world, s, "get " BASICS " box.material.Rgb",
        "{\"Rgb\":{\"r\":46, \"g\":50, \"b\":58}}");
    gallery_expect(world, s, "get gallery.backdrop_view box.view.View.clear.color",
        "{\"r\":32, \"g\":35, \"b\":41, \"a\":255}");

    /* The widgets keep their state across the switch */
    gallery_expect_mut(world, s, BASICS ".toggles.music", "Checkbox", "current", "true");
    test_int(gallery_changes(world, s), 2);
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Gallery_form_apply(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    ecs_entity_t shape = ecs_lookup(world, PREVIEW ".shape");
    test_assert(shape != 0);

    /* Typing a name */
    gallery_tap(world, s, NAME ".field", 0, 0);
    gallery_shortcut(world, s, "A");
    gallery_type(world, s, "Ada");
    gallery_expect_mut(world, s, NAME, "TextInput", "current", "\"Ada\"");

    /* Picking a shape */
    gallery_tap(world, s, SHAPE, 0, 0);
    gallery_tap(world, s, SHAPE ".list.option_2", 0, 0);
    gallery_expect_mut(world, s, SHAPE, "Dropdown", "current", "2");

    /* Moving the sliders: a click at either end of a track, a drag of the knob */
    gallery_slider_click(world, s, RED, 1);
    gallery_slider_click(world, s, GREEN, 0);
    gallery_expect_mut(world, s, RED, "Slider", "current", "255");
    gallery_expect_mut(world, s, GREEN, "Slider", "current", "0");
    gallery_reveal(world, s, BLUE);
    const LayoutBounds *b = ecs_get(world, ecs_lookup(world, BLUE), LayoutBounds);
    float fy = 1.0f - GALLERY_THUMB / b->height;
    char from[64], to[64];
    gallery_point(world, BLUE ".track.knob", 0, 0, from, sizeof(from));
    gallery_point(world, BLUE, 0.98f, fy, to, sizeof(to));
    ecs_os_free(gallery_run(world, s, "mouse_drag left %s %s", from, to));
    gallery_step(world, s);
    double blue = gallery_value(world, s, "get " BLUE " box.widgets.Slider.mut.current");
    test_assert(blue > 240 && blue < 255.5);

    /* Stepping the size, unchecking the shadow, picking the night light */
    gallery_tap(world, s, SIZE, GALLERY_PLUS, 0);
    gallery_expect_mut(world, s, SIZE, "Stepper", "current", "1.25");
    gallery_tap(world, s, SHADOW, -0.8f, 0);
    gallery_expect_mut(world, s, SHADOW, "Checkbox", "current", "false");
    gallery_tap(world, s, LIGHT ".night", 0, 0);
    gallery_expect_mut(world, s, LIGHT, "RadioGroup", "current", "\"Night\"");

    /* Nothing applies before Apply */
    gallery_expect_preview_defaults(world, s);
    gallery_expect(world, s, "get gallery Gallery.mut.draft.name", "\"Ada\"");
    gallery_expect(world, s, "get gallery Gallery.mut.draft.shape", "2");
    gallery_expect(world, s, "get gallery Gallery.mut.draft.size", "1.25");

    gallery_tap(world, s, APPLY, 0, 0);
    gallery_expect(world, s, "get " PREVIEW ".label box.text.Text.value", "\"Ada\"");
    test_bool(ecs_has(world, shape, Torus), true);
    test_bool(ecs_has(world, shape, Box), false);
    test_bool(ecs_has(world, shape, Sphere), false);
    const Rgb *rgb = ecs_get(world, shape, Rgb);
    test_assert(rgb != NULL);
    test_int(rgb->r, 255);
    test_int(rgb->g, 0);
    test_int(rgb->b, (int32_t)blue);
    gallery_expect(world, s, "get " PREVIEW ".shape box.transform.Scale",
        "{\"Scale\":{\"x\":1.25, \"y\":1.25, \"z\":1.25}}");
    test_bool(ecs_has_id(world, shape, NoShadow), true);
    gallery_expect(world, s, "get " PREVIEW ".sun box.material.Rgb",
        "{\"Rgb\":{\"r\":150, \"g\":170, \"b\":255}}");
    char *intensity = gallery_run(world, s,
        "get " PREVIEW ".sun box.light.DirectionalLight.intensity");
    test_assert(fabs(strtod(intensity, NULL) - 0.6 * GLM_PI) < 1e-5);
    ecs_os_free(intensity);
    gallery_expect(world, s, "get " PREVIEW_VIEW " box.view.View.clear.color",
        "{\"r\":16, \"g\":20, \"b\":38, \"a\":255}");
    gallery_expect(world, s, "get " FORM ".body.preview.applied_text box.text.Text.value",
        "\"Ada: Torus, size 1.25, Night light\"");

    /* The form keeps its values after Apply, a second Apply changes nothing */
    gallery_expect_mut(world, s, NAME, "TextInput", "current", "\"Ada\"");
    gallery_expect_mut(world, s, SHAPE, "Dropdown", "current", "2");
    gallery_tap(world, s, APPLY, 0, 0);
    test_bool(ecs_has(world, shape, Torus), true);
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Gallery_form_reset(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    ecs_entity_t shape = ecs_lookup(world, PREVIEW ".shape");

    gallery_tap(world, s, NAME ".field", 0, 0);
    gallery_shortcut(world, s, "A");
    gallery_type(world, s, "Grace");
    gallery_tap(world, s, SHAPE, 0, 0);
    gallery_tap(world, s, SHAPE ".list.option_1", 0, 0);
    gallery_slider_click(world, s, RED, 0);
    gallery_slider_click(world, s, BLUE, 1);
    gallery_tap(world, s, SIZE, GALLERY_PLUS, 0);
    gallery_tap(world, s, SHADOW, -0.8f, 0);
    gallery_tap(world, s, LIGHT ".dusk", 0, 0);
    gallery_tap(world, s, APPLY, 0, 0);
    test_bool(ecs_has(world, shape, Sphere), true);
    gallery_expect(world, s, "get " PREVIEW ".label box.text.Text.value", "\"Grace\"");

    /* Reset puts the defaults back in the widgets, without a change of their own */
    int32_t changes = gallery_changes(world, s);
    gallery_tap(world, s, RESET, 0, 0);
    test_int(gallery_changes(world, s), changes + 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"reset\"");
    gallery_expect_form_defaults(world, s);
    gallery_expect(world, s, "get " SHADOW ".box box.material.Rgb",
        "{\"Rgb\":{\"r\":98, \"g\":168, \"b\":122}}");
    test_assert(ecs_lookup(world, LIGHT ".day.indicator.dot") != 0);
    test_assert(ecs_lookup(world, LIGHT ".dusk.indicator.dot") == 0);

    /* The preview keeps the applied values until Apply */
    test_bool(ecs_has(world, shape, Sphere), true);
    gallery_expect(world, s, "get " PREVIEW ".label box.text.Text.value", "\"Grace\"");
    gallery_tap(world, s, APPLY, 0, 0);
    gallery_expect_preview_defaults(world, s);

    /* The reset widgets work as before: a change after Reset is read */
    gallery_slider_click(world, s, GREEN, 1);
    gallery_expect_mut(world, s, GREEN, "Slider", "current", "255");
    gallery_expect(world, s, "get gallery Gallery.mut.draft.color.g", "255");
    gallery_tap(world, s, RESET, 0, 0);
    gallery_expect_mut(world, s, GREEN, "Slider", "current", "140");
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Gallery_form_enter_applies(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    ecs_entity_t shape = ecs_lookup(world, PREVIEW ".shape");

    gallery_tap(world, s, SHAPE, 0, 0);
    gallery_tap(world, s, SHAPE ".list.option_1", 0, 0);
    gallery_tap(world, s, NAME ".field", 0.9f, 0);
    gallery_type(world, s, "s");
    gallery_expect(world, s, "get " PREVIEW ".label box.text.Text.value", "\"Widget\"");
    test_bool(ecs_has(world, shape, Box), true);

    gallery_press(world, s, "Enter");
    gallery_expect(world, s, "get " PREVIEW ".label box.text.Text.value", "\"Widgets\"");
    test_bool(ecs_has(world, shape, Sphere), true);
    gallery_expect_focus(world, s, NAME ".field");

    /* Enter in a field of another section applies nothing */
    gallery_tap(world, s, SHAPE, 0, 0);
    gallery_tap(world, s, SHAPE ".list.option_2", 0, 0);
    gallery_tap(world, s, TEXT ".submit.message.field", 0, 0);
    gallery_type(world, s, "hi");
    gallery_press(world, s, "Enter");
    gallery_expect(world, s, "get gallery Gallery.mut.submitted", "\"hi\"");
    test_bool(ecs_has(world, shape, Sphere), true);

    ecs_fini(world);
}

void Gallery_tab_order(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *sections[] = { BASICS, TEXT, CONTAINERS, FORM };
    int32_t i, section = 0;
    bool found = false;

    /* From nothing focused Tab starts at the theme switch of the header and follows the
     * reading order of the sections to the form */
    gallery_expect_focus(world, s, "#0");
    gallery_tab_expect(world, s, HEADER ".theme");
    gallery_tab_expect(world, s, BASICS ".buttons.save");
    for (i = 0; i < 80 && !found; i ++) {
        gallery_press(world, s, "Tab");
        char *focused = gallery_focused(world, s);
        while (section < 3 && strncmp(focused, sections[section], strlen(sections[section]))) {
            section ++;
        }

        if (strncmp(focused, sections[section], strlen(sections[section]))) {
            printf("Tab left the reading order at '%s'\n", focused);
        }

        test_assert(strncmp(focused, sections[section], strlen(sections[section])) == 0);
        found = !strcmp(focused, NAME ".field");
        ecs_os_free(focused);
    }

    test_assert(found);
    gallery_expect(world, s, "get $ box.ui.FocusStatus.visible", "true");

    /* The focused field is scrolled into the page view */
    float page[4], r[4];
    gallery_rect(world, PAGE, page);
    gallery_rect(world, NAME ".field", r);
    test_assert(r[1] >= page[1] && r[3] <= page[3]);
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Gallery_keyboard_only(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    ecs_entity_t shape = ecs_lookup(world, PREVIEW ".shape");
    int32_t i;
    bool found = false;

    /* From nothing focused Shift+Tab wraps to the last widget in reading order, Reset, and walks
     * back through the form to its name field */
    gallery_expect_focus(world, s, "#0");
    ecs_os_free(gallery_run(world, s, "key_down ShiftLeft"));
    gallery_press(world, s, "Tab");
    gallery_expect_focus(world, s, RESET);
    for (i = 0; i < 12 && !found; i ++) {
        gallery_press(world, s, "Tab");
        char *focused = gallery_focused(world, s);
        test_assert(strncmp(focused, FORM, strlen(FORM)) == 0);
        found = !strcmp(focused, NAME ".field");
        ecs_os_free(focused);
    }

    ecs_os_free(gallery_run(world, s, "key_up ShiftLeft"));
    gallery_step(world, s);
    test_assert(found);
    gallery_expect(world, s, "get $ box.ui.FocusStatus.visible", "true");

    /* The focused field is scrolled into the page view */
    float page[4], r[4];
    gallery_rect(world, PAGE, page);
    gallery_rect(world, NAME ".field", r);
    test_assert(r[1] >= page[1] && r[3] <= page[3]);

    /* Every widget of the form by keys */
    gallery_shortcut(world, s, "A");
    gallery_type(world, s, "Kim");
    gallery_tab_expect(world, s, SHAPE);
    gallery_press(world, s, "Down");
    gallery_expect_mut(world, s, SHAPE, "Dropdown", "open", "true");
    gallery_press(world, s, "Down");
    gallery_press(world, s, "Enter");
    gallery_expect_mut(world, s, SHAPE, "Dropdown", "current", "1");
    gallery_tab_expect(world, s, RED);
    gallery_press(world, s, "Home");
    gallery_tab_expect(world, s, GREEN);
    gallery_press(world, s, "End");
    gallery_tab_expect(world, s, BLUE);
    gallery_press(world, s, "Right");
    gallery_press(world, s, "Right");
    gallery_tab_expect(world, s, SIZE);
    gallery_press(world, s, "Up");
    gallery_press(world, s, "Up");
    gallery_tab_expect(world, s, SHADOW);
    gallery_press(world, s, "Space");
    gallery_tab_expect(world, s, LIGHT ".day");
    gallery_press(world, s, "Right");
    gallery_expect_focus(world, s, LIGHT ".dusk");
    gallery_tab_expect(world, s, APPLY);
    gallery_tab_expect(world, s, RESET);
    ecs_os_free(gallery_run(world, s, "key_down ShiftLeft"));
    gallery_press(world, s, "Tab");
    ecs_os_free(gallery_run(world, s, "key_up ShiftLeft"));
    gallery_step(world, s);
    gallery_expect_focus(world, s, APPLY);

    gallery_expect_mut(world, s, NAME, "TextInput", "current", "\"Kim\"");
    gallery_expect_mut(world, s, RED, "Slider", "current", "0");
    gallery_expect_mut(world, s, GREEN, "Slider", "current", "255");
    gallery_expect_mut(world, s, BLUE, "Slider", "current", "62");
    gallery_expect_mut(world, s, SIZE, "Stepper", "current", "1.5");
    gallery_expect_mut(world, s, SHADOW, "Checkbox", "current", "false");
    gallery_expect_mut(world, s, LIGHT, "RadioGroup", "current", "\"Dusk\"");
    gallery_expect_preview_defaults(world, s);

    /* Apply with Enter */
    gallery_press(world, s, "Enter");
    gallery_expect(world, s, "get " PREVIEW ".label box.text.Text.value", "\"Kim\"");
    test_bool(ecs_has(world, shape, Sphere), true);
    gallery_expect(world, s, "get " PREVIEW ".shape box.material.Rgb",
        "{\"Rgb\":{\"r\":0, \"g\":255, \"b\":62}}");
    gallery_expect(world, s, "get " PREVIEW ".shape box.transform.Scale",
        "{\"Scale\":{\"x\":1.5, \"y\":1.5, \"z\":1.5}}");
    test_bool(ecs_has_id(world, shape, NoShadow), true);
    gallery_expect(world, s, "get " PREVIEW ".sun box.material.Rgb",
        "{\"Rgb\":{\"r\":255, \"g\":150, \"b\":90}}");
    gallery_expect(world, s, "get " PREVIEW_VIEW " box.view.View.clear.color",
        "{\"r\":196, \"g\":128, \"b\":110, \"a\":255}");

    /* Reset with Space restores the widgets, the focus stays on it */
    gallery_press(world, s, "Tab");
    gallery_press(world, s, "Space");
    gallery_expect_focus(world, s, RESET);
    gallery_expect_form_defaults(world, s);
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Gallery_cursors(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float text[4], page[4];
    char point[64];

    gallery_expect_cursor(world, s, BASICS ".buttons.save", 0, 0, BASICS ".buttons.save",
        CursorPointer);
    gallery_expect_cursor(world, s, TEXT ".name.field", 0, 0, TEXT ".name.field", CursorText);
    gallery_expect_cursor(world, s, BASICS ".buttons.off", 0, 0, PAGE, CursorNotAllowed);
    gallery_expect_cursor(world, s, TEXT ".off.field", 0, 0, PAGE, CursorNotAllowed);
    gallery_expect_cursor(world, s, BASICS ".toggles.locked", -0.8f, 0, PAGE,
        CursorNotAllowed);
    gallery_expect_cursor(world, s, BASICS ".toggles.sound", -0.8f, 0, BASICS ".toggles.sound",
        CursorPointer);
    gallery_expect_cursor(world, s, BASICS ".toggles.wifi", -0.8f, 0, BASICS ".toggles.wifi",
        CursorPointer);
    gallery_expect_cursor(world, s, BASICS ".radios.size.small", 0, 0,
        BASICS ".radios.size.small", CursorPointer);
    gallery_expect_cursor(world, s, BASICS ".sliders.volume", 0, 0.6f, BASICS ".sliders.volume",
        CursorPointer);
    gallery_expect_cursor(world, s, HEADER ".theme", 0, 0, HEADER ".theme", CursorPointer);

    /* Empty space: the page right of the Text panel and the gap in the header */
    gallery_show(world, s, BASICS);
    gallery_rect(world, TEXT, text);
    gallery_rect(world, PAGE, page);
    test_assert(page[2] - text[2] > 100);
    ecs_os_free(gallery_run(world, s, "mouse_move [%.1f, %.1f]",
        (double)((text[2] + page[2]) * 0.5f), (double)((text[1] + text[3]) * 0.5f)));
    gallery_expect_cursor_here(world, s, PAGE, CursorDefault);
    gallery_point(world, HEADER ".gap", 0, 0, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_expect_cursor_here(world, s, "#0", CursorDefault);

    /* The containers and the form */
    gallery_expect_cursor(world, s, CONTAINERS ".tabs.bar.tab_1", 0, 0,
        CONTAINERS ".tabs.bar.tab_1", CursorPointer);
    gallery_expect_cursor(world, s, CONTAINERS ".notes.files.rows.row_1", 0, 0,
        CONTAINERS ".notes.files", CursorPointer);
    gallery_expect_cursor(world, s, CONTAINERS ".notes", 0.98f, -0.9f, CONTAINERS ".notes",
        CursorPointer);
    gallery_expect_cursor(world, s, CONTAINERS ".notes.intro", 0, 0, CONTAINERS ".notes",
        CursorDefault);
    gallery_expect_cursor(world, s, CONTAINERS ".details.head", 0, 0, CONTAINERS ".details",
        CursorPointer);
    gallery_expect_cursor(world, s, SHAPE, 0, 0, SHAPE, CursorPointer);
    gallery_expect_cursor(world, s, SIZE, GALLERY_PLUS, 0, SIZE, CursorPointer);
    gallery_expect_cursor(world, s, NAME ".field", 0, 0, NAME ".field", CursorText);
    gallery_expect_cursor(world, s, APPLY, 0, 0, APPLY, CursorPointer);

    ecs_fini(world);
}

void Gallery_resize(void) {
    // widgets/gallery
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    ecs_entity_t window = ecs_lookup(world, "window");
    float header[4], moved[4], page[4], basics[4], after[4], theme[4];
    int32_t i;

    gallery_rect(world, HEADER, header);
    gallery_expect_at(header[1], 16);

    Surface *surface = ecs_get_mut(world, window, Surface);
    surface->width = 800;
    surface->height = 600;
    ecs_modified(world, window, Surface);
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    test_int(ss->point_width, 800);
    test_int(ss->point_height, 600);

    /* The header stays at the top across the window, the page takes the rest */
    gallery_rect(world, HEADER, moved);
    gallery_rect(world, HEADER ".theme", theme);
    gallery_rect(world, PAGE, page);
    gallery_expect_at(moved[1], 16);
    gallery_expect_at(moved[0], 16);
    gallery_expect_at(moved[2], 784);
    gallery_expect_at(theme[2], 784);
    test_assert(page[1] >= moved[3]);
    gallery_expect_at(page[3], 584);
    gallery_expect_at(page[2], 784);

    /* The content scrolls both ways where it does not fit */
    const LayoutContent *c = ecs_get(world, ecs_lookup(world, PAGE), LayoutContent);
    test_assert(c->max_x > 300);
    test_assert(c->max_y > 500);
    test_assert(ecs_lookup(world, PAGE ".scrollbars.vscroll") != 0);
    test_assert(ecs_lookup(world, PAGE ".scrollbars.hscroll") != 0);

    /* The wheel over the page scrolls it, the header stays */
    gallery_rect(world, BASICS, basics);
    ecs_os_free(gallery_run(world, s, "mouse_move [%.1f, %.1f]",
        (double)(basics[0] + 40), (double)(basics[1] + 10)));
    gallery_step(world, s);
    gallery_expect(world, s, "get $ box.picking.PickStatus.handler", "\"" PAGE "\"");
    ecs_os_free(gallery_run(world, s, "mouse_scroll 0 -3"));
    gallery_step(world, s);
    gallery_rect(world, BASICS, after);
    gallery_expect_at(after[1], basics[1] - 120);
    gallery_rect(world, HEADER, moved);
    gallery_expect_at(moved[1], 16);

    /* Every section scrolls into view, a widget at the far end too */
    gallery_reveal(world, s, FORM);
    gallery_rect(world, FORM, after);
    test_assert(after[2] <= 784 - 11 + 0.5f);
    gallery_tap(world, s, APPLY, 0, 0);
    gallery_expect_focus(world, s, APPLY);
    gallery_rect(world, HEADER, moved);
    gallery_expect_at(moved[1], 16);

    /* Back to 1280x800 the page no longer scrolls sideways */
    surface = ecs_get_mut(world, window, Surface);
    surface->width = 1280;
    surface->height = 800;
    ecs_modified(world, window, Surface);
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    c = ecs_get(world, ecs_lookup(world, PAGE), LayoutContent);
    test_flt(c->max_x, 0);
    test_flt(c->x, 0);
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}
