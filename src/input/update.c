/**
 * @file input/update.c
 * @brief The OnLoad system that applies the device events to Input and emits script events.
 */

#include "input.h"
#include <string.h>

/** Make room for a number of bytes in the text of the frame, the terminating zero included.
 *
 * @param dev The device state, which tracks the capacity of the buffer
 * @param in The Input singleton
 * @param size Bytes the text needs
 */
static void box_input_text_reserve(
    InputDevice *dev,
    Input *in,
    int32_t size)
{
    int32_t capacity;
    if (!in->text || in->text != dev->text) {
        dev->text_capacity = in->text ? (int32_t)strlen(in->text) + 1 : 0;
    }

    if (size > dev->text_capacity) {
        capacity = dev->text_capacity * 2 > size ? dev->text_capacity * 2 : size;
        if (capacity < 32) {
            capacity = 32;
        }

        in->text = ecs_os_realloc(in->text, capacity);
        if (!dev->text_capacity) {
            in->text[0] = '\0';
        }

        dev->text_capacity = capacity;
    }

    dev->text = in->text;
}

/** Clear the state that only describes one frame.
 *
 * @param dev The device state
 * @param in The Input singleton
 */
static void box_input_frame_begin(
    InputDevice *dev,
    Input *in)
{
    int32_t i;
    for (i = 0; i < BOX_KEY_COUNT; i ++) {
        in->keys[i].pressed = false;
        in->keys[i].released = false;
    }

    box_input_text_reserve(dev, in, 1);
    in->text[0] = '\0';
    in->mouse.delta[0] = in->mouse.delta[1] = 0;
    in->mouse.scroll[0] = in->mouse.scroll[1] = 0;
}

/** Check whether either key of a modifier is held.
 *
 * @param in The Input singleton
 * @param left The left key of the modifier
 * @param right The right key of the modifier
 * @return Whether one of the keys is held
 */
static bool box_input_modifier(
    const Input *in,
    Key left,
    Key right)
{
    return in->keys[left].down || in->keys[right].down;
}

/** Collect the held mouse buttons as the bitmask of a script mouse event.
 *
 * @param in The Input singleton
 * @return Bit 0 for the left, bit 1 for the middle and bit 2 for the right button
 */
static uint32_t box_input_buttons(
    const Input *in)
{
    uint32_t result = 0;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        if (in->keys[KeyMouseLeft + i].down) {
            result |= 1u << i;
        }
    }

    return result;
}

/** Append a mouse event at the screen to the events of the frame.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param button Button that changed state, 0 for a move
 */
static void box_input_emit_mouse(
    InputDevice *dev,
    const Input *in,
    int32_t button)
{
    box_input_emit_t *emit = ecs_vec_append_t(NULL, &dev->emits, box_input_emit_t);
    ecs_os_zeromem(emit);
    emit->is_mouse = true;
    emit->mouse = (EcsScriptMouseEvent){
        .target = dev->screen,
        .screen_x = in->mouse.position[0],
        .screen_y = in->mouse.position[1],
        .local_x = in->mouse.position[0],
        .local_y = in->mouse.position[1],
        .button = button,
        .buttons = box_input_buttons(in),
        .ctrl = box_input_modifier(in, KeyCtrlLeft, KeyCtrlRight),
        .shift = box_input_modifier(in, KeyShiftLeft, KeyShiftRight),
        .alt = box_input_modifier(in, KeyAltLeft, KeyAltRight),
        .meta = box_input_modifier(in, KeySuperLeft, KeySuperRight)
    };
}

/** Append a wheel movement at the cursor to the events of the frame.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param ev The scroll event, its value is the wheel offset
 */
static void box_input_emit_scroll(
    InputDevice *dev,
    const Input *in,
    const InputEvent *ev)
{
    box_input_emit_mouse(dev, in, 0);
    box_input_emit_t *emit = ecs_vec_last_t(&dev->emits, box_input_emit_t);
    emit->is_scroll = true;
    emit->mouse.delta_x = ev->value[0];
    emit->mouse.delta_y = ev->value[1];
}

/** Append the event of a key or button change to the events of the frame.
 *
 * @param dev The device state
 * @param in The Input singleton, already holding the new key state
 * @param key The key or button
 * @param down Whether the key went down
 * @param repeat Whether the key was already held
 */
static void box_input_emit_key(
    InputDevice *dev,
    const Input *in,
    Key key,
    bool down,
    bool repeat)
{
    const char *value = box_input_key_value(key);
    box_input_emit_t *emit;
    if (!value) {
        box_input_emit_mouse(dev, in, (int32_t)(key - KeyMouseLeft));
        goto done;
    }

    emit = ecs_vec_append_t(NULL, &dev->emits, box_input_emit_t);
    ecs_os_zeromem(emit);
    emit->keyboard = (EcsScriptKeyboardEvent){
        .key = ECS_CONST_CAST(char*, value),
        .down = down,
        .repeat = repeat,
        .ctrl = box_input_modifier(in, KeyCtrlLeft, KeyCtrlRight),
        .shift = box_input_modifier(in, KeyShiftLeft, KeyShiftRight),
        .alt = box_input_modifier(in, KeyAltLeft, KeyAltRight),
        .meta = box_input_modifier(in, KeySuperLeft, KeySuperRight)
    };
done:
    return;
}

/** Put a key or button down, emitting a repeat for a held key that the platform repeats.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param key The key or button
 * @param repeat Whether the platform repeats a held key
 */
static void box_input_key_down(
    InputDevice *dev,
    Input *in,
    Key key,
    bool repeat)
{
    KeyState *state;
    bool held;
    if (key <= KeyNone || key >= BOX_KEY_COUNT) {
        goto done;
    }

    state = &in->keys[key];
    held = state->down;
    state->pressed = state->pressed || !held;
    state->down = true;
    if (!held || (repeat && box_input_key_value(key))) {
        box_input_emit_key(dev, in, key, true, held);
    }

done:
    return;
}

/** Release a held key or button.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param key The key or button
 */
static void box_input_key_up(
    InputDevice *dev,
    Input *in,
    Key key)
{
    if (key > KeyNone && key < BOX_KEY_COUNT && in->keys[key].down) {
        in->keys[key].down = false;
        in->keys[key].released = true;
        box_input_emit_key(dev, in, key, false, false);
    }
}

/** Release every held key and button.
 *
 * @param dev The device state
 * @param in The Input singleton
 */
static void box_input_release_all(
    InputDevice *dev,
    Input *in)
{
    int32_t i;
    for (i = KeyNone + 1; i < BOX_KEY_COUNT; i ++) {
        box_input_key_up(dev, in, (Key)i);
    }
}

/** Append a code point to the text of the frame as UTF-8, growing the text as needed.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param code The unicode code point
 */
static void box_input_text(
    InputDevice *dev,
    Input *in,
    int32_t code)
{
    char utf8[4];
    int32_t size = 0;
    size_t len = strlen(in->text);
    if (code < 0x80) {
        utf8[size ++] = (char)code;
    } else if (code < 0x800) {
        utf8[size ++] = (char)(0xc0 | (code >> 6));
        utf8[size ++] = (char)(0x80 | (code & 0x3f));
    } else if (code < 0x10000) {
        utf8[size ++] = (char)(0xe0 | (code >> 12));
        utf8[size ++] = (char)(0x80 | ((code >> 6) & 0x3f));
        utf8[size ++] = (char)(0x80 | (code & 0x3f));
    } else {
        utf8[size ++] = (char)(0xf0 | (code >> 18));
        utf8[size ++] = (char)(0x80 | ((code >> 12) & 0x3f));
        utf8[size ++] = (char)(0x80 | ((code >> 6) & 0x3f));
        utf8[size ++] = (char)(0x80 | (code & 0x3f));
    }

    if (code > 0) {
        box_input_text_reserve(dev, in, (int32_t)len + size + 1);
        memcpy(&in->text[len], utf8, (size_t)size);
        in->text[len + (size_t)size] = '\0';
    }
}

/** Move the cursor, adding the movement to the delta of the frame.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param ev The move event
 */
static void box_input_move(
    InputDevice *dev,
    Input *in,
    const InputEvent *ev)
{
    float x = ev->value[0], y = ev->value[1];
    if (ev->normalized) {
        x *= in->size[0];
        y *= in->size[1];
    }

    if (dev->has_position) {
        in->mouse.delta[0] += x - in->mouse.position[0];
        in->mouse.delta[1] += y - in->mouse.position[1];
    }

    in->mouse.position[0] = x;
    in->mouse.position[1] = y;
    dev->has_position = true;
    box_input_emit_mouse(dev, in, 0);
}

/** Change the focus, releasing every held key and action when it is lost.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param focused Whether input is focused
 */
static void box_input_set_focus(
    InputDevice *dev,
    Input *in,
    bool focused)
{
    if (!focused) {
        box_input_release_all(dev, in);
        box_input_hold_release_all(dev);
    }

    if (focused != in->focused) {
        dev->has_position = false;
    }

    in->focused = focused;
}

/** Apply one event to the Input singleton.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param ev The event
 * @param synthetic Whether the event comes from the synthetic queue
 */
static void box_input_apply(
    InputDevice *dev,
    Input *in,
    const InputEvent *ev,
    bool synthetic)
{
    switch (ev->kind) {
    case InputKeyDown:
        box_input_key_down(dev, in, ev->key, ev->repeat);
        break;
    case InputKeyUp:
        box_input_key_up(dev, in, ev->key);
        break;
    case InputText:
        box_input_text(dev, in, ev->code);
        break;
    case InputMove:
        in->mouse.pinned = in->mouse.pinned || synthetic;
        box_input_move(dev, in, ev);
        break;
    case InputScroll:
        in->mouse.scroll[0] += ev->value[0];
        in->mouse.scroll[1] += ev->value[1];
        box_input_emit_scroll(dev, in, ev);
        break;
    case InputFocus:
        box_input_set_focus(dev, in, ev->code != 0);
        break;
    case InputResize:
        in->size[0] = ev->value[0];
        in->size[1] = ev->value[1];
        break;
    case InputRelease:
        in->mouse.pinned = false;
        dev->has_position = false;
        break;
    case InputReset:
        box_input_release_all(dev, in);
        box_input_hold_release_all(dev);
        in->mouse.pinned = false;
        dev->has_position = false;
        break;
    case InputActionDown:
    case InputActionUp:
        box_input_hold(dev, ev->action, ev->kind == InputActionDown);
        break;
    }
}

/** Apply the events the backend pushed, ignoring them while input is unfocused.
 *
 * @param dev The device state
 * @param in The Input singleton
 */
static void box_input_apply_platform(
    InputDevice *dev,
    Input *in)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&dev->platform); i ++) {
        const InputEvent *ev = ecs_vec_get_t(&dev->platform, InputEvent, i);
        bool always = ev->kind == InputFocus || ev->kind == InputResize;
        bool pinned = ev->kind == InputMove && in->mouse.pinned;
        if (always || (in->focused && !pinned)) {
            box_input_apply(dev, in, ev, false);
        }
    }

    ecs_vec_clear(&dev->platform);
}

/** Apply the synthetic events that are due, in order, up to the first one that waits.
 *
 * @param dev The device state
 * @param in The Input singleton
 */
static void box_input_apply_queue(
    InputDevice *dev,
    Input *in)
{
    InputEvent *events = ecs_vec_first_t(&dev->queue, InputEvent);
    int32_t i, count = ecs_vec_count(&dev->queue);
    for (i = 0; i < count; i ++) {
        if (events[i].delay > 0) {
            events[i].delay --;
            break;
        }

        box_input_apply(dev, in, &events[i], true);
    }

    if (i > 0) {
        memmove(events, &events[i], (size_t)(count - i) * sizeof(InputEvent));
        ecs_vec_set_count_t(NULL, &dev->queue, InputEvent, count - i);
    }
}

/** Derive the normalized cursor from the window size.
 *
 * @param in The Input singleton
 */
static void box_input_derive(
    Input *in)
{
    int32_t i;
    for (i = 0; i < 2; i ++) {
        in->mouse.normalized[i] = in->size[i] > 0 ? in->mouse.position[i] / in->size[i] : 0;
    }
}

/** Emit the script events of the frame through the router or flecs.script.event.
 *
 * @param world The world
 * @param dev The device state
 */
static void box_input_emit(
    ecs_world_t *world,
    InputDevice *dev)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&dev->emits); i ++) {
        box_input_emit_t emit = *ecs_vec_get_t(&dev->emits, box_input_emit_t, i);
        if (dev->router.route) {
            dev->router.route(world, &emit, dev->router.ctx);
        } else if (!emit.is_mouse) {
            ecs_script_keyboard_event(world, &emit.keyboard);
        } else if (!emit.is_scroll) {
            ecs_script_mouse_event(world, &emit.mouse);
        }
    }

    ecs_vec_clear(&dev->emits);
}

void BoxInputUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    InputDevice *dev = box_input_device(world);
    Input *in;
    if (dev->backend.poll) {
        dev->backend.poll(world, dev->backend.ctx);
    }

    in = ecs_singleton_get_mut(world, Input);
    box_input_frame_begin(dev, in);
    box_input_apply_platform(dev, in);
    box_input_apply_queue(dev, in);
    box_input_derive(in);
    ecs_singleton_modified(world, Input);
    box_input_emit(world, dev);
}
