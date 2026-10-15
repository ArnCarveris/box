/**
 * @file picking/picking.h
 * @brief Private state of box.picking: the queued events, the queries and the pick of a cursor.
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

/** Private state of the module, kept on the heap behind the PickingState singleton */
typedef struct box_pick_state_t {
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

/** Send the pointer and keyboard events the router queued this frame, then pick again under a
 * still cursor so hover follows what moved.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxPickingUpdate(
    ecs_iter_t *it);

#endif
