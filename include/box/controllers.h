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
 * its scene, stands on them (and goes and turns with their mover, its carrier), steps onto
 * ledges and only stands up when there is room; the ground plane holds it where none is below.
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
    float run;             /**< Walking units per second while Fast is held, 0 to ignore Fast */
    float climb;           /**< Units per second Forward and Back climb a Climbable shape, 2 */
    float swim_speed;      /**< Walking speed multiplier in water, 0.45 by default */
    float swim_jump;       /**< Upward speed of a jump in water, 3.5 by default */
    float swim_depth;      /**< Depth of the feet below the surface of the water, 1.3 by default */
    float swim_step;       /**< Height of the ledges a swimmer climbs out onto, 3.2 by default */
    bool climbing;         /**< Climbing a Climbable shape, written by the controller */
    bool swimming;         /**< Swimming in water, written by the controller */
    ecs_entity_t carrier;  /**< Mover the feet stand on, which carries the body, written */
    float carrier_pose[16]; /**< World transform of the carrier at the last update, written */
});

/** A shape, or the shapes below an entity, that a first person controller climbs like a ladder.
 *
 * While its body touches one, Forward climbs up and Back down at its climb speed, without
 * gravity; at the top Forward steps off. Inherited through IsA, so a ladder prefab marks all
 * of its instances. The shapes still block like any other.
 */
extern ECS_TAG_DECLARE(Climbable);

/** A mesh shape that controllers collide with by its triangles instead of its bounding box.
 *
 * For terrains, hulls and other large or concave meshes; the triangle hierarchy of the mesh is
 * built the first time a body comes near it. Triangles block from both sides.
 */
extern ECS_TAG_DECLARE(TriangleCollider);

/** What steers a Vessel */
ECS_ENUM(VesselMode, {
    VesselDocked,    /**< At rest on the dock mark, waiting to sail */
    VesselCruising,  /**< The autopilot follows the route and stops at the dock mark */
    VesselManual,    /**< A pilot steers at the helm */
    VesselRejoining  /**< The autopilot steers back onto the route after the pilot let go */
});

/** A ship that cruises a closed route and docks, or that a first person controller steers.
 *
 * Its pivot, a point in its local frame, follows the Catmull-Rom Path3D of route, in the parent
 * space of the vessel (its x and z, seen from above). On the route the vessel accelerates at
 * accel to speed, brakes to rest exactly on waypoint dock of the route, waits wait seconds and
 * sails again; its heading (Rotation.y, 0 along +Z) turns toward the tangent through a
 * critically damped spring of rate yaw_response. Waves pitch, roll and heave it, calmer while
 * docked, and it heels into a turn.
 *
 * The Use action of actions with a first person controller within reach of stand makes that
 * controller its pilot: the controller stays at the stand, reads only Look and the turn actions,
 * and Ahead and Astern move the throttle, Port and Starboard turn the self-centring rudder, Stop
 * stops the engine, Centre centres the rudder and Use lets go. The rudder only turns the vessel
 * with water flowing past it, so the turn rate follows the speed and reverses astern. The hull,
 * the Path2D of outline in local x and z at the deck, must not come down on ground higher than
 * keel (along its outline moved inset toward the middle) or on shapes higher than water (along
 * the outline itself) below deck: a turn into them goes straight on, running into them stops
 * the vessel with a bump, and a hull already on them only moves where no more of it is. Let go,
 * the autopilot steers back onto the route and docks.
 *
 * Writes its Position and Rotation, the Rotation.z of wheel (the rudder) and the Rotation.x of
 * lever (the throttle). Implies VesselStatus, Position, Rotation and DynamicTransform.
 *
 * The route, the pivot height and water are in the frame of the route, which origin places in
 * the parent space; a WorldOrigin shift moves origin with the vessel.
 */
ECS_STRUCT(Vessel, {
    ecs_entity_t route;   /**< Entity with the closed Path3D to cruise, its waypoints the anchors */
    vec3 pivot;           /**< Point of the local frame that follows the route */
    int32_t dock;         /**< Waypoint of the route the vessel docks at, 0 by default */
    float speed;          /**< Cruising speed in units per second, 6 by default */
    float accel;          /**< Acceleration in units per second squared, 0.3 by default */
    float wait;           /**< Seconds at the dock before sailing, 25 by default */
    float max_speed;      /**< Speed at full ahead, half of it full astern, 9 by default */
    float max_yaw_rate;   /**< Turn rate at full rudder and half max_speed, 0.12 rad/s */
    float yaw_response;   /**< Rate of the heading spring on the route, 2.5 per second */
    float steer_heel;     /**< Heel per turn rate times speed, 0.6 by default */
    float max_heel;       /**< Greatest heel from steering in radians, 0.105 by default */
    vec3 waves;           /**< Pitch and roll in radians and heave in units of the waves */
    ecs_entity_t outline; /**< Entity with the Path2D of the hull, 0 for no collision */
    float keel;           /**< Local height of the keel, -5 by default */
    float deck;           /**< Local height of the deck, 5 by default */
    float inset;          /**< How far the keel line lies inside the outline, 1.2 by default */
    float water;          /**< Height of the water in the parent space, 0 by default */
    vec3 stand;           /**< Local point the pilot's feet stand on at the helm */
    float reach;          /**< Distance from the stand within which Use takes the helm, 1.4 */
    ecs_entity_t wheel;   /**< Entity turned with the rudder about its Z, 0 for none */
    ecs_entity_t lever;   /**< Entity tilted with the throttle about its X, 0 for none */
    float throttle_rate;  /**< Throttle per second Ahead and Astern move, 0.35 by default */
    float rudder_rate;    /**< Rudder per second Port and Starboard move, 0.9 by default */
    float rudder_return;  /**< Rudder per second it centres by itself, 0.5 by default */
    ecs_entity_t actions; /**< Scope of the helm actions, default box.controllers.helm */
    vec3 origin;          /**< Added to the route, the pivot height and water to place them in
                           *   the parent space, default zero; a WorldOrigin shift moves it */
});

/** The state of a Vessel, written by its system every frame; set mode or distance to seek */
ECS_STRUCT(VesselStatus, {
    VesselMode mode;      /**< What steers the vessel */
    float distance;       /**< Distance along the route of the autopilot */
    float speed;          /**< Speed in units per second, negative astern */
    float heading;        /**< Heading in radians, 0 along +Z, positive toward +X */
    float yaw_rate;       /**< Turn rate in radians per second */
    float throttle;       /**< Throttle, -0.5 full astern to 1 full ahead */
    float rudder;         /**< Rudder, -1 hard to port to 1 hard to starboard */
    float wait;           /**< Seconds left at the dock, 0 when not docked */
    float to_dock;        /**< Distance along the route to the dock mark */
    float heel;           /**< Heel from steering in radians */
    float pitch;          /**< Pitch in radians, bow up positive */
    float roll;           /**< Roll in radians, heel included */
    float heave;          /**< Heave in units */
    float swell;          /**< Share of the waves the vessel feels, low while docked */
    float x;              /**< X of the pivot in the frame of the route */
    float z;              /**< Z of the pivot in the frame of the route */
    vec2 offset;          /**< Place off the route after rejoining it, easing to zero */
    float time;           /**< Seconds the waves have run */
    float backoff;        /**< Seconds the autopilot backs away from a bump */
    int32_t bumps;        /**< Collisions that stopped the vessel so far */
    ecs_entity_t pilot;   /**< First person controller at the helm, 0 for none */
    ecs_entity_t pilot_actions; /**< The pilot's own action scope, given back when it lets go */
    bool placed;          /**< The vessel was put on its route, false to put it at the dock */
});

/** Register the box.controllers module and the default controller actions.
 *
 * Under box.controllers.camera: Forward (W), Back (S), Left (A), Right (D), Up (E), Down (Q),
 * Fast (ShiftLeft, ShiftRight as a child binding), Look (MouseRight) and TurnLeft, TurnRight,
 * TurnUp and TurnDown (the arrow keys). Under box.controllers.first_person the same.
 *
 * The first person scope adds Jump (Space), Crouch (CtrlLeft) and Fly (F). Under
 * box.controllers.helm: Use (F), Ahead (W), Astern (S), Port (A), Starboard (D), Stop (X),
 * Centre (Space), Look and the turn actions. The actions of a scope stay disabled while no
 * controller uses them, so they only bind in worlds that do.
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
