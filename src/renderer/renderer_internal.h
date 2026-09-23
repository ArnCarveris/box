/**
 * @file renderer/renderer_internal.h
 * @brief Private renderer state shared between the renderer source files.
 */

#ifndef BOX_RENDERER_INTERNAL_H
#define BOX_RENDERER_INTERNAL_H

#include "gpu.h"
#include "renderer.h"
#include "cluster.h"
#include "frustum.h"
#include "bake.h"
#include "shadow.h"
#include "blur.h"
#include "effect_mip.h"
#include "effect_compute.h"
#include "effect_temporal.h"
#include "effect_texture.h"
#include "hiz.h"
#include "upscale.h"
#include "sky.h"
#include "particle.h"
#include "queue.h"

/** Faces of a cubemap */
#define BOX_ENV_FACES (6)

/** Edge length of the environment and prefiltered specular cubemaps */
#define BOX_ENV_SIZE (64)

/** Edge length of the irradiance cubemap */
#define BOX_ENV_IRRADIANCE_SIZE (16)

/** Edge length of the split sum BRDF lookup texture */
#define BOX_ENV_BRDF_SIZE (64)

/** Intermediate targets the post process passes of a frame share */
#define BOX_EFFECT_POOL_MAX (8)

/** Byte stride between two post process uniforms, the alignment a dynamic offset needs */
#define BOX_EFFECT_UNIFORM_STRIDE (256)

/** Format of the motion vector target, which holds a signed uv delta per texel */
#define BOX_EFFECT_MOTION_FORMAT (WGPUTextureFormat_RG16Float)

#define BOX_EFFECT_HDR_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Target formats a pass is compiled for: the view format and the float one */
#define BOX_EFFECT_FORMATS (2)

/** Format of the second attachment of the mesh pass.
 *
 * The attachment holds the world normal octahedrally in its first two channels
 * and the indirect diffuse and indirect specular fraction of the pixel in the
 * other two, which is what a pass changing the indirect light of a pixel needs.
 */
#define BOX_EFFECT_NORMAL_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Bytes between the uniforms of two environment passes */
#define BOX_ENV_STRIDE (256)

/** Bindings the environment takes at the start of the shared fragment bind group */
#define BOX_ENV_BINDINGS (4)

/** First binding of the scene copy in the shared fragment bind group */
#define BOX_SCENE_BINDING (BOX_ENV_BINDINGS + BOX_TEXTURE_PAGES + 1)

/** Entries of the shared fragment bind group */
#define BOX_MESH_BINDINGS (BOX_SCENE_BINDING + 2)

/** Prefiltered environments the renderer keeps, shared by the views that match one */
#define BOX_ENV_SLOTS (4)

/** Cull modes a sorted pipeline is built for: back faces culled or kept */
#define BOX_CULL_MODES (2)

/** Sorted pipelines with and without the normal attachment of a chain */
#define BOX_COVER_MODES (2)

/** The draw writes no normal attachment */
#define BOX_NORMALS_NONE (0)

/** The draw writes the world normal and the indirect fractions of its surface */
#define BOX_NORMALS_WRITE (1)

/** The draw folds its coverage into the indirect fractions of the surface behind */
#define BOX_NORMALS_COVER (2)

/** The draw composites a render target instead of shading a surface */
#define BOX_NORMALS_GROUP (3)

/** The composite also folds its coverage into the normal attachment of a chain */
#define BOX_NORMALS_GROUP_COVER (4)

/** Bytes of the uniform block holding the parameters of every material layer */
#define BOX_LAYER_UNIFORM_SIZE (BOX_LAYER_MAX * sizeof(box_layer_params_t))

/** Bytes of the uniform buffer the light modulator parameters live in */
#define BOX_MODULATOR_UNIFORM_SIZE (BOX_MODULATOR_MAX * sizeof(box_modulator_params_t))

/** Static instances a batch may hold before the renderer stops mirroring it on the CPU */
#define BOX_CULL_MIRROR_MAX (4096)

/** Static instances one upload of a packet writes at most, which bounds the staging run */
#define BOX_SCENE_UPLOAD_RUN (4096)

/** Bytes a buffer may hold when the adapter reports no limits, the WebGPU default */
#define BOX_DEVICE_BUFFER_DEFAULT (268435456ull)

/** Bytes a storage binding may cover when the adapter reports no limits */
#define BOX_DEVICE_STORAGE_DEFAULT (134217728ull)

/** Instances a buffer may hold at most, the range a slot index of an instance covers */
#define BOX_DEVICE_INSTANCES_MAX (0xFFFFFFFFull)

/** Byte stride between two culling uniforms, the alignment a dynamic offset needs */
#define BOX_CULL_UNIFORM_STRIDE (256)

/** Argument offset of a draw the CPU culled, which needs no indirect arguments */
#define BOX_CULL_NO_ARGS (0xFFFFFFFFu)

/** Byte stride between two occlusion uniforms, the alignment a dynamic offset needs */
#define BOX_HIZ_UNIFORM_STRIDE (512)

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

/** Uniform of the bake dispatch */
typedef struct box_bake_uniform_t {
    uint32_t job_count; /**< Probe jobs the dispatch runs */
    uint32_t pad[3];    /**< Padding to the 16 byte GPU stride */
} box_bake_uniform_t;

/** Frustum and range of one dispatch of the culling pass */
typedef struct box_cull_uniform_t {
    float planes[BOX_FRUSTUM_PLANES][4]; /**< Planes of the frustum of the view */
    uint32_t first;  /**< First instance of the source buffer the dispatch tests */
    uint32_t count;  /**< Instances the dispatch tests */
    uint32_t base;   /**< First instance of the visible buffer the dispatch writes */
    uint32_t arg;    /**< Index of the draw arguments the dispatch counts into */
    uint32_t caster; /**< The dispatch keeps the shadow casters of the range only */
    uint32_t pad[3]; /**< Padding to the 16 byte GPU stride */
    float clip[16];  /**< World to clip matrix of the view, for the pixel size test */
    float pixels[4]; /**< View width, height, minimum pixel size of an instance, unused */
} box_cull_uniform_t;

/** Frustum, matrices and pyramid extent of one dispatch of the occlusion pass */
typedef struct box_hiz_uniform_t {
    float clip[16];                      /**< World to clip matrix of the view */
    float prev[16];                      /**< World to clip matrix of the previous frame */
    float planes[BOX_FRUSTUM_PLANES][4]; /**< Planes of the frustum of the view */
    uint32_t range[4];  /**< First slot, instances, first visible slot and argument index */
    uint32_t screen[4]; /**< Pyramid width, height, levels and the mode of the dispatch */
    float pixels[4];    /**< View width, height, minimum pixel size of an instance, unused */
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
    WGPUTexture color;         /**< Color texture of the target */
    WGPUTextureView view;      /**< Render attachment view of the color texture */
    WGPUBindGroup composite;   /**< Mesh group that binds the color in the scene slot */
    uint32_t composite_epoch;  /**< Environment epoch that group was built for */
    uint32_t width;            /**< Width in pixels */
    uint32_t height;           /**< Height in pixels */
    bool alive;                /**< Handle is in use */
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
    WGPUBuffer hiz_read;             /**< Readback of one level of the pyramid, for the LOD walk */
    uint32_t hiz_read_size;          /**< Bytes the readback buffer holds */
    uint32_t hiz_read_row;           /**< Bytes one row of the readback takes */
    uint32_t hiz_read_level;         /**< Level of the pyramid the readback copies */
    bool hiz_read_busy;              /**< The readback is recorded or mapped */
    bool hiz_read_want;              /**< The view asked for the grid its LOD traversal walks */
    bool hiz_read_ready;             /**< The readback of this frame is waiting for a map */
    box_lod_depth_t hiz_depth;       /**< Last grid that arrived, with the matrix of its frame */
    float hiz_read_clip[16];         /**< Matrix the recorded readback belongs to */
    bool hiz_active;                 /**< The frame recorded occlusion dispatches for the view */
    WGPUBuffer view_buffer;          /**< Uniform buffer holding the view */
    WGPUBindGroup view_group;        /**< Bind group with the view and layer uniforms */
    WGPUBuffer sky_uniform;          /**< Atmosphere uniform of the view */
    WGPUBindGroup sky_group;         /**< Bind group of the sky pass of the view */
    uint32_t sky_epoch;              /**< Epoch of the tables that bind group was built for */
    WGPUBuffer lights;               /**< Light storage buffer of the view */
    uint32_t light_capacity;         /**< Lights the light buffer of the view can hold */
    WGPUBuffer clusters;             /**< Light offset and count per cluster of the view */
    WGPUBuffer light_indices;        /**< Light indices of every cluster of the view */
    WGPUBindGroup cluster_group;     /**< View, lights and grid of its compute pass */
    WGPUBindGroup light_group;       /**< Lights and grid the mesh shader reads for it */
    box_cluster_t grid;              /**< Grid its cluster buffers were built for */
    box_vec_t light_snapshot;        /**< box_light_t the light buffer was last written with */
    float cluster_view[16];          /**< View matrix the clusters were last assigned with */
    float cluster_proj[16];          /**< Projection the clusters were last assigned with */
    bool cluster_dirty;              /**< The cluster assignment must run this frame */
    bool scene_needed;               /**< A sorted draw of the view samples the copy */
    WGPUTexture scene;               /**< Copy of the opaque image of the view */
    WGPUTextureView scene_view;      /**< View of its whole blur chain */
    WGPUTextureView scene_mips[BOX_BLUR_LEVELS_MAX]; /**< Attachment view of one of its levels */
    WGPUBindGroup scene_down[BOX_BLUR_LEVELS_MAX]; /**< Downsample source, the finer level */
    WGPUBindGroup scene_up[BOX_BLUR_LEVELS_MAX];   /**< Upsample source, the coarser level */
    uint32_t scene_mips_count;       /**< Levels its blur chain holds */
    WGPUTextureFormat scene_format;  /**< Format of that copy, which follows the scene target */
    WGPUBuffer setup_compute_state;  /**< State buffer a compute setup pass keeps across frames */
    WGPUBuffer setup_compute_work;   /**< Work buffer of the compute setup pass */
    uint32_t setup_compute_size[2];  /**< Bytes of the work buffer and of the state buffer */
    WGPUTexture setup_mip;           /**< Pyramid of the mip chain setup pass */
    WGPUTextureView setup_mip_view;  /**< View of the finest level of the pyramid */
    WGPUTextureView setup_mip_mips[BOX_EFFECT_MIP_LEVELS_MAX]; /**< Attachment view of a level */
    WGPUBindGroup setup_mip_down[BOX_EFFECT_MIP_LEVELS_MAX]; /**< Downsample source, finer level */
    WGPUBindGroup setup_mip_up[BOX_EFFECT_MIP_LEVELS_MAX];   /**< Upsample source, coarser level */
    uint32_t setup_mip_levels;       /**< Levels the pyramid holds */
    uint32_t setup_mip_width;        /**< Width of the finest level in pixels */
    uint32_t setup_mip_height;       /**< Height of the finest level in pixels */
    WGPUTextureFormat setup_mip_format; /**< Format of the pyramid */
    WGPUBuffer setup_mip_uniform;    /**< Effect uniform buffer the bind groups were built with */
    WGPUTexture setup_mip_history[2]; /**< Finest levels blended with the previous frame */
    WGPUTextureView setup_mip_history_view[2]; /**< Views of the two history textures */
    uint32_t setup_mip_history_index; /**< History texture the last frame wrote */
    uint32_t setup_mip_history_frame; /**< Frame the history was last written */
    uint32_t setup_mip_history_slot;  /**< Uniform slot of the pass that wrote the history */
    bool setup_mip_history_valid;     /**< The history holds an image */
    WGPUTexture setup_temporal_work;  /**< Raw image of the temporal setup pass */
    WGPUTextureView setup_temporal_work_view; /**< View of the raw image */
    WGPUTexture setup_temporal_hist[2]; /**< Resolved images of this and the previous frame */
    WGPUTextureView setup_temporal_hist_view[2]; /**< Views of the two resolved images */
    uint32_t setup_temporal_width;    /**< Width of the temporal targets in pixels */
    uint32_t setup_temporal_height;   /**< Height of the temporal targets in pixels */
    WGPUTextureFormat setup_temporal_format; /**< Format of the temporal targets */
    uint32_t setup_temporal_slot;     /**< Resolved image the next frame writes */
    bool setup_temporal_valid;        /**< A resolve was recorded since the targets were made */
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
    uint32_t shadow_patch;           /**< Cascades whose static layer gets added casters drawn */
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
    box_vec_t mirror;                /**< box_instance_t copy of the static buffer, for the CPU */
    bool mirror_valid;               /**< The mirror holds every static instance of the mesh */
    box_vec_t cutout_slots;          /**< uint8_t per slot, 1 when the instance can discard */
    uint32_t cutout_count;           /**< Slots whose instance can discard fragments */
    box_vec_t transition_index;      /**< uint32_t entry per slot, BOX_TRANSITION_NONE for none */
    box_vec_t transition_slots;      /**< uint32_t slots of the mesh that carry a transition */
    bool transition_sorted;          /**< The slots are in ascending order, so uploads coalesce */
    bool alive;                      /**< Handle is in use */
} box_gpu_mesh_t;

/** One page of the texture pager: a 2D array texture of one size class */
typedef struct box_gpu_texpage_t {
    WGPUTexture texture;   /**< Array texture holding the layers of the page */
    WGPUTextureView view;  /**< Array view the mesh shader samples */
    uint32_t cls;          /**< Size class the page holds */
    uint32_t layers;       /**< Layers the array texture holds */
    uint64_t ready[BOX_TEXTURE_PAGE_LAYERS / 64]; /**< Layers holding an image or the fallback */
    bool used;             /**< The page holds an array texture */
} box_gpu_texpage_t;

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

/** Intermediate color target the post process passes share */
typedef struct box_gpu_pool_t {
    WGPUTexture texture; /**< The target, NULL until the entry is first used */
    WGPUTextureView view; /**< View of the target */
    uint32_t width;      /**< Width in pixels */
    uint32_t height;     /**< Height in pixels */
    WGPUTextureFormat format; /**< Format of the target */
    bool in_use;         /**< A pass of the current view holds it */
} box_gpu_pool_t;

/** Pipelines of one effect shader, built the first time the pass writes a format */
typedef struct box_gpu_effect_t {
    WGPURenderPipeline pipeline[BOX_EFFECT_FORMATS]; /**< Pipeline per target format */
    bool tried[BOX_EFFECT_FORMATS];                  /**< The format was compiled once */
} box_gpu_effect_t;

/** Uniform block of one post process pass */
typedef struct box_effect_uniform_t {
    float params[BOX_EFFECT_PARAM_MAX]; /**< Parameters of its parameter component */
    float size[4];                      /**< Target width, height and their reciprocals */
    float eye[4];                       /**< Camera position, w is the display encode flag */
    float frame[4];                     /**< Delta, time, and the decode and encode flags */
    float clip[16];                     /**< World to clip of the view, for a pass that marches */
    float inv_clip[16];                 /**< Clip to world of the view */
} box_effect_uniform_t;

/** Adapted exposure of a view, which its compute passes keep between frames */
typedef struct box_effect_storage_t {
    float value[4]; /**< Values a compute setup pass carries to the next frame */
} box_effect_storage_t;

/** Uniform block of the motion vector pass */
typedef struct box_motion_uniform_t {
    float inv_clip[16];  /**< Clip to world of this frame */
    float prev_clip[16]; /**< World to clip of the previous frame */
} box_motion_uniform_t;

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
    WGPURenderPipeline pipeline[BOX_EFFECT_FORMATS]; /**< Lit mesh pipeline per target format */
    WGPUShaderModule mesh_shader;    /**< Module every mesh pipeline is built from */
    WGPUPipelineLayout mesh_layout;  /**< Pipeline layout shared by the mesh pipelines */
    WGPURenderPipeline sorted_pipelines[BOX_BLEND_COUNT][BOX_CULL_MODES]
        [BOX_EFFECT_FORMATS][BOX_COVER_MODES];
        /**< Sorted pass, per blend mode, cull mode, target format and coverage */
    WGPURenderPipeline group_pipelines[BOX_EFFECT_FORMATS][BOX_COVER_MODES];
        /**< Composite of a group, per target format and coverage */
    WGPUBuffer sorted;               /**< Instance buffer of the sorted pass */
    uint32_t sorted_capacity;        /**< Entries the sorted instance buffer can hold */
    WGPUSampler scene_sampler;       /**< Linear sampler of the copy, full level range */
    WGPUBindGroupLayout blur_layout; /**< Source texture and sampler of a blur pass */
    WGPURenderPipeline blur_down[BOX_EFFECT_FORMATS]; /**< Thirteen tap downsample of a level */
    WGPURenderPipeline blur_up[BOX_EFFECT_FORMATS]; /**< Eight tap tent upsample, mixed up */
    WGPUTexture upscale;             /**< Scaled image the surface views draw into */
    WGPUTextureView upscale_view;    /**< View of that image */
    WGPUBindGroup upscale_group;     /**< Binding of that image and its sampler */
    WGPUSampler upscale_sampler;     /**< Linear sampler the magnifying pass reads with */
    WGPUBindGroupLayout upscale_layout; /**< Layout of the magnifying pass */
    WGPURenderPipeline upscale_pipeline; /**< Full screen pass that magnifies the image */
    uint32_t upscale_width;          /**< Width the scaled image has */
    uint32_t upscale_height;         /**< Height it has */
    WGPUBuffer layer_buffer;         /**< Parameters of every material layer */
    WGPUBuffer modulator_buffer;     /**< Parameter block of every light modulator */
    box_vec_t shaders;               /**< char* of the preprocessed WGSL, indexed by handle */
    box_gpu_pool_t pool[BOX_EFFECT_POOL_MAX]; /**< Intermediate targets the passes share */
    box_gpu_pool_t *effect_scene;    /**< Target the passes of the current view read first */
    box_gpu_pool_t *effect_normal;   /**< World normal target of the current view, or NULL */
    box_gpu_pool_t *effect_motion;   /**< Motion vector target of the current view, or NULL */
    uint32_t effect_run[BOX_EFFECT_MAX]; /**< Passes of the current view that have a pipeline */
    uint32_t effect_run_count;       /**< Passes in that list */
    bool shader_failed;              /**< A shader did not compile and was already reported */
    bool initialized;                /**< The pipelines were built from the builtin sources */
    WGPURenderPipeline solid_pipeline[BOX_EFFECT_FORMATS][2];
    bool solid_supported;
    WGPURenderPipeline normal_pipeline[BOX_EFFECT_FORMATS]; /**< Mesh pipeline writing normals */
    box_vec_t effect_pipelines;      /**< box_gpu_effect_t indexed by shader handle */
    WGPUBindGroupLayout effect_layout;  /**< Uniforms, inputs and sampler of an effect pass */
    WGPUPipelineLayout effect_pipeline_layout; /**< Pipeline layout of every effect pass */
    WGPUSampler effect_sampler;      /**< Linear clamped sampler the passes read through */
    WGPUBuffer effect_uniform;       /**< Parameters of the passes, one dynamic slot each */
    uint32_t effect_uniform_slots;   /**< Slots the parameter buffer holds */
    WGPUTexture effect_dummy;        /**< One texel color target bound to unused inputs */
    WGPUTextureView effect_dummy_view; /**< View of that texture */
    WGPUTexture effect_depth_dummy;  /**< One texel depth target bound to an unused input */
    WGPUTextureView effect_depth_dummy_view; /**< View of that texture */
    WGPUTexture effect_cube_dummy;   /**< One texel cubemap bound when a view has no environment */
    WGPUTextureView effect_cube_dummy_view; /**< Cube view of that texture */
    WGPUTexture effect_texture[BOX_EFFECT_TEXTURE_MAX];
    WGPUTextureView effect_texture_view[BOX_EFFECT_TEXTURE_MAX];
    WGPUTexture effect_texture_dummy;
    WGPUTextureView effect_texture_dummy_view;
    WGPUBindGroupLayout setup_temporal_layout;
    WGPURenderPipeline setup_temporal_first;
    WGPURenderPipeline setup_temporal_second;
    uint32_t setup_temporal_shader;
    WGPUComputePipeline setup_compute_first;
    WGPUComputePipeline setup_compute_second;
    WGPUBindGroupLayout setup_compute_layout;
    uint32_t setup_compute_shader;
    WGPUBindGroupLayout setup_mip_layout;
    WGPURenderPipeline setup_mip_pipeline[4];
    uint32_t setup_mip_shader;
    WGPUBindGroupLayout motion_layout; /**< Uniform and depth target of the motion pass */
    WGPURenderPipeline motion_pipeline; /**< Pass that writes the motion vectors of a view */
    WGPUBuffer motion_uniform;       /**< Matrices of the motion pass */
    WGPURenderPipeline lines_pipeline[BOX_EFFECT_FORMATS]; /**< Line list pass per target format */
    bool lines_tried[BOX_EFFECT_FORMATS]; /**< The line pipeline of a format was built once */
    WGPUBuffer lines_buffer;         /**< Vertex buffer of the line list of the frame */
    uint32_t lines_capacity;         /**< Vertices the line buffer can hold */
    uint32_t lines_count;            /**< Vertices the line buffer holds this frame */
    volatile bool device_error;      /**< The device reported an error since it was cleared */
    volatile uint32_t device_errors; /**< Device errors reported since the device was created */
    WGPULimits limits;               /**< Limits the device was created with */
    WGPUBindGroupLayout view_layout; /**< Layout of the view uniform bind group */
    box_vec_t meshes;                /**< box_gpu_mesh_t indexed by handle */
    box_vec_t views;                 /**< box_gpu_view_t, one per view of the packet */
    box_gpu_view_t *v;               /**< View the renderer is recording, never NULL in a pass */
    box_vec_t targets;               /**< box_gpu_target_t indexed by target handle */
    box_gpu_env_t envs[BOX_ENV_SLOTS]; /**< Prefiltered environments the views share */
    box_sky_gpu_t sky;               /**< Tables and pipelines of the atmosphere */
    uint32_t env_clock;              /**< Counts the environments bound, to pick an entry */
    box_vec_t static_staging;        /**< box_instance_t run a static upload coalesces into */
    box_transition_pool_t transitions; /**< Entries and lanes of the instances still moving */
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
    uint32_t env_epoch;              /**< Rises when an environment group is built again */
    box_bake_state_t bake;
    WGPUBuffer bake_uniform;
    WGPUBuffer bake_lights;
    WGPUBuffer bake_emitters;
    WGPUBuffer bake_data;
    WGPUBuffer bake_probes;
    WGPUBuffer bake_jobs;
    uint32_t bake_light_capacity;
    uint32_t bake_emitter_capacity;
    uint32_t bake_data_capacity;
    uint32_t bake_probe_capacity;
    uint32_t bake_job_capacity;
    WGPUBindGroupLayout bake_layout;
    WGPUBindGroupLayout bake_mesh_layout;
    WGPUBindGroupLayout probe_layout;
    WGPUBindGroup bake_group;
    WGPUBindGroup bake_work_group;
    WGPUBindGroup probe_group;
    WGPUComputePipeline bake_pipeline;
    box_gpu_texpage_t texpage[BOX_TEXTURE_PAGES]; /**< Array textures of the texture pager */
    WGPUSampler texture_sampler;     /**< Sampler of every page, with the anisotropy setting */
    uint64_t texture_layout;         /**< Hash of the page layout the bind group was built for */
    uint32_t texture_max_layers;     /**< Layers a page may hold, queried from the device */
    WGPUTexture texture_dummy;       /**< Single texel array texture of an unopened page */
    WGPUTextureView texture_dummy_view; /**< View of the single texel array texture */
    bool texture_limit_logged;       /**< The device layer limit was reported once */
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
    box_particle_gpu_t particles;    /**< Pools, pipelines and layouts of the particles */
    box_renderer_stats_t stats;      /**< Instance counts of the last frame */
    box_timing_t timing;             /**< Passes of the frame in flight and of the resolved one */
    WGPUQuerySet timing_set;         /**< Timestamps the passes of a frame write */
    WGPUBuffer timing_resolve;       /**< Resolved timestamps of a recorded frame */
    WGPUBuffer timing_readback;      /**< Mappable copy of those timestamps */
    volatile bool timing_enabled;    /**< The world asked for the timings of every pass */
    bool timing_supported;           /**< The device has the timestamp query feature */
    bool timing_busy;                /**< The readback buffer is waiting for its map */
    bool timing_encoded;             /**< The resolve of that frame was encoded */
    uint32_t timing_wait;            /**< Frames that map has been waited for */
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

/** Instances one buffer of the device may hold, from the limits it was made with.
 *
 * @param r The renderer
 * @param storage The buffer is bound as a storage buffer, which is capped lower
 * @return The instance count
 */
uint32_t box_device_instances(
    const box_renderer_t *r,
    bool storage);

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

/** Whether the target of a view is a format the hardware encodes for the pass.
 *
 * @param r The renderer
 * @return True when the color format of the frame is an sRGB one
 */
static inline bool box_renderer_srgb(
    const box_renderer_t *r)
{
    return r->format == WGPUTextureFormat_BGRA8UnormSrgb ||
        r->format == WGPUTextureFormat_RGBA8UnormSrgb;
}

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

/** Build a mesh bind group that binds a texture in the slot of the scene copy.
 *
 * The composite of a group reads the target its subtree was rendered into out
 * of that slot, so it needs the environment group of its view with one binding
 * swapped instead of a bind group layout of its own.
 *
 * @param r The renderer
 * @param scene The texture view the scene slot binds
 * @return The bind group, owned by the caller
 */
WGPUBindGroup box_environment_scene_group(
    box_renderer_t *r,
    WGPUTextureView scene);

/** Mesh pipeline of an opaque draw, for the scene target and normals of the current view.
 *
 * @param r The renderer
 * @param solid No instance of the draw discards, so the solid variant can be used
 * @return The pipeline
 */
WGPURenderPipeline box_frame_opaque_pipeline(
    box_renderer_t *r,
    bool solid);

/** Create a variant of the mesh pipeline.
 *
 * @param r The renderer
 * @param stride Bytes between two instances in the instance buffer
 * @param offset Bytes from an instance buffer element to its instance data
 * @param cull Faces the rasterizer drops
 * @param depth_write The draw writes the depth buffer
 * @param blend Blend state of the color target, NULL for no blending
 * @param hdr The draw writes the float scene target of a view with a chain
 * @param normals One of the BOX_NORMALS constants, what it writes there
 * @return The pipeline
 */
WGPURenderPipeline box_frame_pipeline(
    box_renderer_t *r,
    uint64_t stride,
    uint64_t offset,
    WGPUCullMode cull,
    bool depth_write,
    const WGPUBlendState *blend,
    bool hdr,
    uint32_t normals);

/** Render and present one packet.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_frame_render(
    box_renderer_t *r,
    box_packet_t *p);

/** Apply the resource and instance changes of a packet to GPU buffers.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_scene_apply(
    box_renderer_t *r,
    box_packet_t *p);

/** Get the mesh entry for a handle, growing the table as needed.
 *
 * @param r The renderer
 * @param handle Mesh handle
 * @return The mesh entry
 */
box_gpu_mesh_t* box_scene_mesh(
    box_renderer_t *r,
    uint32_t handle);

/** Write the staged run of static instances to its mesh buffer and clear it.
 *
 * @param r The renderer
 * @param mesh The mesh the run belongs to
 * @param first The slot the run starts at
 */
void box_scene_flush_statics(
    box_renderer_t *r,
    uint32_t mesh,
    uint32_t first);

/** Keep the CPU mirror of a static buffer in step with an instance update.
 *
 * The mirror feeds the CPU culling of small batches. A batch that grows past
 * the mirror limit drops it and is culled by the compute pass from then on.
 *
 * @param m The mesh entry
 * @param slot The instance slot
 * @param data The new instance data
 */
void box_scene_mirror(
    box_gpu_mesh_t *m,
    uint32_t slot,
    const box_instance_t *data);

/** Start or replace the transitions of a static instance slot.
 *
 * @param r The renderer
 * @param m The mesh entry the slot belongs to
 * @param p The packet holding the command and its records
 * @param cmd The command
 * @param data The instance data the records move
 */
void box_transition_start(
    box_renderer_t *r,
    box_gpu_mesh_t *m,
    const box_packet_t *p,
    const box_transition_cmd_t *cmd,
    const box_instance_t *data);

/** Forget the transitions of a static instance slot.
 *
 * @param r The renderer
 * @param m The mesh entry the slot belongs to
 * @param slot The instance slot
 */
void box_transition_stop(
    box_renderer_t *r,
    box_gpu_mesh_t *m,
    uint32_t slot);

/** Forget every transition of a mesh.
 *
 * @param r The renderer
 * @param m The mesh entry
 */
void box_transition_clear(
    box_renderer_t *r,
    box_gpu_mesh_t *m);

/** Evaluate the transitions the renderer holds and upload what they changed.
 *
 * @param r The renderer
 * @param p The packet, which receives the instances that finished
 */
void box_transition_frame(
    box_renderer_t *r,
    box_packet_t *p);

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

/** Map the depth readback of every view that recorded one.
 *
 * @param r The renderer
 */
void box_hiz_publish(
    box_renderer_t *r);

/** Copy the coarse depth of the first view into the packet it is returned with.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_hiz_depth(
    box_renderer_t *r,
    box_packet_t *p);

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

/** Create the uniform buffer the light modulator parameters live in.
 *
 * @param r The renderer
 */
void box_modulators_init(
    box_renderer_t *r);

/** Upload the modulator parameter blocks a packet carries.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_modulators_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** Release the modulator uniform buffer.
 *
 * @param r The renderer
 */
void box_modulators_fini(
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

/** Create the bind group layouts, sampler and dummies of the post process passes.
 *
 * @param r The renderer
 */
void box_effect_init(
    box_renderer_t *r);

/** Release the pipelines, the target pool and the resources of the passes.
 *
 * @param r The renderer
 */
void box_effect_fini(
    box_renderer_t *r);

/** Upload the line list of a packet into the vertex buffer of the renderer.
 *
 * A packet without lines allocates nothing and leaves the buffer alone, so a
 * frame that draws no debug geometry pays for none of this.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_lines_upload(
    box_renderer_t *r,
    const box_packet_t *p);

/** Draw the line list of the frame over the image of a view.
 *
 * Runs after the opaque and sorted draws and before the effect chain, depth
 * tested without depth writes, using the view uniform.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param attachment The render target view the view draws into
 * @param hdr The passes of the view write the float scene target
 */
void box_lines_record(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTextureView attachment,
    bool hdr);

/** Release the pipelines and the vertex buffer of the line pass.
 *
 * @param r The renderer
 */
void box_lines_fini(
    box_renderer_t *r);

/** Drop the cached pipeline of a shader handle so the next pass rebuilds it.
 *
 * @param r The renderer
 * @param handle The shader handle
 */
void box_effect_invalidate(
    box_renderer_t *r,
    uint32_t handle);

/** Size the uniform buffer of the passes for a packet.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_effect_frame(
    box_renderer_t *r,
    const box_packet_t *p);

/** Take the targets the chain of a view needs and count the passes that can run.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 * @return The number of passes that will run, 0 when the view draws directly
 */
uint32_t box_effect_prepare(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p);

/** Record the pass that writes the motion vectors of a view.
 *
 * @param r The renderer
 * @param view The view
 * @param index Index of the view in the packet
 * @param encoder The command encoder
 */
void box_effect_motion(
    box_renderer_t *r,
    const box_view_t *view,
    uint32_t index,
    WGPUCommandEncoder encoder);

/** Record the chain of a view and release the targets it took.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 * @param encoder The command encoder
 * @param dst The render target view the last pass writes
 */
void box_effect_run(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTextureView dst);

/** Create the bind group layout of the compute setup passes.
 *
 * @param r The renderer
 */
void box_effect_compute_init(
    box_renderer_t *r);

/** Release the pipelines and the layout of the compute setup passes.
 *
 * @param r The renderer
 */
void box_effect_compute_fini(
    box_renderer_t *r);

/** Release the compute setup buffers of one view.
 *
 * @param v The view
 */
void box_effect_compute_view_fini(
    box_gpu_view_t *v);

/** State buffer of the current view, recreated when the pass asks for another size.
 *
 * @param r The renderer
 * @param fx The pass whose second buffer size and initial value it takes, or NULL
 * @return The state buffer, 16 bytes when the pass declared no size
 */
WGPUBuffer box_effect_storage(
    box_renderer_t *r,
    const box_effect_t *fx);

/** Record the two dispatches of the compute setup pass of a pass.
 *
 * @param r The renderer
 * @param fx The pass
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param src The image the setup pass reads
 * @param encoder The command encoder
 */
void box_effect_compute_run(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    box_gpu_pool_t *src,
    WGPUCommandEncoder encoder);

/** Create the bind group layout of the temporal setup passes.
 *
 * @param r The renderer
 */
void box_effect_temporal_init(
    box_renderer_t *r);

/** Release the pipelines and the layout of the temporal setup passes.
 *
 * @param r The renderer
 */
void box_effect_temporal_fini(
    box_renderer_t *r);

/** Release the temporal targets of one view.
 *
 * @param v The view
 */
void box_effect_temporal_view_fini(
    box_gpu_view_t *v);

/** Record the raw and the resolve pass of the temporal setup pass of a pass.
 *
 * @param r The renderer
 * @param fx The pass
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param encoder The command encoder
 * @return The resolved image of this frame, NULL when the pipelines could not be built
 */
WGPUTextureView box_effect_temporal_run(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    WGPUCommandEncoder encoder);

/** Prefiltered environment cubemap of the current view.
 *
 * @param r The renderer
 * @return The cube view, a one texel dummy when the view has no environment
 */
WGPUTextureView box_environment_specular(
    box_renderer_t *r);

/** Create the bind group layout of the mip chain setup passes.
 *
 * @param r The renderer
 */
void box_effect_mip_init(
    box_renderer_t *r);

/** Release the pipelines and the layout of the mip chain setup passes.
 *
 * @param r The renderer
 */
void box_effect_mip_fini(
    box_renderer_t *r);

/** Release the pyramid of one view.
 *
 * @param v The view
 */
void box_effect_mip_view_fini(
    box_gpu_view_t *v);

/** Fill the pyramid of the current view from the image of a pass.
 *
 * @param r The renderer
 * @param fx The pass, whose parameters the filter reads
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param frame Frame number of the packet, which decides whether the history is recent
 * @param src The image the pyramid is built from
 * @param encoder The command encoder
 * @return The view of the finest level or of its history blend, NULL when not built
 */
WGPUTextureView box_effect_mip_run(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    uint32_t frame,
    box_gpu_pool_t *src,
    WGPUCommandEncoder encoder);

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
 * @param format Format of the copy, which follows the target the scene passes write
 */
void box_transparent_resize(
    box_renderer_t *r,
    WGPUTextureFormat format);

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
 * @param out Entries to fill, one per page of the pager plus the sampler
 * @param base First binding of the texture half
 */
void box_texture_bind_layout(
    WGPUBindGroupLayoutEntry *out,
    uint32_t base);

/** Fill the texture half of the shared fragment bind group.
 *
 * @param r The renderer
 * @param out Entries to fill, one per page of the pager plus the sampler
 * @param base First binding of the texture half
 */
void box_texture_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base);

/** Upload the textures of a packet, growing the array texture of their page.
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

/** Create the sampler, uniform and layouts of the atmosphere.
 *
 * @param r The renderer
 */
void box_sky_init(
    box_renderer_t *r);

/** Write the uniform of a view and record the table passes that are out of date.
 *
 * The tables are shared, so only the first view of a frame that finds them
 * behind the generation of the packet records them.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 * @param encoder The command encoder
 */
void box_sky_update(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder);

/** Draw the sky and the aerial perspective of a view over what it rendered.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 * @param encoder The command encoder
 * @param attachment The color target the view drew into
 */
void box_sky_record(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTextureView attachment);

/** Check whether the environment of a view comes from the atmosphere.
 *
 * @param r The renderer
 * @param view The view
 * @return true when the sky view table fills its environment cubemap
 */
bool box_sky_environment(
    box_renderer_t *r,
    const box_view_t *view);

/** Fill one face of an environment cubemap from the sky view table.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param target The face to fill
 * @param index Index of the face in the environment uniform buffer
 */
void box_sky_environment_face(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTextureView target,
    uint32_t index);

/** Drop the pipelines a shader handle of the atmosphere built.
 *
 * @param r The renderer
 * @param handle The shader slot that changed
 */
void box_sky_invalidate(
    box_renderer_t *r,
    uint32_t handle);

/** Release the uniform and bind group the atmosphere kept for a view.
 *
 * @param v The view
 */
void box_sky_view_release(
    box_gpu_view_t *v);

/** Release the tables, pipelines and bind groups of the atmosphere.
 *
 * @param r The renderer
 */
void box_sky_fini(
    box_renderer_t *r);

/** Rebuild the bake pipeline from the current bake shader.
 *
 * @param r The renderer
 */
void box_bake_reload(
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
