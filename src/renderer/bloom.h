/**
 * @file renderer/bloom.h
 * @brief Levels of the bloom pyramid and the weight its first downsample uses.
 *
 * The pyramid starts at half the size of the image and halves down while its
 * coarsest level stays wide enough to magnify. The radius of the component
 * picks how many of those levels feed back, which is the width of the kernel.
 */

#ifndef BOX_RENDERER_BLOOM_H
#define BOX_RENDERER_BLOOM_H

#include <stdint.h>

/** Texels the shorter axis of the coarsest level of the pyramid keeps */
#define BOX_BLOOM_MIN_SIZE (8)

/** Levels a pyramid can hold */
#define BOX_BLOOM_LEVELS_MAX (8)

/** Levels the pyramid of an image holds at its widest.
 *
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return The level count, at least one
 */
static inline uint32_t box_bloom_chain(
    uint32_t width,
    uint32_t height)
{
    uint32_t size = (width < height ? width : height) / 2;
    uint32_t result = 1;
    while (size >= BOX_BLOOM_MIN_SIZE * 2 && result < BOX_BLOOM_LEVELS_MAX) {
        size >>= 1;
        result ++;
    }

    return result;
}

/** Levels of that pyramid a radius lets feed back.
 *
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param radius Radius of the component, 0 to 1
 * @return The level count, at least one and never past the chain
 */
static inline uint32_t box_bloom_levels(
    uint32_t width,
    uint32_t height,
    float radius)
{
    uint32_t chain = box_bloom_chain(width, height);
    float clamped = radius;
    if (clamped < 0.0f) {
        clamped = 0.0f;
    }

    if (clamped > 1.0f) {
        clamped = 1.0f;
    }

    uint32_t result = 1 + (uint32_t)(clamped * (float)(chain - 1) + 0.5f);

    return result > chain ? chain : result;
}

/** Weight the Karis average gives a group of taps of the first downsample.
 *
 * The weight falls with the brightness of the group, so one very bright texel
 * cannot carry a whole level and flicker between frames.
 *
 * @param luma Linear luminance of the group
 * @return The weight, at most one
 */
static inline float box_bloom_karis(
    float luma)
{
    float lit = luma > 0.0f ? luma : 0.0f;

    return 1.0f / (1.0f + lit);
}

#endif
