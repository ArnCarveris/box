/**
 * @file renderer/blur.h
 * @brief Levels of the blurred background copy and the level a roughness reads.
 *
 * The dual filter chain of the copy stops while its coarsest level is still
 * wide enough to magnify without creases. The same mapping runs in the mesh
 * fragment shader, so a change here changes both sides.
 */

#ifndef BOX_RENDERER_BLUR_H
#define BOX_RENDERER_BLUR_H

#include <math.h>
#include <stdint.h>

/** Texels the shorter axis of the coarsest level of the chain keeps */
#define BOX_BLUR_MIN_SIZE (32)

/** Levels a chain can hold, enough for the shortest axis of an 8192 pixel target */
#define BOX_BLUR_LEVELS_MAX (10)

/** Levels the blur chain of a target holds.
 *
 * @param width Target width in pixels
 * @param height Target height in pixels
 * @return The level count, at least one
 */
static inline uint32_t box_blur_levels(
    uint32_t width,
    uint32_t height)
{
    uint32_t size = width < height ? width : height;
    uint32_t result = 1;
    while (size >= BOX_BLUR_MIN_SIZE * 2 && result < BOX_BLUR_LEVELS_MAX) {
        size >>= 1;
        result ++;
    }

    return result;
}

/** Level of the chain a roughness reads.
 *
 * The level follows the square root of the roughness, so a quarter of roughness
 * lands halfway up the chain, since the blur radius doubles with every level.
 *
 * @param roughness Material roughness, clamped to 0 .. 1
 * @param levels Levels the chain holds
 * @return The level, a fraction between two levels of the chain
 */
static inline float box_blur_level(
    float roughness,
    uint32_t levels)
{
    float clamped = roughness;
    if (clamped < 0.0f) {
        clamped = 0.0f;
    }

    if (clamped > 1.0f) {
        clamped = 1.0f;
    }

    return sqrtf(clamped) * (float)(levels - 1);
}

#endif
