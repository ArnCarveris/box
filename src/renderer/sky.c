/**
 * @file renderer/sky.c
 * @brief Renders the tables of the atmosphere and draws it over the image of a view.
 *
 * The three table passes run once per frame, as often as the packet says they
 * are out of date, and the sky pass then runs once per view: it writes the sky
 * view table where the depth target of the view is empty and the light the air
 * in front of a surface scattered where it is not, blended with what is left of
 * that surface. The same tables fill the environment cubemap of a view whose
 * image based lighting names the atmosphere.
 */

#include "renderer_internal.h"
#include <stdio.h>

/** Faces of one table pass, which covers its target with one triangle */
#define BOX_SKY_VERTICES (3)

/** Blend that puts premultiplied in scattered light over what a view drew.
 *
 * The pass writes the radiance the air added and, in the alpha channel, how
 * much of the image behind it the air swallowed, so one equation covers the
 * sky, which swallows all of it, and the haze over a surface, which swallows
 * part of it.
 */
static const WGPUBlendState box_sky_blend = {
    .color = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
        WGPUBlendFactor_OneMinusSrcAlpha },
    .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
        WGPUBlendFactor_OneMinusSrcAlpha }
};

/** Create one table of the atmosphere and its view.
 *
 * @param r The renderer
 * @param width Width in texels
 * @param height Height in texels
 * @param texture Receives the texture
 * @param view Receives the view
 */
static void box_sky_table(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height,
    WGPUTexture *texture,
    WGPUTextureView *view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = BOX_SKY_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    *texture = wgpuDeviceCreateTexture(r->device, &desc);
    *view = wgpuTextureCreateView(*texture, NULL);
}

/** Create the layout the three table passes and the sky pass share.
 *
 * @param r The renderer
 */
static void box_sky_layouts(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[BOX_SKY_BINDINGS] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_sky_uniform_t)
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = { .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = { .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }, {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = { .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D }
        }, {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .texture = { .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D }
        }
    };
    WGPUBindGroupLayoutDescriptor lut_desc = { .entryCount = 4, .entries = entries };
    r->sky.lut_layout = wgpuDeviceCreateBindGroupLayout(r->device, &lut_desc);

    WGPUBindGroupLayoutDescriptor sky_desc = {
        .entryCount = BOX_SKY_BINDINGS,
        .entries = entries
    };
    r->sky.layout = wgpuDeviceCreateBindGroupLayout(r->device, &sky_desc);

    WGPUBindGroupLayoutEntry env_entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_env_uniform_t)
            }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_sky_uniform_t)
            }
        }, {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = { .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D }
        }, {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor env_desc = { .entryCount = 4, .entries = env_entries };
    r->sky.env_layout = wgpuDeviceCreateBindGroupLayout(r->device, &env_desc);
}

/** Bind group of one table pass, with the table it writes left unbound.
 *
 * @param r The renderer
 * @param first The table bound at the first slot
 * @param second The table bound at the second slot
 * @return The bind group
 */
static WGPUBindGroup box_sky_lut_group(
    box_renderer_t *r,
    WGPUTextureView first,
    WGPUTextureView second)
{
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = r->sky.uniform, .size = sizeof(box_sky_uniform_t) },
        { .binding = 1, .textureView = first },
        { .binding = 2, .textureView = second },
        { .binding = 3, .sampler = r->sky.sampler }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->sky.lut_layout,
        .entryCount = 4,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

/** Create the tables, their bind groups and the environment bind group.
 *
 * @param r The renderer
 */
static void box_sky_resources(
    box_renderer_t *r)
{
    box_sky_table(r, BOX_SKY_TRANS_W, BOX_SKY_TRANS_H, &r->sky.trans, &r->sky.trans_view);
    box_sky_table(r, BOX_SKY_MS_W, BOX_SKY_MS_H, &r->sky.multi, &r->sky.multi_view);
    box_sky_table(r, BOX_SKY_VIEW_W, BOX_SKY_VIEW_H, &r->sky.view, &r->sky.view_view);
    box_sky_table(r, 1, 1, &r->sky.dummy, &r->sky.dummy_view);

    r->sky.trans_group = box_sky_lut_group(r, r->sky.dummy_view, r->sky.dummy_view);
    r->sky.multi_group = box_sky_lut_group(r, r->sky.trans_view, r->sky.dummy_view);
    r->sky.view_group = box_sky_lut_group(r, r->sky.trans_view, r->sky.multi_view);

    WGPUBindGroupEntry env_entries[4] = {
        { .binding = 0, .buffer = r->env_uniform, .size = sizeof(box_env_uniform_t) },
        { .binding = 3, .buffer = r->sky.uniform, .size = sizeof(box_sky_uniform_t) },
        { .binding = 4, .textureView = r->sky.view_view },
        { .binding = 5, .sampler = r->sky.sampler }
    };
    WGPUBindGroupDescriptor env_desc = {
        .layout = r->sky.env_layout,
        .entryCount = 4,
        .entries = env_entries
    };
    r->sky.env_group = wgpuDeviceCreateBindGroup(r->device, &env_desc);
    r->sky.epoch ++;
}

/** Create a pipeline of one entry point of a sky shader.
 *
 * @param r The renderer
 * @param module The compiled module
 * @param entry Name of the fragment entry point
 * @param layout The bind group layout of the pass
 * @param format Format of its target
 * @param blend Blend state, NULL to replace what the target held
 * @return The pipeline, NULL when it does not build
 */
static WGPURenderPipeline box_sky_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    const char *entry,
    WGPUBindGroupLayout layout,
    WGPUTextureFormat format,
    const WGPUBlendState *blend)
{
    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &layout_desc);

    WGPUColorTargetState target = {
        .format = format,
        .blend = blend,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = entry, .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN }
        },
        .fragment = &fragment,
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    r->device_error = false;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    if (r->device_error && result) {
        wgpuRenderPipelineRelease(result);
        result = NULL;
    }

    return result;
}

/** Release every pipeline the sky shaders built.
 *
 * @param r The renderer
 */
static void box_sky_pipelines_fini(
    box_renderer_t *r)
{
    if (r->sky.trans_pipeline) wgpuRenderPipelineRelease(r->sky.trans_pipeline);
    if (r->sky.multi_pipeline) wgpuRenderPipelineRelease(r->sky.multi_pipeline);
    if (r->sky.view_pipeline) wgpuRenderPipelineRelease(r->sky.view_pipeline);
    if (r->sky.env_pipeline) wgpuRenderPipelineRelease(r->sky.env_pipeline);
    r->sky.trans_pipeline = NULL;
    r->sky.multi_pipeline = NULL;
    r->sky.view_pipeline = NULL;
    r->sky.env_pipeline = NULL;

    uint32_t i;
    for (i = 0; i < BOX_SKY_FORMATS; i ++) {
        if (r->sky.pipeline[i]) wgpuRenderPipelineRelease(r->sky.pipeline[i]);
        r->sky.pipeline[i] = NULL;
    }

    r->sky.tables = false;
}

/** Build the table and environment pipelines from the table shader of a packet.
 *
 * @param r The renderer
 * @param handle The shader handle of the table passes
 */
static void box_sky_build_tables(
    box_renderer_t *r,
    uint32_t handle)
{
    WGPUShaderModule module = box_shader_module(r, handle);
    if (!module) {
        goto done;
    }

    r->sky.trans_pipeline = box_sky_pipeline(r, module, "fs_transmittance",
        r->sky.lut_layout, BOX_SKY_FORMAT, NULL);
    r->sky.multi_pipeline = box_sky_pipeline(r, module, "fs_multiscatter",
        r->sky.lut_layout, BOX_SKY_FORMAT, NULL);
    r->sky.view_pipeline = box_sky_pipeline(r, module, "fs_view",
        r->sky.lut_layout, BOX_SKY_FORMAT, NULL);
    wgpuShaderModuleRelease(module);

    WGPUShaderModule env = box_shader_module(r, BOX_SHADER_ENV);
    if (env) {
        r->sky.env_pipeline = box_sky_pipeline(r, env, "fs_sky", r->sky.env_layout,
            BOX_SKY_FORMAT, NULL);
        wgpuShaderModuleRelease(env);
    }

done:
    return;
}

/** Build the sky pass from the sky shader of a packet, one per target format.
 *
 * @param r The renderer
 * @param handle The shader handle of the sky pass
 */
static void box_sky_build_pass(
    box_renderer_t *r,
    uint32_t handle)
{
    WGPUShaderModule module = box_shader_module(r, handle);
    if (!module) {
        goto done;
    }

    r->sky.pipeline[0] = box_sky_pipeline(r, module, "fs_main", r->sky.layout,
        r->format, &box_sky_blend);
    r->sky.pipeline[1] = box_sky_pipeline(r, module, "fs_main", r->sky.layout,
        BOX_EFFECT_HDR_FORMAT, &box_sky_blend);
    wgpuShaderModuleRelease(module);
done:
    return;
}

/** Make sure the pipelines of the sky match the shaders a packet carries.
 *
 * @param r The renderer
 * @param p The packet
 * @return true when every pass of the atmosphere can run
 */
static bool box_sky_ensure(
    box_renderer_t *r,
    const box_packet_t *p)
{
    bool result = false;
    if (p->sky_lut_shader == BOX_SHADER_NONE || p->sky_shader == BOX_SHADER_NONE) {
        goto done;
    }

    if (r->sky.lut_shader != p->sky_lut_shader || r->sky.shader != p->sky_shader) {
        box_sky_pipelines_fini(r);
        if (!r->sky.trans) {
            box_sky_resources(r);
        }

        r->sky.lut_shader = p->sky_lut_shader;
        r->sky.shader = p->sky_shader;
        box_sky_build_tables(r, p->sky_lut_shader);
        box_sky_build_pass(r, p->sky_shader);
    }

    result = r->sky.trans_pipeline && r->sky.multi_pipeline &&
        r->sky.view_pipeline && r->sky.pipeline[0] && r->sky.pipeline[1];
    if (!result && !r->sky.failed) {
        fprintf(stderr, "box: the sky shaders do not compile, the sky is not drawn\n");
        r->sky.failed = true;
    }

done:
    return result;
}

/** Record one table pass over the whole of its target.
 *
 * @param encoder The command encoder
 * @param pipeline The pipeline of the pass
 * @param group The bind group of the pass
 * @param target The table it fills
 */
static void box_sky_pass(
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group,
    WGPUTextureView target)
{
    WGPURenderPassColorAttachment color = {
        .view = target,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, BOX_SKY_VERTICES, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Fill the uniform block of the atmosphere for one view.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 * @param hdr The passes of the view write the float scene target
 * @param out The block to fill
 */
static void box_sky_uniform(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    bool hdr,
    box_sky_uniform_t *out)
{
    memcpy(out->inv_clip, view->inv_clip, sizeof(out->inv_clip));
    out->camera[0] = view->eye[0];
    out->camera[1] = view->eye[1];
    out->camera[2] = view->eye[2];
    out->sky = p->sky;
    out->camera[3] = out->sky.world_scale[3] +
        (view->eye[1] - out->sky.world_scale[2]) * out->sky.world_scale[1];
    out->sky.sun_dir[3] = box_view_gamma(hdr, box_renderer_srgb(r));
}

void box_sky_update(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder)
{
    if (!view->sky || !p->sky.active || !box_sky_ensure(r, p)) {
        goto done;
    }

    bool hdr = r->effect_scene != NULL;
    box_sky_uniform_t uniform;
    box_sky_uniform(r, view, p, hdr, &uniform);
    if (!r->v->sky_uniform) {
        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(box_sky_uniform_t)
        };
        r->v->sky_uniform = wgpuDeviceCreateBuffer(r->device, &desc);
    }

    wgpuQueueWriteBuffer(r->gpu_queue, r->v->sky_uniform, 0, &uniform, sizeof(uniform));
    if (r->sky.tables && r->sky.generation == p->sky.generation) {
        goto done;
    }

    wgpuQueueWriteBuffer(r->gpu_queue, r->sky.uniform, 0, &uniform, sizeof(uniform));
    if (p->sky.lut_dirty || !r->sky.tables) {
        box_sky_pass(encoder, r->sky.trans_pipeline, r->sky.trans_group,
            r->sky.trans_view);
        box_sky_pass(encoder, r->sky.multi_pipeline, r->sky.multi_group,
            r->sky.multi_view);
    }

    box_sky_pass(encoder, r->sky.view_pipeline, r->sky.view_group, r->sky.view_view);
    r->sky.generation = p->sky.generation;
    r->sky.tables = true;
done:
    return;
}

/** Bind group of the sky pass of the current view, created when it is missing.
 *
 * @param r The renderer
 * @return The bind group
 */
static WGPUBindGroup box_sky_group(
    box_renderer_t *r)
{
    if (r->v->sky_group && r->v->sky_epoch == r->sky.epoch) {
        goto done;
    }

    if (r->v->sky_group) {
        wgpuBindGroupRelease(r->v->sky_group);
    }

    WGPUBindGroupEntry entries[BOX_SKY_BINDINGS] = {
        { .binding = 0, .buffer = r->v->sky_uniform, .size = sizeof(box_sky_uniform_t) },
        { .binding = 1, .textureView = r->sky.trans_view },
        { .binding = 2, .textureView = r->sky.multi_view },
        { .binding = 3, .sampler = r->sky.sampler },
        { .binding = 4, .textureView = r->sky.view_view },
        { .binding = 5, .textureView = r->v->depth_sample }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->sky.layout,
        .entryCount = BOX_SKY_BINDINGS,
        .entries = entries
    };
    r->v->sky_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    r->v->sky_epoch = r->sky.epoch;
done:
    return r->v->sky_group;
}

void box_sky_record(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTextureView attachment)
{
    if (!view->sky || !p->sky.active || !r->sky.tables || !r->v->sky_uniform) {
        goto done;
    }

    bool hdr = r->effect_scene != NULL;
    WGPURenderPipeline pipeline = r->sky.pipeline[hdr ? 1 : 0];
    if (!pipeline) {
        goto done;
    }

    WGPURenderPassColorAttachment color = {
        .view = attachment,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, box_sky_group(r), 0, NULL);
    wgpuRenderPassEncoderDraw(pass, BOX_SKY_VERTICES, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
done:
    return;
}

bool box_sky_environment(
    box_renderer_t *r,
    const box_view_t *view)
{
    return view->sky_env && r->sky.tables && r->sky.env_pipeline != NULL;
}

void box_sky_environment_face(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTextureView target,
    uint32_t index)
{
    WGPURenderPassColorAttachment color = {
        .view = target,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    uint32_t offset = index * BOX_ENV_STRIDE;
    wgpuRenderPassEncoderSetPipeline(pass, r->sky.env_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->sky.env_group, 1, &offset);
    wgpuRenderPassEncoderDraw(pass, BOX_SKY_VERTICES, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void box_sky_invalidate(
    box_renderer_t *r,
    uint32_t handle)
{
    if (handle == r->sky.lut_shader || handle == r->sky.shader ||
        handle == BOX_SHADER_ENV)
    {
        box_sky_pipelines_fini(r);
        r->sky.lut_shader = BOX_SHADER_NONE;
        r->sky.shader = BOX_SHADER_NONE;
        r->sky.failed = false;
    }
}

void box_sky_init(
    box_renderer_t *r)
{
    r->sky.lut_shader = BOX_SHADER_NONE;
    r->sky.shader = BOX_SHADER_NONE;
    box_sky_layouts(r);

    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .maxAnisotropy = 1
    };
    r->sky.sampler = wgpuDeviceCreateSampler(r->device, &sampler_desc);

    WGPUBufferDescriptor buffer_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(box_sky_uniform_t)
    };
    r->sky.uniform = wgpuDeviceCreateBuffer(r->device, &buffer_desc);
}

void box_sky_view_release(
    box_gpu_view_t *v)
{
    if (v->sky_group) wgpuBindGroupRelease(v->sky_group);
    if (v->sky_uniform) wgpuBufferRelease(v->sky_uniform);
    v->sky_group = NULL;
    v->sky_uniform = NULL;
    v->sky_epoch = 0;
}

void box_sky_fini(
    box_renderer_t *r)
{
    box_sky_pipelines_fini(r);
    if (r->sky.trans_group) wgpuBindGroupRelease(r->sky.trans_group);
    if (r->sky.multi_group) wgpuBindGroupRelease(r->sky.multi_group);
    if (r->sky.view_group) wgpuBindGroupRelease(r->sky.view_group);
    if (r->sky.env_group) wgpuBindGroupRelease(r->sky.env_group);
    if (r->sky.trans_view) wgpuTextureViewRelease(r->sky.trans_view);
    if (r->sky.multi_view) wgpuTextureViewRelease(r->sky.multi_view);
    if (r->sky.view_view) wgpuTextureViewRelease(r->sky.view_view);
    if (r->sky.dummy_view) wgpuTextureViewRelease(r->sky.dummy_view);
    if (r->sky.trans) wgpuTextureRelease(r->sky.trans);
    if (r->sky.multi) wgpuTextureRelease(r->sky.multi);
    if (r->sky.view) wgpuTextureRelease(r->sky.view);
    if (r->sky.dummy) wgpuTextureRelease(r->sky.dummy);
    if (r->sky.lut_layout) wgpuBindGroupLayoutRelease(r->sky.lut_layout);
    if (r->sky.layout) wgpuBindGroupLayoutRelease(r->sky.layout);
    if (r->sky.env_layout) wgpuBindGroupLayoutRelease(r->sky.env_layout);
    if (r->sky.sampler) wgpuSamplerRelease(r->sky.sampler);
    if (r->sky.uniform) wgpuBufferRelease(r->sky.uniform);
    memset(&r->sky, 0, sizeof(box_sky_gpu_t));
}
