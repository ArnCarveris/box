/**
 * @file renderer/upscale.h
 * @brief The pass that magnifies a scaled frame onto the image that is presented.
 *
 * With resolution_scale above one the views render into a smaller image that one
 * full screen pass magnifies with a linear sampler before presenting.
 */

#ifndef BOX_RENDERER_UPSCALE_H
#define BOX_RENDERER_UPSCALE_H

#include "gpu.h"
#include "packet.h"

/** The renderer, declared in renderer_internal.h */
typedef struct box_renderer_t box_renderer_t;

/** Set up the sampler, the layout and the pipeline of the magnifying pass.
 *
 * @param r The renderer
 */
void box_upscale_init(
    box_renderer_t *r);

/** Give the frame the scaled image its surface views draw into.
 *
 * @param r The renderer
 * @param p The packet
 * @return The image, NULL when the frame is drawn at the size of the window
 */
WGPUTexture box_upscale_begin(
    box_renderer_t *r,
    const box_packet_t *p);

/** Magnify the scaled image onto the image that is presented.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param dst The texture of the window
 */
void box_upscale_blit(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTexture dst);

/** Release the image, the pipeline and the sampler of the magnifying pass.
 *
 * @param r The renderer
 */
void box_upscale_fini(
    box_renderer_t *r);

#endif
