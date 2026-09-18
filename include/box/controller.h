/**
 * @file box/controller.h
 * @brief Controllers that turn named actions into camera and character movement.
 *
 * A controller reads the InputAction state of the actions the module binds by
 * default, so a scene rebinds it by editing the bindings under box.input.map.
 */

#ifndef BOX_CONTROLLER_H
#define BOX_CONTROLLER_H

#include "types.h"

#ifdef BOX_CONTROLLER_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Fly controller of a camera, driven by the Forward to Down and Fast actions.
 *
 * The system writes Position and Orientation every frame, and LookAt when the
 * entity has one. Pitch and yaw follow the Orientation convention, so a positive
 * pitch looks down, and a member that is zero or less takes its default.
 */
ECS_STRUCT(CameraController, {
    float speed;          /**< Units per second at full throttle, 10 by default */
    float fast;           /**< Speed multiplier while Fast is held, 4 by default */
    float acceleration;   /**< How fast the velocity follows the input, 8 by default */
    float sensitivity;    /**< Radians turned per point of cursor movement */
    ecs_entity_t terrain; /**< Entity the camera stays above, 0 for none */
    float clearance;      /**< Units kept above the bounds of the terrain */
    float yaw;            /**< Yaw in radians, written every frame */
    float pitch;          /**< Pitch in radians, written every frame */
    vec3 velocity;        /**< Velocity in units per second, written every frame */
});

/** First person controller of a character, driven by the same movement actions.
 *
 * The controller walks in the XZ plane at the walk or the run speed, falls with
 * gravity, jumps from the ground and lowers its eye while Crouch is held. The Fly
 * action toggles free flight, which moves like the camera controller. The ground
 * is the top of the bounds of the terrain entity, or the ground height when there
 * is none, and a member that is zero or less takes its default.
 */
ECS_STRUCT(FirstPersonController, {
    float walk;           /**< Units per second while walking, 4 by default */
    float run;            /**< Units per second while Run is held, 8 by default */
    float jump;           /**< Upward speed of a jump, 5 by default */
    float gravity;        /**< Downward acceleration, 18 by default */
    float eye;            /**< Height of the eye above the ground, 1.7 by default */
    float crouch;         /**< Height of the eye while crouching, 0.9 by default */
    float acceleration;   /**< How fast the velocity follows the input, 12 by default */
    float sensitivity;    /**< Radians turned per point of cursor movement */
    ecs_entity_t terrain; /**< Entity whose bounds are the ground, 0 for the height */
    float ground;         /**< Height of the ground without a terrain entity */
    float yaw;            /**< Yaw in radians, written every frame */
    float pitch;          /**< Pitch in radians, written every frame */
    vec3 velocity;        /**< Velocity in units per second, written every frame */
    bool grounded;        /**< The character stands on the ground */
    bool flying;          /**< Free flight, toggled by the Fly action */
});

/** Register the box.controller module.
 *
 * @param world The world
 */
void BoxControllerImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
