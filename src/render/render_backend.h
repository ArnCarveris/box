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
    int32_t transmission_snapshots; /**< Snapshots recorded for transmission, per frame of a
                                 *   view one before its first transmissive draw and one before
                                 *   each later one that follows other draws, at most 4 */
    int32_t transmission_textures; /**< Snapshot textures created for transmission, one per
                                 *   color format of the views of a target that draw a
                                 *   Transmission shape, again only when its render size changes */
    int32_t forward_passes;     /**< Forward render passes begun for the views: one per frame,
                                 *   two (early and late) for a view that culls by occlusion
                                 *   without a depth prepass.
                                 *
                                 *   A view has none when it is unlit, its camera has no depth,
                                 *   the mesh shader is not compiled yet or none of its culled
                                 *   batches draws in it (only blended or transmissive draws).
                                 *
                                 *   Plus a split after each transmission snapshot, one that
                                 *   reads the depth for its fog volumes and soft particles
                                 *   and one more when draws follow those.
                                 *
                                 *   Not the depth prepasses, the snapshot downsample passes,
                                 *   nor the shadow, cull, atmosphere, particle simulation or
                                 *   effect passes. */
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
} box_render_cull_stats_t;

/** The culling of a view the GPU backend read back, for its CullStatus */
typedef struct box_render_cull_status_t {
    ecs_entity_t view;          /**< Entity with the View */
    CullStatus status;          /**< The counts */
} box_render_cull_status_t;

/** Packet ring between the main thread and the consumer of the packets */
typedef struct box_render_ring_t box_render_ring_t;

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
 * @param out Receives the casters tested and drawn in tested and visible
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
