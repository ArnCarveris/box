/**
 * @file Vessel.c
 * @brief A vessel cruises its route and docks, a pilot steers it at the helm, the hull stops at
 * quays, and the autopilot rejoins the route, on the vessel of test/assets/controller/vessel.flecs.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Seconds of one frame */
#define VESSEL_DT (0.1f)

/** Load the vessel scene into a new world.
 *
 * @return The world
 */
static ecs_world_t* vessel_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = box_app_locate("test/assets/controller/vessel.flecs");
    test_assert(path != NULL);

    ecs_entity_t s = ecs_script(world, {
        .filename = path
    });
    test_assert(s != 0);
    ecs_os_free(path);

    return world;
}

/** Find the vessel of the scene.
 *
 * @param world The world
 * @return The vessel
 */
static ecs_entity_t vessel_ship(
    ecs_world_t *world)
{
    ecs_entity_t ship = ecs_lookup(world, "ship");
    test_assert(ship != 0);

    return ship;
}

/** Get the state of the vessel.
 *
 * @param world The world
 * @return The state
 */
static const VesselStatus* vessel_status(
    ecs_world_t *world)
{
    const VesselStatus *s = ecs_get(world, vessel_ship(world), VesselStatus);
    test_assert(s != NULL);

    return s;
}

/** Run frames for a number of seconds.
 *
 * @param world The world
 * @param seconds The seconds
 */
static void vessel_run(
    ecs_world_t *world,
    float seconds)
{
    int32_t frames = (int32_t)(seconds / VESSEL_DT + 0.5f);
    for (int32_t i = 0; i < frames; i ++) {
        ecs_progress(world, VESSEL_DT);
    }
}

/** Run frames until the vessel is in a mode, at most for a number of seconds.
 *
 * @param world The world
 * @param mode The mode
 * @param seconds The most seconds
 * @return Whether the vessel came into the mode
 */
static bool vessel_until(
    ecs_world_t *world,
    VesselMode mode,
    float seconds)
{
    int32_t frames = (int32_t)(seconds / VESSEL_DT + 0.5f);
    int32_t i = 0;
    while (i < frames && vessel_status(world)->mode != mode) {
        ecs_progress(world, VESSEL_DT);
        i ++;
    }

    return vessel_status(world)->mode == mode;
}

/** Hold a key down for a number of seconds.
 *
 * @param world The world
 * @param key The key
 * @param seconds The seconds
 */
static void vessel_hold(
    ecs_world_t *world,
    Key key,
    float seconds)
{
    box_input_key(world, key, true);
    vessel_run(world, seconds);
    box_input_key(world, key, false);
    ecs_progress(world, VESSEL_DT);
}

/** Put a walker on the stand of the helm and make it take the helm with F.
 *
 * @param world The world
 * @return The walker
 */
static ecs_entity_t vessel_take(
    ecs_world_t *world)
{
    ecs_entity_t fp = ecs_entity(world, { .name = "walker" });
    ecs_set(world, fp, Position, { 0, 3.7f, -6 });
    ecs_add(world, fp, FirstPersonController);
    vessel_run(world, 1);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    box_input_key_press(world, KeyF, 1);
    vessel_run(world, 0.3f);

    const VesselStatus *s = vessel_status(world);
    test_assert(s->pilot == fp);
    test_int(s->mode, VesselManual);
    test_bool(ecs_get(world, fp, FirstPersonController)->flying, false);

    return fp;
}

/** Add a box shape as a quay or a pier at a place, turned about the vertical.
 *
 * @param world The world
 * @param x X of its middle
 * @param z Z of its middle
 * @param yaw Its turn about the vertical
 * @param size Its size
 */
static void vessel_quay(
    ecs_world_t *world,
    float x,
    float z,
    float yaw,
    Box size)
{
    ecs_entity_t quay = ecs_new(world);
    ecs_set(world, quay, Position, { x, 0, z });
    ecs_set(world, quay, Rotation, { 0, yaw, 0 });
    ecs_set_ptr(world, quay, Box, &size);
}

void Vessel_cruises_and_docks(void) {
    // controllers/vessel, controllers/route
    ecs_world_t *world = vessel_world();
    ecs_progress(world, VESSEL_DT);

    const VesselStatus *s = vessel_status(world);
    test_int(s->mode, VesselDocked);
    test_assert(fabsf(s->x) < 1e-4f);
    test_assert(fabsf(s->z) < 1e-4f);
    test_assert(fabsf(s->heading) < 1e-3f);

    test_bool(vessel_until(world, VesselCruising, 3), true);
    vessel_run(world, 15);

    s = vessel_status(world);
    test_assert(s->speed > 5.9f && s->speed <= 6.0001f);
    test_assert(s->distance > 30);
    test_assert(s->to_dock > 0);

    float most = 0;
    int32_t frames = 0;
    while (frames < 3000 && vessel_status(world)->mode != VesselDocked) {
        ecs_progress(world, VESSEL_DT);
        most = fmaxf(most, vessel_status(world)->speed);
        frames ++;
    }

    s = vessel_status(world);
    test_int(s->mode, VesselDocked);
    test_assert(most <= 6.0001f);
    test_assert(fabsf(s->x) < 1e-3f);
    test_assert(fabsf(s->z) < 1e-3f);
    test_flt(s->speed, 0);

    const Position *p = ecs_get(world, vessel_ship(world), Position);
    test_assert(fabsf(p->x) < 0.05f);
    test_assert(fabsf(p->z) < 0.05f);

    ecs_fini(world);
}

void Vessel_heading_follows_route(void) {
    // controllers/vessel: the heading follows the direction of travel through the spring
    ecs_world_t *world = vessel_world();
    test_bool(vessel_until(world, VesselCruising, 3), true);
    vessel_run(world, 10);

    float worst = 0;
    float x = vessel_status(world)->x;
    float z = vessel_status(world)->z;
    for (int32_t i = 0; i < 300; i ++) {
        ecs_progress(world, VESSEL_DT);
        const VesselStatus *s = vessel_status(world);
        const Rotation *r = ecs_get(world, vessel_ship(world), Rotation);
        test_assert(fabsf(r->y - s->heading) < 1e-5f);

        float travel = atan2f(s->x - x, s->z - z);
        float error = fabsf(atan2f(sinf(travel - s->heading), cosf(travel - s->heading)));
        worst = fmaxf(worst, error);
        x = s->x;
        z = s->z;
    }

    test_assert(worst < 0.25f);

    ecs_fini(world);
}

void Vessel_wheel_and_lever_follow(void) {
    // controllers/helm
    ecs_world_t *world = vessel_world();
    test_bool(vessel_until(world, VesselCruising, 3), true);
    vessel_run(world, 20);

    const VesselStatus *s = vessel_status(world);
    const Rotation *wheel = ecs_get(world, ecs_lookup(world, "ship.wheel"), Rotation);
    const Rotation *lever = ecs_get(world, ecs_lookup(world, "ship.lever"), Rotation);
    test_assert(s->throttle > 0.5f);
    test_assert(fabsf(wheel->z - s->rudder * 2.6f) < 1e-4f);
    test_assert(fabsf(lever->x - s->throttle * 0.6f) < 1e-4f);

    ecs_fini(world);
}

void Vessel_helm_throttle_and_rudder(void) {
    // controllers/helm, controllers/vessel
    ecs_world_t *world = vessel_world();
    ecs_entity_t fp = vessel_take(world);

    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_assert(c->actions == ecs_lookup(world, "box.controllers.helm"));

    vessel_hold(world, KeyW, 4);
    const VesselStatus *s = vessel_status(world);
    test_flt(s->throttle, 1);
    test_assert(s->speed > 1);

    vessel_run(world, 6);
    float before = vessel_status(world)->heading;
    vessel_hold(world, KeyD, 3);
    s = vessel_status(world);
    test_assert(s->rudder > 0.9f);
    test_assert(s->yaw_rate < -0.01f);
    test_assert(s->heading < before - 0.02f);

    vessel_run(world, 3);
    test_assert(vessel_status(world)->rudder < 0.01f);

    box_input_key_press(world, KeyX, 1);
    vessel_run(world, 0.3f);
    test_flt(vessel_status(world)->throttle, 0);

    const Position *p = ecs_get(world, fp, Position);
    WorldTransform buf;
    const WorldTransform *w = box_transform_world(world, vessel_ship(world), &buf);
    vec3 eye;
    glm_mat4_mulv3(ECS_CONST_CAST(vec4*, w->m), (vec3){ 0, 3.7f, -6 }, 1.0f, eye);
    test_assert(fabsf(p->x - eye[0]) < 0.05f);
    test_assert(fabsf(p->y - eye[1]) < 0.05f);
    test_assert(fabsf(p->z - eye[2]) < 0.05f);

    ecs_fini(world);
}

void Vessel_rudder_reverses_astern(void) {
    // controllers/vessel
    ecs_world_t *world = vessel_world();
    vessel_take(world);

    vessel_hold(world, KeyS, 2);
    test_flt(vessel_status(world)->throttle, -0.5f);
    vessel_run(world, 6);
    test_assert(vessel_status(world)->speed < -1);

    vessel_hold(world, KeyD, 3);
    const VesselStatus *s = vessel_status(world);
    test_assert(s->rudder > 0.9f);
    test_assert(s->yaw_rate > 0.005f);

    ecs_fini(world);
}

void Vessel_quay_ahead_stops(void) {
    // controllers/hull, controllers/vessel
    ecs_world_t *world = vessel_world();
    vessel_quay(world, 0, 25, 0, (Box){ 40, 3, 6 });
    vessel_take(world);

    vessel_hold(world, KeyW, 3);
    float front = 0;
    for (int32_t i = 0; i < 300; i ++) {
        ecs_progress(world, VESSEL_DT);
        front = fmaxf(front, vessel_status(world)->z + 10);
    }

    const VesselStatus *s = vessel_status(world);
    test_assert(s->bumps > 0);
    test_assert(front < 22.0f + 0.5f);
    test_assert(front > 20.0f);
    test_assert(s->speed < 0.5f);

    ecs_fini(world);
}

void Vessel_turn_into_quay_goes_straight(void) {
    // controllers/hull, controllers/vessel: a turn to port would swing the bow into the quay
    ecs_world_t *world = vessel_world();
    vessel_quay(world, 6.5f, 10, 0, (Box){ 6, 3, 80 });
    vessel_take(world);

    vessel_hold(world, KeyW, 3);
    vessel_run(world, 3);
    float before = vessel_status(world)->heading;
    float z = vessel_status(world)->z;
    vessel_hold(world, KeyA, 4);

    const VesselStatus *s = vessel_status(world);
    test_assert(s->rudder < -0.9f);
    test_assert(fabsf(s->heading - before) < 0.08f);
    test_assert(s->z > z + 3);

    ecs_fini(world);
}

void Vessel_open_water_turns(void) {
    // controllers/hull: the same turn with no quay beside the hull
    ecs_world_t *world = vessel_world();
    vessel_take(world);

    vessel_hold(world, KeyW, 3);
    vessel_run(world, 3);
    float before = vessel_status(world)->heading;
    vessel_hold(world, KeyD, 4);

    test_assert(vessel_status(world)->heading < before - 0.2f);

    ecs_fini(world);
}

void Vessel_autopilot_rejoins(void) {
    // controllers/vessel, controllers/helm
    ecs_world_t *world = vessel_world();
    ecs_entity_t fp = vessel_take(world);

    vessel_hold(world, KeyW, 3);
    vessel_run(world, 4);
    vessel_hold(world, KeyA, 4);
    vessel_run(world, 4);

    const VesselStatus *s = vessel_status(world);
    test_assert(fabsf(s->x) > 2);

    box_input_key_press(world, KeyF, 1);
    vessel_run(world, 0.3f);
    s = vessel_status(world);
    test_int(s->mode, VesselRejoining);
    test_assert(s->pilot == 0);

    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_assert(c->actions == ecs_lookup(world, "box.controllers.first_person"));

    test_bool(vessel_until(world, VesselCruising, 200), true);
    test_bool(vessel_until(world, VesselDocked, 200), true);

    s = vessel_status(world);
    test_assert(fabsf(s->x) < 1e-3f);
    test_assert(fabsf(s->z) < 1e-3f);

    ecs_fini(world);
}

void Vessel_walker_rides(void) {
    // controllers/vessel, controllers/ride
    ecs_world_t *world = vessel_world();
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 1, 3.7f, 4 });
    ecs_add(world, fp, FirstPersonController);
    vessel_run(world, 1);

    test_bool(vessel_until(world, VesselCruising, 3), true);
    vessel_run(world, 30);

    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_assert(c->carrier == vessel_ship(world));

    WorldTransform buf;
    const WorldTransform *w = box_transform_world(world, vessel_ship(world), &buf);
    mat4 inverse;
    glm_mat4_inv(ECS_CONST_CAST(vec4*, w->m), inverse);

    const Position *p = ecs_get(world, fp, Position);
    vec3 local;
    glm_mat4_mulv3(inverse, (vec3){ p->x, p->y, p->z }, 1.0f, local);
    test_assert(fabsf(local[0] - 1) < 0.05f);
    test_assert(fabsf(local[1] - 3.7f) < 0.05f);
    test_assert(fabsf(local[2] - 4) < 0.05f);

    ecs_fini(world);
}
