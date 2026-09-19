/**
 * @file Hysteresis.c
 * @brief Tests for the level switch and the band it has to cross.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Hysteresis_without_a_current_level_the_threshold_decides(void)
{
    // lod/screen_space_error
    float levels[4] = {0.0f, 1.0f, 4.0f, 16.0f};
    box_lod_select_desc_t desc = { .error_px = levels, .count = 4, .current = -1,
        .threshold = 4.0f, .hysteresis = 0.25f };

    test_int(box_lod_select(&desc), 2);
}

void Hysteresis_a_level_holds_inside_the_band(void)
{
    // lod/screen_space_error
    float levels[4] = {0.0f, 1.0f, 4.0f, 16.0f};
    box_lod_select_desc_t desc = { .error_px = levels, .count = 4, .current = 2,
        .threshold = 4.0f, .hysteresis = 0.25f };

    test_int(box_lod_select(&desc), 2);

    desc.current = 1;

    test_int(box_lod_select(&desc), 1);
}

void Hysteresis_an_error_above_the_band_goes_finer(void)
{
    // lod/screen_space_error
    float levels[4] = {0.0f, 1.0f, 6.0f, 20.0f};
    box_lod_select_desc_t desc = { .error_px = levels, .count = 4, .current = 2,
        .threshold = 4.0f, .hysteresis = 0.25f };

    test_int(box_lod_select(&desc), 1);
}

void Hysteresis_an_error_below_the_band_goes_coarser(void)
{
    // lod/screen_space_error
    float levels[4] = {0.0f, 1.0f, 2.5f, 20.0f};
    box_lod_select_desc_t desc = { .error_px = levels, .count = 4, .current = 1,
        .threshold = 4.0f, .hysteresis = 0.25f };

    test_int(box_lod_select(&desc), 2);
}

void Hysteresis_the_band_only_widens_the_threshold(void)
{
    // lod/screen_space_error
    float levels[4] = {0.0f, 1.0f, 4.5f, 20.0f};
    box_lod_select_desc_t held = { .error_px = levels, .count = 4, .current = 2,
        .threshold = 4.0f, .hysteresis = 0.25f };
    box_lod_select_desc_t bare = { .error_px = levels, .count = 4, .current = 2,
        .threshold = 4.0f };

    test_int(box_lod_select(&held), 2);
    test_int(box_lod_select(&bare), 1);
}

void Hysteresis_without_a_band_the_switch_is_at_the_threshold(void)
{
    // lod/screen_space_error
    float levels[4] = {0.0f, 1.0f, 3.2f, 20.0f};
    box_lod_select_desc_t desc = { .error_px = levels, .count = 4, .current = 1,
        .threshold = 4.0f };

    test_int(box_lod_select(&desc), 2);

    desc.hysteresis = 0.25f;

    test_int(box_lod_select(&desc), 1);
}

void Hysteresis_the_finest_level_is_the_floor(void)
{
    // lod/screen_space_error
    float levels[3] = {10.0f, 20.0f, 30.0f};
    box_lod_select_desc_t desc = { .error_px = levels, .count = 3, .current = 2,
        .threshold = 4.0f, .hysteresis = 0.25f };

    test_int(box_lod_select(&desc), 0);

    desc.current = -1;

    test_int(box_lod_select(&desc), 0);
}

void Hysteresis_a_level_out_of_range_is_ignored(void)
{
    // lod/screen_space_error
    float levels[4] = {0.0f, 1.0f, 4.0f, 16.0f};
    box_lod_select_desc_t desc = { .error_px = levels, .count = 4, .current = 7,
        .threshold = 4.0f, .hysteresis = 0.25f };

    test_int(box_lod_select(&desc), 2);
}

void Hysteresis_a_single_level_is_always_selected(void)
{
    // lod/screen_space_error
    float levels[1] = {40.0f};
    box_lod_select_desc_t desc = { .error_px = levels, .count = 1, .current = 0,
        .threshold = 4.0f, .hysteresis = 0.25f };
    box_lod_select_desc_t empty = { .error_px = levels, .count = 0, .current = -1,
        .threshold = 4.0f };

    test_int(box_lod_select(&desc), 0);
    test_int(box_lod_select(&empty), 0);
}

void Hysteresis_a_coarser_threshold_selects_a_coarser_level(void)
{
    // lod/screen_space_error
    float levels[4] = {0.0f, 1.0f, 4.0f, 16.0f};
    box_lod_select_desc_t view = { .error_px = levels, .count = 4, .current = -1,
        .threshold = 2.0f, .hysteresis = 0.25f };
    box_lod_select_desc_t shadow = { .error_px = levels, .count = 4, .current = -1,
        .threshold = 16.0f, .hysteresis = 0.25f };

    test_int(box_lod_select(&view), 1);
    test_int(box_lod_select(&shadow), 3);
}
