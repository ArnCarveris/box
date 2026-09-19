/**
 * @file Report.c
 * @brief Tests for the report of entities that no script defines.
 */

#include <box.h>
#include <bake_test.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

void Report_unowned_entity_is_reported(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    ecs_entity_t e = ecs_entity(world, { .name = "runtime" });
    ecs_set(world, e, Position, {4, 5, 6});

    char *report = NULL;
    char *folder = box_checkpoint_write(world, NULL, &report);
    test_assert(folder != NULL);
    test_assert(report != NULL);
    test_str(report, BOX_CHECKPOINT_SKIPPED "runtime\n");

    ecs_os_free(report);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Report_owned_entity_is_not_reported(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *report = NULL;
    char *folder = box_checkpoint_write(world, NULL, &report);
    test_assert(folder != NULL);
    test_assert(report != NULL);
    test_str(report, "");

    ecs_os_free(report);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Report_entity_without_a_value_is_not_reported(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t tag = ecs_entity(world, { .name = "Marker" });
    ecs_entity_t e = ecs_entity(world, { .name = "marked" });
    ecs_add_id(world, e, tag);

    char *report = NULL;
    char *folder = box_checkpoint_write(world, NULL, &report);
    test_assert(folder != NULL);
    test_assert(report != NULL);
    test_str(report, "");

    ecs_os_free(report);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Report_command_returns_the_folder(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    ecs_entity_t t = ecs_entity(world, { .name = "agent0" });
    ecs_add(world, t, Terminal);

    bool error = true;
    char *out = box_command_run(world, t, "checkpoint", &error);
    test_assert(out != NULL);
    test_bool(error, false);

    char *end = strchr(out, '\n');
    if (end) {
        end[0] = '\0';
    }

    char *expect = flecs_asprintf("%s/checkpoints/0",
        ecs_singleton_get(world, App)->process_dir);
    test_str(out, expect);

    struct stat st;
    char *path = flecs_asprintf("%s/scene.flecs", out);
    test_int(stat(path, &st), 0);

    ecs_os_free(path);
    ecs_os_free(expect);
    ecs_os_free(out);
    ecs_fini(world);
}

void Report_command_reports_an_unowned_entity(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    ecs_entity_t e = ecs_entity(world, { .name = "runtime" });
    ecs_set(world, e, Position, {4, 5, 6});

    ecs_entity_t t = ecs_entity(world, { .name = "agent0" });
    ecs_add(world, t, Terminal);

    char *out = box_command_run(world, t, "checkpoint", NULL);
    test_assert(out != NULL);
    test_assert(strstr(out, BOX_CHECKPOINT_SKIPPED "runtime\n") != NULL);

    ecs_os_free(out);
    ecs_fini(world);
}

void Report_command_takes_a_name(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    ecs_entity_t t = ecs_entity(world, { .name = "agent0" });
    ecs_add(world, t, Terminal);

    char *out = box_command_run(world, t, "checkpoint before", NULL);
    test_assert(out != NULL);

    char *end = strchr(out, '\n');
    if (end) {
        end[0] = '\0';
    }

    char *expect = flecs_asprintf("%s/checkpoints/before",
        ecs_singleton_get(world, App)->process_dir);
    test_str(out, expect);

    ecs_os_free(expect);
    ecs_os_free(out);
    ecs_fini(world);
}
