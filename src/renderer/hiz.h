/**
 * @file renderer/hiz.h
 * @brief Depth pyramid rule, screen bounds of a sphere and the occlusion test.
 *
 * With reverse Z a pyramid texel keeps the minimum (farthest) depth it covers,
 * and an instance is occluded when its nearest depth is farther than that across
 * its screen rectangle. The pyramid and culling shaders mirror this rule.
 */

#ifndef BOX_RENDERER_HIZ_H
#define BOX_RENDERER_HIZ_H

#include <stdint.h>
#include <stdbool.h>

/** Levels a depth pyramid can hold, enough for the longest axis of a 8192 pixel view */
#define BOX_HIZ_LEVELS_MAX (14)

/** Extent of the first level of the pyramid of a view, half of the view itself.
 *
 * Halving the view costs the test one texel of conservatism and saves the
 * widest pass of the chain, which is the one that reads the depth target.
 *
 * @param size Extent of the view in pixels
 * @return The extent of the first level, at least one
 */
static inline uint32_t box_hiz_half(
    uint32_t size)
{
    return size > 2 ? size / 2 : 1;
}

/** Screen rectangle and nearest depth of the bounds of one instance */
typedef struct box_hiz_rect_t {
    float min[2];  /**< Minimum corner in uv, x right and y down */
    float max[2];  /**< Maximum corner in uv, x right and y down */
    float depth;   /**< Depth of the point of the bounds nearest to the camera */
    bool clamped;  /**< The bounds cross the near plane and the rectangle is the whole view */
} box_hiz_rect_t;

/** A depth pyramid held in memory, which the tests build by hand */
typedef struct box_hiz_pyramid_t {
    const float *levels[BOX_HIZ_LEVELS_MAX]; /**< Depth values of every level, row by row */
    uint32_t width;   /**< Width of the first level in texels */
    uint32_t height;  /**< Height of the first level in texels */
    uint32_t count;   /**< Levels the pyramid holds */
} box_hiz_pyramid_t;

/** Extent of one level of a pyramid, the mip size rule of WebGPU.
 *
 * @param size Extent of the first level in texels
 * @param level The level
 * @return The extent of that level, at least one
 */
static inline uint32_t box_hiz_extent(
    uint32_t size,
    uint32_t level)
{
    uint32_t result = size >> level;

    return result ? result : 1u;
}

/** Levels the pyramid of a view holds.
 *
 * @param width View width in pixels
 * @param height View height in pixels
 * @param limit Levels the view asks for, 0 for the whole chain
 * @return The level count, at least one
 */
static inline uint32_t box_hiz_levels(
    uint32_t width,
    uint32_t height,
    uint32_t limit)
{
    uint32_t size = width > height ? width : height;
    uint32_t result = 1;
    while (size > 1 && result < BOX_HIZ_LEVELS_MAX) {
        size >>= 1;
        result ++;
    }

    if (limit && limit < result) {
        result = limit;
    }

    return result;
}

/** Value one texel of the next level of a pyramid takes from the level below it.
 *
 * A texel covers the two by two block under it, plus the trailing row or column
 * when that axis of the source is odd.
 *
 * @param src Depth values of the source level, row by row
 * @param width Width of the source level in texels
 * @param height Height of the source level in texels
 * @param x Column of the texel in the level above
 * @param y Row of the texel in the level above
 * @return The farthest depth of the covered block
 */
static inline float box_hiz_reduce(
    const float *src,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y)
{
    uint32_t span_x = 2 + (width & 1);
    uint32_t span_y = 2 + (height & 1);
    float result = src[0];
    uint32_t row;
    for (row = 0; row < span_y; row ++) {
        uint32_t sy = y * 2 + row;
        uint32_t col;
        if (sy >= height) {
            sy = height - 1;
        }

        for (col = 0; col < span_x; col ++) {
            uint32_t sx = x * 2 + col;
            if (sx >= width) {
                sx = width - 1;
            }

            float value = src[sy * width + sx];
            if (!row && !col) {
                result = value;
            } else if (value < result) {
                result = value;
            }
        }
    }

    return result;
}

/** Project the bounding sphere of an instance to a screen rectangle.
 *
 * The rectangle bounds the projected corners of the box around the sphere. A
 * sphere that crosses the near plane takes the whole view and near plane depth.
 *
 * @param clip World to clip matrix, sixteen floats in column major order
 * @param center Center of the sphere, three floats
 * @param radius Radius of the sphere
 * @param out The rectangle to fill
 * @return True when the rectangle bounds the sphere, false when it is the whole view
 */
static inline bool box_hiz_project(
    const float *clip,
    const float *center,
    float radius,
    box_hiz_rect_t *out)
{
    float min_x = 1.0f;
    float min_y = 1.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float depth = 0.0f;
    bool crossed = false;
    int corner;
    for (corner = 0; corner < 8; corner ++) {
        float p[3];
        int axis;
        for (axis = 0; axis < 3; axis ++) {
            p[axis] = center[axis] + ((corner >> axis) & 1 ? radius : -radius);
        }

        float w = clip[3] * p[0] + clip[7] * p[1] + clip[11] * p[2] + clip[15];
        if (w <= 1e-6f) {
            crossed = true;
            continue;
        }

        float x = (clip[0] * p[0] + clip[4] * p[1] + clip[8] * p[2] + clip[12]) / w;
        float y = (clip[1] * p[0] + clip[5] * p[1] + clip[9] * p[2] + clip[13]) / w;
        float z = (clip[2] * p[0] + clip[6] * p[1] + clip[10] * p[2] + clip[14]) / w;
        float u = x * 0.5f + 0.5f;
        float v = 0.5f - y * 0.5f;
        min_x = u < min_x ? u : min_x;
        min_y = v < min_y ? v : min_y;
        max_x = u > max_x ? u : max_x;
        max_y = v > max_y ? v : max_y;
        depth = z > depth ? z : depth;
    }

    if (crossed) {
        min_x = 0.0f;
        min_y = 0.0f;
        max_x = 1.0f;
        max_y = 1.0f;
        depth = 1.0f;
    }

    out->min[0] = min_x < 0.0f ? 0.0f : min_x;
    out->min[1] = min_y < 0.0f ? 0.0f : min_y;
    out->max[0] = max_x > 1.0f ? 1.0f : max_x;
    out->max[1] = max_y > 1.0f ? 1.0f : max_y;
    out->depth = depth > 1.0f ? 1.0f : depth;
    out->clamped = crossed;

    return !crossed;
}

/** Level of a pyramid a screen rectangle is tested against.
 *
 * The level is the first one whose texels are as wide as the rectangle, so the
 * rectangle touches at most two texels on each axis wherever it happens to sit
 * and its four corners cover every texel it reaches.
 *
 * @param rect The screen rectangle
 * @param width Width of the first level in texels
 * @param height Height of the first level in texels
 * @param levels Levels the pyramid holds
 * @return The level, below the level count
 */
static inline uint32_t box_hiz_mip(
    const box_hiz_rect_t *rect,
    uint32_t width,
    uint32_t height,
    uint32_t levels)
{
    float span_x = (rect->max[0] - rect->min[0]) * (float)width;
    float span_y = (rect->max[1] - rect->min[1]) * (float)height;
    float span = span_x > span_y ? span_x : span_y;
    uint32_t result = 0;
    while (span > 1.0f && result + 1 < levels) {
        span *= 0.5f;
        result ++;
    }

    return result;
}

/** Test the screen rectangle of an instance against a depth pyramid.
 *
 * @param p The pyramid
 * @param rect The screen rectangle and nearest depth of the instance
 * @return True when the instance may be visible, false when it is hidden
 */
static inline bool box_hiz_visible(
    const box_hiz_pyramid_t *p,
    const box_hiz_rect_t *rect)
{
    bool result = true;
    if (rect->clamped) {
        goto done;
    }

    uint32_t level = box_hiz_mip(rect, p->width, p->height, p->count);
    uint32_t level_w = box_hiz_extent(p->width, level);
    uint32_t level_h = box_hiz_extent(p->height, level);
    uint32_t x0 = (uint32_t)(rect->min[0] * (float)level_w);
    uint32_t x1 = (uint32_t)(rect->max[0] * (float)level_w);
    uint32_t y0 = (uint32_t)(rect->min[1] * (float)level_h);
    uint32_t y1 = (uint32_t)(rect->max[1] * (float)level_h);
    x0 = x0 < level_w ? x0 : level_w - 1;
    x1 = x1 < level_w ? x1 : level_w - 1;
    y0 = y0 < level_h ? y0 : level_h - 1;
    y1 = y1 < level_h ? y1 : level_h - 1;

    const float *values = p->levels[level];
    float farthest = values[y0 * level_w + x0];
    float other = values[y0 * level_w + x1];
    farthest = other < farthest ? other : farthest;
    other = values[y1 * level_w + x0];
    farthest = other < farthest ? other : farthest;
    other = values[y1 * level_w + x1];
    farthest = other < farthest ? other : farthest;
    result = rect->depth >= farthest;
done:
    return result;
}

#endif
