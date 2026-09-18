/**
 * @file effects/effects.c
 * @brief Registers the box.effects module, its defaults and the color grade effect.
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
        fx[i].order = 0;
        fx[i].scale = 1.0f;
        fx[i].enabled = true;
        fx[i].color = true;
        fx[i].depth = false;
        fx[i].normals = false;
        fx[i].motion = false;
        fx[i].pyramid = false;
        fx[i].exposure = false;
        fx[i].occlusion = false;
    }
}

/** ColorGrade constructor: the neutral grade leaves the image alone.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_grade_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ColorGrade *grade = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        grade[i].exposure = 0.0f;
        grade[i].saturation = 1.0f;
    }
}

/** AutoExposure constructor: a slow adaptation around middle grey.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_exposure_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    AutoExposure *exposure = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        exposure[i].speed_up = 1.0f;
        exposure[i].speed_down = 1.0f;
        exposure[i].min_ev = -4.0f;
        exposure[i].max_ev = 4.0f;
        exposure[i].target = 0.18f;
        exposure[i].compensation = 0.0f;
        exposure[i].pinned = false;
    }
}

/** ToneMapping constructor: white at one with the dither on.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_tonemap_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ToneMapping *tonemap = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        tonemap[i].white = 1.0f;
        tonemap[i].dither = true;
    }
}

/** Bloom constructor: a wide, subtle pyramid without a knee.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_bloom_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Bloom *bloom = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        bloom[i].intensity = 0.08f;
        bloom[i].radius = 0.7f;
        bloom[i].knee = 0.0f;
        bloom[i].firefly_clamp = 8.0f;
    }
}

/** Gtao constructor: two slices at half resolution with the history on.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_gtao_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Gtao *gtao = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        gtao[i].radius = BOX_GTAO_RADIUS;
        gtao[i].slices = (float)BOX_GTAO_SLICES;
        gtao[i].steps = (float)BOX_GTAO_STEPS;
        gtao[i].power = BOX_GTAO_POWER;
        gtao[i].temporal = true;
        gtao[i].half_res = true;
    }
}

/** Fog constructor: a thin grey haze that starts at the camera.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_fog_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Fog *fog = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        fog[i].color = (vec3){ 0.55f, 0.6f, 0.68f };
        fog[i].density = 0.02f;
        fog[i].falloff = 0.1f;
        fog[i].start = 0.0f;
        fog[i].max_opacity = 1.0f;
        fog[i].height = 0.0f;
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

/** One effect the engine registers, disabled until a scene enables it */
typedef struct box_effect_builtin_t {
    const char *name; /**< Entity name under the shaders and the stack scope */
    const char *path; /**< WGSL file its pass is compiled from */
    int32_t order;    /**< Order in the chain, the lowest runs first */
    uint32_t inputs;  /**< BOX_EFFECT bits the pass declares */
} box_effect_builtin_t;

/** Effects the engine registers */
#define BOX_EFFECT_BUILTIN_COUNT (6)

/** The effects every world gets, in the order they run */
static const box_effect_builtin_t box_effect_builtins[] = {
    { "color_grade", "etc/shaders/color_grade.wgsl", 100, BOX_EFFECT_COLOR },
    { "gtao", "etc/shaders/gtao.wgsl", 600, BOX_EFFECT_COLOR | BOX_EFFECT_DEPTH |
        BOX_EFFECT_NORMALS | BOX_EFFECT_MOTION | BOX_EFFECT_OCCLUSION },
    { "fog", "etc/shaders/fog.wgsl", 700, BOX_EFFECT_COLOR | BOX_EFFECT_DEPTH },
    { "bloom", "etc/shaders/bloom.wgsl", 800, BOX_EFFECT_COLOR | BOX_EFFECT_PYRAMID },
    { "auto_exposure", "etc/shaders/auto_exposure.wgsl", 850,
        BOX_EFFECT_COLOR | BOX_EFFECT_EXPOSURE },
    { "tonemap", "etc/shaders/tonemap.wgsl", 900, BOX_EFFECT_COLOR }
};

/** Create the shader and the effect entity of one builtin effect.
 *
 * @param world The world
 * @param shaders Scope the Shader entities live in
 * @param stack Scope the effect entities live in
 * @param builtin The effect to create
 * @return The effect entity
 */
static ecs_entity_t box_effect_builtin(
    ecs_world_t *world,
    ecs_entity_t shaders,
    ecs_entity_t stack,
    const box_effect_builtin_t *builtin)
{
    ecs_entity_t shader = ecs_entity(world, { .name = builtin->name, .parent = shaders });
    Shader *source = ecs_ensure(world, shader, Shader);
    source->path = ecs_os_strdup(builtin->path);
    source->macros = NULL;
    ecs_modified(world, shader, Shader);

    ecs_entity_t result = ecs_entity(world, { .name = builtin->name, .parent = stack });
    ecs_set(world, result, Effect, {
        .shader = shader,
        .order = builtin->order,
        .scale = 1.0f,
        .enabled = false,
        .color = (builtin->inputs & BOX_EFFECT_COLOR) != 0,
        .depth = (builtin->inputs & BOX_EFFECT_DEPTH) != 0,
        .normals = (builtin->inputs & BOX_EFFECT_NORMALS) != 0,
        .motion = (builtin->inputs & BOX_EFFECT_MOTION) != 0,
        .pyramid = (builtin->inputs & BOX_EFFECT_PYRAMID) != 0,
        .exposure = (builtin->inputs & BOX_EFFECT_EXPOSURE) != 0,
        .occlusion = (builtin->inputs & BOX_EFFECT_OCCLUSION) != 0
    });

    return result;
}

/** Register the builtin effects with their parameter components.
 *
 * Every one of them is off, so a world that enables none of them produces the
 * frame it produced before the effects existed.
 *
 * @param world The world
 * @param shaders Scope the Shader entities live in
 * @param stack Scope the effect entities live in
 */
static void box_effect_stack(
    ecs_world_t *world,
    ecs_entity_t shaders,
    ecs_entity_t stack)
{
    ecs_entity_t created[BOX_EFFECT_BUILTIN_COUNT];
    int32_t i;
    for (i = 0; i < BOX_EFFECT_BUILTIN_COUNT; i ++) {
        created[i] = box_effect_builtin(world, shaders, stack, &box_effect_builtins[i]);
    }

    ecs_add(world, created[0], ColorGrade);
    ecs_add(world, created[1], Gtao);
    ecs_add(world, created[2], Fog);
    ecs_add(world, created[3], Bloom);
    ecs_add(world, created[4], AutoExposure);
    ecs_add(world, created[5], ToneMapping);
}

void BoxEffectsImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxEffects);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxShader);

    ECS_META_COMPONENT(world, Effect);
    ECS_META_COMPONENT(world, ColorGrade);
    ECS_META_COMPONENT(world, AutoExposure);
    ECS_META_COMPONENT(world, ToneMapping);
    ECS_META_COMPONENT(world, Bloom);
    ECS_META_COMPONENT(world, Gtao);
    ECS_META_COMPONENT(world, Fog);
    ECS_COMPONENT_DEFINE(world, EffectState);

    ecs_set_hooks(world, Effect, { .ctor = box_effect_ctor });
    ecs_set_hooks(world, ColorGrade, { .ctor = box_grade_ctor });
    ecs_set_hooks(world, AutoExposure, { .ctor = box_exposure_ctor });
    ecs_set_hooks(world, ToneMapping, { .ctor = box_tonemap_ctor });
    ecs_set_hooks(world, Bloom, { .ctor = box_bloom_ctor });
    ecs_set_hooks(world, Gtao, { .ctor = box_gtao_ctor });
    ecs_set_hooks(world, Fog, { .ctor = box_fog_ctor });
    ecs_set_hooks(world, EffectState, {
        .ctor = flecs_default_ctor,
        .dtor = box_effect_state_dtor
    });

    ecs_query_t *query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "query" }),
        .terms = {{ .id = ecs_id(Effect), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_entity_t stack = ecs_entity(world, { .name = "stack" });
    ecs_entity_t shaders = ecs_entity(world, { .name = "shaders" });
    box_effect_stack(world, shaders, stack);

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
