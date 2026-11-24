/**
 * @file render/gpu_effects.c
 * @brief Effect passes of the webgpu backend: HDR targets, the effect chain and its state.
 *
 * A view with effects renders into an HDR image; its effects run in order as fullscreen passes
 * between two HDR images and the last pass writes the target. State kept across frames, such
 * as an adapted exposure, is keyed by target, view and effect entity.
 *
 * The history of a bloom is a pair of history textures (gpu_history.c), which a reset of the
 * view invalidates.
 */

#include "gpu_effects.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

float box_gpu_fx_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Create a buffer that starts out zeroed.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer, besides copy destination
 * @return The buffer
 */
static WGPUBuffer box_gpu_fx_buffer(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    WGPUBufferDescriptor desc = { .usage = usage | WGPUBufferUsage_CopyDst, .size = size };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

WGPUTexture box_gpu_fx_texture(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format,
    uint32_t mips)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { width ? width : 1, height ? height : 1, 1 },
        .format = format,
        .mipLevelCount = mips,
        .sampleCount = 1
    };

    return wgpuDeviceCreateTexture(gpu->device, &desc);
}

/** Create a texture with a mip chain in the HDR format that compute passes write.
 *
 * @param gpu The backend
 * @param width Width of the first mip, at least 1
 * @param height Height of the first mip, at least 1
 * @param mips Mip levels
 * @return The texture
 */
static WGPUTexture box_gpu_fx_chain_texture(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    uint32_t mips)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { width ? width : 1, height ? height : 1, 1 },
        .format = BOX_GPU_HDR_FORMAT,
        .mipLevelCount = mips,
        .sampleCount = 1
    };

    return wgpuDeviceCreateTexture(gpu->device, &desc);
}

/** Create the view of one mip level of a texture.
 *
 * @param texture The texture
 * @param level The mip level
 * @return The view
 */
static WGPUTextureView box_gpu_fx_level(
    WGPUTexture texture,
    uint32_t level)
{
    WGPUTextureViewDescriptor desc = {
        .format = wgpuTextureGetFormat(texture),
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = level,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &desc);
}

/** Create a 3D texture that passes sample, filled with texels of 4 bytes.
 *
 * @param gpu The backend
 * @param size Texels along each edge
 * @param format Color format, of 4 bytes per texel
 * @param texels The texels, x fastest, then y, then z
 * @param view Receives the view of the texture
 * @return The texture
 */
static WGPUTexture box_gpu_fx_volume(
    box_gpu_t *gpu,
    uint32_t size,
    WGPUTextureFormat format,
    const uint32_t *texels,
    WGPUTextureView *view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = { size, size, size },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &desc);
    WGPUTexelCopyTextureInfo dst = { .texture = result, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferLayout layout = { .bytesPerRow = size * 4, .rowsPerImage = size };
    WGPUExtent3D extent = { size, size, size };
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };
    wgpuQueueWriteTexture(gpu->queue, &dst, texels, (size_t)size * size * size * 4, &layout,
        &extent);
    *view = wgpuTextureCreateView(result, &view_desc);

    return result;
}

/** Get the view of the Tony McMapface LUT, loaded from its file on first use.
 *
 * @param gpu The backend
 * @return The view, NULL when the file cannot be loaded
 */
static WGPUTextureView box_gpu_fx_tony(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;
    char *error = NULL;
    uint32_t *texels = NULL;
    if (fx->tony_view || fx->tony_failed) {
        goto done;
    }

    texels = box_render_tony_load(BOX_RENDER_TONY_PATH, &error);
    if (!texels) {
        ecs_err("tonemap: %s", error);
        ecs_os_free(error);
        fx->tony_failed = true;
        goto done;
    }

    fx->tony = box_gpu_fx_volume(gpu, BOX_RENDER_TONY_SIZE, BOX_GPU_TONY_FORMAT, texels,
        &fx->tony_view);
    ecs_os_free(texels);
done:
    return fx->tony_view;
}

void box_gpu_fx_release(
    WGPUTexture *texture,
    WGPUTextureView *view)
{
    if (*view) {
        wgpuTextureViewRelease(*view);
        *view = NULL;
    }

    if (*texture) {
        wgpuTextureRelease(*texture);
        *texture = NULL;
    }
}

void box_gpu_effects_init(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = ecs_os_calloc_t(box_gpu_effects_t);
    WGPUShaderStage frag = WGPUShaderStage_Fragment;
    WGPUBindGroupLayoutEntry entries[7] = {
        { .binding = 0, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 1, .visibility = frag, .sampler = {
            .type = WGPUSamplerBindingType_Filtering } },
        { .binding = 2, .visibility = frag, .buffer = {
            .type = WGPUBufferBindingType_Uniform } },
        { .binding = 3, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 4, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 5, .visibility = frag, .buffer = {
            .type = WGPUBufferBindingType_ReadOnlyStorage } },
        { .binding = 6, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_3D } }
    };
    WGPUBindGroupLayoutEntry smaa[BOX_GPU_SMAA_BINDINGS] = {
        { .binding = 0, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 1, .visibility = frag, .sampler = {
            .type = WGPUSamplerBindingType_Filtering } },
        { .binding = 2, .visibility = frag | WGPUShaderStage_Vertex, .buffer = {
            .type = WGPUBufferBindingType_Uniform } },
        { .binding = 3, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 4, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 5, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 6, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } }
    };
    WGPUShaderStage comp = WGPUShaderStage_Compute;
    WGPUBindGroupLayoutEntry measure[4] = {
        { .binding = 0, .visibility = comp, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 1, .visibility = comp, .buffer = {
            .type = WGPUBufferBindingType_Storage } },
        { .binding = 2, .visibility = comp, .buffer = {
            .type = WGPUBufferBindingType_Storage } },
        { .binding = 3, .visibility = comp, .buffer = {
            .type = WGPUBufferBindingType_Uniform } }
    };
    WGPUBindGroupLayoutEntry bloom[6] = {
        { .binding = 0, .visibility = comp, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 1, .visibility = comp, .sampler = {
            .type = WGPUSamplerBindingType_Filtering } },
        { .binding = 2, .visibility = comp, .buffer = {
            .type = WGPUBufferBindingType_Uniform } },
        { .binding = 4, .visibility = comp, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 5, .visibility = comp, .storageTexture = {
            .access = WGPUStorageTextureAccess_WriteOnly,
            .format = BOX_GPU_HDR_FORMAT,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 6, .visibility = comp, .buffer = {
            .type = WGPUBufferBindingType_Uniform } }
    };
    WGPUSamplerDescriptor sampler = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 32,
        .maxAnisotropy = 1
    };
    float unit[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    uint32_t black = 0;
    fx->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 7, .entries = entries });
    fx->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &fx->layout });
    fx->smaa_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = BOX_GPU_SMAA_BINDINGS,
            .entries = smaa });
    fx->smaa_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &fx->smaa_layout });
    fx->measure_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 4, .entries = measure });
    fx->measure_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &fx->measure_layout });
    fx->bloom_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = bloom });
    fx->bloom_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &fx->bloom_layout });
    fx->sampler = wgpuDeviceCreateSampler(gpu->device, &sampler);
    fx->placeholder = box_gpu_fx_texture(gpu, 1, 1, BOX_GPU_HDR_FORMAT, 1);
    fx->placeholder_view = wgpuTextureCreateView(fx->placeholder, NULL);
    fx->placeholder_3d = box_gpu_fx_volume(gpu, 1, BOX_GPU_TONY_FORMAT, &black,
        &fx->placeholder_3d_view);
    fx->unit_exposure = box_gpu_fx_buffer(gpu, sizeof(unit), WGPUBufferUsage_Storage);
    fx->no_uniforms = box_gpu_fx_buffer(gpu, BOX_GPU_FX_UNIFORMS, WGPUBufferUsage_Uniform);
    wgpuQueueWriteBuffer(gpu->queue, fx->unit_exposure, 0, unit, sizeof(unit));
    ecs_vec_init_t(NULL, &fx->pipelines, box_gpu_fx_pipeline_t, 0);
    ecs_vec_init_t(NULL, &fx->states, box_gpu_fx_state_t*, 0);
    ecs_vec_init_t(NULL, &fx->targets, box_gpu_fx_target_t*, 0);
    gpu->effects = fx;
}

/** Release the mip chain and the prefilter levels of a bloom.
 *
 * @param s The state
 */
static void box_gpu_fx_mips_free(
    box_gpu_fx_state_t *s)
{
    int32_t i;
    for (i = 0; i < s->group_count; i ++) {
        wgpuBindGroupRelease(s->groups[i].group);
    }

    s->group_count = 0;
    for (i = 0; i < s->mip_count; i ++) {
        wgpuTextureViewRelease(s->mips[i]);
        s->mips[i] = NULL;
    }

    if (s->bloom) {
        wgpuTextureRelease(s->bloom);
        s->bloom = NULL;
    }

    for (i = 0; i < s->prefilter_count; i ++) {
        wgpuTextureViewRelease(s->prefilter_mips[i]);
        s->prefilter_mips[i] = NULL;
    }

    if (s->prefilter) {
        wgpuTextureRelease(s->prefilter);
        s->prefilter = NULL;
    }

    for (i = 0; i < s->mip_count - 1; i ++) {
        wgpuTextureViewRelease(s->up_mips[i]);
        s->up_mips[i] = NULL;
    }

    if (s->up) {
        wgpuTextureRelease(s->up);
        s->up = NULL;
    }

    s->mip_count = 0;
    s->prefilter_count = 0;
    s->width = 0;
    s->height = 0;
    s->render_width = 0;
    s->render_height = 0;
}

/** Release the state of an effect and everything it owns.
 *
 * @param s The state
 */
static void box_gpu_fx_state_free(
    box_gpu_fx_state_t *s)
{
    box_gpu_fx_mips_free(s);
    if (s->uniforms) {
        wgpuBufferRelease(s->uniforms);
    }

    if (s->histogram) {
        wgpuBufferRelease(s->histogram);
    }

    if (s->exposure) {
        wgpuBufferRelease(s->exposure);
    }

    if (s->steps) {
        wgpuBufferRelease(s->steps);
    }

    ecs_os_free(s);
}

/** Release the HDR images of a target.
 *
 * @param ft The images
 */
static void box_gpu_fx_target_free(
    box_gpu_fx_target_t *ft)
{
    box_gpu_fx_release(&ft->hdr[0], &ft->hdr_views[0]);
    box_gpu_fx_release(&ft->hdr[1], &ft->hdr_views[1]);
    box_gpu_fx_release(&ft->smaa_edges, &ft->smaa_edges_view);
    box_gpu_fx_release(&ft->smaa_weights, &ft->smaa_weights_view);
    box_gpu_fx_dof_release(ft);
    ecs_os_free(ft);
}

/** Release a compute pipeline when it exists.
 *
 * @param pipeline The pipeline, set to NULL
 */
static void box_gpu_fx_compute_release(
    WGPUComputePipeline *pipeline)
{
    if (*pipeline) {
        wgpuComputePipelineRelease(*pipeline);
        *pipeline = NULL;
    }
}

/** Release the effect pipelines.
 *
 * @param fx The effects
 */
static void box_gpu_fx_pipelines_free(
    box_gpu_effects_t *fx)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&fx->pipelines); i ++) {
        box_gpu_fx_pipeline_t *p = ecs_vec_get_t(&fx->pipelines, box_gpu_fx_pipeline_t, i);
        if (p->pipeline) {
            wgpuRenderPipelineRelease(p->pipeline);
        }
    }

    ecs_vec_clear(&fx->pipelines);
    if (fx->histogram) {
        wgpuComputePipelineRelease(fx->histogram);
        fx->histogram = NULL;
    }

    if (fx->adapt) {
        wgpuComputePipelineRelease(fx->adapt);
        fx->adapt = NULL;
    }

    for (i = 0; i < BOX_GPU_BLOOM_ENTRIES; i ++) {
        box_gpu_fx_compute_release(&fx->bloom[i]);
    }

}

void box_gpu_effects_fini(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;
    int32_t i;
    if (!fx) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&fx->states); i ++) {
        box_gpu_fx_state_free(*ecs_vec_get_t(&fx->states, box_gpu_fx_state_t*, i));
    }

    for (i = 0; i < ecs_vec_count(&fx->targets); i ++) {
        box_gpu_fx_target_free(*ecs_vec_get_t(&fx->targets, box_gpu_fx_target_t*, i));
    }

    box_gpu_fx_pipelines_free(fx);
    ecs_vec_fini_t(NULL, &fx->pipelines, box_gpu_fx_pipeline_t);
    ecs_vec_fini_t(NULL, &fx->states, box_gpu_fx_state_t*);
    ecs_vec_fini_t(NULL, &fx->targets, box_gpu_fx_target_t*);
    box_gpu_fx_release(&fx->placeholder, &fx->placeholder_view);
    box_gpu_fx_release(&fx->placeholder_3d, &fx->placeholder_3d_view);
    box_gpu_fx_release(&fx->tony, &fx->tony_view);
    box_gpu_fx_release(&fx->smaa_area, &fx->smaa_area_view);
    box_gpu_fx_release(&fx->smaa_search, &fx->smaa_search_view);
    wgpuBufferRelease(fx->unit_exposure);
    wgpuBufferRelease(fx->no_uniforms);
    wgpuSamplerRelease(fx->sampler);
    wgpuPipelineLayoutRelease(fx->smaa_pipeline_layout);
    wgpuBindGroupLayoutRelease(fx->smaa_layout);
    wgpuPipelineLayoutRelease(fx->measure_pipeline_layout);
    wgpuBindGroupLayoutRelease(fx->measure_layout);
    wgpuPipelineLayoutRelease(fx->bloom_pipeline_layout);
    wgpuBindGroupLayoutRelease(fx->bloom_layout);
    wgpuPipelineLayoutRelease(fx->pipeline_layout);
    wgpuBindGroupLayoutRelease(fx->layout);
    ecs_os_free(fx);
    gpu->effects = NULL;
done:
    return;
}

/** Get the revision of the module of a shader.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @param module Receives the module, NULL when the shader has none
 * @return The revision
 */
static int32_t box_gpu_fx_module(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule *module)
{
    box_gpu_shader_t *s = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_t, handle);
    *module = s ? s->module : NULL;

    return s ? s->revision : 0;
}

/** Get the blend state of a way to write the output.
 *
 * @param blend How the output is written
 * @param out Receives the blend state
 * @return The blend state, NULL to replace the output
 */
static WGPUBlendState* box_gpu_fx_blend(
    box_gpu_fx_blend_t blend,
    WGPUBlendState *out)
{
    WGPUBlendState *result = out;
    WGPUBlendComponent over = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
    };
    WGPUBlendComponent add = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_Constant,
        .dstFactor = WGPUBlendFactor_One
    };
    WGPUBlendComponent keep = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_Zero,
        .dstFactor = WGPUBlendFactor_One
    };
    if (blend == BOX_GPU_FX_OVER) {
        out->color = over;
        out->alpha = over;
    } else if (blend == BOX_GPU_FX_ADD) {
        out->color = add;
        out->alpha = keep;
    } else {
        result = NULL;
    }

    return result;
}

/** Get the pipeline of an effect entry point, created on first use and again when its shader
 * was recompiled, keeping the previous one until then. The SMAA shader has a layout of its own;
 * the vertex entry point of a pass follows from its fragment entry point.
 *
 * @param gpu The backend
 * @param pass The pass
 * @return The pipeline, NULL while the shader has no module
 */
static WGPURenderPipeline box_gpu_fx_pipeline(
    box_gpu_t *gpu,
    const box_gpu_fx_pass_t *pass)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPUShaderModule module = NULL;
    int32_t revision = box_gpu_fx_module(gpu, pass->shader, &module);
    box_gpu_fx_pipeline_t *found = NULL;
    WGPUBlendState blend;
    int32_t i;
    if (!module) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&fx->pipelines); i ++) {
        box_gpu_fx_pipeline_t *p = ecs_vec_get_t(&fx->pipelines, box_gpu_fx_pipeline_t, i);
        if (p->shader == pass->shader && p->format == pass->format &&
            p->blend == pass->blend && !strcmp(p->entry, pass->entry))
        {
            found = p;
            break;
        }
    }

    if (found && found->pipeline && found->revision == revision) {
        goto done;
    }

    if (!found) {
        found = ecs_vec_append_t(NULL, &fx->pipelines, box_gpu_fx_pipeline_t);
        ecs_os_memset_t(found, 0, box_gpu_fx_pipeline_t);
        found->shader = pass->shader;
        found->entry = pass->entry;
        found->format = pass->format;
        found->blend = pass->blend;
    }

    WGPUColorTargetState target = {
        .format = pass->format,
        .blend = box_gpu_fx_blend(pass->blend, &blend),
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = pass->entry, .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = pass->shader == BOX_RENDER_SHADER_SMAA
            ? fx->smaa_pipeline_layout
            : fx->pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = pass->vertex ? pass->vertex : "vs", .length = WGPU_STRLEN }
        },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };
    WGPURenderPipeline *current = &found->pipeline;
    WGPURenderPipeline next = box_gpu_render_pipeline(gpu, &desc);
    if (box_gpu_render_pipelines_swap(&current, &next, 1)) {
        found->revision = revision;
    }

done:
    return module && found ? found->pipeline : NULL;
}

/** Record a fullscreen pass with its pipeline and bind group, and release the bind group.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param pass The pass
 * @param pipeline The pipeline of the pass
 * @param group The bind group of the pass
 */
static void box_gpu_fx_record(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_gpu_fx_pass_t *pass,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group)
{
    WGPURenderPassColorAttachment attachment = {
        .view = pass->output,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = pass->blend == BOX_GPU_FX_REPLACE ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment
    };
    WGPURenderPassEncoder rp = box_gpu_profile_render_pass(gpu, ctx->encoder, &desc,
        box_gpu_shader_label(gpu, box_gpu_shader_get(gpu, pass->shader)));
    wgpuRenderPassEncoderSetPipeline(rp, pipeline);
    wgpuRenderPassEncoderSetBindGroup(rp, 0, group, 0, NULL);
    if (pass->blend == BOX_GPU_FX_ADD) {
        WGPUColor constant = { pass->constant, pass->constant, pass->constant, pass->constant };
        wgpuRenderPassEncoderSetBlendConstant(rp, &constant);
    }

    wgpuRenderPassEncoderDraw(rp, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);
    wgpuBindGroupRelease(group);
}

bool box_gpu_fx_draw(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_gpu_fx_pass_t *pass)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPURenderPipeline pipeline = box_gpu_fx_pipeline(gpu, pass);
    if (!pipeline) {
        goto done;
    }

    WGPUBuffer uniforms = pass->uniforms ? pass->uniforms : fx->no_uniforms;
    WGPUBuffer exposure = pass->exposure ? pass->exposure : fx->unit_exposure;
    WGPUBindGroupEntry entries[7] = {
        { .binding = 0, .textureView = pass->input },
        { .binding = 1, .sampler = fx->sampler },
        { .binding = 2, .buffer = uniforms, .size = BOX_GPU_FX_UNIFORMS },
        { .binding = 3, .textureView = ctx->t->depth_view },
        { .binding = 4, .textureView = pass->aux ? pass->aux : fx->placeholder_view },
        { .binding = 5, .buffer = exposure, .size = 4 * sizeof(float) },
        { .binding = 6, .textureView = pass->lut ? pass->lut : fx->placeholder_3d_view }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = fx->layout,
        .entryCount = 7,
        .entries = entries
    });
    box_gpu_fx_record(gpu, ctx, pass, pipeline, group);
done:
    return pipeline != NULL;
}

box_gpu_fx_state_t* box_gpu_fx_state(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    ecs_entity_t effect,
    int32_t kind)
{
    box_gpu_effects_t *fx = gpu->effects;
    box_gpu_fx_state_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&fx->states); i ++) {
        box_gpu_fx_state_t *s = *ecs_vec_get_t(&fx->states, box_gpu_fx_state_t*, i);
        if (s->target == ctx->t->handle && s->view == ctx->v->view && s->effect == effect &&
            s->kind == kind)
        {
            result = s;
            break;
        }
    }

    if (!result) {
        result = ecs_os_calloc_t(box_gpu_fx_state_t);
        result->target = ctx->t->handle;
        result->view = ctx->v->view;
        result->effect = effect;
        result->kind = kind;
        result->uniforms = box_gpu_fx_buffer(gpu, BOX_GPU_FX_UNIFORMS, WGPUBufferUsage_Uniform);
        *ecs_vec_append_t(NULL, &fx->states, box_gpu_fx_state_t*) = result;
    }

    result->frame = ctx->packet->frame;

    return result;
}

void box_gpu_fx_uniforms(
    box_gpu_t *gpu,
    box_gpu_fx_state_t *s,
    const void *data,
    size_t size)
{
    wgpuQueueWriteBuffer(gpu->queue, s->uniforms, 0, data, size);
}

/** Get the exposure compute pipelines, created again when the exposure shader was recompiled;
 * the previous ones stay until both new ones exist.
 *
 * @param gpu The backend
 * @return Whether both pipelines exist
 */
static bool box_gpu_fx_measure_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPUShaderModule module = NULL;
    int32_t revision = box_gpu_fx_module(gpu, BOX_RENDER_SHADER_EXPOSURE, &module);
    if (!module || (fx->histogram && fx->measure_revision == revision)) {
        goto done;
    }

    WGPUComputePipeline *current[2] = { &fx->histogram, &fx->adapt };
    WGPUComputePipeline next[2] = {
        box_gpu_compute_pipeline(gpu,
            &(WGPUComputePipelineDescriptor){
                .layout = fx->measure_pipeline_layout,
                .compute = { .module = module,
                    .entryPoint = { .data = "histogram_main", .length = WGPU_STRLEN } }
            }),
        box_gpu_compute_pipeline(gpu,
            &(WGPUComputePipelineDescriptor){
                .layout = fx->measure_pipeline_layout,
                .compute = { .module = module,
                    .entryPoint = { .data = "adapt_main", .length = WGPU_STRLEN } }
            })
    };
    if (box_gpu_compute_pipelines_swap(current, next, 2)) {
        fx->measure_revision = revision;
    }

done:
    return module && fx->histogram && fx->adapt;
}

/** Fill the uniforms of the exposure passes from resolved settings.
 *
 * @param a The settings
 * @param delta Seconds the frame advanced
 * @param out Receives the uniforms
 */
static void box_gpu_fx_measure_settings(
    const AutoExposure *a,
    float delta,
    box_gpu_measure_uniforms_t *out)
{
    ecs_os_memset_t(out, 0, box_gpu_measure_uniforms_t);
    out->range[0] = a->min_log_luma;
    out->range[1] = a->max_log_luma - a->min_log_luma;
    out->range[2] = a->low_percentile;
    out->range[3] = a->high_percentile;
    out->adapt[0] = a->speed_up;
    out->adapt[1] = a->speed_down;
    out->adapt[2] = log2f(a->min_brightness);
    out->adapt[3] = log2f(a->max_brightness);
    out->target[0] = a->target_brightness > 0 ? log2f(a->target_brightness) : 0.0f;
    out->target[1] = a->exposure_compensation;
    out->target[2] = a->min_exposure > 0 ? log2f(a->min_exposure) : -BOX_GPU_EXPOSURE_LIMIT;
    out->target[3] = a->max_exposure > 0 ? log2f(a->max_exposure) : BOX_GPU_EXPOSURE_LIMIT;
    out->target[3] = glm_max(out->target[3], out->target[2]);
    out->frame[0] = glm_max(delta, 0.0f);
    out->frame[1] = a->target_brightness > 0 ? 1.0f : 0.0f;
}

/** Measure the brightness of the image and adapt the exposure of an effect.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The effect pass, with an exposure
 * @return The buffer of the adapted exposure, NULL when the exposure shader has no module
 */
static WGPUBuffer box_gpu_fx_measure(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    box_gpu_effects_t *fx = gpu->effects;
    box_gpu_measure_uniforms_t u;
    WGPUBuffer result = NULL;
    if (!box_gpu_fx_measure_pipelines(gpu)) {
        goto done;
    }

    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->exposure, BOX_GPU_FX_MEASURE);
    if (!s->histogram) {
        s->histogram = box_gpu_fx_buffer(gpu,
            BOX_GPU_EXPOSURE_BINS * sizeof(uint32_t), WGPUBufferUsage_Storage);
        s->exposure = box_gpu_fx_buffer(gpu, 4 * sizeof(float), WGPUBufferUsage_Storage);
    }

    uint32_t w = (uint32_t)ctx->ft->width, h = (uint32_t)ctx->ft->height, stride = 1;
    while ((uint64_t)(w / stride) * (h / stride) > BOX_GPU_EXPOSURE_SAMPLES) {
        stride *= 2;
    }

    box_gpu_fx_measure_settings(&e->auto_exposure, ctx->packet->delta, &u);
    u.frame[2] = (float)stride;
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .textureView = ctx->input },
        { .binding = 1, .buffer = s->histogram,
            .size = BOX_GPU_EXPOSURE_BINS * sizeof(uint32_t) },
        { .binding = 2, .buffer = s->exposure, .size = 4 * sizeof(float) },
        { .binding = 3, .buffer = s->uniforms, .size = BOX_GPU_FX_UNIFORMS }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = fx->measure_layout,
        .entryCount = 4,
        .entries = entries
    });
    w = (w + stride - 1) / stride;
    h = (h + stride - 1) / stride;
    WGPUComputePassEncoder cp = box_gpu_profile_compute_pass(gpu, ctx->encoder, "auto exposure");
    wgpuComputePassEncoderSetBindGroup(cp, 0, group, 0, NULL);
    wgpuComputePassEncoderSetPipeline(cp, fx->histogram);
    wgpuComputePassEncoderDispatchWorkgroups(cp,
        (w + BOX_GPU_EXPOSURE_GROUP - 1) / BOX_GPU_EXPOSURE_GROUP,
        (h + BOX_GPU_EXPOSURE_GROUP - 1) / BOX_GPU_EXPOSURE_GROUP, 1);
    wgpuComputePassEncoderSetPipeline(cp, fx->adapt);
    wgpuComputePassEncoderDispatchWorkgroups(cp, 1, 1, 1);
    wgpuComputePassEncoderEnd(cp);
    wgpuComputePassEncoderRelease(cp);
    wgpuBindGroupRelease(group);
    result = s->exposure;
done:
    return result;
}

/** Fill the uniforms of the color grading of a pass.
 *
 * @param gpu The backend
 * @param e The pass
 * @param u Receives the uniforms
 * @return The view of its LUT, NULL without one
 */
static WGPUTextureView box_gpu_fx_grading_fill(
    box_gpu_t *gpu,
    const box_render_effect_t *e,
    box_gpu_grading_uniforms_t *u)
{
    const box_render_grading_t *g = &e->grading;
    const ColorGrading *c = &g->settings;
    WGPUTextureView lut = g->lut && g->lut_size > 1 ? box_gpu_texture_cookie(gpu, g->lut) : NULL;
    float balance[3][3];
    bool curve = false;
    int32_t i, j;
    ecs_os_memset_t(u, 0, box_gpu_grading_uniforms_t);
    box_render_grading_balance(c, balance);
    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 3; j ++) {
            u->balance[i][j] = balance[i][j];
        }

        u->lift[i] = c->lift[i];
        u->gain[i] = c->gain[i];
        u->inv_gamma[i] = 1.0f / c->gamma[i];
        curve = curve || c->lift[i] != 0.0f || c->gamma[i] != 1.0f;
    }

    u->params[0] = c->contrast;
    u->params[1] = c->saturation;
    u->params[2] = c->vibrance;
    u->params[3] = g->display ? 1.0f : 0.0f;
    u->lift[3] = c->lut_strength;
    u->gain[3] = lut ? (float)g->lut_size : 0.0f;
    u->inv_gamma[3] = curve ? 1.0f : 0.0f;

    return lut;
}

/** Fill the uniforms of the vignette of a pass.
 *
 * @param ctx The chain
 * @param e The pass
 * @param u Receives the uniforms
 */
static void box_gpu_fx_vignette_fill(
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e,
    box_gpu_vignette_uniforms_t *u)
{
    const Vignette *v = &e->vignette;
    ecs_os_memset_t(u, 0, box_gpu_vignette_uniforms_t);
    u->params[0] = v->intensity;
    u->params[1] = v->radius;
    u->params[2] = v->smoothness;
    u->params[3] = v->roundness;
    u->shape[0] = v->center[0];
    u->shape[1] = v->center[1];
    u->shape[2] = ctx->ft->height > 0 ? (float)ctx->ft->width / (float)ctx->ft->height : 1.0f;
    u->color[0] = box_gpu_fx_linear(v->color.r);
    u->color[1] = box_gpu_fx_linear(v->color.g);
    u->color[2] = box_gpu_fx_linear(v->color.b);
}

/** Record a tone map or exposure pass, or a copy of the image when the pass is NULL.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass, NULL to copy the image
 * @param grading The color grading pass it applies after the tone map, NULL for none
 * @param vignette The vignette pass it applies last, NULL for none
 * @return Whether the pass was recorded
 */
static bool box_gpu_fx_tonemap(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e,
    const box_render_effect_t *grading,
    const box_render_effect_t *vignette)
{
    box_gpu_tonemap_uniforms_t u;
    WGPUBuffer exposure = e && e->exposure ? box_gpu_fx_measure(gpu, ctx, e) : NULL;
    WGPUTextureView lut = NULL, grading_lut = NULL;
    ecs_os_memset_t(&u, 0, box_gpu_tonemap_uniforms_t);
    if (grading) {
        grading_lut = box_gpu_fx_grading_fill(gpu, grading, &u.grading);
        u.flags[2] |= BOX_GPU_TONEMAP_GRADING;
    }

    if (vignette) {
        box_gpu_fx_vignette_fill(ctx, vignette, &u.vignette);
        u.flags[2] |= BOX_GPU_TONEMAP_VIGNETTE;
    }

    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e ? e->entity : 0,
        e ? (int32_t)e->kind : BOX_GPU_FX_COPY);
    u.flags[0] = exposure ? 1u : 0u;
    if (e && e->kind == BOX_RENDER_EFFECT_TONEMAP) {
        u.flags[1] = 1u;
        if (e->tonemap == TonemapOperatorTonyMcMapface) {
            lut = box_gpu_fx_tony(gpu);
            u.flags[1] = lut ? 2u : 1u;
        }
    }

    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));

    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_TONEMAP,
        .entry = "fs",
        .input = ctx->input,
        .aux = grading_lut,
        .uniforms = s->uniforms,
        .exposure = exposure,
        .lut = lut,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
}

/** Get the blend of a bloom mip into the mip above it, following the boost of the wide mips
 * and the high pass.
 *
 * @param b The settings
 * @param mip The mip
 * @param max_mip The last mip
 * @return The blend
 */
static float box_gpu_fx_bloom_blend(
    const Bloom *b,
    float mip,
    float max_mip)
{
    float result = b->intensity;
    if (max_mip <= 0) {
        goto done;
    }

    float t = mip / max_mip;
    float curve = 1.0f / (1.0f - b->low_frequency_boost_curvature);
    float boost = (1.0f - powf(1.0f - t, curve)) * b->low_frequency_boost;
    float high_pass = 1.0f - glm_clamp(
        (t - b->high_pass_frequency) / b->high_pass_frequency, 0.0f, 1.0f);
    boost *= 1.0f - b->intensity;
    result = (b->intensity + boost) * high_pass;
done:
    return result;
}

/** Make the mip chain of a bloom match the render size and its settings.
 *
 * The first mip is half the render size, or smaller when max_mip_dimension limits it.
 *
 * A smaller first mip gets prefilter levels, the halvings of the image down to the last one
 * at most twice the first mip, so no filter shrinks its input more than twice.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param s The state
 * @param b The settings
 */
static void box_gpu_fx_bloom_mips(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    box_gpu_fx_state_t *s,
    const Bloom *b)
{
    uint32_t rw = (uint32_t)ctx->ft->width, rh = (uint32_t)ctx->ft->height;
    uint32_t longer = rw > rh ? rw : rh;
    uint32_t limit = (uint32_t)b->max_mip_dimension;
    uint32_t hw = rw >> 1 ? rw >> 1 : 1, hh = rh >> 1 ? rh >> 1 : 1;
    uint32_t w = hw, h = hh, shorter, level, count = 1, levels = 0, i;
    if (longer / 2 > limit) {
        w = (uint32_t)(((uint64_t)rw * limit + longer / 2) / longer);
        h = (uint32_t)(((uint64_t)rh * limit + longer / 2) / longer);
        w = w ? w : 1;
        h = h ? h : 1;
    }

    if (s->bloom && s->width == w && s->height == h &&
        s->render_width == rw && s->render_height == rh)
    {
        goto done;
    }

    box_gpu_fx_mips_free(s);
    if (w != hw || h != hh) {
        uint32_t edge = w > h ? w : h;
        level = hw > hh ? hw : hh;
        levels = 1;
        while (level > edge * 2 && levels < BOX_GPU_BLOOM_PREFILTER) {
            levels ++;
            level >>= 1;
        }

        s->prefilter = box_gpu_fx_chain_texture(gpu, hw, hh, levels);
        for (i = 0; i < levels; i ++) {
            s->prefilter_mips[i] = box_gpu_fx_level(s->prefilter, i);
        }
    }

    shorter = w < h ? w : h;
    while (shorter >= BOX_GPU_BLOOM_LEAF * 2 && count < BOX_GPU_BLOOM_MIPS) {
        count ++;
        shorter >>= 1;
    }

    s->bloom = box_gpu_fx_chain_texture(gpu, w, h, count);
    for (i = 0; i < count; i ++) {
        s->mips[i] = box_gpu_fx_level(s->bloom, i);
    }

    if (count > 1) {
        s->up = box_gpu_fx_chain_texture(gpu, w, h, count - 1);
        for (i = 0; i < count - 1; i ++) {
            s->up_mips[i] = box_gpu_fx_level(s->up, i);
        }
    }

    s->mip_count = (int32_t)count;
    s->prefilter_count = (int32_t)levels;
    s->width = w;
    s->height = h;
    s->render_width = rw;
    s->render_height = rh;
done:
    return;
}

/** Fill the uniforms of the bloom passes.
 *
 * @param s The state, with its mip chain
 * @param b The settings
 * @param history Whether the history of the first mip holds the previous frame
 * @param out Receives the uniforms
 */
static void box_gpu_fx_bloom_settings(
    const box_gpu_fx_state_t *s,
    const Bloom *b,
    bool history,
    box_gpu_bloom_uniforms_t *out)
{
    float knee = b->threshold * b->threshold_softness;
    ecs_os_memset_t(out, 0, box_gpu_bloom_uniforms_t);
    out->threshold[0] = b->threshold;
    out->threshold[1] = b->threshold - knee;
    out->threshold[2] = 2.0f * knee;
    out->threshold[3] = 0.25f / (knee + 1e-5f);
    out->params[0] = b->firefly_clamp;
    out->params[1] = box_gpu_fx_bloom_blend(b, 0, (float)(s->mip_count - 1));
    out->params[2] = history ? b->temporal : 0.0f;
    out->first[0] = 1.0f / (float)s->width;
    out->first[1] = 1.0f / (float)s->height;
}

/** Describe the history of the first mip of a bloom.
 *
 * @param ctx The chain
 * @param e The pass
 * @param s The state, with its mip chain
 * @param out Receives the key and shape of the history pair
 */
static void box_gpu_fx_bloom_history_desc(
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e,
    const box_gpu_fx_state_t *s,
    box_gpu_history_desc_t *out)
{
    *out = (box_gpu_history_desc_t){
        .target = ctx->t->handle,
        .view = ctx->v->view,
        .consumer = BOX_GPU_HISTORY_BLOOM,
        .entity = e->entity,
        .dimension = WGPUTextureDimension_2D,
        .width = s->width,
        .height = s->height,
        .depth = 1,
        .format = BOX_GPU_HDR_FORMAT
    };
}

/** Blend the first mip of a bloom with the previous frame into the write image of its history.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param s The state
 * @param desc The key of the history pair
 * @param h The history images of the frame
 * @param first The first mip of the bloom
 * @return The image the composite reads, the first mip when the history pass did not run
 */
static WGPUTextureView box_gpu_fx_bloom_history(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    box_gpu_fx_state_t *s,
    const box_gpu_history_desc_t *desc,
    const box_gpu_history_t *h,
    WGPUTextureView first)
{
    WGPUTextureView result = first;
    bool drawn = box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_BLOOM,
        .entry = "history",
        .input = first,
        .aux = h->read,
        .uniforms = s->uniforms,
        .output = h->write,
        .format = BOX_GPU_HDR_FORMAT,
        .blend = BOX_GPU_FX_REPLACE
    });
    if (drawn) {
        result = h->write;
    } else {
        box_gpu_history_discard(gpu, desc);
    }

    return result;
}

/** Entry points of the bloom shader, by the entry index of a dispatch */
static const char *box_gpu_fx_bloom_entries[BOX_GPU_BLOOM_ENTRIES] = {
    "downsample_first", "downsample_first_tent", "downsample", "downsample_fit", "upsample"
};

/** Create the compute pipelines of the bloom shader when it is new or was recompiled.
 *
 * @param gpu The backend
 * @return Whether every bloom pipeline exists
 */
static bool box_gpu_fx_bloom_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPUShaderModule module = NULL;
    WGPUComputePipeline *current[BOX_GPU_BLOOM_ENTRIES];
    WGPUComputePipeline next[BOX_GPU_BLOOM_ENTRIES];
    int32_t revision = box_gpu_fx_module(gpu, BOX_RENDER_SHADER_BLOOM, &module);
    int32_t i;
    bool result = module != NULL;
    if (!module || (fx->bloom[0] && fx->bloom_revision == revision)) {
        goto done;
    }

    for (i = 0; i < BOX_GPU_BLOOM_ENTRIES; i ++) {
        current[i] = &fx->bloom[i];
        next[i] = box_gpu_compute_pipeline(gpu, &(WGPUComputePipelineDescriptor){
            .layout = fx->bloom_pipeline_layout,
            .compute = { .module = module, .entryPoint = {
                .data = box_gpu_fx_bloom_entries[i], .length = WGPU_STRLEN } }
        });
    }

    if (box_gpu_compute_pipelines_swap(current, next, BOX_GPU_BLOOM_ENTRIES)) {
        fx->bloom_revision = revision;
    }

done:
    for (i = 0; i < BOX_GPU_BLOOM_ENTRIES; i ++) {
        result &= fx->bloom[i] != NULL;
    }

    return result;
}

/** Record one dispatch of a bloom, reusing the bind group the state keeps for its views.
 *
 * A bind group the state has no room for is released after the dispatch.
 *
 * @param gpu The backend
 * @param cp The compute pass
 * @param s The state
 * @param entry Index of the entry point in box_gpu_fx_bloom_entries
 * @param input View it reads
 * @param aux Second view it reads, NULL for none
 * @param output View it writes
 * @param step Upsample step whose blend it reads, 0 for none
 * @param width Width it writes
 * @param height Height it writes
 */
static void box_gpu_fx_bloom_dispatch(
    box_gpu_t *gpu,
    WGPUComputePassEncoder cp,
    box_gpu_fx_state_t *s,
    int32_t entry,
    WGPUTextureView input,
    WGPUTextureView aux,
    WGPUTextureView output,
    int32_t step,
    uint32_t width,
    uint32_t height)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPUBindGroup group = NULL;
    bool transient = false;
    int32_t i;
    for (i = 0; i < s->group_count && !group; i ++) {
        const box_gpu_fx_bloom_group_t *g = &s->groups[i];
        if (g->input == input && g->aux == aux && g->output == output && g->step == step) {
            group = g->group;
        }
    }

    if (!group) {
        WGPUBindGroupEntry entries[6] = {
            { .binding = 0, .textureView = input },
            { .binding = 1, .sampler = fx->sampler },
            { .binding = 2, .buffer = s->uniforms, .size = BOX_GPU_FX_UNIFORMS },
            { .binding = 4, .textureView = aux ? aux : fx->placeholder_view },
            { .binding = 5, .textureView = output },
            { .binding = 6, .buffer = s->steps, .offset = (uint64_t)step * BOX_GPU_BLOOM_STEP,
                .size = 4 * sizeof(float) }
        };
        group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = fx->bloom_layout,
            .entryCount = 6,
            .entries = entries
        });
        if (s->group_count < BOX_GPU_BLOOM_GROUPS) {
            s->groups[s->group_count ++] = (box_gpu_fx_bloom_group_t){
                .input = input, .aux = aux, .output = output, .step = step, .group = group
            };
        } else {
            transient = true;
        }
    }

    width = width ? width : 1;
    height = height ? height : 1;
    wgpuComputePassEncoderSetPipeline(cp, fx->bloom[entry]);
    wgpuComputePassEncoderSetBindGroup(cp, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(cp,
        (width + BOX_GPU_BLOOM_GROUP - 1) / BOX_GPU_BLOOM_GROUP,
        (height + BOX_GPU_BLOOM_GROUP - 1) / BOX_GPU_BLOOM_GROUP, 1);
    if (transient) {
        wgpuBindGroupRelease(group);
    }
}

/** Record the passes of a bloom: halve the image, downsample it through the prefilter levels
 * into the mip chain, upsample back up and add the first mip to the image.
 *
 * The first halving uses a cheaper filter when it is halved again, which smooths it further.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the image was written to the output
 */
static bool box_gpu_fx_bloom(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    const Bloom *b = &e->bloom;
    box_gpu_bloom_uniforms_t u;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    float steps[BOX_GPU_BLOOM_MIPS * BOX_GPU_BLOOM_STEP / sizeof(float)] = {0};
    uint32_t hw, hh;
    int32_t i, last;
    box_gpu_history_desc_t desc;
    box_gpu_history_t h = {0};
    bool result = false, temporal = false;
    box_gpu_fx_bloom_mips(gpu, ctx, s, b);
    hw = s->render_width >> 1;
    hh = s->render_height >> 1;
    if (!box_gpu_fx_bloom_pipelines(gpu)) {
        goto done;
    }

    if (!s->steps) {
        s->steps = box_gpu_fx_buffer(gpu, BOX_GPU_BLOOM_MIPS * BOX_GPU_BLOOM_STEP,
            WGPUBufferUsage_Uniform);
    }

    box_gpu_fx_bloom_history_desc(ctx, e, s, &desc);
    temporal = b->temporal > 0 && box_gpu_history_acquire(gpu, ctx->packet, &desc, &h);
    box_gpu_fx_bloom_settings(s, b, h.valid, &u);
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));
    last = s->mip_count - 1;
    for (i = 1; i <= last; i ++) {
        steps[i * BOX_GPU_BLOOM_STEP / sizeof(float)] =
            box_gpu_fx_bloom_blend(b, (float)i, (float)last);
    }

    wgpuQueueWriteBuffer(gpu->queue, s->steps, 0, steps,
        (size_t)s->mip_count * BOX_GPU_BLOOM_STEP);

    WGPUComputePassEncoder cp = box_gpu_profile_compute_pass(gpu, ctx->encoder, "bloom");
    if (s->prefilter_count) {
        box_gpu_fx_bloom_dispatch(gpu, cp, s, s->prefilter_count > 1 ? 1 : 0, ctx->input,
            NULL, s->prefilter_mips[0], 0, hw, hh);
        for (i = 1; i < s->prefilter_count; i ++) {
            box_gpu_fx_bloom_dispatch(gpu, cp, s, 2, s->prefilter_mips[i - 1], NULL,
                s->prefilter_mips[i], 0, hw >> i, hh >> i);
        }

        box_gpu_fx_bloom_dispatch(gpu, cp, s, 3, s->prefilter_mips[s->prefilter_count - 1],
            NULL, s->mips[0], 0, s->width, s->height);
    } else {
        box_gpu_fx_bloom_dispatch(gpu, cp, s, 0, ctx->input, NULL, s->mips[0], 0,
            s->width, s->height);
    }

    for (i = 1; i <= last; i ++) {
        box_gpu_fx_bloom_dispatch(gpu, cp, s, 2, s->mips[i - 1], NULL, s->mips[i], 0,
            s->width >> i, s->height >> i);
    }

    for (i = last; i > 0; i --) {
        box_gpu_fx_bloom_dispatch(gpu, cp, s, 4, i == last ? s->mips[i] : s->up_mips[i],
            s->mips[i - 1], s->up_mips[i - 1], i, s->width >> (i - 1), s->height >> (i - 1));
    }

    wgpuComputePassEncoderEnd(cp);
    wgpuComputePassEncoderRelease(cp);

    WGPUTextureView first = last > 0 ? s->up_mips[0] : s->mips[0];
    WGPUTextureView glow = temporal
        ? box_gpu_fx_bloom_history(gpu, ctx, s, &desc, &h, first)
        : first;
    result = box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_BLOOM,
        .entry = "composite",
        .input = ctx->input,
        .aux = glow,
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
done:
    return result;
}

/** Record an SMAA pass, with the bindings of the SMAA layout.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param pass The pass
 * @param edges The edge image the pass reads, NULL when it writes it
 * @param weights The weight image the pass reads, NULL when it does not read it
 * @return Whether the pass was recorded, false while the shader has no module
 */
static bool box_gpu_fx_smaa_draw(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_gpu_fx_pass_t *pass,
    WGPUTextureView edges,
    WGPUTextureView weights)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPURenderPipeline pipeline = box_gpu_fx_pipeline(gpu, pass);
    if (!pipeline) {
        goto done;
    }

    WGPUBindGroupEntry entries[BOX_GPU_SMAA_BINDINGS] = {
        { .binding = 0, .textureView = pass->input },
        { .binding = 1, .sampler = fx->sampler },
        { .binding = 2, .buffer = pass->uniforms, .size = BOX_GPU_FX_UNIFORMS },
        { .binding = 3, .textureView = edges ? edges : fx->placeholder_view },
        { .binding = 4, .textureView = fx->smaa_area_view },
        { .binding = 5, .textureView = fx->smaa_search_view },
        { .binding = 6, .textureView = weights ? weights : fx->placeholder_view }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = fx->smaa_layout,
        .entryCount = BOX_GPU_SMAA_BINDINGS,
        .entries = entries
    });
    box_gpu_fx_record(gpu, ctx, pass, pipeline, group);
done:
    return pipeline != NULL;
}

/** Create the area and search textures of the device and the edge and weight images of the
 * target when they do not exist yet.
 *
 * @param gpu The backend
 * @param ft The images of the target, at its render size
 */
static void box_gpu_fx_smaa_images(
    box_gpu_t *gpu,
    box_gpu_fx_target_t *ft)
{
    box_gpu_effects_t *fx = gpu->effects;
    if (!fx->smaa_area) {
        fx->smaa_area = box_gpu_smaa_area(gpu);
        fx->smaa_area_view = wgpuTextureCreateView(fx->smaa_area, NULL);
        fx->smaa_search = box_gpu_smaa_search(gpu);
        fx->smaa_search_view = wgpuTextureCreateView(fx->smaa_search, NULL);
    }

    if (!ft->smaa_edges) {
        ft->smaa_edges = box_gpu_fx_texture(gpu, (uint32_t)ft->width, (uint32_t)ft->height,
            BOX_GPU_SMAA_EDGES_FORMAT, 1);
        ft->smaa_edges_view = wgpuTextureCreateView(ft->smaa_edges, NULL);
        ft->smaa_weights = box_gpu_fx_texture(gpu, (uint32_t)ft->width, (uint32_t)ft->height,
            BOX_GPU_SMAA_WEIGHTS_FORMAT, 1);
        ft->smaa_weights_view = wgpuTextureCreateView(ft->smaa_weights, NULL);
    }
}

/** Record the passes of SMAA 1x: the color edges of the image into the edge image, the
 * blending weights of the edges into the weight image, then the image blended with its
 * neighbors by the weights into the output.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the image was written to the output
 */
static bool box_gpu_fx_smaa(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    box_gpu_fx_target_t *ft = ctx->ft;
    const Smaa *a = &e->smaa;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    float w = (float)(ft->width > 0 ? ft->width : 1);
    float h = (float)(ft->height > 0 ? ft->height : 1);
    box_gpu_smaa_uniforms_t u = {
        .metrics = { 1.0f / w, 1.0f / h, w, h },
        .params = { a->threshold, (float)a->max_search_steps,
            (float)a->max_search_steps_diag, (float)a->corner_rounding / 100.0f }
    };
    bool result = false;
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));
    box_gpu_fx_smaa_images(gpu, ft);
    box_gpu_fx_pass_t pass = {
        .shader = BOX_RENDER_SHADER_SMAA,
        .vertex = "edges_vs",
        .entry = "edges",
        .input = ctx->input,
        .uniforms = s->uniforms,
        .output = ft->smaa_edges_view,
        .format = BOX_GPU_SMAA_EDGES_FORMAT,
        .blend = BOX_GPU_FX_REPLACE
    };
    if (!box_gpu_fx_smaa_draw(gpu, ctx, &pass, NULL, NULL)) {
        goto done;
    }

    pass.vertex = "weights_vs";
    pass.entry = "weights";
    pass.output = ft->smaa_weights_view;
    pass.format = BOX_GPU_SMAA_WEIGHTS_FORMAT;
    if (!box_gpu_fx_smaa_draw(gpu, ctx, &pass, ft->smaa_edges_view, NULL)) {
        goto done;
    }

    pass.vertex = "blend_vs";
    pass.entry = "blend";
    pass.output = ctx->output;
    pass.format = ctx->format;
    pass.blend = ctx->blend;
    result = box_gpu_fx_smaa_draw(gpu, ctx, &pass, ft->smaa_edges_view,
        ft->smaa_weights_view);
done:
    return result;
}

/** Get the HDR images of a target, created or resized to its render size.
 *
 * @param gpu The backend
 * @param t The target
 * @return The images
 */
static box_gpu_fx_target_t* box_gpu_fx_target(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    box_gpu_effects_t *fx = gpu->effects;
    box_gpu_fx_target_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&fx->targets); i ++) {
        box_gpu_fx_target_t *ft = *ecs_vec_get_t(&fx->targets, box_gpu_fx_target_t*, i);
        if (ft->target == t->handle) {
            result = ft;
            break;
        }
    }

    if (!result) {
        result = ecs_os_calloc_t(box_gpu_fx_target_t);
        result->target = t->handle;
        *ecs_vec_append_t(NULL, &fx->targets, box_gpu_fx_target_t*) = result;
    }

    if (result->hdr[0] && result->width == t->desc.render_width &&
        result->height == t->desc.render_height)
    {
        goto done;
    }

    box_gpu_fx_release(&result->hdr[0], &result->hdr_views[0]);
    box_gpu_fx_release(&result->hdr[1], &result->hdr_views[1]);
    box_gpu_fx_release(&result->smaa_edges, &result->smaa_edges_view);
    box_gpu_fx_release(&result->smaa_weights, &result->smaa_weights_view);
    box_gpu_fx_dof_release(result);
    result->width = t->desc.render_width;
    result->height = t->desc.render_height;
    for (i = 0; i < 2; i ++) {
        result->hdr[i] = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                WGPUTextureUsage_CopySrc,
            .dimension = WGPUTextureDimension_2D,
            .size = { (uint32_t)result->width, (uint32_t)result->height, 1 },
            .format = BOX_GPU_HDR_FORMAT,
            .mipLevelCount = 1,
            .sampleCount = 1
        });
        result->hdr_views[i] = wgpuTextureCreateView(result->hdr[i], NULL);
    }

done:
    return result;
}

WGPUTextureView box_gpu_effects_begin(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    WGPUTextureView result = NULL;
    box_gpu_effects_t *fx = gpu->effects;
    if (!fx || v->effect_count <= 0 ||
        v->effect_first + v->effect_count > ecs_vec_count(&packet->effects))
    {
        goto done;
    }

    fx->current = box_gpu_fx_target(gpu, t);
    fx->current->frame = packet->frame;
    result = fx->current->hdr_views[0];
done:
    return result;
}

WGPUTexture box_gpu_effects_image(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;

    return fx && fx->current ? fx->current->hdr[0] : NULL;
}

/** Record a color grading pass, with its strip LUT once the backend has the image.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the pass was recorded
 */
static bool box_gpu_fx_grading(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    box_gpu_grading_uniforms_t u;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    WGPUTextureView lut = box_gpu_fx_grading_fill(gpu, e, &u);
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));

    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_GRADING,
        .entry = "fs",
        .input = ctx->input,
        .aux = lut,
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
}

/** Record a vignette pass.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the pass was recorded
 */
static bool box_gpu_fx_vignette(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    box_gpu_vignette_uniforms_t u;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    box_gpu_fx_vignette_fill(ctx, e, &u);
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));

    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_VIGNETTE,
        .entry = "fs",
        .input = ctx->input,
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
}

/** Record one effect pass of a chain.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the pass wrote its output
 */
static bool box_gpu_fx_effect(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    bool result = false;
    switch (e->kind) {
    case BOX_RENDER_EFFECT_TONEMAP:
    case BOX_RENDER_EFFECT_EXPOSURE:
        result = box_gpu_fx_tonemap(gpu, ctx, e, NULL, NULL);
        break;
    case BOX_RENDER_EFFECT_BLOOM:
        result = box_gpu_fx_bloom(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_FOG:
        result = box_gpu_fx_fog(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_SMAA:
        result = box_gpu_fx_smaa(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_FXAA:
        result = box_gpu_fx_fxaa(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_GRADING:
        result = box_gpu_fx_grading(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_VIGNETTE:
        result = box_gpu_fx_vignette(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_CHROMATIC_ABERRATION:
        result = box_gpu_fx_chromatic_aberration(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_DEPTH_OF_FIELD:
        result = box_gpu_fx_depth_of_field(gpu, ctx, e);
        break;
    default:
        break;
    }

    return result;
}

/** Get the color grading and vignette passes that follow a tone map pass of a view, which the
 * tone map pass applies itself.
 *
 * @param packet The packet
 * @param v The view
 * @param i Index of the pass among the effects of the view
 * @param grading Receives the grading pass, NULL for none
 * @param vignette Receives the vignette pass, NULL for none
 * @return Passes after the pass that it applies
 */
static int32_t box_gpu_fx_fused(
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t i,
    const box_render_effect_t **grading,
    const box_render_effect_t **vignette)
{
    const box_render_effect_t *e = ecs_vec_get_t(
        &packet->effects, box_render_effect_t, v->effect_first + i);
    int32_t result = 0;
    if (e->kind != BOX_RENDER_EFFECT_TONEMAP) {
        goto done;
    }

    if (i + result + 1 < v->effect_count && e[result + 1].kind == BOX_RENDER_EFFECT_GRADING) {
        result ++;
        *grading = &e[result];
    }

    if (i + result + 1 < v->effect_count && e[result + 1].kind == BOX_RENDER_EFFECT_VIGNETTE) {
        result ++;
        *vignette = &e[result];
    }

done:
    return result;
}

void box_gpu_effects_run(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureView color)
{
    box_gpu_effects_t *fx = gpu->effects;
    box_gpu_fx_target_t *ft = fx ? fx->current : NULL;
    box_gpu_fx_blend_t final = v->clear.enabled ? BOX_GPU_FX_REPLACE : BOX_GPU_FX_OVER;
    int32_t i, current = 0;
    bool written = false;
    if (!ft) {
        goto done;
    }

    box_gpu_fx_ctx_t ctx = { .t = t, .ft = ft, .encoder = encoder, .packet = packet, .v = v };
    for (i = 0; i < v->effect_count; i ++) {
        const box_render_effect_t *e = ecs_vec_get_t(
            &packet->effects, box_render_effect_t, v->effect_first + i);
        bool last = i == v->effect_count - 1;
        const box_render_effect_t *grading = NULL, *vignette = NULL;
        int32_t fused = box_gpu_fx_fused(packet, v, i, &grading, &vignette);
        if (fused) {
            last = i + fused == v->effect_count - 1;
        }

        ctx.input = ft->hdr_views[current];
        ctx.output = last ? color : ft->hdr_views[current ^ 1];
        ctx.format = last ? t->format : BOX_GPU_HDR_FORMAT;
        ctx.blend = last ? final : BOX_GPU_FX_REPLACE;
        if (!i && e->kind == BOX_RENDER_EFFECT_FOG && box_gpu_fog_volumes_height_fog(gpu, t, v)) {
            continue;
        }

        if (fused && box_gpu_fx_tonemap(gpu, &ctx, e, grading, vignette)) {
            i += fused;
        } else if (fused || !box_gpu_fx_effect(gpu, &ctx, e)) {
            continue;
        }

        if (last) {
            written = true;
        } else {
            current ^= 1;
        }
    }

    if (!written) {
        ctx.input = ft->hdr_views[current];
        ctx.output = color;
        ctx.format = t->format;
        ctx.blend = final;
        box_gpu_fx_tonemap(gpu, &ctx, NULL, NULL, NULL);
    }

    fx->current = NULL;
done:
    return;
}

void box_gpu_effects_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_effects_t *fx = gpu->effects;
    int32_t i;
    if (!fx) {
        goto done;
    }

    for (i = ecs_vec_count(&fx->states) - 1; i >= 0; i --) {
        box_gpu_fx_state_t *s = *ecs_vec_get_t(&fx->states, box_gpu_fx_state_t*, i);
        if (s->frame != packet->frame) {
            box_gpu_fx_state_free(s);
            ecs_vec_remove_t(&fx->states, box_gpu_fx_state_t*, i);
        }
    }

    for (i = ecs_vec_count(&fx->targets) - 1; i >= 0; i --) {
        box_gpu_fx_target_t *ft = *ecs_vec_get_t(&fx->targets, box_gpu_fx_target_t*, i);
        if (ft->frame != packet->frame) {
            box_gpu_fx_target_free(ft);
            ecs_vec_remove_t(&fx->targets, box_gpu_fx_target_t*, i);
        }
    }

done:
    box_gpu_gtao_sweep(gpu, packet);
}

#endif
