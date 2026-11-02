/**
 * @file render/gpu_transmission.c
 * @brief Opaque snapshot of transmission of the webgpu backend.
 *
 * A view with a transmissive draw copies its image into mip 0 of the snapshot of its target
 * before its first blended draw and blurs every mip from the one below (Jimenez downsample,
 * etc/shaders/transmission.wgsl); its transmissive draws sample it at their roughness.
 *
 * A target has one snapshot per color format of its views, at its render size. Until the
 * downsample shader compiles only mip 0 is copied and bound, so a rough shape samples the
 * sharp image. Views without transmissive draws bind a white image and a depth placeholder.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Mip levels of a snapshot at most, the full chain of a 32768 pixel edge minus one */
#define BOX_GPU_SNAPSHOT_MIPS (16)

struct box_gpu_snapshot_t {
    WGPUTexture texture;            /**< The snapshot and its blur pyramid */
    WGPUTextureView view;           /**< View of every mip, which the forward pass samples once
                                     *   the pyramid is blurred */
    WGPUTextureView mips[BOX_GPU_SNAPSHOT_MIPS]; /**< View of each mip */
    WGPUBindGroup groups[BOX_GPU_SNAPSHOT_MIPS]; /**< Group i samples mip i - 1, from 1 */
    uint32_t width;                 /**< Width of mip 0, the render width of the target */
    uint32_t height;                /**< Height of mip 0, the render height of the target */
    uint32_t mip_count;             /**< Mip levels */
    WGPUTextureFormat format;       /**< Format of the color attachment it copies */
    WGPUTexture depth;              /**< Copy of the depth of the target, which a blur reads
                                     *   as the forward pass cannot bind its own depth */
    WGPUTextureView depth_view;     /**< View of the depth copy */
    WGPURenderPipeline pipeline;    /**< Downsample pipeline of its format in this frame, NULL
                                     *   until the shader exists, which leaves the mips above 0
                                     *   unwritten and unsampled */
};

struct box_gpu_transmission_t {
    WGPUBindGroupLayout layout;     /**< Source mip and sampler of a downsample */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the downsample */
    WGPUSampler sampler;            /**< Linear clamp sampler of the downsample */
    ecs_map_t pipelines;            /**< Color format to the WGPURenderPipeline of the
                                     *   downsample into it */
    int32_t revision;               /**< Revision of the shader of the pipelines */
    WGPUTexture white;              /**< 1x1 white image of the views without a snapshot */
    WGPUTextureView white_view;     /**< View of the white image */
    WGPUTexture no_depth;           /**< 1x1 depth placeholder of the views without a snapshot */
    WGPUTextureView no_depth_view;  /**< View of the depth placeholder */
    bool uncopyable;                /**< A color target that cannot be copied was reported */
};

void box_gpu_transmission_init(
    box_gpu_t *gpu)
{
    box_gpu_transmission_t *tr = ecs_os_calloc_t(box_gpu_transmission_t);
    uint8_t white[4] = { 255, 255, 255, 255 };
    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    tr->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = entries });
    tr->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &tr->layout });
    tr->sampler = wgpuDeviceCreateSampler(gpu->device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    });
    tr->white = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    tr->white_view = wgpuTextureCreateView(tr->white, NULL);
    WGPUTexelCopyTextureInfo dst = { .texture = tr->white, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferLayout layout = { .bytesPerRow = sizeof(white), .rowsPerImage = 1 };
    WGPUExtent3D extent = { 1, 1, 1 };
    wgpuQueueWriteTexture(gpu->queue, &dst, white, sizeof(white), &layout, &extent);
    tr->no_depth = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = BOX_GPU_DEPTH_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    tr->no_depth_view = wgpuTextureCreateView(tr->no_depth, NULL);
    ecs_map_init(&tr->pipelines, NULL);
    gpu->transmission = tr;
}

/** Release the downsample pipelines of every format.
 *
 * @param tr The transmission state
 */
static void box_gpu_transmission_pipelines_free(
    box_gpu_transmission_t *tr)
{
    ecs_map_iter_t it = ecs_map_iter(&tr->pipelines);
    while (ecs_map_next(&it)) {
        WGPURenderPipeline pipeline = (WGPURenderPipeline)(uintptr_t)ecs_map_value(&it);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

    ecs_map_clear(&tr->pipelines);
}

void box_gpu_transmission_fini(
    box_gpu_t *gpu)
{
    box_gpu_transmission_t *tr = gpu->transmission;
    if (!tr) {
        goto done;
    }

    box_gpu_transmission_pipelines_free(tr);
    ecs_map_fini(&tr->pipelines);
    wgpuPipelineLayoutRelease(tr->pipeline_layout);
    wgpuBindGroupLayoutRelease(tr->layout);
    wgpuSamplerRelease(tr->sampler);
    wgpuTextureViewRelease(tr->white_view);
    wgpuTextureRelease(tr->white);
    wgpuTextureViewRelease(tr->no_depth_view);
    wgpuTextureRelease(tr->no_depth);
    ecs_os_free(tr);
    gpu->transmission = NULL;
done:
    return;
}

/** Get the downsample pipeline into a color format, created on first use and again when the
 * shader was recompiled.
 *
 * @param gpu The backend
 * @param format The format of the snapshot
 * @return The pipeline, NULL until the shader exists
 */
static WGPURenderPipeline box_gpu_transmission_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_transmission_t *tr = gpu->transmission;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_TRANSMISSION);
    WGPURenderPipeline result = NULL;
    ecs_map_val_t *cached;
    if (!shader || !shader->module) {
        goto done;
    }

    if (tr->revision != shader->revision) {
        box_gpu_transmission_pipelines_free(tr);
        tr->revision = shader->revision;
    }

    cached = ecs_map_get(&tr->pipelines, (ecs_map_key_t)format);
    if (cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    WGPUColorTargetState target = { .format = format, .writeMask = WGPUColorWriteMask_All };
    WGPUFragmentState fragment = {
        .module = shader->module,
        .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    result = wgpuDeviceCreateRenderPipeline(gpu->device, &(WGPURenderPipelineDescriptor){
        .layout = tr->pipeline_layout,
        .vertex = {
            .module = shader->module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    });
    ecs_map_insert_ptr(&tr->pipelines, (ecs_map_key_t)format, result);
done:
    return result;
}

/** Create a snapshot, its depth copy and the bind groups of its downsample passes.
 *
 * Its mips are the full chain of its size minus one, at least one.
 *
 * @param gpu The backend
 * @param width Width of mip 0
 * @param height Height of mip 0
 * @param format Format of the color attachment it copies
 * @return The snapshot
 */
static box_gpu_snapshot_t* box_gpu_snapshot_new(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format)
{
    box_gpu_transmission_t *tr = gpu->transmission;
    box_gpu_snapshot_t *result = ecs_os_calloc_t(box_gpu_snapshot_t);
    uint32_t edge = width > height ? width : height, max_mips = 1, i;
    while (edge > 1) {
        edge >>= 1;
        max_mips ++;
    }

    result->width = width;
    result->height = height;
    result->format = format;
    result->mip_count = max_mips > 1 ? max_mips - 1 : 1;
    if (result->mip_count > BOX_GPU_SNAPSHOT_MIPS) {
        result->mip_count = BOX_GPU_SNAPSHOT_MIPS;
    }

    result->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = format,
        .mipLevelCount = result->mip_count,
        .sampleCount = 1
    });
    result->view = wgpuTextureCreateView(result->texture, NULL);
    result->depth = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = BOX_GPU_DEPTH_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    result->depth_view = wgpuTextureCreateView(result->depth, NULL);
    box_gpu_count(gpu, BOX_GPU_STAT_TRANSMISSION_TEXTURE);
    for (i = 0; i < result->mip_count; i ++) {
        result->mips[i] = wgpuTextureCreateView(result->texture, &(WGPUTextureViewDescriptor){
            .format = format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = i,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        });
    }

    for (i = 1; i < result->mip_count; i ++) {
        WGPUBindGroupEntry entries[2] = {
            { .binding = 0, .textureView = result->mips[i - 1] },
            { .binding = 1, .sampler = tr->sampler }
        };
        result->groups[i] = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = tr->layout,
            .entryCount = 2,
            .entries = entries
        });
    }

    return result;
}

/** Release a snapshot.
 *
 * @param s The snapshot, NULL for none
 */
static void box_gpu_snapshot_free(
    box_gpu_snapshot_t *s)
{
    uint32_t i;
    if (!s) {
        goto done;
    }

    for (i = 0; i < s->mip_count; i ++) {
        if (s->groups[i]) {
            wgpuBindGroupRelease(s->groups[i]);
        }

        wgpuTextureViewRelease(s->mips[i]);
    }

    wgpuTextureViewRelease(s->view);
    wgpuTextureRelease(s->texture);
    wgpuTextureViewRelease(s->depth_view);
    wgpuTextureRelease(s->depth);
    ecs_os_free(s);
done:
    return;
}

void box_gpu_transmission_release(
    box_gpu_target_t *t)
{
    int32_t i;
    for (i = 0; i < BOX_GPU_SNAPSHOT_FORMATS; i ++) {
        box_gpu_snapshot_free(t->snapshots[i]);
        t->snapshots[i] = NULL;
    }
}

/** Check whether a view draws a transmissive batch.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param slot Slot of the view in the view buffer
 * @return Whether its cull view lists a transmissive draw
 */
static bool box_gpu_transmission_draws(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t slot)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    bool result = false;
    int32_t i;
    for (i = 0; cv && i < cv->draw_count && !result; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        result = ecs_vec_get_t(&packet->batches, box_render_batch_t, d->batch)->transmissive;
    }

    return result;
}

box_gpu_snapshot_t* box_gpu_transmission_begin(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    int32_t slot,
    WGPUTexture source,
    WGPUTextureFormat format)
{
    box_gpu_transmission_t *tr = gpu->transmission;
    box_gpu_snapshot_t *result = NULL, **slot_of_format;
    uint32_t width = (uint32_t)(t->desc.render_width > 0 ? t->desc.render_width : 1);
    uint32_t height = (uint32_t)(t->desc.render_height > 0 ? t->desc.render_height : 1);
    if (!tr || !box_gpu_transmission_draws(gpu, packet, slot)) {
        goto done;
    }

    if (!source || !(wgpuTextureGetUsage(source) & WGPUTextureUsage_CopySrc)) {
        if (!tr->uncopyable) {
            ecs_err("the color target of a view with Transmission cannot be copied, "
                "transmissive shapes show a white background");
            tr->uncopyable = true;
        }

        goto done;
    }

    slot_of_format = &t->snapshots[format == t->format ? 0 : 1];
    result = *slot_of_format;
    if (!result || result->width != width || result->height != height ||
        result->format != format)
    {
        box_gpu_snapshot_free(result);
        result = *slot_of_format = box_gpu_snapshot_new(gpu, width, height, format);
    }

    result->pipeline = box_gpu_transmission_pipeline(gpu, format);
done:
    return result;
}

WGPUTextureView box_gpu_transmission_view(
    box_gpu_t *gpu,
    const box_gpu_snapshot_t *s)
{
    WGPUTextureView result = gpu->transmission->white_view;
    if (s) {
        result = s->pipeline ? s->view : s->mips[0];
    }

    return result;
}

WGPUTextureView box_gpu_transmission_depth_view(
    box_gpu_t *gpu,
    const box_gpu_snapshot_t *s)
{
    return s ? s->depth_view : gpu->transmission->no_depth_view;
}

void box_gpu_transmission_record(
    box_gpu_t *gpu,
    box_gpu_snapshot_t *s,
    WGPUCommandEncoder encoder,
    WGPUTexture source,
    WGPUTexture depth)
{
    WGPURenderPipeline pipeline = s->pipeline;
    uint32_t i;
    wgpuCommandEncoderCopyTextureToTexture(encoder,
        &(WGPUTexelCopyTextureInfo){ .texture = source, .aspect = WGPUTextureAspect_All },
        &(WGPUTexelCopyTextureInfo){ .texture = s->texture, .aspect = WGPUTextureAspect_All },
        &(WGPUExtent3D){ s->width, s->height, 1 });
    wgpuCommandEncoderCopyTextureToTexture(encoder,
        &(WGPUTexelCopyTextureInfo){ .texture = depth, .aspect = WGPUTextureAspect_All },
        &(WGPUTexelCopyTextureInfo){ .texture = s->depth, .aspect = WGPUTextureAspect_All },
        &(WGPUExtent3D){ s->width, s->height, 1 });
    for (i = 1; pipeline && i < s->mip_count; i ++) {
        WGPURenderPassColorAttachment attachment = {
            .view = s->mips[i],
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store
        };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,
            &(WGPURenderPassDescriptor){
                .colorAttachmentCount = 1,
                .colorAttachments = &attachment
            });
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, s->groups[i], 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    box_gpu_count(gpu, BOX_GPU_STAT_TRANSMISSION_SNAPSHOT);
}

#endif
