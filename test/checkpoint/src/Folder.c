/**
 * @file Folder.c
 * @brief Tests for the checkpoint folder of an instance and its lifetime.
 */

#include <box.h>
#include <bake_test.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

void Folder_folder_is_under_the_process_folder(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *expect = flecs_asprintf("%s/checkpoints/0",
        ecs_singleton_get(world, App)->process_dir);
    test_str(folder, expect);

    struct stat st;
    test_int(stat(folder, &st), 0);
    test_assert(S_ISDIR(st.st_mode));

    ecs_os_free(expect);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Folder_folder_is_removed_when_the_instance_stops(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    struct stat st;
    test_int(stat(folder, &st), 0);

    ecs_fini(world);

    test_assert(stat(folder, &st) != 0);
    ecs_os_free(folder);
}

void Folder_repeated_checkpoints_get_their_own_folder(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *first = box_checkpoint_write(world, NULL, NULL);
    ecs_set(world, ecs_lookup(world, "a"), Position, {4, 5, 6});
    char *second = box_checkpoint_write(world, NULL, NULL);
    test_assert(first != NULL);
    test_assert(second != NULL);
    test_assert(ecs_os_strcmp(first, second) != 0);

    const Checkpoint *cp = ecs_singleton_get(world, Checkpoint);
    test_assert(cp != NULL);
    test_int(cp->count, 2);
    test_str(cp->last, second);

    char *path = flecs_asprintf("%s/scene.flecs", first);
    char buffer[4096] = {0};
    FILE *file = fopen(path, "r");
    test_assert(file != NULL);
    buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = '\0';
    fclose(file);
    test_assert(strstr(buffer, "Position: {1, 2, 3}") != NULL);
    ecs_os_free(path);

    path = flecs_asprintf("%s/scene.flecs", second);
    file = fopen(path, "r");
    test_assert(file != NULL);
    buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = '\0';
    fclose(file);
    test_assert(strstr(buffer, "Position: {4, 5, 6}") != NULL);

    ecs_os_free(path);
    ecs_os_free(first);
    ecs_os_free(second);
    ecs_fini(world);
}

void Folder_named_checkpoint_uses_the_name(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *folder = box_checkpoint_write(world, "before", NULL);
    test_assert(folder != NULL);

    char *expect = flecs_asprintf("%s/checkpoints/before",
        ecs_singleton_get(world, App)->process_dir);
    test_str(folder, expect);

    struct stat st;
    test_int(stat(folder, &st), 0);

    ecs_os_free(expect);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Folder_dir_overrides_the_process_folder(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    Checkpoint *cp = ecs_singleton_ensure(world, Checkpoint);
    cp->dir = ecs_os_strdup("/tmp/box_checkpoint_custom/nested");
    ecs_singleton_modified(world, Checkpoint);

    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);
    test_str(folder, "/tmp/box_checkpoint_custom/nested/0");

    char *path = flecs_asprintf("%s/scene.flecs", folder);
    FILE *file = fopen(path, "r");
    test_assert(file != NULL);
    fclose(file);

    ecs_fini(world);

    struct stat st;
    test_int(stat(folder, &st), 0);

    remove(path);
    rmdir(folder);
    rmdir("/tmp/box_checkpoint_custom/nested");
    rmdir("/tmp/box_checkpoint_custom");

    ecs_os_free(path);
    ecs_os_free(folder);
}
