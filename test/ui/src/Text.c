/**
 * @file Text.c
 * @brief Text: measurement, line breaks, kerning, font changes and crisp rendered glyphs.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "preprocess.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Text_measure_line(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t small = ecs_new(world);
    ecs_set(world, small, Text, {.value = "Hello world", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t large = ecs_new(world);
    ecs_set(world, large, Text, {.value = "Hello world", .size = 32,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    const TextBounds *a = ecs_get(world, small, TextBounds);
    const TextBounds *b = ecs_get(world, large, TextBounds);
    test_assert(a != NULL);
    test_assert(b != NULL);
    test_assert(a->width > 16 * 3);
    test_assert(a->width < 16 * 11);
    test_assert(a->height >= 16);
    test_assert(a->height < 16 * 2);
    test_assert(fabsf(b->width - 2 * a->width) < 0.01f * b->width);
    test_assert(fabsf(b->height - 2 * a->height) < 0.01f * b->height);

    ecs_fini(world);
}

void Text_measure_empty(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t empty = ecs_new(world);
    ecs_set(world, empty, Text, {.value = "", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t none = ecs_new(world);
    ecs_set(world, none, Text, {.size = 16, .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, empty, TextBounds)->width, 0);
    test_flt(ecs_get(world, empty, TextBounds)->height, 0);
    test_flt(ecs_get(world, none, TextBounds)->width, 0);
    test_flt(ecs_get(world, none, TextBounds)->height, 0);

    ecs_set(world, empty, Text, {.value = "x", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    test_assert(ecs_get(world, empty, TextBounds)->width > 0);
    ecs_set(world, empty, Text, {.value = "", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    test_flt(ecs_get(world, empty, TextBounds)->width, 0);

    ecs_fini(world);
}

void Text_zero_literal_draws_nothing(void) {
    // engine_core/zero_values
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, Text, {.value = "Hello"});
    ecs_entity_t constructed = ecs_new(world);
    ecs_add(world, constructed, Text);
    const Text *t = ecs_get(world, constructed, Text);
    test_flt(t->size, 16);
    test_uint(t->font, ecs_lookup(world, "box.text.DefaultFont"));
    ecs_get_mut(world, constructed, Text)->value = ecs_os_strdup("Hello");
    ecs_modified(world, constructed, Text);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, literal, TextBounds)->width, 0);
    test_flt(ecs_get(world, literal, TextBounds)->height, 0);
    test_assert(ecs_get(world, constructed, TextBounds)->width > 0);

    ecs_fini(world);
}

void Text_measure_newlines(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t one = ecs_new(world);
    ecs_set(world, one, Text, {.value = "short", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t wide = ecs_new(world);
    ecs_set(world, wide, Text, {.value = "a much longer line", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t two = ecs_new(world);
    ecs_set(world, two, Text, {.value = "short\na much longer line", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t three = ecs_new(world);
    ecs_set(world, three, Text, {.value = "a\nb\nc", .size = 20, .line_height = 30,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    const TextBounds *b1 = ecs_get(world, one, TextBounds);
    const TextBounds *bw = ecs_get(world, wide, TextBounds);
    const TextBounds *b2 = ecs_get(world, two, TextBounds);
    test_flt(b2->height, 2 * b1->height);
    test_flt(b2->width, bw->width);
    test_flt(ecs_get(world, three, TextBounds)->height, 90);

    ecs_fini(world);
}

void Text_trailing_spaces_not_measured(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Text, {.value = "word", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t trailing = ecs_new(world);
    ecs_set(world, trailing, Text, {.value = "word   ", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t leading = ecs_new(world);
    ecs_set(world, leading, Text, {.value = "   word", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    float w = ecs_get(world, plain, TextBounds)->width;
    test_flt(ecs_get(world, trailing, TextBounds)->width, w);
    test_assert(ecs_get(world, leading, TextBounds)->width > w + 10);

    ecs_fini(world);
}

void Text_wrap_between_words(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t pair = ecs_new(world);
    ecs_set(world, pair, Text, {.value = "abc abc", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t line = ecs_new(world);
    ecs_set(world, line, Text, {.value = "abc abc abc abc", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    const TextBounds *bp = ecs_get(world, pair, TextBounds);
    float width = bp->width + 2;
    float height = bp->height;
    test_assert(ecs_get(world, line, TextBounds)->height == height);

    ecs_entity_t wrapped = ecs_new(world);
    ecs_set(world, wrapped, Text, {
        .value = "abc abc abc abc", .size = 20, .width = width,
            .font = ecs_lookup(world, "box.text.DefaultFont")});
    const TextBounds *bw = ecs_get(world, wrapped, TextBounds);
    test_flt(bw->height, 2 * height);
    test_assert(bw->width <= width);
    test_assert(bw->width > width * 0.5f);

    ecs_set(world, wrapped, Text, {
        .value = "abc abc abc abc", .size = 20, .width = width * 0.6f,
            .font = ecs_lookup(world, "box.text.DefaultFont")});
    bw = ecs_get(world, wrapped, TextBounds);
    test_flt(bw->height, 4 * height);
    test_assert(bw->width <= width * 0.6f);

    ecs_fini(world);
}

void Text_wrap_long_word(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t whole = ecs_new(world);
    ecs_set(world, whole, Text, {.value = "abcdefghijklmnop", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);
    const TextBounds *b = ecs_get(world, whole, TextBounds);
    float width = b->width, height = b->height;

    ecs_entity_t broken = ecs_new(world);
    ecs_set(world, broken, Text, {
        .value = "abcdefghijklmnop", .size = 20, .width = width / 3,
            .font = ecs_lookup(world, "box.text.DefaultFont")});
    b = ecs_get(world, broken, TextBounds);
    test_assert(b->width <= width / 3);
    test_assert(b->height >= 3 * height);
    test_assert(b->height <= 4 * height);

    ecs_fini(world);
}

void Text_kerning(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t t = ecs_new(world);
    ecs_set(world, t, Text, {.value = "T T", .size = 100,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t o = ecs_new(world);
    ecs_set(world, o, Text, {.value = "o o", .size = 100,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t to = ecs_new(world);
    ecs_set(world, to, Text, {.value = "To To", .size = 100,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t space = ecs_new(world);
    ecs_set(world, space, Text, {.value = "o  o", .size = 100,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    float wt = ecs_get(world, t, TextBounds)->width;
    float wo = ecs_get(world, o, TextBounds)->width;
    float wto = ecs_get(world, to, TextBounds)->width;
    float ws = ecs_get(world, space, TextBounds)->width;
    float sum = wt + wo - (ws - wo);
    test_assert(wto < sum - 6);
    test_assert(wto > sum - 14);

    ecs_fini(world);
}

void Text_size_change_relayouts(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {.value = "Resize me", .size = 10,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);
    float w = ecs_get(world, e, TextBounds)->width;
    test_assert(w > 0);

    Text *text = ecs_get_mut(world, e, Text);
    text->size = 30;
    ecs_modified(world, e, Text);
    test_assert(fabsf(ecs_get(world, e, TextBounds)->width - 3 * w) < 0.01f * w);

    ecs_fini(world);
}

void Text_font_change_reloads(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_new(world);
    ecs_set(world, font, Font, {.path = "etc/fonts/missing.ttf"});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {.value = "Fonts", .size = 20, .font = font});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const FontStatus *status = ecs_get(world, font, FontStatus);
    test_assert(status != NULL);
    test_int(status->state, FontFailed);
    test_assert(status->error != NULL);
    test_flt(ecs_get(world, e, TextBounds)->width, 0);

    ecs_set(world, font, Font, {.path = "etc/fonts/B612-Regular.ttf"});
    ecs_progress(world, 0);
    status = ecs_get(world, font, FontStatus);
    test_int(status->state, FontReady);
    test_int(status->revision, 1);
    float w = ecs_get(world, e, TextBounds)->width;
    test_assert(w > 0);

    ecs_set(world, font, Font, {.path = "etc/fonts/missing.ttf"});
    ecs_progress(world, 0);
    status = ecs_get(world, font, FontStatus);
    test_int(status->state, FontFailed);
    test_int(status->revision, 1);
    test_flt(ecs_get(world, e, TextBounds)->width, w);

    ecs_set(world, e, Text, {.value = "Fonts", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);
    test_flt(ecs_get(world, e, TextBounds)->width, w);

    ecs_fini(world);
}

void Text_default_font_removed(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t font = ecs_new(world);
    ecs_set(world, font, Font, {.path = "etc/fonts/B612-Regular.ttf"});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {.value = "Fallback", .size = 20, .font = font});
    ecs_progress(world, 0);
    float w = ecs_get(world, e, TextBounds)->width;
    test_assert(w > 0);

    ecs_delete(world, font);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_flt(ecs_get(world, e, TextBounds)->width, w);

    ecs_fini(world);
}

void Text_script(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    const char *code =
        "using box.text\n"
        "using box.layout\n"
        "label {\n"
        "  Text: {value: \"Scripted\", size: 24, align: Right}\n"
        "}\n";
    test_assert(ecs_script_run(world, "text", code, NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t label = ecs_lookup(world, "label");
    test_assert(label != 0);
    const Text *text = ecs_get(world, label, Text);
    test_str(text->value, "Scripted");
    test_int(text->align, AlignRight);
    test_assert(ecs_get(world, label, TextBounds)->width > 24);

    ecs_fini(world);
}

void Text_measured_by_layout(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t row = ecs_new(world);
    ecs_set(world, row, Layout, {.direction = DirectionRow, .spacing = 10});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, row);
    ecs_set(world, a, Text, {.value = "first", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, row);
    ecs_set(world, b, Text, {.value = "second\nline", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    const TextBounds *ta = ecs_get(world, a, TextBounds);
    const TextBounds *tb = ecs_get(world, b, TextBounds);
    const LayoutBounds *l = ecs_get(world, row, LayoutBounds);
    test_flt(l->width, ta->width + tb->width + 10);
    test_flt(l->height, tb->height);
    test_flt(ecs_get(world, a, LayoutBounds)->width, ta->width);

    ecs_set(world, a, Text, {.value = "first and longer", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);
    ta = ecs_get(world, a, TextBounds);
    test_flt(ecs_get(world, row, LayoutBounds)->width, ta->width + tb->width + 10);

    ecs_fini(world);
}

void Text_renders_color(void) {
    // ui/text
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
    ecs_set(world, e, Text, {.value = "I", .size = 60,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, e, Rgb, {255, 128, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "color.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/color.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t x, ink = 0;
    for (x = 0; x < 64; x ++) {
        unsigned char *c = &px[(32 * 64 + x) * 4];
        if (c[0] > 250 && c[1] > 120 && c[1] < 136 && c[2] < 5) {
            ink ++;
        }
    }

    test_assert(ink >= 3);
    test_assert(px[(4 * 64 + 4) * 4] < 5);
    stbi_image_free(px);

    ecs_remove(world, e, Text);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (x = 0; x < 64 * 64; x ++) {
        test_assert(px[x * 4] < 5);
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Text_crisp_at_any_scale(void) {
    // ui/text
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
    ecs_set(world, e, Position, {0.3f, 0, 0});
    ecs_set(world, e, Text, {.value = "I", .size = 1,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 200, .height = 200, .write_to_file = "scale.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/scale.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    float scales[5] = { 14, 30, 80, 250, 900 };
    int32_t i, prev = 0;
    for (i = 0; i < 5; i ++) {
        ecs_set(world, e, Scale, {scales[i], scales[i], 1});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        int32_t x, partial = 0, full = 0;
        for (x = 0; x < 200; x ++) {
            unsigned char v = px[(100 * 200 + x) * 4];
            if (v >= 235) {
                full ++;
            } else if (v > 20) {
                partial ++;
            }
        }

        test_assert(partial >= 1);
        test_assert(partial <= 4);
        test_assert(full >= prev);
        prev = full;
        stbi_image_free(px);
    }

    test_assert(prev > 60);
    ecs_os_free(path);
    ecs_fini(world);
}

void Text_crisp_in_perspective(void) {
    // ui/text
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rotation, {0, 0.6f, 0});
    ecs_set(world, e, Text, {.value = "I", .size = 1,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 128, .height = 128, .write_to_file = "far.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/far.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    float distances[3] = { 2, 20, 200 };
    int32_t d;
    for (d = 0; d < 3; d ++) {
        float dist = distances[d];
        ecs_set(world, e, Position, {0.013f * dist, 0, -dist});
        ecs_set(world, e, Scale, {0.9f * dist, 0.9f * dist, 1});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        int32_t x, partial = 0, full = 0, runs = 0;
        bool inside = false;
        for (x = 0; x < 128; x ++) {
            unsigned char v = px[(64 * 128 + x) * 4];
            if (v >= 235) {
                full ++;
            } else if (v > 20) {
                partial ++;
            }

            if (!inside && v >= 128) {
                runs ++;
            }

            inside = v >= 128;
        }

        test_int(runs, 1);
        test_assert(full >= 4);
        test_assert(partial <= 4);
        stbi_image_free(px);
    }

    ecs_os_free(path);
    ecs_fini(world);
}

void Text_align_lines(void) {
    // ui/text
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
    ecs_set(world, e, Text, {.value = "I\nIIIIIIIIII", .size = 40, .line_height = 50,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 256, .height = 128, .write_to_file = "align.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/align.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    Align aligns[3] = { AlignLeft, AlignCenter, AlignRight };
    int32_t i, firsts[3], lasts[3], wide_first = 0, wide_last = 0;
    for (i = 0; i < 3; i ++) {
        ecs_set(world, e, Text, {
            .value = "I\nIIIIIIIIII", .size = 40, .line_height = 50, .align = aligns[i],
                .font = ecs_lookup(world, "box.text.DefaultFont")});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        int32_t x;
        firsts[i] = -1;
        lasts[i] = -1;
        wide_first = -1;
        for (x = 0; x < 256; x ++) {
            if (px[(39 * 256 + x) * 4] > 128) {
                firsts[i] = firsts[i] < 0 ? x : firsts[i];
                lasts[i] = x;
            }

            if (px[(89 * 256 + x) * 4] > 128) {
                wide_first = wide_first < 0 ? x : wide_first;
                wide_last = x;
            }
        }

        test_assert(firsts[i] >= 0);
        test_assert(wide_first >= 0);
        stbi_image_free(px);
    }

    test_assert(firsts[0] - wide_first <= 6);
    test_assert(wide_last - lasts[2] <= 6);
    test_assert(abs((firsts[1] + lasts[1]) - (wide_first + wide_last)) <= 2);
    test_assert(firsts[0] < firsts[1] - 20);
    test_assert(firsts[1] < firsts[2] - 20);
    ecs_os_free(path);
    ecs_fini(world);
}

void Text_many_glyphs(void) {
    // ui/text
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
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t c, count = 0;
    for (c = 0x21; c < 0x250; c ++) {
        if (c >= 0x7F && c < 0xA1) {
            continue;
        }

        char utf8[4];
        if (c < 0x80) {
            ecs_strbuf_appendch(&buf, (char)c);
        } else {
            utf8[0] = (char)(0xC0 | (c >> 6));
            utf8[1] = (char)(0x80 | (c & 0x3F));
            ecs_strbuf_appendstrn(&buf, utf8, 2);
        }

        count ++;
        if (!(count % 40)) {
            ecs_strbuf_appendch(&buf, '\n');
        }
    }

    ecs_strbuf_appendstr(&buf, "\nIIIIIIII");
    char *value = ecs_strbuf_get(&buf);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Text, {.value = value, .size = 20, .line_height = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 1024, .height = 512, .write_to_file = "many.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    const TextBounds *b = ecs_get(world, e, TextBounds);
    int32_t lines = count / 40 + (count % 40 ? 1 : 0) + 1;
    test_flt(b->height, 20.0f * (float)lines);

    char *path = flecs_asprintf("%s/many.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t y = 256 + (int32_t)(b->height * 0.5f) - 10, x, ink = 0;
    for (x = 0; x < 1024; x ++) {
        if (px[(y * 1024 + x) * 4] > 200) {
            ink ++;
        }
    }

    test_assert(ink >= 8);
    test_assert(ink <= 40);
    int32_t first_line = 0;
    y = 256 - (int32_t)(b->height * 0.5f) + 10;
    for (x = 0; x < 1024; x ++) {
        if (px[(y * 1024 + x) * 4] > 200) {
            first_line ++;
        }
    }

    test_assert(first_line > 40);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_os_free(value);
    ecs_fini(world);
}

void Text_dynamic_transform(void) {
    // ui/text
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
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {-20, 0, 0});
    ecs_set(world, e, Text, {.value = "I", .size = 40,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "move.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/move.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    float xs[2] = { -20, 20 };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        Position *p = ecs_get_mut(world, e, Position);
        p->x = xs[i];
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        int32_t left = px[(32 * 64 + 12) * 4], right = px[(32 * 64 + 52) * 4];
        test_assert(i == 0 ? left > 200 : left < 20);
        test_assert(i == 0 ? right < 20 : right > 200);
        stbi_image_free(px);
    }

    ecs_os_free(path);
    ecs_fini(world);
}

void Text_module_child_fini(void) {
    // ui/fini_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t m = ecs_entity(world, { .name = "game" });
    ecs_add_id(world, m, EcsModule);
    ecs_entity_t t = ecs_entity(world, { .name = "label", .parent = m });
    ecs_set(world, t, Text, {.value = "Hello", .size = 16,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_progress(world, 0);

    const TextBounds *b = ecs_get(world, t, TextBounds);
    test_assert(b != NULL);
    test_assert(b->width > 0);

    test_int(ecs_fini(world), 0);
}

void Text_oversized_glyph_missing(void) {
    // hardening/font_glyph_limit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *src = box_shader_locate("etc/fonts/B612-Regular.ttf");
    test_assert(src != NULL);
    FILE *f = fopen(src, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = ecs_os_malloc((ecs_size_t)size);
    test_int((long)fread(data, 1, (size_t)size, f), size);
    fclose(f);

    uint32_t head = 0, loca = 0, glyf = 0;
    int32_t i, tables = (data[4] << 8) | data[5];
    for (i = 0; i < tables; i ++) {
        const uint8_t *rec = data + 12 + i * 16;
        uint32_t off = ((uint32_t)rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
        if (!memcmp(rec, "head", 4)) {
            head = off;
        } else if (!memcmp(rec, "loca", 4)) {
            loca = off;
        } else if (!memcmp(rec, "glyf", 4)) {
            glyf = off;
        }
    }

    test_assert(head && loca && glyf);
    test_int((data[head + 50] << 8) | data[head + 51], 0);
    int32_t glyph_w = 56;
    uint32_t at = glyf + 2 * (uint32_t)((data[loca + 2 * glyph_w] << 8) |
        data[loca + 2 * glyph_w + 1]);
    data[at + 4] = 0x80;
    data[at + 5] = 0x00;
    data[at + 8] = 0x7f;
    data[at + 9] = 0xff;

    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *path = flecs_asprintf("%s/oversized.ttf", tmp);
    f = fopen(path, "wb");
    test_assert(f != NULL);
    test_int((long)fwrite(data, 1, (size_t)size, f), size);
    fclose(f);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t font = ecs_new(world);
    ecs_set(world, font, Font, {.path = path});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 96, .height = 96, .write_to_file = "glyph.png",
        .resolution_scale = 1});
    ecs_log_set_level(-4);

    const char *values[] = { "W", "\xe4\xb8\x80", "A", "W" };
    ecs_entity_t fonts[] = { font, font, font, ecs_lookup(world, "box.text.DefaultFont") };
    int32_t ink[4] = {0}, w[4] = {0}, h[4] = {0};
    char *png = flecs_asprintf("%s/glyph.png", tmp);
    for (i = 0; i < 4; i ++) {
        ecs_set(world, e, Text, {.value = ECS_CONST_CAST(char*, values[i]), .size = 48,
            .font = fonts[i]});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);
        test_int(ecs_get(world, font, FontStatus)->state, FontReady);
        test_assert(ecs_get(world, e, TextBounds)->width > 0);

        int pw = 0, ph = 0, n = 0, x, y;
        int32_t x0 = 96, y0 = 96, x1 = -1, y1 = -1;
        unsigned char *px = stbi_load(png, &pw, &ph, &n, 4);
        test_assert(px != NULL);
        for (y = 0; y < ph; y ++) {
            for (x = 0; x < pw; x ++) {
                if (px[(y * pw + x) * 4] > 128) {
                    ink[i] ++;
                    x0 = x < x0 ? x : x0;
                    y0 = y < y0 ? y : y0;
                    x1 = x > x1 ? x : x1;
                    y1 = y > y1 ? y : y1;
                }
            }
        }

        w[i] = x1 - x0;
        h[i] = y1 - y0;
        stbi_image_free(px);
    }

    test_assert(ink[0] > 0);
    test_assert(ink[1] > 0);
    test_assert(abs(w[0] - w[1]) <= 1);
    test_assert(abs(h[0] - h[1]) <= 1);
    test_assert(ink[2] > 0);
    test_assert(ink[3] > 0);
    test_assert(abs(w[3] - w[1]) > 4);

    ecs_os_free(png);
    ecs_os_free(path);
    ecs_os_free(data);
    ecs_os_free(src);
    ecs_fini(world);
}

void Text_oversized_warns_once_per_font(void) {
    // hardening/font_glyph_limit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *src = box_shader_locate("etc/fonts/B612-Regular.ttf");
    test_assert(src != NULL);
    FILE *f = fopen(src, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = ecs_os_malloc((ecs_size_t)size);
    test_int((long)fread(data, 1, (size_t)size, f), size);
    fclose(f);

    uint32_t head = 0, loca = 0, glyf = 0;
    int32_t i, tables = (data[4] << 8) | data[5];
    for (i = 0; i < tables; i ++) {
        const uint8_t *rec = data + 12 + i * 16;
        uint32_t off = ((uint32_t)rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
        if (!memcmp(rec, "head", 4)) {
            head = off;
        } else if (!memcmp(rec, "loca", 4)) {
            loca = off;
        } else if (!memcmp(rec, "glyf", 4)) {
            glyf = off;
        }
    }

    test_assert(head && loca && glyf);
    test_int((data[head + 50] << 8) | data[head + 51], 0);
    int32_t glyphs[] = { 56, 34 };
    for (i = 0; i < 2; i ++) {
        uint32_t at = glyf + 2 * (uint32_t)((data[loca + 2 * glyphs[i]] << 8) |
            data[loca + 2 * glyphs[i] + 1]);
        data[at + 4] = 0x80;
        data[at + 5] = 0x00;
        data[at + 8] = 0x7f;
        data[at + 9] = 0xff;
    }

    char *path = flecs_asprintf("%s/oversized.ttf", ecs_singleton_get(world, AppStatus)->tmp_dir);
    f = fopen(path, "wb");
    test_assert(f != NULL);
    test_int((long)fwrite(data, 1, (size_t)size, f), size);
    fclose(f);

    ecs_entity_t t = box_terminal_new(world);
    ecs_entity_t font = ecs_new(world);
    ecs_set(world, font, Font, {.path = path});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {.value = "W", .size = 20, .font = font});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_set(world, e, Text, {.value = "WAW A", .size = 30, .font = font});
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    int32_t warnings = 0;
    char *out = box_terminal_run(world, t, "logs warning 100", NULL);
    const char *p = out;
    while ((p = strstr(p, "larger than an atlas page"))) {
        warnings ++;
        p ++;
    }

    test_int(warnings, 1);
    test_assert(strstr(out, path) != NULL);
    ecs_os_free(out);

    ecs_entity_t font2 = ecs_new(world);
    ecs_set(world, font2, Font, {.path = path});
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, Text, {.value = "AW", .size = 20, .font = font2});
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    warnings = 0;
    out = box_terminal_run(world, t, "logs warning 100", NULL);
    p = out;
    while ((p = strstr(p, "larger than an atlas page"))) {
        warnings ++;
        p ++;
    }

    test_int(warnings, 2);
    ecs_os_free(out);

    ecs_os_free(path);
    ecs_os_free(data);
    ecs_os_free(src);
    ecs_fini(world);
}

void Text_font_without_units_fails(void) {
    // hardening/font_glyph_limit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *src = box_shader_locate("etc/fonts/B612-Regular.ttf");
    test_assert(src != NULL);
    FILE *f = fopen(src, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = ecs_os_malloc((ecs_size_t)size);
    test_int((long)fread(data, 1, (size_t)size, f), size);
    fclose(f);

    uint32_t head = 0;
    int32_t i, tables = (data[4] << 8) | data[5];
    for (i = 0; i < tables; i ++) {
        const uint8_t *rec = data + 12 + i * 16;
        if (!memcmp(rec, "head", 4)) {
            head = ((uint32_t)rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
        }
    }

    test_assert(head != 0);
    data[head + 18] = 0;
    data[head + 19] = 0;

    char *path = flecs_asprintf("%s/no_units.ttf", ecs_singleton_get(world, AppStatus)->tmp_dir);
    f = fopen(path, "wb");
    test_assert(f != NULL);
    test_int((long)fwrite(data, 1, (size_t)size, f), size);
    fclose(f);

    ecs_entity_t font = ecs_new(world);
    ecs_set(world, font, Font, {.path = path});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Text, {.value = "WA", .size = 20, .font = font});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Text, {.value = "WA", .size = 20,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const FontStatus *status = ecs_get(world, font, FontStatus);
    test_int(status->state, FontFailed);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "units per em") != NULL);
    test_flt(ecs_get(world, e, TextBounds)->width, 0);
    test_assert(ecs_get(world, other, TextBounds)->width > 0);

    ecs_os_free(path);
    ecs_os_free(data);
    ecs_os_free(src);
    ecs_fini(world);
}

void Text_recursive_composite_missing(void) {
    // hardening/glyph_recursion
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *src = box_shader_locate("etc/fonts/B612-Regular.ttf");
    test_assert(src != NULL);
    FILE *f = fopen(src, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = ecs_os_malloc((ecs_size_t)size);
    test_int((long)fread(data, 1, (size_t)size, f), size);
    fclose(f);

    uint32_t head = 0, loca = 0, glyf = 0;
    int32_t i, tables = (data[4] << 8) | data[5];
    for (i = 0; i < tables; i ++) {
        const uint8_t *rec = data + 12 + i * 16;
        uint32_t off = ((uint32_t)rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
        if (!memcmp(rec, "head", 4)) {
            head = off;
        } else if (!memcmp(rec, "loca", 4)) {
            loca = off;
        } else if (!memcmp(rec, "glyf", 4)) {
            glyf = off;
        }
    }

    test_assert(head && loca && glyf);
    test_int((data[head + 50] << 8) | data[head + 51], 0);
    int32_t from[] = { 56, 34, 55, 57 };
    int32_t to[] = { 56, 55, 34, 42 };
    for (i = 0; i < 4; i ++) {
        uint32_t at = glyf + 2 * (uint32_t)((data[loca + 2 * from[i]] << 8) |
            data[loca + 2 * from[i] + 1]);
        uint32_t end = glyf + 2 * (uint32_t)((data[loca + 2 * from[i] + 2] << 8) |
            data[loca + 2 * from[i] + 3]);
        test_assert(end - at >= 16);
        data[at] = 0xff;
        data[at + 1] = 0xff;
        data[at + 10] = 0x00;
        data[at + 11] = 0x02;
        data[at + 12] = (uint8_t)(to[i] >> 8);
        data[at + 13] = (uint8_t)to[i];
        data[at + 14] = 0;
        data[at + 15] = 0;
    }

    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *path = flecs_asprintf("%s/recursive.ttf", tmp);
    f = fopen(path, "wb");
    test_assert(f != NULL);
    test_int((long)fwrite(data, 1, (size_t)size, f), size);
    fclose(f);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t font = ecs_new(world);
    ecs_set(world, font, Font, {.path = path});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 96, .height = 96, .write_to_file = "glyph.png",
        .resolution_scale = 1});
    ecs_log_set_level(-4);

    const char *values[] = { "\xe4\xb8\x80", "W", "A", "B", "W", "X", "I" };
    ecs_entity_t fonts[] = {
        font, font, font, font, ecs_lookup(world, "box.text.DefaultFont"), font, font
    };
    int32_t ink[7] = {0}, w[7] = {0}, h[7] = {0};
    char *png = flecs_asprintf("%s/glyph.png", tmp);
    for (i = 0; i < 7; i ++) {
        ecs_set(world, e, Text, {.value = ECS_CONST_CAST(char*, values[i]), .size = 48,
            .font = fonts[i]});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);
        test_int(ecs_get(world, font, FontStatus)->state, FontReady);
        test_assert(ecs_get(world, e, TextBounds)->width > 0);

        int pw = 0, ph = 0, n = 0, x, y;
        int32_t x0 = 96, y0 = 96, x1 = -1, y1 = -1;
        unsigned char *px = stbi_load(png, &pw, &ph, &n, 4);
        test_assert(px != NULL);
        for (y = 0; y < ph; y ++) {
            for (x = 0; x < pw; x ++) {
                if (px[(y * pw + x) * 4] > 128) {
                    ink[i] ++;
                    x0 = x < x0 ? x : x0;
                    y0 = y < y0 ? y : y0;
                    x1 = x > x1 ? x : x1;
                    y1 = y > y1 ? y : y1;
                }
            }
        }

        w[i] = x1 - x0;
        h[i] = y1 - y0;
        stbi_image_free(px);
    }

    test_assert(ink[0] > 0);
    for (i = 1; i < 3; i ++) {
        test_assert(ink[i] > 0);
        test_assert(abs(w[i] - w[0]) <= 1);
        test_assert(abs(h[i] - h[0]) <= 1);
    }

    test_assert(ink[3] > 0);
    test_assert(ink[4] > 0);
    test_assert(abs(w[4] - w[0]) > 4);
    test_assert(ink[5] > 0);
    test_assert(ink[6] > 0);
    test_assert(abs(w[5] - w[6]) <= 1);
    test_assert(abs(h[5] - h[6]) <= 1);
    test_assert(abs(w[6] - w[0]) > 4);

    ecs_os_free(png);
    ecs_os_free(path);
    ecs_os_free(data);
    ecs_os_free(src);
    ecs_fini(world);
}
