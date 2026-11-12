/**
 * @file render/gpu_cull.h
 * @brief GPU culling and hierarchical depth of the webgpu backend.
 *
 * Included by gpu.h. The cull pass tests the instances of a view and its shadow cascades against
 * their frusta, size and depth pyramid (gpu_hiz.c) and compacts the visible ones into indirect
 * draws.
 */

#ifndef BOX_RENDER_GPU_CULL_H
#define BOX_RENDER_GPU_CULL_H

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

/** Add the frustums of a view: the view itself and every cascade of its shadows that renders in
 * the packet (box_gpu_shadow_renders), a kept cascade culls nothing.
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

/** Add the cull view of a planar reflection of a view, a group of its own: the 3D meshes of the
 * scene of the view and of the view itself that are not transmissive, but the shape of the plane.
 *
 * They are tested against the mirrored frustum and the smallest size of the view, not by occlusion.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param index Index of the view in the packet
 * @param slot Slot of the reflection in the view buffer, which identifies its cull view
 * @param view_proj World to clip space of the reflection, with its oblique near plane
 * @param pixels Pixels per unit at w 1 of the reflection image
 * @param exclude_page Handle of the instance page of the shape of the plane, 0 for none
 * @param exclude_slot Slot of that shape in the page
 */
void box_gpu_cull_reflect_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    mat4 view_proj,
    float pixels,
    uint32_t exclude_page,
    uint32_t exclude_slot);

/** Add the tiles of a shadow atlas as cull groups of shadow casters of its scene, at most
 * BOX_GPU_CULL_GROUP_VIEWS frustums per group; octant o of tile i is cascade
 * BOX_GPU_ATLAS_FRUSTUM(i, o) of the slot, a spot light tile only having octant 0.
 *
 * A spot light tile culls by the frustum of its matrix, each octant of a point light tile by
 * the 6 planes of its part of the box around the sphere of its range
 * (box_render_shadow_octant_planes), as the multi view cull only tests planes.
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
 * the dispatches that test what the early draw left out against it, which a mesh draw with a
 * history gets in its late arguments and appended to its early ones.
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
 * vertex_index runs over 0 to index_count - 1. Binds the depth only instance group at group; the
 * caller binds the group of the mesh with its vertices and indices.
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

#endif
