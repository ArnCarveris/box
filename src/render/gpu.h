/**
 * @file render/gpu.h
 * @brief State of the webgpu backend shared by its device, target and capture sources.
 *
 * Everything declared here is only used on the thread that renders the packets.
 */

#ifndef BOX_RENDER_GPU_H
#define BOX_RENDER_GPU_H

#include "render.h"

#ifdef BOX_RENDER_WGPU

#include <webgpu.h>
#include <wgpu.h>

/** Color format of offscreen targets */
#define BOX_GPU_OFFSCREEN_FORMAT (WGPUTextureFormat_RGBA8UnormSrgb)

/** Color formats the blit pipeline is created for, at most */
#define BOX_GPU_BLIT_FORMATS (4)

/** Bytes a row of a texture to buffer copy is aligned to */
#define BOX_GPU_ROW_ALIGN (256)

/** A surface on the GPU: the image it presents and the image views render into */
typedef struct box_gpu_target_t {
    uint32_t handle;                /**< Handle the main thread assigned */
    box_render_target_desc_t desc;  /**< Size and kind of the target */
    WGPUSurface surface;            /**< Surface of a window, NULL for an offscreen target */
    WGPUTextureFormat format;       /**< Color format of the presented image */
    WGPUTextureUsage usage;         /**< Usage of the presented image */
    WGPUTexture output;             /**< Texture of an offscreen target, NULL for a window */
    WGPUTexture scene;              /**< Render size texture, NULL when it equals the output */
    WGPUTextureView scene_view;     /**< View of the scene texture */
    WGPUBindGroup blit_group;       /**< Binds the scene texture to the blit pipeline */
    bool immediate;                 /**< The window can present without vertical sync */
    WGPUBuffer readback;            /**< Buffer a capture copies the image into */
    uint64_t readback_size;         /**< Bytes of the readback buffer */
    uint8_t *pixels;                /**< Copy of the readback buffer the PNG is encoded from */
    uint32_t capture_width;         /**< Width of the copied image */
    uint32_t capture_height;        /**< Height of the copied image */
    uint32_t capture_stride;        /**< Bytes per row of the copied image */
    bool capture_pending;           /**< A copy was recorded and not yet written */
} box_gpu_target_t;

/** Blit pipeline for one color format */
typedef struct box_gpu_blit_t {
    WGPUTextureFormat format;       /**< Format of the color target */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_blit_t;

/** The webgpu backend: device, shared objects and targets */
typedef struct box_gpu_t {
    WGPUInstance instance;          /**< The instance */
    WGPUAdapter adapter;            /**< The adapter */
    WGPUDevice device;              /**< The device */
    WGPUQueue queue;                /**< Queue of the device */
    WGPUShaderModule blit_shader;   /**< Shader that scales the scene texture to the output */
    WGPUBindGroupLayout blit_layout; /**< Bind group layout of the blit */
    WGPUPipelineLayout blit_pipeline_layout; /**< Pipeline layout of the blit */
    WGPUSampler sampler;            /**< Linear sampler of the blit */
    box_gpu_blit_t blits[BOX_GPU_BLIT_FORMATS]; /**< Blit pipelines by format */
    int32_t blit_count;             /**< Blit pipelines created */
    ecs_map_t targets;              /**< Handle to box_gpu_target_t */
    ecs_os_mutex_t lock;            /**< Protects the counters */
    box_render_gpu_stats_t stats;   /**< Counters read by the main thread */
} box_gpu_t;

/** Fields of the counters of the backend */
typedef enum box_gpu_stat_t {
    BOX_GPU_STAT_CREATED,           /**< A target was created */
    BOX_GPU_STAT_CONFIGURED,        /**< A target was configured */
    BOX_GPU_STAT_DESTROYED,         /**< A target was released */
    BOX_GPU_STAT_FRAMES,            /**< A target frame was rendered */
    BOX_GPU_STAT_CAPTURES,          /**< A PNG file was written */
    BOX_GPU_STAT_ERRORS             /**< An error occurred */
} box_gpu_stat_t;

/** Count an event of the backend.
 *
 * @param gpu The backend
 * @param stat The counter
 */
void box_gpu_count(
    box_gpu_t *gpu,
    box_gpu_stat_t stat);

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

/** Wait for the readback buffer of a target and write it to a PNG file.
 *
 * @param gpu The backend
 * @param t The target
 * @param path The file, written next to it first and then renamed over it
 */
void box_gpu_capture_write(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const char *path);

/** Release the readback buffer of a target.
 *
 * @param t The target
 */
void box_gpu_capture_fini(
    box_gpu_target_t *t);

#endif

#endif
