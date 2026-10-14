/**
 * @file Camera.c
 * @brief Tests for the reversed Z projections of cameras, computed on the CPU.
 */

#include <box.h>
#include <bake_test.h>

void Camera_perspective_near_is_one(void) {
    // renderer/views
    Camera camera = {.fov = 60, .near_ = 0.5f};
    mat4 proj;
    vec4 p = {0, 0, -0.5f, 1}, out;
    box_camera_projection(&camera, 100, 100, proj);
    glm_mat4_mulv(proj, p, out);
    test_flt(out[2] / out[3], 1.0f);
}

void Camera_perspective_infinite_far(void) {
    // renderer/views
    Camera camera = {.fov = 60, .near_ = 0.5f};
    mat4 proj;
    vec4 mid = {0, 0, -50, 1}, far = {0, 0, -1e7f, 1}, out;
    box_camera_projection(&camera, 100, 100, proj);
    glm_mat4_mulv(proj, mid, out);
    test_flt(out[2] / out[3], 0.01f);
    glm_mat4_mulv(proj, far, out);
    test_assert(out[2] / out[3] > 0);
    test_assert(out[2] / out[3] < 1e-6f);
}

void Camera_perspective_finite_far(void) {
    // renderer/views
    Camera camera = {.fov = 60, .near_ = 1, .far_ = 100};
    mat4 proj;
    vec4 near = {0, 0, -1, 1}, far = {0, 0, -100, 1}, mid = {0, 0, -10, 1}, out;
    box_camera_projection(&camera, 100, 100, proj);
    glm_mat4_mulv(proj, near, out);
    test_flt(out[2] / out[3], 1.0f);
    glm_mat4_mulv(proj, far, out);
    test_assert(fabsf(out[2] / out[3]) < 1e-6f);
    glm_mat4_mulv(proj, mid, out);
    test_assert(out[2] / out[3] > 0);
    test_assert(out[2] / out[3] < 1);
}

void Camera_perspective_defaults(void) {
    // renderer/views
    Camera camera = {0};
    mat4 proj;
    vec4 near = {0, 0, -0.1f, 1}, top = {0, 1, -1, 1}, out;
    box_camera_projection(&camera, 100, 100, proj);
    glm_mat4_mulv(proj, near, out);
    test_flt(out[2] / out[3], 1.0f);
    glm_mat4_mulv(proj, top, out);
    test_flt(out[1] / out[3], 1.0f / tanf(glm_rad(30)));
    test_flt(proj[2][3], -1.0f);
}

void Camera_perspective_aspect_follows_surface(void) {
    // renderer/views
    Camera camera = {.fov = 90};
    mat4 proj;
    box_camera_projection(&camera, 200, 100, proj);
    test_flt(proj[1][1], 1.0f);
    test_flt(proj[0][0], 0.5f);
    box_camera_projection(&camera, 100, 400, proj);
    test_flt(proj[0][0], 4.0f);
}

void Camera_aspect_ratio_overrides_surface(void) {
    // renderer/views
    Camera camera = {.fov = 90, .aspect_ratio = 4};
    mat4 proj;
    box_camera_projection(&camera, 200, 100, proj);
    test_flt(proj[1][1], 1.0f);
    test_flt(proj[0][0], 0.25f);
}

void Camera_ortho_near_far(void) {
    // renderer/views
    Camera camera = {.ortho = true, .near_ = 1, .far_ = 11};
    mat4 proj;
    vec4 near = {0, 0, -1, 1}, far = {0, 0, -11, 1}, mid = {0, 0, -6, 1}, out;
    box_camera_projection(&camera, 100, 100, proj);
    glm_mat4_mulv(proj, near, out);
    test_flt(out[3], 1.0f);
    test_flt(out[2], 1.0f);
    glm_mat4_mulv(proj, far, out);
    test_assert(fabsf(out[2]) < 1e-6f);
    glm_mat4_mulv(proj, mid, out);
    test_flt(out[2], 0.5f);
}

void Camera_ortho_default_far(void) {
    // renderer/views
    Camera camera = {.ortho = true};
    mat4 proj;
    vec4 near = {0, 0, -0.1f, 1}, far = {0, 0, -1000, 1}, out;
    box_camera_projection(&camera, 100, 100, proj);
    glm_mat4_mulv(proj, near, out);
    test_flt(out[2], 1.0f);
    glm_mat4_mulv(proj, far, out);
    test_assert(fabsf(out[2]) < 1e-5f);
}

void Camera_ortho_height(void) {
    // renderer/views
    Camera camera = {.ortho = true, .ortho_height = 4};
    mat4 proj;
    vec4 corner = {4, 2, -1, 1}, out;
    box_camera_projection(&camera, 100, 50, proj);
    glm_mat4_mulv(proj, corner, out);
    test_flt(out[0], 1.0f);
    test_flt(out[1], 1.0f);
}

void Camera_ortho_default_height(void) {
    // renderer/views
    Camera camera = {.ortho = true};
    mat4 proj;
    vec4 top = {0, 5, -1, 1}, out;
    box_camera_projection(&camera, 100, 100, proj);
    glm_mat4_mulv(proj, top, out);
    test_flt(out[1], 1.0f);
}

void Camera_ortho_scale(void) {
    // renderer/views
    Camera camera = {.ortho = true, .ortho_height = 4, .ortho_scale = 10};
    mat4 proj;
    vec4 corner = {10, 5, -1, 1}, out;
    box_camera_projection(&camera, 200, 100, proj);
    glm_mat4_mulv(proj, corner, out);
    test_flt(out[0], 1.0f);
    test_flt(out[1], 1.0f);
    box_camera_projection(&camera, 400, 200, proj);
    glm_mat4_mulv(proj, corner, out);
    test_flt(out[0], 0.5f);
    test_flt(out[1], 0.5f);
}
