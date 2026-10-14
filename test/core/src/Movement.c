/**
 * @file Movement.c
 * @brief Tests for Velocity and AngularVelocity moving entities.
 */

#include <box.h>
#include <bake_test.h>

void Movement_linear(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Velocity, {2, -4, 8});
    test_assert(ecs_has(world, e, DynamicTransform));

    for (int i = 0; i < 10; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 11);
    test_flt(p->y, -18);
    test_flt(p->z, 43);
    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 11);
    test_flt(ecs_get(world, e, WorldTransform)->m[3][1], -18);

    ecs_fini(world);
}

void Movement_angular(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {0.5f, 0, 0});
    ecs_set(world, e, AngularVelocity, {1, 2, -2});
    test_assert(ecs_has(world, e, DynamicTransform));

    for (int i = 0; i < 8; i ++) {
        ecs_progress(world, 0.25f);
    }

    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 2.5f);
    test_flt(r->y, 4);
    test_flt(r->z, -4);

    ecs_fini(world);
}

void Movement_zero_velocity(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e, Velocity, {0, 0, 0});
    ecs_set(world, e, AngularVelocity, {0, 0, 0});

    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 1.0f);
    }

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);
    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 0.1f);
    test_flt(r->y, 0.2f);
    test_flt(r->z, 0.3f);

    ecs_fini(world);
}

void Movement_without_velocity(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_add(world, e, DynamicTransform);

    ecs_progress(world, 1.0f);

    test_flt(ecs_get(world, e, Position)->x, 1);

    ecs_fini(world);
}

void Movement_child_follows_parent(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.movement\n"
            "mover {\n"
            "  Position: {0, 0, 0}\n"
            "  Velocity: {2, 0, 0}\n"
            "  child {\n"
            "    Position: {1, 0, 0}\n"
            "  }\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t parent = ecs_lookup(world, "mover");
    ecs_entity_t child = ecs_lookup(world, "mover.child");
    test_assert(parent != 0);
    test_assert(child != 0);

    for (int i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    test_flt(ecs_get(world, child, Position)->x, 1);
    test_flt(ecs_get(world, parent, WorldTransform)->m[3][0], 4);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 5);

    ecs_fini(world);
}

void Movement_moving_child(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_set(world, parent, Velocity, {1, 0, 0});

    ecs_entity_t child = ecs_new(world);
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Velocity, {0, 3, 0});

    for (int i = 0; i < 2; i ++) {
        ecs_progress(world, 1.0f);
    }

    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 12);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][1], 6);

    ecs_fini(world);
}
