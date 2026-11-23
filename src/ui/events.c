/**
 * @file ui/events.c
 * @brief The waiters of the box.ui script events, their delivery along the ancestors of a target,
 * the widget reports on.change and on.submit with ui.changed and ui.submit, and ui.delay.
 */

#include "ui.h"

/** Names of the waiting functions in flecs.script.on, indexed by kind */
static const char *box_ui_wait_names[BoxUiWaitCount] = {
    "change", "submit"
};

/** Resolve the waiters of an event on one entity.
 *
 * @param st The module state
 * @param kind What the event is
 * @param target The entity
 * @param type Type of the value
 * @param value The value
 * @return Number of resolved waiters
 */
static int32_t box_ui_deliver_one(
    box_ui_state_t *st,
    box_ui_wait_kind_t kind,
    ecs_entity_t target,
    ecs_entity_t type,
    const void *value)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&st->waiters); ) {
        box_ui_waiter_t w = *ecs_vec_get_t(&st->waiters, box_ui_waiter_t, i);
        if (w.kind != kind || w.target != target) {
            i ++;
            continue;
        }

        ecs_vec_remove_ordered_t(&st->waiters, box_ui_waiter_t, i);
        ecs_script_future_resolve(w.future, &(ecs_value_t){
            .type = type, .ptr = ECS_CONST_CAST(void*, value) });
        ecs_script_future_release(w.future);
        result ++;
    }

    return result;
}

int32_t box_ui_deliver(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_ui_wait_kind_t kind,
    ecs_entity_t target,
    ecs_entity_t type,
    const void *value)
{
    int32_t depth = 0, result = 0;
    while (target && depth < BOX_UI_DEPTH_MAX && ecs_is_alive(world, target)) {
        result += box_ui_deliver_one(st, kind, target, type, value);
        target = ecs_get_parent(world, target);
        depth ++;
    }

    st->progress = st->progress || result > 0;

    return result;
}

void box_ui_progress(
    ecs_world_t *world,
    box_ui_state_t *st)
{
    int32_t rounds = 0;
    while (st->progress && rounds < BOX_UI_PROGRESS_MAX) {
        st->progress = ecs_script_tasks_progress(world) > 0;
        rounds ++;
    }
}

/** Suspend a task until an event of the kind of the function reaches its entity.
 *
 * @param ctx Call context, whose ctx is the box_ui_wait_ctx_t of the function
 * @param argc Number of arguments
 * @param argv The entity
 * @param future Future the event resolves
 */
static void box_ui_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    const box_ui_wait_ctx_t *wait = ctx->ctx;
    ecs_entity_t target = *(ecs_entity_t*)argv[0].ptr;
    char msg[64];
    (void)argc;
    if (!target) {
        ecs_os_snprintf(msg, sizeof(msg), "on.%s expects an entity",
            box_ui_wait_names[wait->kind]);
        ecs_script_future_reject(future, msg);
        ecs_script_future_release(future);
    } else {
        ecs_vec_append_t(NULL, &wait->st->waiters, box_ui_waiter_t)[0] = (box_ui_waiter_t){
            .target = target, .kind = wait->kind, .future = future };
    }
}

/** Drop the waiter of a cancelled task.
 *
 * @param ctx Call context, whose ctx is the box_ui_wait_ctx_t of the function
 * @param future The cancelled future
 */
static void box_ui_wait_cancel(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    const box_ui_wait_ctx_t *wait = ctx->ctx;
    box_ui_state_t *st = wait->st;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&st->waiters); i ++) {
        if (ecs_vec_get_t(&st->waiters, box_ui_waiter_t, i)->future == future) {
            ecs_script_future_release(future);
            ecs_vec_remove_ordered_t(&st->waiters, box_ui_waiter_t, i);
            break;
        }
    }
}

void box_ui_wait_function(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t parent,
    const char *name,
    box_ui_wait_kind_t kind,
    ecs_entity_t type)
{
    ecs_async_function(world, {
        .name = name,
        .parent = parent,
        .return_type = type,
        .params = {{ "target", ecs_id(ecs_entity_t) }},
        .callback = box_ui_wait,
        .cancel = box_ui_wait_cancel,
        .ctx = &st->waits[kind]
    });
}

/** Report a change or a submit of the widget of the argument and resolve at once.
 *
 * The tasks it resolves resume with the next progress of the tasks, after the task that reports.
 *
 * @param ctx Call context, whose ctx is the box_ui_wait_ctx_t of on.change or on.submit
 * @param argc Number of arguments
 * @param argv The widget
 * @param future Future of the call, resolved with the widget
 */
static void box_ui_report(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    const box_ui_wait_ctx_t *wait = ctx->ctx;
    ecs_entity_t widget = *(ecs_entity_t*)argv[0].ptr;
    UiChangeEvent evt = { .widget = widget };
    (void)argc;
    if (!widget) {
        ecs_script_future_reject(future, wait->kind == BoxUiWaitChange ?
            "ui.changed expects an entity" : "ui.submit expects an entity");
    } else {
        box_ui_deliver(ctx->world, wait->st, wait->kind, widget, ecs_id(UiChangeEvent), &evt);
        ecs_script_future_resolve(future, &(ecs_value_t){
            .type = ecs_id(ecs_entity_t), .ptr = &widget });
    }

    ecs_script_future_release(future);
}

/** Suspend a task for a number of seconds of world time.
 *
 * @param ctx Call context, whose ctx is the module state
 * @param argc Number of arguments
 * @param argv The seconds
 * @param future Future the end of the wait resolves with the seconds of world time that passed,
 * at least the seconds asked for
 */
static void box_ui_delay(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    box_ui_state_t *st = ctx->ctx;
    float seconds = *(float*)argv[0].ptr;
    (void)argc;
    ecs_vec_append_t(NULL, &st->timers, box_ui_timer_t)[0] = (box_ui_timer_t){
        .future = future, .seconds = seconds, .left = seconds };
}

/** Drop the timer of a cancelled task.
 *
 * @param ctx Call context, whose ctx is the module state
 * @param future The cancelled future
 */
static void box_ui_delay_cancel(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    box_ui_state_t *st = ctx->ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&st->timers); i ++) {
        if (ecs_vec_get_t(&st->timers, box_ui_timer_t, i)->future == future) {
            ecs_script_future_release(future);
            ecs_vec_remove_ordered_t(&st->timers, box_ui_timer_t, i);
            break;
        }
    }
}

/** Count down the timers of ui.delay by the frame time and resolve the ones that ran out, before
 * flecs resumes the tasks in PreUpdate.
 *
 * @param it The iterator, whose ctx is the module state
 */
static void BoxUiDelayUpdate(
    ecs_iter_t *it)
{
    box_ui_state_t *st = it->ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&st->timers); ) {
        box_ui_timer_t *t = ecs_vec_get_t(&st->timers, box_ui_timer_t, i);
        t->left -= it->delta_time;
        if (t->left > 0) {
            i ++;
            continue;
        }

        box_ui_timer_t done = *t;
        float passed = done.seconds - done.left;
        ecs_vec_remove_ordered_t(&st->timers, box_ui_timer_t, i);
        ecs_script_future_resolve(done.future, &(ecs_value_t){
            .type = ecs_id(ecs_f32_t), .ptr = &passed });
        ecs_script_future_release(done.future);
    }
}

void box_ui_events_import(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t on,
    ecs_entity_t ui)
{
    box_ui_wait_function(world, st, on, "change", BoxUiWaitChange, ecs_id(UiChangeEvent));
    box_ui_wait_function(world, st, on, "submit", BoxUiWaitSubmit, ecs_id(UiChangeEvent));
    ecs_async_function(world, {
        .name = "changed",
        .parent = ui,
        .return_type = ecs_id(ecs_entity_t),
        .params = {{ "widget", ecs_id(ecs_entity_t) }},
        .callback = box_ui_report,
        .ctx = &st->waits[BoxUiWaitChange]
    });
    ecs_async_function(world, {
        .name = "submit",
        .parent = ui,
        .return_type = ecs_id(ecs_entity_t),
        .params = {{ "widget", ecs_id(ecs_entity_t) }},
        .callback = box_ui_report,
        .ctx = &st->waits[BoxUiWaitSubmit]
    });
    ecs_async_function(world, {
        .name = "delay",
        .parent = ui,
        .return_type = ecs_id(ecs_f32_t),
        .params = {{ "seconds", ecs_id(ecs_f32_t) }},
        .callback = box_ui_delay,
        .cancel = box_ui_delay_cancel,
        .ctx = st
    });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "DelayUpdate" }),
        .phase = EcsOnLoad,
        .run = BoxUiDelayUpdate,
        .ctx = st,
        .immediate = true
    });
}
