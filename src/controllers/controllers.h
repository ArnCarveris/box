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

/** Upward component of the normal of an edge the drop of a step rests on */
#define BOX_CONTROLLER_SUPPORT (0.3f)

/** Distance beyond the radius of a body at which it takes hold of a Climbable shape */
#define BOX_CONTROLLER_REACH (0.15f)

/** Shape a collider has in its own frame */
typedef enum box_collider_kind_t {
    BoxColliderBox,      /**< Box with the half extents, also the fallback of other meshes */
    BoxColliderSphere,   /**< Ball with the radius half[0] */
    BoxColliderCylinder, /**< Cylinder along the second axis, radius half[0], half height half[1] */
    BoxColliderCapsule,  /**< Capsule along the second axis, radius half[0], half height half[1] */
    BoxColliderTriangle  /**< Triangle of a TriangleCollider mesh, its corners in corner */
} box_collider_kind_t;

/** A shape of the scene as the narrow phase sees it: an oriented box, ball, cylinder or capsule */
typedef struct box_collider_t {
    ecs_entity_t entity;      /**< The entity of the shape */
    box_collider_kind_t kind; /**< Shape in its own frame */
    vec3 center;              /**< Center in world space */
    vec3 axis[3];             /**< Unit axes of its frame in world space */
    vec3 half;                /**< Half extents along the axes in world units */
    vec3 corner[3];           /**< Corners of a triangle in world space, BoxColliderTriangle */
} box_collider_t;

/** The capsule a controller moves as, from its feet up */
typedef struct box_controller_body_t {
    ecs_entity_t entity; /**< The controlled entity, its shapes and its children's are ignored */
    ecs_entity_t scene;  /**< Scene whose shapes block the body, 0 for the default scene */
    float radius;        /**< Radius of the capsule, 0 moves without collision */
    float height;        /**< Height of the capsule, a ball when at most twice the radius */
    float step;          /**< Height of the ledges the body steps onto, 0 for none */
    float probe;         /**< Distance below the feet the ground is searched for, 0 for none */
    bool upright;        /**< Surfaces too steep to stand on block like walls, never lift it */
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

/** Where a capsule touches a collider */
typedef struct box_collider_contact_t {
    vec3 normal;  /**< Direction from the collider towards the capsule, that moves it out */
    vec3 surface; /**< Normal of the face touched or of the shape below, see box_collider_push */
    float depth;  /**< Overlap of the capsule and the collider along the normal, positive */
    bool round;   /**< The surface is up because the body overhangs a round shape */
} box_collider_contact_t;

/** Find the signed distance between a capsule and a collider.
 *
 * @param c The collider
 * @param a Center of the lower cap of the capsule
 * @param b Center of the upper cap of the capsule
 * @param radius Radius of the capsule
 * @param hint Way out when the segment runs through the core of a ball or capsule, may be NULL
 * @param normal Receives the direction from the collider towards the capsule
 * @return The gap between their surfaces, negative by the depth when they overlap
 */
float box_collider_separation(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    const vec3 hint,
    vec3 normal);

/** Find how far a capsule reaches into a collider.
 *
 * With surface set, a contact that faces up by BOX_CONTROLLER_SUPPORT or more gets the normal
 * of the flat face it lies on, else where a ray down from the lower cap enters the shape.
 *
 * When that ray misses (the body overhangs an edge or a round shape) the surface is up, with
 * round set for a ball, a capsule or the side of a cylinder. Other contacts take the normal.
 *
 * @param c The collider
 * @param a Center of the lower cap of the capsule
 * @param b Center of the upper cap of the capsule
 * @param radius Radius of the capsule
 * @param hint Way out when the segment runs through the core of a ball or capsule, may be NULL
 * @param surface Find the surface of the contact
 * @param out The contact, written when the capsule overlaps the collider
 * @return true when the capsule overlaps the collider
 */
bool box_collider_push(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    const vec3 hint,
    bool surface,
    box_collider_contact_t *out);

/** Intersect a ray with the outside of a collider: its box, ball, cylinder or capsule.
 *
 * @param c The collider
 * @param origin Origin of the ray
 * @param dir Direction of the ray, need not be normalized
 * @param max Longest distance along dir that counts
 * @param t Receives the distance of the hit in lengths of dir, 0 when the origin is inside
 * @param normal Receives the normal of the surface at the hit, zero inside, may be NULL
 * @return true when the ray hits
 */
bool box_collider_ray(
    const box_collider_t *c,
    const vec3 origin,
    const vec3 dir,
    float max,
    float *t,
    vec3 normal);

/** Move a body by its velocity over a frame, sliding along the shapes of its scene.
 *
 * Only the shapes the spatial index finds around the swept capsule are tested. A body that
 * walks into a ledge up to its step height steps onto it, and a body with a probe sticks to
 * ground below it. The velocity loses what pushes into what the body touched.
 *
 * @param world The world
 * @param body The body
 * @param feet The bottom of the capsule, written in place
 * @param velocity Velocity in units per second, written when the move is blocked; a velocity
 * that is not finite does not move the body
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

/** Find the nearest shape of the scene of a body below its feet.
 *
 * @param world The world
 * @param body The body
 * @param feet The bottom of the capsule
 * @return The shape a ray down from the lower cap hits first, 0 for none
 */
ecs_entity_t box_controller_ground_shape(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet);

/** Get the world transform of a mover from its Position, Rotation and Scale as they are now,
 * not as the last WorldTransform update left it (controllers/pose.c).
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the transform
 * @return false when the entity has no place in the world
 */
bool box_controller_pose(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out);

/** Move a point and its yaw with the mover that carries it since the last call
 * (controllers/ride.c).
 *
 * The carrier's world transform now, times the inverse of its pose at the last call, moves the
 * point and adds its turn about the vertical axis to the yaw; the pose becomes the transform
 * now. A carrier that is gone is dropped.
 *
 * @param world The world
 * @param carrier The mover, 0 for none, cleared when it is gone
 * @param pose Its world transform at the last call, 16 floats as a mat4, written
 * @param p The point, moved in place
 * @param r The rotation whose yaw (y) turns with the carrier, NULL to leave it
 * @return Whether the point or the yaw changed
 */
bool box_controller_ride(
    const ecs_world_t *world,
    ecs_entity_t *carrier,
    float *pose,
    Position *p,
    Rotation *r);

/** Take the mover of the shape a body stands on as its carrier (controllers/ride.c).
 *
 * A carrier that changes starts from its world transform now, so it moves the body only from
 * the next box_controller_ride on.
 *
 * @param world The world
 * @param shape The shape below the feet, 0 for none
 * @param carrier The carrier, written: the mover of the shape, 0 when it moves with none
 * @param pose Its world transform, 16 floats as a mat4, written when the carrier changes
 */
void box_controller_board(
    const ecs_world_t *world,
    ecs_entity_t shape,
    ecs_entity_t *carrier,
    float *pose);

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
 * While Look is held the cursor turns the rotation by exactly its movement, never through the
 * angular velocity, so the turn stops when the cursor does. The pitch stops short of vertical,
 * dropping the angular velocity towards it, and the yaw wraps to [-pi, pi].
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

/** Triangle hierarchies of the meshes of TriangleCollider shapes, by mesh handle */
typedef struct BoxTriangleMeshes {
    ecs_map_t meshes; /**< box_triangle_mesh_t* by mesh handle */
} BoxTriangleMeshes;

extern ECS_COMPONENT_DECLARE(BoxTriangleMeshes);

/** Register the tags of the module and the triangle hierarchies of TriangleCollider meshes.
 *
 * @param world The world
 */
void box_collider_triangles_init(
    ecs_world_t *world);

/** Make a triangle collider of a shape from its corners in world space.
 *
 * The center, axes and half extents are the box around the corners, so the bounds of a
 * triangle are found like those of every other collider.
 *
 * @param e The shape
 * @param v The corners
 * @param out The collider, written
 */
void box_triangle_collider(
    ecs_entity_t e,
    const vec3 v[3],
    box_collider_t *out);

/** Find the signed distance between a capsule and a triangle.
 *
 * @param c The triangle collider
 * @param a Center of the lower cap of the capsule
 * @param b Center of the upper cap of the capsule
 * @param radius Radius of the capsule
 * @param normal Receives the direction from the triangle towards the capsule
 * @return The gap between their surfaces, negative by the depth when they overlap
 */
float box_triangle_separation(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    vec3 normal);

/** Find how far a capsule reaches into a triangle, like box_collider_push.
 *
 * With surface set, a contact on a face flat enough to stand on gets the normal of that face.
 *
 * @param c The triangle collider
 * @param a Center of the lower cap of the capsule
 * @param b Center of the upper cap of the capsule
 * @param radius Radius of the capsule
 * @param surface Find the surface of the contact
 * @param out The contact, written when the capsule overlaps the triangle
 * @return true when the capsule overlaps the triangle
 */
bool box_triangle_push(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    bool surface,
    box_collider_contact_t *out);

/** Intersect a ray with a triangle from either side.
 *
 * @param c The triangle collider
 * @param origin Origin of the ray
 * @param dir Direction of the ray, need not be normalized
 * @param max Longest distance along dir that counts, in lengths of dir
 * @param t Receives the distance of the hit in lengths of dir
 * @param normal Receives the normal of the triangle facing the ray, may be NULL
 * @return true when the ray hits
 */
bool box_triangle_ray(
    const box_collider_t *c,
    const vec3 origin,
    const vec3 dir,
    float max,
    float *t,
    vec3 normal);

/** Test whether a shape blocks controllers: Water shapes are fluid and do not.
 *
 * @param world The world
 * @param e The shape
 * @return false for a Water shape
 */
bool box_collider_solid(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Test whether a shape collides by its triangles (TriangleCollider) instead of its bounds.
 *
 * @param world The world
 * @param e The shape
 * @return true for a TriangleCollider shape with a mesh
 */
bool box_collider_has_triangles(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Append the triangles of a TriangleCollider shape whose bounds overlap a box.
 *
 * @param world The world
 * @param e The shape
 * @param min Minimum corner of the box in world space
 * @param max Maximum corner of the box in world space
 * @param out Vector of box_collider_t the triangles are appended to
 * @return Number of triangles appended
 */
int32_t box_collider_triangles(
    const ecs_world_t *world,
    ecs_entity_t e,
    const vec3 min,
    const vec3 max,
    ecs_vec_t *out);

/** Intersect a ray with a shape: its triangles for a TriangleCollider, else its collider.
 *
 * @param world The world
 * @param e The shape
 * @param origin Origin of the ray
 * @param dir Direction of the ray, need not be normalized
 * @param max Longest distance along dir that counts, in lengths of dir
 * @param t Receives the distance of the nearest hit in lengths of dir
 * @param normal Receives the normal of the surface at the hit, may be NULL
 * @return true when the ray hits
 */
bool box_collider_cast(
    const ecs_world_t *world,
    ecs_entity_t e,
    const vec3 origin,
    const vec3 dir,
    float max,
    float *t,
    vec3 normal);

/** Find the shapes of a scene whose bounds overlap a box, the shapes under movers included.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param out Vector of ecs_entity_t the shapes are appended to
 * @return Number of shapes appended
 */
int32_t box_controller_near(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 min,
    const vec3 max,
    ecs_vec_t *out);

/** A Climbable shape a body touches */
typedef struct box_controller_ladder_t {
    ecs_entity_t shape; /**< The shape */
    float top;          /**< Height of its top in world space */
} box_controller_ladder_t;

/** Find a Climbable shape, or one below a Climbable entity, within reach of a body.
 *
 * A body reaches a shape whose bounds, grown by the radius and BOX_CONTROLLER_REACH across, hold
 * its feet, from a little below the bottom of the shape up to a little above its top.
 *
 * @param world The world
 * @param body The body
 * @param feet The bottom of the capsule
 * @param out The shape in reach, written when there is one
 * @return true when a Climbable shape is in reach
 */
bool box_controller_ladder(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet,
    box_controller_ladder_t *out);

/** Find the surface of the water a body is in.
 *
 * Water is a shape with the Water material. Its surface is where a ray down from above the
 * body enters it, so a flat mesh, a box or a TriangleCollider surface all work.
 *
 * The water is open: the body is in it only when no solid shape lies above its feet, so a room
 * dug below the water level, under a ceiling or the ground, stays dry, and so does covered
 * water, such as a sea plane that runs on under an island or a hull.
 *
 * @param world The world
 * @param body The body
 * @param feet The bottom of the capsule
 * @param reach How far above the feet the surface may lie
 * @param below How far below the feet the surface may lie, 0 for feet under the surface
 * @param level Receives the height of the surface
 * @return true when a water surface lies within reach above or below the feet
 */
bool box_controller_water(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet,
    float reach,
    float below,
    float *level);

/** The actions of the helm of a vessel, the index of their name in the action table */
typedef enum box_helm_action_t {
    BoxHelmUse,         /**< Take the helm near the stand, or let go of it */
    BoxHelmAhead,       /**< Move the throttle ahead while held */
    BoxHelmAstern,      /**< Move the throttle astern while held */
    BoxHelmPort,        /**< Turn the rudder to port while held */
    BoxHelmStarboard,   /**< Turn the rudder to starboard while held */
    BoxHelmStop,        /**< Stop the engine */
    BoxHelmCentre,      /**< Centre the rudder */
    BoxHelmLook,        /**< Turn the pilot's view with the cursor while held */
    BoxHelmTurnLeft,    /**< Turn the pilot's view left while held */
    BoxHelmTurnRight,   /**< Turn the pilot's view right while held */
    BoxHelmTurnUp,      /**< Turn the pilot's view up while held */
    BoxHelmTurnDown,    /**< Turn the pilot's view down while held */
    BoxHelmActionCount  /**< Number of helm actions */
} box_helm_action_t;

/** The helm actions, indexed by box_helm_action_t */
extern const box_controller_action_t box_helm_actions[BoxHelmActionCount];

/** The default helm actions, a private singleton, disabled while no vessel uses them */
typedef struct HelmActionMap {
    ecs_entity_t scope;                       /**< box.controllers.helm */
    ecs_entity_t actions[BoxHelmActionCount]; /**< The actions, by index */
    ecs_entity_t alts[BoxHelmActionCount];    /**< Their child bindings, 0 for none */
    bool active;                              /**< The actions are enabled */
} HelmActionMap;

extern ECS_COMPONENT_DECLARE(HelmActionMap);

/** What the helm actions of a vessel do in one frame */
typedef struct box_helm_input_t {
    bool held[BoxHelmActionCount];    /**< The action is held */
    bool pressed[BoxHelmActionCount]; /**< The action went down this frame */
} box_helm_input_t;

/** Arc length table of a closed route seen from above (controllers/route.c) */
typedef struct box_route_t {
    int32_t count;     /**< Segments, one per waypoint */
    int32_t samples;   /**< Samples, BOX_ROUTE_STEPS per segment */
    vec3 (*cubics)[4]; /**< Cubic Bezier points of every segment */
    float *lengths;    /**< Arc length at every sample, from 0 */
    float *params;     /**< Segment index plus segment parameter at every sample */
    float total;       /**< Length of the route, back to its start */
} box_route_t;

/** Samples of one segment of a route */
#define BOX_ROUTE_STEPS 64

/** Build the arc length table of a closed Path3D in the XZ plane.
 *
 * @param path The path, may be NULL
 * @param route The table, written; free it with box_route_fini
 * @return Whether the path is valid, closed and longer than zero
 */
bool box_route_init(
    const Path3D *path,
    box_route_t *route);

/** Free the arrays of a route table.
 *
 * @param route The table
 */
void box_route_fini(
    box_route_t *route);

/** Wrap a distance into a route, 0 up to its length.
 *
 * @param route The table
 * @param distance The distance, any value
 * @return The distance on the route
 */
float box_route_wrap(
    const box_route_t *route,
    float distance);

/** Evaluate the place and the heading of the tangent at a distance along a route.
 *
 * @param route The table
 * @param distance The distance, wrapped into the route
 * @param x Receives X
 * @param z Receives Z
 * @param heading Receives the heading of the tangent, 0 along +Z, positive toward +X
 */
void box_route_at(
    const box_route_t *route,
    float distance,
    float *x,
    float *z,
    float *heading);

/** Find the sample of a route nearest to a point.
 *
 * @param route The table
 * @param x X of the point
 * @param z Z of the point
 * @return The distance along the route of that sample
 */
float box_route_nearest(
    const box_route_t *route,
    float x,
    float z);

/** The distance along a route of one of its waypoints.
 *
 * @param route The table
 * @param index The waypoint, wrapped into the route
 * @return Its distance
 */
float box_route_waypoint(
    const box_route_t *route,
    int32_t index);

/** Count the samples of the hull of a vessel at a place and heading that come down on ground
 * or a shape (controllers/hull.c).
 *
 * @param world The world
 * @param e The vessel, whose own shapes do not count
 * @param vessel Its settings
 * @param y Height of its frame in the parent space, unturned (the heave)
 * @param x X of the pivot in the parent space
 * @param z Z of the pivot in the parent space
 * @param heading The heading
 * @return Samples of the keel line and the deck line that are blocked, 0 when clear
 */
int32_t box_vessel_blocked(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    float y,
    float x,
    float z,
    float heading);

/** Read the helm actions of a scope (controllers/helm.c).
 *
 * @param world The world
 * @param scope The scope, the default scope or 0 for none
 * @param out The input, written
 */
void box_helm_read(
    ecs_world_t *world,
    ecs_entity_t scope,
    box_helm_input_t *out);

/** Move the throttle and the rudder of a vessel as the pilot's actions say (controllers/helm.c).
 *
 * @param vessel The settings
 * @param status The state, throttle and rudder written
 * @param in The actions
 * @param dt Seconds of the frame
 */
void box_helm_steer(
    const Vessel *vessel,
    VesselStatus *status,
    const box_helm_input_t *in,
    float dt);

/** Hand the helm to the first person controller within reach of the stand, or let it go when
 * its pilot uses it again (controllers/helm.c).
 *
 * @param world The world
 * @param e The vessel
 * @param vessel The settings
 * @param status The state, pilot and mode written
 * @param in The helm actions
 */
void box_helm_use(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    VesselStatus *status,
    const box_helm_input_t *in);

/** Keep the pilot at the stand of a vessel at its pose of this frame, and turn the wheel and
 * the lever (controllers/helm.c).
 *
 * @param world The world
 * @param e The vessel
 * @param vessel The settings
 * @param status The state
 */
void box_helm_place(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    const VesselStatus *status);

/** Constructor of Vessel with its defaults and the default helm actions (controllers/vessel.c). */
void box_vessel_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti);

/** Enable the default helm actions while a vessel uses them (controllers/helm.c). */
void BoxHelmActionsActivate(
    ecs_iter_t *it);

/** Move every Vessel along its route or as its pilot steers (controllers/vessel.c). */
void BoxVesselUpdate(
    ecs_iter_t *it);

#endif
