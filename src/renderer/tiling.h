/**
 * @file renderer/tiling.h
 * @brief World space texture tiling math, shared by extract, the tests and the shaders.
 *
 * C twin of the fragment shader uv math: a base uv from the projection of the local
 * position in world meters, times scale plus offset, folded by the wrap mode.
 *
 * The instance scale is tile count / span, where span is the world length of one
 * base uv unit; for MESH the base uv is normalized and the scale is the tile count.
 */

#ifndef BOX_RENDERER_TILING_H
#define BOX_RENDERER_TILING_H

#include <math.h>
#include "packet.h"

/** Ratio of a circle's circumference to its diameter */
#define BOX_TILING_PI (3.14159265358979323846f)

/** Smallest tile count and span the tiling math accepts */
#define BOX_TILING_EPSILON (1e-6f)

/** Base uv of the projection is the mesh uv, used by TilingMeters and by no tiling */
#define BOX_TILE_MESH (0)

/** Base uv of the projection is planar per face, used by TilingBox */
#define BOX_TILE_BOX (1)

/** Base uv of the projection is arc length and height, used by TilingCylinder */
#define BOX_TILE_CYLINDER (2)

/** Base uv of the projection is radius and arc length, used by TilingRadial */
#define BOX_TILE_RADIAL (3)

/** Uv outside 0..1 tiles the texture */
#define BOX_WRAP_REPEAT (0)

/** Uv outside 0..1 holds the edge texel */
#define BOX_WRAP_CLAMP (1)

/** Uv outside 0..1 mirrors every other tile */
#define BOX_WRAP_MIRROR (2)

/** Pack a projection and a wrap mode into the mode word of an instance.
 *
 * @param projection One of the BOX_TILE constants
 * @param wrap One of the BOX_WRAP constants
 * @return The mode word
 */
static inline uint32_t box_tiling_mode(
    uint32_t projection,
    uint32_t wrap)
{
    return (projection & 0xFu) | ((wrap & 0xFu) << 4);
}

/** Projection a mode word holds.
 *
 * @param mode The mode word
 * @return One of the BOX_TILE constants
 */
static inline uint32_t box_tiling_projection(
    uint32_t mode)
{
    return mode & 0xFu;
}

/** Wrap mode a mode word holds.
 *
 * @param mode The mode word
 * @return One of the BOX_WRAP constants
 */
static inline uint32_t box_tiling_wrap(
    uint32_t mode)
{
    return (mode >> 4) & 0xFu;
}

/** World length one unit of base uv covers on each axis of a projection.
 *
 * @param projection One of the BOX_TILE constants
 * @param size World size of the instance, three floats
 * @param out Two floats receiving the u and v span in meters
 */
static inline void box_tiling_span(
    uint32_t projection,
    const float *size,
    float *out)
{
    out[0] = size[0];
    out[1] = size[1];
    if (projection == BOX_TILE_CYLINDER) {
        out[0] = BOX_TILING_PI * size[0];
    } else if (projection == BOX_TILE_RADIAL) {
        out[0] = size[0] * 0.5f;
        out[1] = BOX_TILING_PI * size[0];
    }
}

/** Tiles one axis holds at the requested tile size.
 *
 * The count is span / meters. Fit rounds it to the nearest whole tile unless
 * allow_partial is set; min_tiles above zero bounds the result from below.
 *
 * @param span World length of the axis in meters
 * @param meters World meters one tile spans
 * @param fit Round the count to a whole number of tiles
 * @param allow_partial Keep the fractional count fit would round
 * @param min_tiles Lower bound of the count, 0 for none
 * @return The tile count, never below BOX_TILING_EPSILON
 */
static inline float box_tiling_count(
    float span,
    float meters,
    bool fit,
    bool allow_partial,
    int32_t min_tiles)
{
    float size = meters > BOX_TILING_EPSILON ? meters : 1.0f;
    float result = span / size;
    if (fit && !allow_partial) {
        result = floorf(result + 0.5f);
        if (result < 1.0f) {
            result = 1.0f;
        }
    }

    if (min_tiles > 0 && result < (float)min_tiles) {
        result = (float)min_tiles;
    }

    if (result < BOX_TILING_EPSILON) {
        result = BOX_TILING_EPSILON;
    }

    return result;
}

/** Uv scale one axis needs to show a tile count over its span.
 *
 * @param projection One of the BOX_TILE constants
 * @param span World length of the axis in meters
 * @param tiles The tile count
 * @return The multiplier of the base uv
 */
static inline float box_tiling_scale(
    uint32_t projection,
    float span,
    float tiles)
{
    float result = tiles;
    if (projection != BOX_TILE_MESH) {
        result = tiles / (span > BOX_TILING_EPSILON ? span : 1.0f);
    }

    return result;
}

/** Build the uv transform of an instance from its world size and tiling.
 *
 * @param projection One of the BOX_TILE constants
 * @param size World size of the instance, three floats
 * @param meters World meters one tile spans on u and v, two floats
 * @param fit Round the tile count to a whole number of tiles
 * @param allow_partial Keep the fractional count fit would round
 * @param min_tiles Lower bound of the tile count, 0 for none
 * @param base Scale x, scale y, offset x and offset y of the TextureTransform
 * @param out Four floats receiving the scale and offset of the instance
 */
static inline void box_tiling_transform(
    uint32_t projection,
    const float *size,
    const float *meters,
    bool fit,
    bool allow_partial,
    int32_t min_tiles,
    const float *base,
    float *out)
{
    float span[2];
    box_tiling_span(projection, size, span);

    int32_t axis;
    for (axis = 0; axis < 2; axis ++) {
        float tiles = box_tiling_count(span[axis], meters[axis], fit, allow_partial, min_tiles);
        out[axis] = box_tiling_scale(projection, span[axis], tiles) * base[axis];
        out[axis + 2] = base[axis + 2];
    }
}

/** Base uv a projection derives from a local position and normal.
 *
 * @param projection One of the BOX_TILE constants
 * @param local Local position in world meters, three floats
 * @param normal Local normal, three floats
 * @param mesh_uv The uv of the mesh, two floats
 * @param out Two floats receiving the base uv
 */
static inline void box_tiling_project(
    uint32_t projection,
    const float *local,
    const float *normal,
    const float *mesh_uv,
    float *out)
{
    float ax = fabsf(normal[0]);
    float ay = fabsf(normal[1]);
    float az = fabsf(normal[2]);
    float radius = sqrtf(local[0] * local[0] + local[2] * local[2]);
    float angle = atan2f(local[2], local[0]);

    if (projection == BOX_TILE_CYLINDER) {
        out[0] = angle * radius;
        out[1] = local[1];
    } else if (projection == BOX_TILE_RADIAL) {
        out[0] = radius;
        out[1] = angle * radius;
    } else if (projection != BOX_TILE_BOX) {
        out[0] = mesh_uv[0];
        out[1] = mesh_uv[1];
    } else if (ax >= ay && ax >= az) {
        out[0] = normal[0] < 0.0f ? -local[2] : local[2];
        out[1] = local[1];
    } else if (ay >= az) {
        out[0] = local[0];
        out[1] = normal[1] < 0.0f ? -local[2] : local[2];
    } else {
        out[0] = normal[2] < 0.0f ? local[0] : -local[0];
        out[1] = local[1];
    }
}

/** Fold a uv into 0..1 the way a wrap mode does.
 *
 * Repeat leaves the uv to the sampler, clamp holds the range and mirror folds
 * it with the triangle wave 1 - |(u - 2 floor(u / 2)) - 1|.
 *
 * @param wrap One of the BOX_WRAP constants
 * @param value The uv coordinate
 * @return The folded coordinate
 */
static inline float box_tiling_fold(
    uint32_t wrap,
    float value)
{
    float result = value;
    if (wrap == BOX_WRAP_CLAMP) {
        result = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    } else if (wrap == BOX_WRAP_MIRROR) {
        float t = value - 2.0f * floorf(value * 0.5f);

        result = 1.0f - fabsf(t - 1.0f);
    }

    return result;
}

/** Uv a fragment samples with, the twin of the tile_uv and wrap_uv shader pair.
 *
 * @param mode The mode word of the instance
 * @param xform Scale x, scale y, offset x and offset y of the instance
 * @param local Local position in world meters, three floats
 * @param normal Local normal, three floats
 * @param mesh_uv The uv of the mesh, two floats
 * @param out Two floats receiving the sampled uv
 */
static inline void box_tiling_uv(
    uint32_t mode,
    const float *xform,
    const float *local,
    const float *normal,
    const float *mesh_uv,
    float *out)
{
    float base[2];
    box_tiling_project(box_tiling_projection(mode), local, normal, mesh_uv, base);

    uint32_t wrap = box_tiling_wrap(mode);
    out[0] = box_tiling_fold(wrap, base[0] * xform[0] + xform[2]);
    out[1] = box_tiling_fold(wrap, base[1] * xform[1] + xform[3]);
}

#endif
