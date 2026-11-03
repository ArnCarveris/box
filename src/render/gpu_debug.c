/**
 * @file render/gpu_debug.c
 * @brief Debug passes of the webgpu backend: what View.debug draws over a view after its effects,
 * the infinite grid of etc/shaders/debug_grid.wgsl and the wireframe of etc/shaders/
 * debug_wire.wgsl.
 *
 * A view with a debug item on records one render pass after its effects that loads the color
 * of the target and reads the depth of the view; without debug items it records nothing, and
 * the captures and bakes never do.
 *
 * A grid draw binds its uniform in group 1, a wire draw in group 3, from one growing buffer of
 * the packet; a wire draw binds the vertices and indices of its mesh in group 1, kept with the
 * mesh, the depth only instance group in group 2 and a shared sequence index buffer.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes the uniform of a draw takes in the buffer: the offset alignment of webgpu */
#define BOX_GPU_DEBUG_UNIFORM_STRIDE (256u)

/** Smallest buffer of the draws of a packet in bytes */
#define BOX_GPU_DEBUG_BUFFER_MIN (4096u)

/** Width of the major lines and the axes over the width of the minor lines */
#define BOX_GPU_DEBUG_MAJOR_WIDTH (1.5f)

/** Uniform of a grid draw, matching Grid in debug_grid.wgsl */
typedef struct box_gpu_grid_uniforms_t {
    vec4 color;                     /**< Minor lines, linear, straight alpha */
    vec4 major_color;               /**< Major lines, linear, straight alpha */
    vec4 params;                    /**< Height, cell size, fade distance, line width */
    vec4 config;                    /**< Cells between major lines, width of the major lines,
                                     *   two unused */
} box_gpu_grid_uniforms_t;

/** Uniform of a wire draw, matching Wire in debug_wire.wgsl */
typedef struct box_gpu_wire_uniforms_t {
    vec4 color;                     /**< Lines, linear, straight alpha */
    vec4 params;                    /**< Width of the lines in pixels, three unused */
} box_gpu_wire_uniforms_t;

/** Kinds of debug pipelines, the high bits of their key in the pipeline cache */
typedef enum box_gpu_debug_kind_t {
    BOX_GPU_DEBUG_GRID = 0,         /**< The grid */
    BOX_GPU_DEBUG_WIRE_OVERLAY = 1, /**< Wireframe over the shaded image, WireframeOverlay */
    BOX_GPU_DEBUG_WIRE_ONLY = 2     /**< Wireframe on its own, WireframeOnly */
} box_gpu_debug_kind_t;

/** Constant depth bias of the overlay wireframe toward the camera, in the smallest depth steps */
#define BOX_GPU_DEBUG_WIRE_BIAS (4)

/** Slope scaled depth bias of the overlay wireframe toward the camera */
#define BOX_GPU_DEBUG_WIRE_SLOPE_BIAS (1.0f)

struct box_gpu_debug_t {
    WGPUBindGroupLayout layout;     /**< Uniform of a grid draw */
    WGPUPipelineLayout pipeline_layout; /**< View group and the group of a draw */
    WGPUBindGroupLayout wire_mesh_layout; /**< Vertices and indices of a mesh, group 1 of a wire
                                     *   draw */
    WGPUBindGroupLayout wire_layout; /**< Uniform of the wire draws of a view, group 3 */
    WGPUPipelineLayout wire_pipeline_layout; /**< View, mesh, depth only instance and wire
                                     *   uniform groups */
    ecs_map_t pipelines;            /**< Kind << 32 | color format to the WGPURenderPipeline */
    int32_t grid_revision;          /**< Revision of the grid shader of the pipelines */
    int32_t wire_revision;          /**< Revision of the wire shader of the pipelines */
    WGPUBuffer sequence;            /**< Index buffer 0, 1, 2, ... of the wire draws, NULL until
                                     *   the first one */
    uint32_t sequence_count;        /**< Indices in the sequence */
    WGPUBuffer buffer;              /**< Uniforms of the draws of the packet, NULL until the
                                     *   first draw */
    uint64_t size;                  /**< Bytes of the buffer */
    uint64_t used;                  /**< Bytes of the buffer the draws of the packet took */
};

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_gpu_debug_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Convert a display encoded color to linear light with a straight alpha.
 *
 * @param c The color
 * @param out Receives the linear color
 */
static void box_gpu_debug_color(
    rgba c,
    vec4 out)
{
    out[0] = box_gpu_debug_linear(c.r);
    out[1] = box_gpu_debug_linear(c.g);
    out[2] = box_gpu_debug_linear(c.b);
    out[3] = (float)c.a / 255.0f;
}

void box_gpu_debug_init(
    box_gpu_t *gpu)
{
    box_gpu_debug_t *d = ecs_os_calloc_t(box_gpu_debug_t);
    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(box_gpu_grid_uniforms_t)
        }
    };
    d->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 1, .entries = &entry });
    WGPUBindGroupLayout groups[2] = { gpu->view_layout, d->layout };
    d->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    WGPUBindGroupLayoutEntry mesh_entries[2] = {{
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    }, {
        .binding = 1,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    }};
    d->wire_mesh_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = mesh_entries });
    WGPUBindGroupLayoutEntry wire_entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(box_gpu_wire_uniforms_t)
        }
    };
    d->wire_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 1, .entries = &wire_entry });
    WGPUBindGroupLayout wire_groups[4] = {
        gpu->view_layout, d->wire_mesh_layout, gpu->transform_layout, d->wire_layout
    };
    d->wire_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 4,
            .bindGroupLayouts = wire_groups
        });
    ecs_map_init(&d->pipelines, NULL);
    gpu->debug = d;
}

/** Release the debug pipelines of some kinds, in every format.
 *
 * A released entry stays in the cache with a NULL value, so that a pipeline of another kind
 * that a caller already holds is not touched when one shader is recompiled.
 *
 * @param d The debug state
 * @param kinds Bit 1 << kind for each kind to release
 */
static void box_gpu_debug_pipelines_free(
    box_gpu_debug_t *d,
    uint32_t kinds)
{
    ecs_map_iter_t it = ecs_map_iter(&d->pipelines);
    while (ecs_map_next(&it)) {
        uint32_t kind = (uint32_t)(ecs_map_key(&it) >> 32);
        WGPURenderPipeline pipeline = (WGPURenderPipeline)(uintptr_t)ecs_map_value(&it);
        if (!(kinds & (1u << kind)) || !pipeline) {
            continue;
        }

        wgpuRenderPipelineRelease(pipeline);
        ecs_map_value(&it) = 0;
    }
}

void box_gpu_debug_fini(
    box_gpu_t *gpu)
{
    box_gpu_debug_t *d = gpu->debug;
    if (!d) {
        goto done;
    }

    box_gpu_debug_pipelines_free(d, UINT32_MAX);
    ecs_map_fini(&d->pipelines);
    if (d->buffer) {
        wgpuBufferRelease(d->buffer);
    }

    if (d->sequence) {
        wgpuBufferRelease(d->sequence);
    }

    wgpuPipelineLayoutRelease(d->wire_pipeline_layout);
    wgpuBindGroupLayoutRelease(d->wire_layout);
    wgpuBindGroupLayoutRelease(d->wire_mesh_layout);
    wgpuPipelineLayoutRelease(d->pipeline_layout);
    wgpuBindGroupLayoutRelease(d->layout);
    ecs_os_free(d);
    gpu->debug = NULL;
done:
    return;
}

void box_gpu_debug_begin(
    box_gpu_t *gpu)
{
    if (gpu->debug) {
        gpu->debug->used = 0;
    }
}

bool box_gpu_debug_enabled(
    const box_render_view_t *v)
{
    return v->debug.grid.enabled || v->debug.wireframe != WireframeOff;
}

/** Key of a debug pipeline in the cache.
 *
 * @param kind The kind of pipeline
 * @param format The format of the color target
 * @return The key
 */
static ecs_map_key_t box_gpu_debug_key(
    box_gpu_debug_kind_t kind,
    WGPUTextureFormat format)
{
    return ((ecs_map_key_t)kind << 32) | (ecs_map_key_t)(uint32_t)format;
}

/** Get the grid pipeline that draws into a color format, created on first use and again when
 * the shader was recompiled.
 *
 * It tests the depth of the view with Greater and does not write it; the premultiplied color
 * blends over the target, and so does its alpha.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @return The pipeline, NULL until the shader exists
 */
static WGPURenderPipeline box_gpu_debug_grid_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_debug_t *d = gpu->debug;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_DEBUG_GRID);
    WGPURenderPipeline result = NULL;
    ecs_map_val_t *cached;
    ecs_map_key_t key;
    if (!shader || !shader->module) {
        goto done;
    }

    if (d->grid_revision != shader->revision) {
        box_gpu_debug_pipelines_free(d, 1u << BOX_GPU_DEBUG_GRID);
        d->grid_revision = shader->revision;
    }

    key = box_gpu_debug_key(BOX_GPU_DEBUG_GRID, format);
    cached = ecs_map_get(&d->pipelines, key);
    if (cached && *cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    WGPUBlendState blend = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
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
        .module = shader->module,
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
    result = wgpuDeviceCreateRenderPipeline(gpu->device, &(WGPURenderPipelineDescriptor){
        .layout = d->pipeline_layout,
        .vertex = {
            .module = shader->module,
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
    });
    *ecs_map_ensure(&d->pipelines, key) = (ecs_map_val_t)(uintptr_t)result;
done:
    return result;
}

/** Get the wire pipeline of a mode that draws into a color format, created on first use and
 * again when the shader was recompiled, from the same cache as the grid.
 *
 * The overlay tests the depth of the view with GreaterEqual, biased toward the camera, so what
 * is in front hides the lines; WireframeOnly compares Always and shows every line. Neither
 * writes the depth or culls faces; the premultiplied color and alpha blend over the target.
 *
 * @param gpu The backend
 * @param mode The mode, WireframeOverlay or WireframeOnly
 * @param format The format of the color target
 * @return The pipeline, NULL until the shader exists
 */
static WGPURenderPipeline box_gpu_debug_wire_pipeline(
    box_gpu_t *gpu,
    WireframeMode mode,
    WGPUTextureFormat format)
{
    box_gpu_debug_t *d = gpu->debug;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_DEBUG_WIRE);
    bool overlay = mode == WireframeOverlay;
    WGPURenderPipeline result = NULL;
    ecs_map_val_t *cached;
    ecs_map_key_t key;
    if (!shader || !shader->module) {
        goto done;
    }

    if (d->wire_revision != shader->revision) {
        box_gpu_debug_pipelines_free(d,
            (1u << BOX_GPU_DEBUG_WIRE_OVERLAY) | (1u << BOX_GPU_DEBUG_WIRE_ONLY));
        d->wire_revision = shader->revision;
    }

    key = box_gpu_debug_key(
        overlay ? BOX_GPU_DEBUG_WIRE_OVERLAY : BOX_GPU_DEBUG_WIRE_ONLY, format);
    cached = ecs_map_get(&d->pipelines, key);
    if (cached && *cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 0
    };
    WGPUVertexBufferLayout slots = {
        .stepMode = WGPUVertexStepMode_Instance,
        .arrayStride = sizeof(uint32_t),
        .attributeCount = 1,
        .attributes = &slot_attr
    };
    WGPUBlendState blend = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
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
        .module = shader->module,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_False,
        .depthCompare = overlay
            ? WGPUCompareFunction_GreaterEqual : WGPUCompareFunction_Always,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always },
        .depthBias = overlay ? BOX_GPU_DEBUG_WIRE_BIAS : 0,
        .depthBiasSlopeScale = overlay ? BOX_GPU_DEBUG_WIRE_SLOPE_BIAS : 0.0f
    };
    result = wgpuDeviceCreateRenderPipeline(gpu->device, &(WGPURenderPipelineDescriptor){
        .layout = d->wire_pipeline_layout,
        .vertex = {
            .module = shader->module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN },
            .bufferCount = 1,
            .buffers = &slots
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    });
    *ecs_map_ensure(&d->pipelines, key) = (ecs_map_val_t)(uintptr_t)result;
done:
    return result;
}

/** Make room for the uniform of a draw in the buffer of the packet, replacing the buffer with a
 * larger one when it does not fit; earlier draws keep the old one through their bind groups.
 *
 * @param gpu The backend
 * @return Offset of the draw in the buffer
 */
static uint64_t box_gpu_debug_reserve(
    box_gpu_t *gpu)
{
    box_gpu_debug_t *d = gpu->debug;
    uint64_t offset = d->used;
    if (!d->buffer || offset + BOX_GPU_DEBUG_UNIFORM_STRIDE > d->size) {
        uint64_t size = d->size ? d->size * 2 : BOX_GPU_DEBUG_BUFFER_MIN;
        if (d->buffer) {
            wgpuBufferRelease(d->buffer);
        }

        d->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = size
        });
        d->size = size;
        offset = 0;
    }

    d->used = offset + BOX_GPU_DEBUG_UNIFORM_STRIDE;

    return offset;
}

/** Create the bind group of a grid draw with its uniform, written into the buffer of the
 * packet.
 *
 * @param gpu The backend
 * @param s The grid settings of the view
 * @return The bind group
 */
static WGPUBindGroup box_gpu_debug_grid_group(
    box_gpu_t *gpu,
    const GridSettings *s)
{
    box_gpu_debug_t *d = gpu->debug;
    box_gpu_grid_uniforms_t u;
    uint64_t offset = box_gpu_debug_reserve(gpu);
    ecs_os_memset_t(&u, 0, box_gpu_grid_uniforms_t);
    box_gpu_debug_color(s->color, u.color);
    box_gpu_debug_color(s->major_color, u.major_color);
    u.params[0] = s->height;
    u.params[1] = s->cell_size;
    u.params[2] = s->fade_distance;
    u.params[3] = s->line_width;
    u.config[0] = (float)(s->major_every > 0 ? s->major_every : 0);
    u.config[1] = s->line_width * BOX_GPU_DEBUG_MAJOR_WIDTH;
    wgpuQueueWriteBuffer(gpu->queue, d->buffer, offset, &u, sizeof(u));
    WGPUBindGroupEntry entry = {
        .binding = 0,
        .buffer = d->buffer,
        .offset = offset,
        .size = sizeof(box_gpu_grid_uniforms_t)
    };

    return wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = d->layout,
        .entryCount = 1,
        .entries = &entry
    });
}

/** Create the bind group of the wire draws of a view with its uniform, written into the buffer
 * of the packet.
 *
 * @param gpu The backend
 * @param s The debug settings of the view
 * @return The bind group
 */
static WGPUBindGroup box_gpu_debug_wire_group(
    box_gpu_t *gpu,
    const ViewDebug *s)
{
    box_gpu_debug_t *d = gpu->debug;
    box_gpu_wire_uniforms_t u;
    uint64_t offset = box_gpu_debug_reserve(gpu);
    ecs_os_memset_t(&u, 0, box_gpu_wire_uniforms_t);
    box_gpu_debug_color(s->wireframe_color, u.color);
    u.params[0] = s->wireframe_width;
    wgpuQueueWriteBuffer(gpu->queue, d->buffer, offset, &u, sizeof(u));
    WGPUBindGroupEntry entry = {
        .binding = 0,
        .buffer = d->buffer,
        .offset = offset,
        .size = sizeof(box_gpu_wire_uniforms_t)
    };

    return wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = d->wire_layout,
        .entryCount = 1,
        .entries = &entry
    });
}

/** Get the group of a mesh with its vertices and indices as the storage buffers of its wire
 * draws, created on first use and kept with the mesh, which releases it.
 *
 * A mesh whose vertices exceed the storage buffer binding size of the device is not wired; it
 * logs one error.
 *
 * @param gpu The backend
 * @param m The mesh
 * @return The bind group, NULL when the mesh cannot be wired
 */
static WGPUBindGroup box_gpu_debug_mesh_group(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m)
{
    if (m->wire || !m->wireable) {
        if (!m->wireable && !m->wire_reported) {
            ecs_err("mesh exceeds the storage buffer binding size, it is not wired");
            m->wire_reported = true;
        }

        goto done;
    }

    WGPUBindGroupEntry entries[2] = {
        {
            .binding = 0,
            .buffer = m->vertices,
            .size = wgpuBufferGetSize(m->vertices)
        },
        {
            .binding = 1,
            .buffer = m->indices,
            .size = wgpuBufferGetSize(m->indices)
        }
    };
    m->wire = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->debug->wire_mesh_layout,
        .entryCount = 2,
        .entries = entries
    });
done:
    return m->wire;
}

void box_gpu_debug_mesh_free(
    box_gpu_mesh_t *m)
{
    if (m->wire) {
        wgpuBindGroupRelease(m->wire);
        m->wire = NULL;
    }
}

/** Check whether the wireframe draws a batch: a mesh batch, opaque or blended.
 *
 * @param b The batch
 * @return Whether the batch is wired
 */
static bool box_gpu_debug_wired(
    const box_render_batch_t *b)
{
    return b->pipeline == BOX_RENDER_SHADER_MESH;
}

/** Make the sequence index buffer hold at least a number of indices, replacing it with a
 * larger one filled with 0, 1, 2, ...; draws recorded before keep the old one.
 *
 * @param gpu The backend
 * @param count Indices it must hold
 * @return Whether the sequence holds them
 */
static bool box_gpu_debug_sequence(
    box_gpu_t *gpu,
    uint32_t count)
{
    box_gpu_debug_t *d = gpu->debug;
    WGPULimits limits = WGPU_LIMITS_INIT;
    uint32_t size = d->sequence_count ? d->sequence_count : 1024u;
    uint32_t *data;
    uint32_t i;
    bool result = d->sequence && count <= d->sequence_count;
    if (result || !count) {
        goto done;
    }

    while (size < count) {
        size = size > UINT32_MAX / 2 ? count : size * 2;
    }

    if (wgpuDeviceGetLimits(gpu->device, &limits) != WGPUStatus_Success) {
        goto done;
    }

    if ((uint64_t)size * sizeof(uint32_t) > limits.maxBufferSize) {
        size = count;
        if ((uint64_t)size * sizeof(uint32_t) > limits.maxBufferSize) {
            goto done;
        }
    }

    if (d->sequence) {
        wgpuBufferRelease(d->sequence);
    }

    data = ecs_os_malloc_n(uint32_t, size);
    for (i = 0; i < size; i ++) {
        data[i] = i;
    }

    d->sequence = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)size * sizeof(uint32_t)
    });
    wgpuQueueWriteBuffer(gpu->queue, d->sequence, 0, data, (size_t)size * sizeof(uint32_t));
    ecs_os_free(data);
    d->sequence_count = size;
    result = true;
done:
    return result;
}

/** Record the wire draws of a view in its debug pass: every mesh batch of its cull view with
 * the early and the late arguments the cull pass wrote, as box_gpu_draw_batches draws them.
 *
 * @param gpu The backend
 * @param pass The debug pass, with the view group bound
 * @param pipeline The wire pipeline
 * @param packet The packet
 * @param v The view
 * @param slot Slot of the view in the view buffer
 */
static void box_gpu_debug_wire_draws(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    WGPURenderPipeline pipeline,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t slot)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    WGPUBindGroup instances = NULL, mesh = NULL, wire = NULL;
    uint32_t longest = 0;
    int32_t i;
    for (i = 0; cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        const box_gpu_mesh_t *m = box_gpu_debug_wired(b)
            ? ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh) : NULL;
        if (m && m->wireable && m->index_count > longest) {
            longest = m->index_count;
        }
    }

    if (!longest || !box_gpu_debug_sequence(gpu, longest)) {
        goto done;
    }

    wire = box_gpu_debug_wire_group(gpu, &v->debug);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, wire, 0, NULL);
    for (i = 0; i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        if (!box_gpu_debug_wired(b)) {
            continue;
        }

        box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
        WGPUBindGroup group = m ? box_gpu_debug_mesh_group(gpu, m) : NULL;
        if (!group) {
            continue;
        }

        if (group != mesh) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, group, 0, NULL);
            mesh = group;
        }

        if (box_gpu_cull_draw_wire(gpu, pass, packet, d, 2, false, gpu->debug->sequence,
            &instances))
        {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }

        if (box_gpu_cull_draw_wire(gpu, pass, packet, d, 2, true, gpu->debug->sequence,
            &instances))
        {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }
    }

    wgpuBindGroupRelease(wire);
done:
    return;
}

void box_gpu_debug_record(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    WGPUTextureView color,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot)
{
    WGPURenderPipeline grid = NULL, wire = NULL;
    WGPUBindGroup grid_group = NULL;
    if (!gpu->debug || !color || !t->depth_view) {
        goto done;
    }

    if (v->debug.grid.enabled) {
        grid = box_gpu_debug_grid_pipeline(gpu, t->format);
    }

    if (v->debug.wireframe == WireframeOverlay || v->debug.wireframe == WireframeOnly) {
        wire = box_gpu_debug_wire_pipeline(gpu, v->debug.wireframe, t->format);
    }

    if (!grid && !wire) {
        goto done;
    }

    WGPURenderPassColorAttachment attachment = {
        .view = color,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = t->depth_view,
        .depthLoadOp = WGPULoadOp_Undefined,
        .depthStoreOp = WGPUStoreOp_Undefined,
        .depthReadOnly = true
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment,
        .depthStencilAttachment = &depth
    };
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    if (grid) {
        grid_group = box_gpu_debug_grid_group(gpu, &v->debug.grid);
        wgpuRenderPassEncoderSetPipeline(pass, grid);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, grid_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
    }

    if (wire) {
        box_gpu_debug_wire_draws(gpu, pass, wire, packet, v, slot);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    if (grid_group) {
        wgpuBindGroupRelease(grid_group);
    }

    box_gpu_count(gpu, BOX_GPU_STAT_DEBUG_PASS);
done:
    return;
}

#endif
