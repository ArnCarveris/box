/**
 * @file input/update.c
 * @brief The OnLoad system that applies the device events to Input and emits script events.
 */

#include "input.h"
#include <string.h>

/** Copy the text collected this frame into Input.text.
 *
 * Input.text is reallocated to the size of the text rather than grown in place, so a string
 * that was replaced from outside InputUpdate never has its capacity guessed.
 *
 * @param dev The device state, which holds the text of the frame
 * @param in The Input singleton
 */
static void box_input_text_publish(
    InputDevice *dev,
    Input *in)
{
    int32_t size = ecs_vec_count(&dev->text);
    if (size) {
        in->text = ecs_os_realloc(in->text, size + 1);
        memcpy(in->text, ecs_vec_first(&dev->text), (size_t)size);
        in->text[size] = '\0';
    }
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

    ecs_vec_clear(&dev->text);
    if (in->text) {
        in->text[0] = '\0';
    }

    ecs_os_free(in->paste);
    in->paste = NULL;

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
    dev->held[key] = true;
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
    if (key > KeyNone && key < BOX_KEY_COUNT) {
        dev->held[key] = false;
    }

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

/** Append a code point to the text of the frame as UTF-8.
 *
 * @param dev The device state
 * @param code The unicode code point
 */
static void box_input_text(
    InputDevice *dev,
    int32_t code)
{
    char utf8[4];
    int32_t size = box_utf8_encode(code, utf8);
    if (code > 0) {
        memcpy(ecs_vec_grow_t(NULL, &dev->text, char, size), utf8, (size_t)size);
    }
}

/** Append pasted text to Input.paste, each maximal subpart of an ill formed sequence as U+FFFD.
 *
 * @param in The Input singleton
 * @param text UTF-8 text, nothing is pasted for NULL or empty text
 */
static void box_input_paste_text(
    Input *in,
    const char *text)
{
    const char *cur = text;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    char utf8[4];
    if (!text || !text[0]) {
        goto done;
    }

    if (in->paste) {
        ecs_strbuf_appendstr(&buf, in->paste);
    }

    while (cur[0]) {
        int32_t size = box_utf8_encode(box_utf8_next(&cur), utf8);
        ecs_strbuf_appendstrn(&buf, utf8, size);
    }

    ecs_os_free(in->paste);
    in->paste = ecs_strbuf_get(&buf);
done:
    return;
}

/** Check whether a key event is the paste shortcut: V going down or repeating while control,
 * or command on macOS, is held without alt, which AltGr layouts press with control.
 *
 * The keys held are those of the applied events, so a console that clears Input.keys while it
 * is open still pastes.
 *
 * @param dev The device state, before the key goes down
 * @param ev The key down event
 * @return Whether the event pastes
 */
static bool box_input_paste_shortcut(
    const InputDevice *dev,
    const InputEvent *ev)
{
    bool ctrl = dev->held[KeyCtrlLeft] || dev->held[KeyCtrlRight];
#ifdef __APPLE__
    ctrl = ctrl || dev->held[KeySuperLeft] || dev->held[KeySuperRight];
#endif

    return ev->key == KeyV && (!dev->held[KeyV] || ev->repeat) && ctrl &&
        !dev->held[KeyAltLeft] && !dev->held[KeyAltRight];
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

/** Find what a surface last reported.
 *
 * @param dev The device state
 * @param surface The surface
 * @return The entry of the surface, NULL when it never sent a size or a move
 */
static box_input_surface_t* box_input_surface_find(
    InputDevice *dev,
    ecs_entity_t surface)
{
    box_input_surface_t *surfaces = ecs_vec_first_t(&dev->surfaces, box_input_surface_t);
    box_input_surface_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&dev->surfaces) && !result; i ++) {
        if (surfaces[i].surface == surface) {
            result = &surfaces[i];
        }
    }

    return result;
}

/** Remember the size or cursor position a surface reported, forgetting the surfaces that no
 * longer exist when a new one is added.
 *
 * @param world The world
 * @param dev The device state
 * @param ev A resize or move event of the surface
 */
static void box_input_surface_remember(
    ecs_world_t *world,
    InputDevice *dev,
    const InputEvent *ev)
{
    box_input_surface_t *entry = box_input_surface_find(dev, ev->surface);
    box_input_surface_t *surfaces;
    int32_t i = 0;
    if (!entry) {
        surfaces = ecs_vec_first_t(&dev->surfaces, box_input_surface_t);
        while (i < ecs_vec_count(&dev->surfaces)) {
            if (ecs_is_alive(world, surfaces[i].surface)) {
                i ++;
            } else {
                surfaces[i] = surfaces[ecs_vec_count(&dev->surfaces) - 1];
                ecs_vec_remove_last(&dev->surfaces);
            }
        }

        entry = ecs_vec_append_t(NULL, &dev->surfaces, box_input_surface_t);
        ecs_os_zeromem(entry);
        entry->surface = ev->surface;
    }

    if (ev->kind == InputResize) {
        entry->size[0] = ev->value[0];
        entry->size[1] = ev->value[1];
        entry->has_size = true;
    } else if (!ev->normalized) {
        entry->position[0] = ev->value[0];
        entry->position[1] = ev->value[1];
        entry->has_position = true;
    }
}

/** Give the input to a surface that gains the focus: its last reported size and, unless a
 * synthetic cursor is pinned, its last cursor position, from which the next move measures.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param surface The surface
 */
static void box_input_surface_take(
    InputDevice *dev,
    Input *in,
    ecs_entity_t surface)
{
    const box_input_surface_t *entry = box_input_surface_find(dev, surface);
    in->surface = surface;
    dev->has_position = false;
    if (entry && entry->has_size) {
        in->size[0] = entry->size[0];
        in->size[1] = entry->size[1];
    }

    if (entry && entry->has_position && !in->mouse.pinned) {
        in->mouse.position[0] = entry->position[0];
        in->mouse.position[1] = entry->position[1];
        dev->has_position = true;
    }
}

/** Change the focus, releasing every held key and action when it is lost or moves to another
 * surface. A surface that gains the focus takes the input at its last cursor position.
 *
 * @param dev The device state
 * @param in The Input singleton
 * @param ev The focus event
 */
static void box_input_set_focus(
    InputDevice *dev,
    Input *in,
    const InputEvent *ev)
{
    bool focused = ev->code != 0;
    bool moved = focused && ev->surface && ev->surface != in->surface;
    if (!focused || moved) {
        box_input_release_all(dev, in);
        box_input_hold_release_all(dev);
    }

    if (focused != in->focused) {
        dev->has_position = false;
    }

    if (ev->surface && focused && (moved || !in->focused)) {
        box_input_surface_take(dev, in, ev->surface);
    }

    in->focused = focused;
}

/** Check whether an element under the cursor keeps presses and the wheel from the actions.
 *
 * @param world The world
 * @param dev The device state
 * @param in The Input singleton
 * @return Whether the router finds a blocking element at the cursor
 */
static bool box_input_blocks(
    ecs_world_t *world,
    const InputDevice *dev,
    const Input *in)
{
    return dev->router.blocks && dev->router.blocks(world, in->mouse.position, dev->router.ctx);
}

/** Decide, when a mouse button goes down, whether its press hit a blocking element, which keeps
 * the bindings of the button up until its next press.
 *
 * @param world The world
 * @param dev The device state
 * @param in The Input singleton
 * @param key The key that goes down
 */
static void box_input_press_block(
    ecs_world_t *world,
    InputDevice *dev,
    const Input *in,
    Key key)
{
    if (key >= KeyMouseLeft && key <= KeyMouseRight && !in->keys[key].down) {
        dev->blocked[key - KeyMouseLeft] = box_input_blocks(world, dev, in);
    }
}

bool box_input_key_blocked(
    const InputDevice *dev,
    Key key)
{
    return key >= KeyMouseLeft && key <= KeyMouseRight && dev->blocked[key - KeyMouseLeft];
}

/** Apply one event to the Input singleton.
 *
 * @param world The world
 * @param dev The device state
 * @param in The Input singleton
 * @param ev The event
 * @param synthetic Whether the event comes from the synthetic queue
 */
static void box_input_apply(
    ecs_world_t *world,
    InputDevice *dev,
    Input *in,
    const InputEvent *ev,
    bool synthetic)
{
    bool paste;
    switch (ev->kind) {
    case InputKeyDown:
        paste = box_input_paste_shortcut(dev, ev) && (synthetic || !dev->backend.clipboard_set);
        box_input_press_block(world, dev, in, ev->key);
        box_input_key_down(dev, in, ev->key, ev->repeat);
        if (paste) {
            box_input_paste_text(in, box_input_clipboard_get(world));
        }

        break;
    case InputKeyUp:
        box_input_key_up(dev, in, ev->key);
        break;
    case InputText:
        box_input_text(dev, ev->code);
        break;
    case InputMove:
        in->mouse.pinned = in->mouse.pinned || synthetic;
        box_input_move(dev, in, ev);
        break;
    case InputScroll:
        if (!box_input_blocks(world, dev, in)) {
            in->mouse.scroll[0] += ev->value[0];
            in->mouse.scroll[1] += ev->value[1];
        }

        box_input_emit_scroll(dev, in, ev);
        break;
    case InputFocus:
        box_input_set_focus(dev, in, ev);
        break;
    case InputResize:
        in->size[0] = ev->value[0];
        in->size[1] = ev->value[1];
        break;
    case InputRelease:
        dev->has_position = dev->has_position && !in->mouse.pinned;
        in->mouse.pinned = false;
        break;
    case InputReset:
        box_input_release_all(dev, in);
        box_input_hold_release_all(dev);
        dev->has_position = dev->has_position && !in->mouse.pinned;
        in->mouse.pinned = false;
        break;
    case InputActionDown:
    case InputActionUp:
        box_input_hold(dev, ev->action, ev->kind == InputActionDown);
        break;
    case InputCapture:
        in->capture = ev->code != 0;
        break;
    case InputCaptured:
        if (!ev->code) {
            dev->has_position = dev->has_position && in->mouse.pinned;
        }

        in->mouse.captured = ev->code != 0 && dev->capture_applied;
        break;
    case InputPaste:
        if (!synthetic && ev->text && ev->text[0]) {
            ecs_os_free(dev->clipboard);
            dev->clipboard = ecs_os_strdup(ev->text);
        }

        box_input_paste_text(in, ev->text);
        break;
    }
}

/** Check whether a platform event applies: events of other surfaces than the one with the
 * input focus only apply when they give it the focus, and none but focus and resize events
 * apply while input is unfocused.
 *
 * @param world The world
 * @param dev The device state
 * @param in The Input singleton
 * @param ev The event
 * @return Whether the event applies
 */
static bool box_input_platform_accept(
    ecs_world_t *world,
    InputDevice *dev,
    const Input *in,
    const InputEvent *ev)
{
    bool own = !ev->surface || !in->surface || ev->surface == in->surface;
    bool result;
    if (ev->surface && (ev->kind == InputResize || ev->kind == InputMove)) {
        box_input_surface_remember(world, dev, ev);
    }

    if (ev->kind == InputResize || ev->kind == InputCapture || ev->kind == InputCaptured) {
        result = own;
    } else if (ev->kind == InputFocus) {
        result = own || ev->code != 0;
    } else {
        result = own && in->focused && !(ev->kind == InputMove && in->mouse.pinned);
    }

    return result;
}

/** Apply the events the backend pushed, ignoring them while input is unfocused and those of
 * surfaces without the input focus.
 *
 * @param world The world
 * @param dev The device state
 * @param in The Input singleton
 */
static void box_input_apply_platform(
    ecs_world_t *world,
    InputDevice *dev,
    Input *in)
{
    int32_t i;
    if (in->surface && !ecs_is_alive(world, in->surface)) {
        in->surface = 0;
    }

    for (i = 0; i < ecs_vec_count(&dev->platform); i ++) {
        const InputEvent *ev = ecs_vec_get_t(&dev->platform, InputEvent, i);
        if (box_input_platform_accept(world, dev, in, ev)) {
            box_input_apply(world, dev, in, ev, false);
        }
    }

    box_input_events_clear(&dev->platform);
}

/** Apply the synthetic events that are due, in order, up to the first one that waits.
 *
 * @param world The world
 * @param dev The device state
 * @param in The Input singleton
 */
static void box_input_apply_queue(
    ecs_world_t *world,
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

        box_input_apply(world, dev, in, &events[i], true);
        ecs_os_free(events[i].text);
    }

    if (i > 0) {
        memmove(events, &events[i], (size_t)(count - i) * sizeof(InputEvent));
        ecs_vec_set_count_t(NULL, &dev->queue, InputEvent, count - i);
    }
}

void box_input_capture_release(
    ecs_world_t *world,
    InputDevice *dev)
{
    if (dev->capture_applied && dev->backend.capture) {
        dev->backend.capture(world, dev->capture_surface, false, dev->backend.ctx);
    }

    dev->capture_applied = false;
}

/** Capture the cursor while Input.capture is set and input is focused, in the window of the
 * input surface, and release it otherwise.
 *
 * Taking or letting go of the cursor at once starts a fresh delta unless a synthetic cursor is
 * pinned, because the platform moves the real cursor; a later InputCaptured 0 does so in order.
 *
 * @param world The world
 * @param dev The device state
 * @param in The Input singleton
 */
static void box_input_capture_sync(
    ecs_world_t *world,
    InputDevice *dev,
    Input *in)
{
    bool want = in->capture && in->focused;
    bool moved = false;
    if (dev->capture_applied && (!want || dev->capture_surface != in->surface)) {
        box_input_capture_release(world, dev);
        moved = in->mouse.captured;
        in->mouse.captured = false;
    }

    if (want && !dev->capture_applied) {
        in->mouse.captured = dev->backend.capture &&
            dev->backend.capture(world, in->surface, true, dev->backend.ctx);
        dev->capture_applied = true;
        dev->capture_surface = in->surface;
        moved = moved || in->mouse.captured;
    }

    if (moved) {
        dev->has_position = dev->has_position && in->mouse.pinned;
    }

    in->mouse.captured = in->mouse.captured && dev->capture_applied;
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
    box_input_apply_platform(world, dev, in);
    box_input_apply_queue(world, dev, in);
    box_input_capture_sync(world, dev, in);
    box_input_text_publish(dev, in);
    box_input_derive(in);
    ecs_singleton_modified(world, Input);
    box_input_emit(world, dev);
}
