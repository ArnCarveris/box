/**
 * @file render/gpu_debug.c
 * @brief Debug passes of the webgpu backend: what View.debug draws over a view after its effects,
 * the infinite grid of etc/shaders/debug_grid.wgsl.
 *
 * A view with a debug item on records one render pass after its effects that loads the color
 * of the target and reads the depth of the view; without debug items it records nothing, and
 * the captures and bakes never do.
 *
 * Group 1 holds the uniform of a draw, the uniforms of a packet share one growing buffer.
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

struct box_gpu_debug_t {
    WGPUBindGroupLayout layout;     /**< Uniform of a draw */
    WGPUPipelineLayout pipeline_layout; /**< View group and the group of a draw */
    ecs_map_t grid_pipelines;       /**< Color format to the WGPURenderPipeline of the grid */
    int32_t grid_revision;          /**< Revision of the grid shader of the pipelines */
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
    ecs_map_init(&d->grid_pipelines, NULL);
    gpu->debug = d;
}

/** Release the grid pipelines of every format.
 *
 * @param d The debug state
 */
static void box_gpu_debug_pipelines_free(
    box_gpu_debug_t *d)
{
    ecs_map_iter_t it = ecs_map_iter(&d->grid_pipelines);
    while (ecs_map_next(&it)) {
        WGPURenderPipeline pipeline = (WGPURenderPipeline)(uintptr_t)ecs_map_value(&it);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

    ecs_map_clear(&d->grid_pipelines);
}

void box_gpu_debug_fini(
    box_gpu_t *gpu)
{
    box_gpu_debug_t *d = gpu->debug;
    if (!d) {
        goto done;
    }

    box_gpu_debug_pipelines_free(d);
    ecs_map_fini(&d->grid_pipelines);
    if (d->buffer) {
        wgpuBufferRelease(d->buffer);
    }

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
    return v->debug.grid.enabled;
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
    if (!shader || !shader->module) {
        goto done;
    }

    if (d->grid_revision != shader->revision) {
        box_gpu_debug_pipelines_free(d);
        d->grid_revision = shader->revision;
    }

    cached = ecs_map_get(&d->grid_pipelines, (ecs_map_key_t)format);
    if (cached) {
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
    ecs_map_insert_ptr(&d->grid_pipelines, (ecs_map_key_t)format, result);
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
    WGPURenderPipeline grid = NULL;
    (void)packet;
    if (!gpu->debug || !color || !t->depth_view) {
        goto done;
    }

    if (v->debug.grid.enabled) {
        grid = box_gpu_debug_grid_pipeline(gpu, t->format);
    }

    if (!grid) {
        goto done;
    }

    WGPUBindGroup grid_group = box_gpu_debug_grid_group(gpu, &v->debug.grid);
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
    wgpuRenderPassEncoderSetPipeline(pass, grid);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, grid_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(grid_group);
    box_gpu_count(gpu, BOX_GPU_STAT_DEBUG_PASS);
done:
    return;
}

#endif
