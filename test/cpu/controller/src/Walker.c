/**
 * @file Walker.c
 * @brief The first person walker of the portal bunker: high steps, ladders, swimming, triangle
 * colliders and running, on the courses of test/assets/controller/walker.flecs.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Frame time of the tests */
#define WALKER_DT (1.0f / 60.0f)

/** Eye height of the walker */
#define WALKER_EYE (1.65f)

/** Load the courses into a new world.
 *
 * @return The world
 */
static ecs_world_t* walker_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = box_app_locate("test/assets/controller/walker.flecs");
    test_assert(path != NULL);

    ecs_entity_t s = ecs_script(world, {
        .filename = path
    });
    test_assert(s != 0);
    ecs_os_free(path);

    return world;
}

/** Create a walker with the original demo's tuning, its eye at a position.
 *
 * @param world The world
 * @param x Eye x
 * @param y Eye y
 * @param z Eye z
 * @param yaw Yaw, 0 walks along -z
 * @return The walker
 */
static ecs_entity_t walker_new(
    ecs_world_t *world,
    float x,
    float y,
    float z,
    float yaw)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, { x, y, z });
    ecs_set(world, e, Rotation, { 0, yaw, 0 });

    FirstPersonController *c = ecs_ensure(world, e, FirstPersonController);
    c->speed = 4.2f;
    c->run = 7.5f;
    c->eye = WALKER_EYE;
    c->step = 0.55f;
    c->climb = 2.4f;
    ecs_modified(world, e, FirstPersonController);

    return e;
}

/** Run frames of the world.
 *
 * @param world The world
 * @param frames Number of frames
 */
static void walker_run(
    ecs_world_t *world,
    int32_t frames)
{
    for (int32_t i = 0; i < frames; i ++) {
        ecs_progress(world, WALKER_DT);
    }
}

/** Get the height of the feet of a walker.
 *
 * @param world The world
 * @param e The walker
 * @return The height of the feet
 */
static float walker_feet(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_get(world, e, Position)->y - WALKER_EYE;
}

/** Get the controller of a walker.
 *
 * @param world The world
 * @param e The walker
 * @return The controller
 */
static const FirstPersonController* walker_get(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_get(world, e, FirstPersonController);
}

void Walker_high_step(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 0, WALKER_EYE, 0, 0);
    walker_run(world, 10);

    box_input_key(world, KeyW, true);
    walker_run(world, 40);

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -2.3f);
    test_assert(fabsf(walker_feet(world, fp) - 0.5f) < 0.01f);
    test_bool(walker_get(world, fp)->grounded, true);

    ecs_fini(world);
}

void Walker_high_step_needs_step(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 0, WALKER_EYE, 0, 0);
    ecs_ensure(world, fp, FirstPersonController)->step = 0.3f;
    walker_run(world, 10);

    box_input_key(world, KeyW, true);
    walker_run(world, 40);

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z > -1.75f);
    test_assert(fabsf(walker_feet(world, fp)) < 0.01f);

    ecs_fini(world);
}

void Walker_short_treads(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 10, WALKER_EYE, -0.5f, 0);
    walker_run(world, 10);

    box_input_key(world, KeyW, true);
    walker_run(world, 65);

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -4);
    test_assert(fabsf(walker_feet(world, fp) - 1.5f) < 0.01f);
    test_bool(walker_get(world, fp)->grounded, true);

    ecs_fini(world);
}

void Walker_ladder_climb(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 20, WALKER_EYE, 0, 0);
    walker_run(world, 10);

    box_input_key(world, KeyW, true);
    walker_run(world, 45);

    test_bool(walker_get(world, fp)->climbing, true);
    test_assert(walker_feet(world, fp) > 1);
    test_assert(walker_feet(world, fp) < 2.5f);

    walker_run(world, 80);

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -1.5f);
    test_assert(fabsf(walker_feet(world, fp) - 3.5f) < 0.01f);
    test_bool(walker_get(world, fp)->climbing, false);
    test_bool(walker_get(world, fp)->grounded, true);

    ecs_fini(world);
}

void Walker_ladder_down(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 20, WALKER_EYE, 0, 0);
    walker_run(world, 10);

    box_input_key(world, KeyW, true);
    walker_run(world, 50);
    box_input_key(world, KeyW, false);
    box_input_key(world, KeyS, true);
    ecs_progress(world, WALKER_DT);
    float high = walker_feet(world, fp);
    walker_run(world, 20);

    test_bool(walker_get(world, fp)->climbing, true);
    test_assert(walker_feet(world, fp) < high - 0.6f);

    walker_run(world, 120);

    test_assert(fabsf(walker_feet(world, fp)) < 0.01f);
    test_bool(walker_get(world, fp)->grounded, true);

    ecs_fini(world);
}

void Walker_ladder_needs_climbable(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 30, WALKER_EYE, 0, 0);
    walker_run(world, 10);

    box_input_key(world, KeyW, true);
    walker_run(world, 60);

    test_bool(walker_get(world, fp)->climbing, false);
    test_assert(fabsf(walker_feet(world, fp)) < 0.01f);

    ecs_fini(world);
}

void Walker_swim_floats(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 40, 2 + WALKER_EYE, -5, 0);
    walker_run(world, 120);

    const FirstPersonController *c = walker_get(world, fp);
    test_bool(c->swimming, true);
    test_bool(c->grounded, false);
    test_assert(fabsf(walker_feet(world, fp) + c->swim_depth) < 0.01f);

    ecs_fini(world);
}

void Walker_swim_speed(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 38, WALKER_EYE, -1, 0);
    walker_run(world, 120);
    test_bool(walker_get(world, fp)->swimming, true);

    float z = ecs_get(world, fp, Position)->z;
    box_input_key(world, KeyW, true);
    walker_run(world, 60);

    const FirstPersonController *c = walker_get(world, fp);
    float moved = z - ecs_get(world, fp, Position)->z;
    test_assert(fabsf(moved - c->speed * c->swim_speed) < 0.05f);

    ecs_fini(world);
}

void Walker_swim_climb_out(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 43, WALKER_EYE, -5, -(float)GLM_PI_2);
    walker_run(world, 120);
    test_bool(walker_get(world, fp)->swimming, true);

    box_input_key(world, KeyW, true);
    walker_run(world, 120);
    box_input_key(world, KeyW, false);
    walker_run(world, 30);

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->x > 45.3f);
    test_assert(p->x < 49.7f);
    test_assert(fabsf(walker_feet(world, fp) - 0.5f) < 0.01f);
    test_bool(walker_get(world, fp)->swimming, false);
    test_bool(walker_get(world, fp)->grounded, true);

    ecs_fini(world);
}

void Walker_covered_water_dry(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 60, -3 + WALKER_EYE, -5, 0);

    // The first frame moves the walker before the transforms place the room: a ground plane
    // above it would lift it onto the roof, as it lifts a controller spawned under the plane.
    ecs_ensure(world, fp, FirstPersonController)->ground = -20;
    walker_run(world, 60);

    const FirstPersonController *c = walker_get(world, fp);
    test_bool(c->swimming, false);
    test_bool(c->grounded, true);
    test_assert(fabsf(walker_feet(world, fp) + 4) < 0.01f);

    ecs_fini(world);
}

void Walker_triangle_ramp(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 80, 3 + WALKER_EYE, -1, 0);
    walker_run(world, 60);

    test_bool(walker_get(world, fp)->grounded, true);
    test_assert(fabsf(walker_feet(world, fp) - 0.2f) < 0.05f);

    box_input_key(world, KeyW, true);
    walker_run(world, 60);

    const Position *p = ecs_get(world, fp, Position);
    float surface = -p->z * 0.2f;
    test_assert(p->z < -4);
    test_assert(fabsf(walker_feet(world, fp) - surface) < 0.05f);
    test_bool(walker_get(world, fp)->grounded, true);

    ecs_fini(world);
}

void Walker_run(void) {
    // controllers/walker
    ecs_world_t *world = walker_world();
    ecs_entity_t fp = walker_new(world, 100, WALKER_EYE, 0, 0);
    walker_run(world, 10);

    box_input_key(world, KeyW, true);
    box_input_key(world, KeyShiftLeft, true);
    walker_run(world, 60);

    test_assert(fabsf(ecs_get(world, fp, Position)->z + 7.5f) < 0.2f);

    ecs_fini(world);
}
