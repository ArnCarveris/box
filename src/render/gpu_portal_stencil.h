/**
 * @file render/gpu_portal_stencil.h
 * @brief Portal marks of the webgpu backend: the stencil of a view gets the region of the
 * innermost portal entry of each pixel, so its water draws only where that entry is outdoors.
 *
 * Each mark (box_render_portal_mark_t) draws its clipped portal three times, as the
 * portal-room demo of WebGPU-Entities does:
 * - A: where the opaque depth is not in front of it and the stencil holds the region in front
 *   of it, invert BOX_RENDER_STENCIL_MARK.
 * - B: where the mark bit is set, replace the region bits with the region behind it.
 * - C: where the mark bit is set, clear it.
 */

#ifndef BOX_RENDER_GPU_PORTAL_STENCIL_H
#define BOX_RENDER_GPU_PORTAL_STENCIL_H

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

#ifdef __cplusplus
extern "C" {
#endif

/** Create the shader and pipeline layout of the portal marks.
 *
 * @param gpu The backend, whose view layout exists
 */
void box_gpu_portal_stencil_init(
    box_gpu_t *gpu);

/** Release the portal marks.
 *
 * @param gpu The backend
 */
void box_gpu_portal_stencil_fini(
    box_gpu_t *gpu);

/** Upload the vertices of the portal marks of a packet, before its views draw.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_portal_stencil_upload(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Draw the portal marks of a view into the stencil of its pass, after its opaque draws and
 * before its water. Replaces the pipeline and the vertex buffer at 0, leaves the stencil
 * reference at 0.
 *
 * @param gpu The backend
 * @param pass The render pass of the view, with the bind group of the view at 0
 * @param format The format of the color target of the pass
 * @param packet The packet
 * @param v The view
 * @return Whether a mark was drawn
 */
bool box_gpu_portal_stencil_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v);

#ifdef __cplusplus
}
#endif

#endif

#endif
