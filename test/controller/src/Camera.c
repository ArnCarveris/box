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

    const char *names[] = { "Forward", "Back", "Left", "Right", "Up", "Down", "Fast", "Look" };
    Key keys[] = { KeyW, KeyS, KeyA, KeyD, KeyE, KeyQ, KeyShiftLeft, KeyMouseRight };
    ecs_entity_t scope = ecs_lookup(world, "box.controllers.camera");
    for (int32_t i = 0; i < 8; i ++) {
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
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    test_assert(ecs_has(world, cam, Position));
    test_assert(ecs_has(world, cam, Rotation));
    test_assert(ecs_has(world, cam, WorldTransform));

    const CameraController *cc = ecs_get(world, cam, CameraController);
    test_flt(cc->speed, 10);
    test_flt(cc->velocity[0], 0);
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
    ecs_get_mut(world, cam, CameraController)->speed = 2;

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
    ecs_get_mut(world, cam, CameraController)->boost = 2;

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
    // controllers/camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->acceleration = 2;

    float alpha = 1.0f - expf(-1.0f);
    box_input_key(world, KeyD, true);
    ecs_progress(world, 0.5f);
    float v = 10 * alpha;
    const CameraController *cc = ecs_get(world, cam, CameraController);
    test_assert(fabsf(cc->velocity[0] - v) < 0.0001f);
    test_assert(fabsf(ecs_get(world, cam, Position)->x - v * 0.5f) < 0.0001f);

    ecs_progress(world, 0.5f);
    float x = v * 0.5f;
    v += (10 - v) * alpha;
    x += v * 0.5f;
    cc = ecs_get(world, cam, CameraController);
    test_assert(fabsf(cc->velocity[0] - v) < 0.0001f);
    test_assert(fabsf(ecs_get(world, cam, Position)->x - x) < 0.0001f);

    box_input_key(world, KeyD, false);
    ecs_progress(world, 0.5f);
    v -= v * alpha;
    x += v * 0.5f;
    cc = ecs_get(world, cam, CameraController);
    test_assert(fabsf(cc->velocity[0] - v) < 0.0001f);
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
    ecs_get_mut(world, cam, CameraController)->sensitivity = 0.01f;

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
    ecs_get_mut(world, cam, CameraController)->sensitivity = 0.01f;
    ecs_get_mut(world, cam, CameraController)->invert_y = true;

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
    ecs_get_mut(world, cam, CameraController)->sensitivity = 1;

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
    ecs_get_mut(world, cam, CameraController)->actions = scope;
    ecs_entity_t other = ecs_new(world);
    ecs_add(world, other, CameraController);

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
    ecs_entity_t b = ecs_new(world);
    ecs_add(world, b, CameraController);
    ecs_get_mut(world, b, CameraController)->speed = 2;
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

    box_input_key(world, KeyShiftLeft, true);
    box_input_key(world, KeyW, true);
    ecs_progress(world, 0.5f);
    const Position *p = ecs_get(world, cam, Position);
    test_flt(p->y, 1);
    test_flt(p->z, -6);

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
            "cam { CameraController: {} }\n"
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


void Camera_look_level_horizon(void) {
    // engine_core/rotation_order
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

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
