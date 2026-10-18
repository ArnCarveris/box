/**
 * @file input/queue.c
 * @brief The platform push and the synthetic queue: keys, typing, cursor walks, focus, actions.
 */

#include "input.h"

/** Queue a key event.
 *
 * @param world The world
 * @param kind InputKeyDown or InputKeyUp
 * @param key The key or button
 * @param delay Frames the event waits after the one before it
 */
static void box_input_queue_key(
    ecs_world_t *world,
    InputEventKind kind,
    Key key,
    int32_t delay)
{
    box_input_queue(world, &(InputEvent){ .kind = kind, .key = key, .delay = delay });
}

/** Queue a cursor move.
 *
 * @param world The world
 * @param x Cursor x
 * @param y Cursor y
 * @param normalized Whether x and y are in 0..1 of the window size
 * @param delay Frames the move waits after the event before it
 */
static void box_input_queue_move(
    ecs_world_t *world,
    float x,
    float y,
    bool normalized,
    int32_t delay)
{
    box_input_queue(world, &(InputEvent){
        .kind = InputMove,
        .value = { x, y },
        .normalized = normalized,
        .delay = delay
    });
}

/** Queue a cursor walk, holding a button or an action during it when one is given.
 *
 * @param world The world
 * @param walk The walk
 * @param button Button held during the walk, KeyNone for none
 * @param action Action held during the walk, 0 for none
 */
static void box_input_walk(
    ecs_world_t *world,
    const box_input_sweep_t *walk,
    Key button,
    ecs_entity_t action)
{
    int32_t steps = walk->steps > 0 ? walk->steps : BOX_INPUT_SWEEP_STEPS;
    int32_t frames = walk->frames > 0 ? walk->frames : steps;
    int32_t i, prev = 0;
    box_input_queue_move(world, walk->from[0], walk->from[1], walk->normalized, 0);
    if (button) {
        box_input_queue_key(world, InputKeyDown, button, 0);
    }

    if (action) {
        box_input_queue(world, &(InputEvent){ .kind = InputActionDown, .action = action });
    }

    for (i = 1; i <= steps; i ++) {
        int32_t frame = (i * frames + steps - 1) / steps;
        float t = (float)i / (float)steps;
        float x = walk->from[0] + (walk->to[0] - walk->from[0]) * t;
        float y = walk->from[1] + (walk->to[1] - walk->from[1]) * t;
        box_input_queue_move(world, x, y, walk->normalized, frame - prev);
        prev = frame;
    }

    if (button) {
        box_input_queue_key(world, InputKeyUp, button, 1);
    }

    if (action) {
        box_input_queue(world, &(InputEvent){
            .kind = InputActionUp, .action = action, .delay = 1 });
    }
}

void box_input_platform_push(
    ecs_world_t *world,
    const InputEvent *event)
{
    InputDevice *dev = box_input_device(world);
    InputEvent *ev = ecs_vec_append_t(NULL, &dev->platform, InputEvent);
    *ev = *event;
    ev->delay = 0;
    ev->surface = ev->surface ? ev->surface : dev->source;
    ev->text = event->text ? ecs_os_strdup(event->text) : NULL;
}

void box_input_platform_source(
    ecs_world_t *world,
    ecs_entity_t surface)
{
    box_input_device(world)->source = surface;
}

void box_input_queue(
    ecs_world_t *world,
    const InputEvent *event)
{
    InputDevice *dev = box_input_device(world);
    InputEvent *ev = ecs_vec_append_t(NULL, &dev->queue, InputEvent);
    *ev = *event;
    ev->delay = ev->delay > 0 ? ev->delay : 0;
    ev->text = event->text ? ecs_os_strdup(event->text) : NULL;
}

void box_input_key(
    ecs_world_t *world,
    Key key,
    bool down)
{
    box_input_queue_key(world, down ? InputKeyDown : InputKeyUp, key, 0);
}

void box_input_key_repeat(
    ecs_world_t *world,
    Key key)
{
    box_input_queue(world, &(InputEvent){ .kind = InputKeyDown, .key = key, .repeat = true });
}

void box_input_key_press(
    ecs_world_t *world,
    Key key,
    int32_t frames)
{
    box_input_queue_key(world, InputKeyDown, key, 0);
    box_input_queue_key(world, InputKeyUp, key, frames > 0 ? frames : 1);
}

int32_t box_input_key_type(
    ecs_world_t *world,
    const char *text,
    int32_t frames)
{
    int32_t result = 0, delay = 0;
    frames = frames > 0 ? frames : 1;
    while (text && *text) {
        int32_t code = box_utf8_next(&text);
        Key key = box_input_key_from_char(code);
        if (key) {
            box_input_queue_key(world, InputKeyDown, key, delay);
            delay = 0;
        }

        box_input_queue(world, &(InputEvent){
            .kind = InputText, .code = code, .delay = delay });
        if (key) {
            box_input_queue_key(world, InputKeyUp, key, frames);
        } else {
            delay = frames;
        }

        result ++;
    }

    return result;
}

void box_input_mouse_move(
    ecs_world_t *world,
    float x,
    float y,
    bool normalized)
{
    box_input_queue_move(world, x, y, normalized, 0);
}

void box_input_mouse_sweep(
    ecs_world_t *world,
    const box_input_sweep_t *sweep)
{
    box_input_walk(world, sweep, KeyNone, 0);
}

void box_input_mouse_release(
    ecs_world_t *world)
{
    box_input_queue(world, &(InputEvent){ .kind = InputRelease });
}

void box_input_mouse_click(
    ecs_world_t *world,
    Key button,
    const float *pos,
    bool normalized,
    int32_t frames)
{
    button = button ? button : KeyMouseLeft;
    if (pos) {
        box_input_queue_move(world, pos[0], pos[1], normalized, 0);
    }

    box_input_key_press(world, button, frames);
}

void box_input_mouse_drag(
    ecs_world_t *world,
    const box_input_sweep_t *drag)
{
    box_input_walk(world, drag, drag->button ? drag->button : KeyMouseLeft, 0);
}

void box_input_action(
    ecs_world_t *world,
    ecs_entity_t action,
    bool down)
{
    box_input_queue(world, &(InputEvent){
        .kind = down ? InputActionDown : InputActionUp, .action = action });
}

void box_input_action_press(
    ecs_world_t *world,
    ecs_entity_t action,
    int32_t frames)
{
    box_input_queue(world, &(InputEvent){ .kind = InputActionDown, .action = action });
    box_input_queue(world, &(InputEvent){
        .kind = InputActionUp, .action = action, .delay = frames > 0 ? frames : 1 });
}

void box_input_action_drag(
    ecs_world_t *world,
    ecs_entity_t action,
    const box_input_sweep_t *drag)
{
    box_input_walk(world, drag, KeyNone, action);
}

void box_input_mouse_scroll(
    ecs_world_t *world,
    float dx,
    float dy)
{
    box_input_queue(world, &(InputEvent){ .kind = InputScroll, .value = { dx, dy } });
}

void box_input_focus(
    ecs_world_t *world,
    bool focused)
{
    box_input_queue(world, &(InputEvent){ .kind = InputFocus, .code = focused });
}

void box_input_reset(
    ecs_world_t *world)
{
    InputDevice *dev = box_input_device(world);
    box_input_events_clear(&dev->platform);
    box_input_events_clear(&dev->queue);
    box_input_queue(world, &(InputEvent){ .kind = InputReset });
}

int32_t box_input_pending(
    ecs_world_t *world)
{
    return ecs_vec_count(&box_input_device(world)->queue);
}

int32_t box_input_pending_frames(
    ecs_world_t *world)
{
    InputDevice *dev = box_input_device(world);
    const InputEvent *events = ecs_vec_first_t(&dev->queue, InputEvent);
    int32_t i, count = ecs_vec_count(&dev->queue);
    int32_t result = count ? 1 : 0;
    for (i = 0; i < count; i ++) {
        result += events[i].delay;
    }

    return result;
}
