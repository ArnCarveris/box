/**
 * @file effects/effects.c
 * @brief Registers the components of the box.effects module.
 */

#define BOX_EFFECTS_IMPL
#include <box.h>

void BoxEffectsImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxEffects);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Bloom);
    ECS_META_COMPONENT(world, Ssao);
    ECS_META_COMPONENT(world, HeightFog);
    ECS_META_COMPONENT(world, AutoExposure);
    ECS_META_COMPONENT(world, Tonemap);
}
