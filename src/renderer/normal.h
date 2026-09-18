/**
 * @file renderer/normal.h
 * @brief Screen space tangent frame for normal mapping, shared by the tests and the shaders.
 *
 * A tangent space normal map stores the surface normal in the basis (t, b, n),
 * where t follows +u of the uv layout and b follows +v. The mesh carries no
 * tangents, so the fragment stage rebuilds the frame from the screen space
 * derivatives of the world position and of the uv; `perturb` in shader.c is the
 * WGSL twin of the functions below, so a change here is a change in both places.
 *
 * With p the world position and (u, v) the uv, the chain rule over the screen
 * axes x and y gives two equations in the unknown tangents:
 *
 *   dp1 = dp/dx = t * du/dx + b * dv/dx = t * duv1.x + b * duv1.y
 *   dp2 = dp/dy = t * du/dy + b * dv/dy = t * duv2.x + b * duv2.y
 *
 * Crossing the pair with the normal gives the reciprocal basis of (dp1, dp2) in
 * the tangent plane: with perp1 = dp2 x n and perp2 = n x dp1, both perp1 . dp1
 * and perp2 . dp2 equal area = n . (dp1 x dp2), the determinant of the system.
 * The uv gradients on the surface are then
 *
 *   grad u = (perp1 * duv1.x + perp2 * duv2.x) / area
 *   grad v = (perp1 * duv1.y + perp2 * duv2.y) / area
 *
 * and those are the tangents the normal map expects. Dividing by area is what
 * carries the sign: the frame is a property of the surface and its uvs, not of
 * the screen, so the same expression holds whichever way the framebuffer y axis
 * points and whichever side of the surface the normal faces. Only the length is
 * dropped, by normalizing t and b together so a stretched uv keeps its shape.
 *
 * In this engine area is positive for a surface facing the camera: the world is
 * left handed (X right, Y up, Z forward) while the WebGPU framebuffer y axis
 * points down, and the two flips cancel. The sign is computed rather than
 * assumed so neither convention can silently change the frame.
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
