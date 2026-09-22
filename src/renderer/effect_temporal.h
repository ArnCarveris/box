#ifndef BOX_RENDERER_EFFECT_TEMPORAL_H
#define BOX_RENDERER_EFFECT_TEMPORAL_H

#include <stdint.h>
#include <stdbool.h>

static inline uint32_t box_effect_temporal_size(
    uint32_t size,
    bool reduced)
{
    uint32_t result = reduced ? size / 2 : size;
    return result ? result : 1u;
}

#endif
