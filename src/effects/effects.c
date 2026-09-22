/**
 * @file effects/effects.c
 */

#define BOX_EFFECTS_IMPL
#include "effects_internal.h"

ECS_COMPONENT_DECLARE(EffectState);

/** Effect constructor: an effect is enabled, reads the color and runs full size.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
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
        fx[i].order = 0;
        fx[i].scale = 1.0f;
        fx[i].enabled = true;
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

/** Registry destructor releasing the scratch stack.
 *
 * The query belongs to an entity of the module, so the world releases it.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_effect_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    EffectState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &state[i].stack, box_effect_src_t);
    }
}

/** Recount the effects that can run after one of them changed.
 *
 * A world whose effects are all off skips the chain of every view, which is what
 * a scene that runs no post process pays for the feature.
 *
 * @param it Iterator over the changed effects
 */
static void box_effect_on_change(
    ecs_iter_t *it)
{
    EffectState *state = ecs_get_mut(it->world, ecs_id(EffectState), EffectState);
    if (!state || !state->effects) {
        goto done;
    }

    int32_t count = 0;
    ecs_iter_t eit = ecs_query_iter(it->world, state->effects);
    while (ecs_query_next(&eit)) {
        Effect *fx = ecs_field(&eit, Effect, 0);
        int32_t i;
        for (i = 0; i < eit.count; i ++) {
            count += fx[i].enabled && fx[i].shader ? 1 : 0;
        }
    }

    state->enabled = count;
done:
    return;
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
    ecs_set_hooks(world, EffectState, {
        .ctor = flecs_default_ctor,
        .dtor = box_effect_state_dtor
    });

    ecs_query_t *query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "query" }),
        .terms = {{ .id = ecs_id(Effect), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    EffectState *state = ecs_singleton_ensure(world, EffectState);
    ecs_vec_init_t(NULL, &state->stack, box_effect_src_t, 0);
    state->effects = query;

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Effect) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet, EcsOnRemove },
        .callback = box_effect_on_change
    });
}
