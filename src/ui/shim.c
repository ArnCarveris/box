/**
 * @file ui/shim.c
 * @brief Temporary: on.activate, which flecs script lacks.
 *
 * FlecsScriptActivate adds it to flecs; FlecsActivateAdoption then removes this file. The focus
 * model only calls box_ui_shim_activate.
 */

#include "ui.h"

void box_ui_shim_activate(
    ecs_world_t *world,
    box_ui_state_t *st,
    const UiActivateEvent *evt)
{
    box_ui_deliver(world, st, BoxUiWaitActivate, evt->target, ecs_id(UiActivateEvent), evt);
}

void box_ui_shim_import(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t on)
{
    box_ui_wait_function(world, st, on, "activate", BoxUiWaitActivate, ecs_id(UiActivateEvent));
}
