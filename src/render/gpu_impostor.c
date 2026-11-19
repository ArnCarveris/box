/**
 * @file render/gpu_impostor.c
 * @brief Impostor atlases of the webgpu backend: the frames the targets of a packet rendered
 * are copied into their cells, then the atlases the packet reads are written to PNG files.
 *
 * A frame is the sRGB image of the target of one view (render/impostor.c). The copy reads it
 * decoded and writes the albedo through the sRGB view of the atlas, the normal and the depth
 * through its linear view, so those two hold the values the views computed.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes between the parameters of two copies in the uniform buffer, the uniform offset
 * alignment webgpu guarantees */
#define BOX_GPU_IMPOSTOR_STRIDE (256u)

/** A frame waiting for its copy */
typedef struct box_gpu_impostor_frame_t {
    box_render_impostor_blit_t blit; /**< Where it goes */
    WGPUTexture texture;            /**< Reference to the image of its target */
} box_gpu_impostor_frame_t;

/** Parameters of a copy, the Params of impostor.wgsl */
typedef struct box_gpu_impostor_params_t {
    int32_t cell[4];                /**< Corner of the cell, dilation */
} box_gpu_impostor_params_t;

/** Impostor state of the backend */
typedef struct box_gpu_impostor_t {
    WGPUBindGroupLayout layout;     /**< Binds a frame and the parameters of its copy */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the copies */
    WGPURenderPipeline color;       /**< Copy into the sRGB view of an atlas, NULL until made */
    WGPURenderPipeline linear;      /**< Copy into the linear view of an atlas, NULL until made */
    WGPUBuffer params;              /**< Parameters of the copies, BOX_GPU_IMPOSTOR_STRIDE apart */
    uint64_t params_size;           /**< Bytes of params */
    ecs_vec_t frames;               /**< box_gpu_impostor_frame_t waiting for their copy */
} box_gpu_impostor_t;

/** Get the impostor state of the backend, creating it.
 *
 * @param gpu The backend
 * @return The state
 */
static box_gpu_impostor_t* box_gpu_impostor_get(
    box_gpu_t *gpu)
{
    box_gpu_impostor_t *result = gpu->impostor;
    if (result) {
        goto done;
    }

    result = gpu->impostor = ecs_os_calloc_t(box_gpu_impostor_t);
    ecs_vec_init_t(NULL, &result->frames, box_gpu_impostor_frame_t, 0);
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
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_impostor_params_t)
            }
        }
    };
    result->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = entries });
    result->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &result->layout
        });
done:
    return result;
}

/** Request the copy pipeline of a format of an atlas view.
 *
 * @param gpu The backend
 * @param s The impostor state
 * @param format BOX_GPU_TEXTURE_SRGB for the albedo, else BOX_GPU_TEXTURE_FORMAT
 * @return The pipeline, NULL while the shader or the pipeline does not exist
 */
static WGPURenderPipeline box_gpu_impostor_pipeline(
    box_gpu_t *gpu,
    box_gpu_impostor_t *s,
    WGPUTextureFormat format)
{
    WGPURenderPipeline *slot = format == BOX_GPU_TEXTURE_SRGB ? &s->color : &s->linear;
    WGPUShaderModule shader = box_gpu_shader_get(gpu, BOX_RENDER_SHADER_IMPOSTOR);
    if (*slot || !shader) {
        goto done;
    }

    WGPUColorTargetState target = { .format = format, .writeMask = WGPUColorWriteMask_All };
    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = s->pipeline_layout,
        .vertex = {
            .module = shader,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };
    *slot = box_gpu_render_pipeline(gpu, &desc);
done:
    return *slot;
}

/** Make the uniform buffer hold the parameters of a number of copies.
 *
 * @param gpu The backend
 * @param s The impostor state
 * @param count Copies
 */
static void box_gpu_impostor_reserve(
    box_gpu_t *gpu,
    box_gpu_impostor_t *s,
    int32_t count)
{
    uint64_t size = (uint64_t)count * BOX_GPU_IMPOSTOR_STRIDE;
    if (s->params && s->params_size >= size) {
        goto done;
    }

    if (s->params) {
        wgpuBufferRelease(s->params);
    }

    s->params_size = size;
    s->params = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = size
    });
done:
    return;
}

/** Record the copy of a frame into its cell of its atlas.
 *
 * @param gpu The backend
 * @param s The impostor state
 * @param encoder The encoder
 * @param frame The frame
 * @param index Slot of its parameters in the uniform buffer
 * @param pipeline The pipeline of the view of its atlas
 */
static void box_gpu_impostor_copy(
    box_gpu_t *gpu,
    box_gpu_impostor_t *s,
    WGPUCommandEncoder encoder,
    const box_gpu_impostor_frame_t *frame,
    int32_t index,
    WGPURenderPipeline pipeline)
{
    const box_render_impostor_blit_t *b = &frame->blit;
    bool albedo = b->channel == BOX_RENDER_IMPOSTOR_ALBEDO;
    uint32_t size = wgpuTextureGetWidth(frame->texture);
    box_gpu_texture_t *atlas = box_gpu_texture_output(gpu, b->atlas, b->atlas_size,
        b->atlas_size, BOX_GPU_TEXTURE_FORMAT, albedo);
    box_gpu_impostor_params_t params = {
        .cell = { (int32_t)b->x, (int32_t)b->y, (int32_t)b->dilate, 0 }
    };
    uint64_t offset = (uint64_t)index * BOX_GPU_IMPOSTOR_STRIDE;
    if (!atlas || b->x + size > b->atlas_size || b->y + size > b->atlas_size) {
        goto done;
    }

    wgpuQueueWriteBuffer(gpu->queue, s->params, offset, &params, sizeof(params));
    WGPUTextureViewDescriptor dst_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    dst_desc.format = albedo ? BOX_GPU_TEXTURE_SRGB : BOX_GPU_TEXTURE_FORMAT;
    dst_desc.dimension = WGPUTextureViewDimension_2D;
    dst_desc.baseMipLevel = 0;
    dst_desc.mipLevelCount = 1;
    dst_desc.baseArrayLayer = 0;
    dst_desc.arrayLayerCount = 1;
    dst_desc.aspect = WGPUTextureAspect_All;
    dst_desc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTextureView dst = wgpuTextureCreateView(atlas->texture, &dst_desc);
    WGPUTextureView src = wgpuTextureCreateView(frame->texture, NULL);
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .textureView = src },
        {
            .binding = 1,
            .buffer = s->params,
            .offset = offset,
            .size = sizeof(box_gpu_impostor_params_t)
        }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->layout,
        .entryCount = 2,
        .entries = entries
    });
    WGPURenderPassColorAttachment attachment = {
        .view = dst,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,
        &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment
        });
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuRenderPassEncoderSetViewport(pass, (float)b->x, (float)b->y, (float)size, (float)size,
        0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, b->x, b->y, size, size);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
    wgpuTextureViewRelease(src);
    wgpuTextureViewRelease(dst);
    if (b->last) {
        box_gpu_texture_output_written(gpu, b->atlas);
    }

done:
    return;
}

/** Queue the frames of a packet behind the ones that wait, keeping the images of their targets,
 * which the next packet destroys.
 *
 * @param gpu The backend
 * @param s The impostor state
 * @param packet The packet
 */
static void box_gpu_impostor_queue(
    box_gpu_t *gpu,
    box_gpu_impostor_t *s,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->impostor_blits);
    for (i = 0; i < count; i ++) {
        const box_render_impostor_blit_t *b = ecs_vec_get_t(
            &packet->impostor_blits, box_render_impostor_blit_t, i);
        box_gpu_target_t *t = ecs_map_get_deref(&gpu->targets, box_gpu_target_t, b->target);
        if (!t || !t->output) {
            continue;
        }

        wgpuTextureAddRef(t->output);
        box_gpu_impostor_frame_t *frame = ecs_vec_append_t(
            NULL, &s->frames, box_gpu_impostor_frame_t);
        frame->blit = *b;
        frame->texture = t->output;
    }
}

/** Release the frames that wait for their copy.
 *
 * @param s The impostor state
 */
static void box_gpu_impostor_clear(
    box_gpu_impostor_t *s)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&s->frames); i ++) {
        wgpuTextureRelease(ecs_vec_get_t(&s->frames, box_gpu_impostor_frame_t, i)->texture);
    }

    ecs_vec_clear(&s->frames);
}

void box_gpu_impostor_render(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, frames, reads = ecs_vec_count(&packet->impostor_reads);
    box_gpu_impostor_t *s = gpu->impostor;
    WGPUCommandEncoder encoder = NULL;
    WGPURenderPipeline color = NULL, linear = NULL;
    if (!s && !ecs_vec_count(&packet->impostor_blits) && !reads) {
        goto done;
    }

    s = box_gpu_impostor_get(gpu);
    box_gpu_impostor_queue(gpu, s, packet);
    frames = ecs_vec_count(&s->frames);
    if (!frames && !reads) {
        goto done;
    }

    if (frames) {
        color = box_gpu_impostor_pipeline(gpu, s, BOX_GPU_TEXTURE_SRGB);
        linear = box_gpu_impostor_pipeline(gpu, s, BOX_GPU_TEXTURE_FORMAT);
    }

    encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    if (color && linear) {
        box_gpu_impostor_reserve(gpu, s, frames);
        for (i = 0; i < frames; i ++) {
            const box_gpu_impostor_frame_t *frame = ecs_vec_get_t(
                &s->frames, box_gpu_impostor_frame_t, i);
            box_gpu_impostor_copy(gpu, s, encoder, frame, i,
                frame->blit.channel == BOX_RENDER_IMPOSTOR_ALBEDO ? color : linear);
        }
    }

    box_gpu_capture_t **captures = reads ? ecs_os_calloc_n(box_gpu_capture_t*, reads) : NULL;
    for (i = 0; i < reads; i ++) {
        const box_render_impostor_read_t *r = ecs_vec_get_t(
            &packet->impostor_reads, box_render_impostor_read_t, i);
        box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, r->atlas);
        if (tex) {
            captures[i] = box_gpu_capture_texture(gpu, encoder, tex->texture);
        } else {
            ecs_err("impostor atlas %u has no image to write", r->atlas);
            box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        }
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    if (color && linear) {
        box_gpu_impostor_clear(s);
    }

    for (i = 0; i < reads; i ++) {
        const box_render_impostor_read_t *r = ecs_vec_get_t(
            &packet->impostor_reads, box_render_impostor_read_t, i);
        if (captures[i]) {
            box_gpu_capture_texture_write(gpu, captures[i],
                box_render_packet_data(packet, r->path));
        }
    }

    ecs_os_free(captures);
done:
    return;
}

void box_gpu_impostor_fini(
    box_gpu_t *gpu)
{
    box_gpu_impostor_t *s = gpu->impostor;
    if (!s) {
        goto done;
    }

    box_gpu_impostor_clear(s);
    ecs_vec_fini_t(NULL, &s->frames, box_gpu_impostor_frame_t);
    if (s->color) {
        wgpuRenderPipelineRelease(s->color);
    }

    if (s->linear) {
        wgpuRenderPipelineRelease(s->linear);
    }

    if (s->params) {
        wgpuBufferRelease(s->params);
    }

    wgpuPipelineLayoutRelease(s->pipeline_layout);
    wgpuBindGroupLayoutRelease(s->layout);
    ecs_os_free(s);
    gpu->impostor = NULL;
done:
    return;
}

#endif
