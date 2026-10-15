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
    ecs_add(world, fp, FirstPersonController);
    ecs_get_mut(world, fp, FirstPersonController)->step = 0.6f;
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
    ecs_set(world, sphere, Sphere, { .segments = 24, .radius = 1 });
    ecs_entity_t cylinder = ecs_new(world);
    ecs_set(world, cylinder, Position, { 10, 1, -4 });
    ecs_set(world, cylinder, Cylinder, { .segments = 24, .radius = 1, .length = 2 });
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
    ecs_set(world, sphere, Sphere, { .segments = 24, .radius = 1 });
    ecs_entity_t cylinder = ecs_new(world);
    ecs_set(world, cylinder, Position, { 10, 1, -4 });
    ecs_set(world, cylinder, Cylinder, { .segments = 24, .radius = 1, .length = 2 });
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
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->collide = true;
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
    float z = 0, v = 0;
    for (int32_t i = 0; i < 10; i ++) {
        v += (40 - v) * (1.0f - expf(-0.6f));
        z -= v * 0.1f;
    }

    test_assert(fabsf(ecs_get(world, ghost, Position)->z - z) < 0.001f);

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
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->collide = true;
    ecs_get_mut(world, cam, CameraController)->radius = 1;
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

void Collision_camera_thin_wall_boost(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -20 });
    ecs_set(world, wall, Box, { 40, 40, 0.05f });
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0, 1, 0 });
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->collide = true;
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    box_input_key(world, KeyShiftLeft, true);
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0.5f);
        test_assert(ecs_get(world, cam, Position)->z > -19.73f);
    }

    const Position *p = ecs_get(world, cam, Position);
    test_assert(p->z < -19.7f);
    test_assert(fabsf(p->x) < 0.001f);
    test_assert(fabsf(p->y - 1) < 0.001f);
    test_assert(fabsf(ecs_get(world, cam, CameraController)->velocity[2]) < 0.01f);

    ecs_fini(world);
}

void Collision_first_person_thin_wall_boost(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -20 });
    ecs_set(world, wall, Box, { 40, 40, 0.05f });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_get_mut(world, fp, FirstPersonController)->fly_speed = 100;
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyF, true);
    ecs_progress(world, 0.1f);
    test_bool(ecs_get(world, fp, FirstPersonController)->flying, true);
    box_input_key(world, KeyF, false);
    box_input_key(world, KeyW, true);
    box_input_key(world, KeyShiftLeft, true);
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0.5f);
        test_assert(ecs_get(world, fp, Position)->z > -19.68f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -19.65f);
    test_assert(fabsf(p->x) < 0.001f);
    test_assert(fabsf(ecs_get(world, fp, FirstPersonController)->velocity[2]) < 0.01f);

    ecs_fini(world);
}

void Collision_first_person_thin_wall_walk(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -3 });
    ecs_set(world, wall, Box, { 40, 2, 0.05f });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_get_mut(world, fp, FirstPersonController)->speed = 100;
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0.5f);
        test_assert(ecs_get(world, fp, Position)->z > -2.68f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -2.65f);
    test_flt(p->y, 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_steep_slope_blocks_walk(void) {
    // controller_fixes/steep_slopes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float a = 60 * (float)GLM_PI / 180, s = sinf(a), c = cosf(a);
    ecs_entity_t slope = ecs_new(world);
    ecs_set(world, slope, Position, { 0, -0.5f * c + 5 * s, -2 - 0.5f * s - 5 * c });
    ecs_set(world, slope, Rotation, { a, 0, 0 });
    ecs_set(world, slope, Box, { 10, 1, 10 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    const WorldTransform *t = ecs_get(world, slope, WorldTransform);
    test_assert(fabsf(t->m[1][1] - c) < 0.001f);
    test_assert(fabsf(t->m[1][2] - s) < 0.001f);
    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 180; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        test_assert(ecs_get(world, fp, Position)->y < 1.75f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z > -2.1f);
    test_assert(p->z < -1.5f);

    ecs_fini(world);
}

void Collision_steep_slope_blocks_jump(void) {
    // controller_fixes/steep_slopes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float a = 60 * (float)GLM_PI / 180, s = sinf(a), c = cosf(a);
    ecs_entity_t slope = ecs_new(world);
    ecs_set(world, slope, Position, { 0, -0.5f * c + 5 * s, -2 - 0.5f * s - 5 * c });
    ecs_set(world, slope, Rotation, { a, 0, 0 });
    ecs_set(world, slope, Box, { 10, 1, 10 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    float top = 0;
    for (int32_t i = 0; i < 300; i ++) {
        box_input_key(world, KeySpace, (i % 2) == 0);
        ecs_progress(world, 1.0f / 60.0f);
        float y = ecs_get(world, fp, Position)->y;
        top = y > top ? y : top;
    }

    test_assert(top > 2.0f);
    test_assert(top < 1.7f + 0.75f);
    box_input_key(world, KeyW, false);
    box_input_key(world, KeySpace, false);
    for (int32_t i = 0; i < 120; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->y - 1.7f) < 0.001f);
    test_assert(p->z > -2.1f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_walkable_slope_climbs(void) {
    // controller_fixes/steep_slopes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float a = 40 * (float)GLM_PI / 180, s = sinf(a), c = cosf(a);
    ecs_entity_t slope = ecs_new(world);
    ecs_set(world, slope, Position, { 0, -0.5f * c + 5 * s, -2 - 0.5f * s - 5 * c });
    ecs_set(world, slope, Rotation, { a, 0, 0 });
    ecs_set(world, slope, Box, { 10, 1, 10 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 120; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    const Position *p = ecs_get(world, fp, Position);
    float feet = p->y - 1.7f;
    float surface = (-2 - p->z) * s / c;
    test_assert(p->z < -6);
    test_assert(feet > 3);
    test_assert(fabsf(feet - surface) < 0.3f / c);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_stairs_climb(void) {
    // controller_fixes/steep_slopes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    for (int32_t i = 0; i < 4; i ++) {
        float h = 0.25f * (float)(i + 1);
        ecs_entity_t stair = ecs_new(world);
        ecs_set(world, stair, Position, { 0, h * 0.5f, -2 - 0.5f * (float)i - 5 });
        ecs_set(world, stair, Box, { 2, h, 10 });
    }

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 90; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -5);
    test_assert(fabsf(p->y - 2.7f) < 0.001f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_camera_rotated_thin_wall(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -20 });
    ecs_set(world, wall, Rotation, { 0, 0.6f, 0 });
    ecs_set(world, wall, Box, { 2000, 40, 0.05f });
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0, 1, 0 });
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->collide = true;
    ecs_progress(world, 0.1f);

    const WorldTransform *t = ecs_get(world, wall, WorldTransform);
    float nx = t->m[2][0], nz = t->m[2][2];
    test_assert(fabsf(nx) > 0.5f);
    test_assert(nz > 0);
    box_input_key(world, KeyW, true);
    box_input_key(world, KeyShiftLeft, true);
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0.5f);
        const Position *p = ecs_get(world, cam, Position);
        test_assert(p->x * nx + (p->z + 20) * nz > 0.27f);
    }

    const Position *p = ecs_get(world, cam, Position);
    test_assert(p->x * nx + (p->z + 20) * nz < 0.3f);
    test_assert(fabsf(p->x) > 20);
    test_assert(fabsf(p->y - 1) < 0.001f);

    ecs_fini(world);
}

void Collision_first_person_rotated_thin_wall(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -10 });
    ecs_set(world, wall, Rotation, { 0, 0.6f, 0 });
    ecs_set(world, wall, Box, { 2000, 2, 0.05f });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_get_mut(world, fp, FirstPersonController)->speed = 100;
    ecs_progress(world, 0.1f);

    const WorldTransform *t = ecs_get(world, wall, WorldTransform);
    float nx = t->m[2][0], nz = t->m[2][2];
    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0.5f);
        const Position *p = ecs_get(world, fp, Position);
        test_assert(p->x * nx + (p->z + 10) * nz > 0.32f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->x * nx + (p->z + 10) * nz < 0.35f);
    test_assert(fabsf(p->x) > 20);
    test_flt(p->y, 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_step_up_long_frame(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t step = ecs_new(world);
    ecs_set(world, step, Position, { 0, 0.1f, -13 });
    ecs_set(world, step, Box, { 4, 0.2f, 20 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
        test_assert(ecs_get(world, fp, Position)->y > 1.69f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -5);
    test_assert(fabsf(p->y - 1.9f) < 0.001f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_camera_huge_speed(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -20 });
    ecs_set(world, wall, Box, { 40, 40, 0.05f });
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0, 1, 0 });
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->collide = true;
    cc->speed = 1e12f;
    cc->acceleration = 1e9f;
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_assert(p->z > -19.73f);
    test_assert(p->z < -19.7f);

    ecs_fini(world);
}

void Collision_camera_infinite_velocity(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 1, -20 });
    ecs_set(world, wall, Box, { 40, 40, 0.05f });
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0, 1, 0 });
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->collide = true;
    cc->speed = INFINITY;
    cc->acceleration = 1e9f;
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 1);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Collision_steep_crease_holds(void) {
    // controller_fixes/steep_slopes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float a = 60 * (float)GLM_PI / 180, s = sinf(a), c = cosf(a);
    ecs_entity_t left = ecs_new(world);
    ecs_set(world, left, Position, { 0, 3 - 0.5f * c + 5 * s, -2 - 0.5f * s - 5 * c });
    ecs_set(world, left, Rotation, { a, 0, 0 });
    ecs_set(world, left, Box, { 10, 1, 10 });
    ecs_entity_t right = ecs_new(world);
    ecs_set(world, right, Position, { 0, 3 - 0.5f * c + 5 * s, -2 + 0.5f * s + 5 * c });
    ecs_set(world, right, Rotation, { -a, 0, 0 });
    ecs_set(world, right, Box, { 10, 1, 10 });
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 6, -2 });
    ecs_add(world, fp, FirstPersonController);

    for (int32_t i = 0; i < 120; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        test_assert(ecs_get(world, fp, Position)->y > 4.95f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->y - (3 + 0.3f / c - 0.3f + 1.7f)) < 0.01f);
    test_assert(fabsf(p->z + 2) < 0.01f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);
    test_assert(fabsf(ecs_get(world, fp, FirstPersonController)->velocity[1]) < 0.001f);

    ecs_fini(world);
}

void Collision_steep_slope_wall_crease_holds(void) {
    // controller_fixes/steep_slopes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float a = 60 * (float)GLM_PI / 180, s = sinf(a), c = cosf(a);
    ecs_entity_t slope = ecs_new(world);
    ecs_set(world, slope, Position, { 0, 3 - 0.5f * c + 5 * s, -2 - 0.5f * s - 5 * c });
    ecs_set(world, slope, Rotation, { a, 0, 0 });
    ecs_set(world, slope, Box, { 10, 1, 10 });
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, { 0, 4, -1.5f });
    ecs_set(world, wall, Box, { 10, 8, 1 });
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 7, -2.4f });
    ecs_add(world, fp, FirstPersonController);

    for (int32_t i = 0; i < 120; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        test_assert(ecs_get(world, fp, Position)->y > 5.4f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->y < 5.6f);
    test_assert(fabsf(p->z + 2.3f) < 0.01f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);
    test_assert(fabsf(ecs_get(world, fp, FirstPersonController)->velocity[1]) < 0.001f);

    ecs_fini(world);
}

void Collision_steep_crease_walk_along(void) {
    // controller_fixes/steep_slopes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float a = 60 * (float)GLM_PI / 180, s = sinf(a), c = cosf(a);
    ecs_entity_t left = ecs_new(world);
    ecs_set(world, left, Position, { 0, 3 - 0.5f * c + 5 * s, -2 - 0.5f * s - 5 * c });
    ecs_set(world, left, Rotation, { a, 0, 0 });
    ecs_set(world, left, Box, { 20, 1, 10 });
    ecs_entity_t right = ecs_new(world);
    ecs_set(world, right, Position, { 0, 3 - 0.5f * c + 5 * s, -2 + 0.5f * s + 5 * c });
    ecs_set(world, right, Rotation, { -a, 0, 0 });
    ecs_set(world, right, Box, { 20, 1, 10 });
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 6, -2 });
    ecs_add(world, fp, FirstPersonController);
    for (int32_t i = 0; i < 60; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    box_input_key(world, KeyD, true);
    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 60; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        test_assert(ecs_get(world, fp, Position)->y > 4.95f);
        test_assert(ecs_get(world, fp, Position)->y < 5.3f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->x > 2);
    test_assert(fabsf(p->z + 2) < 0.3f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_ball_through_axis(void) {
    // controller_fixes/narrow_phase
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float dt = 1.0f / 60.0f;
    ecs_entity_t ball = ecs_new(world);
    ecs_set(world, ball, Position, { 0, 50, 0 });
    ecs_set(world, ball, Sphere, { .segments = 24, .radius = 0.4f });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, dt);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 30; i ++) {
        ecs_progress(world, dt);
    }

    float z = ecs_get(world, fp, Position)->z - 4 * dt - 4 * dt;
    ecs_set(world, ball, Position, { 0, 1, z });
    ecs_progress(world, dt);
    for (int32_t i = 0; i < 30; i ++) {
        ecs_progress(world, dt);
        const Position *p = ecs_get(world, fp, Position);
        test_assert(fabsf(p->y - 1.7f) < 0.001f);
        test_assert(p->z > z + 0.69f);
        test_assert(fabsf(p->x) < 0.001f);
    }

    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_walk_into_balls(void) {
    // controller_fixes/narrow_phase
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float radius[] = { 0.4f, 0.6f, 1.0f, 0.4f };
    float y[] = { 0.4f, 0.6f, 1.0f, 1.2f };
    ecs_entity_t fp[4];
    for (int32_t i = 0; i < 4; i ++) {
        ecs_entity_t ball = ecs_new(world);
        ecs_set(world, ball, Position, { 10.0f * (float)i, y[i], -3 });
        ecs_set(world, ball, Sphere, { .segments = 24, .radius = radius[i] });
        fp[i] = ecs_new(world);
        ecs_set(world, fp[i], Position, { 10.0f * (float)i, 1.7f, 0 });
        ecs_add(world, fp[i], FirstPersonController);
    }

    ecs_progress(world, 1.0f / 60.0f);
    box_input_key(world, KeyW, true);
    for (int32_t f = 0; f < 120; f ++) {
        ecs_progress(world, 1.0f / 60.0f);
        for (int32_t i = 0; i < 4; i ++) {
            test_assert(fabsf(ecs_get(world, fp[i], Position)->y - 1.7f) < 0.001f);
        }
    }

    for (int32_t i = 0; i < 4; i ++) {
        const Position *p = ecs_get(world, fp[i], Position);
        float dy = y[i] < 0.3f ? 0.3f - y[i] : (y[i] > 1.55f ? y[i] - 1.55f : 0);
        float reach = radius[i] + 0.3f;
        float dz = p->z + 3;
        test_assert(fabsf(sqrtf(dz * dz + dy * dy) - reach) < 0.02f);
        test_assert(fabsf(p->x - 10.0f * (float)i) < 0.001f);
    }

    ecs_fini(world);
}

void Collision_ground_beside_round_shapes(void) {
    // controller_fixes/narrow_phase
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pillar = ecs_new(world);
    ecs_set(world, pillar, Position, { 0, 2, 0 });
    ecs_set(world, pillar, Cylinder, { .segments = 24, .radius = 2, .length = 4 });
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, { 1.75f, 1.7f, 1.75f });
    ecs_add(world, a, FirstPersonController);
    ecs_entity_t log = ecs_new(world);
    ecs_set(world, log, Position, { 20, 2, 0 });
    ecs_set(world, log, Rotation, { 0, 0, (float)GLM_PI_2 });
    ecs_set(world, log, Capsule, { .segments = 24, .radius = 2, .length = 2 });
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, { 22.75f, 1.7f, 1.75f });
    ecs_add(world, b, FirstPersonController);

    for (int32_t i = 0; i < 60; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        test_assert(fabsf(ecs_get(world, a, Position)->y - 1.7f) < 0.001f);
        test_assert(fabsf(ecs_get(world, b, Position)->y - 1.7f) < 0.001f);
    }

    test_bool(ecs_get(world, a, FirstPersonController)->grounded, true);
    test_bool(ecs_get(world, b, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_step_over_log(void) {
    // controller_fixes/narrow_phase
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t log = ecs_new(world);
    ecs_set(world, log, Position, { 0, 0.14f, -2 });
    ecs_set(world, log, Rotation, { 0, 0, (float)GLM_PI_2 });
    ecs_set(world, log, Capsule, { .segments = 24, .radius = 0.14f, .length = 4 });
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 1.0f / 60.0f);

    box_input_key(world, KeyW, true);
    float top = 0;
    for (int32_t i = 0; i < 90; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        const Position *p = ecs_get(world, fp, Position);
        float dz = fabsf(p->z + 2);
        test_assert(p->y > 1.7f - 0.001f);
        if (dz < 0.1f) {
            float surface = 0.14f + sqrtf(0.44f * 0.44f - dz * dz) - 0.3f;
            test_assert(fabsf(p->y - 1.7f - surface) < 0.01f);
        }

        top = p->y > top ? p->y : top;
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(top > 1.7f + 0.27f);
    test_assert(top < 1.7f + 0.29f);
    test_assert(p->z < -4);
    test_flt(p->y, 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void Collision_sheared_box(void) {
    // controller_fixes/narrow_phase
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, { 0, 0, -3 });
    ecs_set(world, parent, Scale, { 2, 1, 1 });
    ecs_entity_t rhombus = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, rhombus, Position, { 0, 1, 0 });
    ecs_set(world, rhombus, Rotation, { 0, (float)GLM_PI_4, 0 });
    ecs_set(world, rhombus, Box, { 1, 2, 1 });
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0.2f, 1.7f, 0 });
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 1.0f / 60.0f);

    float hx = sqrtf(2), hz = sqrtf(0.5f);
    float corner[4][2] = { { hx, 0 }, { 0, hz }, { -hx, 0 }, { 0, -hz } };
    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 150; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        const Position *p = ecs_get(world, fp, Position);
        float px = p->x, pz = p->z + 3, dist = 1e9f;
        for (int32_t e = 0; e < 4; e ++) {
            float ax = corner[e][0], az = corner[e][1];
            float bx = corner[(e + 1) % 4][0], bz = corner[(e + 1) % 4][1];
            float ex = bx - ax, ez = bz - az;
            float t = ((px - ax) * ex + (pz - az) * ez) / (ex * ex + ez * ez);
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            float dx = px - ax - ex * t, dz = pz - az - ez * t;
            float d = sqrtf(dx * dx + dz * dz);
            dist = d < dist ? d : dist;
        }

        test_assert(dist > 0.29f);
        test_assert(fabsf(p->y - 1.7f) < 0.001f);
    }

    test_assert(ecs_get(world, fp, Position)->z < -5);

    ecs_fini(world);
}

void Collision_fast_move_dense_posts(void) {
    // controller_fixes/substep_colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    for (int32_t i = 0; i < 40; i ++) {
        for (int32_t j = 0; j < 40; j ++) {
            ecs_entity_t post = ecs_new(world);
            ecs_set(world, post, Position, { 1.5f * (float)i - 30, 1, -1.5f * (float)j - 3 });
            ecs_set(world, post, Box, { 0.2f, 40, 0.2f });
        }
    }

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0.75f, 1, 0 });
    ecs_set(world, cam, Rotation, { 0, 0.3f, 0 });
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->collide = true;
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    box_input_key(world, KeyShiftLeft, true);
    float travelled = 0;
    for (int32_t f = 0; f < 4; f ++) {
        Position before = *ecs_get(world, cam, Position);
        ecs_progress(world, 0.5f);
        const Position *p = ecs_get(world, cam, Position);
        travelled += fabsf(p->x - before.x) + fabsf(p->z - before.z);
        for (int32_t i = 0; i < 40; i ++) {
            for (int32_t j = 0; j < 40; j ++) {
                float dx = fabsf(p->x - (1.5f * (float)i - 30)) - 0.1f;
                float dz = fabsf(p->z + 1.5f * (float)j + 3) - 0.1f;
                dx = dx > 0 ? dx : 0;
                dz = dz > 0 ? dz : 0;
                test_assert(sqrtf(dx * dx + dz * dz) > 0.24f);
            }
        }
    }

    test_assert(travelled > 5);

    ecs_fini(world);
}

void Collision_round_obstacles_block(void) {
    // controller_fixes/narrow_phase
    float radius[] = { 0.19f, 0.22f, 0.25f, 0.28f, 0.31f, 0.34f, 0.4f };
    float fps[] = { 30, 60, 144 };
    for (int32_t f = 0; f < 3; f ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        ecs_entity_t fp[14];
        for (int32_t i = 0; i < 14; i ++) {
            float r = radius[i % 7], x = 10.0f * (float)i;
            ecs_entity_t shape = ecs_new(world);
            ecs_set(world, shape, Position, { x, r, -3 });
            if (i < 7) {
                ecs_set(world, shape, Sphere, { .segments = 24, .radius = r });
            } else {
                ecs_set(world, shape, Rotation, { 0, 0, (float)GLM_PI_2 });
                ecs_set(world, shape, Capsule, { .segments = 24, .radius = r, .length = 4 });
            }

            fp[i] = ecs_new(world);
            ecs_set(world, fp[i], Position, { x, 1.7f, 0 });
            ecs_add(world, fp[i], FirstPersonController);
        }

        ecs_progress(world, 1.0f / fps[f]);
        box_input_key(world, KeyW, true);
        for (int32_t n = 0; n < (int32_t)(4 * fps[f]); n ++) {
            ecs_progress(world, 1.0f / fps[f]);
            for (int32_t i = 0; i < 14; i ++) {
                const Position *p = ecs_get(world, fp[i], Position);
                test_assert(p->y < 1.7f + 0.001f);
                test_assert(p->z > -3);
            }
        }

        ecs_fini(world);
    }
}

void Collision_capsule_core_crossing(void) {
    // controller_fixes/narrow_phase
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 1.0f / 60.0f);

    ecs_entity_t log = ecs_new(world);
    ecs_set(world, log, Position, { -sinf((float)GLM_PI_4), 1 + cosf((float)GLM_PI_4), 0 });
    ecs_set(world, log, Rotation, { 0, 0, (float)GLM_PI_4 });
    ecs_set(world, log, Capsule, { .segments = 24, .radius = 0.2f, .length = 4 });
    ecs_progress(world, 1.0f / 60.0f);
    for (int32_t i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        const Position *p = ecs_get(world, fp, Position);
        test_assert(fabsf(p->y - 1.7f) < 0.001f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->z) > 0.49f);
    test_assert(fabsf(p->x) < 0.001f);

    ecs_fini(world);
}

void Collision_camera_on_capsule_core(void) {
    // controller_fixes/narrow_phase
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0, 1, 0 });
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->collide = true;
    cc->speed = 2;
    cc->acceleration = 1e9f;
    ecs_progress(world, 1.0f / 60.0f);

    box_input_key(world, KeyD, true);
    ecs_progress(world, 1.0f / 60.0f);
    ecs_entity_t log = ecs_new(world);
    const Position *p = ecs_get(world, cam, Position);
    ecs_set(world, log, Position, { p->x + 2.0f / 60.0f + 2.0f / 60.0f, 1, 0 });
    ecs_set(world, log, Rotation, { 0, 0, (float)GLM_PI_2 });
    ecs_set(world, log, Capsule, { .segments = 24, .radius = 0.3f, .length = 4 });
    for (int32_t i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    p = ecs_get(world, cam, Position);
    test_assert(sqrtf((p->y - 1) * (p->y - 1) + p->z * p->z) > 0.54f);

    ecs_fini(world);
}

void Collision_sheared_box_gap(void) {
    // controller_fixes/narrow_phase
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, { 0, 0, -3 });
    ecs_set(world, parent, Scale, { 2, 1, 1 });
    ecs_entity_t rhombus = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, rhombus, Position, { 0, 1, 0 });
    ecs_set(world, rhombus, Rotation, { 0, (float)GLM_PI_4, 0 });
    ecs_set(world, rhombus, Box, { 1, 2, 1 });
    float hx = sqrtf(2), hz = sqrtf(0.5f);
    float corner[4][2] = { { hx, 0 }, { 0, hz }, { -hx, 0 }, { 0, -hz } };
    float nx = -hz / sqrtf(2.5f), nz = hx / sqrtf(2.5f);
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { -hx * 0.5f + nx * 3, 1.7f, -3 + hz * 0.5f + nz * 3 });
    ecs_set(world, fp, Rotation, { 0, atan2f(nx, nz), 0 });
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 1.0f / 60.0f);

    box_input_key(world, KeyW, true);
    float closest = 1e9f, stop = -1;
    const Position *start = ecs_get(world, fp, Position);
    float along = -(start->x * nx + (start->z + 3) * nz);
    for (int32_t i = 0; i < 120; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
        const Position *p = ecs_get(world, fp, Position);
        float next = -(p->x * nx + (p->z + 3) * nz);
        bool blocked = next - along < 2.0f / 60.0f;
        along = next;
        float px = p->x, pz = p->z + 3, dist = 1e9f;
        for (int32_t e = 0; e < 4; e ++) {
            float ax = corner[e][0], az = corner[e][1];
            float bx = corner[(e + 1) % 4][0], bz = corner[(e + 1) % 4][1];
            float ex = bx - ax, ez = bz - az;
            float t = ((px - ax) * ex + (pz - az) * ez) / (ex * ex + ez * ez);
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            float dx = px - ax - ex * t, dz = pz - az - ez * t;
            float d = sqrtf(dx * dx + dz * dz);
            dist = d < dist ? d : dist;
        }

        closest = dist < closest ? dist : closest;
        stop = blocked && stop < 0 ? dist : stop;
    }

    test_assert(closest > 0.29f);
    test_assert(stop > 0.29f);
    test_assert(stop < 0.3f + 0.76f);

    ecs_fini(world);
}

void Collision_crowded_stretch_walls(void) {
    // controller_fixes/substep_colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t body[40];
    float wall[40];
    for (int32_t k = 0; k < 40; k ++) {
        bool camera = k < 20;
        float x = 1.5f * (float)k;
        float above = camera ? 1.28f : 2.0f;
        wall[k] = -(5 + 20.0f * (float)(k % 20) / 19.0f + 0.37f * (float)(k / 20));
        for (int32_t i = 0; i < 320; i ++) {
            ecs_entity_t pebble = ecs_new(world);
            ecs_set(world, pebble, Position, { x, above, 1 - 0.1f * (float)i });
            ecs_set(world, pebble, Box, { 0.05f, 0.02f, 0.05f });
        }

        ecs_entity_t w = ecs_new(world);
        ecs_set(world, w, Position, { x, 1, wall[k] });
        ecs_set(world, w, Box, { 1, 4, 0.05f });
        body[k] = ecs_new(world);
        if (camera) {
            ecs_set(world, body[k], Position, { x, 1, 0 });
            ecs_add(world, body[k], CameraController);
            CameraController *cc = ecs_get_mut(world, body[k], CameraController);
            cc->collide = true;
            cc->speed = 60;
            cc->boost = 1;
            cc->acceleration = 1e9f;
        } else {
            ecs_set(world, body[k], Position, { x, 1.7f, 0 });
            ecs_add(world, body[k], FirstPersonController);
            ecs_get_mut(world, body[k], FirstPersonController)->speed = 60;
        }
    }

    ecs_progress(world, 0.1f);
    box_input_key(world, KeyW, true);
    for (int32_t f = 0; f < 2; f ++) {
        ecs_progress(world, 0.5f);
        for (int32_t k = 0; k < 40; k ++) {
            float r = k < 20 ? 0.25f : 0.3f;
            test_assert(ecs_get(world, body[k], Position)->z > wall[k] + 0.025f + r - 0.01f);
        }
    }

    for (int32_t k = 0; k < 40; k ++) {
        float r = k < 20 ? 0.25f : 0.3f;
        test_assert(ecs_get(world, body[k], Position)->z < wall[k] + 0.025f + r + 0.01f);
    }

    ecs_fini(world);
}
