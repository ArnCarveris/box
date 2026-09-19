/**
 * @file Walk.c
 * @brief Tests that record the selected level of a camera walk frame by frame.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Walk_a_walk_towards_and_away_never_oscillates(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-2, -2, -2), .max = vec3_new(2, 2, 2) };
    float errors[4] = {0.0f, 0.05f, 0.2f, 0.8f};
    int32_t levels[400];
    int32_t level = -1;
    int32_t direction = 0;
    int32_t changes = 0;
    int32_t lowest = 3;
    int32_t i;

    for (i = 0; i < 400; i ++) {
        float distance = i < 200 ?
            600.0f - (float)i * 3.0f : 600.0f - (float)(399 - i) * 3.0f;
        box_lod_camera_t camera = { .position = vec3_new(0, 0, -(2.0f + distance)),
            .fov = 60 };
        float scale = box_lod_error_scale(&bounds, &camera, 1080.0f);
        float px[4];
        int32_t j;
        for (j = 0; j < 4; j ++) {
            px[j] = errors[j] * scale;
        }

        box_lod_select_desc_t desc = { .error_px = px, .count = 4, .current = level,
            .threshold = 2.0f, .hysteresis = 0.25f };
        level = box_lod_select(&desc);
        levels[i] = level;
        if (level < lowest) {
            lowest = level;
        }

        if (i && levels[i] != levels[i - 1]) {
            int32_t step = levels[i] > levels[i - 1] ? 1 : -1;
            test_int(levels[i] - levels[i - 1], step);
            if (direction && step != direction) {
                changes ++;
            }

            direction = step;
        }
    }

    test_int(levels[0], 3);
    test_int(lowest, 0);
    test_int(levels[399], 3);
    test_int(changes, 1);
}

void Walk_a_hovering_camera_holds_its_level(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-2, -2, -2), .max = vec3_new(2, 2, 2) };
    float errors[3] = {0.0f, 0.2f, 1.0f};
    int32_t banded_switches = 0;
    int32_t bare_switches = 0;
    int32_t banded = -1;
    int32_t bare = -1;
    int32_t i;

    for (i = 0; i < 40; i ++) {
        float distance = (i % 2) ? 94.5f : 92.5f;
        box_lod_camera_t camera = { .position = vec3_new(0, 0, -(2.0f + distance)),
            .fov = 60 };
        float scale = box_lod_error_scale(&bounds, &camera, 1080.0f);
        float px[3];
        int32_t j;
        for (j = 0; j < 3; j ++) {
            px[j] = errors[j] * scale;
        }

        box_lod_select_desc_t held = { .error_px = px, .count = 3, .current = banded,
            .threshold = 2.0f, .hysteresis = 0.25f };
        box_lod_select_desc_t plain = { .error_px = px, .count = 3, .current = bare,
            .threshold = 2.0f };
        int32_t next_banded = box_lod_select(&held);
        int32_t next_bare = box_lod_select(&plain);
        if (i && next_banded != banded) {
            banded_switches ++;
        }

        if (i && next_bare != bare) {
            bare_switches ++;
        }

        banded = next_banded;
        bare = next_bare;
    }

    test_int(banded_switches, 0);
    test_assert(bare_switches > 30);
}

void Walk_an_orthographic_zoom_never_oscillates(void)
{
    // lod/screen_space_error
    box_lod_bounds_t bounds = { .min = vec3_new(-2, -2, -2), .max = vec3_new(2, 2, 2) };
    float errors[4] = {0.0f, 0.05f, 0.2f, 0.8f};
    int32_t levels[400];
    int32_t level = -1;
    int32_t direction = 0;
    int32_t changes = 0;
    int32_t lowest = 3;
    int32_t i;

    for (i = 0; i < 400; i ++) {
        float height = i < 200 ?
            600.0f - (float)i * 3.0f : 600.0f - (float)(399 - i) * 3.0f;
        box_lod_camera_t camera = { .position = vec3_new(0, 0, -50),
            .ortho_height = height, .orthographic = true };
        float scale = box_lod_error_scale(&bounds, &camera, 1080.0f);
        float px[4];
        int32_t j;
        for (j = 0; j < 4; j ++) {
            px[j] = errors[j] * scale;
        }

        box_lod_select_desc_t desc = { .error_px = px, .count = 4, .current = level,
            .threshold = 2.0f, .hysteresis = 0.25f };
        level = box_lod_select(&desc);
        levels[i] = level;
        if (level < lowest) {
            lowest = level;
        }

        if (i && levels[i] != levels[i - 1]) {
            int32_t step = levels[i] > levels[i - 1] ? 1 : -1;
            test_int(levels[i] - levels[i - 1], step);
            if (direction && step != direction) {
                changes ++;
            }

            direction = step;
        }
    }

    test_int(levels[0], 3);
    test_int(lowest, 0);
    test_int(levels[399], 3);
    test_int(changes, 1);
}
