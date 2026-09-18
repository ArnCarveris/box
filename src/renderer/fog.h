/**
 * @file renderer/fog.h
 * @brief Analytic integral of the height fog along a view ray.
 *
 * The integral is the CPU twin of etc/shaders/fog.wgsl: both sides must answer
 * the same, so a change here changes that shader too.
 */

#ifndef BOX_RENDERER_FOG_H
#define BOX_RENDERER_FOG_H

#include <math.h>

/** Largest exponent the density and the integral evaluate, which keeps a steep
 * falloff far below the fog height finite */
#define BOX_FOG_EXP_MAX (32.0f)

/** Distance the fog integrates to for a pixel that hit no geometry */
#define BOX_FOG_FAR (5000.0f)

/** Optical depth of a ray through a height fog.
 *
 * The density is the density of the component at the fog height and falls off
 * by one e per meter of falloff above it, which integrates in closed form along
 * a straight ray.
 *
 * @param density Extinction per meter at the fog height
 * @param falloff Density e-folds per meter above the height, 0 is uniform
 * @param height World height the density is measured at
 * @param y0 World height of the camera
 * @param dy Height the ray climbs per meter of its length
 * @param t0 Distance the integral starts at
 * @param t1 Distance the integral ends at
 * @return The optical depth, which is never negative
 */
static inline float box_fog_depth(
    float density,
    float falloff,
    float height,
    float y0,
    float dy,
    float t0,
    float t1)
{
    float result = 0.0f;
    float len = t1 - t0;
    if (len <= 0.0f || density <= 0.0f) {
        goto done;
    }

    float exponent = -falloff * (y0 + dy * t0 - height);
    if (exponent > BOX_FOG_EXP_MAX) {
        exponent = BOX_FOG_EXP_MAX;
    }

    if (exponent < -BOX_FOG_EXP_MAX) {
        exponent = -BOX_FOG_EXP_MAX;
    }

    float base = density * expf(exponent);
    float k = falloff * dy;
    if (fabsf(k) < 0.00001f) {
        result = base * len;
        goto done;
    }

    float span = -k * len;
    if (span > BOX_FOG_EXP_MAX) {
        span = BOX_FOG_EXP_MAX;
    }

    if (span < -BOX_FOG_EXP_MAX) {
        span = -BOX_FOG_EXP_MAX;
    }

    result = base * (1.0f - expf(span)) / k;
done:
    return result;
}

/** Fraction of a pixel the fog covers.
 *
 * @param density Extinction per meter at the fog height
 * @param falloff Density e-folds per meter above the height
 * @param height World height the density is measured at
 * @param y0 World height of the camera
 * @param dy Height the ray climbs per meter of its length
 * @param t0 Distance the fog starts at
 * @param t1 Distance of the pixel
 * @param max_opacity Highest fraction the fog covers, 0 takes one
 * @return The fraction, 0 to max_opacity
 */
static inline float box_fog_factor(
    float density,
    float falloff,
    float height,
    float y0,
    float dy,
    float t0,
    float t1,
    float max_opacity)
{
    float optical = box_fog_depth(density, falloff, height, y0, dy, t0, t1);
    float limit = max_opacity > 0.0f ? max_opacity : 1.0f;
    float result = 1.0f - expf(-optical);

    return result < limit ? result : limit;
}

#endif
