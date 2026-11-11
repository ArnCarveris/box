/**
 * @file render/depth_of_field.c
 * @brief The C twin of the view depth and the circle of confusion of
 * etc/shaders/depth_of_field.wgsl.
 */

#include "render.h"
#include <math.h>

void box_render_depth_of_field_linearize(
    const Camera *camera,
    float *out)
{
    float n = camera->near_, f = camera->far_;
    out[0] = 1.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 1.0f;
    if (!box_camera_has_depth(camera)) {
        goto done;
    }

    if (camera->ortho) {
        out[0] = f;
        out[1] = -(f - n);
    } else if (f > n) {
        out[0] = n * f;
        out[2] = f - n;
        out[3] = n;
    } else {
        out[0] = n;
        out[2] = 1.0f;
        out[3] = 0.0f;
    }

done:
    return;
}

float box_render_depth_of_field_view_depth(
    const float *k,
    float depth)
{
    return (k[0] + k[1] * depth) / glm_max(k[2] * depth + k[3], 1e-20f);
}

float box_render_depth_of_field_coc(
    const DepthOfField *dof,
    float z)
{
    float half = dof->focus_range * 0.5f;
    float near_edge = dof->focus_distance - half;
    float far_edge = dof->focus_distance + half;
    float result = 0.0f;
    if (isinf(z) && z > 0) {
        result = isinf(dof->far_transition) ? 0.0f : 1.0f;
    } else if (z < near_edge) {
        float t = dof->near_transition > 0
            ? (near_edge - z) / dof->near_transition
            : 1.0f;
        result = dof->near_blur ? -glm_min(t, 1.0f) : 0.0f;
    } else if (z > far_edge) {
        float t = dof->far_transition > 0
            ? (z - far_edge) / dof->far_transition
            : 1.0f;
        result = glm_min(t, 1.0f);
    }

    return result;
}

float box_render_depth_of_field_coc_depth(
    const DepthOfField *dof,
    const float *k,
    float depth)
{
    float z = depth > 0 ? box_render_depth_of_field_view_depth(k, depth) : INFINITY;

    return box_render_depth_of_field_coc(dof, z);
}
