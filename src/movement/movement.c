/**
 * @file movement/movement.c
 * @brief Registers the components of the box.movement module.
 */

#define BOX_MOVEMENT_IMPL
#include <box.h>

void BoxMovementImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMovement);

    ECS_META_COMPONENT(world, Velocity);
    ECS_META_COMPONENT(world, AngularVelocity);
}
