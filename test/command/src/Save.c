/**
 * @file Save.c
 * @brief Tests for the save command: writing the values of the scene back to its script files.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Save_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Save_teardown(void)
{
    ecs_fini(w);
}

void Save_writes_changed_values_to_the_file(void)
{
    char *path = flecs_asprintf(
        "%s/save_changed.flecs", ecs_singleton_get(w, App)->process_dir);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fprintf(file, "using box.transform\n// crate\ncrate {\n  Position: {1, 2, 3}\n}\n");
    fclose(file);

    ecs_entity_t s = ecs_script(w, { .filename = path });
    test_assert(s != 0);
    ecs_entity_t crate = ecs_lookup(w, "crate");
    test_assert(crate != 0);
    ecs_set(w, crate, Position, {4, 5, 6});

    bool error = true;
    char *out = box_command_run(w, session, "save", &error);
    test_bool(error, false);
    char *expect = flecs_asprintf("saved %s\n", path);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(out);

    file = fopen(path, "r");
    test_assert(file != NULL);
    char buffer[256];
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[size] = '\0';
    test_str(buffer, "using box.transform\n// crate\ncrate {\n  Position: {4, 5, 6}\n}\n");

    crate = ecs_lookup(w, "crate");
    test_assert(crate != 0);
    const Position *p = ecs_get(w, crate, Position);
    test_assert(p != NULL);
    test_flt(p->x, 4);
    test_flt(p->y, 5);
    test_flt(p->z, 6);

    remove(path);
    ecs_os_free(path);
}

void Save_no_changes_leaves_the_file(void)
{
    char *path = flecs_asprintf(
        "%s/save_unchanged.flecs", ecs_singleton_get(w, App)->process_dir);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fprintf(file, "using box.transform\ncrate {\n  Position: {1.0, 2, 3}\n}\n");
    fclose(file);

    ecs_entity_t s = ecs_script(w, { .filename = path });
    test_assert(s != 0);
    ecs_entity_t crate = ecs_lookup(w, "crate");
    test_assert(crate != 0);
    remove(path);

    bool error = true;
    char *out = box_command_run(w, session, "save", &error);
    test_bool(error, false);
    test_str(out, "no changes\n");
    ecs_os_free(out);

    test_assert(fopen(path, "r") == NULL);
    test_assert(ecs_lookup(w, "crate") == crate);

    ecs_os_free(path);
}

void Save_saves_only_the_changed_script(void)
{
    const char *dir = ecs_singleton_get(w, App)->process_dir;
    char *path_a = flecs_asprintf("%s/save_a.flecs", dir);
    char *path_b = flecs_asprintf("%s/save_b.flecs", dir);
    FILE *file = fopen(path_a, "w");
    test_assert(file != NULL);
    fprintf(file, "using box.transform\na {\n  Position: {1, 2, 3}\n}\n");
    fclose(file);
    file = fopen(path_b, "w");
    test_assert(file != NULL);
    fprintf(file, "using box.transform\nb {\n  Position: {1, 2, 3}\n}\n");
    fclose(file);

    test_assert(ecs_script(w, { .filename = path_a }) != 0);
    test_assert(ecs_script(w, { .filename = path_b }) != 0);
    ecs_entity_t b = ecs_lookup(w, "b");
    test_assert(b != 0);
    ecs_set(w, b, Position, {7, 8, 9});

    bool error = true;
    char *out = box_command_run(w, session, "save", &error);
    test_bool(error, false);
    char *expect = flecs_asprintf("saved %s\n", path_b);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(out);

    file = fopen(path_b, "r");
    test_assert(file != NULL);
    char buffer[256];
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[size] = '\0';
    test_str(buffer, "using box.transform\nb {\n  Position: {7, 8, 9}\n}\n");

    remove(path_a);
    remove(path_b);
    ecs_os_free(path_a);
    ecs_os_free(path_b);
}

void Save_script_without_file(void)
{
    ecs_entity_t s = ecs_script(w, {
        .entity = ecs_entity(w, { .name = "inline_scene" }),
        .code = "using box.transform\ncrate {\n  Position: {1, 2, 3}\n}\n"
    });
    test_assert(s != 0);
    ecs_entity_t crate = ecs_lookup(w, "crate");
    test_assert(crate != 0);
    ecs_set(w, crate, Position, {0, 0, 1});

    bool error = true;
    char *out = box_command_run(w, session, "save", &error);
    test_bool(error, false);
    test_str(out, "updated inline_scene, no file to save to\n");
    ecs_os_free(out);

    test_str(ecs_get(w, s, EcsScript)->code,
        "using box.transform\ncrate {\n  Position: {0, 0, 1}\n}\n");
}

void Save_skips_framework_scripts(void)
{
    char *path = flecs_asprintf(
        "%s/save_framework.flecs", ecs_singleton_get(w, App)->process_dir);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fprintf(file, "using box.transform\ncrate {\n  Position: {1, 2, 3}\n}\n");
    fclose(file);

    ecs_entity_t module = ecs_lookup(w, "box");
    test_assert(module != 0);
    ecs_entity_t s = ecs_script(w, {
        .entity = ecs_entity(w, { .parent = module, .name = "save_framework" }),
        .filename = path
    });
    test_assert(s != 0);
    ecs_entity_t crate = ecs_lookup(w, "crate");
    test_assert(crate != 0);
    ecs_set(w, crate, Position, {4, 5, 6});

    bool error = true;
    char *out = box_command_run(w, session, "save", &error);
    test_bool(error, false);
    test_str(out, "no changes\n");
    ecs_os_free(out);

    file = fopen(path, "r");
    test_assert(file != NULL);
    char buffer[256];
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[size] = '\0';
    test_str(buffer, "using box.transform\ncrate {\n  Position: {1, 2, 3}\n}\n");

    remove(path);
    ecs_os_free(path);
}

void Save_help_lists_the_command(void)
{
    bool error = true;
    char *out = box_command_run(w, session, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "save") != NULL);
    ecs_os_free(out);
}
