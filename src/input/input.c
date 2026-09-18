/**
 * @file input/input.c
 * @brief Registers the box.input module: the singletons, the queues and the systems.
 */

#define BOX_INPUT_IMPL
#include "input_internal.h"

ECS_COMPONENT_DECLARE(InputState);
ECS_COMPONENT_DECLARE(ActionEvents);

/** Release the event queues of the module.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_input_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    InputState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &state[i].platform, InputEvent);
        ecs_vec_fini_t(NULL, &state[i].injected, InputEvent);
        ecs_vec_fini_t(NULL, &state[i].scheduled, InputEvent);
        ecs_vec_fini_t(NULL, &state[i].bound, box_binding_t);
        ecs_vec_fini_t(NULL, &state[i].active, box_active_t);
        ecs_vec_fini_t(NULL, &state[i].holds, box_hold_t);
        box_input_queries_fini(&state[i]);
    }
}

/** Release the event list of the frame.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_input_events_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ActionEvents *events = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &events[i].events, ActionEvent);
    }
}

/** Enable a map when it is added, so that a script only has to name it.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_input_map_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    InputMap *map = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        map[i].enabled = true;
    }
}

/** Register the action scope and the default map of the world.
 *
 * @param world The world
 */
static void box_input_map_init(
    ecs_world_t *world)
{
    ecs_entity_t scope = ecs_set_scope(world, 0);
    ecs_entity(world, { .name = "actions" });
    ecs_set_scope(world, scope);
    ecs_entity_t map = ecs_entity(world, { .name = "map" });
    ecs_add(world, map, InputMap);
}

void BoxInputImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxInput);
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);

    ECS_META_COMPONENT(world, InputEventKind);
    ECS_META_COMPONENT(world, InputSource);
    ECS_META_COMPONENT(world, InputEvent);
    ECS_META_COMPONENT(world, KeyState);
    ECS_META_COMPONENT(world, Mouse);
    ECS_META_COMPONENT(world, Input);
    ECS_META_COMPONENT(world, InputAction);
    ECS_META_COMPONENT(world, InputMap);
    ECS_META_COMPONENT(world, Binding);
    ECS_META_COMPONENT(world, ActionPhase);
    ECS_META_COMPONENT(world, ActionEvent);
    ECS_COMPONENT_DEFINE(world, InputState);
    ECS_COMPONENT_DEFINE(world, ActionEvents);

    ecs_set_hooks(world, InputState, {
        .ctor = flecs_default_ctor,
        .dtor = box_input_state_dtor
    });

    ecs_set_hooks(world, Input, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Binding, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, InputMap, { .ctor = box_input_map_ctor });

    ecs_set_hooks(world, ActionEvents, {
        .ctor = flecs_default_ctor,
        .dtor = box_input_events_dtor
    });

    InputState *state = ecs_singleton_ensure(world, InputState);
    state->keys = box_input_keys_init(world);

    ecs_vec_init_t(NULL, &state->platform, InputEvent, 0);
    ecs_vec_init_t(NULL, &state->injected, InputEvent, 0);
    ecs_vec_init_t(NULL, &state->scheduled, InputEvent, 0);
    ecs_vec_init_t(NULL, &state->holds, box_hold_t, 0);
    box_input_actions_init(world, state);
    box_input_map_init(world);

    ActionEvents *events = ecs_singleton_ensure(world, ActionEvents);
    ecs_vec_init_t(NULL, &events->events, ActionEvent, 0);
    box_input_events_init(world);

    Input *in = ecs_singleton_ensure(world, Input);
    in->focused = true;
    ecs_singleton_modified(world, Input);

    ECS_SYSTEM(world, BoxInputBind, EcsPreFrame, [in] Surface, [in] SurfaceImpl);
    ECS_SYSTEM(world, BoxInputUpdate, EcsOnLoad, 0);
    ECS_SYSTEM(world, BoxInputActions, EcsOnLoad, 0);
    ECS_SYSTEM(world, BoxInputRaise, EcsOnLoad, 0);
}
