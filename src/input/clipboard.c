/**
 * @file input/clipboard.c
 * @brief The clipboard of a world, in memory or on the platform, and the pointer cursor shape.
 */

#include "input.h"

void box_input_events_clear(
    ecs_vec_t *events)
{
    InputEvent *ev = ecs_vec_first_t(events, InputEvent);
    int32_t i;
    for (i = 0; i < ecs_vec_count(events); i ++) {
        ecs_os_free(ev[i].text);
    }

    ecs_vec_clear(events);
}

void box_input_clipboard_set(
    ecs_world_t *world,
    const char *text)
{
    InputDevice *dev = box_input_device(world);
    ecs_os_free(dev->clipboard);
    dev->clipboard = ecs_os_strdup(text ? text : "");
    if (dev->backend.clipboard_set) {
        dev->backend.clipboard_set(world, dev->clipboard, dev->backend.ctx);
    }
}

const char* box_input_clipboard_get(
    ecs_world_t *world)
{
    InputDevice *dev = box_input_device(world);
    const char *text = dev->backend.clipboard_get ?
        dev->backend.clipboard_get(world, dev->backend.ctx) : NULL;
    if (text) {
        char *copy = ecs_os_strdup(text);
        ecs_os_free(dev->clipboard);
        dev->clipboard = copy;
    }

    return dev->clipboard ? dev->clipboard : "";
}

void box_input_paste(
    ecs_world_t *world,
    const char *text)
{
    if (text && text[0]) {
        box_input_queue(world, &(InputEvent){
            .kind = InputPaste, .text = ECS_CONST_CAST(char*, text) });
    }
}

void box_input_cursor_sync(
    ecs_world_t *world,
    InputDevice *dev)
{
    if (dev->cursor != dev->cursor_applied && dev->backend.cursor) {
        dev->backend.cursor(world, dev->cursor, dev->backend.ctx);
        dev->cursor_applied = dev->cursor;
    }
}

void box_input_cursor_shape(
    ecs_world_t *world,
    PointerCursor shape)
{
    InputDevice *dev = box_input_device(world);
    dev->cursor = shape >= CursorDefault && shape <= CursorNotAllowed ? shape : CursorDefault;
    box_input_cursor_sync(world, dev);
}

PointerCursor box_input_cursor_get(
    ecs_world_t *world)
{
    return box_input_device(world)->cursor;
}
