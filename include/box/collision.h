/**
 * @file box/collision.h
 * @brief Collision components: root bounds, colliders and the shape tests.
 *
 * The box.collision module recognises a prefab or template instance as a root
 * at instantiation and keeps a RootBounds on it that covers the whole subtree.
 * A descendant that moves, appears or is destroyed marks its root, and the
 * marked roots are recomputed once per frame; a root whose subtree moves as one
 * piece transforms its cached local bounds instead of walking the subtree.
 *
 * Every renderable gets a Collider in the same pass, derived from its shape
 * where a cheap one exists and from its world bounds otherwise, with the
 * instance scale applied. The shape tests take a resolved collider, so picking,
 * controller collision and LOD all test the same geometry.
 */

#ifndef BOX_COLLISION_H
#define BOX_COLLISION_H

#include "types.h"

#ifdef BOX_COLLISION_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Bounds of a root and of every entity below it.
 *
 * The world corners are what queries test. The local corners are the same box
 * in the space of the root, which is what a rigid root transforms when it moves
 * instead of aggregating its subtree again. A root that covers no bounds at all
 * has a count of zero and corners of zero.
 */
ECS_STRUCT(RootBounds, {
    vec3 min;       /**< Minimum corner in world space */
    vec3 max;       /**< Maximum corner in world space */
    vec3 local_min; /**< Minimum corner in the space of the root */
    vec3 local_max; /**< Maximum corner in the space of the root */
    int32_t count;  /**< Entities with bounds the aggregate covers */
});

/** Shape of a resolved collider */
ECS_ENUM(ColliderKind, {
    ColliderNone = 0,    /**< The entity has no collider */
    ColliderSphere = 1,  /**< Sphere around the center */
    ColliderBox = 2,     /**< Box around the center, turned by the axes */
    ColliderCapsule = 3, /**< Capsule along the Y axis of the collider */
    ColliderAabb = 4     /**< Axis aligned box, the fallback for other shapes */
});

/** Sphere collider in the space of the entity, overrides the derived one */
ECS_STRUCT(SphereCollider, {
    vec3 center;  /**< Center in the space of the entity */
    float radius; /**< Radius before the instance scale */
});

/** Box collider in the space of the entity, overrides the derived one */
ECS_STRUCT(BoxCollider, {
    vec3 center; /**< Center in the space of the entity */
    vec3 half;   /**< Half extents before the instance scale */
});

/** Capsule collider along the Y axis of the entity, overrides the derived one */
ECS_STRUCT(CapsuleCollider, {
    vec3 center;  /**< Center in the space of the entity */
    float radius; /**< Radius before the instance scale */
    float height; /**< Length of the cylinder part between the two caps */
});

/** The collider of an entity in world space, written by the engine.
 *
 * A sphere keeps its radius in every component of half, a capsule keeps its
 * radius in x and z and half the length of its cylinder part in y, and a box
 * keeps its half extents. The axes are the normalized axes of the entity; an
 * axis aligned collider keeps the world axes.
 */
ECS_STRUCT(Collider, {
    ColliderKind kind; /**< Shape of the collider */
    vec3 center;       /**< Center in world space */
    vec3 half;         /**< Half extents in world space */
    vec3 axis_x;       /**< X axis of the collider in world space */
    vec3 axis_y;       /**< Y axis of the collider in world space */
    vec3 axis_z;       /**< Z axis of the collider in world space */
});

/** The entity gets no collider, whatever shape it has */
extern ECS_TAG_DECLARE(NoCollider);

/** Intersect a ray with a collider.
 *
 * @param c The collider
 * @param origin Origin of the ray in world space
 * @param dir Direction of the ray, distances are multiples of its length
 * @param max_dist Longest distance that counts as a hit
 * @param dist_out Receives the distance of the hit, NULL to ignore
 * @return true when the ray hits the collider
 */
bool box_collider_ray(
    const Collider *c,
    vec3 origin,
    vec3 dir,
    float max_dist,
    float *dist_out);

/** Test a sphere against a collider.
 *
 * @param c The collider
 * @param center Center of the sphere in world space
 * @param radius Radius of the sphere
 * @return true when the sphere overlaps the collider
 */
bool box_collider_sphere(
    const Collider *c,
    vec3 center,
    float radius);

/** Test a capsule against a collider.
 *
 * @param c The collider
 * @param a First end of the segment of the capsule in world space
 * @param b Second end of the segment of the capsule in world space
 * @param radius Radius of the capsule
 * @return true when the capsule overlaps the collider
 */
bool box_collider_capsule(
    const Collider *c,
    vec3 a,
    vec3 b,
    float radius);

/** Get the world bounds of a collider.
 *
 * @param c The collider
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
void box_collider_bounds(
    const Collider *c,
    vec3 *min,
    vec3 *max);

/** Register the box.collision module.
 *
 * @param world The world
 */
void BoxCollisionImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
