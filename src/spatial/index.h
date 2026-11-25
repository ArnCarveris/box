/**
 * @file spatial/index.h
 * @brief State of the spatial index of a world, private to box.spatial.
 *
 * A static tree with exact leaves and a dynamic tree with fat leaves per scene. A fat leaf
 * reaches ahead along the last displacement, so a mover moves in its tree once it leaves it.
 * The dynamic tree holds one leaf per mover, an entity that owns DynamicTransform, over the
 * bounds of the shapes under it in its frame, so a mover places one leaf however many it holds.
 *
 * Items hold the bounds of the last update; moved static leaves catch up on the next query.
 * Entities under SpatialIgnore have no item, so they cost no update.
 */

#ifndef BOX_SPATIAL_INDEX_H
#define BOX_SPATIAL_INDEX_H

#include "spatial.h"
#include "bvh.h"

/** Place of an entity in the index, added with every ShapeMesh */
typedef struct SpatialProxy {
    ecs_entity_t scene; /**< Scene the entity is indexed in */
    int32_t item;       /**< Item of the entity in its scene, -1 when it is not indexed */
    int32_t mover;      /**< Mover the shape is indexed under, -1 when it is not */
    int32_t member;     /**< Slot of the shape in its mover */
    bool dirty;         /**< The entity is in the list of changed entities */
    bool rescope;       /**< The scene of the entity may have changed */
} SpatialProxy;

/** Component id of SpatialProxy */
extern ECS_COMPONENT_DECLARE(SpatialProxy);

/** World transform the bounds of an indexed dynamic UI element were computed from, all zero
 * (which no world transform is) until the first update places the element */
typedef struct SpatialPose {
    mat4 m; /**< The matrix */
} SpatialPose;

/** Component id of SpatialPose */
extern ECS_COMPONENT_DECLARE(SpatialPose);

/** An indexed entity with its exact world bounds */
typedef struct box_spatial_item_t {
    ecs_entity_t entity; /**< The entity, 0 when the item is free */
    vec3 min;            /**< Minimum corner of the world bounds */
    vec3 max;            /**< Maximum corner of the world bounds */
    int32_t next;        /**< Next free item when the item is free */
    int32_t tree;        /**< BOX_SPATIAL_STATIC, BOX_SPATIAL_DYNAMIC or BOX_SPATIAL_UI */
    uint32_t frame;      /**< Update in which the bounds were last set */
    bool moved;          /**< The static leaf of the item waits for the bounds of the item */
    bool stretched;      /**< The dynamic leaf of the item reaches ahead of its margin */
    bool disabled;       /**< The entity is disabled, queries skip the item */
    int32_t mover;       /**< Mover the item is the leaf of, -1 for the item of an entity */
    uint32_t sig;        /**< Hash of the world transform and local bounds of a static or UI
                          *   item as of its last update */
} box_spatial_item_t;

/** A shape under a mover */
typedef struct box_spatial_member_t {
    ecs_entity_t entity; /**< The shape, 0 when it left the mover */
    vec3 shape_min;      /**< Minimum corner of the local bounds of the shape */
    vec3 shape_max;      /**< Maximum corner of the local bounds of the shape */
    vec3 min;            /**< Minimum corner of the bounds in the frame of the mover */
    vec3 max;            /**< Maximum corner of the bounds in the frame of the mover */
    bool disabled;       /**< The shape is disabled, queries skip it */
} box_spatial_member_t;

/** An entity that owns DynamicTransform, one leaf of the dynamic tree over the shapes under it
 * that are not under another mover */
typedef struct box_spatial_mover_t {
    ecs_entity_t entity; /**< The mover, 0 when the slot is free */
    ecs_entity_t frame;  /**< The mover or its nearest ancestor with a Position, 0 for none */
    ecs_ref_t pose_ref;  /**< WorldTransform of frame */
    ecs_entity_t scene;  /**< Scene of the leaf */
    int32_t item;        /**< Item of the leaf in its scene, -1 when it has none */
    ecs_vec_t members;   /**< box_spatial_member_t, the shapes under the mover */
    int32_t live;        /**< Members that did not leave */
    vec3 min;            /**< Minimum corner of the bounds of the members in the frame */
    vec3 max;            /**< Maximum corner of the bounds of the members in the frame */
    mat4 pose;           /**< World transform of frame the leaf was placed with */
    box_bvh_t tree;      /**< Members by index over their bounds in the frame, when built */
    bool built;          /**< The tree holds the members as of the last refit */
    bool dirty;          /**< The members or their place in the frame changed */
    int32_t next;        /**< Next free slot when the slot is free */
} box_spatial_mover_t;

/** The index of one scene */
typedef struct box_spatial_scene_t {
    ecs_entity_t entity;  /**< The scene, 0 for the default scene */
    box_bvh_t trees[BOX_SPATIAL_TREES]; /**< Static, dynamic and UI tree */
    ecs_vec_t items;      /**< box_spatial_item_t by index */
    ecs_vec_t pending;    /**< int32_t, static items that wait for the next commit */
    ecs_vec_t moved;      /**< int32_t, static items whose leaf waits for their bounds */
    ecs_vec_t stretched;  /**< int32_t, dynamic items whose leaf may reach ahead */
    int32_t free_item;    /**< First free item, -1 when there is none */
    int32_t count;        /**< Items in use */
    uint64_t revision;    /**< Changes of the static and UI items so far
                           *   (box_spatial_static_revision) */
} box_spatial_scene_t;

/** The spatial index of a world */
typedef struct box_spatial_t {
    box_spatial_scene_t *root; /**< Index of the default scene */
    ecs_map_t scenes;          /**< Scene entity to its box_spatial_scene_t */
    ecs_vec_t dirty;           /**< ecs_entity_t, entities whose place may have changed */
    ecs_vec_t rescoped;        /**< ecs_entity_t, roots of subtrees that may change scene */
    ecs_vec_t walk;            /**< ecs_entity_t, stack of a subtree walk */
    ecs_vec_t leaves;          /**< box_bvh_leaf_t, scratch of a build */
    ecs_vec_t empty;           /**< ecs_entity_t, scenes that lost their last entity */
    ecs_query_t *dynamic;      /**< UI elements with SpatialPose */
    ecs_vec_t movers;          /**< box_spatial_mover_t by slot */
    ecs_map_t mover_of;        /**< Mover entity to its slot */
    ecs_vec_t changed;         /**< int32_t, movers whose members changed */
    int32_t free_mover;        /**< First free mover slot, -1 when there is none */
    box_spatial_stats_t stats; /**< Counters of the last update */
    uint32_t frame;            /**< Updates so far, which tell the frames between bounds */
    bool stale;                /**< Leaves may lag behind the entities since the last update */
    bool queried;              /**< The index was queried since the last update */
    int32_t refs;              /**< Observers and systems that use the state */
    uint64_t revision;         /**< Above the revision every dropped scene had, the revision
                                *   of a scene without an index */
} box_spatial_t;

/** Singleton through which the queries find the index of a world */
typedef struct SpatialIndex {
    box_spatial_t *index; /**< The index, owned by the observers and systems */
} SpatialIndex;

/** Component id of SpatialIndex */
extern ECS_COMPONENT_DECLARE(SpatialIndex);

/** Get the index of a world to query it.
 *
 * Moves the static leaves of the last update and places the changed dynamic entities when that
 * did not happen yet, so a frame without queries visits none; once queried, the next update
 * places them itself, so the first query of a frame (often a pick) pays for no moving entity.
 *
 * @param world The world
 * @return The index, NULL when box.spatial was not imported
 */
box_spatial_t* box_spatial_of(
    const ecs_world_t *world);

/** Get the index of a scene.
 *
 * @param index The index
 * @param scene The scene, 0 for the default scene
 * @return The index of the scene, NULL when nothing in it is indexed
 */
box_spatial_scene_t* box_spatial_scene(
    const box_spatial_t *index,
    ecs_entity_t scene);

/** Get an item of a scene.
 *
 * @param s The scene
 * @param item The item
 * @return The item
 */
box_spatial_item_t* box_spatial_item(
    const box_spatial_scene_t *s,
    int32_t item);

/** Create an empty index.
 *
 * @return The index
 */
box_spatial_t* box_spatial_new(void);

/** Free an index with the index of every scene.
 *
 * @param index The index
 */
void box_spatial_free(
    box_spatial_t *index);

/** Get the index of a scene, creating it when it has none.
 *
 * @param index The index
 * @param scene The scene, 0 for the default scene
 * @return The index of the scene
 */
box_spatial_scene_t* box_spatial_scene_ensure(
    box_spatial_t *index,
    ecs_entity_t scene);

/** Add an entity to a scene.
 *
 * A static entity waits for the next commit, a dynamic one enters its tree at once.
 *
 * @param s The scene
 * @param e The entity
 * @param tree BOX_SPATIAL_STATIC, BOX_SPATIAL_DYNAMIC or BOX_SPATIAL_UI
 * @param min Minimum corner of its world bounds
 * @param max Maximum corner of its world bounds
 * @param frame The current update
 * @return The item of the entity
 */
int32_t box_spatial_item_add(
    box_spatial_scene_t *s,
    ecs_entity_t e,
    int32_t tree,
    const vec3 min,
    const vec3 max,
    uint32_t frame);

/** Give an item new bounds, moving it to another tree when it changed kind.
 *
 * A static item that has a leaf keeps it until box_spatial_move gives it the new bounds. A
 * dynamic item that leaves its fat leaf gets one that reaches ahead along its displacement per
 * update, unless that displacement is too large to predict, as a teleport is.
 *
 * @param s The scene
 * @param item The item
 * @param tree BOX_SPATIAL_STATIC, BOX_SPATIAL_DYNAMIC or BOX_SPATIAL_UI
 * @param min Minimum corner of its world bounds
 * @param max Maximum corner of its world bounds
 * @param frame The current update
 * @return true when the leaf of the item changed
 */
bool box_spatial_item_set(
    box_spatial_scene_t *s,
    int32_t item,
    int32_t tree,
    const vec3 min,
    const vec3 max,
    uint32_t frame);

/** Remove an item from a scene.
 *
 * @param index The index, which forgets the scene once it is empty
 * @param s The scene
 * @param item The item
 */
void box_spatial_item_remove(
    box_spatial_t *index,
    box_spatial_scene_t *s,
    int32_t item);

/** Put the waiting static and UI items of every scene in their tree and drop empty scenes.
 *
 * A tree with at least as many waiting items as it has leaves is built again.
 *
 * @param index The index
 */
void box_spatial_commit(
    box_spatial_t *index);

/** Give the leaves of the moved static and UI items of every scene the bounds of their items.
 *
 * @param index The index
 */
void box_spatial_move(
    box_spatial_t *index);

/** Give the dynamic items whose leaf reaches ahead but whose bounds did not change in this
 * update a leaf with only the margin, so a shape that stopped does not keep a stretched leaf.
 *
 * @param index The index
 * @return Number of leaves inserted again
 */
int32_t box_spatial_settle(
    box_spatial_t *index);

/** Build the static and UI trees of a scene again from all their items.
 *
 * @param index The index, which owns the scratch buffers of the build
 * @param s The scene
 */
void box_spatial_scene_rebuild(
    box_spatial_t *index,
    box_spatial_scene_t *s);

/** Move the items of a scene that a shift of the world origin moved by its offset, with the
 * poses of their movers; the trees translate when every item moved, else are built again.
 *
 * @param index The index
 * @param s The scene
 * @param offset Added to the bounds of the moved items
 * @param shifts Tells which entities moved, NULL for all of them
 * @param ctx Context passed to shifts
 */
void box_spatial_scene_shift(
    box_spatial_t *index,
    box_spatial_scene_t *s,
    const vec3 offset,
    box_spatial_shifts_t shifts,
    void *ctx);

/** Find the mover an entity moves with: the nearest of the entity and its ancestors that owns
 * DynamicTransform, or that has it without inheriting it from DynamicTransformSpread.
 *
 * @param world The world
 * @param e The entity
 * @return The mover, 0 when the entity does not move with one
 */
ecs_entity_t box_spatial_mover_of(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Get a mover of the index.
 *
 * @param index The index
 * @param mover The slot of the mover
 * @return The mover
 */
box_spatial_mover_t* box_spatial_mover(
    const box_spatial_t *index,
    int32_t mover);

/** Get the slot of a mover.
 *
 * @param index The index
 * @param e The mover entity
 * @return The slot, -1 when the entity is not an indexed mover
 */
int32_t box_spatial_mover_slot(
    const box_spatial_t *index,
    ecs_entity_t e);

/** Set up the movers of an index.
 *
 * @param index The index
 */
void box_spatial_movers_init(
    box_spatial_t *index);

/** Free the movers of an index.
 *
 * @param index The index
 */
void box_spatial_movers_fini(
    box_spatial_t *index);

/** Put a shape under its mover, or give it new local bounds when it is there.
 *
 * @param index The index
 * @param p The proxy of the shape
 * @param e The shape
 * @param mover The mover entity
 * @param shape_min Minimum corner of the local bounds of the shape
 * @param shape_max Maximum corner of the local bounds of the shape
 * @param disabled The shape is disabled
 */
void box_spatial_mover_join(
    box_spatial_t *index,
    SpatialProxy *p,
    ecs_entity_t e,
    ecs_entity_t mover,
    const vec3 shape_min,
    const vec3 shape_max,
    bool disabled);

/** Take a shape out of its mover.
 *
 * @param index The index
 * @param p The proxy of the shape
 */
void box_spatial_mover_leave(
    box_spatial_t *index,
    SpatialProxy *p);

/** Flag a shape under a mover as disabled or enabled.
 *
 * @param index The index
 * @param p The proxy of the shape
 * @param disabled The shape is disabled
 */
void box_spatial_mover_disable(
    box_spatial_t *index,
    const SpatialProxy *p,
    bool disabled);

/** Refit the mover an entity moves with, after its place in the frame of the mover changed.
 *
 * @param world The world
 * @param index The index
 * @param e The entity
 */
void box_spatial_mover_touch(
    const ecs_world_t *world,
    box_spatial_t *index,
    ecs_entity_t e);

/** Bound the members of the changed movers again and give them their leaves.
 *
 * @param world The world
 * @param index The index
 */
void box_spatial_movers_refit(
    ecs_world_t *world,
    box_spatial_t *index);

/** Move the leaves of the movers whose world transform changed.
 *
 * @param world The world
 * @param index The index
 * @return Number of movers whose transform changed
 */
int32_t box_spatial_movers_place(
    const ecs_world_t *world,
    box_spatial_t *index);

/** Get the tree over the members of a mover in its frame, building it when a refit left it
 * stale, so that a mover nobody queries never builds one.
 *
 * @param index The index, which owns the scratch buffers of the build
 * @param mover The slot of the mover
 * @return The tree, NULL when the mover has too few members for one: test them one by one
 */
const box_bvh_t* box_spatial_mover_tree(
    box_spatial_t *index,
    int32_t mover);

/** Get the world bounds of a shape under a mover, from its world transform.
 *
 * @param world The world
 * @param m The shape
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
void box_spatial_member_bounds(
    const ecs_world_t *world,
    const box_spatial_member_t *m,
    vec3 min,
    vec3 max);

/** Compute the world bounds of local bounds placed by a matrix.
 *
 * @param l_min Minimum corner of the local bounds
 * @param l_max Maximum corner of the local bounds
 * @param m The 16 floats of the matrix, column by column
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
void box_spatial_bounds_of(
    const vec3 l_min,
    const vec3 l_max,
    const float *m,
    vec3 min,
    vec3 max);

#endif
