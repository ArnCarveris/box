/**
 * @file box/picking.h
 * @brief Picking components and the script events a pick under the cursor raises.
 *
 * Once per frame while the cursor moves, each enabled view casts a ray into the
 * box.collision index. The hit resolves to the nearest Pickable entity or else
 * the prefab root, which carries Hovered while the cursor is on it.
 *
 * Hover, press, release, click and drag are raised as the on.* script functions
 * and bubble up the parent chain until a handler takes them. A click on an
 * entity with an Action fires that action.
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

/** Sink of the events no handler took.
 *
 * An event that reaches no waiter on its way up the parent chain, nor on the
 * hit behind a passthrough tree, resolves the waiters on this entity.
 *
 * A script that does `await on.click(Unhandled)` hears every click the scene
 * left alone, including one on empty space, whose hit names no entity.
 */
extern ECS_TAG_DECLARE(Unhandled);

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
 * box.ui installs one to resolve the cursor against its trees. The source runs
 * before the ray query, and an answer consumes the pick. The hit it receives
 * already carries the cursor and the view.
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
 * The position is that of the Input Mouse: origin at the image center, x right,
 * y up, reaching 1 at the edge. A view of 0 takes the first camera.
 *
 * @param world The world
 * @param view The View entity, 0 for the first camera of the world
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
 * @param view The View entity, 0 for the first camera of the world
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
 * The event walks up the parent chain from the hit entity. A node takes it when
 * it resolves a handler, when a click fires its Action, or when box_event_stop
 * is called during delivery; a taken event reaches no ancestor.
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

/** Raise a pick event at the waiters on Unhandled.
 *
 * @param world The world
 * @param event The interaction to raise
 * @param hit The hit the handlers receive
 * @return Unhandled when a waiter took the event, 0 when none was waiting
 */
ecs_entity_t box_picking_raise_unhandled(
    ecs_world_t *world,
    PickEvent event,
    const PickHit *hit);

/** Drop the hit the last pick left behind so the next update picks again.
 *
 * A still cursor reuses the last hit, so a world that changed under it, such as
 * a UI tree laid out again, calls this to pick again.
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
