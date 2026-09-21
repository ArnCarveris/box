#ifndef BOX_RENDERER_BLOOM_H
#define BOX_RENDERER_BLOOM_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define BOX_BLOOM_LEVELS_MAX (12)

static inline float box_bloom_clamp(float value, float low, float high)
{
    return isfinite(value) ? fminf(fmaxf(value, low), high) : low;
}

static inline void box_bloom_size(
    uint32_t width,
    uint32_t height,
    float max_dimension,
    bool full_res,
    uint32_t *out_width,
    uint32_t *out_height)
{
    if (!width || !height) {
        *out_width = *out_height = 1;
        return;
    }
    if (full_res) {
        *out_width = width / 2 + width % 2;
        *out_height = height / 2 + height % 2;
        return;
    }
    uint32_t cap = (uint32_t)box_bloom_clamp(max_dimension, 8, 65536);
    uint32_t longer = width > height ? width : height;
    uint32_t target = longer / 2;
    if (target > cap) target = cap;
    if (!target) target = 1;
    *out_width = (uint32_t)(((uint64_t)width * target + longer / 2) / longer);
    *out_height = (uint32_t)(((uint64_t)height * target + longer / 2) / longer);
    if (!*out_width) *out_width = 1;
    if (!*out_height) *out_height = 1;
}

static inline uint32_t box_bloom_chain(uint32_t width, uint32_t height)
{
    uint32_t shorter = width < height ? width : height;
    uint32_t longer = width > height ? width : height;
    uint32_t count = shorter < 8 && longer > 1 ? 2 : 1;
    while (shorter >= 8 && count < BOX_BLOOM_LEVELS_MAX) {
        shorter >>= 1;
        count ++;
    }
    return count;
}

static inline float box_bloom_blend(const float *params, uint32_t mip, uint32_t levels)
{
    float intensity = isfinite(params[0]) ? fmaxf(params[0], 0) : 0;
    if (levels < 2) return intensity;
    float normalized = (float)mip / (float)(levels - 1);
    float curvature = box_bloom_clamp(params[2], 0, 0.9999f);
    float boost = (1 - powf(1 - normalized, 1 / (1 - curvature))) *
        box_bloom_clamp(params[1], 0, 1) * (1 - intensity);
    float frequency = box_bloom_clamp(params[3], 0.0001f, 1);
    float high_pass = 1 - box_bloom_clamp((normalized - frequency) / frequency, 0, 1);
    return (intensity + boost) * high_pass;
}

static inline float box_bloom_karis(float luma)
{
    return 1 / (1 + fmaxf(luma, 0) / 4);
}

#endif
