/**
 * @file ActionTrigger.c
 * @brief A click, press or hold on an element with an ActionTrigger drives an input Action.
 */

#include <box.h>
#include <bake_test.h>

/** Modules, the actions, a probe that counts the events of actions.play and a screen UI scene */
#define TRIGGER_PRELUDE \
    "using flecs.script\n" \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.picking\n" \
    "using box.input\n" \
    "actions {\n" \
    "  play { Action: {} }\n" \
    "  fire { Action: {} }\n" \
    "}\n" \
    "template Counter {\n" \
    "  mut presses: i32 = 0\n" \
    "  mut releases: i32 = 0\n" \
    "  mut x: f32 = 0\n" \
    "  mut y: f32 = 0\n" \
    "  async {\n" \
    "    while true {\n" \
    "      const e = await on.pressed(actions.play)\n" \
    "      presses = presses + 1\n" \
    "      x = e.screen_x\n" \
    "      y = e.screen_y\n" \
    "    }\n" \
    "  }\n" \
    "  async {\n" \
    "    while true {\n" \
    "      await on.released(actions.play)\n" \
    "      releases = releases + 1\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "counter { Counter: {} }\n" \
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

/** The mut values of the Counter probe */
typedef struct {
    int32_t presses;  /**< Times actions.play was pressed */
    int32_t releases; /**< Times actions.play was released */
    float x;          /**< Cursor x of the last press */
    float y;          /**< Cursor y of the last press */
} trigger_counts_t;

/** Create a world with the prelude and a scene, and run two frames.
 *
 * @param scene Script code after the prelude
 * @return The world
 */
static ecs_world_t* trigger_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *code = flecs_asprintf("%s%s", TRIGGER_PRELUDE, scene);
    test_assert(ecs_script(world, { .code = code }) != 0);
    ecs_os_free(code);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Run a terminal command.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The command
 */
static void trigger_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd)
{
    ecs_os_free(box_command_run(world, s, cmd, NULL));
}

/** Get the counts of the Counter probe.
 *
 * @param world The world
 * @return The counts
 */
static const trigger_counts_t* trigger_counts(
    ecs_world_t *world)
{
    return ecs_get_id(world, ecs_lookup(world, "counter"), ecs_lookup(world, "Counter.mut"));
}

/** Get the state of an action.
 *
 * @param world The world
 * @param path Path of the action
 * @return Its state
 */
static const ActionState* trigger_state(
    ecs_world_t *world,
    const char *path)
{
    return ecs_get(world, ecs_lookup(world, path), ActionState);
}

/** Run frames and count those in which an action was pressed.
 *
 * @param world The world
 * @param path Path of the action
 * @param frames Frames to run
 * @return Frames in which it was pressed
 */
static int32_t trigger_presses(
    ecs_world_t *world,
    const char *path,
    int32_t frames)
{
    int32_t i, result = 0;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0);
        result += trigger_state(world, path)->pressed;
    }

    return result;
}

void ActionTrigger_script_defaults(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  play { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: actions.play}\n"
        "  }\n"
        "}\n");
    ecs_entity_t play = ecs_lookup(world, "hud.play");
    const ActionTrigger *t = ecs_get(world, play, ActionTrigger);
    test_assert(t != NULL);
    test_uint(t->action, ecs_lookup(world, "actions.play"));
    test_int(t->mode, ActionTriggerClick);
    test_int(t->button, KeyMouseLeft);
    test_bool(ecs_has(world, play, Interactive), true);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ActionTrigger);
    test_int(ecs_get(world, e, ActionTrigger)->button, KeyMouseLeft);
    test_int(ecs_get(world, e, ActionTrigger)->mode, ActionTriggerClick);
    test_uint(ecs_get(world, e, ActionTrigger)->action, 0);
    test_bool(ecs_has(world, e, Interactive), true);

    ecs_fini(world);
}

void ActionTrigger_zero_values_trigger_nothing(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  a { Position: {-200, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "  }\n"
        "  b { Position: {200, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "  }\n"
        "}\n");
    ecs_entity_t play = ecs_lookup(world, "actions.play");
    ecs_entity_t a = ecs_lookup(world, "hud.a");
    ecs_entity_t b = ecs_lookup(world, "hud.b");
    ecs_set(world, a, ActionTrigger, { .action = play });
    ecs_set(world, b, ActionTrigger, { .action = 0, .button = KeyMouseLeft });
    test_int(ecs_get(world, a, ActionTrigger)->button, KeyNone);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_click left [440, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 0);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, a);
    trigger_run(world, s, "mouse_click left [840, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 0);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, b);
    test_int(trigger_counts(world)->presses, 0);

    ecs_fini(world);
}

void ActionTrigger_click_taps_once(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  play { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: actions.play}\n"
        "  }\n"
        "}\n");
    ecs_entity_t play = ecs_lookup(world, "hud.play");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_click left [610, 350]");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->down, false);
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->down, false);
    test_uint(ecs_singleton_get(world, PickStatus)->target, play);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, play);

    ecs_progress(world, 0);
    const ActionState *state = trigger_state(world, "actions.play");
    test_bool(state->pressed, true);
    test_bool(state->down, true);
    test_bool(state->released, false);
    test_flt(state->start[0], 610);
    test_flt(state->start[1], 350);
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 0);
    test_flt(trigger_counts(world)->x, 610);
    test_flt(trigger_counts(world)->y, 350);

    ecs_progress(world, 0);
    state = trigger_state(world, "actions.play");
    test_bool(state->pressed, false);
    test_bool(state->down, false);
    test_bool(state->released, true);
    test_int(trigger_counts(world)->releases, 1);

    test_int(trigger_presses(world, "actions.play", 4), 0);
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 1);

    ecs_fini(world);
}

void ActionTrigger_three_clicks(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  play { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: actions.play}\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_click left [640, 360]");
    trigger_run(world, s, "mouse_click left [650, 360]");
    trigger_run(world, s, "mouse_click left [660, 360]");
    test_int(trigger_presses(world, "actions.play", 12), 3);
    test_int(trigger_counts(world)->presses, 3);
    test_int(trigger_counts(world)->releases, 3);
    test_bool(trigger_state(world, "actions.play")->down, false);

    ecs_fini(world);
}

void ActionTrigger_release_outside(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  play { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: actions.play}\n"
        "  }\n"
        "}\n");
    ecs_entity_t play = ecs_lookup(world, "hud.play");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_move [640, 360]");
    trigger_run(world, s, "mouse_button left true");
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, play);
    trigger_run(world, s, "mouse_move [100, 100]");
    ecs_progress(world, 0);
    trigger_run(world, s, "mouse_button left false");
    test_int(trigger_presses(world, "actions.play", 5), 0);
    test_int(trigger_counts(world)->presses, 0);
    test_int(trigger_counts(world)->releases, 0);

    trigger_run(world, s, "mouse_click left [640, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 1);

    ecs_fini(world);
}

void ActionTrigger_press_mode(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  play { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: actions.play, mode: ActionTriggerPress}\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_move [640, 360]");
    trigger_run(world, s, "mouse_button left true");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->down, false);
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->pressed, true);
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->released, true);
    test_int(trigger_presses(world, "actions.play", 3), 0);
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 1);

    trigger_run(world, s, "mouse_button left false");
    test_int(trigger_presses(world, "actions.play", 4), 0);
    test_int(trigger_counts(world)->presses, 1);

    ecs_fini(world);
}

/** The scene of the hold tests: a Hold trigger at the center of the screen */
#define TRIGGER_HOLD_SCENE \
    "hud {\n" \
    "  play { Position: {0, 0, 0}\n" \
    "    Rect: {120, 32}\n" \
    "    ActionTrigger: {action: actions.play, mode: ActionTriggerHold}\n" \
    "  }\n" \
    "}\n"

/** Press the button on the hold element and run until the action is down.
 *
 * @param world The world
 * @param s The terminal session
 */
static void trigger_hold_start(
    ecs_world_t *world,
    ecs_entity_t s)
{
    trigger_run(world, s, "mouse_move [640, 360]");
    trigger_run(world, s, "mouse_button left true");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->down, false);
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->pressed, true);
    test_bool(trigger_state(world, "actions.play")->down, true);
    test_int(trigger_counts(world)->presses, 1);
}

void ActionTrigger_hold_released_outside(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(TRIGGER_HOLD_SCENE);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_hold_start(world, s);
    trigger_run(world, s, "mouse_move [900, 500]");
    ecs_progress(world, 0);
    const ActionState *state = trigger_state(world, "actions.play");
    test_bool(state->down, true);
    test_bool(state->pressed, false);
    test_flt(state->start[0], 640);
    test_flt(state->start[1], 360);
    test_flt(state->delta[0], 260);
    test_flt(state->drag[0], 260);
    test_flt(state->drag[1], 140);
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->down, true);
    test_int(trigger_counts(world)->releases, 0);

    trigger_run(world, s, "mouse_button left false");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->down, true);
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->released, true);
    test_bool(trigger_state(world, "actions.play")->down, false);
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 1);

    ecs_fini(world);
}

void ActionTrigger_hold_released_by_delete(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(TRIGGER_HOLD_SCENE);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_hold_start(world, s);
    ecs_delete(world, ecs_lookup(world, "hud.play"));
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->released, true);
    test_bool(trigger_state(world, "actions.play")->down, false);
    trigger_run(world, s, "mouse_button left false");
    test_int(trigger_presses(world, "actions.play", 3), 0);
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 1);

    ecs_fini(world);
}

void ActionTrigger_hold_released_by_remove(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(TRIGGER_HOLD_SCENE);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_hold_start(world, s);
    ecs_remove(world, ecs_lookup(world, "hud.play"), ActionTrigger);
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->released, true);
    test_bool(trigger_state(world, "actions.play")->down, false);
    test_int(trigger_counts(world)->releases, 1);

    ecs_fini(world);
}

void ActionTrigger_hold_released_by_reset(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(TRIGGER_HOLD_SCENE);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_hold_start(world, s);
    trigger_run(world, s, "input_reset");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->released, true);
    test_bool(trigger_state(world, "actions.play")->down, false);
    test_int(trigger_presses(world, "actions.play", 3), 0);
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 1);

    ecs_fini(world);
}

void ActionTrigger_hold_released_by_focus_loss(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(TRIGGER_HOLD_SCENE);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_hold_start(world, s);
    box_input_focus(world, false);
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->released, true);
    test_bool(trigger_state(world, "actions.play")->down, false);
    test_int(trigger_presses(world, "actions.play", 3), 0);
    test_int(trigger_counts(world)->releases, 1);

    ecs_fini(world);
}

void ActionTrigger_right_button_ignores_left(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  play { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: actions.play, button: MouseRight}\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_click left [640, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 0);
    trigger_run(world, s, "mouse_click right [640, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 1);
    test_int(trigger_counts(world)->presses, 1);

    ecs_fini(world);
}

void ActionTrigger_blocker_keeps_binding_up(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "actions {\n"
        "  fire { Binding: {key: MouseLeft} }\n"
        "}\n"
        "hud {\n"
        "  panel { Position: {0, 0, 0}\n"
        "    Rect: {300, 200}\n"
        "    InputBlocker\n"
        "    play { Position: {0, 0, 1}\n"
        "      Rect: {120, 32}\n"
        "      ActionTrigger: {action: actions.play}\n"
        "    }\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    int32_t i, play = 0, fire = 0;
    trigger_run(world, s, "mouse_click left [640, 360]");
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        play += trigger_state(world, "actions.play")->pressed;
        fire += trigger_state(world, "actions.fire")->pressed;
    }

    test_int(play, 1);
    test_int(fire, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, ecs_lookup(world, "hud.panel.play"));

    ecs_remove(world, ecs_lookup(world, "hud.panel"), InputBlocker);
    play = fire = 0;
    trigger_run(world, s, "mouse_click left [640, 360]");
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        play += trigger_state(world, "actions.play")->pressed;
        fire += trigger_state(world, "actions.fire")->pressed;
    }

    test_int(play, 1);
    test_int(fire, 1);

    ecs_fini(world);
}

void ActionTrigger_world_space_element(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "panel {\n"
        "  Position: {0, 0, 0}\n"
        "  Scale: {0.01, 0.01, 0.01}\n"
        "  Rect: {400, 200}\n"
        "  ActionTrigger: {action: actions.play}\n"
        "}\n");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_click left [640, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 1);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, ecs_lookup(world, "panel"));
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 1);

    trigger_run(world, s, "mouse_click left [100, 100]");
    test_int(trigger_presses(world, "actions.play", 5), 0);

    ecs_fini(world);
}

void ActionTrigger_shape(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "shape {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {2, 2, 2}\n"
        "  ActionTrigger: {action: actions.play, mode: ActionTriggerHold}\n"
        "}\n");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_drag left [640, 360] [700, 400] 2 2");
    int32_t i, presses = 0, downs = 0;
    for (i = 0; i < 8; i ++) {
        ecs_progress(world, 0);
        presses += trigger_state(world, "actions.play")->pressed;
        downs += trigger_state(world, "actions.play")->down;
    }

    test_int(presses, 1);
    test_assert(downs >= 2);
    test_bool(trigger_state(world, "actions.play")->down, false);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, ecs_lookup(world, "shape"));
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 1);
    test_flt(trigger_counts(world)->x, 640);
    test_flt(trigger_counts(world)->y, 360);

    ecs_fini(world);
}

void ActionTrigger_binding_keeps_action_down(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "actions {\n"
        "  play { space { Binding: {key: Space} } }\n"
        "}\n"
        TRIGGER_HOLD_SCENE);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "key_down Space");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->pressed, true);
    trigger_run(world, s, "mouse_move [640, 360]");
    trigger_run(world, s, "mouse_button left true");
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    trigger_run(world, s, "mouse_button left false");
    test_int(trigger_presses(world, "actions.play", 4), 0);
    test_bool(trigger_state(world, "actions.play")->down, true);
    test_int(trigger_counts(world)->releases, 0);

    trigger_run(world, s, "key_up Space");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->released, true);
    test_int(trigger_counts(world)->presses, 1);
    test_int(trigger_counts(world)->releases, 1);

    trigger_run(world, s, "action_down actions.play");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->pressed, true);
    trigger_run(world, s, "mouse_button left true");
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    trigger_run(world, s, "mouse_button left false");
    test_int(trigger_presses(world, "actions.play", 4), 0);
    test_bool(trigger_state(world, "actions.play")->down, true);
    trigger_run(world, s, "action_up actions.play");
    ecs_progress(world, 0);
    test_bool(trigger_state(world, "actions.play")->released, true);
    test_int(trigger_counts(world)->presses, 2);
    test_int(trigger_counts(world)->releases, 2);

    ecs_fini(world);
}

void ActionTrigger_trigger_on_ancestor_does_nothing(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  panel { Position: {0, 0, 0}\n"
        "    Rect: {300, 200}\n"
        "    ActionTrigger: {action: actions.play}\n"
        "    button { Position: {0, 0, 1}\n"
        "      Rect: {120, 32}\n"
        "      Interactive\n"
        "    }\n"
        "  }\n"
        "}\n");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    trigger_run(world, s, "mouse_click left [640, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 0);
    test_uint(ecs_singleton_get(world, PickStatus)->handler,
        ecs_lookup(world, "hud.panel.button"));
    trigger_run(world, s, "mouse_click left [520, 300]");
    test_int(trigger_presses(world, "actions.play", 5), 1);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, ecs_lookup(world, "hud.panel"));

    ecs_fini(world);
}

void ActionTrigger_disabled_element(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "hud {\n"
        "  play { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: actions.play}\n"
        "  }\n"
        "}\n");
    ecs_entity_t play = ecs_lookup(world, "hud.play");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_enable(world, play, false);
    trigger_run(world, s, "mouse_click left [640, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 0);
    test_int(trigger_counts(world)->presses, 0);

    ecs_enable(world, play, true);
    trigger_run(world, s, "mouse_click left [640, 360]");
    test_int(trigger_presses(world, "actions.play", 5), 1);

    ecs_fini(world);
}

void ActionTrigger_deleted_action(void) {
    // ui/action_trigger
    ecs_world_t *world = trigger_world(
        "plain {}\n"
        "hud {\n"
        "  play { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: actions.play}\n"
        "  }\n"
        "  other { Position: {200, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    ActionTrigger: {action: plain}\n"
        "  }\n"
        "}\n");
    ecs_entity_t play = ecs_lookup(world, "hud.play");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_delete(world, ecs_lookup(world, "actions.play"));
    trigger_run(world, s, "mouse_click left [640, 360]");
    trigger_run(world, s, "mouse_click left [840, 360]");
    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    test_assert(ecs_is_alive(world, play));
    test_int(trigger_counts(world)->presses, 0);
    test_bool(ecs_has(world, ecs_lookup(world, "plain"), ActionState), false);

    ecs_fini(world);
}
