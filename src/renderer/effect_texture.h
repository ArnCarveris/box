/**
 * @file renderer/effect_texture.h
 * @brief Lookup cubes a post process pass can sample, uploaded from packet commands.
 */

#ifndef BOX_RENDERER_EFFECT_TEXTURE_H
#define BOX_RENDERER_EFFECT_TEXTURE_H

#include "gpu.h"
#include "renderer.h"
#include "packet.h"

/** Create the one texel cube a pass without a lookup cube samples.
 *
 * @param r The renderer
 */
void box_effect_texture_init(
    box_renderer_t *r);

/** Upload the lookup cubes a packet creates and free their texel arrays.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_effect_texture_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** View of a lookup cube.
 *
 * @param r The renderer
 * @param handle Handle of the lookup cube
 * @return The 3D view, the dummy cube when the handle holds no cube
 */
WGPUTextureView box_effect_texture_view(
    box_renderer_t *r,
    uint32_t handle);

/** Release every lookup cube and the dummy cube.
 *
 * @param r The renderer
 */
void box_effect_texture_fini(
    box_renderer_t *r);

#endif
