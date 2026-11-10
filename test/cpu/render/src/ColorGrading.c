/**
 * @file ColorGrading.c
 * @brief Tests for the C twin of the color grading pass: white balance, the parametric steps and
 * the strip LUT.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Get the default color grading settings.
 *
 * @return The settings a constructed ColorGrading has
 */
static ColorGrading grading_defaults(void) {
    return (ColorGrading){
        .contrast = 1, .saturation = 1, .gamma = {1, 1, 1}, .gain = {1, 1, 1},
        .lut_strength = 1
    };
}

/** Get the saturation of a linear color: (max - min) / max.
 *
 * @param c The color
 * @return The saturation, 0 for black
 */
static float grading_saturation(
    const float *c)
{
    float hi = glm_max(c[0], glm_max(c[1], c[2]));
    float lo = glm_min(c[0], glm_min(c[1], c[2]));

    return hi > 0 ? (hi - lo) / hi : 0.0f;
}

/** Encode a linear channel to sRGB.
 *
 * @param c The channel
 * @return The encoded channel
 */
static float grading_encode(
    float c)
{
    c = glm_clamp(c, 0.0f, 1.0f);

    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/** Fill an identity strip LUT as the generator of test/assets/luts writes it.
 *
 * @param n Slices of the LUT
 * @return The RGBA rows, freed with ecs_os_free
 */
static uint8_t* grading_identity(
    int32_t n)
{
    uint8_t *result = ecs_os_malloc((ecs_size_t)(n * n * n * 4));
    int32_t x, y;
    for (y = 0; y < n; y ++) {
        for (x = 0; x < n * n; x ++) {
            uint8_t *px = &result[(y * n * n + x) * 4];
            px[0] = (uint8_t)roundf((float)(x % n) / (float)(n - 1) * 255.0f);
            px[1] = (uint8_t)roundf((float)y / (float)(n - 1) * 255.0f);
            px[2] = (uint8_t)roundf((float)(x / n) / (float)(n - 1) * 255.0f);
            px[3] = 255;
        }
    }

    return result;
}

/** Colors the cases grade, linear */
static const float grading_colors[6][3] = {
    {0, 0, 0}, {0.18f, 0.18f, 0.18f}, {0.6f, 0.3f, 0.2f}, {0.8f, 0.02f, 0.02f},
    {1, 1, 1}, {0.05f, 0.4f, 0.9f}
};

/* The cases call no flecs function that would set up the OS API the LUTs allocate with. */
void ColorGrading_setup(void) {
    ecs_os_set_api_defaults();
}

void ColorGrading_balance_identity_at_defaults(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    float m[3][3];
    int32_t i, j;
    box_render_grading_balance(&g, m);
    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 3; j ++) {
            test_flt(m[i][j], i == j ? 1.0f : 0.0f);
        }
    }

    g.exposure = 1;
    box_render_grading_balance(&g, m);
    test_flt(m[0][0], 2);
    test_flt(m[1][1], 2);
    test_flt(m[2][2], 2);
    test_flt(m[0][1], 0);
}

void ColorGrading_balance_tiny_shift_close_to_identity(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    float m[3][3];
    int32_t i, j;
    g.temperature = 1e-4f;
    box_render_grading_balance(&g, m);
    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 3; j ++) {
            test_assert(fabsf(m[i][j] - (i == j ? 1.0f : 0.0f)) < 2e-3f);
        }
    }
}

void ColorGrading_temperature_warms(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    const float grey[3] = {0.18f, 0.18f, 0.18f};
    float out[3];
    g.temperature = 0.5f;
    box_render_grading_apply(&g, false, NULL, 0, grey, out);
    test_assert(out[0] > out[2] * 1.1f);
    g.temperature = -0.5f;
    box_render_grading_apply(&g, false, NULL, 0, grey, out);
    test_assert(out[2] > out[0] * 1.1f);
}

void ColorGrading_tint_magenta(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    const float grey[3] = {0.18f, 0.18f, 0.18f};
    float out[3];
    g.tint = 0.5f;
    box_render_grading_apply(&g, false, NULL, 0, grey, out);
    test_assert(out[1] < out[0]);
    test_assert(out[1] < out[2]);
    g.tint = -0.5f;
    box_render_grading_apply(&g, false, NULL, 0, grey, out);
    test_assert(out[1] > out[0]);
    test_assert(out[1] > out[2]);
}

void ColorGrading_defaults_no_op(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    float out[3];
    int32_t i, c;
    for (i = 0; i < 6; i ++) {
        box_render_grading_apply(&g, false, NULL, 0, grading_colors[i], out);
        for (c = 0; c < 3; c ++) {
            test_flt(out[c], grading_colors[i][c]);
        }

        box_render_grading_apply(&g, true, NULL, 0, grading_colors[i], out);
        for (c = 0; c < 3; c ++) {
            test_flt(out[c], grading_colors[i][c]);
        }
    }
}

void ColorGrading_contrast_pivots(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    const float grey[3] = {0.18f, 0.18f, 0.18f};
    const float half[3] = {0.5f, 0.5f, 0.5f};
    const float bright[3] = {2, 0.6f, 0.05f};
    float out[3];
    g.contrast = 1.5f;
    box_render_grading_apply(&g, false, NULL, 0, grey, out);
    test_assert(fabsf(out[0] - 0.18f) < 1e-4f);
    box_render_grading_apply(&g, false, NULL, 0, bright, out);
    test_assert(fabsf(out[0] - 0.18f * powf(2 / 0.18f, 1.5f)) < 1e-3f);
    test_assert(out[1] > 0.6f);
    test_assert(out[2] < 0.05f);
    box_render_grading_apply(&g, true, NULL, 0, half, out);
    test_assert(fabsf(out[0] - 0.5f) < 1e-5f);
    box_render_grading_apply(&g, true, NULL, 0, grey, out);
    test_assert(fabsf(out[0] - 0.05f) < 1e-5f);
}

void ColorGrading_saturation_zero_is_grey(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    const float *c = grading_colors[2];
    float out[3];
    g.saturation = 0;
    box_render_grading_apply(&g, false, NULL, 0, c, out);
    float luma = c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f;
    test_flt(out[0], luma);
    test_flt(out[1], luma);
    test_flt(out[2], luma);
    g.saturation = 1.5f;
    box_render_grading_apply(&g, false, NULL, 0, c, out);
    test_assert(grading_saturation(out) > grading_saturation(c));
}

void ColorGrading_vibrance_favors_muted(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    const float *muted = grading_colors[2];
    const float *vivid = grading_colors[3];
    float a[3], b[3];
    g.vibrance = 1;
    box_render_grading_apply(&g, false, NULL, 0, muted, a);
    box_render_grading_apply(&g, false, NULL, 0, vivid, b);
    float da = grading_saturation(a) - grading_saturation(muted);
    float db = grading_saturation(b) - grading_saturation(vivid);
    test_assert(da > 0.05f);
    test_assert(da > db);
    box_render_grading_apply(&g, false, NULL, 0, grading_colors[1], a);
    test_assert(fabsf(a[0] - 0.18f) < 1e-5f);
}

void ColorGrading_lift_gamma_gain(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    const float c[3] = {0.25f, 0.5f, 0.75f};
    float out[3];
    int32_t i;
    glm_vec3_copy((vec3){0.1f, 0, -1}, g.lift);
    glm_vec3_copy((vec3){2, 1, 0.5f}, g.gamma);
    glm_vec3_copy((vec3){1, 0.5f, 1}, g.gain);
    box_render_grading_apply(&g, false, NULL, 0, c, out);
    for (i = 0; i < 3; i ++) {
        float expected = powf(glm_max(c[i] * g.gain[i] + g.lift[i], 0.0f), 1.0f / g.gamma[i]);
        test_assert(fabsf(out[i] - expected) < 1e-5f);
    }
}

void ColorGrading_lut_identity_no_op(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    int32_t sizes[2] = {16, 32};
    int32_t s, r, gr, b, c;
    for (s = 0; s < 2; s ++) {
        uint8_t *lut = grading_identity(sizes[s]);
        for (r = 0; r <= 20; r ++) {
            for (gr = 0; gr <= 20; gr ++) {
                for (b = 0; b <= 20; b ++) {
                    float in[3] = {
                        powf((float)r / 20.0f, 2.2f),
                        powf((float)gr / 20.0f, 2.2f),
                        powf((float)b / 20.0f, 2.2f)
                    };
                    float out[3];
                    box_render_grading_apply(&g, true, lut, sizes[s], in, out);
                    for (c = 0; c < 3; c ++) {
                        test_assert(fabsf(grading_encode(out[c]) - grading_encode(in[c])) <=
                            1.0f / 255.0f);
                    }
                }
            }
        }

        ecs_os_free(lut);
    }
}

void ColorGrading_lut_strength_blends(void) {
    // lighting/effects
    ColorGrading g = grading_defaults();
    uint8_t *lut = grading_identity(16);
    int32_t i;
    const float in[3] = {0.3f, 0.6f, 0.1f};
    float full[3], none[3], half[3];
    for (i = 0; i < 16 * 256 * 4; i += 4) {
        lut[i] = (uint8_t)(255 - lut[i]);
    }

    box_render_grading_apply(&g, true, lut, 16, in, full);
    g.lut_strength = 0;
    box_render_grading_apply(&g, true, lut, 16, in, none);
    g.lut_strength = 0.5f;
    box_render_grading_apply(&g, true, lut, 16, in, half);
    test_flt(none[0], in[0]);
    test_assert(full[0] < in[0]);
    test_assert(fabsf(half[0] - (in[0] + full[0]) * 0.5f) < 1e-5f);
    test_assert(fabsf(full[1] - in[1]) < 0.01f);
    ecs_os_free(lut);
}
