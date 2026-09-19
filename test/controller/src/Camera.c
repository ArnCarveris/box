/**
 * @file Camera.c
 * @brief Tests for the camera controller: movement, mouse look and clearance.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;
static ecs_entity_t camera;

void Camera_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxController);
    camera = ecs_entity(w, { .name = "camera" });
    ecs_add(w, camera, CameraController);

    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 640;
    surface->height = 360;
}

void Camera_teardown(void)
{
    ecs_fini(w);
}

void Camera_the_module_binds_the_movement_actions(void)
{
    // controllers/camera_controller
    ecs_entity_t forward = ecs_lookup(w, "actions.Forward");
    test_assert(forward != 0);
    test_assert(ecs_get(w, forward, InputAction) != NULL);
    test_str(ecs_get(w, forward, InputAction)->description,
        "Move along the view direction");

    uint64_t masks[4];
    test_int(box_input_bindings(w, forward, masks, 4), 1);
    test_uint(masks[0], BoxKeyW);

    test_assert(ecs_lookup(w, "actions.Fast") != 0);
    test_int(box_input_bindings(
        w, ecs_lookup(w, "actions.Fast"), masks, 4), 1);
    test_uint(masks[0], BoxKeyShift);
    test_assert(ecs_lookup(w, "box.input.map.Forward") != 0);
}

void Camera_forward_moves_along_the_view(void)
{
    // controllers/camera_controller
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    const Position *p = ecs_get(w, camera, Position);
    test_assert(p->z > 1.0f);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_assert(ecs_get(w, camera, CameraController)->velocity.z > 5.0f);
}

void Camera_back_moves_against_the_view(void)
{
    // controllers/camera_controller
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyS });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_assert(ecs_get(w, camera, Position)->z < -1.0f);
}

void Camera_strafing_moves_sideways(void)
{
    // controllers/camera_controller
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyD });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    const Position *p = ecs_get(w, camera, Position);
    test_assert(p->x < -1.0f);
    test_flt(p->z, 0);

    box_input_reset(w);
    ecs_progress(w, 0.1f);
    float back = ecs_get(w, camera, Position)->x;
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyA });
    for (i = 0; i < 10; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_assert(ecs_get(w, camera, Position)->x > back);
}

void Camera_up_and_down_move_the_height(void)
{
    // controllers/camera_controller
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyE });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_assert(ecs_get(w, camera, Position)->y > 1.0f);

    box_input_reset(w);
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyQ });
    for (i = 0; i < 10; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_assert(ecs_get(w, camera, Position)->y < 0);
}

void Camera_the_fast_modifier_multiplies_the_speed(void)
{
    // controllers/camera_controller
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    float plain = ecs_get(w, camera, Position)->z;
    box_input_reset(w);
    ecs_set(w, camera, CameraController, { .speed = 10, .fast = 3 });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });

    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    float fast = ecs_get(w, camera, Position)->z - plain;
    test_assert(fast > plain * 2.0f);
}

void Camera_the_mouse_turns_the_camera_while_captured(void)
{
    // controllers/camera_controller
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->cursor_captured = true;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {0, 0} });
    ecs_progress(w, 0.1f);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {10, 20} });
    ecs_progress(w, 0.1f);

    const CameraController *cc = ecs_get(w, camera, CameraController);
    test_flt(cc->yaw, -0.03f);
    test_flt(cc->pitch, 0.06f);
    test_flt(ecs_get(w, camera, Orientation)->yaw, -0.03f);
    test_flt(ecs_get(w, camera, Orientation)->pitch, 0.06f);
}

void Camera_the_mouse_is_ignored_without_capture(void)
{
    // controllers/camera_controller
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {0, 0} });
    ecs_progress(w, 0.1f);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {10, 20} });
    ecs_progress(w, 0.1f);

    test_flt(ecs_get(w, camera, CameraController)->yaw, 0);
    test_flt(ecs_get(w, camera, CameraController)->pitch, 0);
}

void Camera_the_look_action_turns_without_capture(void)
{
    // controllers/camera_controller
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {0, 0} });
    ecs_progress(w, 0.1f);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseRight });
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {10, 0} });
    ecs_progress(w, 0.1f);

    test_flt(ecs_get(w, camera, CameraController)->yaw, -0.03f);
}

void Camera_sensitivity_scales_the_turn(void)
{
    // controllers/camera_controller
    ecs_set(w, camera, CameraController, { .sensitivity = 0.01f });
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->cursor_captured = true;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {0, 0} });
    ecs_progress(w, 0.1f);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {10, 0} });
    ecs_progress(w, 0.1f);

    test_flt(ecs_get(w, camera, CameraController)->yaw, -0.1f);
}

void Camera_pitch_stops_short_of_straight_down(void)
{
    // controllers/camera_controller
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->cursor_captured = true;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {0, 0} });
    ecs_progress(w, 0.1f);

    int32_t i;
    for (i = 0; i < 10; i ++) {
        box_input_inject(w, &(InputEvent){
            .kind = InputMouseMove, .value = {0, (float)(1000 * (i + 1))} });
        ecs_progress(w, 0.1f);
    }

    float pitch = ecs_get(w, camera, CameraController)->pitch;
    test_assert(pitch > 1.5f);
    test_assert(pitch < 1.5708f);
}

void Camera_yaw_turns_the_movement_with_the_view(void)
{
    // controllers/camera_controller
    CameraController *cc = ecs_get_mut(w, camera, CameraController);
    cc->yaw = 1.5707963f;
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    const Position *p = ecs_get(w, camera, Position);
    test_assert(p->x > 1.0f);
    test_assert(p->z < 0.01f);
    test_assert(p->z > -0.01f);
}

void Camera_clearance_keeps_the_camera_above_the_terrain(void)
{
    // controllers/camera_controller
    ecs_entity_t terrain = ecs_entity(w, { .name = "ground" });
    ecs_set(w, terrain, AABB, {
        .min = {-5, -1, -5}, .max = {5, 0.5f, 5} });
    ecs_set(w, camera, CameraController, {
        .terrain = terrain, .clearance = 2 });

    ecs_progress(w, 0.1f);
    test_flt(ecs_get(w, camera, Position)->y, 2.5f);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyQ });
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_flt(ecs_get(w, camera, Position)->y, 2.5f);
    test_flt(ecs_get(w, camera, CameraController)->velocity.y, 0);
}

void Camera_a_scene_rebinds_an_action(void)
{
    // controllers/camera_controller
    ecs_entity_t binding = ecs_lookup(w, "box.input.map.Forward");
    test_assert(binding != 0);
    ecs_set(w, binding, Binding, {
        .mask = BoxKeyUp, .action = ecs_lookup(w, "actions.Forward") });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_flt(ecs_get(w, camera, Position)->z, 0);

    box_input_reset(w);
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyUp });
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_assert(ecs_get(w, camera, Position)->z > 1.0f);
}

void Camera_the_controller_writes_orientation_and_lookat(void)
{
    // controllers/camera_controller
    ecs_add(w, camera, LookAt);
    CameraController *cc = ecs_get_mut(w, camera, CameraController);
    cc->yaw = 1.5707963f;
    ecs_progress(w, 0.1f);

    const LookAt *look = ecs_get(w, camera, LookAt);
    test_flt(look->x, 1.0f);
    test_flt(look->y, 0);
    test_assert(look->z < 0.01f);
    test_flt(ecs_get(w, camera, Orientation)->yaw, 1.5707963f);
}

void Camera_collision_keeps_the_camera_out_of_geometry(void)
{
    // collision/controller_collision
    ecs_entity_t block = ecs_entity(w, { .name = "block" });
    ecs_set(w, block, AABB, { .min = {-5, -5, 3}, .max = {5, 5, 4} });
    box_collision_update(w);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, 1.0f / 60);
    }

    const Position *p = ecs_get(w, camera, Position);
    test_assert(p->z > 2.3f);
    test_assert(p->z < 2.6f);
}

void Camera_disabling_collision_flies_through_geometry(void)
{
    // collision/controller_collision
    ecs_entity_t block = ecs_entity(w, { .name = "block" });
    ecs_set(w, block, AABB, { .min = {-5, -5, 3}, .max = {5, 5, 4} });
    ecs_get_mut(w, camera, CameraController)->no_collision = true;
    box_collision_update(w);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, 1.0f / 60);
    }

    test_assert(ecs_get(w, camera, Position)->z > 4.0f);
}
