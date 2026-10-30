/**
 * @file Logs.c
 * @brief Tests for the log capture and the logs command.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

void Logs_captures_error(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_err("boom %d", 1);

    bool error = true;
    char *out = box_command_run(world, s, "logs error 1", &error);
    test_bool(error, false);
    test_str(out, "error: boom 1");

    ecs_os_free(out);
    ecs_fini(world);
}

void Logs_level_filter(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_warn("w1");
    ecs_err("e1");
    ecs_warn("w2");

    char *out = box_command_run(world, s, "logs error", NULL);
    test_assert(strstr(out, "error: e1") != NULL);
    test_assert(strstr(out, "w1") == NULL);
    test_assert(strstr(out, "w2") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "logs warning 3", NULL);
    test_str(out, "warning: w1\nerror: e1\nwarning: w2");
    ecs_os_free(out);

    ecs_fini(world);
}

void Logs_count(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_err("e1");
    ecs_warn("w1");
    ecs_err("e2");
    ecs_err("e3");

    char *out = box_command_run(world, s, "logs error 2", NULL);
    test_str(out, "error: e2\nerror: e3");

    ecs_os_free(out);
    ecs_fini(world);
}

void Logs_default_all_levels(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_log_set_level(0);
    ecs_trace("t1");
    ecs_log_set_level(-1);
    ecs_warn("w1");

    char *out = box_command_run(world, s, "logs", NULL);
    const char *t1 = strstr(out, "info: t1");
    const char *w1 = strstr(out, "warning: w1");
    test_assert(t1 != NULL);
    test_assert(w1 != NULL);
    test_assert(t1 < w1);
    ecs_os_free(out);

    out = box_command_run(world, s, "logs \"\" 1", NULL);
    test_str(out, "warning: w1");
    ecs_os_free(out);

    ecs_fini(world);
}

void Logs_numeric_level(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_warn("w1");
    ecs_err("e1");

    char *out = box_command_run(world, s, "logs -3 5", NULL);
    test_str(out, "error: e1");

    ecs_os_free(out);
    ecs_fini(world);
}

void Logs_unknown_level(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "logs loud", &error);
    test_bool(error, true);
    test_str(out, "error: unknown log level 'loud'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Logs_no_color_codes(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_log_enable_colors(true);
    ecs_err("#[red]red#[reset] text");

    char *out = box_command_run(world, s, "logs error 1", NULL);
    test_str(out, "error: red text");

    ecs_os_free(out);
    ecs_fini(world);
}

void Logs_empty_is_string(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_warn("w1");

    ecs_shell_result_t r = ecs_shell_run(world, s, "logs error");
    test_int(ecs_vec_count(&r.errors), 0);
    test_assert(r.result.type == ecs_id(ecs_string_t));
    test_assert(r.result.ptr != NULL);
    test_str(*(char**)r.result.ptr, "");

    ecs_shell_result_fini(world, &r);
    ecs_fini(world);
}
