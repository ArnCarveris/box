/**
 * @file transition/transition_internal.h
 * @brief Private types and helpers of the box.transition module.
 */

#ifndef BOX_TRANSITION_INTERNAL_H
#define BOX_TRANSITION_INTERNAL_H

#include <box.h>
#include <string.h>
#include "../renderer/transition.h"

/** Number of member entities of a component a Transition may key */
#define BOX_TRANSITION_MEMBER_MAX (8)

/** Representation of one float of a transitionable component */
typedef enum {
    BoxTransitionF32 = 0, /**< The value is a float */
    BoxTransitionU8 = 1   /**< The value is a byte holding 0 to 255 */
} box_transition_kind_t;

/** Where one interpolated float lives inside a component value */
typedef struct {
    uint16_t offset;           /**< Byte offset in the component */
    box_transition_kind_t kind; /**< Representation at that offset */
} box_transition_slot_t;

/** Range of floats a member entity of a component covers */
typedef struct {
    ecs_entity_t member; /**< The member entity */
    uint8_t index;       /**< First float of the member */
    uint8_t count;       /**< Number of floats of the member */
} box_transition_member_t;

/** Float layout of a transitionable component, held by the component entity */
typedef struct {
    box_transition_slot_t slots[BOX_TRANSITION_MAX];        /**< One entry per float */
    box_transition_member_t members[BOX_TRANSITION_MEMBER_MAX]; /**< Keyable members */
    ecs_entity_t key;     /**< Entity a Transition keys the component by */
    uint8_t count;        /**< Number of floats the component is made of */
    uint8_t member_count; /**< Number of keyable members */
} TransitionType;

extern ECS_COMPONENT_DECLARE(TransitionType);

/** One value of one component that a Transition governs.
 *
 * A record outlives the transition it describes: once it has finished it holds
 * the value that is displayed, which is where the next assignment starts from.
 */
typedef struct {
    ecs_entity_t component;                      /**< Component the floats live in */
    double start;                                /**< Time the transition started */
    float from[BOX_TRANSITION_MAX];              /**< Value displayed when it started */
    float to[BOX_TRANSITION_MAX];                /**< Value that was assigned */
    float duration;                              /**< Seconds the value takes */
    float delay;                                 /**< Seconds before the value moves */
    TimingFunction timing;                       /**< Curve the progress follows */
    box_transition_slot_t slots[BOX_TRANSITION_MAX]; /**< Where the floats live */
    uint8_t index;                               /**< First float of the component */
    uint8_t count;                               /**< Number of floats of the record */
} box_transition_t;

/** Transitions of one entity, present only while the entity has any */
typedef struct {
    ecs_vec_t records; /**< Vector of box_transition_t, one per governed value */
    double until;      /**< Time the last transition in flight ends */
    bool dirty;        /**< A view has not shown the end of the last transition yet */
    bool published;    /**< The entity carries a (TransitionValue, Component) pair */
} TransitionState;

extern ECS_COMPONENT_DECLARE(TransitionState);

/** Interpolate one record at a time.
 *
 * The interpolation itself lives in the renderer header, because the renderer
 * thread runs it over the records the packet carries.
 *
 * @param record The record
 * @param time The time to evaluate at
 * @param out Receives record->count floats
 * @return true while the transition has not reached its end
 */
bool box_transition_eval(
    const box_transition_t *record,
    double time,
    float *out);

/** Overwrite the governed floats of a component value with the displayed ones.
 *
 * @param state The transitions of the entity
 * @param component The component the value belongs to
 * @param time The time to evaluate at
 * @param value The authored value, receives the displayed one
 * @return true when a record wrote into the value
 */
bool box_transition_display(
    const TransitionState *state,
    ecs_entity_t component,
    double time,
    void *value);

/** Read the floats of a component value.
 *
 * @param slots Where the floats live
 * @param count Number of floats
 * @param value The component value
 * @param out Receives count floats
 */
void box_transition_read(
    const box_transition_slot_t *slots,
    int32_t count,
    const void *value,
    float *out);

/** Write floats back into a component value.
 *
 * @param slots Where the floats live
 * @param count Number of floats
 * @param in The floats
 * @param value The component value to write into
 */
void box_transition_write(
    const box_transition_slot_t *slots,
    int32_t count,
    const float *in,
    void *value);

/** Derive the float layout of a component from its reflection.
 *
 * @param world The world
 * @param component The component entity
 * @param key Entity a Transition keys the component by, for the member names
 * @param out Receives the layout
 * @return 0 when the component can be interpolated, nonzero when it cannot
 */
int box_transition_layout(
    const ecs_world_t *world,
    ecs_entity_t component,
    ecs_entity_t key,
    TransitionType *out);

/** Give a component a float layout and watch it for assignments.
 *
 * @param world The world, used for reading
 * @param stage The stage that queues the writes
 * @param component The component entity
 * @param key Entity a Transition keys the component by
 */
void box_transition_add_type(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t component,
    ecs_entity_t key);

/** Register the OnSet observer that starts the transitions of a component.
 *
 * @param world The world
 * @param component The component entity
 */
void box_transition_watch(
    ecs_world_t *world,
    ecs_entity_t component);

/** Register the observer that turns Transitionable into a float layout.
 *
 * @param world The world
 */
void box_transition_types_init(
    ecs_world_t *world);

/** Register the observers and the system that keep transitions running.
 *
 * @param world The world
 */
void box_transition_records_init(
    ecs_world_t *world);

/** Start the transitions a Transition asks for after a component was assigned.
 *
 * @param world The world
 * @param e The entity
 * @param component The component that was assigned
 * @param state The transitions of the entity
 */
void box_transition_assign(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component,
    TransitionState *state);

#endif
