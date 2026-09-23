/**
 * @file renderer/effect_mip.h
 * @brief Size rules of the mip chain setup pass, shared by the renderer and the tests.
 */

#ifndef BOX_RENDERER_EFFECT_MIP_H
#define BOX_RENDERER_EFFECT_MIP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/** Levels the pyramid of a mip chain setup pass holds at most */
#define BOX_EFFECT_MIP_LEVELS_MAX (12)

/** Clamp a value to a range, mapping a value that is not finite to the lower bound.
 *
 * @param value The value
 * @param low Lower bound
 * @param high Upper bound
 * @return The clamped value
 */
static inline float box_effect_mip_clamp(
    float value,
    float low,
    float high)
{
    return isfinite(value) ? fminf(fmaxf(value, low), high) : low;
}

/** Size of the finest level of the pyramid built from an image.
 *
 * @param width Width of the source image in pixels
 * @param height Height of the source image in pixels
 * @param max_dimension Longest edge of the finest level, clamped to 8 up to 65536
 * @param full_size The finest level is half the source rounded up, without the cap
 * @param out_width Receives the width, never 0
 * @param out_height Receives the height, never 0
 */
static inline void box_effect_mip_size(
    uint32_t width,
    uint32_t height,
    float max_dimension,
    bool full_size,
    uint32_t *out_width,
    uint32_t *out_height)
{
    if (!width || !height) {
        *out_width = *out_height = 1;
    } else if (full_size) {
        *out_width = width / 2 + width % 2;
        *out_height = height / 2 + height % 2;
    } else {
        uint32_t cap = (uint32_t)box_effect_mip_clamp(max_dimension, 8, 65536);
        uint32_t longer = width > height ? width : height;
        uint32_t target = longer / 2;
        if (target > cap) target = cap;
        if (!target) target = 1;
        *out_width = (uint32_t)(((uint64_t)width * target + longer / 2) / longer);
        *out_height = (uint32_t)(((uint64_t)height * target + longer / 2) / longer);
        if (!*out_width) *out_width = 1;
        if (!*out_height) *out_height = 1;
    }
}

/** Levels of the pyramid, halving until the shorter edge drops below 8 pixels.
 *
 * @param width Width of the finest level in pixels
 * @param height Height of the finest level in pixels
 * @return The level count, at least 1 and at most BOX_EFFECT_MIP_LEVELS_MAX
 */
static inline uint32_t box_effect_mip_count(
    uint32_t width,
    uint32_t height)
{
    uint32_t shorter = width < height ? width : height;
    uint32_t longer = width > height ? width : height;
    uint32_t count = shorter < 8 && longer > 1 ? 2 : 1;
    while (shorter >= 8 && count < BOX_EFFECT_MIP_LEVELS_MAX) {
        shorter >>= 1;
        count ++;
    }

    return count;
}

#endif
