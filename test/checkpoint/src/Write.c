/**
 * @file Write.c
 * @brief Tests for writing the state of the loaded scripts into a checkpoint.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>

void Write_writes_a_file_per_script(void)
{
    // checkpoints/checkpoint_write
    const char *first = "/tmp/box_checkpoint_first.flecs";
    const char *second = "/tmp/box_checkpoint_second.flecs";
    FILE *file = fopen(first, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n", file);
    fclose(file);

    file = fopen(second, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\nb {\n  Position: {4, 5, 6}\n}\n", file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .filename = first }) != 0);
    test_assert(ecs_script(world, { .filename = second }) != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *path = flecs_asprintf("%s/box_checkpoint_first.flecs", folder);
    file = fopen(path, "r");
    test_assert(file != NULL);
    fclose(file);
    ecs_os_free(path);

    path = flecs_asprintf("%s/box_checkpoint_second.flecs", folder);
    file = fopen(path, "r");
    test_assert(file != NULL);
    fclose(file);
    ecs_os_free(path);

    ecs_os_free(folder);
    ecs_fini(world);
    remove(first);
    remove(second);
}

void Write_values_are_updated(void)
{
    // checkpoints/checkpoint_write
    const char *scene = "/tmp/box_checkpoint_values.flecs";
    FILE *file = fopen(scene, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n", file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .filename = scene }) != 0);

    ecs_entity_t a = ecs_lookup(world, "a");
    test_assert(a != 0);
    ecs_set(world, a, Position, {7, 8, 9});

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *path = flecs_asprintf("%s/box_checkpoint_values.flecs", folder);
    char buffer[4096] = {0};
    file = fopen(path, "r");
    test_assert(file != NULL);
    buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = '\0';
    fclose(file);

    test_assert(strstr(buffer, "Position: {7, 8, 9}") != NULL);
    test_assert(strstr(buffer, "Position: {1, 2, 3}") == NULL);

    ecs_os_free(path);
    ecs_os_free(folder);
    ecs_fini(world);
    remove(scene);
}

void Write_layout_and_comments_are_preserved(void)
{
    // checkpoints/checkpoint_write
    const char *scene = "/tmp/box_checkpoint_layout.flecs";
    FILE *file = fopen(scene, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\n// the only entity\na {\n  Position: {1, 2, 3}\n}\n",
        file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .filename = scene }) != 0);
    ecs_set(world, ecs_lookup(world, "a"), Position, {0, 0, 1});

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *path = flecs_asprintf("%s/box_checkpoint_layout.flecs", folder);
    char buffer[4096] = {0};
    file = fopen(path, "r");
    test_assert(file != NULL);
    buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = '\0';
    fclose(file);

    test_assert(strstr(buffer, "using box.transform") != NULL);
    test_assert(strstr(buffer, "// the only entity") != NULL);
    test_assert(strstr(buffer, "Position: {0, 0, 1}") != NULL);

    ecs_os_free(path);
    ecs_os_free(folder);
    ecs_fini(world);
    remove(scene);
}

void Write_derived_components_are_left_out(void)
{
    // checkpoints/checkpoint_write
    const char *scene = "/tmp/box_checkpoint_derived.flecs";
    FILE *file = fopen(scene, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n", file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .filename = scene }) != 0);
    ecs_progress(world, 0);

    ecs_entity_t a = ecs_lookup(world, "a");
    test_assert(ecs_has(world, a, WorldTransform));

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *path = flecs_asprintf("%s/box_checkpoint_derived.flecs", folder);
    char buffer[4096] = {0};
    file = fopen(path, "r");
    test_assert(file != NULL);
    buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = '\0';
    fclose(file);

    test_assert(strstr(buffer, "WorldTransform") == NULL);
    test_assert(strstr(buffer, "AABB") == NULL);

    ecs_os_free(path);
    ecs_os_free(folder);
    ecs_fini(world);
    remove(scene);
}

void Write_component_added_at_runtime_is_written(void)
{
    // checkpoints/checkpoint_write
    const char *scene = "/tmp/box_checkpoint_added.flecs";
    FILE *file = fopen(scene, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n", file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .filename = scene }) != 0);
    ecs_set(world, ecs_lookup(world, "a"), Rotation, {0, 1, 0});

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *path = flecs_asprintf("%s/box_checkpoint_added.flecs", folder);
    char buffer[4096] = {0};
    file = fopen(path, "r");
    test_assert(file != NULL);
    buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = '\0';
    fclose(file);

    test_assert(strstr(buffer, "Rotation: {x: 0, y: 1, z: 0}") != NULL);

    ecs_os_free(path);
    ecs_os_free(folder);
    ecs_fini(world);
    remove(scene);
}

void Write_script_without_a_file_uses_its_name(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n"
    });
    test_assert(s != 0);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    char *path = flecs_asprintf("%s/scene.flecs", folder);
    FILE *file = fopen(path, "r");
    test_assert(file != NULL);
    fclose(file);

    ecs_os_free(path);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Write_entity_without_a_statement_is_reported(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\nfor i in 0..2 {\n"
            "  _ {\n    Position: {i, 0, 0}\n  }\n}\n"
    });
    test_assert(s != 0);
    test_int(ecs_count(world, Position), 2);

    char *report = NULL;
    char *folder = box_checkpoint_write(world, NULL, &report);
    test_assert(folder != NULL);
    test_assert(report != NULL);

    int32_t count = 0;
    const char *p = report;
    while ((p = strstr(p, BOX_CHECKPOINT_SKIPPED))) {
        count ++;
        p ++;
    }

    test_int(count, 2);

    ecs_os_free(report);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Write_template_body_entities_are_reported(void)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\n\ntemplate Tree {\n"
            "  trunk {\n    Position: {0, 1, 0}\n  }\n}\n\n"
            "a {\n  Position: {1, 2, 3}\n  Tree\n}\n"
    });
    test_assert(s != 0);
    test_assert(ecs_lookup(world, "a.trunk") != 0);

    char *report = NULL;
    char *folder = box_checkpoint_write(world, NULL, &report);
    test_assert(folder != NULL);
    test_assert(report != NULL);
    test_assert(strstr(report, "1 entities of a template body") != NULL);

    char *path = flecs_asprintf("%s/scene.flecs", folder);
    char buffer[4096] = {0};
    FILE *file = fopen(path, "r");
    test_assert(file != NULL);
    buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = '\0';
    fclose(file);

    test_assert(strstr(buffer, "template Tree") != NULL);

    ecs_os_free(path);
    ecs_os_free(report);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Write_checkpoint_reloads_into_the_same_state(void)
{
    // checkpoints/checkpoint_write
    const char *scene = "/tmp/box_checkpoint_reload.flecs";
    FILE *file = fopen(scene, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n\na {\n  Position: {1, 2, 3}\n}\n", file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_script(world, { .filename = scene });
    test_assert(s != 0);
    ecs_set(world, ecs_lookup(world, "a"), Position, {7, 8, 9});

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_set(world, ecs_lookup(world, "a"), Position, {0, 0, 0});

    char *path = flecs_asprintf("%s/box_checkpoint_reload.flecs", folder);
    char buffer[4096] = {0};
    file = fopen(path, "r");
    test_assert(file != NULL);
    buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = '\0';
    fclose(file);

    test_int(ecs_script_reload(world, s, buffer), 0);

    const Position *p = ecs_get(world, ecs_lookup(world, "a"), Position);
    test_assert(p != NULL);
    test_int(p->x, 7);
    test_int(p->y, 8);
    test_int(p->z, 9);

    ecs_os_free(path);
    ecs_os_free(folder);
    ecs_fini(world);
    remove(scene);
}
