/**
 * @file Action.c
 * @brief Actions driven by key bindings and synthetic holds, and the script events they raise.
 */

#include <box.h>
#include <bake_test.h>

static InputEvent platform_events[4];
static int32_t platform_count = 0;

static void PlatformPoll(ecs_world_t *world, void *ctx) {
    (void)ctx;
    for (int32_t i = 0; i < platform_count; i ++) {
        box_input_platform_push(world, &platform_events[i]);
    }

    platform_count = 0;
}

void Action_import(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_lookup(world, "box.input.Action") == ecs_id(Action));
    test_assert(ecs_lookup(world, "box.input.ActionState") == ecs_id(ActionState));
    test_assert(ecs_lookup(world, "box.input.Binding") == ecs_id(Binding));
    test_assert(ecs_lookup(world, "box.input.ActionEvent") == ecs_id(ActionEvent));
    test_assert(ecs_lookup(world, "flecs.script.on.pressed") != 0);
    test_assert(ecs_lookup(world, "flecs.script.on.held") != 0);
    test_assert(ecs_lookup(world, "flecs.script.on.dragged") != 0);
    test_assert(ecs_lookup(world, "flecs.script.on.released") != 0);
    test_assert(ecs_lookup(world, "flecs.script.on.key_down") != 0);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { .description = "Jump" });
    test_assert(ecs_has(world, jump, ActionState));
    const ActionState *s = ecs_get(world, jump, ActionState);
    test_bool(s->down, false);
    test_bool(s->pressed, false);
    test_bool(s->released, false);

    ecs_fini(world);
}

void Action_binding_key(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Binding, { .action = jump, .key = KeySpace });

    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, jump, ActionState);
    test_bool(s->down, false);
    test_bool(s->pressed, false);

    box_input_key_press(world, KeySpace, 2);
    ecs_progress(world, 0);
    s = ecs_get(world, jump, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, true);
    test_bool(s->released, false);

    ecs_progress(world, 0);
    s = ecs_get(world, jump, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, false);
    test_bool(s->released, false);

    ecs_progress(world, 0);
    s = ecs_get(world, jump, ActionState);
    test_bool(s->down, false);
    test_bool(s->pressed, false);
    test_bool(s->released, true);

    ecs_progress(world, 0);
    s = ecs_get(world, jump, ActionState);
    test_bool(s->down, false);
    test_bool(s->released, false);

    ecs_fini(world);
}

void Action_binding_child(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, jump);
    ecs_set(world, b, Binding, { .key = KeyW });

    box_input_key(world, KeyW, true);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, jump, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, true);

    ecs_fini(world);
}

void Action_binding_self(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Action, { 0 });
    ecs_entity_t jump = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, jump, Action, { 0 });
    ecs_set(world, jump, Binding, { .key = KeyW });

    box_input_key(world, KeyW, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, true);
    test_bool(ecs_get(world, parent, ActionState)->down, false);

    ecs_fini(world);
}

void Action_binding_multiple(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fwd = ecs_new(world);
    ecs_set(world, fwd, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = fwd, .key = KeyW });
    ecs_set(world, ecs_new(world), Binding, { .action = fwd, .key = KeyUp });

    box_input_key(world, KeyW, true);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, fwd, ActionState);
    test_bool(s->pressed, true);

    box_input_key(world, KeyUp, true);
    ecs_progress(world, 0);
    s = ecs_get(world, fwd, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, false);

    box_input_key(world, KeyW, false);
    ecs_progress(world, 0);
    s = ecs_get(world, fwd, ActionState);
    test_bool(s->down, true);
    test_bool(s->released, false);

    box_input_key(world, KeyUp, false);
    ecs_progress(world, 0);
    s = ecs_get(world, fwd, ActionState);
    test_bool(s->down, false);
    test_bool(s->released, true);

    ecs_fini(world);
}

void Action_binding_modifier(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t save = ecs_new(world);
    ecs_set(world, save, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = save, .key = KeyS, .ctrl = true });

    box_input_key(world, KeyS, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->down, false);

    box_input_key(world, KeyS, false);
    box_input_key(world, KeyCtrlRight, true);
    box_input_key(world, KeyS, true);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, save, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, true);

    box_input_key(world, KeyShiftLeft, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->down, true);

    box_input_key(world, KeyCtrlRight, false);
    ecs_progress(world, 0);
    s = ecs_get(world, save, ActionState);
    test_bool(s->down, false);
    test_bool(s->released, true);

    ecs_fini(world);
}

void Action_binding_mouse_button(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fire = ecs_new(world);
    ecs_set(world, fire, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = fire, .key = KeyMouseRight });

    box_input_mouse_click(world, KeyMouseRight, (float[]){ 10, 20 }, false, 1);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, fire, ActionState);
    test_bool(s->pressed, true);
    test_flt(s->start[0], 10);
    test_flt(s->start[1], 20);

    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->released, true);

    ecs_fini(world);
}

void Action_binding_disabled(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Binding, { .action = jump, .key = KeySpace });
    ecs_enable(world, b, false);

    box_input_key(world, KeySpace, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, false);

    ecs_enable(world, b, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, true);

    ecs_fini(world);
}

void Action_binding_prefab(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });
    ecs_entity_t map = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, map);
    ecs_set(world, b, Binding, { .action = jump, .key = KeySpace });

    box_input_key(world, KeySpace, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, false);

    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, map);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, true);

    ecs_delete(world, inst);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->released, true);

    ecs_fini(world);
}

void Action_tap_in_one_frame(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = jump, .key = KeySpace });
    box_input_platform_set(world, &(box_input_platform_t){ .poll = PlatformPoll });

    platform_events[0] = (InputEvent){ .kind = InputKeyDown, .key = KeySpace };
    platform_events[1] = (InputEvent){ .kind = InputKeyUp, .key = KeySpace };
    platform_count = 2;
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, jump, ActionState);
    test_bool(s->pressed, true);
    test_bool(s->released, true);
    test_bool(s->down, false);

    ecs_progress(world, 0);
    s = ecs_get(world, jump, ActionState);
    test_bool(s->pressed, false);
    test_bool(s->released, false);

    ecs_fini(world);
}

void Action_drag_delta(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t place = ecs_new(world);
    ecs_set(world, place, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = place, .key = KeyMouseLeft });

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = {10, 10}, .to = {40, 70}, .steps = 3, .frames = 3 });

    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, place, ActionState);
    test_bool(s->pressed, true);
    test_flt(s->start[0], 10);
    test_flt(s->start[1], 10);
    test_flt(s->delta[0], 0);
    test_flt(s->drag[0], 0);

    ecs_progress(world, 0);
    s = ecs_get(world, place, ActionState);
    test_bool(s->down, true);
    test_flt(s->delta[0], 10);
    test_flt(s->delta[1], 20);
    test_flt(s->drag[0], 10);
    test_flt(s->drag[1], 20);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    s = ecs_get(world, place, ActionState);
    test_bool(s->down, true);
    test_flt(s->delta[0], 10);
    test_flt(s->drag[0], 30);
    test_flt(s->drag[1], 60);

    ecs_progress(world, 0);
    s = ecs_get(world, place, ActionState);
    test_bool(s->released, true);
    test_flt(s->delta[0], 0);
    test_flt(s->drag[0], 30);
    test_flt(s->drag[1], 60);

    ecs_progress(world, 0);
    s = ecs_get(world, place, ActionState);
    test_bool(s->released, false);
    test_flt(s->drag[0], 0);

    ecs_fini(world);
}

void Action_hold(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });

    box_input_action(world, jump, true);
    test_assert(box_input_pending(world) > 0);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, jump, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, true);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    s = ecs_get(world, jump, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, false);

    box_input_action(world, jump, false);
    ecs_progress(world, 0);
    s = ecs_get(world, jump, ActionState);
    test_bool(s->down, false);
    test_bool(s->released, true);

    ecs_fini(world);
}

void Action_hold_and_binding(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = jump, .key = KeySpace });

    box_input_key(world, KeySpace, true);
    ecs_progress(world, 0);
    box_input_action(world, jump, true);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, jump, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, false);

    box_input_key(world, KeySpace, false);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, true);

    box_input_action(world, jump, false);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->released, true);

    ecs_fini(world);
}

void Action_press(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });

    box_input_action_press(world, jump, 3);
    test_assert(box_input_pending(world) > 0);
    int32_t frames = 0;
    bool pressed = false, released = false;
    while (!released && frames < 10) {
        ecs_progress(world, 0);
        const ActionState *s = ecs_get(world, jump, ActionState);
        pressed = pressed || s->pressed;
        released = s->released;
        frames += s->down;
    }

    test_bool(pressed, true);
    test_bool(released, true);
    test_int(frames, 3);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Action_press_one_frame(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });

    box_input_action_press(world, jump, 0);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, jump, ActionState);
    test_bool(s->pressed, true);
    test_bool(s->down, true);

    ecs_progress(world, 0);
    s = ecs_get(world, jump, ActionState);
    test_bool(s->released, true);
    test_bool(s->down, false);

    ecs_fini(world);
}

void Action_drag(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t aim = ecs_new(world);
    ecs_set(world, aim, Action, { 0 });

    box_input_action_drag(world, aim, &(box_input_sweep_t){
        .from = {100, 100}, .to = {60, 100}, .steps = 2, .frames = 2 });

    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, aim, ActionState);
    test_bool(s->pressed, true);
    test_flt(s->start[0], 100);

    ecs_progress(world, 0);
    s = ecs_get(world, aim, ActionState);
    test_flt(s->delta[0], -20);
    test_flt(s->drag[0], -20);

    ecs_progress(world, 0);
    s = ecs_get(world, aim, ActionState);
    test_flt(s->drag[0], -40);
    test_bool(s->down, true);

    ecs_progress(world, 0);
    s = ecs_get(world, aim, ActionState);
    test_bool(s->released, true);
    test_flt(s->drag[0], -40);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyMouseLeft].down, false);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Action_reset_releases_holds(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });

    box_input_action(world, jump, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, true);

    box_input_reset(world);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, jump, ActionState);
    test_bool(s->down, false);
    test_bool(s->released, true);

    ecs_fini(world);
}

void Action_focus_lost_releases_holds(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });
    ecs_entity_t run = ecs_new(world);
    ecs_set(world, run, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = run, .key = KeyShiftLeft });

    box_input_action(world, jump, true);
    box_input_key(world, KeyShiftLeft, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, true);
    test_bool(ecs_get(world, run, ActionState)->down, true);

    box_input_focus(world, false);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->released, true);
    test_bool(ecs_get(world, run, ActionState)->released, true);

    ecs_fini(world);
}

void Action_deleted_action(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_new(world);
    ecs_set(world, jump, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = jump, .key = KeySpace });

    box_input_action(world, jump, true);
    box_input_key(world, KeySpace, true);
    ecs_progress(world, 0);
    ecs_delete(world, jump);
    ecs_progress(world, 0);
    box_input_action(world, jump, false);
    ecs_progress(world, 0);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Action_script_bindings(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.input\n"
            "actions {\n"
            "  Jump { Action: {description: \"Jump\"} }\n"
            "  Save { Action: {}\n"
            "    Binding: {key: S, ctrl: true}\n"
            "  }\n"
            "  Fire { Action: {}\n"
            "    mouse { Binding: {key: MouseLeft} }\n"
            "  }\n"
            "}\n"
            "play_map {\n"
            "  jump_space { Binding: {action: actions.Jump, key: Space} }\n"
            "  jump_up { Binding: {action: actions.Jump, key: Up} }\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t jump = ecs_lookup(world, "actions.Jump");
    ecs_entity_t save = ecs_lookup(world, "actions.Save");
    ecs_entity_t fire = ecs_lookup(world, "actions.Fire");
    test_assert(jump != 0);
    test_assert(save != 0);
    test_assert(fire != 0);
    test_str(ecs_get(world, jump, Action)->description, "Jump");
    const Binding *b = ecs_get(world, ecs_lookup(world, "play_map.jump_up"), Binding);
    test_assert(b != NULL);
    test_uint(b->action, jump);
    test_int(b->key, KeyUp);

    box_input_key(world, KeyUp, true);
    box_input_key(world, KeyMouseLeft, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->pressed, true);
    test_bool(ecs_get(world, fire, ActionState)->pressed, true);

    box_input_key(world, KeyUp, false);
    box_input_key(world, KeyMouseLeft, false);
    box_input_key(world, KeyCtrlLeft, true);
    box_input_key(world, KeyS, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->pressed, true);

    ecs_fini(world);
}

void Action_script_pressed_released(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "using box.input\n"
            "Jump { Action: {}\n"
            "  space { Binding: {key: Space} }\n"
            "}\n"
            "template Probe {\n"
            "  mut presses = 0\n"
            "  mut releases = 0\n"
            "  mut action: entity = 0\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.pressed(Jump)\n"
            "      presses = presses + 1\n"
            "      action = e.action\n"
            "    }\n"
            "  }\n"
            "  async {\n"
            "    while true {\n"
            "      await on.released(Jump)\n"
            "      releases = releases + 1\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 2);

    typedef struct { int64_t presses, releases; ecs_entity_t action; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t jump = ecs_lookup(world, "Jump");
    test_assert(p != 0);
    test_assert(mut != 0);

    box_input_key_press(world, KeySpace, 2);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->presses, 1);
    test_int(m->releases, 0);
    test_uint(m->action, jump);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    m = ecs_get_id(world, p, mut);
    test_int(m->presses, 1);
    test_int(m->releases, 1);

    box_input_action_press(world, jump, 1);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    m = ecs_get_id(world, p, mut);
    test_int(m->presses, 2);
    test_int(m->releases, 2);

    ecs_fini(world);
}

void Action_script_held(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "using box.input\n"
            "Run { Action: {} }\n"
            "template Probe {\n"
            "  mut frames = 0\n"
            "  async {\n"
            "    while true {\n"
            "      await on.held(Run)\n"
            "      frames = frames + 1\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 1);

    typedef struct { int64_t frames; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");

    box_input_action_press(world, ecs_lookup(world, "Run"), 4);
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0);
    }

    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->frames, 4);

    ecs_fini(world);
}

void Action_script_dragged(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "using box.input\n"
            "Place { Action: {}\n"
            "  Binding: {key: MouseLeft}\n"
            "}\n"
            "template Probe {\n"
            "  mut drags = 0\n"
            "  mut dx: f32 = 0\n"
            "  mut drag_x: f32 = 0\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.dragged(Place)\n"
            "      drags = drags + 1\n"
            "      dx = dx + e.delta_x\n"
            "    }\n"
            "  }\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.released(Place)\n"
            "      drag_x = e.drag_x\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 2);

    typedef struct { int64_t drags; float dx; float drag_x; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = {0, 0}, .to = {30, 0}, .steps = 3, .frames = 3 });
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->drags, 3);
    test_flt(m->dx, 30);
    test_flt(m->drag_x, 30);

    ecs_fini(world);
}

void Action_script_parent_target(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "using box.input\n"
            "actions {\n"
            "  Left { Action: {} }\n"
            "  Right { Action: {} }\n"
            "}\n"
            "template Probe {\n"
            "  mut presses = 0\n"
            "  mut last: entity = 0\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.pressed(actions)\n"
            "      presses = presses + 1\n"
            "      last = e.action\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 1);

    typedef struct { int64_t presses; ecs_entity_t last; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t left = ecs_lookup(world, "actions.Left");
    ecs_entity_t right = ecs_lookup(world, "actions.Right");

    box_input_action_press(world, left, 1);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->presses, 1);
    test_uint(m->last, left);

    box_input_action_press(world, right, 1);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    m = ecs_get_id(world, p, mut);
    test_int(m->presses, 2);
    test_uint(m->last, right);

    ecs_fini(world);
}

void Action_script_tap_sequence(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "using box.input\n"
            "Jump { Action: {}\n"
            "  Binding: {key: Space}\n"
            "}\n"
            "template Probe {\n"
            "  mut taps = 0\n"
            "  async {\n"
            "    while true {\n"
            "      await on.pressed(Jump)\n"
            "      await on.released(Jump)\n"
            "      taps = taps + 1\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 1);

    typedef struct { int64_t taps; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    box_input_platform_set(world, &(box_input_platform_t){ .poll = PlatformPoll });

    platform_events[0] = (InputEvent){ .kind = InputKeyDown, .key = KeySpace };
    platform_events[1] = (InputEvent){ .kind = InputKeyUp, .key = KeySpace };
    platform_count = 2;
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->taps, 1);

    ecs_fini(world);
}

void Action_script_wait_no_action(void) {
    // input/actions
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_log_set_level(-4);
    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "template Probe {\n"
            "  mut reached = false\n"
            "  async {\n"
            "    await on.pressed(0)\n"
            "    reached = true\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    ecs_script_tasks_progress(world);
    ecs_progress(world, 0);

    typedef struct { bool reached; } Mut;
    const Mut *m = ecs_get_id(world, ecs_lookup(world, "p"), ecs_lookup(world, "Probe.mut"));
    test_bool(m->reached, false);

    ecs_fini(world);
}

void Action_chord_most_specific(void) {
    // input_fixes/chord_bindings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Action, { 0 });
    ecs_entity_t save = ecs_new(world);
    ecs_set(world, save, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = back, .key = KeyS });
    ecs_set(world, ecs_new(world), Binding, { .action = save, .key = KeyS, .ctrl = true });

    box_input_key(world, KeyCtrlLeft, true);
    box_input_key(world, KeyS, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->pressed, true);
    test_bool(ecs_get(world, save, ActionState)->down, true);
    test_bool(ecs_get(world, back, ActionState)->pressed, false);
    test_bool(ecs_get(world, back, ActionState)->down, false);

    box_input_key(world, KeyS, false);
    box_input_key(world, KeyCtrlLeft, false);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->released, true);
    test_bool(ecs_get(world, back, ActionState)->down, false);

    box_input_key(world, KeyS, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, back, ActionState)->pressed, true);
    test_bool(ecs_get(world, back, ActionState)->down, true);
    test_bool(ecs_get(world, save, ActionState)->down, false);

    ecs_fini(world);
}

void Action_chord_two_modifiers(void) {
    // input_fixes/chord_bindings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t save = ecs_new(world);
    ecs_set(world, save, Action, { 0 });
    ecs_entity_t save_as = ecs_new(world);
    ecs_set(world, save_as, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = save, .key = KeyS, .ctrl = true });
    ecs_set(world, ecs_new(world), Binding, {
        .action = save_as, .key = KeyS, .ctrl = true, .shift = true });

    box_input_key(world, KeyCtrlRight, true);
    box_input_key(world, KeyShiftLeft, true);
    box_input_key(world, KeyS, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save_as, ActionState)->pressed, true);
    test_bool(ecs_get(world, save, ActionState)->down, false);

    ecs_fini(world);
}

void Action_chord_unbound_modifier(void) {
    // input_fixes/chord_bindings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = back, .key = KeyS });

    box_input_key(world, KeyCtrlLeft, true);
    box_input_key(world, KeyS, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, back, ActionState)->pressed, false);
    test_bool(ecs_get(world, back, ActionState)->down, false);

    box_input_key(world, KeyCtrlLeft, false);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, back, ActionState)->pressed, true);
    test_bool(ecs_get(world, back, ActionState)->down, true);

    box_input_key(world, KeyAltRight, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, back, ActionState)->down, false);
    test_bool(ecs_get(world, back, ActionState)->released, true);

    ecs_fini(world);
}

void Action_chord_bound_modifier(void) {
    // input_fixes/chord_bindings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fwd = ecs_new(world);
    ecs_set(world, fwd, Action, { 0 });
    ecs_entity_t fast = ecs_new(world);
    ecs_set(world, fast, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, { .action = fwd, .key = KeyW });
    ecs_set(world, ecs_new(world), Binding, { .action = fast, .key = KeyShiftLeft });

    box_input_key(world, KeyShiftLeft, true);
    box_input_key(world, KeyW, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fwd, ActionState)->pressed, true);
    test_bool(ecs_get(world, fast, ActionState)->pressed, true);

    box_input_key(world, KeyShiftRight, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fwd, ActionState)->down, false);
    test_bool(ecs_get(world, fast, ActionState)->down, true);

    ecs_fini(world);
}

void Action_chord_ignore_modifiers(void) {
    // input_fixes/chord_bindings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fire = ecs_new(world);
    ecs_set(world, fire, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, {
        .action = fire, .key = KeyMouseLeft, .ignore_modifiers = true });

    box_input_key(world, KeyCtrlLeft, true);
    box_input_key(world, KeyAltLeft, true);
    box_input_key(world, KeyMouseLeft, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->pressed, true);
    test_bool(ecs_get(world, fire, ActionState)->down, true);

    box_input_key(world, KeyCtrlLeft, false);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->down, true);

    ecs_fini(world);
}

void Action_chord_ignore_modifiers_masked(void) {
    // input_fixes/chord_bindings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Action, { 0 });
    ecs_entity_t save = ecs_new(world);
    ecs_set(world, save, Action, { 0 });
    ecs_set(world, ecs_new(world), Binding, {
        .action = back, .key = KeyS, .ignore_modifiers = true });
    ecs_set(world, ecs_new(world), Binding, { .action = save, .key = KeyS, .ctrl = true });

    box_input_key(world, KeyAltLeft, true);
    box_input_key(world, KeyS, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, back, ActionState)->pressed, true);
    test_bool(ecs_get(world, save, ActionState)->down, false);

    box_input_key(world, KeyCtrlLeft, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->down, true);
    test_bool(ecs_get(world, back, ActionState)->down, false);

    ecs_fini(world);
}

void Action_chord_script(void) {
    // input_fixes/chord_bindings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.input\n"
            "Back { Action: {}\n"
            "  Binding: {key: S}\n"
            "}\n"
            "Save { Action: {}\n"
            "  Binding: {key: S, ctrl: true}\n"
            "}\n"
            "Fire { Action: {}\n"
            "  Binding: {key: MouseLeft, ignore_modifiers: true}\n"
            "}\n"
    });
    test_assert(s != 0);
    ecs_entity_t back = ecs_lookup(world, "Back");
    ecs_entity_t save = ecs_lookup(world, "Save");
    ecs_entity_t fire = ecs_lookup(world, "Fire");

    box_input_key(world, KeyCtrlLeft, true);
    box_input_key(world, KeyS, true);
    box_input_key(world, KeyMouseLeft, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->pressed, true);
    test_bool(ecs_get(world, back, ActionState)->down, false);
    test_bool(ecs_get(world, fire, ActionState)->pressed, true);

    ecs_fini(world);
}
