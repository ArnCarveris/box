/**
 * @file effects/effects.c
 */

#define BOX_EFFECTS_IMPL
#include "effects_internal.h"

ECS_COMPONENT_DECLARE(EffectState);

static void box_effect_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Effect *fx = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        fx[i].shader = 0;
        fx[i].params = 0;
        fx[i].setup = 0;
        fx[i].texture = 0;
        fx[i].scale = 1.0f;
        fx[i].color = true;
        fx[i].depth = false;
        fx[i].normals = false;
        fx[i].motion = false;
    }
}

static void box_effect_texture_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    EffectTexture *texture = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        texture[i].path = NULL;
        texture[i].parameter = -1;
        texture[i].value = 0;
    }
}

void BoxEffectsImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxEffects);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxShader);

    ECS_META_COMPONENT(world, Effect);
    ECS_META_COMPONENT(world, EffectSetupKind);
    ECS_META_COMPONENT(world, EffectTextureFormat);
    ECS_META_COMPONENT(world, EffectSetup);
    ECS_META_COMPONENT(world, EffectTexture);
    ECS_COMPONENT_DEFINE(world, EffectState);

    ecs_set_hooks(world, Effect, { .ctor = box_effect_ctor });
    ecs_set_hooks(world, EffectTexture, { .ctor = box_effect_texture_ctor });
    ecs_set_hooks(world, EffectState, { .ctor = flecs_default_ctor });

    EffectState *state = ecs_singleton_ensure(world, EffectState);
    (void)state;
    ecs_add_pair(world, ecs_id(Effect), EcsOnInstantiate, EcsInherit);
}
