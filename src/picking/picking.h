/**
 * @file picking/picking.h
 * @brief Private state of box.picking: the queued events, the queries and the pick of a cursor.
 */

#ifndef BOX_PICKING_INTERNAL_H
#define BOX_PICKING_INTERNAL_H

#include <box.h>
#include "../input/input.h"

/** Deepest render-to-texture nesting a pick follows */
#define BOX_PICK_DEPTH_MAX (4)

/** Most views of one surface a pick visits */
#define BOX_PICK_VIEW_MAX (64)

/** Most entities an event bubbles through */
#define BOX_PICK_CHAIN_MAX (256)

/** What a pick at a cursor found */
typedef struct box_pick_t {
    ecs_entity_t target;   /**< Hit of the first view that handles events, 0 when none does */
    ecs_entity_t handler;  /**< Interactive entity on the path of target */
    ecs_entity_t view;     /**< View of target */
    vec3 point;            /**< Hit position of target in its scene */
    float distance;        /**< Distance of the hit of target along its ray */
    ecs_entity_t hit;      /**< Nearest entity of the front most view that hit one */
    ecs_entity_t hit_view; /**< View of hit */
    vec3 hit_point;        /**< Hit position of hit in its scene */
    float hit_distance;    /**< Distance of the hit of hit along its ray */
} box_pick_t;

/** A hit the narrow phase of a ray cast accepted */
typedef struct box_pick_candidate_t {
    ecs_entity_t entity; /**< The entity */
    float distance;      /**< Distance along the ray */
    bool ui;             /**< A UI element hit by its bounds, drawn over shapes it ties with */
} box_pick_candidate_t;

/** A script task waiting for a wheel movement over an entity */
typedef struct box_pick_waiter_t {
    ecs_entity_t target;         /**< Entity the task listens on */
    ecs_script_future_t *future; /**< Future the event resolves */
} box_pick_waiter_t;

/** Private state of the module, kept on the heap behind the PickingState singleton */
typedef struct box_pick_state_t {
    ecs_query_t *views;     /**< Entities with a View */
    ecs_query_t *surfaces;  /**< Entities with a Surface */
    ecs_query_t *elements;  /**< UI elements outside the spatial index: LayoutBounds, TextBounds */
    ecs_vec_t emits;        /**< box_input_emit_t the router queued this frame */
    ecs_vec_t candidates;   /**< box_pick_candidate_t of the ray being cast */
    ecs_vec_t waiters;      /**< box_pick_waiter_t of on.scroll */
    ecs_entity_t screen;    /**< Target of the events no view handles */
    ecs_entity_t hovered;   /**< Entity that carries Hovered */
    ecs_entity_t pressed;   /**< Entity that carries Pressed */
    ecs_entity_t last;      /**< Target the last pointer event went to, 0 for the screen */
    vec2 cursor;            /**< Cursor of the last pointer event */
    uint32_t buttons;       /**< Buttons held at the last pointer event */
    bool has_cursor;        /**< A pointer event arrived, so the cursor is known */
} box_pick_state_t;

/** Singleton that owns the state of the module */
typedef struct PickingState {
    box_pick_state_t *state; /**< The state */
} PickingState;

extern ECS_COMPONENT_DECLARE(PickingState);

/** Pick the entities under a cursor in window points.
 *
 * @param world The world
 * @param st The module state
 * @param cursor Cursor in points of the window, origin at the top left
 * @param out Receives what the pick found
 */
void box_pick_at(
    ecs_world_t *world,
    box_pick_state_t *st,
    const vec2 cursor,
    box_pick_t *out);

/** Create the queries the picks use.
 *
 * @param world The world
 * @param st The module state
 */
void box_pick_queries_init(
    ecs_world_t *world,
    box_pick_state_t *st);

/** Send the pointer and keyboard events the router queued this frame, then pick again under a
 * still cursor so hover follows what moved.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxPickingUpdate(
    ecs_iter_t *it);

#endif
