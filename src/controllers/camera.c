/**
 * @file controllers/camera.c
 * @brief The OnUpdate system that flies a camera with the actions of its controller.
 */

#include "controllers.h"
#include "../render/render.h"
#include <math.h>

/** Units per second of a constructed controller */
#define BOX_CAMERA_SPEED (40.0f)

/** Speed multiplier while Fast is held of a constructed controller */
#define BOX_CAMERA_BOOST (5.0f)

/** Rate the velocity follows a held movement of a constructed controller */
#define BOX_CAMERA_ACCELERATION (6.0f)

/** Rate the velocity follows a stop of a constructed controller */
#define BOX_CAMERA_DECELERATION (18.0f)

/** Radians per second of the turn actions of a constructed controller */
#define BOX_CAMERA_TURN_SPEED (1.5f)

/** Rate the turn follows a held turn action of a constructed controller */
#define BOX_CAMERA_TURN_ACCELERATION (5.0f)

/** Rate the turn follows a stop of a constructed controller */
#define BOX_CAMERA_TURN_DECELERATION (15.0f)

/** Yaw rate multiplier while Fast is held of a constructed controller */
#define BOX_CAMERA_BOOST_YAW (0.33f)

/** Radians per point of cursor movement of a constructed controller */
#define BOX_CAMERA_SENSITIVITY (0.003f)

/** Units per second a wheel notch adds along the view of a constructed controller */
#define BOX_CAMERA_ZOOM (3.0f)

/** Radius of the colliding sphere of a constructed controller */
#define BOX_CAMERA_RADIUS (0.25f)

/** What the actions of one scope ask a camera for this frame */
typedef struct box_camera_input_t {
    ecs_entity_t scope;                 /**< Scope of the actions, 0 for none */
    bool held[BoxCameraActionCount];    /**< Which actions are down */
    vec2 look;                          /**< Cursor movement of the Look action this frame */
    float wheel;                        /**< Vertical wheel offset of the frame */
} box_camera_input_t;

void box_camera_controller_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    const ecs_entity_t *scope = ti->hooks.ctx;
    CameraController *values = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        values[i] = (CameraController){
            .speed = BOX_CAMERA_SPEED,
            .boost = BOX_CAMERA_BOOST,
            .acceleration = BOX_CAMERA_ACCELERATION,
            .deceleration = BOX_CAMERA_DECELERATION,
            .turn_speed = BOX_CAMERA_TURN_SPEED,
            .turn_acceleration = BOX_CAMERA_TURN_ACCELERATION,
            .turn_deceleration = BOX_CAMERA_TURN_DECELERATION,
            .boost_yaw = BOX_CAMERA_BOOST_YAW,
            .sensitivity = BOX_CAMERA_SENSITIVITY,
            .zoom = BOX_CAMERA_ZOOM,
            .actions = *scope,
            .radius = BOX_CAMERA_RADIUS
        };
    }
}

/** Read the state of the camera actions of a controller and the wheel of the frame.
 *
 * @param world The world
 * @param map The default action map
 * @param scope Scope of the actions, 0 for none
 * @param out The input, written
 */
static void box_camera_read(
    ecs_world_t *world,
    const CameraActionMap *map,
    ecs_entity_t scope,
    box_camera_input_t *out)
{
    int32_t i;
    const Input *input = ecs_singleton_get(world, Input);
    ecs_os_zeromem(out);
    out->scope = scope;
    out->wheel = input ? input->mouse.scroll[1] : 0;
    for (i = 0; i < BoxCameraActionCount; i ++) {
        ecs_entity_t action = scope == map->scope
            ? map->actions[i]
            : (scope ? ecs_lookup_child(world, scope, box_camera_actions[i].name) : 0);
        const ActionState *state = action && ecs_is_alive(world, action)
            ? ecs_get(world, action, ActionState)
            : NULL;
        if (!state) {
            continue;
        }

        out->held[i] = state->down;
        if (i == BoxCameraLook) {
            out->look[0] = state->delta[0];
            out->look[1] = state->delta[1];
        }
    }
}

/** Turn a camera with the turn actions and the cursor movement of the Look action.
 *
 * @param cc The controller, its angular velocity is written
 * @param in The input of the frame
 * @param r The rotation, written in place
 * @param dt Seconds the frame took
 * @return Whether the rotation changed
 */
static bool box_camera_turn(
    CameraController *cc,
    const box_camera_input_t *in,
    Rotation *r,
    float dt)
{
    const bool *held = in->held;
    box_controller_turn_t turn = {
        .speed = cc->turn_speed,
        .acceleration = cc->turn_acceleration,
        .deceleration = cc->turn_deceleration,
        .sensitivity = cc->sensitivity,
        .yaw_scale = held[BoxCameraFast] ? cc->boost_yaw : 1.0f,
        .invert_y = cc->invert_y,
        .yaw = (float)(held[BoxCameraTurnLeft] - held[BoxCameraTurnRight]),
        .pitch = (float)(held[BoxCameraTurnUp] - held[BoxCameraTurnDown]),
        .look = held[BoxCameraLook],
        .delta = { in->look[0], in->look[1] }
    };

    return box_controller_turn(&turn, cc->angular_velocity, r, dt);
}

/** Move the velocity of a controller towards what the movement actions ask for.
 *
 * Forward and right lie in the XZ plane along the yaw; up is the Y axis. The wheel then pushes
 * the velocity along the view.
 *
 * @param cc The controller, its velocity is written
 * @param in The input of the frame
 * @param r The rotation of the camera
 * @param dt Seconds the frame took
 */
static void box_camera_accelerate(
    CameraController *cc,
    const box_camera_input_t *in,
    const Rotation *r,
    float dt)
{
    const bool *held = in->held;
    float forward = (float)(held[BoxCameraForward] - held[BoxCameraBack]);
    float right = (float)(held[BoxCameraRight] - held[BoxCameraLeft]);
    float up = (float)(held[BoxCameraUp] - held[BoxCameraDown]);
    float s = sinf(r->y), c = cosf(r->y);
    float sp = sinf(r->x), cp = cosf(r->x);
    vec3 dir = { right * c - forward * s, up, -forward * c - right * s };
    vec3 view = { -s * cp, sp, -c * cp };
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    float speed = cc->speed;
    float rate = len > 0 ? cc->acceleration : cc->deceleration;
    float alpha = 1.0f - expf(-rate * dt);
    float push = in->wheel * cc->zoom;
    int32_t i;
    if (held[BoxCameraFast]) {
        speed *= cc->boost;
    }

    for (i = 0; i < 3; i ++) {
        float target = len > 0 ? dir[i] * speed / len : 0;
        cc->velocity[i] += (target - cc->velocity[i]) * alpha;
        cc->velocity[i] += view[i] * push;
    }
}

/** Move a camera by its velocity, as a sphere around the eye when it collides.
 *
 * @param world The world
 * @param e The camera
 * @param cc The controller, its velocity is written when the move is blocked
 * @param p The position, written in place
 * @param dt Seconds the frame took
 * @return Whether the position changed
 */
static bool box_camera_move(
    ecs_world_t *world,
    ecs_entity_t e,
    CameraController *cc,
    Position *p,
    float dt)
{
    float radius = cc->collide ? cc->radius : 0;
    box_controller_body_t body = {
        .entity = e,
        .scene = cc->collide ? box_render_scene_of(world, e) : 0,
        .radius = radius,
        .height = radius * 2
    };
    Position feet = { p->x, p->y - radius, p->z };
    bool result = box_controller_move(world, &body, &feet, cc->velocity, dt, NULL);
    p->x = feet.x;
    p->y = feet.y + radius;
    p->z = feet.z;

    return result;
}

void BoxCameraActionsActivate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    CameraActionMap *map = ecs_singleton_get_mut(world, CameraActionMap);
    bool used = false;
    int32_t i;
    while (ecs_iter_next(it)) {
        const CameraController *cc = ecs_field(it, CameraController, 0);
        for (i = 0; i < it->count; i ++) {
            used = used || cc[i].actions == map->scope;
        }
    }

    if (used != map->active) {
        box_controller_actions_enable(world, map->actions, map->alts, BoxCameraActionCount, used);
    }

    map->active = used;
}

void BoxCameraControllerUpdate(
    ecs_iter_t *it)
{
    CameraController *cc = ecs_field(it, CameraController, 0);
    Position *p = ecs_field(it, Position, 1);
    Rotation *r = ecs_field(it, Rotation, 2);
    const CameraActionMap *map = ecs_singleton_get(it->world, CameraActionMap);
    box_camera_input_t in = { 0 };
    float dt = fminf(it->delta_time, BOX_CONTROLLER_DT_MAX);
    bool read = false;
    int32_t i;
    for (i = 0; dt > 0 && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!read || in.scope != cc[i].actions) {
            box_camera_read(it->world, map, cc[i].actions, &in);
            read = true;
        }

        if (box_camera_turn(&cc[i], &in, &r[i], dt)) {
            ecs_modified(it->world, e, Rotation);
        }

        box_camera_accelerate(&cc[i], &in, &r[i], dt);
        if (box_camera_move(it->world, e, &cc[i], &p[i], dt)) {
            ecs_modified(it->world, e, Position);
        }
    }
}
