/**
 * @file render/grading.c
 * @brief White balance matrix of a ColorGrading and the C twin of etc/shaders/grading.wgsl.
 */

#include "render.h"
#include <math.h>

/** Linear Rec.709 to LMS, the rows of the matrix of Unity and Bevy */
static const float box_grading_lin_to_lms[3][3] = {
    {3.90405e-1f, 5.49941e-1f, 8.92632e-3f},
    {7.08416e-2f, 9.63172e-1f, 1.35775e-3f},
    {2.31082e-2f, 1.28021e-1f, 9.36245e-1f}
};

/** LMS to linear Rec.709, the inverse of box_grading_lin_to_lms */
static const float box_grading_lms_to_lin[3][3] = {
    {2.85847e+0f, -1.62879e+0f, -2.48910e-2f},
    {-2.10182e-1f, 1.15820e+0f, 3.24281e-4f},
    {-4.18120e-2f, -1.18169e-1f, 1.06867e+0f}
};

/** Log2 of the middle grey contrast pivots around in HDR */
#define BOX_GRADING_LOG_GREY (-2.4739312f)

/** Get the scale of each LMS channel that adapts the white of a temperature and tint to D65.
 *
 * @param temperature The temperature, -1 blue to 1 warm
 * @param tint The tint, -1 green to 1 magenta
 * @param out Receives the scale of L, M and S
 */
static void box_grading_lms_balance(
    float temperature,
    float tint,
    float *out)
{
    const float white[3] = {0.949237f, 1.03542f, 1.08728f};
    float t1 = temperature * 10.0f / 6.0f;
    float t2 = tint * 10.0f / 6.0f;
    float x = 0.31271f - t1 * (t1 < 0 ? 0.1f : 0.05f);
    float y = 2.87f * x - 3.0f * x * x - 0.27509507f + t2 * 0.05f;
    float X = x / y;
    float Z = (1.0f - x - y) / y;
    float lms[3] = {
        0.7328f * X + 0.4296f - 0.1624f * Z,
        -0.7036f * X + 1.6975f + 0.0061f * Z,
        0.0030f * X + 0.0136f + 0.9834f * Z
    };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = white[i] / lms[i];
    }
}

void box_render_grading_balance(
    const ColorGrading *g,
    float out[3][3])
{
    float scale = exp2f(g->exposure);
    float lms[3] = {1.0f, 1.0f, 1.0f};
    bool balance = g->temperature != 0 || g->tint != 0;
    int32_t i, j, k;
    if (balance) {
        box_grading_lms_balance(g->temperature, g->tint, lms);
    }

    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 3; j ++) {
            float sum = 0;
            for (k = 0; k < 3 && balance; k ++) {
                sum += box_grading_lms_to_lin[i][k] * lms[k] * box_grading_lin_to_lms[k][j];
            }

            out[i][j] = (balance ? sum : (i == j ? 1.0f : 0.0f)) * scale;
        }
    }
}

/** Luminance of a linear color, as luminance() of etc/shaders/include/effect.wgsl.
 *
 * @param c The color
 * @return The luminance
 */
static float box_grading_luminance(
    const float *c)
{
    return c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f;
}

/** Encode a linear channel in 0..1 to sRGB.
 *
 * @param c The channel
 * @return The encoded channel
 */
static float box_grading_encode(
    float c)
{
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/** Decode an sRGB channel to linear.
 *
 * @param c The channel
 * @return The linear channel
 */
static float box_grading_decode(
    float c)
{
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Fetch a texel of a strip LUT.
 *
 * @param lut The rows of the strip
 * @param n Slices of the strip
 * @param x Column within the slice
 * @param y Row
 * @param slice The slice
 * @param out Receives the texel, 0..1 as stored
 */
static void box_grading_texel(
    const uint8_t *lut,
    int32_t n,
    int32_t x,
    int32_t y,
    int32_t slice,
    float *out)
{
    const uint8_t *px = &lut[((size_t)y * (size_t)(n * n) + (size_t)(slice * n + x)) * 4];
    int32_t c;
    for (c = 0; c < 3; c ++) {
        out[c] = (float)px[c] / 255.0f;
    }
}

/** Sample a strip LUT with trilinear filtering: bilinear within the two nearest slices,
 * blended by blue.
 *
 * @param lut The rows of the strip
 * @param n Slices of the strip
 * @param e The sRGB encoded color in 0..1
 * @param out Receives the encoded result
 */
static void box_grading_lut(
    const uint8_t *lut,
    int32_t n,
    const float *e,
    float *out)
{
    float last = (float)(n - 1);
    float fx = e[0] * last, fy = e[1] * last, fb = e[2] * last;
    int32_t x0 = (int32_t)floorf(fx), y0 = (int32_t)floorf(fy), s0 = (int32_t)floorf(fb);
    int32_t x1 = glm_imin(x0 + 1, n - 1), y1 = glm_imin(y0 + 1, n - 1);
    int32_t s1 = glm_imin(s0 + 1, n - 1);
    int32_t slices[2] = {s0, s1};
    float tx = fx - (float)x0, ty = fy - (float)y0, tb = fb - (float)s0;
    float slice[2][3];
    int32_t i, c;
    for (i = 0; i < 2; i ++) {
        float a[3], b[3], d[3], f[3];
        box_grading_texel(lut, n, x0, y0, slices[i], a);
        box_grading_texel(lut, n, x1, y0, slices[i], b);
        box_grading_texel(lut, n, x0, y1, slices[i], d);
        box_grading_texel(lut, n, x1, y1, slices[i], f);
        for (c = 0; c < 3; c ++) {
            float top = a[c] + (b[c] - a[c]) * tx;
            float bottom = d[c] + (f[c] - d[c]) * tx;
            slice[i][c] = top + (bottom - top) * ty;
        }
    }

    for (c = 0; c < 3; c ++) {
        out[c] = slice[0][c] + (slice[1][c] - slice[0][c]) * tb;
    }
}

/** Blend a color toward its luminance or away from it, and clamp it at 0.
 *
 * @param c The color, changed in place
 * @param amount 0 for grey, 1 for the color
 */
static void box_grading_saturate(
    float *c,
    float amount)
{
    float luma = box_grading_luminance(c);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        c[i] = glm_max(luma + (c[i] - luma) * amount, 0.0f);
    }
}

void box_render_grading_apply(
    const ColorGrading *g,
    bool display,
    const uint8_t *lut,
    int32_t lut_size,
    const float *in,
    float *out)
{
    float m[3][3], c[3];
    int32_t i;
    box_render_grading_balance(g, m);
    for (i = 0; i < 3; i ++) {
        c[i] = m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2];
    }

    if (g->contrast != 1.0f) {
        for (i = 0; i < 3; i ++) {
            if (display) {
                c[i] = (c[i] - 0.5f) * g->contrast + 0.5f;
            } else {
                c[i] = c[i] > 0 ? exp2f((log2f(c[i]) - BOX_GRADING_LOG_GREY) * g->contrast +
                    BOX_GRADING_LOG_GREY) : 0.0f;
            }

            c[i] = glm_max(c[i], 0.0f);
        }
    }

    if (g->saturation != 1.0f) {
        box_grading_saturate(c, g->saturation);
    }

    if (g->vibrance != 0.0f) {
        float hi = glm_max(c[0], glm_max(c[1], c[2]));
        float lo = glm_min(c[0], glm_min(c[1], c[2]));
        float s = hi > 1e-6f ? glm_clamp((hi - lo) / hi, 0.0f, 1.0f) : 0.0f;
        box_grading_saturate(c, 1.0f + g->vibrance * (1.0f - s));
    }

    bool curve = false;
    for (i = 0; i < 3; i ++) {
        curve = curve || g->lift[i] != 0.0f || g->gamma[i] != 1.0f;
    }

    for (i = 0; i < 3; i ++) {
        c[i] = c[i] * g->gain[i] + g->lift[i];
        if (curve) {
            c[i] = c[i] > 0 ? powf(c[i], 1.0f / g->gamma[i]) : 0.0f;
        }
    }

    if (lut && lut_size > 1 && g->lut_strength > 0) {
        float e[3], r[3];
        for (i = 0; i < 3; i ++) {
            e[i] = box_grading_encode(glm_clamp(c[i], 0.0f, 1.0f));
        }

        box_grading_lut(lut, lut_size, e, r);
        for (i = 0; i < 3; i ++) {
            c[i] += (box_grading_decode(r[i]) - c[i]) * g->lut_strength;
        }
    }

    glm_vec3_copy(c, out);
}
