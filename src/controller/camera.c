/**
 * @file controller/camera.c
 * @brief The OnUpdate system that flies a camera with the movement actions.
 */

#include "controller_internal.h"
#include <math.h>

/** Units per second the camera moves at when the controller does not say */
#define BOX_CAMERA_SPEED (40.0f)

/** Speed multiplier of the Fast action when the controller does not say */
#define BOX_CAMERA_FAST (5.0f)

/** How fast the velocity follows a held action when the controller does not say */
#define BOX_CAMERA_ACCEL (6.0f)

/** How fast the velocity follows the stop when the controller does not say */
#define BOX_CAMERA_DECEL (18.0f)

/** Radians per second the turn actions reach when the controller does not say */
#define BOX_CAMERA_ANGULAR_SPEED (1.5f)

/** How fast a turn follows a held action when the controller does not say */
#define BOX_CAMERA_ANGULAR_ACCEL (5.0f)

/** How fast a turn follows its stop when the controller does not say */
#define BOX_CAMERA_ANGULAR_DECEL (15.0f)

/** Fraction of the angular speed the yaw keeps while sprinting */
#define BOX_CAMERA_YAW_SPRINT_SCALE (1.0f / 3.0f)

/** Units per second of push the wheel gives the camera along the view */
#define BOX_CAMERA_ZOOM_SPEED (3.0f)

/** Radius the camera keeps free of geometry when the controller does not say */
#define BOX_CAMERA_RADIUS (0.5f)

/** Aim a controller the way the Position and LookAt or the Rotation of its
 * camera already look, so a placed camera keeps facing that way.
 *
 * @param world The world
 * @param e The entity of the camera
 * @param cc The controller, its yaw and pitch are written
 */
static void box_camera_aim(
    ecs_world_t *world,
    ecs_entity_t e,
    CameraController *cc)
{
    const Position *p = ecs_get(world, e, Position);
    const LookAt *look = ecs_get(world, e, LookAt);
    const Rotation *rotation = ecs_get(world, e, Rotation);
    if (p && look) {
        vec3 to = vec3_new(look->x, look->y, look->z);
        vec3 from = vec3_new(p->x, p->y, p->z);
        vec3 dir = vec3_sub(to, from);
        float len = vec3_length(dir);
        if (len <= 0.0f) {
            goto done;
        }

        dir = vec3_scale(dir, 1.0f / len);
        cc->yaw = atan2f(dir.x, dir.z);
        cc->pitch = asinf(-dir.y);
    } else if (rotation) {
        cc->yaw = rotation->y;
        cc->pitch = rotation->x;
    } else {
        goto done;
    }

    if (cc->pitch > BOX_CONTROLLER_PITCH_MAX) {
        cc->pitch = BOX_CONTROLLER_PITCH_MAX;
    }

    if (cc->pitch < -BOX_CONTROLLER_PITCH_MAX) {
        cc->pitch = -BOX_CONTROLLER_PITCH_MAX;
    }

done:
    return;
}

void BoxCameraControllerOnAdd(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    CameraController *cc = ecs_field(it, CameraController, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (cc[i].yaw == 0.0f && cc[i].pitch == 0.0f) {
            box_camera_aim(world, it->entities[i], &cc[i]);
        }
    }
}

void BoxCameraControllerOnPlace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    CameraController *cc = ecs_field(it, CameraController, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_camera_aim(world, it->entities[i], &cc[i]);
    }
}

/** Collect the turn the turn actions ask for, as an angular velocity.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param cc The controller
 * @param yaw_scale Fraction of the angular speed the yaw keeps
 * @return The target angular velocity, x pitch and y yaw
 */
static vec2 box_camera_turn_target(
    ecs_world_t *world,
    const ControllerActions *actions,
    const CameraController *cc,
    float yaw_scale)
{
    vec2 result = { 0, 0 };
    float speed = box_controller_value(
        cc->angular_speed, BOX_CAMERA_ANGULAR_SPEED);
    if (box_controller_held(world, actions->turn_right)) {
        result.y += 1.0f;
    }

    if (box_controller_held(world, actions->turn_left)) {
        result.y -= 1.0f;
    }

    if (box_controller_held(world, actions->turn_up)) {
        result.x -= 1.0f;
    }

    if (box_controller_held(world, actions->turn_down)) {
        result.x += 1.0f;
    }

    float length = sqrtf(result.x * result.x + result.y * result.y);
    if (length > 0) {
        result.x *= speed / length;
        result.y *= speed * yaw_scale / length;
    }

    return result;
}

/** Turn a camera towards what the turn actions or the cursor ask for.
 *
 * The angular velocity chases the turn actions at the angular acceleration
 * while one is held and coasts to a stop at the angular deceleration once
 * none is, which is what keeps a released turn from snapping still. While the
 * Look action is held the turn is set straight from the cursor delta of the
 * frame instead.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param cc The controller, its yaw, pitch and angular velocity are written
 * @param sprint Whether the Fast action is held
 * @param dt Seconds the frame took
 */
static void box_camera_turn(
    ecs_world_t *world,
    const ControllerActions *actions,
    CameraController *cc,
    bool sprint,
    float dt)
{
    float yaw_scale = sprint ? BOX_CAMERA_YAW_SPRINT_SCALE : 1.0f;
    vec2 target = box_camera_turn_target(world, actions, cc, yaw_scale);
    bool turning = target.x != 0.0f || target.y != 0.0f;
    float rate = turning
        ? box_controller_value(
            cc->angular_acceleration, BOX_CAMERA_ANGULAR_ACCEL)
        : box_controller_value(
            cc->angular_deceleration, BOX_CAMERA_ANGULAR_DECEL);
    float alpha = 1.0f - expf(-rate * dt);

    cc->angular_velocity.x += (target.x - cc->angular_velocity.x) * alpha;
    cc->angular_velocity.y += (target.y - cc->angular_velocity.y) * alpha;
    if (box_controller_looking(world, actions)) {
        float sensitivity = box_controller_value(
            cc->sensitivity, BOX_CONTROLLER_SENSITIVITY);
        const Input *in = ecs_singleton_get(world, Input);
        cc->angular_velocity.y = in
            ? in->mouse.delta.x * sensitivity * yaw_scale / dt : 0.0f;
        cc->angular_velocity.x = in
            ? in->mouse.delta.y * sensitivity / dt : 0.0f;
    }

    cc->yaw += cc->angular_velocity.y * dt;
    cc->pitch += cc->angular_velocity.x * dt;
    if (cc->pitch > BOX_CONTROLLER_PITCH_MAX) {
        cc->pitch = BOX_CONTROLLER_PITCH_MAX;
        if (cc->angular_velocity.x > 0) {
            cc->angular_velocity.x = 0;
        }
    }

    if (cc->pitch < -BOX_CONTROLLER_PITCH_MAX) {
        cc->pitch = -BOX_CONTROLLER_PITCH_MAX;
        if (cc->angular_velocity.x < 0) {
            cc->angular_velocity.x = 0;
        }
    }
}

/** Move the velocity of a controller towards what its actions ask for.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param cc The controller, its velocity is written
 * @param sprint Whether the Fast action is held
 * @param dt Seconds the frame took
 */
static void box_camera_move(
    ecs_world_t *world,
    const ControllerActions *actions,
    CameraController *cc,
    bool sprint,
    float dt)
{
    vec3 dir = box_controller_direction(world, actions, cc->yaw);
    float length = vec3_length(dir);
    float speed = box_controller_value(cc->speed, BOX_CAMERA_SPEED);
    if (sprint) {
        speed *= box_controller_value(cc->fast, BOX_CAMERA_FAST);
    }

    vec3 target = vec3_new(0, 0, 0);
    if (length > 0) {
        target = vec3_scale(dir, speed / length);
    }

    float accel = box_controller_value(cc->acceleration, BOX_CAMERA_ACCEL);
    float decel = box_controller_value(cc->deceleration, BOX_CAMERA_DECEL);
    float rate = length > 0 ? accel : decel;

    box_controller_ease(&cc->velocity, target, rate, dt);
}

/** Push the velocity of a camera along the view with the wheel of the frame.
 *
 * @param world The world
 * @param cc The controller, its velocity is written
 */
static void box_camera_wheel(
    ecs_world_t *world,
    CameraController *cc)
{
    const Input *in = ecs_singleton_get(world, Input);
    float scroll = in ? in->mouse.scroll_delta.y : 0.0f;
    if (scroll == 0.0f) {
        goto done;
    }

    vec3 forward = box_controller_forward(cc->yaw, cc->pitch);
    float impulse = scroll * BOX_CAMERA_ZOOM_SPEED;
    cc->velocity = vec3_add(cc->velocity, vec3_scale(forward, impulse));

done:
    return;
}

/** Move a camera through the scene, sliding along the geometry it runs into.
 *
 * @param world The world
 * @param cc The controller, its fall is stopped by a ceiling
 * @param e The entity of the camera
 * @param p The position, written
 * @param dt Seconds the frame took
 */
static void box_camera_travel(
    ecs_world_t *world,
    CameraController *cc,
    ecs_entity_t e,
    Position *p,
    float dt)
{
    float radius = box_controller_value(cc->radius, BOX_CAMERA_RADIUS);
    box_controller_body_t body = {
        .entity = e,
        .radius = radius,
        .height = radius * 2.0f,
        .enabled = !cc->no_collision
    };

    vec3 foot = vec3_new(p->x, p->y - radius, p->z);
    box_controller_move(world, &body, &foot, &cc->velocity, dt);
    p->x = foot.x;
    p->y = foot.y + radius;
    p->z = foot.z;
}

void BoxCameraControllerUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    CameraController *cc = ecs_field(it, CameraController, 0);
    Position *p = ecs_field(it, Position, 1);
    Orientation *o = ecs_field(it, Orientation, 2);
    LookAt *look = ecs_field(it, LookAt, 3);
    const ControllerActions *actions = ecs_singleton_get(world, ControllerActions);
    float dt = it->delta_time;
    int32_t i;
    for (i = 0; dt > 0 && i < it->count; i ++) {
        bool sprint = box_controller_held(world, actions->fast);

        box_camera_turn(world, actions, &cc[i], sprint, dt);
        box_camera_move(world, actions, &cc[i], sprint, dt);
        box_camera_wheel(world, &cc[i]);
        box_camera_travel(world, &cc[i], it->entities[i], &p[i], dt);
        box_controller_clearance(
            world, cc[i].terrain, cc[i].clearance, &p[i], &cc[i].velocity);
        box_controller_pose(
            &p[i], &o[i], look ? &look[i] : NULL, cc[i].yaw, cc[i].pitch);
    }
}
