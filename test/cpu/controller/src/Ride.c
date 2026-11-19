/**
 * @file Ride.c
 * @brief First person controllers standing on the shapes of a mover go and turn with it.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Make a mover at the origin with a deck 10 by 10 whose top is at height 1.
 *
 * @param world The world
 * @return The mover
 */
static ecs_entity_t ride_ship(
    ecs_world_t *world)
{
    ecs_entity_t ship = ecs_entity(world, { .name = "ship" });
    ecs_set(world, ship, Position, { 0, 0, 0 });
    ecs_set(world, ship, Rotation, { 0, 0, 0 });
    ecs_add(world, ship, DynamicTransform);

    ecs_entity_t deck = ecs_new_w_pair(world, EcsChildOf, ship);
    ecs_set(world, deck, Position, { 0, 0.5f, 0 });
    ecs_set(world, deck, Box, { 10, 1, 10 });

    return ship;
}

/** Make a first person controller whose eye starts at a point and let it settle.
 *
 * @param world The world
 * @param x X of the eye
 * @param y Y of the eye
 * @param z Z of the eye
 * @return The controller
 */
static ecs_entity_t ride_walker(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { x, y, z });
    ecs_add(world, fp, FirstPersonController);

    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    return fp;
}

void Ride_deck_carries(void) {
    // controllers/ride
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t ship = ride_ship(world);
    ecs_entity_t fp = ride_walker(world, 2, 4, 1);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_assert(c->carrier == ship);

    for (int32_t i = 1; i <= 10; i ++) {
        ecs_set(world, ship, Position, { 0.5f * (float)i, 0, 0 });
        ecs_progress(world, 0.1f);
    }

    ecs_progress(world, 0.1f);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->x - 7) < 0.01f);
    test_assert(fabsf(p->y - 2.7f) < 0.01f);
    test_assert(fabsf(p->z - 1) < 0.01f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Ride_deck_turns(void) {
    // controllers/ride
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t ship = ride_ship(world);
    ecs_entity_t fp = ride_walker(world, 2, 4, 1);
    float yaw = ecs_get(world, fp, Rotation)->y;
    WorldTransform buf;
    vec3 expect;

    for (int32_t i = 1; i <= 10; i ++) {
        ecs_set(world, ship, Rotation, { 0, 0.1f * (float)i, 0 });
        ecs_progress(world, 0.1f);
    }

    ecs_progress(world, 0.1f);
    const WorldTransform *m = box_transform_world(world, ship, &buf);
    glm_mat4_mulv3(ECS_CONST_CAST(vec4*, m->m), (vec3){ 2, 2.7f, 1 }, 1.0f, expect);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->x - expect[0]) < 0.01f);
    test_assert(fabsf(p->y - expect[1]) < 0.01f);
    test_assert(fabsf(p->z - expect[2]) < 0.01f);
    test_assert(fabsf(ecs_get(world, fp, Rotation)->y - yaw - 1.0f) < 0.01f);

    ecs_fini(world);
}

void Ride_ground_stays(void) {
    // controllers/ride
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t ship = ride_ship(world);
    ecs_entity_t fp = ride_walker(world, 20, 3, 0);
    test_assert(ecs_get(world, fp, FirstPersonController)->carrier == 0);

    for (int32_t i = 1; i <= 10; i ++) {
        ecs_set(world, ship, Position, { 0, 0, 0.5f * (float)i });
        ecs_set(world, ship, Rotation, { 0, 0.1f * (float)i, 0 });
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->x - 20) < 0.001f);
    test_assert(fabsf(p->z) < 0.001f);

    ecs_fini(world);
}

void Ride_off_deck_drops_carrier(void) {
    // controllers/ride
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t ship = ride_ship(world);
    ecs_entity_t fp = ride_walker(world, 4, 4, 0);
    test_assert(ecs_get(world, fp, FirstPersonController)->carrier == ship);

    box_input_key(world, KeyD, true);
    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    box_input_key(world, KeyD, false);
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0.1f);
    }

    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_assert(c->carrier == 0);
    float x = ecs_get(world, fp, Position)->x;
    test_assert(x > 5);

    ecs_set(world, ship, Position, { 0, 0, 3 });
    ecs_progress(world, 0.1f);
    test_assert(fabsf(ecs_get(world, fp, Position)->x - x) < 0.001f);
    test_assert(fabsf(ecs_get(world, fp, Position)->z) < 0.001f);

    ecs_fini(world);
}

void Ride_bulwark_keeps_floor(void) {
    // controllers/ride
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = box_app_locate("test/assets/controller/ride_bulwark.flecs");
    test_assert(path != NULL);

    ecs_entity_t s = ecs_script(world, {
        .filename = path
    });
    test_assert(s != 0);
    ecs_os_free(path);

    ecs_entity_t ship = ecs_lookup(world, "ship");
    test_assert(ship != 0);

    ecs_entity_t fp = ride_walker(world, 1, 2.7f, 1);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_assert(c->carrier == ship);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 2.7f) < 0.01f);

    for (int32_t i = 1; i <= 10; i ++) {
        ecs_set(world, ship, Position, { 0.2f * (float)i, 0, 0 });
        ecs_set(world, ship, Rotation, { 0, 0.03f * (float)i, 0 });
        ecs_progress(world, 0.1f);
    }

    ecs_progress(world, 0.1f);
    const Position *p = ecs_get(world, fp, Position);
    float dx = p->x - 2;
    float dz = p->z;
    test_assert(fabsf(p->y - 2.7f) < 0.01f);
    test_assert(fabsf(sqrtf(dx * dx + dz * dz) - sqrtf(2)) < 0.01f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}
