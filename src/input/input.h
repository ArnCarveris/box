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
    EcsScriptMouseEvent mouse;        /**< Mouse event to emit */
    EcsScriptKeyboardEvent keyboard;  /**< Keyboard event to emit */
} box_input_emit_t;

/** A synthetic hold of an action */
typedef struct box_input_hold_t {
    ecs_entity_t action; /**< The action entity */
    bool down;           /**< The hold is active */
    bool pressed;        /**< The hold started since the actions last resolved */
} box_input_hold_t;

/** Queues and backend of the module, a private singleton */
typedef struct InputDevice {
    ecs_vec_t platform;            /**< InputEvent pushed by the backend since the last frame */
    ecs_vec_t queue;               /**< InputEvent of the synthetic queue, applied in order */
    ecs_vec_t emits;               /**< box_input_emit_t of the frame */
    ecs_vec_t holds;               /**< box_input_hold_t of the synthetic action holds */
    box_input_platform_t backend;  /**< Backend that feeds real device input */
    ecs_entity_t screen;           /**< Target of the mouse events, focused for keyboard events */
    bool has_position;             /**< A cursor position arrived since focus was gained */
} InputDevice;

extern ECS_COMPONENT_DECLARE(InputDevice);

/** Private state of the action layer, a singleton */
typedef struct InputActions {
    ecs_vec_t waiters;      /**< box_input_waiter_t of the script tasks awaiting an action */
    ecs_vec_t events;       /**< box_input_action_event_t the frame raises */
    ecs_map_t bound;        /**< Action entity to the BOX_INPUT_BOUND_* bits of this frame */
    ecs_query_t *bindings;  /**< Entities with a Binding */
    ecs_query_t *actions;   /**< Entities with an Action and its ActionState */
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

/** End every synthetic hold of an action.
 *
 * @param dev The device state
 */
void box_input_hold_release_all(
    InputDevice *dev);

/** Clear the per frame state, apply the platform and synthetic events and emit them.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxInputUpdate(
    ecs_iter_t *it);

#endif
