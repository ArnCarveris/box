/**
 * @file controllers/vessel.c
 * @brief Vessels that cruise a route and dock, or that a pilot steers at the helm.
 *
 * On the route the pivot of a vessel stays on the spline: it speeds up, cruises, brakes to rest
 * on the dock mark and waits, and the heading follows the tangent through a damped spring.
 *
 * Steered by hand or by the autopilot that rejoins the route, the speed follows the throttle,
 * the turn rate the rudder and the water flowing past it, and the hull is checked against the
 * ground and structures below its deck. Heel and waves give the pose; the helm comes last.
 */

#include "controllers.h"
#include <math.h>

/** Default cruising speed in units per second */
#define BOX_VESSEL_SPEED (6.0f)

/** Default acceleration in units per second squared */
#define BOX_VESSEL_ACCEL (0.3f)

/** Default seconds at the dock */
#define BOX_VESSEL_WAIT (25.0f)

/** Distance from the dock mark within which a braking vessel is at rest on it */
#define BOX_VESSEL_ARRIVE (0.03f)

/** How far ahead along the route the autopilot that rejoins it aims */
#define BOX_VESSEL_LOOKAHEAD (45.0f)

/** Distance from the route and heading error within which a rejoining vessel is on it */
#define BOX_VESSEL_ON_ROUTE (3.0f)
#define BOX_VESSEL_ON_HEADING (0.15f)

/** Rate per second at which the place off the route left by rejoining it eases away */
#define BOX_VESSEL_SETTLE (0.5f)

/** Throttle of the autopilot rejoining the route, and backing away from a bump */
#define BOX_VESSEL_REJOIN_THROTTLE (0.6f)
#define BOX_VESSEL_BACKOFF_THROTTLE (-0.4f)

/** Seconds the autopilot backs away after a bump */
#define BOX_VESSEL_BACKOFF (3.0f)

/** Share of its speed a vessel keeps, the other way, when it runs into something */
#define BOX_VESSEL_BUMP (-0.2f)

/** Share of the waves a docked vessel feels */
#define BOX_VESSEL_SWELL_DOCKED (0.08f)

void box_vessel_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    const ecs_entity_t *scope = ti->hooks.ctx;
    Vessel *values = ptr;
    for (int32_t i = 0; i < count; i ++) {
        values[i] = (Vessel){
            .speed = BOX_VESSEL_SPEED,
            .accel = BOX_VESSEL_ACCEL,
            .wait = BOX_VESSEL_WAIT,
            .max_speed = 9,
            .max_yaw_rate = 0.12f,
            .yaw_response = 2.5f,
            .steer_heel = 0.6f,
            .max_heel = 0.105f,
            .waves = { 0.021f, 0.035f, 0.25f },
            .keel = -5,
            .deck = 5,
            .inset = 1.2f,
            .reach = 1.4f,
            .throttle_rate = 0.35f,
            .rudder_rate = 0.9f,
            .rudder_return = 0.5f,
            .actions = *scope
        };
    }
}

/** Wrap an angle into -pi to pi.
 *
 * @param a The angle
 * @return The same direction within -pi to pi
 */
static float box_vessel_wrap_angle(
    float a)
{
    float w = fmodf(a + GLM_PIf, 2 * GLM_PIf);
    if (w < 0) {
        w += 2 * GLM_PIf;
    }

    return w - GLM_PIf;
}

/** Put a vessel at rest on its dock mark, docked and waiting.
 *
 * @param vessel The settings
 * @param status The state, written
 * @param route The route
 */
static void box_vessel_dock(
    const Vessel *vessel,
    VesselStatus *status,
    const box_route_t *route)
{
    status->distance = box_route_waypoint(route, vessel->dock);
    status->offset[0] = 0;
    status->offset[1] = 0;
    status->speed = 0;
    status->mode = VesselDocked;
    status->wait = vessel->wait;
}

/** Keep the pivot on the route, apart from an offset left by rejoining it that eases away, and
 * turn the heading toward the tangent with the spring.
 *
 * @param vessel The settings
 * @param status The state, place, heading and turn rate written
 * @param route The route
 * @param dt Seconds of the frame
 */
static void box_vessel_follow(
    const Vessel *vessel,
    VesselStatus *status,
    const box_route_t *route,
    float dt)
{
    float target;
    float rx;
    float rz;
    box_route_at(route, status->distance, &rx, &rz, &target);

    float settle = expf(-BOX_VESSEL_SETTLE * dt);
    status->offset[0] *= settle;
    status->offset[1] *= settle;
    status->x = rx + status->offset[0];
    status->z = rz + status->offset[1];

    float k = vessel->yaw_response;
    float error = box_vessel_wrap_angle(target - status->heading);
    status->yaw_rate += (k * k * error - 2 * k * status->yaw_rate) * dt;
    status->heading = box_vessel_wrap_angle(status->heading + status->yaw_rate * dt);
}

/** Drive along the route: speed up to the cruising speed and brake to rest on the dock mark.
 *
 * @param vessel The settings
 * @param status The state
 * @param route The route
 * @param dt Seconds of the frame
 */
static void box_vessel_cruise(
    const Vessel *vessel,
    VesselStatus *status,
    const box_route_t *route,
    float dt)
{
    float dock = box_route_waypoint(route, vessel->dock);
    float left = box_route_wrap(route, dock - status->distance);
    if (left <= 0) {
        left = route->total;
    }

    float brake = 1.2f * vessel->accel;
    float most = 1.5f * vessel->accel;
    float want = fminf(vessel->speed, sqrtf(2 * brake * left));
    float v = fmaxf(0, status->speed);
    if (want >= v) {
        v = fminf(want, v + vessel->accel * dt);
    } else {
        v = fmaxf(want, v - most * dt);
    }

    float ds = fminf(v * dt, left);
    status->speed = v;
    status->distance = box_route_wrap(route, status->distance + ds);

    if (left - ds < BOX_VESSEL_ARRIVE) {
        box_vessel_dock(vessel, status, route);
    }
}

/** Steer by throttle and rudder off the route, checking the hull against what is around it.
 *
 * @param world The world
 * @param e The vessel
 * @param vessel The settings
 * @param status The state
 * @param y Height of the vessel's frame, unturned (the heave)
 * @param dt Seconds of the frame
 */
static void box_vessel_steer(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    VesselStatus *status,
    float y,
    float dt)
{
    float vmax = vessel->max_speed;
    float acc = vessel->accel * 1.5f * dt;
    float v = status->speed;
    v += glm_clamp(status->throttle * vmax - v, -acc, acc);

    float flow = vmax > 0 ? glm_clamp(v / (vmax * 0.5f), -1, 1) : 0;
    float target = -status->rudder * vessel->max_yaw_rate * flow;
    status->yaw_rate += (target - status->yaw_rate) * fminf(1, dt * 0.8f);

    float h = status->heading + status->yaw_rate * dt;
    float nx = status->x + sinf(h) * v * dt;
    float nz = status->z + cosf(h) * v * dt;
    status->speed = v;

    bool moves = nx != status->x || nz != status->z || h != status->heading;
    bool check = moves && vessel->outline;
    int32_t now = check
        ? box_vessel_blocked(world, e, vessel, y, status->x, status->z, status->heading)
        : 0;
    int32_t turned = check ? box_vessel_blocked(world, e, vessel, y, nx, nz, h) : 0;
    bool turn = !check || turned == 0 || (now > 0 && turned <= now);
    float sx = status->x + sinf(status->heading) * v * dt;
    float sz = status->z + cosf(status->heading) * v * dt;
    int32_t ahead = turn
        ? 0
        : box_vessel_blocked(world, e, vessel, y, sx, sz, status->heading);
    bool straight = !turn && (ahead == 0 || (now > 0 && ahead <= now));

    if (turn) {
        status->x = nx;
        status->z = nz;
        status->heading = box_vessel_wrap_angle(h);
    } else if (straight) {
        status->yaw_rate = 0;
        status->x = sx;
        status->z = sz;
    } else {
        status->yaw_rate = 0;
        status->speed = BOX_VESSEL_BUMP * v;
        status->bumps ++;
        if (!status->pilot) {
            status->backoff = BOX_VESSEL_BACKOFF;
        }
    }
}

/** Steer back onto the route by pure pursuit, and hand over to the route once on it.
 *
 * @param world The world
 * @param e The vessel
 * @param vessel The settings
 * @param status The state
 * @param route The route
 * @param y Height of the vessel's frame, unturned (the heave)
 * @param dt Seconds of the frame
 */
static void box_vessel_rejoin(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    VesselStatus *status,
    const box_route_t *route,
    float y,
    float dt)
{
    float near = box_route_nearest(route, status->x, status->z);
    float ax, az, ah;
    box_route_at(route, near + BOX_VESSEL_LOOKAHEAD, &ax, &az, &ah);

    float want = atan2f(ax - status->x, az - status->z);
    float error = box_vessel_wrap_angle(want - status->heading);
    status->rudder = glm_clamp(-error * 3, -1, 1);
    status->throttle = BOX_VESSEL_REJOIN_THROTTLE;
    if (status->backoff > 0) {
        status->backoff -= dt;
        status->throttle = BOX_VESSEL_BACKOFF_THROTTLE;
        status->rudder = -status->rudder;
    }

    box_vessel_steer(world, e, vessel, status, y, dt);

    float qx, qz, qh;
    near = box_route_nearest(route, status->x, status->z);
    box_route_at(route, near, &qx, &qz, &qh);

    float off = sqrtf((qx - status->x) * (qx - status->x) + (qz - status->z) * (qz - status->z));
    float turn = fabsf(box_vessel_wrap_angle(qh - status->heading));
    if (off < BOX_VESSEL_ON_ROUTE && turn < BOX_VESSEL_ON_HEADING) {
        status->mode = VesselCruising;
        status->distance = near;
        status->offset[0] = status->x - qx;
        status->offset[1] = status->z - qz;
        status->backoff = 0;
    }
}

/** Show the motion of a vessel on its helm while nobody holds it, so the wheel and the lever
 * move on autopilot.
 *
 * @param vessel The settings
 * @param status The state, throttle and rudder written
 */
static void box_vessel_helm_from_motion(
    const Vessel *vessel,
    VesselStatus *status)
{
    float vmax = vessel->max_speed;
    if (vmax > 0 && vessel->max_yaw_rate > 0) {
        float flow = fmaxf(0.3f, fabsf(status->speed) / (vmax * 0.5f));
        status->throttle = status->speed / vmax;
        status->rudder = glm_clamp(-status->yaw_rate / (vessel->max_yaw_rate * flow), -1, 1);
    }
}

/** Heel into turns and move with the waves, calmer while docked.
 *
 * @param vessel The settings
 * @param status The state, heel, swell, pitch, roll and heave written
 * @param turned Heading change of the frame
 * @param dt Seconds of the frame
 */
static void box_vessel_waves(
    const Vessel *vessel,
    VesselStatus *status,
    float turned,
    float dt)
{
    float rate = dt > 0 ? turned / dt : 0;
    float lean = glm_clamp(-rate * status->speed * vessel->steer_heel, -1, 1) * vessel->max_heel;
    status->heel += (lean - status->heel) * fminf(1, dt * 0.7f);

    float cruise = vessel->speed > 0 ? vessel->speed : 1;
    float swell = status->mode == VesselDocked
        ? BOX_VESSEL_SWELL_DOCKED
        : 0.35f + 0.65f * fminf(1, fabsf(status->speed) / cruise);
    status->swell += (swell - status->swell) * fminf(1, dt * 0.4f);
    status->time += dt;

    float t = status->time;
    float a = status->swell;
    status->pitch = a * vessel->waves[0] * (sinf(0.55f * t) + 0.5f * sinf(0.93f * t + 1.3f));
    status->roll = status->heel +
        a * vessel->waves[1] * (sinf(0.41f * t + 0.7f) + 0.4f * sinf(1.07f * t));
    status->heave = a * vessel->waves[2] * (sinf(0.63f * t) + 0.5f * sinf(1.21f * t + 2));
}

/** Write the pose of a vessel: the pivot at its place, turned, pitched and rolled about it.
 *
 * @param vessel The settings
 * @param status The state
 * @param p The Position, written
 * @param r The Rotation, written
 */
static void box_vessel_pose(
    const Vessel *vessel,
    const VesselStatus *status,
    Position *p,
    Rotation *r)
{
    vec3 angles = { -status->pitch, status->heading, status->roll };
    mat4 m;
    glm_euler_yxz(angles, m);

    vec3 turned;
    glm_mat4_mulv3(m, ECS_CONST_CAST(float*, vessel->pivot), 0.0f, turned);

    p->x = vessel->origin[0] + status->x - turned[0];
    p->y = vessel->origin[1] + vessel->pivot[1] + status->heave - turned[1];
    p->z = vessel->origin[2] + status->z - turned[2];
    r->x = angles[0];
    r->y = angles[1];
    r->z = angles[2];
}

/** Move one vessel with a valid route through a frame.
 *
 * @param world The world
 * @param e The vessel
 * @param vessel The settings
 * @param status The state
 * @param route Its route
 * @param p Its Position
 * @param r Its Rotation
 * @param dt Seconds of the frame
 */
static void box_vessel_voyage(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    VesselStatus *status,
    const box_route_t *route,
    Position *p,
    Rotation *r,
    float dt)
{
    if (!status->placed) {
        box_vessel_dock(vessel, status, route);
        box_route_at(route, status->distance, &status->x, &status->z, &status->heading);
        status->swell = BOX_VESSEL_SWELL_DOCKED;
        status->placed = true;
    }

    box_helm_input_t in;
    box_helm_read(world, vessel->actions, &in);
    box_helm_use(world, e, vessel, status, &in);
    if (status->pilot) {
        box_helm_steer(vessel, status, &in, dt);
    }

    float heading = status->heading;
    if (status->mode == VesselDocked) {
        status->speed = 0;
        status->wait -= dt;
        box_vessel_follow(vessel, status, route, dt);
        if (status->wait <= 0) {
            status->wait = 0;
            status->mode = VesselCruising;
        }
    } else if (status->mode == VesselCruising) {
        box_vessel_cruise(vessel, status, route, dt);
        box_vessel_follow(vessel, status, route, dt);
    } else if (status->mode == VesselManual) {
        box_vessel_steer(world, e, vessel, status, status->heave, dt);
    } else {
        box_vessel_rejoin(world, e, vessel, status, route, status->heave, dt);
    }

    if (!status->pilot) {
        box_vessel_helm_from_motion(vessel, status);
    }

    float dock = box_route_waypoint(route, vessel->dock);
    float here = status->mode == VesselManual || status->mode == VesselRejoining
        ? box_route_nearest(route, status->x, status->z)
        : status->distance;
    status->to_dock = status->mode == VesselDocked ? 0 : box_route_wrap(route, dock - here);

    box_vessel_waves(vessel, status, box_vessel_wrap_angle(status->heading - heading), dt);
    box_vessel_pose(vessel, status, p, r);
}

/** Move one vessel through a frame.
 *
 * @param world The world
 * @param e The vessel
 * @param vessel The settings
 * @param status The state
 * @param p Its Position
 * @param r Its Rotation
 * @param dt Seconds of the frame
 * @return Whether it has a route and moved
 */
static bool box_vessel_update(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    VesselStatus *status,
    Position *p,
    Rotation *r,
    float dt)
{
    const Path3D *path = vessel->route && ecs_is_alive(world, vessel->route)
        ? ecs_get(world, vessel->route, Path3D)
        : NULL;
    box_route_t route;
    bool valid = box_route_init(path, &route);
    if (valid) {
        box_vessel_voyage(world, e, vessel, status, &route, p, r, dt);
        box_route_fini(&route);
    }

    return valid;
}

void BoxVesselUpdate(
    ecs_iter_t *it)
{
    const Vessel *v = ecs_field(it, Vessel, 0);
    VesselStatus *s = ecs_field(it, VesselStatus, 1);
    Position *p = ecs_field(it, Position, 2);
    Rotation *r = ecs_field(it, Rotation, 3);
    float dt = fminf(it->delta_time, BOX_CONTROLLER_DT_MAX);
    for (int32_t i = 0; dt > 0 && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (box_vessel_update(it->world, e, &v[i], &s[i], &p[i], &r[i], dt)) {
            ecs_modified(it->world, e, Position);
            ecs_modified(it->world, e, Rotation);
            ecs_modified(it->world, e, VesselStatus);
            box_helm_place(it->world, e, &v[i], &s[i]);
        }
    }
}
