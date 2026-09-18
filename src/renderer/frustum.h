/**
 * @file renderer/frustum.h
 * @brief View frustum planes with a sphere and a box test, shared by extract and the renderer.
 *
 * The planes come out of the view projection matrix in the clip convention the
 * engine uses: x and y span minus one to one and z spans zero to one, with a
 * reverse Z projection whose far plane sits at infinity. That far plane then has
 * a zero normal and a positive distance, so every point passes it.
 */

#ifndef BOX_RENDERER_FRUSTUM_H
#define BOX_RENDERER_FRUSTUM_H

#include <math.h>
#include "packet.h"

/** Index of the left plane */
#define BOX_FRUSTUM_LEFT (0)

/** Index of the right plane */
#define BOX_FRUSTUM_RIGHT (1)

/** Index of the bottom plane */
#define BOX_FRUSTUM_BOTTOM (2)

/** Index of the top plane */
#define BOX_FRUSTUM_TOP (3)

/** Index of the near plane, which the clip condition z less than w gives */
#define BOX_FRUSTUM_NEAR (4)

/** Index of the far plane, which the clip condition z above zero gives */
#define BOX_FRUSTUM_FAR (5)

/** The six planes of a view frustum in world space */
typedef struct box_frustum_t {
    float planes[BOX_FRUSTUM_PLANES][4]; /**< Inward normal xyz and distance w of a plane */
} box_frustum_t;

/** Write a plane into a frustum, normalized so distances are world units.
 *
 * A plane whose normal is zero keeps its distance, which makes the test read
 * that every point is on its inner side; that is what an infinite far plane is.
 *
 * @param out The four floats of the plane
 * @param x Normal x
 * @param y Normal y
 * @param z Normal z
 * @param w Distance term
 */
static inline void box_frustum_plane(
    float *out,
    float x,
    float y,
    float z,
    float w)
{
    float length = sqrtf(x * x + y * y + z * z);
    float scale = length > 0 ? 1.0f / length : 1.0f;
    out[0] = x * scale;
    out[1] = y * scale;
    out[2] = z * scale;
    out[3] = w * scale;
}

/** Multiply two column major 4x4 matrices.
 *
 * @param a The left matrix
 * @param b The right matrix
 * @param out The product
 */
static inline void box_frustum_mul(
    const float *a,
    const float *b,
    float *out)
{
    int col;
    int row;
    for (col = 0; col < 4; col ++) {
        for (row = 0; row < 4; row ++) {
            out[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
}

/** Build the six planes of a frustum from a world to clip matrix.
 *
 * @param m The world to clip matrix, sixteen floats
 * @param out The frustum to fill
 */
static inline void box_frustum_of_clip(
    const float *m,
    box_frustum_t *out)
{
    box_frustum_plane(out->planes[BOX_FRUSTUM_LEFT],
        m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);
    box_frustum_plane(out->planes[BOX_FRUSTUM_RIGHT],
        m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);
    box_frustum_plane(out->planes[BOX_FRUSTUM_BOTTOM],
        m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);
    box_frustum_plane(out->planes[BOX_FRUSTUM_TOP],
        m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);
    box_frustum_plane(out->planes[BOX_FRUSTUM_NEAR],
        m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);
    box_frustum_plane(out->planes[BOX_FRUSTUM_FAR], m[2], m[6], m[10], m[14]);
}

/** Build the six planes of a frustum from a view and a projection matrix.
 *
 * @param view The world to view matrix, sixteen floats
 * @param proj The view to clip matrix, sixteen floats
 * @param out The frustum to fill
 */
static inline void box_frustum_build(
    const float *view,
    const float *proj,
    box_frustum_t *out)
{
    float m[16];
    box_frustum_mul(proj, view, m);
    box_frustum_of_clip(m, out);
}

/** Build the planes of the frustum of a view.
 *
 * @param v The view
 * @param out The frustum to fill
 */
static inline void box_frustum_of_view(
    const box_view_t *v,
    box_frustum_t *out)
{
    box_frustum_build(v->view, v->proj, out);
}

/** Replace the far plane of a frustum with one at a view space distance.
 *
 * The projection has no far plane of its own, so a pass that needs a bounded
 * frustum, like the clustered light assignment, closes it at its own distance.
 *
 * @param f The frustum
 * @param view The world to view matrix, sixteen floats
 * @param far_ View space distance of the plane
 */
static inline void box_frustum_close(
    box_frustum_t *f,
    const float *view,
    float far_)
{
    box_frustum_plane(f->planes[BOX_FRUSTUM_FAR],
        -view[2], -view[6], -view[10], far_ - view[14]);
}

/** Test a world space sphere against a frustum.
 *
 * @param f The frustum
 * @param center Center of the sphere, three floats
 * @param radius Radius of the sphere
 * @return True when the sphere is not fully outside one of the planes
 */
static inline bool box_frustum_sphere(
    const box_frustum_t *f,
    const float *center,
    float radius)
{
    bool result = true;
    int i;
    for (i = 0; i < BOX_FRUSTUM_PLANES && result; i ++) {
        const float *p = f->planes[i];
        result = p[0] * center[0] + p[1] * center[1] + p[2] * center[2] + p[3] >= -radius;
    }

    return result;
}

/** Test a world space axis aligned box against a frustum.
 *
 * Only the corner farthest along the normal of a plane is tested, which rejects
 * a box when every corner is outside that plane.
 *
 * @param f The frustum
 * @param min Minimum corner, three floats
 * @param max Maximum corner, three floats
 * @return True when the box is not fully outside one of the planes
 */
static inline bool box_frustum_aabb(
    const box_frustum_t *f,
    const float *min,
    const float *max)
{
    bool result = true;
    int i;
    for (i = 0; i < BOX_FRUSTUM_PLANES && result; i ++) {
        const float *p = f->planes[i];
        float distance = p[3];
        int axis;
        for (axis = 0; axis < 3; axis ++) {
            distance += p[axis] * (p[axis] >= 0 ? max[axis] : min[axis]);
        }

        result = distance >= 0;
    }

    return result;
}

#endif
