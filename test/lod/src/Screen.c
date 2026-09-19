/**
 * @file Screen.c
 * @brief Tests for the projected error of a node, for both camera projections.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Screen_an_error_projects_at_the_nearest_point(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t camera = { .position = vec3_new(0, 0, -11), .fov = 60 };

    float px = box_lod_error_px(&bounds, 0.1f, &camera, 1000.0f);

    test_assert(fabsf(px - 8.660254f) < 1e-4f);
}

void Screen_twice_the_distance_halves_the_error(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t near_camera = { .position = vec3_new(0, 0, -11), .fov = 60 };
    box_lod_camera_t far_camera = { .position = vec3_new(0, 0, -21), .fov = 60 };

    float near_px = box_lod_error_px(&bounds, 0.1f, &near_camera, 1000.0f);
    float far_px = box_lod_error_px(&bounds, 0.1f, &far_camera, 1000.0f);

    test_assert(fabsf(far_px - 4.330127f) < 1e-4f);
    test_assert(fabsf(near_px - 2.0f * far_px) < 1e-4f);
}

void Screen_the_nearest_point_is_not_the_center(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t camera = { .position = vec3_new(5, 0, -11), .fov = 60 };

    float px = box_lod_error_px(&bounds, 0.1f, &camera, 1000.0f);

    test_assert(fabsf(px - 8.040844f) < 1e-4f);
}

void Screen_a_camera_inside_the_bounds_stays_finite(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t camera = { .position = vec3_new(0.5f, 0, 0), .fov = 60 };

    float px = box_lod_error_px(&bounds, 0.1f, &camera, 1000.0f);

    test_assert(isfinite(px));
    test_assert(px > 1000.0f);
}

void Screen_a_wider_fov_lowers_the_error(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t narrow = { .position = vec3_new(0, 0, -11), .fov = 60 };
    box_lod_camera_t wide = { .position = vec3_new(0, 0, -11), .fov = 90 };

    float narrow_px = box_lod_error_px(&bounds, 0.1f, &narrow, 1000.0f);
    float wide_px = box_lod_error_px(&bounds, 0.1f, &wide, 1000.0f);

    test_assert(fabsf(wide_px - 5.0f) < 1e-4f);
    test_assert(wide_px < narrow_px);
}

void Screen_a_taller_viewport_raises_the_error(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t camera = { .position = vec3_new(0, 0, -11), .fov = 60 };

    float small = box_lod_error_px(&bounds, 0.1f, &camera, 1000.0f);
    float large = box_lod_error_px(&bounds, 0.1f, &camera, 2000.0f);

    test_assert(fabsf(large - 2.0f * small) < 1e-4f);
}

void Screen_an_orthographic_error_ignores_the_distance(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t near_camera = { .position = vec3_new(0, 0, -11),
        .ortho_height = 10, .orthographic = true };
    box_lod_camera_t far_camera = { .position = vec3_new(0, 0, -1011),
        .ortho_height = 10, .orthographic = true };

    float near_px = box_lod_error_px(&bounds, 0.1f, &near_camera, 1000.0f);
    float far_px = box_lod_error_px(&bounds, 0.1f, &far_camera, 1000.0f);

    test_assert(fabsf(near_px - 10.0f) < 1e-4f);
    test_assert(fabsf(far_px - near_px) < 1e-6f);
}

void Screen_an_orthographic_error_follows_the_height(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t zoomed = { .position = vec3_new(0, 0, -11),
        .ortho_height = 10, .orthographic = true };
    box_lod_camera_t wide = { .position = vec3_new(0, 0, -11),
        .ortho_height = 20, .orthographic = true };

    float zoomed_px = box_lod_error_px(&bounds, 0.1f, &zoomed, 1000.0f);
    float wide_px = box_lod_error_px(&bounds, 0.1f, &wide, 1000.0f);

    test_assert(fabsf(zoomed_px - 2.0f * wide_px) < 1e-4f);
}

void Screen_the_scale_is_the_error_of_one_unit(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t camera = { .position = vec3_new(0, 0, -11), .fov = 60 };

    float scale = box_lod_error_scale(&bounds, &camera, 1000.0f);
    float unit = box_lod_error_px(&bounds, 1.0f, &camera, 1000.0f);
    float tenth = box_lod_error_px(&bounds, 0.1f, &camera, 1000.0f);

    test_assert(fabsf(scale - 86.602540f) < 1e-3f);
    test_assert(fabsf(unit - scale) < 1e-4f);
    test_assert(fabsf(tenth - 0.1f * scale) < 1e-4f);
}

void Screen_a_representation_without_error_is_free(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t camera = { .position = vec3_new(0, 0, -11), .fov = 60 };

    test_flt(box_lod_error_px(&bounds, 0.0f, &camera, 1000.0f), 0);
    test_flt(box_lod_error_px(&bounds, 0.1f, &camera, 0.0f), 0);
}

void Screen_zero_fields_take_the_camera_defaults(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_camera_t implicit = { .position = vec3_new(0, 0, -11) };
    box_lod_camera_t explicit_camera = { .position = vec3_new(0, 0, -11), .fov = BOX_LOD_FOV };
    box_lod_camera_t ortho = { .position = vec3_new(0, 0, -11), .orthographic = true };
    box_lod_camera_t ortho_explicit = { .position = vec3_new(0, 0, -11),
        .ortho_height = BOX_LOD_ORTHO_HEIGHT, .orthographic = true };

    test_assert(fabsf(box_lod_error_px(&bounds, 0.1f, &implicit, 1000.0f) -
        box_lod_error_px(&bounds, 0.1f, &explicit_camera, 1000.0f)) < 1e-6f);
    test_assert(fabsf(box_lod_error_px(&bounds, 0.1f, &ortho, 1000.0f) -
        box_lod_error_px(&bounds, 0.1f, &ortho_explicit, 1000.0f)) < 1e-6f);
}

void Screen_reversed_bounds_measure_the_same(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-1, -1, -1), .max = vec3_new(1, 1, 1) };
    box_lod_bounds_t reversed = { .min = vec3_new(1, 1, 1), .max = vec3_new(-1, -1, -1) };
    box_lod_camera_t camera = { .position = vec3_new(5, 0, -11), .fov = 60 };

    float px = box_lod_error_px(&bounds, 0.1f, &camera, 1000.0f);
    float reversed_px = box_lod_error_px(&reversed, 0.1f, &camera, 1000.0f);

    test_assert(fabsf(px - reversed_px) < 1e-6f);
}
