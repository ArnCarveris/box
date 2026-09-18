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

/** Height the character stands at, which is the ground plus its eye height.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param fp The controller
 * @return The height of the eye when the character stands on the ground
 */
static float box_person_stand(
    ecs_world_t *world,
    const ControllerActions *actions,
    const FirstPersonController *fp)
{
    const AABB *bounds = fp->terrain && ecs_is_alive(world, fp->terrain)
        ? ecs_get(world, fp->terrain, AABB)
        : NULL;
    float ground = bounds ? bounds->max.y : fp->ground;
    float eye = box_controller_value(fp->eye, BOX_PERSON_EYE);
    if (box_controller_held(world, actions->crouch)) {
        eye = box_controller_value(fp->crouch, BOX_PERSON_CROUCH);
    }

    return ground + eye;
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

/** Apply gravity, the jump and the ground to a walking character.
 *
 * @param world The world
 * @param actions The actions of the module
 * @param fp The controller, its velocity and grounded flag are written
 * @param p The position, written
 * @param dt Seconds the frame took
 */
static void box_person_ground(
    ecs_world_t *world,
    const ControllerActions *actions,
    FirstPersonController *fp,
    Position *p,
    float dt)
{
    const InputAction *jump = actions->jump
        ? ecs_get(world, actions->jump, InputAction)
        : NULL;
    if (fp->grounded && jump && jump->pressed) {
        fp->velocity.y = box_controller_value(fp->jump, BOX_PERSON_JUMP);
        fp->grounded = false;
    }

    float stand = box_person_stand(world, actions, fp);
    if (fp->grounded) {
        p->y = stand;
        fp->velocity.y = 0;
    } else {
        fp->velocity.y -=
            box_controller_value(fp->gravity, BOX_PERSON_GRAVITY) * dt;
        p->y += fp->velocity.y * dt;
        if (p->y <= stand) {
            p->y = stand;
            fp->velocity.y = 0;
            fp->grounded = true;
        }
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
            p[i].y += fp[i].velocity.y * dt;
        } else {
            box_person_walk(world, actions, &fp[i], dt);
            box_person_ground(world, actions, &fp[i], &p[i], dt);
        }

        p[i].x += fp[i].velocity.x * dt;
        p[i].z += fp[i].velocity.z * dt;
        box_controller_pose(
            &p[i], &o[i], look ? &look[i] : NULL, fp[i].yaw, fp[i].pitch);
    }
}
