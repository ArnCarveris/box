/**
 * @file light/light.c
 * @brief Registers the components of the box.light module.
 */

#define BOX_LIGHT_IMPL
#include <box.h>

void BoxLightImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLight);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, DirectionalLight);
    ECS_META_COMPONENT(world, PointLight);
    ECS_META_COMPONENT(world, SpotLight);
}
