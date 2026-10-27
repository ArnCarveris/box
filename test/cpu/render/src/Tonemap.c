/**
 * @file Tonemap.c
 * @brief Tests for the Tony McMapface LUT and the C twins of the tone map operators.
 *
 * The LUT is etc/luts/tony_mc_mapface.dds; flecs-engine packs the same texels as a uint32_t
 * RGB9E5 array of 110592 entries, row by row with x fastest, which the first texels match.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Load the LUT, failing the test when it does not load.
 *
 * @return The texels
 */
static uint32_t* tony_lut(void) {
    char *error = NULL;
    uint32_t *result = box_render_tony_load(BOX_RENDER_TONY_PATH, &error);
    if (!result) {
        printf("%s\n", error);
    }

    test_assert(result != NULL);
    test_assert(error == NULL);

    return result;
}

/** Get the saturation of a color: (max - min) / max.
 *
 * @param c The color
 * @return The saturation, 0 for black
 */
static float saturation(
    const float *c)
{
    float hi = glm_max(c[0], glm_max(c[1], c[2]));
    float lo = glm_min(c[0], glm_min(c[1], c[2]));

    return hi > 0 ? (hi - lo) / hi : 0.0f;
}

void Tonemap_lut_size(void) {
    // lighting/effects
    test_int(BOX_RENDER_TONY_SIZE, 48);
    test_int(BOX_RENDER_TONY_TEXELS, 110592);
    uint32_t *lut = tony_lut();
    float last[3];
    box_render_rgb9e5_decode(lut[BOX_RENDER_TONY_TEXELS - 1], last);
    test_assert(last[0] > 0.99f && last[0] <= 1.0f);
    test_assert(last[1] > 0.99f && last[1] <= 1.0f);
    test_assert(last[2] > 0.99f && last[2] <= 1.0f);
    ecs_os_free(lut);
}

void Tonemap_lut_first_texels(void) {
    // lighting/effects
    const uint32_t expect[8] = {
        0x1c7e3f1f, 0x5008078b, 0x5804058d, 0x6004032c,
        0x60040593, 0x600807fb, 0x68040533, 0x68080769
    };
    uint32_t *lut = tony_lut();
    int32_t i;
    for (i = 0; i < 8; i ++) {
        test_uint(lut[i], expect[i]);
    }

    ecs_os_free(lut);
}

void Tonemap_lut_missing_file_fails(void) {
    // lighting/effects
    char *error = NULL;
    uint32_t *lut = box_render_tony_load("etc/luts/missing.dds", &error);
    test_assert(lut == NULL);
    test_assert(error != NULL);
    ecs_os_free(error);

    error = NULL;
    lut = box_render_tony_load("etc/luts/README.md", &error);
    test_assert(lut == NULL);
    test_assert(error != NULL);
    ecs_os_free(error);
}

/* Exponent 15 is a scale of 2^-9, so a mantissa of 256 is 0.5; the first texel of the LUT has
 * mantissas 0x11f and exponent 3. */
void Tonemap_rgb9e5_decode(void) {
    // lighting/effects
    float c[3];
    box_render_rgb9e5_decode(0, c);
    test_flt(c[0], 0);
    test_flt(c[1], 0);
    test_flt(c[2], 0);

    box_render_rgb9e5_decode((15u << 27) | (128u << 18) | (256u << 9) | 511u, c);
    test_flt(c[0], 511.0f / 512.0f);
    test_flt(c[1], 0.5f);
    test_flt(c[2], 0.25f);

    box_render_rgb9e5_decode(0x1c7e3f1f, c);
    test_flt(c[0], 287.0f / 2097152.0f);
    test_flt(c[1], c[0]);
    test_flt(c[2], c[0]);
}

void Tonemap_black_stays_black(void) {
    // lighting/effects
    uint32_t *lut = tony_lut();
    float black[3] = {0, 0, 0}, out[3];
    box_render_tonemap_tony(lut, black, out);
    test_assert(out[0] >= 0 && out[0] < 0.5f / 255.0f);
    test_assert(out[1] >= 0 && out[1] < 0.5f / 255.0f);
    test_assert(out[2] >= 0 && out[2] < 0.5f / 255.0f);
    box_render_tonemap_pbr_neutral(black, out);
    test_flt(out[0], 0);
    test_flt(out[1], 0);
    test_flt(out[2], 0);
    ecs_os_free(lut);
}

/* PBR Neutral subtracts its toe offset of 0.04 from a gray this bright. */
void Tonemap_mid_gray(void) {
    // lighting/effects
    uint32_t *lut = tony_lut();
    float gray[3] = {0.18f, 0.18f, 0.18f}, out[3], pbr[3];
    box_render_tonemap_tony(lut, gray, out);
    test_assert(out[0] > 0.16f && out[0] < 0.18f);
    test_assert(fabsf(out[0] - out[1]) < 1e-3f);
    test_assert(fabsf(out[0] - out[2]) < 1e-3f);

    box_render_tonemap_pbr_neutral(gray, pbr);
    test_assert(fabsf(pbr[0] - 0.14f) < 1e-5f);
    test_flt(pbr[1], pbr[0]);
    ecs_os_free(lut);
}

void Tonemap_brightness_rolls_off_to_white(void) {
    // lighting/effects
    uint32_t *lut = tony_lut();
    float levels[5] = {0.05f, 0.5f, 1.0f, 4.0f, 100.0f};
    float last = 0, c[3], out[3];
    int32_t i;
    for (i = 0; i < 5; i ++) {
        c[0] = c[1] = c[2] = levels[i];
        box_render_tonemap_tony(lut, c, out);
        test_assert(out[1] > last);
        test_assert(out[1] < 1.0f);
        last = out[1];
    }

    test_assert(last > 0.99f);
    ecs_os_free(lut);
}

/* A bright orange and a bright grass green keep their hue, the order of their channels, and
 * keep less of their chroma than with PBR Neutral. */
void Tonemap_saturated_color_loses_chroma(void) {
    // lighting/effects
    uint32_t *lut = tony_lut();
    float colors[2][3] = {
        {1.0f, 0.2f, 0.05f},
        {0.6f, 0.9f, 0.1f}
    };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        float tony[3], pbr[3];
        box_render_tonemap_tony(lut, colors[i], tony);
        box_render_tonemap_pbr_neutral(colors[i], pbr);
        test_assert(saturation(tony) < saturation(pbr) - 0.04f);
        test_assert(saturation(tony) > 0.5f);
        test_assert((tony[0] > tony[1]) == (colors[i][0] > colors[i][1]));
        test_assert(tony[1] > tony[2]);
    }

    ecs_os_free(lut);
}
