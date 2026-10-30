/**
 * @file Limits.c
 * @brief Tests for the bounds of command arguments.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

void Limits_f64_not_finite(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "mouse_scroll 0 1e999", &error);
    test_bool(error, true);
    test_str(out, "error: dy: inf is not a finite number");
    ecs_os_free(out);

    out = box_command_run(world, s, "mouse_scroll -1e999 0", &error);
    test_bool(error, true);
    test_str(out, "error: dx: -inf is not a finite number");
    ecs_os_free(out);

    out = box_command_run(world, s, "mouse_scroll inf 0", &error);
    test_bool(error, true);
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Limits_sweep_steps_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *line = flecs_asprintf("mouse_sweep [0, 0] [10, 10] %d", BOX_COMMAND_STEPS_MAX + 1);
    char *expect = flecs_asprintf("error: steps: %d is out of range, 0 to %d",
        BOX_COMMAND_STEPS_MAX + 1, BOX_COMMAND_STEPS_MAX);
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);
    ecs_os_free(expect);
    ecs_os_free(line);

    out = box_command_run(world, s, "mouse_sweep [0, 0] [10, 10] 2000000000", &error);
    test_bool(error, true);
    ecs_os_free(out);

    line = flecs_asprintf("mouse_sweep [0, 0] [10, 10] %d", BOX_COMMAND_STEPS_MAX);
    out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    ecs_os_free(out);
    ecs_os_free(line);

    ecs_fini(world);
}

void Limits_drag_steps_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "mouse_drag left [0, 0] [1, 1] 2000000000", &error);
    test_bool(error, true);
    char *expect = flecs_asprintf("error: steps: 2000000000 is out of range, 0 to %d",
        BOX_COMMAND_STEPS_MAX);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(out);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyMouseLeft].down, false);

    ecs_fini(world);
}

void Limits_action_drag_steps_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_entity(world, { .name = "Jump" });
    ecs_set(world, jump, Action, { 0 });

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "action_drag Jump [0, 0] [1, 1] 2000000000", &error);
    test_bool(error, true);
    char *expect = flecs_asprintf("error: steps: 2000000000 is out of range, 0 to %d",
        BOX_COMMAND_STEPS_MAX);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_dur_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *expect = flecs_asprintf("error: dur: 2000000000 is out of range, 0 to %d",
        BOX_COMMAND_FRAMES_MAX);
    char *out = box_command_run(world, s, "key_press A 2000000000", &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);

    out = box_command_run(world, s, "mouse_sweep [0, 0] [1, 1] 10 2000000000", &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);
    ecs_os_free(expect);

    out = box_command_run(world, s, "key_press A -1", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_fini(world);
}
