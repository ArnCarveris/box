/**
 * @file light/light.c
 * @brief Registers the box.light module.
 */

#define BOX_LIGHT_IMPL
#include <box.h>

void BoxLightImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLight);
    ECS_IMPORT(world, BoxTransform);

    ECS_META_COMPONENT(world, DirectionalLight);
    ECS_META_COMPONENT(world, PointLight);
    ECS_META_COMPONENT(world, SpotLight);

    ecs_add_pair(world, ecs_id(DirectionalLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PointLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(SpotLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(DirectionalLight), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(PointLight), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(SpotLight), EcsWith, ecs_id(WorldTransform));
}
