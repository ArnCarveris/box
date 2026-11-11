/**
 * @file render/gpu_planar.h
 * @brief Planar reflections of the webgpu backend (gpu_planar.c).
 *
 * Included by gpu.h. A lit view renders the scene mirrored about each of its reflection planes
 * into an HDR image of its own, before its forward pass.
 */

#ifndef BOX_RENDER_GPU_PLANAR_H
#define BOX_RENDER_GPU_PLANAR_H

/** Get the forward pipeline of box_gpu_forward_pipeline, or its variant whose front faces wind
 * clockwise, for a view that a reflection mirrors.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @param b The batch
 * @param equal Whether the pipeline draws after a depth prepass, see box_gpu_forward_pipeline
 * @param mirrored Whether the view is mirrored, so the front faces of the image wind clockwise
 * @return The pipeline, NULL as box_gpu_forward_pipeline
 */
WGPURenderPipeline box_gpu_forward_pipeline_mirrored(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_batch_t *b,
    bool equal,
    bool mirrored);

/** Record the sky of a view into the pass of one of its planar reflections, as
 * box_gpu_atmosphere_draw_sky does, the directions of the pixels from the mirrored camera.
 *
 * The uniforms of the reflection are a copy of those of the view with its clip to world
 * matrix, in a buffer of the view per reflection slot. Replaces the bind group at 0.
 *
 * @param gpu The backend
 * @param t The target
 * @param pass The render pass of the reflection
 * @param format The format of the color target
 * @param v The view
 * @param mirror Slot of the reflection in the view, below BOX_PLANAR_REFLECTION_MAX
 * @param inv_view_proj Clip to world space of the reflection
 * @return Whether the sky was drawn
 */
bool box_gpu_atmosphere_draw_sky_mirrored(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    int32_t mirror,
    mat4 inv_view_proj);

/** Create the state of the planar reflections of the views.
 *
 * @param gpu The backend
 */
void box_gpu_planar_init(
    box_gpu_t *gpu);

/** Release the images of the planar reflections.
 *
 * @param gpu The backend
 */
void box_gpu_planar_fini(
    box_gpu_t *gpu);

/** Forget the reflections of the previous target, before the views of a target are written.
 *
 * @param gpu The backend
 */
void box_gpu_planar_begin(
    box_gpu_t *gpu);

/** Write the uniforms and add the cull views of the planar reflections of a view of a target.
 *
 * Each reflection takes two slots of the view buffer after first. Its meshes draw with the
 * first: the mirrored camera and oblique projection with the view matrix of the view, which
 * finds the shadow cascades and light clusters of the view.
 *
 * Its background and particles draw with the second, whose view matrix is the mirrored one.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param index Index of the view in the packet
 * @param slot Slot of the view in the view buffer
 * @param u Uniforms of the view
 * @param first First free slot of the view buffer
 * @return Slots the reflections took
 */
int32_t box_gpu_planar_add(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    const box_gpu_view_uniforms_t *u,
    int32_t first);

/** Write the uniforms of the reflections added for a target into the view data, which must
 * hold their slots, and the planar fields of their views: the share of the reflection array
 * each image covers and the instance of the shape of each plane, which samples it.
 *
 * @param gpu The backend
 * @param data The view data of the target, BOX_GPU_VIEW_STRIDE per slot
 */
void box_gpu_planar_uniforms(
    box_gpu_t *gpu,
    uint8_t *data);

/** Record the planar reflections of a view into their images, before its forward pass: the
 * cull of each, then a pass that draws its background, its meshes, sky and particles mirrored
 * onto a transparent clear, then its copy into the reflection array (box_gpu_planar_view).
 *
 * The alpha of an image is the coverage of what the reflection drew; the shape of the plane
 * shows the probes where it is 0.
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param packet The packet
 * @param v The view
 * @param slot Slot of the view in the view buffer
 */
void box_gpu_planar_render(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t slot);

/** Get the reflection array of a view, which its forward pass samples (binding 21 of the view
 * group): a layer per plane slot with the image of its last reflection in a corner and a blur
 * pyramid.
 * @param gpu The backend
 * @param t The target, NULL for the captures
 * @param v The view, NULL for the captures
 * @return The 2D array view of every mip, the 1x1 transparent array without one
 */
WGPUTextureView box_gpu_planar_view(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v);

/** Release the images and reflection arrays of the reflections no target rendered in a packet:
 * of a plane or view that went away, or a slot a view no longer fills.
 *
 * @param gpu The backend
 * @param packet The packet that rendered
 */
void box_gpu_planar_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

#endif
