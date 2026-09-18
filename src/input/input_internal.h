/**
 * @file input/input_internal.h
 * @brief Private state of the box.input module: the event queues and the window binding.
 */

#ifndef BOX_INPUT_INTERNAL_H
#define BOX_INPUT_INTERNAL_H

#include <box.h>
#include "../platform/platform.h"

/** One binding of the frame, from a Binding component or from a pair of a map */
typedef struct box_binding_t {
    uint64_t mask;       /**< Key mask of the binding */
    ecs_entity_t action; /**< Action the mask fires */
    ecs_entity_t entity; /**< Entity the binding is stored on */
} box_binding_t;

/** One binding that resolved this frame */
typedef struct box_active_t {
    ecs_entity_t action; /**< Action the binding fires */
    ecs_entity_t source; /**< Entity the binding is stored on */
    uint64_t mask;       /**< Key mask that fired */
} box_active_t;

/** Queues the update system applies, and the state it keeps between frames */
typedef struct InputState {
    ecs_vec_t platform;    /**< InputEvent the platform layer queued this frame */
    ecs_vec_t injected;    /**< InputEvent injection queued, with a delay in frames */
    ecs_vec_t scheduled;   /**< InputEvent scratch holding the up events of a duration */
    ecs_vec_t bound;       /**< box_binding_t collected from the maps this frame */
    ecs_vec_t active;      /**< box_active_t of the bindings that fired this frame */
    ecs_query_t *bindings; /**< Entities that hold a Binding */
    ecs_query_t *maps;     /**< Entities that hold an InputMap */
    ecs_query_t *actions;  /**< Entities that hold an InputAction */
    ecs_entity_t keys;     /**< The keys enum type the key constants live under */
    box_window_t *window;  /**< Window the sink is installed on, NULL while headless */
    bool captured;         /**< Cursor capture last applied to the window */
    bool cursor_valid;     /**< A cursor position arrived since the last focus gain */
} InputState;

extern ECS_COMPONENT_DECLARE(InputState);

/** One script task waiting for a phase of an action */
typedef struct box_waiter_t {
    ecs_entity_t action;           /**< Action the task waits for */
    ActionPhase phase;             /**< Phase that resolves the wait */
    ecs_script_future_t *future;   /**< Future of the awaiting task */
} box_waiter_t;

/** Private list of the script tasks that await an action */
typedef struct ActionWaiters {
    ecs_vec_t waiters; /**< box_waiter_t, one per pending await */
} ActionWaiters;

extern ECS_COMPONENT_DECLARE(ActionWaiters);

/** Private per frame list of the action events, read by the script event layer */
typedef struct ActionEvents {
    ecs_vec_t events; /**< ActionEvent of the frame, cleared by BoxInputActions */
} ActionEvents;

extern ECS_COMPONENT_DECLARE(ActionEvents);

/** Direct pointer to the queue singleton, never a deferred copy.
 *
 * @param world The world
 * @return The queues
 */
InputState* box_input_state(
    ecs_world_t *world);

/** Sink the platform layer reports its events to.
 *
 * @param ctx The queues
 * @param event The event the OS reported
 */
void box_input_sink(
    void *ctx,
    const box_input_event_t *event);

/** Register the key name constants under the keys scope.
 *
 * @param world The world
 * @return The enum type holding the constants
 */
ecs_entity_t box_input_keys_init(
    ecs_world_t *world);

/** Clear the key, button and text state, as losing focus does.
 *
 * @param in The Input singleton
 */
void box_input_clear_keys(
    Input *in);

/** Install the event sink on the window of a surface and apply its cursor capture.
 *
 * @param it Iterator over the surfaces that have runtime objects
 */
void BoxInputBind(
    ecs_iter_t *it);

/** Clear the per frame state, then apply the platform and the injected events.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxInputUpdate(
    ecs_iter_t *it);

/** Key mask a key code stands for.
 *
 * A modifier key gives the modifier bits it stands for, so that a mask combines
 * with it, and every other key gives its own code.
 *
 * @param code The key code
 * @return The key mask of the code
 */
uint64_t box_input_key_mask(
    int32_t code);

/** Create the queries the action layer resolves its bindings with.
 *
 * @param world The world
 * @param state The module state
 */
void box_input_actions_init(
    ecs_world_t *world,
    InputState *state);

/** Free the queries of the action layer.
 *
 * @param state The module state
 */
void box_input_queries_fini(
    InputState *state);

/** Register the waiters and the on.pressed, on.down and on.up async functions.
 *
 * @param world The world
 */
void box_input_events_init(
    ecs_world_t *world);

/** Resolve the tasks that await a phase of an action.
 *
 * @param world The world
 * @param action The action that fired
 * @param phase The phase it reached
 * @param source Binding or emitter the phase comes from
 */
void box_input_deliver(
    ecs_world_t *world,
    ecs_entity_t action,
    ActionPhase phase,
    ecs_entity_t source);

/** Raise the action events of the frame at the tasks that await them.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxInputRaise(
    ecs_iter_t *it);

/** Resolve the bindings of the maps into the state of the actions of the frame.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxInputActions(
    ecs_iter_t *it);

#endif
