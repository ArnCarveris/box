/**
 * @file render/gpu_environment_image.c
 * @brief The GPU side of EnvironmentImage, the reference environment source: an equirectangular
 * image is uploaded into a texture and converted by environment.wgsl into the target of its
 * cubemap, which is then reported written.
 */

#include "gpu_environment.h"

#ifdef BOX_RENDER_WGPU

/** Texels per side of a workgroup of the conversion, as in environment.wgsl */
#define BOX_GPU_ENV_IMAGE_GROUP (8)

/** Bytes of a texel of an uploaded image */
#define BOX_GPU_ENV_IMAGE_TEXEL (8)

/** Bytes of the uniforms of the conversion */
#define BOX_GPU_ENV_IMAGE_PARAMS (16)

/** An uploaded image that waits for its conversion */
typedef struct box_gpu_env_pending_t {
    uint32_t handle;        /**< Handle of the cubemap it is written into */
    int32_t version;        /**< EnvironmentStatus.version of the content */
    WGPUTexture texture;    /**< The image, rgba16float */
    WGPUTextureView view;   /**< View of the image */
} box_gpu_env_pending_t;

struct box_gpu_env_image_t {
    WGPUBindGroupLayout layout;     /**< Binds the uniforms, the image, its sampler and the
                                     *   target */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the conversion */
    WGPUComputePipeline pipeline;   /**< Conversion pipeline, NULL until the shader exists */
    int32_t revision;               /**< Revision of the shader the pipeline was made with */
    WGPUSampler sampler;            /**< Linear sampler, repeating along the longitude */
    WGPUBuffer params;              /**< Uniforms of the conversion */
    ecs_vec_t pending;              /**< box_gpu_env_pending_t that wait for the conversion */
};

/** Release an image that waited for its conversion.
 *
 * @param p The image
 */
static void box_gpu_env_pending_free(
    box_gpu_env_pending_t *p)
{
    wgpuTextureViewRelease(p->view);
    wgpuTextureRelease(p->texture);
}

/** Get the state of the conversion, creating its layouts, sampler and uniforms on first use.
 *
 * @param gpu The backend
 * @return The state
 */
static box_gpu_env_image_t* box_gpu_env_image_get(
    box_gpu_t *gpu)
{
    box_gpu_env_image_t *s = gpu->environment_image;
    if (s) {
        goto done;
    }

    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Uniform }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = BOX_GPU_ENVIRONMENT_FORMAT,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        }
    };
    s = ecs_os_calloc_t(box_gpu_env_image_t);
    s->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 4, .entries = entries });
    s->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &s->layout
        });
    s->sampler = wgpuDeviceCreateSampler(gpu->device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_Repeat,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    });
    s->params = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_GPU_ENV_IMAGE_PARAMS
    });
    ecs_vec_init_t(NULL, &s->pending, box_gpu_env_pending_t, 0);
    gpu->environment_image = s;
done:
    return s;
}

void box_gpu_environment_image_fini(
    box_gpu_t *gpu)
{
    box_gpu_env_image_t *s = gpu->environment_image;
    int32_t i;
    if (!s) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&s->pending); i ++) {
        box_gpu_env_pending_free(ecs_vec_get_t(&s->pending, box_gpu_env_pending_t, i));
    }

    ecs_vec_fini_t(NULL, &s->pending, box_gpu_env_pending_t);
    if (s->pipeline) {
        wgpuComputePipelineRelease(s->pipeline);
    }

    wgpuPipelineLayoutRelease(s->pipeline_layout);
    wgpuBindGroupLayoutRelease(s->layout);
    wgpuSamplerRelease(s->sampler);
    wgpuBufferRelease(s->params);
    ecs_os_free(s);
    gpu->environment_image = NULL;
done:
    return;
}

/** Drop the image of a handle that waits for its conversion.
 *
 * @param s The state
 * @param handle Handle of the cubemap
 */
static void box_gpu_env_image_drop(
    box_gpu_env_image_t *s,
    uint32_t handle)
{
    int32_t i;
    for (i = ecs_vec_count(&s->pending) - 1; i >= 0; i --) {
        box_gpu_env_pending_t *p = ecs_vec_get_t(&s->pending, box_gpu_env_pending_t, i);
        if (p->handle == handle) {
            box_gpu_env_pending_free(p);
            ecs_vec_remove_t(&s->pending, box_gpu_env_pending_t, i);
        }
    }
}

void box_gpu_environment_image_apply(
    box_gpu_t *gpu,
    uint32_t handle,
    const void *data,
    uint32_t size)
{
    const box_render_environment_image_t *desc = data;
    if (!desc || size < sizeof(box_render_environment_image_t) || !desc->width ||
        !desc->height || (uint64_t)desc->width * desc->height * BOX_GPU_ENV_IMAGE_TEXEL >
            size - sizeof(box_render_environment_image_t))
    {
        goto done;
    }

    box_gpu_env_image_t *s = box_gpu_env_image_get(gpu);
    box_gpu_env_image_drop(s, handle);
    box_gpu_env_pending_t *p = ecs_vec_append_t(NULL, &s->pending, box_gpu_env_pending_t);
    p->handle = handle;
    p->version = desc->version;
    p->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { desc->width, desc->height, 1 },
        .format = WGPUTextureFormat_RGBA16Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    p->view = wgpuTextureCreateView(p->texture, NULL);
    wgpuQueueWriteTexture(gpu->queue,
        &(WGPUTexelCopyTextureInfo){ .texture = p->texture, .aspect = WGPUTextureAspect_All },
        desc + 1, (size_t)desc->width * desc->height * BOX_GPU_ENV_IMAGE_TEXEL,
        &(WGPUTexelCopyBufferLayout){
            .bytesPerRow = desc->width * BOX_GPU_ENV_IMAGE_TEXEL,
            .rowsPerImage = desc->height
        },
        &(WGPUExtent3D){ desc->width, desc->height, 1 });
done:
    return;
}

/** Get the conversion pipeline, created again when the environment shader was recompiled.
 *
 * @param gpu The backend
 * @param s The state
 * @return The pipeline, NULL while the shader has no module
 */
static WGPUComputePipeline box_gpu_env_image_pipeline(
    box_gpu_t *gpu,
    box_gpu_env_image_t *s)
{
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_ENVIRONMENT);
    if (!shader || !shader->module) {
        goto done;
    }

    if (!s->pipeline || s->revision != shader->revision) {
        if (s->pipeline) {
            wgpuComputePipelineRelease(s->pipeline);
        }

        s->pipeline = box_gpu_compute_pipeline(gpu,
            &(WGPUComputePipelineDescriptor){
                .layout = s->pipeline_layout,
                .compute = {
                    .module = shader->module,
                    .entryPoint = { .data = "equirect_to_cube", .length = WGPU_STRLEN }
                }
            });
        s->revision = shader->revision;
    }

done:
    return shader && shader->module ? s->pipeline : NULL;
}

/** Record and submit the conversion of an image into the target of its cubemap.
 *
 * @param gpu The backend
 * @param s The state
 * @param pipeline The conversion pipeline
 * @param p The image
 * @param target The target of the cubemap
 * @param res Face edge of the cubemap in texels
 */
static void box_gpu_env_image_convert(
    box_gpu_t *gpu,
    box_gpu_env_image_t *s,
    WGPUComputePipeline pipeline,
    const box_gpu_env_pending_t *p,
    WGPUTextureView target,
    uint32_t res)
{
    uint32_t params[4] = { res, 0, 0, 0 };
    uint32_t groups = (res + BOX_GPU_ENV_IMAGE_GROUP - 1) / BOX_GPU_ENV_IMAGE_GROUP;
    wgpuQueueWriteBuffer(gpu->queue, s->params, 0, params, sizeof(params));
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->layout,
        .entryCount = 4,
        .entries = (WGPUBindGroupEntry[4]){
            { .binding = 0, .buffer = s->params, .size = BOX_GPU_ENV_IMAGE_PARAMS },
            { .binding = 1, .textureView = p->view },
            { .binding = 2, .sampler = s->sampler },
            { .binding = 3, .textureView = target }
        }
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, groups, 6);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(group);
}

void box_gpu_environment_image_render(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_env_image_t *s = gpu->environment_image;
    WGPUComputePipeline pipeline = NULL;
    int32_t i;
    (void)packet;
    if (!s || !ecs_vec_count(&s->pending)) {
        goto done;
    }

    pipeline = box_gpu_environment_ready(gpu) ? box_gpu_env_image_pipeline(gpu, s) : NULL;
    for (i = ecs_vec_count(&s->pending) - 1; i >= 0; i --) {
        box_gpu_env_pending_t *p = ecs_vec_get_t(&s->pending, box_gpu_env_pending_t, i);
        uint32_t res = 0;
        WGPUTextureView target = box_gpu_environment_target(gpu, p->handle, &res);
        if (target && !pipeline) {
            continue;
        }

        if (target) {
            box_gpu_env_image_convert(gpu, s, pipeline, p, target, res);
            box_gpu_environment_written(gpu, p->handle, p->version);
        }

        box_gpu_env_pending_free(p);
        ecs_vec_remove_t(&s->pending, box_gpu_env_pending_t, i);
    }

done:
    return;
}

#endif
