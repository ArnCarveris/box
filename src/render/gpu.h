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

#ifdef __EMSCRIPTEN__
#include <webgpu/webgpu.h>
#else
#include <webgpu.h>
#include <wgpu.h>
#endif

/** Color format of offscreen targets */
#define BOX_GPU_OFFSCREEN_FORMAT (WGPUTextureFormat_RGBA8UnormSrgb)

/** Color formats the blit pipeline is created for, at most */
#define BOX_GPU_BLIT_FORMATS (4)

/** Bytes a row of a texture to buffer copy is aligned to */
#define BOX_GPU_ROW_ALIGN (256)

/** Depth format of the views, cleared to 0 and compared with greater for reversed Z */
#define BOX_GPU_DEPTH_FORMAT (WGPUTextureFormat_Depth32Float)

/** Bytes between the uniforms of two views, the dynamic offset alignment of webgpu */
#define BOX_GPU_VIEW_STRIDE (256)

/** Forward pipelines by color format, blend mode and back face mode, at most */
#define BOX_GPU_FORWARD_PIPELINES (64)

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
    WGPUTexture depth;              /**< Depth texture of the views at the render size */
    WGPUTextureView depth_view;     /**< View of the depth texture */
    bool immediate;                 /**< The window can present without vertical sync */
    WGPUBuffer readback;            /**< Buffer a capture copies the image into */
    uint64_t readback_size;         /**< Bytes of the readback buffer */
    uint8_t *pixels;                /**< Copy of the readback buffer the PNG is encoded from */
    uint32_t capture_width;         /**< Width of the copied image */
    uint32_t capture_height;        /**< Height of the copied image */
    uint32_t capture_stride;        /**< Bytes per row of the copied image */
    bool capture_pending;           /**< A copy was recorded and not yet written */
} box_gpu_target_t;

/** A compiled shader module */
typedef struct box_gpu_shader_t {
    WGPUShaderModule module;        /**< The module */
    int32_t revision;               /**< Times the module was compiled under its handle */
} box_gpu_shader_t;

/** Vertex and index buffers of a mesh */
typedef struct box_gpu_mesh_t {
    WGPUBuffer vertices;            /**< box_vertex_t */
    WGPUBuffer indices;             /**< uint32_t, three per triangle */
    uint32_t index_count;           /**< Indices to draw */
} box_gpu_mesh_t;

/** A buffer the main thread created by handle */
typedef struct box_gpu_buffer_t {
    WGPUBuffer buffer;              /**< The buffer */
    uint32_t size;                  /**< Bytes of the buffer */
} box_gpu_buffer_t;

/** Uniforms of a view, written per target because the projection follows its size.
 *
 * The layout matches View in etc/shaders/include/lights.wgsl.
 */
typedef struct box_gpu_view_uniforms_t {
    mat4 view_proj;                 /**< Projection times view matrix */
    mat4 view;                      /**< View matrix, the inverse of the camera transform */
    vec4 camera;                    /**< World position of the camera */
    vec4 ambient_sky;               /**< Linear ambient light from above */
    vec4 ambient_ground;            /**< Linear ambient light from below */
    vec4 cluster;                   /**< Inverse projection scale of x and y, slice near, far */
    uint32_t lights[4];             /**< Directional lights, lights, lit, orthographic */
    uint32_t grid[4];               /**< Clusters per axis, lights per cluster at most */
} box_gpu_view_uniforms_t;

/** Pipeline of the blit or the forward pass for one color format */
typedef struct box_gpu_blit_t {
    WGPUTextureFormat format;       /**< Format of the color target */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_blit_t;

/** Pipeline of the forward pass for one color format, blend mode and back face mode */
typedef struct box_gpu_forward_t {
    WGPUTextureFormat format;       /**< Format of the color target */
    uint32_t blend;                 /**< BlendMode */
    uint32_t back_face;             /**< BackFaceMode */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_forward_t;

/** The webgpu backend: device, shared objects and targets */
typedef struct box_gpu_t {
    WGPUInstance instance;          /**< The instance */
    WGPUAdapter adapter;            /**< The adapter */
    WGPUDevice device;              /**< The device */
    WGPUQueue queue;                /**< Queue of the device */
    WGPUBindGroupLayout blit_layout; /**< Bind group layout of the blit */
    WGPUPipelineLayout blit_pipeline_layout; /**< Pipeline layout of the blit */
    WGPUSampler sampler;            /**< Linear sampler of the blit */
    box_gpu_blit_t blits[BOX_GPU_BLIT_FORMATS]; /**< Blit pipelines by format */
    int32_t blit_count;             /**< Blit pipelines created */
    WGPUBindGroupLayout view_layout; /**< Bind group layout of the view uniforms */
    WGPUPipelineLayout mesh_layout; /**< Pipeline layout of the forward pass */
    box_gpu_forward_t forward[BOX_GPU_FORWARD_PIPELINES]; /**< Forward pipelines */
    int32_t forward_count;          /**< Forward pipelines created */
    WGPUBuffer view_buffer;         /**< Uniforms of the views of a target */
    int32_t view_capacity;          /**< Views the view buffer holds */
    WGPUBindGroupLayout cluster_layout; /**< Bind group layout of the cluster pass */
    WGPUPipelineLayout cluster_pipeline_layout; /**< Pipeline layout of the cluster pass */
    WGPUComputePipeline cluster;    /**< Cluster pipeline, NULL until the shader exists */
    int32_t cluster_revision;       /**< Revision of the shader the pipeline was made with */
    ecs_vec_t clusters;             /**< WGPUBuffer, the cluster buffer of each view slot */
    WGPUBuffer no_lights;           /**< Light buffer of a view whose light buffer is missing */
    ecs_vec_t view_data;            /**< Bytes of the view uniforms of a target */
    WGPUBindGroupLayout scatter_layout; /**< Bind group layout of the scatter pass */
    WGPUPipelineLayout scatter_pipeline_layout; /**< Pipeline layout of the scatter pass */
    WGPUComputePipeline scatter;    /**< Scatter pipeline, NULL until the shader exists */
    int32_t scatter_revision;       /**< Revision of the shader the pipeline was made with */
    ecs_map_t meshes;               /**< Handle to box_gpu_mesh_t */
    ecs_map_t buffers;              /**< Handle to box_gpu_buffer_t */
    ecs_map_t targets;              /**< Handle to box_gpu_target_t */
    ecs_map_t shaders;              /**< Handle to box_gpu_shader_t */
    ecs_vec_t results;              /**< box_render_shader_result_t not yet taken */
    ecs_os_mutex_t lock;            /**< Protects the counters and the results */
    box_render_gpu_stats_t stats;   /**< Counters read by the main thread */
    uint32_t canvas;                /**< Target presented on the page canvas, 0 for none */
} box_gpu_t;

/** Fields of the counters of the backend */
typedef enum box_gpu_stat_t {
    BOX_GPU_STAT_CREATED,           /**< A target was created */
    BOX_GPU_STAT_CONFIGURED,        /**< A target was configured */
    BOX_GPU_STAT_DESTROYED,         /**< A target was released */
    BOX_GPU_STAT_FRAMES,            /**< A target frame was rendered */
    BOX_GPU_STAT_CAPTURES,          /**< A PNG file was written */
    BOX_GPU_STAT_ERRORS,            /**< An error occurred */
    BOX_GPU_STAT_SHADER_CREATED,    /**< A shader module got its handle */
    BOX_GPU_STAT_SHADER_DESTROYED,  /**< A shader module was released */
    BOX_GPU_STAT_COMPILED,          /**< A shader module compiled */
    BOX_GPU_STAT_SHADER_ERROR,      /**< A shader module failed to compile */
    BOX_GPU_STAT_MESH_CREATED,      /**< A mesh was created */
    BOX_GPU_STAT_MESH_DESTROYED,    /**< A mesh was released */
    BOX_GPU_STAT_BUFFER_CREATED,    /**< A buffer was created */
    BOX_GPU_STAT_BUFFER_DESTROYED,  /**< A buffer was released */
    BOX_GPU_STAT_PASS,              /**< A view pass was rendered */
    BOX_GPU_STAT_DRAW,              /**< A draw call was recorded */
    BOX_GPU_STAT_SCATTER            /**< A scatter dispatch was recorded */
} box_gpu_stat_t;

#ifdef __EMSCRIPTEN__
/** Stand in for the wgpu-native poll, which the browser has no equivalent of.
 *
 * The browser maps buffers from its event loop, so a readback that waits inside a frame never
 * completes there; the web build does not record captures and aborts a blocking readback.
 *
 * @param device The device
 * @param wait Whether to wait for the queue
 * @param index Unused submission index
 * @return Never returns
 */
static inline WGPUBool wgpuDevicePoll(
    WGPUDevice device,
    WGPUBool wait,
    const void *index)
{
    (void)device;
    (void)wait;
    (void)index;
    ecs_abort(ECS_UNSUPPORTED, "blocking GPU readback is not available in the browser");

    return 0;
}
#endif

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

/** Compile, recompile or release a shader module.
 *
 * A module that fails to compile is reported and leaves the previous module in place.
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

/** Create the layouts the forward pass shares.
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
 */
void box_gpu_forward_reset(
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
 * @param gpu The backend
 * @param t The target
 * @param packet The packet with the views and batches
 * @param frame The target of the packet
 * @param encoder The encoder of the frame
 * @param color The texture view the views render into
 */
void box_gpu_draw_views(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_target_t *frame,
    WGPUCommandEncoder encoder,
    WGPUTextureView color);

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

/** Record the cluster pass of a view when it has point or spot lights.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param v The view
 * @param slot Slot of the view in the view buffer
 */
void box_gpu_light_cluster(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v,
    int32_t slot);

/** Create the bind group of the forward pass of a view: its uniforms, lights and clusters.
 *
 * @param gpu The backend
 * @param v The view
 * @param slot Slot of the view in the view buffer
 * @return The bind group, released by the caller
 */
WGPUBindGroup box_gpu_light_group(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    int32_t slot);

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
