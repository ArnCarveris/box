/**
 * @file render/gpu_particle.c
 * @brief Particle passes of the webgpu backend: one integrate and one spawn dispatch over every
 * pool of a packet, before its targets, and one instanced draw per blend and sprite of the pools
 * of a view's scene.
 *
 * Pools are ranges of the particle arena and the blocks of a packet are read by pool index, so
 * every pass binds the same buffers and the bind groups live until a buffer they bind changes.
 *
 * A draw walks chunks of BOX_GPU_PARTICLE_CHUNK slots of its pools that the view writes into the
 * chunk buffer, so the CPU never reads particle data back.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Invocations of a workgroup of the particle compute shader */
#define BOX_GPU_PARTICLE_GROUP (256u)

/** Bytes between the blocks of two pools */
#define BOX_GPU_PARTICLE_STRIDE (544u)

_Static_assert(sizeof(box_render_particle_block_t) <= BOX_GPU_PARTICLE_STRIDE,
    "a particle block must fit in its stride");

/** Slots of a pool one entry of the chunk buffer draws, as CHUNK in particle_draw.wgsl */
#define BOX_GPU_PARTICLE_CHUNK (64u)

/** Storage buffers the vertex stage of the draw reads: blocks, particles, chunks, the curve
 * table and the reflection probes of the view group */
#define BOX_GPU_PARTICLE_VERTEX_STORAGE (5u)

/** Storage buffers of the integrate and spawn passes: blocks, particles, spawn slots, mesh
 * tables, the curve table, the work items and the grid and heights of the sampled terrain */
#define BOX_GPU_PARTICLE_SIM_STORAGE (8u)

/** Bytes between the batch uniforms of two dispatches, the dynamic offset alignment */
#define BOX_GPU_PARTICLE_BATCH_STRIDE (256u)

/** Workgroups of one dispatch at most, the default maxComputeWorkgroupsPerDimension */
#define BOX_GPU_PARTICLE_DISPATCH_MAX (65535u)

/** Draw pipelines, one per color format and ParticleBlend */
#define BOX_GPU_PARTICLE_DRAWS (BOX_GPU_BLIT_FORMATS * 2)

/** The draw of a pool in the views of its scene, valid for the packet that simulated it */
typedef struct box_gpu_particle_draw_t {
    ecs_entity_t scene;             /**< Scene root of the pool */
    uint32_t block;                 /**< Index of its block */
    uint32_t used;                  /**< Slots that may hold a particle */
    uint32_t blend;                 /**< ParticleBlend of the pipeline */
    WGPUTextureView texture;        /**< Sprite it reads, white for none */
    bool soft;                      /**< It fades in front of the depth of the view */
    float origin[3];                /**< World position of the emitter */
    ecs_entity_t terrain;           /**< Sampled terrain its passes bind, 0 for none */
} box_gpu_particle_draw_t;

/** A dispatch of the simulation: a batch of work items of one entry point */
typedef struct box_gpu_particle_dispatch_t {
    uint32_t batch;                 /**< Batch uniform of its first work item */
    uint32_t groups;                /**< Workgroups */
    bool spawn;                     /**< The spawn entry point, else integrate */
    ecs_entity_t terrain;           /**< Sampled terrain of its pools, 0 for none */
} box_gpu_particle_dispatch_t;

/** The draw group of a sprite, kept until a buffer it binds is replaced */
typedef struct box_gpu_particle_group_t {
    WGPUTextureView texture;        /**< Sprite it binds */
    WGPUBindGroup group;            /**< The group */
} box_gpu_particle_group_t;

/** A draw pipeline of a color format and a blend */
typedef struct box_gpu_particle_pipeline_t {
    WGPUTextureFormat format;       /**< Format of the color target */
    uint32_t blend;                 /**< ParticleBlend */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_particle_pipeline_t;

/** A draw of a view in the order it is recorded */
typedef struct box_gpu_particle_order_t {
    uint32_t blend;                 /**< ParticleBlend, alpha first */
    WGPUTextureView texture;        /**< Sprite, one draw per sprite and blend */
    float depth;                    /**< View depth of the emitter, far first */
    int32_t index;                  /**< Index of the draw in the draws of the packet */
} box_gpu_particle_order_t;

struct box_gpu_particles_t {
    WGPUBindGroupLayout sim_layout; /**< Blocks, particles, spawn slots, mesh tables, curves,
                                     *   work items and the batch */
    WGPUPipelineLayout sim_pipeline_layout; /**< Pipeline layout of the compute passes: the
                                     *   simulation and the sampled terrain, created with the
                                     *   first compute pipelines */
    WGPUBuffer no_terrain_params;   /**< Params of no sampled terrain, zero */
    WGPUBuffer no_terrain_grid;     /**< Grid of no sampled terrain */
    WGPUBindGroup no_terrain;       /**< The sampled terrain of pools without one */
    WGPUComputePipeline integrate;  /**< Ages and moves the used slots of the pools */
    WGPUComputePipeline spawn;      /**< Writes the spawned particles into their slots */
    int32_t sim_revision;           /**< Revision of the shader the compute pipelines use */
    WGPUBindGroupLayout draw_layout; /**< Blocks, particles, chunks, sprite, sampler, curves */
    WGPUBindGroupLayout depth_layout; /**< Depth of the view a soft particle reads */
    WGPUPipelineLayout draw_pipeline_layout; /**< View uniforms, the draw, the depth */
    box_gpu_particle_pipeline_t draws[BOX_GPU_PARTICLE_DRAWS]; /**< Draw pipelines */
    int32_t draw_count;             /**< Draw pipelines created */
    int32_t draw_revision;          /**< Revision of the shader the draw pipelines use */
    WGPUBuffer blocks;              /**< box_render_particle_block_t per pool of the packet */
    uint64_t blocks_size;           /**< Bytes of the block buffer */
    WGPUBuffer slots;               /**< Spawn slots of every pool of the packet */
    uint64_t slots_size;            /**< Bytes of the slot buffer */
    WGPUBuffer work;                /**< Pool and chunk of every workgroup of the dispatches of
                                     *   the packet, two uint32_t each */
    uint64_t work_size;             /**< Bytes of the work buffer */
    WGPUBuffer batches;             /**< First work item of every dispatch, a uniform each
                                     *   BOX_GPU_PARTICLE_BATCH_STRIDE apart */
    uint64_t batches_size;          /**< Bytes of the batch buffer */
    WGPUBuffer chunks;              /**< Pool and first slot of the chunks the views of the
                                     *   packet draw, two uint32_t each */
    uint64_t chunks_size;           /**< Bytes of the chunk buffer */
    WGPUBuffer no_table;            /**< Mesh tables and curve table of a packet without */
    WGPUTexture no_depth;           /**< Depth bound while the view writes its depth */
    WGPUBindGroup no_depth_group;   /**< Binds no_depth */
    WGPUBuffer arena;               /**< Particle arena of the packet */
    uint64_t arena_size;            /**< Bytes of the arena */
    WGPUBuffer tables;              /**< Mesh tables of the packet, no_table without */
    uint64_t tables_size;           /**< Bytes of the mesh tables */
    WGPUBuffer curves;              /**< Curve table of the packet, no_table without */
    uint64_t curves_size;           /**< Bytes of the curve table */
    WGPUBindGroup sim_group;        /**< Binds the buffers of the simulation, NULL when one of
                                     *   them was replaced */
    ecs_vec_t groups;               /**< box_gpu_particle_group_t, the draw groups, cleared
                                     *   when a buffer they bind is replaced */
    ecs_vec_t block_data;           /**< Bytes of the blocks of the packet */
    ecs_vec_t slot_data;            /**< uint32_t, the spawn slots of the packet */
    ecs_vec_t work_data;            /**< uint32_t, the work items of the packet */
    ecs_vec_t batch_data;           /**< Bytes of the batch uniforms of the packet */
    ecs_vec_t chunk_data;           /**< uint32_t, the chunks the views of the packet draw */
    ecs_vec_t dispatches;           /**< box_gpu_particle_dispatch_t of the packet */
    ecs_vec_t draws_of_packet;      /**< box_gpu_particle_draw_t of the last packet */
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

/** Forget the bind groups, after a buffer they bind was replaced.
 *
 * @param s The particle state
 */
static void box_gpu_particle_groups_clear(
    box_gpu_particles_t *s)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->groups); i ++) {
        wgpuBindGroupRelease(ecs_vec_get_t(&s->groups, box_gpu_particle_group_t, i)->group);
    }

    ecs_vec_clear(&s->groups);
    if (s->sim_group) {
        wgpuBindGroupRelease(s->sim_group);
        s->sim_group = NULL;
    }
}

/** Make a buffer hold a number of bytes, growing it in powers of two and forgetting the bind
 * groups that bound the buffer it replaces.
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
    uint64_t capacity = *size ? *size : 4096;
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
    box_gpu_particle_groups_clear(gpu->particles);
done:
    return;
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
    WGPUBindGroupLayoutEntry draw[6] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
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
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = draw });
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
    WGPUBindGroupLayoutEntry sim[7] = {
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
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 7, .entries = sim });
    box_gpu_particle_draw_init(gpu, s);
    s->no_table = box_gpu_particle_buffer(gpu, 32, WGPUBufferUsage_Storage);
    ecs_vec_init_t(NULL, &s->groups, box_gpu_particle_group_t, 0);
    ecs_vec_init_t(NULL, &s->block_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &s->slot_data, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->work_data, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->batch_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &s->chunk_data, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->dispatches, box_gpu_particle_dispatch_t, 0);
    ecs_vec_init_t(NULL, &s->draws_of_packet, box_gpu_particle_draw_t, 0);
    ecs_vec_init_t(NULL, &s->order, box_gpu_particle_order_t, 0);
    gpu->particles = s;
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
}

void box_gpu_particles_fini(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = gpu->particles;
    if (!s) {
        goto done;
    }

    box_gpu_particle_groups_clear(s);
    box_gpu_particle_draw_reset(s);
    box_gpu_particle_sim_reset(s);
    BOX_GPU_PARTICLE_RELEASE(s->sim_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sim_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_terrain, wgpuBindGroupRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_terrain_params, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_terrain_grid, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->draw_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->draw_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_depth_group, wgpuBindGroupRelease);
    BOX_GPU_PARTICLE_RELEASE(s->depth_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_depth, wgpuTextureRelease);
    BOX_GPU_PARTICLE_RELEASE(s->blocks, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->slots, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->work, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->batches, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->chunks, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_table, wgpuBufferRelease);
    ecs_vec_fini_t(NULL, &s->groups, box_gpu_particle_group_t);
    ecs_vec_fini_t(NULL, &s->block_data, uint8_t);
    ecs_vec_fini_t(NULL, &s->slot_data, uint32_t);
    ecs_vec_fini_t(NULL, &s->work_data, uint32_t);
    ecs_vec_fini_t(NULL, &s->batch_data, uint8_t);
    ecs_vec_fini_t(NULL, &s->chunk_data, uint32_t);
    ecs_vec_fini_t(NULL, &s->dispatches, box_gpu_particle_dispatch_t);
    ecs_vec_fini_t(NULL, &s->draws_of_packet, box_gpu_particle_draw_t);
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

/** Create the pipeline layout of the compute passes and the group of no sampled terrain, after
 * the layout of the sampled terrains exists.
 *
 * @param gpu The backend
 */
static void box_gpu_particle_sim_layout(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = gpu->particles;
    if (s->sim_pipeline_layout) {
        goto done;
    }

    WGPUBindGroupLayout layouts[2] = { s->sim_layout, gpu->terrain_sample_layout };
    s->sim_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = layouts
        });
    s->no_terrain_params = box_gpu_particle_buffer(gpu, sizeof(box_gpu_terrain_sample_params_t),
        WGPUBufferUsage_Uniform);
    s->no_terrain_grid = box_gpu_particle_buffer(gpu, 16, WGPUBufferUsage_Storage);
    WGPUBindGroupEntry entries[4] = {
        {
            .binding = 0,
            .buffer = s->no_terrain_params,
            .size = sizeof(box_gpu_terrain_sample_params_t)
        },
        { .binding = 1, .buffer = s->no_terrain_grid, .size = 16 },
        { .binding = 2, .buffer = gpu->no_assets, .size = WGPU_WHOLE_SIZE },
        { .binding = 3, .textureView = gpu->white->linear }
    };
    s->no_terrain = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->terrain_sample_layout,
        .entryCount = 4,
        .entries = entries
    });
done:
    return;
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

    box_gpu_particle_sim_layout(gpu);
    WGPUComputePipeline *current[2] = { &s->integrate, &s->spawn };
    WGPUComputePipeline next[2] = {
        box_gpu_particle_compute(gpu, s->sim_pipeline_layout, shader->module, "integrate"),
        box_gpu_particle_compute(gpu, s->sim_pipeline_layout, shader->module, "spawn")
    };
    if (box_gpu_compute_pipelines_swap(current, next, 2)) {
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

/** Get a buffer of the packet that the passes bind, forgetting the bind groups when it is not
 * the one they bound.
 *
 * @param gpu The backend
 * @param handle Handle of the buffer
 * @param buffer The buffer the groups bound, replaced
 * @param size Its bytes, replaced
 */
static void box_gpu_particle_bound(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUBuffer *buffer,
    uint64_t *size)
{
    box_gpu_particles_t *s = gpu->particles;
    const box_gpu_buffer_t *b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, handle);
    WGPUBuffer next = b ? b->buffer : (handle == BOX_RENDER_BUFFER_PARTICLE_FIRST
        ? NULL : s->no_table);
    uint64_t next_size = b ? b->size : 32;
    if (next != *buffer || next_size != *size) {
        box_gpu_particle_groups_clear(s);
        *buffer = next;
        *size = next_size;
    }
}

/** Forget the bind groups when a packet creates or destroys a texture, whose views a draw group
 * may bind.
 *
 * @param gpu The backend
 * @param packet The packet
 */
static void box_gpu_particle_textures_changed(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&packet->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &packet->resources, box_render_resource_t, i);
        if ((r->kind == BOX_RENDER_TEXTURE || r->kind == BOX_RENDER_BUFFER) &&
            r->op != BOX_RENDER_UPDATE)
        {
            box_gpu_particle_groups_clear(gpu->particles);
            break;
        }
    }
}

/** Add the work items of a pool, one per chunk of BOX_GPU_PARTICLE_GROUP.
 *
 * @param s The particle state
 * @param index Index of the pool in the packet
 * @param count Slots or spawns of the pool
 * @return The work items added
 */
static uint32_t box_gpu_particle_work(
    box_gpu_particles_t *s,
    uint32_t index,
    uint32_t count)
{
    uint32_t c, chunks = (count + BOX_GPU_PARTICLE_GROUP - 1) / BOX_GPU_PARTICLE_GROUP;
    for (c = 0; c < chunks; c ++) {
        uint32_t *w = ecs_vec_grow_t(NULL, &s->work_data, uint32_t, 2);
        w[0] = index;
        w[1] = c;
    }

    return chunks;
}

/** Split the work items from a first one into dispatches of at most
 * BOX_GPU_PARTICLE_DISPATCH_MAX workgroups, each with the batch uniform of its first item.
 *
 * @param s The particle state
 * @param first The first work item
 * @param spawn Whether they run the spawn entry point
 * @param terrain Sampled terrain of their pools, 0 for none
 */
static void box_gpu_particle_dispatches(
    box_gpu_particles_t *s,
    uint32_t first,
    bool spawn,
    ecs_entity_t terrain)
{
    uint32_t end = (uint32_t)ecs_vec_count(&s->work_data) / 2u;
    while (first < end) {
        uint32_t groups = end - first;
        groups = groups < BOX_GPU_PARTICLE_DISPATCH_MAX ? groups : BOX_GPU_PARTICLE_DISPATCH_MAX;
        box_gpu_particle_dispatch_t *d = ecs_vec_append_t(
            NULL, &s->dispatches, box_gpu_particle_dispatch_t);
        d->batch = (uint32_t)ecs_vec_count(&s->batch_data) / BOX_GPU_PARTICLE_BATCH_STRIDE;
        d->groups = groups;
        d->spawn = spawn;
        d->terrain = terrain;
        uint8_t *b = ecs_vec_grow_t(NULL, &s->batch_data, uint8_t,
            (int32_t)BOX_GPU_PARTICLE_BATCH_STRIDE);
        ecs_os_memset(b, 0, (ecs_size_t)BOX_GPU_PARTICLE_BATCH_STRIDE);
        *(uint32_t*)b = first;
        first += groups;
    }
}

/** Add the work items and dispatches of an entry point over the draws of the packet, one run of
 * dispatches per sampled terrain of the draws.
 *
 * @param s The particle state
 * @param spawn Whether they run the spawn entry point over the spawns of the pools, else
 *              integrate over their used slots
 */
static void box_gpu_particle_works(
    box_gpu_particles_t *s,
    bool spawn)
{
    int32_t i, j, count = ecs_vec_count(&s->draws_of_packet);
    for (i = 0; i < count; i ++) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(
            &s->draws_of_packet, box_gpu_particle_draw_t, i);
        bool seen = false;
        for (j = 0; j < i && !seen; j ++) {
            seen = ecs_vec_get_t(&s->draws_of_packet, box_gpu_particle_draw_t, j)->terrain ==
                d->terrain;
        }

        if (seen) {
            continue;
        }

        uint32_t first = (uint32_t)ecs_vec_count(&s->work_data) / 2u;
        for (j = i; j < count; j ++) {
            const box_gpu_particle_draw_t *e = ecs_vec_get_t(
                &s->draws_of_packet, box_gpu_particle_draw_t, j);
            if (e->terrain != d->terrain) {
                continue;
            }

            const box_render_particle_block_t *b = (const box_render_particle_block_t*)
                ecs_vec_get_t(&s->block_data, uint8_t,
                    (int32_t)(e->block * BOX_GPU_PARTICLE_STRIDE));
            box_gpu_particle_work(s, e->block, spawn ? b->count : e->used);
        }

        box_gpu_particle_dispatches(s, first, spawn, d->terrain);
    }
}

/** Check that a pool fits the arena.
 *
 * @param gpu The backend
 * @param pool The pool
 * @return Whether it fits
 */
static bool box_gpu_particle_fits(
    box_gpu_t *gpu,
    const box_render_particle_pool_t *pool)
{
    box_gpu_particles_t *s = gpu->particles;
    uint64_t size = (uint64_t)pool->capacity * BOX_RENDER_PARTICLE_SIZE;
    bool result = s->arena && pool->buffer == BOX_RENDER_BUFFER_PARTICLE_FIRST &&
        pool->offset + size <= s->arena_size && pool->block.used <= pool->capacity;
    if (!result) {
        ecs_err("particle pool of %u slots at %u does not fit the arena of %llu bytes",
            pool->capacity, pool->offset, (unsigned long long)s->arena_size);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
    }

    return result;
}

/** Copy the blocks and spawn slots of the pools of a packet into the frame buffers, give each
 * block its first spawn slot and slot in the arena, and keep the work items and the draw of
 * every pool that fits the arena.
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
    int32_t i;
    ecs_vec_set_count_t(NULL, &s->block_data, uint8_t,
        count * (int32_t)BOX_GPU_PARTICLE_STRIDE);
    ecs_os_memset(ecs_vec_first(&s->block_data), 0, count * (int32_t)BOX_GPU_PARTICLE_STRIDE);
    ecs_vec_clear(&s->slot_data);
    ecs_vec_clear(&s->work_data);
    ecs_vec_clear(&s->batch_data);
    ecs_vec_clear(&s->dispatches);
    for (i = 0; i < count; i ++) {
        const box_render_particle_pool_t *pool = ecs_vec_get_t(
            &packet->particles, box_render_particle_pool_t, i);
        box_render_particle_block_t *b = (box_render_particle_block_t*)ecs_vec_get_t(
            &s->block_data, uint8_t, i * (int32_t)BOX_GPU_PARTICLE_STRIDE);
        *b = pool->block;
        b->base = pool->offset / BOX_RENDER_PARTICLE_SIZE;
        box_gpu_origin_split(packet->origin, b->origin);
        b->slot_first = (uint32_t)ecs_vec_count(&s->slot_data);
        if (!pool->table || s->tables == s->no_table) {
            b->points = 0;
        }

        if (b->count) {
            uint32_t *dst = ecs_vec_grow_t(NULL, &s->slot_data, uint32_t, (int32_t)b->count);
            ecs_os_memcpy(dst, box_render_packet_data(packet, pool->slots),
                (ecs_size_t)(b->count * sizeof(uint32_t)));
        }

        if (!box_gpu_particle_fits(gpu, pool)) {
            continue;
        }

        box_gpu_particle_draw_t *d = ecs_vec_append_t(
            NULL, &s->draws_of_packet, box_gpu_particle_draw_t);
        d->scene = pool->scene;
        d->block = (uint32_t)i;
        d->used = b->used;
        d->blend = b->blend;
        d->texture = box_gpu_texture_color(gpu,
            b->look == ParticleSprite || b->look == ParticleCard ? pool->texture : 0);
        d->soft = b->softness > 0;
        d->origin[0] = b->model[3][0];
        d->origin[1] = b->model[3][1];
        d->origin[2] = b->model[3][2];
        d->terrain = b->sampled ? pool->terrain : 0;
    }

    box_gpu_particle_works(s, false);
    box_gpu_particle_works(s, true);
    if (!ecs_vec_count(&s->slot_data)) {
        *ecs_vec_append_t(NULL, &s->slot_data, uint32_t) = BOX_RENDER_PARTICLE_NO_SLOT;
    }

    if (!ecs_vec_count(&s->work_data)) {
        uint32_t *w = ecs_vec_grow_t(NULL, &s->work_data, uint32_t, 2);
        w[0] = w[1] = 0;
    }

    if (!ecs_vec_count(&s->batch_data)) {
        uint8_t *b = ecs_vec_grow_t(NULL, &s->batch_data, uint8_t,
            (int32_t)BOX_GPU_PARTICLE_BATCH_STRIDE);
        ecs_os_memset(b, 0, (ecs_size_t)BOX_GPU_PARTICLE_BATCH_STRIDE);
    }

    uint64_t slot_bytes = (uint64_t)ecs_vec_count(&s->slot_data) * sizeof(uint32_t);
    uint64_t work_bytes = (uint64_t)ecs_vec_count(&s->work_data) * sizeof(uint32_t);
    uint64_t batch_bytes = (uint64_t)ecs_vec_count(&s->batch_data);
    box_gpu_particle_reserve(gpu, &s->blocks, &s->blocks_size,
        (uint64_t)count * BOX_GPU_PARTICLE_STRIDE, WGPUBufferUsage_Storage);
    box_gpu_particle_reserve(gpu, &s->slots, &s->slots_size, slot_bytes,
        WGPUBufferUsage_Storage);
    box_gpu_particle_reserve(gpu, &s->work, &s->work_size, work_bytes,
        WGPUBufferUsage_Storage);
    box_gpu_particle_reserve(gpu, &s->batches, &s->batches_size, batch_bytes,
        WGPUBufferUsage_Uniform);
    wgpuQueueWriteBuffer(gpu->queue, s->blocks, 0, ecs_vec_first(&s->block_data),
        (size_t)count * BOX_GPU_PARTICLE_STRIDE);
    wgpuQueueWriteBuffer(gpu->queue, s->slots, 0, ecs_vec_first(&s->slot_data),
        (size_t)slot_bytes);
    wgpuQueueWriteBuffer(gpu->queue, s->work, 0, ecs_vec_first(&s->work_data),
        (size_t)work_bytes);
    wgpuQueueWriteBuffer(gpu->queue, s->batches, 0, ecs_vec_first(&s->batch_data),
        (size_t)batch_bytes);
}

/** Get the bind group of the simulation, created again after a buffer it binds was replaced.
 *
 * @param gpu The backend
 * @return The group
 */
static WGPUBindGroup box_gpu_particle_sim_group(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = gpu->particles;
    if (!s->sim_group) {
        WGPUBindGroupEntry entries[7] = {
            { .binding = 0, .buffer = s->blocks, .offset = 0, .size = s->blocks_size },
            { .binding = 1, .buffer = s->arena, .offset = 0, .size = s->arena_size },
            { .binding = 2, .buffer = s->slots, .offset = 0, .size = s->slots_size },
            { .binding = 3, .buffer = s->tables, .offset = 0, .size = s->tables_size },
            { .binding = 8, .buffer = s->curves, .offset = 0, .size = s->curves_size },
            { .binding = 9, .buffer = s->work, .offset = 0, .size = s->work_size },
            { .binding = 10, .buffer = s->batches, .offset = 0, .size = 16 }
        };
        s->sim_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = s->sim_layout,
            .entryCount = 7,
            .entries = entries
        });
    }

    return s->sim_group;
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

    ecs_vec_clear(&s->draws_of_packet);
    ecs_vec_clear(&s->chunk_data);
    if (!count) {
        goto done;
    }

    if (!box_gpu_particle_sim_pipelines(gpu)) {
        goto done;
    }

    box_gpu_particle_textures_changed(gpu, packet);
    box_gpu_particle_bound(gpu, BOX_RENDER_BUFFER_PARTICLE_FIRST, &s->arena, &s->arena_size);
    box_gpu_particle_bound(gpu, BOX_RENDER_BUFFER_PARTICLE_TABLE_FIRST, &s->tables,
        &s->tables_size);
    box_gpu_particle_bound(gpu, BOX_RENDER_BUFFER_PARTICLE_CURVES, &s->curves, &s->curves_size);
    box_gpu_particle_upload(gpu, packet, count);
    if (!ecs_vec_count(&s->draws_of_packet)) {
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (i = 0; i < count; i ++) {
        const box_render_particle_pool_t *pool = ecs_vec_get_t(
            &packet->particles, box_render_particle_pool_t, i);
        uint64_t size = (uint64_t)pool->capacity * BOX_RENDER_PARTICLE_SIZE;
        if (pool->clear && s->arena && pool->offset + size <= s->arena_size) {
            wgpuCommandEncoderClearBuffer(encoder, s->arena, pool->offset, size);
        }
    }

    WGPUBindGroup group = box_gpu_particle_sim_group(gpu);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    for (i = 0; i < ecs_vec_count(&s->dispatches); i ++) {
        const box_gpu_particle_dispatch_t *d = ecs_vec_get_t(
            &s->dispatches, box_gpu_particle_dispatch_t, i);
        uint32_t offset = d->batch * BOX_GPU_PARTICLE_BATCH_STRIDE;
        WGPUBindGroup terrain = d->terrain
            ? box_gpu_terrain_sample_group(gpu, d->terrain)
            : NULL;
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        wgpuComputePassEncoderSetBindGroup(pass, 1, terrain ? terrain : s->no_terrain, 0, NULL);
        wgpuComputePassEncoderSetPipeline(pass, d->spawn ? s->spawn : s->integrate);
        wgpuComputePassEncoderDispatchWorkgroups(pass, d->groups, 1, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
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

/** Compare two draws of a view: alpha before additive, then by sprite, then the emitter
 * farthest from the camera first, then packet order.
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
    int result = (x->blend > y->blend) - (x->blend < y->blend);
    if (!result) {
        result = ((uintptr_t)x->texture > (uintptr_t)y->texture) -
            ((uintptr_t)x->texture < (uintptr_t)y->texture);
    }

    if (!result && x->depth != y->depth) {
        result = x->depth > y->depth ? -1 : 1;
    }

    if (!result) {
        result = (x->index > y->index) - (x->index < y->index);
    }

    return result;
}

/** Collect the draws of the scene of a view in the order they are recorded.
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
        if (d->scene != v->scene || !d->used) {
            continue;
        }

        box_gpu_particle_order_t *o = ecs_vec_append_t(
            NULL, &s->order, box_gpu_particle_order_t);
        o->blend = d->blend;
        o->texture = d->texture;
        o->index = i;
        o->depth = d->blend == ParticleAdditive ? 0.0f : -((d->origin[0] - eye[0]) * back[0] +
            (d->origin[1] - eye[1]) * back[1] + (d->origin[2] - eye[2]) * back[2]);
    }

    if (ecs_vec_count(&s->order) > 1) {
        qsort(ecs_vec_first(&s->order), (size_t)ecs_vec_count(&s->order),
            sizeof(box_gpu_particle_order_t), box_gpu_particle_order_cmp);
    }

    return ecs_vec_count(&s->order);
}

/** Get the draw group of a sprite, created on first use after the buffers it binds changed.
 *
 * @param gpu The backend
 * @param texture The sprite
 * @return The group
 */
static WGPUBindGroup box_gpu_particle_draw_group(
    box_gpu_t *gpu,
    WGPUTextureView texture)
{
    box_gpu_particles_t *s = gpu->particles;
    WGPUBindGroup result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->groups); i ++) {
        box_gpu_particle_group_t *g = ecs_vec_get_t(&s->groups, box_gpu_particle_group_t, i);
        if (g->texture == texture) {
            result = g->group;
            goto done;
        }
    }

    WGPUBindGroupEntry entries[6] = {
        { .binding = 0, .buffer = s->blocks, .offset = 0, .size = s->blocks_size },
        { .binding = 1, .buffer = s->arena, .offset = 0, .size = s->arena_size },
        { .binding = 2, .buffer = s->chunks, .offset = 0, .size = s->chunks_size },
        { .binding = 4, .textureView = texture },
        { .binding = 5, .sampler = box_gpu_texture_sampler(gpu, WrapClamp, 1) },
        { .binding = 6, .buffer = s->curves, .offset = 0, .size = s->curves_size }
    };
    box_gpu_particle_group_t *g = ecs_vec_append_t(NULL, &s->groups, box_gpu_particle_group_t);
    g->texture = texture;
    g->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->draw_layout,
        .entryCount = 6,
        .entries = entries
    });
    result = g->group;
done:
    return result;
}

/** Write the chunks of the draws of a view after those of the views before it in the packet.
 *
 * @param gpu The backend
 * @param count Draws of the view in s->order
 * @return Index of the first chunk of the view
 */
static uint32_t box_gpu_particle_chunks(
    box_gpu_t *gpu,
    int32_t count)
{
    box_gpu_particles_t *s = gpu->particles;
    uint32_t first = (uint32_t)ecs_vec_count(&s->chunk_data) / 2u, c;
    int32_t i;
    for (i = 0; i < count; i ++) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(&s->draws_of_packet,
            box_gpu_particle_draw_t, ecs_vec_get_t(&s->order, box_gpu_particle_order_t, i)->index);
        for (c = 0; c < d->used; c += BOX_GPU_PARTICLE_CHUNK) {
            uint32_t *chunk = ecs_vec_grow_t(NULL, &s->chunk_data, uint32_t, 2);
            chunk[0] = d->block;
            chunk[1] = c;
        }
    }

    uint32_t end = (uint32_t)ecs_vec_count(&s->chunk_data) / 2u;
    box_gpu_particle_reserve(gpu, &s->chunks, &s->chunks_size,
        (uint64_t)end * 2 * sizeof(uint32_t), WGPUBufferUsage_Storage);
    wgpuQueueWriteBuffer(gpu->queue, s->chunks, (uint64_t)first * 2 * sizeof(uint32_t),
        ecs_vec_get_t(&s->chunk_data, uint32_t, (int32_t)first * 2),
        (size_t)(end - first) * 2 * sizeof(uint32_t));

    return first;
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
    int32_t i, j, count = s ? box_gpu_particle_order(s, v) : 0;
    if (!count) {
        goto done;
    }

    uint32_t chunk = box_gpu_particle_chunks(gpu, count);
    for (i = 0; i < count; i = j) {
        const box_gpu_particle_order_t *o = ecs_vec_get_t(&s->order, box_gpu_particle_order_t, i);
        uint32_t chunks = 0;
        for (j = i; j < count; j ++) {
            const box_gpu_particle_order_t *n = ecs_vec_get_t(
                &s->order, box_gpu_particle_order_t, j);
            if (n->blend != o->blend || n->texture != o->texture) {
                break;
            }

            const box_gpu_particle_draw_t *d = ecs_vec_get_t(
                &s->draws_of_packet, box_gpu_particle_draw_t, n->index);
            chunks += (d->used + BOX_GPU_PARTICLE_CHUNK - 1) / BOX_GPU_PARTICLE_CHUNK;
        }

        WGPURenderPipeline pipeline = box_gpu_particle_draw_pipeline(gpu, format, o->blend);
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

        wgpuRenderPassEncoderSetBindGroup(pass, 1,
            box_gpu_particle_draw_group(gpu, o->texture), 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 6, chunks * BOX_GPU_PARTICLE_CHUNK, 0,
            chunk * BOX_GPU_PARTICLE_CHUNK);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        chunk += chunks;
        result = true;
    }

    if (depth_group && depth_group != s->no_depth_group) {
        wgpuBindGroupRelease(depth_group);
    }

done:
    return result;
}

#endif
