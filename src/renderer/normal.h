/**
 * @file renderer/normal.h
 * @brief Screen space tangent frame for normal mapping, shared by the tests and the shaders.
 *
 * The frame (t along +u, b along +v, n) is rebuilt from screen space derivatives
 * of world position and uv; `perturb` in shader.c is the WGSL twin of these functions.
 *
 * With perp1 = dp2 x n, perp2 = n x dp1 and area = n . (dp1 x dp2), the tangents
 * are t = (perp1 * duv1.x + perp2 * duv2.x) / area and the same with .y for b,
 * normalized together. Dividing by the signed area keeps the frame screen independent.
 */

#ifndef BOX_RENDERER_NORMAL_H
#define BOX_RENDERER_NORMAL_H

#include <box/math.h>

/** Smallest squared tangent length the frame normalizes */
#define BOX_NORMAL_EPSILON (1e-12f)

/** Build the tangent frame of a surface from its screen space derivatives.
 *
 * @param n Unit surface normal in world space
 * @param dp1 Derivative of the world position along the screen x axis
 * @param dp2 Derivative of the world position along the screen y axis
 * @param duv1 Derivative of the uv along the screen x axis
 * @param duv2 Derivative of the uv along the screen y axis
 * @param t Receives the tangent along +u, scaled with b to a shared length
 * @param b Receives the tangent along +v, scaled with t to a shared length
 */
static inline void box_cotangent_frame(
    vec3 n,
    vec3 dp1,
    vec3 dp2,
    vec2 duv1,
    vec2 duv2,
    vec3 *t,
    vec3 *b)
{
    vec3 perp1 = vec3_cross(dp2, n);
    vec3 perp2 = vec3_cross(n, dp1);
    vec3 tu = vec3_add(vec3_scale(perp1, duv1.x), vec3_scale(perp2, duv2.x));
    vec3 tv = vec3_add(vec3_scale(perp1, duv1.y), vec3_scale(perp2, duv2.y));
    float area = vec3_dot(n, vec3_cross(dp1, dp2));
    float longest = fmaxf(fmaxf(vec3_dot(tu, tu), vec3_dot(tv, tv)), BOX_NORMAL_EPSILON);
    float scale = (area >= 0.0f ? 1.0f : -1.0f) / sqrtf(longest);

    *t = vec3_scale(tu, scale);
    *b = vec3_scale(tv, scale);
}

/** Bend a surface normal with a tangent space normal map sample.
 *
 * @param n Unit surface normal in world space
 * @param dp1 Derivative of the world position along the screen x axis
 * @param dp2 Derivative of the world position along the screen y axis
 * @param duv1 Derivative of the uv along the screen x axis
 * @param duv2 Derivative of the uv along the screen y axis
 * @param ts Normal map sample decoded to -1..1, x along +u, y along +v
 * @return The perturbed unit normal in world space
 */
static inline vec3 box_perturb_normal(
    vec3 n,
    vec3 dp1,
    vec3 dp2,
    vec2 duv1,
    vec2 duv2,
    vec3 ts)
{
    vec3 t, b;

    box_cotangent_frame(n, dp1, dp2, duv1, duv2, &t, &b);

    return vec3_normalize(vec3_add(vec3_add(vec3_scale(t, ts.x), vec3_scale(b, ts.y)),
        vec3_scale(n, ts.z)));
}

#endif
