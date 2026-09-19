/**
 * @file box/collision.h
 * @brief Collision components: aggregate bounds of prefab and template roots.
 *
 * The box.collision module recognises a prefab or template instance as a root
 * at instantiation and keeps a RootBounds on it that covers the whole subtree.
 * A descendant that moves, appears or is destroyed marks its root, and the
 * marked roots are recomputed once per frame; a root whose subtree moves as one
 * piece transforms its cached local bounds instead of walking the subtree.
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
