/**
 * @file picking/picking.c
 * @brief Registers the box.picking module: its components, its state and its system.
 */

#define BOX_PICKING_IMPL
#include "picking_internal.h"

ECS_COMPONENT_DECLARE(PickState);
ECS_TAG_DECLARE(Pickable);
ECS_TAG_DECLARE(Hovered);
ECS_TAG_DECLARE(Unhandled);

/** PickSettings constructor: picking is on, with the default reach and threshold.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_pick_settings_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    PickSettings *settings = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        settings[i].max_distance = BOX_PICK_DISTANCE;
        settings[i].drag_threshold = BOX_PICK_DRAG_THRESHOLD;
        settings[i].enabled = true;
    }
}

/** Release the queries and the futures of the tasks that are still waiting.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_pick_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    PickState *state = ptr;
    int32_t i, w;
    for (i = 0; i < count; i ++) {
        for (w = 0; w < ecs_vec_count(&state[i].waiters); w ++) {
            ecs_script_future_release(ecs_vec_get_t(
                &state[i].waiters, box_pick_waiter_t, w)->future);
        }

        ecs_vec_fini_t(NULL, &state[i].waiters, box_pick_waiter_t);
        if (state[i].views) {
            ecs_query_fini(state[i].views);
        }

        if (state[i].cameras) {
            ecs_query_fini(state[i].cameras);
        }
    }
}

void box_picking_set_source(
    ecs_world_t *world,
    box_picking_source_t source)
{
    PickState *state = box_picking_state(world);
    if (state) {
        state->source = source;
    }
}

void box_picking_set_through(
    ecs_world_t *world,
    box_picking_through_t through)
{
    PickState *state = box_picking_state(world);
    if (state) {
        state->through = through;
    }
}

void box_picking_invalidate(
    ecs_world_t *world)
{
    PickState *state = box_picking_state(world);
    if (state) {
        state->valid = false;
    }
}

PickState* box_picking_state(
    const ecs_world_t *world)
{
    PickState *result = NULL;
    if (ecs_id(PickState) && !ecs_is_fini(world)) {
        result = ECS_CONST_CAST(PickState*,
            ecs_get_id(world, ecs_id(PickState), ecs_id(PickState)));
    }

    return result;
}

void BoxPickingImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxPicking);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxCollision);

    ECS_META_COMPONENT(world, PickSettings);
    ECS_META_COMPONENT(world, PickHit);
    ECS_META_COMPONENT(world, PickEvent);
    ECS_META_COMPONENT(world, Action);
    ECS_TAG_DEFINE(world, Pickable);
    ECS_TAG_DEFINE(world, Hovered);
    ECS_TAG_DEFINE(world, Unhandled);
    ECS_COMPONENT_DEFINE(world, PickState);

    ecs_set_hooks(world, PickSettings, { .ctor = box_pick_settings_ctor });
    ecs_set_hooks(world, PickHit, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Action, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, PickState, {
        .ctor = flecs_default_ctor,
        .dtor = box_pick_state_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    ecs_add_pair(world, Hovered, EcsOnInstantiate, EcsDontInherit);

    PickState *state = ecs_singleton_ensure(world, PickState);
    ecs_vec_init_t(NULL, &state->waiters, box_pick_waiter_t, 0);
    state->views = ecs_query(world, {
        .terms = {{ .id = ecs_id(View), .inout = EcsIn }}
    });

    state->cameras = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Camera), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        }
    });

    ecs_singleton_ensure(world, PickSettings);
    ecs_singleton_modified(world, PickSettings);
    box_picking_events_init(world);

    ECS_SYSTEM(world, BoxPickingUpdate, EcsOnLoad, 0);
}
