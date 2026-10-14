/**
 * @file box/controllers.h
 * @brief Controllers that move and turn entities with the actions of the input system.
 *
 * A controller reads the ActionState of the actions in its action scope, never a device, so a
 * scene remaps it by editing the Binding of those actions or by pointing it at its own scope.
 */

#ifndef BOX_CONTROLLERS_H
#define BOX_CONTROLLERS_H

#include "types.h"

#ifdef BOX_CONTROLLERS_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Fly controller of a camera that writes the Position and Rotation of its entity.
 *
 * Forward, Back, Left and Right move in the XZ plane along the yaw, Up and Down move along Y
 * and Fast multiplies the speed. A member that is zero takes its default.
 *
 * While Look is held the cursor turns the camera: Rotation.y is the yaw, Rotation.x the pitch,
 * which stops short of straight up and down. The camera looks along -Z of its rotation, so a
 * positive yaw turns left and a positive pitch looks up.
 */
ECS_STRUCT(CameraController, {
    float speed;           /**< Units per second, 10 by default */
    float boost;           /**< Speed multiplier while Fast is held, 4 by default */
    float sensitivity;     /**< Radians turned per point of cursor movement, 0.003 by default */
    float acceleration;    /**< How fast the velocity follows the input per second, 0 snaps */
    bool invert_y;         /**< Moving the cursor down looks up */
    ecs_entity_t actions;  /**< Scope of the actions, box.controllers.camera when 0 */
    vec3 velocity;         /**< Velocity in units per second, written by the controller */
});

/** First person controller that walks its entity and writes its Position and Rotation.
 *
 * The Position is the eye, above the feet. WASD walk along the yaw, Look turns, Jump leaves the
 * ground, gravity pulls the feet onto the ground plane and Crouch lowers the eye and slows down.
 * Fly toggles free flight that moves like a camera controller. A zero setting takes its default.
 */
ECS_STRUCT(FirstPersonController, {
    float speed;           /**< Walking units per second, 4 by default */
    float crouch_speed;    /**< Units per second while crouching, 2 by default */
    float fly_speed;       /**< Units per second in free flight, 10 by default */
    float boost;           /**< Flight speed multiplier while Fast is held, 4 by default */
    float jump;            /**< Upward speed of a jump, 5 by default */
    float gravity;         /**< Downward acceleration in units per second squared, 18 by default */
    float eye;             /**< Height of the eye above the feet, 1.7 by default */
    float crouch_eye;      /**< Height of the eye above the feet while crouching, 0.9 by default */
    float ground;          /**< Height of the ground plane, 0 by default */
    float sensitivity;     /**< Radians turned per point of cursor movement, 0.003 by default */
    float acceleration;    /**< How fast the walk or flight follows the input per second, 0 snaps */
    bool invert_y;         /**< Moving the cursor down looks up */
    ecs_entity_t actions;  /**< Scope of the actions, box.controllers.first_person when 0 */
    vec3 velocity;         /**< Velocity in units per second, written by the controller */
    float height;          /**< Height of the eye above the feet, written by the controller */
    bool grounded;         /**< The feet stand on the ground, written by the controller */
    bool crouched;         /**< Crouch is held while walking, written by the controller */
    bool flying;           /**< Free flight, toggled by the Fly action */
});

/** Register the box.controllers module and the default controller actions.
 *
 * The actions Forward (W), Back (S), Left (A), Right (D), Up (E), Down (Q), Fast (ShiftLeft)
 * and Look (MouseRight) are created under box.controllers.camera, each with its Binding.
 *
 * The same actions plus Jump (Space), Crouch (CtrlLeft) and Fly (F) are created under
 * box.controllers.first_person. The actions of a scope stay disabled while no controller uses
 * them, so they only bind in worlds that do.
 *
 * @param world The world
 */
void BoxControllersImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
