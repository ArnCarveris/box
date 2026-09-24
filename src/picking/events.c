/**
 * @file picking/events.c
 * @brief Raises the pick events of the frame as script events a task can await.
 *
 * Raising an event walks the parent chain of the hit and stops at the first
 * entity with a waiter. The action of a click walks the same chain, so a widget
 * fires the action a key binding fires.
 *
 * A press nobody waited for resolves the first waiter on its entity that
 * registers while the button is still held.
 */

#include "picking_internal.h"
#include "../input/input_internal.h"

/** The async functions of the on scope, in the order of the PickEvent constants */
static const char *box_pick_wait_names[5] = {
    "hover", "press", "release", "click", "drag"
};

/** Suspend a task until a pick event reaches the entity of the argument.
 *
 * @param ctx Call context, whose ctx holds the event
 * @param argc Number of arguments
 * @param argv The entity to listen on
 * @param future Future the event resolves
 */
static void box_pick_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    PickEvent event = (PickEvent)(uintptr_t)ctx->ctx;
    ecs_entity_t entity = argc == 1 ? *(ecs_entity_t*)argv[0].ptr : ctx->entity;
    PickState *state = box_picking_state(ctx->world);

    if (!entity || !state) {
        ecs_script_future_reject(future, "on.click expects an entity");
        ecs_script_future_release(future);
    } else if (event == PickPress && state->press_missed && state->pressed &&
        state->press.entity == entity)
    {
        state->press_missed = false;
        ecs_script_future_resolve(future, &(ecs_value_t){
            .type = ecs_id(PickHit), .ptr = &state->press });
        ecs_script_future_release(future);
    } else {
        box_pick_waiter_t *waiter = ecs_vec_append_t(
            NULL, &state->waiters, box_pick_waiter_t);
        waiter->entity = entity;
        waiter->event = event;
        waiter->future = future;
    }
}

/** Drop the waiter of a future whose task was cancelled.
 *
 * @param ctx Call context
 * @param future The future that was cancelled
 */
static void box_pick_wait_cancel(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    PickState *state = box_picking_state(ctx->world);
    int32_t i, count = state ? ecs_vec_count(&state->waiters) : 0;
    for (i = 0; i < count; i ++) {
        box_pick_waiter_t *waiter = ecs_vec_get_t(
            &state->waiters, box_pick_waiter_t, i);
        if (waiter->future == future) {
            ecs_script_future_release(future);
            ecs_vec_remove_ordered_t(&state->waiters, box_pick_waiter_t, i);
            break;
        }
    }
}

/** Resolve every waiter that listens for an event on one entity.
 *

 * @param state The module state
 * @param entity The entity the event reached
 * @param event The interaction that was raised
 * @param hit The hit the handlers receive
 * @return true when at least one waiter resolved
 */
static bool box_pick_deliver(
    PickState *state,
    ecs_entity_t entity,
    PickEvent event,
    const PickHit *hit)
{
    bool result = false;
    int32_t i = 0;
    while (i < ecs_vec_count(&state->waiters)) {
        box_pick_waiter_t *waiter = ecs_vec_get_t(
            &state->waiters, box_pick_waiter_t, i);
        if (waiter->entity != entity || waiter->event != event) {
            i ++;
            continue;
        }

        ecs_script_future_t *future = waiter->future;
        ecs_vec_remove_ordered_t(&state->waiters, box_pick_waiter_t, i);
        ecs_script_future_resolve(future, &(ecs_value_t){
            .type = ecs_id(PickHit), .ptr = ECS_CONST_CAST(PickHit*, hit) });
        ecs_script_future_release(future);
        result = true;
    }

    return result;
}

/** Fire the action of a node a click landed on, which takes the click.
 *
 * @param world The world
 * @param e The node
 * @return true when the node carries an action that fired
 */
static bool box_pick_node_action(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const Action *action = ecs_get(world, e, Action);
    bool result = action && action->action &&
        ecs_has(world, action->action, InputAction);
    if (result) {
        box_input_emit(world, action->action, e);
    }

    return result;
}

ecs_entity_t box_picking_raise(
    ecs_world_t *world,
    PickEvent event,
    const PickHit *hit)
{
    PickState *state = box_picking_state(world);
    Raise *raise = box_input_raise(world);
    Raise outer = raise ? raise[0] : (Raise){0};
    ecs_entity_t result = 0;
    ecs_entity_t e = hit->entity;
    while (state && e && ecs_is_alive(world, e)) {
        if (raise) {
            raise->target = e;
            raise->source = hit->entity;
            raise->priority = BOX_PICK_PRIORITY;
            raise->consumed = false;
        }

        bool taken = box_pick_deliver(state, e, event, hit);
        taken = (event == PickClick && box_pick_node_action(world, e)) || taken;
        taken = (raise && raise->consumed) || taken;
        if (taken) {
            result = e;
            break;
        }

        e = ecs_get_parent(world, e);
    }

    if (raise) {
        raise[0] = outer;
    }

    return result;
}

ecs_entity_t box_picking_raise_unhandled(
    ecs_world_t *world,
    PickEvent event,
    const PickHit *hit)
{
    PickState *state = box_picking_state(world);
    ecs_entity_t result = 0;
    if (state && box_pick_deliver(state, Unhandled, event, hit)) {
        result = Unhandled;
    }

    return result;
}

ecs_entity_t box_picking_action(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_entity_t cur = e;
    while (cur && ecs_is_alive(world, cur)) {
        const Action *action = ecs_get(world, cur, Action);
        if (action && action->action &&
            ecs_has(world, action->action, InputAction))
        {
            box_input_emit(world, action->action, cur);
            result = cur;
            break;
        }

        cur = ecs_get_parent(world, cur);
    }

    return result;
}

void box_picking_events_init(
    ecs_world_t *world)
{
    ecs_entity_t on = ecs_entity(world, {
        .name = "on", .parent = ecs_lookup(world, "flecs.script") });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_async_function(world, {
            .name = box_pick_wait_names[i],
            .parent = on,
            .return_type = ecs_id(PickHit),
            .params = {{ "entity", ecs_id(ecs_entity_t) }},
            .callback = box_pick_wait,
            .cancel = box_pick_wait_cancel,
            .ctx = (void*)(uintptr_t)i
        });
    }
}
