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
    int32_t priority;    /**< Priority of the map the binding belongs to */
    bool consume;        /**< The key and the action are kept from lower maps */
} box_binding_t;

/** Where a held action was pressed and whether it has moved far enough to drag */
typedef struct box_drag_t {
    ecs_entity_t action; /**< Action that is held */
    Mouse start;         /**< Mouse of the frame the action was pressed */
    bool dragging;       /**< The cursor passed the drag threshold since then */
} box_drag_t;

/** One action a handler of this frame took */
typedef struct box_reject_t {
    ecs_entity_t action; /**< Action that was consumed */
    bool held;           /**< Whether it was held when the consume cleared it */
} box_reject_t;

/** One binding that resolved this frame */
typedef struct box_active_t {
    ecs_entity_t action; /**< Action the binding fires */
    ecs_entity_t source; /**< Entity the binding is stored on */
    uint64_t mask;       /**< Key mask that fired */
    int32_t priority;    /**< Priority of the map the binding belongs to */
    bool consume;        /**< The binding keeps its key from every lower map */
} box_active_t;

/** One action that code holds without a key */
typedef struct box_hold_t {
    ecs_entity_t action; /**< Action that is held */
    ecs_entity_t source; /**< Entity that holds it */
    int32_t frames;      /**< Frames it stays active, 0 until it is released */
} box_hold_t;

/** Queues the update system applies, and the state it keeps between frames */
typedef struct InputState {
    ecs_vec_t platform;    /**< InputEvent the platform layer queued this frame */
    ecs_vec_t injected;    /**< InputEvent injection queued, with a delay in frames */
    ecs_vec_t scheduled;   /**< InputEvent scratch holding the up events of a duration */
    ecs_vec_t bound;       /**< box_binding_t collected from the maps this frame */
    ecs_vec_t active;      /**< box_active_t of the bindings that fired this frame */
    ecs_vec_t holds;       /**< box_hold_t of the actions that code holds */
    ecs_vec_t drags;       /**< box_drag_t of the actions that are held */
    ecs_vec_t rejected;    /**< box_reject_t of the consumes of the last frame */
    ecs_query_t *bindings; /**< Entities that hold a Binding */
    ecs_query_t *maps;     /**< Entities that hold an InputMap */
    ecs_query_t *actions;  /**< Entities that hold an InputAction */
    ecs_entity_t keys;     /**< The keys enum type the key constants live under */
    box_window_t *window;  /**< Window the sink is installed on, NULL while headless */
    bool captured;         /**< Cursor capture last applied to the window */
    bool cursor_pinned;    /**< Platform cursor movement is ignored */
    bool cursor_valid;     /**< A cursor position arrived since the last focus gain */
} InputState;

extern ECS_COMPONENT_DECLARE(InputState);

/** One script task waiting for a phase of an action */
typedef struct box_waiter_t {
    ecs_entity_t action;           /**< Action the task waits for */
    ActionPhase phase;             /**< Phase that resolves the wait */
    ecs_script_future_t *future;   /**< Future of the awaiting task */
} box_waiter_t;

/** Last raise of a phase that reached no waiter, kept until one registers */
typedef struct box_missed_t {
    ecs_entity_t action; /**< Action the raise was about */
    ActionPhase phase;   /**< Phase it reached */
    ecs_entity_t source; /**< Binding or emitter the raise came from */
} box_missed_t;

/** Private list of the script tasks that await an action */
typedef struct ActionWaiters {
    ecs_vec_t waiters; /**< box_waiter_t, one per pending await */
    ecs_vec_t missed;  /**< box_missed_t, one per phase no waiter has caught up to */
} ActionWaiters;

extern ECS_COMPONENT_DECLARE(ActionWaiters);

/** Private per frame list of the action events, read by the script event layer */
typedef struct ActionEvents {
    ecs_vec_t events; /**< ActionEvent of the frame, cleared by BoxInputActions */
} ActionEvents;

extern ECS_COMPONENT_DECLARE(ActionEvents);

/** Direct pointer to the raise record of the world, never a deferred copy.
 *
 * @param world The world
 * @return The record, or NULL while the world is torn down
 */
Raise* box_input_raise(
    ecs_world_t *world);

/** Where a held action was pressed, for the raise of its drag.
 *
 * @param state The module state
 * @param action The action entity
 * @return The drag, or NULL when the action is not held
 */
const box_drag_t* box_input_drag_of(
    const InputState *state,
    ecs_entity_t action);

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

/** Key code of a single modifier bit.
 *
 * @param bit The modifier bit
 * @return The key code of the modifier, or BoxKeyNone
 */
int32_t box_input_mod_key(
    uint64_t bit);

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

/** Register the waiters, the async functions and the event and input scopes.
 *
 * @param world The world
 * @param module The box.input module entity the function scope lives under
 */
void box_input_events_init(
    ecs_world_t *world,
    ecs_entity_t module);

/** Take a key away from every map below the one a handler took it in.
 *
 * The actions those maps bound to the same key are consumed for the frame, which
 * is the handler side of what a binding with consume does before a map below it
 * resolves at all.
 *
 * @param world The world
 * @param mask Key mask the handler took
 * @param priority Priority of the map the handler took it in
 */
void box_input_stop_below(
    ecs_world_t *world,
    uint64_t mask,
    int32_t priority);

/** Forget the raises of an action that reached no waiter.
 *
 * @param world The world
 * @param action The action a handler took
 */
void box_input_forget(
    ecs_world_t *world,
    ecs_entity_t action);

/** Resolve the tasks that await a phase of an action.
 *
 * The record of the world names the raise while the handlers run, so one of them
 * stops it and the handlers behind it are left out.
 *
 * @param world The world
 * @param event The raise, which carries the action, the phase and its source
 * @param consumed Receives whether a handler stopped the raise
 * @return Number of waiters the raise resolved
 */
int32_t box_input_deliver(
    ecs_world_t *world,
    const ActionEvent *event,
    bool *consumed);

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
