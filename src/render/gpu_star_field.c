/**
 * @file render/gpu_star_field.c
 * @brief Star fields of the webgpu backend: the stars of the StarFields a view draws, generated
 * in the vertex stage of etc/shaders/star_field.wgsl, one instanced quad per star.
 *
 * A view draws its fields in its forward pass right after its sky, before the blended draws: a
 * star passes the depth test only where the depth is still the clear value, so geometry hides
 * it, and adds its light to the target. Without fields nothing is recorded.
 *
 * Group 1 holds the uniform of a draw (box_gpu_star_uniforms_t); the uniforms of every draw of a
 * packet follow each other in one buffer, which grows when a draw does not fit.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes the uniform of a draw takes in the buffer: the offset alignment of webgpu */
#define BOX_GPU_STAR_UNIFORM_STRIDE (256u)

/** Smallest buffer of the draws of a packet in bytes */
#define BOX_GPU_STAR_BUFFER_MIN (4096u)

/** Uniform of a draw, matching StarDraw in star_field.wgsl */
typedef struct box_gpu_star_uniforms_t {
    mat4 clip_from_local;           /**< Clip space of the view from the local space of the
                                     *   field, applied to directions */
    float viewport[4];              /**< Width and height of the target in pixels, then their
                                     *   inverses */
    float params[4];                /**< brightness, falloff, size, saturation */
    float shape[4];                 /**< temperature_min, temperature_max, band, band_width */
    uint32_t seed;                  /**< Seed of the hash */
    uint32_t count;                 /**< Stars */
    uint32_t pad[2];                /**< Padding to the size of StarDraw */
} box_gpu_star_uniforms_t;

struct box_gpu_star_fields_t {
    WGPUBindGroupLayout layout;     /**< Uniform of a draw */
    WGPUPipelineLayout pipeline_layout; /**< View group and the group of a draw */
    ecs_map_t pipelines;            /**< Color format to the WGPURenderPipeline that draws into
                                     *   it */
    int32_t revision;               /**< Revision of the shader of the pipelines */
    WGPUBuffer buffer;              /**< Uniforms of the draws of the packet */
    uint64_t size;                  /**< Bytes of the buffer */
    uint64_t used;                  /**< Bytes of the buffer the draws of the packet took */
};

void box_gpu_star_fields_init(
    box_gpu_t *gpu)
{
    box_gpu_star_fields_t *f = ecs_os_calloc_t(box_gpu_star_fields_t);
    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(box_gpu_star_uniforms_t)
        }
    };
    f->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 1, .entries = &entry });
    WGPUBindGroupLayout groups[2] = { gpu->view_layout, f->layout };
    f->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    ecs_map_init(&f->pipelines, NULL);
    gpu->star_fields = f;
}

/** Release the pipelines of every format.
 *
 * @param f The star field state
 */
static void box_gpu_star_pipelines_free(
    box_gpu_star_fields_t *f)
{
    ecs_map_iter_t it = ecs_map_iter(&f->pipelines);
    while (ecs_map_next(&it)) {
        WGPURenderPipeline pipeline = (WGPURenderPipeline)(uintptr_t)ecs_map_value(&it);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

    ecs_map_clear(&f->pipelines);
}

void box_gpu_star_fields_fini(
    box_gpu_t *gpu)
{
    box_gpu_star_fields_t *f = gpu->star_fields;
    if (!f) {
        goto done;
    }

    box_gpu_star_pipelines_free(f);
    ecs_map_fini(&f->pipelines);
    if (f->buffer) {
        wgpuBufferRelease(f->buffer);
    }

    wgpuPipelineLayoutRelease(f->pipeline_layout);
    wgpuBindGroupLayoutRelease(f->layout);
    ecs_os_free(f);
    gpu->star_fields = NULL;
done:
    return;
}

void box_gpu_star_fields_begin(
    box_gpu_t *gpu)
{
    if (gpu->star_fields) {
        gpu->star_fields->used = 0;
    }
}

/** Get the pipeline that draws into a color format, created on first use and again when the
 * shader was recompiled.
 *
 * A fragment passes where its depth, the clear value 0 of reverse Z, is greater or equal to the
 * depth of the pass, only where no geometry was drawn; there is no depth write. The color adds
 * to the target, the alpha of the target is kept.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @return The pipeline, NULL until the shader exists
 */
static WGPURenderPipeline box_gpu_star_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_star_fields_t *f = gpu->star_fields;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_STAR_FIELD);
    WGPURenderPipeline result = NULL;
    ecs_map_val_t *cached;
    if (!shader || !shader->module) {
        goto done;
    }

    if (f->revision != shader->revision) {
        box_gpu_star_pipelines_free(f);
        f->revision = shader->revision;
    }

    cached = ecs_map_get(&f->pipelines, (ecs_map_key_t)format);
    if (cached && *cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    WGPUBlendState blend = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_One
        },
        .alpha = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_Zero,
            .dstFactor = WGPUBlendFactor_One
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
        .depthCompare = WGPUCompareFunction_GreaterEqual,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    result = box_gpu_render_pipeline(gpu, &(WGPURenderPipelineDescriptor){
        .layout = f->pipeline_layout,
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
    if (result) {
        *ecs_map_ensure(&f->pipelines, (ecs_map_key_t)format) = (ecs_map_val_t)(uintptr_t)result;
    }

done:
    return result;
}

bool box_gpu_star_fields_ready(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    return gpu->star_fields && v->star_count > 0 &&
        box_gpu_shader_get(gpu, BOX_RENDER_SHADER_STAR_FIELD) != NULL;
}

/** Make room for the uniforms of the draws of a view in the buffer of the packet, replacing the
 * buffer with a larger one when they do not fit; earlier draws keep the old one through their
 * bind groups.
 *
 * @param gpu The backend
 * @param need Bytes of the draws
 * @return Offset of the first draw in the buffer
 */
static uint64_t box_gpu_star_reserve(
    box_gpu_t *gpu,
    uint64_t need)
{
    box_gpu_star_fields_t *f = gpu->star_fields;
    uint64_t offset = f->used;
    if (!f->buffer || offset + need > f->size) {
        uint64_t size = f->size ? f->size : BOX_GPU_STAR_BUFFER_MIN;
        while (size < need * 2) {
            size *= 2;
        }

        if (f->buffer) {
            wgpuBufferRelease(f->buffer);
        }

        f->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = size
        });
        f->size = size;
        offset = 0;
    }

    f->used = offset + need;

    return offset;
}

/** Write the uniform of a star field as a view draws it.
 *
 * @param s The record of the field
 * @param view_proj Projection times view matrix of the view
 * @param width Width of the target in pixels
 * @param height Height of the target in pixels
 * @param out Receives the uniform
 */
static void box_gpu_star_uniforms(
    const box_render_star_field_t *s,
    mat4 view_proj,
    uint32_t width,
    uint32_t height,
    box_gpu_star_uniforms_t *out)
{
    mat4 rotation = GLM_MAT4_IDENTITY_INIT;
    int32_t k;
    ecs_os_memset_t(out, 0, box_gpu_star_uniforms_t);
    for (k = 0; k < 3; k ++) {
        glm_vec3_copy(ECS_CONST_CAST(float*, s->rotation[k]), rotation[k]);
        rotation[k][3] = 0.0f;
    }

    glm_mat4_mul(view_proj, rotation, out->clip_from_local);
    out->viewport[0] = (float)width;
    out->viewport[1] = (float)height;
    out->viewport[2] = 1.0f / (float)width;
    out->viewport[3] = 1.0f / (float)height;
    out->params[0] = s->brightness;
    out->params[1] = s->falloff;
    out->params[2] = s->size;
    out->params[3] = s->saturation;
    out->shape[0] = s->temperature_min;
    out->shape[1] = s->temperature_max;
    out->shape[2] = s->band;
    out->shape[3] = s->band_width;
    out->seed = s->seed;
    out->count = (uint32_t)s->count;
}

bool box_gpu_star_fields_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_gpu_target_t *t,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    box_gpu_star_fields_t *f = gpu->star_fields;
    WGPURenderPipeline pipeline = box_gpu_star_fields_ready(gpu, v)
        ? box_gpu_star_pipeline(gpu, format)
        : NULL;
    bool result = false;
    int32_t i;
    if (!pipeline || v->star_first + v->star_count > ecs_vec_count(&packet->star_fields)) {
        goto done;
    }

    uint32_t width = (uint32_t)(t->desc.render_width > 0 ? t->desc.render_width : 1);
    uint32_t height = (uint32_t)(t->desc.render_height > 0 ? t->desc.render_height : 1);
    const box_render_star_field_t *fields = ecs_vec_get_t(
        &packet->star_fields, box_render_star_field_t, v->star_first);
    uint64_t offset = box_gpu_star_reserve(gpu,
        (uint64_t)v->star_count * BOX_GPU_STAR_UNIFORM_STRIDE);
    mat4 proj, view_proj;
    box_camera_projection(&v->camera_data,
        (float)t->desc.point_width, (float)t->desc.point_height, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), view_proj);
    WGPUBindGroupEntry entry = {
        .binding = 0,
        .buffer = f->buffer,
        .offset = 0,
        .size = sizeof(box_gpu_star_uniforms_t)
    };
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    result = true;
    for (i = 0; i < v->star_count; i ++) {
        box_gpu_star_uniforms_t u;
        if (fields[i].count <= 0) {
            continue;
        }

        uint64_t at = offset + (uint64_t)i * BOX_GPU_STAR_UNIFORM_STRIDE;
        box_gpu_star_uniforms(&fields[i], view_proj, width, height, &u);
        wgpuQueueWriteBuffer(gpu->queue, f->buffer, at, &u, sizeof(u));
        entry.offset = at;
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device,
            &(WGPUBindGroupDescriptor){
                .layout = f->layout,
                .entryCount = 1,
                .entries = &entry
            });
        wgpuRenderPassEncoderSetBindGroup(pass, 1, group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 6, (uint32_t)fields[i].count, 0, 0);
        wgpuBindGroupRelease(group);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        box_gpu_count(gpu, BOX_GPU_STAT_STAR_FIELD);
    }

done:
    return result;
}

#endif
