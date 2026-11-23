/**
 * @file render/gpu_forward.h
 * @brief Forward pipeline functions shared by gpu_forward.c and gpu_draw.c.
 */

#ifndef BOX_RENDER_GPU_FORWARD_H
#define BOX_RENDER_GPU_FORWARD_H

#include "gpu_environment.h"

#ifdef BOX_RENDER_WGPU

/** Check whether a batch draws in the depth prepass of a lit view: an opaque mesh of a mesh
 * shader, plain or alpha tested. Its forward pipeline in such a view tests with Equal.
 *
 * @param b The batch
 * @return Whether the prepass draws it
 */
bool box_gpu_prepass_draws(
    const box_render_batch_t *b);

/** Sweeps a depth only pass draws its batches in (box_gpu_depth_sweep) */
#define BOX_GPU_DEPTH_SWEEPS (3)

/** Get the sweep of a depth only pass that draws a batch: 0 the plain meshes, 1 the alpha
 * tested ones, 2 the impostor cards. A tiled GPU shades every fragment of an alpha tested
 * draw that passes the depth of the draws before it, so the plain ones go first and the cards,
 * the farthest, last.
 *
 * @param b The batch
 * @return The sweep, below BOX_GPU_DEPTH_SWEEPS
 */
int32_t box_gpu_depth_sweep(
    const box_render_batch_t *b);

/** Get the depth prepass pipeline of a batch the prepass draws, created on first use in the
 * forward cache from the mesh shader of the batch, so a recompiled or released mesh shader
 * empties it too.
 *
 * @param gpu The backend
 * @param b The batch, box_gpu_prepass_draws
 * @param count Format of the overdraw count target, Undefined for a depth only prepass
 * @return The pipeline, NULL without a mesh shader
 */
WGPURenderPipeline box_gpu_prepass_pipeline(
    box_gpu_t *gpu,
    const box_render_batch_t *b,
    WGPUTextureFormat count);

/** Request the pipelines of the extended mesh shader a view of a scene with material features
 * draws with, so they compile before the first shape needs them; other blend modes than
 * AlphaBlend compile on first use.
 *
 * Those are the opaque, alpha tested and TextureBlend ones of both back face modes with the
 * depth states of the view, the alpha blended and the transmissive ones, each when enabled.
 *
 * @param gpu The backend
 * @param format The format of the color target of the view
 * @param v The view
 * @param prepass Whether the view draws a depth prepass, so the opaque draws test Equal
 */
void box_gpu_draw_prewarm(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    bool prepass);

#endif

#endif
