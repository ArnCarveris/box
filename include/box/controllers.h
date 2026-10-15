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

/** Fly controller of a camera that moves and turns like the flecs-engine camera controller.
 *
 * Forward, Back, Left and Right move along the yaw, Up and Down along Y, Fast boosts and the
 * wheel pushes along the view. Velocity and turn rate ease to their target at e^-(rate * t),
 * at the acceleration rates while an action is held, else at the deceleration rates.
 *
 * Turn actions ease to turn_speed, Look adds the cursor and stops on release. The yaw (Rotation.y)
 * turns left and stays in [-pi, pi]: one set outside is rewritten next frame, with an OnSet. The
 * pitch looks up, 0.05 short of vertical. Members apply as specified, a rate of 0 never changes.
 */
ECS_STRUCT(CameraController, {
    float speed;             /**< Units per second, 40 by default */
    float boost;             /**< Speed multiplier while Fast is held, 5 by default */
    float acceleration;      /**< Rate the velocity follows a held movement, 6 per second */
    float deceleration;      /**< Rate the velocity follows a stop, 18 per second */
    float turn_speed;        /**< Radians per second of the turn actions, 1.5 by default */
    float turn_acceleration; /**< Rate the turn follows a held turn action, 5 per second */
    float turn_deceleration; /**< Rate the turn follows a stop, 15 per second */
    float boost_yaw;         /**< Yaw rate multiplier while Fast is held, 0.33 by default */
    float sensitivity;       /**< Radians turned per point of cursor movement, 0.003 by default */
    float zoom;              /**< Units per second one wheel notch adds along the view, 3 */
    bool invert_y;           /**< Moving the cursor down looks up */
    ecs_entity_t actions;    /**< Scope of the actions, default box.controllers.camera */
    vec3 velocity;           /**< Velocity in units per second, written by the controller */
    vec2 angular_velocity;   /**< Pitch (x) and yaw (y) rate in radians per second, written */
    bool collide;            /**< Slide along the shapes of the scene instead of flying through */
    float radius;            /**< Radius of the sphere that collides, 0.25 by default */
});

/** First person controller that walks its entity and writes its Position and Rotation.
 *
 * The Position is the eye, above the feet. WASD walk along the yaw, Look and the turn actions
 * turn like a camera controller, Jump leaves the ground, gravity pulls the feet down, Crouch
 * lowers the eye and Fly toggles free flight. The constructor sets the defaults.
 *
 * The body is a capsule from the feet to just above the eye that slides along the shapes of
 * its scene, stands on them, steps onto ledges and only stands up when there is room. The
 * ground plane holds the feet only where no shape is below them.
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
    float ground;          /**< Height of the ground plane where no shape is below, default 0 */
    float radius;          /**< Radius of the body, 0.3 by default */
    float step;            /**< Height of the ledges the feet step onto, 0.3 by default */
    float sensitivity;     /**< Radians turned per point of cursor movement, 0.003 by default */
    float acceleration;    /**< How fast the walk or flight follows the input per second, 0 snaps */
    float turn_speed;      /**< Radians per second of the turn actions, 1.5 by default */
    float turn_acceleration; /**< Rate the turn follows a held turn action, 5 per second */
    float turn_deceleration; /**< Rate the turn follows a stop, 15 per second */
    bool invert_y;         /**< Moving the cursor down looks up */
    ecs_entity_t actions;  /**< Scope of the actions, default box.controllers.first_person */
    vec3 velocity;         /**< Velocity in units per second, written by the controller */
    vec2 angular_velocity; /**< Pitch (x) and yaw (y) turn rate in radians per second, written */
    float height;          /**< Height of the eye above the feet, written by the controller */
    bool grounded;         /**< The feet stand on the ground, written by the controller */
    bool crouched;         /**< Crouch is held or there is no room to stand, written */
    bool flying;           /**< Free flight, toggled by the Fly action */
});

/** Register the box.controllers module and the default controller actions.
 *
 * Under box.controllers.camera: Forward (W), Back (S), Left (A), Right (D), Up (E), Down (Q),
 * Fast (ShiftLeft, ShiftRight as a child binding), Look (MouseRight) and TurnLeft, TurnRight,
 * TurnUp and TurnDown (the arrow keys). Under box.controllers.first_person the same.
 *
 * The first person scope adds Jump (Space), Crouch (CtrlLeft) and Fly (F). The actions of a
 * scope stay disabled while no controller uses them, so they only bind in worlds that do.
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
