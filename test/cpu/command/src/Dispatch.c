/**
 * @file Dispatch.c
 * @brief Tests for splitting command lines and converting their arguments.
 */

#include <box.h>
#include <bake_test.h>
#include <command.h>

/** Command that prints its arguments separated by |.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the arguments
 * @param result Receives the output
 */
static void Dispatch_echo(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i;
    for (i = 1; i < argc; i ++) {
        ecs_entity_t type = argv[i].type;
        if (i > 1) {
            ecs_strbuf_appendch(&buf, '|');
        }

        if (type == ecs_id(ecs_string_t)) {
            const char *str = *(char**)argv[i].ptr;
            ecs_strbuf_appendstr(&buf, str ? str : "(null)");
        } else if (type == ecs_id(ecs_i32_t)) {
            ecs_strbuf_append(&buf, "%d", *(ecs_i32_t*)argv[i].ptr);
        } else if (type == ecs_id(ecs_f64_t)) {
            ecs_strbuf_append(&buf, "%g", *(ecs_f64_t*)argv[i].ptr);
        } else if (type == ecs_id(ecs_bool_t)) {
            ecs_strbuf_appendstr(&buf, *(ecs_bool_t*)argv[i].ptr ? "true" : "false");
        } else {
            char *path = ecs_get_path(ctx->world, *(ecs_entity_t*)argv[i].ptr);
            ecs_strbuf_appendstr(&buf, path);
            ecs_os_free(path);
        }
    }

    *(char**)result->ptr = ecs_strbuf_get(&buf);
}

/** Command that always fails.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output
 */
static void Dispatch_fail(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    box_command_error(ctx, result, "bad");
}

void Dispatch_unknown_command(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "foo bar", &error);
    test_bool(error, true);
    test_str(out, "error: unknown command 'foo', see help");

    ecs_os_free(out);
    ecs_fini(world);
}

void Dispatch_string_int_float_bool(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "echo",
        .params = {
            { .name = "s", .type = ecs_id(ecs_string_t) },
            { .name = "n", .type = ecs_id(ecs_i32_t) },
            { .name = "f", .type = ecs_id(ecs_f64_t) },
            { .name = "b", .type = ecs_id(ecs_bool_t) }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "echo hello -42 1.5 true", &error);
    test_bool(error, false);
    test_str(out, "hello|-42|1.5|true");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "echo x 0 0 off", &error);
    test_str(out, "x|0|0|false");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_entity_argument(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "pick",
        .params = {{ .name = "e", .type = ecs_id(ecs_entity_t) }},
        .callback = Dispatch_echo
    });

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "child", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "pick parent.child", NULL);
    test_str(out, "parent.child");

    ecs_os_free(out);
    ecs_fini(world);
}

void Dispatch_entity_relative_to_cwd(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "pick",
        .params = {{ .name = "e", .type = ecs_id(ecs_entity_t) }},
        .callback = Dispatch_echo
    });

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "child", .parent = parent });

    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    ecs_set(world, s1, Terminal, { .cwd = "parent" });

    char *out = box_terminal_run(world, s1, "pick child", NULL);
    test_str(out, "parent.child");
    ecs_os_free(out);

    out = box_terminal_run(world, s2, "pick child", NULL);
    test_str(out, "error: e: no entity 'child'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_unknown_entity(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "pick",
        .params = {{ .name = "e", .type = ecs_id(ecs_entity_t) }},
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "pick nope", &error);
    test_bool(error, true);
    test_str(out, "error: e: no entity 'nope'");

    ecs_os_free(out);
    ecs_fini(world);
}

void Dispatch_optional_omitted(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "opt",
        .params = {
            { .name = "n", .type = ecs_id(ecs_i32_t), .optional = true },
            { .name = "s", .type = ecs_id(ecs_string_t), .optional = true }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "opt", NULL);
    test_str(out, "0|(null)");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "opt 3", NULL);
    test_str(out, "3|(null)");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "opt 3 x", NULL);
    test_str(out, "3|x");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_too_few_arguments(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "two",
        .params = {
            { .name = "a", .type = ecs_id(ecs_string_t) },
            { .name = "b", .type = ecs_id(ecs_string_t) }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "two x", &error);
    test_bool(error, true);
    test_str(out, "error: two takes 2 arguments, got 1");

    ecs_os_free(out);
    ecs_fini(world);
}

void Dispatch_too_many_arguments(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "opt",
        .params = {
            { .name = "a", .type = ecs_id(ecs_string_t) },
            { .name = "b", .type = ecs_id(ecs_string_t), .optional = true }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "opt x y z", NULL);
    test_str(out, "error: opt takes 1 to 2 arguments, got 3");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "commands 1 2", NULL);
    test_str(out, "error: commands takes 0 to 1 arguments, got 2");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_invalid_int(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "commands x", &error);
    test_bool(error, true);
    test_str(out, "error: count: invalid i32 'x'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "commands 2x", &error);
    test_str(out, "error: count: invalid i32 '2x'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_invalid_bool(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "flag",
        .params = {{ .name = "b", .type = ecs_id(ecs_bool_t) }},
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "flag maybe", NULL);
    test_str(out, "error: b: invalid bool 'maybe'");

    ecs_os_free(out);
    ecs_fini(world);
}

void Dispatch_quoted_argument(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "say",
        .params = {
            { .name = "a", .type = ecs_id(ecs_string_t) },
            { .name = "b", .type = ecs_id(ecs_string_t) }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "say \"a b\"  c", NULL);
    test_str(out, "a b|c");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "say \"\" c", NULL);
    test_str(out, "|c");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_quoted_escape(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "say",
        .params = {
            { .name = "a", .type = ecs_id(ecs_string_t) },
            { .name = "b", .type = ecs_id(ecs_string_t) }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "say \"a \\\"b\\\"\" c", NULL);
    test_str(out, "a \"b\"|c");

    ecs_os_free(out);
    ecs_fini(world);
}

void Dispatch_bracket_argument(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "say",
        .params = {
            { .name = "a", .type = ecs_id(ecs_string_t) },
            { .name = "b", .type = ecs_id(ecs_string_t) }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "say {x: 1, y: {2, 3}} (Rel, Tgt)", NULL);
    test_str(out, "{x: 1, y: {2, 3}}|(Rel, Tgt)");

    ecs_os_free(out);
    ecs_fini(world);
}

void Dispatch_rest_argument(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "write",
        .params = {
            { .name = "path", .type = ecs_id(ecs_string_t) },
            { .name = "code", .type = ecs_id(ecs_string_t), .rest = true }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "write a.flecs e {\n  Position: {1, 2}\n}  ", NULL);
    test_str(out, "a.flecs|e {\n  Position: {1, 2}\n}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "write a.flecs", NULL);
    test_str(out, "error: write takes 2 arguments, got 1");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_rest_single_quoted(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "write",
        .params = {
            { .name = "path", .type = ecs_id(ecs_string_t) },
            { .name = "code", .type = ecs_id(ecs_string_t), .rest = true }
        },
        .callback = Dispatch_echo
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "write a \"x y\"", NULL);
    test_str(out, "a|x y");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "write a \"x\" y", NULL);
    test_str(out, "a|\"x\" y");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_dotted_command(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cmd = box_command_register(world, &(box_command_desc_t){
        .name = "key.press",
        .brief = "Press a key",
        .params = {{ .name = "key", .type = ecs_id(ecs_string_t) }},
        .callback = Dispatch_echo
    });

    test_uint(ecs_lookup(world, "box.command.commands.key.press"), cmd);
    test_str(ecs_doc_get_brief(world, cmd), "Press a key");

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "key.press a", NULL);
    test_str(out, "a");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "key", NULL);
    test_str(out, "error: unknown command 'key', see help");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_error_result_logged(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "fail",
        .callback = Dispatch_fail,
        .quiet = true
    });

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "fail", &error);
    test_bool(error, true);
    test_str(out, "error: bad");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "commands", NULL);
    test_str(out, "$ fail\nerror: bad");
    ecs_os_free(out);

    ecs_fini(world);
}

void Dispatch_quiet_result_not_logged(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "loud",
        .params = {{ .name = "a", .type = ecs_id(ecs_string_t) }},
        .callback = Dispatch_echo
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "quiet",
        .params = {{ .name = "a", .type = ecs_id(ecs_string_t) }},
        .callback = Dispatch_echo,
        .quiet = true
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "quiet big", NULL);
    test_str(out, "big");
    ecs_os_free(out);

    ecs_os_free(box_terminal_run(world, s, "loud x", NULL));

    out = box_terminal_run(world, s, "commands", NULL);
    test_str(out, "$ quiet big\n$ loud x\nx");
    ecs_os_free(out);

    ecs_fini(world);
}
