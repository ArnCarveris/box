/**
 * @file renderer/gtao.h
 * @brief Horizon integral of ground truth ambient occlusion and its trace size.
 *
 * The trace walks a few slices of the hemisphere around a pixel and finds, on
 * each side of a slice, the direction of the highest occluder it can see. The
 * visibility of that slice is the cosine weighted integral between the two
 * horizons, clamped to the hemisphere of the normal, which is the closed form
 * of Jimenez et al. The same expression runs here and in the trace shader, so a
 * change here changes both sides.
 *
 * The angles are signed and measured from the view vector inside the plane of
 * the slice: the horizon on one side is negative, the horizon on the other
 * positive and the projected normal sits somewhere between them.
 */

#ifndef BOX_RENDERER_GTAO_H
#define BOX_RENDERER_GTAO_H

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

/** Quarter turn in radians, the widest a horizon reaches from the normal */
#define BOX_GTAO_HALF_PI (1.5707963268f)

/** Slices of the hemisphere the trace walks by default */
#define BOX_GTAO_SLICES (2)

/** Steps one side of a slice marches by default */
#define BOX_GTAO_STEPS (4)

/** World radius the horizon search covers by default */
#define BOX_GTAO_RADIUS (1.0f)

/** Exponent the occlusion is raised to by default */
#define BOX_GTAO_POWER (1.0f)

/** Weight the accumulated history keeps when a pixel is accepted */
#define BOX_GTAO_HISTORY (0.9f)

/** Clamp a horizon angle into the hemisphere of the normal.
 *
 * @param angle The horizon angle, signed and measured from the view vector
 * @param normal Angle of the projected normal in the same frame
 * @return The angle, never further than a quarter turn from the normal
 */
static inline float box_gtao_clamp(
    float angle,
    float normal)
{
    float lo = normal - BOX_GTAO_HALF_PI;
    float hi = normal + BOX_GTAO_HALF_PI;
    float result = angle < lo ? lo : angle;
    if (result > hi) {
        result = hi;
    }

    return result;
}

/** Visibility of one slice between its two horizons.
 *
 * @param normal Angle of the projected normal, measured from the view vector
 * @param h1 Horizon angle on the negative side of the slice
 * @param h2 Horizon angle on the positive side of the slice
 * @param proj_len Length of the normal projected into the plane of the slice
 * @return The visible fraction of the slice, one when nothing occludes it
 */
static inline float box_gtao_slice(
    float normal,
    float h1,
    float h2,
    float proj_len)
{
    float a = box_gtao_clamp(h1, normal);
    float b = box_gtao_clamp(h2, normal);
    float cn = cosf(normal);
    float sn = sinf(normal);
    float first = -cosf(2.0f * a - normal) + cn + 2.0f * a * sn;
    float second = -cosf(2.0f * b - normal) + cn + 2.0f * b * sn;

    return proj_len * 0.25f * (first + second);
}

/** Factor the occlusion pass multiplies the color of a pixel by.
 *
 * The chain runs after the lighting, so the pass cannot reach the ambient term
 * of a pixel and scales the fraction of it that came from the environment and
 * the probes instead. The same expression runs in gtao.wgsl.
 *
 * @param indirect Indirect diffuse fraction the frame recorded for the pixel
 * @param ao Visibility of the pixel, one where nothing occludes it
 * @return The factor, one where nothing is occluded or nothing is indirect
 */
static inline float box_gtao_ambient(
    float indirect,
    float ao)
{
    float f = indirect < 0.0f ? 0.0f : (indirect > 1.0f ? 1.0f : indirect);
    float v = ao < 0.0f ? 0.0f : (ao > 1.0f ? 1.0f : ao);

    return 1.0f - f * (1.0f - v);
}

/** Indirect fraction a pixel keeps after a sorted draw blended over it.
 *
 * A pixel a transparent draw covered is no longer the surface the mesh pass
 * shaded: only what is left of that surface may be darkened, so the sorted pass
 * folds the coverage of its fragment into the fraction the frame recorded. The
 * blend state of the normal attachment runs the same expression.
 *
 * @param indirect The fraction the mesh pass recorded
 * @param coverage Alpha the sorted draw resolved for its fragment
 * @return The fraction left of the surface behind the draw
 */
static inline float box_gtao_covered(
    float indirect,
    float coverage)
{
    float c = coverage < 0.0f ? 0.0f : (coverage > 1.0f ? 1.0f : coverage);

    return indirect * (1.0f - c);
}

/** Extent of the occlusion textures of a view on one axis.
 *
 * @param size Extent of the view in pixels
 * @param half The trace runs at half resolution
 * @return The extent of the trace, at least one
 */
static inline uint32_t box_gtao_size(
    uint32_t size,
    bool half)
{
    uint32_t result = size;
    if (half) {
        result = size / 2;
    }

    return result ? result : 1u;
}

#endif
