/**
 * @file render/render_backend.h
 * @brief Backend statistics, readbacks and test hooks of the null and GPU render backends.
 *
 * Included by render.h; the functions are safe to call from the main thread.
 */

#ifndef BOX_RENDER_BACKEND_H
#define BOX_RENDER_BACKEND_H

#include "render_packet.h"

/** Packets in a row without staged copies after which the GPU backend releases the staging
 * buffer of the copies, and the packets after which it shrinks that buffer to what the largest
 * staged copies of a packet among them needed */
#define BOX_RENDER_COPY_STAGING_FRAMES (60)

/** Counters of the GPU backend, written on the render thread */
typedef struct box_render_gpu_stats_t {
    bool ready;                 /**< The device exists */
    bool failed;                /**< Creating the device failed */
    int32_t targets;            /**< Targets alive */
    int32_t created;            /**< Targets created */
    int32_t configured;         /**< Times a target was configured, creation included */
    int32_t destroyed;          /**< Targets released */
    int32_t frames;             /**< Target frames rendered */
    int32_t captures;           /**< PNG files written */
    int32_t errors;             /**< Device errors and failed captures */
    int32_t width;              /**< Width of the last configured target */
    int32_t height;             /**< Height of the last configured target */
    int32_t render_width;       /**< Render width of the last configured target */
    int32_t render_height;      /**< Render height of the last configured target */
    int32_t shaders;            /**< Shader modules alive */
    int32_t compiles;           /**< Shader modules compiled */
    int32_t pipelines;          /**< Render and compute pipelines created */
    int32_t pipelines_building; /**< Pipelines whose creation is in progress in the background,
                                 *   until their outcome is delivered at the start of a packet */
    int32_t shader_errors;      /**< Shader modules that failed to compile */
    int32_t meshes;             /**< Meshes alive */
    int32_t buffers;            /**< Buffers alive */
    int32_t passes;             /**< View passes rendered */
    int32_t draws;              /**< Draw calls recorded, those of the depth prepasses
                                 *   excluded */
    int32_t prepasses;          /**< Depth prepass render passes recorded, of the lit views
                                 *   that draw opaque meshes: one per frame, two for a view
                                 *   that culls by occlusion */
    int32_t prepass_draws;      /**< Draw calls recorded in the depth prepasses */
    int32_t debug_passes;       /**< Debug render passes recorded, one per frame of each view
                                 *   that draws a debug item (View.debug), after its effects;
                                 *   their draw calls count among draws */
    int32_t fog_volumes;        /**< Fog volumes drawn, one per volume of a view in each frame
                                 *   it renders; their draw calls count among draws */
    int32_t star_fields;        /**< Star fields drawn, one per StarField of a view in each
                                 *   frame it renders; their draw calls count among draws */
    int32_t transmission_snapshots; /**< Snapshots recorded for transmission, per frame of a
                                 *   view one before its first transmissive draw and one before
                                 *   each later one that overlaps the draws since the last one,
                                 *   at most 4 */
    int32_t transmission_textures; /**< Snapshot textures created for transmission, one per
                                 *   color format of the views of a target that draw a
                                 *   Transmission shape, again only when its render size changes */
    int32_t forward_passes;     /**< Forward render passes begun for the views: one per frame,
                                 *   two (early and late) for a view that culls by occlusion
                                 *   without a depth prepass.
                                 *
                                 *   Plus a split after each transmission snapshot, one that
                                 *   reads the depth for fog volumes and soft particles and one
                                 *   more when draws follow those.
                                 *
                                 *   Not the depth prepasses, snapshot downsamples, shadow, cull,
                                 *   atmosphere, particle simulation or effect passes. */
    int32_t instance_binds;     /**< Instance bind groups set in the cull, depth prepass,
                                 *   forward, shadow and capture passes, only when the page or
                                 *   surface assets differ from the ones bound last in the pass */
    int32_t cull_dispatches;    /**< Compute dispatches the cull passes of views recorded:
                                 *   cull_instances, cull_ordered, cull_scan and cull_write,
                                 *   not the pyramid reductions nor the probe capture culls */
    int32_t cull_instance_dispatches; /**< cull_instances dispatches among cull_dispatches */
    int32_t cull_page_slots;    /**< Most visible slots one cull page held in the last
                                 *   published work that culled, at most the cull window
                                 *   unless one draw needs more */
    int32_t scatters;           /**< Scatter dispatches recorded */
    int32_t scattered;          /**< Instances the scatter dispatches copied */
    int32_t probes;             /**< Probes captured */
    int64_t cull_ns;            /**< GPU time of the cull passes of views read back, 0 without
                                 *   timestamp queries */
    int32_t cull_passes;        /**< Cull passes whose GPU time cull_ns holds */
    int32_t texture_layers;     /**< maxTextureArrayLayers of the device, 0 before it exists */
    int32_t atmosphere_luts;    /**< Atmospheres whose transmittance and multi scattering LUTs
                                 *   exist */
    int32_t atmosphere_views;   /**< Views and targets whose sky view and aerial perspective
                                 *   exist */
    int32_t atmosphere_builds;  /**< Times the LUTs of an atmosphere were built */
    int64_t atmosphere_ns[BOX_RENDER_ATMOS_PASSES]; /**< GPU time of each atmosphere pass read
                                 *   back, by box_render_atmos_pass_t, 0 without timestamp
                                 *   queries */
    int32_t atmosphere_passes[BOX_RENDER_ATMOS_PASSES]; /**< Passes whose GPU time
                                 *   atmosphere_ns holds */
    int32_t shadow_tiles;       /**< Shadow atlas tiles rendered */
    int32_t shadow_reused;      /**< Shadow atlas tiles kept from an earlier frame instead of
                                 *   rendered */
    int32_t shadow_passes;      /**< Shadow atlas render passes recorded, none in a frame that
                                 *   renders no tile */
    int32_t shadow_draws;       /**< Draw calls recorded in the shadow passes of the cascades
                                 *   and the atlas tiles, not those of the probe captures; a
                                 *   point light tile draws each caster draw once per octant */
    int32_t shadow_cascades;    /**< Cascades of views rendered */
    int32_t shadow_cascades_kept; /**< Cascades of views kept from an earlier frame instead of
                                 *   rendered, sampled with the matrix they rendered with */
    int32_t copies_direct;      /**< Copies between buffers (box_render_copy_t) that went from
                                 *   their source buffer straight into their destination */
    int32_t copies_staged;      /**< Copies between buffers that went through the staging
                                 *   buffer: within one buffer, or reading a range another copy
                                 *   of their packet writes */
    int64_t copy_staging;       /**< Bytes of the staging buffer of the copies now, 0 while it
                                 *   is released (BOX_RENDER_COPY_STAGING_FRAMES) */
    int64_t copy_staging_peak;  /**< Most bytes copy_staging held at a publish */
    bool clip_distances;        /**< The device has the clip distances feature, which clips the
                                 *   octants of the point light tiles */
    int64_t blend_bytes;        /**< Bytes of the texture arrays of the TextureBlend layers now,
                                 *   with their mip levels */
    int32_t blend_arrays;       /**< Texture arrays of the TextureBlend layers alive */
    int32_t blend_builds;       /**< Times such an array was built */
    int32_t cluster_resizes;    /**< Times the cluster buffer of a view was created again with
                                 *   another size, its first creation included */
    int64_t cluster_entries;    /**< Light and probe index entries the cluster buffers of the
                                 *   views hold together now */
    int32_t texture_graphs;     /**< Texture graphs alive */
    int32_t texture_graph_programs; /**< Generated stage programs alive, shared by the graphs
                                 *   of one structure */
    int32_t texture_graph_evaluations; /**< Evaluations of texture graphs recorded */
    int32_t texture_graph_dispatches; /**< Stage dispatches of texture graphs recorded */
    int64_t texture_graph_texels; /**< Texels the stage dispatches computed */
    int64_t texture_graph_ns;   /**< GPU time of the texture graph evaluations read back, 0
                                 *   without timestamp queries */
    int32_t texture_graph_timed; /**< Packets of evaluations whose GPU time texture_graph_ns
                                 *   holds */
    int64_t texture_graph_pool_bytes; /**< Bytes of the pooled intermediate textures now */
    int64_t texture_graph_key_bytes; /**< Bytes of the key images of the interpolated texture
                                 *   graphs now */
    int32_t texture_graph_blends; /**< Crossfades of key images of interpolated texture graphs
                                 *   recorded */
    int32_t texture_graph_reads; /**< Texture graph readbacks delivered */
    int32_t texture_graph_hits; /**< Stages of texture graphs whose value the cache had, so
                                 *   they were not dispatched; not the stages of an animated
                                 *   update that kept the values the graph held */
    int32_t texture_graph_misses; /**< Stages of texture graphs whose value the cache did not
                                 *   have, dispatched into a new entry */
    int32_t texture_graph_evictions; /**< Cached values evicted to keep the budget */
    int32_t texture_graph_updates; /**< Evaluations of animated texture graphs that only
                                 *   updated them over time, also counted in
                                 *   texture_graph_evaluations */
    int64_t texture_graph_cached_bytes; /**< Bytes of the cached values now, in use or not */
    int32_t texture_graph_cached; /**< Cached values now */
} box_render_gpu_stats_t;

/** GPU time of the probe captures the GPU backend measured since the last time it was taken */
typedef struct box_render_bake_timing_t {
    int32_t probes;             /**< Probes the measured capture batches held */
    double ms;                  /**< Milliseconds the GPU spent on them */
    bool measured;              /**< The device measures with timestamp queries */
} box_render_bake_timing_t;

/** A probe the GPU backend captured, reported back to the main thread */
typedef struct box_render_bake_result_t {
    uint32_t volume;            /**< Handle of the volume */
    uint32_t generation;        /**< Generation of the bake batch of the probe */
    int32_t pass;               /**< Pass of the bake batch of the probe */
    uint32_t probe;             /**< Index of the probe in the volume */
} box_render_bake_result_t;

/** Take the probes the GPU backend of a world captured since the last call.
 *
 * A probe of a packet that was rendered and is not among them was not captured.
 *
 * @param world The world
 * @param out Receives box_render_bake_result_t
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_bake_captured(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Take the GPU time of the probe captures the GPU backend of a world measured.
 *
 * Timestamps are read back without waiting, so a measurement arrives a few frames after its
 * captures. Without timestamp queries the time from recording until the queue reports the work
 * done is measured on the CPU instead, which includes waits and errs on the expensive side.
 *
 * @param world The world
 * @param out Receives the time, zero when nothing arrived
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_bake_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out);

/** Take the reflection probes the GPU backend of a world captured since the last call.
 *
 * Each result has the texture handle as volume, the ticket of the probe as generation and its
 * slot as probe. A probe of a packet that was rendered and is not among them was not captured.
 *
 * @param world The world
 * @param out Receives box_render_bake_result_t
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_reflect_captured(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Take the GPU time of the reflection captures the GPU backend of a world measured, as
 * box_render_gpu_bake_timing does for the probe captures.
 *
 * @param world The world
 * @param out Receives the time, zero when nothing arrived
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_reflect_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out);

/** Copy mip 0 of one face of a reflection probe cubemap into memory, waiting for the copy.
 *
 * Texel (x, y) of the face, y down, follows the WebGPU cubemap convention. Only call this when
 * the main thread renders, after box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param texture Handle of the BOX_RENDER_REFLECTION
 * @param slot Cubemap slot
 * @param face Face, 0 to 5 for +x, -x, +y, -y, +z, -z
 * @param out Receives resolution squared linear RGBA texels, four floats each
 * @param count Floats out holds
 * @return Whether the texture exists and the texels were copied
 */
bool box_render_gpu_read_reflection(
    ecs_world_t *world,
    uint32_t texture,
    uint32_t slot,
    uint32_t face,
    float *out,
    int32_t count);

/** Copy the image of a planar reflection of a view into memory, waiting for the copy, for
 * tests. Texel (x, y), y down, lines up with the screen of the view.
 *
 * Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_read_buffer. A view rendered by several targets has one image per target;
 * the first one found is read.
 *
 * @param world The world
 * @param view The view entity
 * @param slot Slot of the plane in the view, 0 for the nearest
 * @param width Receives the width of the image, NULL for none
 * @param height Receives the height of the image, NULL for none
 * @param out Receives width times height linear RGBA texels, four floats each, NULL to only
 * check the image exists and get its size
 * @param count Floats out holds
 * @return Whether the image exists and, with out, its texels were copied
 */
bool box_render_gpu_read_planar(
    ecs_world_t *world,
    ecs_entity_t view,
    int32_t slot,
    int32_t *width,
    int32_t *height,
    float *out,
    int32_t count);

/** Copy the L2 SH irradiance of a reflection probe cubemap slot into memory, waiting for the
 * copy.
 *
 * The coefficients E_lm = A_l * L_lm of the capture are in the basis and order of mesh.wgsl, as
 * the probes of a volume; a slot gets them with its prefiltered mips. Only call this when the
 * main thread renders, after box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param texture Handle of the BOX_RENDER_REFLECTION
 * @param slot Cubemap slot
 * @param out Receives the linear rgb coefficients, w unused
 * @return Whether the texture exists and the coefficients were copied
 */
bool box_render_gpu_read_reflection_sh(
    ecs_world_t *world,
    uint32_t texture,
    uint32_t slot,
    float out[BOX_RENDER_SH_COEFFICIENTS][4]);

/** Copy the L2 SH irradiance of a probe of a probe buffer of a volume into memory, waiting for
 * the copy. Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param volume Handle of the BOX_RENDER_VOLUME
 * @param buffer Probe buffer, 0 or 1
 * @param probe Index of the probe
 * @param out Receives the linear rgb coefficients, w unused
 * @return Whether the volume has the probe and the coefficients were copied
 */
bool box_render_gpu_read_volume_sh(
    ecs_world_t *world,
    uint32_t volume,
    uint32_t buffer,
    uint32_t probe,
    float out[BOX_RENDER_SH_COEFFICIENTS][4]);

/** What the cull pass of a view tested and kept in the last target the GPU backend rendered */
typedef struct box_render_cull_stats_t {
    int32_t tested;             /**< Instances the view tested */
    int32_t visible;            /**< Instances the view draws */
    int32_t casters_tested;     /**< Shadow casters tested, summed over the cascades */
    int32_t casters_visible;    /**< Shadow casters drawn, summed over the cascades */
    int32_t skipped;            /**< Batches the view did not draw by their bounds
                                 *   (CullSettings.bounds), whose instances count as tested */
    int32_t casters_skipped;    /**< Batches the cascades did not draw by their bounds, summed */
    int64_t indices;            /**< Indices the view draws: the index count of the mesh of the
                                 *   level each visible instance draws at, summed */
    int64_t casters_indices;    /**< Indices the cascades draw, summed */
    int32_t levels[BOX_LOD_LEVELS_MAX]; /**< Visible instances the view draws at each level of
                                 *   detail (LodSettings), all at level 0 without levels */
    int32_t casters_levels[BOX_LOD_LEVELS_MAX]; /**< Casters drawn at each level, summed over
                                 *   the cascades */
} box_render_cull_stats_t;

/** The culling of a view the GPU backend read back, for its CullStatus */
typedef struct box_render_cull_status_t {
    ecs_entity_t view;          /**< Entity with the View */
    CullStatus status;          /**< The counts */
} box_render_cull_status_t;

/** Packet ring between the main thread and the consumer of the packets */
typedef struct box_render_ring_t box_render_ring_t;

/** Description of a BOX_RENDER_TEXTURE_NODE_KIND create, followed by the name of its WGSL
 * function and its WGSL source, each null terminated; its handle is its TextureNodeKind.id.
 *
 * Kinds are sent before the graphs that use them, and again to a restarted backend.
 */
typedef struct box_render_texture_node_kind_t {
    uint32_t output;            /**< TextureValueType of the value of a node */
    uint32_t input_count;       /**< Inputs of a node */
    uint32_t input_types[BOX_TEXTURE_NODE_INPUTS]; /**< TextureValueType of each input */
    uint32_t coords_inputs;     /**< Bit j: input j without a node is the domain coordinates */
    uint32_t param_size;        /**< Bytes of the parameters of a node, a multiple of 16 */
    uint32_t flags;             /**< BOX_TEXTURE_NODE_NEIGHBOURHOOD, _ANIMATED, _TIME_SCALE,
                                 *   _PLANE, _SAMPLES */
    uint32_t passes;            /**< Passes of the function, from 1 */
    uint32_t function_size;     /**< Bytes of the function name, its terminator included */
    uint32_t source_size;       /**< Bytes of the source, its terminator included */
} box_render_texture_node_kind_t;

/** A node of a compiled texture graph, which comes after the nodes of its inputs */
typedef struct box_render_texture_graph_node_t {
    uint32_t kind;              /**< Id of its BOX_RENDER_TEXTURE_NODE_KIND */
    uint32_t output;            /**< TextureValueType of its value */
    int32_t inputs[BOX_TEXTURE_NODE_INPUTS]; /**< Index of the node of each input, -1 for the
                                 *   constant of the input in its parameters */
    uint32_t params;            /**< Offset of its bytes in the parameter block: the parameters
                                 *   of its kind, then a vec4 per input, the constant of an
                                 *   input without a node (zero without one) */
    uint32_t param_size;        /**< Bytes of the node in the parameter block */
    int32_t texture;            /**< Index of the texture a node of a sampling kind samples in
                                 *   the textures of the graph, -1 for none */
} box_render_texture_graph_node_t;

/** Flag of a texture of a compiled texture graph whose color channels read as stored, not
 * decoded from display encoding */
#define BOX_RENDER_TEXTURE_GRAPH_LINEAR (1u << 0)

/** A texture a compiled texture graph samples. Which image it is does not belong to the
 * structure of the graph, so another one only changes the bindings of its stages. */
typedef struct box_render_texture_graph_texture_t {
    uint32_t handle;            /**< Texture handle of the image, BOX_RENDER_TEXTURE_TARGET with
                                 *   a target handle for the image of a surface, 0 for none */
    uint32_t flags;             /**< BOX_RENDER_TEXTURE_GRAPH_LINEAR */
} box_render_texture_graph_texture_t;

/** Description of a BOX_RENDER_TEXTURE_GRAPH create, followed by node_count nodes in
 * topological order, the output last, then param_size bytes of parameters, then texture_count
 * textures.
 *
 * The handle is the texture handle the image goes to. A change of the parameters only keeps
 * structure_hash, so the backend keeps the pipeline it evaluates the graph with.
 */
typedef struct box_render_texture_graph_t {
    uint32_t width;             /**< Width of the image */
    uint32_t height;            /**< Height of the image */
    uint32_t domain;            /**< TextureGraphDomain of the coordinates */
    uint32_t format;            /**< TextureGraphFormat of the image */
    uint32_t node_count;        /**< Nodes after the description */
    uint32_t param_size;        /**< Parameter bytes after the nodes */
    uint64_t structure_hash;    /**< Hash of the kinds and edges of the nodes */
    uint64_t param_hash;        /**< Hash of the parameter bytes */
    uint32_t serial;            /**< Graphs sent for the texture, which the outcome of the
                                 *   evaluation names (box_render_texture_graph_result_t) */
    float rate;                 /**< Updates per second of an animated graph, 0 for every
                                 *   frame (TextureGraph.rate) */
    uint32_t texture_count;     /**< Textures after the parameters */
    uint32_t interpolate;       /**< 1 to crossfade keyframes of an animated graph with a rate
                                 *   (TextureGraph.interpolate) */
} box_render_texture_graph_t;

/** Get the nodes of a compiled texture graph.
 *
 * @param graph The description
 * @return Its node_count nodes
 */
const box_render_texture_graph_node_t* box_render_texture_graph_nodes(
    const box_render_texture_graph_t *graph);

/** Get the parameter bytes of a compiled texture graph.
 *
 * @param graph The description
 * @return Its param_size bytes
 */
const uint8_t* box_render_texture_graph_params(
    const box_render_texture_graph_t *graph);

/** Get the textures a compiled texture graph samples.
 *
 * @param graph The description
 * @return Its texture_count textures
 */
const box_render_texture_graph_texture_t* box_render_texture_graph_textures(
    const box_render_texture_graph_t *graph);

/** Get the name of the WGSL function of a node kind.
 *
 * @param kind The description
 * @return The null terminated name
 */
const char* box_render_texture_node_kind_function(
    const box_render_texture_node_kind_t *kind);

/** Get the WGSL source of a node kind.
 *
 * @param kind The description
 * @return The null terminated source
 */
const char* box_render_texture_node_kind_source(
    const box_render_texture_node_kind_t *kind);

/** Get the last texture graph a null backend received for a texture handle, for tests; call
 * after box_render_sync.
 *
 * @param null The null backend
 * @param handle The texture handle
 * @return The description followed by its nodes and parameters, NULL when it has none
 */
const box_render_texture_graph_t* box_render_null_texture_graph(
    const box_render_null_t *null,
    uint32_t handle);

/** Get the last description of a node kind a null backend received, for tests; call after
 * box_render_sync.
 *
 * @param null The null backend
 * @param id The id of the kind
 * @return The description followed by its function name and source, NULL when it has none
 */
const box_render_texture_node_kind_t* box_render_null_texture_node_kind(
    const box_render_null_t *null,
    uint32_t id);

/** Outcome of the first evaluation of a graph the GPU backend received, or why it failed */
typedef struct box_render_texture_graph_result_t {
    uint32_t handle;            /**< Texture handle of the graph */
    uint32_t serial;            /**< Serial of the graph (box_render_texture_graph_t) */
    int32_t node;               /**< Index of the node an error came from, -1 for none */
    char *error;                /**< Why the graph failed, NULL when its image was written,
                                 *   freed by the caller */
} box_render_texture_graph_result_t;

/** Take the outcomes of the texture graphs the GPU backend of a world evaluated or failed since
 * the last call, oldest first.
 *
 * @param world The world
 * @param out Receives box_render_texture_graph_result_t
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_texture_graph_results(
    ecs_world_t *world,
    ecs_vec_t *out);

/** A readback of a texture graph that the GPU backend delivered */
typedef struct box_render_texture_read_t {
    ecs_entity_t texture;       /**< Texture entity, set by box_render_texture_graph_reads */
    ecs_entity_t node;          /**< Node entity read, 0 for the image; set by
                                 *   box_render_texture_graph_reads */
    uint32_t handle;            /**< Texture handle of the graph */
    int32_t index;              /**< Index of the node read in the compiled graph, -1 for the
                                 *   image */
    uint32_t ticket;            /**< Ticket of the request */
    int32_t width;              /**< Width of the texels */
    int32_t height;             /**< Height of the texels */
    float *texels;              /**< Four floats per texel from the top row, NULL when failed,
                                 *   freed by the caller: the image as stored (R8 as (r, 0, 0,
                                 *   1)), a scalar node as (x, 0, 0, 1), a vector one in halves */
} box_render_texture_read_t;

/** Request a readback of the image of a texture graph of the GPU backend of a world, or of the
 * value of one of its nodes, which the graph then keeps materialized in a texture.
 *
 * The backend copies it after the next evaluation that has it, or at once from the image of a
 * graph that is evaluated, and delivers it once the copy mapped, unless
 * box_render_gpu_readback_delivery holds. The output node reads as the image.
 *
 * @param world The world
 * @param handle Texture handle of the graph
 * @param node Index of the node in the compiled graph, -1 for the image
 * @param ticket Number the delivered readback carries
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_texture_graph_read(
    ecs_world_t *world,
    uint32_t handle,
    int32_t node,
    uint32_t ticket);

/** Take the texture graph readbacks the GPU backend of a world delivered since the last call.
 *
 * @param world The world
 * @param out Receives box_render_texture_read_t, without texture and node entities
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_texture_graph_reads(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Request a readback of the image of the TextureGraph of a texture entity, or of the value of
 * one of its nodes, see box_render_gpu_texture_graph_read.
 *
 * @param world The world
 * @param texture The texture entity
 * @param node The node entity, 0 for the image
 * @param ticket Number the delivered readback carries
 * @return Whether the request was made: the world renders with the GPU backend, the graph of
 * the texture was sent to it and the node is part of it
 */
bool box_render_texture_graph_read(
    ecs_world_t *world,
    ecs_entity_t texture,
    ecs_entity_t node,
    uint32_t ticket);

/** Take the texture graph readbacks delivered since the last call, with their texture and node
 * entities.
 *
 * @param world The world
 * @param out Receives box_render_texture_read_t
 * @return Whether the world renders with the GPU backend
 */
bool box_render_texture_graph_reads(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Create a null backend.
 *
 * @return The backend state, freed by box_render_null_free
 */
box_render_null_t* box_render_null_new(void);

/** Free a null backend.
 *
 * @param ctx The backend state
 */
void box_render_null_free(
    void *ctx);

/** Get the backend interface of a null backend that the caller keeps ownership of.
 *
 * @param null The backend state
 * @return The backend interface, with a NULL free
 */
box_render_backend_t box_render_null_backend(
    box_render_null_t *null);

/** Create the backend that renders with webgpu, available when built with BOX_RENDER_WGPU.
 *
 * The device is created by init on the thread that renders and released by fini.
 *
 * @return The backend, which frees its state with its free callback
 */
box_render_backend_t box_render_gpu_backend(void);

/** Read the counters of the GPU backend of a world.
 *
 * @param world The world
 * @param stats The counters
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_stats(
    ecs_world_t *world,
    box_render_gpu_stats_t *stats);

/** Check whether the GPU backend of a world may still write the capture of a file: its packet
 * rendered and the readback of the image is still mapping.
 *
 * @param world The world
 * @param path The file
 * @return Whether a capture of the file is mapping
 */
bool box_render_gpu_capture_pending(
    ecs_world_t *world,
    const char *path);

/** Destroy the device of the GPU backend of a world before its next packet, which loses it the
 * way a driver reset or a removed GPU does, to test the recovery.
 *
 * @param world The world
 */
void box_render_gpu_lose_device(
    ecs_world_t *world);

/** When the GPU backend applies the outcome of a shader compile or a pipeline creation.
 *
 * Natively a compile outcome arrives during the compile, in the browser after the frame; tests
 * pick a delayed delivery to run the asynchronous path of the browser natively. Pipelines are
 * delivered at the start of a packet, except while held.
 */
typedef enum box_render_compile_delivery_t {
    BOX_RENDER_COMPILE_AT_ONCE = 0,     /**< During the compile, the native default */
    BOX_RENDER_COMPILE_HOLD = 1,        /**< Held until the delivery changes */
    BOX_RENDER_COMPILE_NEXT_PACKET = 2, /**< At the start of the next packet, oldest first */
    BOX_RENDER_COMPILE_NEWEST_FIRST = 3 /**< At the start of the next packet, newest first */
} box_render_compile_delivery_t;

/** Set when the GPU backend of a world applies shader compile and pipeline outcomes, for tests.
 *
 * Outcomes held when the delivery returns to BOX_RENDER_COMPILE_AT_ONCE are applied at the
 * start of the next packet, oldest first. A held pipeline is not drawn with until then.
 *
 * @param world The world
 * @param delivery When outcomes are applied
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_compile_delivery(
    ecs_world_t *world,
    box_render_compile_delivery_t delivery);

/** When the GPU backend publishes the cull counters it read back.
 *
 * Copies finish mapping in any order; tests hold them and publish them newest first to make
 * an older frame arrive after a newer one.
 */
typedef enum box_render_readback_delivery_t {
    BOX_RENDER_READBACK_AT_ONCE = 0,     /**< Every mapped copy, in slot order, the default */
    BOX_RENDER_READBACK_HOLD = 1,        /**< Mapped copies wait until the delivery changes */
    BOX_RENDER_READBACK_NEWEST_FIRST = 2, /**< Every mapped copy, newest frame first */
    BOX_RENDER_READBACK_ONE_NEWEST = 3   /**< Only the newest mapped copy per frame */
} box_render_readback_delivery_t;

/** Set the slots one page of the cull pass holds, for tests.
 *
 * The visible slots of the draws of a target and the occlusion history of a view are split
 * over pages of at most this many 4 byte words, each bound whole as storage; the default is
 * the 128 MiB storage binding every WebGPU device supports. Applies from the next cull pass.
 *
 * @param world The world
 * @param slots Words per page, at least 1
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_cull_window(
    ecs_world_t *world,
    uint32_t slots);

/** Add copies of the frustum of cascade 0 to the cull of every view with shadows, for tests.
 *
 * A copy is culled like a cascade, as cascade cascades + i of the view, and nothing draws it;
 * box_render_gpu_cull_frustum reads it. It makes a view cull more frustums than one cull group
 * holds (BOX_GPU_CULL_GROUP_VIEWS). Applies from the next cull pass.
 *
 * @param world The world
 * @param copies Copies per view, 0 for none
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_cull_copies(
    ecs_world_t *world,
    int32_t copies);

/** Make the GPU backend of a world render the octants of the point light tiles the core
 * WebGPU way, discarding the fragments outside an octant, even when the device has clip
 * distances, for tests. Applies from the next packet.
 *
 * @param world The world
 * @param core Whether to discard instead of clipping
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_shadow_core(
    ecs_world_t *world,
    bool core);

/** Set when the GPU backend of a world publishes cull counters, for tests.
 *
 * @param world The world
 * @param delivery When and in which order mapped copies are published
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_readback_delivery(
    ecs_world_t *world,
    box_render_readback_delivery_t delivery);

/** Take the oldest shader compile result the GPU backend of a world has not handed out yet.
 *
 * @param world The world
 * @param result Receives the result, its message is released with ecs_os_free
 * @return Whether there was a result
 */
bool box_render_gpu_shader_result(
    ecs_world_t *world,
    box_render_shader_result_t *result);

/** Get the backend of a world.
 *
 * @param world The world
 * @return The backend, valid until the backend is replaced
 */
const box_render_backend_t* box_render_get_backend(
    const ecs_world_t *world);

/** Render with the GPU backend unless the world was given a backend of its own.
 *
 * The box.surface module calls this when a surface is set, so a world without surfaces never
 * creates a device. Without BOX_RENDER_WGPU the null backend stays.
 *
 * @param world The world
 */
void box_render_use_gpu(
    ecs_world_t *world);

/** Copy bytes of a buffer of the GPU backend of a world into memory, waiting for the copy.
 *
 * Only call this when the main thread renders (RenderSettings.main_thread), after
 * box_render_sync, so the device is used by one thread; a call from another thread than the
 * one that renders asserts, and returns false when asserts are compiled out.
 *
 * @param world The world
 * @param handle Handle of the buffer
 * @param offset Byte offset in the buffer, a multiple of 4
 * @param size Bytes to copy, a multiple of 4
 * @param out Receives the bytes
 * @return Whether the backend is the GPU backend and the bytes were copied
 */
bool box_render_gpu_read_buffer(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t offset,
    uint32_t size,
    void *out);

/** Get the bytes of a buffer the GPU backend of a world created for a handle.
 *
 * Only call this when the main thread renders (RenderSettings.main_thread), after
 * box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param handle Handle of the buffer
 * @return The bytes, 0 when the backend has no buffer for the handle or is not the GPU backend
 */
uint64_t box_render_gpu_buffer_size(
    ecs_world_t *world,
    uint32_t handle);

/** Read back what the cull pass of a view kept in the last target the GPU backend rendered.
 *
 * Only call this when the main thread renders (RenderSettings.main_thread), after
 * box_render_sync, so the device is used by one thread; a call from another thread than the
 * one that renders asserts, and returns false when asserts are compiled out.
 *
 * @param world The world
 * @param view Entity with the View
 * @param out Receives the counts
 * @return Whether the last rendered target culled for the view
 */
bool box_render_gpu_cull_stats(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_cull_stats_t *out);

/** Read back what one frustum of the cull pass of a view kept in the last target the GPU
 * backend rendered: the view itself or one of its cascades.
 *
 * Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_cull_stats.
 *
 * @param world The world
 * @param view Entity with the View
 * @param cascade Cascade, -1 for the view itself
 * @param out Receives the counts in tested and visible
 * @return Whether the last rendered target culled the frustum
 */
bool box_render_gpu_cull_frustum(
    ecs_world_t *world,
    ecs_entity_t view,
    int32_t cascade,
    box_render_cull_stats_t *out);

/** Read back what the cull pass kept of the shadow casters of one tile of the shadow atlases of
 * the last packet the GPU backend rendered.
 *
 * Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_cull_stats.
 *
 * @param world The world
 * @param tile Index of the tile in the shadow tiles of the packet
 * @param out Receives the casters tested and drawn in tested and visible, summed over the
 * octants of a point light tile
 * @return Whether the last packet culled the tile, false for a tile it kept instead of rendering
 */
bool box_render_gpu_cull_tile(
    ecs_world_t *world,
    int32_t tile,
    box_render_cull_stats_t *out);

/** Take the culling status of the views the GPU backend read back since the last call.
 *
 * @param world The world
 * @param out Receives box_render_cull_status_t, one per view, the latest counts
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_cull_status(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Register the system that writes the CullStatus of the views.
 *
 * @param world The world
 */
void box_render_cull_status_init(
    ecs_world_t *world);

#endif
