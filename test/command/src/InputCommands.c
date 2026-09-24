/**
 * @file InputCommands.c
 * @brief Tests for the key, mouse, input and action commands of a session.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void InputCommands_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxCommand);
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);

    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 640;
    surface->height = 360;
}

void InputCommands_teardown(void)
{
    ecs_fini(w);
}

void InputCommands_key_press_queues_a_tap(void)
{
    // input/input_commands
    bool error = true;
    char *out = box_command_run(w, session, "key.press W", &error);
    test_bool(error, false);
    test_str(out, "W down for 1 frame");
    ecs_os_free(out);
    test_int(box_input_pending(w), 1);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].state, true);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].pressed, true);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].state, false);
}

void InputCommands_key_press_takes_a_duration(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "key.press Space 3", NULL);
    test_str(out, "Space down for 3 frames");
    ecs_os_free(out);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(w, 0);
        test_bool(ecs_singleton_get(w, Input)->keys[BoxKeySpace].state, true);
    }

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeySpace].state, false);
}

void InputCommands_key_down_and_up_hold_the_key(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "key.down a", NULL);
    test_str(out, "a down");
    ecs_os_free(out);

    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyA].state, true);

    out = box_command_run(w, session, "key.up A", NULL);
    test_str(out, "A up");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyA].state, false);
}

void InputCommands_an_unknown_key_is_an_error(void)
{
    // input/input_commands
    bool error = false;
    char *out = box_command_run(w, session, "key.press Nope", &error);
    test_bool(error, true);
    test_str(out, "unknown key 'Nope'");
    ecs_os_free(out);
    test_int(box_input_pending(w), 0);
}

void InputCommands_key_type_presses_one_key_per_character(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "key.type hi", NULL);
    test_str(out, "typed 2 characters over 2 frames");
    ecs_os_free(out);

    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_int(in->text_len, 1);
    test_bool(in->text[0] == 'h', true);
    test_bool(in->keys[BoxKeyH].state, true);

    ecs_progress(w, 0);
    in = ecs_singleton_get(w, Input);
    test_int(in->text_len, 1);
    test_bool(in->text[0] == 'i', true);
    test_bool(in->keys[BoxKeyI].state, true);
    test_bool(in->keys[BoxKeyH].state, false);
}

void InputCommands_mouse_move_pins_the_cursor(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "mouse.move 10,20", NULL);
    test_str(out, "cursor 10,20");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 10);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.y, 20);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove,
        .source = InputSourcePlatform,
        .value = {100, 200}
    });

    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 10);

    out = box_command_run(w, session, "mouse.release", NULL);
    test_str(out, "cursor released");
    ecs_os_free(out);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove,
        .source = InputSourcePlatform,
        .value = {100, 200}
    });

    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 100);
}

void InputCommands_mouse_move_takes_normalized_coordinates(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "mouse.move 0,0 true", NULL);
    test_str(out, "cursor 320,180");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.normalized.x, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.normalized.y, 0);
}

void InputCommands_an_invalid_position_is_an_error(void)
{
    // input/input_commands
    bool error = false;
    char *out = box_command_run(w, session, "mouse.move 10", &error);
    test_bool(error, true);
    test_str(out, "invalid position '10', expected x,y");
    ecs_os_free(out);
    test_int(box_input_pending(w), 0);
}

void InputCommands_mouse_sweep_spreads_over_frames(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "mouse.sweep 0,0 40,0 4 4", NULL);
    test_str(out, "sweep 0,0 to 40,0 in 4 steps over 4 frames");
    ecs_os_free(out);
    test_int(box_input_pending(w), 5);

    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 0);

    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 10);

    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 40);
    test_int(box_input_pending(w), 0);
}

void InputCommands_mouse_sweep_defaults_to_eight_steps(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "mouse.sweep 0,0 80,0", NULL);
    test_str(out, "sweep 0,0 to 80,0 in 8 steps over 8 frames");
    ecs_os_free(out);
    test_int(box_input_pending(w), 9);
}

void InputCommands_mouse_click_presses_the_button(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "mouse.click left 32,64", NULL);
    test_str(out, "left click for 1 frame");
    ecs_os_free(out);

    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyMouseLeft].pressed, true);
    test_flt(in->mouse.window.x, 32);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyMouseLeft].state, false);
}

void InputCommands_mouse_drag_holds_the_button_over_the_sweep(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "mouse.drag left 0,0 20,0 2 2", NULL);
    test_str(out, "left drag 0,0 to 20,0 in 2 steps over 2 frames");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyMouseLeft].state, true);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 0);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyMouseLeft].state, true);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 10);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyMouseLeft].state, false);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 20);
}

void InputCommands_mouse_scroll_queues_the_offset(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "mouse.scroll 0 1.5", NULL);
    test_str(out, "scroll 0,1.5");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.scroll.y, 1.5);
    test_flt(ecs_singleton_get(w, Input)->mouse.scroll_delta.y, 1.5);
}

void InputCommands_input_reset_empties_the_queue(void)
{
    // input/input_commands
    ecs_os_free(box_command_run(w, session, "key.down W", NULL));
    ecs_progress(w, 0);
    ecs_os_free(box_command_run(w, session, "key.press S 10", NULL));
    test_int(box_input_pending(w), 1);

    char *out = box_command_run(w, session, "input.reset", NULL);
    test_str(out, "input reset");
    ecs_os_free(out);
    test_int(box_input_pending(w), 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].state, false);
}

void InputCommands_input_focus_changes_the_flag(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "input.focus false", NULL);
    test_str(out, "focus false");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->focused, false);

    bool error = false;
    out = box_command_run(w, session, "input.focus maybe", &error);
    test_bool(error, true);
    test_str(out, "invalid boolean 'maybe'");
    ecs_os_free(out);

    out = box_command_run(w, session, "input.focus 1", NULL);
    test_str(out, "focus true");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->focused, true);
}

void InputCommands_input_wait_reports_the_pending_events(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "input.wait", NULL);
    test_str(out, "queue drained");
    ecs_os_free(out);

    ecs_os_free(box_command_run(w, session, "mouse.sweep 0,0 8,0 2 2", NULL));
    out = box_command_run(w, session, "input.wait", NULL);
    test_str(out, "3 events pending");
    ecs_os_free(out);

    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    out = box_command_run(w, session, "input.wait", NULL);
    test_str(out, "queue drained");
    ecs_os_free(out);
}

void InputCommands_action_press_holds_the_action_for_a_frame(void)
{
    // input/input_commands
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);

    bool error = true;
    char *out = box_command_run(w, session, "action.press actions.Forward", &error);
    test_bool(error, false);
    test_str(out, "Forward down for 1 frame");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->pressed, true);
    test_bool(ecs_get(w, forward, InputAction)->held, true);

    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, false);
    test_bool(ecs_get(w, forward, InputAction)->released, true);
}

void InputCommands_action_down_and_up_hold_the_action(void)
{
    // input/input_commands
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);

    char *out = box_command_run(w, session, "action.down actions.Forward", NULL);
    test_str(out, "Forward down");
    ecs_os_free(out);

    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);

    out = box_command_run(w, session, "action.up actions.Forward", NULL);
    test_str(out, "Forward up");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, false);
}

void InputCommands_action_up_without_a_hold_is_an_error(void)
{
    // input/input_commands
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);

    bool error = false;
    char *out = box_command_run(w, session, "action.up actions.Forward", &error);
    test_bool(error, true);
    test_str(out, "'Forward' is not held by a command");
    ecs_os_free(out);
}

void InputCommands_an_action_command_needs_an_action(void)
{
    // input/input_commands
    bool error = false;
    char *out = box_command_run(w, session, "action.press agent0", &error);
    test_bool(error, true);
    test_str(out, "'agent0' is not an action");
    ecs_os_free(out);
}

void InputCommands_action_drag_holds_the_action_while_the_cursor_sweeps(void)
{
    // input/input_commands
    ecs_entity_t look = ecs_entity(w, { .name = "actions.Look" });
    ecs_add(w, look, InputAction);

    char *out = box_command_run(
        w, session, "action.drag actions.Look 0,0 20,0 2 2", NULL);
    test_str(out, "Look drag 0,0 to 20,0 in 2 steps over 2 frames");
    ecs_os_free(out);

    ecs_progress(w, 0);
    test_bool(ecs_get(w, look, InputAction)->held, true);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 0);

    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(ecs_get(w, look, InputAction)->held, true);
    test_flt(ecs_singleton_get(w, Input)->mouse.window.x, 20);

    ecs_progress(w, 0);
    test_bool(ecs_get(w, look, InputAction)->held, false);
}

void InputCommands_action_list_shows_the_bindings(void)
{
    // input/input_commands
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_set(w, forward, InputAction, { .description = "Move forward" });
    ecs_set(w, ecs_new(w), Binding, {
        .mask = BOX_KEY_MOD_CTRL | BoxKeyW, .action = forward });
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyUp, .action = forward });

    char *out = box_command_run(w, session, "action.list", NULL);
    test_str(out, "actions.Forward\tCtrl+W,Up\tMove forward\n");
    ecs_os_free(out);
}

void InputCommands_the_help_listing_includes_the_input_commands(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "help", NULL);
    test_assert(strstr(out, "key.press <key:string> [frames:i32]") != NULL);
    test_assert(strstr(out, "action.list - ") != NULL);
    test_assert(strstr(out, "mouse.drag <button:string>") != NULL);
    ecs_os_free(out);
}

void InputCommands_an_unknown_input_command_is_an_error(void)
{
    // input/input_commands
    bool error = false;
    char *out = box_command_run(w, session, "key.smash W", &error);
    test_bool(error, true);
    test_str(out, "unknown command 'key.smash'");
    ecs_os_free(out);

    error = false;
    out = box_command_run(w, session, "key", &error);
    test_bool(error, true);
    test_str(out, "unknown command 'key'");
    ecs_os_free(out);
}

void InputCommands_mouse_sweep_caps_steps(void)
{
    // input/input_commands
    char *out = box_command_run(w, session, "mouse.sweep 0,0 40,0 2147483647 2147483647", NULL);
    test_str(out, "sweep 0,0 to 40,0 in 4096 steps over 36000 frames");
    ecs_os_free(out);
    test_int(box_input_pending(w), 4097);
}
