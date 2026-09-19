/**
 * @file controller/person.c
 * @brief The OnUpdate system that walks, jumps and flies a first person character.
 */

#include "controller_internal.h"
#include <math.h>

/** Units per second the character walks at when the controller does not say */
#define BOX_PERSON_WALK (4.0f)

/** Units per second the character runs at when the controller does not say */
#define BOX_PERSON_RUN (8.0f)

/** Upward speed of a jump when the controller does not say */
#define BOX_PERSON_JUMP (5.0f)

/** Downward acceleration when the controller does not say */
#define BOX_PERSON_GRAVITY (18.0f)

/** Height of the eye above the ground when the controller does not say */
#define BOX_PERSON_EYE (1.7f)

/** Height of the eye while crouching when the controller does not say */
#define BOX_PERSON_CROUCH (0.9f)

/** How fast the velocity follows the input when the controller does not say */
#define BOX_PERSON_ACCEL (12.0f)

/** Radius of the capsule when the controller does not say */
#define BOX_PERSON_RADIUS (0.35f)

/** Highest ledge the character steps onto when the controller does not say */
#define BOX_PERSON_STEP (0.45f)

/** How far below its foot the character looks for the ground */
#define BOX_PERSON_PROBE (0.15f)

/** Height of the eye of a character, which crouching lowers.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param fp The controller
 * @return The height of the eye above the foot
 */
static float box_person_eye(
    ecs_world_t *world,
    const ControllerActions *actions,
    const FirstPersonController *fp)
{
    float eye = box_controller_value(fp->eye, BOX_PERSON_EYE);
    if (box_controller_held(world, actions->crouch)) {
        eye = box_controller_value(fp->crouch, BOX_PERSON_CROUCH);
    }

    return eye;
}

/** Height of the floor the character falls back on without scene geometry.
 *
 * @param world The world
 * @param fp The controller
 * @return The height the foot of the character rests at
 */
static float box_person_floor(
    ecs_world_t *world,
    const FirstPersonController *fp)
{
    const AABB *bounds = fp->terrain && ecs_is_alive(world, fp->terrain)
        ? ecs_get(world, fp->terrain, AABB)
        : NULL;

    return bounds ? bounds->max.y : fp->ground;
}

/** Toggle free flight when the Fly action was pressed this frame.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param fp The controller, its flying flag is written
 */
static void box_person_toggle(
    ecs_world_t *world,
    const ControllerActions *actions,
    FirstPersonController *fp)
{
    const InputAction *fly = actions->fly
        ? ecs_get(world, actions->fly, InputAction)
        : NULL;
    if (fly && fly->pressed) {
        fp->flying = !fp->flying;
        if (fp->flying) {
            fp->grounded = false;
        }
    }
}

/** Move the velocity of a walking character towards what its actions ask for.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param fp The controller, its velocity is written
 * @param dt Seconds the frame took
 */
static void box_person_walk(
    ecs_world_t *world,
    const ControllerActions *actions,
    FirstPersonController *fp,
    float dt)
{
    vec3 dir = box_controller_direction(world, actions, fp->yaw);
    dir.y = 0;

    float speed = box_controller_value(fp->walk, BOX_PERSON_WALK);
    if (box_controller_held(world, actions->run)) {
        speed = box_controller_value(fp->run, BOX_PERSON_RUN);
    }

    float length = vec3_length(dir);
    vec3 target = vec3_new(0, fp->velocity.y, 0);
    if (length > 0) {
        target.x = dir.x * speed / length;
        target.z = dir.z * speed / length;
    }

    box_controller_ease(&fp->velocity, target,
        box_controller_value(fp->acceleration, BOX_PERSON_ACCEL), dt);
}

/** Apply the jump of the frame and the gravity of a character in the air.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param fp The controller, its velocity and grounded flag are written
 * @param dt Seconds the frame took
 */
static void box_person_gravity(
    ecs_world_t *world,
    const ControllerActions *actions,
    FirstPersonController *fp,
    float dt)
{
    const InputAction *jump = actions->jump
        ? ecs_get(world, actions->jump, InputAction)
        : NULL;
    if (fp->grounded && jump && jump->pressed) {
        fp->velocity.y = box_controller_value(fp->jump, BOX_PERSON_JUMP);
        fp->grounded = false;
    }

    if (!fp->grounded) {
        fp->velocity.y -=
            box_controller_value(fp->gravity, BOX_PERSON_GRAVITY) * dt;
    }
}

/** Move a flying character, which ignores the ground and the gravity.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param fp The controller, its velocity is written
 * @param dt Seconds the frame took
 */
static void box_person_fly(
    ecs_world_t *world,
    const ControllerActions *actions,
    FirstPersonController *fp,
    float dt)
{
    vec3 dir = box_controller_direction(world, actions, fp->yaw);
    float speed = box_controller_value(fp->run, BOX_PERSON_RUN);
    float length = vec3_length(dir);
    vec3 target = vec3_new(0, 0, 0);
    if (length > 0) {
        target = vec3_scale(dir, speed / length);
    }

    box_controller_ease(&fp->velocity, target,
        box_controller_value(fp->acceleration, BOX_PERSON_ACCEL), dt);
}

/** Move a character through the scene and put its eye where it ended up.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param fp The controller, its grounded flag and fall are written
 * @param e The entity of the character
 * @param p The position of the eye, written
 * @param dt Seconds the frame took
 */
static void box_person_step(
    ecs_world_t *world,
    const ControllerActions *actions,
    FirstPersonController *fp,
    ecs_entity_t e,
    Position *p,
    float dt)
{
    float eye = box_person_eye(world, actions, fp);
    float step = box_controller_value(fp->step, BOX_PERSON_STEP);
    bool stood = fp->grounded && !fp->flying;
    vec3 foot = vec3_new(p->x, stood ? fp->foot : p->y - eye, p->z);
    box_controller_body_t body = {
        .entity = e,
        .radius = box_controller_value(fp->radius, BOX_PERSON_RADIUS),
        .height = eye,
        .step = fp->flying ? 0 : step,
        .probe = fp->flying ? 0 : (stood ? step : BOX_PERSON_PROBE),
        .enabled = !fp->no_collision
    };

    bool grounded = box_controller_move(world, &body, &foot, &fp->velocity, dt);
    if (!fp->flying) {
        float floor = box_person_floor(world, fp);
        bool near = stood && foot.y - floor <= BOX_PERSON_PROBE;
        if (foot.y < floor || (!grounded && near)) {
            foot.y = floor;
            grounded = true;
        }

        if (grounded && fp->velocity.y < 0) {
            fp->velocity.y = 0;
        }

        fp->grounded = grounded;
    }

    fp->foot = foot.y;
    p->x = foot.x;
    p->y = foot.y + eye;
    p->z = foot.z;
}

void BoxFirstPersonUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FirstPersonController *fp = ecs_field(it, FirstPersonController, 0);
    Position *p = ecs_field(it, Position, 1);
    Orientation *o = ecs_field(it, Orientation, 2);
    LookAt *look = ecs_field(it, LookAt, 3);
    const ControllerActions *actions = ecs_singleton_get(world, ControllerActions);
    float dt = it->delta_time;
    int32_t i;
    for (i = 0; dt > 0 && i < it->count; i ++) {
        box_controller_look(world, actions,
            box_controller_value(fp[i].sensitivity, BOX_CONTROLLER_SENSITIVITY),
            &fp[i].yaw, &fp[i].pitch);
        box_person_toggle(world, actions, &fp[i]);
        if (fp[i].flying) {
            box_person_fly(world, actions, &fp[i], dt);
        } else {
            box_person_walk(world, actions, &fp[i], dt);
            box_person_gravity(world, actions, &fp[i], dt);
        }

        box_person_step(
            world, actions, &fp[i], it->entities[i], &p[i], dt);
        box_controller_pose(
            &p[i], &o[i], look ? &look[i] : NULL, fp[i].yaw, fp[i].pitch);
    }
}
