/**
 * @file spatial/index.h
 * @brief State of the spatial index of a world, private to box.spatial.
 *
 * A static tree with exact leaves and a dynamic tree with fat leaves per scene. A fat leaf
 * reaches ahead along the last displacement, so a mover moves in its tree once it leaves it.
 * A query computes bounds only for dynamic shapes whose transform differs from SpatialPose.
 *
 * Items hold the bounds of the last update; moved static leaves catch up on the next query.
 */

#ifndef BOX_SPATIAL_INDEX_H
#define BOX_SPATIAL_INDEX_H

#include "spatial.h"
#include "bvh.h"

/** Tree of the entities that only move on change */
#define BOX_SPATIAL_STATIC (0)

/** Tree of the entities with DynamicTransform */
#define BOX_SPATIAL_DYNAMIC (1)

/** Tree of the UI elements without a shape: LayoutBounds or TextBounds, exact like static */
#define BOX_SPATIAL_UI (2)

/** Number of trees of a scene */
#define BOX_SPATIAL_TREES (3)

/** Place of an entity in the index, added with every ShapeMesh */
typedef struct SpatialProxy {
    ecs_entity_t scene; /**< Scene the entity is indexed in */
    int32_t item;       /**< Item of the entity in its scene, -1 when it is not indexed */
    bool dirty;         /**< The entity is in the list of changed entities */
    bool rescope;       /**< The scene of the entity may have changed */
} SpatialProxy;

/** Component id of SpatialProxy */
extern ECS_COMPONENT_DECLARE(SpatialProxy);

/** World transform the bounds of an indexed dynamic shape were computed from, all zero (which
 * no world transform is) until the first update places the shape */
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
} box_spatial_item_t;

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
    ecs_query_t *dynamic;      /**< Shapes with SpatialPose */
    box_spatial_stats_t stats; /**< Counters of the last update */
    uint32_t frame;            /**< Updates so far, which tell the frames between bounds */
    bool stale;                /**< Leaves may lag behind the entities since the last update */
    int32_t refs;              /**< Observers and systems that use the state */
} box_spatial_t;

/** Singleton through which the queries find the index of a world */
typedef struct SpatialIndex {
    box_spatial_t *index; /**< The index, owned by the observers and systems */
} SpatialIndex;

/** Component id of SpatialIndex */
extern ECS_COMPONENT_DECLARE(SpatialIndex);

/** Get the index of a world to query it.
 *
 * Moves the leaves of the static items of the last update and places the dynamic entities
 * whose transform changed first when that did not happen yet, so a frame without queries does
 * not visit them.
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

#endif
