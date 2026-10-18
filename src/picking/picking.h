/**
 * @file picking/picking.h
 * @brief Private state of box.picking: the queued events, the queries, the pick of a cursor and
 * the active action triggers.
 */

#ifndef BOX_PICKING_INTERNAL_H
#define BOX_PICKING_INTERNAL_H

#include <box.h>
#include "../input/input.h"
#include "../spatial/bvh.h"

/** Deepest render-to-texture nesting a pick follows */
#define BOX_PICK_DEPTH_MAX (4)

/** Most views of one surface a pick visits */
#define BOX_PICK_VIEW_MAX (64)

/** Most entities an event bubbles through */
#define BOX_PICK_CHAIN_MAX (256)

/** Private: an element rays pass through, so its parent is hit in its place by its own area.
 *
 * box.ui puts it on the parts it builds inside a TextEdit, which are not picked separately.
 */
extern ECS_TAG_DECLARE(PickThrough);

/** A surface a pick goes through */
typedef struct box_pick_surface_t {
    ecs_entity_t view_root; /**< Parent of its views, 0 for the views no surface claims */
    float width;            /**< Width in points */
    float height;           /**< Height in points */
} box_pick_surface_t;

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
    box_pick_surface_t surface; /**< Surface of the view of target */
    vec2 surface_cursor;   /**< Cursor of the pick in points of surface */
} box_pick_t;

/** The entity a held button captured, whose events it keeps until the button is released */
typedef struct box_pick_capture_t {
    ecs_entity_t target;        /**< Entity the press went to, 0 when nothing is captured */
    ecs_entity_t handler;       /**< Interactive entity that handles its events */
    ecs_entity_t view;          /**< View the press happened in */
    box_pick_surface_t surface; /**< Surface of that view */
    vec2 offset;                /**< Cursor in surface points minus cursor in window points */
    vec2 start;                 /**< Window cursor of the press */
    vec3 point;                 /**< Position of the press in the scene of the view */
    uint32_t buttons;           /**< Button bits whose release ends the capture */
} box_pick_capture_t;

/** A hit the narrow phase of a ray cast accepted */
typedef struct box_pick_candidate_t {
    ecs_entity_t entity; /**< The entity */
    float distance;      /**< Distance along the ray */
    bool ui;             /**< A UI element hit by its bounds, drawn over shapes it ties with */
} box_pick_candidate_t;

/** The triangle hierarchy of a unit mesh that picks test, which holds a reference to the mesh
 * so its handle keeps its content while the hierarchy exists */
typedef struct box_pick_mesh_t {
    box_bvh_t tree;  /**< Triangles of the unit mesh, the item of a leaf is a triangle index */
    uint32_t handle; /**< Mesh handle */
    int32_t idle;    /**< Frames in a row no shape used the mesh */
    bool building;   /**< A job builds the tree and reads the mesh */
    bool ready;      /**< The tree is built */
} box_pick_mesh_t;

/** A script task waiting for a wheel movement over an entity */
typedef struct box_pick_waiter_t {
    ecs_entity_t target;         /**< Entity the task listens on */
    ecs_script_future_t *future; /**< Future the event resolves */
} box_pick_waiter_t;

/** Mouse buttons a trigger can use, KeyMouseLeft to KeyMouseRight */
#define BOX_PICK_BUTTONS (3)

/** The action a Hold trigger holds while its button is down */
typedef struct box_pick_hold_t {
    ecs_entity_t source; /**< Element of the trigger, 0 when the button holds nothing */
    ecs_entity_t action; /**< The action it holds */
} box_pick_hold_t;

typedef struct box_pick_state_t box_pick_state_t;

/** Follow the start of the events of a frame, before any of them is delivered.
 *
 * @param world The world
 * @param st The module state
 * @param ctx Context of the hook
 */
typedef void (*box_pick_begin_t)(
    ecs_world_t *world,
    box_pick_state_t *st,
    void *ctx);

/** Follow a keyboard event after it went out through flecs.script.event.
 *
 * @param world The world
 * @param st The module state
 * @param emit The keyboard event
 * @param ctx Context of the hook
 */
typedef void (*box_pick_key_t)(
    ecs_world_t *world,
    box_pick_state_t *st,
    const box_input_emit_t *emit,
    void *ctx);

/** Follow a pointer event after it was delivered along the path of its pick.
 *
 * @param world The world
 * @param st The module state
 * @param src The event
 * @param pick The pick the event was delivered by
 * @param pressed The buttons that went down with the event
 * @param released The buttons that came up with the event
 * @param ctx Context of the hook
 */
typedef void (*box_pick_pointer_t)(
    ecs_world_t *world,
    box_pick_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick,
    uint32_t pressed,
    uint32_t released,
    void *ctx);

/** Follows the events the router delivers, which box.ui installs for the keyboard focus */
typedef struct box_pick_hook_t {
    box_pick_begin_t begin;     /**< Called before the events of a frame, NULL for none */
    box_pick_key_t key;         /**< Called after each keyboard event, NULL for none */
    box_pick_pointer_t pointer; /**< Called after each pointer event, NULL for none */
    void *ctx;                  /**< Passed to the callbacks */
} box_pick_hook_t;

/** Private state of the module, kept on the heap behind the PickingState singleton */
struct box_pick_state_t {
    ecs_query_t *views;     /**< Entities with a View */
    ecs_query_t *surfaces;  /**< Entities with a Surface */
    ecs_vec_t emits;        /**< box_input_emit_t the router queued this frame */
    ecs_vec_t candidates;   /**< box_pick_candidate_t of the ray being cast */
    ecs_vec_t waiters;      /**< box_pick_waiter_t of on.scroll */
    ecs_map_t meshes;       /**< Mesh handle to the box_pick_mesh_t of a mesh with many triangles */
    ecs_entity_t screen;    /**< Target of the events no view handles */
    ecs_entity_t hovered;   /**< Entity that carries Hovered */
    ecs_entity_t pressed;   /**< Entity that carries Pressed */
    box_pick_capture_t capture; /**< The capture of the held button */
    ecs_entity_t last;      /**< Target the last pointer event went to, 0 for the screen */
    vec2 cursor;            /**< Cursor of the last pointer event */
    uint32_t buttons;       /**< Buttons held at the last pointer event */
    bool has_cursor;        /**< A pointer event arrived, so the cursor is known */
    ecs_entity_t clicks[BOX_PICK_BUTTONS]; /**< Click trigger a press of a button went to */
    box_pick_hold_t holds[BOX_PICK_BUTTONS]; /**< Hold trigger of each button */
    box_pick_hook_t hook;   /**< Follows the delivered events */
};

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

/** Pick the entities under a cursor like box_pick_at, except that while a button holds a
 * capture the target, handler and view are the captured ones wherever the cursor is.
 *
 * @param world The world
 * @param st The module state
 * @param cursor Cursor in points of the window, origin at the top left
 * @param out Receives what the pick found
 */
void box_pick_resolve(
    ecs_world_t *world,
    box_pick_state_t *st,
    const vec2 cursor,
    box_pick_t *out);

/** Collect the views of the surface the window cursor is on (see box_pick_at), the last
 * rendered first.
 *
 * @param world The world
 * @param st The module state
 * @param out Receives the views, BOX_PICK_VIEW_MAX entries
 * @return Number of views
 */
int32_t box_pick_input_views(
    ecs_world_t *world,
    box_pick_state_t *st,
    ecs_entity_t *out);

/** Check whether one element is drawn after another in the hierarchy order of 2D drawing.
 *
 * Floating elements and their descendants draw after the others. Parents draw before their
 * children, siblings by Position z, then OrderedChildren order or id.
 *
 * @param world The world
 * @param a The first element
 * @param b The second element
 * @return Whether a is drawn after b
 */
bool box_pick_drawn_after(
    const ecs_world_t *world,
    ecs_entity_t a,
    ecs_entity_t b);

/** Install the hook that follows the delivered events, replacing the previous one.
 *
 * @param world The world
 * @param hook The hook, NULL to remove it
 */
void box_pick_hook_set(
    ecs_world_t *world,
    const box_pick_hook_t *hook);

/** Drop the triangle hierarchies of meshes no shape has used for a while.
 *
 * @param world The world
 * @param st The module state
 */
void box_pick_meshes_prune(
    ecs_world_t *world,
    box_pick_state_t *st);

/** Free every triangle hierarchy without releasing its mesh, when the world goes away.
 *
 * @param st The module state
 */
void box_pick_meshes_fini(
    box_pick_state_t *st);

/** Create the queries the picks use.
 *
 * @param world The world
 * @param st The module state
 */
void box_pick_queries_init(
    ecs_world_t *world,
    box_pick_state_t *st);

/** Feed the ActionTriggers of a pointer event to box.input after it was delivered: a press
 * starts the trigger of the handler of its pick, a release ends a hold anywhere and ends a click
 * while the cursor is still over the element of the press.
 *
 * @param world The world
 * @param st The module state
 * @param src The event
 * @param pick The pick the event was delivered by
 * @param pressed The buttons that went down with the event
 * @param released The buttons that came up with the event
 */
void box_pick_triggers(
    ecs_world_t *world,
    box_pick_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick,
    uint32_t pressed,
    uint32_t released);

/** Release the holds of triggers that were disabled, changed or lost their action.
 *
 * @param world The world
 * @param st The module state
 */
void box_pick_triggers_check(
    ecs_world_t *world,
    box_pick_state_t *st);

/** Register ActionTrigger with its hooks.
 *
 * @param world The world
 * @param st The module state
 */
void box_pick_triggers_import(
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
