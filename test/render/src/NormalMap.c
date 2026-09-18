/**
 * @file NormalMap.c
 * @brief Tests for the screen space tangent frame that normal mapping builds.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/renderer/normal.h"

void NormalMap_frame_of_a_camera_facing_surface(void)
{
    // textures/normal_frame
    vec3 n = vec3_new(0, 0, -1);
    vec3 dp1 = vec3_new(0.25f, 0, 0);
    vec3 dp2 = vec3_new(0, -0.25f, 0);
    vec2 duv1 = { 0.125f, 0 };
    vec2 duv2 = { 0, -0.125f };
    vec3 t, b;

    box_cotangent_frame(n, dp1, dp2, duv1, duv2, &t, &b);

    test_flt(t.x, 1.0f);
    test_flt(t.y, 0.0f);
    test_flt(t.z, 0.0f);
    test_flt(b.x, 0.0f);
    test_flt(b.y, 1.0f);
    test_flt(b.z, 0.0f);
}

void NormalMap_frame_is_the_same_with_screen_y_up(void)
{
    // textures/normal_frame
    vec3 n = vec3_new(0, 0, -1);
    vec3 dp1 = vec3_new(0.25f, 0, 0);
    vec3 dp2 = vec3_new(0, 0.25f, 0);
    vec2 duv1 = { 0.125f, 0 };
    vec2 duv2 = { 0, 0.125f };
    vec3 t, b;

    box_cotangent_frame(n, dp1, dp2, duv1, duv2, &t, &b);

    test_flt(t.x, 1.0f);
    test_flt(t.y, 0.0f);
    test_flt(b.x, 0.0f);
    test_flt(b.y, 1.0f);
}

void NormalMap_frame_is_the_same_for_a_flipped_normal(void)
{
    // textures/normal_frame
    vec3 n = vec3_new(0, 0, 1);
    vec3 dp1 = vec3_new(0.25f, 0, 0);
    vec3 dp2 = vec3_new(0, -0.25f, 0);
    vec2 duv1 = { 0.125f, 0 };
    vec2 duv2 = { 0, -0.125f };
    vec3 t, b;

    box_cotangent_frame(n, dp1, dp2, duv1, duv2, &t, &b);

    test_flt(t.x, 1.0f);
    test_flt(t.y, 0.0f);
    test_flt(b.x, 0.0f);
    test_flt(b.y, 1.0f);
}

void NormalMap_frame_follows_swapped_uv_axes(void)
{
    // textures/normal_frame
    vec3 n = vec3_new(0, 0, -1);
    vec3 dp1 = vec3_new(0.25f, 0, 0);
    vec3 dp2 = vec3_new(0, -0.25f, 0);
    vec2 duv1 = { 0, 0.125f };
    vec2 duv2 = { -0.125f, 0 };
    vec3 t, b;

    box_cotangent_frame(n, dp1, dp2, duv1, duv2, &t, &b);

    test_flt(t.x, 0.0f);
    test_flt(t.y, 1.0f);
    test_flt(b.x, 1.0f);
    test_flt(b.y, 0.0f);
}

void NormalMap_flat_sample_keeps_the_normal(void)
{
    // textures/normal_frame
    vec3 n = vec3_new(0, 0, -1);
    vec3 dp1 = vec3_new(0.25f, 0, 0);
    vec3 dp2 = vec3_new(0, -0.25f, 0);
    vec2 duv1 = { 0.125f, 0 };
    vec2 duv2 = { 0, -0.125f };
    vec3 r = box_perturb_normal(n, dp1, dp2, duv1, duv2, vec3_new(0, 0, 1));

    test_flt(r.x, 0.0f);
    test_flt(r.y, 0.0f);
    test_flt(r.z, -1.0f);
}

void NormalMap_sample_leans_along_the_u_tangent(void)
{
    // textures/normal_frame
    vec3 n = vec3_new(0, 0, -1);
    vec3 dp1 = vec3_new(0.25f, 0, 0);
    vec3 dp2 = vec3_new(0, -0.25f, 0);
    vec2 duv1 = { 0.125f, 0 };
    vec2 duv2 = { 0, -0.125f };
    vec3 r = box_perturb_normal(n, dp1, dp2, duv1, duv2, vec3_new(0.6f, 0, 0.8f));

    test_flt(r.x, 0.6f);
    test_flt(r.y, 0.0f);
    test_flt(r.z, -0.8f);
}

void NormalMap_sample_leans_along_the_v_tangent(void)
{
    // textures/normal_frame
    vec3 n = vec3_new(0, 0, -1);
    vec3 dp1 = vec3_new(0.25f, 0, 0);
    vec3 dp2 = vec3_new(0, -0.25f, 0);
    vec2 duv1 = { 0.125f, 0 };
    vec2 duv2 = { 0, -0.125f };
    vec3 r = box_perturb_normal(n, dp1, dp2, duv1, duv2, vec3_new(0, 0.6f, 0.8f));

    test_flt(r.x, 0.0f);
    test_flt(r.y, 0.6f);
    test_flt(r.z, -0.8f);
}

void NormalMap_degenerate_derivatives_keep_the_normal(void)
{
    // textures/normal_frame
    vec3 n = vec3_new(0, 0, -1);
    vec3 zero = vec3_new(0, 0, 0);
    vec2 duv = { 0, 0 };
    vec3 r = box_perturb_normal(n, zero, zero, duv, duv, vec3_new(0.6f, 0, 0.8f));

    test_flt(r.x, 0.0f);
    test_flt(r.y, 0.0f);
    test_flt(r.z, -1.0f);
}
