/**
 * @file renderer/effect.c
 * @brief Post process chain of a view: target pool, pass pipelines and the inputs.
 *
 * A view with a chain renders into a pooled target, the passes ping pong between
 * pooled targets and the last one writes the image of the view. Targets are
 * pooled by size and reused across passes and views.
 *
 * Declared inputs decide what the frame produces: color, view depth, world normals
 * and motion vectors. An input no pass asked for binds a one texel dummy.
 *
 */

#include "renderer_internal.h"
#include <stdio.h>

/** Create or resize the texture of a pool entry.
 *
 * @param r The renderer
 * @param entry The entry
 * @param width Width in pixels
 * @param height Height in pixels
 * @param format Format of the target
 */
static void box_effect_texture(
    box_renderer_t *r,
    box_gpu_pool_t *entry,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format)
{
    if (entry->view) wgpuTextureViewRelease(entry->view);
    if (entry->texture) wgpuTextureRelease(entry->texture);
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    entry->texture = wgpuDeviceCreateTexture(r->device, &desc);
    entry->view = wgpuTextureCreateView(entry->texture, NULL);
    entry->width = width;
    entry->height = height;
    entry->format = format;
}

/** Take a target of a size out of the pool.
 *
 * @param r The renderer
 * @param width Width in pixels
 * @param height Height in pixels
 * @param format Format of the target
 * @return The entry, or NULL when every entry is in use
 */
static box_gpu_pool_t* box_effect_acquire(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format)
{
    box_gpu_pool_t *result = NULL;
    uint32_t i;
    for (i = 0; i < BOX_EFFECT_POOL_MAX && !result; i ++) {
        box_gpu_pool_t *entry = &r->pool[i];
        if (!entry->in_use && entry->texture && entry->width == width &&
            entry->height == height && entry->format == format)
        {
            result = entry;
        }
    }

    for (i = 0; i < BOX_EFFECT_POOL_MAX && !result; i ++) {
        if (!r->pool[i].in_use) {
            result = &r->pool[i];
            box_effect_texture(r, result, width, height, format);
        }
    }

    if (result) {
        result->in_use = true;
    }

    return result;
}

/** Hand a target back to the pool.
 *
 * @param entry The entry, NULL is ignored
 */
static void box_effect_release(
    box_gpu_pool_t *entry)
{
    if (entry) {
        entry->in_use = false;
    }
}

/** Create the bind group layout every pass is built against.
 *
 * @param r The renderer
 */
static void box_effect_layout(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[11] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_effect_uniform_t)
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(box_effect_storage_t)
            }
        }, {
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 8,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 9,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube
            }
        }, {
            .binding = 10,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_3D
            }
        }
    };
    WGPUBindGroupLayoutDescriptor desc = { .entryCount = 11, .entries = entries };
    r->effect_layout = wgpuDeviceCreateBindGroupLayout(r->device, &desc);

    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->effect_layout
    };
    r->effect_pipeline_layout = wgpuDeviceCreatePipelineLayout(r->device, &layout_desc);
}

/** Create the one texel textures an input no pass asked for binds.
 *
 * @param r The renderer
 */
static void box_effect_dummies(
    box_renderer_t *r)
{
    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = r->format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->effect_dummy = wgpuDeviceCreateTexture(r->device, &color_desc);
    r->effect_dummy_view = wgpuTextureCreateView(r->effect_dummy, NULL);

    WGPUTextureDescriptor depth_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = WGPUTextureFormat_Depth32Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->effect_depth_dummy = wgpuDeviceCreateTexture(r->device, &depth_desc);
    r->effect_depth_dummy_view = wgpuTextureCreateView(r->effect_depth_dummy, NULL);

    WGPUTextureDescriptor cube_desc = {
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, BOX_ENV_FACES },
        .format = BOX_EFFECT_HDR_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    box_effect_texture_init(r);
    r->effect_cube_dummy = wgpuDeviceCreateTexture(r->device, &cube_desc);
    WGPUTextureViewDescriptor cube_view = {
        .format = BOX_EFFECT_HDR_FORMAT,
        .dimension = WGPUTextureViewDimension_Cube,
        .mipLevelCount = 1,
        .arrayLayerCount = BOX_ENV_FACES,
        .aspect = WGPUTextureAspect_All
    };
    r->effect_cube_dummy_view = wgpuTextureCreateView(r->effect_cube_dummy, &cube_view);
}

/** Create the layout and the pipeline of the motion vector pass.
 *
 * @param r The renderer
 */
static void box_effect_motion_pipeline(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_motion_uniform_t)
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 2, .entries = entries };
    r->motion_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->motion_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_MOTION);
    if (module) {
        WGPUColorTargetState target = { .format = BOX_EFFECT_MOTION_FORMAT,
            .writeMask = WGPUColorWriteMask_All };
        WGPUFragmentState fragment = {
            .module = module,
            .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
            .targetCount = 1,
            .targets = &target
        };
        WGPURenderPipelineDescriptor desc = {
            .label = BOX_GPU_LABEL("motion"),
            .layout = layout,
            .vertex = {
                .module = module,
                .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN }
            },
            .fragment = &fragment,
            .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
            .multisample = { .count = 1, .mask = 0xFFFFFFFF }
        };
        r->motion_pipeline = wgpuDeviceCreateRenderPipeline(r->device, &desc);
        wgpuShaderModuleRelease(module);
    }

    wgpuPipelineLayoutRelease(layout);
}

void box_effect_init(
    box_renderer_t *r)
{
    box_vec_init(&r->effect_pipelines, sizeof(box_gpu_effect_t));
    box_effect_layout(r);
    box_effect_dummies(r);
    box_effect_motion_pipeline(r);
    box_effect_compute_init(r);
    box_effect_mip_init(r);
    box_effect_temporal_init(r);

    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .maxAnisotropy = 1
    };
    r->effect_sampler = wgpuDeviceCreateSampler(r->device, &sampler_desc);

    WGPUBufferDescriptor motion_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_EFFECT_UNIFORM_STRIDE
    };
    r->motion_uniform = wgpuDeviceCreateBuffer(r->device, &motion_desc);
}

void box_effect_invalidate(
    box_renderer_t *r,
    uint32_t handle)
{
    if (r->setup_compute_shader == handle) {
        if (r->setup_compute_first) wgpuComputePipelineRelease(r->setup_compute_first);
        if (r->setup_compute_second) wgpuComputePipelineRelease(r->setup_compute_second);
        r->setup_compute_first = NULL;
        r->setup_compute_second = NULL;
        r->setup_compute_shader = BOX_SHADER_NONE;
    }

    if (r->setup_mip_shader == handle) {
        box_effect_mip_release(r);
    }

    if (r->setup_temporal_shader == handle) {
        if (r->setup_temporal_first) wgpuRenderPipelineRelease(r->setup_temporal_first);
        if (r->setup_temporal_second) wgpuRenderPipelineRelease(r->setup_temporal_second);
        r->setup_temporal_first = NULL;
        r->setup_temporal_second = NULL;
        r->setup_temporal_shader = BOX_SHADER_NONE;
    }

    if (handle >= r->effect_pipelines.count) {
        goto done;
    }

    box_gpu_effect_t *entry = box_vec_at(&r->effect_pipelines, handle);
    uint32_t i;
    for (i = 0; i < BOX_EFFECT_FORMATS; i ++) {
        if (entry->pipeline[i]) {
            wgpuRenderPipelineRelease(entry->pipeline[i]);
        }

        entry->pipeline[i] = NULL;
        entry->tried[i] = false;
    }

done:
    return;
}

/** Pipeline of an effect shader, compiled the first time the pass writes a format.
 *
 * @param r The renderer
 * @param handle The shader handle
 * @param hdr The pass writes an intermediate target instead of the view target
 * @return The pipeline, NULL when the source is missing or does not compile
 */
static WGPURenderPipeline box_effect_pipeline(
    box_renderer_t *r,
    uint32_t handle,
    bool hdr)
{
    while (r->effect_pipelines.count <= handle) {
        box_gpu_effect_t *created = box_vec_push(&r->effect_pipelines);
        memset(created, 0, sizeof(box_gpu_effect_t));
    }

    uint32_t slot = hdr ? 1 : 0;
    box_gpu_effect_t *entry = box_vec_at(&r->effect_pipelines, handle);
    if (entry->tried[slot]) {
        goto done;
    }

    entry->tried[slot] = true;
    WGPUShaderModule module = box_shader_module(r, handle);
    if (!module) {
        goto done;
    }

    WGPUColorTargetState target = { .format = hdr ? BOX_EFFECT_HDR_FORMAT : r->format,
        .writeMask = WGPUColorWriteMask_All };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .label = BOX_GPU_LABEL("effect"),
        .layout = r->effect_pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN }
        },
        .fragment = &fragment,
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    r->device_error = false;
    entry->pipeline[slot] = wgpuDeviceCreateRenderPipeline(r->device, &desc);
    if (r->device_error && entry->pipeline[slot]) {
        wgpuRenderPipelineRelease(entry->pipeline[slot]);
        entry->pipeline[slot] = NULL;
    }

    wgpuShaderModuleRelease(module);
done:
    return entry->pipeline[slot];
}

void box_effect_frame(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < BOX_EFFECT_POOL_MAX; i ++) {
        r->pool[i].in_use = false;
    }

    uint32_t slots = p->effects.count ? p->effects.count : 1;
    if (slots <= r->effect_uniform_slots) {
        goto done;
    }

    if (r->effect_uniform) wgpuBufferRelease(r->effect_uniform);
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)slots * BOX_EFFECT_UNIFORM_STRIDE
    };
    r->effect_uniform = wgpuDeviceCreateBuffer(r->device, &desc);
    r->effect_uniform_slots = slots;
done:
    return;
}

uint32_t box_effect_prepare(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p)
{
    r->effect_run_count = 0;
    r->effect_scene = NULL;
    r->effect_normal = NULL;
    r->effect_motion = NULL;
    uint32_t i;
    for (i = 0; i < view->effect_count && r->effect_run_count < BOX_EFFECT_MAX; i ++) {
        const box_effect_t *fx = box_vec_at(&p->effects, view->effect_first + i);
        if (box_effect_pipeline(r, fx->shader, false)) {
            r->effect_run[r->effect_run_count ++] = view->effect_first + i;
        }
    }

    if (!r->effect_run_count) {
        goto done;
    }

    r->effect_scene = box_effect_acquire(r, view->width, view->height,
        BOX_EFFECT_HDR_FORMAT);
    if (!r->effect_scene) {
        r->effect_run_count = 0;
        goto done;
    }

    if (view->effect_inputs & BOX_EFFECT_NORMALS) {
        r->effect_normal = box_effect_acquire(r, view->width, view->height,
            BOX_EFFECT_NORMAL_FORMAT);
    }

    if (view->effect_inputs & BOX_EFFECT_MOTION) {
        r->effect_motion = box_effect_acquire(r, view->width, view->height,
            BOX_EFFECT_MOTION_FORMAT);
    }

done:
    return r->effect_run_count;
}

void box_effect_motion(
    box_renderer_t *r,
    const box_view_t *view,
    uint32_t index,
    WGPUCommandEncoder encoder)
{
    if (!r->effect_motion || !r->motion_pipeline) {
        goto done;
    }

    (void)index;
    box_motion_uniform_t u;
    memcpy(u.inv_clip, view->inv_clip, sizeof(u.inv_clip));
    memcpy(u.prev_clip, view->prev_clip, sizeof(u.prev_clip));
    wgpuQueueWriteBuffer(r->gpu_queue, r->motion_uniform, 0, &u, sizeof(u));

    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .buffer = r->motion_uniform, .size = sizeof(box_motion_uniform_t) },
        { .binding = 1, .textureView = r->v->depth_sample }
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->motion_layout,
        .entryCount = 2,
        .entries = entries
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(r->device, &group_desc);

    WGPURenderPassColorAttachment color = {
        .view = r->effect_motion->view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0.0, 0.0, 0.0, 0.0 }
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "motion");
    uint32_t offset = 0;
    wgpuRenderPassEncoderSetPipeline(pass, r->motion_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
done:
    return;
}

/** Record one pass of a chain.
 *
 * @param r The renderer
 * @param fx The pass
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param chain Index of the pass in the chain of its view, which names it
 * @param src The target the pass reads
 * @param dst The render target view the pass writes
 * @param encoder The command encoder
 * @param hdr The pass writes an intermediate target instead of the view target
 * @param setup0 First image its setup pass produced, NULL binds the dummy texture
 * @param setup1 Second image its setup pass produced, NULL binds the dummy texture
 */
static void box_effect_pass(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    uint32_t chain,
    box_gpu_pool_t *src,
    WGPUTextureView dst,
    WGPUCommandEncoder encoder,
    bool hdr,
    WGPUTextureView setup0,
    WGPUTextureView setup1)
{
    WGPURenderPipeline pipeline = box_effect_pipeline(r, fx->shader, hdr);
    if (!pipeline) {
        goto done;
    }

    WGPUBindGroupEntry entries[11] = {
        { .binding = 0, .buffer = r->effect_uniform, .size = sizeof(box_effect_uniform_t) },
        { .binding = 1, .textureView = src->view },
        { .binding = 2, .sampler = r->effect_sampler },
        { .binding = 3, .textureView = (fx->inputs & BOX_EFFECT_DEPTH) ?
            r->v->depth_sample : r->effect_depth_dummy_view },
        { .binding = 4, .textureView = r->effect_normal ?
            r->effect_normal->view : r->effect_dummy_view },
        { .binding = 5, .textureView = r->effect_motion ?
            r->effect_motion->view : r->effect_dummy_view },
        { .binding = 6, .buffer = box_effect_storage(r, fx),
            .size = fx->setup_buffer_size[1] ? fx->setup_buffer_size[1] : 16 },
        { .binding = 7, .textureView = setup0 ? setup0 : r->effect_dummy_view },
        { .binding = 8, .textureView = setup1 ? setup1 : r->effect_dummy_view },
        { .binding = 9, .textureView = box_environment_specular(r) },
        { .binding = 10, .textureView = (fx->inputs & BOX_EFFECT_TEXTURE) ?
            box_effect_texture_view(r, fx->texture) : r->effect_texture_dummy_view }
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->effect_layout,
        .entryCount = 11,
        .entries = entries
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(r->device, &group_desc);

    WGPURenderPassColorAttachment color = {
        .view = dst,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0.0, 0.0, 0.0, 1.0 }
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    char name[BOX_TIMING_NAME_MAX];
    snprintf(name, sizeof(name), "effect.%u", chain);
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, name);
    uint32_t offset = slot * BOX_EFFECT_UNIFORM_STRIDE;
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
done:
    return;
}

/** Fill the uniform block of one pass of a chain.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 * @param fx The pass
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param width Width of the target it writes
 * @param height Height of that target
 * @param last The pass writes the image of the view
 */
static void box_effect_uniform_write(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    const box_effect_t *fx,
    uint32_t slot,
    uint32_t width,
    uint32_t height,
    bool last)
{
    bool srgb = box_renderer_srgb(r);
    bool hdr = r->effect_scene != NULL;
    box_effect_uniform_t u;
    memcpy(u.params, fx->params, sizeof(u.params));
    u.size[0] = (float)width;
    u.size[1] = (float)height;
    u.size[2] = width ? 1.0f / (float)width : 0.0f;
    u.size[3] = height ? 1.0f / (float)height : 0.0f;
    u.eye[0] = view->eye[0];
    u.eye[1] = view->eye[1];
    u.eye[2] = view->eye[2];
    u.eye[3] = srgb ? 0.0f : 1.0f;
    u.frame[0] = p->delta;
    u.frame[1] = (float)p->time;
    u.frame[2] = box_effect_decode_flag(hdr, srgb);
    u.frame[3] = box_effect_encode_flag(hdr, srgb, last);
    memcpy(u.clip, view->clip, sizeof(u.clip));
    memcpy(u.inv_clip, view->inv_clip, sizeof(u.inv_clip));
    wgpuQueueWriteBuffer(r->gpu_queue, r->effect_uniform,
        (uint64_t)slot * BOX_EFFECT_UNIFORM_STRIDE, &u, sizeof(u));
}

void box_effect_run(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTextureView dst)
{
    box_gpu_pool_t *src = r->effect_scene;
    uint32_t i;
    for (i = 0; i < r->effect_run_count; i ++) {
        uint32_t slot = r->effect_run[i];
        const box_effect_t *fx = box_vec_at(&p->effects, slot);
        bool last = i + 1 == r->effect_run_count;
        uint32_t width = view->width;
        uint32_t height = view->height;
        box_gpu_pool_t *target = NULL;
        WGPUTextureView out = dst;
        if (!last) {
            box_effect_size(view->width, view->height, fx->scale, &width, &height);
            target = box_effect_acquire(r, width, height, BOX_EFFECT_HDR_FORMAT);
            if (!target) {
                break;
            }

            out = target->view;
        }

        box_effect_uniform_write(r, view, p, fx, slot, width, height, last);
        WGPUTextureView setup0 = NULL;
        WGPUTextureView setup1 = NULL;
        if (fx->setup_kind == BOX_EFFECT_SETUP_COMPUTE) {
            box_effect_compute_run(r, fx, slot, src, encoder);
        } else if (fx->setup_kind == BOX_EFFECT_SETUP_MIP_CHAIN) {
            setup0 = box_effect_mip_run(r, fx, slot, p->frame, src, encoder);
        } else if (fx->setup_kind == BOX_EFFECT_SETUP_TEMPORAL) {
            setup0 = box_effect_temporal_run(r, fx, slot, encoder);
        }

        box_effect_pass(r, fx, slot, i, src, out, encoder, !last, setup0, setup1);
        if (src != r->effect_scene) {
            box_effect_release(src);
        }

        src = target;
    }

    if (src && src != r->effect_scene) {
        box_effect_release(src);
    }

    box_effect_release(r->effect_scene);
    box_effect_release(r->effect_normal);
    box_effect_release(r->effect_motion);
    r->effect_scene = NULL;
    r->effect_normal = NULL;
    r->effect_motion = NULL;
    r->effect_run_count = 0;
}

void box_effect_fini(
    box_renderer_t *r)
{
    box_effect_compute_fini(r);
    box_effect_mip_fini(r);
    box_effect_temporal_fini(r);
    uint32_t i;
    for (i = 0; i < r->effect_pipelines.count; i ++) {
        box_gpu_effect_t *entry = box_vec_at(&r->effect_pipelines, i);
        uint32_t format;
        for (format = 0; format < BOX_EFFECT_FORMATS; format ++) {
            if (entry->pipeline[format]) wgpuRenderPipelineRelease(entry->pipeline[format]);
        }
    }

    box_vec_fini(&r->effect_pipelines);
    for (i = 0; i < BOX_EFFECT_POOL_MAX; i ++) {
        if (r->pool[i].view) wgpuTextureViewRelease(r->pool[i].view);
        if (r->pool[i].texture) wgpuTextureRelease(r->pool[i].texture);
        memset(&r->pool[i], 0, sizeof(box_gpu_pool_t));
    }

    if (r->motion_pipeline) wgpuRenderPipelineRelease(r->motion_pipeline);
    if (r->motion_layout) wgpuBindGroupLayoutRelease(r->motion_layout);
    if (r->motion_uniform) wgpuBufferRelease(r->motion_uniform);
    if (r->effect_sampler) wgpuSamplerRelease(r->effect_sampler);
    if (r->effect_uniform) wgpuBufferRelease(r->effect_uniform);
    if (r->effect_dummy_view) wgpuTextureViewRelease(r->effect_dummy_view);
    if (r->effect_dummy) wgpuTextureRelease(r->effect_dummy);
    if (r->effect_depth_dummy_view) wgpuTextureViewRelease(r->effect_depth_dummy_view);
    if (r->effect_depth_dummy) wgpuTextureRelease(r->effect_depth_dummy);
    if (r->effect_cube_dummy_view) wgpuTextureViewRelease(r->effect_cube_dummy_view);
    if (r->effect_cube_dummy) wgpuTextureRelease(r->effect_cube_dummy);
    box_effect_texture_fini(r);
    if (r->effect_pipeline_layout) wgpuPipelineLayoutRelease(r->effect_pipeline_layout);
    if (r->effect_layout) wgpuBindGroupLayoutRelease(r->effect_layout);
    r->motion_pipeline = NULL;
    r->motion_layout = NULL;
    r->motion_uniform = NULL;
    r->effect_sampler = NULL;
    r->effect_uniform = NULL;
    r->effect_uniform_slots = 0;
    r->effect_dummy_view = NULL;
    r->effect_dummy = NULL;
    r->effect_depth_dummy_view = NULL;
    r->effect_depth_dummy = NULL;
    r->effect_cube_dummy_view = NULL;
    r->effect_cube_dummy = NULL;
    r->effect_pipeline_layout = NULL;
    r->effect_layout = NULL;
}
