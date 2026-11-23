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


/** The frame of the local origin in f64: absolute = t + s * R * local */
typedef struct box_world_frame_t {
    double t[3];         /**< Absolute position of the origin */
    double q[4];         /**< Rotation, a unit quaternion x y z w */
    double r[3][3];      /**< The rotation as a matrix: r[c] is the absolute direction of the
                          *   local axis c */
    double s;            /**< Absolute meters per local unit */
} box_world_frame_t;

/** Get the frame of the WorldOrigin singleton: the identity without one, an unset rotation
 * or a scale of 0 or less as the identity.
 *
 * @param world The world
 * @param out Receives the frame
 */
void box_world_frame_get(
    const ecs_world_t *world,
    box_world_frame_t *out);

/** Set a frame from its origin, rotation and scale, normalizing the rotation.
 *
 * @param frame The frame
 * @param t Absolute position of the origin
 * @param q Rotation, a quaternion x y z w; all 0 for the identity
 * @param s Meters per local unit; 0 or less for 1
 */
void box_world_frame_set(
    box_world_frame_t *frame,
    const double t[3],
    const double q[4],
    double s);

/** Check whether a frame has no rotation and a scale of 1.
 *
 * @param frame The frame
 * @return Whether it only translates
 */
bool box_world_frame_is_translation(
    const box_world_frame_t *frame);

/** Get the absolute position of a local position in a frame.
 *
 * @param frame The frame
 * @param local The local position
 * @param out Receives the absolute position
 */
void box_world_frame_to_absolute(
    const box_world_frame_t *frame,
    const double local[3],
    double out[3]);

/** Get the local position of an absolute position in a frame.
 *
 * @param frame The frame
 * @param absolute The absolute position
 * @param out Receives the local position
 */
void box_world_frame_to_local(
    const box_world_frame_t *frame,
    const double absolute[3],
    double out[3]);

/** Get the shortest-arc rotation that turns one unit direction onto another, without twist.
 *
 * @param from The unit direction it turns
 * @param to The unit direction it turns onto
 * @param out Receives the rotation, a quaternion x y z w
 */
void box_world_quat_from_to(
    const double from[3],
    const double to[3],
    double out[4]);

/** Multiply two rotations: out turns by b, then by a.
 *
 * @param a The rotation applied second, a quaternion x y z w
 * @param b The rotation applied first
 * @param out Receives the product, which can be a or b
 */
void box_world_quat_mul(
    const double a[4],
    const double b[4],
    double out[4]);

/** Get the rotation of a Rotation (yaw, then pitch, then roll) as a quaternion.
 *
 * @param r The Euler angles
 * @param out Receives the rotation, a quaternion x y z w
 */
void box_world_quat_of_rotation(
    const Rotation *r,
    double out[4]);

/** Get the Rotation (yaw, then pitch, then roll) of a unit quaternion. The angles give the same
 * orientation at any pitch; at a pitch of 90 degrees, where yaw and roll turn about one axis,
 * the roll is 0.
 *
 * @param q The rotation, a quaternion x y z w
 * @param out Receives the Euler angles
 */
void box_world_rotation_of_quat(
    const double q[4],
    Rotation *out);

/** Get the Position of a WorldCell root in a frame: the corner of its cell in local units,
 * computed in f64.
 *
 * @param cell The cell
 * @param frame The frame of the local origin
 * @param out Receives the position
 */
void box_world_cell_position(
    const WorldCell *cell,
    const box_world_frame_t *frame,
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
