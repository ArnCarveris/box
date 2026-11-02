/**
 * @file render/gpu_fog_volume.c
 * @brief Fog volumes of the webgpu backend: the boxes of the FogVolumes a view draws,
 * raymarched in its forward pass (etc/shaders/fog_volume.wgsl).
 *
 * The forward pass of a view with fog volumes ends after its opaque meshes and its sky and
 * continues in a pass that reads its depth, where one instanced draw of 36 vertices per volume
 * marches the boxes back to front before the particles. Without volumes nothing is recorded.
 *
 * Group 0 is the view group, group 1 the records of the view, the inverse view projection of its
 * target and its depth. The uniforms and records of every draw of a packet follow each other in
 * one buffer, which grows when a draw does not fit.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes the uniform of a draw takes at the start of its range: the offset alignment of webgpu,
 * so the records after it start at an aligned offset */
#define BOX_GPU_FOG_UNIFORM_STRIDE (256u)

/** Bytes of the uniform of a draw, FogDraw in fog_volume.wgsl */
#define BOX_GPU_FOG_UNIFORM_SIZE (64u)

/** Smallest buffer of the draws of a packet in bytes */
#define BOX_GPU_FOG_BUFFER_MIN (16384u)

/** Storage buffers the fragment stage of the pipeline binds: the five of the view group (lights,
 * clusters, reflection probes and their SH, probe buffer of the view) and the records */
#define BOX_GPU_FOG_FRAGMENT_STORAGE (6u)

/** Uniform of a draw, matching FogDraw in fog_volume.wgsl */
typedef struct box_gpu_fog_uniforms_t {
    mat4 inv_view_proj;             /**< Clip space of the target to world */
} box_gpu_fog_uniforms_t;

struct box_gpu_fog_volumes_t {
    WGPUBindGroupLayout layout;     /**< Records, uniform and depth of a draw */
    WGPUPipelineLayout pipeline_layout; /**< View group and the group of a draw */
    ecs_map_t pipelines;            /**< Color format to the WGPURenderPipeline that draws into
                                     *   it */
    int32_t revision;               /**< Revision of the shader of the pipelines */
    WGPUBuffer buffer;              /**< Uniforms and records of the draws of the packet */
    uint64_t size;                  /**< Bytes of the buffer */
    uint64_t used;                  /**< Bytes of the buffer the draws of the packet took */
    bool unsupported;               /**< The device binds too few storage buffers in a fragment
                                     *   stage, so no volume draws */
};

void box_gpu_fog_volumes_init(
    box_gpu_t *gpu)
{
    box_gpu_fog_volumes_t *f = ecs_os_calloc_t(box_gpu_fog_volumes_t);
    WGPULimits limits = WGPU_LIMITS_INIT;
    WGPUBindGroupLayoutEntry entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(box_render_fog_volume_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_GPU_FOG_UNIFORM_SIZE
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    f->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 3, .entries = entries });
    WGPUBindGroupLayout groups[2] = { gpu->view_layout, f->layout };
    f->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    if (wgpuDeviceGetLimits(gpu->device, &limits) == WGPUStatus_Success &&
        limits.maxStorageBuffersPerShaderStage != WGPU_LIMIT_U32_UNDEFINED &&
        limits.maxStorageBuffersPerShaderStage < BOX_GPU_FOG_FRAGMENT_STORAGE)
    {
        ecs_warn("fog volumes need %u storage buffers per shader stage, the device has %u",
            BOX_GPU_FOG_FRAGMENT_STORAGE, limits.maxStorageBuffersPerShaderStage);
        f->unsupported = true;
    }

    ecs_map_init(&f->pipelines, NULL);
    gpu->fog_volumes = f;
}

/** Release the pipelines of every format.
 *
 * @param f The fog volume state
 */
static void box_gpu_fog_pipelines_free(
    box_gpu_fog_volumes_t *f)
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

void box_gpu_fog_volumes_fini(
    box_gpu_t *gpu)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    if (!f) {
        goto done;
    }

    box_gpu_fog_pipelines_free(f);
    ecs_map_fini(&f->pipelines);
    if (f->buffer) {
        wgpuBufferRelease(f->buffer);
    }

    wgpuPipelineLayoutRelease(f->pipeline_layout);
    wgpuBindGroupLayoutRelease(f->layout);
    ecs_os_free(f);
    gpu->fog_volumes = NULL;
done:
    return;
}

void box_gpu_fog_volumes_begin(
    box_gpu_t *gpu)
{
    if (gpu->fog_volumes) {
        gpu->fog_volumes->used = 0;
    }
}

/** Get the pipeline that draws into a color format, created on first use and again when the
 * shader was recompiled.
 *
 * Front faces are culled, so the box covers its pixels from inside too; there is no depth test
 * and no depth write. The color adds the scattered light to the target times the transmittance,
 * the alpha of the target is kept.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @return The pipeline, NULL until the shader exists
 */
static WGPURenderPipeline box_gpu_fog_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_FOG_VOLUME);
    WGPURenderPipeline result = NULL;
    ecs_map_val_t *cached;
    if (!shader || !shader->module) {
        goto done;
    }

    if (f->revision != shader->revision) {
        box_gpu_fog_pipelines_free(f);
        f->revision = shader->revision;
    }

    cached = ecs_map_get(&f->pipelines, (ecs_map_key_t)format);
    if (cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    WGPUBlendState blend = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_SrcAlpha
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
        .depthCompare = WGPUCompareFunction_Always,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    result = wgpuDeviceCreateRenderPipeline(gpu->device, &(WGPURenderPipelineDescriptor){
        .layout = f->pipeline_layout,
        .vertex = {
            .module = shader->module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_Front
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    });
    ecs_map_insert_ptr(&f->pipelines, (ecs_map_key_t)format, result);
done:
    return result;
}

bool box_gpu_fog_volumes_ready(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    const box_gpu_fog_volumes_t *f = gpu->fog_volumes;

    return f && !f->unsupported && v->fog_count > 0 &&
        box_gpu_shader_get(gpu, BOX_RENDER_SHADER_FOG_VOLUME) != NULL;
}

/** Make room for the uniform and the records of a draw in the buffer of the packet, replacing
 * the buffer with a larger one when they do not fit; earlier draws keep the old one through
 * their bind groups.
 *
 * @param gpu The backend
 * @param need Bytes of the draw
 * @return Offset of the draw in the buffer
 */
static uint64_t box_gpu_fog_reserve(
    box_gpu_t *gpu,
    uint64_t need)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    uint64_t offset = f->used;
    if (!f->buffer || offset + need > f->size) {
        uint64_t size = f->size ? f->size : BOX_GPU_FOG_BUFFER_MIN;
        while (size < need * 2) {
            size *= 2;
        }

        if (f->buffer) {
            wgpuBufferRelease(f->buffer);
        }

        f->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage |
                WGPUBufferUsage_CopyDst,
            .size = size
        });
        f->size = size;
        offset = 0;
    }

    f->used = (offset + need + BOX_GPU_FOG_UNIFORM_STRIDE - 1) &
        ~(uint64_t)(BOX_GPU_FOG_UNIFORM_STRIDE - 1);

    return offset;
}

bool box_gpu_fog_volumes_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_gpu_target_t *t,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureView depth)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    WGPURenderPipeline pipeline = box_gpu_fog_volumes_ready(gpu, v)
        ? box_gpu_fog_pipeline(gpu, format)
        : NULL;
    bool result = false;
    int32_t i;
    if (!pipeline || v->fog_first + v->fog_count > ecs_vec_count(&packet->fog_volumes)) {
        goto done;
    }

    box_gpu_fog_uniforms_t u;
    mat4 proj, view_proj;
    uint64_t records = (uint64_t)v->fog_count * sizeof(box_render_fog_volume_t);
    uint64_t offset = box_gpu_fog_reserve(gpu, BOX_GPU_FOG_UNIFORM_STRIDE + records);
    ecs_os_memset_t(&u, 0, box_gpu_fog_uniforms_t);
    box_camera_projection(&v->camera_data,
        (float)t->desc.point_width, (float)t->desc.point_height, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), view_proj);
    glm_mat4_inv(view_proj, u.inv_view_proj);
    wgpuQueueWriteBuffer(gpu->queue, f->buffer, offset, &u, sizeof(u));
    wgpuQueueWriteBuffer(gpu->queue, f->buffer, offset + BOX_GPU_FOG_UNIFORM_STRIDE,
        ecs_vec_get_t(&packet->fog_volumes, box_render_fog_volume_t, v->fog_first),
        (size_t)records);

    WGPUBindGroupEntry entries[3] = {
        {
            .binding = 0,
            .buffer = f->buffer,
            .offset = offset + BOX_GPU_FOG_UNIFORM_STRIDE,
            .size = records
        },
        {
            .binding = 1,
            .buffer = f->buffer,
            .offset = offset,
            .size = BOX_GPU_FOG_UNIFORM_SIZE
        },
        {
            .binding = 2,
            .textureView = depth
        }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = f->layout,
        .entryCount = 3,
        .entries = entries
    });
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 36, (uint32_t)v->fog_count, 0, 0);
    wgpuBindGroupRelease(group);
    box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
    for (i = 0; i < v->fog_count; i ++) {
        box_gpu_count(gpu, BOX_GPU_STAT_FOG_VOLUME);
    }

    result = true;
done:
    return result;
}

#endif
