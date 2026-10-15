/**
 * @file TextLines.c
 * @brief Text: small sizes, kerning pairs, line break edge cases and single line alignment.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void TextLines_small_sizes_scale_linearly(void) {
    // ui_fixes/text_tests
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    float sizes[5] = { 6, 8, 10, 12, 14 };
    ecs_entity_t texts[5];
    int32_t i;
    for (i = 0; i < 5; i ++) {
        texts[i] = ecs_new(world);
        ecs_set(world, texts[i], Text, {.value = "Hello world", .size = sizes[i],
            .font = ecs_lookup(world, "box.text.DefaultFont")});
    }

    ecs_progress(world, 0);

    const TextBounds *ref = ecs_get(world, texts[4], TextBounds);
    test_assert(ref->width > 14 * 3);
    for (i = 0; i < 4; i ++) {
        const TextBounds *b = ecs_get(world, texts[i], TextBounds);
        test_assert(b->width > 0);
        test_assert(fabsf(b->width / sizes[i] - ref->width / 14) < 0.01f * ref->width / 14);
        test_assert(fabsf(b->height / sizes[i] - ref->height / 14) < 0.01f * ref->height / 14);
    }

    ecs_fini(world);
}

void TextLines_small_size_ink_in_bounds(void) {
    // ui_fixes/text_tests
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 96, .height = 32, .write_to_file = "small.png",
        .resolution_scale = 1});
    char *png = flecs_asprintf("%s/small.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    float sizes[3] = { 8, 10, 12 };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_set(world, e, Text, {.value = "Hello", .size = sizes[i],
            .font = ecs_lookup(world, "box.text.DefaultFont")});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);

        const TextBounds *b = ecs_get(world, e, TextBounds);
        int w = 0, h = 0, n = 0, x, y;
        unsigned char *px = stbi_load(png, &w, &h, &n, 4);
        test_assert(px != NULL);
        int32_t x0 = w, y0 = h, x1 = -1, y1 = -1, bright = 0;
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                unsigned char v = px[(y * w + x) * 4];
                bright += v > 128;
                if (v > 40) {
                    x0 = x < x0 ? x : x0;
                    y0 = y < y0 ? y : y0;
                    x1 = x > x1 ? x : x1;
                    y1 = y > y1 ? y : y1;
                }
            }
        }

        test_assert(bright >= 5);
        test_assert(x1 + 1 - x0 <= b->width + 2);
        test_assert(x1 + 1 - x0 >= b->width * 0.8f);
        test_assert(y1 + 1 - y0 <= b->height + 2);
        test_assert(fabsf((float)(x0 + x1 + 1) * 0.5f - (float)w * 0.5f) <= 2);
        test_assert((float)x0 >= ((float)w - b->width) * 0.5f - 2);
        test_assert((float)y0 >= ((float)h - b->height) * 0.5f - 2);
        stbi_image_free(px);
    }

    ecs_os_free(png);
    ecs_fini(world);
}

void TextLines_kerning_pairs(void) {
    // ui_fixes/text_tests
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    const char *pairs[][3] = {
        { "To", "T", "o" }, { "WA", "W", "A" }, { "LT", "L", "T" }, { "Ty", "T", "y" },
        { "nn", "n", "n" }, { "AV", "A", "V" }, { "HH", "H", "H" }
    };
    ecs_entity_t texts[7][3];
    int32_t i, j;
    for (i = 0; i < 7; i ++) {
        for (j = 0; j < 3; j ++) {
            texts[i][j] = ecs_new(world);
            ecs_set(world, texts[i][j], Text, {.value = ECS_CONST_CAST(char*, pairs[i][j]),
                .size = 100, .font = ecs_lookup(world, "box.text.DefaultFont")});
        }
    }

    ecs_progress(world, 0);

    for (i = 0; i < 7; i ++) {
        float pair = ecs_get(world, texts[i][0], TextBounds)->width;
        float apart = ecs_get(world, texts[i][1], TextBounds)->width +
            ecs_get(world, texts[i][2], TextBounds)->width;
        test_flt(pair, i < 5 ? apart - 5 : apart);
    }

    ecs_fini(world);
}

void TextLines_line_break_edges(void) {
    // ui_fixes/text_tests
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    const char *values[] = {
        "ab", "a\n\nb", "\nab", "a\nb", "a\r\nb", "a b", "a\tb", "   ", "abc"
    };
    ecs_entity_t texts[9];
    int32_t i;
    for (i = 0; i < 9; i ++) {
        texts[i] = ecs_new(world);
        ecs_set(world, texts[i], Text, {.value = ECS_CONST_CAST(char*, values[i]), .size = 20,
            .line_height = 25, .font = ecs_lookup(world, "box.text.DefaultFont")});
    }

    ecs_entity_t narrow = ecs_new(world);
    ecs_set(world, narrow, Text, {.value = "abc", .size = 20, .line_height = 25, .width = 1,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    const TextBounds *b[9];
    for (i = 0; i < 9; i ++) {
        b[i] = ecs_get(world, texts[i], TextBounds);
    }

    test_flt(b[0]->height, 25);
    test_flt(b[1]->height, 75);
    test_flt(b[2]->height, 50);
    test_flt(b[2]->width, b[0]->width);
    test_flt(b[3]->height, 50);
    test_flt(b[4]->height, b[3]->height);
    test_flt(b[4]->width, b[3]->width);
    test_flt(b[6]->width, b[5]->width);
    test_flt(b[7]->width, 0);
    test_flt(b[7]->height, 25);

    const TextBounds *nb = ecs_get(world, narrow, TextBounds);
    test_flt(nb->height, 75);
    test_assert(nb->width < b[8]->width * 0.6f);

    ecs_entity_t exact = ecs_new(world);
    ecs_set(world, exact, Text, {.value = "abc abc", .size = 20, .line_height = 25,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    float measured = ecs_get(world, exact, TextBounds)->width;
    ecs_set(world, exact, Text, {.value = "abc abc", .size = 20, .line_height = 25,
        .width = measured, .font = ecs_lookup(world, "box.text.DefaultFont")});
    test_flt(ecs_get(world, exact, TextBounds)->height, 25);
    test_flt(ecs_get(world, exact, TextBounds)->width, measured);

    ecs_entity_t spaced = ecs_new(world);
    ecs_set(world, spaced, Text, {.value = "abc        abc", .size = 20, .line_height = 25,
        .width = b[8]->width + 2, .font = ecs_lookup(world, "box.text.DefaultFont")});
    test_flt(ecs_get(world, spaced, TextBounds)->height, 50);
    test_flt(ecs_get(world, spaced, TextBounds)->width, b[8]->width);

    ecs_fini(world);
}

void TextLines_single_line_align_same(void) {
    // ui_fixes/text_tests
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 96, .height = 32, .write_to_file = "single.png",
        .resolution_scale = 1});
    char *png = flecs_asprintf("%s/single.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    Align aligns[3] = { AlignLeft, AlignCenter, AlignRight };
    unsigned char *images[3];
    int w = 0, h = 0, n = 0, i, p;
    for (i = 0; i < 3; i ++) {
        ecs_set(world, e, Text, {.value = "Align", .size = 16, .align = aligns[i],
            .font = ecs_lookup(world, "box.text.DefaultFont")});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);
        images[i] = stbi_load(png, &w, &h, &n, 4);
        test_assert(images[i] != NULL);
    }

    int32_t ink = 0;
    for (p = 0; p < w * h; p ++) {
        ink += images[0][p * 4] > 128;
        test_int(images[1][p * 4], images[0][p * 4]);
        test_int(images[2][p * 4], images[0][p * 4]);
    }

    test_assert(ink > 20);
    for (i = 0; i < 3; i ++) {
        stbi_image_free(images[i]);
    }

    ecs_os_free(png);
    ecs_fini(world);
}
