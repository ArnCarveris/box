/**
 * @file render/gpu_particle.c
 * @brief Particle passes of the webgpu backend: the integrate and spawn dispatches of every
 * pool of a packet, before its targets, the sort of the alpha pools of a view and the indirect
 * draw of the pools of a view's scene.
 *
 * Pools are ranges of the particle pages the extract created; the blocks of a packet are one
 * uniform buffer bound with a dynamic offset per pool, and the integrate pass writes the
 * indirect draw arguments of each pool, so the CPU never reads particle data back.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Invocations of a workgroup of the particle compute shader */
#define BOX_GPU_PARTICLE_GROUP (256u)

/** Bytes between the blocks of two pools, a multiple of the dynamic offset alignment of
 * webgpu */
#define BOX_GPU_PARTICLE_STRIDE (512u)

_Static_assert(sizeof(box_render_particle_block_t) <= BOX_GPU_PARTICLE_STRIDE,
    "a particle block must fit in its stride");

/** Bytes of the indirect draw arguments of a pool */
#define BOX_GPU_PARTICLE_ARGS (16u)

/** Bytes between two steps of the sort step table, the dynamic offset alignment */
#define BOX_GPU_PARTICLE_STEP_STRIDE (256u)

/** Largest log2 of the keys of a sort, of BOX_PARTICLE_MAX */
#define BOX_GPU_PARTICLE_SORT_LEVELS (22u)

/** Steps of the sort step table: a step per run length 2^m and compare distance 2^i, i < m */
#define BOX_GPU_PARTICLE_STEPS \
    ((BOX_GPU_PARTICLE_SORT_LEVELS * (BOX_GPU_PARTICLE_SORT_LEVELS + 1u)) / 2u)

/** Log2 of BOX_RENDER_PARTICLE_SORT_MIN, the keys a workgroup sorts */
#define BOX_GPU_PARTICLE_SORT_GROUP_LOG (9u)

/** Storage buffers the vertex stage of the draw reads: particles, keys, corners and the curve
 * table, and the reflection probes of the view group, visible to the vertex stage */
#define BOX_GPU_PARTICLE_VERTEX_STORAGE (5u)

/** Storage buffers of the integrate and spawn passes: blocks, particles, spawn slots, mesh
 * table, arguments, the curve table and the work items */
#define BOX_GPU_PARTICLE_SIM_STORAGE (7u)

/** Bytes between the batch uniforms of two batched dispatches, the dynamic offset alignment */
#define BOX_GPU_PARTICLE_BATCH_STRIDE (256u)

/** Draw pipelines, one per color format and ParticleBlend */
#define BOX_GPU_PARTICLE_DRAWS (BOX_GPU_BLIT_FORMATS * 2)

/** The draw of a pool in the views of its scene, valid for the packet that simulated it */
typedef struct box_gpu_particle_draw_t {
    ecs_entity_t scene;             /**< Scene root of the pool */
    uint32_t block;                 /**< Index of its block and its draw arguments */
    WGPUBindGroup group;            /**< Binds its block, particles, keys, mesh, sprite and the
                                     *   curve table */
    WGPUBuffer page;                /**< Particle page of the pool */
    uint64_t page_size;             /**< Bytes of the page */
    uint32_t offset;                /**< Byte offset of the pool in the page */
    uint64_t size;                  /**< Bytes of the pool */
    uint32_t sort_count;            /**< Keys the sort of a view sorts, 0 when unsorted */
    uint32_t blend;                 /**< ParticleBlend of the pipeline */
    bool soft;                      /**< It fades in front of the depth of the view */
    float origin[3];                /**< World position of the emitter */
} box_gpu_particle_draw_t;

/** A bind group of the particle passes, shared by the pools of a packet that bind the same
 * resources: the pools of a page differ only in their block, bound with a dynamic offset */
typedef struct box_gpu_particle_group_t {
    WGPUBuffer page;                /**< Particle page */
    WGPUBuffer keys;                /**< Sort keys, no_table when unsorted */
    WGPUBuffer table;               /**< Mesh table or billboard corners, no_table for none */
    WGPUTextureView texture;        /**< Sprite of a draw group, NULL for the compute groups */
    WGPUBuffer curves;              /**< Curve table, no_table for none */
    WGPUBindGroup group;            /**< The group */
} box_gpu_particle_group_t;

/** The pools of a packet that one batched integrate and spawn dispatch simulate: those that
 * bind the same page, mesh table and curve table */
typedef struct box_gpu_particle_sim_t {
    box_gpu_particle_group_t key;   /**< Resources the pools bind, its group unused */
    uint64_t page_size;             /**< Bytes of the page */
    uint64_t table_size;            /**< Bytes of the mesh table, 32 for none */
    uint64_t curves_size;           /**< Bytes of the curve table, 32 for none */
    uint32_t integrate;             /**< Batch of the integrate workgroups */
    uint32_t integrate_groups;      /**< Workgroups of the integrate dispatch */
    uint32_t spawn;                 /**< Batch of the spawn workgroups */
    uint32_t spawn_groups;          /**< Workgroups of the spawn dispatch, 0 without spawns */
} box_gpu_particle_sim_t;

/** The sorted pools of a packet that one batched dispatch per sort pass sorts: those of a page
 * and a scene that sort at most a workgroup of keys, or one pool that sorts more */
typedef struct box_gpu_particle_sort_t {
    WGPUBuffer page;                /**< Particle page of the pools */
    uint64_t page_size;             /**< Bytes of the page */
    ecs_entity_t scene;             /**< Scene root of the pools */
    uint32_t keys;                  /**< Batch of the sort_keys workgroups */
    uint32_t keys_groups;           /**< Workgroups of the sort_keys dispatch */
    uint32_t runs;                  /**< Batch of the workgroup runs of the pools */
    uint32_t runs_groups;           /**< Workgroups of the sort_local, sort_global and
                                     *   sort_merge dispatches */
    uint32_t count;                 /**< Keys of the one pool that sorts more than a
                                     *   workgroup, 0 for the batch of the smaller pools */
} box_gpu_particle_sort_t;

/** A pool of a packet to batch, with the resources that decide its batch */
typedef struct box_gpu_particle_ref_t {
    uintptr_t page;                 /**< Particle page */
    uintptr_t table;                /**< Mesh table, or the scene of a sorted pool */
    uintptr_t curves;               /**< Curve table, 0 for a sorted pool */
    uint32_t index;                 /**< Index of the pool in the packet */
} box_gpu_particle_ref_t;

/** A draw pipeline of a color format and a blend */
typedef struct box_gpu_particle_pipeline_t {
    WGPUTextureFormat format;       /**< Format of the color target */
    uint32_t blend;                 /**< ParticleBlend */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_particle_pipeline_t;

/** A draw of a view in the order it is recorded */
typedef struct box_gpu_particle_order_t {
    float depth;                    /**< View depth of the emitter */
    int32_t index;                  /**< Index of the draw in the draws of the packet */
} box_gpu_particle_order_t;

struct box_gpu_particles_t {
    WGPUBindGroupLayout sim_layout; /**< Block, particles, spawn slots, mesh table, arguments */
    WGPUPipelineLayout sim_pipeline_layout; /**< Pipeline layout of the compute passes */
    WGPUComputePipeline integrate;  /**< Ages and moves the used slots of a pool */
    WGPUComputePipeline spawn;      /**< Writes the spawned particles into their slots */
    WGPUBindGroupLayout sort_layout; /**< Block, particles, keys, view uniforms, sort step */
    WGPUPipelineLayout sort_pipeline_layout; /**< Pipeline layout of the sort passes */
    WGPUComputePipeline sort_keys;  /**< Writes the keys of a pool for a view */
    WGPUComputePipeline sort_local; /**< Sorts every run of a workgroup of keys */
    WGPUComputePipeline sort_global; /**< One step of a long compare distance */
    WGPUComputePipeline sort_merge; /**< The short steps of a run length */
    int32_t sim_revision;           /**< Revision of the shader the compute pipelines use */
    WGPUBindGroupLayout draw_layout; /**< Block, particles, keys, corners, sprite, sampler */
    WGPUBindGroupLayout depth_layout; /**< Depth of the view a soft particle reads */
    WGPUPipelineLayout draw_pipeline_layout; /**< View uniforms, the draw, the depth */
    box_gpu_particle_pipeline_t draws[BOX_GPU_PARTICLE_DRAWS]; /**< Draw pipelines */
    int32_t draw_count;             /**< Draw pipelines created */
    int32_t draw_revision;          /**< Revision of the shader the draw pipelines use */
    WGPUBuffer blocks;              /**< box_render_particle_block_t per pool of the packet */
    uint64_t blocks_size;           /**< Bytes of the block buffer */
    WGPUBuffer slots;               /**< Spawn slots of every pool of the packet */
    uint64_t slots_size;            /**< Bytes of the slot buffer */
    WGPUBuffer args;                /**< Indirect draw arguments per pool of the packet */
    uint64_t args_size;             /**< Bytes of the argument buffer */
    WGPUBuffer keys;                /**< Sort keys of the sorted pools of the packet */
    uint64_t keys_size;             /**< Bytes of the key buffer */
    WGPUBuffer steps;               /**< Run length and compare distance of every sort step */
    WGPUBuffer work;                /**< Pool and chunk of every workgroup of the batched
                                     *   dispatches of the packet, two uint32_t each */
    uint64_t work_size;             /**< Bytes of the work buffer */
    WGPUBuffer batches;             /**< First work item of every batch, a uniform each
                                     *   BOX_GPU_PARTICLE_BATCH_STRIDE apart */
    uint64_t batches_size;          /**< Bytes of the batch buffer */
    WGPUBuffer no_table;            /**< Mesh table, keys and corners of the pools without */
    WGPUTexture no_depth;           /**< Depth bound while the view writes its depth */
    WGPUBindGroup no_depth_group;   /**< Binds no_depth */
    ecs_vec_t block_data;           /**< Bytes of the blocks of the packet */
    ecs_vec_t slot_data;            /**< uint32_t, the spawn slots of the packet */
    ecs_vec_t work_data;            /**< uint32_t, the work items of the packet */
    ecs_vec_t batch_data;           /**< Bytes of the batch uniforms of the packet */
    ecs_vec_t refs;                 /**< box_gpu_particle_ref_t, the pools being batched */
    ecs_vec_t sims;                 /**< box_gpu_particle_sim_t of the packet */
    ecs_vec_t sorts;                /**< box_gpu_particle_sort_t of the packet */
    ecs_vec_t draws_of_packet;      /**< box_gpu_particle_draw_t of the last packet */
    ecs_vec_t groups_of_packet;     /**< box_gpu_particle_group_t, the draw groups the draws of
                                     *   the last packet share */
    ecs_vec_t groups_of_pass;       /**< box_gpu_particle_group_t, the groups of the compute
                                     *   pass being recorded */
    ecs_vec_t order;                /**< box_gpu_particle_order_t, the draws of a view */
};

/** Create a buffer.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer, CopyDst is added
 * @return The buffer
 */
static WGPUBuffer box_gpu_particle_buffer(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    WGPUBufferDescriptor desc = {
        .usage = usage | WGPUBufferUsage_CopyDst,
        .size = (size + 15) & ~(uint64_t)15
    };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

/** Make a buffer hold a number of bytes, growing it in powers of two.
 *
 * @param gpu The backend
 * @param buffer The buffer, replaced when it is too small
 * @param size The bytes it holds, updated
 * @param need Bytes it must hold
 * @param usage Usage of the buffer
 */
static void box_gpu_particle_reserve(
    box_gpu_t *gpu,
    WGPUBuffer *buffer,
    uint64_t *size,
    uint64_t need,
    WGPUBufferUsage usage)
{
    uint64_t capacity = *size ? *size : 1024;
    if (*buffer && need <= *size) {
        goto done;
    }

    while (capacity < need) {
        capacity *= 2;
    }

    if (*buffer) {
        wgpuBufferRelease(*buffer);
    }

    *buffer = box_gpu_particle_buffer(gpu, capacity, usage);
    *size = capacity;
done:
    return;
}

/** Fill the sort step table: the run length and compare distance of step i of run length 2^m
 * at index m * (m - 1) / 2 + i.
 *
 * @param gpu The backend
 * @param s The particle state
 */
static void box_gpu_particle_steps(
    box_gpu_t *gpu,
    box_gpu_particles_t *s)
{
    uint64_t size = (uint64_t)BOX_GPU_PARTICLE_STEPS * BOX_GPU_PARTICLE_STEP_STRIDE;
    uint8_t *data = ecs_os_calloc((ecs_size_t)size);
    uint32_t m, i;
    for (m = 1; m <= BOX_GPU_PARTICLE_SORT_LEVELS; m ++) {
        for (i = 0; i < m; i ++) {
            uint32_t *step = (uint32_t*)(data +
                (m * (m - 1) / 2 + i) * BOX_GPU_PARTICLE_STEP_STRIDE);
            step[0] = 1u << m;
            step[1] = 1u << i;
        }
    }

    s->steps = box_gpu_particle_buffer(gpu, size, WGPUBufferUsage_Uniform);
    wgpuQueueWriteBuffer(gpu->queue, s->steps, 0, data, (size_t)size);
    ecs_os_free(data);
}

/** Create the layout of the sort passes and its pipeline layout.
 *
 * @param gpu The backend
 * @param s The particle state
 */
static void box_gpu_particle_sort_init(
    box_gpu_t *gpu,
    box_gpu_particles_t *s)
{
    WGPUBindGroupLayoutEntry sort[7] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_view_uniforms_t)
            }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = 16
            }
        },
        {
            .binding = 9,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 10,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = 16
            }
        }
    };
    s->sort_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 7, .entries = sort });
    s->sort_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &s->sort_layout
        });
    box_gpu_particle_steps(gpu, s);
}

/** Create the layouts of the draw, its pipeline layout and the depth of the views that write
 * their depth.
 *
 * @param gpu The backend
 * @param s The particle state
 */
static void box_gpu_particle_draw_init(
    box_gpu_t *gpu,
    box_gpu_particles_t *s)
{
    WGPUBindGroupLayoutEntry draw[7] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_render_particle_block_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutEntry depth = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D
        }
    };
    s->draw_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 7, .entries = draw });
    s->depth_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 1, .entries = &depth });
    WGPUBindGroupLayout groups[3] = { gpu->view_layout, s->draw_layout, s->depth_layout };
    s->draw_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 3,
            .bindGroupLayouts = groups
        });
    s->no_depth = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = BOX_GPU_DEPTH_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    WGPUTextureView view = wgpuTextureCreateView(s->no_depth, NULL);
    WGPUBindGroupEntry entry = { .binding = 0, .textureView = view };
    s->no_depth_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->depth_layout,
        .entryCount = 1,
        .entries = &entry
    });
    wgpuTextureViewRelease(view);
}

void box_gpu_particles_init(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = ecs_os_calloc_t(box_gpu_particles_t);
    WGPULimits limits = WGPU_LIMITS_INIT;
    WGPUBindGroupLayoutEntry sim[8] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 9,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 10,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = 16
            }
        }
    };
    if (wgpuDeviceGetLimits(gpu->device, &limits) == WGPUStatus_Success &&
        limits.maxStorageBuffersPerShaderStage != WGPU_LIMIT_U32_UNDEFINED &&
        (limits.maxStorageBuffersPerShaderStage < BOX_GPU_PARTICLE_VERTEX_STORAGE ||
            limits.maxStorageBuffersPerShaderStage < BOX_GPU_PARTICLE_SIM_STORAGE))
    {
        ecs_warn("particles need %u storage buffers per shader stage, the device has %u",
            BOX_GPU_PARTICLE_SIM_STORAGE, limits.maxStorageBuffersPerShaderStage);
    }

    s->sim_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 8, .entries = sim });
    s->sim_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &s->sim_layout
        });
    box_gpu_particle_sort_init(gpu, s);
    box_gpu_particle_draw_init(gpu, s);
    s->no_table = box_gpu_particle_buffer(gpu, 32, WGPUBufferUsage_Storage);
    ecs_vec_init_t(NULL, &s->block_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &s->slot_data, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->work_data, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->batch_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &s->refs, box_gpu_particle_ref_t, 0);
    ecs_vec_init_t(NULL, &s->sims, box_gpu_particle_sim_t, 0);
    ecs_vec_init_t(NULL, &s->sorts, box_gpu_particle_sort_t, 0);
    ecs_vec_init_t(NULL, &s->draws_of_packet, box_gpu_particle_draw_t, 0);
    ecs_vec_init_t(NULL, &s->groups_of_packet, box_gpu_particle_group_t, 0);
    ecs_vec_init_t(NULL, &s->groups_of_pass, box_gpu_particle_group_t, 0);
    ecs_vec_init_t(NULL, &s->order, box_gpu_particle_order_t, 0);
    gpu->particles = s;
}

/** Find a shared group that binds the same resources as a key.
 *
 * @param groups The groups, box_gpu_particle_group_t
 * @param key The resources
 * @return The group, NULL when there is none
 */
static WGPUBindGroup box_gpu_particle_group_find(
    const ecs_vec_t *groups,
    const box_gpu_particle_group_t *key)
{
    const box_gpu_particle_group_t *g = ecs_vec_first_t(groups, box_gpu_particle_group_t);
    WGPUBindGroup result = NULL;
    int32_t i, count = ecs_vec_count(groups);
    for (i = count - 1; i >= 0 && !result; i --) {
        if (g[i].page == key->page && g[i].keys == key->keys && g[i].table == key->table &&
            g[i].texture == key->texture && g[i].curves == key->curves)
        {
            result = g[i].group;
        }
    }

    return result;
}

/** Keep a new shared group with the resources of its key.
 *
 * @param groups The groups, box_gpu_particle_group_t
 * @param key The resources
 * @param group The group, released with the groups
 * @return The group
 */
static WGPUBindGroup box_gpu_particle_group_add(
    ecs_vec_t *groups,
    const box_gpu_particle_group_t *key,
    WGPUBindGroup group)
{
    box_gpu_particle_group_t *g = ecs_vec_append_t(NULL, groups, box_gpu_particle_group_t);
    *g = *key;
    g->group = group;

    return group;
}

/** Release the shared groups.
 *
 * @param groups The groups, box_gpu_particle_group_t
 */
static void box_gpu_particle_groups_clear(
    ecs_vec_t *groups)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(groups); i ++) {
        wgpuBindGroupRelease(ecs_vec_get_t(groups, box_gpu_particle_group_t, i)->group);
    }

    ecs_vec_clear(groups);
}

/** Release the draw groups of the pools of the last packet.
 *
 * @param s The particle state
 */
static void box_gpu_particle_draws_clear(
    box_gpu_particles_t *s)
{
    box_gpu_particle_groups_clear(&s->groups_of_packet);
    ecs_vec_clear(&s->draws_of_packet);
}

/** Release the draw pipelines, which are created again with the current draw shader.
 *
 * @param s The particle state
 */
static void box_gpu_particle_draw_reset(
    box_gpu_particles_t *s)
{
    int32_t i;
    for (i = 0; i < s->draw_count; i ++) {
        wgpuRenderPipelineRelease(s->draws[i].pipeline);
    }

    s->draw_count = 0;
}

/** Release a webgpu object when it exists.
 *
 * @param obj The object
 * @param release The release function of its type
 */
#define BOX_GPU_PARTICLE_RELEASE(obj, release) \
    if (obj) { \
        release(obj); \
        obj = NULL; \
    }

/** Release the compute pipelines, which are created again with the current simulation shader.
 *
 * @param s The particle state
 */
static void box_gpu_particle_sim_reset(
    box_gpu_particles_t *s)
{
    BOX_GPU_PARTICLE_RELEASE(s->integrate, wgpuComputePipelineRelease);
    BOX_GPU_PARTICLE_RELEASE(s->spawn, wgpuComputePipelineRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sort_keys, wgpuComputePipelineRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sort_local, wgpuComputePipelineRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sort_global, wgpuComputePipelineRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sort_merge, wgpuComputePipelineRelease);
}

void box_gpu_particles_fini(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = gpu->particles;
    if (!s) {
        goto done;
    }

    box_gpu_particle_draws_clear(s);
    box_gpu_particle_draw_reset(s);
    box_gpu_particle_sim_reset(s);
    BOX_GPU_PARTICLE_RELEASE(s->sim_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sim_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sort_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sort_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->draw_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->draw_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_depth_group, wgpuBindGroupRelease);
    BOX_GPU_PARTICLE_RELEASE(s->depth_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_depth, wgpuTextureRelease);
    BOX_GPU_PARTICLE_RELEASE(s->blocks, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->slots, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->args, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->keys, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->steps, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->work, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->batches, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_table, wgpuBufferRelease);
    ecs_vec_fini_t(NULL, &s->block_data, uint8_t);
    ecs_vec_fini_t(NULL, &s->slot_data, uint32_t);
    ecs_vec_fini_t(NULL, &s->work_data, uint32_t);
    ecs_vec_fini_t(NULL, &s->batch_data, uint8_t);
    ecs_vec_fini_t(NULL, &s->refs, box_gpu_particle_ref_t);
    ecs_vec_fini_t(NULL, &s->sims, box_gpu_particle_sim_t);
    ecs_vec_fini_t(NULL, &s->sorts, box_gpu_particle_sort_t);
    ecs_vec_fini_t(NULL, &s->draws_of_packet, box_gpu_particle_draw_t);
    ecs_vec_fini_t(NULL, &s->groups_of_packet, box_gpu_particle_group_t);
    ecs_vec_fini_t(NULL, &s->groups_of_pass, box_gpu_particle_group_t);
    ecs_vec_fini_t(NULL, &s->order, box_gpu_particle_order_t);
    ecs_os_free(s);
    gpu->particles = NULL;
done:
    return;
}

/** Create a compute pipeline of an entry point of the simulation shader.
 *
 * @param gpu The backend
 * @param layout The pipeline layout
 * @param module The module
 * @param entry The entry point
 * @return The pipeline
 */
static WGPUComputePipeline box_gpu_particle_compute(
    box_gpu_t *gpu,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    const char *entry)
{
    WGPUComputePipelineDescriptor desc = {
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    };

    return box_gpu_compute_pipeline(gpu, &desc);
}

/** Make the compute pipelines match the simulation shader; the previous ones stay until every
 * new one exists.
 *
 * @param gpu The backend
 * @return Whether they exist, false while the shader has no module or the first ones are created
 */
static bool box_gpu_particle_sim_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = gpu->particles;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_PARTICLE_SIM);
    if (!shader || !shader->module || (s->integrate && s->sim_revision == shader->revision)) {
        goto done;
    }

    WGPUComputePipeline *current[6] = {
        &s->integrate, &s->spawn, &s->sort_keys, &s->sort_local, &s->sort_global, &s->sort_merge
    };
    WGPUComputePipeline next[6] = {
        box_gpu_particle_compute(gpu, s->sim_pipeline_layout, shader->module, "integrate"),
        box_gpu_particle_compute(gpu, s->sim_pipeline_layout, shader->module, "spawn"),
        box_gpu_particle_compute(gpu, s->sort_pipeline_layout, shader->module, "sort_keys"),
        box_gpu_particle_compute(gpu, s->sort_pipeline_layout, shader->module, "sort_local"),
        box_gpu_particle_compute(gpu, s->sort_pipeline_layout, shader->module, "sort_global"),
        box_gpu_particle_compute(gpu, s->sort_pipeline_layout, shader->module, "sort_merge")
    };
    if (box_gpu_compute_pipelines_swap(current, next, 6)) {
        s->sim_revision = shader->revision;
    }

done:
    return shader && shader->module && s->integrate;
}

/** Create the draw pipeline of a color format and a blend.
 *
 * Alpha blends the color by its opacity over the image; additive adds the color the shader
 * premultiplied by its opacity and keeps the alpha of the image.
 *
 * @param gpu The backend
 * @param module The module of the draw shader
 * @param format The format of the color target
 * @param blend_mode The ParticleBlend
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_particle_draw_create(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    WGPUTextureFormat format,
    uint32_t blend_mode)
{
    WGPUBlendState blend = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_SrcAlpha,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
        },
        .alpha = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
        }
    };
    if (blend_mode == ParticleAdditive) {
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_One;
        blend.alpha.srcFactor = WGPUBlendFactor_Zero;
        blend.alpha.dstFactor = WGPUBlendFactor_One;
    }

    WGPUColorTargetState target = {
        .format = format,
        .blend = &blend,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_False,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = gpu->particles->draw_pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };

    return box_gpu_render_pipeline(gpu, &desc);
}

/** Get the draw pipeline of a color format and a blend, created on first use and again after
 * the draw shader recompiled.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @param blend The ParticleBlend
 * @return The pipeline, NULL without a draw shader or when too many formats are in use
 */
static WGPURenderPipeline box_gpu_particle_draw_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    uint32_t blend)
{
    box_gpu_particles_t *s = gpu->particles;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_PARTICLE_DRAW);
    WGPURenderPipeline result = NULL;
    int32_t i;
    if (!shader || !shader->module) {
        goto done;
    }

    if (s->draw_revision != shader->revision) {
        box_gpu_particle_draw_reset(s);
        s->draw_revision = shader->revision;
    }

    for (i = 0; i < s->draw_count; i ++) {
        if (s->draws[i].format == format && s->draws[i].blend == blend) {
            result = s->draws[i].pipeline;
            goto done;
        }
    }

    if (s->draw_count < BOX_GPU_PARTICLE_DRAWS) {
        result = box_gpu_particle_draw_create(gpu, shader->module, format, blend);
    }

    if (result) {
        s->draws[s->draw_count].format = format;
        s->draws[s->draw_count].blend = blend;
        s->draws[s->draw_count].pipeline = result;
        s->draw_count ++;
    }

done:
    return result;
}

/** Copy the blocks and spawn slots of the pools of a packet into the frame buffers, giving
 * each block its first spawn slot, its draw arguments and its first sort key.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param count Pools of the packet
 */
static void box_gpu_particle_upload(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t count)
{
    box_gpu_particles_t *s = gpu->particles;
    uint64_t keys = 0;
    int32_t i;
    ecs_vec_set_count_t(NULL, &s->block_data, uint8_t,
        count * (int32_t)BOX_GPU_PARTICLE_STRIDE);
    ecs_os_memset(ecs_vec_first(&s->block_data), 0, count * (int32_t)BOX_GPU_PARTICLE_STRIDE);
    ecs_vec_clear(&s->slot_data);
    for (i = 0; i < count; i ++) {
        const box_render_particle_pool_t *pool = ecs_vec_get_t(
            &packet->particles, box_render_particle_pool_t, i);
        box_render_particle_block_t *b = (box_render_particle_block_t*)ecs_vec_get_t(
            &s->block_data, uint8_t, i * (int32_t)BOX_GPU_PARTICLE_STRIDE);
        *b = pool->block;
        b->base = pool->offset / BOX_RENDER_PARTICLE_SIZE;
        b->slot_first = (uint32_t)ecs_vec_count(&s->slot_data);
        b->args = (uint32_t)i;
        b->sort_first = (uint32_t)keys;
        if (!pool->table || !ecs_map_get(&gpu->buffers, pool->table)) {
            b->points = 0;
        }

        if (b->look == ParticleBillboard &&
            (!pool->mesh || !ecs_map_get(&gpu->buffers, pool->mesh)))
        {
            b->look = ParticleCircle;
            b->corners = 6;
        }

        if (b->sort_count > pool->capacity) {
            b->sort_count = pool->capacity;
        }

        b->sorted = b->sort_count ? 1u : 0u;
        keys += b->sort_count;
        if (b->count) {
            uint32_t *dst = ecs_vec_grow_t(NULL, &s->slot_data, uint32_t, (int32_t)b->count);
            ecs_os_memcpy(dst, box_render_packet_data(packet, pool->slots),
                (ecs_size_t)(b->count * sizeof(uint32_t)));
        }
    }

    if (!ecs_vec_count(&s->slot_data)) {
        *ecs_vec_append_t(NULL, &s->slot_data, uint32_t) = BOX_RENDER_PARTICLE_NO_SLOT;
    }

    uint64_t slot_bytes = (uint64_t)ecs_vec_count(&s->slot_data) * sizeof(uint32_t);
    box_gpu_particle_reserve(gpu, &s->blocks, &s->blocks_size,
        (uint64_t)count * BOX_GPU_PARTICLE_STRIDE,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage);
    box_gpu_particle_reserve(gpu, &s->slots, &s->slots_size, slot_bytes,
        WGPUBufferUsage_Storage);
    box_gpu_particle_reserve(gpu, &s->args, &s->args_size,
        (uint64_t)count * BOX_GPU_PARTICLE_ARGS,
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect);
    box_gpu_particle_reserve(gpu, &s->keys, &s->keys_size,
        (keys ? keys : 1) * 2 * sizeof(uint32_t), WGPUBufferUsage_Storage);
    wgpuQueueWriteBuffer(gpu->queue, s->blocks, 0, ecs_vec_first(&s->block_data),
        (size_t)count * BOX_GPU_PARTICLE_STRIDE);
    wgpuQueueWriteBuffer(gpu->queue, s->slots, 0, ecs_vec_first(&s->slot_data),
        (size_t)slot_bytes);
}

/** Get the curve table a pool reads, NULL when it reads none or the table does not exist.
 *
 * The table is created again when it grows, so the groups that bind it are made per packet.
 *
 * @param gpu The backend
 * @param pool The pool
 * @return The buffer
 */
static const box_gpu_buffer_t* box_gpu_particle_curves(
    box_gpu_t *gpu,
    const box_render_particle_pool_t *pool)
{
    return pool->curves
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, pool->curves)
        : NULL;
}

/** Keep the draw of a pool for the views of its scene, with the bind group of its block, its
 * particle page, its sort keys, its billboard corners and its sprite, shared with the pools that
 * bind the same.
 *
 * @param gpu The backend
 * @param pool The pool
 * @param index Index of the pool in the packet
 * @param page The particle page of the pool
 */
static void box_gpu_particle_keep(
    box_gpu_t *gpu,
    const box_render_particle_pool_t *pool,
    uint32_t index,
    const box_gpu_buffer_t *page)
{
    box_gpu_particles_t *s = gpu->particles;
    const box_render_particle_block_t *b = (const box_render_particle_block_t*)ecs_vec_get_t(
        &s->block_data, uint8_t, (int32_t)(index * BOX_GPU_PARTICLE_STRIDE));
    box_gpu_buffer_t *mesh = b->look == ParticleBillboard
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, pool->mesh)
        : NULL;
    const box_gpu_buffer_t *curves = box_gpu_particle_curves(gpu, pool);
    uint64_t size = (uint64_t)pool->capacity * BOX_RENDER_PARTICLE_SIZE;
    box_gpu_particle_group_t key = {
        .page = page->buffer,
        .keys = b->sorted ? s->keys : s->no_table,
        .table = mesh ? mesh->buffer : s->no_table,
        .texture = box_gpu_texture_color(gpu, b->look == ParticleSprite ? pool->texture : 0),
        .curves = curves ? curves->buffer : s->no_table
    };
    WGPUBindGroup group = box_gpu_particle_group_find(&s->groups_of_packet, &key);
    if (!group) {
        WGPUBindGroupEntry draw[7] = {
            {
                .binding = 0, .buffer = s->blocks, .offset = 0,
                .size = sizeof(box_render_particle_block_t)
            },
            { .binding = 1, .buffer = page->buffer, .offset = 0, .size = page->size },
            {
                .binding = 2, .buffer = key.keys, .offset = 0,
                .size = b->sorted ? s->keys_size : 32
            },
            { .binding = 3, .buffer = key.table, .offset = 0, .size = mesh ? mesh->size : 32 },
            { .binding = 4, .textureView = key.texture },
            { .binding = 5, .sampler = box_gpu_texture_sampler(gpu, WrapClamp, 1) },
            {
                .binding = 6, .buffer = key.curves, .offset = 0,
                .size = curves ? curves->size : 32
            }
        };
        group = box_gpu_particle_group_add(&s->groups_of_packet, &key,
            wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
                .layout = s->draw_layout,
                .entryCount = 7,
                .entries = draw
            }));
    }

    box_gpu_particle_draw_t *d = ecs_vec_append_t(
        NULL, &s->draws_of_packet, box_gpu_particle_draw_t);
    d->scene = pool->scene;
    d->block = index;
    d->page = page->buffer;
    d->page_size = page->size;
    d->offset = pool->offset;
    d->size = size;
    d->sort_count = b->sorted ? b->sort_count : 0;
    d->blend = b->blend;
    d->soft = b->softness > 0;
    d->origin[0] = b->model[3][0];
    d->origin[1] = b->model[3][1];
    d->origin[2] = b->model[3][2];
    d->group = group;
}

/** Check that a pool fits its particle page.
 *
 * @param gpu The backend
 * @param pool The pool
 * @return Its page, NULL when it does not fit
 */
static box_gpu_buffer_t* box_gpu_particle_page(
    box_gpu_t *gpu,
    const box_render_particle_pool_t *pool)
{
    box_gpu_buffer_t *page = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, pool->buffer);
    uint64_t size = (uint64_t)pool->capacity * BOX_RENDER_PARTICLE_SIZE;
    if (!page || pool->offset + size > page->size || pool->block.used > pool->capacity) {
        ecs_err("particle pool of %u slots at %u does not fit buffer %u",
            pool->capacity, pool->offset, pool->buffer);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        page = NULL;
    }

    return page;
}

/** Order pools to batch by their page, then their table or scene, then their curve table.
 *
 * @param a A box_gpu_particle_ref_t
 * @param b A box_gpu_particle_ref_t
 * @return Less than, equal to or more than 0
 */
static int box_gpu_particle_ref_cmp(
    const void *a,
    const void *b)
{
    const box_gpu_particle_ref_t *ra = a, *rb = b;
    int result = (ra->page > rb->page) - (ra->page < rb->page);
    result = result ? result : (ra->table > rb->table) - (ra->table < rb->table);
    result = result ? result : (ra->curves > rb->curves) - (ra->curves < rb->curves);
    result = result ? result : (ra->index > rb->index) - (ra->index < rb->index);

    return result;
}

/** Start a batch: its uniform holds the first work item the workgroups of its dispatch take.
 *
 * @param s The particle state
 * @return Index of the batch
 */
static uint32_t box_gpu_particle_batch(
    box_gpu_particles_t *s)
{
    uint32_t result = (uint32_t)ecs_vec_count(&s->batch_data) / BOX_GPU_PARTICLE_BATCH_STRIDE;
    uint8_t *b = ecs_vec_grow_t(NULL, &s->batch_data, uint8_t,
        (int32_t)BOX_GPU_PARTICLE_BATCH_STRIDE);
    ecs_os_memset(b, 0, (ecs_size_t)BOX_GPU_PARTICLE_BATCH_STRIDE);
    *(uint32_t*)b = (uint32_t)ecs_vec_count(&s->work_data) / 2u;

    return result;
}

/** Add the workgroups of a pool to the batch being built, one per chunk.
 *
 * @param s The particle state
 * @param index Index of the pool in the packet
 * @param chunks Workgroups of the pool
 * @return The workgroups added
 */
static uint32_t box_gpu_particle_work(
    box_gpu_particles_t *s,
    uint32_t index,
    uint32_t chunks)
{
    uint32_t c;
    for (c = 0; c < chunks; c ++) {
        uint32_t *w = ecs_vec_grow_t(NULL, &s->work_data, uint32_t, 2);
        w[0] = index;
        w[1] = c;
    }

    return chunks;
}

/** Group the simulated pools of a packet into batches of the same page, mesh table and curve
 * table: an integrate batch over the used slots and a spawn batch over the spawns of each.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param refs The pools that fit their page, box_gpu_particle_ref_t
 */
static void box_gpu_particle_sims(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_vec_t *refs)
{
    box_gpu_particles_t *s = gpu->particles;
    box_gpu_particle_ref_t *r = ecs_vec_first_t(refs, box_gpu_particle_ref_t);
    int32_t i, j, k, count = ecs_vec_count(refs);
    qsort(r, (size_t)count, sizeof(box_gpu_particle_ref_t), box_gpu_particle_ref_cmp);
    for (i = 0; i < count; i = j) {
        const box_render_particle_pool_t *first = ecs_vec_get_t(
            &packet->particles, box_render_particle_pool_t, (int32_t)r[i].index);
        const box_gpu_buffer_t *page = ecs_map_get_deref(
            &gpu->buffers, box_gpu_buffer_t, first->buffer);
        const box_gpu_buffer_t *table = first->table
            ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, first->table)
            : NULL;
        const box_gpu_buffer_t *curves = box_gpu_particle_curves(gpu, first);
        box_gpu_particle_sim_t *b = ecs_vec_append_t(NULL, &s->sims, box_gpu_particle_sim_t);
        ecs_os_memset_t(b, 0, box_gpu_particle_sim_t);
        b->key.page = page->buffer;
        b->key.table = table ? table->buffer : s->no_table;
        b->key.curves = curves ? curves->buffer : s->no_table;
        b->page_size = page->size;
        b->table_size = table ? table->size : 32;
        b->curves_size = curves ? curves->size : 32;
        for (j = i; j < count && r[j].page == r[i].page && r[j].table == r[i].table &&
            r[j].curves == r[i].curves; j ++)
        {
        }

        b->integrate = box_gpu_particle_batch(s);
        for (k = i; k < j; k ++) {
            const box_render_particle_pool_t *pool = ecs_vec_get_t(
                &packet->particles, box_render_particle_pool_t, (int32_t)r[k].index);
            b->integrate_groups += box_gpu_particle_work(s, r[k].index,
                (pool->block.used + BOX_GPU_PARTICLE_GROUP - 1) / BOX_GPU_PARTICLE_GROUP);
        }

        b->spawn = box_gpu_particle_batch(s);
        for (k = i; k < j; k ++) {
            const box_render_particle_pool_t *pool = ecs_vec_get_t(
                &packet->particles, box_render_particle_pool_t, (int32_t)r[k].index);
            b->spawn_groups += box_gpu_particle_work(s, r[k].index,
                (pool->block.count + BOX_GPU_PARTICLE_GROUP - 1) / BOX_GPU_PARTICLE_GROUP);
        }
    }
}

/** Group the sorted pools of a packet into sort batches: the pools of a page and a scene that
 * sort at most a workgroup of keys share one, a pool that sorts more has its own.
 *
 * @param gpu The backend
 * @param refs The sorted pools, box_gpu_particle_ref_t
 */
static void box_gpu_particle_sorts(
    box_gpu_t *gpu,
    ecs_vec_t *refs)
{
    box_gpu_particles_t *s = gpu->particles;
    box_gpu_particle_ref_t *r = ecs_vec_first_t(refs, box_gpu_particle_ref_t);
    int32_t i, j, k, count = ecs_vec_count(refs);
    qsort(r, (size_t)count, sizeof(box_gpu_particle_ref_t), box_gpu_particle_ref_cmp);
    for (i = 0; i < count; i = j) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(
            &s->draws_of_packet, box_gpu_particle_draw_t, (int32_t)r[i].index);
        bool alone = d->sort_count > BOX_RENDER_PARTICLE_SORT_MIN;
        box_gpu_particle_sort_t *b = ecs_vec_append_t(NULL, &s->sorts, box_gpu_particle_sort_t);
        ecs_os_memset_t(b, 0, box_gpu_particle_sort_t);
        b->page = d->page;
        b->page_size = d->page_size;
        b->scene = d->scene;
        b->count = alone ? d->sort_count : 0;
        for (j = i + 1; !alone && j < count && r[j].page == r[i].page &&
            r[j].table == r[i].table && r[j].curves == r[i].curves; j ++)
        {
        }

        j = alone ? i + 1 : j;
        b->keys = box_gpu_particle_batch(s);
        for (k = i; k < j; k ++) {
            const box_gpu_particle_draw_t *e = ecs_vec_get_t(
                &s->draws_of_packet, box_gpu_particle_draw_t, (int32_t)r[k].index);
            b->keys_groups += box_gpu_particle_work(s, e->block,
                (e->sort_count + BOX_GPU_PARTICLE_GROUP - 1) / BOX_GPU_PARTICLE_GROUP);
        }

        b->runs = box_gpu_particle_batch(s);
        for (k = i; k < j; k ++) {
            const box_gpu_particle_draw_t *e = ecs_vec_get_t(
                &s->draws_of_packet, box_gpu_particle_draw_t, (int32_t)r[k].index);
            b->runs_groups += box_gpu_particle_work(s, e->block,
                alone ? e->sort_count / BOX_RENDER_PARTICLE_SORT_MIN : 1u);
        }
    }
}

/** Batch the pools of a packet and upload the work items and batch uniforms of the batches:
 * the pools that fit their page keep their draw, then are grouped into the simulation batches,
 * and the sorted ones into the sort batches every view that draws them runs.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param count Pools of the packet
 */
static void box_gpu_particle_batches(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t count)
{
    box_gpu_particles_t *s = gpu->particles;
    int32_t i;
    ecs_vec_clear(&s->work_data);
    ecs_vec_clear(&s->batch_data);
    ecs_vec_clear(&s->sims);
    ecs_vec_clear(&s->sorts);
    ecs_vec_clear(&s->refs);
    for (i = 0; i < count; i ++) {
        const box_render_particle_pool_t *pool = ecs_vec_get_t(
            &packet->particles, box_render_particle_pool_t, i);
        box_gpu_buffer_t *page = box_gpu_particle_page(gpu, pool);
        if (page) {
            const box_gpu_buffer_t *table = pool->table
                ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, pool->table)
                : NULL;
            const box_gpu_buffer_t *curves = box_gpu_particle_curves(gpu, pool);
            box_gpu_particle_ref_t *r = ecs_vec_append_t(NULL, &s->refs, box_gpu_particle_ref_t);
            r->page = (uintptr_t)page->buffer;
            r->table = (uintptr_t)(table ? table->buffer : s->no_table);
            r->curves = (uintptr_t)(curves ? curves->buffer : s->no_table);
            r->index = (uint32_t)i;
            box_gpu_particle_keep(gpu, pool, (uint32_t)i, page);
        }
    }

    box_gpu_particle_sims(gpu, packet, &s->refs);
    ecs_vec_clear(&s->refs);
    for (i = 0; i < ecs_vec_count(&s->draws_of_packet); i ++) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(
            &s->draws_of_packet, box_gpu_particle_draw_t, i);
        if (d->sort_count) {
            box_gpu_particle_ref_t *r = ecs_vec_append_t(NULL, &s->refs, box_gpu_particle_ref_t);
            r->page = (uintptr_t)d->page;
            r->table = (uintptr_t)d->scene;
            r->curves = d->sort_count > BOX_RENDER_PARTICLE_SORT_MIN ? 1u : 0u;
            r->index = (uint32_t)i;
        }
    }

    box_gpu_particle_sorts(gpu, &s->refs);
    if (!ecs_vec_count(&s->work_data)) {
        uint32_t *w = ecs_vec_grow_t(NULL, &s->work_data, uint32_t, 2);
        w[0] = w[1] = 0;
    }

    if (!ecs_vec_count(&s->batch_data)) {
        box_gpu_particle_batch(s);
    }

    uint64_t work_bytes = (uint64_t)ecs_vec_count(&s->work_data) * sizeof(uint32_t);
    uint64_t batch_bytes = (uint64_t)ecs_vec_count(&s->batch_data);
    box_gpu_particle_reserve(gpu, &s->work, &s->work_size, work_bytes, WGPUBufferUsage_Storage);
    box_gpu_particle_reserve(gpu, &s->batches, &s->batches_size, batch_bytes,
        WGPUBufferUsage_Uniform);
    wgpuQueueWriteBuffer(gpu->queue, s->work, 0, ecs_vec_first(&s->work_data),
        (size_t)work_bytes);
    wgpuQueueWriteBuffer(gpu->queue, s->batches, 0, ecs_vec_first(&s->batch_data),
        (size_t)batch_bytes);
}

/** Record the batched integrate and spawn dispatches of the simulation batches of a packet.
 *
 * @param gpu The backend
 * @param pass The compute pass
 */
static void box_gpu_particle_dispatch(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass)
{
    box_gpu_particles_t *s = gpu->particles;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->sims); i ++) {
        const box_gpu_particle_sim_t *b = ecs_vec_get_t(&s->sims, box_gpu_particle_sim_t, i);
        WGPUBindGroupEntry entries[8] = {
            { .binding = 0, .buffer = s->blocks, .offset = 0, .size = s->blocks_size },
            { .binding = 1, .buffer = b->key.page, .offset = 0, .size = b->page_size },
            { .binding = 2, .buffer = s->slots, .offset = 0, .size = s->slots_size },
            { .binding = 3, .buffer = b->key.table, .offset = 0, .size = b->table_size },
            { .binding = 4, .buffer = s->args, .offset = 0, .size = s->args_size },
            { .binding = 8, .buffer = b->key.curves, .offset = 0, .size = b->curves_size },
            { .binding = 9, .buffer = s->work, .offset = 0, .size = s->work_size },
            { .binding = 10, .buffer = s->batches, .offset = 0, .size = 16 }
        };
        WGPUBindGroup group = box_gpu_particle_group_add(&s->groups_of_pass, &b->key,
            wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
                .layout = s->sim_layout,
                .entryCount = 8,
                .entries = entries
            }));
        uint32_t offset = b->integrate * BOX_GPU_PARTICLE_BATCH_STRIDE;
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        wgpuComputePassEncoderSetPipeline(pass, s->integrate);
        wgpuComputePassEncoderDispatchWorkgroups(pass, b->integrate_groups, 1, 1);
        if (b->spawn_groups) {
            offset = b->spawn * BOX_GPU_PARTICLE_BATCH_STRIDE;
            wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
            wgpuComputePassEncoderSetPipeline(pass, s->spawn);
            wgpuComputePassEncoderDispatchWorkgroups(pass, b->spawn_groups, 1, 1);
        }
    }
}

void box_gpu_particles_simulate(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_particles_t *s = gpu->particles;
    int32_t i, count = ecs_vec_count(&packet->particles);
    if (!s) {
        goto done;
    }

    box_gpu_particle_draws_clear(s);
    ecs_vec_clear(&s->sorts);
    if (!count) {
        goto done;
    }

    if (!box_gpu_particle_sim_pipelines(gpu)) {
        ecs_err("particle shader is not available, %d pools skipped", count);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    box_gpu_particle_upload(gpu, packet, count);
    box_gpu_particle_batches(gpu, packet, count);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (i = 0; i < count; i ++) {
        const box_render_particle_pool_t *pool = ecs_vec_get_t(
            &packet->particles, box_render_particle_pool_t, i);
        box_gpu_buffer_t *page = ecs_map_get_deref(
            &gpu->buffers, box_gpu_buffer_t, pool->buffer);
        uint64_t size = (uint64_t)pool->capacity * BOX_RENDER_PARTICLE_SIZE;
        if (pool->clear && page && pool->offset + size <= page->size) {
            wgpuCommandEncoderClearBuffer(encoder, page->buffer, pool->offset, size);
        }
    }

    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    box_gpu_particle_dispatch(gpu, pass);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    box_gpu_particle_groups_clear(&s->groups_of_pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
}

/** Set the dynamic offsets of a sort group for a step and a batch and bind it.
 *
 * @param pass The compute pass
 * @param group The sort group of the page
 * @param offsets Offset of the view uniforms, receives the step and batch offsets
 * @param step Index of the step in the step table
 * @param batch The batch
 */
static void box_gpu_particle_sort_bind(
    WGPUComputePassEncoder pass,
    WGPUBindGroup group,
    uint32_t *offsets,
    uint32_t step,
    uint32_t batch)
{
    offsets[1] = step * BOX_GPU_PARTICLE_STEP_STRIDE;
    offsets[2] = batch * BOX_GPU_PARTICLE_BATCH_STRIDE;
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 3, offsets);
}

/** Record the sort of the keys of a sort batch by the view depth of its particles in a view:
 * the keys, the sort of every workgroup run, then for a pool that sorts more than a workgroup
 * the long steps and the merge of the short ones per longer run length.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param b The batch
 * @param slot Slot of the view in the view buffer
 */
static void box_gpu_particle_sort_batch(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    const box_gpu_particle_sort_t *b,
    int32_t slot)
{
    box_gpu_particles_t *s = gpu->particles;
    uint32_t n = b->count, m, i;
    uint32_t offsets[3] = { (uint32_t)slot * BOX_GPU_VIEW_STRIDE, 0, 0 };
    box_gpu_particle_group_t key = {
        .page = b->page, .keys = s->keys, .table = s->no_table, .curves = s->no_table
    };
    WGPUBindGroup group = box_gpu_particle_group_find(&s->groups_of_pass, &key);
    if (!group) {
        WGPUBindGroupEntry entries[7] = {
            { .binding = 0, .buffer = s->blocks, .offset = 0, .size = s->blocks_size },
            { .binding = 1, .buffer = b->page, .offset = 0, .size = b->page_size },
            { .binding = 5, .buffer = s->keys, .offset = 0, .size = s->keys_size },
            {
                .binding = 6, .buffer = gpu->view_buffer, .offset = 0,
                .size = sizeof(box_gpu_view_uniforms_t)
            },
            { .binding = 7, .buffer = s->steps, .offset = 0, .size = 16 },
            { .binding = 9, .buffer = s->work, .offset = 0, .size = s->work_size },
            { .binding = 10, .buffer = s->batches, .offset = 0, .size = 16 }
        };
        group = box_gpu_particle_group_add(&s->groups_of_pass, &key,
            wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
                .layout = s->sort_layout,
                .entryCount = 7,
                .entries = entries
            }));
    }

    box_gpu_particle_sort_bind(pass, group, offsets, 0, b->keys);
    wgpuComputePassEncoderSetPipeline(pass, s->sort_keys);
    wgpuComputePassEncoderDispatchWorkgroups(pass, b->keys_groups, 1, 1);
    box_gpu_particle_sort_bind(pass, group, offsets, 0, b->runs);
    wgpuComputePassEncoderSetPipeline(pass, s->sort_local);
    wgpuComputePassEncoderDispatchWorkgroups(pass, b->runs_groups, 1, 1);
    for (m = BOX_GPU_PARTICLE_SORT_GROUP_LOG + 1; n && (1u << m) <= n; m ++) {
        wgpuComputePassEncoderSetPipeline(pass, s->sort_global);
        for (i = m - 1; i >= BOX_GPU_PARTICLE_SORT_GROUP_LOG; i --) {
            box_gpu_particle_sort_bind(pass, group, offsets, m * (m - 1) / 2 + i, b->runs);
            wgpuComputePassEncoderDispatchWorkgroups(pass, b->runs_groups, 1, 1);
        }

        box_gpu_particle_sort_bind(pass, group, offsets, m * (m - 1) / 2, b->runs);
        wgpuComputePassEncoderSetPipeline(pass, s->sort_merge);
        wgpuComputePassEncoderDispatchWorkgroups(pass, b->runs_groups, 1, 1);
    }
}

void box_gpu_particles_sort(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v,
    int32_t slot)
{
    box_gpu_particles_t *s = gpu->particles;
    WGPUComputePassEncoder pass = NULL;
    int32_t i;
    for (i = 0; s && s->sort_keys && gpu->view_buffer && i < ecs_vec_count(&s->sorts); i ++) {
        const box_gpu_particle_sort_t *b = ecs_vec_get_t(&s->sorts, box_gpu_particle_sort_t, i);
        if (b->scene != v->scene) {
            continue;
        }

        if (!pass) {
            pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        }

        box_gpu_particle_sort_batch(gpu, pass, b, slot);
    }

    if (pass) {
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

    if (s) {
        box_gpu_particle_groups_clear(&s->groups_of_pass);
    }
}

bool box_gpu_particles_soft(
    const box_gpu_t *gpu,
    const box_render_view_t *v)
{
    const box_gpu_particles_t *s = gpu->particles;
    bool result = false;
    int32_t i;
    for (i = 0; s && i < ecs_vec_count(&s->draws_of_packet); i ++) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(
            &s->draws_of_packet, box_gpu_particle_draw_t, i);
        if (d->scene == v->scene && d->soft) {
            result = true;
            break;
        }
    }

    return result;
}

/** Compare two draws of a view: the emitter farthest from the camera first, then packet order.
 *
 * @param a The first draw
 * @param b The second draw
 * @return Negative when a draws first
 */
static int box_gpu_particle_order_cmp(
    const void *a,
    const void *b)
{
    const box_gpu_particle_order_t *x = a, *y = b;
    int result = x->index < y->index ? -1 : x->index > y->index;
    if (x->depth != y->depth) {
        result = x->depth > y->depth ? -1 : 1;
    }

    return result;
}

/** Collect the draws of the scene of a view, emitters back to front.
 *
 * @param s The particle state
 * @param v The view
 * @return Draws collected in s->order
 */
static int32_t box_gpu_particle_order(
    box_gpu_particles_t *s,
    const box_render_view_t *v)
{
    const float *eye = v->camera_transform[3], *back = v->camera_transform[2];
    int32_t i;
    ecs_vec_clear(&s->order);
    for (i = 0; i < ecs_vec_count(&s->draws_of_packet); i ++) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(
            &s->draws_of_packet, box_gpu_particle_draw_t, i);
        if (d->scene != v->scene) {
            continue;
        }

        box_gpu_particle_order_t *o = ecs_vec_append_t(
            NULL, &s->order, box_gpu_particle_order_t);
        o->index = i;
        o->depth = -((d->origin[0] - eye[0]) * back[0] + (d->origin[1] - eye[1]) * back[1] +
            (d->origin[2] - eye[2]) * back[2]);
    }

    if (ecs_vec_count(&s->order) > 1) {
        qsort(ecs_vec_first(&s->order), (size_t)ecs_vec_count(&s->order),
            sizeof(box_gpu_particle_order_t), box_gpu_particle_order_cmp);
    }

    return ecs_vec_count(&s->order);
}

bool box_gpu_particles_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    WGPUTextureView depth)
{
    box_gpu_particles_t *s = gpu->particles;
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup depth_group = NULL;
    bool result = false;
    int32_t i, count = s ? box_gpu_particle_order(s, v) : 0;
    for (i = 0; i < count; i ++) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(&s->draws_of_packet,
            box_gpu_particle_draw_t, ecs_vec_get_t(&s->order, box_gpu_particle_order_t, i)->index);
        uint32_t offset = d->block * BOX_GPU_PARTICLE_STRIDE;
        WGPURenderPipeline pipeline = box_gpu_particle_draw_pipeline(gpu, format, d->blend);
        if (!pipeline) {
            break;
        }

        if (!depth_group) {
            WGPUBindGroupEntry entry = { .binding = 0, .textureView = depth };
            depth_group = depth
                ? wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
                    .layout = s->depth_layout,
                    .entryCount = 1,
                    .entries = &entry
                })
                : s->no_depth_group;
            wgpuRenderPassEncoderSetBindGroup(pass, 2, depth_group, 0, NULL);
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound = pipeline;
        }

        wgpuRenderPassEncoderSetBindGroup(pass, 1, d->group, 1, &offset);
        wgpuRenderPassEncoderDrawIndirect(pass, s->args,
            (uint64_t)d->block * BOX_GPU_PARTICLE_ARGS);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        result = true;
    }

    if (depth_group && depth_group != s->no_depth_group) {
        wgpuBindGroupRelease(depth_group);
    }

    return result;
}

#endif
