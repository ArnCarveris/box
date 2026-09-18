/**
 * @file script/script.c
 * @brief Registers the box.script module and expands templates in PreUpdate.
 */

#define BOX_SCRIPT_IMPL
#include "script_internal.h"

ECS_COMPONENT_DECLARE(ScriptState);

/** Free the instantiation state of a world.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_script_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;

    ScriptState *ss = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (ss[i].state) {
            box_script_state_free(ss[i].state);
            ss[i].state = NULL;
        }
    }
}

/** Mark a script template as manual so its instances are left pending.
 *
 * @param it The iterator of the observer
 */
static void box_script_on_script(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    const EcsScript *script = ecs_field(it, EcsScript, 0);
    ecs_entity_t *templates = ecs_os_alloca_n(ecs_entity_t, it->count);
    int32_t i, count = 0;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (script[i].template_ && !ecs_has_id(world, e, EcsScriptTemplateManual)) {
            templates[count ++] = e;
        }
    }

    for (i = 0; i < count; i ++) {
        ecs_add_id(world, templates[i], EcsScriptTemplateManual);
    }
}

/** Mark the queue for a rebuild when an instance starts waiting.
 *
 * @param it The iterator of the observer
 */
static void box_script_on_pending(
    ecs_iter_t *it)
{
    box_script_state_t *state = it->ctx;
    state->dirty = true;
}

/** Expand the pending instances a frame can afford and report the counters.
 *
 * @param it The iterator of the system
 */
static void BoxScriptUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    const ScriptSettings *settings = ecs_singleton_get(world, ScriptSettings);
    ScriptStats *stats = ecs_get_mut(world, ecs_id(ScriptStats), ScriptStats);
    ecs_assert(settings != NULL, ECS_INVALID_OPERATION, "ScriptSettings is missing");
    ecs_assert(stats != NULL, ECS_INVALID_OPERATION, "ScriptStats is missing");

    box_script_drain(world, it->ctx, settings, stats);
}

void BoxScriptImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxScript);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);

    ECS_META_COMPONENT(world, ScriptSettings);
    ECS_META_COMPONENT(world, ScriptStats);
    ECS_COMPONENT_DEFINE(world, ScriptState);

    ecs_set_hooks(world, ScriptState, {
        .ctor = flecs_default_ctor,
        .dtor = box_script_state_dtor
    });
    ecs_add_pair(world, ecs_id(ScriptState), EcsOnInstantiate, EcsDontInherit);

    ecs_singleton_set(world, ScriptSettings, {
        .templates_per_frame = 256,
        .budget_ms = 1.0,
        .reorder_distance = 4.0,
        .reorder_interval = 30,
        .nearest_first = true,
        .enabled = true
    });
    ecs_singleton_ensure(world, ScriptStats);
    ecs_singleton_modified(world, ScriptStats);

    ecs_world_t *raw = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    box_script_state_t *state = box_script_state_new(raw);
    ScriptState *ss = ecs_singleton_ensure(world, ScriptState);
    ss->state = state;
    ecs_singleton_modified(world, ScriptState);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(EcsScript) }},
        .events = { EcsOnSet },
        .callback = box_script_on_script,
        .yield_existing = true
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(EcsScriptTemplatePending, EcsWildcard) }},
        .events = { EcsOnAdd },
        .callback = box_script_on_pending,
        .ctx = state
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxScriptUpdate" }),
        .phase = EcsPreUpdate,
        .query.terms = {{ .id = ecs_id(ScriptSettings) }},
        .callback = BoxScriptUpdate,
        .ctx = state,
        .immediate = true
    });
}
