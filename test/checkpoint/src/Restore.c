/**
 * @file Restore.c
 * @brief Tests for reloading the scripts of a checkpoint folder.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

void Restore_last_checkpoint_is_restored(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_set(world, ecs_lookup(world, "a"), Position, {7, 8, 9});

    char *report = NULL;
    char *restored = box_checkpoint_restore(world, NULL, 0, &report);
    test_assert(restored != NULL);
    test_str(restored, folder);
    test_str(report, "");

    const Position *p = ecs_get(world, ecs_lookup(world, "a"), Position);
    test_assert(p != NULL);
    test_int(p->x, 1);
    test_int(p->y, 2);
    test_int(p->z, 3);

    ecs_os_free(report);
    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_named_checkpoint_is_restored(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, "before", NULL);
    test_assert(folder != NULL);

    ecs_set(world, ecs_lookup(world, "a"), Position, {7, 8, 9});
    char *after = box_checkpoint_write(world, "after", NULL);
    test_assert(after != NULL);

    char *restored = box_checkpoint_restore(world, "before", 0, NULL);
    test_assert(restored != NULL);
    test_str(restored, folder);

    const Position *p = ecs_get(world, ecs_lookup(world, "a"), Position);
    test_assert(p != NULL);
    test_int(p->x, 1);

    ecs_os_free(restored);
    ecs_os_free(after);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_every_script_is_restored(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "first" }),
        .code = "using box.transform\n\na {\n  Position: {1, 0, 0}\n}\n"
    }) != 0);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "second" }),
        .code = "using box.transform\n\nb {\n  Position: {2, 0, 0}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_set(world, ecs_lookup(world, "a"), Position, {9, 0, 0});
    ecs_set(world, ecs_lookup(world, "b"), Position, {9, 0, 0});

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);

    test_int(ecs_get(world, ecs_lookup(world, "a"), Position)->x, 1);
    test_int(ecs_get(world, ecs_lookup(world, "b"), Position)->x, 2);

    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_entity_added_after_the_checkpoint_is_removed(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    });
    test_assert(s != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    test_int(ecs_script_update(world, s, 0,
        "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
        "\nb {\n  Position: {4, 5, 6}\n}\n"), 0);
    test_assert(ecs_lookup(world, "b") != 0);

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);
    test_assert(ecs_lookup(world, "a") != 0);
    test_assert(ecs_lookup(world, "b") == 0);

    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_entity_deleted_after_the_checkpoint_is_back(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_delete(world, ecs_lookup(world, "a"));
    test_assert(ecs_lookup(world, "a") == 0);

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);

    ecs_entity_t a = ecs_lookup(world, "a");
    test_assert(a != 0);
    test_int(ecs_get(world, a, Position)->x, 1);

    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_unowned_entity_is_left_alone_and_reported(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_entity_t runtime = ecs_entity(world, { .name = "runtime" });
    ecs_set(world, runtime, Position, {4, 5, 6});

    char *report = NULL;
    char *restored = box_checkpoint_restore(world, NULL, 0, &report);
    test_assert(restored != NULL);
    test_str(report, BOX_CHECKPOINT_KEPT "runtime\n");

    test_assert(ecs_lookup(world, "runtime") == runtime);
    test_int(ecs_get(world, runtime, Position)->x, 4);

    ecs_os_free(report);
    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_entity_handles_change(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    ecs_entity_t before = ecs_lookup(world, "a");
    test_assert(before != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);

    ecs_entity_t after = ecs_lookup(world, "a");
    test_assert(after != 0);
    test_assert(after != before);
    test_int(ecs_get(world, after, Position)->x, 1);

    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_kept_entity_keeps_its_script(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "session" }),
        .code = "using box.transform\n\nkeeper {\n  Position: {1, 0, 0}\n}\n"
    }) != 0);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {2, 0, 0}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_entity_t keeper = ecs_lookup(world, "keeper");
    test_assert(keeper != 0);
    ecs_set(world, keeper, Position, {9, 0, 0});
    ecs_set(world, ecs_lookup(world, "a"), Position, {9, 0, 0});

    char *restored = box_checkpoint_restore(world, NULL, keeper, NULL);
    test_assert(restored != NULL);

    test_assert(ecs_lookup(world, "keeper") == keeper);
    test_int(ecs_get(world, keeper, Position)->x, 9);
    test_int(ecs_get(world, ecs_lookup(world, "a"), Position)->x, 2);

    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_counters_survive_a_restore(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\nusing box.checkpoint\n\n"
            "$ {\n  Checkpoint: {dir: \"/tmp/box_restore_counters\"}\n}\n"
            "\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);
    test_str(folder, "/tmp/box_restore_counters/0");

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);

    const Checkpoint *cp = ecs_singleton_get(world, Checkpoint);
    test_assert(cp != NULL);
    test_str(cp->dir, "/tmp/box_restore_counters");
    test_str(cp->last, folder);
    test_int(cp->count, 1);

    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_restore_without_a_checkpoint_fails(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *report = NULL;
    test_assert(box_checkpoint_restore(world, NULL, 0, &report) == NULL);
    test_assert(report != NULL);
    test_str(report, BOX_CHECKPOINT_FAILED "no checkpoint was written\n");

    ecs_os_free(report);
    ecs_fini(world);
}

void Restore_unknown_name_fails(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);
    ecs_set(world, ecs_lookup(world, "a"), Position, {7, 8, 9});

    char *report = NULL;
    test_assert(box_checkpoint_restore(world, "nope", 0, &report) == NULL);
    test_assert(report != NULL);
    test_assert(!strncmp(report, BOX_CHECKPOINT_FAILED,
        strlen(BOX_CHECKPOINT_FAILED)));
    test_assert(strstr(report, "nope") != NULL);
    test_int(ecs_get(world, ecs_lookup(world, "a"), Position)->x, 7);

    ecs_os_free(report);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_corrupt_checkpoint_keeps_the_world(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "first" }),
        .code = "using box.transform\n\na {\n  Position: {1, 0, 0}\n}\n"
    }) != 0);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "second" }),
        .code = "using box.transform\n\nb {\n  Position: {2, 0, 0}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *path = flecs_asprintf("%s/second.flecs", folder);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\nb {\n  Position: {2, 0,\n", file);
    fclose(file);

    ecs_set(world, ecs_lookup(world, "a"), Position, {7, 0, 0});
    ecs_set(world, ecs_lookup(world, "b"), Position, {8, 0, 0});

    test_assert(box_checkpoint_restore(world, NULL, 0, NULL) == NULL);
    test_int(ecs_get(world, ecs_lookup(world, "a"), Position)->x, 7);
    test_int(ecs_get(world, ecs_lookup(world, "b"), Position)->x, 8);

    ecs_os_free(path);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_corrupt_checkpoint_names_the_file(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *path = flecs_asprintf("%s/scene.flecs", folder);
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\na {\n  Position: {1, 2,\n", file);
    fclose(file);

    char *report = NULL;
    test_assert(box_checkpoint_restore(world, NULL, 0, &report) == NULL);
    test_assert(report != NULL);
    test_assert(!strncmp(report, BOX_CHECKPOINT_FAILED "scene.flecs\n",
        strlen(BOX_CHECKPOINT_FAILED "scene.flecs\n")));

    ecs_os_free(report);
    ecs_os_free(path);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Restore_command_restores_the_last_checkpoint(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    ecs_entity_t t = ecs_entity(world, { .name = "agent0" });
    ecs_add(world, t, Terminal);

    ecs_os_free(box_command_run(world, t, "checkpoint", NULL));
    ecs_set(world, ecs_lookup(world, "a"), Position, {7, 8, 9});

    bool error = true;
    char *out = box_command_run(world, t, "restore", &error);
    test_assert(out != NULL);
    test_bool(error, false);

    char *end = strchr(out, '\n');
    if (end) {
        end[0] = '\0';
    }

    char *expect = flecs_asprintf("%s/checkpoints/0",
        ecs_singleton_get(world, App)->process_dir);
    test_str(out, expect);
    test_int(ecs_get(world, ecs_lookup(world, "a"), Position)->x, 1);

    ecs_os_free(expect);
    ecs_os_free(out);
    ecs_fini(world);
}

void Restore_command_takes_a_name(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    ecs_entity_t t = ecs_entity(world, { .name = "agent0" });
    ecs_add(world, t, Terminal);

    ecs_os_free(box_command_run(world, t, "checkpoint before", NULL));
    ecs_set(world, ecs_lookup(world, "a"), Position, {7, 8, 9});
    ecs_os_free(box_command_run(world, t, "checkpoint after", NULL));

    bool error = true;
    char *out = box_command_run(world, t, "restore before", &error);
    test_assert(out != NULL);
    test_bool(error, false);

    char *end = strchr(out, '\n');
    if (end) {
        end[0] = '\0';
    }

    char *expect = flecs_asprintf("%s/checkpoints/before",
        ecs_singleton_get(world, App)->process_dir);
    test_str(out, expect);
    test_int(ecs_get(world, ecs_lookup(world, "a"), Position)->x, 1);

    ecs_os_free(expect);
    ecs_os_free(out);
    ecs_fini(world);
}

void Restore_command_reports_an_unknown_name(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxTransform);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    ecs_entity_t t = ecs_entity(world, { .name = "agent0" });
    ecs_add(world, t, Terminal);

    bool error = false;
    char *out = box_command_run(world, t, "restore nope", &error);
    test_assert(out != NULL);
    test_bool(error, true);
    test_assert(strstr(out, "nope") != NULL);

    ecs_os_free(out);
    ecs_fini(world);
}
