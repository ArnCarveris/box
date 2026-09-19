/**
 * @file renderer/ssr.h
 * @brief Screen space reflections: the march test, the roughness weight and the fade.
 *
 * A reflection ray is marched in world space and projected into the image at
 * every step. A step is a crossing when the ray has travelled past the surface
 * the image shows at that texel while the step before it was still in front of
 * that surface, which finds a hit from the change of side alone and does not
 * depend on where the steps happened to land. How far behind the crossing ended
 * up, measured against the distance the ray and the surface both moved over
 * that step, is its confidence, which fades the hit into the environment
 * instead of stepping to it. The same rules run here and in the reflection
 * shader, so a change here changes both.
 *
 * A hit is weighted down as the surface roughens and as the path of its ray
 * approaches the edge of the image, where the march would leave what the frame
 * drew; what is left falls back to the prefiltered environment, so a ray that
 * leaves the image finds its neighbours already faded to the same environment
 * instead of standing out as a single missed pixel.
 */

#ifndef BOX_RENDERER_SSR_H
#define BOX_RENDERER_SSR_H

#include <stdint.h>
#include <stdbool.h>

/** Steps a reflection ray marches by default */
#define BOX_SSR_STEPS (24)

/** World thickness a surface is given behind its depth, by default */
#define BOX_SSR_THICKNESS (0.4f)

/** Roughness a surface stops reflecting at by default */
#define BOX_SSR_MAX_ROUGHNESS (0.6f)

/** Fraction of the traced reflection that reaches the image by default */
#define BOX_SSR_INTENSITY (1.0f)

/** Fraction of the image the reflection fades out over at its edges, by default */
#define BOX_SSR_FADE (0.12f)

/** Hermite interpolation between two edges, the smoothstep of the shader.
 *
 * @param low Value the result reaches zero at
 * @param high Value the result reaches one at
 * @param x The value
 * @return The interpolation, between zero and one
 */
static inline float box_ssr_smoothstep(
    float low,
    float high,
    float x)
{
    float span = high - low;
    float t = (x - low) / (span > 0.000001f ? span : 0.000001f);
    if (t < 0.0f) {
        t = 0.0f;
    }

    if (t > 1.0f) {
        t = 1.0f;
    }

    return t * t * (3.0f - 2.0f * t);
}

/** Whether one step of a march ended up behind the surface the image shows.
 *
 * @param ray View distance of the point on the ray
 * @param scene View distance the image holds at the texel it projects to
 * @return True when the ray is behind that surface
 */
static inline bool box_ssr_behind(
    float ray,
    float scene)
{
    return scene > 0.0f && ray > scene;
}

/** Confidence a crossing keeps for how far behind the surface it landed.
 *
 * @param gap World distance the refined crossing ended up behind the surface
 * @param tolerance Distance the ray and the surface moved over the step
 * @return One inside the tolerance, falling to zero one tolerance further
 */
static inline float box_ssr_confidence(
    float gap,
    float tolerance)
{
    float tol = tolerance > 0.000001f ? tolerance : 0.000001f;
    float over = (gap - tol) / tol;
    if (over < 0.0f) {
        over = 0.0f;
    }

    if (over > 1.0f) {
        over = 1.0f;
    }

    return 1.0f - over;
}

/** March a ray over the view distances its steps sampled.
 *
 * @param scene View distance of the image at every step, 0 where it drew nothing
 * @param ray View distance of the ray at every step
 * @param steps Steps both arrays hold
 * @return Index of the step that crossed behind a surface, -1 when the ray missed
 */
static inline int32_t box_ssr_march(
    const float *scene,
    const float *ray,
    int32_t steps)
{
    int32_t result = -1;
    bool front = true;
    int32_t i;
    for (i = 0; i < steps; i ++) {
        bool behind = box_ssr_behind(ray[i], scene[i]);
        if (result < 0 && behind && front) {
            result = i;
        }

        front = !behind;
    }

    return result;
}

/** Weight a hit keeps at a roughness.
 *
 * @param roughness Roughness of the surface the ray started from
 * @param max_roughness Roughness the reflection has faded out at, 0 takes the default
 * @return The weight, one on a mirror and zero past the maximum
 */
static inline float box_ssr_roughness_weight(
    float roughness,
    float max_roughness)
{
    float limit = max_roughness > 0.0f ? max_roughness : BOX_SSR_MAX_ROUGHNESS;

    return 1.0f - box_ssr_smoothstep(limit * 0.5f, limit, roughness);
}

/** How far a texture coordinate is from the nearest border of the image.
 *
 * @param u Horizontal texture coordinate
 * @param v Vertical texture coordinate
 * @return The distance to the nearest border, in texture coordinates
 */
static inline float box_ssr_border(
    float u,
    float v)
{
    float du = u < 1.0f - u ? u : 1.0f - u;
    float dv = v < 1.0f - v ? v : 1.0f - v;

    return du < dv ? du : dv;
}

/** Weight a hit keeps for how near the edge of the image its ray passed.
 *
 * @param border Smallest distance to a border the path of the ray reached
 * @param fade Fraction of the image the reflection fades out over, 0 takes the default
 * @return The weight, one in the middle and zero at the border
 */
static inline float box_ssr_edge_fade(
    float border,
    float fade)
{
    float width = fade > 0.0f ? fade : BOX_SSR_FADE;

    return box_ssr_smoothstep(0.0f, width, border);
}

#endif
