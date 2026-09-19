/**
 * @file collision/collision_internal.h
 * @brief State and helpers of the box.collision module.
 *
 * The state holds the roots that were marked since the last flush, with the
 * kind of work each one needs, the entities whose collider has to be resolved
 * again and the instances that still have to be turned into roots.
 */

#ifndef BOX_COLLISION_INTERNAL_H
#define BOX_COLLISION_INTERNAL_H

#include <box.h>
#include "../transform/transform_internal.h"

/** The bounds of the root must be aggregated from its subtree again */
#define BOX_ROOT_FULL (1)

/** The root moved, so its cached local bounds only have to be transformed */
#define BOX_ROOT_MOVED (2)

/** Collision state of a world, owned by the CollisionState singleton */
typedef struct box_collision_state_t {
    ecs_map_t dirty;     /**< Root entity to the flags it was marked with */
    ecs_map_t colliders; /**< Entities whose collider must be resolved again */
    ecs_vec_t pass;      /**< Roots a flush took off the dirty map */
    ecs_vec_t resolve;   /**< Entities a flush took off the collider map */
    ecs_vec_t instances; /**< Instances that still need a RootBounds */
    bool shapes;         /**< The shape observers of box.geometry exist */
} box_collision_state_t;

/** Private singleton that owns the state of a world */
typedef struct CollisionState {
    box_collision_state_t *state; /**< Freed when the component is destroyed */
} CollisionState;

extern ECS_COMPONENT_DECLARE(CollisionState);

/** Create the state of a world.
 *
 * @return The state
 */
box_collision_state_t* box_collision_state_new(void);

/** Free the state of a world.
 *
 * @param state The state
 */
void box_collision_state_free(
    box_collision_state_t *state);

/** Mark a root as needing work before the next query.
 *
 * @param state The state
 * @param root The root entity
 * @param flags BOX_ROOT_FULL, BOX_ROOT_MOVED or both
 */
void box_collision_mark(
    box_collision_state_t *state,
    ecs_entity_t root,
    int32_t flags);

/** Mark an entity as needing a new collider before the next query.
 *
 * @param state The state
 * @param e The entity
 */
void box_collision_touch(
    box_collision_state_t *state,
    ecs_entity_t e);

/** Find the nearest root at or above an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The root, 0 when the entity is not below one
 */
ecs_entity_t box_collision_root_of(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Work out the collider of an entity in world space.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the collider
 * @return true when the entity has a collider at all
 */
bool box_collider_resolve(
    const ecs_world_t *world,
    ecs_entity_t e,
    Collider *out);

/** Register the observers that recognise roots and mark them.
 *
 * @param world The world
 * @param state The state
 */
void box_collision_roots_init(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Register the observers that mark entities for a new collider.
 *
 * @param world The world
 * @param state The state
 */
void box_collision_colliders_init(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Register the observers on the primitive shapes once box.geometry exists.
 *
 * @param world The world
 * @param state The state
 */
void box_collision_shapes_init(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Give every marked root the bounds of its subtree.
 *
 * @param world The world, never a stage
 * @param state The state
 */
void box_collision_roots_flush(
    ecs_world_t *world,
    box_collision_state_t *state);

/** Give every marked entity the collider its shape asks for.
 *
 * @param world The world, never a stage
 * @param state The state
 */
void box_collision_colliders_flush(
    ecs_world_t *world,
    box_collision_state_t *state);

#endif
