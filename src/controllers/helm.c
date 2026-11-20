/**
 * @file controllers/helm.c
 * @brief The helm of a vessel: its actions, taking and letting go of it, the throttle and the
 * rudder, the wheel and the lever, and the pilot kept at the stand.
 */

#include "controllers.h"
#include <math.h>

ECS_COMPONENT_DECLARE(HelmActionMap);

/** Turn of the wheel in radians at full rudder, about three quarters of a turn */
#define BOX_HELM_WHEEL_TURN (2.6f)

/** Tilt of the lever in radians at full throttle */
#define BOX_HELM_LEVER_TURN (0.6f)

/** Lowest throttle, full astern */
#define BOX_HELM_ASTERN (-0.5f)

const box_controller_action_t box_helm_actions[BoxHelmActionCount] = {
    [BoxHelmUse] = { "Use", "Take the helm near its stand, or let go of it", KeyF, KeyNone },
    [BoxHelmAhead] = { "Ahead", "Move the throttle ahead", KeyW, KeyNone },
    [BoxHelmAstern] = { "Astern", "Move the throttle astern", KeyS, KeyNone },
    [BoxHelmPort] = { "Port", "Turn the rudder to port", KeyA, KeyNone },
    [BoxHelmStarboard] = { "Starboard", "Turn the rudder to starboard", KeyD, KeyNone },
    [BoxHelmStop] = { "Stop", "Stop the engine", KeyX, KeyNone },
    [BoxHelmCentre] = { "Centre", "Centre the rudder", KeySpace, KeyNone },
    [BoxHelmLook] = { "Look", "Look around with the cursor", KeyMouseRight, KeyNone },
    [BoxHelmTurnLeft] = { "TurnLeft", "Look left", KeyLeft, KeyNone },
    [BoxHelmTurnRight] = { "TurnRight", "Look right", KeyRight, KeyNone },
    [BoxHelmTurnUp] = { "TurnUp", "Look up", KeyUp, KeyNone },
    [BoxHelmTurnDown] = { "TurnDown", "Look down", KeyDown, KeyNone }
};

void BoxHelmActionsActivate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    HelmActionMap *map = ecs_singleton_get_mut(world, HelmActionMap);
    bool used = false;
    while (ecs_iter_next(it)) {
        const Vessel *v = ecs_field(it, Vessel, 0);
        for (int32_t i = 0; i < it->count; i ++) {
            used = used || v[i].actions == map->scope;
        }
    }

    if (used != map->active) {
        box_controller_actions_enable(world, map->actions, map->alts, BoxHelmActionCount, used);
    }

    map->active = used;
}

void box_helm_read(
    ecs_world_t *world,
    ecs_entity_t scope,
    box_helm_input_t *out)
{
    const HelmActionMap *map = ecs_singleton_get(world, HelmActionMap);
    ecs_os_zeromem(out);
    for (int32_t i = 0; i < BoxHelmActionCount; i ++) {
        ecs_entity_t action = map && scope == map->scope
            ? map->actions[i]
            : (scope ? ecs_lookup_child(world, scope, box_helm_actions[i].name) : 0);
        const ActionState *state = action && ecs_is_alive(world, action)
            ? ecs_get(world, action, ActionState)
            : NULL;
        if (state) {
            out->held[i] = state->down;
            out->pressed[i] = state->pressed;
        }
    }
}

void box_helm_steer(
    const Vessel *vessel,
    VesselStatus *status,
    const box_helm_input_t *in,
    float dt)
{
    float throttle = status->throttle;
    if (in->held[BoxHelmAhead]) {
        throttle = fminf(1, throttle + vessel->throttle_rate * dt);
    }

    if (in->held[BoxHelmAstern]) {
        throttle = fmaxf(BOX_HELM_ASTERN, throttle - vessel->throttle_rate * dt);
    }

    if (in->pressed[BoxHelmStop] || in->held[BoxHelmStop]) {
        throttle = 0;
    }

    float rudder = status->rudder;
    float steer = (in->held[BoxHelmStarboard] ? 1.0f : 0.0f) -
        (in->held[BoxHelmPort] ? 1.0f : 0.0f);
    if (steer != 0) {
        rudder = glm_clamp(rudder + steer * vessel->rudder_rate * dt, -1, 1);
    } else {
        float back = fminf(fabsf(rudder), vessel->rudder_return * dt);
        rudder -= rudder < 0 ? -back : back;
    }

    if (in->pressed[BoxHelmCentre] || in->held[BoxHelmCentre]) {
        rudder = 0;
    }

    status->throttle = throttle;
    status->rudder = rudder;
}

/** Find the place of a local point of a vessel in the world.
 *
 * @param world The world
 * @param e The vessel
 * @param local The point in the local frame of the vessel
 * @param out Receives the point in the world
 */
static void box_helm_to_world(
    const ecs_world_t *world,
    ecs_entity_t e,
    const vec3 local,
    vec3 out)
{
    mat4 m;
    if (box_controller_pose(world, e, m)) {
        glm_mat4_mulv3(m, ECS_CONST_CAST(float*, local), 1.0f, out);
    } else {
        glm_vec3_copy(ECS_CONST_CAST(float*, local), out);
    }
}

/** Find the first person controller whose feet are nearest to the stand, within reach.
 *
 * @param world The world
 * @param stand The stand in the world
 * @param reach The reach
 * @return The controller, 0 for none
 */
static ecs_entity_t box_helm_nearest(
    ecs_world_t *world,
    const vec3 stand,
    float reach)
{
    ecs_entity_t best = 0;
    float best_distance = reach;
    ecs_iter_t it = ecs_each(world, FirstPersonController);
    while (ecs_each_next(&it)) {
        const FirstPersonController *fp = ecs_field(&it, FirstPersonController, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            WorldTransform buf;
            const WorldTransform *w = box_transform_world(world, e, &buf);
            if (!w) {
                continue;
            }

            vec3 feet = { w->m[3][0], w->m[3][1] - fp[i].height, w->m[3][2] };
            float d = glm_vec3_distance(feet, ECS_CONST_CAST(float*, stand));
            if (d <= best_distance) {
                best_distance = d;
                best = e;
            }
        }
    }

    return best;
}

/** Give the pilot of a vessel its own actions back and let the autopilot steer.
 *
 * @param world The world
 * @param status The state, pilot cleared
 */
static void box_helm_let_go(
    ecs_world_t *world,
    VesselStatus *status)
{
    ecs_entity_t pilot = status->pilot;
    FirstPersonController *fp = pilot && ecs_is_alive(world, pilot)
        ? ecs_get_mut(world, pilot, FirstPersonController)
        : NULL;
    if (fp) {
        fp->actions = status->pilot_actions;
        ecs_modified(world, pilot, FirstPersonController);
    }

    status->pilot = 0;
    status->pilot_actions = 0;
    status->mode = VesselRejoining;
    status->backoff = 0;
}

/** Make the first person controller within reach of the stand of a vessel its pilot.
 *
 * @param world The world
 * @param e The vessel
 * @param vessel The settings
 * @param status The state, pilot, mode, throttle and rudder written
 */
static void box_helm_take(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    VesselStatus *status)
{
    vec3 stand;
    box_helm_to_world(world, e, vessel->stand, stand);
    ecs_entity_t pilot = box_helm_nearest(world, stand, vessel->reach);
    FirstPersonController *fp = pilot ? ecs_get_mut(world, pilot, FirstPersonController) : NULL;
    if (fp) {
        status->pilot = pilot;
        status->pilot_actions = fp->actions;
        fp->actions = vessel->actions;
        fp->flying = false;
        glm_vec3_zero(fp->velocity);
        ecs_modified(world, pilot, FirstPersonController);

        status->mode = VesselManual;
        status->throttle = vessel->max_speed > 0
            ? glm_clamp(status->speed / vessel->max_speed, BOX_HELM_ASTERN, 1)
            : 0;
        status->rudder = 0;
    }
}

void box_helm_use(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    VesselStatus *status,
    const box_helm_input_t *in)
{
    ecs_entity_t pilot = status->pilot;
    bool gone = pilot &&
        (!ecs_is_alive(world, pilot) || !ecs_has(world, pilot, FirstPersonController));
    if (gone) {
        status->pilot = 0;
        status->pilot_actions = 0;
        status->mode = VesselRejoining;
        status->backoff = 0;
    } else if (in->pressed[BoxHelmUse] && pilot) {
        box_helm_let_go(world, status);
    } else if (in->pressed[BoxHelmUse]) {
        box_helm_take(world, e, vessel, status);
    }
}

/** Set one angle of the Rotation of an entity when it differs.
 *
 * @param world The world
 * @param e The entity, 0 for none
 * @param axis 0 for X, 2 for Z
 * @param angle The angle
 */
static void box_helm_turn(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t axis,
    float angle)
{
    const Rotation *r = e && ecs_is_alive(world, e) ? ecs_get(world, e, Rotation) : NULL;
    Rotation now = r ? *r : (Rotation){ 0, 0, 0 };
    float *value = axis ? &now.z : &now.x;
    bool changed = !r || fabsf(*value - angle) >= 1e-6f;
    if (e && ecs_is_alive(world, e) && changed) {
        *value = angle;
        ecs_set_ptr(world, e, Rotation, &now);
    }
}

/** Keep the pilot of a vessel at the stand, its eye above it, at the pose of this frame.
 *
 * @param world The world
 * @param e The vessel
 * @param vessel The settings
 * @param pilot The pilot
 */
static void box_helm_hold(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    ecs_entity_t pilot)
{
    FirstPersonController *fp = ecs_get_mut(world, pilot, FirstPersonController);
    Position *p = ecs_get_mut(world, pilot, Position);
    float height = fp && fp->height > 0 ? fp->height : (fp ? fp->eye : 0);
    vec3 eye = { vessel->stand[0], vessel->stand[1] + height, vessel->stand[2] };
    vec3 at;
    box_helm_to_world(world, e, eye, at);

    mat4 now;
    bool placed = box_controller_pose(world, e, now);
    ecs_entity_t parent = ecs_get_parent(world, pilot);
    WorldTransform buf;
    const WorldTransform *w = parent ? box_transform_world(world, parent, &buf) : NULL;
    if (w) {
        mat4 inverse;
        glm_mat4_inv(ECS_CONST_CAST(vec4*, w->m), inverse);
        glm_mat4_mulv3(inverse, at, 1.0f, at);
    }

    if (fp && p) {
        p->x = at[0];
        p->y = at[1];
        p->z = at[2];
        glm_vec3_zero(fp->velocity);
        fp->flying = false;
        fp->carrier = placed ? e : 0;
        if (placed) {
            ecs_os_memcpy(fp->carrier_pose, now, ECS_SIZEOF(mat4));
        }

        ecs_modified(world, pilot, Position);
        ecs_modified(world, pilot, FirstPersonController);
    }
}

void box_helm_place(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    const VesselStatus *status)
{
    box_helm_turn(world, vessel->wheel, 2, status->rudder * BOX_HELM_WHEEL_TURN);
    box_helm_turn(world, vessel->lever, 0, status->throttle * BOX_HELM_LEVER_TURN);
    if (status->pilot) {
        box_helm_hold(world, e, vessel, status->pilot);
    }
}
