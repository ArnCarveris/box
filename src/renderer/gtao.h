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
