/**
 * @file render/gpu_bake.c
 * @brief Probe captures of the webgpu backend, which bake the irradiance probe volumes.
 *
 * Six faces per probe are shaded with one probe buffer and projected onto SH in the other by
 * bake_sh.wgsl. A chunk is one command buffer: a cull that lists the instances of each face, a
 * render pass that replays a render bundle per face into an atlas tile, and the projection.
 */

#include "gpu.h"
#include <float.h>

#ifdef BOX_RENDER_WGPU

/** Probes captured by one command buffer at most */
#define BOX_GPU_BAKE_CHUNK (128)

/** Visible slots the probes of a chunk may use at most */
#define BOX_GPU_BAKE_VISIBLE (1 << 22)

/** Texels of the face atlas at most */
#define BOX_GPU_BAKE_TEXELS (1 << 22)

/** Words of the cluster buffer before its light list: entries, offset and count of the cluster */
#define BOX_GPU_BAKE_CLUSTER_HEADER (3)

/** Words the cluster buffer holds before it first grows */
#define BOX_GPU_BAKE_CLUSTER_WORDS (BOX_GPU_BAKE_CLUSTER_HEADER + 64)

/** Near plane of the faces in world units */
#define BOX_GPU_BAKE_NEAR (0.01f)

/** Bytes between the cull uniforms of two batches, the dynamic offset alignment of webgpu */
#define BOX_GPU_BAKE_JOB_STRIDE (256)

/** Words of the indexed indirect draw arguments of one draw */
#define BOX_GPU_BAKE_ARGS (5)

/** Timestamp measurements in flight at most */
#define BOX_GPU_BAKE_READBACKS (4)

/** Milliseconds a measurement of the captures of one packet may take at most, longer ones are
 * stale timestamps or a stalled queue and are dropped */
#define BOX_GPU_BAKE_MEASURE_MAX (1000.0)

#define BOX_GPU_BAKE_IN_FLIGHT (4)

/** Byte alignment of the destination of a query resolve */
#define BOX_GPU_BAKE_RESOLVE_ALIGN (256)

/** Depth bias of the capture cascade in texels, as the View default */
#define BOX_GPU_BAKE_SHADOW_BIAS (2.0f)

/** Normal offset of the capture cascade in texels, as the View default */
#define BOX_GPU_BAKE_SHADOW_OFFSET (1.5f)

/** PCF taps per axis of the capture cascade, as the View default */
#define BOX_GPU_BAKE_SHADOW_FILTER (3)

/** View depth beyond every capture, where the capture cascade would end */
#define BOX_GPU_BAKE_FAR (1e30f)

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
} box_gpu_bake_readback_t;

/** A measurement of the captures of a packet from the CPU, used without timestamp queries:
 * from the start of their recording until the queue reports their work done */
typedef struct box_gpu_bake_done_t {
    struct box_gpu_bake_t *bake;    /**< The capture state */
    box_gpu_t *gpu;                 /**< The backend */
    uint64_t start;                 /**< ecs_os_now when the captures started recording */
    int32_t probes;                 /**< Probes the captures held, 0 while the slot is free */
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
    uint32_t probes[BOX_GPU_BAKE_CHUNK]; /**< Volume index of each probe of the chunk */
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
    WGPURenderBundle *bundles;      /**< Draws of each face of a chunk, NULL before they exist */
    int32_t bundle_count;           /**< Faces the bundles cover */
    WGPUBindGroup faces_group;      /**< View group the bundles bind, NULL with no bundles */
    box_gpu_bake_key_t key;         /**< What the bundles bind, held while they exist */
    ecs_vec_t key_draws;            /**< box_gpu_bake_draw_t the bundles draw, held as well */
    WGPUBuffer uniforms;            /**< View uniforms of the faces of a chunk */
    WGPUBuffer params;              /**< box_gpu_bake_params_t of a chunk */
    WGPUBuffer clusters;            /**< One cluster that lists every point and spot light */
    ecs_vec_t view_data;            /**< Bytes of the view uniforms of a chunk */
    ecs_vec_t light_data;           /**< Words of the cluster buffer of a chunk */
    int32_t size;                   /**< Face edge of the atlas tiles, 0 before they exist */
    int32_t probes;                 /**< Rows of the atlas, one per probe of a chunk */
    WGPUTexture color;              /**< rgba16float atlas of the faces */
    WGPUTextureView color_view;     /**< View the faces render into and the projection reads */
    WGPUTexture depth;              /**< Depth32float atlas of the faces */
    WGPUTextureView depth_view;     /**< View the faces render into */
    WGPUTextureView depth_read;     /**< Depth aspect view the projection reads */
    WGPUQuerySet queries;           /**< Two timestamps per readback, NULL without the feature */
    WGPUBuffer resolve;             /**< Resolved timestamps */
    box_gpu_bake_readback_t readbacks[BOX_GPU_BAKE_READBACKS]; /**< Timestamp copies */
    box_gpu_bake_done_t done[BOX_GPU_BAKE_READBACKS]; /**< Measurements without timestamps */
    box_render_bake_timing_t timing; /**< Measured time not yet taken, under the gpu lock */
    ecs_vec_t captured;             /**< box_render_bake_result_t not taken, under the gpu lock */
    int32_t in_flight;
};

/** Axis a face looks along and its up axis, the right axis is their cross product */
static const float box_gpu_bake_faces[6][2][3] = {
    {{1, 0, 0}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 1, 0}},
    {{0, 1, 0}, {0, 0, -1}},
    {{0, -1, 0}, {0, 0, 1}},
    {{0, 0, 1}, {0, 1, 0}},
    {{0, 0, -1}, {0, 1, 0}}
};

/** Create a buffer that is written from the CPU.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_bake_buffer(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    WGPUBufferDescriptor desc = {
        .usage = usage | WGPUBufferUsage_CopyDst,
        .size = (size + 3) & ~(uint64_t)3
    };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

/** Release a texture view when it exists.
 *
 * @param view The view, set to NULL
 */
static void box_gpu_bake_view_release(
    WGPUTextureView *view)
{
    if (*view) {
        wgpuTextureViewRelease(*view);
        *view = NULL;
    }
}

/** Release the color and depth atlas of the captures.
 *
 * @param b The capture state
 */
static void box_gpu_bake_atlas_free(
    box_gpu_bake_t *b)
{
    box_gpu_bake_view_release(&b->color_view);
    box_gpu_bake_view_release(&b->depth_view);
    box_gpu_bake_view_release(&b->depth_read);
    if (b->color) {
        wgpuTextureRelease(b->color);
        b->color = NULL;
    }

    if (b->depth) {
        wgpuTextureRelease(b->depth);
        b->depth = NULL;
    }

    b->size = 0;
    b->probes = 0;
}

/** Take or drop a reference to everything the render bundles of the faces bind, so no handle
 * they were made with is reused for another object while they exist.
 *
 * @param b The capture state
 * @param hold Take the references, drop them otherwise
 */
static void box_gpu_bake_hold(
    box_gpu_bake_t *b,
    bool hold)
{
    const box_gpu_bake_key_t *k = &b->key;
    int32_t d, count = ecs_vec_count(&b->key_draws);
    WGPUBuffer buffers[4] = { k->lights, k->clusters, k->slots, k->args };
    for (d = 0; d < 4; d ++) {
        if (buffers[d] && hold) {
            wgpuBufferAddRef(buffers[d]);
        } else if (buffers[d]) {
            wgpuBufferRelease(buffers[d]);
        }
    }

    if (k->shadow && hold) {
        wgpuTextureViewAddRef(k->shadow);
    } else if (k->shadow) {
        wgpuTextureViewRelease(k->shadow);
    }

    if (k->ambient && hold) {
        wgpuBindGroupAddRef(k->ambient);
    } else if (k->ambient) {
        wgpuBindGroupRelease(k->ambient);
    }

    for (d = 0; d < count; d ++) {
        const box_gpu_bake_draw_t *draw = ecs_vec_get_t(&b->key_draws, box_gpu_bake_draw_t, d);
        if (!draw->pipeline) {
            continue;
        }

        if (hold) {
            wgpuRenderPipelineAddRef(draw->pipeline);
            wgpuBindGroupAddRef(draw->material);
            wgpuBindGroupAddRef(draw->instances);
            wgpuBufferAddRef(draw->vertices);
            wgpuBufferAddRef(draw->indices);
        } else {
            wgpuRenderPipelineRelease(draw->pipeline);
            wgpuBindGroupRelease(draw->material);
            wgpuBindGroupRelease(draw->instances);
            wgpuBufferRelease(draw->vertices);
            wgpuBufferRelease(draw->indices);
        }
    }
}

/** Release the render bundles of the faces and what they hold.
 *
 * @param b The capture state
 */
static void box_gpu_bake_bundles_free(
    box_gpu_bake_t *b)
{
    int32_t i;
    for (i = 0; i < b->bundle_count; i ++) {
        wgpuRenderBundleRelease(b->bundles[i]);
    }

    if (b->faces_group) {
        wgpuBindGroupRelease(b->faces_group);
    }

    if (b->bundles) {
        box_gpu_bake_hold(b, false);
    }

    ecs_os_free(b->bundles);
    b->bundles = NULL;
    b->bundle_count = 0;
    b->faces_group = NULL;
    ecs_os_memset_t(&b->key, 0, box_gpu_bake_key_t);
    ecs_vec_clear(&b->key_draws);
}

/** Release the capture state.
 *
 * @param b The capture state
 */
static void box_gpu_bake_state_free(
    box_gpu_bake_t *b)
{
    int32_t i;
    box_gpu_bake_atlas_free(b);
    ecs_vec_fini_t(NULL, &b->captured, box_render_bake_result_t);
    ecs_vec_fini_t(NULL, &b->view_data, uint8_t);
    ecs_vec_fini_t(NULL, &b->light_data, uint32_t);
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_readback_t *r = &b->readbacks[i];
        if (r->state == BOX_GPU_BAKE_MAPPED) {
            wgpuBufferUnmap(r->buffer);
        }

        if (r->buffer) {
            wgpuBufferRelease(r->buffer);
        }
    }

    if (b->queries) {
        wgpuQuerySetRelease(b->queries);
    }

    if (b->resolve) {
        wgpuBufferRelease(b->resolve);
    }

    if (b->project) {
        wgpuComputePipelineRelease(b->project);
    }

    if (b->cull) {
        wgpuComputePipelineRelease(b->cull);
    }

    if (b->cull_group) {
        wgpuBindGroupRelease(b->cull_group);
    }

    WGPUBuffer buffers[4] = { b->jobs, b->slots, b->args, b->lists };
    for (i = 0; i < 4; i ++) {
        if (buffers[i]) {
            wgpuBufferRelease(buffers[i]);
        }
    }

    box_gpu_bake_bundles_free(b);
    ecs_vec_fini_t(NULL, &b->key_draws, box_gpu_bake_draw_t);
    ecs_vec_fini_t(NULL, &b->draws, box_gpu_bake_draw_t);
    ecs_vec_fini_t(NULL, &b->job_data, uint8_t);
    ecs_vec_fini_t(NULL, &b->list_data, uint32_t);
    wgpuPipelineLayoutRelease(b->cull_pipeline_layout);
    wgpuBindGroupLayoutRelease(b->cull_layout);
    wgpuBufferRelease(b->uniforms);
    wgpuBufferRelease(b->params);
    wgpuBufferRelease(b->clusters);
    wgpuPipelineLayoutRelease(b->pipeline_layout);
    wgpuBindGroupLayoutRelease(b->layout);
    ecs_os_free(b);
}

/** Create the timestamp queries and their readbacks when the device has the feature.
 *
 * @param gpu The backend
 * @param b The capture state
 */
static void box_gpu_bake_queries_init(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    int32_t i;
    if (!gpu->timestamps) {
        goto done;
    }

    b->queries = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
        .type = WGPUQueryType_Timestamp,
        .count = 2 * BOX_GPU_BAKE_READBACKS
    });
    b->resolve = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = 2 * BOX_GPU_BAKE_RESOLVE_ALIGN * BOX_GPU_BAKE_READBACKS
    });
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        b->readbacks[i].buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = 2 * sizeof(uint64_t)
        });
    }

done:
    return;
}

/** Create the capture state: layouts, buffers and the timestamp queries.
 *
 * @param gpu The backend
 * @return The state
 */
static box_gpu_bake_t* box_gpu_bake_state_new(
    box_gpu_t *gpu)
{
    box_gpu_bake_t *b = ecs_os_calloc_t(box_gpu_bake_t);
    WGPUBindGroupLayoutEntry entries[6] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_bake_params_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 9,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }
    };
    b->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = entries });
    b->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &b->layout
        });
    WGPUBindGroupLayoutEntry cull[5] = {
        entries[0],
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_bake_job_t)
            }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    b->cull_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 5, .entries = cull });
    WGPUBindGroupLayout cull_groups[2] = { b->cull_layout, gpu->instance_layout };
    b->cull_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = cull_groups
        });
    b->uniforms = box_gpu_bake_buffer(gpu,
        (uint64_t)6 * BOX_GPU_BAKE_CHUNK * BOX_GPU_VIEW_STRIDE, WGPUBufferUsage_Uniform);
    b->params = box_gpu_bake_buffer(gpu, sizeof(box_gpu_bake_params_t), WGPUBufferUsage_Uniform);
    b->clusters = box_gpu_bake_buffer(gpu, BOX_GPU_BAKE_CLUSTER_WORDS * sizeof(uint32_t),
        WGPUBufferUsage_Storage);
    ecs_vec_init_t(NULL, &b->view_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &b->light_data, uint32_t, 0);
    ecs_vec_init_t(NULL, &b->captured, box_render_bake_result_t, 0);
    ecs_vec_init_t(NULL, &b->draws, box_gpu_bake_draw_t, 0);
    ecs_vec_init_t(NULL, &b->key_draws, box_gpu_bake_draw_t, 0);
    ecs_vec_init_t(NULL, &b->job_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &b->list_data, uint32_t, 0);
    box_gpu_bake_queries_init(gpu, b);

    return b;
}

/** Create a compute pipeline of an entry point of the bake shader, releasing the one it
 * replaces.
 *
 * @param gpu The backend
 * @param pipeline The pipeline, replaced
 * @param layout The pipeline layout
 * @param module The shader module
 * @param entry The entry point
 */
static void box_gpu_bake_compute(
    box_gpu_t *gpu,
    WGPUComputePipeline *pipeline,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    const char *entry)
{
    if (*pipeline) {
        wgpuComputePipelineRelease(*pipeline);
    }

    *pipeline = wgpuDeviceCreateComputePipeline(gpu->device, &(WGPUComputePipelineDescriptor){
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    });
}

/** Get the projection pipeline, created again with the cull pipeline when the bake shader was
 * recompiled.
 *
 * @param gpu The backend
 * @param b The capture state
 * @return The pipeline, NULL while the shader has no module
 */
static WGPUComputePipeline box_gpu_bake_pipeline(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_BAKE);
    if (!s || !s->module || (b->project && b->revision == s->revision)) {
        goto done;
    }

    box_gpu_bake_compute(gpu, &b->project, b->pipeline_layout, s->module, "project");
    box_gpu_bake_compute(gpu, &b->cull, b->cull_pipeline_layout, s->module, "cull");
    b->revision = s->revision;
done:
    return s && s->module && b->cull ? b->project : NULL;
}

/** Make a buffer of the cull hold a number of bytes, growing it by doubling.
 *
 * @param gpu The backend
 * @param buffer The buffer
 * @param size Bytes of the buffer, updated
 * @param needed Bytes it must hold
 * @param usage Usage of the buffer
 * @return Whether the buffer was created
 */
static bool box_gpu_bake_reserve(
    box_gpu_t *gpu,
    WGPUBuffer *buffer,
    uint64_t *size,
    uint64_t needed,
    WGPUBufferUsage usage)
{
    uint64_t capacity = *size ? *size : 256;
    bool result = false;
    if (*buffer && needed <= *size) {
        goto done;
    }

    while (capacity < needed) {
        capacity *= 2;
    }

    if (*buffer) {
        wgpuBufferRelease(*buffer);
    }

    *buffer = box_gpu_bake_buffer(gpu, capacity, usage);
    *size = capacity;
    result = true;
done:
    return result;
}

/** List the batches the faces of a bake batch draw, write the cull uniforms of each and make
 * the slots and arguments hold every face of a chunk.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param batch The bake batch
 * @param chunk Probes per chunk
 */
static void box_gpu_bake_draws(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch,
    int32_t chunk)
{
    int32_t i, d, count = ecs_vec_count(&packet->batches);
    bool grown = false;
    ecs_vec_clear(&b->draws);
    ecs_vec_clear(&b->list_data);
    b->stride = 0;
    for (i = 0; i < count; i ++) {
        const box_render_batch_t *rb = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        if (box_gpu_cull_capture_draws(gpu, rb, batch->scene, -1)) {
            box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, rb->mesh);
            box_gpu_buffer_t *inst = ecs_map_get_deref(
                &gpu->buffers, box_gpu_buffer_t, rb->instances);
            *ecs_vec_append_t(NULL, &b->draws, box_gpu_bake_draw_t) = (box_gpu_bake_draw_t){
                .batch = i,
                .region = b->stride,
                .count = rb->instance_count,
                .pipeline = box_gpu_forward_pipeline(gpu, BOX_GPU_HDR_FORMAT, rb),
                .material = box_gpu_material_group(gpu, rb, packet->anisotropy),
                .instances = box_gpu_instance_group(gpu, inst),
                .vertices = m->vertices,
                .indices = m->indices
            };
            b->stride += rb->instance_count;
        }
    }

    int32_t draws = ecs_vec_count(&b->draws);
    ecs_vec_set_count_t(NULL, &b->job_data, uint8_t, draws * BOX_GPU_BAKE_JOB_STRIDE);
    for (d = 0; d < draws; d ++) {
        const box_gpu_bake_draw_t *draw = ecs_vec_get_t(&b->draws, box_gpu_bake_draw_t, d);
        const box_render_batch_t *rb = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, draw->batch);
        box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, rb->mesh);
        box_gpu_bake_job_t *job = (box_gpu_bake_job_t*)ecs_vec_get_t(
            &b->job_data, uint8_t, d * BOX_GPU_BAKE_JOB_STRIDE);
        ecs_os_memset_t(job, 0, box_gpu_bake_job_t);
        job->first = rb->first_instance;
        job->count = draw->count;
        job->draw = (uint32_t)d;
        job->index_count = m->index_count;
        job->region = draw->region;
        job->draws = (uint32_t)draws;
        job->stride = b->stride;
        glm_vec3_copy(m->center, job->center);
        glm_vec3_copy(m->extent, job->extent);
        if (rb->listed) {
            job->first = (uint32_t)ecs_vec_count(&b->list_data);
            job->list = 1;
            ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &b->list_data, uint32_t, (int32_t)draw->count),
                box_render_packet_data(packet, rb->slots), uint32_t, (int32_t)draw->count);
        }
    }

    uint64_t faces = (uint64_t)chunk * 6;
    int32_t lists = ecs_vec_count(&b->list_data);
    grown |= box_gpu_bake_reserve(gpu, &b->jobs, &b->jobs_size,
        (uint64_t)glm_max(draws, 1) * BOX_GPU_BAKE_JOB_STRIDE, WGPUBufferUsage_Uniform);
    grown |= box_gpu_bake_reserve(gpu, &b->slots, &b->slots_size,
        faces * glm_max(b->stride, 1) * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex);
    grown |= box_gpu_bake_reserve(gpu, &b->args, &b->args_size,
        faces * (uint64_t)glm_max(draws, 1) * BOX_GPU_BAKE_ARGS * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect);
    grown |= box_gpu_bake_reserve(gpu, &b->lists, &b->lists_size,
        (uint64_t)glm_max(lists, 1) * sizeof(uint32_t), WGPUBufferUsage_Storage);
    if (draws) {
        wgpuQueueWriteBuffer(gpu->queue, b->jobs, 0, ecs_vec_first(&b->job_data),
            (size_t)draws * BOX_GPU_BAKE_JOB_STRIDE);
    }

    if (lists) {
        wgpuQueueWriteBuffer(gpu->queue, b->lists, 0, ecs_vec_first(&b->list_data),
            (size_t)lists * sizeof(uint32_t));
    }

    if (grown && b->cull_group) {
        wgpuBindGroupRelease(b->cull_group);
        b->cull_group = NULL;
    }

    if (!b->cull_group) {
        WGPUBindGroupEntry entries[5] = {
            { .binding = 0, .buffer = b->params, .size = sizeof(box_gpu_bake_params_t) },
            { .binding = 5, .buffer = b->jobs, .size = sizeof(box_gpu_bake_job_t) },
            { .binding = 6, .buffer = b->slots, .size = b->slots_size },
            { .binding = 7, .buffer = b->args, .size = b->args_size },
            { .binding = 8, .buffer = b->lists, .size = b->lists_size }
        };
        b->cull_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = b->cull_layout,
            .entryCount = 5,
            .entries = entries
        });
    }
}

/** Record the cull of the faces of a chunk: a dispatch per batch, a workgroup per probe.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param count Probes of the chunk
 * @param writes Timestamp the pass writes, NULL for none
 * @return Whether the pass was recorded
 */
static bool box_gpu_bake_cull(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    int32_t d, draws = ecs_vec_count(&b->draws);
    if (!draws) {
        goto done;
    }

    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,
        &(WGPUComputePassDescriptor){ .timestampWrites = writes });
    wgpuComputePassEncoderSetPipeline(pass, b->cull);
    for (d = 0; d < draws; d ++) {
        const box_gpu_bake_draw_t *draw = ecs_vec_get_t(&b->draws, box_gpu_bake_draw_t, d);
        uint32_t offset = (uint32_t)(d * BOX_GPU_BAKE_JOB_STRIDE);
        wgpuComputePassEncoderSetBindGroup(pass, 0, b->cull_group, 1, &offset);
        wgpuComputePassEncoderSetBindGroup(pass, 1, draw->instances, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (uint32_t)count, 1, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
done:
    return draws > 0;
}

/** Make the atlas hold the faces of a chunk at a face size: six tiles across, one row per probe.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param size Face edge in texels
 * @param probes Probes of a chunk
 */
static void box_gpu_bake_atlas(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t size,
    int32_t probes)
{
    WGPUExtent3D extent = { (uint32_t)(size * 6), (uint32_t)(size * probes), 1 };
    if (b->color && b->size == size && b->probes >= probes) {
        goto done;
    }

    box_gpu_bake_atlas_free(b);
    b->color = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = extent,
        .format = BOX_GPU_HDR_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    b->color_view = wgpuTextureCreateView(b->color, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_HDR_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });
    b->depth = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = extent,
        .format = BOX_GPU_DEPTH_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    b->depth_view = wgpuTextureCreateView(b->depth, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_DEPTH_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });
    b->depth_read = wgpuTextureCreateView(b->depth, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_DEPTH_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_DepthOnly
    });
    b->size = size;
    b->probes = probes;
done:
    return;
}

/** Record the outcome of the map of a timestamp readback.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_bake_readback_t
 * @param userdata2 Unused
 */
static void box_gpu_bake_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_bake_readback_t *r = userdata1;
    (void)message;
    (void)userdata2;
    if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        r->state = status == WGPUMapAsyncStatus_Success
            ? BOX_GPU_BAKE_MAPPED
            : BOX_GPU_BAKE_FAILED;
    }
}

/** Add a measurement of the captures of a packet to the time the main thread takes.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param probes Probes the captures held
 * @param ms Milliseconds they took, dropped when not plausible
 */
static void box_gpu_bake_measured(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t probes,
    double ms)
{
    if (probes > 0 && ms > 0 && ms < BOX_GPU_BAKE_MEASURE_MAX) {
        ecs_os_mutex_lock(gpu->lock);
        b->timing.probes += probes;
        b->timing.ms += ms;
        ecs_os_mutex_unlock(gpu->lock);
    }
}

/** Measure the captures of a packet when the queue finished their work.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_bake_done_t
 * @param userdata2 Unused
 */
static void box_gpu_bake_on_done(
    WGPUQueueWorkDoneStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_bake_done_t *d = userdata1;
    (void)message;
    (void)userdata2;
    if (status == WGPUQueueWorkDoneStatus_Success) {
        box_gpu_bake_measured(d->gpu, d->bake, d->probes,
            (double)(ecs_os_now() - d->start) * 1e-6);
    }

    if (status != WGPUQueueWorkDoneStatus_CallbackCancelled) {
        d->probes = 0;
    }
}

/** Get the nanoseconds of one tick of the timestamps.
 *
 * @param gpu The backend
 * @return The period, 1 in the browser, which resolves timestamps in nanoseconds
 */
static double box_gpu_bake_period(
    box_gpu_t *gpu)
{
#ifdef __EMSCRIPTEN__
    (void)gpu;

    return 1.0;
#else
    return (double)wgpuQueueGetTimestampPeriod(gpu->queue);
#endif
}

/** Map the timestamp copies that were submitted and publish the ones that arrived.
 *
 * @param gpu The backend
 * @param b The capture state
 */
static void box_gpu_bake_readback_poll(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    int32_t i;
#ifndef __EMSCRIPTEN__
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        if (b->readbacks[i].state == BOX_GPU_BAKE_MAPPING || b->done[i].probes) {
            wgpuDevicePoll(gpu->device, false, NULL);
            break;
        }
    }
#endif

    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_readback_t *r = &b->readbacks[i];
        if (r->state == BOX_GPU_BAKE_MAPPED) {
            const uint64_t *t = wgpuBufferGetConstMappedRange(r->buffer, 0,
                2 * sizeof(uint64_t));
            if (t && t[1] > t[0]) {
                box_gpu_bake_measured(gpu, b, r->probes,
                    (double)(t[1] - t[0]) * box_gpu_bake_period(gpu) * 1e-6);
            }

            wgpuBufferUnmap(r->buffer);
            r->state = BOX_GPU_BAKE_FREE;
        } else if (r->state == BOX_GPU_BAKE_FAILED) {
            r->state = BOX_GPU_BAKE_FREE;
        } else if (r->state == BOX_GPU_BAKE_COPIED) {
            r->state = BOX_GPU_BAKE_MAPPING;
            wgpuBufferMapAsync(r->buffer, WGPUMapMode_Read, 0, 2 * sizeof(uint64_t),
                (WGPUBufferMapCallbackInfo){
                    .mode = WGPUCallbackMode_AllowSpontaneous,
                    .callback = box_gpu_bake_on_map,
                    .userdata1 = r
                });
        }
    }
}

/** Find a free timestamp readback.
 *
 * @param b The capture state
 * @return Index of the readback, -1 without timestamps or when every one is in flight
 */
static int32_t box_gpu_bake_readback(
    box_gpu_bake_t *b)
{
    int32_t i, result = -1;
    for (i = 0; b->queries && i < BOX_GPU_BAKE_READBACKS; i ++) {
        if (b->readbacks[i].state == BOX_GPU_BAKE_FREE) {
            result = i;
            break;
        }
    }

    return result;
}

/** Finish an encoder, submit it and release it.
 *
 * @param gpu The backend
 * @param encoder The encoder
 */
static void box_gpu_bake_submit(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder)
{
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

/** Resolve the timestamps of the readbacks whose captures were submitted before the last
 * packet and copy them into their readbacks.
 *
 * Metal writes the timestamps of a pass when the pass completed, which a resolve in the same
 * or the next command buffer does not wait for, so the resolve waits for one more packet.
 *
 * @param gpu The backend
 * @param b The capture state
 */
static void box_gpu_bake_resolve(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    WGPUCommandEncoder encoder = NULL;
    int32_t r;
    uint32_t i;
    for (r = 0; r < BOX_GPU_BAKE_READBACKS; r ++) {
        box_gpu_bake_readback_t *rb = &b->readbacks[r];
        uint64_t offset = (uint64_t)r * 2 * BOX_GPU_BAKE_RESOLVE_ALIGN;
        if (rb->state != BOX_GPU_BAKE_WRITTEN) {
            continue;
        }

        if (!encoder) {
            encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        }

        for (i = 0; i < 2; i ++) {
            wgpuCommandEncoderResolveQuerySet(encoder, b->queries, (uint32_t)r * 2 + i, 1,
                b->resolve, offset + i * BOX_GPU_BAKE_RESOLVE_ALIGN);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, b->resolve,
                offset + i * BOX_GPU_BAKE_RESOLVE_ALIGN, rb->buffer,
                i * sizeof(uint64_t), sizeof(uint64_t));
        }

        rb->state = BOX_GPU_BAKE_COPIED;
    }

    if (encoder) {
        box_gpu_bake_submit(gpu, encoder);
    }
}

/** Write the uniforms of a face of a probe.
 *
 * The ambient terms are black, so the captures hold direct light and emission; the sun is
 * shadowed by the capture cascade, which ends beyond every capture. The point and spot lights
 * are one cluster that lists them all, see box_gpu_bake_lights.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param batch The bake batch
 * @param rv View with the lights of the batch, its view matrix is set here
 * @param probe The probe
 * @param slot Slot of the face
 * @param cascade The capture cascade, NULL without a sun
 */
static void box_gpu_bake_face(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_bake_batch_t *batch,
    box_render_view_t *rv,
    const box_render_probe_capture_t *probe,
    int32_t slot,
    const box_render_cascade_t *cascade)
{
    const float (*face)[3] = box_gpu_bake_faces[slot % 6];
    box_gpu_view_uniforms_t *u = (box_gpu_view_uniforms_t*)ecs_vec_get_t(
        &b->view_data, uint8_t, slot * BOX_GPU_VIEW_STRIDE);
    vec3 eye, center;
    mat4 proj;
    float size = (float)batch->size;
    glm_vec3_copy(ECS_CONST_CAST(float*, probe->position), eye);
    glm_vec3_add(eye, ECS_CONST_CAST(float*, face[0]), center);
    glm_lookat(eye, center, ECS_CONST_CAST(float*, face[1]), rv->view_matrix);
    box_camera_projection(&rv->camera_data, size, size, proj);
    ecs_os_memset_t(u, 0, box_gpu_view_uniforms_t);
    glm_mat4_mul(proj, rv->view_matrix, u->view_proj);
    glm_vec3_copy(eye, u->camera);
    u->camera[3] = 1;
    u->viewport[0] = size;
    u->viewport[1] = size;
    u->viewport[2] = 1.0f / size;
    u->viewport[3] = 1.0f / size;
    box_gpu_light_uniforms(gpu, rv, proj, u);
    u->grid[0] = 1;
    u->grid[1] = 1;
    u->grid[2] = 1;
    if (cascade) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, cascade->matrix), u->shadow_matrix[0]);
        u->shadow_split[0] = BOX_GPU_BAKE_FAR;
        u->shadow_bias[0] = BOX_GPU_BAKE_SHADOW_BIAS * cascade->texel / cascade->depth_range;
        u->shadow_offset[0] = BOX_GPU_BAKE_SHADOW_OFFSET * cascade->texel;
        u->shadow_params[0] = BOX_GPU_BAKE_FAR;
        u->shadow_params[1] = BOX_GPU_BAKE_FAR;
        u->shadow_params[2] = 1.0f / (float)BOX_GPU_CAPTURE_SHADOW_RESOLUTION;
        u->shadow_config[0] = 1;
        u->shadow_config[1] = BOX_GPU_BAKE_SHADOW_FILTER;
    }
}

/** Write the cluster buffer of the faces of a chunk: one cluster that lists every point and spot
 * light the faces shade with, in the layout of the cluster pass (etc/shaders/cluster.wgsl).
 *
 * A face is a few texels across, so a cluster grid would cull next to nothing for the cost of
 * a cluster pass per face; lights beyond their range or cone add nothing either way.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param u Uniforms of a face of the chunk, which every face shares the lights of
 */
static void box_gpu_bake_lights(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_gpu_view_uniforms_t *u)
{
    uint32_t i, first = u->lights[0], count = u->lights[1] > first ? u->lights[1] - first : 0;
    uint64_t bytes = (uint64_t)(BOX_GPU_BAKE_CLUSTER_HEADER + count) * sizeof(uint32_t);
    ecs_vec_set_count_t(NULL, &b->light_data, uint32_t,
        BOX_GPU_BAKE_CLUSTER_HEADER + (int32_t)count);
    uint32_t *words = ecs_vec_first(&b->light_data);
    words[0] = count;
    words[1] = 0;
    words[2] = count;
    for (i = 0; i < count; i ++) {
        words[BOX_GPU_BAKE_CLUSTER_HEADER + i] = first + i;
    }

    if (bytes > wgpuBufferGetSize(b->clusters)) {
        wgpuBufferRelease(b->clusters);
        b->clusters = box_gpu_bake_buffer(gpu, bytes * 2, WGPUBufferUsage_Storage);
    }

    wgpuQueueWriteBuffer(gpu->queue, b->clusters, 0, words, (size_t)bytes);
}

/** Check whether two lists of the batches the faces draw bind and draw the same.
 *
 * @param a The first list
 * @param b The second list
 * @return Whether every batch has the same slots, pipeline, groups and buffers
 */
static bool box_gpu_bake_draws_equal(
    const ecs_vec_t *a,
    const ecs_vec_t *b)
{
    int32_t d, count = ecs_vec_count(a);
    bool result = count == ecs_vec_count(b);
    for (d = 0; result && d < count; d ++) {
        const box_gpu_bake_draw_t *x = ecs_vec_get_t(a, box_gpu_bake_draw_t, d);
        const box_gpu_bake_draw_t *y = ecs_vec_get_t(b, box_gpu_bake_draw_t, d);
        result = x->region == y->region && x->count == y->count &&
            x->pipeline == y->pipeline && x->material == y->material &&
            x->instances == y->instances && x->vertices == y->vertices &&
            x->indices == y->indices;
    }

    return result;
}

/** Record the render bundle of one face of a chunk: its view uniforms, the ambient term and
 * the draws of the instances the cull lists for it.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param face Face of the chunk, six per probe
 * @return The bundle
 */
static WGPURenderBundle box_gpu_bake_bundle(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t face)
{
    WGPUTextureFormat format = BOX_GPU_HDR_FORMAT;
    WGPURenderBundleEncoder e = wgpuDeviceCreateRenderBundleEncoder(gpu->device,
        &(WGPURenderBundleEncoderDescriptor){
            .colorFormatCount = 1,
            .colorFormats = &format,
            .depthStencilFormat = BOX_GPU_DEPTH_FORMAT,
            .sampleCount = 1
        });
    WGPURenderPipeline pipeline = NULL;
    WGPUBindGroup material = NULL;
    uint32_t offset = (uint32_t)(face * BOX_GPU_VIEW_STRIDE);
    int32_t d, draws = ecs_vec_count(&b->draws);
    wgpuRenderBundleEncoderSetBindGroup(e, 0, b->faces_group, 1, &offset);
    wgpuRenderBundleEncoderSetBindGroup(e, 3, b->key.ambient, 0, NULL);
    for (d = 0; d < draws; d ++) {
        const box_gpu_bake_draw_t *draw = ecs_vec_get_t(&b->draws, box_gpu_bake_draw_t, d);
        uint64_t first = (uint64_t)face * b->stride + draw->region;
        uint64_t args = (uint64_t)face * (uint64_t)draws + (uint64_t)d;
        if (!draw->pipeline) {
            continue;
        }

        if (draw->pipeline != pipeline) {
            wgpuRenderBundleEncoderSetPipeline(e, draw->pipeline);
            pipeline = draw->pipeline;
        }

        if (draw->material != material) {
            wgpuRenderBundleEncoderSetBindGroup(e, 1, draw->material, 0, NULL);
            material = draw->material;
        }

        wgpuRenderBundleEncoderSetBindGroup(e, 2, draw->instances, 0, NULL);
        wgpuRenderBundleEncoderSetVertexBuffer(e, 0, draw->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderBundleEncoderSetVertexBuffer(e, 1, b->slots, first * sizeof(uint32_t),
            (uint64_t)draw->count * sizeof(uint32_t));
        wgpuRenderBundleEncoderSetIndexBuffer(
            e, draw->indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderBundleEncoderDrawIndexedIndirect(e, b->args,
            args * BOX_GPU_BAKE_ARGS * sizeof(uint32_t));
    }

    WGPURenderBundle result = wgpuRenderBundleEncoderFinish(e, NULL);
    wgpuRenderBundleEncoderRelease(e);

    return result;
}

/** Make the render bundles of the faces of a chunk, again only when what they bind changed:
 * the batches the faces draw, the lights, the shadow map, the ambient term or the buffers of
 * the cull. Static content records them once for every chunk of every frame.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param rv View with the lights of the batch
 * @param shadow Array view of the shadow map the faces sample
 * @param ambient Volume group the faces shade their ambient term with
 * @param faces Faces of the chunk
 */
static void box_gpu_bake_bundles(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_view_t *rv,
    WGPUTextureView shadow,
    WGPUBindGroup ambient,
    int32_t faces)
{
    box_gpu_bake_key_t key;
    int32_t i, count = glm_max(faces, b->probes * 6);
    ecs_os_memset_t(&key, 0, box_gpu_bake_key_t);
    key.lights = box_gpu_light_binding(gpu, rv, &key.lights_size);
    key.clusters = b->clusters;
    key.shadow = shadow;
    key.ambient = ambient;
    key.slots = b->slots;
    key.args = b->args;
    key.stride = b->stride;
    if (b->bundles && b->bundle_count >= faces &&
        !memcmp(&key, &b->key, sizeof(box_gpu_bake_key_t)) &&
        box_gpu_bake_draws_equal(&b->draws, &b->key_draws))
    {
        goto done;
    }

    box_gpu_bake_bundles_free(b);
    b->key = key;
    ecs_vec_set_count_t(NULL, &b->key_draws, box_gpu_bake_draw_t, ecs_vec_count(&b->draws));
    if (ecs_vec_count(&b->draws)) {
        ecs_os_memcpy_n(ecs_vec_first(&b->key_draws), ecs_vec_first(&b->draws),
            box_gpu_bake_draw_t, ecs_vec_count(&b->draws));
    }

    box_gpu_bake_hold(b, true);
    b->faces_group = box_gpu_light_group_with(gpu, rv, b->uniforms, b->clusters, shadow);
    b->bundles = ecs_os_calloc_n(WGPURenderBundle, count);
    b->bundle_count = count;
    for (i = 0; i < count; i ++) {
        b->bundles[i] = box_gpu_bake_bundle(gpu, b, i);
    }

done:
    return;
}

/** Record the render pass that draws the faces of a chunk into their tiles of the atlas.
 *
 * Face f of probe p is the tile at column f and row p; each face replays its render bundle,
 * which draws the instances the cull listed for it with its own view uniforms.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param rv View with the lights of the batch
 * @param faces Faces of the chunk
 * @param shadow Array view of the shadow map the faces sample
 * @param ambient Volume group the faces shade their ambient term with
 */
static void box_gpu_bake_faces_render(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    const box_render_view_t *rv,
    int32_t faces,
    WGPUTextureView shadow,
    WGPUBindGroup ambient)
{
    uint32_t size = (uint32_t)b->size;
    int32_t i, d, draws = 0;
    WGPURenderPassColorAttachment color = {
        .view = b->color_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0, 0, 0, 0 }
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = b->depth_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    box_gpu_bake_bundles(gpu, b, rv, shadow, ambient, faces);
    for (d = 0; d < ecs_vec_count(&b->draws); d ++) {
        draws += ecs_vec_get_t(&b->draws, box_gpu_bake_draw_t, d)->pipeline ? 1 : 0;
    }

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,
        &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color,
            .depthStencilAttachment = &depth
        });
    for (i = 0; i < faces; i ++) {
        uint32_t x = (uint32_t)(i % 6) * size, y = (uint32_t)(i / 6) * size;
        wgpuRenderPassEncoderSetViewport(pass, (float)x, (float)y, (float)size, (float)size,
            0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, x, y, size, size);
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &b->bundles[i]);
        for (d = 0; d < draws; d ++) {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Write the uniforms the cull and the projection of a chunk share.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param batch The bake batch
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 * @param spacing Spacing of the probes of the volume
 */
static void box_gpu_bake_params(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_bake_batch_t *batch,
    const box_render_probe_capture_t *probes,
    int32_t count,
    float spacing)
{
    box_gpu_buffer_t *baked = batch->baked
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, batch->baked)
        : NULL;
    uint32_t capacity = baked ? baked->size / (uint32_t)sizeof(box_render_light_t) : 0;
    box_gpu_bake_params_t params = {
        .size = { (uint32_t)batch->size, (uint32_t)count, 0, 0 },
        .near_ = {
            BOX_GPU_BAKE_NEAR, (float)batch->size * 0.5f, BOX_RENDER_CULL_MIN_DIMENSION, spacing
        }
    };
    int32_t i;
    params.lights[0] = glm_min(batch->baked_count, capacity);
    params.lights[1] = glm_min(batch->baked_directional, params.lights[0]);
    glm_vec4_copy(ECS_CONST_CAST(float*, batch->lights.ambient_sky), params.sky);
    glm_vec4_copy(ECS_CONST_CAST(float*, batch->lights.ambient_ground), params.ground);
    for (i = 0; i < count; i ++) {
        params.probes[i] = probes[i].index;
        glm_vec3_copy(ECS_CONST_CAST(float*, probes[i].position), params.positions[i]);
    }

    wgpuQueueWriteBuffer(gpu->queue, b->params, 0, &params, sizeof(params));
}

/** Record the projection of the faces of a chunk onto the SH of its probes.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param pipeline The projection pipeline
 * @param volume The volume the probes are written into
 * @param batch The bake batch
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 * @param writes Timestamp the pass writes, NULL for none
 */
static void box_gpu_bake_project(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    WGPUComputePipeline pipeline,
    box_gpu_volume_t *volume,
    const box_render_bake_batch_t *batch,
    const box_render_probe_capture_t *probes,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    box_gpu_buffer_t *baked = batch->baked
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, batch->baked)
        : NULL;
    int32_t i;
    WGPUBindGroupEntry entries[6] = {
        { .binding = 0, .buffer = b->params, .size = sizeof(box_gpu_bake_params_t) },
        { .binding = 1, .textureView = b->color_view },
        {
            .binding = 2,
            .buffer = volume->probes[batch->target],
            .size = (uint64_t)volume->count * BOX_GPU_PROBE_SIZE
        },
        { .binding = 3, .textureView = b->depth_read },
        {
            .binding = 4,
            .buffer = baked ? baked->buffer : gpu->no_lights,
            .size = baked ? baked->size : sizeof(box_render_light_t)
        },
        {
            .binding = 9,
            .buffer = volume->visibility,
            .size = (uint64_t)volume->count * BOX_GPU_VISIBILITY_SIZE
        }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = b->layout,
        .entryCount = 6,
        .entries = entries
    });
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,
        &(WGPUComputePassDescriptor){ .timestampWrites = writes });
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, (uint32_t)count, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.probes += count;
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &b->captured, box_render_bake_result_t) =
            (box_render_bake_result_t){
                .volume = batch->volume,
                .generation = batch->generation,
                .pass = batch->pass,
                .probe = probes[i].index
            };
    }

    ecs_os_mutex_unlock(gpu->lock);
}

/** Get the probes of a chunk: as many as the atlas, slots and visible buffer allow.
 *
 * @param packet The packet
 * @param batch The bake batch
 * @return Probes per chunk, at least 1
 */
static int32_t box_gpu_bake_chunk(
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch)
{
    int64_t instances = 0, texels = (int64_t)batch->size * batch->size * 6;
    int32_t i, result = BOX_GPU_BAKE_CHUNK;
    for (i = 0; i < ecs_vec_count(&packet->batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        if (b->scene == batch->scene && b->bake && b->view == -1) {
            instances += b->instance_count;
        }
    }

    if (instances * 6 * result > BOX_GPU_BAKE_VISIBLE) {
        result = (int32_t)(BOX_GPU_BAKE_VISIBLE / (instances * 6));
    }

    if (texels * result > BOX_GPU_BAKE_TEXELS) {
        result = (int32_t)(BOX_GPU_BAKE_TEXELS / texels);
    }

    return result < 1 ? 1 : result;
}

/** Capture one chunk of probes of a bake batch in one command buffer.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param batch The bake batch
 * @param rv View with the lights of the batch
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 * @param cascade The capture cascade, NULL without a sun
 * @param shadow Receives the shadow map the chunk sampled, rendered by the first chunk
 * @param timer The timestamps of the packet
 * @param last Whether the chunk is the last of the packet
 */
static void box_gpu_bake_chunk_capture(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch,
    box_render_view_t *rv,
    const box_render_probe_capture_t *probes,
    int32_t count,
    const box_render_cascade_t *cascade,
    WGPUTextureView *shadow,
    box_gpu_bake_timer_t *timer,
    bool last)
{
    WGPUComputePipeline pipeline = box_gpu_bake_pipeline(gpu, b);
    box_gpu_volume_t *volume = ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, batch->volume);
    int32_t i, faces = count * 6;
    bool caster = cascade && !*shadow;
    if (!pipeline || !volume) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        if (probes[i].index >= volume->count) {
            goto done;
        }
    }

    ecs_vec_set_count_t(NULL, &b->view_data, uint8_t, faces * BOX_GPU_VIEW_STRIDE);
    for (i = 0; i < faces; i ++) {
        box_gpu_bake_face(gpu, b, batch, rv, &probes[i / 6], i, cascade);
    }

    box_gpu_bake_lights(gpu, b, ecs_vec_first(&b->view_data));
    box_gpu_bake_params(gpu, b, batch, probes, count, volume->spacing);
    if (caster) {
        box_gpu_cull_reset(gpu);
        box_gpu_cull_capture_add(gpu, packet, batch->scene, 0, 0,
            ECS_CONST_CAST(vec4*, cascade->matrix), 0);
        box_gpu_cull_upload(gpu);
    }

    wgpuQueueWriteBuffer(gpu->queue, b->uniforms, 0, ecs_vec_first(&b->view_data),
        (size_t)faces * BOX_GPU_VIEW_STRIDE);
    uint32_t query = (uint32_t)timer->readback * 2;
    WGPUPassTimestampWrites begin = {
        .querySet = b->queries,
        .beginningOfPassWriteIndex = query,
        .endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED
    };
    WGPUPassTimestampWrites end = {
        .querySet = b->queries,
        .beginningOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED,
        .endOfPassWriteIndex = query + 1
    };
    bool timed = timer->readback != -1;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    if (box_gpu_bake_cull(gpu, b, encoder, count,
        timed && !timer->begun ? &begin : NULL))
    {
        timer->begun |= timed;
    }

    if (caster) {
        box_gpu_cull_record_all(gpu, encoder, NULL);
        *shadow = box_gpu_shadow_capture(gpu, encoder, packet, 0, cascade);
    }

    timed = timed && timer->begun && last;
    box_gpu_bake_faces_render(gpu, b, encoder, rv, faces,
        *shadow ? *shadow : gpu->shadow_empty_view,
        batch->source == BOX_RENDER_BAKE_BLACK ? gpu->no_volume->group[0]
            : volume->group[batch->source]);
    box_gpu_bake_project(gpu, b, encoder, pipeline, volume, batch, probes, count,
        timed ? &end : NULL);
    timer->ended |= timed;
    box_gpu_bake_submit(gpu, encoder);
#ifndef __EMSCRIPTEN__
    if (++ b->in_flight >= BOX_GPU_BAKE_IN_FLIGHT) {
        wgpuDevicePoll(gpu->device, true, NULL);
        b->in_flight = 0;
    }
#endif
done:
    return;
}

/** Capture the probes of a bake batch, chunk by chunk.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param batch The bake batch
 * @param timer The timestamps of the packet
 * @param last Whether the batch is the last of the packet
 */
static void box_gpu_bake_batch(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch,
    box_gpu_bake_timer_t *timer,
    bool last)
{
    box_render_view_t rv;
    box_render_cascade_t cascade;
    WGPUTextureView shadow = NULL;
    int32_t first, chunk = box_gpu_bake_chunk(packet, batch);
    int32_t size = batch->size;
    const box_render_probe_capture_t *probes = ecs_vec_get_t(
        &packet->probes, box_render_probe_capture_t, batch->first);
    ecs_os_memset_t(&rv, 0, box_render_view_t);
    rv.scene = batch->scene;
    rv.lights = batch->lights;
    rv.lights.enabled = true;
    glm_vec4_zero(rv.lights.ambient_sky);
    glm_vec4_zero(rv.lights.ambient_ground);
    rv.camera_data = (Camera){ .fov = 90, .near_ = BOX_GPU_BAKE_NEAR };
    bool sun = rv.lights.directional && glm_vec3_norm(rv.lights.sun) > 1e-6f;
    if (sun) {
        vec3 direction;
        glm_vec3_normalize_to(rv.lights.sun, direction);
        box_gpu_shadow_capture_fit(batch->bounds_min, batch->bounds_max, direction, &cascade);
    }

    box_gpu_bake_atlas(gpu, b, size, chunk);
    box_gpu_bake_draws(gpu, b, packet, batch, chunk);
    for (first = 0; first < batch->count; first += chunk) {
        int32_t count = batch->count - first < chunk ? batch->count - first : chunk;
        box_gpu_bake_chunk_capture(gpu, b, packet, batch, &rv, &probes[first], count,
            sun ? &cascade : NULL, &shadow, timer, last && first + count >= batch->count);
    }
}

void box_gpu_bake_fini(
    box_gpu_t *gpu)
{
    if (gpu->bake) {
        box_gpu_bake_t *b = gpu->bake;
        ecs_os_mutex_lock(gpu->lock);
        gpu->bake = NULL;
        ecs_os_mutex_unlock(gpu->lock);
        box_gpu_bake_state_free(b);
    }
}

/** Check whether the backend can capture a bake batch of a packet.
 *
 * @param packet The packet
 * @param batch The bake batch
 * @return Whether the batch has probes, a face size and its probes are in the packet
 */
static bool box_gpu_bake_batch_valid(
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch)
{
    bool source = batch->source == BOX_RENDER_BAKE_BLACK ||
        batch->source == 0 || batch->source == 1;
    bool target = batch->target == 0 || batch->target == 1;

    return batch->count > 0 && batch->size > 0 && batch->first >= 0 && source && target &&
        batch->first + batch->count <= ecs_vec_count(&packet->probes);
}

/** Measure captures from the CPU until the queue reports their work done, the fallback of
 * devices without timestamp queries. The time includes queued work before the captures and,
 * natively, the wait for the next poll, so it errs on the expensive side.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param start ecs_os_now when the captures started recording
 * @param probes Probes the captures held
 */
static void box_gpu_bake_done_measure(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    uint64_t start,
    int32_t probes)
{
    int32_t i;
    for (i = 0; probes && i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_done_t *d = &b->done[i];
        if (!d->probes) {
            d->bake = b;
            d->gpu = gpu;
            d->start = start;
            d->probes = probes;
            wgpuQueueOnSubmittedWorkDone(gpu->queue, (WGPUQueueWorkDoneCallbackInfo){
                .mode = WGPUCallbackMode_AllowSpontaneous,
                .callback = box_gpu_bake_on_done,
                .userdata1 = d
            });
            break;
        }
    }
}

void box_gpu_bake_capture(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->bakes), probes = 0, last = -1;
    box_gpu_bake_timer_t timer = { .readback = -1 };
    uint64_t start = ecs_os_now();
    if (gpu->bake) {
        box_gpu_bake_readback_poll(gpu, gpu->bake);
        box_gpu_bake_resolve(gpu, gpu->bake);
    }

    for (i = 0; i < count; i ++) {
        if (box_gpu_bake_batch_valid(packet,
            ecs_vec_get_t(&packet->bakes, box_render_bake_batch_t, i)))
        {
            last = i;
        }
    }

    if (last == -1) {
        goto done;
    }

    if (!gpu->bake) {
        box_gpu_bake_t *created = box_gpu_bake_state_new(gpu);
        ecs_os_mutex_lock(gpu->lock);
        gpu->bake = created;
        ecs_os_mutex_unlock(gpu->lock);
    }

    box_gpu_bake_t *b = gpu->bake;
    timer.readback = box_gpu_bake_readback(b);
    for (i = 0; i <= last; i ++) {
        const box_render_bake_batch_t *batch = ecs_vec_get_t(
            &packet->bakes, box_render_bake_batch_t, i);
        if (box_gpu_bake_batch_valid(packet, batch)) {
            box_gpu_bake_batch(gpu, b, packet, batch, &timer, i == last);
            probes += batch->count;
        }
    }

    if (timer.ended) {
        b->readbacks[timer.readback].probes = probes;
        b->readbacks[timer.readback].state = BOX_GPU_BAKE_WRITTEN;
    } else if (!b->queries) {
        box_gpu_bake_done_measure(gpu, b, start, probes);
    }

done:
    return;
}

bool box_render_gpu_bake_captured(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    box_render_gpu_stats_t stats;
    bool result = box_render_gpu_stats(world, &stats);
    ecs_vec_clear(out);
    if (!result) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    ecs_os_mutex_lock(gpu->lock);
    int32_t count = gpu->bake ? ecs_vec_count(&gpu->bake->captured) : 0;
    if (count) {
        ecs_vec_set_count_t(NULL, out, box_render_bake_result_t, count);
        ecs_os_memcpy_n(ecs_vec_first(out), ecs_vec_first(&gpu->bake->captured),
            box_render_bake_result_t, count);
        ecs_vec_clear(&gpu->bake->captured);
    }

    ecs_os_mutex_unlock(gpu->lock);
done:
    return result;
}

bool box_render_gpu_bake_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    box_render_gpu_stats_t stats;
    bool result = box_render_gpu_stats(world, &stats);
    ecs_os_memset_t(out, 0, box_render_bake_timing_t);
    if (!result) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    ecs_os_mutex_lock(gpu->lock);
    out->measured = gpu->timestamps;
    if (gpu->bake) {
        out->probes = gpu->bake->timing.probes;
        out->ms = gpu->bake->timing.ms;
        gpu->bake->timing.probes = 0;
        gpu->bake->timing.ms = 0;
    }

    ecs_os_mutex_unlock(gpu->lock);
done:
    return result;
}

#else

bool box_render_gpu_bake_captured(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    (void)world;
    ecs_vec_clear(out);

    return false;
}

bool box_render_gpu_bake_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    (void)world;
    ecs_os_memset_t(out, 0, box_render_bake_timing_t);

    return false;
}

#endif
