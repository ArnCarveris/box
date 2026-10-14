/**
 * @file Input.c
 * @brief Tests for the key, mouse, input and action commands.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

void Input_key_press(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "key.press W", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyW].down, true);
    test_bool(in->keys[KeyW].pressed, true);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyW].down, false);
    test_bool(in->keys[KeyW].released, true);

    ecs_fini(world);
}

void Input_key_press_dur(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "key.press space 3", NULL));

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        test_bool(ecs_singleton_get(world, Input)->keys[KeySpace].down, true);
    }

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeySpace].down, false);

    ecs_fini(world);
}

void Input_key_press_unknown(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "key.press Nope", &error);
    test_bool(error, true);
    test_str(out, "error: unknown key 'Nope'");
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_key_press_negative_dur(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "key.press W -2", &error);
    test_bool(error, true);
    test_str(out, "error: dur: negative frame count -2");
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_key_down_up(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "key.down ArrowLeft", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyLeft].down, true);

    ecs_os_free(box_terminal_run(world, s, "key.up ArrowLeft", NULL));
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyLeft].down, false);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyLeft].released, true);

    ecs_fini(world);
}

void Input_key_type(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "key.type \"h i\"", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_str(in->text, "h");
    test_bool(in->keys[KeyH].down, true);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_str(in->text, " ");
    test_bool(in->keys[KeyH].down, false);
    test_bool(in->keys[KeySpace].down, true);

    ecs_progress(world, 0);
    test_str(ecs_singleton_get(world, Input)->text, "i");

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_str(in->text, "");
    test_bool(in->keys[KeyI].down, false);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_key_type_dur(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "key.type ab 2", NULL));

    ecs_progress(world, 0);
    test_str(ecs_singleton_get(world, Input)->text, "a");
    ecs_progress(world, 0);
    test_str(ecs_singleton_get(world, Input)->text, "");
    test_bool(ecs_singleton_get(world, Input)->keys[KeyA].down, true);
    ecs_progress(world, 0);
    test_str(ecs_singleton_get(world, Input)->text, "b");
    test_bool(ecs_singleton_get(world, Input)->keys[KeyA].down, false);

    ecs_fini(world);
}

void Input_mouse_move(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "mouse.move [10, 20.5]", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 10);
    test_flt(in->mouse.position[1], 20.5);
    test_bool(in->mouse.pinned, true);

    ecs_os_free(box_terminal_run(world, s, "mouse.move 30,40", NULL));
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 30);
    test_flt(in->mouse.position[1], 40);
    test_flt(in->mouse.delta[0], 20);
    test_flt(in->mouse.delta[1], 19.5);

    ecs_fini(world);
}

void Input_mouse_move_normalized(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_queue(world, &(InputEvent){ .kind = InputResize, .value = { 200, 100 } });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.move {0.5 0.25} true", NULL));

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 100);
    test_flt(in->mouse.position[1], 25);

    ecs_fini(world);
}

void Input_mouse_move_invalid(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "mouse.move [10]", &error);
    test_bool(error, true);
    test_str(out, "error: pos: invalid position '[10]'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse.move [10, 20, 30]", &error);
    test_bool(error, true);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse.move here", &error);
    test_bool(error, true);
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_mouse_sweep(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "mouse.sweep [0, 0] [100, 0] 4 4", &error);
    test_bool(error, false);
    ecs_os_free(out);

    float expect[] = { 0, 25, 50, 75, 100 };
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
        test_flt(ecs_singleton_get(world, Input)->mouse.position[0], expect[i]);
    }

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_mouse_sweep_default_steps(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.sweep [0, 0] [0, 100]", NULL));

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_flt(ecs_singleton_get(world, Input)->mouse.position[1], 10);

    int32_t i;
    for (i = 0; i < 9; i ++) {
        ecs_progress(world, 0);
    }

    test_flt(ecs_singleton_get(world, Input)->mouse.position[1], 100);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_mouse_release(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.move [1, 2]", NULL));
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.pinned, true);

    bool error = true;
    char *out = box_terminal_run(world, s, "mouse.release", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.pinned, false);

    ecs_fini(world);
}

void Input_mouse_button(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.button left true", NULL));
    ecs_os_free(box_terminal_run(world, s, "mouse.button MouseRight on", NULL));
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseLeft].down, true);
    test_bool(in->keys[KeyMouseRight].down, true);
    test_bool(in->keys[KeyLeft].down, false);

    ecs_os_free(box_terminal_run(world, s, "mouse.button Left false", NULL));
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseLeft].down, false);
    test_bool(in->keys[KeyMouseRight].down, true);

    ecs_fini(world);
}

void Input_mouse_button_unknown(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "mouse.button W true", &error);
    test_bool(error, true);
    test_str(out, "error: unknown button 'W'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse.button left maybe", &error);
    test_bool(error, true);
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_mouse_click(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "mouse.click right [5, 6]", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 5);
    test_flt(in->mouse.position[1], 6);
    test_bool(in->keys[KeyMouseRight].pressed, true);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyMouseRight].released, true);

    ecs_fini(world);
}

void Input_mouse_click_no_pos(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.click left", NULL));
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseLeft].pressed, true);
    test_bool(in->mouse.pinned, false);

    ecs_fini(world);
}

void Input_mouse_click_dur(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.click middle - 3", NULL));
    test_int(box_input_pending(world), 2);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        test_bool(ecs_singleton_get(world, Input)->keys[KeyMouseMiddle].down, true);
    }

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyMouseMiddle].down, false);
    test_bool(ecs_singleton_get(world, Input)->mouse.pinned, false);

    ecs_fini(world);
}

void Input_mouse_drag(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "mouse.drag left [0, 0] [10, 0] 2 2", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 0);
    test_bool(in->keys[KeyMouseLeft].pressed, true);

    ecs_progress(world, 0);
    test_flt(ecs_singleton_get(world, Input)->mouse.position[0], 5);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 10);
    test_bool(in->keys[KeyMouseLeft].down, true);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyMouseLeft].released, true);

    ecs_fini(world);
}

void Input_mouse_drag_invalid(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "mouse.drag left [0, 0] nowhere", &error);
    test_bool(error, true);
    test_str(out, "error: to: invalid position 'nowhere'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse.drag Space [0, 0] [1, 1]", &error);
    test_bool(error, true);
    test_str(out, "error: unknown button 'Space'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse.drag left [0, 0] [1, 1] -1", &error);
    test_bool(error, true);
    test_str(out, "error: steps: negative frame count -1");
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_mouse_scroll(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.scroll 0 -1.5", NULL));
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.scroll[0], 0);
    test_flt(in->mouse.scroll[1], -1.5);

    ecs_progress(world, 0);
    test_flt(ecs_singleton_get(world, Input)->mouse.scroll[1], 0);

    ecs_fini(world);
}

void Input_input_reset(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "key.down W", NULL));
    ecs_os_free(box_terminal_run(world, s, "mouse.move [3, 4]", NULL));
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyW].down, true);

    ecs_os_free(box_terminal_run(world, s, "key.type hello", NULL));
    test_assert(box_input_pending(world) > 1);

    bool error = true;
    char *out = box_terminal_run(world, s, "input.reset", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);
    test_int(box_input_pending(world), 1);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyW].down, false);
    test_bool(in->keys[KeyW].released, true);
    test_bool(in->mouse.pinned, false);
    test_str(in->text, "");
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_input_focus(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "key.down W", NULL));
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_terminal_run(world, s, "input.focus false", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->focused, false);
    test_bool(in->keys[KeyW].down, false);

    ecs_os_free(box_terminal_run(world, s, "input.focus true", NULL));
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->focused, true);

    ecs_fini(world);
}

void Input_input_focus_invalid(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "input.focus maybe", &error);
    test_bool(error, true);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "input.focus", &error);
    test_bool(error, true);
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_input_wait_empty(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "input.wait", &error);
    test_bool(error, false);
    test_str(out, "0");
    ecs_os_free(out);

    ecs_fini(world);
}

void Input_input_wait_pending(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "key.press W 3", NULL));

    char *out = box_terminal_run(world, s, "input.wait", NULL);
    test_str(out, "4");
    ecs_os_free(out);

    ecs_progress(world, 0);
    out = box_terminal_run(world, s, "input.wait", NULL);
    test_str(out, "3");
    ecs_os_free(out);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    out = box_terminal_run(world, s, "input.wait", NULL);
    test_str(out, "0");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyW].released, true);

    ecs_fini(world);
}

void Input_input_wait_sweep(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.sweep [0, 0] [8, 0] 4 8", NULL));

    char *out = box_terminal_run(world, s, "input.wait", NULL);
    test_str(out, "9");
    ecs_os_free(out);

    int32_t i;
    for (i = 0; i < 9; i ++) {
        test_assert(box_input_pending(world) > 0);
        ecs_progress(world, 0);
    }

    out = box_terminal_run(world, s, "input.wait", NULL);
    test_str(out, "0");
    ecs_os_free(out);
    test_flt(ecs_singleton_get(world, Input)->mouse.position[0], 8);

    ecs_fini(world);
}

void Input_action_press(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_entity(world, { .name = "Jump" });
    ecs_set(world, jump, Action, { .description = "Jump up" });

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "action.press Jump", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_progress(world, 0);
    const ActionState *st = ecs_get(world, jump, ActionState);
    test_bool(st->down, true);
    test_bool(st->pressed, true);

    ecs_progress(world, 0);
    st = ecs_get(world, jump, ActionState);
    test_bool(st->down, false);
    test_bool(st->released, true);

    ecs_fini(world);
}

void Input_action_press_dur(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_entity(world, { .name = "Jump" });
    ecs_set(world, jump, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "action.press Jump 3", NULL));

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        test_bool(ecs_get(world, jump, ActionState)->down, true);
    }

    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, false);

    ecs_fini(world);
}

void Input_action_down_up(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fire = ecs_entity(world, { .name = "Fire" });
    ecs_set(world, fire, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "action.down Fire", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const ActionState *st = ecs_get(world, fire, ActionState);
    test_bool(st->down, true);
    test_bool(st->pressed, false);

    ecs_os_free(box_terminal_run(world, s, "action.up Fire", NULL));
    ecs_progress(world, 0);
    st = ecs_get(world, fire, ActionState);
    test_bool(st->down, false);
    test_bool(st->released, true);

    ecs_fini(world);
}

void Input_action_by_name(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_entity(world, { .name = "controls.Jump" });
    ecs_set(world, jump, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "action.down Jump", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_os_free(box_terminal_run(world, s, "action.press controls.Jump", &error));
    test_bool(error, false);

    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, true);

    ecs_fini(world);
}

void Input_action_relative_to_cwd(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "controls.Jump" });
    ecs_set(world, a, Action, { 0 });
    ecs_entity_t b = ecs_entity(world, { .name = "menu.Jump" });
    ecs_set(world, b, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd menu", NULL));
    bool error = true;
    char *out = box_terminal_run(world, s, "action.down Jump", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_progress(world, 0);
    test_bool(ecs_get(world, a, ActionState)->down, false);
    test_bool(ecs_get(world, b, ActionState)->down, true);

    ecs_fini(world);
}

void Input_action_ambiguous(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "controls.Jump" });
    ecs_set(world, a, Action, { 0 });
    ecs_entity_t b = ecs_entity(world, { .name = "menu.Jump" });
    ecs_set(world, b, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "action.down Jump", &error);
    test_bool(error, true);
    test_str(out, "error: ambiguous action 'Jump', 2 match");
    ecs_os_free(out);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_action_unknown(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "Thing" });

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "action.press Nope", &error);
    test_bool(error, true);
    test_str(out, "error: no action 'Nope'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "action.up Thing", &error);
    test_bool(error, true);
    test_str(out, "error: 'Thing' is not an action");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "action.drag Nope [0, 0] [1, 1]", &error);
    test_bool(error, true);
    test_str(out, "error: no action 'Nope'");
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_action_prefab_ignored(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_entity(world, { .name = "map.Jump" });
    ecs_add_id(world, p, EcsPrefab);
    ecs_set(world, p, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "action.down Jump", &error);
    test_bool(error, true);
    test_str(out, "error: no action 'Jump'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "action.list", NULL);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Input_action_drag(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pan = ecs_entity(world, { .name = "Pan" });
    ecs_set(world, pan, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "action.drag Pan [0, 0] [10, 4] 2 2", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_progress(world, 0);
    const ActionState *st = ecs_get(world, pan, ActionState);
    test_bool(st->pressed, true);
    test_flt(st->drag[0], 0);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    st = ecs_get(world, pan, ActionState);
    test_bool(st->down, true);
    test_flt(st->drag[0], 10);
    test_flt(st->drag[1], 4);

    ecs_progress(world, 0);
    test_bool(ecs_get(world, pan, ActionState)->released, true);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Input_action_list(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_entity(world, { .name = "controls.Jump" });
    ecs_set(world, jump, Action, { .description = "Jump up" });
    ecs_entity_t fire = ecs_entity(world, { .name = "controls.Fire" });
    ecs_set(world, fire, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "action.down Fire", NULL));
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_terminal_run(world, s, "action.list", &error);
    test_bool(error, false);
    test_str(out, "controls.Fire (down)\ncontrols.Jump: Jump up");
    ecs_os_free(out);

    ecs_fini(world);
}

void Input_action_list_empty(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "action.list", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Input_not_imported(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, BoxCommand);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "key.press W", &error);
    test_bool(error, true);
    test_str(out, "error: box.input is not imported");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "action.list", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_fini(world);
}

void Input_http_exec(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *path = flecs_asprintf("/terminal/exec?session=%s&cmd=key.press%%20Enter%%202",
        ecs_get_name(world, s));
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &reply), 200);
    ecs_os_free(ecs_strbuf_get(&reply.body));
    ecs_os_free(path);

    path = flecs_asprintf("/terminal/exec?session=%s&cmd=input.wait", ecs_get_name(world, s));
    ecs_http_reply_t wait = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &wait), 200);
    char *body = ecs_strbuf_get(&wait.body);
    test_str(body, "3");
    ecs_os_free(body);
    ecs_os_free(path);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyEnter].down, true);

    ecs_fini(world);
}

void Input_commands_are_script_functions(void) {
    // input/commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *names[] = {
        "key.press", "key.down", "key.up", "key.type",
        "mouse.move", "mouse.sweep", "mouse.release", "mouse.button",
        "mouse.click", "mouse.drag", "mouse.scroll",
        "input.reset", "input.focus", "input.wait",
        "action.press", "action.down", "action.up", "action.drag", "action.list"
    };

    int32_t i;
    for (i = 0; i < 19; i ++) {
        char *path = flecs_asprintf("box.command.commands.%s", names[i]);
        ecs_entity_t f = ecs_lookup(world, path);
        test_assert(f != 0);
        test_assert(ecs_has(world, f, EcsScriptFunction));
        ecs_os_free(path);
    }

    ecs_fini(world);
}
