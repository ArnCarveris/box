/**
 * @file render/vignette.c
 * @brief The C twin of the factor of etc/shaders/vignette.wgsl.
 */

#include "render.h"
#include <math.h>

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_vignette_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

float box_render_vignette_factor(
    const Vignette *v,
    float aspect,
    float u,
    float w)
{
    float sx = 1.0f + (aspect - 1.0f) * v->roundness;
    float x = (u - v->center[0]) * sx;
    float y = w - v->center[1];
    float half = 0.5f * sqrtf(sx * sx + 1.0f);
    float d = sqrtf(x * x + y * y) / half;
    float result;
    if (v->smoothness <= 0.0f) {
        result = d >= v->radius ? 1.0f : 0.0f;
    } else {
        float t = glm_clamp((d - v->radius) / v->smoothness, 0.0f, 1.0f);
        result = t * t * (3.0f - 2.0f * t);
    }

    return result;
}

void box_render_vignette_apply(
    const Vignette *v,
    float aspect,
    float u,
    float w,
    const float *in,
    float *out)
{
    float k = box_render_vignette_factor(v, aspect, u, w) * v->intensity;
    float color[3] = {
        box_vignette_linear(v->color.r),
        box_vignette_linear(v->color.g),
        box_vignette_linear(v->color.b)
    };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = in[i] + (in[i] * color[i] - in[i]) * k;
    }
}
