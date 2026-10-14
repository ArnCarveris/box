/**
 * @file box/input.h
 * @brief Keyboard and mouse device state, the synthetic input queue and the platform hook.
 *
 * A platform backend pushes device events and agents queue synthetic ones; the InputUpdate
 * system applies both to the Input singleton and emits them through flecs.script.event.
 */

#ifndef BOX_INPUT_H
#define BOX_INPUT_H

#include "types.h"

#ifdef BOX_INPUT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Number of keys, the size of Input.keys */
#define BOX_KEY_COUNT (104)

/** Bytes of text one frame collects, including the terminating zero */
#define BOX_INPUT_TEXT_MAX (32)

/** Steps of a cursor sweep or drag that does not give them */
#define BOX_INPUT_SWEEP_STEPS (10)

/** Keys and mouse buttons, the index of their state in Input.keys */
ECS_ENUM(Key, {
    KeyNone,                                                    /**< No key */
    KeyA, KeyB, KeyC, KeyD, KeyE, KeyF, KeyG, KeyH, KeyI,       /**< Letters A to I */
    KeyJ, KeyK, KeyL, KeyM, KeyN, KeyO, KeyP, KeyQ, KeyR,       /**< Letters J to R */
    KeyS, KeyT, KeyU, KeyV, KeyW, KeyX, KeyY, KeyZ,             /**< Letters S to Z */
    KeyNum0, KeyNum1, KeyNum2, KeyNum3, KeyNum4,                /**< Digits 0 to 4 */
    KeyNum5, KeyNum6, KeyNum7, KeyNum8, KeyNum9,                /**< Digits 5 to 9 */
    KeySpace, KeyEnter, KeyEscape, KeyTab, KeyBackspace,        /**< Editing keys */
    KeyInsert, KeyDelete, KeyHome, KeyEnd, KeyPageUp, KeyPageDown, /**< Navigation keys */
    KeyLeft, KeyRight, KeyUp, KeyDown,                          /**< Arrow keys */
    KeyF1, KeyF2, KeyF3, KeyF4, KeyF5, KeyF6,                   /**< Function keys F1 to F6 */
    KeyF7, KeyF8, KeyF9, KeyF10, KeyF11, KeyF12,                /**< Function keys F7 to F12 */
    KeyShiftLeft, KeyShiftRight, KeyCtrlLeft, KeyCtrlRight,     /**< Shift and control */
    KeyAltLeft, KeyAltRight, KeySuperLeft, KeySuperRight,       /**< Alt and super */
    KeyCapsLock,                                                /**< Caps lock */
    KeyApostrophe, KeyComma, KeyMinus, KeyPeriod, KeySlash,     /**< Punctuation ' , - . / */
    KeySemicolon, KeyEqual, KeyBracketLeft, KeyBackslash,       /**< Punctuation ; = [ backslash */
    KeyBracketRight, KeyGrave,                                  /**< Punctuation ] ` */
    KeyKp0, KeyKp1, KeyKp2, KeyKp3, KeyKp4,                     /**< Keypad 0 to 4 */
    KeyKp5, KeyKp6, KeyKp7, KeyKp8, KeyKp9,                     /**< Keypad 5 to 9 */
    KeyKpDecimal, KeyKpDivide, KeyKpMultiply, KeyKpSubtract,    /**< Keypad operators */
    KeyKpAdd, KeyKpEnter, KeyKpEqual,                           /**< Keypad add, enter, equal */
    KeyMouseLeft, KeyMouseMiddle, KeyMouseRight                 /**< Mouse buttons */
});

/** State of one key or mouse button in the current frame */
ECS_STRUCT(KeyState, {
    bool down;     /**< The key is held */
    bool pressed;  /**< The key went down this frame */
    bool released; /**< The key came up this frame */
});

/** What an input event does */
ECS_ENUM(InputEventKind, {
    InputKeyDown,  /**< Key or mouse button went down */
    InputKeyUp,    /**< Key or mouse button came up */
    InputText,     /**< Character typed, code is a unicode code point */
    InputMove,     /**< Cursor moved to value */
    InputScroll,   /**< Wheel moved by value */
    InputFocus,    /**< Focus changed, code is 1 when focused */
    InputResize,   /**< Window resized to value points */
    InputRelease,  /**< The synthetic cursor is unpinned, the real pointer resumes */
    InputReset     /**< Every held key and button is released and the cursor unpinned */
});

/** One device event, pushed by the platform or queued as synthetic input */
ECS_STRUCT(InputEvent, {
    InputEventKind kind; /**< What the event does */
    Key key;             /**< Key or button of a key event */
    int32_t code;        /**< Code point of a text event, 1 or 0 for a focus event */
    vec2 value;          /**< Cursor position, wheel offset or window size */
    bool normalized;     /**< The position of a move is in 0..1 of the window size */
    bool repeat;         /**< A platform key down repeated while the key is held */
    int32_t delay;       /**< Frames a synthetic event waits after the one before it */
});

/** Cursor and wheel state of the current frame */
ECS_STRUCT(Mouse, {
    vec2 position;   /**< Cursor in window points, origin at the top left */
    vec2 delta;      /**< Cursor movement during this frame */
    vec2 normalized; /**< Cursor over the window size, 0..1 from the top left */
    vec2 scroll;     /**< Wheel offset of this frame */
    bool pinned;     /**< A synthetic cursor is pinned, platform movement is ignored */
});

/** Device state of the current frame, written by the InputUpdate system */
ECS_STRUCT(Input, {
    KeyState keys[104]; /**< State of every key and mouse button, indexed by Key */
    Mouse mouse;        /**< Cursor and wheel state */
    char text[32];      /**< Text typed this frame as zero terminated UTF-8 */
    vec2 size;          /**< Window size in points, 0 until a resize event */
    bool focused;       /**< Platform input is applied, false ignores it */
});

/** Backend that feeds real device input, such as glfw or the browser */
typedef struct box_input_platform_t {
    void (*poll)(ecs_world_t *world, void *ctx); /**< Pushes the OS events at frame start */
    void (*free)(void *ctx);                     /**< Releases ctx, NULL when not needed */
    void *ctx;                                   /**< Backend state passed to the callbacks */
} box_input_platform_t;

/** A synthetic cursor walk, the arguments of a sweep or a drag */
typedef struct box_input_sweep_t {
    vec2 from;       /**< Start position */
    vec2 to;         /**< End position */
    int32_t steps;   /**< Moves after the start, BOX_INPUT_SWEEP_STEPS when 0 */
    int32_t frames;  /**< Frames the walk takes after the start, steps when 0 */
    bool normalized; /**< Positions are in 0..1 of the window size */
    Key button;      /**< Button a drag holds, KeyMouseLeft when KeyNone */
} box_input_sweep_t;

/** Register the box.input module.
 *
 * @param world The world
 */
void BoxInputImport(
    ecs_world_t *world);

/** Install the backend that feeds real device input, replacing and freeing the previous one.
 *
 * @param world The world
 * @param platform The backend, NULL to remove it
 */
void box_input_platform_set(
    ecs_world_t *world,
    const box_input_platform_t *platform);

/** Push a real device event, applied by the next InputUpdate unless input is unfocused.
 *
 * Cursor moves are ignored while a synthetic cursor is pinned; focus and resize events
 * always apply. The delay of the event is ignored.
 *
 * @param world The world
 * @param event The event
 */
void box_input_platform_push(
    ecs_world_t *world,
    const InputEvent *event);

/** Append an event to the synthetic queue.
 *
 * The queue applies in order, one event after the other; an event waits its delay in frames
 * after the event before it. A synthetic move pins the cursor.
 *
 * @param world The world
 * @param event The event
 */
void box_input_queue(
    ecs_world_t *world,
    const InputEvent *event);

/** Queue a key or button going down or up.
 *
 * @param world The world
 * @param key The key or button
 * @param down Whether the key goes down
 */
void box_input_key(
    ecs_world_t *world,
    Key key,
    bool down);

/** Queue a key tap that holds the key for a number of frames.
 *
 * @param world The world
 * @param key The key or button
 * @param frames Frames the key is held, one when 0
 */
void box_input_key_press(
    ecs_world_t *world,
    Key key,
    int32_t frames);

/** Queue one key tap per character of a text, with the character as typed text.
 *
 * A character without a key, such as an accented letter, only adds text.
 *
 * @param world The world
 * @param text UTF-8 text
 * @param frames Frames each key is held, which also spaces the characters, one when 0
 * @return Number of characters queued
 */
int32_t box_input_key_type(
    ecs_world_t *world,
    const char *text,
    int32_t frames);

/** Queue a cursor move that pins the cursor at a position.
 *
 * @param world The world
 * @param x Cursor x
 * @param y Cursor y
 * @param normalized Whether x and y are in 0..1 of the window size
 */
void box_input_mouse_move(
    ecs_world_t *world,
    float x,
    float y,
    bool normalized);

/** Queue a cursor walk from one position to another, spread over frames.
 *
 * @param world The world
 * @param sweep The walk
 */
void box_input_mouse_sweep(
    ecs_world_t *world,
    const box_input_sweep_t *sweep);

/** Queue the end of the synthetic cursor, after which the real pointer moves it again.
 *
 * @param world The world
 */
void box_input_mouse_release(
    ecs_world_t *world);

/** Queue a click: an optional move, the button down and its release frames later.
 *
 * @param world The world
 * @param button The button, KeyMouseLeft when KeyNone
 * @param pos Position to click at, NULL to click where the cursor is
 * @param normalized Whether pos is in 0..1 of the window size
 * @param frames Frames the button is held, one when 0
 */
void box_input_mouse_click(
    ecs_world_t *world,
    Key button,
    const float *pos,
    bool normalized,
    int32_t frames);

/** Queue a drag: the button goes down at the start, the cursor walks and the button comes up.
 *
 * @param world The world
 * @param drag The walk and its button
 */
void box_input_mouse_drag(
    ecs_world_t *world,
    const box_input_sweep_t *drag);

/** Queue a wheel movement.
 *
 * @param world The world
 * @param dx Horizontal wheel offset
 * @param dy Vertical wheel offset
 */
void box_input_mouse_scroll(
    ecs_world_t *world,
    float dx,
    float dy);

/** Queue a focus change; losing focus releases the held keys and ignores platform input.
 *
 * @param world The world
 * @param focused Whether input is focused
 */
void box_input_focus(
    ecs_world_t *world,
    bool focused);

/** Empty both queues and queue the release of every held key and the synthetic cursor.
 *
 * @param world The world
 */
void box_input_reset(
    ecs_world_t *world);

/** Count the synthetic events that have not been applied yet.
 *
 * @param world The world
 * @return Events in the synthetic queue
 */
int32_t box_input_pending(
    ecs_world_t *world);

/** Look up a key by its name, case insensitive.
 *
 * Accepts the constant names of Key (W, Space, MouseLeft), the key values of keyboard events
 * (ArrowLeft, Control) and single characters (a, 1, /).
 *
 * @param name The name
 * @return The key, KeyNone when no key has that name
 */
Key box_input_key_from_name(
    const char *name);

/** Get the constant name of a key.
 *
 * @param key The key
 * @return The name, such as W or MouseLeft, NULL for an invalid key
 */
const char* box_input_key_name(
    Key key);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
