/**
 * @file renderer/renderer_internal.h
 * @brief Private renderer state shared between the renderer source files.
 */

#ifndef BOX_RENDERER_INTERNAL_H
#define BOX_RENDERER_INTERNAL_H

#include "renderer.h"
#include "cluster.h"
#include "frustum.h"
#include "bake.h"
#include "shadow.h"
#include "blur.h"
#include "hiz.h"
#include "queue.h"
#include "gpu.h"

/** Faces of a cubemap */
#define BOX_ENV_FACES (6)

/** Edge length of the environment and prefiltered specular cubemaps */
#define BOX_ENV_SIZE (64)

/** Edge length of the irradiance cubemap */
#define BOX_ENV_IRRADIANCE_SIZE (16)

/** Edge length of the split sum BRDF lookup texture */
#define BOX_ENV_BRDF_SIZE (64)

/** Bytes between the uniforms of two environment passes */
#define BOX_ENV_STRIDE (256)

/** Bindings the environment takes at the start of the shared fragment bind group */
#define BOX_ENV_BINDINGS (4)

/** First binding of the scene copy in the shared fragment bind group */
#define BOX_SCENE_BINDING (BOX_ENV_BINDINGS + BOX_TEXTURE_CLASSES * 2)

/** Entries of the shared fragment bind group */
#define BOX_MESH_BINDINGS (BOX_SCENE_BINDING + 2)

/** Prefiltered environments the renderer keeps, shared by the views that match one */
#define BOX_ENV_SLOTS (4)

/** Cull modes a sorted pipeline is built for: back faces culled or kept */
#define BOX_CULL_MODES (2)

/** Bytes of the uniform block holding the parameters of every material layer */
#define BOX_LAYER_UNIFORM_SIZE (BOX_LAYER_MAX * sizeof(box_layer_params_t))

/** Static instances a batch may hold before the renderer stops mirroring it on the CPU */
#define BOX_CULL_MIRROR_MAX (4096)

/** Byte stride between two culling uniforms, the alignment a dynamic offset needs */
#define BOX_CULL_UNIFORM_STRIDE (256)

/** Argument offset of a draw the CPU culled, which needs no indirect arguments */
#define BOX_CULL_NO_ARGS (0xFFFFFFFFu)

/** Byte stride between two occlusion uniforms, the alignment a dynamic offset needs */
#define BOX_HIZ_UNIFORM_STRIDE (256)

/** Counters the occlusion pass keeps: instances tested and instances found hidden */
#define BOX_HIZ_STAT_WORDS (2)

/** Draw of a view that holds the instances the previous frame already saw */
#define BOX_HIZ_PASS_VISIBLE (0)

/** Draw of a view that holds the instances the pyramid uncovered this frame */
#define BOX_HIZ_PASS_NEW (1)

/** Passes of a prefilter: one per face of the source, every specular mip and the irradiance */
#define BOX_ENV_PASSES (BOX_ENV_FACES * (BOX_ENV_MIPS + 2))

/** Uniforms of one environment pass, written at a dynamic offset */
typedef struct box_env_uniform_t {
    float sky[4];     /**< Linear radiance of the upper hemisphere */
    float ground[4];  /**< Linear radiance of the lower hemisphere */
    float params[4];  /**< Cube face, roughness, unused, unused */
} box_env_uniform_t;

/** Uniforms shared by the occupancy and probe bake passes */
typedef struct box_bake_uniform_t {
    float grid_min[4];      /**< World minimum corner of the occupancy grid */
    float grid_cell[4];     /**< World size of one voxel, w is its diagonal */
    uint32_t grid_dim[4];   /**< Voxels on each axis, w unused */
    uint32_t light_count;   /**< Static lights in the bake light buffer */
    uint32_t emitter_count; /**< Emitters in the emitter buffer */
    uint32_t steps;         /**< Ray march samples per visibility test */
    uint32_t pad;           /**< Padding to the 16 byte GPU stride */
} box_bake_uniform_t;

/** Descriptor and job count of one mesh, the per mesh uniform of the bake passes */
typedef struct box_bake_info_t {
    uint32_t desc_count;  /**< Static instance slots of the mesh */
    uint32_t job_count;   /**< Probes the bake pass writes this frame */
    uint32_t pad[2];      /**< Padding to the 16 byte GPU stride */
} box_bake_info_t;

/** Frustum and range of one dispatch of the culling pass */
typedef struct box_cull_uniform_t {
    float planes[BOX_FRUSTUM_PLANES][4]; /**< Planes of the frustum of the view */
    uint32_t first;  /**< First instance of the source buffer the dispatch tests */
    uint32_t count;  /**< Instances the dispatch tests */
    uint32_t base;   /**< First instance of the visible buffer the dispatch writes */
    uint32_t arg;    /**< Index of the draw arguments the dispatch counts into */
    uint32_t caster; /**< The dispatch keeps the shadow casters of the range only */
    uint32_t pad[3]; /**< Padding to the 16 byte GPU stride */
} box_cull_uniform_t;

/** Frustum, matrices and pyramid extent of one dispatch of the occlusion pass */
typedef struct box_hiz_uniform_t {
    float clip[16];                      /**< World to clip matrix of the view */
    float prev[16];                      /**< World to clip matrix of the previous frame */
    float planes[BOX_FRUSTUM_PLANES][4]; /**< Planes of the frustum of the view */
    uint32_t range[4];  /**< First slot, instances, first visible slot and argument index */
    uint32_t screen[4]; /**< Pyramid width, height, levels and the mode of the dispatch */
} box_hiz_uniform_t;

/** Bind group of the occlusion dispatches of one mesh in one view */
typedef struct box_hiz_mesh_t {
    WGPUBindGroup group;      /**< Bind group of its occlusion dispatches */
    WGPUBuffer src;           /**< Static instance buffer the group binds */
    WGPUBuffer dst;           /**< Visible instance buffer the group binds */
    WGPUBuffer args;          /**< Argument buffer the group binds */
    WGPUTextureView pyramid;  /**< Pyramid of this frame the group binds */
    WGPUTextureView history;  /**< Pyramid of the previous frame the group binds */
} box_hiz_mesh_t;

/** One draw of the shadow pass of a view, in the order the draws are recorded */
typedef struct box_shadow_draw_t {
    uint32_t mesh;    /**< Mesh handle */
    uint32_t first;   /**< First instance of the caster buffer of the view */
    uint32_t count;   /**< Instances a CPU culled draw holds */
    uint32_t limit;   /**< Instances the slice of the caster buffer holds at most */
    uint32_t args;    /**< Byte offset of its arguments, BOX_CULL_NO_ARGS on the CPU path */
    uint32_t stage;   /**< First instance of the staging array a compacted draw wrote */
    uint32_t cascade; /**< Cascade the draw belongs to */
    uint32_t statics; /**< The draw fills the cached static layer of its cascade */
} box_shadow_draw_t;

/** One dispatch of the culling pass, recorded for the view being drawn */
typedef struct box_cull_job_t {
    uint32_t mesh;   /**< Mesh whose static instance buffer the dispatch reads */
    uint32_t first;  /**< First slot of the mesh the dispatch tests */
    uint32_t count;  /**< Instances the dispatch tests */
    uint32_t slot;   /**< Index of its uniform in the uniform buffer */
    uint32_t draw;   /**< Index of its draw in the draw list of the view */
    uint32_t pass;   /**< Pass of the view the dispatch feeds, a BOX_HIZ_PASS constant */
} box_cull_job_t;

/** One draw of a view after culling */
typedef struct box_cull_draw_t {
    uint32_t mesh;   /**< Mesh handle */
    uint32_t first;  /**< First instance of the visible buffer of the view */
    uint32_t count;  /**< Instances a CPU culled draw holds */
    uint32_t limit;  /**< Instances the slice of the visible buffer holds at most */
    uint32_t args;   /**< Byte offset of its arguments, BOX_CULL_NO_ARGS on the CPU path */
    uint32_t baked;  /**< The draw reads the probes of its mesh */
    uint32_t stage;  /**< First instance of the staging array a compacted draw wrote */
    uint32_t pass;   /**< Pass the draw belongs to, one of the BOX_HIZ_PASS constants */
    const box_instance_t *src; /**< Source of a draw the frustum kept whole, NULL otherwise */
} box_cull_draw_t;

/** Contiguous range of static slots a view shows */
typedef struct box_cull_range_t {
    uint32_t first;  /**< First slot of the range */
    uint32_t count;  /**< Slots in the range */
} box_cull_range_t;

/** Bind group of the culling dispatches of one mesh in one view */
typedef struct box_gpu_cull_t {
    WGPUBindGroup group; /**< Bind group of its dispatches */
    WGPUBuffer src;      /**< Static instance buffer the group binds */
    WGPUBuffer dst;      /**< Visible instance buffer the group binds */
    WGPUBuffer args;     /**< Argument buffer the group binds */
} box_gpu_cull_t;

/** Offscreen color texture a view renders into, created from a packet command */
typedef struct box_gpu_target_t {
    WGPUTexture color;     /**< Color texture of the target */
    WGPUTextureView view;  /**< Render attachment view of the color texture */
    uint32_t width;        /**< Width in pixels */
    uint32_t height;       /**< Height in pixels */
    bool alive;            /**< Handle is in use */
} box_gpu_target_t;

/** One prefiltered environment, shared by every view of its generation */
typedef struct box_gpu_env_t {
    WGPUTexture source;                /**< Cubemap the gradient is rendered into */
    WGPUTextureView source_view;       /**< Cube view of the gradient */
    WGPUTexture specular;              /**< Specular cubemap, one mip per roughness step */
    WGPUTextureView specular_view;     /**< Cube view of the specular mip chain */
    WGPUTexture irradiance;            /**< Diffuse irradiance cubemap */
    WGPUTextureView irradiance_view;   /**< Cube view of the irradiance */
    WGPUBindGroup filter_group;        /**< Bind group of the specular and irradiance passes */
    uint32_t generation;               /**< View generation the prefilter was built for */
    uint32_t used;                     /**< Frame the entry was last bound by a view */
} box_gpu_env_t;

/** GPU resources of one view of the frame */
typedef struct box_gpu_view_t {
    uint32_t width;                  /**< Width the targets of the view were created for */
    uint32_t height;                 /**< Height the targets of the view were created for */
    WGPUTexture depth;               /**< Depth target of the view */
    WGPUTextureView depth_view;      /**< Attachment view of the depth target */
    WGPUTextureView depth_sample;    /**< Sampled view of the depth target, read by the pyramid */
    WGPUTexture hiz[2];              /**< Depth pyramids of the view, r32float mip chains */
    WGPUTextureView hiz_view[2];     /**< View of a whole pyramid the occlusion test reads */
    WGPUTextureView hiz_mips[2][BOX_HIZ_LEVELS_MAX]; /**< Attachment view of one of its levels */
    WGPUBindGroup hiz_source[2][BOX_HIZ_LEVELS_MAX]; /**< Source of the pass that fills a level */
    uint32_t hiz_levels;             /**< Levels its pyramids hold, 0 when it has none */
    uint32_t hiz_slot;               /**< Pyramid this frame builds, the other one is its history */
    bool hiz_history;                /**< The other pyramid holds the depth of the previous frame */
    float hiz_clip[16];              /**< World to clip matrix the history pyramid was built with */
    box_vec_t hiz_meshes;            /**< box_hiz_mesh_t indexed by mesh handle */
    bool hiz_active;                 /**< The frame recorded occlusion dispatches for the view */
    WGPUBuffer view_buffer;          /**< Uniform buffer holding the view */
    WGPUBindGroup view_group;        /**< Bind group with the view and layer uniforms */
    WGPUBuffer lights;               /**< Light storage buffer of the view */
    uint32_t light_capacity;         /**< Lights the light buffer of the view can hold */
    WGPUBuffer clusters;             /**< Light offset and count per cluster of the view */
    WGPUBuffer light_indices;        /**< Light indices of every cluster of the view */
    WGPUBindGroup cluster_group;     /**< View, lights and grid of its compute pass */
    WGPUBindGroup light_group;       /**< Lights and grid the mesh shader reads for it */
    box_cluster_t grid;              /**< Grid its cluster buffers were built for */
    bool scene_needed;               /**< A sorted draw of the view samples the copy */
    WGPUTexture scene;               /**< Copy of the opaque image of the view */
    WGPUTextureView scene_view;      /**< View of its whole blur chain */
    WGPUTextureView scene_mips[BOX_BLUR_LEVELS_MAX]; /**< Attachment view of one of its levels */
    WGPUBindGroup scene_down[BOX_BLUR_LEVELS_MAX]; /**< Downsample source, the finer level */
    WGPUBindGroup scene_up[BOX_BLUR_LEVELS_MAX];   /**< Upsample source, the coarser level */
    uint32_t scene_mips_count;       /**< Levels its blur chain holds */
    WGPUBindGroup env_group;         /**< Environment, textures and scene copy of the view */
    uint32_t env_slot;               /**< Environment entry its group binds */
    bool env_dirty;                  /**< The environment bind group must be rebuilt */
    WGPUBuffer visible;              /**< Instances the culling of the view compacted */
    uint32_t visible_capacity;       /**< Instances the visible buffer can hold */
    box_vec_t draws;                 /**< box_cull_draw_t of the frame, in draw order */
    box_vec_t staging;               /**< box_instance_t the CPU culling compacted this frame */
    box_vec_t culls;                 /**< box_gpu_cull_t indexed by mesh handle */
    bool culled;                     /**< The draw list of the view holds this frame */
    WGPUTexture shadow_depth;        /**< Depth array the shadow lookup of the view samples */
    WGPUTextureView shadow_sample;   /**< Array view of the working depth */
    WGPUTexture shadow_static;       /**< Cached depth of the static casters of every cascade */
    WGPUTextureView shadow_attach[BOX_SHADOW_CASCADE_MAX];  /**< Attachment of a working layer */
    WGPUTextureView shadow_cached[BOX_SHADOW_CASCADE_MAX];  /**< Attachment of a static layer */
    uint32_t shadow_size;            /**< Edge the shadow textures were created for */
    uint32_t shadow_count;           /**< Cascades the shadow textures hold */
    box_shadow_cache_t shadow_cache; /**< State of the cached static layers */
    uint32_t shadow_rebuild;         /**< Cascades whose static layer this frame renders */
    WGPUBuffer shadow_uniform;       /**< Cascade matrix of every shadow pass of the view */
    WGPUBindGroup shadow_group;      /**< Uniform bind group of the shadow pass */
    WGPUBuffer shadow_instances;     /**< Caster instances the shadow culling compacted */
    uint32_t shadow_capacity;        /**< Instances the caster buffer can hold */
    box_vec_t shadow_draws;          /**< box_shadow_draw_t of the frame, in draw order */
    box_vec_t shadow_staging;        /**< box_instance_t the CPU shadow culling compacted */
    box_vec_t shadow_culls;          /**< box_gpu_cull_t of the shadow culling, per mesh */
} box_gpu_view_t;

/** GPU resources of one mesh and its static instances */
typedef struct box_gpu_mesh_t {
    WGPUBuffer vertices;             /**< Interleaved vertex buffer */
    WGPUBuffer indices;              /**< Index buffer */
    uint32_t index_count;            /**< Indices to draw */
    WGPUBuffer statics;              /**< Static instance buffer */
    uint32_t static_capacity;        /**< Instances the static buffer can hold */
    uint32_t static_count;           /**< Static instances to draw */
    box_vec_t groups;                /**< box_static_group_t, empty when no slot has a layer */
    WGPUBuffer probe_descs;          /**< Probe grid descriptor per static slot */
    WGPUBuffer probes;               /**< Probe pool of the mesh, baked spherical harmonics */
    WGPUBuffer probe_jobs;           /**< Probes the bake pass writes this frame */
    WGPUBuffer probe_info;           /**< box_bake_info_t of the mesh */
    WGPUBindGroup probe_group;       /**< Probes and descriptors the mesh shader reads */
    WGPUBindGroup bake_group;        /**< Probe buffers of the bake pass */
    WGPUBindGroup grid_group;        /**< Probe descriptors of the occupancy pass */
    box_vec_t mirror;                /**< box_instance_t copy of the static buffer, for the CPU */
    bool mirror_valid;               /**< The mirror holds every static instance of the mesh */
    uint32_t desc_capacity;          /**< Descriptors the descriptor buffer can hold */
    uint32_t probe_capacity;         /**< Probes the probe buffer can hold */
    uint32_t job_capacity;           /**< Jobs the job buffer can hold */
    bool alive;                      /**< Handle is in use */
} box_gpu_mesh_t;

/** The 2D array texture of one texture size class */
typedef struct box_gpu_texclass_t {
    WGPUTexture texture;   /**< Array texture holding every layer of the class */
    WGPUTextureView view;  /**< Array view the mesh shader samples */
    WGPUSampler sampler;   /**< Sampler of the class, with the anisotropy setting */
    uint32_t layers;       /**< Layers the array texture holds */
} box_gpu_texclass_t;

#ifdef BOX_PLATFORM_WEB
/** Captures that can wait for their buffer map at the same time */
#define BOX_CAPTURE_PENDING_MAX (2)

/** A capture whose buffer is being mapped by the browser */
typedef struct box_capture_t {
    WGPUBuffer buffer;               /**< Mappable copy of the frame target */
    uint32_t width;                  /**< Image width in pixels */
    uint32_t height;                 /**< Image height in pixels */
    uint32_t stride;                 /**< Bytes per row in the buffer */
    bool bgra;                       /**< Rows need a BGRA to RGBA swap */
    bool busy;                       /**< Slot holds a capture in flight */
    char path[BOX_CAPTURE_PATH_MAX]; /**< PNG the callback writes */
} box_capture_t;
#endif

/** Everything the renderer needs to produce frames */
struct box_renderer_t {
    box_renderer_desc_t desc;        /**< Creation parameters */
    box_queue_t queue;               /**< Packet handoff */
    WGPUInstance instance;           /**< WebGPU instance */
    WGPUSurface surface;             /**< Window surface, NULL when offscreen */
    WGPUAdapter adapter;             /**< Selected adapter */
    WGPUDevice device;               /**< Logical device */
    WGPUQueue gpu_queue;             /**< Device queue */
    WGPUTextureFormat format;        /**< Color target format */
    WGPUTexture offscreen;           /**< Offscreen color target, NULL with a surface */
    WGPUTextureView offscreen_view;  /**< View of the offscreen target */
    WGPURenderPipeline pipeline;     /**< Lit instanced mesh pipeline */
    WGPUShaderModule mesh_shader;    /**< Module every mesh pipeline is built from */
    WGPUPipelineLayout mesh_layout;  /**< Pipeline layout shared by the mesh pipelines */
    WGPURenderPipeline sorted_pipelines[BOX_BLEND_COUNT][BOX_CULL_MODES]; /**< Sorted pass */
    WGPUBuffer sorted;               /**< Instance buffer of the sorted pass */
    uint32_t sorted_capacity;        /**< Entries the sorted instance buffer can hold */
    WGPUSampler scene_sampler;       /**< Linear sampler of the copy, full level range */
    WGPUBindGroupLayout blur_layout; /**< Source texture and sampler of a blur pass */
    WGPURenderPipeline blur_down;    /**< Thirteen tap downsample of a level into the next */
    WGPURenderPipeline blur_up;      /**< Eight tap tent upsample, mixed into the level above */
    WGPUBuffer layer_buffer;         /**< Parameters of every material layer */
    char *shader_src[BOX_SHADER_BUILTIN_MAX]; /**< Preprocessed WGSL of every builtin slot */
    bool shader_failed;              /**< A shader did not compile and was already reported */
    bool initialized;                /**< The pipelines were built from the builtin sources */
    volatile bool device_error;      /**< The device reported an error since it was cleared */
    WGPUBindGroupLayout view_layout; /**< Layout of the view uniform bind group */
    box_vec_t meshes;                /**< box_gpu_mesh_t indexed by handle */
    box_vec_t views;                 /**< box_gpu_view_t, one per view of the packet */
    box_gpu_view_t *v;               /**< View the renderer is recording, never NULL in a pass */
    box_vec_t targets;               /**< box_gpu_target_t indexed by target handle */
    box_gpu_env_t envs[BOX_ENV_SLOTS]; /**< Prefiltered environments the views share */
    uint32_t env_clock;              /**< Counts the environments bound, to pick an entry */
    WGPUBuffer dynamics;             /**< Shared dynamic instance buffer */
    uint32_t dynamic_capacity;       /**< Instances the dynamic buffer can hold */
    WGPUComputePipeline cluster_pipeline; /**< Cluster assignment pass */
    WGPUBindGroupLayout cluster_layout;   /**< Layout of the compute bind group */
    WGPUBindGroupLayout light_layout; /**< Layout of the fragment light bind group */
    WGPUTexture env_brdf;            /**< Split sum BRDF lookup table */
    WGPUTextureView env_brdf_view;   /**< View of the lookup table */
    WGPUSampler env_sampler;         /**< Linear sampler of the environment textures */
    WGPUBuffer env_uniform;          /**< Uniforms of every prefilter pass */
    WGPUBindGroupLayout env_pass_layout;   /**< Uniform only layout of the gradient and BRDF */
    WGPUBindGroup env_pass_group;    /**< Uniform bind group of the gradient and BRDF */
    WGPUBindGroupLayout env_filter_layout; /**< Uniform, source cube and sampler layout */
    WGPURenderPipeline env_gradient_pipeline;   /**< Sky and ground gradient into a face */
    WGPURenderPipeline env_specular_pipeline;   /**< GGX prefilter of one mip face */
    WGPURenderPipeline env_irradiance_pipeline; /**< Cosine convolution of one face */
    WGPURenderPipeline env_brdf_pipeline; /**< Split sum integration of the lookup table */
    WGPUBindGroupLayout env_layout;  /**< Layout of the environment group of the mesh shader */
    box_bake_state_t bake;           /**< Probe descriptors, dirty set and previous inputs */
    WGPUBuffer bake_uniform;         /**< Grid and count uniforms of the bake passes */
    WGPUBuffer bake_lights;          /**< Static lights the bake accumulates */
    uint32_t bake_light_capacity;    /**< Lights the bake light buffer can hold */
    WGPUBuffer bake_emitters;        /**< Emissive static instances the bake accumulates */
    uint32_t bake_emitter_capacity;  /**< Emitters the emitter buffer can hold */
    WGPUBuffer occupancy;            /**< Occupancy bits of the static scene */
    uint32_t occupancy_words;        /**< Words the occupancy buffer holds */
    WGPUBindGroupLayout bake_layout; /**< Uniform, lights, emitters and occupancy */
    WGPUBindGroup bake_group;        /**< Global bind group of the bake pass */
    WGPUBindGroupLayout grid_layout; /**< Uniform and writable occupancy */
    WGPUBindGroup grid_group;        /**< Global bind group of the occupancy pass */
    WGPUBindGroupLayout bake_mesh_layout; /**< Per mesh layout of the bake pass */
    WGPUBindGroupLayout grid_mesh_layout; /**< Per mesh layout of the occupancy pass */
    WGPUBindGroupLayout probe_layout; /**< Probe and descriptor layout the mesh shader reads */
    WGPUBuffer probe_dummy;          /**< Zero probe bound by dynamic draws */
    WGPUBuffer desc_dummy;           /**< Zero descriptor bound by dynamic draws */
    WGPUBindGroup probe_dummy_group; /**< Bind group of the zero probe */
    WGPUComputePipeline bake_pipeline; /**< Probe bake pass */
    WGPUComputePipeline grid_pipeline; /**< Occupancy fill pass */
    box_gpu_texclass_t texclass[BOX_TEXTURE_CLASSES]; /**< One array texture per size class */
    WGPUTexture texture_dummy;       /**< Single texel array texture of an unused class */
    WGPUTextureView texture_dummy_view; /**< View of the single texel array texture */
    bool texture_dirty;              /**< A class texture changed, the bind group is stale */
    WGPUComputePipeline cull_pipeline; /**< Frustum culling pass */
    WGPUBindGroupLayout cull_layout; /**< Uniform, source, visible and argument layout */
    WGPUBuffer cull_uniform;         /**< Frustum and range of every dispatch of the frame */
    uint32_t cull_uniform_capacity;  /**< Dispatches the uniform buffer can hold */
    WGPUBuffer cull_args;            /**< Indirect arguments of every compute culled draw */
    uint32_t cull_args_capacity;     /**< Draws the argument buffer can hold */
    box_vec_t cull_jobs;             /**< box_cull_job_t of the view being recorded */
    box_vec_t cull_ranges;           /**< box_cull_range_t scratch of one mesh */
    uint32_t cull_slots;             /**< Uniforms the frame handed out */
    uint32_t cull_arg_count;         /**< Draw arguments the frame handed out */
    uint32_t cull_drawn;             /**< Instances the CPU culling of the frame kept */
    uint32_t cull_culled;            /**< Instances the CPU culling of the frame skipped */
    uint32_t cull_tested;            /**< Instances the compute culling of the frame tested */
    uint32_t cull_visible;           /**< Instances the last read back compute culling kept */
    WGPURenderPipeline hiz_copy;     /**< Depth target into the first level of a pyramid */
    WGPURenderPipeline hiz_reduce;   /**< One level of a pyramid into the next */
    WGPUBindGroupLayout hiz_depth_layout; /**< Depth target layout of the copy pass */
    WGPUBindGroupLayout hiz_level_layout; /**< Level layout of the reduce pass */
    WGPUComputePipeline hiz_first;   /**< Occlusion pass that keeps what the last frame saw */
    WGPUComputePipeline hiz_second;  /**< Occlusion pass that keeps what the pyramid uncovered */
    WGPUBindGroupLayout hiz_layout;  /**< Uniform, source, visible, arguments, bits and pyramid */
    WGPUBuffer hiz_uniform;          /**< Frustum, range and pyramid of every occlusion dispatch */
    uint32_t hiz_uniform_capacity;   /**< Dispatches the uniform buffer can hold */
    WGPUBuffer hiz_stats;            /**< Instances the occlusion pass tested and hid */
    WGPUBuffer hiz_dummy;            /**< Visibility bits a dispatch without occlusion binds */
    box_vec_t hiz_jobs;              /**< box_cull_job_t of the occlusion pass being recorded */
    uint32_t hiz_slots;              /**< Occlusion uniforms the frame handed out */
    bool hiz_used;                   /**< The frame recorded occlusion dispatches */
    uint32_t hiz_tested;             /**< Instances the last read back occlusion pass tested */
    uint32_t hiz_occluded;           /**< Instances the last read back occlusion pass hid */
    box_renderer_stats_t stats;      /**< Instance counts of the last frame */
    WGPUBuffer cull_readback;        /**< Mappable copy of the arguments of a recorded frame */
    uint32_t cull_readback_capacity; /**< Draws the readback buffer can hold */
    uint32_t cull_readback_count;    /**< Draws the pending readback holds */
    bool cull_readback_busy;         /**< The readback buffer is waiting for its map */
    WGPURenderPipeline shadow_pipeline; /**< Depth only pipeline of the shadow pass */
    WGPUShaderModule shadow_shader;  /**< Module the shadow pipeline is built from */
    WGPUBindGroupLayout shadow_layout; /**< Cascade uniform layout of the shadow pass */
    WGPUPipelineLayout shadow_pipeline_layout; /**< Pipeline layout of the shadow pass */
    WGPUSampler shadow_sampler;      /**< Comparison sampler of the shadow lookup */
    WGPUTexture shadow_dummy;        /**< Depth array a view without shadows binds */
    WGPUTextureView shadow_dummy_view; /**< Array view of the placeholder depth */
    WGPUBuffer shadow_cull_uniform;  /**< Frustum and range of every shadow dispatch */
    uint32_t shadow_uniform_capacity; /**< Dispatches the shadow uniform buffer can hold */
    WGPUBuffer shadow_args;          /**< Indirect arguments of every compute culled caster draw */
    box_vec_t shadow_jobs;           /**< box_cull_job_t of the shadow pass being recorded */
    uint32_t shadow_slots;           /**< Shadow uniforms the frame handed out */
    uint32_t shadow_arg_count;       /**< Shadow draw arguments the frame handed out */
    uint32_t shadow_rebuilds;        /**< Static cascade layers the frame rendered */
    uint32_t width;                  /**< Current target width */
    uint32_t height;                 /**< Current target height */
    volatile bool ready;             /**< Device and targets are usable */
    volatile bool failed;            /**< Device creation failed */
    volatile uint32_t frames;        /**< Frames presented */
#ifdef BOX_PLATFORM_WEB
    box_capture_t captures[BOX_CAPTURE_PENDING_MAX]; /**< Captures waiting for their map */
#else
    pthread_t thread;                /**< Renderer thread when threaded */
    bool thread_started;             /**< Thread was created */
#endif
};

/** Create instance, surface, adapter and device; blocks on native.
 *
 * @param r The renderer
 * @return 0 on success, nonzero on failure
 */
int box_device_init(
    box_renderer_t *r);

/** Release device, adapter, surface and instance.
 *
 * @param r The renderer
 */
void box_device_fini(
    box_renderer_t *r);

/** Create or resize the color and depth targets for the packet size.
 *
 * @param r The renderer
 * @param width Target width
 * @param height Target height
 */
void box_device_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height);

/** Create the pipeline of the placeholder triangle.
 *
 * @param r The renderer
 */
void box_frame_init(
    box_renderer_t *r);

/** Make a view of the frame the one the renderer records, creating its slot.
 *
 * @param r The renderer
 * @param index Index of the view in the packet
 */
void box_view_select(
    box_renderer_t *r,
    uint32_t index);

/** Build the bind group of the view and layer uniforms of the current view.
 *
 * @param r The renderer
 */
void box_view_group(
    box_renderer_t *r);

/** Create or resize the targets and buffers of the current view.
 *
 * @param r The renderer
 * @param view The view of the packet
 */
void box_view_prepare(
    box_renderer_t *r,
    const box_view_t *view);

/** Release the GPU resources of one view.
 *
 * @param v The view resources
 */
void box_view_release(
    box_gpu_view_t *v);

/** Release the resources of every view.
 *
 * @param r The renderer
 */
void box_view_fini(
    box_renderer_t *r);

/** Create and destroy the render target textures a packet asks for.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_target_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** Entry of a live render target handle.
 *
 * @param r The renderer
 * @param handle The target handle
 * @return The entry, NULL when the handle names no live target
 */
box_gpu_target_t* box_target_get(
    box_renderer_t *r,
    uint32_t handle);

/** Release every render target texture.
 *
 * @param r The renderer
 */
void box_target_fini(
    box_renderer_t *r);

/** Create a variant of the mesh pipeline.
 *
 * @param r The renderer
 * @param stride Bytes between two instances in the instance buffer
 * @param offset Bytes from an instance buffer element to its instance data
 * @param cull Faces the rasterizer drops
 * @param depth_write The draw writes the depth buffer
 * @param blend Blend state of the color target, NULL for no blending
 * @return The pipeline
 */
WGPURenderPipeline box_frame_pipeline(
    box_renderer_t *r,
    uint64_t stride,
    uint64_t offset,
    WGPUCullMode cull,
    bool depth_write,
    const WGPUBlendState *blend);

/** Render and present one packet.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_frame_render(
    box_renderer_t *r,
    const box_packet_t *p);

/** Apply the resource and instance changes of a packet to GPU buffers.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_scene_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** Record draw calls for every mesh with instances the view shows.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param p The packet
 * @param pass The render pass
 */
void box_scene_draw(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPURenderPassEncoder pass);

/** Release all mesh and instance buffers.
 *
 * @param r The renderer
 */
void box_scene_fini(
    box_renderer_t *r);

/** Create the culling pipeline, its layout and the shared uniform and argument buffers.
 *
 * @param r The renderer
 */
void box_cull_init(
    box_renderer_t *r);

/** Check whether every view of a packet culls, so the shared dynamic upload is skipped.
 *
 * @param p The packet
 * @return True when no view draws the whole dynamic instance array
 */
bool box_cull_packet(
    const box_packet_t *p);

/** Grow the shared culling buffers for the dispatches a packet can produce.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_cull_frame(
    box_renderer_t *r,
    const box_packet_t *p);

/** Build the visible instances and the draw list of the current view.
 *
 * Small static batches and the dynamic batches are culled and compacted on the
 * CPU, large static batches get a dispatch of the culling pass.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param p The packet
 */
void box_cull_view(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p);

/** Record the culling dispatches of the current view.
 *
 * @param r The renderer
 * @param encoder The command encoder
 */
void box_cull_dispatch(
    box_renderer_t *r,
    WGPUCommandEncoder encoder);

/** Record the draws of one pass of the draw list of the current view.
 *
 * @param r The renderer
 * @param pass The render pass
 * @param group One of the BOX_HIZ_PASS constants
 */
void box_cull_draw(
    box_renderer_t *r,
    WGPURenderPassEncoder pass,
    uint32_t group);

/** Create the pyramid pipelines, the occlusion pass and their shared buffers.
 *
 * @param r The renderer
 */
void box_hiz_init(
    box_renderer_t *r);

/** Reset the per frame occlusion counters and grow the shared uniform buffer.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_hiz_frame(
    box_renderer_t *r,
    const box_packet_t *p);

/** Create or resize the depth pyramid of the current view.
 *
 * @param r The renderer
 * @param view The view being drawn
 */
void box_hiz_prepare(
    box_renderer_t *r,
    const box_view_t *view);

/** Add the occlusion dispatch of one range of a mesh to one pass of the view.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @param first First slot of the range
 * @param count Slots in the range
 * @param draw Index of its draw in the draw list of the view
 * @param pass One of the BOX_HIZ_PASS constants
 */
void box_hiz_job(
    box_renderer_t *r,
    uint32_t mesh,
    uint32_t first,
    uint32_t count,
    uint32_t draw,
    uint32_t pass);

/** Write the uniform of every occlusion dispatch of the current view.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param f The frustum of the view
 */
void box_hiz_upload(
    box_renderer_t *r,
    const box_view_t *view,
    const box_frustum_t *f);

/** Record the passes that fill the depth pyramid of the current view.
 *
 * @param r The renderer
 * @param encoder The command encoder
 */
void box_hiz_build(
    box_renderer_t *r,
    WGPUCommandEncoder encoder);

/** Record the occlusion dispatches of one pass of the current view.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param pass One of the BOX_HIZ_PASS constants
 */
void box_hiz_dispatch(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    uint32_t pass);

/** Release the pyramid textures and the level bind groups of one view.
 *
 * @param v The view resources
 */
void box_hiz_resize(
    box_gpu_view_t *v);

/** Release the pyramid and the visibility bits of one view.
 *
 * @param v The view resources
 */
void box_hiz_view_fini(
    box_gpu_view_t *v);

/** Release the pyramid pipelines, the occlusion pass and their buffers.
 *
 * @param r The renderer
 */
void box_hiz_fini(
    box_renderer_t *r);

/** Copy the draw arguments of the frame into the readback buffer.
 *
 * @param r The renderer
 * @param encoder The command encoder
 */
void box_cull_record(
    box_renderer_t *r,
    WGPUCommandEncoder encoder);

/** Publish the instance counts of the frame and map the pending readback.
 *
 * @param r The renderer
 */
void box_cull_publish(
    box_renderer_t *r);

/** Release the culling resources of one view.
 *
 * @param v The view resources
 */
void box_cull_view_fini(
    box_gpu_view_t *v);

/** Release the culling pipeline and its buffers.
 *
 * @param r The renderer
 */
void box_cull_fini(
    box_renderer_t *r);

/** Create the shadow pipeline, its sampler and the placeholder depth array.
 *
 * @param r The renderer
 */
void box_shadow_init(
    box_renderer_t *r);

/** Reset the per frame shadow counters and grow the shared shadow buffers.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_shadow_frame(
    box_renderer_t *r,
    const box_packet_t *p);

/** Build the cascade textures, the dirty set and the caster draws of the current view.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param p The packet
 */
void box_shadow_prepare(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p);

/** Record the culling dispatches of the shadow pass of the current view.
 *
 * @param r The renderer
 * @param encoder The command encoder
 */
void box_shadow_dispatch(
    box_renderer_t *r,
    WGPUCommandEncoder encoder);

/** Record the static and the dynamic caster passes of the current view.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param encoder The command encoder
 */
void box_shadow_record(
    box_renderer_t *r,
    const box_view_t *view,
    WGPUCommandEncoder encoder);

/** Fill the shadow map half of the view bind group of the current view.
 *
 * @param r The renderer
 * @param out Entries to fill, two of them
 * @param base First binding of the shadow map half
 */
void box_shadow_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base);

/** Release the shadow resources of one view.
 *
 * @param v The view resources
 */
void box_shadow_view_fini(
    box_gpu_view_t *v);

/** Release the shadow pipeline, its sampler and the shared shadow buffers.
 *
 * @param r The renderer
 */
void box_shadow_fini(
    box_renderer_t *r);

/** Create the uniform buffer the material layer parameters live in.
 *
 * @param r The renderer
 */
void box_layers_init(
    box_renderer_t *r);

/** Upload the layer parameters a packet carries.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_layers_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** Release the layer uniform buffer.
 *
 * @param r The renderer
 */
void box_layers_fini(
    box_renderer_t *r);

/** Take the shader sources a packet carries and rebuild what they feed.
 *
 * @param r The renderer
 * @param p The packet, whose sources move into the renderer
 */
void box_shader_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** Preprocessed source of a shader slot.
 *
 * @param r The renderer
 * @param handle The shader handle
 * @return The source, NULL when the slot never received one
 */
const char* box_shader_source(
    box_renderer_t *r,
    uint32_t handle);

/** Compile the source of a shader slot into a module.
 *
 * @param r The renderer
 * @param handle The shader handle
 * @return The module, NULL when there is no source or it does not compile
 */
WGPUShaderModule box_shader_module(
    box_renderer_t *r,
    uint32_t handle);

/** Check whether every builtin shader slot holds a source.
 *
 * @param r The renderer
 * @return true when the pipelines can be built
 */
bool box_shader_ready(
    box_renderer_t *r);

/** Release the shader sources the renderer holds.
 *
 * @param r The renderer
 */
void box_shader_fini(
    box_renderer_t *r);

/** Create the resource tables that do not need a shader to exist.
 *
 * @param r The renderer
 */
void box_frame_resources(
    box_renderer_t *r);

/** Recompile the mesh shader and rebuild every pipeline built from it.
 *
 * @param r The renderer
 * @return true when the new source compiled into a pipeline
 */
bool box_frame_rebuild(
    box_renderer_t *r);

/** Release the lazily built pipelines of the sorted pass.
 *
 * @param r The renderer
 */
void box_transparent_reset(
    box_renderer_t *r);

/** Create the sampler, the blur pipelines and the views of the scene copy.
 *
 * @param r The renderer
 */
void box_transparent_init(
    box_renderer_t *r);

/** Recreate the copy of the opaque image of the current view and its blur chain.
 *
 * @param r The renderer
 */
void box_transparent_resize(
    box_renderer_t *r);

/** Release the copy of the opaque image of one view and its bind groups.
 *
 * @param v The view resources
 */
void box_transparent_view_fini(
    box_gpu_view_t *v);

/** Note whether a sorted draw of a view reads the copy of its opaque image.
 *
 * @param r The renderer
 * @param view The view
 */
void box_transparent_needed(
    box_renderer_t *r,
    const box_view_t *view);

/** Describe the scene copy half of the shared fragment bind group layout.
 *
 * @param out Entries to fill, two of them
 * @param base First binding of the scene copy half
 */
void box_transparent_bind_layout(
    WGPUBindGroupLayoutEntry *out,
    uint32_t base);

/** Fill the scene copy half of the shared fragment bind group.
 *
 * @param r The renderer
 * @param out Entries to fill, two of them
 * @param base First binding of the scene copy half
 */
void box_transparent_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base);

/** Upload the sorted draws of a packet into the instance buffer.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_transparent_upload(
    box_renderer_t *r,
    const box_packet_t *p);

/** Copy the opaque target into the scene copy and run its dual filter blur.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param target The texture the opaque pass rendered into
 */
void box_transparent_prepare(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTexture target);

/** Record the sorted draws of a view, back to front, one pipeline per mode.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 * @param pass The render pass
 */
void box_transparent_draw(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPURenderPassEncoder pass);

/** Release the scene copy, its pipelines and the sorted instance buffer.
 *
 * @param r The renderer
 */
void box_transparent_fini(
    box_renderer_t *r);

/** Create the texture samplers and the placeholder array texture.
 *
 * @param r The renderer
 */
void box_texture_init(
    box_renderer_t *r);

/** Describe the texture half of the shared fragment bind group layout.
 *
 * @param out Entries to fill, BOX_TEXTURE_CLASSES times two of them
 * @param base First binding of the texture half
 */
void box_texture_bind_layout(
    WGPUBindGroupLayoutEntry *out,
    uint32_t base);

/** Fill the texture half of the shared fragment bind group.
 *
 * @param r The renderer
 * @param out Entries to fill, BOX_TEXTURE_CLASSES times two of them
 * @param base First binding of the texture half
 */
void box_texture_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base);

/** Upload the textures of a packet, growing the array texture of their class.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_texture_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** Release the array textures, samplers and bind groups.
 *
 * @param r The renderer
 */
void box_texture_fini(
    box_renderer_t *r);

/** Create the cluster pipeline, its bind group layouts and the light buffer.
 *
 * @param r The renderer
 */
void box_lights_init(
    box_renderer_t *r);

/** Create the light buffer of the current view.
 *
 * @param r The renderer
 */
void box_lights_view_init(
    box_renderer_t *r);

/** Release the light and cluster buffers of one view.
 *
 * @param v The view resources
 */
void box_lights_view_fini(
    box_gpu_view_t *v);

/** Upload the lights of a view and rebuild its grid buffers when they changed.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 */
void box_lights_update(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p);

/** Record the compute pass that assigns the lights of a view to its clusters.
 *
 * @param r The renderer
 * @param view The view
 * @param encoder The command encoder
 */
void box_lights_dispatch(
    box_renderer_t *r,
    const box_view_t *view,
    WGPUCommandEncoder encoder);

/** Release the light and cluster resources.
 *
 * @param r The renderer
 */
void box_lights_fini(
    box_renderer_t *r);

/** Create the environment textures, pipelines and bind groups and integrate the BRDF.
 *
 * @param r The renderer
 */
void box_environment_init(
    box_renderer_t *r);

/** Bind the prefiltered environment of a view, prefiltering it when it is new.
 *
 * @param r The renderer
 * @param view The view
 * @param encoder The command encoder
 */
void box_environment_update(
    box_renderer_t *r,
    const box_view_t *view,
    WGPUCommandEncoder encoder);

/** Build the shared fragment bind group of the current view.
 *
 * @param r The renderer
 */
void box_environment_group(
    box_renderer_t *r);

/** Mark the shared fragment bind group of every view as stale.
 *
 * @param r The renderer
 */
void box_environment_rebuild(
    box_renderer_t *r);

/** Release the environment textures, pipelines and bind groups.
 *
 * @param r The renderer
 */
void box_environment_fini(
    box_renderer_t *r);

/** Create the bake pipelines, their layouts and the probe bookkeeping.
 *
 * @param r The renderer
 */
void box_bake_init(
    box_renderer_t *r);

/** Apply the bake inputs of a packet and record the occupancy and bake passes.
 *
 * @param r The renderer
 * @param p The packet
 * @param encoder The command encoder
 */
void box_bake_update(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder);

/** Bind group a draw of a mesh reads its probes through.
 *
 * @param r The renderer
 * @param mesh Mesh handle, ignored when the mesh has no probes
 * @param baked Draw is a static batch with probes
 * @return The bind group, the zero probe group when the mesh has none
 */
WGPUBindGroup box_bake_group(
    box_renderer_t *r,
    uint32_t mesh,
    bool baked);

/** Release the probe buffers and bind groups of a mesh.
 *
 * @param m The mesh entry
 */
void box_bake_mesh_release(
    box_gpu_mesh_t *m);

/** Release the bake pipelines, buffers and bookkeeping.
 *
 * @param r The renderer
 */
void box_bake_fini(
    box_renderer_t *r);

/** Read back a texture and write it as PNG.
 *
 * @param r The renderer
 * @param texture Texture with CopySrc usage
 * @param path Output file
 * @return 0 on success, nonzero on failure
 */
int box_capture_texture(
    box_renderer_t *r,
    WGPUTexture texture,
    const char *path);

/** Write an RGBA8 image as PNG.
 *
 * @param path Output file
 * @param width Image width
 * @param height Image height
 * @param stride Bytes per row
 * @param pixels Pixel data
 * @return 0 on success, nonzero on failure
 */
int box_image_write_png(
    const char *path,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    const void *pixels);

#endif
