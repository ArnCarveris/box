/**
 * @file Coverage.c
 * @brief Tests for the analytic rounded rectangle the border shader evaluates.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Coverage_center_is_inside(void)
{
    // shapes_2d/primitives
    float radii[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    test_flt(box_shape2d_distance(0.0f, 0.0f, 2.0f, 1.0f, radii), -1.0f);
}

void Coverage_edge_is_zero(void)
{
    // shapes_2d/primitives
    float radii[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    test_flt(box_shape2d_distance(2.0f, 0.0f, 2.0f, 1.0f, radii), 0.0f);
    test_flt(box_shape2d_distance(0.0f, -1.0f, 2.0f, 1.0f, radii), 0.0f);
}

void Coverage_outside_is_positive(void)
{
    // shapes_2d/primitives
    float radii[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    test_flt(box_shape2d_distance(3.0f, 0.0f, 2.0f, 1.0f, radii), 1.0f);
    test_flt(box_shape2d_distance(0.0f, 4.0f, 2.0f, 1.0f, radii), 3.0f);
}

void Coverage_a_radius_cuts_the_corner(void)
{
    // shapes_2d/primitives
    float sharp[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float round[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    test_flt(box_shape2d_distance(1.0f, 1.0f, 1.0f, 1.0f, sharp), 0.0f);
    test_flt(box_shape2d_distance(1.0f, 1.0f, 1.0f, 1.0f, round), sqrtf(2.0f) - 1.0f);
    test_assert(box_shape2d_distance(0.0f, 0.9f, 1.0f, 1.0f, round) < 0.0f);
}

void Coverage_a_radius_is_clamped_to_the_short_side(void)
{
    // shapes_2d/primitives
    float huge[4] = { 100.0f, 100.0f, 100.0f, 100.0f };

    test_flt(box_shape2d_distance(0.0f, 0.0f, 4.0f, 1.0f, huge), -1.0f);
    test_flt(box_shape2d_distance(4.0f, 0.0f, 4.0f, 1.0f, huge), 0.0f);
}

void Coverage_corners_are_resolved_apart(void)
{
    // shapes_2d/primitives
    float radii[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    test_flt(box_shape2d_distance(-1.0f, 1.0f, 1.0f, 1.0f, radii), sqrtf(2.0f) - 1.0f);
    test_flt(box_shape2d_distance(1.0f, 1.0f, 1.0f, 1.0f, radii), 0.0f);
    test_flt(box_shape2d_distance(1.0f, -1.0f, 1.0f, 1.0f, radii), 0.0f);
    test_flt(box_shape2d_distance(-1.0f, -1.0f, 1.0f, 1.0f, radii), 0.0f);
}

void Coverage_is_one_inside(void)
{
    // shapes_2d/primitives
    test_flt(box_shape2d_coverage(-1.0f, 0.1f), 1.0f);
    test_flt(box_shape2d_coverage(-0.05f, 0.1f), 1.0f);
}

void Coverage_is_zero_outside(void)
{
    // shapes_2d/primitives
    test_flt(box_shape2d_coverage(1.0f, 0.1f), 0.0f);
    test_flt(box_shape2d_coverage(0.05f, 0.1f), 0.0f);
}

void Coverage_is_half_on_the_outline(void)
{
    // shapes_2d/primitives
    test_flt(box_shape2d_coverage(0.0f, 0.1f), 0.5f);
    test_flt(box_shape2d_coverage(0.0f, 4.0f), 0.5f);
    test_flt(box_shape2d_coverage(-0.025f, 0.1f), 0.75f);
}

void Coverage_a_hard_edge_has_no_fringe(void)
{
    // shapes_2d/primitives
    test_flt(box_shape2d_coverage(-0.001f, 0.0f), 1.0f);
    test_flt(box_shape2d_coverage(0.001f, 0.0f), 0.0f);
}
