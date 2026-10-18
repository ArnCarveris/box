/**
 * @file ui/shim.c
 * @brief Temporary: on.focus, on.blur, on.key, on.activate and ui.focus, which flecs script lacks.
 *
 * FlecsScriptFocus and FlecsScriptActivate add them to flecs; FlecsFocusAdoption and
 * FlecsActivateAdoption then remove this file. The focus model only calls the box_ui_shim_ ones.
 */

#include "ui.h"

void box_ui_shim_focus(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t from,
    bool from_visible,
    ecs_entity_t to,
    bool to_visible)
{
    UiFocusEvent blur = { .target = from, .visible = from_visible };
    UiFocusEvent focus = { .target = to, .visible = to_visible };
    if (from) {
        box_ui_deliver(world, st, BoxUiWaitBlur, from, ecs_id(UiFocusEvent), &blur);
    }

    if (to) {
        box_ui_deliver(world, st, BoxUiWaitFocus, to, ecs_id(UiFocusEvent), &focus);
    }
}

void box_ui_shim_key(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t focused,
    const UiKeyEvent *evt)
{
    box_ui_deliver(world, st, BoxUiWaitKey, focused, ecs_id(UiKeyEvent), evt);
}

void box_ui_shim_activate(
    ecs_world_t *world,
    box_ui_state_t *st,
    const UiActivateEvent *evt)
{
    box_ui_deliver(world, st, BoxUiWaitActivate, evt->target, ecs_id(UiActivateEvent), evt);
}

/** Move the focus to the entity of the argument from an async block, visible after Tab, and
 * resolve at once with the focused entity. The tasks on.blur and on.focus resolve resume after
 * the calling task.
 *
 * @param ctx Call context, whose ctx is the module state
 * @param argc Number of arguments
 * @param argv The entity, 0 clears the focus
 * @param future Future of the call
 */
static void box_ui_shim_focus_call(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    box_ui_state_t *st = ctx->ctx;
    ecs_entity_t e = *(ecs_entity_t*)argv[0].ptr;
    ecs_world_t *world = ctx->world;
    (void)argc;
    if (!e || box_ui_can_focus(world, e)) {
        box_ui_focus_set(world, st, e, st->keyboard);
    }

    ecs_script_future_resolve(future, &(ecs_value_t){
        .type = ecs_id(ecs_entity_t), .ptr = &st->focused });
    ecs_script_future_release(future);
}

void box_ui_shim_import(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t on,
    ecs_entity_t ui)
{
    box_ui_wait_function(world, st, on, "focus", BoxUiWaitFocus, ecs_id(UiFocusEvent));
    box_ui_wait_function(world, st, on, "blur", BoxUiWaitBlur, ecs_id(UiFocusEvent));
    box_ui_wait_function(world, st, on, "key", BoxUiWaitKey, ecs_id(UiKeyEvent));
    box_ui_wait_function(world, st, on, "activate", BoxUiWaitActivate, ecs_id(UiActivateEvent));
    ecs_async_function(world, {
        .name = "focus",
        .parent = ui,
        .return_type = ecs_id(ecs_entity_t),
        .params = {{ "entity", ecs_id(ecs_entity_t) }},
        .callback = box_ui_shim_focus_call,
        .ctx = st
    });
}
