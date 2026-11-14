/**
 * @file spatial/spatial.h
 * @brief Queries on the spatial index of box.spatial, shared with box modules.
 *
 * Every query is scoped to one scene (0 for the default scene) and answers with the world
 * bounds of shapes: the unit mesh bounds scaled by the shape and placed by WorldTransform.
 *
 * UI elements without a shape (LayoutBounds, else TextBounds, flat in the XY plane of their
 * WorldTransform) are indexed too, and only ray casts that ask for them see them. An entity
 * without a WorldTransform is not indexed.
 */

#ifndef BOX_SPATIAL_PRIVATE_H
#define BOX_SPATIAL_PRIVATE_H

#include <box.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Tree of the entities that only move on change */
#define BOX_SPATIAL_STATIC (0)

/** Tree of the entities with DynamicTransform */
#define BOX_SPATIAL_DYNAMIC (1)

/** Tree of the UI elements without a shape: LayoutBounds or TextBounds, exact like static */
#define BOX_SPATIAL_UI (2)

/** Number of trees of a scene */
#define BOX_SPATIAL_TREES (3)

/** An entity a ray hit */
typedef struct box_spatial_hit_t {
    ecs_entity_t entity; /**< The entity */
    float distance;      /**< Distance from the ray origin at which the ray enters its bounds */
} box_spatial_hit_t;

/** Accept or reject an entity whose bounds a ray hit, the narrow phase of a ray cast.
 *
 * @param ctx Context of the ray
 * @param e The entity
 * @param distance Distance at which the ray enters the bounds, may be moved further away
 * @return false to ignore the entity
 */
typedef bool (*box_spatial_filter_t)(
    void *ctx,
    ecs_entity_t e,
    float *distance);

/** A ray cast into one scene */
typedef struct box_spatial_ray_t {
    ecs_entity_t scene;          /**< Scene to cast into, 0 for the default scene */
    vec3 origin;                 /**< Origin of the ray */
    vec3 dir;                    /**< Direction, need not be normalized */
    float max_distance;          /**< Longest distance that counts, 0 for no limit */
    box_spatial_filter_t filter; /**< Narrow phase, NULL to accept every hit of the bounds */
    void *ctx;                   /**< Context passed to filter */
    bool ui;                     /**< Also hit UI elements without a shape, false for shapes only */
} box_spatial_ray_t;

/** Counters of the spatial index of a world */
typedef struct box_spatial_stats_t {
    int32_t scenes;   /**< Scenes with indexed entities */
    int32_t entities; /**< Indexed entities */
    int32_t updated;  /**< Entities the last update placed again */
    int32_t rebound;  /**< Dynamic entities whose transform changed since the last update */
    int32_t moved;    /**< Dynamic entities the last update moved in their tree */
} box_spatial_stats_t;

/** Counters of the spatial layout of a world */
typedef struct box_spatial_layout_stats_t {
    int32_t nodes;  /**< Layout nodes kept between updates */
    int32_t solved; /**< Nodes the last update solved again */
    int32_t synced; /**< Arrays whose instances the last update synced with their prototypes */
} box_spatial_layout_stats_t;

/** Find the nearest entity a ray hits.
 *
 * @param world The world
 * @param ray The ray
 * @param hit Receives the entity and distance
 * @return true when the ray hit an entity
 */
bool box_spatial_ray(
    const ecs_world_t *world,
    const box_spatial_ray_t *ray,
    box_spatial_hit_t *hit);

/** Find every entity a ray hits, nearest first.
 *
 * @param world The world
 * @param ray The ray
 * @param hits Vector of box_spatial_hit_t the hits are appended to
 * @return Number of hits appended
 */
int32_t box_spatial_ray_all(
    const ecs_world_t *world,
    const box_spatial_ray_t *ray,
    ecs_vec_t *hits);

/** Find the entities whose bounds overlap a box.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param result Vector of ecs_entity_t the entities are appended to
 * @return Number of entities appended
 */
int32_t box_spatial_overlap_box(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 min,
    const vec3 max,
    ecs_vec_t *result);

/** Find the entities whose bounds overlap a sphere.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param center Center of the sphere
 * @param radius Radius of the sphere
 * @param result Vector of ecs_entity_t the entities are appended to
 * @return Number of entities appended
 */
int32_t box_spatial_overlap_sphere(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 center,
    float radius,
    ecs_vec_t *result);

/** Get the bounds the index holds for an entity.
 *
 * @param world The world
 * @param e The entity
 * @param scene Receives the scene of the entity, may be NULL
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 * @return false when the entity is not indexed
 */
bool box_spatial_bounds(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t *scene,
    vec3 min,
    vec3 max);

/** Get the box around the static shapes the index holds for a scene.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 * @return false when the scene has no static shapes
 */
bool box_spatial_static_bounds(
    const ecs_world_t *world,
    ecs_entity_t scene,
    vec3 min,
    vec3 max);

/** Receive a box of a tree of the spatial index, a node before the nodes below it.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param depth Depth of the box, 0 for the root
 * @param leaf The box is the leaf of an entity, else of a node over two boxes
 * @param ctx Context of the walk
 * @return false to skip the boxes below it
 */
typedef bool (*box_spatial_box_visit_t)(
    const vec3 min,
    const vec3 max,
    int32_t depth,
    bool leaf,
    void *ctx);

/** Walk the boxes of a tree of the spatial index of a scene, from the root down.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param tree BOX_SPATIAL_STATIC, BOX_SPATIAL_DYNAMIC or BOX_SPATIAL_UI
 * @param visit Receives the boxes
 * @param ctx Context passed to visit
 * @return Number of boxes visited
 */
int32_t box_spatial_tree_walk(
    const ecs_world_t *world,
    ecs_entity_t scene,
    int32_t tree,
    box_spatial_box_visit_t visit,
    void *ctx);

/** Bring the index up to date with the changes since the last update.
 *
 * The box.spatial.Update system does this every frame in OnStore, after the transforms.
 *
 * @param world The world
 */
void box_spatial_update(
    ecs_world_t *world);

/** Rebuild the static tree of every scene from scratch.
 *
 * @param world The world
 */
void box_spatial_rebuild(
    ecs_world_t *world);

/** Read the counters of the spatial index.
 *
 * @param world The world
 * @param stats Receives the counters
 */
void box_spatial_stats(
    const ecs_world_t *world,
    box_spatial_stats_t *stats);

/** Read the counters of the spatial layout.
 *
 * @param world The world
 * @param stats Receives the counters
 */
void box_spatial_layout_stats(
    const ecs_world_t *world,
    box_spatial_layout_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
