/**
 * @file box/world.h
 * @brief Open world: the floating world origin and the focus that streaming systems follow.
 *
 * Position, Rotation and WorldTransform are f32, so content far from the origin loses
 * precision. WorldOrigin holds the absolute position of the local origin in f64; when the focus
 * moves beyond its threshold the content of its scene shifts back by whole steps.
 */

#ifndef BOX_WORLD_H
#define BOX_WORLD_H

#include "types.h"

#ifdef BOX_WORLD_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Distance of the focus from the local origin along an axis that shifts it, by default */
#define BOX_WORLD_ORIGIN_THRESHOLD (2048.0f)

/** Shifts are whole multiples of this distance, by default */
#define BOX_WORLD_ORIGIN_STEP (256.0f)

/** Marks the entities that streaming systems follow, such as a player or a camera.
 *
 * The systems take every WorldFocus entity; without any they follow the camera of the first
 * View. The world origin follows the first of them (box_world_focus).
 */
extern ECS_TAG_DECLARE(WorldFocus);

/** Keeps a root entity and its descendants where they are when the world origin shifts, for
 * content placed relative to the local origin rather than in the world */
extern ECS_TAG_DECLARE(WorldOriginFixed);

/** Singleton with the absolute world position of the local origin and when it shifts.
 *
 * Absolute = origin + local. When the first focus is beyond threshold along an axis, the origin
 * moves along it by the whole steps nearest to the focus and the content of the scene of the
 * focus moves back by as much, in EcsPostUpdate before the transforms are computed.
 *
 * Without the singleton nothing shifts and the origin is zero. The constructor sets the
 * defaults; values apply as specified, a threshold or step of 0 or less never shifts.
 */
ECS_STRUCT(WorldOrigin, {
    double x;            /**< Absolute X of the local origin */
    double y;            /**< Absolute Y of the local origin */
    double z;            /**< Absolute Z of the local origin */
    float threshold;     /**< Distance of the focus along an axis that shifts, default 2048 */
    float step;          /**< Shifts are whole multiples of it, default 256 */
    bool enabled;        /**< Shift when the focus passes the threshold, default true */
});

/** Shifts of the world origin so far, written by the engine next to WorldOrigin */
ECS_STRUCT(WorldOriginStatus, {
    int32_t shifts;      /**< Shifts since the singleton was added */
    double x;            /**< X the origin moved by in the last shift */
    double y;            /**< Y the origin moved by in the last shift */
    double z;            /**< Z the origin moved by in the last shift */
});

/** Event emitted on the WorldOrigin singleton entity with every shift, its payload the shift.
 *
 * Observe it with a term on WorldOrigin and the event ecs_id(WorldOriginShift); local positions
 * moved by minus the offset in the frame of the event.
 */
ECS_STRUCT(WorldOriginShift, {
    double x;            /**< X the origin moved by */
    double y;            /**< Y the origin moved by */
    double z;            /**< Z the origin moved by */
    ecs_entity_t scene;  /**< Scene whose content moved, 0 for the default scene */
});

/** A focus of the streaming systems: a WorldFocus entity or the camera of the first View */
typedef struct box_world_focus_t {
    ecs_entity_t entity; /**< The focus entity */
    ecs_entity_t scene;  /**< Scene of the entity, 0 for the default scene */
    vec3 position;       /**< Local world position: the Position of a root, else the
                          *   translation of its WorldTransform */
} box_world_focus_t;

/** Get the focus positions that streaming systems follow: every WorldFocus entity with a
 * place in the world, or without any the camera of the first View by order.
 *
 * @param world The world
 * @param out Receives the foci, NULL to count them
 * @param max Foci out holds
 * @return Number of foci, which can be more than max
 */
int32_t box_world_focus(
    const ecs_world_t *world,
    box_world_focus_t *out,
    int32_t max);

/** Get the absolute world position of a local position: origin + local.
 *
 * @param world The world
 * @param local The local position
 * @param out Receives the absolute position
 */
void box_world_to_absolute(
    const ecs_world_t *world,
    const vec3 local,
    double out[3]);

/** Get the local position of an absolute world position: absolute - origin.
 *
 * @param world The world
 * @param absolute The absolute position
 * @param out Receives the local position
 */
void box_world_to_local(
    const ecs_world_t *world,
    const double absolute[3],
    vec3 out);

/** Shift the world origin of a scene now: the origin moves by offset, the root positions,
 * cameras, particles, spatial index and render caches of the scene by minus offset.
 *
 * The rebase system calls it with whole steps; a call with other offsets applies as given.
 * The transforms follow in EcsPreStore, as for any change of Position.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param offset Distance the origin moves along each axis
 */
void box_world_origin_shift(
    ecs_world_t *world,
    ecs_entity_t scene,
    const double offset[3]);

/** Register the box.world module.
 *
 * Adds the script functions box.world.absolute_x, absolute_y and absolute_z, which take a
 * local coordinate, and local_x, local_y and local_z, which take an absolute one, all f64.
 *
 * @param world The world
 */
void BoxWorldImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
