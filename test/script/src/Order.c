/**
 * @file Order.c
 * @brief Tests for the ordering of pending instances around the camera.
 */

#include <box.h>
#include <bake_test.h>

void Order_nearest_to_the_camera_first(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_ensure(world, ScriptSettings)->enabled = true;
    ecs_singleton_modified(world, ScriptSettings);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.camera\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "eye {\n"
        "  Position: {0, 0, 0}\n"
        "  Camera: {fov: 60}\n"
        "}\n"
        "far {\n"
        "  Position: {10, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "near {\n"
        "  Position: {1, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "mid {\n"
        "  Position: {5, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "near.trunk") != 0);
    test_assert(ecs_lookup(world, "mid.trunk") == 0);
    test_assert(ecs_lookup(world, "far.trunk") == 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "mid.trunk") != 0);
    test_assert(ecs_lookup(world, "far.trunk") == 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "far.trunk") != 0);

    ecs_fini(world);
}

void Order_reorder_when_the_camera_moves(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_ensure(world, ScriptSettings)->enabled = true;
    ecs_singleton_modified(world, ScriptSettings);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.camera\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "eye {\n"
        "  Position: {0, 0, 0}\n"
        "  Camera: {fov: 60}\n"
        "}\n"
        "here_a {\n"
        "  Position: {1, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "here_b {\n"
        "  Position: {2, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "there_a {\n"
        "  Position: {100, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "there_b {\n"
        "  Position: {101, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "here_a.trunk") != 0);

    ecs_entity_t eye = ecs_lookup(world, "eye");
    ecs_set(world, eye, Position, {100, 0, 0});
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "there_a.trunk") != 0);
    test_assert(ecs_lookup(world, "here_b.trunk") == 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "there_b.trunk") != 0);
    test_assert(ecs_lookup(world, "here_b.trunk") == 0);

    ecs_fini(world);
}

void Order_nearest_first_off_keeps_script_order(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_ensure(world, ScriptSettings)->enabled = true;
    ecs_singleton_modified(world, ScriptSettings);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 1;
    settings->budget_ms = 0;
    settings->nearest_first = false;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.camera\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "eye {\n"
        "  Position: {0, 0, 0}\n"
        "  Camera: {fov: 60}\n"
        "}\n"
        "far {\n"
        "  Position: {10, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "near {\n"
        "  Position: {1, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "far.trunk") != 0);
    test_assert(ecs_lookup(world, "near.trunk") == 0);

    ecs_fini(world);
}

void Order_no_camera_keeps_script_order(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_ensure(world, ScriptSettings)->enabled = true;
    ecs_singleton_modified(world, ScriptSettings);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "far {\n"
        "  Position: {10, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "near {\n"
        "  Position: {1, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "far.trunk") != 0);
    test_assert(ecs_lookup(world, "near.trunk") == 0);

    ecs_fini(world);
}

void Order_instance_without_position_goes_first(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_ensure(world, ScriptSettings)->enabled = true;
    ecs_singleton_modified(world, ScriptSettings);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.camera\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "eye {\n"
        "  Position: {0, 0, 0}\n"
        "  Camera: {fov: 60}\n"
        "}\n"
        "far {\n"
        "  Position: {100, 0, 0}\n"
        "  Tree\n"
        "}\n"
        "nowhere { Tree }\n", NULL), 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "nowhere.trunk") != 0);
    test_assert(ecs_lookup(world, "far.trunk") == 0);

    ecs_fini(world);
}

void Order_world_position_orders_a_child_instance(void)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_ensure(world, ScriptSettings)->enabled = true;
    ecs_singleton_modified(world, ScriptSettings);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, ScriptSettings);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.camera\n"
        "template Tree {\n"
        "  trunk { Rotation: {0, 1, 0} }\n"
        "}\n"
        "eye {\n"
        "  Position: {0, 0, 0}\n"
        "  Camera: {fov: 60}\n"
        "}\n"
        "lodge { Position: {100, 0, 0}\n"
        "  kid {\n"
        "    Position: {0, 0, 0}\n"
        "    Tree\n"
        "  }\n"
        "}\n"
        "root {\n"
        "  Position: {10, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "root.trunk") != 0);
    test_assert(ecs_lookup(world, "lodge.kid.trunk") == 0);

    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, "lodge.kid.trunk") != 0);

    ecs_fini(world);
}
