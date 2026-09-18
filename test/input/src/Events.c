/**
 * @file Events.c
 * @brief Tests for the action events a script task awaits with on.pressed.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/input/input_internal.h"

static ecs_world_t *w;
static ecs_entity_t fired_tag;

void Events_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxInput);
    fired_tag = ecs_entity(w, { .name = "Fired" });
}

void Events_teardown(void)
{
    ecs_fini(w);
}

void Events_a_script_resumes_on_the_pressed_frame(void)
{
    // input/script_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Forward { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Forward)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter waiter\n" });
    test_assert(s != 0);

    ecs_entity_t forward = ecs_lookup(w, "actions.Forward");
    ecs_entity_t waiter = ecs_lookup(w, "waiter");
    test_assert(forward != 0);
    test_assert(waiter != 0);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired_tag), false);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired_tag), true);
}

void Events_on_down_resumes_while_the_action_is_held(void)
{
    // input/script_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Forward { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.down(actions.Forward)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter waiter\n" });
    test_assert(s != 0);

    ecs_entity_t forward = ecs_lookup(w, "actions.Forward");
    ecs_entity_t waiter = ecs_lookup(w, "waiter");
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired_tag), false);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired_tag), true);
}

void Events_on_up_resumes_when_the_action_is_released(void)
{
    // input/script_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Forward { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.up(actions.Forward)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter waiter\n" });
    test_assert(s != 0);

    ecs_entity_t forward = ecs_lookup(w, "actions.Forward");
    ecs_entity_t waiter = ecs_lookup(w, "waiter");
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired_tag), false);

    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired_tag), true);
}

void Events_several_waiters_resume_on_one_action(void)
{
    // input/script_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Forward { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Forward)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter first\n"
        "Waiter second\n" });
    test_assert(s != 0);

    ecs_entity_t forward = ecs_lookup(w, "actions.Forward");
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_progress(w, 0);
    const ActionWaiters *waiters = ecs_singleton_get(w, ActionWaiters);
    test_int(ecs_vec_count(&waiters->waiters), 2);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ecs_lookup(w, "first"), fired_tag), true);
    test_bool(ecs_has_id(w, ecs_lookup(w, "second"), fired_tag), true);
    test_int(ecs_vec_count(&waiters->waiters), 0);
}

void Events_an_emitted_action_resumes_a_waiter(void)
{
    // input/script_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Fire { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Fire)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter waiter\n" });
    test_assert(s != 0);

    ecs_entity_t fire = ecs_lookup(w, "actions.Fire");
    ecs_entity_t waiter = ecs_lookup(w, "waiter");
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired_tag), false);

    box_input_emit(w, fire, ecs_entity(w, { .name = "button" }));
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired_tag), true);
}

void Events_the_wait_carries_the_source(void)
{
    // input/script_events
    ecs_entity_t source = ecs_entity(w, { .name = "Source" });
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Fire { InputAction } }\n"
        "template Waiter {\n"
        "  mut src: entity = 0\n"
        "  async {\n"
        "    const from = await on.pressed(actions.Fire)\n"
        "    src = from\n"
        "  }\n"
        "  if src != 0 {\n"
        "    (Source, src)\n"
        "  }\n"
        "}\n"
        "Waiter waiter\n" });
    test_assert(s != 0);

    ecs_entity_t fire = ecs_lookup(w, "actions.Fire");
    ecs_entity_t waiter = ecs_lookup(w, "waiter");
    ecs_entity_t button = ecs_entity(w, { .name = "button" });
    ecs_progress(w, 0);
    box_input_emit(w, fire, button);
    ecs_progress(w, 0);
    test_uint(ecs_get_target(w, waiter, source, 0), button);
}

void Events_deleting_the_entity_of_a_task_cancels_the_wait(void)
{
    // input/script_events
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Forward { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Forward)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter waiter\n" });
    test_assert(s != 0);

    ecs_entity_t forward = ecs_lookup(w, "actions.Forward");
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_progress(w, 0);
    const ActionWaiters *waiters = ecs_singleton_get(w, ActionWaiters);
    test_int(ecs_vec_count(&waiters->waiters), 1);

    ecs_delete(w, ecs_lookup(w, "waiter"));
    test_int(ecs_vec_count(&waiters->waiters), 0);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_assert(ecs_lookup(w, "waiter") == 0);
}
