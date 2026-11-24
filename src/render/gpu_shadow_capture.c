/**
 * @file render/gpu_shadow_capture.c
 * @brief Fit of the cascade of the shadow map of the probe captures of the webgpu backend.
 */

#include "gpu.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

void box_gpu_shadow_capture_fit(
    const float *min,
    const float *max,
    const float *direction,
    box_render_cascade_t *out)
{
    vec3 center, forward, right, up, pole = {0, 1, 0};
    float res = (float)BOX_GPU_CAPTURE_SHADOW_RESOLUTION;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        center[i] = (min[i] + max[i]) * 0.5f;
        forward[i] = -direction[i];
    }

    float radius = glm_max(glm_vec3_distance(ECS_CONST_CAST(float*, min),
        ECS_CONST_CAST(float*, max)) * 0.5f, 1e-3f);
    float extent = radius * res / (res - 2.0f * (float)BOX_GPU_CAPTURE_SHADOW_MARGIN);
    if (glm_vec3_norm(forward) < 1e-6f) {
        glm_vec3_copy((vec3){0, -1, 0}, forward);
    }

    glm_vec3_normalize(forward);
    if (fabsf(forward[1]) > 0.99f) {
        glm_vec3_copy((vec3){0, 0, 1}, pole);
    }

    glm_vec3_crossn(forward, pole, right);
    glm_vec3_cross(right, forward, up);
    float cx = glm_vec3_dot(right, center);
    float cy = glm_vec3_dot(up, center);
    float cz = glm_vec3_dot(forward, center);
    float d_near = cz - extent;
    float d_far = cz + extent;
    float range = d_far - d_near;
    glm_mat4_zero(out->matrix);
    for (i = 0; i < 3; i ++) {
        out->matrix[i][0] = right[i] / extent;
        out->matrix[i][1] = up[i] / extent;
        out->matrix[i][2] = -forward[i] / range;
    }

    out->matrix[3][0] = -cx / extent;
    out->matrix[3][1] = -cy / extent;
    out->matrix[3][2] = d_far / range;
    out->matrix[3][3] = 1.0f;
    out->texel = 2.0f * extent / res;
    out->depth_range = range;
}

#endif
