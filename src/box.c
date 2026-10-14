/**
 * @file box.c
 * @brief Imports all modules of the box module.
 */

#include <box.h>

void BoxImport(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxEffects);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxMovement);
    ECS_IMPORT(world, BoxLayout);
    ECS_IMPORT(world, BoxApp);
}
