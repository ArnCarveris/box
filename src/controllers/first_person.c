/**
 * @file controllers/first_person.c
 * @brief The OnUpdate system that walks, jumps, crouches and flies a first person controller.
 */

#include "controllers.h"
#include "../render/render.h"
#include <math.h>

/** Pitch the eye stops at, short of looking straight up or down */
#define BOX_FIRST_PERSON_PITCH_MAX (1.55f)

/** Walking units per second of a constructed controller */
#define BOX_FIRST_PERSON_SPEED (4.0f)

/** Units per second while crouching of a constructed controller */
#define BOX_FIRST_PERSON_CROUCH_SPEED (2.0f)

/** Units per second in free flight of a constructed controller */
#define BOX_FIRST_PERSON_FLY_SPEED (10.0f)

/** Flight speed multiplier while Fast is held of a constructed controller */
#define BOX_FIRST_PERSON_BOOST (4.0f)

/** Upward speed of a jump of a constructed controller */
#define BOX_FIRST_PERSON_JUMP (5.0f)

/** Downward acceleration of a constructed controller */
#define BOX_FIRST_PERSON_GRAVITY (18.0f)

/** Height of the eye above the feet of a constructed controller */
#define BOX_FIRST_PERSON_EYE (1.7f)

/** Height of the eye while crouching of a constructed controller */
#define BOX_FIRST_PERSON_CROUCH_EYE (0.9f)

/** Radians per point of cursor movement of a constructed controller */
#define BOX_FIRST_PERSON_SENSITIVITY (0.003f)

/** Radius of the body of a constructed controller */
#define BOX_FIRST_PERSON_RADIUS (0.3f)

/** Height of the ledges the feet step onto of a constructed controller */
#define BOX_FIRST_PERSON_STEP (0.3f)

/** Height of the top of the body above the eye */
#define BOX_FIRST_PERSON_HEAD (0.15f)

/** Distance above the ground within which falling feet snap onto it */
#define BOX_FIRST_PERSON_SNAP (0.001f)

const box_controller_action_t box_first_person_actions[BoxFirstPersonActionCount] = {
    [BoxFirstPersonForward] = { "Forward", "Walk forward", KeyW },
    [BoxFirstPersonBack] = { "Back", "Walk back", KeyS },
    [BoxFirstPersonLeft] = { "Left", "Walk left", KeyA },
    [BoxFirstPersonRight] = { "Right", "Walk right", KeyD },
    [BoxFirstPersonUp] = { "Up", "Move up in free flight", KeyE },
    [BoxFirstPersonDown] = { "Down", "Move down in free flight", KeyQ },
    [BoxFirstPersonFast] = { "Fast", "Fly faster while held", KeyShiftLeft },
    [BoxFirstPersonLook] = { "Look", "Turn with the cursor while held", KeyMouseRight },
    [BoxFirstPersonJump] = { "Jump", "Jump from the ground", KeySpace },
    [BoxFirstPersonCrouch] = { "Crouch", "Lower the eye and walk slower while held", KeyCtrlLeft },
    [BoxFirstPersonFly] = { "Fly", "Toggle free flight", KeyF }
};

/** What the actions of one scope ask a first person controller for this frame */
typedef struct box_first_person_input_t {
    ecs_entity_t scope;                       /**< Scope of the actions, 0 for none */
    bool held[BoxFirstPersonActionCount];     /**< Which actions are down */
    bool pressed[BoxFirstPersonActionCount];  /**< Which actions went down this frame */
    vec2 look;                                /**< Cursor movement of the Look action */
} box_first_person_input_t;

void box_first_person_controller_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    const ecs_entity_t *scope = ti->hooks.ctx;
    FirstPersonController *values = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        values[i] = (FirstPersonController){
            .speed = BOX_FIRST_PERSON_SPEED,
            .crouch_speed = BOX_FIRST_PERSON_CROUCH_SPEED,
            .fly_speed = BOX_FIRST_PERSON_FLY_SPEED,
            .boost = BOX_FIRST_PERSON_BOOST,
            .jump = BOX_FIRST_PERSON_JUMP,
            .gravity = BOX_FIRST_PERSON_GRAVITY,
            .eye = BOX_FIRST_PERSON_EYE,
            .crouch_eye = BOX_FIRST_PERSON_CROUCH_EYE,
            .radius = BOX_FIRST_PERSON_RADIUS,
            .step = BOX_FIRST_PERSON_STEP,
            .sensitivity = BOX_FIRST_PERSON_SENSITIVITY,
            .actions = *scope
        };
    }
}

/** Read the state of the first person actions of a controller.
 *
 * @param world The world
 * @param map The default action map
 * @param scope Scope of the actions, 0 for none
 * @param out The input, written
 */
static void box_first_person_read(
    ecs_world_t *world,
    const FirstPersonActionMap *map,
    ecs_entity_t scope,
    box_first_person_input_t *out)
{
    int32_t i;
    ecs_os_zeromem(out);
    out->scope = scope;
    for (i = 0; i < BoxFirstPersonActionCount; i ++) {
        ecs_entity_t action = scope == map->scope
            ? map->actions[i]
            : (scope ? ecs_lookup_child(world, scope, box_first_person_actions[i].name) : 0);
        const ActionState *state = action && ecs_is_alive(world, action)
            ? ecs_get(world, action, ActionState)
            : NULL;
        if (!state) {
            continue;
        }

        out->held[i] = state->down;
        out->pressed[i] = state->pressed;
        if (i == BoxFirstPersonLook) {
            out->look[0] = state->delta[0];
            out->look[1] = state->delta[1];
        }
    }
}

/** Turn a controlled entity by the cursor movement of the Look action.
 *
 * @param fp The controller
 * @param in The input of the frame
 * @param r The rotation, written in place
 * @return Whether the rotation changed
 */
static bool box_first_person_turn(
    const FirstPersonController *fp,
    const box_first_person_input_t *in,
    Rotation *r)
{
    float sensitivity = fp->sensitivity;
    float dy = fp->invert_y ? -in->look[1] : in->look[1];
    float yaw = r->y - in->look[0] * sensitivity;
    float pitch = r->x - dy * sensitivity;
    bool result = false;
    if (pitch > BOX_FIRST_PERSON_PITCH_MAX) {
        pitch = BOX_FIRST_PERSON_PITCH_MAX;
    }

    if (pitch < -BOX_FIRST_PERSON_PITCH_MAX) {
        pitch = -BOX_FIRST_PERSON_PITCH_MAX;
    }

    result = yaw != r->y || pitch != r->x;
    r->y = yaw;
    r->x = pitch;

    return result;
}

/** Speed the controller walks or flies at this frame.
 *
 * @param fp The controller
 * @param in The input of the frame
 * @return Units per second
 */
static float box_first_person_speed(
    const FirstPersonController *fp,
    const box_first_person_input_t *in)
{
    float result = fp->speed;
    if (fp->flying) {
        result = fp->fly_speed;
        if (in->held[BoxFirstPersonFast]) {
            result *= fp->boost;
        }
    } else if (fp->crouched) {
        result = fp->crouch_speed;
    }

    return result;
}

/** Move the velocity of a controller towards what the movement actions ask for.
 *
 * Walking eases the XZ velocity only, its Y velocity belongs to the jump and the gravity.
 * Flight eases all three, with Up and Down along the Y axis.
 *
 * @param fp The controller, its velocity is written
 * @param in The input of the frame
 * @param yaw The yaw of the entity
 * @param dt Seconds the frame took
 */
static void box_first_person_accelerate(
    FirstPersonController *fp,
    const box_first_person_input_t *in,
    float yaw,
    float dt)
{
    const bool *held = in->held;
    float forward = (float)(held[BoxFirstPersonForward] - held[BoxFirstPersonBack]);
    float right = (float)(held[BoxFirstPersonRight] - held[BoxFirstPersonLeft]);
    float up = fp->flying ? (float)(held[BoxFirstPersonUp] - held[BoxFirstPersonDown]) : 0;
    float s = sinf(yaw), c = cosf(yaw);
    vec3 dir = { right * c - forward * s, up, -forward * c - right * s };
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    float speed = box_first_person_speed(fp, in);
    float alpha = fp->acceleration > 0 ? 1.0f - expf(-fp->acceleration * dt) : 1.0f;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float target = len > 0 ? dir[i] * speed / len : 0;
        if (i != 1 || fp->flying) {
            fp->velocity[i] += (target - fp->velocity[i]) * alpha;
        }
    }
}

/** Apply the flight toggle, the jump and the gravity of a frame.
 *
 * @param fp The controller, its velocity and state are written
 * @param in The input of the frame
 * @param dt Seconds the frame took
 */
static void box_first_person_state(
    FirstPersonController *fp,
    const box_first_person_input_t *in,
    float dt)
{
    if (in->pressed[BoxFirstPersonFly]) {
        fp->flying = !fp->flying;
        fp->velocity[1] = 0;
    }

    if (fp->flying) {
        fp->grounded = false;
    }

    if (!fp->flying && fp->grounded && in->pressed[BoxFirstPersonJump]) {
        fp->velocity[1] = fp->jump;
        fp->grounded = false;
    }

    if (!fp->flying && !fp->grounded) {
        fp->velocity[1] -= fp->gravity * dt;
    }
}

/** Get the body of a controller for an eye height.
 *
 * @param world The world
 * @param e The controlled entity
 * @param fp The controller
 * @param height Height of the eye above the feet
 * @param body The body, written
 */
static void box_first_person_body(
    ecs_world_t *world,
    ecs_entity_t e,
    const FirstPersonController *fp,
    float height,
    box_controller_body_t *body)
{
    bool walking = fp->grounded && !fp->flying;
    float step = fp->step;
    *body = (box_controller_body_t){
        .entity = e,
        .scene = box_render_scene_of(world, e),
        .radius = fp->radius,
        .height = height + BOX_FIRST_PERSON_HEAD,
        .step = walking ? step : 0,
        .probe = walking && fp->velocity[1] <= 0 ? step : 0
    };
}

/** Crouch while Crouch is held, and stay down while there is no room to stand up.
 *
 * @param world The world
 * @param e The controlled entity
 * @param fp The controller, its crouched flag is written
 * @param in The input of the frame
 * @param p The position of the eye
 */
static void box_first_person_crouch(
    ecs_world_t *world,
    ecs_entity_t e,
    FirstPersonController *fp,
    const box_first_person_input_t *in,
    const Position *p)
{
    bool crouch = !fp->flying && in->held[BoxFirstPersonCrouch];
    float eye = fp->eye;
    if (fp->crouched && !crouch && fp->height > 0 && fp->height < eye) {
        box_controller_body_t body;
        Position feet = { p->x, p->y - fp->height, p->z };
        box_first_person_body(world, e, fp, eye, &body);
        crouch = !box_controller_fits(world, &body, &feet);
    }

    fp->crouched = crouch;
}

/** Move the feet of a controller over the frame and put its eye above them.
 *
 * The ground plane only holds the feet when no shape is below them.
 *
 * @param world The world
 * @param e The controlled entity
 * @param fp The controller, its velocity, height and grounded flag are written
 * @param p The position of the eye, written in place
 * @param dt Seconds the frame took
 * @return Whether the position changed
 */
static bool box_first_person_step(
    ecs_world_t *world,
    ecs_entity_t e,
    FirstPersonController *fp,
    Position *p,
    float dt)
{
    float eye = fp->eye;
    float height = fp->crouched ? fp->crouch_eye : eye;
    float previous = fp->height > 0 ? fp->height : height;
    Position feet = { p->x, p->y - previous, p->z };
    Position eye_pos = { 0 };
    box_controller_body_t body;
    box_controller_hit_t hit;
    bool landed = false;
    bool result = false;
    box_first_person_body(world, e, fp, height, &body);
    box_controller_move(world, &body, &feet, fp->velocity, dt, &hit);
    landed = !hit.grounded && (feet.y < fp->ground ||
        (feet.y <= fp->ground + BOX_FIRST_PERSON_SNAP && fp->velocity[1] <= 0));
    landed = landed && !box_controller_ground_below(world, &body, &feet);
    if (landed) {
        feet.y = fp->ground;
        fp->velocity[1] = fp->velocity[1] < 0 ? 0 : fp->velocity[1];
    }

    fp->grounded = (landed || hit.grounded) && !fp->flying;
    fp->height = height;
    eye_pos.x = feet.x;
    eye_pos.y = feet.y + height;
    eye_pos.z = feet.z;
    result = eye_pos.x != p->x || eye_pos.y != p->y || eye_pos.z != p->z;
    *p = eye_pos;

    return result;
}

void box_first_person_actions_create(
    ecs_world_t *world,
    FirstPersonActionMap *map)
{
    int32_t i;
    map->scope = ecs_entity(world, { .name = "first_person" });
    for (i = 0; i < BoxFirstPersonActionCount; i ++) {
        const box_controller_action_t *a = &box_first_person_actions[i];
        ecs_entity_t action = ecs_entity(world, { .name = a->name, .parent = map->scope });
        ecs_set(world, action, Action, { .description = ECS_CONST_CAST(char*, a->description) });
        ecs_set(world, action, Binding, { .key = a->key });
        ecs_enable(world, action, false);
        map->actions[i] = action;
    }
}

void BoxFirstPersonActionsActivate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FirstPersonActionMap *map = ecs_singleton_get_mut(world, FirstPersonActionMap);
    bool used = false;
    int32_t i;
    while (ecs_iter_next(it)) {
        const FirstPersonController *fp = ecs_field(it, FirstPersonController, 0);
        for (i = 0; i < it->count; i ++) {
            used = used || fp[i].actions == map->scope;
        }
    }

    for (i = 0; used != map->active && i < BoxFirstPersonActionCount; i ++) {
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

void BoxFirstPersonControllerUpdate(
    ecs_iter_t *it)
{
    FirstPersonController *fp = ecs_field(it, FirstPersonController, 0);
    Position *p = ecs_field(it, Position, 1);
    Rotation *r = ecs_field(it, Rotation, 2);
    const FirstPersonActionMap *map = ecs_singleton_get(it->world, FirstPersonActionMap);
    box_first_person_input_t in = { 0 };
    float dt = it->delta_time;
    bool read = false;
    int32_t i;
    for (i = 0; dt > 0 && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!read || in.scope != fp[i].actions) {
            box_first_person_read(it->world, map, fp[i].actions, &in);
            read = true;
        }

        if (box_first_person_turn(&fp[i], &in, &r[i])) {
            ecs_modified(it->world, e, Rotation);
        }

        box_first_person_state(&fp[i], &in, dt);
        box_first_person_crouch(it->world, e, &fp[i], &in, &p[i]);
        box_first_person_accelerate(&fp[i], &in, r[i].y, dt);
        if (box_first_person_step(it->world, e, &fp[i], &p[i], dt)) {
            ecs_modified(it->world, e, Position);
        }
    }
}
