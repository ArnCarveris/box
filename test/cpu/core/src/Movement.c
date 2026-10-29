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

void Movement_velocity_adds_position(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Velocity, {1, 0, 0});
    test_assert(ecs_has(world, e, Position));
    test_assert(!ecs_has(world, e, Rotation));
    test_assert(ecs_has(world, e, DynamicTransform));

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Movement_angular_velocity_adds_rotation(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AngularVelocity, {1, 0, 0});
    test_assert(ecs_has(world, e, Rotation));
    test_assert(!ecs_has(world, e, Velocity));
    test_assert(ecs_has(world, e, DynamicTransform));

    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 0);
    test_flt(r->y, 0);
    test_flt(r->z, 0);

    ecs_fini(world);
}

void Movement_velocity_keeps_position(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Velocity, {0, 0, 0});

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);

    ecs_fini(world);
}

void Movement_angular_velocity_keeps_rotation(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {4, 5, 6});
    ecs_set(world, e, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e, AngularVelocity, {0, 0, 0});

    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 0.1f);
    test_flt(r->y, 0.2f);
    test_flt(r->z, 0.3f);
    test_flt(ecs_get(world, e, Position)->x, 4);

    ecs_fini(world);
}

void Movement_velocity_only_moves(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Velocity, {2, -1, 4});

    ecs_progress(world, 0.5f);

    const Position *p = ecs_get(world, e, Position);
    test_assert(p != NULL);
    test_flt(p->x, 1);
    test_flt(p->y, -0.5f);
    test_flt(p->z, 2);
    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 1);
    test_flt(ecs_get(world, e, WorldTransform)->m[3][2], 2);

    ecs_fini(world);
}

void Movement_angular_velocity_only_rotates(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AngularVelocity, {1, 2, -2});

    ecs_progress(world, 0.25f);

    const Rotation *r = ecs_get(world, e, Rotation);
    test_assert(r != NULL);
    test_flt(r->x, 0.25f);
    test_flt(r->y, 0.5f);
    test_flt(r->z, -0.5f);

    ecs_fini(world);
}

void Movement_velocity_from_script(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.movement\n"
            "mover {\n"
            "  Velocity: {2, 0, 0}\n"
            "}\n"
            "placed {\n"
            "  Position: {5, 6, 7}\n"
            "  Velocity: {0, 0, 0}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t mover = ecs_lookup(world, "mover");
    ecs_entity_t placed = ecs_lookup(world, "placed");
    test_assert(mover != 0);
    test_assert(placed != 0);
    test_assert(ecs_has(world, mover, Position));
    test_assert(!ecs_has(world, mover, Rotation));
    test_flt(ecs_get(world, placed, Position)->x, 5);
    test_flt(ecs_get(world, placed, Position)->y, 6);
    test_flt(ecs_get(world, placed, Position)->z, 7);

    ecs_progress(world, 1.0f);

    test_flt(ecs_get(world, mover, Position)->x, 2);
    test_flt(ecs_get(world, mover, WorldTransform)->m[3][0], 2);
    test_flt(ecs_get(world, placed, Position)->x, 5);

    ecs_fini(world);
}

void Movement_angular_velocity_from_script(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.movement\n"
            "spinner {\n"
            "  AngularVelocity: {0, 1, 0}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t e = ecs_lookup(world, "spinner");
    test_assert(e != 0);
    test_assert(ecs_has(world, e, Rotation));

    ecs_progress(world, 0.5f);

    test_flt(ecs_get(world, e, Rotation)->y, 0.5f);

    ecs_fini(world);
}

void Movement_velocity_prefab_instance(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.movement\n"
            "prefab Mover {\n"
            "  Velocity: {1, 0, 0}\n"
            "  AngularVelocity: {0, 0, 2}\n"
            "}\n"
            "a : Mover {}\n"
            "b : Mover {\n"
            "  Position: {10, 0, 0}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t a = ecs_lookup(world, "a");
    ecs_entity_t b = ecs_lookup(world, "b");
    test_assert(a != 0);
    test_assert(b != 0);
    test_assert(ecs_owns(world, a, Position));
    test_assert(ecs_owns(world, a, Rotation));
    test_assert(ecs_owns(world, b, Position));

    ecs_progress(world, 1.0f);

    test_flt(ecs_get(world, a, Position)->x, 1);
    test_flt(ecs_get(world, a, Rotation)->z, 2);
    test_flt(ecs_get(world, b, Position)->x, 11);

    ecs_fini(world);
}
