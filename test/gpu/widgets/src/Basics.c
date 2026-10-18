/**
 * @file Basics.c
 * @brief The Basics section of the widget gallery driven through the command API: buttons,
 * check boxes, switches, radio buttons, sliders and steppers by pointer and keyboard, disabled
 * widgets, Tab, the focus ring, the theme and muts set from the terminal.
 */

#include "gallery.h"
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>

/** Width of the thumb of a gallery slider, the knob center stays thumb / 2 from either edge */
#define BASICS_THUMB (16.0f)

/** Button parts of a gallery stepper, 28 points wide on a stepper 112 points wide */
#define BASICS_MINUS (-0.75f)
#define BASICS_PLUS (0.75f)

/** Get the place across a slider of the knob center at a ratio of its range, as gallery_point.
 *
 * @param world The world
 * @param path Path of the slider
 * @param ratio Place in the range, 0 for min and 1 for max; outside it past the ends
 * @param fy Receives the place across the height of the middle of the track
 * @return The place across the width
 */
static float basics_slider_at(
    ecs_world_t *world,
    const char *path,
    float ratio,
    float *fy)
{
    const LayoutBounds *b = ecs_get(world, ecs_lookup(world, path), LayoutBounds);
    test_assert(b != NULL);
    *fy = 1.0f - BASICS_THUMB / b->height;

    return (-b->width * 0.5f + BASICS_THUMB * 0.5f + ratio * (b->width - BASICS_THUMB)) /
        (b->width * 0.5f);
}

/** Get the window point of the knob center of a slider at a ratio of its range.
 *
 * @param world The world
 * @param path Path of the slider
 * @param ratio Place in the range
 * @param out Receives the point as [x, y]
 * @param size Bytes of out
 */
static void basics_slider_point(
    ecs_world_t *world,
    const char *path,
    float ratio,
    char *out,
    size_t size)
{
    float fy, fx = basics_slider_at(world, path, ratio, &fy);
    gallery_point(world, path, fx, fy, out, size);
}

/** Tab until a widget has the focus.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the widget
 */
static void basics_tab_to(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path)
{
    int32_t i;
    bool found = false;
    for (i = 0; i < 60 && !found; i ++) {
        gallery_key(world, s, "Tab");
        char *focused = gallery_focused(world, s);
        found = !strcmp(focused, path);
        ecs_os_free(focused);
    }

    test_assert(found);
}

/** Check the entity with the focus.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Its path, #0 for none
 */
static void basics_expect_focus(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path)
{
    char *focused = gallery_focused(world, s);
    test_str(focused, path);
    ecs_os_free(focused);
}

/** Check a member of the mut of a widget.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the widget below the Basics panel
 * @param tmpl Template of the widget
 * @param member Member of the mut
 * @param expect The value as the get command prints it
 */
static void basics_expect_mut(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    const char *tmpl,
    const char *member,
    const char *expect)
{
    char *out = gallery_mut(world, s, path, tmpl, member);
    test_str(out, expect);
    ecs_os_free(out);
}

void Basics_scene_loads(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_expect(world, s, "logs warning 100", "");
    gallery_expect(world, s, "get gallery Gallery.mut",
        "{last: \"none\", changes: 0, pings: 0, submitted: \"\"}");
    gallery_expect(world, s, "get gallery.hud.root.header.status Text.value",
        "\"Last change: none\"");
    gallery_expect(world, s, "get " BASICS ".heading Text.value", "\"Basics\"");
    gallery_expect(world, s, "get " BASICS ".heading Rgb", "{r: 214, g: 218, b: 224}");
    gallery_expect(world, s, "get " BASICS ".buttons.save Rgb", "{r: 98, g: 168, b: 122}");
    gallery_expect(world, s, "get " BASICS ".buttons.save.caption Text.value", "\"Save\"");
    gallery_expect(world, s, "get gallery.backdrop_view View.clear.color",
        "{r: 32, g: 35, b: 41, a: 255}");
    basics_expect_mut(world, s, "toggles.music", "Checkbox", "current", "true");
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Medium\"");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "40");
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "2");
    basics_expect_mut(world, s, "progress.download", "ProgressBar", "current", "0.6499999762");
    gallery_expect(world, s, "get " BASICS ".sliders.volume.head.amount Text.value", "\"40%\"");
    gallery_expect(world, s, "get " BASICS ".progress.download.head.amount Text.value",
        "\"65%\"");
    basics_expect_focus(world, s, "#0");

    const Position *labels = ecs_get(world, ecs_lookup(world, BASICS ".labels"), Position);
    const Position *sep = ecs_get(world, ecs_lookup(world, BASICS ".labels_end"), Position);
    const Position *buttons = ecs_get(world, ecs_lookup(world, BASICS ".buttons"), Position);
    test_assert(labels && sep && buttons);
    test_assert(labels->y > sep->y);
    test_assert(sep->y > buttons->y);

    ecs_fini(world);
}

void Basics_button_click(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_hover(world, s, BASICS ".buttons.save", 0, 0, BASICS ".buttons.save");
    basics_expect_mut(world, s, "buttons.save", "Button", "hover", "true");
    gallery_click(world, s, BASICS ".buttons.save", 0, 0, BASICS ".buttons.save");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"save\"");
    gallery_expect(world, s, "get gallery.hud.root.header.status Text.value",
        "\"Last change: save\"");
    basics_expect_focus(world, s, BASICS ".buttons.save");

    gallery_click(world, s, BASICS ".buttons.cancel", 0.5f, 0, BASICS ".buttons.cancel");
    test_int(gallery_changes(world, s), 2);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"cancel\"");
    gallery_expect(world, s, "get gallery Gallery.mut.pings", "0");

    ecs_fini(world);
}

void Basics_button_enter(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    basics_tab_to(world, s, BASICS ".buttons.more");
    test_int(gallery_changes(world, s), 0);
    gallery_key(world, s, "Enter");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"more\"");
    gallery_key(world, s, "Enter");
    test_int(gallery_changes(world, s), 2);

    ecs_fini(world);
}

void Basics_button_space(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    basics_tab_to(world, s, BASICS ".buttons.remove");
    ecs_os_free(gallery_run(world, s, "key.down Space"));
    gallery_settle(world, s);
    basics_expect_mut(world, s, "buttons.remove", "Button", "down", "true");
    test_int(gallery_changes(world, s), 0);
    ecs_os_free(gallery_run(world, s, "key.up Space"));
    gallery_settle(world, s);
    basics_expect_mut(world, s, "buttons.remove", "Button", "down", "false");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"remove\"");

    ecs_fini(world);
}

void Basics_button_action(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_click(world, s, BASICS ".buttons.ping", 0, 0, BASICS ".buttons.ping");
    gallery_expect(world, s, "get gallery Gallery.mut.pings", "1");
    test_int(gallery_changes(world, s), 0);

    basics_tab_to(world, s, BASICS ".buttons.ping");
    gallery_key(world, s, "Enter");
    gallery_expect(world, s, "get gallery Gallery.mut.pings", "2");
    gallery_key(world, s, "Space");
    gallery_expect(world, s, "get gallery Gallery.mut.pings", "3");
    test_int(gallery_changes(world, s), 0);

    ecs_fini(world);
}

void Basics_checkbox_click(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *sound = BASICS ".toggles.sound";

    gallery_click(world, s, sound, -0.8f, 0, sound);
    basics_expect_mut(world, s, "toggles.sound", "Checkbox", "current", "true");
    test_assert(ecs_lookup(world, BASICS ".toggles.sound.box.mark") != 0);
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"sound: true\"");

    gallery_click(world, s, sound, 0.6f, 0, sound);
    basics_expect_mut(world, s, "toggles.sound", "Checkbox", "current", "false");
    test_assert(ecs_lookup(world, BASICS ".toggles.sound.box.mark") == 0);
    test_int(gallery_changes(world, s), 2);

    gallery_click(world, s, BASICS ".toggles.music", 0, 0, BASICS ".toggles.music");
    basics_expect_mut(world, s, "toggles.music", "Checkbox", "current", "false");
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"music: false\"");

    ecs_fini(world);
}

void Basics_checkbox_space(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    basics_tab_to(world, s, BASICS ".toggles.sound");
    gallery_key(world, s, "Space");
    basics_expect_mut(world, s, "toggles.sound", "Checkbox", "current", "true");
    test_int(gallery_changes(world, s), 1);
    gallery_key(world, s, "Space");
    basics_expect_mut(world, s, "toggles.sound", "Checkbox", "current", "false");
    test_int(gallery_changes(world, s), 2);
    basics_expect_focus(world, s, BASICS ".toggles.sound");

    ecs_fini(world);
}

void Basics_switch_click(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *wifi = BASICS ".toggles.wifi";
    const Position *knob = NULL;

    knob = ecs_get(world, ecs_lookup(world, BASICS ".toggles.wifi.track.knob"), Position);
    test_assert(knob != NULL);
    test_assert(knob->x < 0);
    gallery_click(world, s, wifi, -0.6f, 0, wifi);
    basics_expect_mut(world, s, "toggles.wifi", "Switch", "current", "true");
    knob = ecs_get(world, ecs_lookup(world, BASICS ".toggles.wifi.track.knob"), Position);
    test_assert(knob->x > 0);
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"wifi: true\"");

    gallery_click(world, s, wifi, 0.6f, 0, wifi);
    basics_expect_mut(world, s, "toggles.wifi", "Switch", "current", "false");
    test_int(gallery_changes(world, s), 2);

    ecs_fini(world);
}

void Basics_switch_space(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    basics_tab_to(world, s, BASICS ".toggles.wifi");
    gallery_key(world, s, "Space");
    basics_expect_mut(world, s, "toggles.wifi", "Switch", "current", "true");
    gallery_key(world, s, "Space");
    basics_expect_mut(world, s, "toggles.wifi", "Switch", "current", "false");
    test_int(gallery_changes(world, s), 2);

    ecs_fini(world);
}

void Basics_radio_click(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *large = BASICS ".radios.size.large";

    test_assert(ecs_lookup(world, BASICS ".radios.size.medium.indicator.dot") != 0);
    gallery_click(world, s, large, 0, 0, large);
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Large\"");
    test_assert(ecs_lookup(world, BASICS ".radios.size.large.indicator.dot") != 0);
    test_assert(ecs_lookup(world, BASICS ".radios.size.medium.indicator.dot") == 0);
    basics_expect_focus(world, s, large);
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"size: Large\"");

    gallery_click(world, s, large, 0.5f, 0, large);
    test_int(gallery_changes(world, s), 1);

    gallery_click(world, s, BASICS ".radios.size.small", 0, 0, BASICS ".radios.size.small");
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Small\"");
    test_int(gallery_changes(world, s), 2);

    ecs_fini(world);
}

void Basics_radio_arrows(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    basics_tab_to(world, s, BASICS ".radios.size.medium");
    gallery_key(world, s, "Down");
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Large\"");
    basics_expect_focus(world, s, BASICS ".radios.size.large");
    test_int(gallery_changes(world, s), 1);

    gallery_key(world, s, "Down");
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Small\"");
    basics_expect_focus(world, s, BASICS ".radios.size.small");
    gallery_expect(world, s, "get box.ui.FocusStatus FocusStatus.previous",
        BASICS ".radios.size.large");

    gallery_key(world, s, "Up");
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Large\"");
    gallery_key(world, s, "Left");
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Medium\"");
    gallery_key(world, s, "Right");
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Large\"");
    basics_expect_focus(world, s, BASICS ".radios.size.large");
    test_int(gallery_changes(world, s), 5);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"size: Large\"");

    ecs_fini(world);
}

void Basics_radio_one_tab_stop(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    test_bool(ecs_has_id(world, ecs_lookup(world, BASICS ".radios.size.small"), Focusable),
        false);
    test_bool(ecs_has_id(world, ecs_lookup(world, BASICS ".radios.size.medium"), Focusable),
        true);
    test_bool(ecs_has_id(world, ecs_lookup(world, BASICS ".radios.size.large"), Focusable),
        false);

    basics_tab_to(world, s, BASICS ".radios.size.medium");
    gallery_key(world, s, "Tab");
    basics_expect_focus(world, s, BASICS ".sliders.volume");
    ecs_os_free(gallery_run(world, s, "key.down ShiftLeft"));
    gallery_key(world, s, "Tab");
    ecs_os_free(gallery_run(world, s, "key.up ShiftLeft"));
    gallery_settle(world, s);
    basics_expect_focus(world, s, BASICS ".radios.size.medium");

    gallery_key(world, s, "Up");
    basics_expect_focus(world, s, BASICS ".radios.size.small");
    test_bool(ecs_has_id(world, ecs_lookup(world, BASICS ".radios.size.medium"), Focusable),
        false);
    gallery_key(world, s, "Tab");
    basics_expect_focus(world, s, BASICS ".sliders.volume");

    ecs_fini(world);
}

void Basics_slider_drag(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *volume = BASICS ".sliders.volume";
    char from[64], to[64];
    float fy, fx = basics_slider_at(world, volume, 0.4f, &fy);

    gallery_hover(world, s, volume, fx, fy, volume);
    basics_slider_point(world, volume, 0.4f, from, sizeof(from));
    basics_slider_point(world, volume, 0.8f, to, sizeof(to));
    ecs_os_free(gallery_run(world, s, "mouse.drag left %s %s", from, to));
    gallery_settle(world, s);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "80");
    gallery_expect(world, s, "get " BASICS ".sliders.volume.head.amount Text.value", "\"80%\"");
    test_assert(gallery_changes(world, s) >= 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"volume: 80.00\"");
    basics_expect_focus(world, s, volume);

    basics_slider_point(world, volume, 0.8f, from, sizeof(from));
    basics_slider_point(world, volume, 0.2f, to, sizeof(to));
    ecs_os_free(gallery_run(world, s, "mouse.drag left %s %s", from, to));
    gallery_settle(world, s);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "20");

    const Position *knob = ecs_get(world, ecs_lookup(world, BASICS ".sliders.volume.track.knob"),
        Position);
    const LayoutBounds *b = ecs_get(world, ecs_lookup(world, volume), LayoutBounds);
    test_assert(knob != NULL);
    test_flt(knob->x, -b->width * 0.5f + BASICS_THUMB * 0.5f + 0.2f * (b->width - BASICS_THUMB));

    ecs_fini(world);
}

void Basics_slider_track_click(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *volume = BASICS ".sliders.volume";
    float fy, fx = basics_slider_at(world, volume, 0.9f, &fy);

    gallery_click(world, s, volume, fx, fy, volume);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "90");
    test_int(gallery_changes(world, s), 1);

    fx = basics_slider_at(world, volume, 0.1f, &fy);
    gallery_click(world, s, volume, fx, fy, volume);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "10");
    test_int(gallery_changes(world, s), 2);

    gallery_click(world, s, volume, 0.5f, -0.8f, volume);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "10");
    test_int(gallery_changes(world, s), 2);

    gallery_click(world, s, volume, fx, fy, volume);
    test_int(gallery_changes(world, s), 2);

    ecs_fini(world);
}

void Basics_slider_arrows(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    basics_tab_to(world, s, BASICS ".sliders.volume");
    gallery_key(world, s, "Right");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "50");
    gallery_key(world, s, "Up");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "60");
    gallery_key(world, s, "Left");
    gallery_key(world, s, "Down");
    gallery_key(world, s, "Down");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "30");
    gallery_key(world, s, "PageUp");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "40");
    gallery_key(world, s, "PageDown");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "30");
    test_int(gallery_changes(world, s), 7);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"volume: 30.00\"");

    gallery_key(world, s, "Tab");
    basics_expect_focus(world, s, BASICS ".sliders.gain");
    gallery_key(world, s, "Right");
    test_flt(gallery_number(world, s, "sliders.gain", "Slider", "current"), 0.51);
    gallery_expect(world, s, "get " BASICS ".sliders.gain.head.amount Text.value", "\"0.51\"");
    gallery_key(world, s, "PageDown");
    test_flt(gallery_number(world, s, "sliders.gain", "Slider", "current"), 0.41);

    ecs_fini(world);
}

void Basics_slider_home_end(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    basics_tab_to(world, s, BASICS ".sliders.volume");
    gallery_key(world, s, "End");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "100");
    gallery_key(world, s, "Home");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "0");
    test_int(gallery_changes(world, s), 2);
    gallery_expect(world, s, "get " BASICS ".sliders.volume.head.amount Text.value", "\"0%\"");

    ecs_fini(world);
}

void Basics_slider_step_snapping(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *volume = BASICS ".sliders.volume";
    const char *gain = BASICS ".sliders.gain";
    float fy, fx = basics_slider_at(world, volume, 0.66f, &fy);

    gallery_click(world, s, volume, fx, fy, volume);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "70");
    fx = basics_slider_at(world, volume, 0.33f, &fy);
    gallery_click(world, s, volume, fx, fy, volume);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "30");

    fx = basics_slider_at(world, gain, 0.25f, &fy);
    gallery_click(world, s, gain, fx, fy, gain);
    double v = gallery_number(world, s, "sliders.gain", "Slider", "current");
    test_assert(v > 0.24 && v < 0.26);

    ecs_os_free(gallery_run(world, s, "set " BASICS ".sliders.volume box.widgets.Slider.mut "
        "{current: 33}"));
    basics_tab_to(world, s, BASICS ".sliders.volume");
    gallery_key(world, s, "Right");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "40");

    ecs_fini(world);
}

void Basics_slider_clamping(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *volume = BASICS ".sliders.volume";
    char from[64], to[64];
    float fy, fx = basics_slider_at(world, volume, 0.4f, &fy);

    gallery_hover(world, s, volume, fx, fy, volume);
    basics_slider_point(world, volume, 0.4f, from, sizeof(from));
    basics_slider_point(world, volume, 3.0f, to, sizeof(to));
    ecs_os_free(gallery_run(world, s, "mouse.drag left %s %s", from, to));
    gallery_settle(world, s);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "100");

    basics_slider_point(world, volume, 1.0f, from, sizeof(from));
    basics_slider_point(world, volume, -3.0f, to, sizeof(to));
    ecs_os_free(gallery_run(world, s, "mouse.drag left %s %s", from, to));
    gallery_settle(world, s);
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "0");

    int32_t changes = gallery_changes(world, s);
    gallery_key(world, s, "Left");
    gallery_key(world, s, "PageDown");
    gallery_key(world, s, "Home");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "0");
    test_int(gallery_changes(world, s), changes);
    gallery_key(world, s, "End");
    gallery_key(world, s, "Right");
    gallery_key(world, s, "PageUp");
    basics_expect_mut(world, s, "sliders.volume", "Slider", "current", "100");
    test_int(gallery_changes(world, s), changes + 1);

    ecs_fini(world);
}

void Basics_stepper_buttons(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *count = BASICS ".steppers.count";

    gallery_hover(world, s, count, BASICS_PLUS, 0, count);
    basics_expect_mut(world, s, "steppers.count", "Stepper", "part", "1");
    gallery_click(world, s, count, BASICS_PLUS, 0, count);
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "3");
    gallery_expect(world, s, "get " BASICS ".steppers.count.amount.digits Text.value", "\"3\"");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"count: 3\"");

    gallery_click(world, s, count, BASICS_PLUS, 0, count);
    gallery_click(world, s, count, BASICS_PLUS, 0, count);
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "5");
    test_int(gallery_changes(world, s), 3);

    gallery_click(world, s, count, BASICS_PLUS, 0, count);
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "5");
    test_int(gallery_changes(world, s), 3);
    gallery_expect(world, s, "get " BASICS ".steppers.count.plus.sign Rgb",
        "{r: 142, g: 149, b: 160}");

    gallery_click(world, s, count, 0, 0, count);
    test_int(gallery_changes(world, s), 3);

    gallery_hover(world, s, count, BASICS_MINUS, 0, count);
    basics_expect_mut(world, s, "steppers.count", "Stepper", "part", "-1");
    int32_t i;
    for (i = 0; i < 6; i ++) {
        gallery_click(world, s, count, BASICS_MINUS, 0, count);
    }

    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "0");
    test_int(gallery_changes(world, s), 8);
    gallery_expect(world, s, "get " BASICS ".steppers.count.minus.sign Rgb",
        "{r: 142, g: 149, b: 160}");

    ecs_fini(world);
}

void Basics_stepper_keys(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    basics_tab_to(world, s, BASICS ".steppers.count");
    gallery_key(world, s, "Up");
    gallery_key(world, s, "Right");
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "4");
    gallery_key(world, s, "Down");
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "3");
    gallery_key(world, s, "End");
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "5");
    test_int(gallery_changes(world, s), 4);

    gallery_key(world, s, "Up");
    gallery_key(world, s, "PageUp");
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "5");
    test_int(gallery_changes(world, s), 4);
    gallery_key(world, s, "Home");
    gallery_key(world, s, "Left");
    gallery_key(world, s, "PageDown");
    basics_expect_mut(world, s, "steppers.count", "Stepper", "current", "0");
    test_int(gallery_changes(world, s), 5);

    ecs_fini(world);
}

void Basics_disabled_ignore_clicks(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float fy, fx = basics_slider_at(world, BASICS ".sliders.muted", 0.1f, &fy);

    gallery_click(world, s, BASICS ".buttons.off", 0, 0, "#0");
    gallery_click(world, s, BASICS ".toggles.locked", -0.8f, 0, "#0");
    gallery_click(world, s, BASICS ".toggles.airplane", -0.6f, 0, "#0");
    gallery_click(world, s, BASICS ".radios.size.huge", 0, 0, "#0");
    gallery_click(world, s, BASICS ".radios.quality.low", 0, 0, "#0");
    gallery_click(world, s, BASICS ".sliders.muted", fx, fy, "#0");
    gallery_click(world, s, BASICS ".steppers.fixed", BASICS_PLUS, 0, "#0");

    test_int(gallery_changes(world, s), 0);
    basics_expect_focus(world, s, "#0");
    basics_expect_mut(world, s, "buttons.off", "Button", "down", "false");
    basics_expect_mut(world, s, "toggles.locked", "Checkbox", "current", "false");
    basics_expect_mut(world, s, "toggles.airplane", "Switch", "current", "true");
    basics_expect_mut(world, s, "radios.size", "RadioGroup", "current", "\"Medium\"");
    basics_expect_mut(world, s, "radios.quality", "RadioGroup", "current", "\"High\"");
    basics_expect_mut(world, s, "sliders.muted", "Slider", "current", "70");
    basics_expect_mut(world, s, "steppers.fixed", "Stepper", "current", "3");

    gallery_expect(world, s, "get " BASICS ".buttons.off Opacity", "{value: 0.4499999881}");
    gallery_expect(world, s, "get " BASICS ".radios.quality Opacity", "{value: 0.4499999881}");
    test_assert(!ecs_has(world, ecs_lookup(world, BASICS ".buttons.save"), Opacity));

    ecs_fini(world);
}

void Basics_disabled_skip_tab(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *disabled[] = {
        BASICS ".buttons.off", BASICS ".toggles.locked", BASICS ".toggles.airplane",
        BASICS ".radios.size.huge", BASICS ".radios.quality.low", BASICS ".radios.quality.high",
        BASICS ".sliders.muted", BASICS ".steppers.fixed"
    };
    int32_t i, d;

    for (d = 0; d < 8; d ++) {
        test_bool(ecs_has_id(world, ecs_lookup(world, disabled[d]), Focusable), false);
    }

    for (i = 0; i < 26; i ++) {
        gallery_key(world, s, "Tab");
        char *focused = gallery_focused(world, s);
        for (d = 0; d < 8; d ++) {
            test_assert(strcmp(focused, disabled[d]) != 0);
        }

        test_assert(strcmp(focused, "#0") != 0);
        ecs_os_free(focused);
    }

    ecs_fini(world);
}

void Basics_tab_order(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *order[] = {
        BASICS ".buttons.save", BASICS ".buttons.cancel", BASICS ".buttons.more",
        BASICS ".buttons.remove", BASICS ".buttons.ping", BASICS ".toggles.sound",
        BASICS ".toggles.music", BASICS ".toggles.wifi", BASICS ".radios.size.medium",
        BASICS ".sliders.volume", BASICS ".sliders.gain", BASICS ".steppers.count",
        TEXT ".name.field", TEXT ".secret.field", TEXT ".fixed.field",
        TEXT ".submit.message.field", CONTAINERS ".tabs.bar.tab_0"
    };
    int32_t i;

    for (i = 0; i < 17; i ++) {
        gallery_key(world, s, "Tab");
        basics_expect_focus(world, s, order[i]);
    }

    gallery_expect(world, s, "get box.ui.FocusStatus FocusStatus.visible", "true");
    test_int(gallery_changes(world, s), 0);

    ecs_fini(world);
}

void Basics_focus_ring_after_tab(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    const char *ring = "{width: 2, radius: 4, color: {r: 120, g: 172, b: 240, a: 255}}";

    gallery_click(world, s, BASICS ".buttons.cancel", 0, 0, BASICS ".buttons.cancel");
    basics_expect_focus(world, s, BASICS ".buttons.cancel");
    basics_expect_mut(world, s, "buttons.cancel", "Button", "focused", "true");
    gallery_expect(world, s, "get " BASICS ".buttons.cancel Border",
        "{width: 1, radius: 4, color: {r: 70, g: 76, b: 87, a: 255}}");

    gallery_key(world, s, "Tab");
    basics_expect_focus(world, s, BASICS ".buttons.more");
    gallery_expect(world, s, "get " BASICS ".buttons.more Border", ring);
    gallery_expect(world, s, "get " BASICS ".buttons.cancel Border",
        "{width: 1, radius: 4, color: {r: 70, g: 76, b: 87, a: 255}}");

    gallery_click(world, s, BASICS ".buttons.more", 0, 0, BASICS ".buttons.more");
    gallery_expect(world, s, "get box.ui.FocusStatus FocusStatus.visible", "false");
    gallery_expect(world, s, "get " BASICS ".buttons.more Border",
        "{width: 0, radius: 4, color: {r: 0, g: 0, b: 0, a: 255}}");

    basics_tab_to(world, s, BASICS ".toggles.sound");
    gallery_expect(world, s, "get " BASICS ".toggles.sound.box Border.width", "2");
    basics_tab_to(world, s, BASICS ".radios.size.medium");
    gallery_expect(world, s, "get " BASICS ".radios.size.medium.indicator Border.width", "2");
    basics_tab_to(world, s, BASICS ".sliders.volume");
    gallery_expect(world, s, "get " BASICS ".sliders.volume.track.knob Border.color",
        "{r: 120, g: 172, b: 240, a: 255}");
    basics_tab_to(world, s, BASICS ".steppers.count");
    gallery_expect(world, s, "get " BASICS ".steppers.count Border.width", "2");
    gallery_expect(world, s, "get " BASICS ".toggles.sound.box Border.width", "1");

    ecs_fini(world);
}

void Basics_light_theme(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    ecs_entity_t theme = ecs_lookup(world, "box.ui.theme");
    ecs_entity_t light = ecs_lookup(world, "box.widgets.LightTheme");
    test_assert(theme != 0);
    test_assert(light != 0);

    gallery_expect(world, s, "get " BASICS ".buttons.save Rgb", "{r: 98, g: 168, b: 122}");
    ecs_value_t v = ecs_const_var_get(world, light);
    test_assert(v.type == ecs_id(UiTheme));
    test_assert(v.ptr != NULL);
    ecs_set_id(world, theme, ecs_id(UiTheme), sizeof(UiTheme), v.ptr);
    ecs_progress(world, 0);

    gallery_expect(world, s, "get " BASICS ".buttons.save Rgb", "{r: 44, g: 128, b: 82}");
    gallery_expect(world, s, "get " BASICS " Rgb", "{r: 250, g: 251, b: 253}");
    gallery_expect(world, s, "get " BASICS ".buttons.save.caption Rgb",
        "{r: 255, g: 255, b: 255}");
    gallery_expect(world, s, "get gallery.backdrop_view View.clear.color",
        "{r: 232, g: 235, b: 240, a: 255}");
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Basics_set_mut_no_change(void) {
    // widgets/basics
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    test_assert(ecs_lookup(world, BASICS ".toggles.sound.box.mark") == 0);
    ecs_os_free(gallery_run(world, s, "set " BASICS ".toggles.sound box.widgets.Checkbox.mut "
        "{current: true}"));
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BASICS ".toggles.sound.box.mark") != 0);
    gallery_expect(world, s, "get " BASICS ".toggles.sound.box Rgb", "{r: 98, g: 168, b: 122}");

    ecs_os_free(gallery_run(world, s, "set " BASICS ".sliders.volume box.widgets.Slider.mut "
        "{current: 80}"));
    ecs_os_free(gallery_run(world, s, "set " BASICS ".radios.size box.widgets.RadioGroup.mut "
        "{current: \"Small\"}"));
    ecs_os_free(gallery_run(world, s, "set " BASICS ".steppers.count box.widgets.Stepper.mut "
        "{current: 4}"));
    ecs_progress(world, 0);
    gallery_expect(world, s, "get " BASICS ".sliders.volume.head.amount Text.value", "\"80%\"");
    test_assert(ecs_lookup(world, BASICS ".radios.size.small.indicator.dot") != 0);
    test_assert(ecs_lookup(world, BASICS ".radios.size.medium.indicator.dot") == 0);
    gallery_expect(world, s, "get " BASICS ".steppers.count.amount.digits Text.value", "\"4\"");

    test_int(gallery_changes(world, s), 0);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"none\"");

    ecs_fini(world);
}
