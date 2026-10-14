/**
 * @file controllers/controllers.h
 * @brief Private declarations of the box.controllers module: the action table and the move seam.
 */

#ifndef BOX_CONTROLLERS_INTERNAL_H
#define BOX_CONTROLLERS_INTERNAL_H

#include <box.h>

/** The actions a camera controller reads, the index of their name in the action table */
typedef enum box_camera_action_t {
    BoxCameraForward,    /**< Move along the yaw in the XZ plane */
    BoxCameraBack,       /**< Move against the yaw in the XZ plane */
    BoxCameraLeft,       /**< Strafe left in the XZ plane */
    BoxCameraRight,      /**< Strafe right in the XZ plane */
    BoxCameraUp,         /**< Move up along Y */
    BoxCameraDown,       /**< Move down along Y */
    BoxCameraFast,       /**< Multiply the speed by the boost while held */
    BoxCameraLook,       /**< Turn with the cursor while held */
    BoxCameraActionCount /**< Number of camera actions */
} box_camera_action_t;

/** Name, description and default key of one controller action */
typedef struct box_controller_action_t {
    const char *name;        /**< Name of the action entity under its scope */
    const char *description; /**< What the action does */
    Key key;                 /**< Key of the default binding */
} box_controller_action_t;

/** The camera actions, indexed by box_camera_action_t */
extern const box_controller_action_t box_camera_actions[BoxCameraActionCount];

/** The default camera actions, a private singleton.
 *
 * The actions are disabled while no camera controller uses them, so they neither bind nor
 * show up as actions in a world without a camera controller.
 */
typedef struct CameraActionMap {
    ecs_entity_t scope;                          /**< box.controllers.camera */
    ecs_entity_t actions[BoxCameraActionCount];  /**< The actions, by box_camera_action_t */
    ecs_query_t *controllers;                    /**< Entities with a CameraController */
    bool active;                                 /**< The actions are enabled */
} CameraActionMap;

extern ECS_COMPONENT_DECLARE(CameraActionMap);

/** The actions a first person controller reads, the index of their name in the action table */
typedef enum box_first_person_action_t {
    BoxFirstPersonForward,    /**< Walk along the yaw in the XZ plane */
    BoxFirstPersonBack,       /**< Walk against the yaw in the XZ plane */
    BoxFirstPersonLeft,       /**< Strafe left in the XZ plane */
    BoxFirstPersonRight,      /**< Strafe right in the XZ plane */
    BoxFirstPersonUp,         /**< Move up along Y in free flight */
    BoxFirstPersonDown,       /**< Move down along Y in free flight */
    BoxFirstPersonFast,       /**< Multiply the flight speed by the boost while held */
    BoxFirstPersonLook,       /**< Turn with the cursor while held */
    BoxFirstPersonJump,       /**< Leave the ground when pressed */
    BoxFirstPersonCrouch,     /**< Lower the eye and walk slower while held */
    BoxFirstPersonFly,        /**< Toggle free flight when pressed */
    BoxFirstPersonActionCount /**< Number of first person actions */
} box_first_person_action_t;

/** The first person actions, indexed by box_first_person_action_t */
extern const box_controller_action_t box_first_person_actions[BoxFirstPersonActionCount];

/** The default first person actions, a private singleton.
 *
 * The actions are disabled while no first person controller uses them.
 */
typedef struct FirstPersonActionMap {
    ecs_entity_t scope;                              /**< box.controllers.first_person */
    ecs_entity_t actions[BoxFirstPersonActionCount]; /**< The actions, by index */
    bool active;                                     /**< The actions are enabled */
} FirstPersonActionMap;

extern ECS_COMPONENT_DECLARE(FirstPersonActionMap);

/** Move a controlled entity by its velocity over a frame.
 *
 * This is where a controller meets the scene: collision slides the move along what it hits
 * and writes the velocity of a blocked direction. Without collision the move is unobstructed.
 *
 * @param world The world
 * @param entity The controlled entity
 * @param p The position, written in place
 * @param velocity Velocity in units per second, written when the move is blocked
 * @param dt Seconds the frame took
 * @return Whether the position changed
 */
bool box_controller_move(
    ecs_world_t *world,
    ecs_entity_t entity,
    Position *p,
    vec3 velocity,
    float dt);

/** Enable the default camera actions while a controller uses them, else disable them.
 *
 * @param it Iterator of the system, which has no terms
 */
void BoxCameraActionsActivate(
    ecs_iter_t *it);

/** Move and turn the entities that have a camera controller.
 *
 * @param it Iterator over CameraController, Position and Rotation
 */
void BoxCameraControllerUpdate(
    ecs_iter_t *it);

/** Create the default first person actions, disabled and each bound to its key.
 *
 * @param world The world
 * @param map The action map, written
 */
void box_first_person_actions_create(
    ecs_world_t *world,
    FirstPersonActionMap *map);

/** Enable the default first person actions while a controller uses them, else disable them.
 *
 * @param it Iterator over FirstPersonController
 */
void BoxFirstPersonActionsActivate(
    ecs_iter_t *it);

/** Walk, jump, fly and turn the entities that have a first person controller.
 *
 * @param it Iterator over FirstPersonController, Position and Rotation
 */
void BoxFirstPersonControllerUpdate(
    ecs_iter_t *it);

#endif
