/**
 * @file renderer/effect_temporal.h
 * @brief Size rule of the temporal setup pass, shared by the renderer and the tests.
 */

#ifndef BOX_RENDERER_EFFECT_TEMPORAL_H
#define BOX_RENDERER_EFFECT_TEMPORAL_H

#include <stdint.h>
#include <stdbool.h>

/** Edge of the temporal targets for one edge of the view.
 *
 * @param size Edge of the view in pixels
 * @param reduced The targets are rendered at half resolution
 * @return The edge in pixels, never 0
 */
static inline uint32_t box_effect_temporal_size(
    uint32_t size,
    bool reduced)
{
    uint32_t result = reduced ? size / 2 : size;

    return result ? result : 1u;
}

#endif
