/**
 * @file Reload.c
 * @brief Tests for reloading a scene file with ecs_script_reload.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>

void Reload_rewritten_file_is_reloaded(void)
{
    // checkpoints/script_reload
    const char *path = "/tmp/box_script_reload_rewritten.flecs";
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n"
        "a { Position: {1, 2, 3} }\n"
        "b { Position: {4, 5, 6} }\n", file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);

    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .filename = path
    });
    test_assert(s != 0);

    ecs_entity_t a = ecs_lookup(world, "a");
    test_assert(a != 0);
    test_assert(ecs_lookup(world, "b") != 0);
    test_assert(ecs_lookup(world, "c") == 0);

    const Position *p = ecs_get(world, a, Position);
    test_assert(p != NULL);
    test_int(p->x, 1);

    file = fopen(path, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n"
        "a { Position: {7, 8, 9} }\n"
        "c { Position: {0, 0, 0} }\n", file);
    fclose(file);

    test_int(ecs_script_reload(world, s, NULL), 0);

    a = ecs_lookup(world, "a");
    test_assert(a != 0);
    test_assert(ecs_lookup(world, "b") == 0);
    test_assert(ecs_lookup(world, "c") != 0);

    p = ecs_get(world, a, Position);
    test_assert(p != NULL);
    test_int(p->x, 7);
    test_int(p->y, 8);
    test_int(p->z, 9);

    ecs_fini(world);
    remove(path);
}

void Reload_parse_error_keeps_the_scene(void)
{
    // checkpoints/script_reload
    const char *path = "/tmp/box_script_reload_broken.flecs";
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n"
        "a { Position: {1, 2, 3} }\n", file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);

    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .filename = path
    });
    test_assert(s != 0);
    test_assert(ecs_lookup(world, "a") != 0);

    file = fopen(path, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n"
        "a { Position: {1, 2, 3}\n", file);
    fclose(file);

    ecs_log_set_level(-4);
    test_int(ecs_script_reload(world, s, NULL), -1);
    ecs_log_set_level(-1);

    ecs_entity_t a = ecs_lookup(world, "a");
    test_assert(a != 0);

    const Position *p = ecs_get(world, a, Position);
    test_assert(p != NULL);
    test_int(p->x, 1);
    test_int(p->y, 2);
    test_int(p->z, 3);

    const EcsScript *script = ecs_get(world, s, EcsScript);
    test_assert(script != NULL);
    test_assert(script->error != NULL);

    ecs_fini(world);
    remove(path);
}

void Reload_reloaded_instances_are_pending(void)
{
    // checkpoints/script_reload
    const char *path = "/tmp/box_script_reload_template.flecs";
    FILE *file = fopen(path, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "for i in 0..3 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", file);
    fclose(file);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);

    ecs_entity_t s = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .filename = path
    });
    test_assert(s != 0);

    ecs_entity_t t = ecs_lookup(world, "Tree");
    test_assert(t != 0);
    test_assert(ecs_has_id(world, t, EcsScriptTemplateManual));
    test_int(ecs_count_id(world, ecs_pair(EcsScriptTemplatePending, t)), 3);

    file = fopen(path, "w");
    test_assert(file != NULL);
    fputs("using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "for i in 0..5 {\n"
        "  _ {\n"
        "    Position: {i, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n", file);
    fclose(file);

    test_int(ecs_script_reload(world, s, NULL), 0);

    t = ecs_lookup(world, "Tree");
    test_assert(t != 0);
    test_assert(ecs_has_id(world, t, EcsScriptTemplateManual));
    test_int(ecs_count_id(world, ecs_pair(EcsScriptTemplatePending, t)), 5);
    test_int(ecs_count_id(world, ecs_id(Rotation)), 0);

    ecs_fini(world);
    remove(path);
}
