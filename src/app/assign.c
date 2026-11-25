/**
 * @file app/assign.c
 * @brief The script function flecs.script.on.assign, which suspends an async block until an
 * exported mut variable of a scope is assigned.
 *
 * A block that derives state from exported mut variables awaits the next assignment instead of
 * evaluating every frame: an observer on the variables resolves the waits of their scope, and
 * costs nothing while nothing is assigned.
 */

#include "assign.h"

/** A script task waiting for an assignment */
typedef struct box_assign_waiter_t {
    ecs_script_future_t *future; /**< Future the assignment resolves */
    ecs_entity_t scope;          /**< The variable, or the scope of the variables, it waits for */
} box_assign_waiter_t;

/** The waits of a world */
typedef struct AppAssignWaits {
    ecs_vec_t waiters; /**< box_assign_waiter_t, in the order the tasks called on.assign */
} AppAssignWaits;

static ECS_COMPONENT_DECLARE(AppAssignWaits);

/** Get the waits of a world, NULL while it is torn down.
 *
 * @param world The world or a stage
 * @return The waits
 */
static AppAssignWaits* box_assign_waits(
    ecs_world_t *world)
{
    ecs_world_t *real = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    AppAssignWaits *result = NULL;
    if (ecs_id(AppAssignWaits) && !ecs_is_fini(real)) {
        result = ecs_singleton_get_mut(real, AppAssignWaits);
    }

    return result;
}

/** Release the futures of the waits.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_assign_waits_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AppAssignWaits *st = ptr;
    int32_t i, w;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_assign_waiter_t *waiters = ecs_vec_first_t(&st[i].waiters, box_assign_waiter_t);
        for (w = 0; w < ecs_vec_count(&st[i].waiters); w ++) {
            ecs_script_future_release(waiters[w].future);
        }

        ecs_vec_fini_t(NULL, &st[i].waiters, box_assign_waiter_t);
    }
}

/** Resolve the waits for a variable or a scope.
 *
 * @param st The waits
 * @param scope The variable or its scope
 */
static void box_assign_deliver(
    AppAssignWaits *st,
    ecs_entity_t scope)
{
    bool value = true;
    int32_t i = 0;
    while (i < ecs_vec_count(&st->waiters)) {
        box_assign_waiter_t w = *ecs_vec_get_t(&st->waiters, box_assign_waiter_t, i);
        if (w.scope != scope) {
            i ++;
            continue;
        }

        ecs_vec_remove_ordered_t(&st->waiters, box_assign_waiter_t, i);
        ecs_script_future_resolve(w.future,
            &(ecs_value_t){ .type = ecs_id(ecs_bool_t), .ptr = &value });
        ecs_script_future_release(w.future);
    }
}

/** An exported mut variable was assigned: resolve the waits for it and for its scope.
 *
 * @param it Iterator of the observer
 */
static void BoxAppOnAssign(
    ecs_iter_t *it)
{
    AppAssignWaits *st = box_assign_waits(it->world);
    int32_t i;
    for (i = 0; st && ecs_vec_count(&st->waiters) && i < it->count; i ++) {
        ecs_entity_t parent = ecs_get_parent(it->world, it->entities[i]);
        box_assign_deliver(st, it->entities[i]);
        if (parent) {
            box_assign_deliver(st, parent);
        }
    }
}

/** Suspend a task until an exported mut variable of a scope is assigned.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The scope entity
 * @param future Future the assignment resolves
 */
static void box_assign_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_entity_t scope = *(ecs_entity_t*)argv[0].ptr;
    AppAssignWaits *st = box_assign_waits(ctx->world);
    (void)argc;
    if (!scope || !st) {
        ecs_script_future_reject(future, "on.assign expects the scope of exported mut variables");
        ecs_script_future_release(future);
        goto done;
    }

    ecs_vec_append_t(NULL, &st->waiters, box_assign_waiter_t)[0] =
        (box_assign_waiter_t){ future, scope };
done:
    return;
}

/** Forget the wait of a cancelled task.
 *
 * @param ctx Call context
 * @param future The cancelled future
 */
static void box_assign_wait_cancel(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    AppAssignWaits *st = box_assign_waits(ctx->world);
    int32_t i, count = st ? ecs_vec_count(&st->waiters) : 0;
    for (i = 0; i < count; i ++) {
        if (ecs_vec_get_t(&st->waiters, box_assign_waiter_t, i)->future == future) {
            ecs_script_future_release(future);
            ecs_vec_remove_ordered_t(&st->waiters, box_assign_waiter_t, i);
            break;
        }
    }
}

void box_app_assign_init(
    ecs_world_t *world)
{
    AppAssignWaits *st;
    ecs_set_name_prefix(world, "App");
    ECS_COMPONENT_DEFINE(world, AppAssignWaits);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, AppAssignWaits, {
        .ctor = flecs_default_ctor,
        .dtor = box_assign_waits_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    st = ecs_singleton_ensure(world, AppAssignWaits);
    ecs_vec_init_t(NULL, &st->waiters, box_assign_waiter_t, 0);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OnAssign" }),
        .query.terms = {{ .id = ecs_id(EcsScriptMutVar), .src.id = EcsSelf }},
        .events = { EcsOnSet },
        .callback = BoxAppOnAssign
    });

    ecs_async_function(world, {
        .name = "assign",
        .parent = ecs_entity(world, { .name = "on", .parent = ecs_lookup(world, "flecs.script") }),
        .return_type = ecs_id(ecs_bool_t),
        .params = {{ "scope", ecs_id(ecs_entity_t) }},
        .callback = box_assign_wait,
        .cancel = box_assign_wait_cancel
    });
}
