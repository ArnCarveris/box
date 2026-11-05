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

/** Storage buffers of the integrate and spawn passes: particles, spawn slots, mesh table,
 * arguments and the curve table */
#define BOX_GPU_PARTICLE_SIM_STORAGE (5u)

/** Draw pipelines, one per color format and ParticleBlend */
#define BOX_GPU_PARTICLE_DRAWS (BOX_GPU_BLIT_FORMATS * 2)

/** The draw of a pool in the views of its scene, valid for the packet that simulated it */
typedef struct box_gpu_particle_draw_t {
    ecs_entity_t scene;             /**< Scene root of the pool */
    uint32_t block;                 /**< Index of its block and its draw arguments */
    WGPUBindGroup group;            /**< Binds its block, particles, keys, mesh, sprite and the
                                     *   curve table */
    WGPUBuffer page;                /**< Particle page of the pool */
    uint32_t offset;                /**< Byte offset of the pool in the page */
    uint64_t size;                  /**< Bytes of the pool */
    uint32_t sort_count;            /**< Keys the sort of a view sorts, 0 when unsorted */
    uint32_t blend;                 /**< ParticleBlend of the pipeline */
    bool soft;                      /**< It fades in front of the depth of the view */
    float origin[3];                /**< World position of the emitter */
} box_gpu_particle_draw_t;

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
    WGPUBuffer no_table;            /**< Mesh table, keys and corners of the pools without */
    WGPUTexture no_depth;           /**< Depth bound while the view writes its depth */
    WGPUBindGroup no_depth_group;   /**< Binds no_depth */
    ecs_vec_t block_data;           /**< Bytes of the blocks of the packet */
    ecs_vec_t slot_data;            /**< uint32_t, the spawn slots of the packet */
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
    WGPUBindGroupLayoutEntry sort[5] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_render_particle_block_t)
            }
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
        }
    };
    s->sort_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 5, .entries = sort });
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
    WGPUBindGroupLayoutEntry sim[6] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_render_particle_block_t)
            }
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
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = sim });
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
    ecs_vec_init_t(NULL, &s->draws_of_packet, box_gpu_particle_draw_t, 0);
    ecs_vec_init_t(NULL, &s->order, box_gpu_particle_order_t, 0);
    gpu->particles = s;
}

/** Release the draw groups of the pools of the last packet.
 *
 * @param s The particle state
 */
static void box_gpu_particle_draws_clear(
    box_gpu_particles_t *s)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->draws_of_packet); i ++) {
        wgpuBindGroupRelease(ecs_vec_get_t(
            &s->draws_of_packet, box_gpu_particle_draw_t, i)->group);
    }

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
    BOX_GPU_PARTICLE_RELEASE(s->no_table, wgpuBufferRelease);
    ecs_vec_fini_t(NULL, &s->block_data, uint8_t);
    ecs_vec_fini_t(NULL, &s->slot_data, uint32_t);
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

/** Make the compute pipelines match the simulation shader.
 *
 * @param gpu The backend
 * @return Whether they exist, false while the shader has no module
 */
static bool box_gpu_particle_sim_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = gpu->particles;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_PARTICLE_SIM);
    bool result = shader && shader->module;
    if (!result || (s->integrate && s->sim_revision == shader->revision)) {
        goto done;
    }

    box_gpu_particle_sim_reset(s);
    s->integrate = box_gpu_particle_compute(
        gpu, s->sim_pipeline_layout, shader->module, "integrate");
    s->spawn = box_gpu_particle_compute(gpu, s->sim_pipeline_layout, shader->module, "spawn");
    s->sort_keys = box_gpu_particle_compute(
        gpu, s->sort_pipeline_layout, shader->module, "sort_keys");
    s->sort_local = box_gpu_particle_compute(
        gpu, s->sort_pipeline_layout, shader->module, "sort_local");
    s->sort_global = box_gpu_particle_compute(
        gpu, s->sort_pipeline_layout, shader->module, "sort_global");
    s->sort_merge = box_gpu_particle_compute(
        gpu, s->sort_pipeline_layout, shader->module, "sort_merge");
    s->sim_revision = shader->revision;
done:
    return result;
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
        (uint64_t)count * BOX_GPU_PARTICLE_STRIDE, WGPUBufferUsage_Uniform);
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
 * particles, its sort keys, its billboard corners and its sprite.
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
    WGPUBindGroupEntry draw[7] = {
        {
            .binding = 0, .buffer = s->blocks, .offset = 0,
            .size = sizeof(box_render_particle_block_t)
        },
        { .binding = 1, .buffer = page->buffer, .offset = pool->offset, .size = size },
        {
            .binding = 2, .buffer = b->sorted ? s->keys : s->no_table, .offset = 0,
            .size = b->sorted ? s->keys_size : 32
        },
        {
            .binding = 3, .buffer = mesh ? mesh->buffer : s->no_table, .offset = 0,
            .size = mesh ? mesh->size : 32
        },
        {
            .binding = 4,
            .textureView = box_gpu_texture_color(gpu, b->look == ParticleSprite ? pool->texture : 0)
        },
        { .binding = 5, .sampler = box_gpu_texture_sampler(gpu, WrapClamp, 1) },
        {
            .binding = 6, .buffer = curves ? curves->buffer : s->no_table, .offset = 0,
            .size = curves ? curves->size : 32
        }
    };
    box_gpu_particle_draw_t *d = ecs_vec_append_t(
        NULL, &s->draws_of_packet, box_gpu_particle_draw_t);
    d->scene = pool->scene;
    d->block = index;
    d->page = page->buffer;
    d->offset = pool->offset;
    d->size = size;
    d->sort_count = b->sorted ? b->sort_count : 0;
    d->blend = b->blend;
    d->soft = b->softness > 0;
    d->origin[0] = b->model[3][0];
    d->origin[1] = b->model[3][1];
    d->origin[2] = b->model[3][2];
    d->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->draw_layout,
        .entryCount = 7,
        .entries = draw
    });
}

/** Record the integrate and spawn dispatches of a pool and keep its draw.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param pool The pool
 * @param index Index of the pool in the packet
 */
static void box_gpu_particle_dispatch(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    const box_render_particle_pool_t *pool,
    uint32_t index)
{
    box_gpu_particles_t *s = gpu->particles;
    box_gpu_buffer_t *page = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, pool->buffer);
    box_gpu_buffer_t *table = pool->table
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, pool->table)
        : NULL;
    const box_gpu_buffer_t *curves = box_gpu_particle_curves(gpu, pool);
    uint64_t size = (uint64_t)pool->capacity * BOX_RENDER_PARTICLE_SIZE;
    uint32_t offset = index * BOX_GPU_PARTICLE_STRIDE;
    if (!page || pool->offset + size > page->size || pool->block.used > pool->capacity) {
        ecs_err("particle pool of %u slots at %u does not fit buffer %u",
            pool->capacity, pool->offset, pool->buffer);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    WGPUBindGroupEntry entries[6] = {
        {
            .binding = 0, .buffer = s->blocks, .offset = 0,
            .size = sizeof(box_render_particle_block_t)
        },
        { .binding = 1, .buffer = page->buffer, .offset = pool->offset, .size = size },
        { .binding = 2, .buffer = s->slots, .offset = 0, .size = s->slots_size },
        {
            .binding = 3, .buffer = table ? table->buffer : s->no_table, .offset = 0,
            .size = table ? table->size : 32
        },
        { .binding = 4, .buffer = s->args, .offset = 0, .size = s->args_size },
        {
            .binding = 8, .buffer = curves ? curves->buffer : s->no_table, .offset = 0,
            .size = curves ? curves->size : 32
        }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->sim_layout,
        .entryCount = 6,
        .entries = entries
    });
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuComputePassEncoderSetPipeline(pass, s->integrate);
    wgpuComputePassEncoderDispatchWorkgroups(pass,
        (pool->block.used + BOX_GPU_PARTICLE_GROUP - 1) / BOX_GPU_PARTICLE_GROUP, 1, 1);
    if (pool->block.count) {
        wgpuComputePassEncoderSetPipeline(pass, s->spawn);
        wgpuComputePassEncoderDispatchWorkgroups(pass,
            (pool->block.count + BOX_GPU_PARTICLE_GROUP - 1) / BOX_GPU_PARTICLE_GROUP, 1, 1);
    }

    wgpuBindGroupRelease(group);
    box_gpu_particle_keep(gpu, pool, index, page);
done:
    return;
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
    if (!count) {
        goto done;
    }

    if (!box_gpu_particle_sim_pipelines(gpu)) {
        ecs_err("particle shader is not available, %d pools skipped", count);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    box_gpu_particle_upload(gpu, packet, count);
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
    for (i = 0; i < count; i ++) {
        box_gpu_particle_dispatch(gpu, pass,
            ecs_vec_get_t(&packet->particles, box_render_particle_pool_t, i), (uint32_t)i);
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

/** Set the dynamic offsets of the sort group of a pool for a step and bind it.
 *
 * @param pass The compute pass
 * @param group The sort group of the pool
 * @param offsets Offsets of the block and the view uniforms, receives the step offset
 * @param m Log2 of the run length of the step
 * @param i Log2 of the compare distance of the step
 */
static void box_gpu_particle_sort_bind(
    WGPUComputePassEncoder pass,
    WGPUBindGroup group,
    uint32_t *offsets,
    uint32_t m,
    uint32_t i)
{
    offsets[2] = (m * (m - 1) / 2 + i) * BOX_GPU_PARTICLE_STEP_STRIDE;
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 3, offsets);
}

/** Record the sort of the keys of a pool by the view depth of its particles in a view: the
 * keys, the sort of every workgroup run, then per longer run length the long steps and the
 * merge of the short ones.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param d The draw of the pool
 * @param slot Slot of the view in the view buffer
 */
static void box_gpu_particle_sort_pool(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    const box_gpu_particle_draw_t *d,
    int32_t slot)
{
    box_gpu_particles_t *s = gpu->particles;
    uint32_t n = d->sort_count, groups = n / BOX_RENDER_PARTICLE_SORT_MIN, m, i;
    uint32_t offsets[3] = {
        d->block * BOX_GPU_PARTICLE_STRIDE, (uint32_t)slot * BOX_GPU_VIEW_STRIDE, 0
    };
    WGPUBindGroupEntry entries[5] = {
        {
            .binding = 0, .buffer = s->blocks, .offset = 0,
            .size = sizeof(box_render_particle_block_t)
        },
        { .binding = 1, .buffer = d->page, .offset = d->offset, .size = d->size },
        { .binding = 5, .buffer = s->keys, .offset = 0, .size = s->keys_size },
        {
            .binding = 6, .buffer = gpu->view_buffer, .offset = 0,
            .size = sizeof(box_gpu_view_uniforms_t)
        },
        {
            .binding = 7, .buffer = s->steps, .offset = 0, .size = 16
        }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->sort_layout,
        .entryCount = 5,
        .entries = entries
    });
    box_gpu_particle_sort_bind(pass, group, offsets, 1, 0);
    wgpuComputePassEncoderSetPipeline(pass, s->sort_keys);
    wgpuComputePassEncoderDispatchWorkgroups(pass, n / BOX_GPU_PARTICLE_GROUP, 1, 1);
    wgpuComputePassEncoderSetPipeline(pass, s->sort_local);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
    for (m = BOX_GPU_PARTICLE_SORT_GROUP_LOG + 1; (1u << m) <= n; m ++) {
        wgpuComputePassEncoderSetPipeline(pass, s->sort_global);
        for (i = m - 1; i >= BOX_GPU_PARTICLE_SORT_GROUP_LOG; i --) {
            box_gpu_particle_sort_bind(pass, group, offsets, m, i);
            wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
        }

        box_gpu_particle_sort_bind(pass, group, offsets, m, 0);
        wgpuComputePassEncoderSetPipeline(pass, s->sort_merge);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
    }

    wgpuBindGroupRelease(group);
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
    for (i = 0; s && s->sort_keys && i < ecs_vec_count(&s->draws_of_packet); i ++) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(
            &s->draws_of_packet, box_gpu_particle_draw_t, i);
        if (d->scene != v->scene || !d->sort_count || !gpu->view_buffer) {
            continue;
        }

        if (!pass) {
            pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        }

        box_gpu_particle_sort_pool(gpu, pass, d, slot);
    }

    if (pass) {
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
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
