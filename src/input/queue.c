/**
 * @file input/queue.c
 * @brief The event queues that the platform layer and injection push into.
 */

#include "input_internal.h"

_Static_assert((int)InputKeyDown == (int)BoxInputKeyDown &&
    (int)InputMinimize == (int)BoxInputMinimize,
    "InputEventKind must mirror box_input_kind_t");

InputState* box_input_state(
    ecs_world_t *world)
{
    InputState *state = ecs_get_mut(world, ecs_id(InputState), InputState);
    ecs_assert(state != NULL, ECS_INVALID_OPERATION, "box.input is not imported");

    return state;
}

Raise* box_input_raise(
    ecs_world_t *world)
{
    Raise *result = NULL;
    if (ecs_id(Raise) && !ecs_is_fini(world)) {
        result = ecs_get_mut(world, ecs_id(Raise), Raise);
    }

    return result;
}

void box_input_sink(
    void *ctx,
    const box_input_event_t *event)
{
    InputState *state = ctx;
    InputEvent *ev = ecs_vec_append_t(NULL, &state->platform, InputEvent);
    ev->kind = (InputEventKind)event->kind;
    ev->source = InputSourcePlatform;
    ev->code = event->code;
    ev->value.x = event->x;
    ev->value.y = event->y;
    ev->delay = 0;
    ev->duration = 0;
}

void box_input_inject(
    ecs_world_t *world,
    const InputEvent *event)
{
    InputState *state = box_input_state(world);
    ecs_vec_t *queue = &state->injected;
    if (event->source == InputSourcePlatform) {
        queue = &state->platform;
    }

    ecs_vec_append_t(NULL, queue, InputEvent)[0] = *event;
}

void box_input_cursor_pin(
    ecs_world_t *world,
    bool pinned)
{
    box_input_state(world)->cursor_pinned = pinned;
}

void box_input_reset(
    ecs_world_t *world)
{
    InputState *state = box_input_state(world);
    ecs_vec_clear(&state->platform);
    ecs_vec_clear(&state->injected);
    ecs_vec_clear(&state->scheduled);
    box_input_clear_keys(ecs_get_mut(world, ecs_id(Input), Input));
}

int32_t box_input_pending(
    ecs_world_t *world)
{
    InputState *state = box_input_state(world);

    return ecs_vec_count(&state->platform) + ecs_vec_count(&state->injected);
}
