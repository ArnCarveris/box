/**
 * @file Dispatch.c
 * @brief Tests for command line parsing, argument conversion and the help command.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

static void probe_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx;
    (void)argc;
    const char *text = *(char**)argv[1].ptr;
    ecs_i32_t n = *(ecs_i32_t*)argv[2].ptr;
    ecs_f64_t x = *(ecs_f64_t*)argv[3].ptr;
    *(char**)result->ptr = flecs_asprintf("%s|%d|%.2f", text ? text : "", n, x);
}

static void pick_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t e = *(ecs_entity_t*)argv[1].ptr;
    *(char**)result->ptr = ecs_get_path(ctx->world, e);
}

static void maybe_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx;
    (void)argc;
    *(char**)result->ptr = flecs_asprintf("n=%d", *(ecs_i32_t*)argv[1].ptr);
}

void Dispatch_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxCommand);

    box_command_param_t probe_params[] = {
        { .name = "text", .type = ecs_id(ecs_string_t) },
        { .name = "n", .type = ecs_id(ecs_i32_t) },
        { .name = "x", .type = ecs_id(ecs_f64_t) }
    };
    box_command_register(
        w, "probe", "Echo the arguments", probe_params, 3, probe_command);

    box_command_param_t pick_params[] = {
        { .name = "e", .type = ecs_id(ecs_entity_t) }
    };
    box_command_register(
        w, "pick", "Return the path of an entity", pick_params, 1, pick_command);

    box_command_param_t maybe_params[] = {
        { .name = "n", .type = ecs_id(ecs_i32_t), .optional = true }
    };
    box_command_register(
        w, "maybe", "Echo an optional number", maybe_params, 1, maybe_command);

    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Dispatch_teardown(void)
{
    ecs_fini(w);
}

void Dispatch_command_with_arguments(void)
{
    // command/terminal
    bool error = true;
    char *out = box_command_run(w, session, "probe hi 42 1.5", &error);
    test_bool(error, false);
    test_str(out, "hi|42|1.50");
    ecs_os_free(out);
}

void Dispatch_quoted_argument(void)
{
    // command/terminal
    char *out = box_command_run(w, session, "probe \"hello world\"  7 2", NULL);
    test_str(out, "hello world|7|2.00");
    ecs_os_free(out);
}

void Dispatch_unknown_command(void)
{
    // command/terminal
    bool error = false;
    char *out = box_command_run(w, session, "nope arg", &error);
    test_bool(error, true);
    test_str(out, "unknown command 'nope'");
    ecs_os_free(out);
}

void Dispatch_too_many_arguments(void)
{
    // command/terminal
    bool error = false;
    char *out = box_command_run(w, session, "probe a 1 2.0 extra", &error);
    test_bool(error, true);
    test_str(out, "command 'probe' takes 3 arguments, got 4");
    ecs_os_free(out);
}

void Dispatch_missing_argument(void)
{
    // command/terminal
    bool error = false;
    char *out = box_command_run(w, session, "probe a 1", &error);
    test_bool(error, true);
    test_str(out, "command 'probe' takes 3 arguments, got 2");
    ecs_os_free(out);
}

void Dispatch_invalid_i32_argument(void)
{
    // command/terminal
    bool error = false;
    char *out = box_command_run(w, session, "probe a x 2.0", &error);
    test_bool(error, true);
    test_str(out, "argument 'n': invalid i32 value 'x'");
    ecs_os_free(out);
}

void Dispatch_f64_argument(void)
{
    // command/terminal
    char *out = box_command_run(w, session, "probe a 0 3.25", NULL);
    test_str(out, "a|0|3.25");
    ecs_os_free(out);
}

void Dispatch_entity_argument(void)
{
    // command/terminal
    ecs_entity(w, { .name = "thing" });
    bool error = true;
    char *out = box_command_run(w, session, "pick thing", &error);
    test_bool(error, false);
    test_str(out, "thing");
    ecs_os_free(out);
}

void Dispatch_entity_relative_to_cwd(void)
{
    // command/terminal
    ecs_entity(w, { .name = "parent.child" });
    Terminal *term = ecs_ensure(w, session, Terminal);
    term->cwd = ecs_os_strdup("parent");
    ecs_modified(w, session, Terminal);
    char *out = box_command_run(w, session, "pick child", NULL);
    test_str(out, "parent.child");
    ecs_os_free(out);
}

void Dispatch_unknown_entity_argument(void)
{
    // command/terminal
    bool error = false;
    char *out = box_command_run(w, session, "pick nothing", &error);
    test_bool(error, true);
    test_str(out, "argument 'e': unknown entity 'nothing'");
    ecs_os_free(out);
}

void Dispatch_optional_argument_omitted(void)
{
    // command/terminal
    char *out = box_command_run(w, session, "maybe", NULL);
    test_str(out, "n=0");
    ecs_os_free(out);
    out = box_command_run(w, session, "maybe 7", NULL);
    test_str(out, "n=7");
    ecs_os_free(out);
}

void Dispatch_help_lists_commands(void)
{
    // command/terminal
    bool error = true;
    char *out = box_command_run(w, session, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "commands [count:i32] - Show the last") != NULL);
    test_assert(strstr(out, "help - List every command") != NULL);
    test_assert(strstr(out, "maybe [n:i32] - Echo an optional number") != NULL);
    test_assert(strstr(out, "pick <e:entity> - Return the path") != NULL);
    test_assert(strstr(out, "probe <text:string> <n:i32> <x:f64> - Echo") != NULL);
    ecs_os_free(out);
}
