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
#define BOX_GPU_BLIT_FORMATS (6)

/** Format of sampled textures, which albedo and emissive layers read through an sRGB view */
#define BOX_GPU_TEXTURE_FORMAT (WGPUTextureFormat_RGBA8Unorm)

/** Format of the view of a texture that decodes display encoded colors */
#define BOX_GPU_TEXTURE_SRGB (WGPUTextureFormat_RGBA8UnormSrgb)

/** TextureWrap modes the samplers are created for */
#define BOX_GPU_WRAPS (3)

/** Anisotropy levels the samplers are created for: 1, 2, 4, 8 and 16 samples */
#define BOX_GPU_ANISOTROPY_LEVELS (5)

/** Bytes a row of a texture to buffer copy is aligned to */
#define BOX_GPU_ROW_ALIGN (256)

/** Depth format of the views, cleared to 0 and compared with greater for reversed Z */
#define BOX_GPU_DEPTH_FORMAT (WGPUTextureFormat_Depth32Float)

/** Bytes between the uniforms of two views, a multiple of the dynamic offset alignment */
#define BOX_GPU_VIEW_STRIDE (768)

/** Bytes between the matrices of two cascades, the dynamic offset alignment of webgpu */
#define BOX_GPU_CASCADE_STRIDE (256)

/** Depth format of the shadow maps, cleared to 0 and compared with greater for reversed Z */
#define BOX_GPU_SHADOW_FORMAT (WGPUTextureFormat_Depth32Float)

/** Color format of the HDR target a view with effects renders into */
#define BOX_GPU_HDR_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Effect passes, HDR targets and the state effects keep across frames */
typedef struct box_gpu_effects_t box_gpu_effects_t;

/** Edge in texels of the shadow map of the probe captures */
#define BOX_GPU_CAPTURE_SHADOW_RESOLUTION (2048)

/** Textures, pipelines and timing of the probe captures */
typedef struct box_gpu_bake_t box_gpu_bake_t;

/** A capture on its way from the GPU to its PNG file */
typedef struct box_gpu_capture_t box_gpu_capture_t;

/** A surface on the GPU: the image it presents and the image views render into */
typedef struct box_gpu_target_t {
    uint32_t handle;                /**< Handle the main thread assigned */
    box_render_target_desc_t desc;  /**< Size and kind of the target */
    WGPUSurface surface;            /**< Surface of a window, NULL for an offscreen target */
    WGPUTextureFormat format;       /**< Color format of the presented image */
    WGPUTextureUsage usage;         /**< Usage of the presented image */
    WGPUTexture output;             /**< Texture of an offscreen target, NULL for a window */
    WGPUTextureView output_view;    /**< View materials sample the output of a texture target */
    WGPUTexture scene;              /**< Render size texture, NULL when it equals the output */
    WGPUTextureView scene_view;     /**< View of the scene texture */
    WGPUBindGroup blit_group;       /**< Binds the scene texture to the blit pipeline */
    WGPUTexture depth;              /**< Depth texture of the views at the render size */
    WGPUTextureView depth_view;     /**< View of the depth texture */
    bool immediate;                 /**< The window can present without vertical sync */
    WGPUBuffer readback;            /**< Idle buffer the next capture copies the image into */
    uint64_t readback_size;         /**< Bytes of the idle readback buffer */
    box_gpu_capture_t *capture;     /**< Copy recorded in this frame, not yet mapped */
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
    float center[3];                /**< Center of the box around the vertex positions */
    float extent[3];                /**< Half size of the box around the vertex positions */
} box_gpu_mesh_t;

/** A sampled texture with its mip chain and the two views the layers bind */
typedef struct box_gpu_texture_t {
    WGPUTexture texture;            /**< The texture, BOX_GPU_TEXTURE_FORMAT */
    WGPUTextureView linear;         /**< View of every mip level as stored */
    WGPUTextureView srgb;           /**< View of every mip level decoded from sRGB */
    uint32_t mips;                  /**< Mip levels of the texture */
    bool mipped;                    /**< The levels below the first were generated */
} box_gpu_texture_t;

/** Bind group of the texture layers and sampler of a batch, cached by its key */
typedef struct box_gpu_material_t {
    uint32_t textures[BOX_RENDER_TEXTURE_SLOTS]; /**< Texture handles, 0 for the default */
    uint32_t wrap;                  /**< TextureWrap of the sampler */
    uint32_t anisotropy;            /**< Anisotropic samples of the sampler */
    WGPUBindGroup group;            /**< The bind group */
    struct box_gpu_material_t *next; /**< Next material with the same hash */
} box_gpu_material_t;

/** A buffer the main thread created by handle */
typedef struct box_gpu_buffer_t {
    WGPUBuffer buffer;              /**< The buffer */
    uint64_t size;                  /**< Bytes of the buffer */
    uint32_t usage;                 /**< box_render_buffer_usage_t flags */
    WGPUBindGroup instances;        /**< Binds it as the instances of a batch, made on use */
    uint64_t version;               /**< Write that last changed it, unique across buffers */
} box_gpu_buffer_t;

/** Levels of a depth pyramid at most, enough for a 16384 pixel axis */
#define BOX_GPU_HIZ_LEVELS (15)

/** Depth pyramid of a render size: power of two levels, each texel the farthest depth below it.
 *
 * The views of every target of that render size share it; a view builds it from its own depth
 * right before its late cull phase reads it.
 */
typedef struct box_gpu_hiz_t {
    uint32_t width;                 /**< Width of the first level, a power of two */
    uint32_t height;                /**< Height of the first level, a power of two */
    uint32_t levels;                /**< Levels down to one texel */
    uint32_t id;                    /**< Number of the pyramid, never reused */
    int64_t stamp;                  /**< Cull pass the pyramid was last used in */
    WGPUTexture texture;            /**< R32Float texture with every level */
    WGPUTextureView view;           /**< View of every level, read by the cull pass */
    WGPUTextureView mips[BOX_GPU_HIZ_LEVELS]; /**< View of each level */
    WGPUBindGroup reduce[BOX_GPU_HIZ_LEVELS]; /**< Reduces the level below into each level */
} box_gpu_hiz_t;

/** A buffer of history words, at most one storage binding window large */
typedef struct box_gpu_hiz_page_t {
    WGPUBuffer buffer;              /**< The words, zero when created */
    uint32_t capacity;              /**< Words of the buffer */
    uint32_t used;                  /**< Words handed out to ranges */
    WGPUBindGroup group;            /**< Binds the words and the pyramid for the cull pass */
    uint32_t group_hiz;             /**< Id of the pyramid the group binds */
} box_gpu_hiz_page_t;

/** Which instances a view drew in its previous frame, one word per instance slot.
 *
 * Each instance buffer the view draws from has a range of words, 0 for an instance that was
 * drawn. A range is only a guess for the early phase: after a slot moved or a buffer was
 * replaced the late phase still tests and draws every instance the early phase missed.
 */
typedef struct box_gpu_cull_history_t {
    ecs_vec_t pages;                /**< box_gpu_hiz_page_t, each bound whole as storage */
    ecs_map_t ranges;               /**< Instance buffer handle to page, offset and words */
    int64_t stamp;                  /**< Cull pass the view was last culled in */
} box_gpu_cull_history_t;

/** A frustum the cull pass tests instances against: a view, or a cascade of its shadows */
typedef struct box_gpu_cull_view_t {
    ecs_entity_t view;              /**< Entity of the view */
    int32_t slot;                   /**< Slot of the view in the view buffer */
    int32_t cascade;                /**< Cascade of its shadow map, -1 for the view itself */
    int32_t first_draw;             /**< First of its draws */
    int32_t draw_count;             /**< Batches it draws */
    int32_t first_dispatch;         /**< First of its dispatches */
    int32_t dispatch_count;         /**< Dispatches that cull its batches */
    uint32_t counter;               /**< Index of its counters */
    box_gpu_hiz_t *hiz;             /**< Pyramid of a view that culls by occlusion, else NULL */
    WGPUBindGroup group;            /**< History and pyramid, or the empty ones */
    bool report;                    /**< Its counts become the CullStatus, false for captures */
} box_gpu_cull_view_t;

/** A batch a cull view draws: where its visible instances and its draw arguments are */
typedef struct box_gpu_cull_draw_t {
    int32_t batch;                  /**< Index of the batch in the packet */
    uint32_t args;                  /**< Index of its indexed indirect draw arguments */
    uint32_t late;                  /**< Arguments of its late phase, UINT32_MAX for none */
    uint32_t page;                  /**< Page that holds its visible slots */
    uint32_t base;                  /**< First of its entries in the visible slots of the page */
    uint32_t count;                 /**< Instances it tests */
} box_gpu_cull_draw_t;

/** A dispatch of the cull pass over the jobs of one instance buffer */
typedef struct box_gpu_cull_dispatch_t {
    uint32_t buffer;                /**< Handle of the instance buffer */
    uint32_t groups;                /**< Workgroups to dispatch */
    bool ordered;                   /**< One invocation per job keeps the order of its slots */
    bool late;                      /**< Runs after the early draw, against the pyramid */
    uint32_t page;                  /**< Page that holds the visible slots of its jobs */
    WGPUBindGroup history;          /**< History page and pyramid, NULL for those of the view */
    bool pooled;                    /**< Its jobs read the instance pool, not their buffer */
    uint32_t first;                 /**< Its job */
    uint32_t scan;                  /**< First scan word of its workgroups, parallel only */
    uint32_t scan_groups;           /**< Its workgroups, 0 for an ordered dispatch */
} box_gpu_cull_dispatch_t;

/** The parallel dispatches of one cull view, phase and page that one cull_scan and one
 * cull_write dispatch finish */
typedef struct box_gpu_cull_table_t {
    int32_t view;                   /**< Index of the cull view */
    bool late;                      /**< Phase of its dispatches */
    uint32_t page;                  /**< Page of its dispatches */
    uint32_t uniform;               /**< Index of its uniforms in the uniform buffer */
    uint32_t buffer;                /**< An instance buffer of its dispatches, for group 1 */
    uint32_t entries;               /**< Draws, the workgroups of cull_scan */
    uint32_t groups;                /**< Workgroups of all its draws, those of cull_write */
} box_gpu_cull_table_t;

/** One cull_instances dispatch over consecutive parallel draws of a table that read the same
 * instances and history: the pool, or the buffer of the one draw that is not in it */
typedef struct box_gpu_cull_run_t {
    int32_t view;                   /**< Index of the cull view */
    bool late;                      /**< Phase of its draws */
    uint32_t page;                  /**< Page of its draws */
    uint32_t uniform;               /**< Index of its uniforms, its part of the workgroup list */
    uint32_t buffer;                /**< Instance buffer of its draw, 0 for the pool */
    WGPUBindGroup history;          /**< History page and pyramid, NULL for those of the view */
    uint32_t groups;                /**< Workgroups of its draws */
} box_gpu_cull_run_t;

/** Range of the instance pool that holds a copy of an instance buffer */
typedef struct box_gpu_cull_pool_range_t {
    uint64_t offset;                /**< First byte in the pool */
    uint64_t size;                  /**< Bytes it holds */
    uint64_t version;               /**< Version of the buffer it holds, 0 for none yet */
} box_gpu_cull_pool_range_t;

/** Range of instances of one draw the cull shader tests, as in Job of cull.wgsl */
typedef struct box_gpu_cull_job_t {
    uint32_t first;                 /**< First slot in the instance buffer */
    uint32_t count;                 /**< Slots to test */
    uint32_t args;                  /**< Index of the draw arguments it counts into */
    uint32_t base;                  /**< First entry it writes in the visible slots of its page */
    float center[3];                /**< Center of the mesh bounds */
    uint32_t history;               /**< First history word of its buffer, UINT32_MAX for none */
    float extent[3];                /**< Half size of the mesh bounds */
    uint32_t late;                  /**< Draw arguments of its late phase, UINT32_MAX for none */
    uint32_t list;                  /**< 1 when first indexes the slot lists, 0 for a range */
    uint32_t pool;                  /**< Instance of the pool its slot 0 is at, 0 for none */
    uint32_t pad[2];                /**< Pads the job to the size of Job in cull.wgsl */
} box_gpu_cull_job_t;

/** Uniforms of one cull dispatch, as in Cull of cull.wgsl */
typedef struct box_gpu_cull_uniforms_t {
    vec4 planes[6];                 /**< World space planes, inside where non negative */
    vec4 clip_w;                    /**< Row of the view projection that gives clip w, the
                                     *   probe position when size[3] is 1 */
    vec4 size;                      /**< Pixels per unit at w 1, smallest size, w guard, 1 when
                                     *   w is the depth in a face of the probe at clip_w */
    uint32_t jobs[4];               /**< First job, jobs, phase, counters of the cull view */
    mat4 view_proj;                 /**< World to clip space of the view */
    uint32_t hiz[4];                /**< Pyramid width, height and levels, 0 levels for none */
    uint32_t scan[4];               /**< First scan word of a parallel dispatch, three unused */
} box_gpu_cull_uniforms_t;

/** What a view culled in a target, kept until its counters are read back */
typedef struct box_gpu_cull_record_t {
    ecs_entity_t view;              /**< Entity of the view */
    int32_t tested;                 /**< Instances it tested */
    uint32_t counter;               /**< Index of its counters */
} box_gpu_cull_record_t;

/** A copy of the cull counters of a target on its way back to the main thread */
typedef struct box_gpu_cull_readback_t {
    WGPUBuffer buffer;              /**< Mappable copy of the counters */
    uint64_t size;                  /**< Bytes of the buffer */
    uint64_t used;                  /**< Bytes copied into it */
    int32_t state;                  /**< Free, copied, mapping, mapped or failed */
    int64_t frame;                  /**< Frame of the packet */
    ecs_vec_t records;              /**< box_gpu_cull_record_t of the views */
    uint64_t stamp_offset;          /**< Byte offset of the timestamps after the counters */
    uint32_t stamps;                /**< Timestamp pairs of its cull passes, 0 for none */
} box_gpu_cull_readback_t;

/** The cluster buffer of a view slot and the read back of the entries its pass allocated */
typedef struct box_gpu_cluster_t {
    WGPUBuffer buffer;              /**< Allocated entries, offset and count per cluster, list */
    uint64_t entries;               /**< Entries the light and probe index list holds */
    WGPUBuffer readback;            /**< Mappable copy of the allocated entries */
    int32_t state;                  /**< Free, copied, mapping, mapped or failed */
    int64_t frame;                  /**< Frame of the packet the copy was recorded in */
    bool cut;                       /**< The last read back pass listed fewer lights or probes
                                     *   than reach */
} box_gpu_cluster_t;

/** Visible slots and slot lists of a group of draws, each bound whole as storage */
typedef struct box_gpu_cull_page_t {
    WGPUBuffer visible;             /**< Slots of the visible instances of its draws */
    WGPUBuffer lists;               /**< Slots of its listed jobs in draw order */
    WGPUBuffer scan;                /**< Masks, counts and offsets of the workgroups culling it */
    uint64_t visible_size;          /**< Bytes of the visible buffer */
    uint64_t lists_size;            /**< Bytes of the list buffer */
    uint64_t scan_size;             /**< Bytes of the scan buffer */
    uint32_t scan_used;             /**< Scan words its parallel dispatches and tables use */
    uint32_t table_base;            /**< First scan word of its tables */
    ecs_vec_t table_data;           /**< uint32_t, its tables and workgroup lists */
    ecs_vec_t list_data;            /**< uint32_t, the slot lists of the target */
    uint32_t used;                  /**< Visible slots its draws use in the target */
    WGPUBindGroup group;            /**< Binds the shared buffers and its own */
} box_gpu_cull_page_t;

/** Slots of one page of visible slots or history words: the 128 MiB storage binding every
 * WebGPU device supports, in 4 byte words */
#define BOX_GPU_CULL_WINDOW ((uint32_t)((128u << 20) / sizeof(uint32_t)))

/** Smallest size of the instance pool of the cull pass in bytes */
#define BOX_GPU_POOL_MIN_SIZE (65536)

/** Copies of the cull counters in flight at most */
#define BOX_GPU_CULL_READBACKS (4)

/** Cull pass state: the frustums, draws and buffers of the target being rendered */
typedef struct box_gpu_cull_t {
    WGPUBindGroupLayout layout;     /**< Bind group layout of the cull pass */
    WGPUBindGroupLayout history_layout; /**< Binds the history and pyramid of a cull view */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the cull pass */
    WGPUComputePipeline parallel;   /**< Tests one instance per invocation, NULL without shader */
    WGPUComputePipeline ordered;    /**< Tests one job per invocation, keeping slot order */
    WGPUComputePipeline scan;       /**< Turns the workgroup counts of a dispatch into offsets */
    WGPUComputePipeline write;      /**< Writes the drawn slots of a dispatch at their offsets */
    WGPUBindGroupLayout hiz_first_layout; /**< Binds the depth and the first level */
    WGPUBindGroupLayout hiz_reduce_layout; /**< Binds a level and the level above it */
    WGPUPipelineLayout hiz_first_pipeline_layout; /**< Pipeline layout of the first level */
    WGPUPipelineLayout hiz_reduce_pipeline_layout; /**< Pipeline layout of the other levels */
    WGPUComputePipeline hiz_first;  /**< Reduces the depth into the first level */
    WGPUComputePipeline hiz_reduce; /**< Reduces a level into the next */
    int32_t revision;               /**< Revision of the shader the pipelines were made with */
    WGPUBuffer uniforms;            /**< box_gpu_cull_uniforms_t per dispatch */
    WGPUBuffer jobs;                /**< box_gpu_cull_job_t */
    WGPUBuffer args;                /**< Indexed indirect draw arguments, per draw */
    WGPUBuffer counters;            /**< Drawn and frustum culled instances per cull view */
    uint64_t uniforms_size;         /**< Bytes of the uniform buffer */
    uint64_t jobs_size;             /**< Bytes of the job buffer */
    uint64_t args_size;             /**< Bytes of the argument buffer */
    uint64_t counters_size;         /**< Bytes of the counter buffer */
    ecs_vec_t pages;                /**< box_gpu_cull_page_t, kept between targets */
    int32_t page_count;             /**< Pages the draws of the target use */
    uint32_t window;                /**< Slots of a page and words of a history page */
    bool bound;                     /**< A pass uploaded its buffers and bound its pages */
    WGPUBuffer empty_history;       /**< History of the cull views without occlusion */
    WGPUTexture empty_hiz;          /**< Pyramid of the cull views without occlusion */
    WGPUTextureView empty_hiz_view; /**< View of the empty pyramid */
    WGPUBindGroup empty_group;      /**< Binds the empty history and pyramid */
    ecs_map_t histories;            /**< View and target key to box_gpu_cull_history_t */
    ecs_map_t pyramids;             /**< Render size to box_gpu_hiz_t */
    uint32_t next_hiz;              /**< Id of the next pyramid */
    int64_t stamp;                  /**< Cull passes begun, ages histories and pyramids */
    box_gpu_cull_readback_t readbacks[BOX_GPU_CULL_READBACKS]; /**< Counter copies */
    ecs_vec_t status;               /**< box_render_cull_status_t, under the lock of the gpu */
    int64_t frame;                  /**< Frame of the packet of the target being rendered */
    ecs_vec_t views;                /**< box_gpu_cull_view_t of the target */
    ecs_vec_t draws;                /**< box_gpu_cull_draw_t of the target */
    ecs_vec_t dispatches;           /**< box_gpu_cull_dispatch_t of the target */
    ecs_vec_t uniform_data;         /**< Bytes of the uniforms of the dispatches */
    ecs_vec_t job_data;             /**< box_gpu_cull_job_t of the target */
    ecs_vec_t arg_data;             /**< uint32_t, the draw arguments before culling */
    ecs_vec_t order;                /**< uint64_t, instance buffer and draw of blended draws */
    ecs_vec_t tables;               /**< box_gpu_cull_table_t of the target */
    ecs_vec_t runs;                 /**< box_gpu_cull_run_t of the target */
    WGPUBuffer pool;                /**< Copies of the instance buffers the cull reads */
    uint64_t pool_size;             /**< Bytes of the pool buffer */
    uint64_t pool_used;             /**< Bytes of the pool its ranges use */
    uint64_t pool_waste;            /**< Bytes of ranges that no longer hold a buffer */
    WGPUBindGroup pool_group;       /**< Binds the pool as the instances, made on use */
    ecs_map_t pool_ranges;          /**< Instance buffer handle to box_gpu_cull_pool_range_t */
    WGPUQuerySet stamps;            /**< Begin and end of the cull passes of a target, NULL
                                     *   without timestamp queries */
    WGPUBuffer stamp_resolve;       /**< Resolved timestamps, copied into the readback */
    uint32_t stamp_count;           /**< Timestamps the passes of the target wrote */
} box_gpu_cull_t;

/** Uniforms of a view, written per target because the projection follows its size.
 *
 * The layout matches View in etc/shaders/include/lights.wgsl.
 */
typedef struct box_gpu_view_uniforms_t {
    mat4 view_proj;                 /**< Projection times view matrix */
    mat4 view;                      /**< View matrix, the inverse of the camera transform */
    vec4 camera;                    /**< World position of the camera */
    vec4 viewport;                  /**< Render width and height in pixels, and their inverse */
    vec4 ambient_sky;               /**< Linear ambient light from above */
    vec4 ambient_ground;            /**< Linear ambient light from below */
    vec4 cluster;                   /**< Inverse projection scale of x and y, slice near, far */
    uint32_t lights[4];             /**< Directional lights, lights, lit, orthographic */
    uint32_t grid[4];               /**< Clusters per axis, reflection probes the clusters list */
    mat4 shadow_matrix[BOX_RENDER_SHADOW_CASCADES]; /**< World to shadow clip per cascade */
    vec4 shadow_split;              /**< View depth each cascade ends at */
    vec4 shadow_bias;               /**< Depth added to the compared depth per cascade */
    vec4 shadow_offset;             /**< World offset along the normal per cascade */
    vec4 shadow_params;             /**< Fade start, shadow end, texel in uv, unused */
    uint32_t shadow_config[4];      /**< Cascades, 0 without shadows, PCF taps, two unused */
} box_gpu_view_uniforms_t;

/** Pipeline of the blit or the forward pass for one color format */
typedef struct box_gpu_blit_t {
    WGPUTextureFormat format;       /**< Format of the color target */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_blit_t;

/** Uniforms of a probe volume, as in Volume of etc/shaders/mesh.wgsl */
typedef struct box_gpu_volume_uniforms_t {
    float origin[4];                /**< First probe, w is the spacing */
    uint32_t size[4];               /**< Probes per axis, w is 1 when views sample the probes */
} box_gpu_volume_uniforms_t;

/** An irradiance probe volume: grid uniforms, two buffers with the SH of every probe, which
 * the passes of a bake read and write in turn, the validity mask and the visibility maps */
typedef struct box_gpu_volume_t {
    WGPUBuffer grid;                /**< box_gpu_volume_uniforms_t */
    WGPUBuffer probes[2];           /**< BOX_RENDER_SH_COEFFICIENTS vec4 per probe */
    WGPUBuffer valid;               /**< A byte per cell, a bit per corner probe views sample */
    WGPUBuffer visibility;          /**< BOX_GPU_VISIBILITY_TEXELS packed halves per probe */
    uint32_t count;                 /**< Probes of the volume */
    float spacing;                  /**< World units between neighbouring probes */
    WGPUBindGroup group[2];         /**< Binds the grid, a probe buffer, the mask and the
                                     *   visibility as group 3 */
} box_gpu_volume_t;

/** Format of the reflection probe cubemaps, the smallest one that is filterable and writable
 * as a storage texture on core WebGPU everywhere */
#define BOX_GPU_REFLECT_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Face edge in texels of the last prefiltered mip level of a reflection cubemap at least */
#define BOX_GPU_REFLECT_MIN_MIP (4)

/** Cubemap array of the reflection probes of a scene: 6 layers per slot, mip 0 the mirror and
 * each higher mip level down to BOX_GPU_REFLECT_MIN_MIP texels the GGX prefiltered radiance of
 * a rougher surface, with the L2 SH irradiance of each slot.
 *
 * The source cubes the prefilter of a chunk samples are one too, with a full box filtered mip
 * chain and no SH. */
typedef struct box_gpu_reflect_t {
    WGPUTexture texture;            /**< The texture, BOX_GPU_REFLECT_FORMAT */
    WGPUTextureView cube;           /**< Cube array view of every mip level */
    WGPUTextureView *levels;        /**< 2D array view of every layer of each mip level */
    WGPUBuffer sh;                  /**< BOX_RENDER_SH_COEFFICIENTS vec4 per slot, in the basis
                                     *   and order of mesh.wgsl, NULL for source cubes */
    WGPUBuffer visibility;          /**< Visibility maps the projection writes besides the SH,
                                     *   unused, NULL for source cubes */
    uint32_t resolution;            /**< Face edge of mip 0 in texels */
    uint32_t cubes;                 /**< Cubemap slots */
    uint32_t mips;                  /**< Mip levels */
} box_gpu_reflect_t;

/** The webgpu backend: device, shared objects and targets */
typedef struct box_gpu_t {
    WGPUInstance instance;          /**< The instance */
    WGPUAdapter adapter;            /**< The adapter */
    WGPUDevice device;              /**< The device */
    WGPUQueue queue;                /**< Queue of the device */
    uint64_t fence_done;            /**< Last fence whose work the queue reported done */
    WGPUBindGroupLayout blit_layout; /**< Bind group layout of the blit */
    WGPUPipelineLayout blit_pipeline_layout; /**< Pipeline layout of the blit */
    WGPUSampler sampler;            /**< Linear sampler of the blit */
    box_gpu_blit_t blits[BOX_GPU_BLIT_FORMATS]; /**< Blit pipelines by format */
    int32_t blit_count;             /**< Blit pipelines created */
    WGPUBindGroupLayout view_layout; /**< Bind group layout of the view uniforms */
    WGPUBindGroupLayout material_layout; /**< Bind group layout of the texture layers */
    WGPUSampler samplers[BOX_GPU_WRAPS][BOX_GPU_ANISOTROPY_LEVELS]; /**< Created on use */
    box_gpu_texture_t *white;       /**< Default of the albedo, emissive and roughness layers */
    box_gpu_texture_t *flat;        /**< Default of the normal layer, an unperturbed normal */
    ecs_map_t textures;             /**< Handle to box_gpu_texture_t */
    ecs_map_t materials;            /**< Key hash to the first box_gpu_material_t */
    ecs_vec_t mips;                 /**< uint32_t, textures whose mip levels are not generated */
    WGPUPipelineLayout mesh_layout; /**< Pipeline layout of the forward pass */
    WGPUBindGroupLayout volume_layout; /**< Binds the probe volume a view samples */
    ecs_map_t volumes;              /**< Handle to box_gpu_volume_t */
    box_gpu_volume_t *no_volume;    /**< 1x1x1 volume of the views that use the hemisphere */
    ecs_map_t reflections;          /**< Handle to box_gpu_reflect_t */
    WGPUBindGroupLayout reflect_store_layout; /**< Binds a face atlas and mip 0 of a cube array */
    WGPUPipelineLayout reflect_store_pipeline_layout; /**< Pipeline layout of the store */
    WGPUComputePipeline reflect_store; /**< Store pipeline, NULL until the bake shader exists */
    WGPUBindGroupLayout reflect_mip_layout; /**< Binds two mip levels of a cube array */
    WGPUPipelineLayout reflect_mip_pipeline_layout; /**< Pipeline layout of the mip pass */
    WGPUComputePipeline reflect_mip; /**< Mip pipeline, NULL until the bake shader exists */
    WGPUBindGroupLayout reflect_prefilter_layout; /**< Binds the source cubes and a mip level */
    WGPUPipelineLayout reflect_prefilter_pipeline_layout; /**< Pipeline layout of the prefilter */
    WGPUComputePipeline reflect_prefilter; /**< Prefilter pipeline, NULL until the bake shader
                                            *   exists */
    WGPUSampler reflect_sampler;    /**< Trilinear sampler of the source cubes */
    box_gpu_reflect_t *reflect_source; /**< Source cubes of a chunk, NULL before the first */
    ecs_map_t forward;              /**< Forward pipeline by shader, format, blend, back face */
    WGPUBuffer view_buffer;         /**< Uniforms of the views of a target */
    int32_t view_capacity;          /**< Views the view buffer holds */
    WGPUBindGroupLayout cluster_layout; /**< Bind group layout of the cluster pass */
    WGPUPipelineLayout cluster_pipeline_layout; /**< Pipeline layout of the cluster pass */
    WGPUComputePipeline cluster;    /**< Cluster pipeline, NULL until the shader exists */
    int32_t cluster_revision;       /**< Revision of the shader the pipeline was made with */
    ecs_vec_t clusters;             /**< box_gpu_cluster_t of each view slot */
    WGPUBuffer no_lights;           /**< Light buffer of a view whose light buffer is missing */
    WGPUBuffer no_probes;           /**< Reflection probe buffer of a cluster pass without probes */
    ecs_vec_t view_data;            /**< Bytes of the view uniforms of a target */
    WGPUBindGroupLayout shadow_layout; /**< Bind group layout of the shadow pass */
    WGPUPipelineLayout shadow_pipeline_layout; /**< Pipeline layout of the shadow pass */
    WGPURenderPipeline shadow;      /**< Shadow pipeline, NULL until the shader exists */
    int32_t shadow_revision;        /**< Revision of the shader the pipeline was made with */
    WGPUSampler shadow_sampler;     /**< Comparison sampler of the shadow lookups */
    WGPUTexture shadow_empty;       /**< Shadow map bound by views without shadows */
    WGPUTextureView shadow_empty_view; /**< Array view of the empty shadow map */
    WGPUTexture shadow_map;         /**< Shadow map the views render their cascades into */
    WGPUTextureView shadow_map_view; /**< Array view of the shadow map */
    WGPUTextureView shadow_layers[BOX_RENDER_SHADOW_CASCADES]; /**< View of each cascade */
    int32_t shadow_resolution;      /**< Edge of the shadow map, 0 before it exists */
    int32_t shadow_cascades;        /**< Layers of the shadow map */
    WGPUBuffer shadow_buffer;       /**< Cascade matrices of the views of a target */
    int32_t shadow_capacity;        /**< Views the cascade buffer holds */
    ecs_vec_t shadow_data;          /**< Bytes of the cascade matrices of a target */
    WGPUTexture capture_shadow;     /**< Shadow map of the probe captures, one cascade */
    WGPUTextureView capture_shadow_view; /**< Array view the captures sample */
    WGPUTextureView capture_shadow_layer; /**< View the casters render into */
    WGPUBuffer capture_shadow_buffer; /**< Matrix of the cascade of the captures */
    WGPUBindGroup capture_shadow_group; /**< Binds the matrix to the shadow pipeline */
    WGPUBindGroupLayout instance_layout; /**< Binds the instance buffer of a batch */
    box_gpu_cull_t cull;            /**< Cull pass state */
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
    box_render_compile_delivery_t delivery; /**< When compile outcomes apply, under the lock */
    box_render_readback_delivery_t readbacks; /**< When cull counters publish, under the lock */
    uint32_t cull_window;           /**< Slots of a cull page from the next pass, under the lock */
    uint64_t buffer_writes;         /**< Writes to buffers, the version of the last one */
    box_render_gpu_stats_t stats;   /**< Counters read by the main thread */
    box_render_gpu_stats_t pending; /**< Counter changes not yet published, render thread only */
    ecs_os_thread_id_t thread;      /**< Thread that ran init, the only one using the device */
    uint32_t canvas;                /**< Target presented on the page canvas, 0 for none */
    box_gpu_effects_t *effects;     /**< Effect passes of the views */
    uint32_t rendering;             /**< Target that is rendering, materials cannot sample it */
    bool timestamps;                /**< The device has timestamp queries */
    box_gpu_bake_t *bake;           /**< Probe captures, created on the first capture */
    ecs_vec_t captures;             /**< box_gpu_capture_t* whose readback is mapping */
    bool lost;                      /**< The device was lost, the ring recreates it */
    bool lose;                      /**< box_render_gpu_lose_device asked to destroy it */
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

/** Count an event of the backend in the pending counters, without taking the lock.
 *
 * @param gpu The backend
 * @param stat The counter
 */
void box_gpu_count(
    box_gpu_t *gpu,
    box_gpu_stat_t stat);

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

/** Release every cached bind group of the texture layers, after a texture they bind changed.
 *
 * @param gpu The backend
 */
void box_gpu_materials_clear(
    box_gpu_t *gpu);

/** Get the bind group of the texture layers and sampler of a batch, created on first use.
 *
 * A layer whose handle has BOX_RENDER_TEXTURE_TARGET samples the image of that target, or the
 * default of the layer while the target itself renders or does not exist.
 *
 * @param gpu The backend
 * @param b The batch
 * @param anisotropy Anisotropic samples of the sampler
 * @return The bind group
 */
WGPUBindGroup box_gpu_material_group(
    box_gpu_t *gpu,
    const box_render_batch_t *b,
    uint32_t anisotropy);

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

/** Create the store, mip and prefilter pipelines from the module of the bake shader.
 *
 * @param gpu The backend
 * @param module The module
 */
void box_gpu_reflect_pipelines(
    box_gpu_t *gpu,
    WGPUShaderModule module);

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
 * empties it, and a batch whose shader does not exist draws nothing.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @param b The batch
 * @return The pipeline, NULL without a shader or for an unknown blend mode
 */
WGPURenderPipeline box_gpu_forward_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_batch_t *b);

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

/** Create a forward pipeline from a mesh or 2D shape module inside an error scope, to find out
 * whether the module fits the pipeline layout before it replaces the module that renders.
 *
 * wgpu-native reports the scope at once; the browser reports it later, so there the check
 * passes and an invalid pipeline shows as a device error.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @param shader The module
 * @return NULL when the pipeline is valid, else the error, which the caller frees
 */
char* box_gpu_forward_check(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule shader);

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

/** Record the cluster pass of a view when it has point or spot lights or reflection probes.
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

/** Create the bind group of the forward pass of a view with explicit buffers.
 *
 * @param gpu The backend
 * @param v The view
 * @param uniforms Buffer with the uniforms, bound with a dynamic offset
 * @param clusters Cluster buffer of the view
 * @param shadow Array view of the shadow map the view samples
 * @return The bind group, released by the caller
 */
WGPUBindGroup box_gpu_light_group_with(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    WGPUBuffer uniforms,
    WGPUBuffer clusters,
    WGPUTextureView shadow);

/** Create the bind group of the forward pass of a view: its uniforms, lights, clusters and
 * shadow map.
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

/** Get the shadow map a view samples.
 *
 * @param gpu The backend
 * @param v The view
 * @return The array view of the shadow map, the empty one for a view without shadows
 */
WGPUTextureView box_gpu_shadow_binding(
    box_gpu_t *gpu,
    const box_render_view_t *v);

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

/** Release the HDR targets and effect state that the targets of a packet did not use.
 *
 * @param gpu The backend
 * @param packet The packet that rendered
 */
void box_gpu_effects_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

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

/** Get the bind group of an instance buffer, made on first use.
 *
 * @param gpu The backend
 * @param b The buffer
 * @return The bind group, NULL when the buffer is not a storage buffer
 */
WGPUBindGroup box_gpu_instance_group(
    box_gpu_t *gpu,
    box_gpu_buffer_t *b);

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

/** Create the pyramid pipelines from the module of the cull shader.
 *
 * @param gpu The backend
 * @param module The cull shader
 */
void box_gpu_hiz_pipelines(
    box_gpu_t *gpu,
    WGPUShaderModule module);

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

/** Give every instance buffer a view draws opaque batches from a range of its history.
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

/** Get the first history word of an instance buffer in its page.
 *
 * @param h The history
 * @param buffer Handle of the instance buffer
 * @return The offset, UINT32_MAX when the buffer has no range
 */
uint32_t box_gpu_hiz_offset(
    const box_gpu_cull_history_t *h,
    uint32_t buffer);

/** Get the page of the history words of an instance buffer.
 *
 * @param h The history
 * @param buffer Handle of the instance buffer
 * @return The page, 0 when the buffer has no range
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

/** Forget every range of the instance pool, so the next draws copy their buffers again from
 * the start of the pool.
 *
 * @param c The cull state
 */
void box_gpu_cull_pool_reset(
    box_gpu_cull_t *c);

/** Drop the ranges of the instance pool whose buffer no longer exists, counting them as waste.
 *
 * @param gpu The backend
 */
void box_gpu_cull_pool_purge(
    box_gpu_t *gpu);

/** Find the range of the instance pool that holds a copy of an instance buffer, adding one at
 * the end of the pool for a buffer it does not hold yet or that outgrew its range. The copy
 * itself is made by box_gpu_cull_pool_upload.
 *
 * @param gpu The backend
 * @param handle Handle of the instance buffer
 * @return The instance its slot 0 is at in the pool, UINT32_MAX when it does not fit
 */
uint32_t box_gpu_cull_pool_add(
    box_gpu_t *gpu,
    uint32_t handle);

/** Test whether the instance pool holds an instance buffer whole.
 *
 * @param gpu The backend
 * @param handle Handle of the instance buffer
 * @return Whether the jobs of the buffer read the pool
 */
bool box_gpu_cull_pooled(
    box_gpu_t *gpu,
    uint32_t handle);

/** Make the instance pool hold every range and copy the buffers written since their range was
 * copied, then bind it. The copies are submitted before the frame, after its uploads and
 * scatters, so the cull of the frame reads its instances.
 *
 * @param gpu The backend
 */
void box_gpu_cull_pool_upload(
    box_gpu_t *gpu);

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
 * Binds the mesh, the instance buffer at group, the visible slots of the draw and draws with
 * the arguments the cull pass wrote.
 *
 * @param gpu The backend
 * @param pass The render pass
 * @param packet The packet
 * @param draw The draw
 * @param group Bind group index the instance buffer is bound at
 * @param late Draw what the late phase kept instead of the early or only phase
 * @return Whether the draw was recorded
 */
bool box_gpu_cull_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_gpu_cull_draw_t *draw,
    uint32_t group,
    bool late);

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
