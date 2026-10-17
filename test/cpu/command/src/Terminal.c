/**
 * @file Terminal.c
 * @brief Tests for terminal sessions, their folders and logs, and the HTTP endpoints.
 */

#include <box.h>
#include <bake_test.h>
#include <command.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void Terminal_new_creates_folder(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    test_assert(s != 0);
    test_str(ecs_get_name(world, s), "s1");
    test_assert(ecs_has(world, s, Terminal));

    const TerminalStatus *status = ecs_get(world, s, TerminalStatus);
    test_assert(status != NULL);
    char *expect = flecs_asprintf("/tmp/box/%d/s1", (int)getpid());
    test_str(status->dir, expect);

    struct stat st;
    test_int(stat(expect, &st), 0);
    test_assert(S_ISDIR(st.st_mode));

    char *resources = flecs_asprintf("%s/resources", expect);
    test_int(stat(resources, &st), 0);
    test_assert(S_ISDIR(st.st_mode));

    char *log = flecs_asprintf("%s/commands.log", expect);
    test_int(stat(log, &st), 0);
    test_assert(S_ISREG(st.st_mode));

    ecs_os_free(log);
    ecs_os_free(resources);
    ecs_os_free(expect);
    ecs_fini(world);
}

void Terminal_new_unique_handles(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    test_assert(s1 != s2);
    test_str(ecs_get_name(world, s1), "s1");
    test_str(ecs_get_name(world, s2), "s2");
    test_assert(strcmp(ecs_get(world, s1, TerminalStatus)->dir,
        ecs_get(world, s2, TerminalStatus)->dir) != 0);

    ecs_delete(world, s1);
    ecs_entity_t s3 = box_terminal_new(world);
    test_str(ecs_get_name(world, s3), "s3");

    ecs_fini(world);
}

void Terminal_lookup_handle(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s1 = box_terminal_new(world);
    test_uint(box_terminal_lookup(world, "s1"), s1);
    test_uint(box_terminal_lookup(world, "s2"), 0);
    test_uint(box_terminal_lookup(world, NULL), 0);
    test_uint(box_terminal_lookup(world, ""), 0);
    test_uint(box_terminal_lookup(world, "s1.x"), 0);

    ecs_fini(world);
}

void Terminal_close_removes_folder(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *dir = ecs_os_strdup(ecs_get(world, s, TerminalStatus)->dir);
    char *file = flecs_asprintf("%s/resources/a.txt", dir);
    FILE *f = fopen(file, "w");
    test_assert(f != NULL);
    fputs("a", f);
    fclose(f);

    ecs_delete(world, s);

    struct stat st;
    test_assert(stat(dir, &st) != 0);
    test_uint(box_terminal_lookup(world, "s1"), 0);

    ecs_os_free(file);
    ecs_os_free(dir);
    ecs_fini(world);
}

void Terminal_remove_terminal_removes_folder(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *dir = ecs_os_strdup(ecs_get(world, s, TerminalStatus)->dir);

    ecs_remove(world, s, Terminal);
    test_assert(ecs_is_alive(world, s));
    test_assert(!ecs_has(world, s, TerminalStatus));

    struct stat st;
    test_assert(stat(dir, &st) != 0);

    ecs_os_free(dir);
    ecs_fini(world);
}

void Terminal_script_terminal(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(ecs_script_run(world, "t",
        "using box.command\nagent {\n  Terminal: {}\n}\n", NULL), 0);

    ecs_entity_t agent = ecs_lookup(world, "agent");
    test_assert(agent != 0);
    const TerminalStatus *status = ecs_get(world, agent, TerminalStatus);
    test_assert(status != NULL);
    char *expect = flecs_asprintf("/tmp/box/%d/agent", (int)getpid());
    test_str(status->dir, expect);

    bool error = true;
    char *out = box_terminal_run(world, agent, "commands", &error);
    test_bool(error, false);
    test_str(out, "");

    ecs_os_free(out);
    ecs_os_free(expect);
    ecs_fini(world);
}

void Terminal_headless_folder_in_tmp_dir(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .headless = true, .frames = 1, .cpu_budget = 100 });
    const AppStatus *app = ecs_singleton_get(world, AppStatus);
    test_assert(app->tmp_dir != NULL);

    ecs_entity_t s = box_terminal_new(world);
    char *expect = flecs_asprintf("%s/s1", app->tmp_dir);
    test_str(ecs_get(world, s, TerminalStatus)->dir, expect);

    char *root = ecs_os_strdup(app->tmp_dir);
    ecs_fini(world);

    struct stat st;
    test_assert(stat(root, &st) != 0);

    ecs_os_free(root);
    ecs_os_free(expect);
}

void Terminal_root_removed_at_fini(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *root = flecs_asprintf("/tmp/box/%d", (int)getpid());
    struct stat st;
    test_assert(stat(root, &st) != 0);

    ecs_entity_t s = box_terminal_new(world);
    test_assert(s != 0);
    test_int(stat(root, &st), 0);

    ecs_fini(world);
    test_assert(stat(root, &st) != 0);

    ecs_os_free(root);
}

void Terminal_sessions_isolated(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s1, "nope", NULL));
    ecs_os_free(box_terminal_run(world, s1, "nope2", NULL));

    char *out = box_terminal_run(world, s2, "commands", NULL);
    test_str(out, "");
    ecs_os_free(out);

    out = box_terminal_run(world, s1, "commands", NULL);
    test_str(out,
        "$ nope\nerror: unknown command 'nope', see help\n"
        "$ nope2\nerror: unknown command 'nope2', see help");
    ecs_os_free(out);

    test_int(ecs_get(world, s1, TerminalStatus)->commands, 3);
    test_int(ecs_get(world, s2, TerminalStatus)->commands, 1);

    ecs_fini(world);
}

void Terminal_cwd_per_session(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    ecs_set(world, s1, Terminal, { .cwd = "box.app" });

    test_str(ecs_get(world, s1, Terminal)->cwd, "box.app");
    test_assert(ecs_get(world, s2, Terminal)->cwd == NULL);
    test_assert(ecs_get(world, s1, TerminalStatus)->dir != NULL);

    ecs_fini(world);
}

void Terminal_log_file(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "nope a", NULL));
    ecs_os_free(box_terminal_run(world, s, "commands", NULL));

    char *path = flecs_asprintf("%s/commands.log", ecs_get(world, s, TerminalStatus)->dir);
    FILE *f = fopen(path, "r");
    test_assert(f != NULL);
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    test_str(buf, "$ nope a\nerror: unknown command 'nope', see help\n$ commands\n");

    ecs_os_free(path);
    ecs_fini(world);
}

void Terminal_log_counts_commands(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    test_int(ecs_get(world, s, TerminalStatus)->commands, 0);
    ecs_os_free(box_terminal_run(world, s, "commands", NULL));
    ecs_os_free(box_terminal_run(world, s, "logs", NULL));
    test_int(ecs_get(world, s, TerminalStatus)->commands, 2);

    ecs_fini(world);
}

void Terminal_empty_line_not_logged(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "   ", &error);
    test_str(out, "");
    test_bool(error, false);
    ecs_os_free(out);

    out = box_terminal_run(world, s, NULL, &error);
    test_str(out, "");
    ecs_os_free(out);

    test_int(ecs_get(world, s, TerminalStatus)->commands, 0);

    ecs_fini(world);
}

void Terminal_commands_all(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "a", NULL));
    ecs_os_free(box_terminal_run(world, s, "b", NULL));
    ecs_os_free(box_terminal_run(world, s, "c", NULL));

    bool error = true;
    char *out = box_terminal_run(world, s, "commands", &error);
    test_bool(error, false);
    test_str(out,
        "$ a\nerror: unknown command 'a', see help\n"
        "$ b\nerror: unknown command 'b', see help\n"
        "$ c\nerror: unknown command 'c', see help");

    ecs_os_free(out);
    ecs_fini(world);
}

void Terminal_commands_count(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "a", NULL));
    ecs_os_free(box_terminal_run(world, s, "b", NULL));
    ecs_os_free(box_terminal_run(world, s, "c", NULL));

    char *out = box_terminal_run(world, s, "commands 2", NULL);
    test_str(out, "$ b\nerror: unknown command 'b', see help\n"
        "$ c\nerror: unknown command 'c', see help");

    ecs_os_free(out);
    ecs_fini(world);
}

void Terminal_commands_count_above_total(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "a", NULL));

    char *out = box_terminal_run(world, s, "commands 10", NULL);
    test_str(out, "$ a\nerror: unknown command 'a', see help");

    ecs_os_free(out);
    ecs_fini(world);
}

void Terminal_commands_not_nested(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "a", NULL));
    ecs_os_free(box_terminal_run(world, s, "commands", NULL));

    char *out = box_terminal_run(world, s, "commands", NULL);
    test_str(out, "$ a\nerror: unknown command 'a', see help\n$ commands");

    ecs_os_free(out);
    ecs_fini(world);
}

void Terminal_commands_not_a_session(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    bool error = false;
    char *out = box_terminal_run(world, e, "commands", &error);
    test_bool(error, true);
    test_str(out, "error: not a terminal session");

    ecs_os_free(out);
    ecs_fini(world);
}

void Terminal_commands_is_script_function(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "a", NULL));

    ecs_entity_t f = ecs_lookup(world, "box.command.commands.commands");
    test_assert(f != 0);
    test_assert(ecs_has(world, f, EcsScriptFunction));

    ecs_i32_t count = 0;
    ecs_value_t argv[] = {
        { .type = ecs_id(ecs_entity_t), .ptr = &s },
        { .type = ecs_id(ecs_i32_t), .ptr = &count }
    };
    char *out = NULL;
    ecs_value_t result = { .type = ecs_id(ecs_string_t), .ptr = &out };
    test_int(ecs_function_call(world, f, 2, argv, &result), 0);
    test_str(out, "$ a\nerror: unknown command 'a', see help");

    ecs_os_free(out);
    ecs_fini(world);
}

void Terminal_http_new(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/terminal/new", NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "s1");
    test_str(reply.content_type, "text/plain");
    ecs_os_free(body);

    test_assert(box_terminal_lookup(world, "s1") != 0);

    reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "POST", "/terminal/new", NULL, &reply), 200);
    body = ecs_strbuf_get(&reply.body);
    test_str(body, "s2");
    ecs_os_free(body);

    ecs_fini(world);
}

void Terminal_http_exec_get(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "a", NULL));
    ecs_os_free(box_terminal_run(world, s, "b", NULL));

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/terminal/exec?session=s1&cmd=commands%201", NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "$ b\nerror: unknown command 'b', see help");
    ecs_os_free(body);

    test_int(ecs_get(world, s, TerminalStatus)->commands, 3);

    ecs_fini(world);
}

void Terminal_http_exec_post(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "a", NULL));

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "POST",
        "/terminal/exec?session=s1", "commands 1", &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "$ a\nerror: unknown command 'a', see help");
    ecs_os_free(body);

    ecs_fini(world);
}

void Terminal_http_exec_error(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_terminal_new(world);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/terminal/exec?session=s1&cmd=nope", NULL, &reply), 400);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "error: unknown command 'nope', see help");
    ecs_os_free(body);

    ecs_fini(world);
}

void Terminal_http_exec_missing_cmd(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/terminal/exec?session=s1", NULL, &reply), 400);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "error: missing cmd");
    ecs_os_free(body);

    test_int(ecs_get(world, s, TerminalStatus)->commands, 0);

    ecs_fini(world);
}

void Terminal_http_exec_unknown_session(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/terminal/exec?session=s9&cmd=commands", NULL, &reply), 404);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "error: unknown session");
    ecs_os_free(body);

    reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/terminal/exec?cmd=commands", NULL, &reply), 404);
    ecs_strbuf_reset(&reply.body);

    ecs_fini(world);
}

void Terminal_http_close(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *dir = ecs_os_strdup(ecs_get(world, s, TerminalStatus)->dir);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/terminal/close?session=s1", NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_assert(body == NULL || !body[0]);
    ecs_os_free(body);

    test_assert(!ecs_is_alive(world, s));
    struct stat st;
    test_assert(stat(dir, &st) != 0);

    reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/terminal/exec?session=s1&cmd=commands", NULL, &reply), 404);
    ecs_strbuf_reset(&reply.body);

    ecs_os_free(dir);
    ecs_fini(world);
}

void Terminal_http_close_unknown_session(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/terminal/close?session=s2", NULL, &reply), 404);
    ecs_strbuf_reset(&reply.body);
    test_assert(ecs_is_alive(world, s));

    ecs_fini(world);
}

void Terminal_http_rest_forward(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET",
        "/component/box/app/App?component=box.app.App", NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_assert(body != NULL);
    test_assert(strstr(body, "\"fps\":60") != NULL);
    ecs_os_free(body);

    reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/nope", NULL, &reply), 404);
    ecs_strbuf_reset(&reply.body);

    ecs_fini(world);
}

void Terminal_resource_text(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *path = flecs_asprintf("%s/resources/a.txt", ecs_get(world, s, TerminalStatus)->dir);
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("hello", f);
    fclose(f);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/a.txt?session=s1", NULL, &reply),
        200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "hello");
    test_str(reply.content_type, "text/plain");

    ecs_os_free(body);
    ecs_os_free(path);
    ecs_fini(world);
}

void Terminal_resource_png(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *path = flecs_asprintf("%s/resources/a.png", ecs_get(world, s, TerminalStatus)->dir);
    FILE *f = fopen(path, "wb");
    test_assert(f != NULL);
    const unsigned char data[] = { 0x89, 'P', 'N', 'G', 0, 1, 2 };
    fwrite(data, 1, sizeof(data), f);
    fclose(f);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/a.png?session=s1", NULL, &reply),
        200);
    test_str(reply.content_type, "image/png");
    test_int(ecs_strbuf_written(&reply.body), (int32_t)sizeof(data));
    char *body = ecs_strbuf_get(&reply.body);
    test_assert(!memcmp(body, data, sizeof(data)));

    ecs_os_free(body);
    ecs_os_free(path);
    ecs_fini(world);
}

void Terminal_resource_subfolder(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *dir = flecs_asprintf("%s/resources/sub", ecs_get(world, s, TerminalStatus)->dir);
    test_int(mkdir(dir, 0777), 0);
    char *path = flecs_asprintf("%s/b.json", dir);
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("{}", f);
    fclose(f);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/sub/b.json?session=s1", NULL,
        &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "{}");
    test_str(reply.content_type, "application/json");

    ecs_os_free(body);
    ecs_os_free(path);
    ecs_os_free(dir);
    ecs_fini(world);
}

void Terminal_resource_missing(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_terminal_new(world);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/x.png?session=s1", NULL, &reply),
        404);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "error: no resource");
    ecs_os_free(body);

    reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/?session=s1", NULL, &reply), 404);
    ecs_strbuf_reset(&reply.body);

    ecs_fini(world);
}

void Terminal_resource_folder(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *dir = flecs_asprintf("%s/resources/sub", ecs_get(world, s, TerminalStatus)->dir);
    test_int(mkdir(dir, 0777), 0);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/sub?session=s1", NULL, &reply),
        404);
    ecs_strbuf_reset(&reply.body);

    ecs_os_free(dir);
    ecs_fini(world);
}

void Terminal_resource_parent_path(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "a", NULL));

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/../commands.log?session=s1", NULL,
        &reply), 403);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "error: outside resources");
    ecs_os_free(body);

    reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/x/../../commands.log?session=s1",
        NULL, &reply), 403);
    ecs_strbuf_reset(&reply.body);

    reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/..?session=s1", NULL, &reply), 403);
    ecs_strbuf_reset(&reply.body);

    ecs_fini(world);
}

void Terminal_resource_absolute_path(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_terminal_new(world);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource//etc/hosts?session=s1", NULL,
        &reply), 403);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "error: outside resources");
    ecs_os_free(body);

    ecs_fini(world);
}

void Terminal_resource_symlink_outside(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    const char *dir = ecs_get(world, s, TerminalStatus)->dir;
    char *target = flecs_asprintf("%s/commands.log", dir);
    char *link = flecs_asprintf("%s/resources/log.txt", dir);
    test_int(symlink(target, link), 0);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/log.txt?session=s1", NULL, &reply),
        403);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "error: outside resources");
    ecs_os_free(body);

    ecs_os_free(link);
    ecs_os_free(target);
    ecs_fini(world);
}

void Terminal_resource_other_session(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s1 = box_terminal_new(world);
    box_terminal_new(world);
    char *path = flecs_asprintf("%s/resources/a.txt", ecs_get(world, s1, TerminalStatus)->dir);
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("secret", f);
    fclose(f);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/a.txt?session=s2", NULL, &reply),
        404);
    ecs_strbuf_reset(&reply.body);

    reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/../../s1/resources/a.txt?session=s2",
        NULL, &reply), 403);
    ecs_strbuf_reset(&reply.body);

    ecs_os_free(path);
    ecs_fini(world);
}

void Terminal_resource_no_session(void) {
    // command/terminal
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *path = flecs_asprintf("%s/resources/a.txt", ecs_get(world, s, TerminalStatus)->dir);
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("a", f);
    fclose(f);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/resource/a.txt", NULL, &reply), 404);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "error: unknown session");
    ecs_os_free(body);

    ecs_os_free(path);
    ecs_fini(world);
}

void Terminal_import_without_app(void) {
    // command/import_app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);

    ecs_entity_t s = box_terminal_new(world);
    test_assert(s != 0);
    test_assert(ecs_has(world, s, Terminal));

    ecs_fini(world);
}

void Terminal_root_private(void) {
    // hardening/tmp_dir
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    test_assert(s != 0);

    char *root = flecs_asprintf("/tmp/box/%d", (int)getpid());
    struct stat st;
    test_int(lstat(root, &st), 0);
    test_assert(S_ISDIR(st.st_mode));
    test_int(st.st_mode & 07777, 0700);
    test_int(st.st_uid, geteuid());

    ecs_fini(world);
    ecs_os_free(root);
}

void Terminal_root_symlink_refused(void) {
    // hardening/tmp_dir
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *root = flecs_asprintf("/tmp/box/%d", (int)getpid());
    char *target = flecs_asprintf("/tmp/box_command_target_%d", (int)getpid());
    char *marker = flecs_asprintf("%s/marker", target);
    mkdir("/tmp/box", 0700);
    test_int(mkdir(target, 0700), 0);
    FILE *f = fopen(marker, "w");
    test_assert(f != NULL);
    fclose(f);
    test_int(symlink(target, root), 0);

    ecs_log_set_level(-4);
    test_assert(box_terminal_new(world) == 0);

    ecs_fini(world);

    struct stat st;
    test_int(lstat(root, &st), 0);
    test_assert(S_ISLNK(st.st_mode));
    test_int(stat(marker, &st), 0);

    unlink(root);
    unlink(marker);
    rmdir(target);
    ecs_os_free(marker);
    ecs_os_free(target);
    ecs_os_free(root);
}

void Terminal_name_not_a_path(void) {
    // hardening/tmp_dir
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *root = flecs_asprintf("/tmp/box/%d", (int)getpid());
    ecs_entity_t e = ecs_new(world);
    ecs_set_name(world, e, "..");
    ecs_add(world, e, Terminal);

    const TerminalStatus *status = ecs_get(world, e, TerminalStatus);
    test_assert(status != NULL);
    test_assert(strncmp(status->dir, root, strlen(root)) == 0);
    test_assert(status->dir[strlen(root)] == '/');
    test_assert(strstr(status->dir + strlen(root), "..") == NULL);

    ecs_fini(world);

    struct stat st;
    test_int(stat("/tmp/box", &st), 0);
    ecs_os_free(root);
}

void Terminal_name_with_slash(void) {
    // hardening/tmp_dir
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *root = flecs_asprintf("/tmp/box/%d", (int)getpid());
    ecs_entity_t e = ecs_new(world);
    ecs_set_name(world, e, "a/b");
    ecs_add(world, e, Terminal);

    const TerminalStatus *status = ecs_get(world, e, TerminalStatus);
    test_assert(status != NULL);
    test_assert(strncmp(status->dir, root, strlen(root)) == 0);
    test_assert(status->dir[strlen(root)] == '/');
    test_assert(strchr(status->dir + strlen(root) + 1, '/') == NULL);

    struct stat st;
    test_int(lstat(status->dir, &st), 0);
    test_assert(S_ISDIR(st.st_mode));

    ecs_fini(world);
    ecs_os_free(root);
}

void Terminal_headless_root_shared(void) {
    // hardening/tmp_dir
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    test_assert(s != 0);
    char *root = flecs_asprintf("/tmp/box/%d", (int)getpid());

    ecs_singleton_set(world, App, { .headless = true, .frames = 1, .cpu_budget = 100 });
    test_str(ecs_singleton_get(world, AppStatus)->tmp_dir, root);

    ecs_entity_t t = box_terminal_new(world);
    test_assert(t != 0);
    const TerminalStatus *status = ecs_get(world, t, TerminalStatus);
    test_assert(status != NULL);
    test_assert(strncmp(status->dir, root, strlen(root)) == 0);

    ecs_fini(world);

    struct stat st;
    test_assert(lstat(root, &st) != 0);
    ecs_os_free(root);
}

void Terminal_host_removed(void) {
    // hardening/session_isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_remove(world, ecs_id(TerminalHost), TerminalHost);
    ecs_log_set_level(-4);

    bool error = false;
    char *out = box_terminal_run(world, s, "commands", &error);
    test_bool(error, true);
    test_str(out, "error: box.command is not imported");
    ecs_os_free(out);

    test_assert(box_terminal_new(world) == 0);
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/terminal/new", NULL, &reply), 503);
    ecs_os_free(ecs_strbuf_get(&reply.body));
    ecs_progress(world, 0);

    ecs_fini(world);
}
