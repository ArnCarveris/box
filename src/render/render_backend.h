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
                                 *   it injects into its froxel grid; the apply draw calls of
                                 *   the grids count among draws */
    int32_t fog_dispatches;     /**< Compute dispatches of the froxel grids of the fog volumes,
                                 *   inject and integrate: two per frame of a view with volumes
                                 *   however many it has */
    int32_t fog_grid[3];        /**< Cells across, cells down and slices of the last froxel
                                 *   grid injected, 0 before the first */
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
    int32_t data_binds;         /**< Per vertex data streams bound in the depth prepass,
                                 *   forward, shadow and capture passes, one per draw of a mesh
                                 *   with data (Mesh3.data0 to data3); never for a mesh without */
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
    int32_t grass_passes;       /**< Compute passes that placed the grass blades of a view,
                                 *   one per frame of a lit view whose scene has a Grass */
    int64_t grass_ns;           /**< GPU time of the grass compute passes read back, 0 without
                                 *   timestamp queries */
    int32_t grass_timed;        /**< Grass compute passes whose GPU time grass_ns holds */
    int64_t grass_generated[BOX_GRASS_LODS]; /**< Blades the grass passes placed in the patches
                                 *   the views saw, per level of detail, read back */
    int64_t grass_drawn[BOX_GRASS_LODS]; /**< Blades of those the views drew per level of
                                 *   detail, read back */
    int32_t grass_reads;        /**< Packets whose blades grass_generated and grass_drawn
                                 *   hold */
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
    int32_t packets;            /**< Packets rendered */
    int32_t pipelines_cached;   /**< Pipelines in the pipeline cache now, built or building */
    int32_t api_render_passes;  /**< Render passes begun through the webgpu API */
    int32_t api_compute_passes; /**< Compute passes begun through the webgpu API */
    int32_t api_pipelines;      /**< Pipelines set in passes and bundles */
    int32_t api_bind_groups;    /**< Bind groups set in passes and bundles */
    int32_t api_buffer_binds;   /**< Vertex and index buffers set in passes and bundles */
    int32_t api_draws;          /**< Draw calls of every kind, direct and indirect */
    int64_t api_triangles;      /**< Vertices of the direct draws times their instances, over
                                 *   3; the triangles of indirect draws are read back */
    int32_t api_dispatches;     /**< Compute dispatches of every kind */
    int64_t api_workgroups;     /**< Workgroups of the direct dispatches */
    int32_t api_copies;         /**< Copies between buffers and textures in command encoders */
    int64_t api_copy_bytes;     /**< Bytes of the copies between buffers */
    int32_t api_writes;         /**< Buffer and texture writes on the queue */
    int64_t api_write_bytes;    /**< Bytes of the writes on the queue */
    int32_t api_submits;        /**< Submits on the queue */
    int32_t api_command_buffers; /**< Command buffers submitted */
    int32_t api_bundles;        /**< Render bundles executed */
    int64_t view_triangles;     /**< Triangles the indirect draws of views drew after culling,
                                 *   read back while box_render_gpu_stats_detail is on */
    int64_t shadow_triangles;   /**< Triangles the indirect draws of shadow cascades drew after
                                 *   culling, read back like view_triangles */
    int32_t triangle_reads;     /**< Frames whose triangles view_triangles and
                                 *   shadow_triangles hold */
    int32_t packet_resources;   /**< Resource creates, updates and destroys of the packets */
    int32_t packet_uploads;     /**< Uploads of the packets */
    int32_t packet_scatters;    /**< Scatters of the packets */
    int32_t packet_copies;      /**< Copies between buffers of the packets */
    int32_t packet_composes;    /**< Composes of the packets */
    int32_t packet_batches;     /**< Batches of the packets */
    int32_t packet_views;       /**< Views of the packets */
    int64_t packet_bytes;       /**< Bytes of data the resources and uploads of the packets
                                 *   point into */
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

/** The window of a field of WaterRipples the backend last stepped */
typedef struct box_render_ripples_read_t {
    int32_t resolution;     /**< Cells per side */
    float cell;             /**< World units of a cell */
    float corner[2];        /**< World XZ of the corner of the window: cell x, z covers
                             *   corner + (x, z) * cell .. corner + (x + 1, z + 1) * cell */
    float height;           /**< World Y of the Water */
} box_render_ripples_read_t;

/** Copy the heights of the field of WaterRipples of a Water into memory, waiting for the copy,
 * in the order of its window: cell x, z of the window at out[z * resolution + x]. Only call this
 * when the main thread renders, after box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param water The Water entity
 * @param info Receives the window, NULL for none
 * @param out Receives resolution squared heights in world units, NULL to only get the window
 * @param count Floats out holds
 * @return Whether the field exists and, with out, its heights were copied
 */
bool box_render_gpu_read_ripples(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count);

/** Copy the foam of the field of WaterRipples of a Water into memory like
 * box_render_gpu_read_ripples copies its heights: cell x, z of the window at
 * out[z * resolution + x], 0..1.
 *
 * @param world The world
 * @param water The Water entity
 * @param info Receives the window, NULL for none
 * @param out Receives resolution squared foam values, NULL to only get the window
 * @param count Floats out holds
 * @return Whether the field exists and, with out, its foam was copied
 */
bool box_render_gpu_read_ripple_foam(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count);

/** Copy the velocity of the water of the field of WaterRipples of a Water into memory like
 * box_render_gpu_read_ripples copies its heights, two floats per cell: the world velocity X and Z
 * of cell x, z of the window at out[(z * resolution + x) * 2], world units per second, as the
 * flow pass last wrote it.
 *
 * @param world The world
 * @param water The Water entity
 * @param info Receives the window, NULL for none
 * @param out Receives twice resolution squared floats, NULL to only get the window
 * @param count Floats out holds
 * @return Whether the field exists and, with out, its velocity was copied
 */
bool box_render_gpu_read_ripple_flow(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count);

/** Copy a cascade of the spectrum of the WaterSpectrum owned by an entity into memory, waiting
 * for the copy, as the backend last evolved it: four floats per texel of its largest mip, the
 * slope along X and Z, the height and the square of the slope of texel x, z at
 * out[(z * resolution + x) * 4]. Texel x, z covers the world XZ
 * (x, z) * size / resolution - offset of the tile of the cascade. Only call this when the main
 * thread renders, after box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param water The entity that owns the WaterSpectrum
 * @param cascade The cascade
 * @param resolution Receives the texels per side, NULL for none
 * @param out Receives four times resolution squared floats, NULL to only get the resolution
 * @param count Floats out holds
 * @return Whether the spectrum and the cascade exist and, with out, it was copied
 */
bool box_render_gpu_read_spectrum(
    ecs_world_t *world,
    ecs_entity_t water,
    int32_t cascade,
    int32_t *resolution,
    float *out,
    int32_t count);

/** Run the inverse FFT of the spectrum shader on a square grid of complex values and copy the
 * result into memory, waiting for it, for tests: two complex values per texel, in[(z *
 * resolution + x) * 4] the first (real, imaginary) and in[... + 2] the second; out receives,
 * per texel at out[(z * resolution + x) * 4], the real and imaginary parts of the transform of
 * the first, the real part of the transform of the second and the sum of the squares of the
 * first two, sum over (u, v) of in(u, v) e^(2 pi i (u x + v z) / resolution), without
 * normalization, rounded to half floats as the slopes of a spectrum are. Needs the spectrum
 * shader, which the backend creates once a packet has a WaterSpectrum. Only call this when the
 * main thread renders, after box_render_sync.
 *
 * @param world The world
 * @param resolution Texels per side, a power of two from
 * BOX_MATERIAL_SPECTRUM_RESOLUTION_MIN to BOX_MATERIAL_SPECTRUM_RESOLUTION_MAX
 * @param in Four times resolution squared floats
 * @param out Receives four times resolution squared floats
 * @return Whether the shader was ready and the result copied
 */
bool box_render_gpu_spectrum_fft(
    ecs_world_t *world,
    int32_t resolution,
    const float *in,
    float *out);

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

/** vec4 of a probe of a BOX_RENDER_VOLUME on the GPU, which stores its irradiance as ZH3
 * (Roughton et al., "ZH3: Quadratic Zonal Harmonics"): (E0, z.r), (E1, z.g), (E2, z.b),
 * (E3, 0), with bands 0 and 1 of the L2 coefficients and z the zonal band 2 coefficient along
 * the axis of the luminance of band 1 */
#define BOX_RENDER_ZH3_VECTORS (4)

/** Length of the luminance of band 1 below which a ZH3 probe takes +z as its axis */
#define BOX_RENDER_ZH3_AXIS_EPSILON (1e-8f)

/** Axis of a ZH3 probe: the normalized luminance (0.2126, 0.7152, 0.0722) of its band 1
 * coefficients E1..E3 in Sloan's order (y, z, x), +z when its length is below
 * BOX_RENDER_ZH3_AXIS_EPSILON, as zh3_axis() of etc/shaders/include/sh.wgsl.
 *
 * @param band1 E1, E2 and E3, linear rgb: 3 rows
 * @param out Receives the axis
 */
void box_render_zh3_axis(
    const float band1[][4],
    float out[3]);

/** Pack L2 SH irradiance into ZH3: bands 0 and 1 as they are, and per channel the projection
 * z = sum_m E_2m y_m of band 2 onto y_m = sqrt(4 pi / 5) Y_2m(axis).
 *
 * @param sh The L2 coefficients, linear rgb, w unused
 * @param out Receives the ZH3 probe
 */
void box_render_sh_to_zh3(
    const float sh[BOX_RENDER_SH_COEFFICIENTS][4],
    float out[BOX_RENDER_ZH3_VECTORS][4]);

/** Expand a ZH3 probe into L2 SH irradiance: bands 0 and 1 as stored, band 2 as z y_m along
 * the axis of band 1.
 *
 * @param zh3 The ZH3 probe
 * @param out Receives the L2 coefficients, w 0
 */
void box_render_zh3_to_sh(
    const float zh3[BOX_RENDER_ZH3_VECTORS][4],
    float out[BOX_RENDER_SH_COEFFICIENTS][4]);

/** Copy the L2 SH irradiance of a probe of a probe buffer of a volume into memory, waiting for
 * the copy: the ZH3 the probe is stored as, expanded by box_render_zh3_to_sh. Only call this
 * when the main thread renders, after box_render_sync, as box_render_gpu_read_buffer.
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

/** Get the bytes of each of the two probe buffers of a volume on the GPU,
 * BOX_RENDER_ZH3_VECTORS vec4 per probe. Only call this when the main thread renders, after
 * box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param volume Handle of the BOX_RENDER_VOLUME
 * @param out Receives the bytes of a probe buffer
 * @return Whether the volume exists
 */
bool box_render_gpu_volume_bytes(
    ecs_world_t *world,
    uint32_t volume,
    uint64_t *out);

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
    uint32_t window;            /**< Offset of the window elements of a node of a kind with a
                                 *   window function on DomainWorld in the parameter block,
                                 *   after the bytes of every node; 0 for none */
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
    float region[4];            /**< Longitude start and end, latitude start and end of the
                                 *   image on the sphere in radians, resolved (whole sphere
                                 *   (0, 2 pi, -pi / 2, pi / 2) by default), 0 on the plane */
    double origin[2];           /**< World x and z of the first texel on DomainWorld, 0 else */
    float spacing[2];           /**< Meters between neighbouring texels along x and z on
                                 *   DomainWorld, 0 else */
    uint32_t apron;             /**< Texels the stages before the output grow the image by on
                                 *   each side on DomainWorld for the reach of the neighbourhood
                                 *   nodes, 0 else */
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

/** Turn on or off the counters of the GPU backend of a world that cost GPU work: the triangles
 * the indirect draws drew, which the read back of the cull counters then also copies.
 *
 * Applies from the next cull pass; off by default.
 *
 * @param world The world
 * @param detail Whether the costly counters are on
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_stats_detail(
    ecs_world_t *world,
    bool detail);

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

/** A grass blade the GPU backend placed, read back for tests */
typedef struct box_render_grass_blade_t {
    float position[3];          /**< Local world position of its base in the packet that
                                 *   placed it */
    float yaw;                  /**< Angle of its facing around the vertical in radians */
    float height;               /**< Height in meters, shrunk where it fades out */
    float width;                /**< Width at its base in meters, widened with distance and
                                 *   to about a pixel */
    float tint[3];              /**< Factor of its color from the variation over the world */
    int32_t level;              /**< Level of detail it is drawn with */
} box_render_grass_blade_t;

/** Read back the blades of a Grass as the last view that placed them left them, waiting for the
 * copy, for tests. Only call this when the main thread renders, after box_render_sync.
 *
 * @param world The world
 * @param grass The Grass entity
 * @param out Receives box_render_grass_blade_t, the blades of level 0 first
 * @return Whether the backend placed blades of the Grass and they were read
 */
bool box_render_gpu_grass_blades(
    ecs_world_t *world,
    ecs_entity_t grass,
    ecs_vec_t *out);

/** Run a compute shader that reads a terrain with TerrainSampled (terrain_sample.wgsl) and copy
 * what it wrote into memory, waiting for the copy, for tests.
 *
 * The shader file is preprocessed as the engine shaders are; its entry point main gets the
 * bindings of the terrain of the last packet at group 0, its input as a read only storage buffer
 * at group 1 binding 0 and its output as a read write storage buffer at group 1 binding 1. Only
 * call this when the main thread renders, after box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param terrain The terrain entity, with TerrainSampled
 * @param path The WGSL file
 * @param in The input, NULL when in_size is 0
 * @param in_size Bytes of the input, a multiple of 4, 0 for an empty buffer
 * @param out Receives the output
 * @param out_size Bytes of the output, a multiple of 4
 * @param groups Workgroups dispatched along x
 * @return Whether the last packet sampled the terrain and the output was copied
 */
bool box_render_gpu_terrain_compute(
    ecs_world_t *world,
    ecs_entity_t terrain,
    const char *path,
    const void *in,
    uint32_t in_size,
    void *out,
    uint32_t out_size,
    uint32_t groups);

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

/** Frames the temporal sequences of a view cycle over (box_render_temporal_sequence_2d, 3d and
 * TEMPORAL_SEQUENCE_LENGTH of etc/shaders/include/temporal.wgsl) */
#define BOX_RENDER_TEMPORAL_LENGTH (16)

/** What a view renders a frame with about the frame before it, the temporal members of View in
 * etc/shaders/include/lights.wgsl */
typedef struct box_render_temporal_t {
    mat4 prev_view_proj;        /**< View projection of the previous frame the view rendered,
                                 *   the one of this frame when the history is not valid */
    vec4 prev_camera;           /**< World position of the camera in that frame, w 1 */
    uint32_t frame_index;       /**< Frames rendered since the last reset, 0 on a reset */
    bool valid;                 /**< The previous frame is the history of this one */
    int64_t prev_frame;         /**< Packet frame of the previous frame the view rendered, -1
                                 *   for none */
} box_render_temporal_t;

/** History of a view in a target, kept by a backend from frame to frame */
typedef struct box_render_history_t {
    uint32_t target;            /**< Handle of the target */
    ecs_entity_t view;          /**< View entity, 0 for a capture without a view */
    int64_t frame;              /**< Packet frame the view last rendered in */
    ecs_entity_t camera;        /**< Camera entity of that frame */
    int32_t width;              /**< Render width of that frame */
    int32_t height;             /**< Render height of that frame */
    mat4 proj;                  /**< Projection of that frame */
    mat4 view_proj;             /**< View projection of that frame */
    vec4 camera_position;       /**< World position of the camera in that frame, w 1 */
    box_render_temporal_t current; /**< What the view rendered that frame with */
} box_render_history_t;

/** Advance the history of a view in a target to a frame and get what the frame renders with.
 *
 * The history restarts, invalid and at frame index 0, on the first frame of the view in the
 * target, and when the render size, the camera entity or the projection changed or the view has
 * history_reset. A second call in the same frame returns what the first one did.
 *
 * @param histories box_render_history_t of the backend
 * @param target Handle of the target
 * @param v The view
 * @param frame Packet frame
 * @param width Render width
 * @param height Render height
 * @param proj Projection the view renders the target with
 * @return The temporal state of the frame, valid until the next call
 */
const box_render_temporal_t* box_render_history_update(
    ecs_vec_t *histories,
    uint32_t target,
    const box_render_view_t *v,
    int64_t frame,
    int32_t width,
    int32_t height,
    const mat4 proj);

/** Find the history of a view in a target.
 *
 * @param histories box_render_history_t of the backend
 * @param target Handle of the target, 0 for the first target of the view
 * @param view View entity
 * @return The history, NULL for none
 */
const box_render_history_t* box_render_history_find(
    const ecs_vec_t *histories,
    uint32_t target,
    ecs_entity_t view);

/** Drop the histories of the views that did not render in a frame.
 *
 * @param histories box_render_history_t of the backend
 * @param frame Packet frame that rendered
 */
void box_render_history_sweep(
    ecs_vec_t *histories,
    int64_t frame);

/** Point of the 2D low discrepancy sequence of a frame index, the R2 sequence over
 * BOX_RENDER_TEMPORAL_LENGTH frames, as temporal_sequence_2d of temporal.wgsl.
 *
 * @param index Frame index
 * @param out Receives the point, each coordinate in [0, 1)
 */
void box_render_temporal_sequence_2d(
    uint32_t index,
    float *out);

/** Point of the 3D low discrepancy sequence of a frame index, Halton 2, 3, 5 over
 * BOX_RENDER_TEMPORAL_LENGTH frames, as temporal_sequence_3d of temporal.wgsl.
 *
 * @param index Frame index
 * @param out Receives the point, each coordinate in (0, 1)
 */
void box_render_temporal_sequence_3d(
    uint32_t index,
    float *out);

/** A history texture pair of the GPU backend, for tests */
typedef struct box_render_gpu_history_pair_t {
    uint32_t target;            /**< Handle of the target */
    ecs_entity_t view;          /**< View entity */
    uint32_t consumer;          /**< Consumer kind, BOX_GPU_HISTORY_* */
    ecs_entity_t entity;        /**< Entity of the consumer, such as the Bloom effect */
    uint32_t width;             /**< Width of the textures */
    uint32_t height;            /**< Height of the textures */
    uint32_t depth;             /**< Depth of 3D textures, 1 for 2D */
    uint32_t format;            /**< WGPUTextureFormat of the textures */
    const void *read;           /**< Texture view read in the last frame it was used */
    const void *write;          /**< Texture view written in that frame */
    bool valid;                 /**< The read view held the frame before */
    int64_t frame;              /**< Packet frame it was last used in */
} box_render_gpu_history_pair_t;

/** Get the history of a view the GPU backend keeps, for tests. Only call this after
 * box_render_sync, while the render thread is idle.
 *
 * @param world The world
 * @param view The view entity
 * @param out Receives the history of the first target that renders the view
 * @return Whether the world renders with the GPU backend and the view has a history
 */
bool box_render_gpu_view_history(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_history_t *out);

/** Get the history texture pairs of the GPU backend, for tests. Only call this after
 * box_render_sync, while the render thread is idle.
 *
 * @param world The world
 * @param out Receives the pairs, NULL to only count them
 * @param capacity Pairs out holds
 * @return The pairs of the backend, 0 without the GPU backend
 */
int32_t box_render_gpu_history_pairs(
    ecs_world_t *world,
    box_render_gpu_history_pair_t *out,
    int32_t capacity);

#endif
