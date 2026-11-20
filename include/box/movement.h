/**
 * @file box/movement.h
 * @brief Velocity components and path following that move Position and Rotation every frame.
 *
 * Systems in EcsOnUpdate add Velocity times the delta time to the local Position and
 * AngularVelocity times the delta time to the local Rotation, before the transform systems of
 * EcsPreStore run. Children follow through their parent's transform.
 *
 * Velocity and AngularVelocity imply DynamicTransform, which stays when the velocity is removed.
 * Velocity implies Position and AngularVelocity implies Rotation, so an entity with only a
 * velocity moves from the next frame; a missing Position or Rotation starts at zero.
 */

#ifndef BOX_MOVEMENT_H
#define BOX_MOVEMENT_H

#include "types.h"

#ifdef BOX_MOVEMENT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Units per second added to Position */
ECS_STRUCT(Velocity, {
    float x; /**< X velocity */
    float y; /**< Y velocity */
    float z; /**< Z velocity */
});

/** Radians per second added to Rotation */
ECS_STRUCT(AngularVelocity, {
    float x; /**< Angular velocity about X */
    float y; /**< Angular velocity about Y */
    float z; /**< Angular velocity about Z */
});

/** How a PathFollow continues at the ends of its path */
ECS_ENUM(PathFollowMode, {
    PathFollowOnce,    /**< Stop at the end, or at the start when moving backwards */
    PathFollowLoop,    /**< Wrap the distance around to the other end */
    PathFollowPingPong /**< Reflect at the ends and reverse the sign of speed */
});

/** Moves the entity along the Path3D of another entity at a constant speed.
 *
 * The path points are in the parent space of the follower. The system writes the local
 * Position, and the Rotation when orient is set: -Z along the tangent, +Y up (+X along Y).
 *
 * Implies Position and DynamicTransform. A missing or invalid path leaves the entity as is.
 */
ECS_STRUCT(PathFollow, {
    ecs_entity_t path;   /**< Entity with the Path3D to follow, may inherit it from a prefab */
    float speed;         /**< Units per second along the path, may be negative */
    float distance;      /**< Distance along the path, written by the system, set it to seek */
    PathFollowMode mode; /**< Once stops at the ends, Loop wraps, PingPong reverses */
    bool orient;         /**< Set Rotation so the entity faces along the path tangent */
});

/** Pans the local Rotation about Y back and forth on a sine, as a security camera sweeps.
 *
 * Every frame Rotation.y = center + amplitude * sin(time * speed + phase), where time is the
 * total world time in seconds; Rotation.x and Rotation.z stay as they are, so a pitched
 * camera keeps its pitch while it pans (Rotation turns about Y, then about X).
 *
 * Implies Rotation and DynamicTransform.
 */
ECS_STRUCT(Pan, {
    float center;        /**< Yaw in radians at the middle of the sweep */
    float amplitude;     /**< Radians the yaw swings to either side, 0 holds it at center */
    float speed;         /**< Angular frequency of the sine in radians per second */
    float phase;         /**< Radians added to the angle of the sine, so sweeps run out of step */
});

/** Register the box.movement module.
 *
 * @param world The world
 */
void BoxMovementImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
