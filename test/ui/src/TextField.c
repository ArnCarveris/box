/**
 * @file TextField.c
 * @brief Text: glyph distance fields, their atlas uploads, overlapping contours and corners.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "render.h"
#include "preprocess.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Outline of B612 glyph 538 (U+E040, an hourglass): x, y in font units and 1 for on curve */
static const int32_t text_field_hourglass[][3] = {
    { 1000, 1400, 1 }, { 1000, 1306, 0 }, { 974, 1172, 0 }, { 932, 1075, 0 }, { 878, 1009, 0 },
    { 822, 958, 0 }, { 768, 914, 0 }, { 726, 863, 0 }, { 700, 798, 0 }, { 700, 750, 1 },
    { 700, 701, 0 }, { 744, 627, 0 }, { 811, 561, 0 }, { 889, 492, 0 }, { 956, 403, 0 },
    { 1000, 283, 0 }, { 1000, 200, 1 }, { 1000, 0, 1 }, { 100, 0, 1 }, { 100, 200, 1 },
    { 100, 284, 0 }, { 144, 402, 0 }, { 211, 491, 0 }, { 289, 560, 0 }, { 356, 626, 0 },
    { 400, 700, 0 }, { 400, 750, 1 }, { 400, 810, 0 }, { 356, 884, 0 }, { 289, 945, 0 },
    { 211, 1011, 0 }, { 144, 1115, 0 }, { 100, 1275, 0 }, { 100, 1400, 1 }, { 100, 1500, 1 },
    { 1000, 1500, 1 }, { 550, 600, 1 }, { 552, 599, 0 }, { 560, 561, 0 }, { 569, 503, 0 },
    { 581, 437, 0 }, { 590, 379, 0 }, { 598, 341, 0 }, { 600, 340, 1 }, { 652, 316, 0 },
    { 728, 293, 0 }, { 776, 270, 0 }, { 799, 236, 0 }, { 799, 200, 1 }, { 899, 200, 1 },
    { 899, 275, 0 }, { 855, 385, 0 }, { 788, 471, 0 }, { 711, 542, 0 }, { 644, 614, 0 },
    { 600, 696, 0 }, { 600, 750, 1 }, { 600, 812, 0 }, { 644, 900, 0 }, { 711, 972, 0 },
    { 788, 1038, 0 }, { 855, 1119, 0 }, { 899, 1225, 0 }, { 899, 1300, 1 }, { 200, 1300, 1 },
    { 200, 1225, 0 }, { 244, 1120, 0 }, { 311, 1039, 0 }, { 389, 972, 0 }, { 456, 900, 0 },
    { 500, 812, 0 }, { 500, 750, 1 }, { 500, 698, 0 }, { 456, 618, 0 }, { 389, 546, 0 },
    { 312, 473, 0 }, { 245, 386, 0 }, { 200, 275, 0 }, { 200, 200, 1 }, { 301, 200, 1 },
    { 301, 235, 0 }, { 324, 270, 0 }, { 372, 293, 0 }, { 446, 316, 0 }, { 498, 340, 1 },
    { 500, 341, 0 }, { 508, 379, 0 }, { 518, 436, 0 }, { 529, 503, 0 }, { 540, 560, 0 },
    { 548, 599, 0 }, { 550, 850, 1 }, { 550, 887, 0 }, { 516, 955, 0 }, { 465, 1016, 0 },
    { 406, 1072, 0 }, { 355, 1125, 0 }, { 321, 1175, 0 }, { 321, 1200, 1 }, { 778, 1200, 1 },
    { 778, 1175, 0 }, { 744, 1125, 0 }, { 694, 1072, 0 }, { 634, 1016, 0 }, { 584, 955, 0 },
    { 550, 887, 0 }
};

/** Last point of each contour of text_field_hourglass */
static const int32_t text_field_hourglass_ends[] = { 35, 90, 105 };

void TextField_overlapping_contours_no_seam(void) {
    // ui_fixes/msdf_correction
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
    uint32_t at = glyf + 2 * (uint32_t)((data[loca + 2 * 41] << 8) | data[loca + 2 * 41 + 1]);
    uint32_t end = glyf + 2 * (uint32_t)((data[loca + 2 * 42] << 8) | data[loca + 2 * 42 + 1]);
    test_assert(end - at >= 26);
    const uint8_t composite[26] = {
        0xff, 0xff, 0x00, 20, 0x00, 0x00, 0x02, 0x08, 0x05, 0xf0,
        0x00, 0x23, 0x00, 42, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x03, 0x00, 14, 0xff, 0x4c, 0x00, 0x00
    };
    ecs_os_memcpy(data + at, composite, 26);

    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *path = flecs_asprintf("%s/overlap.ttf", tmp);
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
    ecs_set(world, e, Text, {.value = "H", .size = 320, .font = font});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 400, .height = 400, .write_to_file = "overlap.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_get(world, font, FontStatus)->state, FontReady);

    char *png = flecs_asprintf("%s/overlap.png", tmp);
    int w = 0, h = 0, c = 0, x, y;
    unsigned char *px = stbi_load(png, &w, &h, &c, 4);
    test_assert(px != NULL);
    int32_t x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            if (px[(y * w + x) * 4] > 128) {
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
        }
    }

    static const double outline[] = {
        190, 0, 190, 1290, 160, 1500, 400, 1520, 400, 0,
        20, 600, 520, 600, 520, 445, 20, 445
    };
    const int32_t first[] = { 0, 5, 9 };
    test_assert(x1 - x0 > 60);
    double sx = (x1 + 1 - x0) / 500.0, sy = (y1 + 1 - y0) / 1520.0;
    int32_t inner = 0, holes = 0;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            int32_t sample, around = 0, k;
            for (sample = 0; sample < 49; sample ++) {
                double qx = 20 + (x + (sample % 7) * 0.5 - 1.0 - x0) / sx;
                double qy = 1520 - (y + (sample / 7) * 0.5 - 1.0 - y0) / sy;
                bool in = false;
                for (k = 0; k < 2 && !in; k ++) {
                    int32_t i, n = first[k + 1] - first[k];
                    const double *o = &outline[first[k] * 2];
                    for (i = 0; i < n; i ++) {
                        double ax = o[i * 2], ay = o[i * 2 + 1];
                        double bx = o[((i + 1) % n) * 2], by = o[((i + 1) % n) * 2 + 1];
                        if ((ay > qy) != (by > qy) && ax + (qy - ay) * (bx - ax) / (by - ay) > qx) {
                            in = !in;
                        }
                    }
                }

                around += in;
            }

            inner += around == 49;
            holes += around == 49 && px[(y * w + x) * 4] < 235;
        }
    }

    test_assert(inner > 1000);
    test_int(holes, 0);

    stbi_image_free(px);
    ecs_os_free(png);
    ecs_os_free(path);
    ecs_os_free(data);
    ecs_os_free(src);
    ecs_fini(world);
}

void TextField_sharp_corners_large(void) {
    // ui_fixes/msdf_correction
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
    ecs_set(world, s, Surface, {.width = 400, .height = 400, .write_to_file = "corners.png",
        .resolution_scale = 1});
    char *png = flecs_asprintf("%s/corners.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    static const double outline_t[] = {
        495, 0, 495, 1340, 80, 1340, 80, 1500, 1115, 1500, 1115, 1340, 705, 1340, 705, 0
    };
    static const double outline_k[] = {
        190, 0, 190, 1290, 160, 1500, 400, 1520, 400, 856, 941, 1500, 1163, 1500, 632, 865,
        463, 802, 637, 744, 1190, 0, 953, 0, 400, 746, 400, 0
    };
    const char *values[] = { "T", "K" };
    const double *outlines[] = { outline_t, outline_k };
    const int32_t points[] = { 8, 14 };
    int32_t v;
    for (v = 0; v < 2; v ++) {
        const double *o = outlines[v];
        int32_t n = points[v], i;
        ecs_set(world, e, Text, {.value = ECS_CONST_CAST(char*, values[v]), .size = 320,
            .font = ecs_lookup(world, "box.text.DefaultFont")});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, c = 0, x, y;
        unsigned char *px = stbi_load(png, &w, &h, &c, 4);
        test_assert(px != NULL);
        int32_t x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                if (px[(y * w + x) * 4] > 128) {
                    x0 = x < x0 ? x : x0;
                    y0 = y < y0 ? y : y0;
                    x1 = x > x1 ? x : x1;
                    y1 = y > y1 ? y : y1;
                }
            }
        }

        double fx0 = o[0], fx1 = o[0], fy0 = o[1], fy1 = o[1];
        for (i = 0; i < n; i ++) {
            fx0 = fmin(fx0, o[i * 2]);
            fx1 = fmax(fx1, o[i * 2]);
            fy0 = fmin(fy0, o[i * 2 + 1]);
            fy1 = fmax(fy1, o[i * 2 + 1]);
        }

        test_assert(x1 - x0 > 100);
        double sx = (x1 + 1 - x0) / (fx1 - fx0), sy = (y1 + 1 - y0) / (fy1 - fy0);
        int32_t wrong = 0, deep = 0;
        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                int32_t sample, covered = 0, around = 0;
                for (sample = 0; sample < 16 + 49; sample ++) {
                    int32_t g = sample - 16;
                    double ox = sample < 16 ? (sample % 4 + 0.5) / 4 : (g % 7) * 0.5 - 1.0;
                    double oy = sample < 16 ? (sample / 4 + 0.5) / 4 : (g / 7) * 0.5 - 1.0;
                    double qx = fx0 + (x + ox - x0) / sx, qy = fy1 - (y + oy - y0) / sy;
                    bool in = false;
                    for (i = 0; i < n; i ++) {
                        double ax = o[i * 2], ay = o[i * 2 + 1];
                        double bx = o[((i + 1) % n) * 2], by = o[((i + 1) % n) * 2 + 1];
                        if ((ay > qy) != (by > qy) && ax + (qy - ay) * (bx - ax) / (by - ay) > qx) {
                            in = !in;
                        }
                    }

                    covered += sample < 16 && in;
                    around += sample >= 16 && in;
                }

                int32_t value = px[(y * w + x) * 4];
                wrong += abs(value - covered * 255 / 16) > 160;
                deep += (around == 49 && value < 235) || (around == 0 && value > 20);
            }
        }

        test_int(wrong, 0);
        test_int(deep, 0);
        stbi_image_free(px);
    }

    ecs_os_free(png);
    ecs_fini(world);
}

void TextField_tiny_fades(void) {
    // ui_fixes/msdf_correction
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
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "tiny.png",
        .resolution_scale = 1});
    char *png = flecs_asprintf("%s/tiny.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    float sizes[3] = { 4, 20, 40 };
    double ink[3];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_set(world, e, Text, {.value = "M", .size = sizes[i],
            .font = ecs_lookup(world, "box.text.DefaultFont")});
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, n = 0, p;
        unsigned char *px = stbi_load(png, &w, &h, &n, 4);
        test_assert(px != NULL);
        ink[i] = 0;
        for (p = 0; p < w * h; p ++) {
            double c = px[p * 4] / 255.0;
            ink[i] += c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
        }

        ink[i] /= sizes[i] * sizes[i];
        stbi_image_free(px);
    }

    test_assert(fabs(ink[1] - ink[2]) < 0.05 * ink[2]);
    test_assert(ink[0] > 0.1 * ink[2]);
    test_assert(ink[0] < 0.75 * ink[2]);

    ecs_os_free(png);
    ecs_fini(world);
}

void TextField_glyphs_added_mid_run(void) {
    // ui_fixes/atlas_upload
    unsigned char *images[2] = { NULL, NULL };
    int w = 0, h = 0, n = 0, run;
    for (run = 0; run < 2; run ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_strbuf_t buf = ECS_STRBUF_INIT;
        int32_t c, count = 0;
        for (c = 0x21; c < 0x180 && count < 240; c ++) {
            if (c >= 0x7F && c < 0xA1) {
                continue;
            }

            if (c < 0x80) {
                ecs_strbuf_appendch(&buf, (char)c);
            } else {
                char utf8[2] = { (char)(0xC0 | (c >> 6)), (char)(0x80 | (c & 0x3F)) };
                ecs_strbuf_appendstrn(&buf, utf8, 2);
            }

            count ++;
            if (!(count % 30)) {
                ecs_strbuf_appendch(&buf, '\n');
            }
        }

        char *value = ecs_strbuf_get(&buf);

        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 0, 10});
        ecs_set(world, camera, Camera, {
            .ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {
            .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, 0});
        ecs_set(world, e, Rgb, {255, 255, 255});
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 640, .height = 256, .write_to_file = "typed.png",
            .resolution_scale = 1});
        int32_t len = (int32_t)strlen(value), at = run ? len : 1;
        while (true) {
            char *part = ecs_os_strdup(value);
            part[at] = 0;
            ecs_set(world, e, Text, {.value = part, .size = 18, .line_height = 20,
                .font = ecs_lookup(world, "box.text.DefaultFont")});
            ecs_os_free(part);
            ecs_progress(world, 0);
            if (at == len) {
                break;
            }

            at = at + 7 < len ? at + 7 : len;
            while (at < len && (value[at] & 0xC0) == 0x80) {
                at ++;
            }
        }

        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);
        char *png = flecs_asprintf("%s/typed.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        images[run] = stbi_load(png, &w, &h, &n, 4);
        test_assert(images[run] != NULL);
        ecs_os_free(png);
        ecs_os_free(value);
        ecs_fini(world);
    }

    int32_t i, ink = 0, off = 0;
    for (i = 0; i < w * h; i ++) {
        int32_t a = images[0][i * 4], b = images[1][i * 4];
        ink += b > 128;
        off += abs(a - b) > 16;
    }

    test_assert(ink > 2000);
    test_int(off, 0);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

void TextField_budget_more_glyphs_than_a_page(void) {
    // ui_fixes/atlas_upload
    ecs_os_init();
    char *src = box_shader_locate("etc/fonts/B612-Regular.ttf");
    test_assert(src != NULL);
    FILE *f = fopen(src, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    test_int((long)fread(data, 1, (size_t)size, f), size);
    fclose(f);
    int32_t t, tables = (data[4] << 8) | data[5];
    for (t = 0; t < tables; t ++) {
        const uint8_t *rec = data + 12 + t * 16;
        uint32_t off = ((uint32_t)rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
        if (!memcmp(rec, "head", 4)) {
            data[off + 18] = 250 >> 8;
            data[off + 19] = 250 & 0xff;
        }
    }

    char path[64];
    snprintf(path, sizeof(path), "/tmp/box_big_glyphs_%d.ttf", (int)getpid());
    f = fopen(path, "wb");
    test_assert(f != NULL);
    test_int((long)fwrite(data, 1, (size_t)size, f), size);
    fclose(f);
    free(data);
    ecs_os_free(src);
    unsigned char *images[2] = { NULL, NULL };
    int w = 0, h = 0, n = 0, run;
    for (run = 0; run < 2; run ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_strbuf_t buf = ECS_STRBUF_INIT;
        int32_t c, count = 0;
        for (c = 0x21; c < 0x180 && count < 60; c ++) {
            if (c >= 0x7F && c < 0xA1) {
                continue;
            }

            if (c < 0x80) {
                ecs_strbuf_appendch(&buf, (char)c);
            } else {
                char utf8[2] = { (char)(0xC0 | (c >> 6)), (char)(0x80 | (c & 0x3F)) };
                ecs_strbuf_appendstrn(&buf, utf8, 2);
            }

            count ++;
            if (!(count % 12)) {
                ecs_strbuf_appendch(&buf, '\n');
            }
        }

        char *value = ecs_strbuf_get(&buf);
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 0, 10});
        ecs_set(world, camera, Camera, {
            .ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {
            .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
        ecs_entity_t font = ecs_new(world);
        ecs_set(world, font, Font, {.path = path, .atlas_pages = run ? 16 : 1});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, 0});
        ecs_set(world, e, Rgb, {255, 255, 255});
        ecs_set(world, e, Text, {.value = value, .size = 3, .line_height = 40, .font = font});
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 640, .height = 400, .write_to_file = "budget.png",
            .resolution_scale = 1});
        int32_t i;
        for (i = 0; i < 4; i ++) {
            ecs_progress(world, 0);
        }

        box_render_sync(world);
        test_int(ecs_get(world, font, FontStatus)->state, FontReady);
        char *png = flecs_asprintf("%s/budget.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        images[run] = stbi_load(png, &w, &h, &n, 4);
        test_assert(images[run] != NULL);
        ecs_os_free(png);
        ecs_os_free(value);
        ecs_fini(world);
    }

    int32_t i, ink = 0, off = 0;
    for (i = 0; i < w * h; i ++) {
        int32_t a = images[0][i * 4], b = images[1][i * 4];
        ink += b > 128;
        off += abs(a - b) > 16;
    }

    test_assert(ink > 3000);
    test_int(off, 0);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
    remove(path);
    ecs_os_fini();
}

void TextField_small_budget_reuses_pages(void) {
    // ui_fixes/atlas_upload
    ecs_os_init();
    char *src = box_shader_locate("etc/fonts/B612-Regular.ttf");
    test_assert(src != NULL);
    FILE *f = fopen(src, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    test_int((long)fread(data, 1, (size_t)size, f), size);
    fclose(f);
    int32_t t, tables = (data[4] << 8) | data[5];
    for (t = 0; t < tables; t ++) {
        const uint8_t *rec = data + 12 + t * 16;
        uint32_t off = ((uint32_t)rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
        if (!memcmp(rec, "head", 4)) {
            data[off + 18] = 250 >> 8;
            data[off + 19] = 250 & 0xff;
        }
    }

    char path[64];
    snprintf(path, sizeof(path), "/tmp/box_big_glyphs_%d.ttf", (int)getpid());
    f = fopen(path, "wb");
    test_assert(f != NULL);
    test_int((long)fwrite(data, 1, (size_t)size, f), size);
    fclose(f);
    free(data);
    ecs_os_free(src);
    unsigned char *images[4] = { NULL, NULL, NULL, NULL };
    int w = 0, h = 0, n = 0, run, k;
    for (run = 0; run < 3; run ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        char *sets[3];
        for (k = 0; k < 3; k ++) {
            ecs_strbuf_t buf = ECS_STRBUF_INIT;
            int32_t c, count = 0;
            for (c = 0x21 + k * 30; count < 30; c ++) {
                if (c >= 0x7F && c < 0xA1) {
                    continue;
                }

                if (c < 0x80) {
                    ecs_strbuf_appendch(&buf, (char)c);
                } else {
                    char utf8[2] = { (char)(0xC0 | (c >> 6)), (char)(0x80 | (c & 0x3F)) };
                    ecs_strbuf_appendstrn(&buf, utf8, 2);
                }

                count ++;
                if (!(count % 10)) {
                    ecs_strbuf_appendch(&buf, '\n');
                }
            }

            sets[k] = ecs_strbuf_get(&buf);
        }

        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 0, 10});
        ecs_set(world, camera, Camera, {
            .ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {
            .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
        ecs_entity_t font = ecs_new(world);
        ecs_set(world, font, Font, {.path = path, .atlas_pages = run ? 16 : 1});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 0, 0});
        ecs_set(world, e, Rgb, {255, 255, 255});
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 640, .height = 256, .write_to_file = "reuse.png",
            .resolution_scale = 1});
        char *png = flecs_asprintf("%s/reuse.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        const char *steps[7] = { sets[0], "", sets[1], "", sets[2], "", sets[0] };
        int32_t first = run == 2 ? 6 : (run == 1 ? 4 : 0), last = run == 1 ? 4 : 6, i;
        for (k = first; k <= last; k ++) {
            ecs_set(world, e, Text, {.value = ECS_CONST_CAST(char*, steps[k]), .size = 3,
                .line_height = 40, .font = font});
            for (i = 0; i < 3; i ++) {
                ecs_progress(world, 0);
            }

            box_render_sync(world);
            if (k == 4 || k == 6) {
                int32_t slot = run ? run + 1 : k / 2 - 2;
                images[slot] = stbi_load(png, &w, &h, &n, 4);
                test_assert(images[slot] != NULL);
            }
        }

        ecs_os_free(png);
        ecs_os_free(sets[0]);
        ecs_os_free(sets[1]);
        ecs_os_free(sets[2]);
        ecs_fini(world);
    }

    for (k = 0; k < 2; k ++) {
        int32_t i, ink = 0, off = 0;
        for (i = 0; i < w * h; i ++) {
            int32_t a = images[k][i * 4], b = images[k + 2][i * 4];
            ink += b > 128;
            off += abs(a - b) > 16;
        }

        test_assert(ink > 1000);
        test_int(off, 0);
    }

    for (k = 0; k < 4; k ++) {
        stbi_image_free(images[k]);
    }

    remove(path);
    ecs_os_fini();
}

void TextField_cusp_glyph(void) {
    // ui_fixes/msdf_correction
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
    ecs_set(world, e, Text, {.value = "\xEE\x81\x80", .size = 320,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 400, .height = 400, .write_to_file = "cusp.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    static double line[2048][2];
    int32_t contour_start[4], k, i, j, sub, n = 0;
    for (k = 0; k < 3; k ++) {
        int32_t first = k ? text_field_hourglass_ends[k - 1] + 1 : 0;
        int32_t count = text_field_hourglass_ends[k] - first + 1;
        double on[2] = { text_field_hourglass[first][0], text_field_hourglass[first][1] };
        double ctrl[2] = { 0, 0 };
        bool pending = false;
        contour_start[k] = n;
        line[n][0] = on[0];
        line[n ++][1] = on[1];
        for (j = 1; j <= count; j ++) {
            const int32_t *q = text_field_hourglass[first + j % count];
            double end[2] = { q[0], q[1] };
            if (!q[2] && pending) {
                end[0] = (ctrl[0] + q[0]) * 0.5;
                end[1] = (ctrl[1] + q[1]) * 0.5;
            }

            if (!q[2] && !pending) {
                ctrl[0] = q[0];
                ctrl[1] = q[1];
                pending = true;
                continue;
            }

            for (sub = 1; sub <= 8; sub ++) {
                double t = sub / 8.0;
                double c0 = pending ? (1 - t) * (1 - t) : 1 - t, c2 = pending ? t * t : t;
                double c1 = pending ? 2 * t * (1 - t) : 0;
                line[n][0] = c0 * on[0] + c1 * ctrl[0] + c2 * end[0];
                line[n ++][1] = c0 * on[1] + c1 * ctrl[1] + c2 * end[1];
            }

            on[0] = end[0];
            on[1] = end[1];
            pending = !q[2];
            ctrl[0] = q[0];
            ctrl[1] = q[1];
        }

        n --;
    }

    contour_start[3] = n;
    char *png = flecs_asprintf("%s/cusp.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, c = 0, x, y;
    unsigned char *px = stbi_load(png, &w, &h, &c, 4);
    test_assert(px != NULL);
    int32_t x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            if (px[(y * w + x) * 4] > 128) {
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
        }
    }

    test_assert(x1 - x0 > 100);
    double sx = (x1 + 1 - x0) / 900.0, sy = (y1 + 1 - y0) / 1500.0;
    int32_t inner = 0, holes = 0, spill = 0;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            int32_t sample, around = 0;
            for (sample = 0; sample < 49; sample ++) {
                double qx = 100 + (x + (sample % 7) * 0.5 - 1.0 - x0) / sx;
                double qy = 1500 - (y + (sample / 7) * 0.5 - 1.0 - y0) / sy;
                bool in = false;
                for (k = 0; k < 3; k ++) {
                    int32_t a = contour_start[k], m = contour_start[k + 1] - a;
                    for (i = 0; i < m; i ++) {
                        const double *pa = line[a + i], *pb = line[a + (i + 1) % m];
                        if ((pa[1] > qy) != (pb[1] > qy) &&
                            pa[0] + (qy - pa[1]) * (pb[0] - pa[0]) / (pb[1] - pa[1]) > qx)
                        {
                            in = !in;
                        }
                    }
                }

                around += in;
            }

            int32_t value = px[(y * w + x) * 4];
            inner += around == 49;
            holes += around == 49 && value < 235;
            spill += around == 0 && value > 20;
        }
    }

    test_assert(inner > 10000);
    test_int(holes, 0);
    test_assert(spill <= 2);
    stbi_image_free(px);
    ecs_os_free(png);
    ecs_fini(world);
}
