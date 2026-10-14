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
    BoxCameraTurnLeft,   /**< Turn left while held */
    BoxCameraTurnRight,  /**< Turn right while held */
    BoxCameraTurnUp,     /**< Look up while held */
    BoxCameraTurnDown,   /**< Look down while held */
    BoxCameraActionCount /**< Number of camera actions */
} box_camera_action_t;

/** Name, description and default keys of one controller action */
typedef struct box_controller_action_t {
    const char *name;        /**< Name of the action entity under its scope */
    const char *description; /**< What the action does */
    Key key;                 /**< Key of the default binding on the action */
    Key alt;                 /**< Key of a child binding named after it, KeyNone for none */
} box_controller_action_t;

/** The turn of a controller over one frame, as its settings and actions ask for it */
typedef struct box_controller_turn_t {
    float speed;        /**< Radians per second the turn actions reach */
    float acceleration; /**< Rate the turn follows held turn actions, per second */
    float deceleration; /**< Rate the turn follows a stop, per second */
    float sensitivity;  /**< Radians per point of cursor movement */
    float yaw_scale;    /**< Multiplier of the yaw rate */
    bool invert_y;      /**< Moving the cursor down looks up */
    float yaw;          /**< Turn actions: 1 turns left, -1 right */
    float pitch;        /**< Turn actions: 1 looks up, -1 down */
    bool look;          /**< Look is held, the cursor turns */
    vec2 delta;         /**< Cursor movement of the frame */
} box_controller_turn_t;

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
    ecs_entity_t alts[BoxCameraActionCount];     /**< Their child bindings, 0 for none */
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
    BoxFirstPersonTurnLeft,   /**< Turn left while held */
    BoxFirstPersonTurnRight,  /**< Turn right while held */
    BoxFirstPersonTurnUp,     /**< Look up while held */
    BoxFirstPersonTurnDown,   /**< Look down while held */
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
    ecs_entity_t alts[BoxFirstPersonActionCount];    /**< Their child bindings, 0 for none */
    bool active;                                     /**< The actions are enabled */
} FirstPersonActionMap;

extern ECS_COMPONENT_DECLARE(FirstPersonActionMap);

/** Longest frame a controller simulates: a longer frame, such as a stall, counts as this long */
#define BOX_CONTROLLER_DT_MAX (0.5f)

/** Shape a collider has in its own frame */
typedef enum box_collider_kind_t {
    BoxColliderBox,      /**< Box with the half extents, also the fallback of other meshes */
    BoxColliderSphere,   /**< Ball with the radius half[0] */
    BoxColliderCylinder, /**< Cylinder along the second axis, radius half[0], half height half[1] */
    BoxColliderCapsule   /**< Capsule along the second axis, radius half[0], half height half[1] */
} box_collider_kind_t;

/** A shape of the scene as the narrow phase sees it: an oriented box, ball, cylinder or capsule */
typedef struct box_collider_t {
    ecs_entity_t entity;      /**< The entity of the shape */
    box_collider_kind_t kind; /**< Shape in its own frame */
    vec3 center;              /**< Center in world space */
    vec3 axis[3];             /**< Unit axes of its frame in world space */
    vec3 half;                /**< Half extents along the axes in world units */
} box_collider_t;

/** The capsule a controller moves as, from its feet up */
typedef struct box_controller_body_t {
    ecs_entity_t entity; /**< The controlled entity, its shapes and its children's are ignored */
    ecs_entity_t scene;  /**< Scene whose shapes block the body, 0 for the default scene */
    float radius;        /**< Radius of the capsule, 0 moves without collision */
    float height;        /**< Height of the capsule, a ball when at most twice the radius */
    float step;          /**< Height of the ledges the body steps onto, 0 for none */
    float probe;         /**< Distance below the feet the ground is searched for, 0 for none */
} box_controller_body_t;

/** What a move of a body ran into */
typedef struct box_controller_hit_t {
    bool grounded; /**< The body ended on a surface flat enough to stand on */
    bool blocked;  /**< A shape pushed the body back */
} box_controller_hit_t;

/** Resolve the collider of an indexed shape.
 *
 * Boxes are exact, Sphere and IcoSphere become balls, Cylinder a cylinder and Capsule a
 * capsule while their scale keeps them round; every other mesh is its oriented bounding box.
 *
 * @param world The world
 * @param e The shape
 * @param out The collider, written
 * @return false when the entity has no mesh
 */
bool box_collider_resolve(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_collider_t *out);

/** Find the signed distance between a capsule and a collider.
 *
 * @param c The collider
 * @param a Center of the lower cap of the capsule
 * @param b Center of the upper cap of the capsule
 * @param radius Radius of the capsule
 * @param normal Receives the direction from the collider towards the capsule
 * @return The gap between their surfaces, negative by the depth when they overlap
 */
float box_collider_separation(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    vec3 normal);

/** Find how far a capsule reaches into a collider.
 *
 * @param c The collider
 * @param a Center of the lower cap of the capsule
 * @param b Center of the upper cap of the capsule
 * @param radius Radius of the capsule
 * @param normal Receives the direction that moves the capsule out
 * @param depth Receives the distance the capsule must move along the normal
 * @return true when the capsule overlaps the collider
 */
bool box_collider_push(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    vec3 normal,
    float *depth);

/** Intersect a ray with the oriented box of a collider, or the ball of a sphere.
 *
 * @param c The collider
 * @param origin Origin of the ray
 * @param dir Direction of the ray, need not be normalized
 * @param max Longest distance along dir that counts
 * @param t Receives the distance of the hit in lengths of dir
 * @return true when the ray hits
 */
bool box_collider_ray(
    const box_collider_t *c,
    const vec3 origin,
    const vec3 dir,
    float max,
    float *t);

/** Move a body by its velocity over a frame, sliding along the shapes of its scene.
 *
 * Only the shapes the spatial index finds around the swept capsule are tested. A body that
 * walks into a ledge up to its step height steps onto it, and a body with a probe sticks to
 * ground below it. The velocity loses what pushes into what the body touched.
 *
 * @param world The world
 * @param body The body
 * @param feet The bottom of the capsule, written in place
 * @param velocity Velocity in units per second, written when the move is blocked
 * @param dt Seconds the frame took
 * @param hit Receives what the move ran into, may be NULL
 * @return Whether the position changed
 */
bool box_controller_move(
    ecs_world_t *world,
    const box_controller_body_t *body,
    Position *feet,
    vec3 velocity,
    float dt,
    box_controller_hit_t *hit);

/** Test whether a body fits at a position without overlapping a shape of its scene.
 *
 * @param world The world
 * @param body The body
 * @param feet The bottom of the capsule
 * @return true when the body overlaps no shape
 */
bool box_controller_fits(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet);

/** Test whether a shape of the scene of a body lies anywhere below its feet.
 *
 * @param world The world
 * @param body The body
 * @param feet The bottom of the capsule
 * @return true when a ray down from the lower cap hits a shape
 */
bool box_controller_ground_below(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet);

/** Create the default actions of a controller under a scope, disabled and bound to their keys.
 *
 * @param world The world
 * @param name Name of the scope
 * @param table The actions
 * @param count Number of actions
 * @param actions The action entities, written
 * @param alts The child bindings of the actions, written, 0 for none
 * @return The scope
 */
ecs_entity_t box_controller_actions_create(
    ecs_world_t *world,
    const char *name,
    const box_controller_action_t *table,
    int32_t count,
    ecs_entity_t *actions,
    ecs_entity_t *alts);

/** Enable or disable default actions and their child bindings, releasing their state.
 *
 * @param world The world
 * @param actions The action entities
 * @param alts The child bindings of the actions, 0 for none
 * @param count Number of actions
 * @param enabled Whether the actions bind
 */
void box_controller_actions_enable(
    ecs_world_t *world,
    const ecs_entity_t *actions,
    const ecs_entity_t *alts,
    int32_t count,
    bool enabled);

/** Turn an entity over one frame like the flecs-engine camera controller.
 *
 * The angular velocity eases towards the turn actions at the acceleration rate while one is
 * held and towards a stop at the deceleration rate when none is.
 *
 * While Look is held the cursor movement sets it so the frame turns by exactly the movement.
 * The pitch stops short of straight up and down, dropping the angular velocity towards it.
 *
 * @param turn What the frame asks for
 * @param angular_velocity Pitch (x) and yaw (y) rate in radians per second, written in place
 * @param r The rotation, written in place
 * @param dt Seconds the frame took
 * @return Whether the rotation changed
 */
bool box_controller_turn(
    const box_controller_turn_t *turn,
    vec2 angular_velocity,
    Rotation *r,
    float dt);

/** Construct CameraController values with the documented defaults and the default actions.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info, whose context points to the scope of the default camera actions
 */
void box_camera_controller_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti);

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

/** Construct FirstPersonController values with the documented defaults and the default actions.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info, whose context points to the scope of the default first person actions
 */
void box_first_person_controller_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti);

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
