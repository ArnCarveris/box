/**
 * @file input/update.c
 * @brief The OnLoad system that turns the queued events into the Input singleton.
 */

#include "input_internal.h"
#include <string.h>
#include "../surface/surface_internal.h"

/** Mark a key or button as down, keeping the press of a key that is already held.
 *
 * @param in The Input singleton
 * @param code The key code
 */
static void box_input_key_down(
    Input *in,
    int32_t code)
{
    if (code >= 0 && code < BOX_KEY_COUNT) {
        if (!in->keys[code].current) {
            in->keys[code].pressed = true;
        }

        in->keys[code].current = true;
        in->keys[code].state = true;
    }
}

/** Mark a key or button as up, leaving the press of this frame in place.
 *
 * @param in The Input singleton
 * @param code The key code
 */
static void box_input_key_up(
    Input *in,
    int32_t code)
{
    if (code >= 0 && code < BOX_KEY_COUNT) {
        in->keys[code].current = false;
        in->keys[code].state = false;
    }
}

/** Append a code point to the text buffer of the frame as UTF-8.
 *
 * @param in The Input singleton
 * @param code The unicode code point
 */
static void box_input_char(
    Input *in,
    int32_t code)
{
    char utf8[4];
    int32_t size = 0;
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

    if (in->text_len + size <= BOX_TEXT_MAX) {
        memcpy(&in->text[in->text_len], utf8, (size_t)size);
        in->text_len += size;
    }
}

/** Move the cursor, adding the movement to the delta of the frame.
 *
 * The first position after a focus gain only moves the cursor, so that a window
 * that regains focus under a moved pointer does not report a jump.
 *
 * @param in The Input singleton
 * @param state The queues
 * @param x Cursor x in window points
 * @param y Cursor y in window points
 */
static void box_input_move(
    Input *in,
    InputState *state,
    float x,
    float y)
{
    if (state->cursor_valid) {
        in->mouse.delta.x += x - in->mouse.window.x;
        in->mouse.delta.y += y - in->mouse.window.y;
    }

    state->cursor_valid = true;
    in->mouse.window.x = x;
    in->mouse.window.y = y;
}

void box_input_clear_keys(
    Input *in)
{
    int32_t i;
    for (i = 0; i < BOX_KEY_COUNT; i ++) {
        in->keys[i].pressed = false;
        in->keys[i].state = false;
        in->keys[i].current = false;
    }

    in->text_len = 0;
}

/** Apply a focus change, clearing the key state so no press survives it.
 *
 * @param in The Input singleton
 * @param state The queues
 * @param focused Whether the window has focus
 */
static void box_input_focus(
    Input *in,
    InputState *state,
    bool focused)
{
    in->focused = focused;
    box_input_clear_keys(in);
    state->cursor_valid = false;
}

/** Apply one event to the input state.
 *
 * @param in The Input singleton
 * @param state The queues
 * @param ev The event
 */
static void box_input_apply(
    Input *in,
    InputState *state,
    const InputEvent *ev)
{
    switch (ev->kind) {
    case InputKeyDown:
    case InputButtonDown:
        box_input_key_down(in, ev->code);
        break;
    case InputKeyUp:
    case InputButtonUp:
        box_input_key_up(in, ev->code);
        break;
    case InputChar:
        box_input_char(in, ev->code);
        break;
    case InputMouseMove:
        box_input_move(in, state, ev->value.x, ev->value.y);
        break;
    case InputScroll:
        in->mouse.scroll.x += ev->value.x;
        in->mouse.scroll.y += ev->value.y;
        in->mouse.scroll_delta.x += ev->value.x;
        in->mouse.scroll_delta.y += ev->value.y;
        break;
    case InputFocus:
        box_input_focus(in, state, ev->code != 0);
        break;
    case InputMinimize:
        in->minimized = ev->code != 0;
        break;
    }
}

/** Clear the fields that only hold what happened during one frame.
 *
 * @param in The Input singleton
 */
static void box_input_clear_frame(
    Input *in)
{
    int32_t i;
    for (i = 0; i < BOX_KEY_COUNT; i ++) {
        in->keys[i].pressed = false;
    }

    in->text_len = 0;
    in->mouse.delta.x = 0;
    in->mouse.delta.y = 0;
    in->mouse.scroll_delta.x = 0;
    in->mouse.scroll_delta.y = 0;
}

/** Apply the events the platform queued, ignoring input while the window is away.
 *
 * @param in The Input singleton
 * @param state The queues
 */
static void box_input_apply_platform(
    Input *in,
    InputState *state)
{
    int32_t i, count = ecs_vec_count(&state->platform);
    for (i = 0; i < count; i ++) {
        const InputEvent *ev = ecs_vec_get_t(&state->platform, InputEvent, i);
        bool away = !in->focused || in->minimized;
        bool pinned = state->cursor_pinned && ev->kind == InputMouseMove;
        if ((!away || ev->kind == InputFocus || ev->kind == InputMinimize) && !pinned) {
            box_input_apply(in, state, ev);
        }
    }

    ecs_vec_clear(&state->platform);
}

/** Queue the up event of a down event that carries a duration.
 *
 * @param state The queues
 * @param ev The applied event
 */
static void box_input_schedule_up(
    InputState *state,
    const InputEvent *ev)
{
    bool down = ev->kind == InputKeyDown || ev->kind == InputButtonDown;
    if (down && ev->duration > 0) {
        InputEvent *up = ecs_vec_append_t(NULL, &state->scheduled, InputEvent);
        *up = *ev;
        up->kind = ev->kind == InputKeyDown ? InputKeyUp : InputButtonUp;
        up->delay = ev->duration - 1;
        up->duration = 0;
    }
}

/** Apply the injected events that are due and age the ones that are not.
 *
 * @param in The Input singleton
 * @param state The queues
 */
static void box_input_apply_injected(
    Input *in,
    InputState *state)
{
    int32_t i, keep = 0, count = ecs_vec_count(&state->injected);
    InputEvent *events = ecs_vec_first_t(&state->injected, InputEvent);
    for (i = 0; i < count; i ++) {
        InputEvent ev = events[i];
        if (ev.delay > 0) {
            ev.delay --;
            events[keep ++] = ev;
        } else {
            box_input_apply(in, state, &ev);
            box_input_schedule_up(state, &ev);
        }
    }

    ecs_vec_set_count_t(NULL, &state->injected, InputEvent, keep);
    for (i = 0; i < ecs_vec_count(&state->scheduled); i ++) {
        const InputEvent *up = ecs_vec_get_t(&state->scheduled, InputEvent, i);
        ecs_vec_append_t(NULL, &state->injected, InputEvent)[0] = *up;
    }

    ecs_vec_clear(&state->scheduled);
}

/** Derive the view centered and the normalized cursor from the surface size.
 *
 * @param world The world
 * @param in The Input singleton
 */
static void box_input_derive(
    ecs_world_t *world,
    Input *in)
{
    const Surface *s = ecs_singleton_get(world, Surface);
    float width = 0, height = 0;
    if (s) {
        width = (float)(s->point_width ? s->point_width : s->width);
        height = (float)(s->point_height ? s->point_height : s->height);
    }

    float center_x = width * 0.5f, center_y = height * 0.5f;
    in->mouse.view.x = in->mouse.window.x - center_x;
    in->mouse.view.y = center_y - in->mouse.window.y;
    in->mouse.normalized.x = center_x > 0 ? in->mouse.view.x / center_x : 0;
    in->mouse.normalized.y = center_y > 0 ? in->mouse.view.y / center_y : 0;
}

void BoxInputBind(
    ecs_iter_t *it)
{
    const Surface *s = ecs_field(it, Surface, 0);
    SurfaceImpl *impl = ecs_field(it, SurfaceImpl, 1);
    InputState *state = box_input_state(it->world);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (!impl[i].window) {
            continue;
        }

        if (state->window != impl[i].window) {
            state->window = impl[i].window;
            state->captured = false;
            box_window_set_input(state->window, box_input_sink, state);
        }

        if (state->captured != s[i].cursor_captured) {
            state->captured = s[i].cursor_captured;
            box_window_set_cursor_captured(state->window, state->captured);
        }
    }
}

void BoxInputUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    Input *in = ecs_get_mut(world, ecs_id(Input), Input);
    InputState *state = box_input_state(world);
    box_input_clear_frame(in);
    box_input_apply_platform(in, state);
    box_input_apply_injected(in, state);
    box_input_derive(world, in);
}
