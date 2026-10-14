/**
 * @file box/input.h
 * @brief Keyboard and mouse device state, the synthetic input queue and the platform hook.
 *
 * A platform backend pushes device events and agents queue synthetic ones; the InputUpdate
 * system applies both to the Input singleton and emits them through flecs.script.event.
 * Binding entities map keys to Action entities, which ActionUpdate resolves every frame.
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
    InputKeyDown,    /**< Key or mouse button went down */
    InputKeyUp,      /**< Key or mouse button came up */
    InputText,       /**< Character typed, code is a unicode code point */
    InputMove,       /**< Cursor moved to value */
    InputScroll,     /**< Wheel moved by value */
    InputFocus,      /**< Focus changed, code is 1 when focused */
    InputResize,     /**< Window resized to value points */
    InputRelease,    /**< The synthetic cursor is unpinned, the real pointer resumes */
    InputReset,      /**< Every held key, button and action is released, the cursor unpinned */
    InputActionDown, /**< A synthetic hold of an action starts */
    InputActionUp    /**< A synthetic hold of an action ends */
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
    ecs_entity_t action; /**< Action of an action event */
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
    char *text;         /**< Text typed this frame as UTF-8, owned, NULL for none */
    vec2 size;          /**< Window size in points, 0 until a resize event */
    bool focused;       /**< Platform input is applied, false ignores it */
});

/** A named game action, an entity that bindings and synthetic input drive */
ECS_STRUCT(Action, {
    char *description; /**< What the action does */
});

/** State of an action in the current frame, written by the ActionUpdate system */
ECS_STRUCT(ActionState, {
    bool down;     /**< The action is held */
    bool pressed;  /**< The action went down this frame */
    bool released; /**< The action came up this frame */
    vec2 start;    /**< Cursor position of the frame the action went down */
    vec2 delta;    /**< Cursor movement this frame while the action is held */
    vec2 drag;     /**< Cursor movement since the action went down */
});

/** Binds a key, with optional modifiers, to an action.
 *
 * The modifiers held when the key goes down decide: of the bindings whose modifiers were held,
 * those requiring the most apply (Ctrl+S masks S), unless another modifier key was held that no
 * binding uses as its key and the binding does not ignore. Equally specific bindings all apply.
 *
 * The action is held until the key comes up or a required modifier is released; a modifier
 * pressed later changes nothing and no other binding takes the key before it is pressed again.
 * Disabled entities and prefabs do not bind.
 */
ECS_STRUCT(Binding, {
    ecs_entity_t action;   /**< Action, when 0 the binding entity if it is one, else its parent */
    Key key;               /**< Key or mouse button */
    bool ctrl;             /**< A control key must be held too */
    bool shift;            /**< A shift key must be held too */
    bool alt;              /**< An alt key must be held too */
    bool meta;             /**< A super key must be held too */
    bool ignore_modifiers; /**< Applies while modifiers it does not require are held */
});

/** What on.pressed, on.held, on.dragged and on.released resolve with */
ECS_STRUCT(ActionEvent, {
    ecs_entity_t action; /**< Action the event is about */
    float screen_x;      /**< Cursor x in window points */
    float screen_y;      /**< Cursor y in window points */
    float delta_x;       /**< Horizontal cursor movement this frame while held */
    float delta_y;       /**< Vertical cursor movement this frame while held */
    float drag_x;        /**< Horizontal cursor movement since the action went down */
    float drag_y;        /**< Vertical cursor movement since the action went down */
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

/** Translate a glfw key code, a GLFW_KEY_* value, to a key.
 *
 * Codes name a position on a US layout, so W is the key left of E on every layout.
 *
 * @param key The glfw key code
 * @return The key, KeyNone for a code without a key
 */
Key box_input_glfw_key(
    int32_t key);

/** Translate a glfw mouse button, a GLFW_MOUSE_BUTTON_* value, to a key.
 *
 * @param button The glfw button
 * @return KeyMouseLeft, KeyMouseRight or KeyMouseMiddle, KeyNone for other buttons
 */
Key box_input_glfw_button(
    int32_t button);

/** Push the platform event of a glfw key callback.
 *
 * @param world The world
 * @param key The glfw key code
 * @param action GLFW_RELEASE (0), GLFW_PRESS (1) or GLFW_REPEAT (2)
 */
void box_input_glfw_on_key(
    ecs_world_t *world,
    int32_t key,
    int32_t action);

/** Push the platform event of a glfw mouse button callback.
 *
 * @param world The world
 * @param button The glfw button
 * @param action GLFW_RELEASE (0) or GLFW_PRESS (1)
 */
void box_input_glfw_on_button(
    ecs_world_t *world,
    int32_t button,
    int32_t action);

/** Translate the code of a browser keyboard event, such as KeyW or ArrowLeft, to a key.
 *
 * @param code The KeyboardEvent.code value
 * @return The key, KeyNone for a code without a key
 */
Key box_input_dom_key(
    const char *code);

/** Translate the button of a browser mouse event to a key.
 *
 * @param button The MouseEvent.button value, 0 left, 1 middle, 2 right
 * @return KeyMouseLeft, KeyMouseMiddle or KeyMouseRight, KeyNone for other buttons
 */
Key box_input_dom_button(
    int32_t button);

/** Push the platform events of a browser keydown or keyup: the key and the typed text.
 *
 * A keydown whose key value is one printable character types it, unless control or meta is
 * held.
 *
 * @param world The world
 * @param code The KeyboardEvent.code value
 * @param key The KeyboardEvent.key value
 * @param down Whether the event is a keydown
 * @param repeat Whether the browser repeats a held key
 * @param shortcut Whether control or meta is held
 * @return Whether the page must not act on the event: a mapped key that is not a function key
 * and not part of a shortcut
 */
bool box_input_dom_on_key(
    ecs_world_t *world,
    const char *code,
    const char *key,
    bool down,
    bool repeat,
    bool shortcut);

/** Push the platform event of a browser wheel event in the units of glfw: one per wheel
 * notch, positive y when the wheel turns away from the user.
 *
 * A notch is 100 pixels or 3 lines; a page counts as one notch.
 *
 * @param world The world
 * @param dx WheelEvent.deltaX
 * @param dy WheelEvent.deltaY
 * @param mode WheelEvent.deltaMode, 0 pixels, 1 lines, 2 pages
 */
void box_input_dom_on_wheel(
    ecs_world_t *world,
    double dx,
    double dy,
    int32_t mode);

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

/** Queue a focus change; losing focus releases the held keys and actions and ignores platform
 * input.
 *
 * @param world The world
 * @param focused Whether input is focused
 */
void box_input_focus(
    ecs_world_t *world,
    bool focused);

/** Empty both queues and queue the release of every held key, action and the synthetic cursor.
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

/** Queue a synthetic hold of an action starting or ending.
 *
 * A held action stays down until it is released here, regardless of its bindings.
 *
 * @param world The world
 * @param action The action entity
 * @param down Whether the hold starts
 */
void box_input_action(
    ecs_world_t *world,
    ecs_entity_t action,
    bool down);

/** Queue an action tap that holds the action for a number of frames.
 *
 * @param world The world
 * @param action The action entity
 * @param frames Frames the action is held, one when 0
 */
void box_input_action_press(
    ecs_world_t *world,
    ecs_entity_t action,
    int32_t frames);

/** Queue an action drag: the action goes down at the start, the cursor walks and it comes up.
 *
 * @param world The world
 * @param action The action entity
 * @param drag The walk, its button is ignored
 */
void box_input_action_drag(
    ecs_world_t *world,
    ecs_entity_t action,
    const box_input_sweep_t *drag);

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
