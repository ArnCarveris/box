/**
 * @file DepthOfField.c
 * @brief Tests for the C twin of the circle of confusion of the depth of field: at the focus, the
 * edges of the sharp range, the ends of the transitions, the sky, and the view depth of
 * perspective, infinite far and orthographic cameras; the band, transition, symmetry and angle of
 * the tilt shift.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Get the default depth of field settings.
 *
 * @return The settings a constructed DepthOfField has
 */
static DepthOfField dof_defaults(void) {
    return (DepthOfField){
        .focus_distance = 10, .focus_range = 2, .near_transition = 4, .far_transition = 20,
        .max_radius = 0.01f, .near_blur = true
    };
}

/** Get the default tilt shift settings.
 *
 * @return The settings a constructed DepthOfField with mode DofTiltShift has
 */
static DepthOfField dof_tilt_defaults(void) {
    DepthOfField result = dof_defaults();
    result.mode = DofTiltShift;
    result.tilt_center = 0.55f;
    result.tilt_band = 0.08f;
    result.tilt_transition = 0.3f;
    return result;
}

/** Get the reversed Z depth of a view depth with a perspective projection.
 *
 * @param n The near plane
 * @param f The far plane, 0 for none
 * @param z The view depth
 * @return The depth
 */
static float dof_perspective_depth(
    float n,
    float f,
    float z)
{
    return f > 0 ? n * (f - z) / (z * (f - n)) : n / z;
}

void DepthOfField_focus_is_sharp(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    test_flt(box_render_depth_of_field_coc(&d, 10), 0);
    test_flt(box_render_depth_of_field_coc(&d, 9.5f), 0);
    test_flt(box_render_depth_of_field_coc(&d, 10.5f), 0);

    d.focus_range = 0;
    test_flt(box_render_depth_of_field_coc(&d, 10), 0);
    test_assert(box_render_depth_of_field_coc(&d, 10.2f) > 0);
    test_assert(box_render_depth_of_field_coc(&d, 9.8f) < 0);
}

void DepthOfField_range_edges(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    // The sharp range is 9..11
    test_flt(box_render_depth_of_field_coc(&d, 9), 0);
    test_flt(box_render_depth_of_field_coc(&d, 11), 0);
    test_assert(box_render_depth_of_field_coc(&d, 8.9f) < 0);
    test_assert(box_render_depth_of_field_coc(&d, 8.9f) > -0.05f);
    test_assert(box_render_depth_of_field_coc(&d, 11.1f) > 0);
    test_assert(box_render_depth_of_field_coc(&d, 11.1f) < 0.01f);
}

void DepthOfField_transition_ends(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    // Near: 9 - 4 = 5 is full blur, halfway at 7
    test_flt(box_render_depth_of_field_coc(&d, 7), -0.5f);
    test_flt(box_render_depth_of_field_coc(&d, 5), -1);
    test_flt(box_render_depth_of_field_coc(&d, 1), -1);
    test_flt(box_render_depth_of_field_coc(&d, 0), -1);
    // Far: 11 + 20 = 31 is full blur, halfway at 21
    test_flt(box_render_depth_of_field_coc(&d, 21), 0.5f);
    test_flt(box_render_depth_of_field_coc(&d, 31), 1);
    test_flt(box_render_depth_of_field_coc(&d, 40), 1);
    test_flt(box_render_depth_of_field_coc(&d, 1e6f), 1);
}

void DepthOfField_hard_step(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    d.near_transition = 0;
    d.far_transition = 0;
    test_flt(box_render_depth_of_field_coc(&d, 8.99f), -1);
    test_flt(box_render_depth_of_field_coc(&d, 9), 0);
    test_flt(box_render_depth_of_field_coc(&d, 11), 0);
    test_flt(box_render_depth_of_field_coc(&d, 11.01f), 1);
}

void DepthOfField_near_blur_off(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    d.near_blur = false;
    test_flt(box_render_depth_of_field_coc(&d, 5), 0);
    test_flt(box_render_depth_of_field_coc(&d, 0.5f), 0);
    test_flt(box_render_depth_of_field_coc(&d, 10), 0);
    test_flt(box_render_depth_of_field_coc(&d, 40), 1);
}

void DepthOfField_sky_is_far(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    float k[4];
    box_render_depth_of_field_linearize(&(Camera){.fov = 60, .near_ = 0.1f, .far_ = 100}, k);
    test_flt(box_render_depth_of_field_coc(&d, INFINITY), 1);
    test_flt(box_render_depth_of_field_coc_depth(&d, k, 0), 1);

    // Depth 0 is the sky whatever the transition, unless it never ends
    d.far_transition = 1e6f;
    test_flt(box_render_depth_of_field_coc_depth(&d, k, 0), 1);
    d.far_transition = 0;
    test_flt(box_render_depth_of_field_coc_depth(&d, k, 0), 1);
    d.far_transition = INFINITY;
    test_flt(box_render_depth_of_field_coc_depth(&d, k, 0), 0);

    // Also without near blur, and with an orthographic camera
    d = dof_defaults();
    d.near_blur = false;
    box_render_depth_of_field_linearize(&(Camera){.ortho = true, .near_ = 0.1f, .far_ = 100}, k);
    test_flt(box_render_depth_of_field_coc_depth(&d, k, 0), 1);
}

void DepthOfField_perspective_depth(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    float k[4];
    box_render_depth_of_field_linearize(&(Camera){.fov = 60, .near_ = 0.1f, .far_ = 100}, k);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k, 1) - 0.1f) < 1e-4f);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k,
        dof_perspective_depth(0.1f, 100, 10)) - 10) < 1e-3f);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k,
        dof_perspective_depth(0.1f, 100, 40)) - 40) < 1e-2f);
    test_assert(fabsf(box_render_depth_of_field_coc_depth(&d, k,
        dof_perspective_depth(0.1f, 100, 10))) < 1e-4f);
    test_assert(fabsf(box_render_depth_of_field_coc_depth(&d, k,
        dof_perspective_depth(0.1f, 100, 21)) - 0.5f) < 1e-3f);
    test_flt(box_render_depth_of_field_coc_depth(&d, k, dof_perspective_depth(0.1f, 100, 5)),
        -1);
}

void DepthOfField_perspective_infinite_far(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    float k[4];
    box_render_depth_of_field_linearize(&(Camera){.fov = 60, .near_ = 0.1f}, k);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k, 1) - 0.1f) < 1e-4f);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k, 0.01f) - 10) < 1e-3f);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k, 0.1f / 40) - 40) < 1e-2f);
    test_assert(fabsf(box_render_depth_of_field_coc_depth(&d, k, 0.01f)) < 1e-4f);
    test_assert(fabsf(box_render_depth_of_field_coc_depth(&d, k, 0.1f / 7) + 0.5f) < 1e-3f);
    test_flt(box_render_depth_of_field_coc_depth(&d, k, 0.1f / 40), 1);
    test_flt(box_render_depth_of_field_coc_depth(&d, k, 0), 1);
}

void DepthOfField_orthographic(void) {
    // lighting/effects
    DepthOfField d = dof_defaults();
    float k[4];
    box_render_depth_of_field_linearize(&(Camera){.ortho = true, .near_ = 0.1f, .far_ = 100,
        .ortho_height = 10}, k);
    // Depth is linear: (far - z) / (far - near)
    test_assert(fabsf(box_render_depth_of_field_view_depth(k, 1) - 0.1f) < 1e-4f);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k, (100 - 10) / 99.9f) - 10) < 1e-3f);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k, 1e-6f) - 100) < 1e-3f);
    test_assert(fabsf(box_render_depth_of_field_coc_depth(&d, k, (100 - 10) / 99.9f)) < 1e-4f);
    test_assert(fabsf(box_render_depth_of_field_coc_depth(&d, k, (100 - 7) / 99.9f) + 0.5f)
        < 1e-3f);
    test_assert(fabsf(box_render_depth_of_field_coc_depth(&d, k, (100 - 21) / 99.9f) - 0.5f)
        < 1e-3f);
    test_flt(box_render_depth_of_field_coc_depth(&d, k, (100 - 40) / 99.9f), 1);

    // A near plane behind the camera
    box_render_depth_of_field_linearize(&(Camera){.ortho = true, .near_ = -50, .far_ = 50}, k);
    test_assert(fabsf(box_render_depth_of_field_view_depth(k, 0.4f) - 10) < 1e-3f);
}

void DepthOfField_settings_clamped(void) {
    // lighting/effects
    DepthOfField out;
    box_render_depth_of_field_settings(&(DepthOfField){.focus_distance = -1, .focus_range = -2,
        .near_transition = -3, .far_transition = -4, .max_radius = 0.2f, .near_blur = true},
        &out);
    test_flt(out.focus_distance, 0);
    test_flt(out.focus_range, 0);
    test_flt(out.near_transition, 0);
    test_flt(out.far_transition, 0);
    test_flt(out.max_radius, 0.05f);
    test_bool(out.near_blur, true);

    box_render_depth_of_field_settings(&(DepthOfField){.focus_distance = 3, .focus_range = 1,
        .near_transition = 2, .far_transition = 5, .max_radius = -1}, &out);
    test_flt(out.focus_distance, 3);
    test_flt(out.focus_range, 1);
    test_flt(out.near_transition, 2);
    test_flt(out.far_transition, 5);
    test_flt(out.max_radius, 0);
    test_bool(out.near_blur, false);
}

void DepthOfField_tilt_inside_band(void) {
    // lighting/effects
    DepthOfField d = dof_tilt_defaults();
    // The band is 0.47..0.63 of the image height, at any x
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.55f, 16.0f / 9.0f), 0);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.0f, 0.5f, 16.0f / 9.0f), 0);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 1.0f, 0.62f, 16.0f / 9.0f), 0);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.3f, 0.48f, 1.0f), 0);
    test_assert(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.65f, 1.0f) > 0);
    test_assert(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.65f, 1.0f) < 0.05f);
}

void DepthOfField_tilt_past_transition(void) {
    // lighting/effects
    DepthOfField d = dof_tilt_defaults();
    // Full blur from 0.38 away from the center: above 0.17, below 0.93
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.0f, 1.0f), 1);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.16f, 1.0f), 1);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.94f, 1.0f), 1);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.5f, 1.0f, 1.0f), 1);
    // Halfway through the transition, smoothstep is 0.5
    test_assert(fabsf(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.55f + 0.23f, 1.0f) - 0.5f)
        < 1e-4f);

    // A transition of 0 is a hard step at the band edge
    d.tilt_transition = 0;
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.62f, 1.0f), 0);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.64f, 1.0f), 1);
}

void DepthOfField_tilt_symmetric(void) {
    // lighting/effects
    DepthOfField d = dof_tilt_defaults();
    float o;
    for (o = 0; o < 0.45f; o += 0.05f) {
        float above = box_render_depth_of_field_tilt_coc(&d, 0.25f, 0.55f - o, 2.0f);
        float below = box_render_depth_of_field_tilt_coc(&d, 0.75f, 0.55f + o, 2.0f);
        test_assert(fabsf(above - below) < 1e-5f);
        test_assert(above >= 0);
    }

    // Increasing away from the band on both sides
    test_assert(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.3f, 1.0f) >
        box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.4f, 1.0f));
    test_assert(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.8f, 1.0f) >
        box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.7f, 1.0f));
}

void DepthOfField_tilt_rotated(void) {
    // lighting/effects
    DepthOfField d = dof_tilt_defaults();
    d.tilt_center = 0.5f;
    // A quarter turn makes the band vertical through the middle of the image
    d.tilt_angle = (float)GLM_PI_2;
    test_assert(box_render_depth_of_field_tilt_coc(&d, 0.5f, 0.0f, 2.0f) < 1e-5f);
    test_assert(box_render_depth_of_field_tilt_coc(&d, 0.5f, 1.0f, 2.0f) < 1e-5f);
    // 0.25 of the width is 0.5 of the height away on a 2:1 image, past the transition
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.25f, 0.5f, 2.0f), 1);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.75f, 0.5f, 2.0f), 1);

    // An eighth turn counterclockwise: the line rises to the right, so up and right is sharp
    d.tilt_angle = (float)GLM_PI_4;
    test_assert(box_render_depth_of_field_tilt_coc(&d, 0.8f, 0.2f, 1.0f) < 1e-5f);
    test_assert(box_render_depth_of_field_tilt_coc(&d, 0.2f, 0.8f, 1.0f) < 1e-5f);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.2f, 0.2f, 1.0f), 1);
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.8f, 0.8f, 1.0f), 1);
    // Unrotated, the same points are at the same distance from the band
    d.tilt_angle = 0;
    test_flt(box_render_depth_of_field_tilt_coc(&d, 0.8f, 0.2f, 1.0f),
        box_render_depth_of_field_tilt_coc(&d, 0.2f, 0.2f, 1.0f));
}

void DepthOfField_tilt_settings(void) {
    // lighting/effects
    DepthOfField out;
    DepthOfField in = dof_tilt_defaults();
    in.tilt_band = -1;
    in.tilt_transition = -1;
    box_render_depth_of_field_settings(&in, &out);
    test_int(out.mode, DofTiltShift);
    test_flt(out.tilt_band, 0);
    test_flt(out.tilt_transition, 0);
    test_flt(out.tilt_center, 0.55f);
    // A tilt shift has no near field
    test_bool(out.near_blur, false);

    in = dof_defaults();
    box_render_depth_of_field_settings(&in, &out);
    test_int(out.mode, DofCamera);
    test_bool(out.near_blur, true);
}
