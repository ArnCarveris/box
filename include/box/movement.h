/**
 * @file box/movement.h
 * @brief Velocity components integrated into Position and Rotation every frame.
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
