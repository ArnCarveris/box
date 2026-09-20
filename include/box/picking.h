/**
 * @file box/picking.h
 * @brief Picking components and the script events a pick under the cursor raises.
 *
 * A pick is a ray from the camera of a view through a normalized screen
 * position, resolved against the spatial index of box.collision. The collider
 * the ray hits is resolved to the entity an author cares about: the prefab or
 * template root it belongs to, unless it or an entity between it and that root
 * carries Pickable, which targets that entity instead.
 *
 * The engine picks once per frame for every enabled view that renders into the
 * surface, and only while the cursor moves: a frame in which the cursor stands
 * still reuses the hit of the last one and costs no ray query at all. The
 * entity the cursor points at carries Hovered while it does.
 *
 * Hover, press, release, click and drag are derived from that hit and raised as
 * the on.hover, on.press, on.release, on.click and on.drag script functions, so
 * a task awaits a pick the way it awaits an action:
 *
 *     template Button {
 *       async {
 *         const hit = await on.click(this)
 *       }
 *     }
 *
 * An event starts at the entity the pick resolved to and bubbles up the parent
 * chain; the first entity with a handler consumes it, so a parent only hears
 * what its children leave alone. An entity that carries an Action fires it
 * through the input action layer when a click resolves on it, so a widget and a
 * key binding share one action.
 *
 * Not everything a view draws has a collider: a source may answer the pick of a
 * view before its ray query runs, which is how box.ui resolves the cursor
 * against the rectangles of its trees and consumes the pick of the frame before
 * the scene under the UI ever sees it.
 */

#ifndef BOX_PICKING_H
#define BOX_PICKING_H

#include "types.h"

#ifdef BOX_PICKING_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Longest distance a ray of a pick travels when the settings leave it open */
#define BOX_PICK_DISTANCE (1000.0f)

/** Window points a press moves before it becomes a drag */
#define BOX_PICK_DRAG_THRESHOLD (3.0f)

/** Settings of the picking module, read by every update */
ECS_STRUCT(PickSettings, {
    float max_distance;   /**< Longest ray distance that counts as a hit, default 1000 */
    float drag_threshold; /**< Window points a press moves before it drags, default 3 */
    bool enabled;         /**< Picks run and raise their events, default true */
});

/** A descendant of a root that a pick targets instead of targeting the root.
 *
 * The tag is inherited on instantiation, so a prefab that marks one of its
 * children marks that child in every instance.
 */
extern ECS_TAG_DECLARE(Pickable);

/** The entity a pick of this frame resolved to, written by the engine */
extern ECS_TAG_DECLARE(Hovered);

/** Input action an entity fires when a click resolves on it.
 *
 * A click walks up from the entity it resolved to and the first Action it finds
 * fires through the action layer with that entity as its source, so a button of
 * a UI tree and a key binding share one action.
 */
ECS_STRUCT(Action, {
    ecs_entity_t action; /**< The action entity, which must carry an InputAction */
});

/** What a pick found, the value a script handler receives */
ECS_STRUCT(PickHit, {
    ecs_entity_t entity;   /**< Entity the pick resolved to, 0 when it hit nothing */
    ecs_entity_t collider; /**< Entity whose collider the ray hit, 0 when a source answered */
    ecs_entity_t view;     /**< View the ray was built from, 0 when a camera gave it */
    vec3 point;            /**< Hit position in world space */
    vec3 normal;           /**< Surface normal of the collider at the hit */
    vec2 cursor;           /**< Normalized cursor position the ray came from */
    float distance;        /**< Distance of the hit along the ray */
});

/** Interaction a pick event reports */
ECS_ENUM(PickEvent, {
    PickHover = 0,   /**< The cursor moved onto the entity */
    PickPress = 1,   /**< A mouse button went down on the entity */
    PickRelease = 2, /**< A mouse button came up over the entity */
    PickClick = 3,   /**< A press and its release landed on the same entity */
    PickDrag = 4     /**< The cursor moved while the entity was pressed */
});

/** Answer the pick of a view from something the ray query cannot see.
 *
 * A UI view draws elements that have no collider at all, so box.ui installs a
 * source that resolves the cursor against the rectangles of its trees. The
 * source is asked before the ray query of a view runs and one that answers
 * consumes the pick, which is what puts a UI tree over the scene behind it. The
 * hit it receives already carries the cursor and the view.
 *
 * @param world The world
 * @param view The view being picked, 0 for the camera of a world without views
 * @param cursor Normalized position in the image of the view
 * @param out Receives the entity, the point and the normal of the hit
 * @return true when the source answered the pick of this view
 */
typedef bool (*box_picking_source_t)(
    const ecs_world_t *world,
    ecs_entity_t view,
    vec2 cursor,
    PickHit *out);

/** Install the source every pick asks before it runs the ray query of a view.
 *
 * @param world The world
 * @param source The source, 0 to drop the one that was installed
 */
void box_picking_set_source(
    ecs_world_t *world,
    box_picking_source_t source);

/** Tell whether an entity a source answered lets unconsumed events through.
 *
 * A tree of box.ui that opted in keeps its element under the cursor, but an
 * event no handler of that element or its ancestors took goes on to the ray of
 * the views behind it, so a scene under a transparent overlay still hears it.
 *
 * @param world The world
 * @param entity The entity the source answered with
 * @return true when an unconsumed event falls through to the ray query
 */
typedef bool (*box_picking_through_t)(
    const ecs_world_t *world,
    ecs_entity_t entity);

/** Install the test that decides whether a source hit lets events through.
 *
 * @param world The world
 * @param through The test, 0 to drop the one that was installed
 */
void box_picking_set_through(
    ecs_world_t *world,
    box_picking_through_t through);

/** Priority the Raise record carries while a pick event is delivered */
#define BOX_PICK_PRIORITY (-1)

/** Build the world ray of a normalized position in a view.
 *
 * The position is the one the Mouse of the Input singleton reports: the origin
 * is the center of the image, x points right and y points up, and both reach 1
 * at the edge. A view of 0 takes the first camera of the world, which is what a
 * world without a View renders.
 *
 * @param world The world
 * @param view The view entity, 0 for the camera a world without views renders
 * @param cursor Normalized position in the image of the view
 * @param origin Receives the origin of the ray in world space
 * @param dir Receives the normalized direction of the ray in world space
 * @return true when the view has a camera the ray could be built from
 */
bool box_picking_ray(
    const ecs_world_t *world,
    ecs_entity_t view,
    vec2 cursor,
    vec3 *origin,
    vec3 *dir);

/** Resolve the entity a collider belongs to.
 *
 * The result is the entity itself or the nearest ancestor that carries
 * Pickable, and the prefab or template root of the collider when neither
 * carries one.
 *
 * @param world The world
 * @param collider The entity whose collider was hit
 * @return The entity a pick of that collider targets
 */
ecs_entity_t box_picking_target(
    const ecs_world_t *world,
    ecs_entity_t collider);

/** Pick the nearest entity along the ray of a normalized position in a view.
 *
 * Two colliders at the same distance resolve to the lower entity id, so the
 * result does not depend on the order the index visits them.
 *
 * @param world The world
 * @param view The view entity, 0 for the camera a world without views renders
 * @param cursor Normalized position in the image of the view
 * @param out Receives the hit, which is zeroed when the ray hit nothing
 * @return true when the ray hit a collider
 */
bool box_picking_pick(
    const ecs_world_t *world,
    ecs_entity_t view,
    vec2 cursor,
    PickHit *out);

/** Raise a pick event at the handlers of an entity and of its ancestors.
 *
 * The event starts at the entity of the hit and walks up the parent chain. The
 * bubble is delivered against the Raise record of the world, one node at a
 * time from the hit up: a handler the node resolves takes the event, because a
 * task resumes after the delivery and cannot stop it itself, a click on a node
 * with an Action fires the action and takes it, and box_event_stop called
 * inside the delivery takes it too. A taken event reaches no ancestor.
 *
 * @param world The world
 * @param event The interaction to raise
 * @param hit The hit the handlers receive
 * @return The entity that took the event, 0 when nothing did
 */
ecs_entity_t box_picking_raise(
    ecs_world_t *world,
    PickEvent event,
    const PickHit *hit);

/** Drop the hit the last pick left behind so the next update picks again.
 *
 * A pick runs while the cursor moves and answers from its last hit while it
 * stands still, so a world that changed under a still cursor has to say so: a
 * UI tree that was laid out again invalidates the pick of its frame, which is
 * what hovers an element that appeared or grew under the cursor.
 *
 * @param world The world
 */
void box_picking_invalidate(
    ecs_world_t *world);

/** Get the hit the last pick of the world left behind.
 *
 * @param world The world
 * @param out Receives the hit of the last pick
 * @return true when the cursor points at an entity
 */
bool box_picking_hovered(
    const ecs_world_t *world,
    PickHit *out);

/** Register the box.picking module.
 *
 * @param world The world
 */
void BoxPickingImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
