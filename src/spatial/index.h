/**
 * @file spatial/index.h
 * @brief State of the spatial index of a world, private to box.spatial.
 *
 * Every scene has a static tree with exact leaves and a dynamic tree whose leaves have a
 * margin, so a mover only changes its place in the tree once it leaves its margin.
 */

#ifndef BOX_SPATIAL_INDEX_H
#define BOX_SPATIAL_INDEX_H

#include "spatial.h"
#include "bvh.h"

/** Tree of the entities that only move on change */
#define BOX_SPATIAL_STATIC (0)

/** Tree of the entities with DynamicTransform */
#define BOX_SPATIAL_DYNAMIC (1)

/** Place of an entity in the index, added with every ShapeMesh */
typedef struct SpatialProxy {
    ecs_entity_t scene; /**< Scene the entity is indexed in */
    int32_t item;       /**< Item of the entity in its scene, -1 when it is not indexed */
    bool dirty;         /**< The entity is in the list of changed entities */
    bool rescope;       /**< The scene of the entity may have changed */
} SpatialProxy;

/** Component id of SpatialProxy */
extern ECS_COMPONENT_DECLARE(SpatialProxy);

/** An indexed entity with its exact world bounds */
typedef struct box_spatial_item_t {
    ecs_entity_t entity; /**< The entity, 0 when the item is free */
    vec3 min;            /**< Minimum corner of the world bounds */
    vec3 max;            /**< Maximum corner of the world bounds */
    int32_t node;        /**< Leaf in its tree, -1 while it waits, next free item when free */
    int32_t tree;        /**< BOX_SPATIAL_STATIC or BOX_SPATIAL_DYNAMIC */
} box_spatial_item_t;

/** The index of one scene */
typedef struct box_spatial_scene_t {
    ecs_entity_t entity;  /**< The scene, 0 for the default scene */
    box_bvh_t trees[2];   /**< Static and dynamic tree */
    ecs_vec_t items;      /**< box_spatial_item_t by index */
    ecs_vec_t pending;    /**< int32_t, static items that wait for the next commit */
    int32_t free_item;    /**< First free item, -1 when there is none */
    int32_t count;        /**< Items in use */
} box_spatial_scene_t;

/** The spatial index of a world */
typedef struct box_spatial_t {
    box_spatial_scene_t *root; /**< Index of the default scene */
    ecs_map_t scenes;          /**< Scene entity to its box_spatial_scene_t */
    ecs_vec_t dirty;           /**< ecs_entity_t, entities whose place may have changed */
    ecs_vec_t moved;           /**< ecs_entity_t, roots of subtrees that moved */
    ecs_vec_t rescoped;        /**< ecs_entity_t, roots of subtrees that may change scene */
    ecs_vec_t walk;            /**< ecs_entity_t, stack of a subtree walk */
    ecs_vec_t leaves;          /**< box_bvh_leaf_t, scratch of a build */
    ecs_vec_t nodes;           /**< int32_t, scratch of a build */
    ecs_vec_t empty;           /**< ecs_entity_t, scenes that lost their last entity */
    ecs_query_t *dynamic;      /**< Indexed entities with DynamicTransform */
    box_spatial_stats_t stats; /**< Counters of the last update */
    bool stale;                /**< Dynamic entities may have moved since they were placed */
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
 * Places the dynamic entities for their current transforms first when they may have moved
 * since they were last placed, so a frame without queries does not visit them.
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
 * @param tree BOX_SPATIAL_STATIC or BOX_SPATIAL_DYNAMIC
 * @param min Minimum corner of its world bounds
 * @param max Maximum corner of its world bounds
 * @return The item of the entity
 */
int32_t box_spatial_item_add(
    box_spatial_scene_t *s,
    ecs_entity_t e,
    int32_t tree,
    const vec3 min,
    const vec3 max);

/** Give an item new bounds, moving it to another tree when it changed kind.
 *
 * @param s The scene
 * @param item The item
 * @param tree BOX_SPATIAL_STATIC or BOX_SPATIAL_DYNAMIC
 * @param min Minimum corner of its world bounds
 * @param max Maximum corner of its world bounds
 * @return true when the leaf of the item changed
 */
bool box_spatial_item_set(
    box_spatial_scene_t *s,
    int32_t item,
    int32_t tree,
    const vec3 min,
    const vec3 max);

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

/** Put the waiting static items of every scene in their tree and drop empty scenes.
 *
 * A scene with at least as many waiting items as its tree has leaves is built again.
 *
 * @param index The index
 */
void box_spatial_commit(
    box_spatial_t *index);

/** Build the static tree of a scene again from all its static items.
 *
 * @param index The index, which owns the scratch buffers of the build
 * @param s The scene
 */
void box_spatial_scene_rebuild(
    box_spatial_t *index,
    box_spatial_scene_t *s);

#endif
