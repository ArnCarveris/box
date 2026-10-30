/**
 * @file Shell.c
 * @brief Tests for the box commands in the flecs script shell and box_command_run.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void Shell_box_scope_first(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const EcsScriptShell *shell = ecs_singleton_get(world, EcsScriptShell);
    test_assert(shell != NULL);
    test_assert(ecs_vec_count(&shell->command_paths) > 1);
    test_uint(ecs_vec_first_t(&shell->command_paths, ecs_entity_t)[0],
        ecs_lookup(world, BOX_COMMAND_SCOPE));
    test_assert(ecs_lookup(world, "flecs.script.shell.commands.core") != 0);

    ecs_fini(world);
}

void Shell_commands_are_script_functions(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *names[] = {
        "key_press", "key_down", "key_up", "key_repeat", "key_type", "mouse_move",
        "mouse_sweep", "mouse_release", "mouse_button", "mouse_click", "mouse_drag",
        "mouse_scroll", "action_press", "action_down", "action_up", "action_drag",
        "action_list", "input_reset", "input_focus", "input_wait", "clipboard_set",
        "clipboard_get", "capture", "render", "fps", "frames", "edit", "logs"
    };

    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(names) / sizeof(names[0])); i ++) {
        ecs_entity_t f = ecs_lookup_child(world, ecs_lookup(world, BOX_COMMAND_SCOPE), names[i]);
        test_assert(f != 0);
        test_assert(ecs_has(world, f, EcsScriptFunction));
        test_assert(ecs_doc_get_brief(world, f) != NULL);
    }

    ecs_fini(world);
}

void Shell_box_command_overrides_builtin(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "key_down A", &error);
    test_bool(error, false);
    test_str(out, "1");
    ecs_os_free(out);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyA].down, true);

    ecs_fini(world);
}

void Shell_builtin_commands(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "parent.a" });
    ecs_entity(world, { .name = "parent.b" });

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "cd parent", &error);
    test_bool(error, false);
    test_str(out, "parent");
    ecs_os_free(out);
    test_uint(ecs_get(world, s, EcsCommandSession)->scope, parent);

    out = box_command_run(world, s, "ls", &error);
    test_bool(error, false);
    test_str(out, "parent.a\nparent.b");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_string_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "clipboard_set \"hello world\"", NULL));

    bool error = true;
    char *out = box_command_run(world, s, "clipboard_get", &error);
    test_bool(error, false);
    test_str(out, "hello world");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_entity_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "parent.child" });

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, s, "pwd", NULL);
    test_str(out, "#0");
    ecs_os_free(out);

    ecs_os_free(box_command_run(world, s, "cd parent.child", NULL));
    out = box_command_run(world, s, "pwd", NULL);
    test_str(out, "parent.child");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_value_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_set(world, e, Position, {1, 2, 3});

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "get e box.transform.Position", &error);
    test_bool(error, false);
    test_str(out, "{\"Position\":{\"x\":1, \"y\":2, \"z\":3}}");
    ecs_os_free(out);

    out = box_command_run(world, s, "set e box.transform.Position {y: 5}", &error);
    test_bool(error, false);
    ecs_os_free(out);

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 5);
    test_flt(p->z, 3);

    ecs_fini(world);
}

void Shell_run_no_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "clipboard_set text", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_unknown_command(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "nope", &error);
    test_bool(error, true);
    test_str(out, BOX_COMMAND_ERROR "unknown command 'nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_command_error(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "key_press Nope", &error);
    test_bool(error, true);
    test_str(out, BOX_COMMAND_ERROR "unknown key 'Nope'");
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Shell_run_missing_argument(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "key_press", &error);
    test_bool(error, true);
    test_assert(strstr(out, BOX_COMMAND_ERROR "missing argument 'key'") == out);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_not_a_session(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    bool error = false;
    char *out = box_command_run(world, e, "input_wait", &error);
    test_bool(error, true);
    test_assert(strstr(out, "is not a command session") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_format_vector(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t vec = ecs_vector(world, { .type = ecs_id(ecs_i32_t) });
    ecs_vec_t v = {0};
    ecs_vec_append_t(NULL, &v, int32_t)[0] = 10;
    ecs_vec_append_t(NULL, &v, int32_t)[0] = 20;

    char *out = box_command_format(world, &(ecs_value_t){ .type = vec, .ptr = &v });
    test_str(out, "10\n20");
    ecs_os_free(out);

    out = box_command_format(world, &(ecs_value_t){0});
    test_str(out, "");
    ecs_os_free(out);

    ecs_vec_fini_t(NULL, &v, int32_t);
    ecs_fini(world);
}

void Shell_run_text_unchanged(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity(world, { .name = "things.alpha.leaf" });
    ecs_entity(world, { .name = "things.beta" });
    ecs_entity_t cube = ecs_entity(world, { .name = "cube" });
    ecs_set(world, cube, Position, {1, 2, 3});

    // The console colors its own output, sessions keep the plain text: full paths, JSON for
    // structured results, help without groups and errors without color codes
    ecs_os_free(box_console_run(world, "tree things", NULL));
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "ls things", &error);
    test_bool(error, false);
    test_str(out, "things.alpha\nthings.beta");
    ecs_os_free(out);

    out = box_command_run(world, s, "tree things", &error);
    test_bool(error, false);
    test_str(out,
        "{\"entity\":\"things.alpha\", \"depth\":0}\n"
        "{\"entity\":\"things.alpha.leaf\", \"depth\":1}\n"
        "{\"entity\":\"things.beta\", \"depth\":0}");
    ecs_os_free(out);

    out = box_command_run(world, s, "get cube box.transform.Position", &error);
    test_bool(error, false);
    test_str(out, "{\"Position\":{\"x\":1, \"y\":2, \"z\":3}}");
    ecs_os_free(out);

    out = box_command_run(world, s, "help ls", &error);
    test_bool(error, false);
    test_str(out, "ls [scope]  Direct children\n"
        "    scope: Scope to list (default: session scope)");
    ecs_os_free(out);

    out = box_command_run(world, s, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\n\n") == NULL);
    test_assert(strstr(out, "console only") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "key_press Nope", &error);
    test_bool(error, true);
    test_str(out, BOX_COMMAND_ERROR "unknown key 'Nope'");
    ecs_os_free(out);

    // The session of the console too
    out = box_command_run(world, ecs_lookup(world, BOX_CONSOLE_SESSION), "ls things", &error);
    test_bool(error, false);
    test_str(out, "things.alpha\nthings.beta");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_help_lists_box_commands(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "key_press <key> [dur]  Tap a key, held for dur frames") != NULL);
    test_assert(strstr(out, "capture [camera]  Write a PNG") != NULL);
    test_assert(strstr(out, "\nls [scope]  ") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "help edit", &error);
    test_bool(error, false);
    test_str(out, "edit [state]  Open or close the editor, toggle without a state\n"
        "    state: on or off (default: toggle)");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_history(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "input_wait", NULL));
    ecs_os_free(box_command_run(world, s, "nope", NULL));

    const EcsCommandSession *cs = ecs_get(world, s, EcsCommandSession);
    test_int(ecs_vec_count(&cs->history), 2);
    test_str(ecs_vec_get_t(&cs->history, char*, 0)[0], "input_wait");
    test_str(ecs_vec_get_t(&cs->history, char*, 1)[0], "nope");

    ecs_fini(world);
}

void Shell_script_call(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "test", "const pending = "
        "box.command.commands.key_press(\"W\", 3)\n", NULL) == 0);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyW].down, true);

    ecs_fini(world);
}

void Shell_rest_endpoint(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_http_server_t *srv = ecs_rest_server_init(world, NULL);
    test_assert(srv != NULL);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "PUT", "/shell/session", NULL, &reply), 0);
    test_int(reply.code, 200);
    char *body = ecs_strbuf_get(&reply.body);
    unsigned long long session = 0;
    test_int(sscanf(body, "{\"session\":%llu}", &session), 1);
    ecs_os_free(body);

    char *path = flecs_asprintf("/shell/session/%llu", session);
    reply = (ecs_http_reply_t)ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "PUT", path, "key_press W 2", &reply), 0);
    test_int(reply.code, 200);
    body = ecs_strbuf_get(&reply.body);
    test_str(body, "{\"type\":\"flecs.meta.i32\", \"result\":3, \"errors\":[]}");
    ecs_os_free(body);

    reply = (ecs_http_reply_t)ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "PUT", path, "key_press Nope", &reply), 0);
    test_int(reply.code, 200);
    body = ecs_strbuf_get(&reply.body);
    test_str(body, "{\"type\":\"flecs.meta.i32\", \"result\":0, "
        "\"errors\":[\"unknown key 'Nope'\"]}");
    ecs_os_free(body);

    ecs_os_free(path);
    ecs_rest_server_fini(srv);
    ecs_fini(world);
}

void Shell_quit(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    test_bool(ecs_progress(world, 0), true);

    bool error = true;
    char *out = box_command_run(world, s, "quit", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    test_bool(ecs_should_quit(world), true);
    test_bool(ecs_progress(world, 0), false);

    ecs_fini(world);
}

void Shell_console_session(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });

    bool error = true;
    char *out = box_console_run(world, "cd parent", &error);
    test_bool(error, false);
    test_str(out, "parent");
    ecs_os_free(out);

    ecs_entity_t session = ecs_lookup(world, BOX_CONSOLE_SESSION);
    test_assert(session != 0);
    test_assert(ecs_has(world, session, EcsCommandSession));
    test_uint(ecs_get(world, session, EcsCommandSession)->scope, parent);

    out = box_console_run(world, "pwd", &error);
    test_bool(error, false);
    test_str(out, "parent");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_resource_file(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    test_assert(box_command_resource_file(world, s, "1") == NULL);

    ecs_shell_resource_t r = {0};
    test_int(ecs_shell_resource_create(world, s, "text/plain", "hello", 5, &r), 0);
    test_str(r.name, "1");

    char *file = box_command_resource_file(world, s, r.name);
    char *expect = flecs_asprintf("%s/1", ecs_get(world, s, EcsCommandSession)->resource_path);
    test_str(file, expect);
    FILE *f = fopen(file, "rb");
    test_assert(f != NULL);
    char buf[8] = {0};
    test_int((int)fread(buf, 1, sizeof(buf), f), 5);
    fclose(f);
    test_str(buf, "hello");

    ecs_delete(world, s);
    test_assert(access(file, F_OK) != 0);

    ecs_os_free(expect);
    ecs_os_free(file);
    ecs_os_free(r.name);
    ecs_os_free(r.content_type);
    ecs_fini(world);
}
