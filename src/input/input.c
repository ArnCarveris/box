/**
 * @file input/input.c
 * @brief Registers the box.input module: the Input singleton, the queues and the systems.
 */

#define BOX_INPUT_IMPL
#include "input_internal.h"

ECS_COMPONENT_DECLARE(InputState);

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
    }
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
    ECS_COMPONENT_DEFINE(world, InputState);

    ecs_set_hooks(world, InputState, {
        .ctor = flecs_default_ctor,
        .dtor = box_input_state_dtor
    });

    ecs_set_hooks(world, Input, { .ctor = flecs_default_ctor });

    box_input_keys_init(world);

    InputState *state = ecs_singleton_ensure(world, InputState);
    ecs_vec_init_t(NULL, &state->platform, InputEvent, 0);
    ecs_vec_init_t(NULL, &state->injected, InputEvent, 0);
    ecs_vec_init_t(NULL, &state->scheduled, InputEvent, 0);

    Input *in = ecs_singleton_ensure(world, Input);
    in->focused = true;
    ecs_singleton_modified(world, Input);

    ECS_SYSTEM(world, BoxInputBind, EcsPreFrame, [in] Surface, [in] SurfaceImpl);
    ECS_SYSTEM(world, BoxInputUpdate, EcsOnLoad, 0);
}
