/**
 * @file Camera.c
 * @brief The camera controller driven by key, cursor and action input and by commands.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Camera_import(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_lookup(world, "box.controllers.CameraController") ==
        ecs_id(CameraController));
    test_assert(ecs_lookup(world, "box.controllers.camera") != 0);

    ecs_fini(world);
}

void Camera_default_bindings(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *names[] = {
        "Forward", "Back", "Left", "Right", "Up", "Down", "Fast", "Look",
        "TurnLeft", "TurnRight", "TurnUp", "TurnDown"
    };
    Key keys[] = {
        KeyW, KeyS, KeyA, KeyD, KeyE, KeyQ, KeyShiftLeft, KeyMouseRight,
        KeyLeft, KeyRight, KeyUp, KeyDown
    };
    ecs_entity_t scope = ecs_lookup(world, "box.controllers.camera");
    for (int32_t i = 0; i < 12; i ++) {
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

    ecs_entity_t shift = ecs_lookup(world, "box.controllers.camera.Fast.ShiftRight");
    test_assert(shift != 0);
    test_int(ecs_get(world, shift, Binding)->key, KeyShiftRight);
    test_uint(ecs_get(world, shift, Binding)->action, 0);

    ecs_fini(world);
}

void Camera_default_actions_disabled(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t forward = ecs_lookup(world, "box.controllers.camera.Forward");
    test_assert(forward != 0);
    test_assert(ecs_has_id(world, forward, EcsDisabled));

    box_input_key(world, KeyW, true);
    ecs_progress(world, 0.5f);
    test_bool(ecs_get(world, forward, ActionState)->down, false);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_progress(world, 0.5f);
    test_assert(!ecs_has_id(world, forward, EcsDisabled));
    test_bool(ecs_get(world, forward, ActionState)->down, true);
    test_flt(ecs_get(world, cam, Position)->z, -5);

    ecs_delete(world, cam);
    ecs_progress(world, 0.5f);
    test_assert(ecs_has_id(world, forward, EcsDisabled));
    test_bool(ecs_get(world, forward, ActionState)->down, false);

    ecs_fini(world);
}

void Camera_default_actions_custom_only(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scope = ecs_entity(world, { .name = "keys" });
    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_get_mut(world, cam, CameraController)->actions = scope;
    ecs_progress(world, 0.5f);

    ecs_entity_t forward = ecs_lookup(world, "box.controllers.camera.Forward");
    test_assert(ecs_has_id(world, forward, EcsDisabled));

    ecs_add(world, cam, CameraController);

    ecs_get_mut(world, cam, CameraController)->actions =
        ecs_lookup(world, "box.controllers.camera");
    box_input_key(world, KeyW, true);
    ecs_progress(world, 0.5f);
    test_assert(!ecs_has_id(world, forward, EcsDisabled));
    test_flt(ecs_get(world, cam, Position)->z, -5);

    ecs_fini(world);
}

void Camera_adds_position_rotation(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    test_assert(ecs_has(world, cam, Position));
    test_assert(ecs_has(world, cam, Rotation));
    test_assert(ecs_has(world, cam, WorldTransform));

    const CameraController *cc = ecs_get(world, cam, CameraController);
    test_flt(cc->speed, 40);
    test_flt(cc->boost, 5);
    test_flt(cc->acceleration, 6);
    test_flt(cc->deceleration, 18);
    test_flt(cc->turn_speed, 1.5f);
    test_flt(cc->turn_acceleration, 5);
    test_flt(cc->turn_deceleration, 15);
    test_flt(cc->boost_yaw, 0.33f);
    test_flt(cc->sensitivity, 0.003f);
    test_flt(cc->zoom, 3);
    test_bool(cc->invert_y, false);
    test_flt(cc->velocity[0], 0);
    test_flt(cc->angular_velocity[1], 0);
    test_bool(cc->collide, false);
    test_flt(cc->radius, 0.25f);
    test_uint(cc->actions, ecs_lookup(world, "box.controllers.camera"));

    ecs_fini(world);
}

void Camera_idle(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 1, 2, 3 });
    ecs_set(world, cam, Rotation, { 0.1f, 0.2f, 0 });
    ecs_add(world, cam, CameraController);

    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);

    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);
    const Rotation *r = ecs_get(world, cam, Rotation);
    test_flt(r->x, 0.1f);
    test_flt(r->y, 0.2f);

    ecs_fini(world);
}

void Camera_forward(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyW, 2);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, -5);

    ecs_progress(world, 0.5f);
    p = ecs_get(world, cam, Position);
    test_flt(p->z, -10);

    ecs_progress(world, 0.5f);
    p = ecs_get(world, cam, Position);
    test_flt(p->z, -10);

    ecs_fini(world);
}

void Camera_back(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyS, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 5);

    ecs_fini(world);
}

void Camera_left(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyA, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, -5);
    test_flt(p->y, 0);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Camera_right(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyD, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 5);
    test_flt(p->y, 0);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Camera_up(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyE, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 5);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Camera_down(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyQ, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 0);
    test_flt(p->y, -5);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Camera_diagonal(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key(world, KeyW, true);
    box_input_key(world, KeyD, true);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_assert(fabsf(p->x - 5.0f / sqrtf(2.0f)) < 0.0001f);
    test_flt(p->y, 0);
    test_assert(fabsf(p->z + 5.0f / sqrtf(2.0f)) < 0.0001f);
    test_assert(fabsf(sqrtf(p->x * p->x + p->z * p->z) - 5.0f) < 0.0001f);

    ecs_fini(world);
}

void Camera_forward_follows_yaw(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Rotation, { 0, (float)GLM_PI_2, 0 });
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_assert(fabsf(p->x + 5) < 0.0001f);
    test_flt(p->y, 0);
    test_assert(fabsf(p->z) < 0.0001f);

    box_input_key_press(world, KeyD, 1);
    ecs_progress(world, 0.5f);
    p = ecs_get(world, cam, Position);
    test_assert(fabsf(p->x + 5) < 0.0001f);
    test_assert(fabsf(p->z + 5) < 0.0001f);

    ecs_fini(world);
}

void Camera_forward_ignores_pitch(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Rotation, { 0.5f, 0, 0 });
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, -5);
    test_flt(ecs_get(world, cam, Rotation)->x, 0.5f);

    ecs_fini(world);
}

void Camera_fast(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key(world, KeyShiftLeft, true);
    box_input_key(world, KeyW, true);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -20);

    box_input_key(world, KeyShiftLeft, false);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -25);

    ecs_fini(world);
}

void Camera_speed(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    cc->speed = 2;

    box_input_key_press(world, KeyE, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->y, 1);

    ecs_fini(world);
}

void Camera_zero_speed_stays(void) {
    // engine_core/zero_values
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, CameraController, {
        .boost = 4, .actions = ecs_lookup(world, "box.controllers.camera") });

    box_input_key_press(world, KeyE, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->y, 0);

    ecs_fini(world);
}

void Camera_boost(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    cc->boost = 2;

    box_input_key(world, KeyShiftLeft, true);
    box_input_key(world, KeyS, true);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, 10);

    ecs_fini(world);
}

void Camera_release_stops(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key(world, KeyD, true);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->x, 5);
    test_flt(ecs_get(world, cam, CameraController)->velocity[0], 10);

    box_input_key(world, KeyD, false);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->x, 5);
    test_flt(ecs_get(world, cam, CameraController)->velocity[0], 0);

    ecs_fini(world);
}

void Camera_acceleration(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->acceleration = 2;
    cc->deceleration = 4;

    float up = 1.0f - expf(-1.0f), down = 1.0f - expf(-2.0f);
    box_input_key(world, KeyD, true);
    ecs_progress(world, 0.5f);
    float v = 10 * up;
    const CameraController *c = ecs_get(world, cam, CameraController);
    test_assert(fabsf(c->velocity[0] - v) < 0.0001f);
    test_assert(fabsf(ecs_get(world, cam, Position)->x - v * 0.5f) < 0.0001f);

    ecs_progress(world, 0.5f);
    float x = v * 0.5f;
    v += (10 - v) * up;
    x += v * 0.5f;
    c = ecs_get(world, cam, CameraController);
    test_assert(fabsf(c->velocity[0] - v) < 0.0001f);
    test_assert(fabsf(ecs_get(world, cam, Position)->x - x) < 0.0001f);

    box_input_key(world, KeyD, false);
    ecs_progress(world, 0.5f);
    v -= v * down;
    x += v * 0.5f;
    c = ecs_get(world, cam, CameraController);
    test_assert(fabsf(c->velocity[0] - v) < 0.0001f);
    test_assert(fabsf(ecs_get(world, cam, Position)->x - x) < 0.0001f);
    test_assert(v > 0);

    ecs_fini(world);
}

void Camera_look_drag(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 100, 100 }, .to = { 200, 150 }, .steps = 4, .frames = 4,
        .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_assert(fabsf(r->y + 0.3f) < 0.0001f);
    test_assert(fabsf(r->x + 0.15f) < 0.0001f);
    test_flt(r->z, 0);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Camera_look_action_drag(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_entity_t look = ecs_lookup(world, "box.controllers.camera.Look");
    test_assert(look != 0);

    box_input_action_drag(world, look, &(box_input_sweep_t){
        .from = { 300, 300 }, .to = { 200, 300 }, .steps = 2, .frames = 2
    });
    for (int32_t i = 0; i < 6; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_assert(fabsf(r->y - 0.3f) < 0.0001f);
    test_flt(r->x, 0);

    ecs_fini(world);
}

void Camera_look_needs_hold(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

    box_input_mouse_sweep(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { 100, 100 }, .steps = 4
    });
    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 100, 100 }, .to = { 0, 0 }, .steps = 4, .button = KeyMouseLeft
    });
    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0.5f);
    }

    test_int(box_input_pending(world), 0);
    const Rotation *r = ecs_get(world, cam, Rotation);
    test_flt(r->x, 0);
    test_flt(r->y, 0);

    ecs_fini(world);
}

void Camera_look_sensitivity(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    cc->sensitivity = 0.01f;

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { 10, 20 }, .steps = 1, .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_assert(fabsf(r->y + 0.1f) < 0.0001f);
    test_assert(fabsf(r->x + 0.2f) < 0.0001f);

    ecs_fini(world);
}

void Camera_look_invert_y(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    cc->sensitivity = 0.01f;
    cc->invert_y = true;

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { 10, 20 }, .steps = 1, .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_assert(fabsf(r->y + 0.1f) < 0.0001f);
    test_assert(fabsf(r->x - 0.2f) < 0.0001f);

    ecs_fini(world);
}

void Camera_pitch_clamped(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    cc->sensitivity = 1;

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { 0, 100 }, .steps = 2, .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_assert(r->x < -1.5f);
    test_assert(r->x > -(float)GLM_PI_2);

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 100 }, .to = { 0, -200 }, .steps = 2, .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0.5f);
    }

    r = ecs_get(world, cam, Rotation);
    test_assert(r->x > 1.5f);
    test_assert(r->x < (float)GLM_PI_2);

    ecs_fini(world);
}

void Camera_remap_binding(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_entity_t forward = ecs_lookup(world, "box.controllers.camera.Forward");
    ecs_set(world, forward, Binding, { .key = KeyUp });

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, 0);

    box_input_key_press(world, KeyUp, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -5);

    ecs_fini(world);
}

void Camera_extra_binding(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_entity_t forward = ecs_lookup(world, "box.controllers.camera.Forward");
    ecs_entity_t arrow = ecs_new_w_pair(world, EcsChildOf, forward);
    ecs_set(world, arrow, Binding, { .key = KeyUp });

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -5);

    box_input_key_press(world, KeyUp, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -10);

    box_input_key(world, KeyW, true);
    box_input_key(world, KeyUp, true);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -15);

    ecs_fini(world);
}

void Camera_action_hold(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_entity_t left = ecs_lookup(world, "box.controllers.camera.Left");
    ecs_entity_t down = ecs_lookup(world, "box.controllers.camera.Down");

    box_input_action(world, left, true);
    box_input_action_press(world, down, 1);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    float d = 5.0f / sqrtf(2.0f);
    test_assert(fabsf(p->x + d) < 0.0001f);
    test_assert(fabsf(p->y + d) < 0.0001f);
    test_flt(p->z, 0);

    ecs_progress(world, 0.5f);
    p = ecs_get(world, cam, Position);
    test_assert(fabsf(p->x + d + 5) < 0.0001f);
    test_assert(fabsf(p->y + d) < 0.0001f);

    box_input_action(world, left, false);
    ecs_progress(world, 0.5f);
    p = ecs_get(world, cam, Position);
    test_assert(fabsf(p->x + d + 5) < 0.0001f);

    ecs_fini(world);
}

void Camera_custom_actions(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scope = ecs_entity(world, { .name = "arrows" });
    ecs_entity_t forward = ecs_entity(world, { .name = "Forward", .parent = scope });
    ecs_set(world, forward, Action, { .description = "Forward" });
    ecs_set(world, forward, Binding, { .key = KeyUp });
    ecs_entity_t look = ecs_entity(world, { .name = "Look", .parent = scope });
    ecs_set(world, look, Action, { .description = "Look" });
    ecs_set(world, look, Binding, { .key = KeyMouseLeft });

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    cc->actions = scope;
    ecs_entity_t other = ecs_new(world);
    ecs_add(world, other, CameraController);
    CameraController *cc_other = ecs_get_mut(world, other, CameraController);
    cc_other->speed = 10;
    cc_other->boost = 4;
    cc_other->acceleration = 1e9f;
    cc_other->deceleration = 1e9f;
    cc_other->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, 0);
    test_flt(ecs_get(world, other, Position)->z, -5);

    box_input_key_press(world, KeyUp, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -5);
    test_flt(ecs_get(world, other, Position)->z, -5);

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { 100, 0 }, .steps = 1, .button = KeyMouseLeft
    });
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    test_assert(fabsf(ecs_get(world, cam, Rotation)->y + 0.3f) < 0.0001f);
    test_flt(ecs_get(world, other, Rotation)->y, 0);

    ecs_fini(world);
}

void Camera_custom_actions_missing(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scope = ecs_entity(world, { .name = "empty" });
    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->actions = scope;

    box_input_key_press(world, KeyW, 1);
    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { 100, 0 }, .steps = 1, .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    test_flt(ecs_get(world, cam, Position)->z, 0);
    test_flt(ecs_get(world, cam, Rotation)->y, 0);

    ecs_fini(world);
}

void Camera_multiple_cameras(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, CameraController);
    CameraController *cc_a = ecs_get_mut(world, a, CameraController);
    cc_a->speed = 10;
    cc_a->boost = 4;
    cc_a->acceleration = 1e9f;
    cc_a->deceleration = 1e9f;
    cc_a->turn_deceleration = 1e9f;
    ecs_entity_t b = ecs_new(world);
    ecs_add(world, b, CameraController);
    CameraController *cc_b = ecs_get_mut(world, b, CameraController);
    cc_b->speed = 10;
    cc_b->boost = 4;
    cc_b->acceleration = 1e9f;
    cc_b->deceleration = 1e9f;
    cc_b->turn_deceleration = 1e9f;
    cc_b->speed = 2;
    ecs_set(world, b, Rotation, { 0, (float)GLM_PI, 0 });

    box_input_key_press(world, KeyW, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, a, Position)->z, -5);
    test_assert(fabsf(ecs_get(world, b, Position)->z - 1) < 0.0001f);

    ecs_fini(world);
}

void Camera_world_transform(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Camera, { .near_ = 0.1f });
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;

    box_input_key_press(world, KeyE, 1);
    ecs_progress(world, 0.5f);
    const WorldTransform *t = ecs_get(world, cam, WorldTransform);
    test_flt(t->m[3][0], 0);
    test_flt(t->m[3][1], 5);
    test_flt(t->m[3][2], 0);

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 0, 0 }, .to = { -100, 0 }, .steps = 1, .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_assert(fabsf(r->y - 0.3f) < 0.0001f);
    t = ecs_get(world, cam, WorldTransform);
    test_assert(fabsf(t->m[2][0] - sinf(0.3f)) < 0.0001f);
    test_assert(fabsf(t->m[2][2] - cosf(0.3f)) < 0.0001f);
    test_flt(t->m[3][1], 5);

    ecs_fini(world);
}

void Camera_script_controller(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.controllers\n"
            "using box.transform\n"
            "cam {\n"
            "  Position: {0, 1, 0}\n"
            "  CameraController: {speed: 4, boost: 3, sensitivity: 0.01, invert_y: true}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t cam = ecs_lookup(world, "cam");
    test_assert(cam != 0);
    const CameraController *cc = ecs_get(world, cam, CameraController);
    test_assert(cc != NULL);
    test_flt(cc->speed, 4);
    test_flt(cc->boost, 3);
    test_flt(cc->sensitivity, 0.01f);
    test_bool(cc->invert_y, true);

    test_flt(cc->acceleration, 6);
    test_flt(cc->deceleration, 18);

    box_input_key(world, KeyShiftLeft, true);
    box_input_key(world, KeyW, true);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->y, 1);
    test_assert(fabsf(p->z + 12 * (1.0f - expf(-3.0f)) * 0.5f) < 0.0001f);

    ecs_fini(world);
}

void Camera_script_remap(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.input\n"
            "using box.controllers\n"
            "box { controllers { camera { Up {\n"
            "  Binding: {key: Space}\n"
            "} } } }\n"
            "page_up { Binding: {action: box.controllers.camera.Up, key: PageUp} }\n"
            "cam { CameraController: {speed: 10, acceleration: 1e9, deceleration: 1e9} }\n"
    });
    test_assert(s != 0);

    ecs_entity_t cam = ecs_lookup(world, "cam");
    test_assert(cam != 0);

    box_input_key_press(world, KeyE, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->y, 0);

    box_input_key_press(world, KeySpace, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->y, 5);

    box_input_key_press(world, KeyPageUp, 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->y, 10);

    ecs_fini(world);
}

void Camera_command_key(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_entity_t s = box_terminal_new(world);

    bool error = true;
    ecs_os_free(box_terminal_run(world, s, "key.down A", &error));
    test_bool(error, false);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->x, -10);

    ecs_os_free(box_terminal_run(world, s, "key.up A", &error));
    test_bool(error, false);
    ecs_os_free(box_terminal_run(world, s, "key.press Q 2", &error));
    test_bool(error, false);
    for (int32_t i = 0; i < 3; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, -10);
    test_flt(p->y, -10);

    ecs_fini(world);
}

void Camera_command_drag(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_entity_t s = box_terminal_new(world);

    bool error = true;
    ecs_os_free(box_terminal_run(world, s, "mouse.drag right [0, 0] [50, -100] 4 4", &error));
    test_bool(error, false);
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_assert(fabsf(r->y + 0.15f) < 0.0001f);
    test_assert(fabsf(r->x - 0.3f) < 0.0001f);

    ecs_fini(world);
}

void Camera_command_action(void) {
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->boost = 4;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;
    cc->turn_deceleration = 1e9f;
    ecs_entity_t s = box_terminal_new(world);

    bool error = true;
    ecs_os_free(box_terminal_run(world, s, "action.press box.controllers.camera.Back 2", &error));
    test_bool(error, false);
    for (int32_t i = 0; i < 3; i ++) {
        ecs_progress(world, 0.5f);
    }

    test_flt(ecs_get(world, cam, Position)->z, 10);

    ecs_os_free(box_terminal_run(world, s, "action.down box.controllers.camera.Fast", &error));
    test_bool(error, false);
    ecs_os_free(box_terminal_run(world, s, "action.press box.controllers.camera.Right", &error));
    test_bool(error, false);
    for (int32_t i = 0; i < 2; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->x, 20);
    test_flt(p->z, 10);

    ecs_fini(world);
}


void Camera_arrows_turn(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

    float dt = 1.0f / 60.0f, yaw = 0, av = 0;
    box_input_glfw_on_key(world, 263, 1);
    for (int32_t n = 1; n <= 30; n ++) {
        ecs_progress(world, dt);
        av += (1.5f - av) * (1.0f - expf(-5.0f * dt));
        yaw += av * dt;
        const CameraController *cc = ecs_get(world, cam, CameraController);
        float expect = 1.5f * (1.0f - expf(-5.0f * n * dt));
        test_assert(fabsf(cc->angular_velocity[1] - expect) < 0.0001f);
        test_assert(fabsf(ecs_get(world, cam, Rotation)->y - yaw) < 0.0001f);
    }

    test_assert(ecs_get(world, cam, CameraController)->angular_velocity[1] < 1.5f * 0.95f);
    test_assert(ecs_get(world, cam, Position)->z == 0);

    box_input_glfw_on_key(world, 263, 0);
    float released = av;
    for (int32_t n = 1; n <= 10; n ++) {
        ecs_progress(world, dt);
        av -= av * (1.0f - expf(-15.0f * dt));
        yaw += av * dt;
        const CameraController *cc = ecs_get(world, cam, CameraController);
        test_assert(fabsf(cc->angular_velocity[1] - released * expf(-15.0f * n * dt)) < 0.0001f);
        test_assert(fabsf(ecs_get(world, cam, Rotation)->y - yaw) < 0.0001f);
    }

    box_input_glfw_on_key(world, 262, 1);
    for (int32_t n = 0; n < 60; n ++) {
        ecs_progress(world, dt);
    }

    test_assert(ecs_get(world, cam, CameraController)->angular_velocity[1] < -1.4f);
    test_assert(ecs_get(world, cam, Rotation)->y < yaw - 0.5f);
    test_flt(ecs_get(world, cam, Rotation)->x, 0);

    ecs_fini(world);
}

void Camera_arrows_pitch(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

    box_input_glfw_on_key(world, 265, 1);
    for (int32_t n = 0; n < 20; n ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    float up = ecs_get(world, cam, Rotation)->x;
    test_assert(up > 0.1f);
    test_flt(ecs_get(world, cam, Rotation)->y, 0);

    box_input_glfw_on_key(world, 265, 0);
    box_input_glfw_on_key(world, 264, 1);
    for (int32_t n = 0; n < 60; n ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    test_assert(ecs_get(world, cam, Rotation)->x < up - 0.5f);
    test_assert(ecs_get(world, cam, CameraController)->angular_velocity[0] < -1.4f);

    ecs_fini(world);
}

void Camera_arrows_diagonal(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->turn_acceleration = 1e9f;

    box_input_key(world, KeyLeft, true);
    box_input_key(world, KeyUp, true);
    ecs_progress(world, 0.1f);

    const CameraController *cc = ecs_get(world, cam, CameraController);
    float rate = 1.5f / sqrtf(2.0f);
    test_assert(fabsf(cc->angular_velocity[0] - rate) < 0.0001f);
    test_assert(fabsf(cc->angular_velocity[1] - rate) < 0.0001f);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->x - rate * 0.1f) < 0.0001f);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->y - rate * 0.1f) < 0.0001f);

    ecs_fini(world);
}

void Camera_turn_speed_zero(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->turn_speed = 0;

    box_input_key(world, KeyLeft, true);
    box_input_key(world, KeyDown, true);
    for (int32_t n = 0; n < 10; n ++) {
        ecs_progress(world, 0.1f);
    }

    test_flt(ecs_get(world, cam, Rotation)->x, 0);
    test_flt(ecs_get(world, cam, Rotation)->y, 0);

    ecs_fini(world);
}

void Camera_pitch_limit(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

    box_input_key(world, KeyUp, true);
    for (int32_t n = 0; n < 40; n ++) {
        ecs_progress(world, 0.1f);
    }

    float limit = (float)GLM_PI_2 - 0.05f;
    test_assert(fabsf(ecs_get(world, cam, Rotation)->x - limit) < 0.0001f);
    test_flt(ecs_get(world, cam, CameraController)->angular_velocity[0], 0);

    box_input_key(world, KeyUp, false);
    box_input_key(world, KeyDown, true);
    for (int32_t n = 0; n < 60; n ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(fabsf(ecs_get(world, cam, Rotation)->x + limit) < 0.0001f);
    test_flt(ecs_get(world, cam, CameraController)->angular_velocity[0], 0);

    ecs_fini(world);
}

void Camera_mouse_look_platform(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    float dt = 1.0f / 60.0f;

    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 100, 100 } });
    box_input_glfw_on_button(world, 1, 1);
    ecs_progress(world, dt);
    test_flt(ecs_get(world, cam, Rotation)->y, 0);

    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 150, 100 } });
    ecs_progress(world, dt);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->y + 0.15f) < 0.00001f);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->x) < 0.00001f);

    ecs_progress(world, dt);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->y + 0.15f) < 0.00001f);
    test_flt(ecs_get(world, cam, CameraController)->angular_velocity[1], 0);

    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 130, 120 } });
    ecs_progress(world, dt);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->y + 0.09f) < 0.00001f);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->x + 0.06f) < 0.00001f);
    test_flt(ecs_get(world, cam, Position)->z, 0);

    ecs_fini(world);
}

void Camera_mouse_look_coasts(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    float dt = 1.0f / 60.0f;

    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 100, 100 } });
    box_input_glfw_on_button(world, 1, 1);
    ecs_progress(world, dt);
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 80, 100 } });
    ecs_progress(world, dt);
    float yaw = ecs_get(world, cam, Rotation)->y;
    float av = 20 * 0.003f / dt;
    test_assert(fabsf(yaw - 0.06f) < 0.00001f);
    test_assert(fabsf(ecs_get(world, cam, CameraController)->angular_velocity[1] - av) < 0.001f);

    box_input_glfw_on_button(world, 1, 0);
    for (int32_t n = 1; n <= 20; n ++) {
        ecs_progress(world, dt);
        av -= av * (1.0f - expf(-15.0f * dt));
        yaw += av * dt;
        test_assert(fabsf(ecs_get(world, cam, Rotation)->y - yaw) < 0.0001f);
    }

    test_assert(yaw > 0.06f + 0.1f);
    test_assert(ecs_get(world, cam, CameraController)->angular_velocity[1] < av + 0.001f);

    ecs_fini(world);
}

void Camera_mouse_left_no_look(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 100, 100 } });
    box_input_glfw_on_button(world, 0, 1);
    ecs_progress(world, 0.1f);
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 200, 160 } });
    ecs_progress(world, 0.1f);

    test_flt(ecs_get(world, cam, Rotation)->x, 0);
    test_flt(ecs_get(world, cam, Rotation)->y, 0);

    ecs_fini(world);
}

void Camera_ease_in(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    float dt = 1.0f / 60.0f, z = 0;

    box_input_glfw_on_key(world, 'W', 1);
    for (int32_t n = 1; n <= 60; n ++) {
        ecs_progress(world, dt);
        float v = 40 * (1.0f - expf(-6.0f * n * dt));
        z -= v * dt;
        const CameraController *cc = ecs_get(world, cam, CameraController);
        test_assert(fabsf(cc->velocity[2] + v) < 0.001f);
        test_assert(fabsf(ecs_get(world, cam, Position)->z - z) < 0.001f);
        if (n == 1) {
            test_assert(-cc->velocity[2] < 40 * 0.1f);
        }
    }

    test_assert(-ecs_get(world, cam, CameraController)->velocity[2] > 39);
    test_flt(ecs_get(world, cam, Position)->x, 0);
    test_flt(ecs_get(world, cam, Position)->y, 0);

    ecs_fini(world);
}

void Camera_ease_out(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    float dt = 1.0f / 60.0f;

    box_input_glfw_on_key(world, 'D', 1);
    for (int32_t n = 0; n < 120; n ++) {
        ecs_progress(world, dt);
    }

    float v0 = ecs_get(world, cam, CameraController)->velocity[0];
    float x = ecs_get(world, cam, Position)->x;
    test_assert(v0 > 39.9f);

    box_input_glfw_on_key(world, 'D', 0);
    for (int32_t n = 1; n <= 20; n ++) {
        ecs_progress(world, dt);
        float v = v0 * expf(-18.0f * n * dt);
        x += v * dt;
        test_assert(fabsf(ecs_get(world, cam, CameraController)->velocity[0] - v) < 0.001f);
        test_assert(fabsf(ecs_get(world, cam, Position)->x - x) < 0.001f);
        if (n == 1) {
            test_assert(v > v0 * 0.5f);
        }
    }

    test_assert(ecs_get(world, cam, CameraController)->velocity[0] < 1);

    ecs_fini(world);
}

void Camera_deceleration_zero_coasts(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->deceleration = 0;

    box_input_key(world, KeyS, true);
    ecs_progress(world, 0.5f);
    float v = ecs_get(world, cam, CameraController)->velocity[2];
    test_assert(v > 30);

    box_input_key(world, KeyS, false);
    ecs_progress(world, 0.5f);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, CameraController)->velocity[2], v);

    ecs_fini(world);
}

void Camera_boost_shift_right(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

    box_input_glfw_on_key(world, 344, 1);
    box_input_glfw_on_key(world, 'W', 1);
    for (int32_t n = 0; n < 30; n ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(fabsf(ecs_get(world, cam, CameraController)->velocity[2] + 200) < 0.01f);

    ecs_fini(world);
}

void Camera_boost_yaw(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

    box_input_key(world, KeyShiftLeft, true);
    box_input_key(world, KeyLeft, true);
    for (int32_t n = 0; n < 30; n ++) {
        ecs_progress(world, 0.1f);
    }

    const CameraController *cc = ecs_get(world, cam, CameraController);
    test_assert(fabsf(cc->angular_velocity[1] - 1.5f * 0.33f) < 0.001f);

    box_input_key(world, KeyLeft, false);
    for (int32_t n = 0; n < 30; n ++) {
        ecs_progress(world, 0.1f);
    }

    float yaw = ecs_get(world, cam, Rotation)->y;
    float pitch = ecs_get(world, cam, Rotation)->x;
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 0, 0 } });
    box_input_glfw_on_button(world, 1, 1);
    ecs_progress(world, 0.1f);
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 100, 100 } });
    ecs_progress(world, 0.1f);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->y - (yaw - 0.3f * 0.33f)) < 0.0001f);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->x - (pitch - 0.3f)) < 0.0001f);

    ecs_fini(world);
}

void Camera_wheel_zoom(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Rotation, { 0.5f, 0, 0 });
    ecs_add(world, cam, CameraController);
    float dt = 1.0f / 60.0f;

    box_input_platform_push(world, &(InputEvent){ .kind = InputScroll, .value = { 0, 2 } });
    ecs_progress(world, dt);
    const CameraController *cc = ecs_get(world, cam, CameraController);
    test_assert(fabsf(cc->velocity[1] - 6 * sinf(0.5f)) < 0.0001f);
    test_assert(fabsf(cc->velocity[2] + 6 * cosf(0.5f)) < 0.0001f);
    test_flt(cc->velocity[0], 0);
    const Position *p = ecs_get(world, cam, Position);
    test_assert(fabsf(p->y - 6 * sinf(0.5f) * dt) < 0.0001f);
    test_assert(fabsf(p->z + 6 * cosf(0.5f) * dt) < 0.0001f);

    ecs_progress(world, dt);
    cc = ecs_get(world, cam, CameraController);
    test_assert(fabsf(cc->velocity[2] + 6 * cosf(0.5f) * expf(-18.0f * dt)) < 0.0001f);

    box_input_mouse_scroll(world, 0, -3);
    ecs_progress(world, dt);
    cc = ecs_get(world, cam, CameraController);
    test_assert(cc->velocity[2] > 0);

    ecs_fini(world);
}

void Camera_zoom_zero(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->zoom = 0;

    box_input_mouse_scroll(world, 0, 3);
    ecs_progress(world, 0.1f);
    test_flt(ecs_get(world, cam, Position)->z, 0);
    test_flt(ecs_get(world, cam, CameraController)->velocity[2], 0);

    ecs_fini(world);
}

void Camera_script_turn_settings(void) {
    // controllers/camera_flecs_engine
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.controllers\n"
            "cam {\n"
            "  CameraController: {turn_speed: 2, turn_acceleration: 1e9, boost_yaw: 0.5}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t cam = ecs_lookup(world, "cam");
    const CameraController *cc = ecs_get(world, cam, CameraController);
    test_flt(cc->speed, 40);
    test_flt(cc->turn_deceleration, 15);

    box_input_key(world, KeyShiftRight, true);
    box_input_key(world, KeyRight, true);
    ecs_progress(world, 0.5f);
    test_assert(fabsf(ecs_get(world, cam, Rotation)->y + 0.5f) < 0.0001f);

    ecs_fini(world);
}

void Camera_look_level_horizon(void) {
    // engine_core/rotation_order
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->turn_deceleration = 1e9f;

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 300, 300 }, .to = { 100, 300 }, .steps = 2, .frames = 2,
        .button = KeyMouseRight
    });
    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = { 100, 300 }, .to = { 100, 400 }, .steps = 2, .frames = 2,
        .button = KeyMouseRight
    });
    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_assert(fabsf(r->y - 0.6f) < 0.0001f);
    test_assert(fabsf(r->x + 0.3f) < 0.0001f);
    test_flt(r->z, 0);

    const WorldTransform *t = ecs_get(world, cam, WorldTransform);
    test_assert(fabsf(t->m[0][1]) < 0.0001f);
    test_assert(fabsf(t->m[0][0] - cosf(0.6f)) < 0.0001f);
    test_assert(fabsf(t->m[0][2] + sinf(0.6f)) < 0.0001f);
    test_assert(t->m[1][1] > 0.9f);
    test_assert(fabsf(t->m[2][0] - cosf(0.3f) * sinf(0.6f)) < 0.0001f);
    test_assert(fabsf(t->m[2][1] - sinf(0.3f)) < 0.0001f);
    test_assert(fabsf(t->m[2][2] - cosf(0.3f) * cosf(0.6f)) < 0.0001f);

    ecs_fini(world);
}

void Camera_stall_clamped(void) {
    // controller_fixes/tunneling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;

    box_input_key(world, KeyW, true);
    ecs_progress(world, 5.0f);
    test_flt(ecs_get(world, cam, Position)->z, -5);
    ecs_progress(world, 0.25f);
    test_flt(ecs_get(world, cam, Position)->z, -7.5f);

    ecs_fini(world);
}
