/**
 * @file renderer/particle.c
 * @brief Particle pools of the renderer and the passes that fill and draw them.
 *
 * Every emitter of a packet owns a pool: one buffer per particle channel, a
 * dead list of the free slots and a counter block. The integrate pass ages and
 * moves the live particles and pushes the slots that expire onto the dead list,
 * the spawn pass pops as many slots as the packet asked for and fills them from
 * a hash of the emitter seed and the spawn index, and the sprite pass expands
 * every slot into a camera facing quad. A pool lives as long as its emitter
 * keeps appearing in the packets.
 */

#include "renderer_internal.h"
#include <stdio.h>

/** Create one storage buffer of a pool.
 *
 * @param r The renderer
 * @param size Bytes the buffer holds
 * @return The buffer, zero filled by the device
 */
static WGPUBuffer box_particle_storage(
    box_renderer_t *r,
    uint64_t size)
{
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = size
    };

    return wgpuDeviceCreateBuffer(r->device, &desc);
}

/** Fill the dead list of a pool with every slot and mark all of them free.
 *
 * The list is written back to front, so the first particle of an emitter lands
 * in slot zero and the pool fills in order.
 *
 * @param r The renderer
 * @param pool The pool
 */
static void box_particle_free_list(
    box_renderer_t *r,
    box_gpu_emitter_t *pool)
{
    uint32_t *dead = malloc((size_t)pool->capacity * sizeof(uint32_t));
    uint32_t i;
    for (i = 0; i < pool->capacity; i ++) {
        dead[i] = pool->capacity - 1 - i;
    }

    wgpuQueueWriteBuffer(r->gpu_queue, pool->dead, 0, dead,
        (size_t)pool->capacity * sizeof(uint32_t));
    free(dead);

    uint32_t counts[BOX_PARTICLE_COUNTS] = { pool->capacity, 0, 0, 0 };
    wgpuQueueWriteBuffer(r->gpu_queue, pool->counts, 0, counts, sizeof(counts));
}

/** Release every buffer and bind group of a pool.
 *
 * @param pool The pool
 */
static void box_particle_pool_fini(
    box_gpu_emitter_t *pool)
{
    if (pool->sim_group) wgpuBindGroupRelease(pool->sim_group);
    if (pool->draw_group) wgpuBindGroupRelease(pool->draw_group);
    if (pool->position) wgpuBufferRelease(pool->position);
    if (pool->velocity) wgpuBufferRelease(pool->velocity);
    if (pool->force) wgpuBufferRelease(pool->force);
    if (pool->color) wgpuBufferRelease(pool->color);
    if (pool->size) wgpuBufferRelease(pool->size);
    if (pool->dead) wgpuBufferRelease(pool->dead);
    if (pool->counts) wgpuBufferRelease(pool->counts);
    if (pool->uniform) wgpuBufferRelease(pool->uniform);
    if (pool->table) wgpuBufferRelease(pool->table);
    memset(pool, 0, sizeof(box_gpu_emitter_t));
}

/** Create the channel buffers, the dead list and the uniform of a pool.
 *
 * @param r The renderer
 * @param pool The pool
 * @param id Emitter the pool belongs to
 * @param capacity Particles it holds
 */
static void box_particle_pool_init(
    box_renderer_t *r,
    box_gpu_emitter_t *pool,
    uint32_t id,
    uint32_t capacity)
{
    memset(pool, 0, sizeof(box_gpu_emitter_t));
    pool->id = id;
    pool->capacity = capacity;
    pool->position = box_particle_storage(r, (uint64_t)capacity * 16);
    pool->velocity = box_particle_storage(r, (uint64_t)capacity * 16);
    pool->force = box_particle_storage(r, (uint64_t)capacity * 16);
    pool->color = box_particle_storage(r, (uint64_t)capacity * 16);
    pool->size = box_particle_storage(r, (uint64_t)capacity * 4);
    pool->dead = box_particle_storage(r, (uint64_t)capacity * 4);
    pool->counts = box_particle_storage(r, BOX_PARTICLE_COUNTS * sizeof(uint32_t));
    pool->table = box_particle_storage(r, sizeof(box_particle_point_t));

    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_PARTICLE_UNIFORM_SIZE
    };
    pool->uniform = wgpuDeviceCreateBuffer(r->device, &desc);
    box_particle_free_list(r, pool);
}

/** Bind group layouts of the compute and the sprite passes.
 *
 * The compute passes bind the five channels, the dead list, the counters and
 * the shape table next to the uniform of the emitter; the sprite pass reads the
 * channels only, which keeps both stages inside the storage buffer limit every
 * device guarantees.
 *
 * @param r The renderer
 */
static void box_particle_layouts(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry sim[BOX_PARTICLE_SIM_BINDINGS + 1] = {{
        .binding = 0,
        .visibility = WGPUShaderStage_Compute,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = BOX_PARTICLE_UNIFORM_SIZE
        }
    }};
    uint32_t i;
    for (i = 1; i <= BOX_PARTICLE_SIM_BINDINGS; i ++) {
        sim[i].binding = i;
        sim[i].visibility = WGPUShaderStage_Compute;
        sim[i].buffer.type = i == BOX_PARTICLE_SIM_BINDINGS ?
            WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
    }

    WGPUBindGroupLayoutDescriptor sim_desc = {
        .entryCount = BOX_PARTICLE_SIM_BINDINGS + 1,
        .entries = sim
    };
    r->particles.sim_layout = wgpuDeviceCreateBindGroupLayout(r->device, &sim_desc);

    WGPUBindGroupLayoutEntry draw[BOX_PARTICLE_DRAW_BINDINGS] = {0};
    for (i = 0; i < BOX_PARTICLE_DRAW_BINDINGS; i ++) {
        draw[i].binding = i;
        draw[i].visibility = WGPUShaderStage_Vertex;
        draw[i].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    }

    WGPUBindGroupLayoutDescriptor draw_desc = {
        .entryCount = BOX_PARTICLE_DRAW_BINDINGS,
        .entries = draw
    };
    r->particles.draw_layout = wgpuDeviceCreateBindGroupLayout(r->device, &draw_desc);
}

/** Release the pipelines the particle shaders built.
 *
 * @param r The renderer
 */
static void box_particle_pipelines_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < BOX_PARTICLE_FORMATS; i ++) {
        if (r->particles.pipeline[i]) {
            wgpuRenderPipelineRelease(r->particles.pipeline[i]);
        }

        r->particles.pipeline[i] = NULL;
    }

    if (r->particles.spawn) wgpuComputePipelineRelease(r->particles.spawn);
    if (r->particles.integrate) wgpuComputePipelineRelease(r->particles.integrate);
    r->particles.spawn = NULL;
    r->particles.integrate = NULL;
}

/** Build the spawn and integrate pipelines from the simulation shader.
 *
 * @param r The renderer
 * @param handle Shader handle of the simulation module
 */
static void box_particle_build_sim(
    box_renderer_t *r,
    uint32_t handle)
{
    WGPUShaderModule module = box_shader_module(r, handle);
    if (!module) {
        goto done;
    }

    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->particles.sim_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device, &layout_desc);
    WGPUComputePipelineDescriptor spawn = {
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = "cs_spawn", .length = WGPU_STRLEN }
        }
    };
    WGPUComputePipelineDescriptor integrate = {
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = "cs_integrate", .length = WGPU_STRLEN }
        }
    };
    r->particles.spawn = wgpuDeviceCreateComputePipeline(r->device, &spawn);
    r->particles.integrate = wgpuDeviceCreateComputePipeline(r->device, &integrate);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);
done:
    return;
}

/** Build the point sprite pipeline of one target format.
 *
 * @param r The renderer
 * @param module Module of the sprite shader
 * @param layout Pipeline layout of the pass
 * @param hdr The pass writes the float scene target
 * @return The pipeline
 */
static WGPURenderPipeline box_particle_build_draw(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    bool hdr)
{
    WGPUBlendState blend = {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
            WGPUBlendFactor_OneMinusSrcAlpha },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
            WGPUBlendFactor_OneMinusSrcAlpha }
    };
    WGPUColorTargetState target = {
        .format = hdr ? BOX_EFFECT_HDR_FORMAT : r->format,
        .blend = &blend,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = WGPUOptionalBool_False,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always },
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN }
        },
        .fragment = &fragment,
        .depthStencil = &depth,
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };

    return wgpuDeviceCreateRenderPipeline(r->device, &desc);
}

/** Build the pipelines of the particle passes from the shaders of a packet.
 *
 * @param r The renderer
 * @param p The packet
 * @return True when every pipeline of both passes exists
 */
static bool box_particle_ensure(
    box_renderer_t *r,
    const box_packet_t *p)
{
    bool result = false;
    if (p->particle_sim_shader == BOX_SHADER_NONE ||
        p->particle_draw_shader == BOX_SHADER_NONE)
    {
        goto done;
    }

    if (!r->particles.sim_layout) {
        box_particle_layouts(r);
    }

    if (r->particles.sim_shader != p->particle_sim_shader ||
        r->particles.draw_shader != p->particle_draw_shader)
    {
        box_particle_pipelines_fini(r);
        r->particles.sim_shader = p->particle_sim_shader;
        r->particles.draw_shader = p->particle_draw_shader;
        box_particle_build_sim(r, p->particle_sim_shader);

        WGPUShaderModule module = box_shader_module(r, p->particle_draw_shader);
        if (module) {
            WGPUBindGroupLayout groups[2] = { r->view_layout, r->particles.draw_layout };
            WGPUPipelineLayoutDescriptor layout_desc = {
                .bindGroupLayoutCount = 2,
                .bindGroupLayouts = groups
            };
            WGPUPipelineLayout layout =
                wgpuDeviceCreatePipelineLayout(r->device, &layout_desc);
            uint32_t i;
            for (i = 0; i < BOX_PARTICLE_FORMATS; i ++) {
                r->particles.pipeline[i] =
                    box_particle_build_draw(r, module, layout, i != 0);
            }

            wgpuPipelineLayoutRelease(layout);
            wgpuShaderModuleRelease(module);
        }
    }

    result = r->particles.spawn && r->particles.integrate &&
        r->particles.pipeline[0] && r->particles.pipeline[1];
    if (!result && !r->particles.failed) {
        fprintf(stderr, "box: the particle shaders do not compile, no particles are drawn\n");
        r->particles.failed = true;
    }

done:
    return result;
}

/** Pool of an emitter, created when the renderer sees the emitter for the first time.
 *
 * A pool whose emitter changed its maximum is released and built again, which
 * is the only way its buffers change size.
 *
 * @param r The renderer
 * @param e The emitter of the packet
 * @return The pool
 */
static box_gpu_emitter_t* box_particle_pool(
    box_renderer_t *r,
    const box_particle_emitter_t *e)
{
    box_gpu_emitter_t *result = NULL;
    uint32_t i;
    for (i = 0; i < r->particles.pools.count && !result; i ++) {
        box_gpu_emitter_t *pool = box_vec_at(&r->particles.pools, i);
        if (pool->id == e->id) {
            result = pool;
        }
    }

    if (result && result->capacity != e->max) {
        box_particle_pool_fini(result);
        box_particle_pool_init(r, result, e->id, e->max);
    }

    if (!result) {
        result = box_vec_push(&r->particles.pools);
        box_particle_pool_init(r, result, e->id, e->max);
    }

    return result;
}

/** Upload the table of an emitter and grow its buffer when it has to.
 *
 * The table holds the spawn points of the shape and the colliders of the
 * emitter behind them, so both are one upload and one binding.
 *
 * @param r The renderer
 * @param pool The pool
 * @param e The emitter of the packet
 * @param p The packet
 */
static void box_particle_table(
    box_renderer_t *r,
    box_gpu_emitter_t *pool,
    const box_particle_emitter_t *e,
    const box_packet_t *p)
{
    uint32_t count = e->point_count + e->collider_count;
    if (!count) {
        goto done;
    }

    if (count > pool->points) {
        if (pool->table) {
            wgpuBufferRelease(pool->table);
        }

        pool->table = box_particle_storage(r,
            (uint64_t)count * sizeof(box_particle_point_t));
        pool->points = count;
        if (pool->sim_group) {
            wgpuBindGroupRelease(pool->sim_group);
            pool->sim_group = NULL;
        }
    }

    const box_particle_point_t *points = box_vec_at(&p->particle_points, e->point_first);
    wgpuQueueWriteBuffer(r->gpu_queue, pool->table, 0, points,
        (size_t)count * sizeof(box_particle_point_t));
done:
    return;
}

/** Bind groups of a pool, built once and kept while its buffers stay.
 *
 * @param r The renderer
 * @param pool The pool
 */
static void box_particle_groups(
    box_renderer_t *r,
    box_gpu_emitter_t *pool)
{
    uint64_t vectors = (uint64_t)pool->capacity * 16;
    uint64_t scalars = (uint64_t)pool->capacity * 4;
    if (!pool->sim_group) {
        WGPUBindGroupEntry entries[BOX_PARTICLE_SIM_BINDINGS + 1] = {
            { .binding = 0, .buffer = pool->uniform, .size = BOX_PARTICLE_UNIFORM_SIZE },
            { .binding = 1, .buffer = pool->position, .size = vectors },
            { .binding = 2, .buffer = pool->velocity, .size = vectors },
            { .binding = 3, .buffer = pool->force, .size = vectors },
            { .binding = 4, .buffer = pool->color, .size = vectors },
            { .binding = 5, .buffer = pool->size, .size = scalars },
            { .binding = 6, .buffer = pool->dead, .size = scalars },
            { .binding = 7, .buffer = pool->counts,
                .size = BOX_PARTICLE_COUNTS * sizeof(uint32_t) },
            { .binding = 8, .buffer = pool->table, .size = (uint64_t)(pool->points ?
                pool->points : 1) * sizeof(box_particle_point_t) }
        };
        WGPUBindGroupDescriptor desc = {
            .layout = r->particles.sim_layout,
            .entryCount = BOX_PARTICLE_SIM_BINDINGS + 1,
            .entries = entries
        };
        pool->sim_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    }

    if (!pool->draw_group) {
        WGPUBindGroupEntry entries[BOX_PARTICLE_DRAW_BINDINGS] = {
            { .binding = 0, .buffer = pool->position, .size = vectors },
            { .binding = 1, .buffer = pool->velocity, .size = vectors },
            { .binding = 2, .buffer = pool->force, .size = vectors },
            { .binding = 3, .buffer = pool->color, .size = vectors },
            { .binding = 4, .buffer = pool->size, .size = scalars }
        };
        WGPUBindGroupDescriptor desc = {
            .layout = r->particles.draw_layout,
            .entryCount = BOX_PARTICLE_DRAW_BINDINGS,
            .entries = entries
        };
        pool->draw_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    }
}

/** Record the integrate and the spawn pass of one emitter.
 *
 * @param r The renderer
 * @param pool The pool
 * @param e The emitter of the packet
 * @param encoder The command encoder
 */
static void box_particle_dispatch(
    box_renderer_t *r,
    box_gpu_emitter_t *pool,
    const box_particle_emitter_t *e,
    WGPUCommandEncoder encoder)
{
    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "particles");
    wgpuComputePassEncoderSetPipeline(pass, r->particles.integrate);
    wgpuComputePassEncoderSetBindGroup(pass, 0, pool->sim_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,
        (pool->capacity + BOX_PARTICLE_GROUP - 1) / BOX_PARTICLE_GROUP, 1, 1);
    if (e->spawn) {
        wgpuComputePassEncoderSetPipeline(pass, r->particles.spawn);
        wgpuComputePassEncoderDispatchWorkgroups(pass,
            (e->spawn + BOX_PARTICLE_GROUP - 1) / BOX_PARTICLE_GROUP, 1, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}

void box_particle_simulate(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder)
{
    if (!p->particles.count && !r->particles.pools.count) {
        goto done;
    }

    if (!r->particles.pools.size) {
        box_vec_init(&r->particles.pools, sizeof(box_gpu_emitter_t));
        r->particles.sim_shader = BOX_SHADER_NONE;
        r->particles.draw_shader = BOX_SHADER_NONE;
    }

    uint32_t i;
    for (i = 0; i < r->particles.pools.count; i ++) {
        ((box_gpu_emitter_t*)box_vec_at(&r->particles.pools, i))->seen = false;
    }

    if (box_particle_ensure(r, p)) {
        for (i = 0; i < p->particles.count; i ++) {
            const box_particle_emitter_t *e = box_vec_at(&p->particles, i);
            if (!e->max) {
                continue;
            }

            box_gpu_emitter_t *pool = box_particle_pool(r, e);
            pool->seen = true;
            if (e->flags & BOX_PARTICLE_RESET) {
                box_particle_free_list(r, pool);
                wgpuCommandEncoderClearBuffer(encoder, pool->velocity, 0,
                    (uint64_t)pool->capacity * 16);
            }

            box_particle_table(r, pool, e, p);
            box_particle_groups(r, pool);
            wgpuQueueWriteBuffer(r->gpu_queue, pool->uniform, 0, e,
                BOX_PARTICLE_UNIFORM_SIZE);
            box_particle_dispatch(r, pool, e, encoder);
        }
    }

    i = 0;
    while (i < r->particles.pools.count) {
        box_gpu_emitter_t *pool = box_vec_at(&r->particles.pools, i);
        if (pool->seen) {
            i ++;
            continue;
        }

        box_particle_pool_fini(pool);
        r->particles.pools.count --;
        if (i != r->particles.pools.count) {
            *pool = *(box_gpu_emitter_t*)box_vec_at(&r->particles.pools,
                r->particles.pools.count);
        }
    }

done:
    return;
}

void box_particle_record(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTextureView attachment,
    bool hdr)
{
    if (!p->particles.count || !r->particles.pools.count || !r->v->view_group) {
        goto done;
    }

    WGPURenderPipeline pipeline = r->particles.pipeline[hdr ? 1 : 0];
    if (!pipeline) {
        goto done;
    }

    WGPURenderPassColorAttachment color = {
        .view = attachment,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = r->v->depth_view,
        .depthLoadOp = WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color,
        .depthStencilAttachment = &depth
    };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "particles");
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->view_group, 0, NULL);
    uint32_t i;
    for (i = 0; i < r->particles.pools.count; i ++) {
        box_gpu_emitter_t *pool = box_vec_at(&r->particles.pools, i);
        if (!pool->draw_group) {
            continue;
        }

        wgpuRenderPassEncoderSetBindGroup(pass, 1, pool->draw_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, BOX_PARTICLE_VERTICES, pool->capacity, 0, 0);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
done:
    return;
}

void box_particle_invalidate(
    box_renderer_t *r,
    uint32_t handle)
{
    if (handle == r->particles.sim_shader || handle == r->particles.draw_shader) {
        box_particle_pipelines_fini(r);
        r->particles.sim_shader = BOX_SHADER_NONE;
        r->particles.draw_shader = BOX_SHADER_NONE;
        r->particles.failed = false;
    }
}

void box_particle_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < r->particles.pools.count; i ++) {
        box_particle_pool_fini(box_vec_at(&r->particles.pools, i));
    }

    box_vec_fini(&r->particles.pools);
    box_particle_pipelines_fini(r);
    if (r->particles.sim_layout) wgpuBindGroupLayoutRelease(r->particles.sim_layout);
    if (r->particles.draw_layout) wgpuBindGroupLayoutRelease(r->particles.draw_layout);
    r->particles.sim_layout = NULL;
    r->particles.draw_layout = NULL;
    r->particles.sim_shader = BOX_SHADER_NONE;
    r->particles.draw_shader = BOX_SHADER_NONE;
    r->particles.failed = false;
}
