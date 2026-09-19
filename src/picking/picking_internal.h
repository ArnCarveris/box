/**
 * @file picking/picking_internal.h
 * @brief Private state of the box.picking module: the queries, the cached hit and the waiters.
 */

#ifndef BOX_PICKING_INTERNAL_H
#define BOX_PICKING_INTERNAL_H

#include <box.h>

/** Views a single update of the module picks against */
#define BOX_PICK_VIEW_MAX (16)

/** One script task waiting for a pick event on an entity */
typedef struct box_pick_waiter_t {
    ecs_entity_t entity;         /**< Entity the handler listens on */
    PickEvent event;             /**< Interaction that resolves the wait */
    ecs_script_future_t *future; /**< Future of the awaiting task */
} box_pick_waiter_t;

/** One candidate view of an update, collected before the ray queries run */
typedef struct box_pick_view_t {
    ecs_entity_t entity; /**< The view entity, 0 for the camera of a world without views */
    int32_t priority;    /**< Priority of the view, the highest one picks first */
} box_pick_view_t;

/** Private state of the module, kept as a singleton on the world */
typedef struct PickState {
    ecs_query_t *views;      /**< View entities the module picks against */
    ecs_query_t *cameras;    /**< Camera entities with a world transform */
    ecs_vec_t waiters;       /**< box_pick_waiter_t, one per pending await */
    PickHit hit;             /**< Hit of the last ray query */
    PickHit press;           /**< Hit the press of the held button landed on */
    ecs_entity_t hovered;    /**< Entity that carries Hovered */
    bool hover_missed;       /**< The hover of the entity above raised to no waiter yet */
    vec2 cursor;             /**< Normalized cursor of the last ray query */
    vec2 press_window;       /**< Cursor in window points when the button went down */
    bool valid;              /**< The cached hit answers for the cursor of this frame */
    bool held;               /**< The left mouse button was down last frame */
    bool dragging;           /**< The press passed the drag threshold */
    int32_t queries;         /**< Ray queries the last update ran */
} PickState;

extern ECS_COMPONENT_DECLARE(PickState);

/** Direct pointer to the state singleton, never a deferred copy.
 *
 * @param world The world
 * @return The state, or NULL while the world is torn down
 */
PickState* box_picking_state(
    const ecs_world_t *world);

/** Collect the views a pick of this frame runs against, highest priority first.
 *
 * @param world The world
 * @param state The module state
 * @param out Receives the views
 * @param max Size of the array
 * @return Number of views that were written
 */
int32_t box_picking_views(
    const ecs_world_t *world,
    PickState *state,
    box_pick_view_t *out,
    int32_t max);

/** Normal of a collider at a point on its surface.
 *
 * @param c The collider
 * @param point The point in world space
 * @return The outward normal at that point
 */
vec3 box_picking_normal(
    const Collider *c,
    vec3 point);

/** Register the waiters and the on.hover, press, release, click and drag functions.
 *
 * @param world The world
 */
void box_picking_events_init(
    ecs_world_t *world);

/** Pick under the cursor and raise the events the input state of the frame implies.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxPickingUpdate(
    ecs_iter_t *it);

#endif
