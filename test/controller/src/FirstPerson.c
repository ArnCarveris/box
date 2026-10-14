/**
 * @file FirstPerson.c
 * @brief The first person controller driven by key, cursor and action input and by commands.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void FirstPerson_import(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_lookup(world, "box.controllers.FirstPersonController") ==
        ecs_id(FirstPersonController));
    test_assert(ecs_lookup(world, "box.controllers.first_person") != 0);

    ecs_fini(world);
}

void FirstPerson_default_bindings(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *names[] = {
        "Forward", "Back", "Left", "Right", "Up", "Down", "Fast", "Look", "Jump", "Crouch", "Fly"
    };
    Key keys[] = {
        KeyW, KeyS, KeyA, KeyD, KeyE, KeyQ, KeyShiftLeft, KeyMouseRight, KeySpace, KeyCtrlLeft, KeyF
    };
    ecs_entity_t scope = ecs_lookup(world, "box.controllers.first_person");
    for (int32_t i = 0; i < 11; i ++) {
        ecs_entity_t action = ecs_lookup_child(world, scope, names[i]);
        test_assert(action != 0);
        const Action *a = ecs_get(world, action, Action);
        test_assert(a != NULL);
        test_assert(a->description != NULL);
        test_assert(ecs_has(world, action, ActionState));
        const Binding *b = ecs_get(world, action, Binding);
        test_assert(b != NULL);
        test_int(b->key, keys[i]);
        test_uint(b->action, 0);
    }

    ecs_fini(world);
}

void FirstPerson_default_actions_disabled(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_lookup(world, "box.controllers.first_person.Jump");
    ecs_entity_t camera_up = ecs_lookup(world, "box.controllers.camera.Up");
    test_assert(jump != 0);
    test_assert(ecs_has_id(world, jump, EcsDisabled));

    box_input_key(world, KeySpace, true);
    ecs_progress(world, 0.5f);
    test_bool(ecs_get(world, jump, ActionState)->down, false);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.5f);
    test_assert(!ecs_has_id(world, jump, EcsDisabled));
    test_assert(ecs_has_id(world, camera_up, EcsDisabled));
    test_bool(ecs_get(world, jump, ActionState)->down, true);

    ecs_delete(world, fp);
    ecs_progress(world, 0.5f);
    test_assert(ecs_has_id(world, jump, EcsDisabled));
    test_bool(ecs_get(world, jump, ActionState)->down, false);

    ecs_fini(world);
}

void FirstPerson_adds_position_rotation(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    test_assert(ecs_has(world, fp, Position));
    test_assert(ecs_has(world, fp, Rotation));
    test_assert(ecs_has(world, fp, WorldTransform));

    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_flt(c->speed, 0);
    test_flt(c->ground, 0);
    test_flt(c->height, 0);
    test_bool(c->grounded, false);
    test_bool(c->flying, false);
    test_uint(c->actions, 0);

    ecs_fini(world);
}

void FirstPerson_spawn_on_ground(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 1, 0, 3 });
    ecs_add(world, fp, FirstPersonController);

    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 1.7f);
    test_flt(p->z, 3);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_flt(c->height, 1.7f);
    test_flt(c->velocity[1], 0);

    ecs_progress(world, 0.5f);
    p = ecs_get(world, fp, Position);
    test_flt(p->y, 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void FirstPerson_idle(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 1, 1.7f, 3 });
    ecs_set(world, fp, Rotation, { 0.1f, 0.2f, 0 });
    ecs_add(world, fp, FirstPersonController);

    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 1.7f);
    test_flt(p->z, 3);
    const Rotation *r = ecs_get(world, fp, Rotation);
    test_flt(r->x, 0.1f);
    test_flt(r->y, 0.2f);

    ecs_fini(world);
}

void FirstPerson_walk_forward(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);

    box_input_key_press(world, KeyW, 2);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 1.7f);
    test_flt(p->z, -2);

    ecs_progress(world, 0.5f);
    p = ecs_get(world, fp, Position);
    test_flt(p->z, -4);

    ecs_progress(world, 0.5f);
    p = ecs_get(world, fp, Position);
    test_flt(p->z, -4);
    test_flt(ecs_get(world, fp, FirstPersonController)->velocity[2], 0);

    ecs_fini(world);
}

void FirstPerson_walk_back_left_right(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);

    box_input_key_press(world, KeyS, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->z, 2);

    box_input_key_press(world, KeyA, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->x, -2);

    box_input_key_press(world, KeyD, 2);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->x, 2);
    test_flt(p->y, 1.7f);
    test_flt(p->z, 2);

    ecs_fini(world);
}

void FirstPerson_walk_diagonal(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);

    box_input_key(world, KeyW, true);
    box_input_key(world, KeyD, true);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->x - 2.0f / sqrtf(2.0f)) < 0.0001f);
    test_flt(p->y, 1.7f);
    test_assert(fabsf(p->z + 2.0f / sqrtf(2.0f)) < 0.0001f);

    ecs_fini(world);
}

void FirstPerson_walk_follows_yaw(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Rotation, { 0, (float)GLM_PI_2, 0 });
    ecs_add(world, fp, FirstPersonController);

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->x + 2) < 0.0001f);
    test_assert(fabsf(p->z) < 0.0001f);

    box_input_key_press(world, KeyD, 1);
    ecs_progress(world, 0.5f);
    p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->x + 2) < 0.0001f);
    test_assert(fabsf(p->z + 2) < 0.0001f);

    ecs_fini(world);
}

void FirstPerson_walk_ignores_pitch(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Rotation, { 1.0f, 0, 0 });
    ecs_add(world, fp, FirstPersonController);

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 1.7f);
    test_flt(p->z, -2);
    test_flt(ecs_get(world, fp, Rotation)->x, 1.0f);

    ecs_fini(world);
}

void FirstPerson_walk_ignores_up_down(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);

    box_input_key_press(world, KeyE, 2);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);

    box_input_key(world, KeyShiftLeft, true);
    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->z, -2);

    ecs_fini(world);
}

void FirstPerson_speed(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .speed = 6 });

    box_input_key_press(world, KeyS, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->z, 3);
    test_flt(ecs_get(world, fp, FirstPersonController)->speed, 6);

    ecs_fini(world);
}

void FirstPerson_acceleration(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .acceleration = 2 });

    float alpha = 1.0f - expf(-1.0f);
    box_input_key(world, KeyD, true);
    ecs_progress(world, 0.5f);
    float v = 4 * alpha;
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_assert(fabsf(c->velocity[0] - v) < 0.0001f);
    test_flt(c->velocity[1], 0);
    test_assert(fabsf(ecs_get(world, fp, Position)->x - v * 0.5f) < 0.0001f);

    box_input_key(world, KeyD, false);
    ecs_progress(world, 0.5f);
    float x = v * 0.5f;
    v -= v * alpha;
    x += v * 0.5f;
    c = ecs_get(world, fp, FirstPersonController);
    test_assert(fabsf(c->velocity[0] - v) < 0.0001f);
    test_assert(fabsf(ecs_get(world, fp, Position)->x - x) < 0.0001f);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);

    ecs_fini(world);
}

void FirstPerson_look_drag(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 100, 100 }, .to = { 200, 150 }, .steps = 4, .frames = 4,
        .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, fp, Rotation);
    test_assert(fabsf(r->y + 0.3f) < 0.0001f);
    test_assert(fabsf(r->x + 0.15f) < 0.0001f);
    test_flt(r->z, 0);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 1.7f);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void FirstPerson_look_sensitivity_invert_y(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .sensitivity = 0.01f, .invert_y = true });

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { 10, 20 }, .steps = 1, .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, fp, Rotation);
    test_assert(fabsf(r->y + 0.1f) < 0.0001f);
    test_assert(fabsf(r->x - 0.2f) < 0.0001f);

    ecs_fini(world);
}

void FirstPerson_pitch_clamped(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .sensitivity = 1 });

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { 0, 100 }, .steps = 2, .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, fp, Rotation);
    test_assert(r->x < -1.5f);
    test_assert(r->x > -(float)GLM_PI_2);

    ecs_fini(world);
}

void FirstPerson_jump(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .jump = 10, .gravity = 10 });
    ecs_progress(world, 0.1f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    box_input_key_press(world, KeySpace, 1);
    ecs_progress(world, 0.1f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, false);
    test_assert(fabsf(c->velocity[1] - 9) < 0.0001f);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 2.6f) < 0.0001f);

    ecs_progress(world, 0.1f);
    c = ecs_get(world, fp, FirstPersonController);
    test_assert(fabsf(c->velocity[1] - 8) < 0.0001f);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 3.4f) < 0.0001f);

    for (int32_t i = 0; i < 30; i ++) {
        ecs_progress(world, 0.1f);
    }

    c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_flt(c->velocity[1], 0);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);

    ecs_fini(world);
}

void FirstPerson_jump_while_walking(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeyW, true);
    box_input_key_press(world, KeySpace, 1);
    ecs_progress(world, 0.1f);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->y > 1.7f);
    test_assert(fabsf(p->z + 0.4f) < 0.0001f);

    ecs_progress(world, 0.1f);
    p = ecs_get(world, fp, Position);
    test_assert(p->y > 1.7f);
    test_assert(fabsf(p->z + 0.8f) < 0.0001f);

    ecs_fini(world);
}

void FirstPerson_jump_needs_ground(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 10, 0 });
    ecs_add(world, fp, FirstPersonController);

    box_input_key_press(world, KeySpace, 1);
    ecs_progress(world, 0.1f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, false);
    test_assert(fabsf(c->velocity[1] + 1.8f) < 0.0001f);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 9.82f) < 0.0001f);

    ecs_fini(world);
}

void FirstPerson_jump_hold_no_repeat(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key(world, KeySpace, true);
    for (int32_t i = 0; i < 30; i ++) {
        ecs_progress(world, 0.1f);
    }

    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);

    ecs_fini(world);
}

void FirstPerson_fall(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 10, 0 });
    ecs_set(world, fp, FirstPersonController, { .gravity = 10 });

    ecs_progress(world, 0.1f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, false);
    test_assert(fabsf(c->velocity[1] + 1) < 0.0001f);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 9.9f) < 0.0001f);

    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_flt(c->velocity[1], 0);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);

    ecs_fini(world);
}

void FirstPerson_ground_height(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .ground = 2 });

    ecs_progress(world, 0.5f);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 3.7f) < 0.0001f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->y - 3.7f) < 0.0001f);
    test_flt(p->z, -2);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    FirstPersonController *c = ecs_get_mut(world, fp, FirstPersonController);
    c->ground = -1;
    ecs_modified(world, fp, FirstPersonController);
    ecs_progress(world, 0.5f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, false);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    test_assert(fabsf(ecs_get(world, fp, Position)->y - 0.7f) < 0.0001f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void FirstPerson_eye(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .eye = 2.5f, .crouch_eye = 1.5f });

    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->y, 2.5f);
    test_flt(ecs_get(world, fp, FirstPersonController)->height, 2.5f);

    box_input_key(world, KeyCtrlLeft, true);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->y, 1.5f);
    test_flt(ecs_get(world, fp, FirstPersonController)->height, 1.5f);

    ecs_fini(world);
}

void FirstPerson_crouch(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.5f);

    box_input_key(world, KeyCtrlLeft, true);
    ecs_progress(world, 0.5f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->crouched, true);
    test_bool(c->grounded, true);
    test_flt(ecs_get(world, fp, Position)->y, 0.9f);

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->y, 0.9f);
    test_flt(p->z, -1);

    box_input_key(world, KeyCtrlLeft, false);
    ecs_progress(world, 0.5f);
    c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->crouched, false);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->z, -3);

    ecs_fini(world);
}

void FirstPerson_crouch_speed(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .crouch_speed = 1 });

    box_input_key(world, KeyCtrlLeft, true);
    box_input_key(world, KeyD, true);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->x, 0.5f);
    test_flt(p->y, 0.9f);

    ecs_fini(world);
}

void FirstPerson_fly_toggle(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.5f);

    box_input_key_press(world, KeyF, 1);
    ecs_progress(world, 0.5f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->flying, true);
    test_bool(c->grounded, false);

    ecs_progress(world, 0.5f);
    test_bool(ecs_get(world, fp, FirstPersonController)->flying, true);

    box_input_key_press(world, KeyF, 1);
    ecs_progress(world, 0.5f);
    c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->flying, false);
    test_bool(c->grounded, true);

    ecs_fini(world);
}

void FirstPerson_fly_no_gravity(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 10, 0 });
    ecs_set(world, fp, FirstPersonController, { .flying = true });

    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->flying, true);
    test_bool(c->grounded, false);
    test_flt(c->velocity[1], 0);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 10) < 0.0001f);

    ecs_fini(world);
}

void FirstPerson_fly_up_down(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .flying = true });
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);

    box_input_key_press(world, KeyE, 2);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 11.7f) < 0.0001f);

    box_input_key_press(world, KeyQ, 1);
    ecs_progress(world, 0.5f);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 6.7f) < 0.0001f);

    box_input_key_press(world, KeyQ, 4);
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    test_flt(ecs_get(world, fp, Position)->y, 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, false);

    ecs_fini(world);
}

void FirstPerson_fly_moves_like_camera(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, 5, 0 });
    ecs_set(world, fp, Rotation, { 0.5f, 0, 0 });
    ecs_set(world, fp, FirstPersonController, { .flying = true });

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->y - 5) < 0.0001f);
    test_flt(p->z, -5);

    box_input_key(world, KeyShiftLeft, true);
    box_input_key_press(world, KeyD, 1);
    ecs_progress(world, 0.5f);
    p = ecs_get(world, fp, Position);
    test_flt(p->x, 20);
    test_flt(p->z, -5);

    box_input_key(world, KeyCtrlLeft, true);
    box_input_key_press(world, KeySpace, 1);
    ecs_progress(world, 0.5f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->crouched, false);
    test_flt(c->velocity[1], 0);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 5) < 0.0001f);

    ecs_fini(world);
}

void FirstPerson_fly_speed_boost(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .flying = true, .fly_speed = 2, .boost = 3 });

    box_input_key_press(world, KeyS, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->z, 1);

    box_input_key(world, KeyShiftLeft, true);
    box_input_key_press(world, KeyS, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->z, 4);

    ecs_fini(world);
}

void FirstPerson_fly_exit_falls(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_progress(world, 0.1f);

    box_input_key_press(world, KeyF, 1);
    ecs_progress(world, 0.1f);
    box_input_key_press(world, KeyE, 5);
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(fabsf(ecs_get(world, fp, Position)->y - 6.7f) < 0.0001f);

    box_input_key_press(world, KeyF, 1);
    ecs_progress(world, 0.1f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->flying, false);
    test_bool(c->grounded, false);
    test_assert(ecs_get(world, fp, Position)->y < 6.7f);

    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_flt(ecs_get(world, fp, Position)->y, 1.7f);

    ecs_fini(world);
}

void FirstPerson_remap_binding(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_entity_t fly = ecs_lookup(world, "box.controllers.first_person.Fly");
    ecs_set(world, fly, Binding, { .key = KeyV });
    ecs_progress(world, 0.5f);

    box_input_key_press(world, KeyF, 1);
    ecs_progress(world, 0.5f);
    test_bool(ecs_get(world, fp, FirstPersonController)->flying, false);

    box_input_key_press(world, KeyV, 1);
    ecs_progress(world, 0.5f);
    test_bool(ecs_get(world, fp, FirstPersonController)->flying, true);

    ecs_fini(world);
}

void FirstPerson_custom_actions(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scope = ecs_entity(world, { .name = "keys" });
    ecs_entity_t forward = ecs_entity(world, { .name = "Forward", .parent = scope });
    ecs_set(world, forward, Action, { .description = "Forward" });
    ecs_set(world, forward, Binding, { .key = KeyUp });
    ecs_entity_t jump = ecs_entity(world, { .name = "Jump", .parent = scope });
    ecs_set(world, jump, Action, { .description = "Jump" });
    ecs_set(world, jump, Binding, { .key = KeyEnter });

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, FirstPersonController, { .actions = scope });
    ecs_progress(world, 0.1f);
    ecs_entity_t def = ecs_lookup(world, "box.controllers.first_person.Forward");
    test_assert(ecs_has_id(world, def, EcsDisabled));

    box_input_key(world, KeyW, true);
    box_input_key(world, KeySpace, true);
    ecs_progress(world, 0.1f);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->z, 0);
    test_flt(p->y, 1.7f);

    box_input_key(world, KeyW, false);
    box_input_key(world, KeySpace, false);

    box_input_key_press(world, KeyUp, 1);
    ecs_progress(world, 0.1f);
    test_assert(fabsf(ecs_get(world, fp, Position)->z + 0.4f) < 0.0001f);

    box_input_key_press(world, KeyEnter, 1);
    ecs_progress(world, 0.1f);
    test_assert(ecs_get(world, fp, Position)->y > 1.7f);

    ecs_fini(world);
}

void FirstPerson_with_camera_controller(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -5);
    test_flt(ecs_get(world, fp, Position)->z, -2);

    ecs_fini(world);
}

void FirstPerson_world_transform(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Camera, { 0 });
    ecs_add(world, fp, FirstPersonController);

    box_input_key_press(world, KeyD, 1);
    ecs_progress(world, 0.5f);
    const WorldTransform *t = ecs_get(world, fp, WorldTransform);
    test_flt(t->m[3][0], 2);
    test_flt(t->m[3][1], 1.7f);
    test_flt(t->m[3][2], 0);

    ecs_fini(world);
}

void FirstPerson_script_controller(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.controllers\n"
            "using box.transform\n"
            "player {\n"
            "  Position: {0, 0, 0}\n"
            "  FirstPersonController: {speed: 6, eye: 2, ground: 1, jump: 7}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t fp = ecs_lookup(world, "player");
    test_assert(fp != 0);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_assert(c != NULL);
    test_flt(c->speed, 6);
    test_flt(c->eye, 2);
    test_flt(c->ground, 1);
    test_flt(c->jump, 7);

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, fp, Position);
    test_flt(p->y, 3);
    test_flt(p->z, -3);

    ecs_fini(world);
}

void FirstPerson_command_key(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0.1f);

    bool error = true;
    ecs_os_free(box_terminal_run(world, s, "key.down A", &error));
    test_bool(error, false);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, fp, Position)->x, -2);

    ecs_os_free(box_terminal_run(world, s, "key.up A", &error));
    test_bool(error, false);
    ecs_os_free(box_terminal_run(world, s, "key.press Space", &error));
    test_bool(error, false);
    ecs_progress(world, 0.1f);
    test_assert(ecs_get(world, fp, Position)->y > 1.7f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, false);

    ecs_fini(world);
}

void FirstPerson_command_action(void) {
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fp = ecs_new(world);
    ecs_add(world, fp, FirstPersonController);
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0.5f);

    bool error = true;
    ecs_os_free(box_terminal_run(world, s,
        "action.press box.controllers.first_person.Fly", &error));
    test_bool(error, false);
    ecs_progress(world, 0.5f);
    test_bool(ecs_get(world, fp, FirstPersonController)->flying, true);

    ecs_os_free(box_terminal_run(world, s,
        "action.press box.controllers.first_person.Up 2", &error));
    test_bool(error, false);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    test_assert(fabsf(ecs_get(world, fp, Position)->y - 11.7f) < 0.0001f);

    ecs_os_free(box_terminal_run(world, s,
        "action.down box.controllers.first_person.Crouch", &error));
    test_bool(error, false);
    ecs_os_free(box_terminal_run(world, s,
        "action.press box.controllers.first_person.Fly", &error));
    test_bool(error, false);
    for (int32_t i = 0; i < 20; i ++) {
        ecs_progress(world, 0.1f);
    }

    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->flying, false);
    test_bool(c->grounded, true);
    test_bool(c->crouched, true);
    test_flt(ecs_get(world, fp, Position)->y, 0.9f);

    ecs_fini(world);
}
