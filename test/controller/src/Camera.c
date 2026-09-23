/**
 * @file Camera.c
 * @brief Tests for the camera controller: movement, mouse look and clearance.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

static ecs_world_t *w;
static ecs_entity_t camera;

void Camera_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
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
    test_uint(masks[0], BoxKeyShiftLeft);
    test_assert(ecs_lookup(w, "actions.Slow") != 0);
    test_int(box_input_bindings(
        w, ecs_lookup(w, "actions.Slow"), masks, 4), 1);
    test_uint(masks[0], BoxKeyShiftRight);
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
    test_assert(p->x > 1.0f);
    test_flt(p->z, 0);

    box_input_reset(w);
    ecs_progress(w, 0.1f);
    float back = ecs_get(w, camera, Position)->x;
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyA });
    for (i = 0; i < 10; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_assert(ecs_get(w, camera, Position)->x < back);
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
    ecs_set(w, camera, CameraController, { .speed = 10 });
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

void Camera_shift_keys_change_camera_speed(void)
{
    ecs_set(w, camera, CameraController, {
        .speed = 10, .acceleration = 1000, .no_collision = true });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0.1f);
    test_flt(ecs_get(w, camera, CameraController)->velocity.z, 10.0f);

    box_input_reset(w);
    ecs_set(w, camera, CameraController, {
        .speed = 10, .acceleration = 1000, .no_collision = true });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });
    ecs_progress(w, 0.1f);
    test_flt(ecs_get(w, camera, CameraController)->velocity.z, 20.0f);

    box_input_reset(w);
    ecs_set(w, camera, CameraController, {
        .speed = 10, .acceleration = 1000, .no_collision = true });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftRight });
    ecs_progress(w, 0.1f);
    test_flt(ecs_get(w, camera, CameraController)->velocity.z, 5.0f);
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
    test_flt(cc->yaw, 0.03f);
    test_flt(cc->pitch, 0.06f);
    test_flt(ecs_get(w, camera, Orientation)->yaw, 0.03f);
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

    test_flt(ecs_get(w, camera, CameraController)->yaw, 0.03f);
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

    test_flt(ecs_get(w, camera, CameraController)->yaw, 0.1f);
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

void Camera_the_move_velocity_follows_the_smoothing_curve(void)
{
    // ore_engine/camera_controller_port
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });

    float dt = 1.0f / 60.0f;
    float alpha = 1.0f - expf(-6.0f * dt);
    float speed = 40.0f;
    float expected = 0;
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(w, dt);
        expected += (speed - expected) * alpha;
    }

    float actual = ecs_get(w, camera, CameraController)->velocity.z;
    test_assert(fabsf(actual - expected) < 0.01f);
}

void Camera_sprint_scales_the_mouse_yaw(void)
{
    // ore_engine/camera_controller_port
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->cursor_captured = true;
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {0, 0} });
    ecs_progress(w, 0.1f);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {10, 0} });
    ecs_progress(w, 0.1f);

    test_flt(ecs_get(w, camera, CameraController)->yaw, 0.01f);
}

void Camera_the_turn_actions_follow_the_angular_smoothing_curve(void)
{
    // ore_engine/camera_controller_port
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyRight });

    float dt = 1.0f / 60.0f;
    float alpha = 1.0f - expf(-5.0f * dt);
    float expected = 0;
    float yaw = 0;
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(w, dt);
        expected += (1.5f - expected) * alpha;
        yaw += expected * dt;
    }

    const CameraController *cc = ecs_get(w, camera, CameraController);
    test_assert(fabsf(cc->angular_velocity.y - expected) < 0.001f);
    test_assert(fabsf(cc->yaw - yaw) < 0.001f);
    test_flt(cc->angular_velocity.x, 0);
}

void Camera_sprint_scales_the_turn_action_yaw(void)
{
    // ore_engine/camera_controller_port
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyLeft });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });

    int32_t i;
    for (i = 0; i < 300; i ++) {
        ecs_progress(w, 1.0f / 60.0f);
    }

    test_assert(fabsf(ecs_get(w, camera, CameraController)->angular_velocity.y
        + 0.5f) < 0.01f);
}

void Camera_the_wheel_gives_an_impulse_along_the_view(void)
{
    // ore_engine/camera_controller_port
    box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, 1.0f} });
    ecs_progress(w, 0.1f);

    test_flt(ecs_get(w, camera, CameraController)->velocity.z, 3.0f);
}

void Camera_the_wheel_is_ignored_while_the_ui_holds_the_mouse(void)
{
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 360;
    ecs_modified(w, root, Ui);
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, root), Rect, {120, 60});
    ecs_progress(w, 0.1f);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {320, 180} });
    ecs_progress(w, 0.1f);
    test_bool(box_ui_mouse_captured(w), true);

    box_input_inject(w, &(InputEvent){
        .kind = InputScroll, .value = {0, 1.0f} });
    ecs_progress(w, 0.1f);

    test_flt(ecs_get(w, camera, CameraController)->velocity.z, 0);
}

void Camera_the_turn_coasts_to_a_stop_after_the_button_releases(void)
{
    // ore_engine/camera_controller_port
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseRight });
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {0, 0} });
    ecs_progress(w, 1.0f / 60.0f);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {60, 0} });
    ecs_progress(w, 1.0f / 60.0f);

    float yaw_after_turn = ecs_get(w, camera, CameraController)->yaw;
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonUp, .code = BoxKeyMouseRight });
    ecs_progress(w, 1.0f / 60.0f);

    const CameraController *cc = ecs_get(w, camera, CameraController);
    test_assert(cc->yaw > yaw_after_turn);
    test_assert(cc->angular_velocity.y > 0);
}

void Camera_the_angular_velocity_zeroes_at_the_pitch_clamp(void)
{
    // ore_engine/camera_controller_port
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->cursor_captured = true;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {0, 0} });
    ecs_progress(w, 1.0f / 60.0f);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {0, 5000.0f} });
    ecs_progress(w, 1.0f / 60.0f);

    const CameraController *cc = ecs_get(w, camera, CameraController);
    test_assert(cc->pitch > 1.5f);
    test_flt(cc->angular_velocity.x, 0);
}

void Camera_the_controller_starts_facing_an_authored_lookat(void)
{
    // ore_engine/camera_controller_port
    ecs_entity_t e = ecs_entity(w, { .name = "placed" });
    ecs_set(w, e, Position, { 0, 0, 0 });
    ecs_set(w, e, LookAt, { 1, 0, 0 });
    ecs_add(w, e, CameraController);

    const CameraController *cc = ecs_get(w, e, CameraController);
    test_flt(cc->yaw, 1.5707963f);
    test_flt(cc->pitch, 0);
    test_flt(ecs_get(w, e, Position)->x, 0);
}

void Camera_the_controller_starts_facing_an_authored_rotation(void)
{
    // ore_engine/camera_controller_port
    ecs_entity_t e = ecs_entity(w, { .name = "placed" });
    ecs_set(w, e, Position, { 1, 2, 3 });
    ecs_set(w, e, Rotation, { .x = 0.3f, .y = 0.7f, .z = 0 });
    ecs_add(w, e, CameraController);

    const CameraController *cc = ecs_get(w, e, CameraController);
    test_flt(cc->yaw, 0.7f);
    test_flt(cc->pitch, 0.3f);
    test_flt(ecs_get(w, e, Position)->x, 1);
    test_flt(ecs_get(w, e, Position)->y, 2);
    test_flt(ecs_get(w, e, Position)->z, 3);
}

void Camera_the_controller_starts_facing_a_scripted_lookat(void)
{
    // ore_engine/camera_controller_port
    const char *code =
        "using box.transform\n"
        "using box.controller\n"
        "placed {\n"
        "  Position: {3, 2.5, -5}\n"
        "  LookAt: {0, 0.5, 0}\n"
        "  CameraController: {}\n"
        "}\n";
    test_int(ecs_script_run(w, "placed", code, NULL), 0);
    ecs_progress(w, 1.0f / 60.0f);

    ecs_entity_t e = ecs_lookup(w, "placed");
    test_assert(e != 0);
    const CameraController *cc = ecs_get(w, e, CameraController);
    test_assert(cc->yaw < 0);
    test_assert(cc->pitch > 0);
    const Position *p = ecs_get(w, e, Position);
    test_flt(p->x, 3);
    test_flt(p->y, 2.5);
    test_flt(p->z, -5);
    const LookAt *look = ecs_get(w, e, LookAt);
    test_assert(look->x < p->x);
    test_assert(look->y < p->y);
    test_assert(look->z > p->z);
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
