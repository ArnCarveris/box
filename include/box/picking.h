/**
 * @file box/picking.h
 * @brief Picking: the pointer events of a frame go to the entities under the cursor.
 *
 * Every view casts a ray from its camera into its scene, the front view first, so screen UI,
 * world UI and 3D shapes are picked the same way. Events go out through flecs.script.event,
 * and an element with an ActionTrigger drives an input Action.
 */

#ifndef BOX_PICKING_H
#define BOX_PICKING_H

#include "types.h"
#include "input.h"

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
 *
 * World actions do not see it either: a mouse button pressed over a blocker (or an element under
 * one) leaves its bindings up until it is pressed again, and the wheel over one is not counted
 * in Mouse.scroll. A press that started elsewhere keeps acting when the cursor moves over one.
 */
extern ECS_TAG_DECLARE(InputBlocker);

/** Added to the entity the pointer events go to while the cursor is over it.
 *
 * While a button holds a capture it stays on the captured entity, whatever is under the cursor. */
extern ECS_TAG_DECLARE(Hovered);

/** Added to the entity a press went to while a mouse button is held */
extern ECS_TAG_DECLARE(Pressed);

/** How the button of an ActionTrigger drives its action.
 *
 * A tap makes the action pressed in one frame and released in a later one. Taps queue, so N
 * clicks give N taps, also when they arrive in one frame.
 *
 * Scripts use the full constant names, such as ActionTrigger: {action: a, mode: ActionTriggerHold}.
 */
ECS_ENUM(ActionTriggerMode, {
    ActionTriggerClick, /**< Tap when the button comes up over the element it went down on */
    ActionTriggerPress, /**< Tap when the button goes down on the element */
    ActionTriggerHold   /**< Down from the press on the element until the button comes up */
});

/** Makes a mouse button on an element drive an input Action; adding it makes it Interactive.
 *
 * It acts on the events its entity handles as the first Interactive (or InputBlocker) entity on
 * the path of the target, in every view and for the mouse.* commands; disabled entities and
 * prefabs never trigger. Deleting the element or trigger, input.reset and focus loss end a Hold.
 *
 * The action is down while any trigger, Binding or action.down holds it, changes in the frame
 * after the pointer event (picking runs after ActionUpdate) and reports its cursor. A mouse
 * Binding of the button still sees the press unless an InputBlocker is on the path.
 */
ECS_STRUCT(ActionTrigger, {
    ecs_entity_t action;    /**< Action it drives; 0, deleted or not an Action does nothing */
    ActionTriggerMode mode; /**< How the button drives the action, Click by default */
    Key button;             /**< Mouse button, KeyMouseLeft by default; KeyNone does nothing */
});

/** The shape of the pointer cursor over an element, such as Cursor: {shape: CursorPointer}.
 *
 * The shape is that of the first entity with a Cursor on the bubbling path of PickStatus.target,
 * from it up to its handler; that is the captured entity wherever the cursor is, until the frame
 * after the release that ends the capture, like Hovered. PickStatus.shape reports it.
 *
 * Without a Cursor on the path, and over nothing that handles pointer events, the default shows.
 * box.input tells the platform when the shape changes: a glfw standard cursor natively, the CSS
 * cursor of the canvas in the browser. A captured cursor stays hidden until the capture ends.
 */
ECS_STRUCT(Cursor, {
    PointerCursor shape; /**< Shape over the element, CursorDefault by default */
});

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
    PointerCursor shape;  /**< Shape of the pointer cursor, from the Cursor on the path of target */
});

/** Get the world space ray through the cursor in a view, the ray picking casts in it.
 *
 * It follows only the camera of the view and the cursor of its surface (PickStatus.cursor), not
 * what the pick hit, InputBlockers or the views drawn over it, so an application can meet it
 * with its own geometry, such as the ground under the buildings of a scene or under a HUD.
 *
 * A view covers its whole surface, with a perspective or orthographic camera. Picking runs in
 * PreUpdate, so the systems of later phases get the ray of the cursor of the frame.
 *
 * @param world The world
 * @param view The view, an entity with View
 * @param origin Receives the point of the cursor on the near plane of the camera in world space,
 * unchanged when the function returns false
 * @param dir Receives the unit direction of the ray into the scene, unchanged on false
 * @return false when the view has no camera, its surface is not the one the cursor is on (see
 * Input.surface) or renders into a texture, no pointer event has arrived yet, or the cursor is
 * outside the view
 */
bool box_view_cursor_ray(
    const ecs_world_t *world,
    ecs_entity_t view,
    vec3 origin,
    vec3 dir);

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
