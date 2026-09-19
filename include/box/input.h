/**
 * @file box/input.h
 * @brief Device and action input layer: the Input singleton, actions and their bindings.
 *
 * One OnLoad system fills Input from the events the platform layer queues and from
 * injected events, so injected input is indistinguishable from real input. A second
 * one resolves the Binding entities of the input maps into the InputAction state,
 * map by map from the highest InputMap.priority down. A binding that declares
 * consume keeps its key and its action from every map below it, and a handler
 * stops a raise the rest of the frame with box_event_stop, the way a DOM handler
 * calls stopPropagation.
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

/** Window points a held action moves the cursor before on.dragged raises */
#define BOX_INPUT_DRAG_THRESHOLD (3.0f)

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

/** A named action, an entity under the actions scope.
 *
 * The description is authored, the other members are written every frame by the
 * BoxInputActions system from the bindings that resolve to the action.
 */
ECS_STRUCT(InputAction, {
    char *description; /**< What the action does, shown by the action list */
    bool pressed;      /**< The action became active this frame */
    bool held;         /**< The action is active */
    bool released;     /**< The action stopped being active this frame */
    float value;       /**< Analog value or repeat count, 1 while a binding holds it */
});

/** Marks an entity as an input map: the bindings under it resolve while enabled.
 *
 * The maps of a world are layers: they resolve from the highest priority down and
 * the first map that fires an action takes both that action and the keys of the
 * binding that fired it, so a map below it never sees them. A UI map above the map
 * of a game is how a key that both bind reaches the UI alone.
 */
ECS_STRUCT(InputMap, {
    bool enabled;     /**< Bindings of this map resolve, true unless the script clears it */
    int32_t priority; /**< Layer of the map, the highest one resolves first, default 0 */
    bool consume;     /**< Every binding of this map consumes, unless it says so itself */
});

/** What on.dragged hands the handler of an action that is being dragged.
 *
 * The start is the mouse of the frame the action was pressed, which is where a
 * placement that follows the cursor anchors its rectangle, and the cursor is the
 * mouse of the frame the raise belongs to.
 */
ECS_STRUCT(ActionDrag, {
    ecs_entity_t action; /**< Action that is being dragged */
    ecs_entity_t source; /**< Binding that holds it, 0 when code holds it */
    Mouse start;         /**< Mouse of the frame the action was pressed */
    Mouse cursor;        /**< Mouse of this frame */
});

/** One key mask bound to one action, on a child of a map entity.
 *
 * Several bindings may carry the same mask, and an action may be bound by several
 * masks; every binding that resolves this frame makes its action active.
 */
ECS_STRUCT(Binding, {
    uint64_t mask;       /**< Key mask: key code in the low 32 bits, modifier bits above */
    ecs_entity_t action; /**< Action the mask fires */
    bool consume;        /**< The key and the action are kept from every lower map */
});

/** What an action event of the frame reports */
ECS_ENUM(ActionPhase, {
    ActionPressed = 0,  /**< The action became active this frame */
    ActionHeld = 1,     /**< The action is active this frame */
    ActionReleased = 2, /**< The action stopped being active this frame */
    ActionDragged = 3   /**< The action is held and the cursor left the threshold */
});

/** One action event of the frame, kept in the private per frame list */
ECS_STRUCT(ActionEvent, {
    ecs_entity_t action; /**< The action the event is about */
    ActionPhase phase;   /**< What the action did */
    ecs_entity_t source; /**< Binding that fired it, or the entity that emitted it */
    uint64_t mask;       /**< Key mask that fired it, 0 when it was emitted */
    int32_t priority;    /**< Priority of the map the binding belongs to */
});

/** The raise a handler of the frame is running for, and whether it was stopped.
 *
 * One record per world names what is being delivered while a handler runs, so a
 * handler takes an interaction with box_event_stop instead of the framework
 * guessing that it did. A stopped raise reaches no further handler of its own
 * event and takes the key of its binding away from every lower map, which is the
 * handler side of the consume flag of a binding. The order a frame delivers in
 * is the order the layers resolve in: the highest map first, then the deepest
 * node of the UI over the scene, then the scene itself.
 */
ECS_STRUCT(Raise, {
    ecs_entity_t target; /**< Action or entity the raise is about, 0 between raises */
    ecs_entity_t source; /**< Binding, emitter or element the raise came from */
    int32_t priority;    /**< Priority of the map the raise came from */
    bool consumed;       /**< A handler stopped the raise */
});

/** Stop the raise a handler is running for.
 *
 * The rest of the handlers of that raise do not run and the key of its binding
 * is taken from every map below the one it came from, so the actions those maps
 * bind to the same key are cleared for the frame. Outside a raise the call does
 * nothing, which is what makes it safe to call from a task that resumed late.
 *
 * @param world The world
 * @return Whether a raise was stopped
 */
bool box_event_stop(
    ecs_world_t *world);

/** Read the raise a handler is running for.
 *
 * @param world The world
 * @return The record, which names no target between raises
 */
const Raise* box_event_current(
    const ecs_world_t *world);

/** Take an action for the rest of the frame from a handler.
 *
 * The state of the action is cleared, its drag stops and the events it has not
 * raised yet are dropped, so nothing below the handler sees it; the frame after,
 * an action whose key is still down carries on as held rather than pressing
 * again. This is the explicit form of what a binding with consume does before a
 * handler runs at all.
 *
 * @param world The world
 * @param action The action entity
 * @return Whether the action was active this frame
 */
bool box_input_consume(
    ecs_world_t *world,
    ecs_entity_t action);

/** Fire an action for one frame without a key.
 *
 * The action is pressed and released in the same frame and its event carries the
 * source, which is how a UI widget or a command fires the action of a key binding.
 *
 * @param world The world
 * @param action The action entity, which must have an InputAction
 * @param source The entity that fires the action, 0 when there is none
 */
void box_input_emit(
    ecs_world_t *world,
    ecs_entity_t action,
    ecs_entity_t source);

/** Get the action events of the frame.
 *
 * The list is cleared by BoxInputActions in OnLoad and holds one entry per phase
 * per action, in the order the actions resolved, followed by the emitted ones.
 *
 * @param world The world
 * @param events Receives the array, which stays valid until the next frame
 * @return Number of events in the array
 */
int32_t box_input_events(
    ecs_world_t *world,
    const ActionEvent **events);

/** Hold an action from code, without a key.
 *
 * A hold makes the action active the next time the bindings resolve, which is
 * what an action command does; frames counts the frames it stays active and 0
 * holds it until box_input_release.
 *
 * @param world The world
 * @param action The action entity, which must have an InputAction
 * @param source The entity that holds the action, 0 when there is none
 * @param frames Frames the action stays active, 0 to hold it until released
 */
void box_input_hold(
    ecs_world_t *world,
    ecs_entity_t action,
    ecs_entity_t source,
    int32_t frames);

/** Release an action that code holds.
 *
 * @param world The world
 * @param action The action entity
 * @return Whether the action was held
 */
bool box_input_release(
    ecs_world_t *world,
    ecs_entity_t action);

/** Collect the key masks that are bound to an action.
 *
 * @param world The world
 * @param action The action entity
 * @param out Receives the masks
 * @param max Size of the array
 * @return Number of masks written
 */
int32_t box_input_bindings(
    ecs_world_t *world,
    ecs_entity_t action,
    uint64_t *out,
    int32_t max);

/** Look up the key code of a key name, as the keys constants spell it.
 *
 * @param name The key name, case insensitive
 * @return The key code, or -1 when no key has that name
 */
int32_t box_input_key_from_name(
    const char *name);

/** Write a key mask as the names of its modifiers and its key.
 *
 * @param mask The key mask
 * @return The name, for example Ctrl+S, the caller frees it
 */
char* box_input_mask_name(
    uint64_t mask);

/** Ignore or accept the cursor positions the platform layer reports.
 *
 * A pinned cursor keeps the position that injection gives it, which is what the
 * mouse commands need while they drive the pointer.
 *
 * @param world The world
 * @param pinned Whether platform cursor movement is ignored
 */
void box_input_cursor_pin(
    ecs_world_t *world,
    bool pinned);

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
