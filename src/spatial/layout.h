/**
 * @file spatial/layout.h
 * @brief Private types and functions of the spatial layout solver, shared by its source files.
 */

#ifndef BOX_SPATIAL_LAYOUT_PRIVATE_H
#define BOX_SPATIAL_LAYOUT_PRIVATE_H

#include "../geometry/mesh_cache.h"
#include "spatial.h"

/** What the layout wrote to an entity, so that it can be restored when the layout lets go */
typedef struct LayoutOutput {
    vec3 source_scale; /**< Scale of the shape before the layout fitted it */
    bool fitted;       /**< The layout fitted the scale of the shape */
    bool manual;       /**< The layout added ManualTransform to place the entity */
} LayoutOutput;

/** The prototype an entity in an array instance was copied from */
typedef struct PrototypeSource {
    ecs_entity_t entity; /**< The prototype entity */
} PrototypeSource;

/** A node keeps its results between passes.
 *
 * It is reset and solved again when an input of its own entity changed, or when a node it read
 * from (deps) was reset.
 */
typedef struct layout_node_t {
    ecs_entity_t entity;           /**< The entity of the node */
    int pose_state;                /**< 0 unsolved, 1 solving, 2 solved */
    int bounds_state;              /**< 0 unsolved, 1 solving, 2 solved */
    int arrange_state;             /**< 0 unsolved, 1 solving, 2 solved */
    int intrinsic_state;           /**< 0 unsolved, 1 solving, 2 solved */
    int basis_state;               /**< 0 unsolved, 1 solving, 2 solved */
    mat4 pose;                     /**< Solved world transform */
    mat4 basis;                    /**< Solved world orientation and scale, no translation */
    vec3 min;                      /**< Minimum of the resolved bounds */
    vec3 max;                      /**< Maximum of the resolved bounds */
    vec3 intrinsic_min;            /**< Minimum of the bounds before fitting */
    vec3 intrinsic_max;            /**< Maximum of the bounds before fitting */
    vec3 fit;                      /**< Scale factor per axis a Fit applies */
    vec3 placement;                /**< Offset a parent Distribute assigned */
    bool has_bounds;               /**< The resolved bounds are valid */
    bool has_intrinsic;            /**< The intrinsic bounds are valid */
    bool controlled;               /**< The layout places the entity */
    bool placed;                   /**< A parent Distribute assigned a placement */
    bool requested;                /**< The node is solved and published when it is reset */
    bool arranged;                 /**< The node arranged its children */
    SpatialDiagnostic error;       /**< First error of the node in the current solve */
    SpatialDiagnostic array_error; /**< Error of the last array sync, kept until the next sync */
    int32_t index;                 /**< Position in layout_ctx_t::list */
    uint64_t epoch;                /**< Pass that last reset the node */
    uint64_t frame;                /**< Pass that last added the node to layout_ctx_t::frame */
    uint64_t mark;                 /**< Pass that last invalidated the node */
    uint64_t array_epoch;          /**< Pass that last synced the instances of the node */
    ecs_vec_t deps;                /**< Nodes the results of the node were computed from */
} layout_node_t;

/** State of the layout solver of a world */
typedef struct layout_ctx_t {
    ecs_world_t *world;     /**< The world */
    ecs_map_t nodes;        /**< Nodes by entity */
    ecs_vec_t list;         /**< Every node, by layout_node_t::index */
    ecs_vec_t frame;        /**< Nodes reset, created or given an error in the current pass */
    ecs_vec_t seeds;        /**< Entities with changed inputs since the last pass */
    ecs_vec_t dynamic;      /**< Entities of nodes that follow DynamicTransform */
    ecs_vec_t queue;        /**< Nodes invalidated in the current pass */
    ecs_vec_t offsets;      /**< Range in users of the users of every node index */
    ecs_vec_t users;        /**< Node indices of the nodes that read from a node */
    ecs_vec_t roots;        /**< Arrays with changed content, sorted */
    layout_node_t *current; /**< Node whose results are being computed */
    uint64_t epoch;         /**< Number of the current pass */
    int32_t solved;         /**< Nodes solved in the last pass */
    int32_t synced;         /**< Arrays synced in the last pass */
    bool arrays_dirty;      /**< An Array or ArrayInstance changed since the last pass */
    bool active;            /**< The world has layout rules, so changed inputs are recorded */
    bool writing;           /**< The layout is publishing its results */
    int refs;               /**< Observers and systems that share the context */
    bool ok;                /**< The last pass solved every requested node without error */
} layout_ctx_t;

extern ECS_COMPONENT_DECLARE(LayoutOutput);
extern ECS_COMPONENT_DECLARE(PrototypeSource);
extern ECS_TAG_DECLARE(ArrayPrototype);

/** Register the components of the spatial layout.
 *
 * @param world The world
 */
void box_spatial_layout_types(
    ecs_world_t *world);

/** Register the spatial layout: its components, observers and system.
 *
 * Observers on the layout inputs record the entities whose layout is solved and published
 * again, including the WorldTransform of a ManualTransform entity the layout does not place.
 *
 * @param world The world
 */
void box_spatial_layout_init(
    ecs_world_t *world);

/** Get the node of an entity, creating and resetting it when it does not exist.
 *
 * @param ctx The layout context
 * @param e The entity
 * @return The node of the entity
 */
layout_node_t *box_spatial_layout_node_get(
    layout_ctx_t *ctx,
    ecs_entity_t e);

/** Collect the children of an entity that take part in the layout, sorted by entity.
 *
 * Children with an AttachmentFrame and disabled children are skipped. With prototypes, array
 * instances are skipped, otherwise prefabs are.
 *
 * @param world The world
 * @param e The parent entity
 * @param prototypes Whether to collect prototypes instead of instances
 * @return Vector of child entities, to be freed by the caller
 */
ecs_vec_t box_spatial_layout_children(
    ecs_world_t *world,
    ecs_entity_t e,
    bool prototypes);

/** Compare two entities, for qsort and bsearch.
 *
 * @param a Pointer to the first entity
 * @param b Pointer to the second entity
 * @return Negative, zero or positive when a is less than, equal to or greater than b
 */
int box_spatial_layout_entity_compare(
    const void *a,
    const void *b);

/** Get the index of a single axis.
 *
 * @param axis The axis
 * @return 0, 1 or 2 for X, Y or Z, -1 when axis is not a single axis
 */
int box_spatial_layout_axis_index(
    SpatialAxis axis);

/** Test whether every element of a vector is finite.
 *
 * @param v The vector
 * @return Whether the three elements are finite
 */
bool box_spatial_layout_finite3(
    const float *v);

/** Set a component, only when it is missing or its value differs.
 *
 * @param world The world
 * @param e The entity
 * @param id The component
 * @param size Size of the component
 * @param value The new value
 */
void box_spatial_layout_set_changed(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id,
    size_t size,
    const void *value);

/** Sync the instances of the arrays that contain an entity with changed inputs.
 *
 * Instances of removed arrays or past the count of their array are deleted, and prototypes that
 * are no longer in an array become regular entities again.
 *
 * @param ctx The layout context
 * @param seed_count Number of entities in layout_ctx_t::seeds with changed inputs
 */
void box_spatial_layout_arrays(
    layout_ctx_t *ctx,
    int32_t seed_count);

#endif
