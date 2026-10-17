/**
 * @file render/gpu_bake.h
 * @brief State of the probe and reflection captures shared by gpu_bake.c and gpu_bake_time.c,
 * which measures them.
 */

#ifndef BOX_RENDER_GPU_BAKE_H
#define BOX_RENDER_GPU_BAKE_H

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Probes captured by one command buffer at most */
#define BOX_GPU_BAKE_CHUNK (128)

/** Timestamp measurements in flight at most, two per packet */
#define BOX_GPU_BAKE_READBACKS (8)

/** Milliseconds a measurement of the captures of one packet may take at most, longer ones are
 * stale timestamps or a stalled queue and are dropped */
#define BOX_GPU_BAKE_MEASURE_MAX (1000.0)

/** Byte alignment of the destination of a query resolve */
#define BOX_GPU_BAKE_RESOLVE_ALIGN (256)

/** Kind of captures, each with its own atlas, render bundles, timing and results */
typedef enum box_gpu_bake_kind_t {
    BOX_GPU_BAKE_PROBES,            /**< Probe captures of the bake batches */
    BOX_GPU_BAKE_REFLECT,           /**< Reflection captures */
    BOX_GPU_BAKE_KINDS              /**< Kinds of captures */
} box_gpu_bake_kind_t;

/** State of a timestamp readback */
typedef enum box_gpu_bake_readback_state_t {
    BOX_GPU_BAKE_FREE,              /**< Unused */
    BOX_GPU_BAKE_WRITTEN,           /**< The timestamps are submitted, resolved a packet later */
    BOX_GPU_BAKE_COPIED,            /**< The copy is submitted, mapped at the next capture */
    BOX_GPU_BAKE_MAPPING,           /**< Waiting for the map */
    BOX_GPU_BAKE_MAPPED,            /**< Mapped, the timestamps can be read */
    BOX_GPU_BAKE_FAILED             /**< The map failed */
} box_gpu_bake_readback_state_t;

/** A copy of the two timestamps around the captures of a packet */
typedef struct box_gpu_bake_readback_t {
    WGPUBuffer buffer;              /**< Mappable copy of the resolved timestamps */
    int32_t state;                  /**< box_gpu_bake_readback_state_t */
    int32_t probes;                 /**< Probes the measured captures held */
    int32_t kind;                   /**< box_gpu_bake_kind_t of the captures */
} box_gpu_bake_readback_t;

/** A measurement of the captures of a packet from the CPU, used without timestamp queries:
 * from the start of their recording until the queue reports their work done */
typedef struct box_gpu_bake_done_t {
    struct box_gpu_bake_t *bake;    /**< The capture state */
    box_gpu_t *gpu;                 /**< The backend */
    uint64_t start;                 /**< ecs_os_now when the captures started recording */
    int32_t probes;                 /**< Probes the captures held, 0 while the slot is free */
    int32_t kind;                   /**< box_gpu_bake_kind_t of the captures */
} box_gpu_bake_done_t;

/** The timestamps around the captures of a packet */
typedef struct box_gpu_bake_timer_t {
    int32_t readback;               /**< Readback of the timestamps, -1 when not measured */
    bool begun;                     /**< The first cull pass wrote the starting timestamp */
    bool ended;                     /**< The last projection wrote the ending timestamp */
} box_gpu_bake_timer_t;

/** Uniforms of the SH projection, as in Params of etc/shaders/bake_sh.wgsl */
typedef struct box_gpu_bake_params_t {
    uint32_t size[4];               /**< Face edge in texels, probes of the chunk */
    float sky[4];                   /**< Linear ambient light from above */
    float ground[4];                /**< Linear ambient light from below */
    uint32_t probes[BOX_GPU_BAKE_CHUNK]; /**< Volume index or cubemap slot of each probe */
    uint32_t lights[4];             /**< Baked lights, the directional ones among them first */
    float near_[4];                 /**< Near plane of the faces, pixels per unit at depth 1,
                                     *   smallest size in pixels, spacing of the probes */
    float positions[BOX_GPU_BAKE_CHUNK][4]; /**< World position of each probe of the chunk */
} box_gpu_bake_params_t;

/** Uniforms of the cull of one batch of a chunk, as CullJob of etc/shaders/bake_sh.wgsl */
typedef struct box_gpu_bake_job_t {
    uint32_t first;                 /**< First slot, or first entry of the lists when listed */
    uint32_t count;                 /**< Instances of the batch */
    uint32_t list;                  /**< 1 when first indexes the lists, 0 for a range */
    uint32_t draw;                  /**< Index of the batch among the batches of the chunk */
    float center[3];                /**< Center of the mesh bounds */
    uint32_t index_count;           /**< Indices of the mesh */
    float extent[3];                /**< Half size of the mesh bounds */
    uint32_t region;                /**< First entry of the batch in the slots of a face */
    uint32_t draws;                 /**< Batches of the chunk */
    uint32_t stride;                /**< Slots of a face, those of every batch */
    uint32_t pad[2];                /**< Pads the job to the size of CullJob */
} box_gpu_bake_job_t;

/** A batch the faces of the captures draw, with what its draws bind */
typedef struct box_gpu_bake_draw_t {
    int32_t batch;                  /**< Index of the batch in the packet */
    uint32_t region;                /**< First entry of the batch in the slots of a face */
    uint32_t count;                 /**< Instances of the batch */
    WGPURenderPipeline pipeline;    /**< Forward pipeline, NULL when it draws nothing */
    WGPUBindGroup material;         /**< Texture layers and sampler */
    WGPUBindGroup instances;        /**< Instance buffer */
    WGPUBuffer vertices;            /**< Vertices of the mesh */
    WGPUBuffer indices;             /**< Indices of the mesh */
} box_gpu_bake_draw_t;

/** What the render bundles of the faces bind besides their batches */
typedef struct box_gpu_bake_key_t {
    WGPUBuffer lights;              /**< Light buffer of the faces */
    uint64_t lights_size;           /**< Bytes of the light buffer */
    WGPUBuffer clusters;            /**< Cluster buffer of the faces */
    WGPUTextureView shadow;         /**< Shadow map of the faces */
    WGPUBindGroup ambient;          /**< Volume group of the ambient term */
    WGPUBuffer slots;               /**< Visible slots */
    WGPUBuffer args;                /**< Indirect draw arguments */
    uint32_t stride;                /**< Slots of a face */
    uint32_t pad;                   /**< Keeps the key free of padding */
} box_gpu_bake_key_t;

/** The atlas the faces of one kind of captures render into and the render bundles of its faces */
typedef struct box_gpu_bake_set_t {
    WGPURenderBundle *bundles;      /**< Draws of each face of a chunk, NULL before they exist */
    int32_t bundle_count;           /**< Faces the bundles cover */
    WGPUBindGroup faces_group;      /**< View group the bundles bind, NULL with no bundles */
    box_gpu_bake_key_t key;         /**< What the bundles bind, held while they exist */
    ecs_vec_t key_draws;            /**< box_gpu_bake_draw_t the bundles draw, held as well */
    int32_t size;                   /**< Face edge of the atlas tiles, 0 before they exist */
    int32_t probes;                 /**< Rows of the atlas, one per probe of a chunk */
    WGPUTexture color;              /**< rgba16float atlas of the faces */
    WGPUTextureView color_view;     /**< View the faces render into and the projection reads */
    WGPUTexture depth;              /**< Depth32float atlas of the faces */
    WGPUTextureView depth_view;     /**< View the faces render into */
    WGPUTextureView depth_read;     /**< Depth aspect view the projection reads */
} box_gpu_bake_set_t;

/** The captures of one bake or reflection batch: what their faces render and where they go */
typedef struct box_gpu_bake_pass_t {
    int32_t kind;                   /**< box_gpu_bake_kind_t */
    const box_render_bake_batch_t *bake; /**< The bake batch, NULL for reflections */
    const box_render_reflect_batch_t *reflect; /**< The reflection batch, NULL for a bake */
    ecs_entity_t scene;             /**< Scene of the batches the faces draw */
    int32_t size;                   /**< Face edge in texels */
    box_render_view_t rv;           /**< View with the lights the faces are shaded with */
    float sky[4];                   /**< Linear ambient light from above of the sky radiance */
    float ground[4];                /**< Linear ambient light from below of the sky radiance */
    WGPUBindGroup ambient;          /**< Volume group the faces shade their ambient term with */
    box_gpu_volume_t *volume;       /**< Volume a bake writes, NULL for reflections */
    const box_gpu_reflect_t *cube;  /**< Cubemap array reflections write, NULL for a bake */
    float spacing;                  /**< Spacing of the probes of the volume */
    bool sun;                       /**< The faces are shadowed by the capture cascade */
    box_render_cascade_t cascade;   /**< The capture cascade, fitted to the bounds */
    const box_render_probe_capture_t *probes; /**< The probes of the batch */
    int32_t count;                  /**< Probes of the batch */
} box_gpu_bake_pass_t;

struct box_gpu_bake_t {
    WGPUBindGroupLayout layout;     /**< Bind group layout of the projection */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the projection */
    WGPUComputePipeline project;    /**< Projection pipeline, NULL until the shader exists */
    WGPUBindGroupLayout cull_layout; /**< Bind group layout of the cull */
    WGPUPipelineLayout cull_pipeline_layout; /**< Pipeline layout of the cull */
    WGPUComputePipeline cull;       /**< Cull pipeline, NULL until the shader exists */
    int32_t revision;               /**< Revision of the shader the pipelines were made with */
    WGPUBuffer jobs;                /**< box_gpu_bake_job_t of the batches, a stride apart */
    uint64_t jobs_size;             /**< Bytes of jobs */
    WGPUBuffer slots;               /**< Visible slots of every face, batch by batch */
    uint64_t slots_size;            /**< Bytes of slots */
    WGPUBuffer args;                /**< Indirect draw arguments of every face and batch */
    uint64_t args_size;             /**< Bytes of args */
    WGPUBuffer lists;               /**< Slots of the listed batches */
    uint64_t lists_size;            /**< Bytes of lists */
    WGPUBindGroup cull_group;       /**< Binds the buffers of the cull, NULL when they changed */
    ecs_vec_t draws;                /**< box_gpu_bake_draw_t of the bake batch */
    ecs_vec_t job_data;             /**< Bytes of the jobs of the bake batch */
    ecs_vec_t list_data;            /**< uint32_t, the slots of its listed batches */
    uint32_t stride;                /**< Slots of a face, those of every batch */
    box_gpu_bake_set_t sets[BOX_GPU_BAKE_KINDS]; /**< Atlas and bundles of each kind */
    WGPUBuffer uniforms;            /**< View uniforms of the faces of a chunk */
    WGPUBuffer params;              /**< box_gpu_bake_params_t of a chunk */
    WGPUBuffer clusters;            /**< One cluster that lists every point and spot light */
    ecs_vec_t view_data;            /**< Bytes of the view uniforms of a chunk */
    ecs_vec_t light_data;           /**< Words of the cluster buffer of a chunk */
    WGPUQuerySet queries;           /**< Two timestamps per readback, NULL without the feature */
    WGPUBuffer resolve;             /**< Resolved timestamps */
    box_gpu_bake_readback_t readbacks[BOX_GPU_BAKE_READBACKS]; /**< Timestamp copies */
    box_gpu_bake_done_t done[BOX_GPU_BAKE_READBACKS]; /**< Measurements without timestamps */
    box_render_bake_timing_t timing[BOX_GPU_BAKE_KINDS]; /**< Measured time of each kind not
                                                          *   yet taken, under the gpu lock */
    ecs_vec_t captured[BOX_GPU_BAKE_KINDS]; /**< box_render_bake_result_t of each kind not taken,
                                             *   under the gpu lock */
    int32_t in_flight;              /**< Chunks submitted since the queue was last waited for */
};

/** Create the timestamp queries and their readbacks when the device has the feature.
 *
 * @param gpu The backend
 * @param b The capture state
 */
void box_gpu_bake_queries_init(
    box_gpu_t *gpu,
    box_gpu_bake_t *b);

/** Release the timestamp queries and their readbacks.
 *
 * @param b The capture state
 */
void box_gpu_bake_queries_fini(
    box_gpu_bake_t *b);

/** Map the timestamp copies that were submitted and publish the ones that arrived.
 *
 * @param gpu The backend
 * @param b The capture state
 */
void box_gpu_bake_readback_poll(
    box_gpu_t *gpu,
    box_gpu_bake_t *b);

/** Find a free timestamp readback.
 *
 * @param b The capture state
 * @return Index of the readback, -1 without timestamps or when every one is in flight
 */
int32_t box_gpu_bake_readback(
    box_gpu_bake_t *b);

/** Resolve the timestamps of the readbacks whose captures were submitted before the last
 * packet and copy them into their readbacks.
 *
 * @param gpu The backend
 * @param b The capture state
 */
void box_gpu_bake_resolve(
    box_gpu_t *gpu,
    box_gpu_bake_t *b);

/** Measure captures from the CPU until the queue reports their work done, the fallback of
 * devices without timestamp queries.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param kind box_gpu_bake_kind_t of the captures
 * @param start ecs_os_now when the captures started recording
 * @param probes Probes the captures held
 */
void box_gpu_bake_done_measure(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t kind,
    uint64_t start,
    int32_t probes);

/** Finish an encoder, submit it and release it.
 *
 * @param gpu The backend
 * @param encoder The encoder
 */
void box_gpu_bake_submit(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder);

#endif

#endif
