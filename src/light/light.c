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
    ECS_META_COMPONENT(world, EnvironmentLight);
    ECS_META_COMPONENT(world, ClusterSettings);

    ecs_add_pair(world, ecs_id(DirectionalLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PointLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(SpotLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(DirectionalLight), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(PointLight), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(SpotLight), EcsWith, ecs_id(WorldTransform));

    ecs_singleton_set(world, ClusterSettings, {
        .x = BOX_CLUSTER_X,
        .y = BOX_CLUSTER_Y,
        .z = BOX_CLUSTER_Z,
        .max_lights = BOX_CLUSTER_MAX_LIGHTS
    });
}
