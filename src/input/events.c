/**
 * @file input/events.c
 * @brief Raises the action events of the frame as script events a task can await.
 *
 * A raise is delivered against the Raise record of the world, so a handler takes
 * it with event.stop the way a DOM handler calls stopPropagation: the rest of
 * the handlers of that raise do not run and the key it came from is taken from
 * every map below it. input.consume takes a whole action for the frame.
 */

#include "input_internal.h"

ECS_COMPONENT_DECLARE(ActionWaiters);

/** The async functions of the on scope, in the order of their phases */
static const char *box_input_wait_names[4] = {
    "pressed", "down", "up", "dragged"
};

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
 * Every waiter that catches up gets the raise, because the instances of a scene
 * start their tasks together; the raise a waiter resumed on is dropped at the next
 * raise pass, so the await the resumed task registers again does not take it twice.
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
    int32_t missed = waiters && !waiters->resolving
        ? box_input_missed_find(waiters, action, phase)
        : -1;
    if (!action || !waiters) {
        ecs_script_future_reject(future, "on.pressed expects an action entity");
        ecs_script_future_release(future);
    } else if (missed >= 0) {
        box_missed_t *entry = ecs_vec_get_t(&waiters->missed, box_missed_t, missed);
        ecs_entity_t source = entry->source;
        entry->consumed = true;
        waiters->resolving ++;
        ecs_script_future_resolve(future, &(ecs_value_t){
            .type = ecs_id(ecs_entity_t), .ptr = &source });
        ecs_script_future_release(future);
        waiters = box_input_waiters(ctx->world);
        if (waiters) {
            waiters->resolving --;
        }
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

/** Build the value the handler of one phase of an action receives.
 *
 * Every phase but the drag hands the source that fired the action; a drag hands
 * where it began and where the cursor is now, so a handler needs no query of
 * its own to follow it.
 *
 * @param world The world
 * @param action The action the raise is about
 * @param phase The phase it reached
 * @param source Binding or emitter the raise came from
 * @param drag Receives the drag of the action when the phase is a drag
 * @return The value, which points at drag or at the source of the caller
 */
static ecs_value_t box_input_value(
    ecs_world_t *world,
    ecs_entity_t action,
    ActionPhase phase,
    ecs_entity_t *source,
    ActionDrag *drag)
{
    ecs_value_t result = {
        .type = ecs_id(ecs_entity_t), .ptr = source };
    InputState *state = phase == ActionDragged ? box_input_state(world) : NULL;
    const box_drag_t *held = state ? box_input_drag_of(state, action) : NULL;
    const Input *in = held ? ecs_singleton_get(world, Input) : NULL;
    if (!held) {
        goto done;
    }

    drag->action = action;
    drag->source = source[0];
    drag->start = held->start;
    drag->cursor = in ? in->mouse : held->start;
    result.type = ecs_id(ActionDrag);
    result.ptr = drag;
done:
    return result;
}

int32_t box_input_deliver(
    ecs_world_t *world,
    const ActionEvent *event,
    bool *consumed)
{
    ActionWaiters *waiters = box_input_waiters(world);
    Raise *raise = box_input_raise(world);
    Raise outer = raise ? raise[0] : (Raise){0};
    ecs_entity_t source = event->source;
    ActionDrag drag;
    int32_t i = 0, delivered = 0;
    ecs_os_zeromem(&drag);

    if (raise) {
        raise->target = event->action;
        raise->source = event->source;
        raise->priority = event->priority;
        raise->consumed = false;
    }

    while (waiters && i < ecs_vec_count(&waiters->waiters)) {
        box_waiter_t *waiter = ecs_vec_get_t(
            &waiters->waiters, box_waiter_t, i);
        if (waiter->action != event->action || waiter->phase != event->phase) {
            i ++;
            continue;
        }

        ecs_script_future_t *future = waiter->future;
        ecs_value_t value = box_input_value(
            world, event->action, event->phase, &source, &drag);
        ecs_vec_remove_ordered_t(&waiters->waiters, box_waiter_t, i);
        ecs_script_future_resolve(future, &value);
        ecs_script_future_release(future);
        delivered ++;
        if (raise && raise->consumed) {
            break;
        }
    }

    if (raise) {
        consumed[0] = raise->consumed;
        raise[0] = outer;
    }

    return delivered;
}

bool box_event_stop(
    ecs_world_t *world)
{
    Raise *raise = box_input_raise(world);
    bool result = raise != NULL && raise->target != 0 && !raise->consumed;
    if (result) {
        raise->consumed = true;
    }

    return result;
}

const Raise* box_event_current(
    const ecs_world_t *world)
{
    return box_input_raise(ECS_CONST_CAST(ecs_world_t*, world));
}

/** event.stop: take the raise the handler is running for.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments, of which there are none
 * @param result Receives whether a raise was stopped
 */
static void box_input_fn_stop(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;

    *(bool*)result->ptr = box_event_stop(ctx->world);
}

/** event.consumed: whether a handler already took the raise.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments, of which there are none
 * @param result Receives whether the raise was stopped
 */
static void box_input_fn_consumed(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const Raise *raise = box_event_current(ctx->world);
    (void)argc;
    (void)argv;

    *(bool*)result->ptr = raise != NULL && raise->consumed;
}

/** input.consume: take an action for the rest of the frame.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the action entity
 * @param result Receives whether the action was active
 */
static void box_input_fn_consume(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t action = *(ecs_entity_t*)argv[0].ptr;
    (void)argc;

    *(bool*)result->ptr = box_input_consume(ctx->world, action);
}

/** Register the event.stop, event.consumed and input.consume functions.
 *
 * @param world The world
 * @param module The box.input module entity
 */
static void box_input_functions_init(
    ecs_world_t *world,
    ecs_entity_t module)
{
    ecs_entity_t events = ecs_entity(world, {
        .name = "event", .parent = module
    });

    ecs_entity_t scope = ecs_entity(world, {
        .name = "input", .parent = module
    });

    ecs_function(world, {
        .name = "stop",
        .parent = events,
        .return_type = ecs_id(ecs_bool_t),
        .callback = box_input_fn_stop
    });

    ecs_function(world, {
        .name = "consumed",
        .parent = events,
        .return_type = ecs_id(ecs_bool_t),
        .callback = box_input_fn_consumed
    });

    ecs_function(world, {
        .name = "consume",
        .parent = scope,
        .return_type = ecs_id(ecs_bool_t),
        .params = {{ .name = "action", .type = ecs_id(ecs_entity_t) }},
        .callback = box_input_fn_consume
    });
}

void box_input_forget(
    ecs_world_t *world,
    ecs_entity_t action)
{
    ActionWaiters *waiters = box_input_waiters(world);
    int32_t i = 0;
    while (waiters && i < ecs_vec_count(&waiters->missed)) {
        if (ecs_vec_get_t(&waiters->missed, box_missed_t, i)->action == action) {
            ecs_vec_remove_ordered_t(&waiters->missed, box_missed_t, i);
        } else {
            i ++;
        }
    }
}

void box_input_events_init(
    ecs_world_t *world,
    ecs_entity_t module)
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
    for (i = 0; i < 4; i ++) {
        ecs_async_function(world, {
            .name = box_input_wait_names[i],
            .parent = on,
            .return_type = i == ActionDragged
                ? ecs_id(ActionDrag)
                : ecs_id(ecs_entity_t),
            .params = {{ "action", ecs_id(ecs_entity_t) }},
            .callback = box_input_wait,
            .cancel = box_input_wait_cancel,
            .ctx = (void*)(uintptr_t)i
        });
    }

    box_input_functions_init(world, module);
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
    missed->consumed = false;

done:
    return;
}

/** Drop the missed raises a waiter already resumed on in an earlier frame.
 *
 * @param waiters The waiters
 */
static void box_input_missed_expire(
    ActionWaiters *waiters)
{
    int32_t i = 0;
    while (i < ecs_vec_count(&waiters->missed)) {
        if (ecs_vec_get_t(&waiters->missed, box_missed_t, i)->consumed) {
            ecs_vec_remove_ordered_t(&waiters->missed, box_missed_t, i);
        } else {
            i ++;
        }
    }
}

void BoxInputRaise(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    ActionWaiters *waiters = box_input_waiters(world);
    if (waiters) {
        box_input_missed_expire(waiters);
    }

    const ActionEvent *events;
    int32_t i = 0;
    while (i < box_input_events(world, &events)) {
        ActionEvent event = events[i];
        bool consumed = false;
        int32_t delivered = box_input_deliver(world, &event, &consumed);
        if (waiters && event.phase != ActionDragged) {
            box_input_missed_update(waiters, event.action, event.phase,
                event.source, delivered);
        }

        if (consumed) {
            box_input_stop_below(world, event.mask, event.priority);
        }

        i ++;
    }
}
