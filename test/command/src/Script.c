/**
 * @file Script.c
 * @brief Tests for the script_read, script_write, script_grep and script_patch commands.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;
static char *path;

void Script_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxCommand);
    ecs_entity(w, { .name = "Spin" });
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
    path = flecs_asprintf(
        "%s/scene.flecs", ecs_singleton_get(w, App)->process_dir);
    FILE *f = fopen(path, "w");
    fputs("foo {}\nbar { Spin }\n", f);
    fclose(f);
    ecs_script(w, { .filename = path });
}

void Script_teardown(void)
{
    ecs_fini(w);
    ecs_os_free(path);
}

void Script_read_source(void)
{
    // command/script
    char *line = flecs_asprintf("script_read %s", path);
    bool error = true;
    char *out = box_command_run(w, session, line, &error);
    test_bool(error, false);
    test_str(out, "foo {}\nbar { Spin }\n");
    ecs_os_free(out);
    ecs_os_free(line);
}

void Script_read_by_basename(void)
{
    // command/script
    char *out = box_command_run(w, session, "script_read scene.flecs", NULL);
    test_str(out, "foo {}\nbar { Spin }\n");
    ecs_os_free(out);
}

void Script_unknown_file(void)
{
    // command/script
    char *out = box_command_run(w, session, "script_read nope.flecs", NULL);
    test_str(out, "unknown script 'nope.flecs'");
    ecs_os_free(out);
}

void Script_write_changes_world(void)
{
    // command/script
    char *line = flecs_asprintf(
        "script_write %s \"foo {}\nbar { Spin }\nbaz { Spin }\n\"", path);
    char *out = box_command_run(w, session, line, NULL);
    test_str(out, "ok");
    ecs_entity_t baz = ecs_lookup(w, "baz");
    test_assert(baz != 0);
    test_assert(ecs_has_id(w, baz, ecs_lookup(w, "Spin")));
    ecs_os_free(out);
    ecs_os_free(line);
}

void Script_write_parse_error_keeps_state(void)
{
    // command/script
    ecs_log_set_level(-4);
    char *line = flecs_asprintf("script_write %s \"foo {\"", path);
    char *out = box_command_run(w, session, line, NULL);
    test_assert(strstr(out, "unexpected end of script") != NULL);
    ecs_entity_t bar = ecs_lookup(w, "bar");
    test_assert(bar != 0);
    test_assert(ecs_has_id(w, bar, ecs_lookup(w, "Spin")));
    ecs_os_free(out);
    ecs_os_free(line);
    out = box_command_run(w, session, "script_read scene.flecs", NULL);
    test_str(out, "foo {}\nbar { Spin }\n");
    ecs_os_free(out);
}

void Script_grep_lines(void)
{
    // command/script
    char *out = box_command_run(w, session, "script_grep Spin", NULL);
    char *expect = flecs_asprintf("%s:2:bar { Spin }\n", path);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(out);
}

void Script_patch_changes_world(void)
{
    // command/script
    char *line = flecs_asprintf("script_patch %s \"@@ -1,2 +1,3 @@\n"
        " foo {}\n bar { Spin }\n+baz { Spin }\n\"", path);
    char *out = box_command_run(w, session, line, NULL);
    test_str(out, "ok");
    ecs_entity_t baz = ecs_lookup(w, "baz");
    test_assert(baz != 0);
    test_assert(ecs_has_id(w, baz, ecs_lookup(w, "Spin")));
    ecs_os_free(out);
    ecs_os_free(line);
}

void Script_patch_compounds(void)
{
    // command/script
    char *line = flecs_asprintf("script_patch %s \"@@ -1,2 +1,3 @@\n"
        " foo {}\n bar { Spin }\n+baz { Spin }\n\"", path);
    char *out = box_command_run(w, session, line, NULL);
    test_str(out, "ok");
    ecs_os_free(out);
    ecs_os_free(line);
    line = flecs_asprintf("script_patch %s \"@@ -3,1 +3,2 @@\n"
        " baz { Spin }\n+qux { Spin }\n\"", path);
    out = box_command_run(w, session, line, NULL);
    test_str(out, "ok");
    ecs_os_free(out);
    ecs_os_free(line);
    test_assert(ecs_lookup(w, "qux") != 0);
    out = box_command_run(w, session, "script_read scene.flecs", NULL);
    test_str(out, "foo {}\nbar { Spin }\nbaz { Spin }\nqux { Spin }\n");
    ecs_os_free(out);
}

void Script_patch_hunk_mismatch(void)
{
    // command/script
    char *line = flecs_asprintf("script_patch %s \"@@ -1,2 +1,3 @@\n"
        " foo {}\n wrong { Spin }\n+baz { Spin }\n\"", path);
    char *out = box_command_run(w, session, line, NULL);
    test_str(out, "hunk 1: context does not match at line 2");
    test_assert(ecs_lookup(w, "baz") == 0);
    test_assert(ecs_lookup(w, "bar") != 0);
    ecs_os_free(out);
    ecs_os_free(line);
}
