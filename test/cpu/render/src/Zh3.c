/**
 * @file Zh3.c
 * @brief Tests for the ZH3 the probes of a light probe volume are stored as: the packing of L2
 * SH irradiance, its expansion and its evaluation, which etc/shaders/include/sh.wgsl mirrors.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Sloan's real SH basis up to band 2, as basis() of etc/shaders/bake_sh.wgsl.
 *
 * @param d A unit direction
 * @param out Receives the 9 values
 */
static void zh3_basis(
    const float d[3],
    float out[BOX_RENDER_SH_COEFFICIENTS])
{
    out[0] = 0.2820947918f;
    out[1] = 0.4886025119f * d[1];
    out[2] = 0.4886025119f * d[2];
    out[3] = 0.4886025119f * d[0];
    out[4] = 1.0925484306f * d[0] * d[1];
    out[5] = 1.0925484306f * d[1] * d[2];
    out[6] = 0.3153915653f * (3.0f * d[2] * d[2] - 1.0f);
    out[7] = 1.0925484306f * d[0] * d[2];
    out[8] = 0.5462742153f * (d[0] * d[0] - d[1] * d[1]);
}

/** Irradiance of L2 coefficients in a direction, as sh_irradiance() of sh.wgsl.
 *
 * @param sh The coefficients
 * @param d A unit direction
 * @param c The channel
 * @return The irradiance before the division by pi
 */
static float zh3_l2_eval(
    const float sh[BOX_RENDER_SH_COEFFICIENTS][4],
    const float d[3],
    int32_t c)
{
    float y[BOX_RENDER_SH_COEFFICIENTS], result = 0;
    int32_t k;
    zh3_basis(d, y);
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        result += sh[k][c] * y[k];
    }

    return result;
}

/** Irradiance of a ZH3 probe in a direction, as zh3_irradiance() of sh.wgsl.
 *
 * @param zh3 The probe
 * @param d A unit direction
 * @param c The channel
 * @return The irradiance before the division by pi
 */
static float zh3_eval(
    const float zh3[BOX_RENDER_ZH3_VECTORS][4],
    const float d[3],
    int32_t c)
{
    float a[3];
    box_render_zh3_axis(&zh3[1], a);
    float t = a[0] * d[0] + a[1] * d[1] + a[2] * d[2];

    return zh3[0][c] * 0.2820947918f +
        (zh3[1][c] * d[1] + zh3[2][c] * d[2] + zh3[3][c] * d[0]) * 0.4886025119f +
        zh3[c][3] * 0.3153915653f * (3.0f * t * t - 1.0f);
}

/** L2 irradiance of a delta light of a color from a direction: E_lm = A_l Y_lm(d) color.
 *
 * @param d A unit direction toward the light
 * @param color Linear rgb
 * @param out Receives the coefficients
 */
static void zh3_delta(
    const float d[3],
    const float color[3],
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    float y[BOX_RENDER_SH_COEFFICIENTS];
    int32_t k, c;
    zh3_basis(d, y);
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        float band = k == 0 ? GLM_PIf : k < 4 ? 2.0f * GLM_PIf / 3.0f : GLM_PIf / 4.0f;
        for (c = 0; c < 3; c ++) {
            out[k][c] = band * y[k] * color[c];
        }

        out[k][3] = 0;
    }
}

/** Some L2 coefficients with every band 2 coefficient set.
 *
 * @param out Receives the coefficients
 */
static void zh3_mixed(
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    int32_t k, c;
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        for (c = 0; c < 3; c ++) {
            out[k][c] = sinf((float)(k * 3 + c) * 1.3f + 0.4f) * (k ? 0.5f : 2.0f);
        }

        out[k][3] = 0;
    }
}

void Zh3_layout(void) {
    // baked_lighting/probe_volume
    float sh[BOX_RENDER_SH_COEFFICIENTS][4], zh3[BOX_RENDER_ZH3_VECTORS][4], a[3], y[5];
    int32_t k, c;
    zh3_mixed(sh);
    box_render_sh_to_zh3(sh, zh3);
    test_int(BOX_RENDER_ZH3_VECTORS * 4 * (int32_t)sizeof(float), 64);
    for (k = 0; k < 4; k ++) {
        for (c = 0; c < 3; c ++) {
            test_flt(zh3[k][c], sh[k][c]);
        }
    }

    test_flt(zh3[3][3], 0);

    /* z = sum_m E_2m y_m, y_m = sqrt(4 pi / 5) Y_2m(axis) */
    box_render_zh3_axis(&sh[1], a);
    float basis[BOX_RENDER_SH_COEFFICIENTS];
    zh3_basis(a, basis);
    for (k = 0; k < 5; k ++) {
        y[k] = sqrtf(4.0f * GLM_PIf / 5.0f) * basis[4 + k];
    }

    for (c = 0; c < 3; c ++) {
        float z = 0;
        for (k = 0; k < 5; k ++) {
            z += sh[4 + k][c] * y[k];
        }

        test_assert(fabsf(zh3[c][3] - z) < 1e-5f);
    }
}

void Zh3_axis_is_luminance_of_band1(void) {
    // baked_lighting/probe_volume
    float band1[3][4] = {
        {0, 1, 0, 0},   /* y */
        {0, 0, 0, 0},   /* z */
        {1, 0, 0, 0}    /* x */
    };
    float a[3];
    box_render_zh3_axis(band1, a);
    float x = 0.2126f, yy = 0.7152f, len = sqrtf(x * x + yy * yy);
    test_assert(fabsf(a[0] - x / len) < 1e-6f);
    test_assert(fabsf(a[1] - yy / len) < 1e-6f);
    test_assert(fabsf(a[2]) < 1e-6f);
}

void Zh3_zero_band1_axis_z(void) {
    // baked_lighting/probe_volume
    float sh[BOX_RENDER_SH_COEFFICIENTS][4] = {{0}};
    float zh3[BOX_RENDER_ZH3_VECTORS][4], back[BOX_RENDER_SH_COEFFICIENTS][4], a[3];
    int32_t k, c;
    zh3_mixed(sh);
    for (k = 1; k < 4; k ++) {
        for (c = 0; c < 3; c ++) {
            sh[k][c] = 0;
        }
    }

    box_render_zh3_axis(&sh[1], a);
    test_flt(a[0], 0);
    test_flt(a[1], 0);
    test_flt(a[2], 1);
    box_render_sh_to_zh3(sh, zh3);
    box_render_zh3_to_sh(zh3, back);
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        for (c = 0; c < 4; c ++) {
            test_assert(isfinite(back[k][c]));
        }
    }

    for (c = 0; c < 3; c ++) {
        test_flt(zh3[c][3], sh[6][c]);
        test_flt(back[6][c], sh[6][c]);
        test_flt(back[4][c], 0);
        test_flt(back[5][c], 0);
        test_flt(back[7][c], 0);
        test_flt(back[8][c], 0);
    }
}

void Zh3_single_light_exact(void) {
    // baked_lighting/probe_volume
    float d[3] = {0.48f, 0.6f, -0.64f}, color[3] = {1.0f, 0.7f, 0.3f};
    float sh[BOX_RENDER_SH_COEFFICIENTS][4], zh3[BOX_RENDER_ZH3_VECTORS][4];
    float back[BOX_RENDER_SH_COEFFICIENTS][4];
    int32_t k, c;
    glm_vec3_normalize(d);
    zh3_delta(d, color, sh);
    box_render_sh_to_zh3(sh, zh3);
    box_render_zh3_to_sh(zh3, back);
    for (k = 0; k < BOX_RENDER_SH_COEFFICIENTS; k ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(back[k][c] - sh[k][c]) < 1e-5f);
        }
    }
}

void Zh3_eval_matches_expansion(void) {
    // baked_lighting/probe_volume
    float sh[BOX_RENDER_SH_COEFFICIENTS][4], zh3[BOX_RENDER_ZH3_VECTORS][4];
    float back[BOX_RENDER_SH_COEFFICIENTS][4];
    int32_t i, c;
    zh3_mixed(sh);
    box_render_sh_to_zh3(sh, zh3);
    box_render_zh3_to_sh(zh3, back);
    for (i = 0; i < 64; i ++) {
        float d[3] = {sinf((float)i * 0.7f), cosf((float)i * 1.9f), sinf((float)i * 2.3f + 1)};
        glm_vec3_normalize(d);
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(zh3_eval(zh3, d, c) - zh3_l2_eval(back, d, c)) < 1e-4f);
        }
    }
}

void Zh3_expansion_is_idempotent(void) {
    // baked_lighting/probe_volume
    float sh[BOX_RENDER_SH_COEFFICIENTS][4], zh3[BOX_RENDER_ZH3_VECTORS][4];
    float back[BOX_RENDER_SH_COEFFICIENTS][4], again[BOX_RENDER_ZH3_VECTORS][4];
    int32_t k, c;
    zh3_mixed(sh);
    box_render_sh_to_zh3(sh, zh3);
    box_render_zh3_to_sh(zh3, back);
    box_render_sh_to_zh3(back, again);
    for (k = 0; k < BOX_RENDER_ZH3_VECTORS; k ++) {
        for (c = 0; c < 4; c ++) {
            test_assert(fabsf(again[k][c] - zh3[k][c]) < 1e-5f);
        }
    }
}
