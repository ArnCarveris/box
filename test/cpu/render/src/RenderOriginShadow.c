/**
 * @file RenderOriginShadow.c
 * @brief Tests for the matrix of a sun shadow cascade relative to the render origin
 * (box_render_cascade_relative): without a render origin it is the matrix of the cascade bit for
 * bit, and far from the local origin it takes a position relative to the render origin to the
 * clip space of the cascade as an f64 fit of the cascade takes the local position.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Local X, Y and Z of the camera of the far cases */
#define ROS_FAR_X (123456.75)
#define ROS_FAR_Y (12.5)
#define ROS_FAR_Z (-98765.25)

/** Largest clip space difference of the relative matrix and the f64 reference */
#define ROS_EPSILON (1e-6)

/** Fit the second cascade of a camera at a position, looking down and turned.
 *
 * @param x X of the camera
 * @param y Y of the camera
 * @param z Z of the camera
 * @param out Receives the cascade
 */
static void ros_fit(
    float x,
    float y,
    float z,
    box_render_cascade_t *out)
{
    Camera c = {.fov = 60, .near_ = 0.1f};
    mat4 camera, proj;
    float dir[3] = {0.3f, 0.8f, 0.5f};
    glm_vec3_normalize(dir);
    glm_mat4_identity(camera);
    glm_translate(camera, (vec3){x, y, z});
    glm_rotate_y(camera, 0.7f, camera);
    glm_rotate_x(camera, -0.3f, camera);
    box_camera_projection(&c, 160, 90, proj);
    box_render_shadow_cascade(camera, proj, 5, 20, dir, 2048, 50, out);
}

/** Take a local position to the clip space of a cascade in f64: its rotation, and its
 * translation from its f64 center.
 *
 * @param c The cascade
 * @param p The local position
 * @param out Receives x, y and z of the clip position
 */
static void ros_reference(
    const box_render_cascade_t *c,
    const double p[3],
    double out[3])
{
    int32_t r, k;
    out[0] = -c->center[0] / (double)c->extent;
    out[1] = -c->center[1] / (double)c->extent;
    out[2] = c->center[2] / (double)c->depth_range;
    for (r = 0; r < 3; r ++) {
        for (k = 0; k < 3; k ++) {
            out[r] += (double)c->matrix[k][r] * p[k];
        }
    }
}

void RenderOriginShadow_relative_zero_same_matrix(void) {
    // lighting/shadows
    box_render_cascade_t c;
    mat4 relative;
    double zero[3] = {0, 0, 0};
    ros_fit(5, 2, 3, &c);
    box_render_cascade_relative(&c, zero, relative);
    test_assert(!memcmp(relative, c.matrix, sizeof(mat4)));

    ros_fit((float)ROS_FAR_X, (float)ROS_FAR_Y, (float)ROS_FAR_Z, &c);
    box_render_cascade_relative(&c, zero, relative);
    test_assert(!memcmp(relative, c.matrix, sizeof(mat4)));
}

void RenderOriginShadow_relative_far_precise(void) {
    // lighting/shadows
    box_render_cascade_t c;
    mat4 relative;
    double delta[3] = {ROS_FAR_X, ROS_FAR_Y, ROS_FAR_Z};
    ros_fit((float)ROS_FAR_X, (float)ROS_FAR_Y, (float)ROS_FAR_Z, &c);
    box_render_cascade_relative(&c, delta, relative);
    test_assert(c.extent > 0);

    int32_t i, k;
    double worst = 0;
    for (i = 0; i < 64; i ++) {
        double p[3], ref[3];
        vec4 rel, clip;
        p[0] = delta[0] + (double)((i * 7) % 23) * 0.37 - 4.0;
        p[1] = delta[1] + (double)((i * 5) % 11) * 0.21 - 1.0;
        p[2] = delta[2] - (double)((i * 3) % 19) * 0.53 - 2.0;
        for (k = 0; k < 3; k ++) {
            rel[k] = (float)(p[k] - delta[k]);
        }

        rel[3] = 1;
        glm_mat4_mulv(relative, rel, clip);
        for (k = 0; k < 3; k ++) {
            p[k] = delta[k] + (double)rel[k];
        }

        ros_reference(&c, p, ref);
        for (k = 0; k < 3; k ++) {
            double d = fabs((double)clip[k] - ref[k]);
            worst = d > worst ? d : worst;
        }

        test_flt(clip[3], 1.0f);
    }

    test_assert(worst < ROS_EPSILON);
}

void RenderOriginShadow_snap_fixed_in_local_frame(void) {
    // lighting/shadows
    /* Cameras far from the local origin a fraction of a texel apart fit cascades whose f64
     * centers are whole texels of the local frame: the snap does not follow the camera. */
    box_render_cascade_t a, b;
    ros_fit((float)ROS_FAR_X, (float)ROS_FAR_Y, (float)ROS_FAR_Z, &a);
    ros_fit((float)ROS_FAR_X + 0.0625f, (float)ROS_FAR_Y, (float)ROS_FAR_Z + 0.03125f, &b);
    test_flt(a.texel, b.texel);

    int32_t k;
    for (k = 0; k < 2; k ++) {
        double ta = a.center[k] / (double)a.texel, tb = b.center[k] / (double)b.texel;
        test_assert(fabs(ta - round(ta)) < 1e-6);
        test_assert(fabs(tb - round(tb)) < 1e-6);
    }
}
