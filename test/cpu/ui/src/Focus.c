/**
 * @file Focus.c
 * @brief The keyboard focus: presses and Tab move it, KeyCapture and a visible focus keep keys
 * from the bindings, and key repeats reach C.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

/** Modules, actions bound to keys and a screen UI scene */
#define FOCUS_PRELUDE \
    "using flecs.script\n" \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.picking\n" \
    "using box.input\n" \
    "using box.ui\n" \
    "actions {\n" \
    "  jump { Action: {}\n" \
    "    Binding: {key: Space}\n" \
    "  }\n" \
    "  walk { Action: {}\n" \
    "    Binding: {key: W}\n" \
    "  }\n" \
    "  next { Action: {}\n" \
    "    Binding: {key: Tab}\n" \
    "  }\n" \
    "  confirm { Action: {}\n" \
    "    Binding: {key: Enter}\n" \
    "  }\n" \
    "  left { Action: {}\n" \
    "    Binding: {key: Left}\n" \
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

/** A FocusScope form with three focusable buttons drawn b, a, c by z and a plain element.
 * On screen a is at [440, 260], b and its label at [640, 260], c at [840, 260], plain at
 * [640, 460]; [900, 360] is on the form only and [1200, 50] on nothing. */
#define FOCUS_FORM \
    "hud {\n" \
    "  form {\n" \
    "    FocusScope\n" \
    "    Position: {0, 0, 0}\n" \
    "    Rect: {600, 400}\n" \
    "    a { Position: {-200, 100, 2}\n" \
    "      Rect: {100, 40}\n" \
    "      Focusable\n" \
    "    }\n" \
    "    b { Position: {0, 100, 1}\n" \
    "      Rect: {100, 40}\n" \
    "      Focusable\n" \
    "      label { Position: {0, 0, 1}\n" \
    "        Rect: {50, 20}\n" \
    "      }\n" \
    "    }\n" \
    "    c { Position: {200, 100, 3}\n" \
    "      Rect: {100, 40}\n" \
    "      Focusable\n" \
    "    }\n" \
    "    plain { Position: {0, -100, 1}\n" \
    "      Rect: {100, 40}\n" \
    "      Interactive\n" \
    "    }\n" \
    "  }\n" \
    "}\n"

/** A panel with a KeyCapture field at [640, 260] and a focusable element at [640, 460] under a
 * KeyCapture box */
#define FOCUS_CAPTURE \
    "hud {\n" \
    "  panel {\n" \
    "    Position: {0, 0, 0}\n" \
    "    Rect: {600, 400}\n" \
    "    field { Position: {0, 100, 1}\n" \
    "      Rect: {200, 40}\n" \
    "      Focusable\n" \
    "      KeyCapture\n" \
    "    }\n" \
    "    box { Position: {0, -100, 1}\n" \
    "      Rect: {200, 40}\n" \
    "      KeyCapture\n" \
    "      inner { Position: {0, 0, 1}\n" \
    "        Rect: {100, 20}\n" \
    "        Focusable\n" \
    "      }\n" \
    "    }\n" \
    "  }\n" \
    "}\n"

/** Create a world with the prelude and a scene, and run two frames.
 *
 * @param scene Script code after the prelude
 * @return The world
 */
static ecs_world_t* focus_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *code = flecs_asprintf("%s%s", FOCUS_PRELUDE, scene);
    test_assert(ecs_script(world, { .code = code }) != 0);
    ecs_os_free(code);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Run a terminal command and the frames until the input queue is drained, plus one.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The command
 */
static void focus_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd)
{
    ecs_os_free(box_terminal_run(world, s, cmd, NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
}

/** Run a terminal command and count the frames in which an action was pressed.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The command
 * @param path Path of the action
 * @return Frames in which it was pressed, over four frames
 */
static int32_t focus_presses(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd,
    const char *path)
{
    ecs_entity_t action = ecs_lookup(world, path);
    int32_t i, result = 0;
    ecs_os_free(box_terminal_run(world, s, cmd, NULL));
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        result += ecs_get(world, action, ActionState)->pressed;
    }

    return result;
}

/** Get an entity by path.
 *
 * @param world The world
 * @param path The path
 * @return The entity
 */
static ecs_entity_t focus_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the focus status.
 *
 * @param world The world
 * @return The FocusStatus singleton
 */
static const FocusStatus* focus_status(
    ecs_world_t *world)
{
    const FocusStatus *result = ecs_singleton_get(world, FocusStatus);
    test_assert(result != NULL);

    return result;
}

/** Count the entities with Focused.
 *
 * @param world The world
 * @return The count
 */
static int32_t focus_tagged(
    ecs_world_t *world)
{
    return ecs_count_id(world, Focused);
}

void Focus_tags_and_status(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t a = focus_e(world, "hud.form.a");
    test_bool(ecs_has(world, a, Interactive), true);
    test_uint(focus_status(world)->focused, 0);
    test_uint(focus_status(world)->previous, 0);
    test_bool(focus_status(world)->visible, false);
    test_int(focus_tagged(world), 0);
    test_assert(ecs_lookup(world, "box.ui.Focusable") == Focusable);
    test_assert(ecs_lookup(world, "box.ui.KeyCapture") == KeyCapture);
    test_assert(ecs_lookup(world, "box.ui.FocusScope") == FocusScope);
    test_assert(ecs_lookup(world, "flecs.script.on.activate") != 0);
    test_assert(ecs_lookup(world, "flecs.script.ui.changed") != 0);

    ecs_fini(world);
}

void Focus_click_focuses(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t a = focus_e(world, "hud.form.a");
    ecs_entity_t b = focus_e(world, "hud.form.b");
    focus_run(world, s, "mouse.click left [440, 260]");
    test_uint(focus_status(world)->focused, a);
    test_bool(focus_status(world)->visible, false);
    test_bool(ecs_has(world, a, Focused), true);
    test_int(focus_tagged(world), 1);

    focus_run(world, s, "mouse.click left [640, 260]");
    test_uint(ecs_singleton_get(world, PickStatus)->target, focus_e(world, "hud.form.b.label"));
    test_uint(focus_status(world)->focused, b);
    test_uint(focus_status(world)->previous, a);
    test_bool(ecs_has(world, a, Focused), false);
    test_bool(ecs_has(world, b, Focused), true);
    test_int(focus_tagged(world), 1);

    ecs_fini(world);
}

void Focus_any_button_focuses(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    focus_run(world, s, "mouse.click right [840, 260]");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.c"));
    focus_run(world, s, "mouse.click middle [440, 260]");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.a"));

    ecs_fini(world);
}

void Focus_click_empty_clears(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t b = focus_e(world, "hud.form.b");
    focus_run(world, s, "mouse.click left [640, 260]");
    test_uint(focus_status(world)->focused, b);
    focus_run(world, s, "mouse.click left [900, 360]");
    test_uint(focus_status(world)->focused, 0);
    test_uint(focus_status(world)->previous, b);
    test_int(focus_tagged(world), 0);

    focus_run(world, s, "mouse.click left [640, 260]");
    test_uint(focus_status(world)->focused, b);
    focus_run(world, s, "mouse.click left [1200, 50]");
    test_uint(focus_status(world)->focused, 0);

    ecs_fini(world);
}

void Focus_click_non_focusable_clears(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    focus_run(world, s, "mouse.click left [440, 260]");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.a"));
    focus_run(world, s, "mouse.click left [640, 460]");
    test_uint(ecs_singleton_get(world, PickStatus)->handler, focus_e(world, "hud.form.plain"));
    test_uint(focus_status(world)->focused, 0);
    test_int(focus_tagged(world), 0);

    ecs_fini(world);
}

void Focus_disable_clears(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t a = focus_e(world, "hud.form.a");
    focus_run(world, s, "mouse.click left [440, 260]");
    test_uint(focus_status(world)->focused, a);
    ecs_enable(world, a, false);
    ecs_progress(world, 0);
    test_uint(focus_status(world)->focused, 0);
    test_bool(ecs_has(world, a, Focused), false);
    test_bool(box_ui_focus(world, a), false);
    test_uint(focus_status(world)->focused, 0);
    ecs_enable(world, a, true);

    ecs_fini(world);
}

void Focus_delete_clears(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t b = focus_e(world, "hud.form.b");
    focus_run(world, s, "mouse.click left [640, 260]");
    test_uint(focus_status(world)->focused, b);
    ecs_delete(world, b);
    test_uint(focus_status(world)->focused, 0);
    ecs_progress(world, 0);
    test_uint(focus_status(world)->focused, 0);
    test_int(focus_tagged(world), 0);

    focus_run(world, s, "mouse.click left [440, 260]");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.a"));
    ecs_delete(world, focus_e(world, "hud.form"));
    ecs_progress(world, 0);
    test_uint(focus_status(world)->focused, 0);
    test_int(focus_tagged(world), 0);

    ecs_fini(world);
}

void Focus_remove_focusable_clears(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t c = focus_e(world, "hud.form.c");
    focus_run(world, s, "mouse.click left [840, 260]");
    test_uint(focus_status(world)->focused, c);
    ecs_remove(world, c, Focusable);
    test_uint(focus_status(world)->focused, 0);
    test_bool(ecs_has(world, c, Focused), false);

    ecs_fini(world);
}

void Focus_tab_walks_draw_order(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t a = focus_e(world, "hud.form.a");
    ecs_entity_t b = focus_e(world, "hud.form.b");
    ecs_entity_t c = focus_e(world, "hud.form.c");
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, b);
    test_bool(focus_status(world)->visible, true);
    test_bool(ecs_has(world, b, Focused), true);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, a);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, c);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, b);
    test_uint(focus_status(world)->previous, c);
    test_int(focus_tagged(world), 1);

    focus_run(world, s, "mouse.click left [440, 260]");
    test_uint(focus_status(world)->focused, a);
    test_bool(focus_status(world)->visible, false);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, c);
    test_bool(focus_status(world)->visible, true);

    ecs_fini(world);
}

void Focus_shift_tab_walks_back(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    focus_run(world, s, "key.down ShiftLeft");
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.c"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.a"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.b"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.c"));
    focus_run(world, s, "key.up ShiftLeft");
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.b"));

    ecs_fini(world);
}

void Focus_tab_ordered_children(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "hud {\n"
        "  list {\n"
        "    FocusScope\n"
        "    OrderedChildren\n"
        "    z { Position: {0, 100, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "    }\n"
        "    y { Position: {0, 0, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "      inner { Position: {0, 0, 1}\n"
        "        Rect: {50, 20}\n"
        "        Focusable\n"
        "      }\n"
        "    }\n"
        "    x { Position: {0, -100, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "    }\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = box_terminal_new(world);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.list.z"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.list.y"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.list.y.inner"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.list.x"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.list.z"));

    ecs_fini(world);
}

void Focus_tab_skips_disabled(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t a = focus_e(world, "hud.form.a");
    ecs_enable(world, a, false);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.b"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.c"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.b"));
    ecs_enable(world, a, true);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, a);

    ecs_fini(world);
}

void Focus_tab_stays_in_nearest_scope(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "hud {\n"
        "  outer {\n"
        "    FocusScope\n"
        "    p { Position: {-300, 100, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "    }\n"
        "    inner {\n"
        "      FocusScope\n"
        "      q { Position: {0, 100, 0}\n"
        "        Rect: {100, 40}\n"
        "        Focusable\n"
        "      }\n"
        "      r { Position: {0, -100, 0}\n"
        "        Rect: {100, 40}\n"
        "        Focusable\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  other {\n"
        "    FocusScope\n"
        "    t { Position: {300, 100, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "    }\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t q = focus_e(world, "hud.outer.inner.q");
    ecs_entity_t r = focus_e(world, "hud.outer.inner.r");
    ecs_entity_t t = focus_e(world, "hud.other.t");
    focus_run(world, s, "mouse.click left [640, 260]");
    test_uint(focus_status(world)->focused, q);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, r);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, q);

    focus_run(world, s, "mouse.click left [940, 260]");
    test_uint(focus_status(world)->focused, t);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, t);

    focus_run(world, s, "mouse.click left [340, 260]");
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, q);

    ecs_fini(world);
}

void Focus_tab_without_scope_reaches_binding(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "hud {\n"
        "  a { Position: {0, 0, 0}\n"
        "    Rect: {100, 40}\n"
        "    Focusable\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t a = focus_e(world, "hud.a");
    test_int(focus_presses(world, s, "key.press Tab", "actions.next"), 1);
    test_uint(focus_status(world)->focused, 0);
    focus_run(world, s, "mouse.click left [640, 360]");
    test_uint(focus_status(world)->focused, a);
    test_int(focus_presses(world, s, "key.press Tab", "actions.next"), 1);
    test_uint(focus_status(world)->focused, a);
    test_bool(focus_status(world)->visible, false);

    ecs_fini(world);
}

void Focus_tab_with_scope_kept_from_binding(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    test_int(focus_presses(world, s, "key.press Tab", "actions.next"), 0);
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.b"));
    test_int(focus_presses(world, s, "key.press Tab", "actions.next"), 0);
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.a"));

    ecs_fini(world);
}

void Focus_keyboard_focus_keeps_nav_keys(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    focus_run(world, s, "key.press Tab");
    test_bool(focus_status(world)->visible, true);
    test_int(focus_presses(world, s, "key.press Space", "actions.jump"), 0);
    test_int(focus_presses(world, s, "key.press Enter", "actions.confirm"), 0);
    test_int(focus_presses(world, s, "key.press Left", "actions.left"), 0);
    test_int(focus_presses(world, s, "key.press W", "actions.walk"), 1);
    focus_run(world, s, "key.down Space");
    test_bool(ecs_singleton_get(world, Input)->keys[KeySpace].down, false);
    focus_run(world, s, "key.up Space");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.b"));

    ecs_fini(world);
}

void Focus_pointer_focus_lets_keys_through(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    focus_run(world, s, "mouse.click left [640, 260]");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.b"));
    test_bool(focus_status(world)->visible, false);
    test_int(focus_presses(world, s, "key.press Space", "actions.jump"), 1);
    test_int(focus_presses(world, s, "key.press Enter", "actions.confirm"), 1);
    test_int(focus_presses(world, s, "key.press Left", "actions.left"), 1);
    test_int(focus_presses(world, s, "key.press W", "actions.walk"), 1);

    ecs_fini(world);
}

void Focus_key_capture_keeps_keys(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_CAPTURE);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t field = focus_e(world, "hud.panel.field");
    char typed[16] = {0};
    int32_t i;
    focus_run(world, s, "mouse.click left [640, 260]");
    test_uint(focus_status(world)->focused, field);
    test_bool(focus_status(world)->visible, false);
    test_int(focus_presses(world, s, "key.press Space", "actions.jump"), 0);
    test_int(focus_presses(world, s, "key.press W", "actions.walk"), 0);
    test_int(focus_presses(world, s, "key.press Tab", "actions.next"), 0);
    focus_run(world, s, "key.down Space");
    test_bool(ecs_singleton_get(world, Input)->keys[KeySpace].down, false);
    focus_run(world, s, "key.up Space");

    ecs_os_free(box_terminal_run(world, s, "key.type hi", NULL));
    for (i = 0; i < 8; i ++) {
        ecs_progress(world, 0);
        const Input *in = ecs_singleton_get(world, Input);
        test_assert(!in->text || !in->text[0]);
        strncat(typed, box_ui_text(world), sizeof(typed) - strlen(typed) - 1);
    }

    test_str(typed, "hi");
    test_str(box_ui_text(world), "");

    focus_run(world, s, "mouse.click left [1200, 50]");
    test_uint(focus_status(world)->focused, 0);
    test_int(focus_presses(world, s, "key.press Space", "actions.jump"), 1);
    test_int(focus_presses(world, s, "key.press W", "actions.walk"), 1);

    ecs_fini(world);
}

void Focus_key_capture_on_ancestor(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_CAPTURE);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t inner = focus_e(world, "hud.panel.box.inner");
    focus_run(world, s, "mouse.click left [640, 460]");
    test_uint(focus_status(world)->focused, inner);
    test_int(focus_presses(world, s, "key.press W", "actions.walk"), 0);
    ecs_remove(world, focus_e(world, "hud.panel.box"), KeyCapture);
    test_int(focus_presses(world, s, "key.press W", "actions.walk"), 1);

    ecs_fini(world);
}

void Focus_key_capture_added_reactively(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "template Drop {\n"
        "  mut open: bool = false\n"
        "  Position: {0, 0, 0}\n"
        "  Rect: {100, 40}\n"
        "  Focusable\n"
        "  if open {\n"
        "    KeyCapture\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
        "      open = !open\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  drop { Drop: {} }\n"
        "}\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t drop = focus_e(world, "hud.drop");
    focus_run(world, s, "mouse.click left [640, 360]");
    test_uint(focus_status(world)->focused, drop);
    test_bool(ecs_has(world, drop, KeyCapture), true);
    test_int(focus_presses(world, s, "key.press W", "actions.walk"), 0);
    focus_run(world, s, "mouse.click left [640, 360]");
    test_bool(ecs_has(world, drop, KeyCapture), false);
    test_int(focus_presses(world, s, "key.press W", "actions.walk"), 1);

    ecs_fini(world);
}

void Focus_api_from_c(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t a = focus_e(world, "hud.form.a");
    ecs_entity_t c = focus_e(world, "hud.form.c");
    ecs_entity_t plain = focus_e(world, "hud.form.plain");
    test_bool(box_ui_focus(world, a), true);
    test_uint(focus_status(world)->focused, a);
    test_bool(focus_status(world)->visible, false);
    test_bool(ecs_has(world, a, Focused), true);
    test_bool(box_ui_focus(world, plain), false);
    test_uint(focus_status(world)->focused, a);
    test_bool(box_ui_focus(world, c), true);
    test_uint(focus_status(world)->focused, c);
    test_uint(focus_status(world)->previous, a);
    test_bool(box_ui_focus(world, 0), true);
    test_uint(focus_status(world)->focused, 0);
    test_int(focus_tagged(world), 0);

    ecs_fini(world);
}

void Focus_api_from_script(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        FOCUS_FORM
        "template Mover {\n"
        "  mut moved: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(hud.form.plain)\n"
        "      await ui.focus(hud.form.c)\n"
        "      moved = moved + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "mover { Mover: {} }\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t c = focus_e(world, "hud.form.c");
    focus_run(world, s, "mouse.click left [640, 460]");
    const int32_t *moved = ecs_get_id(world, focus_e(world, "mover"),
        focus_e(world, "Mover.mut"));
    test_assert(moved != NULL);
    test_int(*moved, 1);
    test_uint(focus_status(world)->focused, c);
    test_bool(ecs_has(world, c, Focused), true);

    ecs_fini(world);
}

void Focus_prefab_never_focused(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "prefab Button {\n"
        "  Rect: {100, 40}\n"
        "  Focusable\n"
        "}\n"
        "hud {\n"
        "  form {\n"
        "    FocusScope\n"
        "    one { Position: {0, 0, 0}\n"
        "      (IsA, Button)\n"
        "    }\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t prefab = focus_e(world, "Button");
    test_bool(box_ui_focus(world, prefab), false);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.one"));
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.one"));

    ecs_fini(world);
}

void Focus_key_repeat_state(void) {
    // ui/focus
    ecs_world_t *world = focus_world(FOCUS_FORM);
    ecs_entity_t s = box_terminal_new(world);
    int32_t i, repeated = 0, pressed = 0;
    ecs_os_free(box_terminal_run(world, s, "key.down Backspace", NULL));
    ecs_os_free(box_terminal_run(world, s, "key.repeat Backspace 3", NULL));
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        const KeyState *k = &ecs_singleton_get(world, Input)->keys[KeyBackspace];
        test_bool(k->down, true);
        repeated += k->repeated;
        pressed += k->pressed;
    }

    test_int(repeated, 3);
    test_int(pressed, 1);

    box_input_key_repeat(world, KeyBackspace);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyBackspace].repeated, true);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyBackspace].repeated, false);
    focus_run(world, s, "key.up Backspace");
    test_bool(ecs_singleton_get(world, Input)->keys[KeyBackspace].down, false);

    ecs_fini(world);
}

void Focus_tab_first_scope_of_front_view(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "hud {\n"
        "  form {\n"
        "    FocusScope\n"
        "    a { Position: {0, 100, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "    }\n"
        "  }\n"
        "}\n"
        "top {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  empty {\n"
        "    FocusScope\n"
        "  }\n"
        "  menu {\n"
        "    FocusScope\n"
        "    item { Position: {0, -100, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "    }\n"
        "  }\n"
        "}\n"
        "top_view {\n"
        "  View: {camera: top.camera}\n"
        "}\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t item = focus_e(world, "top.menu.item");
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, item);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, item);
    ecs_delete(world, focus_e(world, "top_view"));
    focus_run(world, s, "mouse.click left [1200, 50]");
    test_uint(focus_status(world)->focused, 0);
    focus_run(world, s, "key.press Tab");
    test_uint(focus_status(world)->focused, focus_e(world, "hud.form.a"));

    ecs_fini(world);
}
