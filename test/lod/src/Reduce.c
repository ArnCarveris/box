/**
 * @file Reduce.c
 * @brief Tests for the level ladder of a parametric shape and the level it takes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Reduce_the_ladder_halves_down_to_the_floor(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;

    test_int(box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder), 4);
    test_int(ladder.count, 4);
    test_int(ladder.segments[0], 32);
    test_int(ladder.segments[1], 16);
    test_int(ladder.segments[2], 8);
    test_int(ladder.segments[3], 4);
}

void Reduce_an_uneven_count_halves_too(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;

    test_int(box_lod_ladder(20, BOX_LOD_MIN_SEGMENTS, &ladder), 3);
    test_int(ladder.segments[0], 20);
    test_int(ladder.segments[1], 10);
    test_int(ladder.segments[2], 5);
}

void Reduce_a_shape_at_its_floor_has_one_level(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;

    test_int(box_lod_ladder(4, BOX_LOD_MIN_SEGMENTS, &ladder), 1);
    test_int(ladder.segments[0], 4);
}

void Reduce_a_long_ladder_stops_at_its_levels(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;

    test_int(box_lod_ladder(512, BOX_LOD_MIN_SEGMENTS, &ladder), BOX_LOD_LEVELS);
    test_int(ladder.segments[0], 512);
    test_int(ladder.segments[BOX_LOD_LEVELS - 1], 512 >> (BOX_LOD_LEVELS - 1));
}

void Reduce_a_degenerate_count_has_no_ladder(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;

    test_int(box_lod_ladder(2, BOX_LOD_MIN_SEGMENTS, &ladder), 0);
    test_int(ladder.count, 0);
}

void Reduce_a_lower_floor_buys_more_levels(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;

    test_int(box_lod_ladder(8, 1, &ladder), 4);
    test_int(ladder.segments[3], 1);
}

void Reduce_the_chord_error_grows_with_the_radius(void)
{
    // lod/primitive_reduction
    float one = box_lod_chord_error(16, 1.0f);
    float four = box_lod_chord_error(16, 4.0f);

    test_assert(one > 0.0f);
    test_flt(four, one * 4.0f);
    test_flt(box_lod_chord_error(16, 0.0f), 0.0f);
}

void Reduce_more_segments_lower_the_chord_error(void)
{
    // lod/primitive_reduction
    float coarse = box_lod_chord_error(8, 1.0f);
    float fine = box_lod_chord_error(32, 1.0f);

    test_assert(fine < coarse);
    test_assert(fabsf(coarse - (1.0f - cosf(3.14159265f / 8.0f))) < 0.0001f);
    test_flt(box_lod_chord_error(2, 1.0f), 0.0f);
}

void Reduce_a_near_shape_keeps_the_authored_level(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 1080,
        .radius = 1, .threshold = 2, .current = -1
    };

    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    desc.bounds.min = vec3_new(-1, -1, 4);
    desc.bounds.max = vec3_new(1, 1, 6);
    test_int(box_lod_reduce(&desc), 0);
}

void Reduce_a_distant_shape_takes_the_coarsest_level(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 1080,
        .radius = 1, .threshold = 2, .current = -1
    };

    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    desc.bounds.min = vec3_new(-1, -1, 400);
    desc.bounds.max = vec3_new(1, 1, 402);
    test_int(box_lod_reduce(&desc), 3);
}

void Reduce_a_quarter_arc_is_measured_as_its_circle(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t corner;
    box_lod_ladder_t circle;
    box_lod_reduce_desc_t quarter = {
        .ladder = &corner, .camera = { .fov = 60 }, .viewport_px = 1080,
        .radius = 1, .span = 0.25f, .threshold = 2, .current = -1
    };
    box_lod_reduce_desc_t full = {
        .ladder = &circle, .camera = { .fov = 60 }, .viewport_px = 1080,
        .radius = 1, .threshold = 2, .current = -1
    };

    box_lod_ladder(8, 1, &corner);
    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &circle);
    quarter.bounds.min = vec3_new(-1, -1, 60);
    quarter.bounds.max = vec3_new(1, 1, 62);
    full.bounds = quarter.bounds;
    test_int(box_lod_reduce(&quarter), box_lod_reduce(&full));
    test_flt(box_lod_chord_error(32, 1.0f), box_lod_chord_error(8 * 4, 1.0f));
}

void Reduce_the_band_holds_the_level_of_the_previous_frame(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 1080,
        .radius = 1, .threshold = 2, .hysteresis = 0.25f
    };

    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    desc.bounds.min = vec3_new(-1, -1, 10);
    desc.bounds.max = vec3_new(1, 1, 12);
    desc.current = -1;
    test_int(box_lod_reduce(&desc), 1);
    desc.current = 0;
    test_int(box_lod_reduce(&desc), 0);
    desc.current = 1;
    test_int(box_lod_reduce(&desc), 1);
}

void Reduce_a_zero_threshold_holds_the_authored_level(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 1080,
        .radius = 1, .current = 3
    };

    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    desc.bounds.min = vec3_new(-1, -1, 400);
    desc.bounds.max = vec3_new(1, 1, 402);
    test_int(box_lod_reduce(&desc), 0);
}

void Reduce_an_orthographic_camera_ignores_the_distance(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .ortho_height = 20, .orthographic = true },
        .viewport_px = 1080, .radius = 1, .threshold = 2, .current = -1
    };

    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    desc.bounds.min = vec3_new(-1, -1, 4);
    desc.bounds.max = vec3_new(1, 1, 6);

    int32_t near_level = box_lod_reduce(&desc);
    desc.bounds.min = vec3_new(-1, -1, 400);
    desc.bounds.max = vec3_new(1, 1, 402);
    test_int(box_lod_reduce(&desc), near_level);
    test_int(near_level, 1);
}

void Reduce_a_ladder_of_one_level_never_moves(void)
{
    // lod/primitive_reduction
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 1080,
        .radius = 1, .threshold = 2, .current = -1
    };

    box_lod_ladder(4, BOX_LOD_MIN_SEGMENTS, &ladder);
    desc.bounds.min = vec3_new(-1, -1, 4000);
    desc.bounds.max = vec3_new(1, 1, 4002);
    test_int(box_lod_reduce(&desc), 0);
}
