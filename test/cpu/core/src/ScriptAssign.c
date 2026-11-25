/**
 * @file ScriptAssign.c
 * @brief Tests for flecs.script.on.assign: an async block that waits for an exported mut variable
 * of a scope to be assigned instead of evaluating every frame.
 */

#include <box.h>
#include <bake_test.h>

/** A scope with an input, a value derived from it and the number of times it was derived */
#define SCRIPT_ASSIGN_STATE \
    "using flecs.script\n" \
    "state {\n" \
    "  export mut input: i32 = 1\n" \
    "  export mut twice: i32 = 2\n" \
    "  export mut runs: i32 = 0\n" \
    "}\n" \
    "other {\n" \
    "  export mut input: i32 = 1\n" \
    "}\n" \
    "async {\n" \
    "  while true {\n" \
    "    await on.assign(state)\n" \
    "    state.twice = state.input * 2\n" \
    "    state.runs = state.runs + 1\n" \
    "  }\n" \
    "}\n"

void ScriptAssign_waits_without_assignment(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = SCRIPT_ASSIGN_STATE }) != 0);

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_mut_var_get_t(world, "state.runs", ecs_i32_t), 0);
    test_int(ecs_mut_var_get_t(world, "state.twice", ecs_i32_t), 2);

    ecs_fini(world);
}

void ScriptAssign_resumes_on_assignment(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = SCRIPT_ASSIGN_STATE }) != 0);
    ecs_progress(world, 0);

    ecs_entity_t input = ecs_lookup(world, "state.input");
    test_assert(input != 0);
    *(int32_t*)ecs_mut_var_get(world, input).ptr = 21;
    ecs_mut_var_modified(world, input);
    ecs_progress(world, 0);
    test_int(ecs_mut_var_get_t(world, "state.twice", ecs_i32_t), 42);
    test_int(ecs_mut_var_get_t(world, "state.runs", ecs_i32_t), 1);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_mut_var_get_t(world, "state.runs", ecs_i32_t), 1);

    *(int32_t*)ecs_mut_var_get(world, input).ptr = 4;
    ecs_mut_var_modified(world, input);
    ecs_progress(world, 0);
    test_int(ecs_mut_var_get_t(world, "state.twice", ecs_i32_t), 8);
    test_int(ecs_mut_var_get_t(world, "state.runs", ecs_i32_t), 2);

    ecs_fini(world);
}

void ScriptAssign_other_scope_does_not_resume(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = SCRIPT_ASSIGN_STATE }) != 0);
    ecs_progress(world, 0);

    ecs_entity_t input = ecs_lookup(world, "other.input");
    test_assert(input != 0);
    *(int32_t*)ecs_mut_var_get(world, input).ptr = 21;
    ecs_mut_var_modified(world, input);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_mut_var_get_t(world, "state.runs", ecs_i32_t), 0);

    ecs_fini(world);
}

void ScriptAssign_resumes_on_assignment_by_task(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, {
        .code = SCRIPT_ASSIGN_STATE
            "async {\n"
            "  await next_frame()\n"
            "  await next_frame()\n"
            "  state.input = 5\n"
            "}\n"
    }) != 0);

    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_mut_var_get_t(world, "state.twice", ecs_i32_t), 10);
    test_int(ecs_mut_var_get_t(world, "state.runs", ecs_i32_t), 1);

    ecs_fini(world);
}

void ScriptAssign_deleted_script_stops_waiting(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t state = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "state_script" }),
        .code =
            "state {\n"
            "  export mut input: i32 = 1\n"
            "  export mut runs: i32 = 0\n"
            "}\n"
    });
    ecs_entity_t waiter = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "waiter_script" }),
        .code =
            "using flecs.script\n"
            "async {\n"
            "  while true {\n"
            "    await on.assign(state)\n"
            "    state.runs = state.runs + 1\n"
            "  }\n"
            "}\n"
    });
    test_assert(state != 0);
    test_assert(waiter != 0);
    ecs_progress(world, 0);

    ecs_delete(world, waiter);
    ecs_entity_t input = ecs_lookup(world, "state.input");
    *(int32_t*)ecs_mut_var_get(world, input).ptr = 3;
    ecs_mut_var_modified(world, input);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_mut_var_get_t(world, "state.runs", ecs_i32_t), 0);

    ecs_fini(world);
}

void ScriptAssign_no_scope_rejects(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, {
        .code =
            "using flecs.script\n"
            "state {\n"
            "  export mut caught: bool = false\n"
            "}\n"
            "async {\n"
            "  try {\n"
            "    await on.assign(0)\n"
            "  } catch {\n"
            "    state.caught = true\n"
            "  }\n"
            "}\n"
    }) != 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_bool(ecs_mut_var_get_t(world, "state.caught", ecs_bool_t), true);

    ecs_fini(world);
}
