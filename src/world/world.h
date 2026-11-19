/**
 * @file world/world.h
 * @brief Functions shared by the sources of box.world.
 */

#ifndef BOX_WORLD_PRIVATE_H
#define BOX_WORLD_PRIVATE_H

#include <box.h>

/** Get the scene an entity belongs to: its nearest ancestor with Scene, itself included.
 *
 * @param world The world
 * @param e The entity
 * @return The scene root, 0 for the default scene
 */
ecs_entity_t box_world_scene_of(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Check whether an entity is a root in world space: no ancestor has a WorldTransform, so its
 * Position is its world position.
 *
 * @param world The world
 * @param e The entity
 * @return Whether the entity is a root
 */
bool box_world_is_root(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Register the rebase system and the queries of the shift.
 *
 * @param world The world
 */
void box_world_origin_import(
    ecs_world_t *world);

#endif
