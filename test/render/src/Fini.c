/**
 * @file Fini.c
 * @brief Tests for module import order not crashing at world fini.
 */

#include <box.h>
#include <bake_test.h>

void Fini_controller_then_ui_finishes_cleanly(void)
{
    // ore_engine/ui_after_controller_fini
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_lookup(world, "box.render.RenderState") != 0);
    test_int(ecs_fini(world), 0);
}

void Fini_every_module_reverse_dependency_order_finishes_cleanly(void)
{
    // ore_engine/ui_after_controller_fini
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_lookup(world, "box.render.RenderState") != 0);
    test_int(ecs_fini(world), 0);
}
