/**
 * @file render/gpu_types.h
 * @brief Constants and state types of the webgpu backend.
 *
 * Included by gpu.h, which declares the functions of the backend sources.
 */

#ifndef BOX_RENDER_GPU_TYPES_H
#define BOX_RENDER_GPU_TYPES_H

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
#define BOX_GPU_BLIT_FORMATS (8)

/** Format of sampled textures, which albedo and emissive layers read through an sRGB view */
#define BOX_GPU_TEXTURE_FORMAT (WGPUTextureFormat_RGBA8Unorm)

/** Format of the view of a texture that decodes display encoded colors */
#define BOX_GPU_TEXTURE_SRGB (WGPUTextureFormat_RGBA8UnormSrgb)

/** Format of single channel sampled textures, which every layer reads as stored */
#define BOX_GPU_TEXTURE_SINGLE (WGPUTextureFormat_R8Unorm)

/** TextureWrap modes the samplers are created for */
#define BOX_GPU_WRAPS (3)

/** Anisotropy levels the samplers are created for: 1, 2, 4, 8 and 16 samples */
#define BOX_GPU_ANISOTROPY_LEVELS (5)

/** Bytes a row of a texture to buffer copy is aligned to */
#define BOX_GPU_ROW_ALIGN (256)

/** Depth format of the views, cleared to 0 and compared with greater for reversed Z */
#define BOX_GPU_DEPTH_FORMAT (WGPUTextureFormat_Depth32Float)

/** Bytes between the uniforms of two views, a multiple of the dynamic offset alignment */
#define BOX_GPU_VIEW_STRIDE (1024)

/** Bytes between the matrices of two cascades, the dynamic offset alignment of webgpu */
#define BOX_GPU_CASCADE_STRIDE (256)

/** Bytes of the uniforms of one cascade, tile or octant of the shadow pass, Cascade of
 * etc/shaders/shadow.wgsl: the matrix, the light of a point light tile and its octant, its near
 * and far plane */
#define BOX_GPU_SHADOW_UNIFORM_SIZE (sizeof(mat4) + 2 * sizeof(vec4))

/** Depth format of the shadow maps, cleared to 0 and compared with greater for reversed Z */
#define BOX_GPU_SHADOW_FORMAT (WGPUTextureFormat_Depth32Float)

#define BOX_GPU_SHADOW_VARIANTS (4)

/** Color format of the HDR target a view with effects renders into */
#define BOX_GPU_HDR_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Effect passes, HDR targets and the state effects keep across frames */
typedef struct box_gpu_effects_t box_gpu_effects_t;

/** Pipelines of the ambient occlusion (Gtao) and its images per target and view */
typedef struct box_gpu_gtao_t box_gpu_gtao_t;

/** Pipelines and white fallback of the opaque snapshot of transmission */
typedef struct box_gpu_transmission_t box_gpu_transmission_t;

/** Opaque snapshot of a target that transmissive draws sample, with its blur pyramid */
typedef struct box_gpu_snapshot_t box_gpu_snapshot_t;

/** Opaque snapshots a target keeps, one per color format its views render in: the format of
 * the target for views without effects, BOX_GPU_HDR_FORMAT for views with effects */
#define BOX_GPU_SNAPSHOT_FORMATS (2)

/** Edge in texels of the shadow map of the probe captures */
#define BOX_GPU_CAPTURE_SHADOW_RESOLUTION (2048)

/** Texels the capture cascade reaches beyond the bounding sphere of the static bounds, at least
 * the filter footprint and normal offset of the capture lookup (box_gpu_shadow_margin) */
#define BOX_GPU_CAPTURE_SHADOW_MARGIN (8)

/** Textures, pipelines and timing of the probe captures */
typedef struct box_gpu_bake_t box_gpu_bake_t;

/** LUTs, pipelines and timing of the atmosphere passes */
typedef struct box_gpu_atmos_t box_gpu_atmos_t;

/** Pipelines and frame buffers of the particle passes */
typedef struct box_gpu_particles_t box_gpu_particles_t;

/** Pipelines and the buffer of the draws of the fog volumes */
typedef struct box_gpu_fog_volumes_t box_gpu_fog_volumes_t;

/** Pipelines and the buffer of the draws of the planet atmosphere shells */
typedef struct box_gpu_planets_t box_gpu_planets_t;

/** Pipelines and the uniform buffer of the debug passes of the views */
typedef struct box_gpu_debug_t box_gpu_debug_t;

/** Images of the planar reflections of the views, per target, view and plane slot */
typedef struct box_gpu_planar_t box_gpu_planar_t;

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
    box_gpu_snapshot_t *snapshots[BOX_GPU_SNAPSHOT_FORMATS]; /**< Opaque snapshots of the
                                     *   format of the target, then of BOX_GPU_HDR_FORMAT, NULL
                                     *   until a view draws a transmissive shape in that format */
} box_gpu_target_t;

/** A compiled shader module */
typedef struct box_gpu_shader_t {
    WGPUShaderModule module;        /**< The module */
    int32_t revision;               /**< Times the module was compiled under its handle */
} box_gpu_shader_t;

/** Vertex and index buffers of a mesh */
typedef struct box_gpu_mesh_t {
    WGPUBuffer surface_assets;      /**< Surface assets the shaders read, one vec4 without any */
    uint64_t surface_asset_size;    /**< Bytes of the surface assets */
    bool assets;                    /**< The mesh has surface assets, so its draws bind them */
    WGPUBuffer buffer;              /**< Mesh pool page buffer with the vertices and indices */
    int32_t page;                   /**< Page of the mesh pool */
    uint64_t offset;                /**< Offset of the vertices in the page buffer */
    uint64_t alloc_size;            /**< Bytes allocated from the page */
    uint64_t vertex_size;           /**< Bytes of the vertices */
    uint64_t index_offset;          /**< Offset of the indices in the page buffer */
    uint64_t index_size;            /**< Bytes of the indices */
    uint64_t id;                    /**< Id unique to this mesh, unlike its reusable handle */
    uint32_t index_count;           /**< Indices to draw */
    WGPUBindGroup wire;             /**< Vertices and indices as the storage buffers of a wire
                                     *   draw (debug_wire.wgsl), NULL until the first one */
    WGPUBuffer adjacency;           /**< Edge adjacency of the wire draws, one uint32_t per
                                     *   index (box_gpu_debug_adjacency), NULL when the mesh is
                                     *   not wireable */
    bool wireable;                  /**< The vertices and the adjacency fit in a storage buffer
                                     *   binding, so the mesh can be wired */
    bool wire_reported;             /**< The error of a mesh that is not wireable was logged */
    float center[3];                /**< Center of the box around the vertex positions */
    float extent[3];                /**< Half size of the box around the vertex positions */
} box_gpu_mesh_t;

/** Frames the CPU may submit before it waits for the GPU to finish the oldest */
#define BOX_GPU_FRAMES_IN_FLIGHT (2)

/** A free range of a page of the mesh pool */
typedef struct box_gpu_mesh_range_t {
    uint64_t offset;                /**< Offset of the range in the page buffer */
    uint64_t size;                  /**< Bytes of the range */
} box_gpu_mesh_range_t;

/** A buffer of the mesh pool that meshes take ranges of */
typedef struct box_gpu_mesh_page_t {
    WGPUBuffer buffer;              /**< The buffer, NULL when it could not be created */
    uint64_t size;                  /**< Bytes of the buffer */
    ecs_vec_t free;                 /**< box_gpu_mesh_range_t, the free ranges by offset */
} box_gpu_mesh_page_t;

/** A sampled texture with its mip chain and the two views the layers bind */
typedef struct box_gpu_texture_t {
    WGPUTexture texture;            /**< The texture, BOX_GPU_TEXTURE_FORMAT or
                                     *   BOX_GPU_TEXTURE_SINGLE */
    WGPUTextureView linear;         /**< View of every mip level as stored */
    WGPUTextureView srgb;           /**< View of every mip level decoded from sRGB, as stored
                                     *   for a single channel texture */
    uint32_t mips;                  /**< Mip levels of the texture */
    bool mipped;                    /**< The levels below the first were generated */
} box_gpu_texture_t;

/** Textures of the key of a material group: the layers, then the foam texture of a water batch */
#define BOX_GPU_MATERIAL_TEXTURES (BOX_RENDER_TEXTURE_SLOTS + 1)

/** Bind group of the texture layers and sampler of a batch, cached by its key */
typedef struct box_gpu_material_t {
    uint32_t textures[BOX_GPU_MATERIAL_TEXTURES]; /**< Texture handles of the layers, then of
                                     *   the foam texture of a water batch, 0 for the default */
    uint32_t wrap;                  /**< TextureWrap of the sampler */
    uint32_t anisotropy;            /**< Anisotropic samples of the sampler */
    WGPUBindGroup group;            /**< The bind group */
    struct box_gpu_material_t *next; /**< Next material with the same hash */
} box_gpu_material_t;

/** A texture array of one map of the layers of TextureBlend batches, a slice per layer, shared
 * by every blend with the same sources */
typedef struct box_gpu_blend_array_t {
    uint32_t map;                   /**< BOX_RENDER_BLEND_ALBEDO, _ORM, _NORMAL or _HEIGHT */
    uint32_t count;                 /**< Slices */
    uint32_t quality;               /**< Largest dimension of a slice */
    uint32_t sources[BOX_RENDER_BLEND_SLICES]; /**< Texture handle of each slice, 0 for the
                                     *   default of the map */
    uint32_t serials[BOX_RENDER_BLEND_SLICES]; /**< Serial of the image of each source it was
                                     *   built from, 0 for none: another serial rebuilds it */
    uint32_t version;               /**< Builds of the array, which the bind groups compare */
    WGPUTexture texture;            /**< The array, BOX_GPU_TEXTURE_FORMAT */
    WGPUTextureView view;           /**< 2D array view of every level, decoded from sRGB for the
                                     *   albedo */
    uint64_t bytes;                 /**< Bytes of the array with its mip levels */
    int32_t refs;                   /**< Bind groups that bind it */
    int64_t used;                   /**< Frame of the last packet that drew with it */
    struct box_gpu_blend_array_t *next; /**< Next array */
} box_gpu_blend_array_t;

/** Bind group of the texture_blend variant of the mesh pipeline: the arrays of the layers, the
 * mask, the emissive layer, the sampler and the uniforms of the layers. The heights array is
 * only made with a contrast above 0, else the group binds blend_flat_heights */
typedef struct box_gpu_blend_group_t {
    box_render_texture_blend_t blend; /**< The blend it binds */
    uint32_t emissive;              /**< Texture handle of the emissive layer, 0 for none */
    uint32_t wrap;                  /**< TextureWrap of the sampler */
    uint32_t anisotropy;            /**< Anisotropic samples of the sampler */
    box_gpu_blend_array_t *arrays[BOX_RENDER_BLEND_SOURCES]; /**< Its arrays, referenced, the
                                     *   heights NULL with a contrast of 0 */
    uint32_t versions[BOX_RENDER_BLEND_SOURCES]; /**< Versions of the arrays it binds */
    uint32_t mask_serial;           /**< Serial of the image of the mask it binds */
    uint32_t emissive_serial;       /**< Serial of the image of the emissive layer it binds */
    WGPUBuffer uniforms;            /**< BlendUniforms of mesh.wgsl */
    WGPUBindGroup group;            /**< The bind group, NULL until made */
    int64_t used;                   /**< Frame of the last packet that drew with it */
    int64_t checked;                /**< Frame its arrays and images were last checked */
    struct box_gpu_blend_group_t *next; /**< Next group */
} box_gpu_blend_group_t;

/** Bind group of an instance page with the surface assets of a mesh that has them */
typedef struct box_gpu_instance_group_t {
    uint32_t mesh;                  /**< Handle of the mesh */
    bool shading;                   /**< It binds the shading stream, instance_layout, else
                                     *   transform_layout */
    WGPUBuffer assets;              /**< Surface assets of the mesh it binds, held */
    WGPUBindGroup group;            /**< The bind group */
} box_gpu_instance_group_t;

/** A buffer the main thread created by handle */
typedef struct box_gpu_buffer_t {
    WGPUBuffer buffer;              /**< The buffer */
    uint64_t size;                  /**< Bytes of the buffer */
    uint32_t usage;                 /**< box_render_buffer_usage_t flags */
    WGPUBindGroup instances;        /**< Binds the streams of the instance page whose transform
                                     *   stream it is for the draws of meshes without surface
                                     *   assets (instance_layout), made on use */
    WGPUBindGroup transforms;       /**< The same without the shading stream, for the passes
                                     *   that read no material (transform_layout) */
    ecs_vec_t mesh_groups;          /**< box_gpu_instance_group_t, binds the page with the
                                     *   surface assets of a mesh that has them, made on use */
    int64_t counted;                /**< History ranges pass that last counted its words */
    uint64_t version;               /**< Write that last changed it, unique across buffers */
} box_gpu_buffer_t;

/** How box_gpu_copy runs a copy of a packet */
typedef enum box_gpu_copy_mode_t {
    BOX_GPU_COPY_SKIP,              /**< Invalid or empty, not run */
    BOX_GPU_COPY_DIRECT,            /**< From the source buffer into the destination buffer */
    BOX_GPU_COPY_STAGED             /**< Through the staging buffer: source and destination are
                                     *   the same buffer, or another copy of the packet writes a
                                     *   range of the source it reads */
} box_gpu_copy_mode_t;

/** Range a copy of a packet writes, sorted by buffer and offset to find the copies whose source
 * another copy writes */
typedef struct box_gpu_copy_write_t {
    uint32_t buffer;                /**< Handle of the buffer written */
    uint64_t start;                 /**< First byte written */
    uint64_t end;                   /**< Byte past the last one written, then the largest end
                                     *   of the ranges of the buffer up to this one */
} box_gpu_copy_write_t;

/** Levels of a depth pyramid at most, enough for a 16384 pixel axis */
#define BOX_GPU_HIZ_LEVELS (15)

/** Levels of a depth pyramid one dispatch of the pyramid shader writes */
#define BOX_GPU_HIZ_STEP (4)

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
 * Each instance page the view draws from has a range of words indexed by page slot, 0 for an
 * instance that was drawn, which grows with the page.
 *
 * A range is only a guess for the early phase: after a slot moved, a run of a batch moved to
 * other slots (it reads the words those slots had) or a page was replaced, the late phase still
 * tests and draws every instance the early phase missed.
 */
typedef struct box_gpu_cull_history_t {
    ecs_vec_t pages;                /**< box_gpu_hiz_page_t, each bound whole as storage */
    ecs_map_t ranges;               /**< Instance page handle to page, offset and words */
    int64_t stamp;                  /**< Cull pass the view was last culled in */
} box_gpu_cull_history_t;

/** Frustums of a cull group at most, one bit each of the u32 mask of a job */
#define BOX_GPU_CULL_GROUP_VIEWS (32)

/** Flag of a frustum that draws only shadow casters: opaque mesh batches that cast shadows */
#define BOX_GPU_CULL_CASTERS (1u << 0)

/** Flag of a frustum that tests the smallest projected size */
#define BOX_GPU_CULL_SIZE (1u << 1)

/** Flag of a frustum with an occlusion history, culled in an early and a late phase */
#define BOX_GPU_CULL_HISTORY (1u << 2)

/** Flag of a frustum of probe captures: the batches baked into the captures */
#define BOX_GPU_CULL_BAKE (1u << 3)

/** Flag of a frustum that draws nothing, a camera without a depth range */
#define BOX_GPU_CULL_EMPTY (1u << 4)

/** Flag of a frustum of a planar reflection: the 3D meshes of the scene and of its view that
 * are not transmissive, without occlusion */
#define BOX_GPU_CULL_REFLECT (1u << 5)

/** Flag of a frustum that skips the batches whose world bounds (box_render_batch_t.bounds) are
 * outside it or too small in it, CullSettings.bounds */
#define BOX_GPU_CULL_BOUNDS (1u << 6)

/** Bytes between the uniforms of two dispatches, the dynamic offset alignment of webgpu */
#define BOX_GPU_CULL_STRIDE (256)

/** Invocations of a workgroup of the cull shader */
#define BOX_GPU_CULL_GROUP (64)

/** Words of the indexed indirect draw arguments of one draw */
#define BOX_GPU_CULL_ARGS (5)

/** Scan words of a workgroup of a parallel dispatch: two mask words and its count or offset */
#define BOX_GPU_CULL_SCAN_WORDS (3)

/** Phase of a dispatch without occlusion: draw what is in the frustum */
#define BOX_GPU_CULL_PHASE_ALL (0)

/** Phase that draws what is in the frustum and was drawn the previous frame */
#define BOX_GPU_CULL_PHASE_EARLY (1)

/** Phase that draws what is in the frustum, not hidden and not drawn early */
#define BOX_GPU_CULL_PHASE_LATE (2)

/** Bytes of the frustums of a cull group in the uniforms, a multiple of the uniform stride */
#define BOX_GPU_CULL_FRUSTUMS_SIZE \
    ((uint32_t)sizeof(box_gpu_cull_frustum_t) * BOX_GPU_CULL_GROUP_VIEWS)

/** A frustum the cull pass tests instances against: a view, or a cascade of its shadows */
typedef struct box_gpu_cull_view_t {
    ecs_entity_t view;              /**< Entity of the view */
    int32_t slot;                   /**< Slot of the view in the view buffer */
    int32_t cascade;                /**< Cascade of its shadow map, -1 for the view itself */
    int32_t first_draw;             /**< First of its draws */
    int32_t draw_count;             /**< Batches it draws */
    int32_t group_index;            /**< Index of its cull group */
    int32_t frustum;                /**< Its bit in the masks of the jobs of the group */
    uint32_t flags;                 /**< BOX_GPU_CULL_CASTERS, _SIZE, _HISTORY, _BAKE, _EMPTY */
    uint32_t counter;               /**< Index of its counters */
    box_gpu_hiz_t *hiz;             /**< Pyramid of a view that culls by occlusion, else NULL */
    WGPUBindGroup group;            /**< History and pyramid, or the empty ones */
    bool report;                    /**< Its counts become the CullStatus, false for captures */
    int32_t skipped;                /**< Batches it does not draw by their bounds */
    int32_t skipped_untested;       /**< Instances of those without a job, which count as tested
                                     *   and frustum culled; the job of the frustum with a
                                     *   history still runs, so the GPU counts those */
} box_gpu_cull_view_t;

/** A batch a cull view draws: where its visible instances and its draw arguments are */
typedef struct box_gpu_cull_draw_t {
    int32_t batch;                  /**< Index of the batch in the packet */
    uint32_t args;                  /**< Index of its indexed indirect draw arguments */
    uint32_t late;                  /**< Arguments of its late phase, UINT32_MAX for none */
    uint32_t page;                  /**< Page that holds its visible slots */
    uint32_t base;                  /**< First of its entries in the visible slots of the page */
    uint32_t count;                 /**< Instances it tests */
    bool skip;                      /**< Its cull view skips the batch by its bounds: a pass
                                     *   records no draw but requests its pipelines, as when it
                                     *   draws no instance. Only the frustum with a history has
                                     *   arguments for it, else args and late are UINT32_MAX */
} box_gpu_cull_draw_t;

/** A dispatch of the cull pass: the parallel job of a batch of a group and phase, with the
 * frustums that draw it, or an ordered dispatch over the jobs of one instance page */
typedef struct box_gpu_cull_dispatch_t {
    int32_t group;                  /**< Index of its cull group */
    uint32_t buffer;                /**< Handle of the instance page */
    uint32_t groups;                /**< Workgroups to dispatch */
    bool ordered;                   /**< One invocation per job keeps the order of its slots */
    bool late;                      /**< Runs after the early draw, against the pyramid */
    uint32_t page;                  /**< Page that holds the visible slots of its jobs */
    WGPUBindGroup history;          /**< History page and pyramid, NULL for those of the group */
    uint32_t first;                 /**< Its job */
    uint32_t uniform;               /**< Byte offset of the uniforms of an ordered dispatch */
    uint32_t mask;                  /**< Frustums of the group that draw a parallel job */
    int32_t first_entry;            /**< First of the draws of its frustums in entries, in the
                                     *   order of their bits */
    uint32_t scan;                  /**< First scan word of a parallel job, then those of each
                                     *   of its frustums in the order of their bits */
} box_gpu_cull_dispatch_t;

/** The parallel jobs of one cull group, phase and page that one cull_scan and one cull_write
 * dispatch finish, per (job, frustum) entry */
typedef struct box_gpu_cull_table_t {
    int32_t group;                  /**< Index of the cull group */
    bool late;                      /**< Phase of its jobs */
    uint32_t page;                  /**< Page of its jobs */
    uint32_t uniform;               /**< Byte offset of its uniforms in the uniform buffer */
    uint32_t buffer;                /**< An instance page of its jobs, for group 1 */
    uint32_t entries;               /**< (job, frustum) entries, the workgroups of cull_scan */
    uint32_t groups;                /**< Workgroups of its jobs, those of cull_write */
} box_gpu_cull_table_t;

/** One cull_instances dispatch over consecutive parallel jobs of a table that share an
 * instance page and a history */
typedef struct box_gpu_cull_run_t {
    int32_t group;                  /**< Index of the cull group */
    bool late;                      /**< Phase of its jobs */
    uint32_t page;                  /**< Page of its jobs */
    uint32_t uniform;               /**< Its part of the workgroup list, then the byte offset of
                                     *   its uniforms */
    uint32_t buffer;                /**< Instance page of its jobs, bound as group 1 */
    WGPUBindGroup history;          /**< History page and pyramid, NULL for those of the group */
    uint32_t groups;                /**< Workgroups of its jobs */
} box_gpu_cull_run_t;

/** Range of instances of one batch the cull shader tests, as in Job of cull.wgsl */
typedef struct box_gpu_cull_job_t {
    uint32_t first;                 /**< First slot in the instance page */
    uint32_t count;                 /**< Slots to test */
    uint32_t args;                  /**< Draw arguments of an ordered job */
    uint32_t base;                  /**< First entry an ordered job writes in the visible slots */
    float center[3];                /**< Center of the mesh bounds */
    uint32_t history;               /**< First history word of its page, UINT32_MAX for none */
    float extent[3];                /**< Half size of the mesh bounds */
    uint32_t list;                  /**< 1 when first indexes the slot lists, 0 for a range */
    uint32_t mask;                  /**< Frustums of its group that draw a parallel job */
    uint32_t fixture;               /**< 1 for a batch of LightFixture shapes: a frustum with a
                                     *   light inside the world box of an instance skips it */
    uint32_t exclude;               /**< Page slot plus one of the instance no frustum of the
                                     *   group draws, the shape of a planar reflection, 0 for
                                     *   none */
    uint32_t pad;                   /**< Pads the job to the size of Job in cull.wgsl */
} box_gpu_cull_job_t;

/** A frustum of a cull group in the uniforms, as in Frustum of cull.wgsl */
typedef struct box_gpu_cull_frustum_t {
    vec4 planes[6];                 /**< World space planes, inside where non negative */
    vec4 clip_w;                    /**< Row of the view projection that gives clip w */
    vec4 size;                      /**< Pixels per unit at w 1, smallest size, w guard, unused */
    uint32_t flags[4];              /**< Index of its counters, its flags, two unused */
    vec4 light;                     /**< World position of the light of a shadow tile, w 1, w 0
                                     *   for a frustum without a light */
} box_gpu_cull_frustum_t;

/** A frustum a cull group is built with */
typedef struct box_gpu_cull_desc_t {
    ecs_entity_t view;              /**< Entity of the view it culls for, 0 for captures */
    int32_t slot;                   /**< Slot of the view in the view buffer */
    int32_t cascade;                /**< Cascade of its shadow map, -1 for the view itself */
    int32_t index;                  /**< Index of the view in the packet, whose own batches it
                                     *   draws, -1 for none */
    uint32_t flags;                 /**< BOX_GPU_CULL_CASTERS, _SIZE, _HISTORY, _BAKE, _EMPTY */
    bool report;                    /**< Its counts become the CullStatus of the view */
    box_gpu_cull_frustum_t frustum; /**< Its planes and smallest size */
} box_gpu_cull_desc_t;

/** Uniforms of one cull dispatch, as in Cull of cull.wgsl; the frustums of its group are
 * bound next to them */
typedef struct box_gpu_cull_uniforms_t {
    uint32_t jobs[4];               /**< First job, jobs, phase, frustum of an ordered dispatch */
    mat4 view_proj;                 /**< World to clip space of the view */
    uint32_t hiz[4];                /**< Pyramid width, height and levels, 0 levels for none */
    uint32_t scan[4];               /**< Table entries, their count, workgroup list, workgroups */
} box_gpu_cull_uniforms_t;

/** The frustums one pass of the cull shader tests together: a view of a target and the
 * cascades of its shadows, frustum 0 being the view, or the frustum of probe captures */
typedef struct box_gpu_cull_group_t {
    int32_t slot;                   /**< Slot of its view in the view buffer */
    int32_t first_view;             /**< First of its cull views */
    int32_t view_count;             /**< Its cull views, at most BOX_GPU_CULL_GROUP_VIEWS */
    int32_t first_dispatch;         /**< First of its dispatches */
    int32_t dispatch_count;         /**< Its dispatches, parallel jobs and ordered ones */
    uint32_t frustums;              /**< Byte offset of its frustums in the uniform buffer */
    bool occlusion;                 /**< A frustum has a history: an early and a late phase */
    box_gpu_cull_uniforms_t base;   /**< Uniforms of its view: view projection and pyramid */
    WGPUBindGroup history;          /**< History and pyramid of its view, or the empty ones */
} box_gpu_cull_group_t;

/** What a view culled in a target, kept until its counters are read back */
typedef struct box_gpu_cull_record_t {
    ecs_entity_t view;              /**< Entity of the view */
    int32_t tested;                 /**< Instances it tested */
    int32_t untested;               /**< Instances of the batches it skipped by their bounds
                                     *   without testing them, frustum culled */
    int32_t skipped;                /**< Batches it skipped by their bounds */
    int32_t casters_skipped;        /**< Batches its cascades skipped by their bounds */
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

/** The cluster buffer of a view in a target and the read back of the entries its pass allocated */
typedef struct box_gpu_cluster_t {
    WGPUBuffer buffer;              /**< Allocated entries, offset and count per cluster, list */
    uint64_t entries;               /**< Entries the light and probe index list holds */
    WGPUBuffer readback;            /**< Mappable copy of the allocated entries */
    int32_t state;                  /**< Free, copied, mapping, mapped or failed */
    int64_t frame;                  /**< Frame of the packet the copy was recorded in */
    int64_t stamp;                  /**< Cull pass the view last bound the buffer in */
    uint64_t copied;                /**< Entries the list held in the pass of the read back in
                                     *   flight */
    int64_t oversized;              /**< Cull pass from which every read back fit in a quarter
                                     *   of the list, -1 while one did not */
    uint64_t peak;                  /**< Most entries a pass allocated since oversized */
    bool cut;                       /**< The list is at its largest and a read back pass listed
                                     *   fewer lights or probes than reach, warned once */
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

/** Copies of the cull counters in flight at most */
#define BOX_GPU_CULL_READBACKS (4)

/** A batch of the packet in the order a cull group adds its draws */
typedef struct box_gpu_cull_sort_t {
    int32_t phase;                  /**< Phase of the batch, see box_gpu_cull_phase */
    int32_t batch;                  /**< Index of the batch in the packet */
    uint64_t state;                 /**< Pipeline state of an opaque mesh batch, else 0 */
    uint64_t material;              /**< Hash of its material group, else 0 */
} box_gpu_cull_sort_t;

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
    WGPUTexture hiz_spare;          /**< Layers written instead of the levels past the last of a
                                     *   pyramid by a dispatch of BOX_GPU_HIZ_STEP levels */
    WGPUTextureView hiz_spares[BOX_GPU_HIZ_STEP - 1]; /**< View of each spare layer */
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
    ecs_vec_t order;                /**< uint64_t, instance page and draw of blended draws */
    ecs_vec_t tables;               /**< box_gpu_cull_table_t of the target */
    ecs_vec_t runs;                 /**< box_gpu_cull_run_t of the target */
    ecs_vec_t groups;               /**< box_gpu_cull_group_t of the target */
    ecs_vec_t entries;              /**< int32_t, the draw of each frustum of a parallel job */
    ecs_vec_t masks;                /**< uint32_t, the frustums of the group being added that
                                     *   draw each batch of the packet */
    ecs_vec_t skips;                /**< uint32_t, the frustums of the group being added that
                                     *   skip each batch of the packet by its bounds */
    ecs_vec_t sort;                 /**< box_gpu_cull_sort_t, the batches of the group being
                                     *   added in the order of their draws */
    ecs_vec_t frustums;             /**< Frustums of the view being added */
    int32_t copies;                 /**< Copies of cascade 0 added to each view, for tests */
    uint32_t exclude_page;          /**< Instance page of the instance the jobs of the group
                                     *   being added leave out, 0 for none */
    uint32_t exclude_slot;          /**< Its slot in the page */
    int64_t counting;               /**< History ranges passes, marks the pages counted */
    WGPUQuerySet stamps;            /**< Begin and end of the cull passes of a target, NULL
                                     *   without timestamp queries */
    WGPUBuffer stamp_resolve;       /**< Resolved timestamps, copied into the readback */
    uint32_t stamp_count;           /**< Timestamps the passes of the target wrote */
    WGPURenderPassEncoder mesh_pass; /**< Pass the mesh buffers below were bound in */
    WGPUBuffer mesh_vertices;       /**< Vertex buffer bound at slot 0 of mesh_pass */
    uint64_t mesh_vertex_offset;    /**< Its offset */
    WGPUBuffer mesh_indices;        /**< Index buffer bound in mesh_pass */
    uint64_t mesh_index_offset;     /**< Its offset */
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
    vec4 shadow_depth;              /**< Clip depth range of each cascade over its half
                                     *   extent */
    vec4 shadow_bias;               /**< Depth added to the compared depth per cascade */
    vec4 shadow_offset;             /**< World offset along the geometric normal per cascade
                                     *   at grazing light */
    vec4 shadow_params;             /**< Fade start, shadow end, texel in uv, unused */
    vec4 shadow_edge;               /**< Margin a cascade box shrinks by in clip units, blend
                                     *   band in half extents, two unused */
    uint32_t shadow_config[4];      /**< Cascades, 0 without shadows, width of the tent
                                     *   filter in texels, two unused */
    uint32_t reflection[4];         /**< Probes of the reflection probe buffer the view samples,
                                     *   0 for none, 1 when the view ignores tints as the
                                     *   captures do and 0 when not, two unused */
    vec4 aerial;                    /**< Aerial perspective: km per world unit, inverse of the
                                     *   volume distance in km, its slices, 1 when the view
                                     *   applies it and 0 when not */
    vec4 environment;               /**< Environment: intensity, cosine and sine of the yaw, 1
                                     *   when the view is lit by a processed environment and 0
                                     *   when not */
    uint32_t spot_config[4];        /**< Tile records of the shadow atlas the view samples, 0
                                     *   without spot shadows, width of the tent filter in
                                     *   texels, two unused */
    vec4 spot_bias;                 /**< Depth bias and normal offset at grazing light in texels
                                     *   of a tile, two unused */
    vec4 cookie;                    /**< LightCookie of the first directional light: one over
                                     *   its size, height of its plane, threshold, softness */
    vec4 cookie_offset;             /**< Its wrapped offset in world units, strength, unused */
    vec4 cookie_x;                  /**< Local X axis of the light, w unused */
    vec4 cookie_y;                  /**< Local Y axis of the light, w unused */
    uint32_t cookie_config[4];      /**< 1 when the view applies the cookie and 0 when not,
                                     *   its LightCookieProjection, two unused */
    mat4 inv_view_proj;             /**< Inverse of view_proj, clip space of the target to
                                     *   world, which the blur of a DiffuseTransmission reads
                                     *   the depth of the opaque snapshot with */
    vec4 planar[BOX_PLANAR_REFLECTION_MAX]; /**< Per planar reflection slot: distortion, share
                                     *   of the width and height of the reflection array its
                                     *   image covers, mips of the array; 0 without an image */
    vec4 planar_owner[BOX_PLANAR_REFLECTION_MAX * 3]; /**< Rows of the transform of the
                                     *   instance of the shape of each slot */
    uint32_t planar_slots[4];       /**< Page slot plus one of the instance of the shape of
                                     *   each slot, 0 for none */
} box_gpu_view_uniforms_t;

_Static_assert(sizeof(box_gpu_view_uniforms_t) <= BOX_GPU_VIEW_STRIDE,
    "the uniforms of a view fit in its stride");

/** The shadow atlas of a scene: the depth texture its spot and point light tiles render into */
typedef struct box_gpu_atlas_t {
    WGPUTexture texture;            /**< BOX_GPU_SHADOW_FORMAT, resolution squared */
    WGPUTextureView view;           /**< 2D view, sampled by the forward pass and rendered into */
    int32_t resolution;             /**< Edge of the atlas in texels */
    bool valid;                     /**< Every tile of the last pass rendered, so the tiles that
                                     *   did not change may be kept; false once created */
    int32_t revision;               /**< Revision of the shadow shader its tiles rendered with */
    uint32_t point_shader;          /**< Shadow shader variant its point light tiles rendered
                                     *   with, 0 for none */
    int32_t point_revision;         /**< Revision of that variant */
} box_gpu_atlas_t;

/** Packets a cascade store outlives the last packet that rendered its view */
#define BOX_GPU_CASCADE_STORE_FRAMES (16)

/** The shadow map of a view that keeps cascades between frames (box_render_view_shadows_t.reuse):
 * its own, so no other view renders into it, with the cascade each layer was rendered with */
typedef struct box_gpu_cascade_store_t {
    ecs_entity_t view;              /**< The view entity */
    WGPUTexture texture;            /**< BOX_GPU_SHADOW_FORMAT, a layer per cascade */
    WGPUTextureView array;          /**< Array view, sampled by the forward pass */
    WGPUTextureView layers[BOX_RENDER_SHADOW_CASCADES]; /**< View of each cascade */
    int32_t resolution;             /**< Edge of a layer in texels */
    int32_t cascades;               /**< Layers */
    box_render_cascade_t fits[BOX_RENDER_SHADOW_CASCADES]; /**< Cascade each layer rendered
                                     *   with, or renders with in the packet */
    uint32_t valid;                 /**< Mask of the layers that hold the render of their fit */
    mat4 proj;                      /**< Projection of the view the fits were made with */
    int32_t revision;               /**< Revision of the shadow shader the layers rendered with */
    int64_t frame;                  /**< Frame of the last packet that rendered the view */
} box_gpu_cascade_store_t;

/** The cascades of a view of the target being drawn */
typedef struct box_gpu_cascade_slot_t {
    int32_t store;                  /**< Index of its cascade store, -1 for the shared map */
    uint32_t renders;               /**< Mask of the cascades that render in the packet */
} box_gpu_cascade_slot_t;

#define BOX_GPU_ATLAS_FRUSTUM(tile, octant) ((tile) * BOX_RENDER_SHADOW_OCTANTS + (octant))

/** A draw of the casters of a tile of the last packet, read by box_render_gpu_cull_tile */
typedef struct box_gpu_atlas_read_t {
    int32_t tile;                   /**< Index of the tile in the shadow tiles of the packet */
    uint32_t args;                  /**< Index of its draw arguments */
    uint32_t count;                 /**< Instances it tested */
} box_gpu_atlas_read_t;

/** Bytes of the tile records of a shadow atlas, the uniform array the forward pass binds */
#define BOX_GPU_ATLAS_TILES_SIZE \
    ((uint64_t)BOX_RENDER_SHADOW_TILES * sizeof(box_render_shadow_record_t))

/** Binding of the planet light table in the view group, PlanetLight of planet_light.wgsl per
 * planet light slot */
#define BOX_GPU_PLANET_LIGHTS_BINDING (22)

/** Binding of the transmittance LUT array of the planet lights in the view group */
#define BOX_GPU_PLANET_LUTS_BINDING (23)

/** Planet light of a slot in the view group, PlanetLight of planet_light.wgsl */
typedef struct box_gpu_planet_light_t {
    float center[4];                /**< World center, km per world unit, 0 for an empty slot */
    float radii[4];                 /**< Bottom and top radius in km, layer of its LUT */
    float sun[4];                   /**< Direction toward its sun */
} box_gpu_planet_light_t;

/** Bytes of the planet light table */
#define BOX_GPU_PLANET_LIGHTS_SIZE \
    ((uint64_t)BOX_RENDER_PLANET_LIGHT_MAX * sizeof(box_gpu_planet_light_t))

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
    WGPUBindGroupLayout blit_layout; /**< Bind group layout of the blit */
    WGPUPipelineLayout blit_pipeline_layout; /**< Pipeline layout of the blit */
    WGPUSampler sampler;            /**< Linear sampler of the blit */
    box_gpu_blit_t blits[BOX_GPU_BLIT_FORMATS]; /**< Blit pipelines by format */
    int32_t blit_count;             /**< Blit pipelines created */
    WGPUBindGroupLayout view_layout; /**< Bind group layout of the view uniforms */
    WGPUBindGroupLayout material_layout; /**< Bind group layout of the texture layers */
    WGPUBuffer draw_kinds;          /**< DrawKind of mesh.wgsl of every kind, a
                                     *   BOX_GPU_DRAW_KIND_STRIDE apart (box_gpu_draw_kind) */
    WGPUSampler samplers[BOX_GPU_WRAPS][BOX_GPU_ANISOTROPY_LEVELS]; /**< Created on use */
    box_gpu_texture_t *white;       /**< Default of the albedo, emissive and ORM layers */
    box_gpu_texture_t *flat;        /**< Default of the normal layer, an unperturbed normal */
    box_gpu_texture_t *no_cookie;   /**< 1x1 white R8 texture bound as the light cookie of the
                                     *   views without one */
    ecs_map_t textures;             /**< Handle to box_gpu_texture_t */
    ecs_map_t materials;            /**< Key hash to the first box_gpu_material_t */
    ecs_vec_t mips;                 /**< uint32_t, textures whose mip levels are not generated */
    uint32_t texture_serial;        /**< Serial of the last image written into a texture */
    WGPUBindGroupLayout blend_layout; /**< Bind group layout of the texture_blend variant */
    WGPUTextureView blend_flat_heights; /**< One slice array view of white, the heights a blend
                                     *   with a contrast of 0 binds and does not sample */
    WGPUPipelineLayout blend_mesh_layout; /**< Pipeline layout of the texture_blend variant of
                                     *   the forward pass: mesh_layout with blend_layout */
    box_gpu_blend_array_t *blend_arrays; /**< Texture arrays of the TextureBlend layers */
    box_gpu_blend_group_t *blend_groups; /**< Bind groups of the TextureBlend batches */
    uint64_t blend_bytes;           /**< Bytes of the blend arrays */
    int64_t blend_frame;            /**< Packets the blend caches have seen */
    WGPUPipelineLayout mesh_layout; /**< Pipeline layout of the forward pass */
    WGPUBindGroupLayout prepass_empty_layout; /**< An empty bind group layout */
    WGPUBindGroup prepass_empty;    /**< The empty bind group of prepass_empty_layout */
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
    box_gpu_reflect_t *no_reflection; /**< One black slot, bound by views without probes */
    ecs_map_t forward;              /**< Forward and depth prepass pipelines by shader, format,
                                     *   blend, back face, alpha test, texture blend and depth
                                     *   mode (box_gpu_forward_key) */
    ecs_map_t forward_previous;     /**< Forward pipelines of the modules a recompile replaced,
                                     *   by the same key, drawn with until the pipeline of the
                                     *   new module of their key exists */
    WGPUBuffer view_buffer;         /**< Uniforms of the views of a target */
    int32_t view_capacity;          /**< Views the view buffer holds */
    WGPUBindGroupLayout cluster_layout; /**< Bind group layout of the cluster pass */
    WGPUPipelineLayout cluster_pipeline_layout; /**< Pipeline layout of the cluster pass */
    WGPUComputePipeline cluster;    /**< Cluster pipeline, NULL until the shader exists */
    int32_t cluster_revision;       /**< Revision of the shader the pipeline was made with */
    ecs_map_t clusters;             /**< View and target key to box_gpu_cluster_t */
    uint64_t cluster_entries;       /**< Index entries of the cluster buffers alive */
    WGPUBuffer no_lights;           /**< Light buffer of a view whose light buffer is missing */
    WGPUBuffer no_probes;           /**< Reflection probe buffer of a cluster pass without probes */
    WGPUBuffer no_clips;            /**< Clip buffer of the views while no element clips */
    ecs_vec_t view_data;            /**< Bytes of the view uniforms of a target */
    WGPUBindGroupLayout shadow_layout; /**< Bind group layout of the shadow pass */
    WGPUPipelineLayout shadow_pipeline_layout; /**< Pipeline layout of the shadow pass: the
                                     *   cascade, the instance and the material group */
    WGPURenderPipeline shadow;      /**< Shadow pipeline, NULL until the shader exists */
    WGPURenderPipeline shadow_cutout; /**< Shadow pipeline of the alpha tested casters */
    int32_t shadow_revision;        /**< Revision of the shader the pipelines were made with */
    WGPURenderPipeline shadow_point; /**< Pipeline of the octants of the point light tiles */
    WGPURenderPipeline shadow_point_cutout; /**< Its pipeline of the alpha tested casters */
    WGPUPipelineLayout shadow_blend_layout; /**< Pipeline layout of the alpha tested casters
                                     *   with a TextureBlend: the shadow pass groups and the blend
                                     *   group, NULL until the first one draws */
    WGPURenderPipeline shadow_variants[2][BOX_GPU_SHADOW_VARIANTS]; /**< Pipelines made on
                                     *   first use by box_gpu_shadow_variant, of the cascades and
                                     *   spot tiles, then of the point light octants */
    uint32_t shadow_point_shader;   /**< Handle of the shadow shader variant they were made
                                     *   with, BOX_RENDER_SHADER_SHADOW_CLIP or _SHADOW */
    int32_t shadow_point_revision;  /**< Revision of that shader */
    bool shadow_core;               /**< Point light tiles discard instead of clipping in this
                                     *   packet, see box_render_gpu_shadow_core */
    WGPUSampler shadow_sampler;     /**< Comparison sampler of the shadow lookups */
    WGPUTexture shadow_empty;       /**< Shadow map bound by views without shadows */
    WGPUTextureView shadow_empty_view; /**< Array view of the empty shadow map */
    WGPUTexture shadow_map;         /**< Shadow map the views render their cascades into */
    WGPUTextureView shadow_map_view; /**< Array view of the shadow map */
    WGPUTextureView shadow_layers[BOX_RENDER_SHADOW_CASCADES]; /**< View of each cascade */
    int32_t shadow_resolution;      /**< Edge of the shadow map, 0 before it exists */
    int32_t shadow_cascades;        /**< Layers of the shadow map */
    ecs_vec_t cascade_stores;       /**< box_gpu_cascade_store_t, the shadow maps of the views
                                     *   that keep cascades between frames */
    ecs_vec_t cascade_slots;        /**< box_gpu_cascade_slot_t of each view slot of a target */
    WGPUBuffer shadow_buffer;       /**< Cascade matrices of the views of a target */
    int32_t shadow_capacity;        /**< Views the cascade buffer holds */
    ecs_vec_t shadow_data;          /**< Bytes of the cascade matrices of a target */
    WGPUTexture capture_shadow;     /**< Shadow map of the probe captures, one cascade */
    WGPUTextureView capture_shadow_view; /**< Array view the captures sample */
    WGPUTextureView capture_shadow_layer; /**< View the casters render into */
    WGPUBuffer capture_shadow_buffer; /**< Matrix of the cascade of the captures */
    WGPUBindGroup capture_shadow_group; /**< Binds the matrix to the shadow pipeline */
    ecs_map_t atlases;              /**< Handle to box_gpu_atlas_t, the shadow atlases */
    WGPUTexture no_atlas;           /**< Atlas bound by views without spot shadows */
    WGPUTextureView no_atlas_view;  /**< 2D view of the empty atlas */
    WGPUBuffer no_tiles;            /**< Zero tile records bound by views without spot shadows */
    WGPUBuffer atlas_buffer;        /**< Tile matrices of the atlases of a packet */
    int32_t atlas_capacity;         /**< Blocks the matrix buffer holds */
    ecs_vec_t atlas_data;           /**< Bytes of the tile matrices of a packet, one block of
                                     *   BOX_GPU_CASCADE_STRIDE per spot light tile and one per
                                     *   octant of a point light tile */
    ecs_vec_t atlas_blocks;         /**< int32_t, the first block of each tile of a packet */
    ecs_vec_t atlas_renders;        /**< bool, whether each tile of a packet renders: its render
                                     *   flag, or every tile of an atlas that cannot keep them */
    WGPUPipelineLayout shadow_clear_layout; /**< Pipeline layout without bind groups */
    WGPURenderPipeline shadow_clear; /**< Writes depth 0 over the viewport with compare always,
                                     *   which clears a tile of an atlas the pass loads */
    WGPUBuffer atlas_counts;        /**< Copy of the draw arguments the cull of the tiles of the
                                     *   last packet wrote, for box_render_gpu_cull_tile */
    uint64_t atlas_counts_size;     /**< Bytes of that copy */
    ecs_vec_t atlas_reads;          /**< box_gpu_atlas_read_t, the draws of those tiles */
    WGPUBindGroupLayout instance_layout; /**< Binds the streams of an instance page: 0
                                     *   transforms, 1 surface assets, 2 shading, 3 surfaces,
                                     *   4 material table; forward pass and alpha tested casters */
    WGPUBindGroupLayout transform_layout; /**< instance_layout without the shading stream: the
                                     *   cull, the shadow casters and the capture cull */
    WGPUBuffer no_assets;           /**< One zero vec4, the surface assets of the draws of
                                     *   meshes without any */
    WGPUBuffer no_surfaces;         /**< One zero box_render_surface_t, the surface stream of
                                     *   the pages without one, the plain mesh and 2D pages */
    WGPUBuffer no_materials;        /**< One default material record, the material table of
                                     *   the draws before the main thread sent one */
    box_gpu_cull_t cull;            /**< Cull pass state */
    WGPUBindGroupLayout scatter_layout; /**< Bind group layout of the scatter pass */
    WGPUPipelineLayout scatter_pipeline_layout; /**< Pipeline layout of the scatter pass */
    WGPUComputePipeline scatter;    /**< Scatter pipeline, NULL until the shader exists */
    int32_t scatter_revision;       /**< Revision of the shader the pipeline was made with */
    ecs_map_t meshes;               /**< Handle to box_gpu_mesh_t */
    ecs_vec_t mesh_pages;           /**< box_gpu_mesh_page_t, the pages of the mesh pool */
    uint64_t mesh_ids;              /**< Last id given to a mesh */
    WGPUDevice paced_device;        /**< Device the submissions of paced were made on */
    uint64_t paced[BOX_GPU_FRAMES_IN_FLIGHT]; /**< Submission index of each frame in flight */
    int32_t paced_count;            /**< Frames in paced */
    int32_t paced_next;             /**< Entry of paced the next frame writes */
    ecs_map_t buffers;              /**< Handle to box_gpu_buffer_t */
    WGPUBuffer copy_staging;        /**< Staging buffer of the staged copies of a packet, NULL
                                     *   until one stages: the smallest power of two from 64 KiB
                                     *   that holds them, at most BOX_RENDER_COPY_MAX.
                                     *
                                     *   Released after BOX_RENDER_COPY_STAGING_FRAMES packets
                                     *   without staged copies, else shrunk at most once per that
                                     *   many to what the most a packet among them staged needs */
    uint64_t copy_staging_size;     /**< Bytes of copy_staging, at most BOX_RENDER_COPY_MAX */
    uint64_t copy_staging_peak;     /**< Largest staged bytes of a packet since the last check
                                     *   whether copy_staging shrinks */
    int32_t copy_staging_frames;    /**< Packets since that check */
    int32_t copy_staging_idle;      /**< Packets in a row that staged nothing, at most
                                     *   BOX_RENDER_COPY_STAGING_FRAMES */
    ecs_vec_t copy_writes;          /**< box_gpu_copy_write_t, scratch of box_gpu_copy */
    ecs_vec_t copy_modes;           /**< box_gpu_copy_mode_t by copy, scratch of box_gpu_copy */
    ecs_map_t targets;              /**< Handle to box_gpu_target_t */
    ecs_map_t shaders;              /**< Handle to box_gpu_shader_t */
    ecs_vec_t results;              /**< box_render_shader_result_t not yet taken */
    ecs_os_mutex_t lock;            /**< Protects the counters and the results */
    box_render_compile_delivery_t delivery; /**< When compile outcomes apply, under the lock */
    box_render_readback_delivery_t readbacks; /**< When cull counters publish, under the lock */
    uint32_t cull_window;           /**< Slots of a cull page from the next pass, under the lock */
    int32_t cull_copies;            /**< Copies of cascade 0 the views cull from the next pass,
                                     *   under the lock */
    bool shadow_core_request;       /**< Point light tiles discard instead of clipping from the
                                     *   next packet, under the lock */
    uint64_t buffer_writes;         /**< Writes to buffers, the version of the last one */
    box_render_gpu_stats_t stats;   /**< Counters read by the main thread */
    box_render_gpu_stats_t pending; /**< Counter changes not yet published, render thread only */
    ecs_os_thread_id_t thread;      /**< Thread that ran init, the only one using the device */
    uint32_t canvas;                /**< Target presented on the page canvas, 0 for none */
    box_gpu_effects_t *effects;     /**< Effect passes of the views */
    box_gpu_gtao_t *gtao;           /**< Ambient occlusion of the views */
    box_gpu_transmission_t *transmission; /**< Opaque snapshots of transmission */
    uint32_t rendering;             /**< Target that is rendering, materials cannot sample it */
    bool timestamps;                /**< The device has timestamp queries */
    bool clip_distances;            /**< The device has clip distances */
    box_gpu_bake_t *bake;           /**< Probe captures, created on the first capture */
    box_gpu_atmos_t *atmosphere;    /**< Atmosphere passes */
    ecs_map_t environments;         /**< Handle to box_gpu_environment_t* */
    struct box_gpu_env_image_t *environment_image; /**< Conversion of environment images,
                                                    *   created by the first image */
    box_gpu_particles_t *particles; /**< Particle passes */
    box_gpu_fog_volumes_t *fog_volumes; /**< Fog volume draws */
    box_gpu_planets_t *planets;     /**< Planet atmosphere shell draws */
    box_gpu_debug_t *debug;         /**< Debug passes of the views */
    box_gpu_planar_t *planar;       /**< Planar reflections of the views */
    struct box_gpu_env_process_t *environment_process; /**< Processing of the environments,
                                                        *   created on first use */
    struct box_gpu_env_background_t *environment_background; /**< Background draw of the
                                                              *   views, created on first use */
    WGPUTextureView no_environment; /**< Cube view of the black reflection slot, bound by views
                                     *   without an environment */
    WGPUBuffer no_environment_sh;   /**< Zero SH uniform of views without an environment */
    ecs_vec_t captures;             /**< box_gpu_capture_t* whose readback is mapping */
    struct box_gpu_pipelines_t *pipelines; /**< Render and compute pipelines created in the
                                     *   background, created by the first request */
    bool pipeline_wait;             /**< Requests of this packet wait for their pipeline:
                                     *   natively, a headless run or a packet that captures */
    bool lost;                      /**< The device was lost, the ring recreates it */
    bool lose;                      /**< box_render_gpu_lose_device asked to destroy it */
} box_gpu_t;

/** Render and compute pipelines of the backend and the workers that create them */
typedef struct box_gpu_pipelines_t box_gpu_pipelines_t;

/** The device errors a creation on a thread reports, which box_gpu_on_error hands to it instead
 * of counting them. Natively error scopes belong to the device, not to a thread, so a scope
 * cannot tell a creation on the render thread from one on a pipeline worker. */
typedef struct box_gpu_error_capture_t {
    char *message;                  /**< The first error, NULL without one */
    bool failed;                    /**< An error was reported */
} box_gpu_error_capture_t;

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
    BOX_GPU_STAT_PREPASS,           /**< A depth prepass render pass was recorded */
    BOX_GPU_STAT_PREPASS_DRAW,      /**< A draw call was recorded in a depth prepass */
    BOX_GPU_STAT_TRANSMISSION_SNAPSHOT, /**< An opaque snapshot of a view was recorded */
    BOX_GPU_STAT_TRANSMISSION_TEXTURE, /**< A snapshot texture was created */
    BOX_GPU_STAT_FORWARD_PASS,      /**< A forward render pass of a view was begun */
    BOX_GPU_STAT_SCATTER,           /**< A scatter dispatch was recorded */
    BOX_GPU_STAT_INSTANCE_BIND,     /**< An instance bind group was set in a pass or bundle */
    BOX_GPU_STAT_CULL_DISPATCH,     /**< A dispatch of the cull pass of a view was recorded */
    BOX_GPU_STAT_CULL_INSTANCES,    /**< A cull_instances dispatch of a view was recorded */
    BOX_GPU_STAT_SHADOW_TILE,       /**< A shadow atlas tile was rendered */
    BOX_GPU_STAT_SHADOW_REUSED,     /**< A shadow atlas tile was kept from an earlier frame */
    BOX_GPU_STAT_SHADOW_PASS,       /**< A shadow atlas render pass was recorded */
    BOX_GPU_STAT_SHADOW_DRAW,       /**< A draw call was recorded in a cascade or a tile */
    BOX_GPU_STAT_CASCADE,           /**< A cascade of a view was rendered */
    BOX_GPU_STAT_CASCADE_KEPT,      /**< A cascade of a view was kept from an earlier frame */
    BOX_GPU_STAT_COPY_DIRECT,       /**< A copy went from its source to its destination */
    BOX_GPU_STAT_COPY_STAGED,       /**< A copy went through the staging buffer */
    BOX_GPU_STAT_FOG_VOLUME,        /**< A fog volume was drawn */
    BOX_GPU_STAT_DEBUG_PASS         /**< A debug render pass of a view was recorded */
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

#endif

#endif
