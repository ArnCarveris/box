/**
 * @file renderer/particle.c
 * @brief Particle pools of the renderer and the passes that fill and draw them.
 *
 * Every emitter of a packet owns a pool: one buffer per particle channel, a
 * dead list of the free slots and a counter block. The integrate pass ages and
 * moves the live particles and pushes the slots that expire onto the dead list,
 * the spawn pass pops as many slots as the packet asked for and fills them from
 * a hash of the emitter seed and the spawn index, and the sprite pass expands
 * every slot into a camera facing quad or into the mesh of its emitter. A pool
 * lives as long as its emitter keeps appearing in the packets.
 *
 * The sprite pass reads the block of the emitter next to its channels, so the
 * curves over the life of a particle are evaluated there, and it samples the
 * depth of the view a second time for the soft fade, which is why it takes the
 * depth as a read only attachment. A pool is drawn with the blended or the
 * additive pipeline of the target format, and the blended ones are drawn back
 * to front because their order is what their result depends on.
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

/** Write the counters and the indirect arguments a fresh pool starts from.
 *
 * Every slot is free, nothing is alive and both draws take no instance, which
 * is what an emitter that has not spawned yet draws. The vertex count of the
 * quad draw never changes; the index count of a billboard is written again when
 * the mesh of the emitter does.
 *
 * @param r The renderer
 * @param pool The pool
 */
static void box_particle_counters(
    box_renderer_t *r,
    box_gpu_emitter_t *pool)
{
    uint32_t counts[BOX_PARTICLE_COUNTS] = {0};
    counts[BOX_PARTICLE_COUNT_FREE] = pool->capacity;
    wgpuQueueWriteBuffer(r->gpu_queue, pool->counts, 0, counts, sizeof(counts));

    uint32_t args[BOX_PARTICLE_ARGS] = {0};
    args[BOX_PARTICLE_ARG_DRAW] = BOX_PARTICLE_VERTICES;
    wgpuQueueWriteBuffer(r->gpu_queue, pool->args, 0, args, sizeof(args));
    pool->indices = 0;
    pool->instances = 0;
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

    wgpuQueueWriteBuffer(r->gpu_queue, pool->slots, 0, dead,
        (size_t)pool->capacity * sizeof(uint32_t));
    free(dead);
    box_particle_counters(r, pool);
}

/** Release every buffer and bind group of a pool.
 *
 * @param pool The pool
 */
static void box_particle_pool_fini(
    box_gpu_emitter_t *pool)
{
    if (pool->sim_group) wgpuBindGroupRelease(pool->sim_group);
    if (pool->args_group) wgpuBindGroupRelease(pool->args_group);
    if (pool->draw_group) wgpuBindGroupRelease(pool->draw_group);
    if (pool->position) wgpuBufferRelease(pool->position);
    if (pool->velocity) wgpuBufferRelease(pool->velocity);
    if (pool->color) wgpuBufferRelease(pool->color);
    if (pool->extra) wgpuBufferRelease(pool->extra);
    if (pool->slots) wgpuBufferRelease(pool->slots);
    if (pool->counts) wgpuBufferRelease(pool->counts);
    if (pool->args) wgpuBufferRelease(pool->args);
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
    pool->color = box_particle_storage(r, (uint64_t)capacity * 8);
    pool->extra = box_particle_storage(r, (uint64_t)capacity * 4);
    pool->slots = box_particle_storage(r, (uint64_t)capacity * 8);
    pool->table = box_particle_storage(r, sizeof(box_particle_point_t));

    pool->counts = box_particle_storage(r, BOX_PARTICLE_COUNTS * sizeof(uint32_t));

    WGPUBufferDescriptor args = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst |
            WGPUBufferUsage_Indirect,
        .size = BOX_PARTICLE_ARGS * sizeof(uint32_t)
    };
    pool->args = wgpuDeviceCreateBuffer(r->device, &args);

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

    WGPUBindGroupLayoutEntry args = {
        .binding = 0,
        .visibility = WGPUShaderStage_Compute,
        .buffer = { .type = WGPUBufferBindingType_Storage }
    };
    WGPUBindGroupLayoutDescriptor args_desc = { .entryCount = 1, .entries = &args };
    r->particles.args_layout = wgpuDeviceCreateBindGroupLayout(r->device, &args_desc);

    WGPUBindGroupLayoutEntry draw[BOX_PARTICLE_DRAW_BINDINGS] = {0};
    for (i = 0; i < BOX_PARTICLE_DRAW_BINDINGS - 1; i ++) {
        draw[i].binding = i;
        draw[i].visibility = WGPUShaderStage_Vertex;
        draw[i].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    }

    draw[i].binding = i;
    draw[i].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    draw[i].buffer.type = WGPUBufferBindingType_Uniform;
    draw[i].buffer.minBindingSize = BOX_PARTICLE_UNIFORM_SIZE;

    WGPUBindGroupLayoutDescriptor draw_desc = {
        .entryCount = BOX_PARTICLE_DRAW_BINDINGS,
        .entries = draw
    };
    r->particles.draw_layout = wgpuDeviceCreateBindGroupLayout(r->device, &draw_desc);

    WGPUBindGroupLayoutEntry depth = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D
        }
    };
    WGPUBindGroupLayoutDescriptor depth_desc = { .entryCount = 1, .entries = &depth };
    r->particles.depth_layout = wgpuDeviceCreateBindGroupLayout(r->device, &depth_desc);
}

/** Release the pipelines the particle shaders built.
 *
 * @param r The renderer
 */
static void box_particle_pipelines_fini(
    box_renderer_t *r)
{
    uint32_t i, blend;
    for (i = 0; i < BOX_PARTICLE_FORMATS; i ++) {
        for (blend = 0; blend < BOX_PARTICLE_BLENDS; blend ++) {
            if (r->particles.pipeline[i][blend]) {
                wgpuRenderPipelineRelease(r->particles.pipeline[i][blend]);
            }

            if (r->particles.mesh[i][blend]) {
                wgpuRenderPipelineRelease(r->particles.mesh[i][blend]);
            }

            r->particles.pipeline[i][blend] = NULL;
            r->particles.mesh[i][blend] = NULL;
        }
    }

    if (r->particles.alloc) wgpuComputePipelineRelease(r->particles.alloc);
    if (r->particles.spawn) wgpuComputePipelineRelease(r->particles.spawn);
    if (r->particles.integrate) wgpuComputePipelineRelease(r->particles.integrate);
    if (r->particles.finish) wgpuComputePipelineRelease(r->particles.finish);
    r->particles.alloc = NULL;
    r->particles.spawn = NULL;
    r->particles.integrate = NULL;
    r->particles.finish = NULL;
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

    WGPUBindGroupLayout groups[2] = { r->particles.sim_layout,
        r->particles.args_layout };
    WGPUPipelineLayoutDescriptor step_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = groups
    };
    WGPUPipelineLayout step = wgpuDeviceCreatePipelineLayout(r->device, &step_desc);
    WGPUPipelineLayoutDescriptor args_desc = {
        .bindGroupLayoutCount = 2,
        .bindGroupLayouts = groups
    };
    WGPUPipelineLayout args = wgpuDeviceCreatePipelineLayout(r->device, &args_desc);

    WGPUComputePipelineDescriptor desc = {
        .layout = args,
        .compute = { .module = module }
    };
    desc.compute.entryPoint = (WGPUStringView){ "cs_finish", WGPU_STRLEN };
    r->particles.finish = wgpuDeviceCreateComputePipeline(r->device, &desc);
    desc.layout = step;
    desc.compute.entryPoint = (WGPUStringView){ "cs_alloc", WGPU_STRLEN };
    r->particles.alloc = wgpuDeviceCreateComputePipeline(r->device, &desc);
    desc.compute.entryPoint = (WGPUStringView){ "cs_spawn", WGPU_STRLEN };
    r->particles.spawn = wgpuDeviceCreateComputePipeline(r->device, &desc);
    desc.compute.entryPoint = (WGPUStringView){ "cs_integrate", WGPU_STRLEN };
    r->particles.integrate = wgpuDeviceCreateComputePipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(step);
    wgpuPipelineLayoutRelease(args);
    wgpuShaderModuleRelease(module);
done:
    return;
}

/** Build one sprite pipeline: a target format, a blend and a vertex source.
 *
 * The blended pipeline composites a particle over the image by its opacity and
 * the additive one adds it, which is what fire, sparks and magic are drawn
 * with. The mesh variant reads the vertices of the mesh of an emitter instead
 * of generating a quad, so it takes the vertex layout of the scene.
 *
 * @param r The renderer
 * @param module Module of the sprite shader
 * @param layout Pipeline layout of the pass
 * @param hdr The pass writes the float scene target
 * @param additive The pass adds a particle to the image instead of blending it
 * @param mesh The pass draws the mesh of the emitter
 * @return The pipeline
 */
static WGPURenderPipeline box_particle_build_draw(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    bool hdr,
    bool additive,
    bool mesh)
{
    WGPUBlendState blend = {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
            WGPUBlendFactor_OneMinusSrcAlpha },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
            WGPUBlendFactor_OneMinusSrcAlpha }
    };
    if (additive) {
        blend.color.dstFactor = WGPUBlendFactor_One;
        blend.alpha.srcFactor = WGPUBlendFactor_Zero;
        blend.alpha.dstFactor = WGPUBlendFactor_One;
    }

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
    WGPUVertexAttribute attributes[2] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 1 }
    };
    WGPUVertexBufferLayout vertices = {
        .arrayStride = BOX_VERTEX_FLOATS * sizeof(float),
        .stepMode = WGPUVertexStepMode_Vertex,
        .attributeCount = 2,
        .attributes = attributes
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = mesh ? "vs_mesh" : "vs_main", .length = WGPU_STRLEN },
            .bufferCount = mesh ? 1 : 0,
            .buffers = mesh ? &vertices : NULL
        },
        .fragment = &fragment,
        .depthStencil = &depth,
        .primitive = {
            .topology = mesh ? WGPUPrimitiveTopology_TriangleList :
                WGPUPrimitiveTopology_TriangleStrip
        },
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
            WGPUBindGroupLayout groups[4] = { r->view_layout, r->particles.draw_layout,
                r->env_layout, r->particles.depth_layout };
            WGPUPipelineLayoutDescriptor layout_desc = {
                .bindGroupLayoutCount = 4,
                .bindGroupLayouts = groups
            };
            WGPUPipelineLayout layout =
                wgpuDeviceCreatePipelineLayout(r->device, &layout_desc);
            uint32_t i, blend;
            for (i = 0; i < BOX_PARTICLE_FORMATS; i ++) {
                for (blend = 0; blend < BOX_PARTICLE_BLENDS; blend ++) {
                    r->particles.pipeline[i][blend] = box_particle_build_draw(
                        r, module, layout, i != 0, blend != 0, false);
                    r->particles.mesh[i][blend] = box_particle_build_draw(
                        r, module, layout, i != 0, blend != 0, true);
                }
            }

            wgpuPipelineLayoutRelease(layout);
            wgpuShaderModuleRelease(module);
        }
    }

    result = r->particles.spawn && r->particles.integrate &&
        r->particles.alloc && r->particles.finish &&
        r->particles.pipeline[0][0] && r->particles.pipeline[1][1] &&
        r->particles.mesh[0][0] && r->particles.mesh[1][1];
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
    uint64_t pairs = (uint64_t)pool->capacity * 8;
    uint64_t scalars = (uint64_t)pool->capacity * 4;
    if (!pool->sim_group) {
        WGPUBindGroupEntry entries[BOX_PARTICLE_SIM_BINDINGS + 1] = {
            { .binding = 0, .buffer = pool->uniform, .size = BOX_PARTICLE_UNIFORM_SIZE },
            { .binding = 1, .buffer = pool->position, .size = vectors },
            { .binding = 2, .buffer = pool->velocity, .size = vectors },
            { .binding = 3, .buffer = pool->color, .size = pairs },
            { .binding = 4, .buffer = pool->extra, .size = scalars },
            { .binding = 5, .buffer = pool->slots, .size = pairs },
            { .binding = 6, .buffer = pool->counts,
                .size = BOX_PARTICLE_COUNTS * sizeof(uint32_t) },
            { .binding = 7, .buffer = pool->table, .size = (uint64_t)(pool->points ?
                pool->points : 1) * sizeof(box_particle_point_t) }
        };
        WGPUBindGroupDescriptor desc = {
            .layout = r->particles.sim_layout,
            .entryCount = BOX_PARTICLE_SIM_BINDINGS + 1,
            .entries = entries
        };
        pool->sim_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    }

    if (!pool->args_group) {
        WGPUBindGroupEntry entry = { .binding = 0, .buffer = pool->args,
            .size = BOX_PARTICLE_ARGS * sizeof(uint32_t) };
        WGPUBindGroupDescriptor desc = {
            .layout = r->particles.args_layout,
            .entryCount = 1,
            .entries = &entry
        };
        pool->args_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    }

    if (!pool->draw_group) {
        WGPUBindGroupEntry entries[BOX_PARTICLE_DRAW_BINDINGS] = {
            { .binding = 0, .buffer = pool->position, .size = vectors },
            { .binding = 1, .buffer = pool->velocity, .size = vectors },
            { .binding = 2, .buffer = pool->color, .size = pairs },
            { .binding = 3, .buffer = pool->extra, .size = scalars },
            { .binding = 4, .buffer = pool->slots, .size = pairs },
            { .binding = 5, .buffer = pool->uniform, .size = BOX_PARTICLE_UNIFORM_SIZE }
        };
        WGPUBindGroupDescriptor desc = {
            .layout = r->particles.draw_layout,
            .entryCount = BOX_PARTICLE_DRAW_BINDINGS,
            .entries = entries
        };
        pool->draw_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    }
}

/** Write the counters and the arguments the frame of a pool starts from.
 *
 * An added pool compacts its live particles into a list the frame appends to,
 * so its cursor is cleared here and the finish pass writes what the draw takes
 * from it. A blended pool is drawn over its slots instead, as many as the pool
 * has ever used, which only changes while the pool is filling.
 *
 * @param r The renderer
 * @param pool The pool
 * @param span Slots of the pool that have ever held a particle
 */
static void box_particle_frame(
    box_renderer_t *r,
    box_gpu_emitter_t *pool,
    uint32_t span)
{
    if (pool->additive) {
        uint32_t zero = 0;
        wgpuQueueWriteBuffer(r->gpu_queue, pool->counts,
            BOX_PARTICLE_COUNT_ALIVE * sizeof(uint32_t), &zero, sizeof(zero));
        goto done;
    }

    if (pool->instances != span) {
        pool->instances = span;
        wgpuQueueWriteBuffer(r->gpu_queue, pool->args,
            (BOX_PARTICLE_ARG_DRAW + 1) * sizeof(uint32_t), &span, sizeof(span));
        wgpuQueueWriteBuffer(r->gpu_queue, pool->args,
            (BOX_PARTICLE_ARG_INDEXED + 1) * sizeof(uint32_t), &span, sizeof(span));
    }

done:
    return;
}

/** Record the integrate and the spawn pass of one emitter.
 *
 * A paused emitter records neither: its pool is left exactly as the frame that
 * paused it left it and is still drawn, which is what freezes an effect in
 * place instead of letting it run on without being seen.
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
    uint32_t span = e->span < pool->capacity ? e->span : pool->capacity;
    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "particles");
    wgpuComputePassEncoderSetBindGroup(pass, 0, pool->sim_group, 0, NULL);
    if (span) {
        wgpuComputePassEncoderSetPipeline(pass, r->particles.integrate);
        wgpuComputePassEncoderDispatchWorkgroups(pass,
            (span + BOX_PARTICLE_GROUP - 1) / BOX_PARTICLE_GROUP, 1, 1);
    }

    if (e->spawn) {
        wgpuComputePassEncoderSetPipeline(pass, r->particles.alloc);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, r->particles.spawn);
        wgpuComputePassEncoderDispatchWorkgroups(pass,
            (e->spawn + BOX_PARTICLE_GROUP - 1) / BOX_PARTICLE_GROUP, 1, 1);
    }

    if (pool->additive) {
        wgpuComputePassEncoderSetBindGroup(pass, 1, pool->args_group, 0, NULL);
        wgpuComputePassEncoderSetPipeline(pass, r->particles.finish);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
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
            pool->appearance = e->appearance;
            pool->mesh = e->mesh;
            pool->additive = (e->flags & BOX_PARTICLE_ADDITIVE) != 0;
            pool->origin[0] = e->origin[0];
            pool->origin[1] = e->origin[1];
            pool->origin[2] = e->origin[2];
            if (e->flags & BOX_PARTICLE_RESET) {
                box_particle_free_list(r, pool);
                wgpuCommandEncoderClearBuffer(encoder, pool->velocity, 0,
                    (uint64_t)pool->capacity * 16);
            }

            box_particle_table(r, pool, e, p);
            box_particle_groups(r, pool);
            wgpuQueueWriteBuffer(r->gpu_queue, pool->uniform, 0, e,
                BOX_PARTICLE_UNIFORM_SIZE);
            pool->stepped = !(e->flags & BOX_PARTICLE_PAUSED);
            if (pool->stepped) {
                box_particle_frame(r, pool,
                    e->span < pool->capacity ? e->span : pool->capacity);
                box_particle_dispatch(r, pool, e, encoder);
            }
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

/** Depth group of the view being recorded, built again when its target changed.
 *
 * @param r The renderer
 * @return The bind group, NULL when the view has no sampled depth
 */
static WGPUBindGroup box_particle_depth_group(
    box_renderer_t *r)
{
    if (!r->v->depth_sample) {
        goto done;
    }

    if (r->particles.depth_for != r->v->depth_sample) {
        if (r->particles.depth_group) {
            wgpuBindGroupRelease(r->particles.depth_group);
        }

        WGPUBindGroupEntry entry = { .binding = 0, .textureView = r->v->depth_sample };
        WGPUBindGroupDescriptor desc = {
            .layout = r->particles.depth_layout,
            .entryCount = 1,
            .entries = &entry
        };
        r->particles.depth_group = wgpuDeviceCreateBindGroup(r->device, &desc);
        r->particles.depth_for = r->v->depth_sample;
    }

done:
    return r->particles.depth_group;
}

/** Order the pools of a view: the blended ones back to front, the added ones last.
 *
 * Blending is not commutative, so a pool that sits behind another has to be
 * drawn first; adding is, so the additive pools follow in any order and never
 * have to be mixed into the sorted run.
 *
 * @param r The renderer
 * @param view The view being recorded
 */
static void box_particle_sort(
    box_renderer_t *r,
    const box_view_t *view)
{
    if (!r->particles.order.size) {
        box_vec_init(&r->particles.order, sizeof(box_particle_order_t));
    }

    box_vec_clear(&r->particles.order);
    uint32_t i;
    for (i = 0; i < r->particles.pools.count; i ++) {
        box_gpu_emitter_t *pool = box_vec_at(&r->particles.pools, i);
        if (!pool->draw_group) {
            continue;
        }

        float dx = pool->origin[0] - view->eye[0];
        float dy = pool->origin[1] - view->eye[1];
        float dz = pool->origin[2] - view->eye[2];
        box_particle_order_t *entry = box_vec_push(&r->particles.order);
        entry->pool = i;
        entry->distance = pool->additive ? -1.0f : dx * dx + dy * dy + dz * dz;
    }

    for (i = 1; i < r->particles.order.count; i ++) {
        box_particle_order_t entry = *(box_particle_order_t*)
            box_vec_at(&r->particles.order, i);
        uint32_t j = i;
        while (j > 0) {
            box_particle_order_t *before = box_vec_at(&r->particles.order, j - 1);
            if (before->distance >= entry.distance) {
                break;
            }

            *(box_particle_order_t*)box_vec_at(&r->particles.order, j) = *before;
            j --;
        }

        *(box_particle_order_t*)box_vec_at(&r->particles.order, j) = entry;
    }
}

/** Draw one pool, as a quad per particle or as the mesh of its emitter.
 *
 * @param r The renderer
 * @param pool The pool
 * @param pass The render pass
 * @param hdr The pass writes the float scene target
 */
static void box_particle_draw(
    box_renderer_t *r,
    box_gpu_emitter_t *pool,
    WGPURenderPassEncoder pass,
    bool hdr)
{
    uint32_t format = hdr ? 1 : 0;
    uint32_t blend = pool->additive ? 1 : 0;
    box_gpu_mesh_t *mesh = NULL;
    if (pool->appearance == BOX_PARTICLE_DRAW_BILLBOARD && pool->mesh) {
        mesh = box_scene_mesh(r, pool->mesh);
    }

    wgpuRenderPassEncoderSetBindGroup(pass, 1, pool->draw_group, 0, NULL);
    if (mesh && mesh->index_count && mesh->vertices && mesh->indices) {
        if (pool->indices != mesh->index_count) {
            pool->indices = mesh->index_count;
            wgpuQueueWriteBuffer(r->gpu_queue, pool->args,
                BOX_PARTICLE_ARG_INDEXED * sizeof(uint32_t),
                &pool->indices, sizeof(uint32_t));
        }

        wgpuRenderPassEncoderSetPipeline(pass, r->particles.mesh[format][blend]);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, mesh->indices,
            WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexedIndirect(pass, pool->args,
            BOX_PARTICLE_ARG_INDEXED * sizeof(uint32_t));
    } else {
        wgpuRenderPassEncoderSetPipeline(pass, r->particles.pipeline[format][blend]);
        wgpuRenderPassEncoderDrawIndirect(pass, pool->args,
            BOX_PARTICLE_ARG_DRAW * sizeof(uint32_t));
    }
}

void box_particle_record(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTextureView attachment,
    bool hdr)
{
    if (!p->particles.count || !r->particles.pools.count || !r->v->view_group) {
        goto done;
    }

    WGPUBindGroup depth_group = box_particle_depth_group(r);
    if (!r->particles.pipeline[hdr ? 1 : 0][0] || !r->v->env_group || !depth_group) {
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
        .depthReadOnly = true
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color,
        .depthStencilAttachment = &depth
    };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "particles");
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->view_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 2, r->v->env_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, depth_group, 0, NULL);
    box_particle_sort(r, view);
    uint32_t i;
    for (i = 0; i < r->particles.order.count; i ++) {
        box_particle_order_t *entry = box_vec_at(&r->particles.order, i);
        box_particle_draw(r, box_vec_at(&r->particles.pools, entry->pool), pass, hdr);
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
    box_vec_fini(&r->particles.order);
    box_particle_pipelines_fini(r);
    if (r->particles.depth_group) wgpuBindGroupRelease(r->particles.depth_group);
    if (r->particles.sim_layout) wgpuBindGroupLayoutRelease(r->particles.sim_layout);
    if (r->particles.args_layout) wgpuBindGroupLayoutRelease(r->particles.args_layout);
    if (r->particles.draw_layout) wgpuBindGroupLayoutRelease(r->particles.draw_layout);
    if (r->particles.depth_layout) wgpuBindGroupLayoutRelease(r->particles.depth_layout);
    r->particles.depth_group = NULL;
    r->particles.depth_for = NULL;
    r->particles.sim_layout = NULL;
    r->particles.args_layout = NULL;
    r->particles.draw_layout = NULL;
    r->particles.depth_layout = NULL;
    r->particles.sim_shader = BOX_SHADER_NONE;
    r->particles.draw_shader = BOX_SHADER_NONE;
    r->particles.failed = false;
}
