/**
 * @file renderer/lut.h
 * @brief Display transform lookup cube of the tone map pass.
 *
 * The cube is a 3D texture of RGB9E5 texels a pass samples instead of running a
 * curve. It arrives as one resource command the first time a pass selects the
 * operator, so a frame that never asked for one holds a single texel dummy.
 */

#ifndef BOX_RENDERER_LUT_H
#define BOX_RENDERER_LUT_H

#include "gpu.h"
#include "renderer.h"
#include "packet.h"

/** Create the one texel lookup cube a pass without one binds.
 *
 * @param r The renderer
 */
void box_lut_init(
    box_renderer_t *r);

/** Upload the lookup cubes a packet carries.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_lut_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** View of the lookup cube a pass reads, the dummy until one was uploaded.
 *
 * @param r The renderer
 * @return The view
 */
WGPUTextureView box_lut_view(
    box_renderer_t *r);

/** Release the lookup cube and its dummy.
 *
 * @param r The renderer
 */
void box_lut_fini(
    box_renderer_t *r);

#endif
