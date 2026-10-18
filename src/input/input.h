/**
 * @file input/input.h
 * @brief Private state of the box.input module: the event queues, the backend and the key tables.
 */

#ifndef BOX_INPUT_INTERNAL_H
#define BOX_INPUT_INTERNAL_H

#include <box.h>

/** One script event the frame emits after the Input singleton is written */
typedef struct box_input_emit_t {
    bool is_mouse;                    /**< The event is a mouse event, else a keyboard event */
    Key key;                          /**< Key of a keyboard event */
    bool is_scroll;                   /**< The mouse event is a wheel movement in its delta */
    EcsScriptMouseEvent mouse;        /**< Mouse event to emit, aimed at the screen */
    EcsScriptKeyboardEvent keyboard;  /**< Keyboard event to emit */
} box_input_emit_t;

/** Take one event of the frame in place of flecs.script.event.
 *
 * @param world The world
 * @param emit The event, a mouse event aimed at the screen entity or a keyboard event
 * @param ctx Context of the router
 */
typedef void (*box_input_route_t)(
    ecs_world_t *world,
    const box_input_emit_t *emit,
    void *ctx);

/** Check whether an element that takes pointer events from the world is under a cursor.
 *
 * @param world The world
 * @param cursor Cursor in window points
 * @param ctx Context of the router
 * @return Whether presses and the wheel at the cursor do not reach the actions
 */
typedef bool (*box_input_blocks_t)(
    ecs_world_t *world,
    const vec2 cursor,
    void *ctx);

/** Receives the events of a frame in order instead of flecs.script.event.
 *
 * box.picking installs one to aim the mouse events at the entities under the cursor.
 */
typedef struct box_input_router_t {
    box_input_route_t route;   /**< Takes one event */
    box_input_blocks_t blocks; /**< Finds a blocking element under the cursor, NULL for none */
    void *ctx;                 /**< Passed to route and blocks */
} box_input_router_t;

/** Take keys and text of a frame from Input before the bindings and other consumers see them.
 *
 * It runs after the events of the frame were applied, before Input is marked modified, so the
 * console capture runs after it. box.ui installs one for the keyboard focus.
 *
 * @param world The world
 * @param in The Input singleton, which it may change
 * @param ctx Context of the claim
 */
typedef void (*box_input_claim_t)(
    ecs_world_t *world,
    Input *in,
    void *ctx);

/** A synthetic hold of an action, or what an element with an ActionTrigger feeds it */
typedef struct box_input_hold_t {
    ecs_entity_t action; /**< The action entity */
    ecs_entity_t source; /**< Element of the trigger, 0 for a synthetic hold */
    vec2 cursor;         /**< Cursor of the last pointer event of the trigger */
    int32_t taps;        /**< Taps of the trigger still to resolve, the current one included */
    bool tapped;         /**< The current tap holds the action down this frame */
    bool down;           /**< The hold is active */
    bool pressed;        /**< The hold started since the actions last resolved */
} box_input_hold_t;

/** Last window size and cursor position a surface reported */
typedef struct box_input_surface_t {
    ecs_entity_t surface; /**< The surface */
    vec2 size;            /**< Its size in points */
    vec2 position;        /**< Cursor position of its last move, in its points */
    bool has_size;        /**< The surface reported a size */
    bool has_position;    /**< The surface reported a cursor position */
} box_input_surface_t;

/** Queues and backend of the module, a private singleton */
typedef struct InputDevice {
    ecs_vec_t platform;            /**< InputEvent pushed by the backend since the last frame */
    ecs_vec_t queue;               /**< InputEvent of the synthetic queue, applied in order */
    ecs_vec_t emits;               /**< box_input_emit_t of the frame */
    ecs_vec_t holds;               /**< box_input_hold_t of the synthetic action holds */
    box_input_platform_t backend;  /**< Backend that feeds real device input */
    box_input_router_t router;     /**< Takes the events of a frame, emitted directly when unset */
    box_input_claim_t claim;       /**< Takes keys of a frame from Input, NULL for none */
    void *claim_ctx;               /**< Passed to claim */
    ecs_entity_t screen;           /**< Target of the mouse events, focused for keyboard events */
    ecs_vec_t text;                /**< char of the text typed this frame, without terminator */
    ecs_vec_t surfaces;            /**< box_input_surface_t of the surfaces that sent events */
    ecs_entity_t source;           /**< Surface stamped on pushed events that name none */
    ecs_entity_t capture_surface;  /**< Surface the backend captured the cursor in */
    bool capture_applied;          /**< The backend was asked to capture the cursor */
    bool blocked[3];               /**< The last press of a mouse button hit a blocking element */
    bool has_position;             /**< A cursor position arrived since focus or release */
    bool held[BOX_KEY_COUNT];      /**< Keys held by the applied events, which writes to
                                    *   Input.keys from outside (the console) do not change */
    char *clipboard;               /**< In memory clipboard, the last text set or pasted by the
                                    *   platform, NULL for none */
    PointerCursor cursor;          /**< Shape of the pointer cursor shown */
    PointerCursor cursor_applied;  /**< Shape the backend was last told */
} InputDevice;

extern ECS_COMPONENT_DECLARE(InputDevice);

/** Cursor of the pointer event that triggered an action, which its events report */
typedef struct box_input_cursor_t {
    ecs_entity_t action; /**< The action entity */
    vec2 cursor;         /**< Cursor in window points */
} box_input_cursor_t;

/** Private state of the action layer, a singleton */
typedef struct InputActions {
    ecs_vec_t waiters;                 /**< box_input_waiter_t of the tasks awaiting an action */
    ecs_vec_t events;                  /**< box_input_action_event_t the frame raises */
    ecs_map_t bound;                   /**< Action entity to the BOX_INPUT_BOUND_* bits */
    ecs_vec_t cursors;                 /**< box_input_cursor_t of the triggers this frame */
    ecs_query_t *bindings;             /**< Entities with a Binding */
    ecs_query_t *actions;              /**< Entities with an Action and its ActionState */
    uint8_t press_mods[BOX_KEY_COUNT]; /**< Modifier keys held when each key last went down */
} InputActions;

extern ECS_COMPONENT_DECLARE(InputActions);

/** Register the action hooks, the ActionUpdate system and the on.* functions.
 *
 * @param world The world
 */
void box_input_actions_import(
    ecs_world_t *world);

/** Get the device state of a world.
 *
 * @param world The world or a stage
 * @return The device state
 */
InputDevice* box_input_device(
    ecs_world_t *world);

/** Set the surface that platform events pushed from now on come from, for events that do not
 * name one. Window callbacks set it around their pushes.
 *
 * @param world The world
 * @param surface The surface, 0 for none
 */
void box_input_platform_source(
    ecs_world_t *world,
    ecs_entity_t surface);

/** Free the text of the events of a queue and empty it.
 *
 * @param events The InputEvent queue
 */
void box_input_events_clear(
    ecs_vec_t *events);

/** Set the shape of the pointer cursor, telling the backend when it changed.
 *
 * box.picking sets it once per frame from the Cursor of the element under the cursor.
 *
 * @param world The world
 * @param shape The shape, an invalid shape is the default
 */
void box_input_cursor_shape(
    ecs_world_t *world,
    PointerCursor shape);

/** Tell the backend the shape of the pointer cursor when it is not the one it was last told.
 *
 * @param world The world
 * @param dev The device state
 */
void box_input_cursor_sync(
    ecs_world_t *world,
    InputDevice *dev);

/** Ask the backend to release a cursor it was asked to capture.
 *
 * @param world The world
 * @param dev The device state
 */
void box_input_capture_release(
    ecs_world_t *world,
    InputDevice *dev);

/** Check whether the last press of a mouse button hit an element that blocks it from actions.
 *
 * @param dev The device state
 * @param key The key
 * @return Whether the key is a mouse button whose press was blocked
 */
bool box_input_key_blocked(
    const InputDevice *dev,
    Key key);

/** Install the router that takes the events of every frame, replacing the previous one.
 *
 * Without a router mouse events go to the screen entity and wheel movements are dropped.
 *
 * @param world The world
 * @param router The router, NULL to emit the events directly again
 */
void box_input_router_set(
    ecs_world_t *world,
    const box_input_router_t *router);

/** Install the claim that takes keys and text of every frame, replacing the previous one.
 *
 * @param world The world
 * @param claim The claim, NULL to remove it
 * @param ctx Passed to the claim
 */
void box_input_claim_set(
    ecs_world_t *world,
    box_input_claim_t claim,
    void *ctx);

/** Mark the keys and the required modifier keys of the bindings of an action.
 *
 * The console keeps its toggle with it, box.ui keeps the toggle of the console from KeyCapture.
 *
 * @param world The world
 * @param action The action, 0 marks nothing
 * @param keys Set to true for every key a binding of the action uses, BOX_KEY_COUNT entries
 */
void box_input_action_keys(
    ecs_world_t *world,
    ecs_entity_t action,
    bool *keys);

/** Get the screen entity, the target of mouse events that no entity takes.
 *
 * @param world The world
 * @return The box.input.screen entity
 */
ecs_entity_t box_input_screen(
    ecs_world_t *world);

/** Count the frames until the synthetic queue is drained.
 *
 * @param world The world
 * @return Frames the queue needs to apply every event, 0 when it is empty
 */
int32_t box_input_pending_frames(
    ecs_world_t *world);

/** Get the key value a keyboard event carries for a key, as a browser reports it.
 *
 * @param key The key
 * @return The value, such as a, Enter or ArrowLeft, NULL for a mouse button or invalid key
 */
const char* box_input_key_value(
    Key key);

/** Get the key that types a character without modifiers.
 *
 * @param code The unicode code point
 * @return The key, KeyNone when no key types it
 */
Key box_input_key_from_char(
    int32_t code);

/** Start or end a synthetic hold of an action.
 *
 * @param dev The device state
 * @param action The action entity
 * @param down Whether the hold starts
 */
void box_input_hold(
    InputDevice *dev,
    ecs_entity_t action,
    bool down);

/** End every synthetic hold and trigger hold, and drop the taps of the triggers.
 *
 * @param dev The device state
 */
void box_input_hold_release_all(
    InputDevice *dev);

/** Tap an action for an element: the action goes down in the next resolve and comes up in the
 * one after. Taps queue, so each one goes down and up in its own frames.
 *
 * box.picking calls it for an ActionTrigger. Its action events report the cursor.
 *
 * @param dev The device state
 * @param source The element of the trigger
 * @param action The action entity
 * @param cursor Cursor of the pointer event in window points
 */
void box_input_trigger_tap(
    InputDevice *dev,
    ecs_entity_t source,
    ecs_entity_t action,
    const vec2 cursor);

/** Start or end the hold of an action by an element. It combines with the bindings and the
 * other holds of the action, which stays down while any of them holds it.
 *
 * @param dev The device state
 * @param source The element of the trigger
 * @param action The action entity
 * @param down Whether the hold starts
 * @param cursor Cursor of the pointer event in window points
 */
void box_input_trigger_hold(
    InputDevice *dev,
    ecs_entity_t source,
    ecs_entity_t action,
    bool down,
    const vec2 cursor);

/** Move the cursor of the triggers of an element that are still active, without starting or
 * ending one, so a hold that input.reset ended stays up.
 *
 * @param dev The device state
 * @param source The element of the trigger
 * @param cursor Cursor of the pointer event in window points
 */
void box_input_trigger_cursor(
    InputDevice *dev,
    ecs_entity_t source,
    const vec2 cursor);

/** Clear the per frame state, apply the platform and synthetic events and emit them.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxInputUpdate(
    ecs_iter_t *it);

#endif
