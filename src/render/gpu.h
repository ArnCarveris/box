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

/** Report the error of a pipeline that failed to create as the outcome of the shader whose
 * installed module it was created from, so its ShaderStatus shows the error. A pipeline of a
 * module that is no longer installed is not reported.
 *
 * @param gpu The backend
 * @param module The module of the first stage of the pipeline
 * @param message Error text
 */
void box_gpu_shader_pipeline_failed(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    const char *message);

/** Finish the compiles whose outcome box_render_gpu_compile_delivery held.
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

/** Get the texture of a handle that a texture graph writes, creating it with storage binding
 * (unless R8Unorm) and a full mip chain (one level for R32Float and RGBA32Float, which are not
 * filterable) when the handle has none of the size and format; a replaced image of the handle
 * is released.
 *
 * An RGBA8Unorm texture has an sRGB view, which the color layers sample. Its levels below the
 * first are generated once box_gpu_texture_output_written queued them.
 *
 * @param gpu The backend
 * @param handle Handle of the texture
 * @param width Width of the first level
 * @param height Height of the first level
 * @param format RGBA8Unorm, RGBA16Float, R8Unorm, R32Float or RGBA32Float
 * @param color The texture holds display encoded color, whose levels average linear light
 * @return The texture
 */
box_gpu_texture_t* box_gpu_texture_output(
    box_gpu_t *gpu,
    uint32_t handle,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format,
    bool color);

/** Record a render pass that writes the first level of the texture of a handle from a view of
 * the same size through the blit pipeline, for a format that cannot be a storage texture.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param handle Handle of the texture, made by box_gpu_texture_output
 * @param src The view sampled, filterable
 * @return Whether the pass was recorded, false while the blit pipeline does not exist
 */
bool box_gpu_texture_output_blit(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    uint32_t handle,
    WGPUTextureView src);

/** Queue the levels below the first of the texture of a handle whose first level was written,
 * so box_gpu_texture_mips generates them before the passes of the packet sample it.
 *
 * @param gpu The backend
 * @param handle Handle of the texture
 */
void box_gpu_texture_output_written(
    box_gpu_t *gpu,
    uint32_t handle);

/** Get the serial of the image of a texture handle, which changes with every image written to
 * it and is unique across textures.
 *
 * @param gpu The backend
 * @param handle Handle of the texture, without BOX_RENDER_TEXTURE_TARGET
 * @return The serial, 0 when the handle has no texture
 */
uint32_t box_gpu_texture_serial(
    box_gpu_t *gpu,
    uint32_t handle);

/** Get the view a texture graph samples the image of a handle with: the output of a target,
 * or the texture of the handle decoded from display encoding or as stored.
 *
 * @param gpu The backend
 * @param handle Handle of the texture, with BOX_RENDER_TEXTURE_TARGET for a target
 * @param linear Read the color channels as stored
 * @return The view of every level, NULL when the handle has no image
 */
WGPUTextureView box_gpu_texture_sampled_view(
    box_gpu_t *gpu,
    uint32_t handle,
    bool linear);

/** Create, replace or release a node kind of the texture graphs from a
 * BOX_RENDER_TEXTURE_NODE_KIND resource.
 *
 * @param gpu The backend
 * @param op What happens to the kind
 * @param handle Id of the kind
 * @param desc The kind followed by its function name and source, NULL for a release
 * @param size Bytes of desc
 */
void box_gpu_texture_node_kind_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_texture_node_kind_t *desc,
    uint32_t size);

/** Create, replace or release a texture graph from a BOX_RENDER_TEXTURE_GRAPH resource. A
 * graph of a new structure compiles its stages; until they exist its texture keeps the image
 * it has.
 *
 * @param gpu The backend
 * @param op What happens to the graph
 * @param handle Texture handle of the graph
 * @param desc The graph followed by its nodes and parameters, NULL for a release
 * @param size Bytes of desc
 */
void box_gpu_texture_graph_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_texture_graph_t *desc,
    uint32_t size);

/** Take the shared code of the texture graph stages (BOX_RENDER_SHADER_TEXTURE_GRAPH), which is
 * not compiled alone; the stages are generated again with new code.
 *
 * @param gpu The backend
 * @param op What happens to the code
 * @param desc The code, NULL for a release
 */
void box_gpu_texture_graph_prelude(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    const box_render_shader_desc_t *desc);

/** Learn the outcome of the compile or of the pipeline of a generated texture graph stage.
 *
 * @param gpu The backend
 * @param handle Handle of the stage, from BOX_GPU_TEXTURE_GRAPH_SHADER_FIRST
 * @param message Error text, NULL when it compiled
 */
void box_gpu_texture_graph_compiled(
    box_gpu_t *gpu,
    uint32_t handle,
    const char *message);

/** Evaluate the texture graphs whose stages exist and whose image is out of date or, animated,
 * due an update at the time of the packet, deliver their outcomes and readbacks and evict cached
 * values over the budget.
 *
 * It runs after the resources of a packet applied and before the mip levels are generated, so
 * the passes of the packet sample the new images.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_texture_graphs_evaluate(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Note that a draw of the current packet samples a texture, so its texture graph, when it is
 * animated, updates in the next packet.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 or a target handle for none
 */
void box_gpu_texture_graph_sampled(
    box_gpu_t *gpu,
    uint32_t handle);

/** Release the texture graphs, their programs, intermediates and cached values.
 *
 * @param gpu The backend
 */
void box_gpu_texture_graphs_fini(
    box_gpu_t *gpu);

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
 * @return The view, NULL when the backend has no image of the handle or it is float32
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

/** Binding of the DrawKind of mesh.wgsl in the material and blend groups, a uniform with a
 * dynamic offset */
#define BOX_GPU_DRAW_KIND_BINDING (11)

/** Binding of the foam texture of a water batch in the material group (foam_map of
 * mesh_extended.wgsl), white for a batch without one */
#define BOX_GPU_MATERIAL_FOAM_BINDING (12)

/** Bytes between two DrawKinds in draw_kinds, the largest uniform offset alignment */
#define BOX_GPU_DRAW_KIND_STRIDE (256)

/** DrawKinds in draw_kinds: the BlendMode in the low 4 bits, then transmissive, then the alpha
 * test */
#define BOX_GPU_DRAW_KINDS (64)

/** Get the dynamic offset of the DrawKind a batch draws with in the material or blend group.
 *
 * @param b The batch
 * @param equal Whether the draw follows a depth prepass of its view, which a batch the prepass
 * draws does without the alpha test
 * @return The offset in draw_kinds
 */
uint32_t box_gpu_draw_kind(
    const box_render_batch_t *b,
    bool equal);

/** Compute the box around the vertex positions of a mesh.
 *
 * @param m The mesh
 * @param vertices The vertices
 * @param count Number of vertices
 */
void box_gpu_mesh_bounds(
    box_gpu_mesh_t *m,
    const box_vertex_t *vertices,
    uint32_t count);

/** Check whether a batch draws with the per vertex data variants of the mesh pipelines: a
 * batch of a mesh shader whose mesh has per vertex data (box_gpu_mesh_t.data_size).
 *
 * Its forward pipelines read the data stream in every vertex stage (the _data entry points),
 * its alpha tested depth prepass and shadow pipelines too; the plain depth prepass and shadow
 * pipelines do not read it. Its draws bind the data stream in vertex buffer slot 2.
 *
 * @param gpu The backend
 * @param b The batch
 * @return Whether its mesh has per vertex data
 */
bool box_gpu_vertex_data(
    const box_gpu_t *gpu,
    const box_render_batch_t *b);

/** Vertex buffer layout of the per vertex data stream of the _data entry points of the mesh
 * and shadow shaders: data0 at location 4 and data1 at location 5 (VertexData).
 *
 * @param attrs Receives the two attributes the layout points to
 * @return The layout
 */
WGPUVertexBufferLayout box_gpu_vertex_data_layout(
    WGPUVertexAttribute *attrs);

/** Check whether a batch draws with the layers variants of the mesh pipelines: an opaque,
 * alpha tested or blended batch of a mesh shader with TextureLayers, not transmissive, or a
 * water batch with TextureLayers (fs_water_layers).
 *
 * Its forward and depth prepass pipelines have the layers layouts and bind the group of
 * box_gpu_forward_group; its shadows bind the group without layers.
 *
 * @param b The batch
 * @return Whether it applies texture layers
 */
bool box_gpu_layered(
    const box_render_batch_t *b);

/** Get the bind group the forward and depth prepass pipelines of a batch bind in group 1: the
 * layers group of a batch with box_gpu_layered, else box_gpu_material_group.
 *
 * The layers group binds the entries of the material or blend group of the batch, the texture
 * array of the layer maps and the uniform buffer of each layer entity, which it writes when the
 * values of the layer changed, shared by every batch with the layer.
 *
 * @param gpu The backend
 * @param packet The packet of the batch
 * @param b The batch
 * @return The bind group
 */
WGPUBindGroup box_gpu_forward_group(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b);

/** Get the pipeline layout of the layers variants of the mesh pipelines, made on first use.
 *
 * @param gpu The backend
 * @param blend Whether the variant blends the layers of a TextureBlend
 * @return The layout
 */
WGPUPipelineLayout box_gpu_layer_pipeline_layout(
    box_gpu_t *gpu,
    bool blend);

/** Release the layers groups, buffers and layouts.
 *
 * @param gpu The backend
 */
void box_gpu_layer_layouts_fini(
    box_gpu_t *gpu);

/** Check whether a batch draws with the texture_blend variant of the mesh pipeline: a batch of
 * the mesh shader with a TextureBlend, of any blend mode, alpha tested or transmissive.
 *
 * Its alpha tested depth prepass and shadow pipelines bind the blend group too.
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

/** Bytes of the ZH3 of one probe of a light probe volume */
#define BOX_GPU_VOLUME_PROBE_SIZE (BOX_RENDER_ZH3_VECTORS * 4 * sizeof(float))

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
 * The cache grows with the states in use; a recompiled or released shader empties it, and a
 * batch whose shader does not exist draws nothing. A TextureBlend batch gets the texture_blend
 * variant (fs_blend with blend_mesh_layout) in every pass it draws in.
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
 * Group 0 binds 10 sampled textures to the fragment stage, the planar reflection array (binding
 * 21) the last: with the 5 of the material group the mesh pipelines sample 15 of the 16 a device
 * has at least, the TextureBlend ones all 16, the particle and fog volume pipelines fewer.
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

/** Bytes every range of the mesh pool is a multiple of and aligned to */
#define BOX_GPU_MESH_ALIGN (256)

/** Allocate a range of the mesh pool, creating a page when no page has a free range that fits.
 *
 * @param gpu The backend
 * @param size Bytes of the range, rounded up to BOX_GPU_MESH_ALIGN
 * @param page Receives the index of the page
 * @param offset Receives the offset of the range in the page
 * @return Whether the range was allocated
 */
bool box_gpu_mesh_pool_alloc(
    box_gpu_t *gpu,
    uint64_t size,
    int32_t *page,
    uint64_t *offset);

/** Return a range to the free ranges of its page, merging it with the ranges next to it.
 *
 * @param gpu The backend
 * @param page Index of the page
 * @param offset Offset of the range in the page
 * @param size Bytes of the range as allocated
 */
void box_gpu_mesh_pool_free(
    box_gpu_t *gpu,
    int32_t page,
    uint64_t offset,
    uint64_t size);

/** Get the buffer of a page of the mesh pool.
 *
 * @param gpu The backend
 * @param page Index of the page
 * @return The buffer
 */
WGPUBuffer box_gpu_mesh_pool_buffer(
    const box_gpu_t *gpu,
    int32_t page);

/** Release the buffers and free ranges of every page of the mesh pool.
 *
 * @param gpu The backend
 */
void box_gpu_mesh_pool_fini(
    box_gpu_t *gpu);

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

/** Copy the heights of the terrain tiles of a packet into the surface assets of their grid
 * meshes (box_render_terrain_height_t): the assets of a mesh grow to hold its highest slot, and
 * a slot is copied when the image of its texture changed since it was copied there.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_terrain_heights(
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

/** Record the overdraw count of a view: its depth prepass again with the early arguments, which
 * hold the late ones too, into a depth buffer of its own cleared to 0 with compare Greater.
 *
 * A fragment that passes the depth and alpha tests adds one to red (plain) or green (cutout).
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 * @param count The count target, cleared to 0
 * @param format Format of the count target, with at least a red and a green channel
 * @param depth Depth target of the count, of BOX_GPU_DEPTH_FORMAT and the size of the count
 * @param width Width of the view in the count
 * @param height Height of the view in the count
 * @return Whether the pass was recorded, false without a mesh shader
 */
bool box_gpu_draw_overdraw(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot,
    WGPUTextureView count,
    WGPUTextureFormat format,
    WGPUTextureView depth,
    uint32_t width,
    uint32_t height);

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

/** Start the cascades of the views of a target: forget the cascades of the views of the last
 * target and release the cascade stores of the views no packet rendered for
 * BOX_GPU_CASCADE_STORE_FRAMES frames.
 *
 * @param gpu The backend
 * @param frame Frame of the packet
 */
void box_gpu_shadow_begin(
    box_gpu_t *gpu,
    int64_t frame);

/** Fit the cascades of a view to its projection and write the shadow members of its uniforms.
 *
 * A view that keeps cascades between frames (box_render_view_shadows_t.reuse) fits only the
 * cascades that render in the packet: those of its render mask and those its cascade store does
 * not hold, every one when the store was (re)created, the projection changed or the shadow
 * shader did. The others keep the cascade they rendered with, so they are sampled with it.
 *
 * @param gpu The backend
 * @param v The view
 * @param proj Projection of the view for its target
 * @param slot Slot of the view in the view buffer
 * @param frame Frame of the packet
 * @param u The uniforms
 */
void box_gpu_shadow_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    mat4 proj,
    int32_t slot,
    int64_t frame,
    box_gpu_view_uniforms_t *u);

/** Get the cascades of a view that render in the packet, after box_gpu_shadow_uniforms.
 *
 * @param gpu The backend
 * @param slot Slot of the view in the view buffer
 * @return Mask of the cascades, bit i for cascade i, every bit for a slot it did not write
 */
uint32_t box_gpu_shadow_renders(
    box_gpu_t *gpu,
    int32_t slot);

/** Write the cascade matrices of the views of a target into the cascade buffer.
 *
 * @param gpu The backend
 * @param count Views of the target
 */
void box_gpu_shadow_upload(
    box_gpu_t *gpu,
    int32_t count);

/** Record the shadow passes of a view: its casters drawn into every cascade that renders in
 * the packet, the others keep their depth.
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
 * renders the casters can wait for them (box_gpu_pipelines_building), with those the casters of
 * the packet with a TextureBlend or a parametric surface draw with.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_shadow_request(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Get the shadow map a view samples.
 *
 * @param gpu The backend
 * @param v The view
 * @return The array view of the shadow map: of its cascade store for a view that keeps
 * cascades, else the shared one, the empty one for a view without shadows
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
 * While the cull pipelines don't exist yet (still compiling) the casters aren't culled and draw
 * nothing: the tiles that render are then left empty and the atlas doesn't keep them, so they
 * render again once the cull pipelines exist.
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

/** Record the integrate and spawn dispatches of the particle pools of a packet, one of each
 * over every pool, after its scatters and before its targets, and keep the draws of the pools
 * for its views.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_particles_simulate(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Create the layouts and placeholders of the fields of WaterRipples.
 *
 * @param gpu The backend
 */
void box_gpu_ripples_init(
    box_gpu_t *gpu);

/** Release the fields of WaterRipples, their pipelines and placeholders.
 *
 * @param gpu The backend
 */
void box_gpu_ripples_fini(
    box_gpu_t *gpu);

/** Step the fields of WaterRipples of a packet, after its scatters and before its targets:
 * create the fields that are new, free the ones the packet no longer has, clear the cells that
 * entered their windows and run their steps.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_ripples_simulate(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Fill the view layout entries of the heights and the foam of the field of WaterRipples.
 *
 * @param entries The entries of the view layout, of which BOX_GPU_RIPPLES_HEIGHTS_BINDING,
 * BOX_GPU_RIPPLES_FOAM_BINDING and BOX_GPU_RIPPLES_FLOW_BINDING are filled
 */
void box_gpu_ripples_layout(
    WGPUBindGroupLayoutEntry *entries);

/** Fill the view group entries of the heights and the foam of the field of WaterRipples of the
 * scene of a view, the 1x1 zero texture without one.
 *
 * @param gpu The backend
 * @param v The view, NULL for none
 * @param entries The entries of the view group, of which BOX_GPU_RIPPLES_HEIGHTS_BINDING,
 * BOX_GPU_RIPPLES_FOAM_BINDING and BOX_GPU_RIPPLES_FLOW_BINDING are filled
 */
void box_gpu_ripples_bindings(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    WGPUBindGroupEntry *entries);

/** Get the velocity texture of the field of WaterRipples of the scene of a view, for the mesh
 * shaders: rgba32float in the addressing of the heights, the world velocity of the water of a
 * cell (xy) and its velocity relative to the frame of the field (zw); the 1x1 zero texture
 * without a field.
 *
 * @param gpu The backend
 * @param v The view, NULL for none
 * @return The view of the texture
 */
WGPUTextureView box_gpu_ripples_flow_view(
    const box_gpu_t *gpu,
    const box_render_view_t *v);

/** Write the record of the field of WaterRipples of the scene of a view into its uniforms, a
 * zero record without one.
 *
 * @param gpu The backend
 * @param v The view
 * @param u The uniforms of the view
 */
void box_gpu_ripples_uniforms(
    const box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u);

/** Split the absolute position of the local origin (WorldOrigin) as the shaders take it (View
 * and Cascade, world_origin of world_origin.wgsl): the whole world units of X, Y and Z, clamped
 * to 32 bits, and in w the rest of a unit of each in BOX_GPU_ORIGIN_REST_BITS bits, X lowest;
 * split in double precision, so a shader computes a world space pattern of the absolute position
 * without losing precision (origin_phase). 0 for an origin of 0.
 *
 * @param origin Absolute position of the local origin
 * @param out Receives the split
 */
void box_gpu_origin_split(
    const double origin[3],
    int32_t out[4]);

/** Bits of the rest of a world unit of each axis of the split origin (box_gpu_origin_split) */
#define BOX_GPU_ORIGIN_REST_BITS (10)

/** Write the absolute position of the local origin into the uniforms of a cascade, tile or
 * octant of the shadow pass at BOX_GPU_SHADOW_ORIGIN (box_gpu_origin_split).
 *
 * @param origin Absolute position of the local origin
 * @param block The uniforms of the cascade
 */
void box_gpu_shadow_origin(
    const double origin[3],
    void *block);

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

/** Record the draws of the particle pools of the scene of a view into its forward pass, one
 * instanced draw per blend and sprite: the alpha ones first, their emitters back to front, then
 * the additive ones. Replaces the pipeline and the bind groups at 1 and 2.
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

/** Get the LUTs of an atmosphere that are built, for a pass that samples them.
 *
 * @param gpu The backend
 * @param handle Handle of the BOX_RENDER_ATMOSPHERE
 * @param transmittance Receives the view of the transmittance LUT
 * @param multi Receives the view of the multi scattering LUT
 * @param sampler Receives the linear clamp sampler of the LUTs
 * @return Whether the LUTs exist and are built with the current shader
 */
bool box_gpu_atmosphere_luts(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUTextureView *transmittance,
    WGPUTextureView *multi,
    WGPUSampler *sampler);

/** Get the transmittance and sky irradiance LUTs of an atmosphere that is built, for a copy of
 * them.
 *
 * @param gpu The backend
 * @param handle Handle of the BOX_RENDER_ATMOSPHERE
 * @param texture Receives the transmittance LUT, BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH by
 * BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT RGBA16Float texels
 * @param irradiance Receives the sky irradiance LUT, BOX_RENDER_ATMOS_IRRADIANCE_WIDTH by
 * BOX_RENDER_ATMOS_IRRADIANCE_HEIGHT RGBA16Float texels
 * @param builds Receives how often the LUTs were built, which changes with every build
 * @return Whether the LUTs exist and are built with the current shader
 */
bool box_gpu_atmosphere_transmittance(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUTexture *texture,
    WGPUTexture *irradiance,
    int32_t *builds);

/** Write the planet light table of a packet, a PlanetLight of planet_light.wgsl per slot, and
 * copy the transmittance and sky irradiance LUTs of each of its planets into its layer of the
 * LUT array when the LUTs were built again or its slot changed, after the LUTs are built and
 * before the targets.
 *
 * A planet whose LUTs are not built leaves its slot empty: its meshes are lit as without it.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_planet_lights_prepare(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Write the planet light bindings of the view group: the table at BOX_GPU_PLANET_LIGHTS_BINDING
 * and the LUT array, a read only storage texture of the transmittance and sky irradiance LUT of
 * each slot, at BOX_GPU_PLANET_LUTS_BINDING; a 1 layer placeholder before any planet.
 *
 * @param gpu The backend
 * @param out Receives the two entries
 */
void box_gpu_planet_lights_bindings(
    box_gpu_t *gpu,
    WGPUBindGroupEntry *out);

/** Create the layouts of the planet atmosphere shell draws, after the view layout.
 *
 * @param gpu The backend
 */
void box_gpu_planets_init(
    box_gpu_t *gpu);

/** Release the pipelines, layouts and buffer of the planet atmosphere shell draws.
 *
 * @param gpu The backend
 */
void box_gpu_planets_fini(
    box_gpu_t *gpu);

/** Start the planet shell draws of a packet, which fill the buffer of the draws from its start.
 *
 * @param gpu The backend
 */
void box_gpu_planets_begin(
    box_gpu_t *gpu);

/** Check whether a view draws planet atmosphere shells: it has records and the shader is
 * compiled, so its forward pass splits around them.
 *
 * @param gpu The backend
 * @param v The view
 * @return Whether the view draws its planet shells in this frame
 */
bool box_gpu_planets_ready(
    box_gpu_t *gpu,
    const box_render_view_t *v);

/** Record the planet atmosphere shells of a view, one draw per planet back to front, skipping
 * a planet whose LUTs are not built. Replaces the pipeline and the bind group at 1.
 *
 * @param gpu The backend
 * @param pass The render pass of the view, which reads its depth, with the bind group of the
 * view at 0
 * @param t The target, whose size the projection follows
 * @param format The format of the color target
 * @param packet The packet, which holds the records
 * @param v The view
 * @param depth The depth of the view, read only in the pass
 * @return Whether a shell was drawn
 */
bool box_gpu_planets_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_gpu_target_t *t,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
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

/** Segment of the rays of a view a fog volume draw marches (FogDraw.segment of
 * fog_volume.wgsl) */
typedef enum box_gpu_fog_segment_t {
    BOX_GPU_FOG_SEGMENT_FULL,       /**< From the camera to the opaque depth */
    BOX_GPU_FOG_SEGMENT_BEHIND,     /**< From the nearest transmissive surface of a pixel to the
                                     *   opaque depth, the full segment where it has none; drawn
                                     *   before the snapshot of transmission */
    BOX_GPU_FOG_SEGMENT_FRONT       /**< From the camera to the nearest transmissive surface of
                                     *   a pixel, nothing where it has none; drawn after the last
                                     *   transmissive draw */
} box_gpu_fog_segment_t;

/** Record the fog volumes of a view, one instanced draw back to front. Replaces the pipeline
 * and the bind group at 1. A draw of the front segment does not count its volumes again.
 *
 * @param gpu The backend
 * @param pass The render pass of the view, which reads its depth, with the bind group of the
 * view at 0
 * @param t The target, whose size the projection follows
 * @param format The format of the color target
 * @param packet The packet, which holds the records
 * @param v The view
 * @param depth The depth of the view, read only in the pass
 * @param glass The depth of the nearest transmissive surfaces over the opaque depth
 * (box_gpu_transmission_glass), NULL to march the full segment
 * @param segment The segment of the rays to march, FULL without glass
 * @return Whether the volumes were drawn
 */
bool box_gpu_fog_volumes_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_gpu_target_t *t,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureView depth,
    WGPUTextureView glass,
    box_gpu_fog_segment_t segment);

/** Create the layouts of the star field draws, after the view layout.
 *
 * @param gpu The backend
 */
void box_gpu_star_fields_init(
    box_gpu_t *gpu);

/** Release the pipelines, layouts and buffer of the star field draws.
 *
 * @param gpu The backend
 */
void box_gpu_star_fields_fini(
    box_gpu_t *gpu);

/** Start the star field draws of a packet, which fill the buffer of the draws from its start.
 *
 * @param gpu The backend
 */
void box_gpu_star_fields_begin(
    box_gpu_t *gpu);

/** Check whether a view draws star fields: it has records and the shader is compiled.
 *
 * @param gpu The backend
 * @param v The view
 * @return Whether the view draws its star fields in this frame
 */
bool box_gpu_star_fields_ready(
    box_gpu_t *gpu,
    const box_render_view_t *v);

/** Record the star fields of a view, one instanced draw of a quad per star for each field, where
 * the depth of the pass is still the clear value. Replaces the pipeline and the bind group at 1.
 *
 * @param gpu The backend
 * @param pass The render pass of the view, with the bind group of the view at 0
 * @param t The target, whose size the projection and the pixels of the stars follow
 * @param format The format of the color target
 * @param packet The packet, which holds the records
 * @param v The view
 * @return Whether the pipeline of the fields was bound, so the pipeline and the group at 1
 * bound before are not
 */
bool box_gpu_star_fields_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_gpu_target_t *t,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v);

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
 * blended shapes; beyond it the first one and the largest of the others are taken
 * (box_gpu_transmission_plan), and the glass in between samples an older snapshot. */
#define BOX_GPU_TRANSMISSION_SNAPSHOTS (4)

/** Plan the snapshots of a view before its draws (box_gpu_draw_batches).
 *
 * Transmissive pipelines do not blend: a draw replaces the image under it with the snapshot it
 * samples, which must hold every draw before it that it can overlap, transmissive ones too.
 *
 * The first transmissive draw takes one. A later one takes one when a draw since the last one
 * overlaps it by their bounds in the image plane (box_render_batch_t.screen) or has none, so
 * runs that do not overlap share one. The particles, drawn at the first blended draw that is
 * not a layer around a planet, cover the view.
 *
 * Past BOX_GPU_TRANSMISSION_SNAPSHOTS the first one is taken and the others go to the draws
 * that cover most of the image, the later of equal ones, so a large shape that encloses the
 * others never samples a stale image; the draws in between sample an older snapshot.
 *
 * @param gpu The backend
 * @param t The target, whose size gives the image of the view
 * @param packet The packet
 * @param v The view
 * @param cv The cull view of the view, NULL for none
 * @param plan Receives the draws the snapshots are taken before, ascending, at most
 * BOX_GPU_TRANSMISSION_SNAPSHOTS
 * @return The snapshots planned
 */
int32_t box_gpu_transmission_plan(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    const box_gpu_cull_view_t *cv,
    int32_t *plan);

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

/** Copy the depth of a view into the glass depth of its snapshot, created on first use, for
 * the depth only pass of its transmissive draws that splits its fog volumes
 * (box_gpu_fog_volumes_draw). Called between two render passes.
 *
 * @param gpu The backend
 * @param s The snapshot of the view
 * @param encoder The encoder of the frame
 * @param depth Texture of the depth attachment of the view (CopySrc), the size of the snapshot
 * @return The view of the glass depth, which the transmissive draws then write
 */
WGPUTextureView box_gpu_transmission_glass(
    box_gpu_t *gpu,
    box_gpu_snapshot_t *s,
    WGPUCommandEncoder encoder,
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

/** Create the layouts of the compose pass.
 *
 * @param gpu The backend
 */
void box_gpu_compose_init(
    box_gpu_t *gpu);

/** Release the pipeline, layouts and buffers of the compose pass.
 *
 * @param gpu The backend
 */
void box_gpu_compose_fini(
    box_gpu_t *gpu);

/** Run the composes of a packet, after its scatters and before its targets render: place the
 * slots of every mover page in the world with the mover table.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_compose(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Release the instance bind groups of a buffer, the transform stream of a page.
 *
 * @param b The buffer
 */
void box_gpu_instance_groups_release(
    box_gpu_buffer_t *b);

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

#include "gpu_cull.h"
#include "gpu_planar.h"

#endif

#endif
