/**
 * @file input/input_internal.h
 * @brief Private state of the box.input module: the event queues and the window binding.
 */

#ifndef BOX_INPUT_INTERNAL_H
#define BOX_INPUT_INTERNAL_H

#include <box.h>
#include "../platform/platform.h"

/** Queues the update system applies, and the state it keeps between frames */
typedef struct InputState {
    ecs_vec_t platform;   /**< InputEvent the platform layer queued this frame */
    ecs_vec_t injected;   /**< InputEvent injection queued, with a delay in frames */
    ecs_vec_t scheduled;  /**< InputEvent scratch holding the up events of a duration */
    box_window_t *window; /**< Window the sink is installed on, NULL while headless */
    bool captured;        /**< Cursor capture last applied to the window */
    bool cursor_valid;    /**< A cursor position arrived since the last focus gain */
} InputState;

extern ECS_COMPONENT_DECLARE(InputState);

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

#endif
