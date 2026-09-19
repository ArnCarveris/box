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
    ECS_IMPORT(world, BoxController);
    ECS_IMPORT(world, BoxUi);
    test_assert(ecs_lookup(world, "box.render.RenderState") != 0);
    test_int(ecs_fini(world), 0);
}

void Fini_every_module_reverse_dependency_order_finishes_cleanly(void)
{
    // ore_engine/ui_after_controller_fini
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxScript);
    ECS_IMPORT(world, BoxMonitor);
    ECS_IMPORT(world, BoxUi);
    ECS_IMPORT(world, BoxController);
    ECS_IMPORT(world, BoxLod);
    ECS_IMPORT(world, BoxParticle);
    ECS_IMPORT(world, BoxSpline);
    ECS_IMPORT(world, BoxPicking);
    ECS_IMPORT(world, BoxCollision);
    ECS_IMPORT(world, BoxMovement);
    ECS_IMPORT(world, BoxTexture);
    ECS_IMPORT(world, BoxGltf);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxTransition);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxJob);
    ECS_IMPORT(world, BoxCore);
    test_assert(ecs_lookup(world, "box.render.RenderState") != 0);
    test_int(ecs_fini(world), 0);
}
