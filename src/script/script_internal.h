/**
 * @file script/script_internal.h
 * @brief State and helpers of the incremental template instantiation.
 *
 * The queue of pending instances is a binary min heap on the squared distance
 * to the camera, rebuilt from a cached query when it is marked dirty and
 * consumed a bounded number of entries per frame.
 */

#ifndef BOX_SCRIPT_INTERNAL_H
#define BOX_SCRIPT_INTERNAL_H

#include <box.h>

/** One instance waiting for the body of its template */
typedef struct box_script_entry_t {
    ecs_entity_t instance;  /**< The entity the template component was added to */
    ecs_entity_t template_; /**< The template entity */
    double key;             /**< Squared distance to the camera, or the queue position */
    uint64_t token;
} box_script_entry_t;

/** Incremental instantiation state of a world, owned by the ScriptState singleton */
typedef struct box_script_state_t {
    ecs_vec_t queue;     /**< Heap of box_script_entry_t, the nearest instance first */
    ecs_vec_t additions;
    ecs_map_t queued;
    ecs_query_t *query;  /**< Pending instances with their optional position */
    vec3 eye;            /**< Camera position the queue was ordered against */
    bool has_eye;        /**< Whether a camera was found when the queue was ordered */
    bool dirty;          /**< The queue must be rebuilt before the next drain */
    int32_t age;         /**< Frames since the queue was rebuilt */
    uint64_t next_token;
    double next_order;
    double worst_ms;     /**< Longest a frame spent expanding instances */
} box_script_state_t;

/** Private singleton that owns the state of a world */
typedef struct ScriptState {
    box_script_state_t *state; /**< Freed when the component is destroyed */
} ScriptState;

extern ECS_COMPONENT_DECLARE(ScriptState);

/** Create the query and the queue of a world.
 *
 * @param world The world
 * @return The state
 */
box_script_state_t* box_script_state_new(
    ecs_world_t *world);

/** Free the query and the queue of a world.
 *
 * @param state The state
 */
void box_script_state_free(
    box_script_state_t *state);

void box_script_pending_add(
    box_script_state_t *state,
    ecs_entity_t instance,
    ecs_entity_t template_);

void box_script_pending_remove(
    box_script_state_t *state,
    ecs_entity_t instance,
    ecs_entity_t template_);

/** Expand pending instances until a budget of the settings runs out.
 *
 * @param world The world
 * @param state The state
 * @param settings The budget
 * @param stats Receives the counters of the frame
 */
void box_script_drain(
    ecs_world_t *world,
    box_script_state_t *state,
    const ScriptSettings *settings,
    ScriptStats *stats);

#endif
