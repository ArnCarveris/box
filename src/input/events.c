/**
 * @file input/events.c
 * @brief Raises the action events of the frame as script events a task can await.
 */

#include "input_internal.h"

ECS_COMPONENT_DECLARE(ActionWaiters);

/** The async functions of the on scope, in the order of their phases */
static const char *box_input_wait_names[3] = { "pressed", "down", "up" };

/** Get the waiters of a world, or NULL while it is being torn down.
 *
 * @param world The world
 * @return The waiters, or NULL when the module is gone
 */
static ActionWaiters* box_input_waiters(
    ecs_world_t *world)
{
    ActionWaiters *result = NULL;
    if (ecs_id(ActionWaiters) && !ecs_is_fini(world)) {
        result = ecs_get_mut(world, ecs_id(ActionWaiters), ActionWaiters);
    }

    return result;
}

/** Find the missed raise of an action's phase.
 *
 * @param waiters The waiters
 * @param action The action to find
 * @param phase The phase to find
 * @return Index of the missed raise, or -1 when the phase has none
 */
static int32_t box_input_missed_find(
    const ActionWaiters *waiters,
    ecs_entity_t action,
    ActionPhase phase)
{
    int32_t result = -1;
    int32_t i, count = ecs_vec_count(&waiters->missed);
    for (i = 0; i < count; i ++) {
        const box_missed_t *missed = ecs_vec_get_t(&waiters->missed, box_missed_t, i);
        if (missed->action == action && missed->phase == phase) {
            result = i;
        }
    }

    return result;
}

/** Suspend a task until the action of the argument reaches the phase.
 *
 * Delivers the missed raise of the phase right away when the frame that raised it
 * reached no waiter, the way `box_pick_hover` retries a missed hover, so a waiter
 * that registers in the frame a scene loads still catches the events of that frame.
 *
 * @param ctx Call context, whose ctx holds the phase
 * @param argc Number of arguments
 * @param argv The action entity to wait for
 * @param future Future the phase resolves
 */
static void box_input_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ActionPhase phase = (ActionPhase)(uintptr_t)ctx->ctx;
    ecs_entity_t action = argc == 1 ? *(ecs_entity_t*)argv[0].ptr : 0;
    ActionWaiters *waiters = box_input_waiters(ctx->world);
    int32_t missed = waiters ? box_input_missed_find(waiters, action, phase) : -1;
    if (!action || !waiters) {
        ecs_script_future_reject(future, "on.pressed expects an action entity");
        ecs_script_future_release(future);
    } else if (missed >= 0) {
        ecs_entity_t source = ecs_vec_get_t(&waiters->missed, box_missed_t, missed)->source;
        ecs_vec_remove_ordered_t(&waiters->missed, box_missed_t, missed);
        ecs_script_future_resolve(future, &(ecs_value_t){
            .type = ecs_id(ecs_entity_t), .ptr = &source });
        ecs_script_future_release(future);
    } else {
        box_waiter_t *waiter = ecs_vec_append_t(
            NULL, &waiters->waiters, box_waiter_t);
        waiter->action = action;
        waiter->phase = phase;
        waiter->future = future;
    }
}

/** Drop the waiter of a future whose task was cancelled.
 *
 * @param ctx Call context
 * @param future The future that was cancelled
 */
static void box_input_wait_cancel(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    ActionWaiters *waiters = box_input_waiters(ctx->world);
    int32_t i, count = waiters ? ecs_vec_count(&waiters->waiters) : 0;
    for (i = 0; i < count; i ++) {
        box_waiter_t *waiter = ecs_vec_get_t(
            &waiters->waiters, box_waiter_t, i);
        if (waiter->future == future) {
            ecs_script_future_release(future);
            ecs_vec_remove_ordered_t(&waiters->waiters, box_waiter_t, i);
            break;
        }
    }
}

/** Release the futures of the tasks that are still waiting.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_input_waiters_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ActionWaiters *waiters = ptr;
    int32_t i, w;
    for (i = 0; i < count; i ++) {
        for (w = 0; w < ecs_vec_count(&waiters[i].waiters); w ++) {
            ecs_script_future_release(ecs_vec_get_t(
                &waiters[i].waiters, box_waiter_t, w)->future);
        }

        ecs_vec_fini_t(NULL, &waiters[i].waiters, box_waiter_t);
        ecs_vec_fini_t(NULL, &waiters[i].missed, box_missed_t);
    }
}

int32_t box_input_deliver(
    ecs_world_t *world,
    ecs_entity_t action,
    ActionPhase phase,
    ecs_entity_t source)
{
    ActionWaiters *waiters = box_input_waiters(world);
    int32_t i = 0, delivered = 0;
    while (waiters && i < ecs_vec_count(&waiters->waiters)) {
        box_waiter_t *waiter = ecs_vec_get_t(
            &waiters->waiters, box_waiter_t, i);
        if (waiter->action != action || waiter->phase != phase) {
            i ++;
            continue;
        }

        ecs_script_future_t *future = waiter->future;
        ecs_vec_remove_ordered_t(&waiters->waiters, box_waiter_t, i);
        ecs_script_future_resolve(future, &(ecs_value_t){
            .type = ecs_id(ecs_entity_t), .ptr = &source });
        ecs_script_future_release(future);
        delivered ++;
    }

    return delivered;
}

void box_input_events_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, ActionWaiters);

    ecs_set_hooks(world, ActionWaiters, {
        .ctor = flecs_default_ctor,
        .dtor = box_input_waiters_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    ActionWaiters *waiters = ecs_singleton_ensure(world, ActionWaiters);
    ecs_vec_init_t(NULL, &waiters->waiters, box_waiter_t, 0);
    ecs_vec_init_t(NULL, &waiters->missed, box_missed_t, 0);

    ecs_entity_t on = ecs_entity(world, {
        .name = "on", .parent = ecs_lookup(world, "flecs.script") });

    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_async_function(world, {
            .name = box_input_wait_names[i],
            .parent = on,
            .return_type = ecs_id(ecs_entity_t),
            .params = {{ "action", ecs_id(ecs_entity_t) }},
            .callback = box_input_wait,
            .cancel = box_input_wait_cancel,
            .ctx = (void*)(uintptr_t)i
        });
    }
}

/** Keep or clear the missed raise of an action's phase.
 *
 * @param waiters The waiters
 * @param action The action the raise is about
 * @param phase The phase it reached
 * @param source Binding or emitter the raise came from
 * @param delivered Number of waiters the raise resolved
 */
static void box_input_missed_update(
    ActionWaiters *waiters,
    ecs_entity_t action,
    ActionPhase phase,
    ecs_entity_t source,
    int32_t delivered)
{
    int32_t index = box_input_missed_find(waiters, action, phase);
    box_missed_t *missed;
    if (delivered) {
        if (index >= 0) {
            ecs_vec_remove_ordered_t(&waiters->missed, box_missed_t, index);
        }

        goto done;
    }

    missed = index >= 0
        ? ecs_vec_get_t(&waiters->missed, box_missed_t, index)
        : ecs_vec_append_t(NULL, &waiters->missed, box_missed_t);
    missed->action = action;
    missed->phase = phase;
    missed->source = source;

done:
    return;
}

void BoxInputRaise(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    ActionWaiters *waiters = box_input_waiters(world);
    const ActionEvent *events;
    int32_t i, count = box_input_events(world, &events);
    for (i = 0; i < count; i ++) {
        int32_t delivered = box_input_deliver(
            world, events[i].action, events[i].phase, events[i].source);
        if (waiters) {
            box_input_missed_update(
                waiters, events[i].action, events[i].phase, events[i].source,
                delivered);
        }
    }
}
