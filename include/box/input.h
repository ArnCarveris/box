/**
 * @file box/input.h
 * @brief Device input layer: the Input singleton with key, mouse and text state.
 *
 * One OnLoad system fills Input from the events the platform layer queues and from
 * injected events, so injected input is indistinguishable from real input.
 */

#ifndef BOX_INPUT_H
#define BOX_INPUT_H

#include "types.h"
#include "keys.h"

#ifdef BOX_INPUT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes of the text buffer one frame collects */
#define BOX_TEXT_MAX (32)

/** What an input event does, mirrors box_input_kind_t of the platform layer */
ECS_ENUM(InputEventKind, {
    InputKeyDown = 0,     /**< Key went down, code is a key code */
    InputKeyUp = 1,       /**< Key came up, code is a key code */
    InputChar = 2,        /**< Character typed, code is a unicode code point */
    InputMouseMove = 3,   /**< Cursor moved to value, in window points */
    InputButtonDown = 4,  /**< Mouse button went down, code is a button code */
    InputButtonUp = 5,    /**< Mouse button came up, code is a button code */
    InputScroll = 6,      /**< Wheel moved by value */
    InputFocus = 7,       /**< Window focus changed, code is 1 when focused */
    InputMinimize = 8     /**< Window minimize changed, code is 1 when minimized */
});

/** Queue an event is applied from */
ECS_ENUM(InputSource, {
    InputSourceInjected = 0, /**< Injected, applied after the platform events */
    InputSourcePlatform = 1  /**< Read from the OS, ignored while unfocused */
});

/** One queued input event */
ECS_STRUCT(InputEvent, {
    InputEventKind kind; /**< What the event does */
    InputSource source;  /**< Queue the event waits in */
    int32_t code;        /**< Key code, button code, code point or boolean flag */
    vec2 value;          /**< Cursor position or scroll offset */
    int32_t delay;       /**< Frames before the event applies, 0 for this frame */
    int32_t duration;    /**< Frames a down event stays down, 0 to hold it */
});

/** Per frame state of one key or mouse button */
ECS_STRUCT(KeyState, {
    bool pressed; /**< The key went down this frame, also when it came up again */
    bool state;   /**< The key is held */
    bool current; /**< Raw state the OS last reported */
});

/** Cursor and wheel state in the coordinate spaces of the frame */
ECS_STRUCT(Mouse, {
    vec2 window;       /**< Cursor in window points, origin at the top left */
    vec2 delta;        /**< Cursor movement since the previous frame in points */
    vec2 view;         /**< Cursor in points from the center, x right and y up */
    vec2 normalized;   /**< Cursor from the center in -1 to 1, x right and y up */
    vec2 scroll;       /**< Wheel offset accumulated since the world started */
    vec2 scroll_delta; /**< Wheel offset of this frame */
});

/** Device input state of the frame, written by the BoxInputUpdate system */
ECS_STRUCT(Input, {
    KeyState keys[130]; /**< Key and button slots, indexed by key code */
    char text[32];      /**< Characters typed this frame as UTF-8, not terminated */
    int32_t text_len;   /**< Bytes of text the frame collected */
    Mouse mouse;        /**< Cursor and wheel state */
    bool focused;       /**< The window has keyboard focus */
    bool minimized;     /**< The window is minimized */
});

/** Queue an event, which the next BoxInputUpdate applies.
 *
 * A key or button down event with a duration schedules its up event that many
 * frames later, which is how a press that lasts one frame is injected.
 *
 * @param world The world
 * @param event The event
 */
void box_input_inject(
    ecs_world_t *world,
    const InputEvent *event);

/** Empty both queues and clear the key, button and text state.
 *
 * @param world The world
 */
void box_input_reset(
    ecs_world_t *world);

/** Count the events that are still waiting in the queues.
 *
 * @param world The world
 * @return Events waiting in the platform and injected queues
 */
int32_t box_input_pending(
    ecs_world_t *world);

/** Register the box.input module.
 *
 * @param world The world
 */
void BoxInputImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
