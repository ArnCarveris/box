/**
 * @file portal/clip.c
 * @brief Frustum planes, polygon clipping and box tests of the portal traversal.
 *
 * A plane is (nx, ny, nz, d) and a point is inside it when n . p + d is not negative.
 */

#include "portal.h"
#include <math.h>

/** Get the signed distance of a point to a plane.
 *
 * @param plane The plane
 * @param p The point
 * @return The distance, positive inside
 */
static float box_portal_plane_distance(
    const float *plane,
    const float *p)
{
    return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3];
}

void box_portal_camera_init(
    box_portal_camera_t *camera,
    mat4 view_proj,
    const float *eye)
{
    int32_t i, j;
    glm_vec3_copy(ECS_CONST_CAST(float*, eye), camera->eye);

    for (i = 0; i < 4; i ++) {
        int32_t row = i / 2;
        float sign = (i & 1) ? -1.0f : 1.0f;

        for (j = 0; j < 4; j ++) {
            camera->planes[i][j] = view_proj[j][3] + sign * view_proj[j][row];
        }
    }

    for (j = 0; j < 4; j ++) {
        camera->planes[4][j] = view_proj[j][3] - view_proj[j][2];
        camera->planes[5][j] = view_proj[j][2];
    }

    for (i = 0; i < 6; i ++) {
        float *plane = camera->planes[i];
        float len = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);

        if (len < 1e-12f) {
            glm_vec4_copy((vec4){0, 0, 0, 1}, plane);
        } else {
            glm_vec4_scale(plane, 1.0f / len, plane);
        }
    }
}

int32_t box_portal_clip(
    const vec3 *poly,
    int32_t count,
    const vec4 *planes,
    int32_t plane_count,
    vec3 *out)
{
    vec3 buffers[2][BOX_PORTAL_POLY_MAX];
    int32_t i, k, src_count = count < BOX_PORTAL_POLY_MAX ? count : BOX_PORTAL_POLY_MAX;
    int32_t src = 0;

    for (i = 0; i < src_count; i ++) {
        glm_vec3_copy(ECS_CONST_CAST(float*, poly[i]), buffers[0][i]);
    }

    for (k = 0; k < plane_count && src_count >= 3; k ++) {
        const float *plane = planes[k];
        vec3 *from = buffers[src];
        vec3 *to = buffers[1 - src];
        int32_t to_count = 0;

        for (i = 0; i < src_count; i ++) {
            float *a = from[i];
            float *b = from[(i + 1) % src_count];
            float da = box_portal_plane_distance(plane, a);
            float db = box_portal_plane_distance(plane, b);

            if (da >= 0 && to_count < BOX_PORTAL_POLY_MAX) {
                glm_vec3_copy(a, to[to_count ++]);
            }

            if ((da >= 0) != (db >= 0) && to_count < BOX_PORTAL_POLY_MAX) {
                glm_vec3_lerp(a, b, da / (da - db), to[to_count ++]);
            }
        }

        src = 1 - src;
        src_count = to_count;
    }

    if (src_count < 3) {
        src_count = 0;
    }

    for (i = 0; i < src_count; i ++) {
        glm_vec3_copy(buffers[src][i], out[i]);
    }

    return src_count;
}

int32_t box_portal_hull_planes(
    const float *eye,
    const vec3 *poly,
    int32_t count,
    vec4 *out,
    int32_t max)
{
    vec4 planes[BOX_PORTAL_POLY_MAX];
    float spans[BOX_PORTAL_POLY_MAX];
    vec3 center = {0, 0, 0};
    int32_t i, found = 0, result = 0;

    if (count > BOX_PORTAL_POLY_MAX) {
        count = BOX_PORTAL_POLY_MAX;
    }

    for (i = 0; i < count; i ++) {
        glm_vec3_add(center, ECS_CONST_CAST(float*, poly[i]), center);
    }

    if (count) {
        glm_vec3_scale(center, 1.0f / (float)count, center);
    }

    for (i = 0; i < count; i ++) {
        vec3 a, b, n;
        glm_vec3_sub(ECS_CONST_CAST(float*, poly[i]), ECS_CONST_CAST(float*, eye), a);
        glm_vec3_sub(ECS_CONST_CAST(float*, poly[(i + 1) % count]),
            ECS_CONST_CAST(float*, eye), b);
        glm_vec3_cross(a, b, n);

        float len = glm_vec3_norm(n);
        float reach = glm_vec3_norm(a) * glm_vec3_norm(b);
        if (len < 1e-9f || reach < 1e-12f) {
            continue;
        }

        glm_vec3_scale(n, 1.0f / len, n);
        float d = -glm_vec3_dot(n, ECS_CONST_CAST(float*, eye));
        if (glm_vec3_dot(n, center) + d < 0) {
            glm_vec3_negate(n);
            d = -d;
        }

        glm_vec3_copy(n, planes[found]);
        planes[found][3] = d;
        spans[found] = len / reach;
        found ++;
    }

    while (found > max) {
        int32_t shortest = 0;

        for (i = 1; i < found; i ++) {
            if (spans[i] < spans[shortest]) {
                shortest = i;
            }
        }

        for (i = shortest; i + 1 < found; i ++) {
            glm_vec4_copy(planes[i + 1], planes[i]);
            spans[i] = spans[i + 1];
        }

        found --;
    }

    for (i = 0; i < found; i ++) {
        glm_vec4_copy(planes[i], out[result ++]);
    }

    return result;
}

bool box_portal_box_inside(
    const float *min,
    const float *max,
    const vec4 *planes,
    int32_t count)
{
    bool result = true;
    int32_t i;

    for (i = 0; i < count && result; i ++) {
        const float *p = planes[i];
        float x = p[0] > 0 ? max[0] : min[0];
        float y = p[1] > 0 ? max[1] : min[1];
        float z = p[2] > 0 ? max[2] : min[2];

        result = p[0] * x + p[1] * y + p[2] * z + p[3] >= 0;
    }

    return result;
}

bool box_portal_box_contained(
    const float *min,
    const float *max,
    const vec4 *planes,
    int32_t count)
{
    bool result = true;
    int32_t i;

    for (i = 0; i < count && result; i ++) {
        const float *p = planes[i];
        float x = p[0] > 0 ? min[0] : max[0];
        float y = p[1] > 0 ? min[1] : max[1];
        float z = p[2] > 0 ? min[2] : max[2];

        result = p[0] * x + p[1] * y + p[2] * z + p[3] >= 0;
    }

    return result;
}
