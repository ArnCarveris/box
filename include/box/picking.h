/**
 * @file box/picking.h
 * @brief Picking: the pointer events of a frame go to the entities under the cursor.
 *
 * Every view casts a ray from its camera into its scene, the front view first, so screen UI,
 * world UI and 3D shapes are picked the same way. Events go out through flecs.script.event.
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

/** Marks an entity that handles the pointer events reaching it.
 *
 * An event goes to the entity under the cursor and bubbles up to its first Interactive
 * ancestor (or itself), which handles it. Unhandled, it goes to the views rendered before.
 *
 * A press on one captures the pointer until that button is released: move, drag and release
 * events go to it outside its element, over another view or outside the window, mapped through
 * the view of the press. Nothing else is entered or left; deleting it ends the capture.
 */
extern ECS_TAG_DECLARE(Interactive);

/** Makes an element take the pointer events that hit it or its children, its LayoutBounds or
 * shape, so they do not pass to the views rendered before, such as the 3D scene under a HUD.
 *
 * An Interactive entity on the path of the hit still handles the event; otherwise the blocker
 * does, like an Interactive entity: it is the handler, a press on it captures the pointer, and
 * the event bubbles from the hit to it. Without it a hit nothing handles passes through.
 */
extern ECS_TAG_DECLARE(InputBlocker);

/** Added to the entity the pointer events go to while the cursor is over it.
 *
 * While a button holds a capture it stays on the captured entity, whatever is under the cursor. */
extern ECS_TAG_DECLARE(Hovered);

/** Added to the entity a press went to while a mouse button is held */
extern ECS_TAG_DECLARE(Pressed);

/** What the cursor picked, a singleton written by the engine and read only for applications */
ECS_STRUCT(PickStatus, {
    ecs_entity_t target;  /**< Entity the pointer events go to, 0 for box.input.screen */
    ecs_entity_t handler; /**< Interactive or InputBlocker entity that handles them, or 0 */
    ecs_entity_t view;    /**< View whose ray found target */
    ecs_entity_t pressed; /**< Target of the press while a button is held, 0 otherwise */
    ecs_entity_t hit;     /**< Nearest entity in the front most view that hit one, 0 for none */
    ecs_entity_t hit_view; /**< View whose ray found hit */
    vec3 point;           /**< Hit position of hit in its scene */
    float distance;       /**< Distance of point from the near plane of the view */
    vec2 cursor;          /**< Cursor in window points the pick was made at */
    ecs_entity_t captured; /**< Entity that captured the pointer with a press, 0 for none */
    vec2 drag;            /**< Cursor movement since the press while captured, else 0 */
});

/** Register the box.picking module.
 *
 * Also registers the async script function flecs.script.on.scroll(target), which resolves
 * with an EcsScriptMouseEvent whose delta is the wheel offset when the wheel moves over the
 * target, along the same path as the other pointer events.
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
