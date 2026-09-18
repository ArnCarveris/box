/**
 * @file Terminal.c
 * @brief Tests for session folders, the commands.log file and the log commands.
 */

#include <box.h>
#include <bake_test.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;

void Terminal_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxCommand);
}

void Terminal_teardown(void)
{
    ecs_fini(w);
}

void Terminal_folder_created_and_removed(void)
{
    // command/terminal
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    const Terminal *term = ecs_get(w, t, Terminal);
    test_assert(term != NULL);
    test_assert(term->session_dir != NULL);
    char *dir = ecs_os_strdup(term->session_dir);
    struct stat st;
    test_int(stat(dir, &st), 0);
    test_assert(S_ISDIR(st.st_mode));
    ecs_delete(w, t);
    test_assert(stat(dir, &st) != 0);
    ecs_os_free(dir);
}

void Terminal_resources_subfolder(void)
{
    // command/terminal
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    char *path = flecs_asprintf("%s/resources", ecs_get(w, t, Terminal)->session_dir);
    struct stat st;
    test_int(stat(path, &st), 0);
    test_assert(S_ISDIR(st.st_mode));
    ecs_delete(w, t);
    test_assert(stat(path, &st) != 0);
    ecs_os_free(path);
}

void Terminal_session_dir_under_process_dir(void)
{
    // command/terminal
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    char *expect = flecs_asprintf("%s/agent0", ecs_singleton_get(w, App)->process_dir);
    test_str(ecs_get(w, t, Terminal)->session_dir, expect);
    ecs_os_free(expect);
}

void Terminal_session_dir_kept_after_set(void)
{
    // command/terminal
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    Terminal *term = ecs_ensure(w, t, Terminal);
    term->cwd = ecs_os_strdup("flecs");
    ecs_modified(w, t, Terminal);
    const Terminal *after = ecs_get(w, t, Terminal);
    test_str(after->cwd, "flecs");
    test_assert(after->session_dir != NULL);
    struct stat st;
    test_int(stat(after->session_dir, &st), 0);
}

void Terminal_log_contains_line_and_result(void)
{
    // command/terminal
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    ecs_os_free(box_command_run(w, t, "help", NULL));
    char *path = flecs_asprintf("%s/commands.log", ecs_get(w, t, Terminal)->session_dir);
    FILE *f = fopen(path, "r");
    test_assert(f != NULL);
    char buffer[4096];
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, f);
    fclose(f);
    test_assert(size > 0);
    buffer[size] = '\0';
    test_assert(strstr(buffer, "$ help\n") != NULL);
    test_assert(strstr(buffer, "commands [count:i32]") != NULL);
    ecs_os_free(path);
}

void Terminal_commands_lists_last_lines(void)
{
    // command/terminal
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    ecs_os_free(box_command_run(w, t, "help", NULL));
    ecs_os_free(box_command_run(w, t, "help", NULL));
    ecs_os_free(box_command_run(w, t, "help", NULL));
    bool error = true;
    char *out = box_command_run(w, t, "commands 2", &error);
    test_bool(error, false);
    int32_t count = 0;
    const char *p = strstr(out, "$ help");
    while (p) {
        count ++;
        p = strstr(p + 1, "$ help");
    }

    test_int(count, 2);
    ecs_os_free(out);
}

void Terminal_commands_defaults_to_all_lines(void)
{
    // command/terminal
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    ecs_os_free(box_command_run(w, t, "help", NULL));
    ecs_os_free(box_command_run(w, t, "help", NULL));
    char *out = box_command_run(w, t, "commands", NULL);
    int32_t count = 0;
    const char *p = strstr(out, "$ help");
    while (p) {
        count ++;
        p = strstr(p + 1, "$ help");
    }

    test_int(count, 2);
    ecs_os_free(out);
}

void Terminal_output_written_on_input(void)
{
    // command/terminal
    ecs_entity_t t = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, t, Terminal);
    TerminalInput *in = ecs_ensure(w, t, TerminalInput);
    in->line = ecs_os_strdup("help");
    ecs_modified(w, t, TerminalInput);
    const TerminalOutput *out = ecs_get(w, t, TerminalOutput);
    test_assert(out != NULL);
    test_bool(out->error, false);
    test_assert(out->result != NULL);
    test_assert(strstr(out->result, "help") != NULL);
}

void Terminal_script_session_runs_command(void)
{
    // command/terminal
    const char *code =
        "using box.command\n"
        "terminals {\n"
        "  agent0 {\n"
        "    Terminal: {}\n"
        "    TerminalInput: {line: \"help\"}\n"
        "  }\n"
        "}\n";
    test_int(ecs_script_run(w, "session.flecs", code, NULL), 0);
    ecs_entity_t t = ecs_lookup(w, "terminals.agent0");
    test_assert(t != 0);
    const Terminal *term = ecs_get(w, t, Terminal);
    test_assert(term != NULL);
    test_assert(term->session_dir != NULL);
    struct stat st;
    test_int(stat(term->session_dir, &st), 0);
    const TerminalOutput *out = ecs_get(w, t, TerminalOutput);
    test_assert(out != NULL);
    test_bool(out->error, false);
    test_assert(strstr(out->result, "help - ") != NULL);
}
