/**
 * @file Collision.c
 * @brief Controllers colliding with the shapes of their scene, driven by synthetic input.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Collision_wall_stops(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -3 });
    ecs_set(world, wall, Box, { 4, 2, 0.4f });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z > -2.52f);
    test_assert(p->z < -2.45f);
    test_flt(p->x, 0);
    test_flt(p->y, 1.7f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_assert(fabsf(c->velocity[2]) < 0.01f);
    test_bool(c->grounded, true);

    ecs_fini(world);
}

void Collision_slide_along_wall(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -3 });
    ecs_set(world, wall, Box, { 40, 2, 0.4f });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    box_input_key(world, KeyD, true);
    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z > -2.52f);
    test_assert(p->z < -2.45f);
    test_assert(p->x > 5.6f);
    test_assert(p->x < 5.7f);

    ecs_fini(world);
}

void Collision_slide_along_rotated_wall(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float angle = 0.6f;
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -3 });
    ecs_set(world, wall, Rotation, { 0, angle, 0 });
    ecs_set(world, wall, Box, { 40, 2, 0.4f });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    const WorldTransform *t = ecs_get(world, wall, WorldTransform);
    test_assert(t != NULL);
    float nx = t->m[2][0], nz = t->m[2][2];
    test_assert(fabsf(nx) > 0.5f);
    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 30; i ++) {
        ecs_progress(world, 0.1f);
        const Position *p = ecs_get(world, fp, Position);
        float d = p->x * nx + (p->z + 3) * nz;
        test_assert(d > 0.49f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->x) > 2);
    test_flt(p->y, 1.7f);

    ecs_fini(world);
}

void Collision_land_on_box(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, { 0, 0.5f, 0 });
    ecs_set(world, box, Box, { 2, 1, 2 });
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 4, 0 });
    ecs_add(world, fp, FirstPersonController);

    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->x, 0);
    test_assert(fabsf(p->y - 2.7f) < 0.001f);
    test_flt(p->z, 0);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_flt(c->velocity[1], 0);

    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(fabsf(ecs_get(world, fp, Position)->y - 2.7f) < 0.001f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_walk_off_box(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, { 0, 0.5f, 0 });
    ecs_set(world, box, Box, { 2, 1, 2 });
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 2.7f, 0 });
    ecs_add(world, fp, FirstPersonController);
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(fabsf(ecs_get(world, fp, Position)->y - 2.7f) < 0.001f);

    box_input_key_press(world, KeyW, 10);
    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -3.9f);
    test_flt(p->y, 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_step_up(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t step = ecs_new(world);
    ecs_set(world, step, Position, { 0, 0.1f, -2 });
    ecs_set(world, step, Box, { 2, 0.2f, 2 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key_press(world, KeyW, 6);
    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -2.3f);
    test_assert(fabsf(p->y - 1.9f) < 0.001f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_step_too_high(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t step = ecs_new(world);
    ecs_set(world, step, Position, { 0, 0.3f, -2 });
    ecs_set(world, step, Box, { 2, 0.6f, 2 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key_press(world, KeyW, 6);
    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z > -0.72f);
    test_assert(p->z < -0.65f);
    test_flt(p->y, 1.7f);

    ecs_fini(world);
}

void Collision_step_setting(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t step = ecs_new(world);
    ecs_set(world, step, Position, { 0, 0.25f, -2 });
    ecs_set(world, step, Box, { 2, 0.5f, 2 });
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .step = 0.6f });
    ecs_progress(world, 0.1f);

    box_input_key_press(world, KeyW, 6);
    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -2.3f);
    test_assert(fabsf(p->y - 2.2f) < 0.001f);

    ecs_fini(world);
}

void Collision_crouch_under_ceiling(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t ceiling = ecs_new(world);
    ecs_set(world, ceiling, Position, { 0, 1.6f, -3 });
    ecs_set(world, ceiling, Box, { 4, 0.4f, 2 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyCtrlLeft, true);
    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 15; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->z + 3) < 0.01f);
    test_flt(p->y, 0.9f);

    box_input_key(world, KeyW, false);
    box_input_key(world, KeyCtrlLeft, false);
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0.1f);
    }

    p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->z + 3) < 0.01f);
    test_flt(p->y, 0.9f);
    test_bool(ecs_get(world, fp, FirstPersonController)->crouched, true);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 15; i ++) {
        ecs_progress(world, 0.1f);
    }

    p = ecs_get(world, fp, Position);
    test_assert(p->z < -4.3f);
    test_flt(p->y, 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->crouched, false);

    ecs_fini(world);
}

void Collision_ceiling_blocks_standing(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t ceiling = ecs_new(world);
    ecs_set(world, ceiling, Position, { 0, 1.6f, -3 });
    ecs_set(world, ceiling, Box, { 4, 0.4f, 2 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z > -1.75f);
    test_assert(p->z < -1.65f);
    test_flt(p->y, 1.7f);

    ecs_fini(world);
}

void Collision_jump_hits_ceiling(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t ceiling = ecs_new(world);
    ecs_set(world, ceiling, Position, { 0, 2.2f, 0 });
    ecs_set(world, ceiling, Box, { 4, 0.4f, 4 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key_press(world, KeySpace, 1);
    float top = 0;
    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
        const Position *p = ecs_get(world, fp, Position);
        top = p->y > top ? p->y : top;
    }

    test_assert(top > 1.75f);
    test_assert(top < 1.9f);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_round_shapes(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t sphere = ecs_new(world);
    ecs_set(world, sphere, Position, { 0, 1, -4 });
    ecs_set(world, sphere, Sphere, { .radius = 1 });
    ecs_entity_t cylinder = ecs_new(world);
    ecs_set(world, cylinder, Position, { 10, 1, -4 });
    ecs_set(world, cylinder, Cylinder, { .radius = 1, .length = 2 });
    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, FirstPersonController);
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, { 10, 0, 0 });
    ecs_add(world, b, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, a, Position);
    test_assert(fabsf(p->z + 2.7f) < 0.03f);
    test_assert(fabsf(p->x) < 0.01f);
    p = ecs_get(world, b, Position);
    test_assert(fabsf(p->z + 2.7f) < 0.03f);
    test_assert(fabsf(p->x - 10) < 0.01f);

    ecs_fini(world);
}

void Collision_round_shapes_slide_around(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t sphere = ecs_new(world);
    ecs_set(world, sphere, Position, { 0, 1, -4 });
    ecs_set(world, sphere, Sphere, { .radius = 1 });
    ecs_entity_t cylinder = ecs_new(world);
    ecs_set(world, cylinder, Position, { 10, 1, -4 });
    ecs_set(world, cylinder, Cylinder, { .radius = 1, .length = 2 });
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, { 0.9f, 0, 0 });
    ecs_add(world, a, FirstPersonController);
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, { 10.9f, 0, 0 });
    ecs_add(world, b, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 30; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(ecs_get(world, a, Position)->z < -6);
    test_assert(ecs_get(world, b, Position)->z < -6);

    ecs_fini(world);
}

void Collision_scene_scoping(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, { 0, 1, -3 });
    ecs_set(world, wall, Box, { 40, 2, 0.4f });
    ecs_entity_t outside = ecs_new(world);
    ecs_add(world, outside, FirstPersonController);
    ecs_entity_t inside = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, inside, Position, { 5, 0, 0 });
    ecs_add(world, inside, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(ecs_get(world, outside, Position)->z < -7.9f);
    test_assert(ecs_get(world, inside, Position)->z > -2.52f);

    ecs_fini(world);
}

void Collision_own_shape_ignored(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Box, { 0.6f, 1.8f, 0.6f });
    ecs_add(world, fp, FirstPersonController);
    ecs_entity_t hand = ecs_new_w_pair(world, EcsChildOf, fp);
    ecs_set(world, hand, Position, { 0, 0, -0.5f });
    ecs_set(world, hand, Box, { 0.2f, 0.2f, 0.4f });
    ecs_progress(world, 0.1f);

    box_input_key_press(world, KeyW, 5);
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->z + 2) < 0.001f);
    test_flt(p->y, 1.7f);

    ecs_fini(world);
}

void Collision_ground_below_plane(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, { 0, -3, 0 });
    ecs_set(world, floor, Box, { 4, 2, 4 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, { 10, 0, 0 });
    ecs_add(world, other, FirstPersonController);

    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(fabsf(ecs_get(world, fp, Position)->y + 0.3f) < 0.001f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);
    test_flt(ecs_get(world, other, Position)->y, 1.7f);
    test_bool(ecs_get(world, other, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_camera_collide(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -3 });
    ecs_set(world, wall, Box, { 40, 2, 0.4f });
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0, 1, 0 });
    ecs_set(world, cam, CameraController, { .collide = true });
    ecs_entity_t ghost = ecs_new(world);
    ecs_set(world, ghost, Position, { 5, 1, 0 });
    ecs_add(world, ghost, CameraController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, cam, Position);
    test_assert(p->z > -2.57f);
    test_assert(p->z < -2.5f);
    test_assert(fabsf(p->y - 1) < 0.001f);
    test_assert(fabsf(ecs_get(world, cam, CameraController)->velocity[2]) < 0.01f);
    test_flt(ecs_get(world, ghost, Position)->z, -10);

    ecs_fini(world);
}

void Collision_camera_radius(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -3 });
    ecs_set(world, wall, Box, { 40, 2, 0.4f });
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0, 1, 0 });
    ecs_set(world, cam, CameraController, { .collide = true, .radius = 1 });
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Position *p = ecs_get(world, cam, Position);
    test_assert(p->z > -1.82f);
    test_assert(p->z < -1.75f);

    ecs_fini(world);
}

void Collision_script_settings(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.controllers\n"
            "using box.transform\n"
            "using box.geometry\n"
            "wall {\n"
            "  Position: {0, 1, -3}\n"
            "  Box: {40, 2, 0.4}\n"
            "}\n"
            "player {\n"
            "  FirstPersonController: {radius: 0.5, step: 0.4}\n"
            "}\n"
            "cam {\n"
            "  Position: {5, 1, 0}\n"
            "  CameraController: {collide: true, radius: 0.5}\n"
            "}\n"
    });
    test_assert(s != 0);
    ecs_entity_t fp = ecs_lookup(world, "player");
    ecs_entity_t cam = ecs_lookup(world, "cam");
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_flt(c->radius, 0.5f);
    test_flt(c->step, 0.4f);
    test_bool(ecs_get(world, cam, CameraController)->collide, true);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(fabsf(ecs_get(world, fp, Position)->z + 2.3f) < 0.02f);
    test_assert(fabsf(ecs_get(world, cam, Position)->z + 2.3f) < 0.02f);

    ecs_fini(world);
}
