/**
 * @file UiEvents.c
 * @brief The box.ui script events: on.focus, on.blur, on.key, on.activate, on.change and
 * on.submit, counted in the muts of probe templates.
 */

#include <box.h>
#include <bake_test.h>

/** Modules, an action, a screen UI scene with a form of two buttons, a box with a slider widget
 * and probe templates that count the events of their target.
 *
 * On screen a is at [440, 260], b at [840, 260] with its label, the slider at [640, 460]. */
#define EVENTS_PRELUDE \
    "using flecs.script\n" \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.picking\n" \
    "using box.input\n" \
    "using box.ui\n" \
    "actions {\n" \
    "  play { Action: {} }\n" \
    "}\n" \
    "template Slider {\n" \
    "  mut value: i32 = 0\n" \
    "  Position: {0, -100, 1}\n" \
    "  Rect: {200, 40}\n" \
    "  Focusable\n" \
    "  async {\n" \
    "    while true {\n" \
    "      const e = await on.key(this)\n" \
    "      if e.down && e.key == Right {\n" \
    "        value = value + 1\n" \
    "        await ui.changed(this)\n" \
    "      }\n" \
    "      if e.down && e.key == Enter {\n" \
    "        await ui.submit(this)\n" \
    "      }\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "hud {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  form {\n" \
    "    FocusScope\n" \
    "    a { Position: {-200, 100, 1}\n" \
    "      Rect: {100, 40}\n" \
    "      Focusable\n" \
    "    }\n" \
    "    b { Position: {200, 100, 2}\n" \
    "      Rect: {100, 40}\n" \
    "      Focusable\n" \
    "      label { Position: {0, 0, 1}\n" \
    "        Rect: {50, 20}\n" \
    "      }\n" \
    "    }\n" \
    "    group {\n" \
    "      slider { Slider: {} }\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "hud_view {\n" \
    "  View: {camera: hud.camera}\n" \
    "}\n" \
    "template FocusProbe {\n" \
    "  prop target: entity = 0\n" \
    "  mut focus: i32 = 0\n" \
    "  mut blur: i32 = 0\n" \
    "  mut last: entity = 0\n" \
    "  mut visible: bool = false\n" \
    "  async {\n" \
    "    while true {\n" \
    "      const e = await on.focus(target)\n" \
    "      focus = focus + 1\n" \
    "      last = e.target\n" \
    "      visible = e.visible\n" \
    "    }\n" \
    "  }\n" \
    "  async {\n" \
    "    while true {\n" \
    "      await on.blur(target)\n" \
    "      blur = blur + 1\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "template KeyProbe {\n" \
    "  prop target: entity = 0\n" \
    "  mut downs: i32 = 0\n" \
    "  mut ups: i32 = 0\n" \
    "  mut repeats: i32 = 0\n" \
    "  mut key: Key = None\n" \
    "  mut shift: bool = false\n" \
    "  mut ctrl: bool = false\n" \
    "  async {\n" \
    "    while true {\n" \
    "      const e = await on.key(target)\n" \
    "      if e.down {\n" \
    "        downs = downs + 1\n" \
    "      } else {\n" \
    "        ups = ups + 1\n" \
    "      }\n" \
    "      if e.repeat {\n" \
    "        repeats = repeats + 1\n" \
    "      }\n" \
    "      key = e.key\n" \
    "      shift = e.shift\n" \
    "      ctrl = e.ctrl\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "template ActivateProbe {\n" \
    "  prop target: entity = 0\n" \
    "  mut count: i32 = 0\n" \
    "  mut keys: i32 = 0\n" \
    "  mut last: entity = 0\n" \
    "  async {\n" \
    "    while true {\n" \
    "      const e = await on.activate(target)\n" \
    "      count = count + 1\n" \
    "      if e.keyboard {\n" \
    "        keys = keys + 1\n" \
    "      }\n" \
    "      last = e.target\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "template ChangeProbe {\n" \
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
    "}\n"

/** The muts of FocusProbe */
typedef struct {
    int32_t focus;     /**< Times on.focus resolved */
    int32_t blur;      /**< Times on.blur resolved */
    ecs_entity_t last; /**< Target of the last on.focus */
    bool visible;      /**< Visible of the last on.focus */
} events_focus_t;

/** The muts of KeyProbe */
typedef struct {
    int32_t downs;   /**< Key downs and repeats */
    int32_t ups;     /**< Key ups */
    int32_t repeats; /**< Repeats */
    Key key;         /**< Key of the last event */
    bool shift;      /**< Shift of the last event */
    bool ctrl;       /**< Ctrl of the last event */
} events_key_t;

/** The muts of ActivateProbe */
typedef struct {
    int32_t count;     /**< Activations */
    int32_t keys;      /**< Activations from the keyboard */
    ecs_entity_t last; /**< Target of the last activation */
} events_activate_t;

/** The muts of ChangeProbe */
typedef struct {
    int32_t changes;     /**< Times on.change resolved */
    int32_t submits;     /**< Times on.submit resolved */
    ecs_entity_t widget; /**< Widget of the last event */
} events_change_t;

/** Create a world with the prelude and a scene, and run two frames.
 *
 * @param scene Script code after the prelude
 * @return The world
 */
static ecs_world_t* events_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *code = flecs_asprintf("%s%s", EVENTS_PRELUDE, scene);
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
static void events_run(
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

/** Get an entity by path.
 *
 * @param world The world
 * @param path The path
 * @return The entity
 */
static ecs_entity_t events_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the muts of a probe.
 *
 * @param world The world
 * @param probe Path of the probe
 * @param tmpl Name of its template
 * @return The muts
 */
static const void* events_probe(
    ecs_world_t *world,
    const char *probe,
    const char *tmpl)
{
    char *mut = flecs_asprintf("%s.mut", tmpl);
    const void *result = ecs_get_id(world, events_e(world, probe), events_e(world, mut));
    ecs_os_free(mut);
    test_assert(result != NULL);

    return result;
}

/** Get the muts of a FocusProbe.
 *
 * @param world The world
 * @param probe Path of the probe
 * @return The muts
 */
static const events_focus_t* events_focus(
    ecs_world_t *world,
    const char *probe)
{
    return events_probe(world, probe, "FocusProbe");
}

/** Get the muts of a KeyProbe.
 *
 * @param world The world
 * @param probe Path of the probe
 * @return The muts
 */
static const events_key_t* events_key(
    ecs_world_t *world,
    const char *probe)
{
    return events_probe(world, probe, "KeyProbe");
}

/** Get the muts of an ActivateProbe.
 *
 * @param world The world
 * @param probe Path of the probe
 * @return The muts
 */
static const events_activate_t* events_activate(
    ecs_world_t *world,
    const char *probe)
{
    return events_probe(world, probe, "ActivateProbe");
}

/** Get the muts of a ChangeProbe.
 *
 * @param world The world
 * @param probe Path of the probe
 * @return The muts
 */
static const events_change_t* events_change(
    ecs_world_t *world,
    const char *probe)
{
    return events_probe(world, probe, "ChangeProbe");
}

/** Get the value mut of the slider widget.
 *
 * @param world The world
 * @return The value
 */
static int32_t events_slider(
    ecs_world_t *world)
{
    return *(const int32_t*)events_probe(world, "hud.form.group.slider", "Slider");
}

void UiEvents_focus_blur_bubble(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_a { FocusProbe: {target: hud.form.a} }\n"
        "on_form { FocusProbe: {target: hud.form} }\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t a = events_e(world, "hud.form.a");
    ecs_entity_t b = events_e(world, "hud.form.b");
    events_run(world, s, "mouse.click left [440, 260]");
    test_int(events_focus(world, "on_a")->focus, 1);
    test_int(events_focus(world, "on_a")->blur, 0);
    test_uint(events_focus(world, "on_a")->last, a);
    test_bool(events_focus(world, "on_a")->visible, false);
    test_int(events_focus(world, "on_form")->focus, 1);
    test_uint(events_focus(world, "on_form")->last, a);

    events_run(world, s, "mouse.click left [840, 260]");
    test_int(events_focus(world, "on_a")->focus, 1);
    test_int(events_focus(world, "on_a")->blur, 1);
    test_int(events_focus(world, "on_form")->focus, 2);
    test_int(events_focus(world, "on_form")->blur, 1);
    test_uint(events_focus(world, "on_form")->last, b);

    events_run(world, s, "key.press Tab");
    test_uint(ecs_singleton_get(world, FocusStatus)->focused,
        events_e(world, "hud.form.group.slider"));
    test_int(events_focus(world, "on_form")->focus, 3);
    test_int(events_focus(world, "on_form")->blur, 2);
    test_bool(events_focus(world, "on_form")->visible, true);

    events_run(world, s, "mouse.click left [1200, 50]");
    test_int(events_focus(world, "on_form")->focus, 3);
    test_int(events_focus(world, "on_form")->blur, 3);
    test_int(events_focus(world, "on_a")->blur, 1);

    ecs_fini(world);
}

void UiEvents_key_reports_key_and_modifiers(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_a { KeyProbe: {target: hud.form.a} }\n"
        "on_form { KeyProbe: {target: hud.form} }\n");
    ecs_entity_t s = box_terminal_new(world);
    events_run(world, s, "mouse.click left [440, 260]");
    events_run(world, s, "key.press A");
    test_int(events_key(world, "on_a")->downs, 1);
    test_int(events_key(world, "on_a")->ups, 1);
    test_int(events_key(world, "on_a")->key, KeyA);
    test_bool(events_key(world, "on_a")->shift, false);
    test_int(events_key(world, "on_form")->downs, 1);
    test_int(events_key(world, "on_form")->ups, 1);

    events_run(world, s, "key.down ShiftLeft");
    events_run(world, s, "key.down CtrlLeft");
    events_run(world, s, "key.press Z");
    test_int(events_key(world, "on_a")->key, KeyZ);
    test_bool(events_key(world, "on_a")->shift, true);
    test_bool(events_key(world, "on_a")->ctrl, true);
    test_int(events_key(world, "on_a")->downs, 4);
    test_int(events_key(world, "on_a")->ups, 2);
    events_run(world, s, "key.up ShiftLeft");
    events_run(world, s, "key.up CtrlLeft");
    test_int(events_key(world, "on_a")->ups, 4);
    test_int(events_key(world, "on_a")->repeats, 0);

    ecs_fini(world);
}

void UiEvents_key_nothing_when_not_focused(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_a { KeyProbe: {target: hud.form.a} }\n"
        "on_b { KeyProbe: {target: hud.form.b} }\n");
    ecs_entity_t s = box_terminal_new(world);
    events_run(world, s, "key.press A");
    test_int(events_key(world, "on_a")->downs, 0);
    events_run(world, s, "mouse.click left [840, 260]");
    events_run(world, s, "key.press A");
    test_int(events_key(world, "on_a")->downs, 0);
    test_int(events_key(world, "on_a")->ups, 0);
    test_int(events_key(world, "on_b")->downs, 1);
    events_run(world, s, "mouse.click left [1200, 50]");
    events_run(world, s, "key.press A");
    test_int(events_key(world, "on_b")->downs, 1);

    ecs_fini(world);
}

void UiEvents_key_repeats(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_a { KeyProbe: {target: hud.form.a} }\n");
    ecs_entity_t s = box_terminal_new(world);
    events_run(world, s, "mouse.click left [440, 260]");
    events_run(world, s, "key.down Backspace");
    events_run(world, s, "key.repeat Backspace 3");
    test_int(events_key(world, "on_a")->downs, 4);
    test_int(events_key(world, "on_a")->repeats, 3);
    test_int(events_key(world, "on_a")->ups, 0);
    test_int(events_key(world, "on_a")->key, KeyBackspace);
    events_run(world, s, "key.up Backspace");
    test_int(events_key(world, "on_a")->ups, 1);
    test_int(events_key(world, "on_a")->repeats, 3);

    ecs_fini(world);
}

void UiEvents_activate_click(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_b { ActivateProbe: {target: hud.form.b} }\n"
        "on_form { ActivateProbe: {target: hud.form} }\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t b = events_e(world, "hud.form.b");
    events_run(world, s, "mouse.click left [840, 260]");
    test_int(events_activate(world, "on_b")->count, 1);
    test_int(events_activate(world, "on_b")->keys, 0);
    test_uint(events_activate(world, "on_b")->last, b);
    test_int(events_activate(world, "on_form")->count, 1);
    test_uint(events_activate(world, "on_form")->last, b);

    events_run(world, s, "mouse.click left [880, 260]");
    test_int(events_activate(world, "on_b")->count, 2);
    events_run(world, s, "mouse.click right [840, 260]");
    test_int(events_activate(world, "on_b")->count, 2);
    events_run(world, s, "mouse.click left [440, 260]");
    test_int(events_activate(world, "on_b")->count, 2);
    test_int(events_activate(world, "on_form")->count, 3);

    ecs_fini(world);
}

void UiEvents_activate_not_released_elsewhere(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_b { ActivateProbe: {target: hud.form.b} }\n");
    ecs_entity_t s = box_terminal_new(world);
    events_run(world, s, "mouse.drag left [840, 260] [1200, 50]");
    test_int(events_activate(world, "on_b")->count, 0);
    events_run(world, s, "mouse.drag left [840, 260] [440, 260]");
    test_int(events_activate(world, "on_b")->count, 0);
    events_run(world, s, "mouse.drag left [840, 260] [1200, 50]");
    events_run(world, s, "mouse.drag left [1200, 50] [840, 260]");
    test_int(events_activate(world, "on_b")->count, 0);
    events_run(world, s, "mouse.drag left [840, 260] [870, 270]");
    test_int(events_activate(world, "on_b")->count, 1);

    ecs_fini(world);
}

void UiEvents_activate_enter(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_b { ActivateProbe: {target: hud.form.b} }\n");
    ecs_entity_t s = box_terminal_new(world);
    events_run(world, s, "key.press Enter");
    test_int(events_activate(world, "on_b")->count, 0);
    events_run(world, s, "key.press Tab");
    events_run(world, s, "key.press Tab");
    events_run(world, s, "key.press Tab");
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, events_e(world, "hud.form.b"));
    events_run(world, s, "key.down Enter");
    test_int(events_activate(world, "on_b")->count, 1);
    test_int(events_activate(world, "on_b")->keys, 1);
    events_run(world, s, "key.repeat Enter 2");
    events_run(world, s, "key.up Enter");
    test_int(events_activate(world, "on_b")->count, 1);
    events_run(world, s, "key.press KpEnter");
    test_int(events_activate(world, "on_b")->count, 2);

    ecs_fini(world);
}

void UiEvents_activate_space_on_release(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_b { ActivateProbe: {target: hud.form.b} }\n");
    ecs_entity_t s = box_terminal_new(world);
    events_run(world, s, "mouse.click left [840, 260]");
    test_int(events_activate(world, "on_b")->count, 1);
    events_run(world, s, "key.down Space");
    test_int(events_activate(world, "on_b")->count, 1);
    events_run(world, s, "key.up Space");
    test_int(events_activate(world, "on_b")->count, 2);
    test_int(events_activate(world, "on_b")->keys, 1);

    events_run(world, s, "key.down Space");
    events_run(world, s, "mouse.click left [440, 260]");
    events_run(world, s, "key.up Space");
    test_int(events_activate(world, "on_b")->count, 2);

    ecs_fini(world);
}

void UiEvents_key_capture_no_activate(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_a { ActivateProbe: {target: hud.form.a} }\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_add(world, events_e(world, "hud.form.a"), KeyCapture);
    events_run(world, s, "mouse.click left [440, 260]");
    test_int(events_activate(world, "on_a")->count, 1);
    events_run(world, s, "key.press Enter");
    events_run(world, s, "key.press Space");
    test_int(events_activate(world, "on_a")->count, 1);

    ecs_fini(world);
}

void UiEvents_enter_taps_trigger(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "hud {\n"
        "  form {\n"
        "    go { Position: {0, 0, 1}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "      ActionTrigger: {action: actions.play}\n"
        "    }\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t go = events_e(world, "hud.form.go");
    ecs_entity_t play = events_e(world, "actions.play");
    int32_t i, presses = 0;
    test_bool(box_ui_focus(world, go), true);
    ecs_os_free(box_terminal_run(world, s, "key.press Enter", NULL));
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
        presses += ecs_get(world, play, ActionState)->pressed;
    }

    test_int(presses, 1);
    ecs_os_free(box_terminal_run(world, s, "key.down Space", NULL));
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
        presses += ecs_get(world, play, ActionState)->pressed;
    }

    test_int(presses, 1);
    ecs_os_free(box_terminal_run(world, s, "key.up Space", NULL));
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
        presses += ecs_get(world, play, ActionState)->pressed;
    }

    test_int(presses, 2);

    ecs_fini(world);
}

void UiEvents_space_holds_trigger(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "hud {\n"
        "  form {\n"
        "    go { Position: {0, 0, 1}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "      ActionTrigger: {action: actions.play, mode: ActionTriggerHold}\n"
        "    }\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t go = events_e(world, "hud.form.go");
    ecs_entity_t play = events_e(world, "actions.play");
    test_bool(box_ui_focus(world, go), true);
    events_run(world, s, "key.down Space");
    ecs_progress(world, 0);
    test_bool(ecs_get(world, play, ActionState)->down, true);
    events_run(world, s, "key.repeat Space 2");
    test_bool(ecs_get(world, play, ActionState)->down, true);
    events_run(world, s, "key.up Space");
    ecs_progress(world, 0);
    test_bool(ecs_get(world, play, ActionState)->down, false);

    events_run(world, s, "key.down Space");
    ecs_progress(world, 0);
    test_bool(ecs_get(world, play, ActionState)->down, true);
    box_ui_focus(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, play, ActionState)->down, false);
    events_run(world, s, "key.up Space");

    ecs_fini(world);
}

void UiEvents_change_on_widget_and_ancestor(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_slider { ChangeProbe: {target: hud.form.group.slider} }\n"
        "on_form { ChangeProbe: {target: hud.form} }\n"
        "on_a { ChangeProbe: {target: hud.form.a} }\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t slider = events_e(world, "hud.form.group.slider");
    events_run(world, s, "mouse.click left [640, 460]");
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, slider);
    events_run(world, s, "key.press Right");
    test_int(events_change(world, "on_slider")->changes, 1);
    test_uint(events_change(world, "on_slider")->widget, slider);
    test_int(events_change(world, "on_form")->changes, 1);
    test_int(events_slider(world), 1);
    test_uint(events_change(world, "on_form")->widget, slider);
    test_int(events_change(world, "on_a")->changes, 0);

    events_run(world, s, "key.press Right");
    events_run(world, s, "key.press Right");
    test_int(events_change(world, "on_slider")->changes, 3);
    test_int(events_change(world, "on_form")->changes, 3);
    test_int(events_slider(world), 3);
    test_int(events_change(world, "on_form")->submits, 0);

    ecs_fini(world);
}

void UiEvents_submit_on_widget_and_ancestor(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_slider { ChangeProbe: {target: hud.form.group.slider} }\n"
        "on_group { ChangeProbe: {target: hud.form.group} }\n");
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t slider = events_e(world, "hud.form.group.slider");
    test_bool(box_ui_focus(world, slider), true);
    events_run(world, s, "key.press Enter");
    test_int(events_change(world, "on_slider")->submits, 1);
    test_int(events_change(world, "on_group")->submits, 1);
    test_uint(events_change(world, "on_group")->widget, slider);
    test_int(events_change(world, "on_group")->changes, 0);
    events_run(world, s, "key.press Enter");
    test_int(events_change(world, "on_slider")->submits, 2);
    test_int(events_change(world, "on_group")->submits, 2);

    ecs_fini(world);
}

void UiEvents_change_once_per_report(void) {
    // ui/events
    ecs_world_t *world = events_world(
        "on_form { ChangeProbe: {target: hud.form} }\n");
    ecs_entity_t s = box_terminal_new(world);
    int32_t i;
    test_bool(box_ui_focus(world, events_e(world, "hud.form.group.slider")), true);
    events_run(world, s, "key.down Right");
    events_run(world, s, "key.repeat Right 4");
    events_run(world, s, "key.up Right");
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    test_int(events_change(world, "on_form")->changes, 5);
    test_int(events_slider(world), 5);

    ecs_fini(world);
}
