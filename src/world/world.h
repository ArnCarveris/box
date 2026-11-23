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

/** Get the local world position of an entity as of now: the Position of a root, which the
 * transforms of this frame did not compute yet, else the translation of its world transform.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the position
 * @return Whether the entity has a place in the world
 */
bool box_world_position(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec3 out);

/** Register WorldCell: its DynamicTransform and the observer that places a root at its cell.
 *
 * @param world The world
 */
void box_world_cell_import(
    ecs_world_t *world);

/** Get the Position of a WorldCell root for an absolute origin: the corner of its cell
 * relative to the origin, computed in f64.
 *
 * @param cell The cell
 * @param origin The absolute position of the local origin
 * @param out Receives the position
 */
void box_world_cell_position(
    const WorldCell *cell,
    const double origin[3],
    Position *out);

/** Register the rebase system and the queries of the shift.
 *
 * @param world The world
 */
void box_world_origin_import(
    ecs_world_t *world);

/** Register the hooks of ChunkGrid, which the module registers, its gate and the system
 * that streams its cells.
 *
 * @param world The world
 */
void box_world_chunk_import(
    ecs_world_t *world);

/** Register the hooks of Scatter, which the module registers, and the system that places
 * its instances. Registered after ChunkGrid, so cells created in a frame scatter in it.
 *
 * @param world The world
 */
void box_world_scatter_import(
    ecs_world_t *world);

#endif
