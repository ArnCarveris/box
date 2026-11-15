/**
 * @file render/gpu_environment.h
 * @brief Environment cubemaps of the webgpu backend and the passes of the EnvironmentImage
 * source, only used on the thread that renders the packets.
 */

#ifndef BOX_RENDER_GPU_ENVIRONMENT_H
#define BOX_RENDER_GPU_ENVIRONMENT_H

#include "gpu.h"
#include "environment.h"

#ifdef BOX_RENDER_WGPU

/** Format of the environment cubemaps, filterable and writable as a storage texture */
#define BOX_GPU_ENVIRONMENT_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Bytes of the SH buffer of an environment: the L2 SH, then the ZH3 of a volume probe */
#define BOX_GPU_ENVIRONMENT_SH_BYTES (BOX_GPU_PROBE_SIZE + BOX_GPU_VOLUME_PROBE_SIZE)

/** Face edge in texels of the last mip level of an environment cubemap at least */
#define BOX_GPU_ENVIRONMENT_MIN_MIP (4)

/** The cubemap of an environment source: 6 layers in WebGPU cube order, mip 0 written by the
 * source and the levels above it down to BOX_GPU_ENVIRONMENT_MIN_MIP texels left to the
 * processing of the environments, which also writes the SH irradiance */
typedef struct box_gpu_environment_t {
    WGPUTexture texture;            /**< The texture, BOX_GPU_ENVIRONMENT_FORMAT */
    WGPUTextureView cube;           /**< Cube view of every mip level */
    WGPUTextureView target;         /**< 2D array view of the 6 layers of mip 0, levels[0] */
    WGPUTextureView *levels;        /**< 2D array view of the 6 layers of each mip level */
    WGPUBuffer params;              /**< Params of bake_sh.wgsl of the processing: the
                                     *   resolution and one cube at layer 0 */
    WGPUBuffer sh;                  /**< BOX_RENDER_SH_COEFFICIENTS vec4 of L2 SH irradiance,
                                     *   in the basis and order of mesh.wgsl, then its ZH3
                                     *   that fills light probe volumes,
                                     *   BOX_GPU_ENVIRONMENT_SH_BYTES bytes */
    uint32_t resolution;            /**< Face edge of mip 0 in texels */
    uint32_t filter;                /**< EnvironmentFilter of the mip levels */
    uint32_t mips;                  /**< Mip levels */
    int32_t written;                /**< Version the source last wrote, 0 while empty */
    int32_t processed;              /**< Version the processing last ran for, 0 for none */
    uint32_t processed_filter;      /**< Filter the processing last ran with */
    int32_t runs;                   /**< Times the processing ran since the cubemap was created */
} box_gpu_environment_t;

/** Pipeline and images in flight of the EnvironmentImage conversion */
typedef struct box_gpu_env_image_t box_gpu_env_image_t;

/** Pipeline of the SH projection and the source cubes of the Ggx processing */
typedef struct box_gpu_env_process_t box_gpu_env_process_t;

/** Pipelines of the environment background draw */
typedef struct box_gpu_env_background_t box_gpu_env_background_t;

/** Create the map of the environment cubemaps.
 *
 * @param gpu The backend
 */
void box_gpu_environment_init(
    box_gpu_t *gpu);

/** Release the environment cubemaps and the conversion of environment images.
 *
 * @param gpu The backend
 */
void box_gpu_environment_fini(
    box_gpu_t *gpu);

/** Create, update or release the cubemap of an environment from a BOX_RENDER_ENVIRONMENT
 * resource; a create makes it empty, also over an existing handle.
 *
 * @param gpu The backend
 * @param op What happens to the cubemap
 * @param handle Handle of the cubemap
 * @param desc The resolution and filter, NULL for a release
 */
void box_gpu_environment_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_environment_desc_t *desc);

/** Get the cubemap of an environment.
 *
 * @param gpu The backend
 * @param handle Handle of the cubemap
 * @return The cubemap, NULL when the handle has none
 */
const box_gpu_environment_t* box_gpu_environment_get(
    box_gpu_t *gpu,
    uint32_t handle);

/** Get the view a source writes the content of an environment into: a write only
 * texture_storage_2d_array<rgba16float> of mip 0, 6 layers in WebGPU cube order, the texel
 * directions of cube_direction in bake_sh.wgsl.
 *
 * @param gpu The backend
 * @param handle Handle of the cubemap
 * @param res Receives the texels per edge of a face, 0 without a cubemap
 * @return The view, NULL when the handle has no cubemap
 */
WGPUTextureView box_gpu_environment_target(
    box_gpu_t *gpu,
    uint32_t handle,
    uint32_t *res);

/** Report that a source recorded the pass that writes new content into an environment, before
 * box_gpu_environment_process runs.
 *
 * @param gpu The backend
 * @param handle Handle of the cubemap
 * @param version EnvironmentStatus.version of the content
 */
void box_gpu_environment_written(
    box_gpu_t *gpu,
    uint32_t handle,
    int32_t version);

/** Check whether the environments can be processed, creating the pipelines of the bake shader
 * and the SH projection when it was (re)compiled. A source writes no content before, so a
 * cubemap never holds a version the processing could not follow in the same packet.
 *
 * @param gpu The backend
 * @return Whether the bake shader has a module and the pipelines exist
 */
bool box_gpu_environment_ready(
    box_gpu_t *gpu);

/** Process the environments whose sources wrote a version that was not processed yet, or whose
 * filter changed, after the sources of a packet recorded their passes and before the captures
 * and the targets.
 *
 * Each such cubemap is processed once, however many views use it, all of them in one command
 * buffer: Box box filters mip 0 into the levels above it, Ggx box filters it into a source cube
 * and prefilters the levels from that. Both project a box filtered mip onto the SH.
 *
 * The GPU time is measured with the timestamp queries of the captures.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_environment_process(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Get the environment a view is lit with: the cubemap of its LightSettings.environment, once
 * the processing wrote its mips and SH.
 *
 * @param gpu The backend
 * @param v The view
 * @return The cubemap, NULL when the view has no environment or it was not processed yet
 */
const box_gpu_environment_t* box_gpu_environment_of(
    box_gpu_t *gpu,
    const box_render_view_t *v);

/** Write the environment uniforms of a view: its intensity and yaw, zero when
 * box_gpu_environment_of has no cubemap for it.
 *
 * @param gpu The backend
 * @param v The view
 * @param u The uniforms of the view
 */
void box_gpu_environment_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u);

/** Write the environment cube and its SH uniform as bindings 12 and 13 of the forward pass: the
 * cubemap of the view, else the black reflection slot and a zero SH.
 *
 * @param gpu The backend
 * @param v The view, NULL for the black slot of the probe and reflection captures
 * @param out Receives the two entries
 */
void box_gpu_environment_bindings(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    WGPUBindGroupEntry *out);

/** Record the environment background of a view as the first draw of its forward pass, in place
 * of the clear color: one triangle over the view that samples mip 0 of its environment in the
 * direction of each pixel, turned by its yaw and times its intensity.
 *
 * Depth is neither tested nor written. Replaces the bind group at 0.
 *
 * @param gpu The backend
 * @param pass The render pass of the view
 * @param format The format of the color target
 * @param v The view
 * @param offset Offset of the uniforms of the view in the view buffer
 * @return Whether the background was drawn: the view has LightSettings.environment.background,
 *         a processed environment and the environment shader a module
 */
bool box_gpu_environment_draw_background(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    uint32_t offset);

/** Release the conversion of environment images.
 *
 * @param gpu The backend
 */
void box_gpu_environment_image_fini(
    box_gpu_t *gpu);

/** Take the image of a BOX_RENDER_ENVIRONMENT_IMAGE resource into a texture that waits for its
 * conversion, replacing an image of the handle that still waits.
 *
 * @param gpu The backend
 * @param handle Handle of the cubemap the image is written into
 * @param data The description followed by the texels
 * @param size Bytes of data
 */
void box_gpu_environment_image_apply(
    box_gpu_t *gpu,
    uint32_t handle,
    const void *data,
    uint32_t size);

/** Convert the images that wait into their cubemaps, as the reference environment source:
 * each writes the target of its handle and reports it written.
 *
 * Images wait while the environment shader has no module or box_gpu_environment_ready fails;
 * an image whose cubemap is gone is dropped.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_environment_image_render(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

#endif

#endif
