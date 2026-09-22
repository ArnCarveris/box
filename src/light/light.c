/**
 * @file light/light.c
 * @brief Registers the box.light module.
 */

#define BOX_LIGHT_IMPL
#include <box.h>

ECS_TAG_DECLARE(LightModulatorParams);

/** Modulator constructor, which leaves a modulator that names a shader running.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_light_modulator_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    LightModulator *modulator = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        modulator[i].shader = 0;
        modulator[i].params = 0;
        modulator[i].enabled = true;
    }
}

void box_light_modulator_params(
    ecs_world_t *world,
    ecs_entity_t component)
{
    ecs_add_id(world, component, LightModulatorParams);
}

void BoxLightImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLight);
    ECS_IMPORT(world, BoxTransform);

    ECS_META_COMPONENT(world, DirectionalLight);
    ECS_META_COMPONENT(world, CelestialLight);
    ECS_META_COMPONENT(world, PointLight);
    ECS_META_COMPONENT(world, SpotLight);
    ECS_META_COMPONENT(world, EnvironmentLight);
    ECS_META_COMPONENT(world, ClusterSettings);
    ECS_META_COMPONENT(world, BakeSettings);
    ECS_META_COMPONENT(world, LightModulator);
    ECS_TAG_DEFINE(world, LightModulatorParams);

    ecs_set_hooks(world, LightModulator, { .ctor = box_light_modulator_ctor });
    ecs_add_pair(world, ecs_id(LightModulator), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(DirectionalLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(CelestialLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(PointLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(SpotLight), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(DirectionalLight), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(PointLight), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(SpotLight), EcsWith, ecs_id(WorldTransform));

    ecs_singleton_set(world, BakeSettings, {
        .enabled = true,
        .budget = BOX_BAKE_BUDGET,
        .probe_spacing = BOX_BAKE_SPACING,
        .probe_max = BOX_BAKE_PROBE_MAX,
        .x = BOX_BAKE_GRID_X,
        .y = BOX_BAKE_GRID_Y,
        .z = BOX_BAKE_GRID_Z,
        .steps = BOX_BAKE_STEPS
    });

    ecs_singleton_set(world, ClusterSettings, {
        .x = BOX_CLUSTER_X,
        .y = BOX_CLUSTER_Y,
        .z = BOX_CLUSTER_Z,
        .max_lights = BOX_CLUSTER_MAX_LIGHTS
    });
}
