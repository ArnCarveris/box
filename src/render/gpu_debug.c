/**
 * @file render/gpu_debug.c
 * @brief Debug passes of the webgpu backend: what View.debug draws over a view after its effects,
 * the grid of etc/shaders/debug_grid.wgsl and the probes of etc/shaders/debug_probes.wgsl.
 *
 * A view with a debug item on records one render pass after its effects that loads the color
 * of the target and reads the depth of the view; without debug items it records nothing, and
 * the captures and bakes never do.
 *
 * Group 1 holds the uniform of a grid draw, in a growing buffer, or of the probe draws of a
 * view, at a dynamic offset of a buffer of their own; the probe draws also bind the view (0),
 * the empty group (2) and the volume (3) of the forward pass.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes the uniform of a draw takes in the buffer: the offset alignment of webgpu */
#define BOX_GPU_DEBUG_UNIFORM_STRIDE (256u)

/** Smallest buffer of the draws of a packet in bytes */
#define BOX_GPU_DEBUG_BUFFER_MIN (4096u)

/** Width of the major lines and the axes over the width of the minor lines */
#define BOX_GPU_DEBUG_MAJOR_WIDTH (1.5f)

/** Vertices of the quad of a probe sphere */
#define BOX_GPU_DEBUG_SPHERE_VERTICES (6u)

/** Vertices of the 12 lines of a box */
#define BOX_GPU_DEBUG_BOX_VERTICES (24u)

/** Uniform of the probe draws of a view, matching ProbeDebug in debug_probes.wgsl */
typedef struct box_gpu_probe_uniforms_t {
    vec4 params;                    /**< World radius of the spheres, 0 for the default, three
                                     *   unused */
} box_gpu_probe_uniforms_t;

/** Pipelines of the probe views of a color format */
typedef struct box_gpu_probe_pipelines_t {
    WGPURenderPipeline light;       /**< Light probe spheres */
    WGPURenderPipeline reflection;  /**< Reflection probe spheres */
    WGPURenderPipeline box;         /**< Influence and projection boxes */
} box_gpu_probe_pipelines_t;

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
    WGPUBindGroupLayout probe_layout; /**< Uniform of the probe draws, at a dynamic offset */
    WGPUPipelineLayout probe_pipeline_layout; /**< View, probe uniform, empty group, volume */
    ecs_map_t probe_pipelines;      /**< Color format to box_gpu_probe_pipelines_t */
    int32_t probe_revision;         /**< Revision of the probe shader of the pipelines */
    WGPUBuffer probe_buffer;        /**< Uniforms of the probe draws of the packet, NULL until
                                     *   the first draw */
    WGPUBindGroup probe_group;      /**< Binds the whole probe buffer at a dynamic offset */
    uint64_t probe_size;            /**< Bytes of the probe buffer */
    uint64_t probe_used;            /**< Bytes of the probe buffer the draws of the packet took */
    ecs_map_t probe_warned;         /**< Views warned about drawing BOX_GPU_DEBUG_PROBES_MAX */
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

    WGPUBindGroupLayoutEntry probe = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = true,
            .minBindingSize = sizeof(box_gpu_probe_uniforms_t)
        }
    };
    d->probe_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 1, .entries = &probe });
    WGPUBindGroupLayout probe_groups[4] = {
        gpu->view_layout, d->probe_layout, gpu->prepass_empty_layout, gpu->volume_layout
    };
    d->probe_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 4,
            .bindGroupLayouts = probe_groups
        });
    ecs_map_init(&d->probe_pipelines, NULL);
    ecs_map_init(&d->probe_warned, NULL);
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

/** Release the probe pipelines of every format.
 *
 * @param d The debug state
 */
static void box_gpu_debug_probe_pipelines_free(
    box_gpu_debug_t *d)
{
    ecs_map_iter_t it = ecs_map_iter(&d->probe_pipelines);
    while (ecs_map_next(&it)) {
        box_gpu_probe_pipelines_t *p = ecs_map_ptr(&it);
        if (p->light) {
            wgpuRenderPipelineRelease(p->light);
        }

        if (p->reflection) {
            wgpuRenderPipelineRelease(p->reflection);
        }

        if (p->box) {
            wgpuRenderPipelineRelease(p->box);
        }

        ecs_os_free(p);
    }

    ecs_map_clear(&d->probe_pipelines);
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

    box_gpu_debug_probe_pipelines_free(d);
    ecs_map_fini(&d->probe_pipelines);
    ecs_map_fini(&d->probe_warned);
    if (d->probe_group) {
        wgpuBindGroupRelease(d->probe_group);
    }

    if (d->probe_buffer) {
        wgpuBufferRelease(d->probe_buffer);
    }

    wgpuPipelineLayoutRelease(d->probe_pipeline_layout);
    wgpuBindGroupLayoutRelease(d->probe_layout);

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
        gpu->debug->probe_used = 0;
    }
}

/** Check whether a view draws its light probes: it has them on and samples a volume.
 *
 * @param v The view
 * @return Whether the light probe view of the view is on and has probes to show
 */
static bool box_gpu_debug_light_probes(
    const box_render_view_t *v)
{
    return v->debug.light_probes && v->volume;
}

/** Check whether a view draws its reflection probes: it has them on, is lit and samples
 * reflection probes that its clusters list, which leaves out the scene probe.
 *
 * @param v The view
 * @return Whether the reflection probe view of the view is on and has probes to show
 */
static bool box_gpu_debug_reflection_probes(
    const box_render_view_t *v)
{
    return v->debug.reflection_probes && v->lights.enabled && v->reflections &&
        v->reflection_listed;
}

bool box_gpu_debug_enabled(
    const box_render_view_t *v)
{
    return v->debug.grid.enabled || box_gpu_debug_light_probes(v) ||
        box_gpu_debug_reflection_probes(v);
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

/** Create a probe pipeline: triangles for the spheres, which write their own depth, or lines for
 * the boxes. Both test the depth of the view with Greater, do not write it and do not blend.
 *
 * @param gpu The backend
 * @param module The probe shader
 * @param format The format of the color target
 * @param vs Vertex entry point
 * @param fs Fragment entry point
 * @param topology Primitive topology
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_debug_probe_pipeline_new(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    WGPUTextureFormat format,
    const char *vs,
    const char *fs,
    WGPUPrimitiveTopology topology)
{
    WGPUColorTargetState target = {
        .format = format,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = fs, .length = WGPU_STRLEN },
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

    return wgpuDeviceCreateRenderPipeline(gpu->device, &(WGPURenderPipelineDescriptor){
        .layout = gpu->debug->probe_pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = vs, .length = WGPU_STRLEN }
        },
        .primitive = {
            .topology = topology,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    });
}

/** Get the probe pipelines that draw into a color format, created on first use and again when
 * the shader was recompiled.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @return The pipelines, NULL until the shader exists
 */
static const box_gpu_probe_pipelines_t* box_gpu_debug_probe_pipelines(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_debug_t *d = gpu->debug;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_DEBUG_PROBES);
    box_gpu_probe_pipelines_t *result = NULL;
    if (!shader || !shader->module) {
        goto done;
    }

    if (d->probe_revision != shader->revision) {
        box_gpu_debug_probe_pipelines_free(d);
        d->probe_revision = shader->revision;
    }

    result = ecs_map_get_deref(&d->probe_pipelines, box_gpu_probe_pipelines_t,
        (ecs_map_key_t)format);
    if (result) {
        goto done;
    }

    result = ecs_os_calloc_t(box_gpu_probe_pipelines_t);
    result->light = box_gpu_debug_probe_pipeline_new(gpu, shader->module, format,
        "vs_light", "fs_light", WGPUPrimitiveTopology_TriangleList);
    result->reflection = box_gpu_debug_probe_pipeline_new(gpu, shader->module, format,
        "vs_reflection", "fs_reflection", WGPUPrimitiveTopology_TriangleList);
    result->box = box_gpu_debug_probe_pipeline_new(gpu, shader->module, format,
        "vs_box", "fs_box", WGPUPrimitiveTopology_LineList);
    ecs_map_insert_ptr(&d->probe_pipelines, (ecs_map_key_t)format, result);
done:
    return result;
}

/** Bound the probes of a kind a view draws to BOX_GPU_DEBUG_PROBES_MAX, warning once per view
 * past it.
 *
 * @param gpu The backend
 * @param v The view
 * @param count The probes of the kind
 * @param kind Name of the kind for the warning
 * @return The probes to draw
 */
static uint32_t box_gpu_debug_probe_bound(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    uint32_t count,
    const char *kind)
{
    box_gpu_debug_t *d = gpu->debug;
    uint32_t result = count;
    if (count <= BOX_GPU_DEBUG_PROBES_MAX) {
        goto done;
    }

    if (!ecs_map_get(&d->probe_warned, (ecs_map_key_t)v->view)) {
        ecs_map_insert(&d->probe_warned, (ecs_map_key_t)v->view, 1);
        ecs_warn("view #%u shows %u of its %u %s probes (BOX_GPU_DEBUG_PROBES_MAX)",
            (uint32_t)v->view, BOX_GPU_DEBUG_PROBES_MAX, count, kind);
    }

    result = BOX_GPU_DEBUG_PROBES_MAX;
done:
    return result;
}

/** Count the light probes a view draws: the probes of the resident volume it samples.
 *
 * @param gpu The backend
 * @param v The view
 * @return The probes, 0 when the view does not draw them
 */
static uint32_t box_gpu_debug_light_count(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    const box_gpu_volume_t *volume = box_gpu_debug_light_probes(v)
        ? ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, v->volume)
        : NULL;

    return volume ? box_gpu_debug_probe_bound(gpu, v, volume->count, "light") : 0;
}

/** Count the reflection probes a view draws: those its clusters list, within its probe buffer.
 *
 * @param gpu The backend
 * @param v The view
 * @return The probes, 0 when the view does not draw them
 */
static uint32_t box_gpu_debug_reflection_count(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    const box_gpu_buffer_t *b = box_gpu_debug_reflection_probes(v)
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, v->reflections)
        : NULL;
    uint32_t capacity = b ? b->size / (uint32_t)sizeof(box_render_reflection_probe_t) : 0;

    return box_gpu_debug_probe_bound(gpu, v,
        v->reflection_listed < capacity ? v->reflection_listed : capacity, "reflection");
}

/** Write the uniform of the probe draws of a view into the probe buffer of the packet,
 * replacing the buffer and its bind group with larger ones when it does not fit; earlier draws
 * keep the old ones.
 *
 * @param gpu The backend
 * @param v The view
 * @return Dynamic offset of the uniform in the buffer
 */
static uint32_t box_gpu_debug_probe_uniform(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    box_gpu_debug_t *d = gpu->debug;
    uint64_t offset = d->probe_used;
    box_gpu_probe_uniforms_t u;
    if (!d->probe_buffer || offset + BOX_GPU_DEBUG_UNIFORM_STRIDE > d->probe_size) {
        uint64_t size = d->probe_size ? d->probe_size * 2 : BOX_GPU_DEBUG_BUFFER_MIN;
        if (d->probe_group) {
            wgpuBindGroupRelease(d->probe_group);
        }

        if (d->probe_buffer) {
            wgpuBufferRelease(d->probe_buffer);
        }

        d->probe_buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = size
        });
        WGPUBindGroupEntry entry = {
            .binding = 0,
            .buffer = d->probe_buffer,
            .offset = 0,
            .size = sizeof(box_gpu_probe_uniforms_t)
        };
        d->probe_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = d->probe_layout,
            .entryCount = 1,
            .entries = &entry
        });
        d->probe_size = size;
        offset = 0;
    }

    d->probe_used = offset + BOX_GPU_DEBUG_UNIFORM_STRIDE;
    ecs_os_memset_t(&u, 0, box_gpu_probe_uniforms_t);
    u.params[0] = v->debug.probe_size > 0 ? v->debug.probe_size : 0;
    wgpuQueueWriteBuffer(gpu->queue, d->probe_buffer, offset, &u, sizeof(u));

    return (uint32_t)offset;
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
    const box_gpu_probe_pipelines_t *probes = NULL;
    uint32_t lights = 0, reflections = 0, probe_offset = 0;
    WGPUBindGroup grid_group = NULL;
    (void)packet;
    if (!gpu->debug || !color || !t->depth_view) {
        goto done;
    }

    if (v->debug.grid.enabled) {
        grid = box_gpu_debug_grid_pipeline(gpu, t->format);
    }

    if (v->debug.light_probes || v->debug.reflection_probes) {
        lights = box_gpu_debug_light_count(gpu, v);
        reflections = box_gpu_debug_reflection_count(gpu, v);
        if (lights || reflections) {
            probes = box_gpu_debug_probe_pipelines(gpu, t->format);
        }

        if (!probes) {
            lights = reflections = 0;
        }
    }

    if (!grid && !lights && !reflections) {
        goto done;
    }

    if (grid) {
        grid_group = box_gpu_debug_grid_group(gpu, &v->debug.grid);
    }

    if (lights || reflections) {
        probe_offset = box_gpu_debug_probe_uniform(gpu, v);
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
        wgpuRenderPassEncoderSetPipeline(pass, grid);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, grid_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
    }

    if (lights || reflections) {
        wgpuRenderPassEncoderSetBindGroup(pass, 1, gpu->debug->probe_group, 1, &probe_offset);
        wgpuRenderPassEncoderSetBindGroup(pass, 2, gpu->prepass_empty, 0, NULL);
        wgpuRenderPassEncoderSetBindGroup(pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    }

    if (lights) {
        wgpuRenderPassEncoderSetPipeline(pass, probes->light);
        wgpuRenderPassEncoderDraw(pass, BOX_GPU_DEBUG_SPHERE_VERTICES, lights, 0, 0);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
    }

    if (reflections) {
        wgpuRenderPassEncoderSetPipeline(pass, probes->reflection);
        wgpuRenderPassEncoderDraw(pass, BOX_GPU_DEBUG_SPHERE_VERTICES, reflections, 0, 0);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        wgpuRenderPassEncoderSetPipeline(pass, probes->box);
        wgpuRenderPassEncoderDraw(pass, BOX_GPU_DEBUG_BOX_VERTICES, reflections * 2u, 0, 0);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
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
