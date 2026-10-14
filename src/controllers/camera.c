/**
 * @file controllers/camera.c
 * @brief The OnUpdate system that flies a camera with the actions of its controller.
 */

#include "controllers.h"
#include "../render/render.h"
#include <math.h>

/** Pitch the camera stops at, short of looking straight up or down */
#define BOX_CAMERA_PITCH_MAX (1.55f)

/** Units per second when the controller does not say */
#define BOX_CAMERA_SPEED (10.0f)

/** Speed multiplier while Fast is held when the controller does not say */
#define BOX_CAMERA_BOOST (4.0f)

/** Radians per point of cursor movement when the controller does not say */
#define BOX_CAMERA_SENSITIVITY (0.003f)

/** Radius of the colliding sphere when the controller does not say */
#define BOX_CAMERA_RADIUS (0.25f)

/** What the actions of one scope ask a camera for this frame */
typedef struct box_camera_input_t {
    ecs_entity_t scope;                 /**< Scope of the actions, 0 for the defaults */
    bool held[BoxCameraActionCount];    /**< Which actions are down */
    vec2 look;                          /**< Cursor movement of the Look action this frame */
} box_camera_input_t;

/** Value of a controller member, which takes its default when it is zero.
 *
 * @param value The member
 * @param fallback The default
 * @return The value to use
 */
static float box_camera_value(
    float value,
    float fallback)
{
    return value != 0 ? value : fallback;
}

/** Read the state of the camera actions of a controller.
 *
 * @param world The world
 * @param map The default action map
 * @param scope Scope of the actions, 0 for the default actions
 * @param out The input, written
 */
static void box_camera_read(
    ecs_world_t *world,
    const CameraActionMap *map,
    ecs_entity_t scope,
    box_camera_input_t *out)
{
    int32_t i;
    ecs_os_zeromem(out);
    out->scope = scope;
    for (i = 0; i < BoxCameraActionCount; i ++) {
        ecs_entity_t action = scope
            ? ecs_lookup_child(world, scope, box_camera_actions[i].name)
            : map->actions[i];
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

/** Turn a camera by the cursor movement of the Look action.
 *
 * @param cc The controller
 * @param in The input of the frame
 * @param r The rotation, written in place
 * @return Whether the rotation changed
 */
static bool box_camera_turn(
    const CameraController *cc,
    const box_camera_input_t *in,
    Rotation *r)
{
    float sensitivity = box_camera_value(cc->sensitivity, BOX_CAMERA_SENSITIVITY);
    float dy = cc->invert_y ? -in->look[1] : in->look[1];
    float yaw = r->y - in->look[0] * sensitivity;
    float pitch = r->x - dy * sensitivity;
    bool result = false;
    if (pitch > BOX_CAMERA_PITCH_MAX) {
        pitch = BOX_CAMERA_PITCH_MAX;
    }

    if (pitch < -BOX_CAMERA_PITCH_MAX) {
        pitch = -BOX_CAMERA_PITCH_MAX;
    }

    result = yaw != r->y || pitch != r->x;
    r->y = yaw;
    r->x = pitch;

    return result;
}

/** Move the velocity of a controller towards what the movement actions ask for.
 *
 * Forward and right lie in the XZ plane along the yaw; up is the Y axis.
 *
 * @param cc The controller, its velocity is written
 * @param in The input of the frame
 * @param yaw The yaw of the camera
 * @param dt Seconds the frame took
 */
static void box_camera_accelerate(
    CameraController *cc,
    const box_camera_input_t *in,
    float yaw,
    float dt)
{
    const bool *held = in->held;
    float forward = (float)(held[BoxCameraForward] - held[BoxCameraBack]);
    float right = (float)(held[BoxCameraRight] - held[BoxCameraLeft]);
    float up = (float)(held[BoxCameraUp] - held[BoxCameraDown]);
    float s = sinf(yaw), c = cosf(yaw);
    vec3 dir = { right * c - forward * s, up, -forward * c - right * s };
    vec3 target = { 0, 0, 0 };
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    float speed = box_camera_value(cc->speed, BOX_CAMERA_SPEED);
    float alpha = cc->acceleration > 0 ? 1.0f - expf(-cc->acceleration * dt) : 1.0f;
    int32_t i;
    if (held[BoxCameraFast]) {
        speed *= box_camera_value(cc->boost, BOX_CAMERA_BOOST);
    }

    for (i = 0; i < 3; i ++) {
        target[i] = len > 0 ? dir[i] * speed / len : 0;
        cc->velocity[i] += (target[i] - cc->velocity[i]) * alpha;
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
    float radius = cc->collide ? box_camera_value(cc->radius, BOX_CAMERA_RADIUS) : 0;
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
            used = used || !cc[i].actions;
        }
    }

    for (i = 0; used != map->active && i < BoxCameraActionCount; i ++) {
        ecs_entity_t action = map->actions[i];
        ActionState *state = ecs_is_alive(world, action)
            ? ecs_get_mut(world, action, ActionState)
            : NULL;
        if (state) {
            ecs_os_zeromem(state);
            ecs_enable(world, action, used);
        }
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
    float dt = it->delta_time;
    bool read = false;
    int32_t i;
    for (i = 0; dt > 0 && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!read || in.scope != cc[i].actions) {
            box_camera_read(it->world, map, cc[i].actions, &in);
            read = true;
        }

        if (box_camera_turn(&cc[i], &in, &r[i])) {
            ecs_modified(it->world, e, Rotation);
        }

        box_camera_accelerate(&cc[i], &in, r[i].y, dt);
        if (box_camera_move(it->world, e, &cc[i], &p[i], dt)) {
            ecs_modified(it->world, e, Position);
        }
    }
}
