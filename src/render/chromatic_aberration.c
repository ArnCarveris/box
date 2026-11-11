/**
 * @file render/chromatic_aberration.c
 * @brief The C twin of the offset and the spectral weights of
 * etc/shaders/chromatic_aberration.wgsl.
 */

#include "render.h"
#include <math.h>

/** Get the unnormalized spectral ramp at a position along the taps.
 *
 * @param s Position from 0 (-offset) to 1 (+offset)
 * @param out Receives the red, green and blue weights
 */
static void box_ca_ramp(
    float s,
    float *out)
{
    out[0] = glm_max(1.0f - 2.0f * s, 0.0f);
    out[1] = glm_max(1.0f - fabsf(2.0f * s - 1.0f), 0.0f);
    out[2] = glm_max(2.0f * s - 1.0f, 0.0f);
}

float box_render_chromatic_aberration_corner(
    const ChromaticAberration *ca,
    float width,
    float height)
{
    float dx = glm_max(ca->center[0], 1.0f - ca->center[0]) * width;
    float dy = glm_max(ca->center[1], 1.0f - ca->center[1]) * height;

    return glm_max(sqrtf(dx * dx + dy * dy), 1.0f);
}

float box_render_chromatic_aberration_offset(
    const ChromaticAberration *ca,
    float width,
    float height,
    float x,
    float y)
{
    float dx = x - ca->center[0] * width;
    float dy = y - ca->center[1] * height;
    float d = sqrtf(dx * dx + dy * dy);
    float result = 0.0f;
    if (d > 0.0f) {
        float t = d / box_render_chromatic_aberration_corner(ca, width, height);
        result = ca->intensity * width * powf(t, ca->falloff);
    }

    return result;
}

void box_render_chromatic_aberration_weights(
    int32_t samples,
    int32_t i,
    float *out)
{
    float sum[3] = {0, 0, 0}, w[3];
    int32_t j, c;
    for (j = 0; j < samples; j ++) {
        box_ca_ramp((float)j / (float)(samples - 1), w);
        for (c = 0; c < 3; c ++) {
            sum[c] += w[c];
        }
    }

    box_ca_ramp((float)i / (float)(samples - 1), out);
    for (c = 0; c < 3; c ++) {
        out[c] /= sum[c];
    }
}
