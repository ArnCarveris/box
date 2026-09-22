#ifndef BOX_RENDERER_EFFECT_MIP_H
#define BOX_RENDERER_EFFECT_MIP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define BOX_EFFECT_MIP_LEVELS_MAX (12)

static inline float box_effect_mip_clamp(float value, float low, float high)
{
    return isfinite(value) ? fminf(fmaxf(value, low), high) : low;
}

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
        return;
    }
    if (full_size) {
        *out_width = width / 2 + width % 2;
        *out_height = height / 2 + height % 2;
        return;
    }
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

static inline uint32_t box_effect_mip_count(uint32_t width, uint32_t height)
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
