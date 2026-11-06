/**
 * @file render/gpu.h
 * @brief State of the webgpu backend shared by its device, target and capture sources.
 *
 * Everything declared here is only used on the thread that renders the packets.
 */

#ifndef BOX_RENDER_GPU_H
#define BOX_RENDER_GPU_H

#include "gpu_types.h"

#ifdef BOX_RENDER_WGPU

/** Queue the outcome of a shader compilation for the main thread.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @param revision Revision of the compiled code
 * @param message Error text, NULL when the module compiled
 * @param label Label of the shader, used to prefix the message
 */
void box_gpu_shader_report(
    box_gpu_t *gpu,
    uint32_t handle,
    int32_t revision,
    const char *message,
    const char *label);

/** Count an event of the backend in the pending counters, without taking the lock.
 *
 * @param gpu The backend
 * @param stat The counter
 */
void box_gpu_count(
    box_gpu_t *gpu,
    box_gpu_stat_t stat);

/** Begin a compute pass, timed when it has timestamp writes.
 *
 * Debug builds assert that the query set of the writes is set and that both indices are below
 * its count, so WGPU_QUERY_SET_INDEX_UNDEFINED fails natively too: emdawnwebgpu passes it on as
 * an out of range index, which makes the pass and its command buffer invalid in the browser.
 *
 * @param encoder The encoder
 * @param writes Timestamps the pass writes, NULL when the pass is not timed
 * @return The compute pass
 */
WGPUComputePassEncoder box_gpu_compute_begin(
    WGPUCommandEncoder encoder,
    const WGPUPassTimestampWrites *writes);

/** Request a render pipeline, created in the background and counted in the stats while it is
 * created and once it exists.
 *
 * The first request of a descriptor starts the creation; requests get NULL until the outcome is
 * delivered at the start of a packet, then a reference, NULL again when it failed. A headless or
 * capturing packet creates it during the request, unless box_render_gpu_compile_delivery holds.
 *
 * @param gpu The backend
 * @param desc The pipeline
 * @return A reference the caller releases, NULL while the pipeline is created or when it failed
 */
WGPURenderPipeline box_gpu_render_pipeline(
    box_gpu_t *gpu,
    const WGPURenderPipelineDescriptor *desc);

/** Request a compute pipeline, created in the background like box_gpu_render_pipeline.
 *
 * @param gpu The backend
 * @param desc The pipeline
 * @return A reference the caller releases, NULL while the pipeline is created or when it failed
 */
WGPUComputePipeline box_gpu_compute_pipeline(
    box_gpu_t *gpu,
    const WGPUComputePipelineDescriptor *desc);

/** Get the pipelines being created: requested, and their outcome not delivered yet.
 *
 * @param gpu The backend
 * @return The pipelines
 */
int32_t box_gpu_pipelines_building(
    const box_gpu_t *gpu);

/** Replace a group of render pipelines once every pipeline that replaces them was created, so
 * the group keeps its previous pipelines until then and when one of them failed.
 *
 * @param current The pipelines of the group, released and replaced when every next one exists
 * @param next The pipelines that replace them, released when one is NULL, set to NULL
 * @param count Pipelines of the group
 * @return Whether the group was replaced
 */
bool box_gpu_render_pipelines_swap(
    WGPURenderPipeline *const *current,
    WGPURenderPipeline *next,
    int32_t count);

/** Replace a group of compute pipelines like box_gpu_render_pipelines_swap.
 *
 * @param current The pipelines of the group, released and replaced when every next one exists
 * @param next The pipelines that replace them, released when one is NULL, set to NULL
 * @param count Pipelines of the group
 * @return Whether the group was replaced
 */
bool box_gpu_compute_pipelines_swap(
    WGPUComputePipeline *const *current,
    WGPUComputePipeline *next,
    int32_t count);

/** Release requested compute pipelines that are not used, the ones that are not NULL.
 *
 * @param pipelines The pipelines, set to NULL
 * @param count Pipelines
 */
void box_gpu_compute_pipelines_release(
    WGPUComputePipeline *pipelines,
    int32_t count);

/** Deliver the pipelines whose creation finished, unless box_render_gpu_compile_delivery holds
 * them, at the start of a packet. A packet that waits for its pipelines (headless or with a
 * capture) first waits for the creations in progress.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_pipelines_deliver(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Stop the workers and release every pipeline; the browser frees the ones it still creates
 * when their outcome arrives.
 *
 * @param gpu The backend
 */
void box_gpu_pipelines_fini(
    box_gpu_t *gpu);

/** Hand the device errors reported on this thread to a capture until box_gpu_error_capture_end,
 * instead of the uncaptured error handler counting them.
 *
 * @param capture The capture, zeroed by the caller, which frees its message
 */
void box_gpu_error_capture_begin(
    box_gpu_error_capture_t *capture);

/** Stop handing the device errors reported on this thread to a capture. */
void box_gpu_error_capture_end(void);

/** Take a device error reported on this thread for its capture.
 *
 * @param message Error text
 * @return Whether a capture runs on this thread and took the error
 */
bool box_gpu_error_capture_take(
    WGPUStringView message);

/** Format a wgpu validation error as one line: the causes it lists below its "Caused by:"
 * line, joined by ": ".
 *
 * @param text The error
 * @param length Bytes of the error
 * @return The line, which the caller frees
 */
char* box_gpu_pipeline_message(
    const char *text,
    int32_t length);

/** Add the pending counters to the counters the main thread reads, under the lock once.
 *
 * @param gpu The backend
 */
void box_gpu_stats_publish(
    box_gpu_t *gpu);

/** Create, reconfigure or release a target.
 *
 * @param gpu The backend
 * @param op What happens to the target
 * @param handle Handle of the target
 * @param desc Size and kind of the target, NULL for a release
 */
void box_gpu_target_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_target_desc_t *desc);

/** Clear a target, scale its scene to the presented image, capture it and present it.
 *
 * A window presents each frame once, FIFO with vsync; flecs paces a cap with its target fps.
 * Presenting a frame again holds two of the 3 drawables wgpu gives a CAMetalLayer (latency 2 + 1,
 * clamped to 1..2), and nextDrawable then waits on the compositor without a timeout.
 *
 * @param gpu The backend
 * @param packet The packet the target belongs to
 * @param frame The target of the packet
 */
void box_gpu_target_render(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_target_t *frame);

/** Release every target.
 *
 * @param gpu The backend
 */
void box_gpu_target_fini(
    box_gpu_t *gpu);

/** Compile, recompile or release a shader module.
 *
 * A module that fails to compile is reported and leaves the previous module in place. The clip
 * distances variant of the shadow shader is not compiled by a device without the feature.
 *
 * @param gpu The backend
 * @param op What happens to the module
 * @param handle Handle of the shader
 * @param desc Description followed by the code and the label, NULL for a release
 */
void box_gpu_shader_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_shader_desc_t *desc);

/** Get the module of a shader.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @return The module, NULL when the shader has none
 */
WGPUShaderModule box_gpu_shader_get(
    box_gpu_t *gpu,
    uint32_t handle);

/** Get the label of a shader module.
 *
 * @param gpu The backend
 * @param module The module
 * @return The label, NULL when the module is not the module of a shader
 */
const char* box_gpu_shader_label(
    box_gpu_t *gpu,
    WGPUShaderModule module);

/** Finish the compiles whose outcome box_render_gpu_compile_delivery held and those whose
 * forward check finished.
 *
 * @param gpu The backend
 */
void box_gpu_shader_deliver(
    box_gpu_t *gpu);

/** Release every shader module.
 *
 * @param gpu The backend
 */
void box_gpu_shader_fini(
    box_gpu_t *gpu);

/** Release the blit pipelines, which are created again with the current blit shader.
 *
 * @param gpu The backend
 */
void box_gpu_blit_reset(
    box_gpu_t *gpu);

/** Get the blit pipeline of a color format, creating it on first use.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @return The pipeline, NULL without a blit shader or when too many formats are in use
 */
WGPURenderPipeline box_gpu_blit_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format);

/** Create the bind group layout of the texture layers and the default textures.
 *
 * @param gpu The backend
 */
void box_gpu_texture_init(
    box_gpu_t *gpu);

/** Release the textures, samplers and bind groups of the texture layers.
 *
 * @param gpu The backend
 */
void box_gpu_texture_fini(
    box_gpu_t *gpu);

/** Create, replace or release a texture from a BOX_RENDER_TEXTURE resource.
 *
 * @param gpu The backend
 * @param op What happens to the texture
 * @param handle Handle of the texture
 * @param desc Size followed by the pixels, a box_render_texture_region_t for an update, NULL
 * for a release
 */
void box_gpu_texture_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_texture_desc_t *desc);

/** Generate the mip levels of the queued textures, after the resources of a packet applied.
 *
 * A texture a batch binds in a color layer averages linear light, others the stored values;
 * every chain of the frame is submitted at once, and one that cannot be made stays queued.
 *
 * @param gpu The backend
 * @param packet The packet whose batches tell the layers that bind the textures
 */
void box_gpu_texture_mips(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Release every cached bind group of the texture layers, after a texture they bind changed.
 *
 * @param gpu The backend
 */
void box_gpu_materials_clear(
    box_gpu_t *gpu);

/** Get the view a light cookie samples a texture through: every level as stored, so a texture
 * that is not single channel reads its stored red channel, not decoded from sRGB.
 *
 * @param gpu The backend
 * @param handle Texture handle
 * @return The view, NULL when the backend has no image of the handle
 */
WGPUTextureView box_gpu_texture_cookie(
    box_gpu_t *gpu,
    uint32_t handle);

/** Get the view a particle sprite samples a texture through, decoded from sRGB, and queue its
 * levels for generation in linear light.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 or one the backend lacks for white
 * @return The view
 */
WGPUTextureView box_gpu_texture_color(
    box_gpu_t *gpu,
    uint32_t handle);

/** Get the sampler of a wrap mode and an anisotropy, created on first use.
 *
 * @param gpu The backend
 * @param wrap The TextureWrap
 * @param anisotropy Anisotropic samples, rounded down to a power of two up to 16
 * @return The sampler
 */
WGPUSampler box_gpu_texture_sampler(
    box_gpu_t *gpu,
    uint32_t wrap,
    uint32_t anisotropy);

/** Get the bind group of the texture layers and sampler of a batch, created on first use.
 *
 * A layer whose handle has BOX_RENDER_TEXTURE_TARGET samples the image of that target, or the
 * default of the layer while the target itself renders or does not exist.
 *
 * A batch that draws with the texture_blend variant (box_gpu_texture_blends) gets the group of
 * its TextureBlend instead (blend_layout): the texture arrays of its layers, its mask, its
 * emissive layer, the sampler and the uniforms of its layers.
 *
 * @param gpu The backend
 * @param packet The packet of the batch, whose data holds its TextureBlend and which sets the
 * anisotropic samples of the sampler
 * @param b The batch
 * @return The bind group
 */
WGPUBindGroup box_gpu_material_group(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b);

/** Check whether a batch draws with the texture_blend variant of the mesh pipeline: a batch of
 * the mesh shader with a TextureBlend, of any blend mode, alpha tested or transmissive. Its
 * alpha tested depth prepass (fs_depth_cutout_blend) and shadow (fs_cutout_blend) pipelines bind
 * the blend group too.
 *
 * @param b The batch
 * @return Whether it blends texture layers
 */
bool box_gpu_texture_blends(
    const box_render_batch_t *b);

/** Release the texture arrays and bind groups of the TextureBlend batches.
 *
 * @param gpu The backend
 */
void box_gpu_blends_clear(
    box_gpu_t *gpu);

/** Bytes of the SH of one probe */
#define BOX_GPU_PROBE_SIZE (BOX_RENDER_SH_COEFFICIENTS * 4 * sizeof(float))

/** Texels of the octahedral visibility map of a probe, VISIBILITY_SIZE squared in mesh.wgsl */
#define BOX_GPU_VISIBILITY_TEXELS (64)

/** Bytes of the visibility map of one probe, a word per texel */
#define BOX_GPU_VISIBILITY_SIZE (BOX_GPU_VISIBILITY_TEXELS * sizeof(uint32_t))

/** Texel of a visibility map that sees no surface: halves 2 and 4, the largest distance in
 * spacings of bake_sh.wgsl and its square, so every point is visible before a capture */
#define BOX_GPU_VISIBILITY_OPEN (0x44004000u)

/** Create the layout of the probe volumes and the volume of views without one.
 *
 * @param gpu The backend
 */
void box_gpu_volume_init(
    box_gpu_t *gpu);

/** Release the probe volumes and their layout.
 *
 * @param gpu The backend
 */
void box_gpu_volume_fini(
    box_gpu_t *gpu);

/** Release a probe volume: its buffers, bind groups and the volume.
 *
 * @param v The volume
 */
void box_gpu_volume_free(
    box_gpu_volume_t *v);

/** Create, refill or release a probe volume from a BOX_RENDER_VOLUME resource.
 *
 * @param gpu The backend
 * @param op What happens to the volume
 * @param handle Handle of the volume
 * @param desc Grid, SH and validity mask, NULL for a release
 * @param size Bytes of the description with its mask
 */
void box_gpu_volume_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_volume_desc_t *desc,
    uint32_t size);

/** Get the bind group of the probe volume a view samples.
 *
 * @param gpu The backend
 * @param v The view
 * @return The group of its volume, or of the 1x1x1 volume that leaves the hemisphere ambient
 */
WGPUBindGroup box_gpu_volume_group(
    box_gpu_t *gpu,
    const box_render_view_t *v);

/** Create the layouts of the reflection cubemap passes.
 *
 * @param gpu The backend
 */
void box_gpu_reflect_init(
    box_gpu_t *gpu);

/** Release the reflection cubemaps, their pipelines and layouts.
 *
 * @param gpu The backend
 */
void box_gpu_reflect_fini(
    box_gpu_t *gpu);

/** Create or release the cubemap array of a scene from a BOX_RENDER_REFLECTION resource.
 *
 * A create of an existing handle at the same resolution copies every mip level of the slots
 * both arrays have into the new one.
 *
 * @param gpu The backend
 * @param op What happens to the array
 * @param handle Handle of the array
 * @param desc Resolution and slots, NULL for a release
 */
void box_gpu_reflect_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_reflection_desc_t *desc);

/** Get the cubemap array a view samples its reflection probes from.
 *
 * @param gpu The backend
 * @param v The view
 * @return The cubemap array of the scene of the view, NULL when the view samples no reflection
 *         probes or the backend lacks their buffer or array
 */
const box_gpu_reflect_t* box_gpu_reflect_of(
    box_gpu_t *gpu,
    const box_render_view_t *v);

/** Request the store, mip and prefilter pipelines from the module of the bake shader.
 *
 * @param gpu The backend
 * @param module The module
 * @param next Receives the store, mip and prefilter pipelines, NULL while one is created
 * @param current Receives the fields they replace
 */
void box_gpu_reflect_pipelines(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    WGPUComputePipeline *next,
    WGPUComputePipeline **current);

/** Create a cube array and its views: the slots of a scene with mips down to
 * BOX_GPU_REFLECT_MIN_MIP texels and their SH, or source cubes with a full mip chain.
 *
 * @param gpu The backend
 * @param resolution Face edge of mip 0 in texels
 * @param cubes Cubemap slots
 * @param source Whether the cubes are source cubes
 * @return The cube array
 */
box_gpu_reflect_t* box_gpu_reflect_new(
    box_gpu_t *gpu,
    uint32_t resolution,
    uint32_t cubes,
    bool source);

/** Release a cube array and its views.
 *
 * @param c The cube array
 */
void box_gpu_reflect_free(
    box_gpu_reflect_t *c);

/** Check whether the store, mip and prefilter pipelines exist.
 *
 * @param gpu The backend
 * @return Whether the bake shader made them
 */
bool box_gpu_reflect_ready(
    const box_gpu_t *gpu);

/** Record into a compute pass the reflect_mip dispatches that box filter each mip level of a
 * cube array from the level above, level 1 to the last.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param params Uniforms, Params of bake_sh.wgsl whose size.y is the cubes the faces cover
 * @param params_size Bytes of the uniforms
 * @param levels 2D array view of each mip level
 * @param resolution Face edge of mip 0 in texels
 * @param mips Mip levels
 * @param faces Layers the dispatches cover, six per cube
 */
void box_gpu_reflect_mip_chain(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    WGPUBuffer params,
    uint64_t params_size,
    const WGPUTextureView *levels,
    uint32_t resolution,
    uint32_t mips,
    uint32_t faces);

/** Record into a compute pass the reflect_prefilter dispatches that fill mip levels 1 to the
 * last of a cube array with the GGX prefiltered radiance of box filtered source cubes.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param params Uniforms, Params of bake_sh.wgsl with the resolution as size.x, the cubes as
 *               size.y and the layer of each cube in probes
 * @param params_size Bytes of the uniforms
 * @param source Cube array view of the source cubes with their full mip chain
 * @param levels 2D array view of each mip level written
 * @param resolution Face edge of mip 0 in texels
 * @param mips Mip levels, the last one at least BOX_GPU_REFLECT_MIN_MIP texels
 * @param faces Layers the dispatches cover, six per cube
 */
void box_gpu_reflect_prefilter_chain(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    WGPUBuffer params,
    uint64_t params_size,
    WGPUTextureView source,
    const WGPUTextureView *levels,
    uint32_t resolution,
    uint32_t mips,
    uint32_t faces);

/** Record the pass that writes the faces of a chunk of reflection captures into mip 0 of their
 * slots and fills every higher mip level of those slots with the GGX prefiltered radiance.
 *
 * The faces go to source cubes of the chunk as well, whose box filtered mips the prefilter
 * samples by the solid angle of each sample. The SH of the slots is the projection's.
 *
 * @param gpu The backend
 * @param encoder The encoder of the chunk
 * @param cube The cubemap array
 * @param params Uniforms of the chunk, Params of bake_sh.wgsl with the slots as its probes
 * @param params_size Bytes of the uniforms
 * @param atlas View of the face atlas of the chunk
 * @param environment Cube view of the environment texels nothing covered see when
 *        params.size.z is set, NULL for the black cube
 * @param count Probes of the chunk
 * @param writes Timestamps the pass writes, NULL for none
 * @return Whether the pass was recorded, false while the pipelines do not exist
 */
bool box_gpu_reflect_store(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_gpu_reflect_t *cube,
    WGPUBuffer params,
    uint64_t params_size,
    WGPUTextureView atlas,
    WGPUTextureView environment,
    int32_t count,
    const WGPUPassTimestampWrites *writes);

/** Release the state of the probe captures.
 *
 * @param gpu The backend
 */
void box_gpu_bake_fini(
    box_gpu_t *gpu);

/** Render the probe captures of a packet and project them onto the SH of their volumes, then
 * render its reflection captures into their cubemaps.
 *
 * @param gpu The backend
 * @param packet The packet, after its resources, uploads and scatters applied
 */
void box_gpu_bake_capture(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Get the forward pipeline of a color format and the state of a batch, created on first use.
 *
 * The cache grows with the states in use; a recompiled or released mesh or 2D shape shader
 * empties it, and a batch whose shader does not exist draws nothing. A TextureBlend batch
 * (box_gpu_texture_blends) gets the texture_blend variant, fs_blend with blend_mesh_layout, in
 * every pass it draws in.
 *
 * After a recompile a batch draws with the pipeline of the previous module until the pipeline of
 * the new module exists, or when it failed.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @param b The batch
 * @param equal Whether the pipeline draws after a depth prepass: compare Equal without depth
 * writes and, alpha tested, without discarding, for the opaque meshes of a lit view; false for
 * the compare Greater with depth writes of the other draws, unlit views and the captures
 * @return The pipeline, NULL without a shader, for an unknown blend mode or while the first one
 * of its key is created
 */
WGPURenderPipeline box_gpu_forward_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_batch_t *b,
    bool equal);

/** Create the layouts the forward pass shares.
 *
 * Group 0 binds 9 sampled textures to the fragment stage, the depth copy of the opaque snapshot
 * (binding 20) the last: with the 4 material maps the mesh pipelines sample 13 of the 16 a
 * device has at least, the particle and fog volume pipelines fewer.
 *
 * @param gpu The backend
 */
void box_gpu_draw_init(
    box_gpu_t *gpu);

/** Release the meshes, buffers, pipelines and layouts of the forward pass.
 *
 * @param gpu The backend
 */
void box_gpu_draw_fini(
    box_gpu_t *gpu);

/** Release the forward pipelines, which are created again with the current mesh shader.
 *
 * @param gpu The backend
 * @param keep Whether a recompile replaced the module: its pipelines are drawn with until the
 * pipelines of the new module exist, else they are released too
 */
void box_gpu_forward_reset(
    box_gpu_t *gpu,
    bool keep);

/** Check whether a mesh or 2D shape module fits the pipeline layout before it replaces the
 * module that renders, by requesting its forward pipelines. A
 * mesh module also requests the depth prepass and the texture_blend pipelines (fs_blend and
 * fs_depth_cutout_blend) and a
 * 2D shape module the shadow, slice and polygon pipelines (fs_shadow, fs_slice, fs_polygon,
 * fs_shadow_polygon and the polygon override constant), so they need their entry points.
 *
 * The pipelines are created in the background, so the check is asked again each packet until
 * every one of them exists or one failed. The browser does not check: an invalid pipeline shows
 * as a failed pipeline there.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @param shader The module
 * @param error Receives NULL when the pipelines are valid, else the error, which the caller frees
 * @return Whether the check finished, false while a pipeline is created
 */
bool box_gpu_forward_check(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule shader,
    char **error);

/** Create or release a mesh from a BOX_RENDER_MESH resource.
 *
 * @param gpu The backend
 * @param op What happens to the mesh
 * @param handle Handle of the mesh
 * @param desc Description followed by the vertices and indices, NULL for a release
 */

void box_gpu_mesh_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_mesh_desc_t *desc);

/** Create or release a buffer from a BOX_RENDER_BUFFER resource.
 *
 * A create with BOX_RENDER_BUFFER_KEEP that replaces a buffer copies the bytes the two have
 * in common from the old buffer on the GPU before releasing it.
 *
 * @param gpu The backend
 * @param op What happens to the buffer
 * @param handle Handle of the buffer
 * @param desc Size and usage, NULL for a release
 */
void box_gpu_buffer_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_buffer_desc_t *desc);

/** Check whether a buffer can be the source of GPU copies.
 *
 * @param b The buffer
 * @return Whether it was created with CopySrc usage
 */
bool box_gpu_buffer_copyable(
    const box_gpu_buffer_t *b);

/** Create the state of the copies between buffers, without a staging buffer.
 *
 * @param gpu The backend
 */
void box_gpu_copy_init(
    box_gpu_t *gpu);

/** Release the staging buffer of the copies and their state.
 *
 * @param gpu The backend
 */
void box_gpu_copy_fini(
    box_gpu_t *gpu);

/** Run the copies between buffers of a packet, after its resources and before its uploads.
 *
 * A copy between two buffers whose source no copy of the packet writes runs directly. The
 * others, within one buffer or reading a range another copy writes, go through a staging
 * buffer, so every copy reads its source as it was before the copies of the packet.
 *
 * Runs for every packet, which counts it for the release of the staging buffer.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_copy(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Write the uploads of a packet into their buffers.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_upload(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Render the views of a target in order with the forward pass.
 *
 * A lit view with opaque meshes records its depth prepasses before its forward pass. Between
 * the last prepass and the forward pass the depth of every opaque mesh is complete and nothing
 * is shaded yet: the compute passes that read that depth (Gtao) record there.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet with the views and batches
 * @param frame The target of the packet
 * @param encoder The encoder of the frame
 * @param color The texture view the views render into
 * @param texture The texture of that view, which the opaque snapshot of a view without effects
 * copies
 */
void box_gpu_draw_views(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_target_t *frame,
    WGPUCommandEncoder encoder,
    WGPUTextureView color,
    WGPUTexture texture);

/** Create the layouts of the cluster pass and the empty light buffer.
 *
 * @param gpu The backend
 */
void box_gpu_light_init(
    box_gpu_t *gpu);

/** Release the pipeline, layouts and buffers of the cluster pass.
 *
 * @param gpu The backend
 */
void box_gpu_light_fini(
    box_gpu_t *gpu);

/** Write the light members of the uniforms of a view.
 *
 * @param gpu The backend
 * @param v The view
 * @param proj Projection of the view for its target
 * @param u The uniforms
 */
void box_gpu_light_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    mat4 proj,
    box_gpu_view_uniforms_t *u);

/** Record the cluster pass of a view when it has point or spot lights or reflection probes.
 *
 * @param gpu The backend
 * @param t The target that renders the view
 * @param encoder The encoder of the frame
 * @param v The view
 * @param slot Slot of the view in the view buffer
 */
void box_gpu_light_cluster(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v,
    int32_t slot);

/** Release the cluster buffers of the views no target rendered for a while.
 *
 * @param gpu The backend
 */
void box_gpu_light_purge(
    box_gpu_t *gpu);

/** Record the cluster pass of a view with explicit buffers, when it has point or spot lights or
 * reflection probes.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param v The view
 * @param uniforms Buffer with the uniforms of the view
 * @param offset Byte offset of the uniforms in the buffer
 * @param clusters Cluster buffer the pass writes
 */
void box_gpu_light_cluster_with(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v,
    WGPUBuffer uniforms,
    uint32_t offset,
    WGPUBuffer clusters);

/** Get the light buffer a view binds.
 *
 * @param gpu The backend
 * @param v The view
 * @param size Receives the bytes of the buffer
 * @return The light buffer of the view, or the empty buffer when the backend lacks it
 */
WGPUBuffer box_gpu_light_binding(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    uint64_t *size);

/** Write the light cookie members of the uniforms of a view: the LightCookie of its first
 * directional light, applied when the view is lit and the backend has the image of its texture.
 * The probe and reflection captures leave them zero, so they never apply a cookie.
 *
 * @param gpu The backend
 * @param v The view
 * @param u The uniforms
 */
void box_gpu_cookie_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u);

/** Create the bind group of the forward pass of a view with explicit buffers.
 *
 * @param gpu The backend
 * @param t The target whose aerial perspective volume of the view it binds, NULL for the
 * placeholder volume, the black environment, the zero environment SH, the empty shadow atlas,
 * the zero tile records and the white light cookie of the probe and reflection captures
 * @param v The view
 * @param uniforms Buffer with the uniforms, bound with a dynamic offset
 * @param clusters Cluster buffer of the view
 * @param shadow Array view of the shadow map the view samples
 * @param ao Ambient occlusion image of the view (box_gpu_gtao_begin), NULL for the white image
 * @param snapshot Opaque snapshot of the view (box_gpu_transmission_view), NULL for the white
 * image, as the probe and reflection captures bind
 * @param snapshot_depth Depth copy of the opaque snapshot (box_gpu_transmission_depth_view),
 * NULL for the 1x1 depth placeholder, as the probe and reflection captures bind
 * @return The bind group, released by the caller
 */
WGPUBindGroup box_gpu_light_group_with(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v,
    WGPUBuffer uniforms,
    WGPUBuffer clusters,
    WGPUTextureView shadow,
    WGPUTextureView ao,
    WGPUTextureView snapshot,
    WGPUTextureView snapshot_depth);

/** Create the bind group of the forward pass of a view: its uniforms, lights, clusters, shadow
 * map, aerial perspective volume, environment, ambient occlusion, shadow atlas, tile records,
 * light cookie and opaque snapshot with its depth copy.
 *
 * @param gpu The backend
 * @param t The target
 * @param v The view
 * @param slot Slot of the view in the view buffer
 * @param ao Ambient occlusion image of the view (box_gpu_gtao_begin), NULL for the white image
 * @param snapshot Opaque snapshot of the view (box_gpu_transmission_begin), NULL for the white
 * image and the depth placeholder
 * @return The bind group, released by the caller
 */
WGPUBindGroup box_gpu_light_group(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v,
    int32_t slot,
    WGPUTextureView ao,
    const box_gpu_snapshot_t *snapshot);

/** Create the layouts, sampler and empty shadow map of the shadow pass.
 *
 * @param gpu The backend
 */
void box_gpu_shadow_init(
    box_gpu_t *gpu);

/** Release the pipeline, layouts, textures and buffers of the shadow pass.
 *
 * @param gpu The backend
 */
void box_gpu_shadow_fini(
    box_gpu_t *gpu);

/** Fit the cascades of a view to its projection and write the shadow members of its uniforms.
 *
 * @param gpu The backend
 * @param v The view
 * @param proj Projection of the view for its target
 * @param slot Slot of the view in the view buffer
 * @param u The uniforms
 */
void box_gpu_shadow_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    mat4 proj,
    int32_t slot,
    box_gpu_view_uniforms_t *u);

/** Write the cascade matrices of the views of a target into the cascade buffer.
 *
 * @param gpu The backend
 * @param count Views of the target
 */
void box_gpu_shadow_upload(
    box_gpu_t *gpu,
    int32_t count);

/** Record the shadow passes of a view: its casters drawn into every cascade.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param packet The packet with the batches
 * @param index Index of the view in the packet
 * @param slot Slot of the view in the view buffer
 */
void box_gpu_shadow_render(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot);

/** Write the uniforms of one cascade of a shadow lookup.
 *
 * @param u The view uniforms
 * @param index The cascade
 * @param cascade The fitted cascade
 * @param depth_bias Depth offset of the comparison in texels
 * @param normal_offset Offset along the geometric normal in texels at grazing light
 * @param resolution Edge of the cascade in texels
 */
void box_gpu_shadow_cascade_uniforms(
    box_gpu_view_uniforms_t *u,
    int32_t index,
    const box_render_cascade_t *cascade,
    float depth_bias,
    float normal_offset,
    int32_t resolution);

/** Write the uniforms of the filter and the cascade choice of a shadow lookup.
 *
 * @param u The view uniforms
 * @param cascades The cascades, 0 without shadows
 * @param filter_size Width of the tent filter in texels, odd
 * @param normal_offset Offset along the geometric normal in texels at grazing light
 * @param blend Blend band at the edge of a cascade in half extents
 * @param resolution Edge of a cascade in texels
 */
void box_gpu_shadow_filter_uniforms(
    box_gpu_view_uniforms_t *u,
    int32_t cascades,
    int32_t filter_size,
    float normal_offset,
    float blend,
    int32_t resolution);

/** Get the texels a position stays away from the edge of the cascade it is looked up in: the
 * reach of the tent filter beyond the position, plus the normal offset.
 *
 * @param filter_size Width of the tent filter in texels, odd
 * @param normal_offset Offset along the geometric normal in texels at grazing light
 * @return The margin in texels
 */
float box_gpu_shadow_margin(
    int32_t filter_size,
    float normal_offset);

/** Fit the cascade of the probe captures around the bounding sphere of static bounds.
 *
 * @param min Minimum corner of the bounds
 * @param max Maximum corner of the bounds
 * @param direction Direction toward the light
 * @param out Receives the cascade
 */
void box_gpu_shadow_capture_fit(
    const float *min,
    const float *max,
    const float *direction,
    box_render_cascade_t *out);

/** Render the casters of the cull view of the capture cascade into the capture shadow map.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param packet The packet
 * @param slot Slot of the cull view, whose cascade 0 holds the casters
 * @param cascade The cascade
 * @return The array view the captures sample, the empty map without a shadow pipeline
 */
WGPUTextureView box_gpu_shadow_capture(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    int32_t slot,
    const box_render_cascade_t *cascade);

/** Request the shadow pipelines of the cascades, created on first use, so that a capture that
 * renders the casters can wait for them (box_gpu_pipelines_building).
 *
 * @param gpu The backend
 */
void box_gpu_shadow_request(
    box_gpu_t *gpu);

/** Get the shadow map a view samples.
 *
 * @param gpu The backend
 * @param v The view
 * @return The array view of the shadow map, the empty one for a view without shadows
 */
WGPUTextureView box_gpu_shadow_binding(
    box_gpu_t *gpu,
    const box_render_view_t *v);

/** Create, recreate or release the shadow atlas of a scene from a BOX_RENDER_SHADOW_ATLAS
 * resource.
 *
 * @param gpu The backend
 * @param op What happens to the atlas
 * @param handle Handle of the atlas
 * @param desc Its resolution, NULL for a release
 */
void box_gpu_atlas_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_shadow_atlas_desc_t *desc);

/** Render the tiles of the shadow atlases of a packet, after its probe captures and before its
 * targets, so every forward pass of the packet samples them.
 *
 * The tiles that render (flagged, or all of an atlas that cannot keep them) are culled in one
 * cull pass; the tiles of an atlas then draw in one render pass with a viewport per tile, which
 * clears the atlas when all render, else loads it and clears each tile; none when none renders.
 *
 * A point light tile draws every caster draw once per octant, 8 draws, the octant in the dynamic
 * offset of the shadow uniforms, clipped to it with clip distances or by a discard.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_atlas_render(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Get the shadow atlas and the tile records a view samples.
 *
 * @param gpu The backend
 * @param v The view
 * @param tiles Receives the tile buffer, NULL when the view samples none
 * @return The atlas, NULL when the view has no spot shadows or the backend lacks the atlas or
 *         its tile buffer
 */
const box_gpu_atlas_t* box_gpu_atlas_of(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    WGPUBuffer *tiles);

/** Write the spot shadow members of the uniforms of a view: its tile records and filter, zero
 * when it samples no atlas.
 *
 * @param gpu The backend
 * @param v The view
 * @param u The uniforms
 */
void box_gpu_atlas_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u);

/** Create the layouts, sampler and timestamp queries of the atmosphere passes.
 *
 * @param gpu The backend
 */
void box_gpu_atmosphere_init(
    box_gpu_t *gpu);

/** Release the LUTs, view states, pipelines, layouts and queries of the atmosphere passes.
 *
 * @param gpu The backend
 */
void box_gpu_atmosphere_fini(
    box_gpu_t *gpu);

/** Create, update or release the LUTs of an atmosphere from a BOX_RENDER_ATMOSPHERE resource;
 * a create or an update builds them before the next targets.
 *
 * @param gpu The backend
 * @param op What happens to the LUTs
 * @param handle Handle of the LUTs
 * @param desc The medium, NULL for a release
 */
void box_gpu_atmosphere_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_atmosphere_desc_t *desc);

/** Advance the timestamps of the atmosphere passes and build the LUTs that wait for a build,
 * after the resources of a packet applied and before its targets.
 *
 * LUTs wait while the atmosphere shader has no module, and build again after it recompiled.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_atmosphere_build(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Take the request of a BOX_RENDER_ATMOSPHERE_ENVIRONMENT resource to render the sky of an
 * atmosphere into the cubemap of an environment, replacing one of the handle that still waits.
 *
 * @param gpu The backend
 * @param handle Handle of the cubemap
 * @param desc The request, NULL to drop the one of the handle
 */
void box_gpu_atmosphere_environment_apply(
    box_gpu_t *gpu,
    uint32_t handle,
    const box_render_atmosphere_env_t *desc);

/** Render the environments of atmospheres that wait for it, after box_gpu_atmosphere_build
 * and before box_gpu_environment_process: the sky view LUT of the scene at its ground
 * altitude, then mip 0 of the cubemap from it, reported written with the request's version.
 *
 * A request waits while its LUTs are not built with the current pipelines or
 * box_gpu_environment_ready fails, and is dropped when its cubemap or its LUTs are gone.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_atmosphere_environment(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Record the sky view and then the aerial perspective pass of a view whose scene has built
 * atmosphere LUTs, before its forward pass, and the fog in scatter color when it runs a
 * HeightFog whose atmosphere is that of its scene; other views record nothing.
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param packet The packet
 * @param v The view
 */
void box_gpu_atmosphere_view(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v);

/** Check whether a view draws the sky and applies the aerial perspective in this packet: it
 * has LightSettings.atmosphere and its scene has built atmosphere LUTs, so
 * box_gpu_atmosphere_view records its passes.
 *
 * @param gpu The backend
 * @param v The view
 * @return Whether the view renders its atmosphere
 */
bool box_gpu_atmosphere_sky(
    box_gpu_t *gpu,
    const box_render_view_t *v);

/** Get the fog in scatter color of a view in a target: a 2x1 image of the ambient and the glow
 * toward the main light of the atmosphere, written by box_gpu_atmosphere_view in this packet
 * for a view that renders its atmosphere and runs a HeightFog that refers to it.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param v The view
 * @return The image, NULL when the view has none in this packet
 */
WGPUTextureView box_gpu_atmosphere_fog(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v);

/** Write the aerial perspective uniforms of a view, zero when it does not render its
 * atmosphere.
 *
 * @param gpu The backend
 * @param v The view
 * @param u The uniforms of the view
 */
void box_gpu_atmosphere_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u);

/** Write the aerial perspective volume and its sampler as bindings 9 and 10 of the forward
 * pass: the volume of the view in a target after box_gpu_atmosphere_view, else a placeholder.
 *
 * @param gpu The backend
 * @param t The target, NULL for the placeholder
 * @param v The view
 * @param out Receives the two entries
 */
void box_gpu_atmosphere_bindings(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v,
    WGPUBindGroupEntry *out);

/** Record the sky of a view that renders its atmosphere into its forward pass: a fullscreen
 * triangle at the far plane, depth 0 compared with equal and not written, so only the pixels
 * no geometry covered sample the sky view LUT. Replaces the bind group at 0.
 *
 * @param gpu The backend
 * @param t The target
 * @param pass The render pass of the view
 * @param format The format of the color target
 * @param v The view
 * @return Whether the sky was drawn
 */
bool box_gpu_atmosphere_draw_sky(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v);

/** Release the sky view and aerial perspective of the views the targets of a packet did not
 * render with an atmosphere.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_atmosphere_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Create the layouts and placeholders of the particle passes, after the view layout.
 *
 * @param gpu The backend
 */
void box_gpu_particles_init(
    box_gpu_t *gpu);

/** Release the pipelines, layouts and buffers of the particle passes.
 *
 * @param gpu The backend
 */
void box_gpu_particles_fini(
    box_gpu_t *gpu);

/** Record the integrate and spawn passes of every particle pool of a packet, after its
 * scatters and before its targets, and keep the draws of the pools for its views.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_particles_simulate(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Record the sort passes of the alpha blended particle pools of the scene of a view that draw
 * back to front, before the passes of the view.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param v The view
 * @param slot Slot of the view in the view buffer, which holds its uniforms
 */
void box_gpu_particles_sort(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v,
    int32_t slot);

/** Check whether a pool of the scene of a view fades in front of the depth of the view, so the
 * view draws its particles in a pass that reads the depth instead of writing it.
 *
 * @param gpu The backend
 * @param v The view
 * @return Whether a soft pool has particles in the scene of the view
 */
bool box_gpu_particles_soft(
    const box_gpu_t *gpu,
    const box_render_view_t *v);

/** Record the indirect draws of the particle pools of the scene of a view into its forward
 * pass, emitters back to front. Replaces the pipeline and the bind groups at 1 and 2.
 *
 * @param gpu The backend
 * @param pass The render pass of the view, with the bind group of the view at 0
 * @param format The format of the color target
 * @param v The view
 * @param depth The depth of the view, read only in the pass, NULL when it is written
 * @return Whether a pool was drawn
 */
bool box_gpu_particles_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    WGPUTextureView depth);

/** Create the layouts of the fog volume draws, after the view layout.
 *
 * @param gpu The backend
 */
void box_gpu_fog_volumes_init(
    box_gpu_t *gpu);

/** Release the pipelines, layouts and buffer of the fog volume draws.
 *
 * @param gpu The backend
 */
void box_gpu_fog_volumes_fini(
    box_gpu_t *gpu);

/** Start the fog volume draws of a packet, which fill the buffer of the draws from its start.
 *
 * @param gpu The backend
 */
void box_gpu_fog_volumes_begin(
    box_gpu_t *gpu);

/** Check whether a view draws fog volumes: it has records and the shader is compiled, so its
 * forward pass splits around them.
 *
 * @param gpu The backend
 * @param v The view
 * @return Whether the view draws its fog volumes in this frame
 */
bool box_gpu_fog_volumes_ready(
    box_gpu_t *gpu,
    const box_render_view_t *v);

/** Record the fog volumes of a view, one instanced draw back to front. Replaces the pipeline
 * and the bind group at 1.
 *
 * @param gpu The backend
 * @param pass The render pass of the view, which reads its depth, with the bind group of the
 * view at 0
 * @param t The target, whose size the projection follows
 * @param format The format of the color target
 * @param packet The packet, which holds the records
 * @param v The view
 * @param depth The depth of the view, read only in the pass
 * @return Whether the volumes were drawn
 */
bool box_gpu_fog_volumes_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_gpu_target_t *t,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureView depth);

/** Probes of a kind (light or reflection) the debug pass of a view draws at most */
#define BOX_GPU_DEBUG_PROBES_MAX ((uint32_t)BOX_RENDER_DEBUG_PROBES_MAX)

/** Create the layouts of the debug passes, after the view layout.
 *
 * @param gpu The backend
 */
void box_gpu_debug_init(
    box_gpu_t *gpu);

/** Release the pipelines, layouts and buffer of the debug passes.
 *
 * @param gpu The backend
 */
void box_gpu_debug_fini(
    box_gpu_t *gpu);

/** Start the debug passes of a packet, which fill the uniform buffer from its start.
 *
 * @param gpu The backend
 */
void box_gpu_debug_begin(
    box_gpu_t *gpu);

/** Check whether a view has a debug option on (View.debug), so it records a debug pass and
 * keeps its depth for it.
 *
 * @param v The view
 * @return Whether an item of the debug settings of the view is enabled
 */
bool box_gpu_debug_enabled(
    const box_render_view_t *v);

/** Neighbor of an edge on the border of a mesh, in its edge adjacency */
#define BOX_GPU_WIRE_NONE (0xFFFFFFFFu)

/** Upload the edge adjacency of a mesh that can be wired (box_gpu_mesh_t.wireable) for its
 * wire draws (debug_wire.wgsl): for each index, the vertex of the neighbor triangle across the
 * edge opposite that corner which is not on the edge, or BOX_GPU_WIRE_NONE on a border.
 *
 * Edges are matched by the positions of their vertices, welded within a tiny distance, so the
 * vertices a face or a uv seam duplicates still join, in linear time.
 *
 * The backend keeps no CPU copy of a mesh, so it is built at the upload; it has one uint32_t
 * per index, the size of the indices.
 *
 * @param gpu The backend
 * @param m The mesh, with its index count
 * @param vertices The vertices
 * @param vertex_count Number of vertices
 * @param indices The indices, three per triangle
 */
void box_gpu_debug_mesh_adjacency(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m,
    const box_vertex_t *vertices,
    uint32_t vertex_count,
    const uint32_t *indices);

/** Release the group of the wire draws of a mesh and its edge adjacency, before the mesh.
 *
 * @param m The mesh
 */
void box_gpu_debug_mesh_free(
    box_gpu_mesh_t *m);

/** Record the debug pass of a view after its effects: a render pass that loads the color of the
 * target and reads the depth of the view, drawing the grid, the wireframe of the mesh draws of
 * its cull view, then the probes and their boxes. A view without any records nothing.
 *
 * A debug item whose shader is not compiled yet draws nothing.
 *
 * @param gpu The backend
 * @param t The target, whose depth the pass reads and whose format it draws in
 * @param encoder The encoder of the frame
 * @param color The texture view of the target, the final color of the view with and without
 * effects
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 */
void box_gpu_debug_record(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    WGPUTextureView color,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot);

/** Create the layouts, sampler and placeholders of the effect passes.
 *
 * @param gpu The backend
 */
void box_gpu_effects_init(
    box_gpu_t *gpu);

/** Release the pipelines, layouts, HDR targets and the state of the effect passes.
 *
 * @param gpu The backend
 */
void box_gpu_effects_fini(
    box_gpu_t *gpu);

/** Get the HDR target a view with effects renders its forward pass into.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param v The view
 * @return The HDR image, NULL for a view without effects, which renders into the target
 */
WGPUTextureView box_gpu_effects_begin(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v);

/** Get the texture of the HDR image box_gpu_effects_begin returned for the view being drawn.
 *
 * @param gpu The backend
 * @return The texture, NULL when the view has no effects
 */
WGPUTexture box_gpu_effects_image(
    box_gpu_t *gpu);

/** Record the effect passes of a view over its HDR image, the last one writing the target.
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param packet The packet
 * @param v The view, whose forward pass rendered into the image box_gpu_effects_begin returned
 * @param color The texture view of the target the views render into
 */
void box_gpu_effects_run(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureView color);

/** Release the HDR targets and effect state that the targets of a packet did not use, and the
 * Gtao state (box_gpu_gtao_sweep).
 *
 * @param gpu The backend
 * @param packet The packet that rendered
 */
void box_gpu_effects_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Create the layouts of the Gtao passes and the white image of views without Gtao.
 *
 * @param gpu The backend
 */
void box_gpu_gtao_init(
    box_gpu_t *gpu);

/** Release the pipelines, layouts and images of Gtao.
 *
 * @param gpu The backend
 */
void box_gpu_gtao_fini(
    box_gpu_t *gpu);

/** Get the 1x1 white R32Float image bound as the ambient occlusion of a view without Gtao.
 *
 * @param gpu The backend
 * @return The view of the image
 */
WGPUTextureView box_gpu_gtao_white(
    box_gpu_t *gpu);

/** Get the ambient occlusion image a view binds for its forward pass, created or resized to the
 * render size, when the view applies Gtao (box_render_view_gtao_t) and its shader is ready.
 *
 * Call it only for a view that draws its depth prepass; box_gpu_gtao_record then fills the image
 * between the last prepass and the forward pass.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param v The view
 * @return The view of the image, NULL when the view does not apply Gtao this frame
 */
WGPUTextureView box_gpu_gtao_begin(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v);

/** Record the Gtao passes of a view from the depth of its prepass: the prefilter into the depth
 * mips, the main pass, the denoise passes and at half resolution the bilateral upsample into the
 * image box_gpu_gtao_begin returned.
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param v The view, for which box_gpu_gtao_begin returned an image this frame
 */
void box_gpu_gtao_record(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v);

/** Release the Gtao images of the views the targets of a packet did not render with Gtao.
 *
 * @param gpu The backend
 * @param packet The packet that rendered
 */
void box_gpu_gtao_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Create the bind group layout, sampler and white fallback of the opaque snapshot of
 * transmission.
 *
 * @param gpu The backend
 */
void box_gpu_transmission_init(
    box_gpu_t *gpu);

/** Release the pipelines, layout, sampler and white fallback of the opaque snapshot.
 *
 * @param gpu The backend
 */
void box_gpu_transmission_fini(
    box_gpu_t *gpu);

/** Get the opaque snapshot a view copies its image into, when it has a transmissive draw.
 *
 * It has the render size of the target and the format of the color target of the view. The
 * target keeps one per format (BOX_GPU_SNAPSHOT_FORMATS), each made again only when the render
 * size or its own format changes, so views of both formats do not recreate them every frame.
 *
 * A color target that cannot be copied gets none, logged once.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param slot Slot of the view in the view buffer, whose cull view lists its draws
 * @param source Texture of the color attachment of the view
 * @param format Format of the color attachment of the view
 * @return The snapshot, NULL for none
 */
box_gpu_snapshot_t* box_gpu_transmission_begin(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    int32_t slot,
    WGPUTexture source,
    WGPUTextureFormat format);

/** Get the view of a snapshot which group 0 of the forward pass binds.
 *
 * Every mip once the downsample pipeline of its format exists, else only mip 0: until
 * etc/shaders/transmission.wgsl compiles a rough shape samples the sharp image, not mips that
 * were never blurred.
 *
 * @param gpu The backend
 * @param s The snapshot, NULL for the white fallback
 * @return The view
 */
WGPUTextureView box_gpu_transmission_view(
    box_gpu_t *gpu,
    const box_gpu_snapshot_t *s);

/** Get the depth copy of a snapshot which group 0 of the forward pass binds
 * (opaque_snapshot_depth of mesh.wgsl).
 *
 * @param gpu The backend
 * @param s The snapshot, NULL for the 1x1 depth placeholder
 * @return The view
 */
WGPUTextureView box_gpu_transmission_depth_view(
    box_gpu_t *gpu,
    const box_gpu_snapshot_t *s);

/** Snapshots of transmission a view takes at most in a frame (box_gpu_draw_batches); each costs
 * a color copy, the blur pyramid and a pass split. 4 covers glass over three layers of glass or
 * blended shapes, and beyond it later glass samples the last snapshot. */
#define BOX_GPU_TRANSMISSION_SNAPSHOTS (4)

/** Copy the color attachment of a view into mip 0 of its snapshot and blur every mip above
 * from the one below (a Jimenez 13 tap downsample), and copy its depth attachment into the
 * depth copy of the snapshot. Called between two render passes, once per snapshot of the view.
 *
 * Until the downsample pipeline exists only mip 0 is written, which is all the view of the
 * snapshot shows then (box_gpu_transmission_view).
 *
 * @param gpu The backend
 * @param s The snapshot
 * @param encoder The encoder of the frame
 * @param source Texture of the color attachment, at least as large as the snapshot
 * @param depth Texture of the depth attachment (CopySrc), the size of the snapshot, NULL to
 * keep the depth copy of the previous snapshot of the frame: no blended or transmissive draw
 * writes depth
 */
void box_gpu_transmission_record(
    box_gpu_t *gpu,
    box_gpu_snapshot_t *s,
    WGPUCommandEncoder encoder,
    WGPUTexture source,
    WGPUTexture depth);

/** Release the snapshots of a target, of every format.
 *
 * @param t The target
 */
void box_gpu_transmission_release(
    box_gpu_target_t *t);

/** Create the area texture of SMAA from the data of the reference: 160 x 560 RG8Unorm.
 *
 * @param gpu The backend
 * @return The texture, released by the caller
 */
WGPUTexture box_gpu_smaa_area(
    box_gpu_t *gpu);

/** Create the search texture of SMAA from the data of the reference: 64 x 16 R8Unorm.
 *
 * @param gpu The backend
 * @return The texture, released by the caller
 */
WGPUTexture box_gpu_smaa_search(
    box_gpu_t *gpu);

/** Create the layouts of the scatter pass.
 *
 * @param gpu The backend
 */
void box_gpu_scatter_init(
    box_gpu_t *gpu);

/** Release the pipeline and layouts of the scatter pass.
 *
 * @param gpu The backend
 */
void box_gpu_scatter_fini(
    box_gpu_t *gpu);

/** Run the scatters of a packet, after its uploads and before its targets render.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_scatter(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Copy a range of a buffer into memory, waiting for the copy.
 *
 * @param gpu The backend
 * @param buffer The buffer, with CopySrc usage
 * @param offset Byte offset in the buffer, a multiple of 4
 * @param size Bytes to copy, a multiple of 4
 * @param out Receives the bytes
 * @return Whether the bytes were copied
 */
bool box_gpu_read(
    box_gpu_t *gpu,
    WGPUBuffer buffer,
    uint32_t offset,
    uint32_t size,
    void *out);

/** Create the layouts of the cull pass and the layout that binds instance buffers.
 *
 * @param gpu The backend
 */
void box_gpu_cull_init(
    box_gpu_t *gpu);

/** Release the pipelines, layouts and buffers of the cull pass.
 *
 * @param gpu The backend
 */
void box_gpu_cull_fini(
    box_gpu_t *gpu);

/** Create the bind group layouts of the instance pages.
 *
 * instance_layout binds the transform stream (0), the surface assets of the mesh (1), the shading
 * stream (2), the surface stream (3) and the material table (4); transform_layout every binding
 * but the shading stream and the material table. Only the transform stream is visible to compute.
 *
 * A plain mesh page and a 2D page have no surface stream and bind gpu->no_surfaces at 3.
 *
 * @param gpu The backend
 */
void box_gpu_instance_layouts_init(
    box_gpu_t *gpu);

/** Release the bind group layouts of the instance pages.
 *
 * @param gpu The backend
 */
void box_gpu_instance_layouts_fini(
    box_gpu_t *gpu);

/** Get the bind group of an instance page for the draws of a mesh, made on first use.
 *
 * The draws of every mesh without surface assets share one group per page, so a pass sets it
 * once per page; a mesh with surface assets has a group of its own per page.
 *
 * The groups live with the transform stream of the page and are released when any stream of the
 * page is created again or destroyed, and every group when the material table is.
 *
 * @param gpu The backend
 * @param page Handle of the transform stream of the page, box_render_batch_t.instances
 * @param handle Handle of the mesh
 * @param m The mesh, NULL for a pass that reads no surface assets, such as the cull
 * @param shading Bind the shading stream too (instance_layout), else transform_layout
 * @return The bind group, NULL when a stream of the page is missing or not a storage buffer
 */
WGPUBindGroup box_gpu_instance_group(
    box_gpu_t *gpu,
    uint32_t page,
    uint32_t handle,
    const box_gpu_mesh_t *m,
    bool shading);

/** Forget the cull views of the previous target, before the views of a target are written.
 *
 * Also maps the counter copies of earlier targets, which have been submitted by now, publishes
 * the ones that arrived and releases the histories and pyramids nothing used for a while.
 *
 * @param gpu The backend
 * @param packet The packet the target belongs to
 */
void box_gpu_cull_begin(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Add the frustums of a view: the view itself and every cascade of its shadows.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param index Index of the view in the packet
 * @param slot Slot of the view in the view buffer
 * @param proj Projection of the view for the target
 * @param u Uniforms of the view, with its view projection and cascade matrices
 */
void box_gpu_cull_add(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    mat4 proj,
    const box_gpu_view_uniforms_t *u);

/** Add the tiles of a shadow atlas as cull groups of shadow casters of its scene, at most
 * BOX_GPU_CULL_GROUP_VIEWS tiles per group; tile i is cascade i of the slot.
 *
 * A spot light tile culls by the frustum of its matrix, a point light tile by the 6 planes of
 * the box around the sphere of its range, as the multi view cull only tests planes.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param scene Scene of the casters
 * @param slot Slot that identifies the cull views of the atlas
 * @param tiles The tiles, whose matrices give the frustums
 * @param renders Whether each tile renders, NULL for every tile; a tile that does not is left
 * out of the cull, so it has no cull view
 * @param count Tiles
 */
void box_gpu_cull_casters_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    int32_t slot,
    const box_render_shadow_tile_t *tiles,
    const bool *renders,
    int32_t count);

/** Count the bits of a mask.
 *
 * @param mask The mask
 * @return Its set bits
 */
uint32_t box_gpu_cull_bits(
    uint32_t mask);

/** Append a block of cull uniforms, padded to the dynamic offset alignment.
 *
 * @param c The cull state
 * @param data The uniforms
 * @param size Bytes of the uniforms
 * @return Byte offset of the block in the uniform buffer
 */
uint32_t box_gpu_cull_uniform(
    box_gpu_cull_t *c,
    const void *data,
    uint32_t size);

/** Write the uniforms, jobs and draw arguments of the cull views of a target.
 *
 * @param gpu The backend
 */
void box_gpu_cull_upload(
    box_gpu_t *gpu);

/** Forget the cull views, before the cull views of a batch of probe captures are added.
 *
 * @param gpu The backend
 */
void box_gpu_cull_reset(
    box_gpu_t *gpu);

/** Add a cull view of probe captures with a frustum, the cascade of their sun.
 *
 * It draws the batches of the scene with bake set, the shadow casters among them for a
 * cascade, tested against the frustum and, with pixels, the smallest projected size.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param scene Scene of the captures
 * @param slot Slot that identifies the cull view
 * @param cascade 0 for the cascade, -1 for a face
 * @param view_proj World to clip space of the face or cascade
 * @param pixels Pixels per unit at w 1 of a face, 0 to test the frustum only
 */
void box_gpu_cull_capture_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    int32_t slot,
    int32_t cascade,
    mat4 view_proj,
    float pixels);

/** Check whether a probe capture draws a batch.
 *
 * @param gpu The backend
 * @param b The batch
 * @param scene Scene of the captures
 * @param cascade Cascade of the cull view, -1 for a face
 * @return Whether the batch has opaque static meshes of the scene the captures draw, and for
 * the cascade casts shadows, and the backend has its mesh and storage instance buffer
 */
bool box_gpu_cull_capture_draws(
    box_gpu_t *gpu,
    const box_render_batch_t *b,
    ecs_entity_t scene,
    int32_t cascade);

/** Record the dispatches of every cull view in one compute pass.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param writes Timestamps the compute pass writes, NULL for none
 * @return Whether the pass was recorded
 */
bool box_gpu_cull_record_all(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const WGPUPassTimestampWrites *writes);

/** Record the cull dispatches of a view and its cascades that run before the view draws.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param slot Slot of the view in the view buffer
 */
void box_gpu_cull_record(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    int32_t slot);

/** Check whether a view culls by occlusion and draws in two phases.
 *
 * @param gpu The backend
 * @param slot Slot of the view in the view buffer
 * @return Whether the view has a late phase
 */
bool box_gpu_cull_occludes(
    box_gpu_t *gpu,
    int32_t slot);

/** Record the late phase of a view: its pyramid built from the depth of the early draw, then
 * the dispatches that test what the early draw left out against it.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param depth The depth the early draw wrote
 * @param slot Slot of the view in the view buffer
 */
void box_gpu_cull_record_late(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    WGPUTextureView depth,
    int32_t slot);

/** Copy the counters of the views of a target for the status of their culling.
 *
 * @param gpu The backend
 * @param encoder The encoder of the target, after its views
 */
void box_gpu_cull_finish(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder);

/** Create the layouts, pipelines layouts and empty bindings of the depth pyramid.
 *
 * @param gpu The backend
 */
void box_gpu_hiz_init(
    box_gpu_t *gpu);

/** Release the pyramids, histories, pipelines and layouts of the occlusion culling.
 *
 * @param gpu The backend
 */
void box_gpu_hiz_fini(
    box_gpu_t *gpu);

/** Request the pyramid pipelines from the module of the cull shader.
 *
 * @param gpu The backend
 * @param module The cull shader
 * @param next Receives the first and reduce pipelines, NULL while one is created
 * @param current Receives the fields they replace
 */
void box_gpu_hiz_pipelines(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    WGPUComputePipeline *next,
    WGPUComputePipeline **current);

/** Get the pyramid of a render size, created on first use.
 *
 * @param gpu The backend
 * @param width Render width in pixels
 * @param height Render height in pixels
 * @return The pyramid
 */
box_gpu_hiz_t* box_gpu_hiz_get(
    box_gpu_t *gpu,
    int32_t width,
    int32_t height);

/** Get the history of a view in a target, created on first use.
 *
 * @param gpu The backend
 * @param view Entity of the view
 * @param target Handle of the target
 * @return The history
 */
box_gpu_cull_history_t* box_gpu_hiz_history(
    box_gpu_t *gpu,
    ecs_entity_t view,
    uint32_t target);

/** Give every instance page a view draws opaque batches from a range of its history.
 *
 * When the ranges do not fit, the history starts over in a larger buffer, as if the view drew
 * every instance the previous frame.
 *
 * @param gpu The backend
 * @param h The history
 * @param packet The packet
 * @param index Index of the view in the packet
 */
void box_gpu_hiz_ranges(
    box_gpu_t *gpu,
    box_gpu_cull_history_t *h,
    const box_render_packet_t *packet,
    int32_t index);

/** Get the first history word of an instance page in its history page.
 *
 * @param h The history
 * @param buffer Handle of the instance page
 * @return The offset, UINT32_MAX when the buffer has no range
 */
uint32_t box_gpu_hiz_offset(
    const box_gpu_cull_history_t *h,
    uint32_t buffer);

/** Get the history page of the history words of an instance page.
 *
 * @param h The history
 * @param buffer Handle of the instance page
 * @return The page, 0 when the instance page has no range
 */
uint32_t box_gpu_hiz_page(
    const box_gpu_cull_history_t *h,
    uint32_t buffer);

/** Get the bind group of a history page and a pyramid, created again when either changed.
 *
 * @param gpu The backend
 * @param h The history
 * @param hiz The pyramid
 * @param page The page of the history, the first one when it has none
 * @return The bind group
 */
WGPUBindGroup box_gpu_hiz_group(
    box_gpu_t *gpu,
    box_gpu_cull_history_t *h,
    box_gpu_hiz_t *hiz,
    uint32_t page);

/** Record the reduction of a depth texture into every level of a pyramid.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param hiz The pyramid
 * @param depth The depth texture
 */
void box_gpu_hiz_build(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    box_gpu_hiz_t *hiz,
    WGPUTextureView depth);

/** Release the histories and pyramids that no cull pass used for a while.
 *
 * @param gpu The backend
 */
void box_gpu_hiz_purge(
    box_gpu_t *gpu);

/** Get the cull view of a view or of one of its cascades.
 *
 * @param gpu The backend
 * @param slot Slot of the view in the view buffer
 * @param cascade Cascade, -1 for the view itself
 * @return The cull view, NULL when the view does not draw
 */
const box_gpu_cull_view_t* box_gpu_cull_view(
    box_gpu_t *gpu,
    int32_t slot,
    int32_t cascade);

/** Record the indirect draw of a batch of a cull view.
 *
 * Binds the mesh, the instance page at group unless it is bound there already, the visible
 * slots of the draw and draws with the arguments the cull pass wrote.
 *
 * @param gpu The backend
 * @param pass The render pass
 * @param packet The packet
 * @param draw The draw
 * @param group Bind group index the instance page is bound at
 * @param late Draw what the late phase kept instead of the early or only phase
 * @param shading Bind the shading stream of the page too, for a pipeline with instance_layout
 * @param bound Instance bind group bound last in the pass, NULL for none, updated
 * @return Whether the draw was recorded
 */
bool box_gpu_cull_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_gpu_cull_draw_t *draw,
    uint32_t group,
    bool late,
    bool shading,
    WGPUBindGroup *bound);

/** Record the wire draw of a batch of a cull view (debug_wire.wgsl): the indirect draw of
 * box_gpu_cull_draw with its arguments unchanged, the shared sequence 0, 1, 2, ... as the index
 * buffer and the visible slots at vertex slot 0; the shader loads index and vertex itself.
 *
 * The arguments have a firstIndex, baseVertex and firstInstance of 0 (gpu_cull_group.c), so
 * vertex_index runs over 0 to index_count - 1; a debug build asserts it. Binds the depth only instance group at group;
 * the caller binds the group of the mesh with its vertices and indices.
 *
 * @param gpu The backend
 * @param pass The render pass
 * @param packet The packet
 * @param draw The draw
 * @param group Bind group index the instance page is bound at
 * @param late Draw what the late phase kept instead of the early or only phase
 * @param sequence Index buffer of the sequence, at least index_count of the mesh long
 * @param bound Instance bind group bound last in the pass, NULL for none, updated
 * @return Whether the draw was recorded
 */
bool box_gpu_cull_draw_wire(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_gpu_cull_draw_t *draw,
    uint32_t group,
    bool late,
    WGPUBuffer sequence,
    WGPUBindGroup *bound);

/** Record a copy of a presented image into the readback buffer of its target.
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param texture The presented image
 */
void box_gpu_capture_encode(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    WGPUTexture texture);

/** Map the copy the frame recorded without waiting for it; the map callback writes the PNG
 * file once the GPU finished the copy, as wgpu's map_async does for Bevy's screenshots.
 *
 * @param gpu The backend
 * @param t The target
 * @param path The file, written next to it first and then renamed over it
 */
void box_gpu_capture_write(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const char *path);

/** Release the idle readback buffer of a target. Captures of the target that are still
 * mapping complete on their own.
 *
 * @param t The target
 */
void box_gpu_capture_fini(
    box_gpu_target_t *t);

/** Block on the device until every capture that is mapping wrote its file, the flush callback
 * of the backend; captures a wait does not complete are abandoned, so none outlives the device.
 * The browser maps from its event loop, so there it returns at once.
 *
 * @param ctx The backend
 */
void box_gpu_capture_flush(
    void *ctx);

/** Check whether the capture of a file is still mapping, so the file may still be written.
 *
 * @param gpu The backend
 * @param path The file
 * @return Whether a capture of the file is mapping
 */
bool box_gpu_capture_pending(
    box_gpu_t *gpu,
    const char *path);

#endif

#endif
