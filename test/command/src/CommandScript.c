/**
 * @file CommandScript.c
 * @brief Tests for the command file that a terminal session runs when it is set.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;

void CommandScript_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxCommand);
}

void CommandScript_teardown(void)
{
    ecs_fini(w);
}

void CommandScript_runs_lines_in_order(void)
{
    // command/integration
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    char *path = flecs_asprintf(
        "%s/run.cmd", ecs_singleton_get(w, App)->process_dir);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fprintf(file, "touch alpha\ntouch alpha.beta\nls alpha\n");
    fclose(file);

    CommandScript *script = ecs_ensure(w, t, CommandScript);
    script->path = ecs_os_strdup(path);
    ecs_modified(w, t, CommandScript);
    ecs_os_free(path);

    test_assert(ecs_lookup(w, "alpha") != 0);
    test_assert(ecs_lookup(w, "alpha.beta") != 0);

    const TerminalSession *s = ecs_get(w, t, TerminalSession);
    test_assert(s != NULL);
    test_int(s->count, 3);
    test_str(s->ring[0].line, "touch alpha");
    test_str(s->ring[0].result, "alpha");
    test_str(s->ring[1].line, "touch alpha.beta");
    test_str(s->ring[2].line, "ls alpha");
    test_str(s->ring[2].result, "beta\n");

    char *log = flecs_asprintf("%s/commands.log", ecs_get(w, t, Terminal)->session_dir);
    file = fopen(log, "r");
    test_assert(file != NULL);
    char buffer[4096];
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[size] = '\0';
    test_str(buffer, "$ touch alpha\nalpha\n$ touch alpha.beta\nalpha.beta\n$ ls alpha\nbeta\n\n");
    ecs_os_free(log);
}

void CommandScript_skips_comments_and_blanks(void)
{
    // command/integration
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    char *path = flecs_asprintf(
        "%s/run.cmd", ecs_singleton_get(w, App)->process_dir);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fprintf(file, "# create it\n\ntouch alpha\n   \n  # done\ntouch beta\n");
    fclose(file);

    CommandScript *script = ecs_ensure(w, t, CommandScript);
    script->path = ecs_os_strdup(path);
    ecs_modified(w, t, CommandScript);
    ecs_os_free(path);

    const TerminalSession *s = ecs_get(w, t, TerminalSession);
    test_assert(s != NULL);
    test_int(s->count, 2);
    test_str(s->ring[0].line, "touch alpha");
    test_str(s->ring[1].line, "touch beta");
}

void CommandScript_missing_file_is_error(void)
{
    // command/integration
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    CommandScript *script = ecs_ensure(w, t, CommandScript);
    script->path = ecs_os_strdup("no_such_folder/missing.cmd");
    int prev = ecs_log_set_level(-4);
    ecs_modified(w, t, CommandScript);
    ecs_log_set_level(prev);

    const TerminalOutput *out = ecs_get(w, t, TerminalOutput);
    test_assert(out != NULL);
    test_bool(out->error, true);
    test_assert(strstr(out->result, "no_such_folder/missing.cmd") != NULL);

    const TerminalSession *s = ecs_get(w, t, TerminalSession);
    test_assert(s != NULL);
    test_int(s->count, 0);
}

void CommandScript_quoted_argument_spans_lines(void)
{
    // command/integration
    ecs_struct(w, {
        .entity = ecs_entity(w, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_i32_t) },
            { .name = "y", .type = ecs_id(ecs_i32_t) }
        }
    });

    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    char *path = flecs_asprintf(
        "%s/run.cmd", ecs_singleton_get(w, App)->process_dir);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fprintf(file, "touch alpha\nset alpha Point \"{\n  x: 10,\n  y: 20\n}\"\n"
        "get alpha Point\n");
    fclose(file);

    CommandScript *script = ecs_ensure(w, t, CommandScript);
    script->path = ecs_os_strdup(path);
    ecs_modified(w, t, CommandScript);
    ecs_os_free(path);

    const TerminalSession *s = ecs_get(w, t, TerminalSession);
    test_assert(s != NULL);
    test_int(s->count, 3);
    test_str(s->ring[1].line, "set alpha Point \"{\n  x: 10,\n  y: 20\n}\"");
    test_str(s->ring[2].result, "{x: 10, y: 20}");
}

void CommandScript_log_copied_to_file(void)
{
    // command/integration
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    const char *process_dir = ecs_singleton_get(w, App)->process_dir;
    char *path = flecs_asprintf("%s/run.cmd", process_dir);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fprintf(file, "touch alpha\n");
    fclose(file);

    CommandScript *script = ecs_ensure(w, t, CommandScript);
    script->path = ecs_os_strdup(path);
    ecs_modified(w, t, CommandScript);
    ecs_os_free(path);

    char *copy = flecs_asprintf("%s/session.log", process_dir);
    Terminal *term = ecs_ensure(w, t, Terminal);
    term->log = ecs_os_strdup(copy);
    ecs_modified(w, t, Terminal);
    ecs_os_free(box_command_run(w, t, "touch beta", NULL));

    file = fopen(copy, "r");
    test_assert(file != NULL);
    char buffer[4096];
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[size] = '\0';
    test_str(buffer, "$ touch alpha\nalpha\n$ touch beta\nbeta\n");
    ecs_os_free(copy);
}
