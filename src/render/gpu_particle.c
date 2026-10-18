/**
 * @file render/gpu_particle.c
 * @brief Particle passes of the webgpu backend: the integrate and spawn dispatches of every
 * pool of a packet, before its targets, and the indirect draw of the pools of a view's scene.
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

/** Bytes of the indirect draw arguments of a pool */
#define BOX_GPU_PARTICLE_ARGS (16u)

/** The draw of a pool in the views of its scene, valid for the packet that simulated it */
typedef struct box_gpu_particle_draw_t {
    ecs_entity_t scene;             /**< Scene root of the pool */
    uint32_t block;                 /**< Index of its block and its draw arguments */
    WGPUBindGroup group;            /**< Binds its block and its particles to the draw */
} box_gpu_particle_draw_t;

struct box_gpu_particles_t {
    WGPUBindGroupLayout sim_layout; /**< Block, particles, spawn slots, mesh table, arguments */
    WGPUPipelineLayout sim_pipeline_layout; /**< Pipeline layout of the compute passes */
    WGPUComputePipeline integrate;  /**< Ages and moves the used slots of a pool */
    WGPUComputePipeline spawn;      /**< Writes the spawned particles into their slots */
    int32_t sim_revision;           /**< Revision of the shader the compute pipelines use */
    WGPUBindGroupLayout draw_layout; /**< Block and particles of the draw */
    WGPUPipelineLayout draw_pipeline_layout; /**< View uniforms, then the draw group */
    box_gpu_blit_t draws[BOX_GPU_BLIT_FORMATS]; /**< Draw pipelines by color format */
    int32_t draw_count;             /**< Draw pipelines created */
    int32_t draw_revision;          /**< Revision of the shader the draw pipelines use */
    WGPUBuffer blocks;              /**< box_render_particle_block_t per pool of the packet */
    uint64_t blocks_size;           /**< Bytes of the block buffer */
    WGPUBuffer slots;               /**< Spawn slots of every pool of the packet */
    uint64_t slots_size;            /**< Bytes of the slot buffer */
    WGPUBuffer args;                /**< Indirect draw arguments per pool of the packet */
    uint64_t args_size;             /**< Bytes of the argument buffer */
    WGPUBuffer no_table;            /**< Mesh table of the pools without one */
    ecs_vec_t block_data;           /**< Bytes of the blocks of the packet */
    ecs_vec_t slot_data;            /**< uint32_t, the spawn slots of the packet */
    ecs_vec_t draws_of_packet;      /**< box_gpu_particle_draw_t of the last packet */
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

void box_gpu_particles_init(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = ecs_os_calloc_t(box_gpu_particles_t);
    WGPUBindGroupLayoutEntry sim[5] = {
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
        }
    };
    WGPUBindGroupLayoutEntry draw[2] = {
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
        }
    };
    s->sim_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 5, .entries = sim });
    s->sim_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &s->sim_layout
        });
    s->draw_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = draw });
    WGPUBindGroupLayout groups[2] = { gpu->view_layout, s->draw_layout };
    s->draw_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    s->no_table = box_gpu_particle_buffer(gpu, 32, WGPUBufferUsage_Storage);
    ecs_vec_init_t(NULL, &s->block_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &s->slot_data, uint32_t, 0);
    ecs_vec_init_t(NULL, &s->draws_of_packet, box_gpu_particle_draw_t, 0);
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

void box_gpu_particles_fini(
    box_gpu_t *gpu)
{
    box_gpu_particles_t *s = gpu->particles;
    if (!s) {
        goto done;
    }

    box_gpu_particle_draws_clear(s);
    box_gpu_particle_draw_reset(s);
    BOX_GPU_PARTICLE_RELEASE(s->integrate, wgpuComputePipelineRelease);
    BOX_GPU_PARTICLE_RELEASE(s->spawn, wgpuComputePipelineRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sim_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->sim_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->draw_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->draw_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_PARTICLE_RELEASE(s->blocks, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->slots, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->args, wgpuBufferRelease);
    BOX_GPU_PARTICLE_RELEASE(s->no_table, wgpuBufferRelease);
    ecs_vec_fini_t(NULL, &s->block_data, uint8_t);
    ecs_vec_fini_t(NULL, &s->slot_data, uint32_t);
    ecs_vec_fini_t(NULL, &s->draws_of_packet, box_gpu_particle_draw_t);
    ecs_os_free(s);
    gpu->particles = NULL;
done:
    return;
}

/** Create a compute pipeline of an entry point of the simulation shader.
 *
 * @param gpu The backend
 * @param module The module
 * @param entry The entry point
 * @return The pipeline
 */
static WGPUComputePipeline box_gpu_particle_compute(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    const char *entry)
{
    WGPUComputePipelineDescriptor desc = {
        .layout = gpu->particles->sim_pipeline_layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    };

    return wgpuDeviceCreateComputePipeline(gpu->device, &desc);
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

    BOX_GPU_PARTICLE_RELEASE(s->integrate, wgpuComputePipelineRelease);
    BOX_GPU_PARTICLE_RELEASE(s->spawn, wgpuComputePipelineRelease);
    s->integrate = box_gpu_particle_compute(gpu, shader->module, "integrate");
    s->spawn = box_gpu_particle_compute(gpu, shader->module, "spawn");
    s->sim_revision = shader->revision;
done:
    return result;
}

/** Create the draw pipeline of a color format.
 *
 * @param gpu The backend
 * @param module The module of the draw shader
 * @param format The format of the color target
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_particle_draw_create(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    WGPUTextureFormat format)
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

    return wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
}

/** Get the draw pipeline of a color format, created on first use and again after the draw
 * shader recompiled.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @return The pipeline, NULL without a draw shader or when too many formats are in use
 */
static WGPURenderPipeline box_gpu_particle_draw_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
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
        if (s->draws[i].format == format) {
            result = s->draws[i].pipeline;
            goto done;
        }
    }

    if (s->draw_count < BOX_GPU_BLIT_FORMATS) {
        result = box_gpu_particle_draw_create(gpu, shader->module, format);
        s->draws[s->draw_count].format = format;
        s->draws[s->draw_count].pipeline = result;
        s->draw_count ++;
    }

done:
    return result;
}

/** Copy the blocks and spawn slots of the pools of a packet into the frame buffers, giving
 * each block its first spawn slot and its draw arguments.
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
    ecs_vec_clear(&s->slot_data);
    for (i = 0; i < count; i ++) {
        const box_render_particle_pool_t *pool = ecs_vec_get_t(
            &packet->particles, box_render_particle_pool_t, i);
        box_render_particle_block_t *b = (box_render_particle_block_t*)ecs_vec_get_t(
            &s->block_data, uint8_t, i * (int32_t)BOX_GPU_PARTICLE_STRIDE);
        *b = pool->block;
        b->slot_first = (uint32_t)ecs_vec_count(&s->slot_data);
        b->args = (uint32_t)i;
        if (!pool->table || !ecs_map_get(&gpu->buffers, pool->table)) {
            b->points = 0;
        }

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
    wgpuQueueWriteBuffer(gpu->queue, s->blocks, 0, ecs_vec_first(&s->block_data),
        (size_t)count * BOX_GPU_PARTICLE_STRIDE);
    wgpuQueueWriteBuffer(gpu->queue, s->slots, 0, ecs_vec_first(&s->slot_data),
        (size_t)slot_bytes);
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
    uint64_t size = (uint64_t)pool->capacity * BOX_RENDER_PARTICLE_SIZE;
    uint32_t offset = index * BOX_GPU_PARTICLE_STRIDE;
    if (!page || pool->offset + size > page->size || pool->block.used > pool->capacity) {
        ecs_err("particle pool of %u slots at %u does not fit buffer %u",
            pool->capacity, pool->offset, pool->buffer);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    WGPUBindGroupEntry entries[5] = {
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
        { .binding = 4, .buffer = s->args, .offset = 0, .size = s->args_size }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->sim_layout,
        .entryCount = 5,
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
    WGPUBindGroupEntry draw[2] = { entries[0], entries[1] };
    box_gpu_particle_draw_t *d = ecs_vec_append_t(
        NULL, &s->draws_of_packet, box_gpu_particle_draw_t);
    d->scene = pool->scene;
    d->block = index;
    d->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->draw_layout,
        .entryCount = 2,
        .entries = draw
    });
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

bool box_gpu_particles_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v)
{
    box_gpu_particles_t *s = gpu->particles;
    WGPURenderPipeline pipeline = NULL;
    bool result = false;
    int32_t i;
    for (i = 0; s && i < ecs_vec_count(&s->draws_of_packet); i ++) {
        const box_gpu_particle_draw_t *d = ecs_vec_get_t(
            &s->draws_of_packet, box_gpu_particle_draw_t, i);
        uint32_t offset = d->block * BOX_GPU_PARTICLE_STRIDE;
        if (d->scene != v->scene) {
            continue;
        }

        if (!pipeline) {
            pipeline = box_gpu_particle_draw_pipeline(gpu, format);
            if (!pipeline) {
                break;
            }

            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        }

        wgpuRenderPassEncoderSetBindGroup(pass, 1, d->group, 1, &offset);
        wgpuRenderPassEncoderDrawIndirect(pass, s->args,
            (uint64_t)d->block * BOX_GPU_PARTICLE_ARGS);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        result = true;
    }

    return result;
}

#endif
