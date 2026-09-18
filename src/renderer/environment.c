/**
 * @file renderer/environment.c
 * @brief Environment cubemap, its prefiltered mip chain, irradiance and BRDF table.
 *
 * The gradient of the view is rendered into a cubemap, prefiltered per
 * roughness for specular and convolved with a cosine lobe for diffuse; all
 * three only rerun when the environment generation of the packet changes.
 */

#include "renderer_internal.h"

/** Format every environment texture uses */
#define BOX_ENV_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Create a render target texture of the environment.
 *
 * @param r The renderer
 * @param size Edge length in pixels
 * @param mips Mip levels
 * @param layers Array layers, BOX_ENV_FACES for a cubemap
 * @return The texture
 */
static WGPUTexture box_env_texture(
    box_renderer_t *r,
    uint32_t size,
    uint32_t mips,
    uint32_t layers)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { size, size, layers },
        .format = BOX_ENV_FORMAT,
        .mipLevelCount = mips,
        .sampleCount = 1
    };

    return wgpuDeviceCreateTexture(r->device, &desc);
}

/** Create the view a shader samples a texture through.
 *
 * @param texture The texture
 * @param dimension View dimension
 * @param mips Mip levels the view covers
 * @param layers Array layers the view covers
 * @return The view
 */
static WGPUTextureView box_env_view(
    WGPUTexture texture,
    WGPUTextureViewDimension dimension,
    uint32_t mips,
    uint32_t layers)
{
    WGPUTextureViewDescriptor desc = {
        .format = BOX_ENV_FORMAT,
        .dimension = dimension,
        .mipLevelCount = mips,
        .arrayLayerCount = layers,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &desc);
}

/** Create the render target view of one face of one mip.
 *
 * @param texture The texture
 * @param face Array layer
 * @param mip Mip level
 * @return The view
 */
static WGPUTextureView box_env_face(
    WGPUTexture texture,
    uint32_t face,
    uint32_t mip)
{
    WGPUTextureViewDescriptor desc = {
        .format = BOX_ENV_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = mip,
        .mipLevelCount = 1,
        .baseArrayLayer = face,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &desc);
}

/** Create a full screen pipeline that writes one environment target.
 *
 * @param r The renderer
 * @param module The shader module
 * @param entry Name of the fragment entry point
 * @param layout The bind group layout of the pass
 * @return The pipeline
 */
static WGPURenderPipeline box_env_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    const char *entry,
    WGPUBindGroupLayout layout)
{
    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &layout_desc);

    WGPUColorTargetState target = {
        .format = BOX_ENV_FORMAT,
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
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);

    return result;
}

/** Create the bind group layouts of the prefilter passes and the mesh shader.
 *
 * @param r The renderer
 */
static void box_env_layouts(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry uniform = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = true,
            .minBindingSize = sizeof(box_env_uniform_t)
        }
    };
    WGPUBindGroupLayoutDescriptor pass_desc = { .entryCount = 1, .entries = &uniform };
    r->env_pass_layout = wgpuDeviceCreateBindGroupLayout(r->device, &pass_desc);

    WGPUBindGroupLayoutEntry filter_entries[3] = {
        uniform, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube
            }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor filter_desc = { .entryCount = 3, .entries = filter_entries };
    r->env_filter_layout = wgpuDeviceCreateBindGroupLayout(r->device, &filter_desc);

    WGPUBindGroupLayoutEntry mesh_entries[BOX_MESH_BINDINGS] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube
            }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    box_texture_bind_layout(mesh_entries + BOX_ENV_BINDINGS, BOX_ENV_BINDINGS);
    box_transparent_bind_layout(mesh_entries + BOX_SCENE_BINDING, BOX_SCENE_BINDING);
    WGPUBindGroupLayoutDescriptor mesh_desc = {
        .entryCount = BOX_MESH_BINDINGS,
        .entries = mesh_entries
    };
    r->env_layout = wgpuDeviceCreateBindGroupLayout(r->device, &mesh_desc);
}

/** Create the textures, views and bind group of one prefiltered environment.
 *
 * @param r The renderer
 * @param e The environment entry
 */
static void box_env_slot_init(
    box_renderer_t *r,
    box_gpu_env_t *e)
{
    e->source = box_env_texture(r, BOX_ENV_SIZE, 1, BOX_ENV_FACES);
    e->specular = box_env_texture(r, BOX_ENV_SIZE, BOX_ENV_MIPS, BOX_ENV_FACES);
    e->irradiance = box_env_texture(r, BOX_ENV_IRRADIANCE_SIZE, 1, BOX_ENV_FACES);
    e->source_view = box_env_view(e->source,
        WGPUTextureViewDimension_Cube, 1, BOX_ENV_FACES);
    e->specular_view = box_env_view(e->specular,
        WGPUTextureViewDimension_Cube, BOX_ENV_MIPS, BOX_ENV_FACES);
    e->irradiance_view = box_env_view(e->irradiance,
        WGPUTextureViewDimension_Cube, 1, BOX_ENV_FACES);

    WGPUBindGroupEntry filter_entries[3] = {
        { .binding = 0, .buffer = r->env_uniform, .size = sizeof(box_env_uniform_t) },
        { .binding = 1, .textureView = e->source_view },
        { .binding = 2, .sampler = r->env_sampler }
    };
    WGPUBindGroupDescriptor filter_desc = {
        .layout = r->env_filter_layout,
        .entryCount = 3,
        .entries = filter_entries
    };
    e->filter_group = wgpuDeviceCreateBindGroup(r->device, &filter_desc);
}

/** Release the textures, views and bind group of one prefiltered environment.
 *
 * @param e The environment entry
 */
static void box_env_slot_fini(
    box_gpu_env_t *e)
{
    if (e->filter_group) wgpuBindGroupRelease(e->filter_group);
    if (e->source_view) wgpuTextureViewRelease(e->source_view);
    if (e->specular_view) wgpuTextureViewRelease(e->specular_view);
    if (e->irradiance_view) wgpuTextureViewRelease(e->irradiance_view);
    if (e->source) wgpuTextureRelease(e->source);
    if (e->specular) wgpuTextureRelease(e->specular);
    if (e->irradiance) wgpuTextureRelease(e->irradiance);
    memset(e, 0, sizeof(box_gpu_env_t));
}

/** Create the BRDF table, the sampler and the uniform buffer of the prefilters.
 *
 * @param r The renderer
 */
static void box_env_resources(
    box_renderer_t *r)
{
    r->env_brdf = box_env_texture(r, BOX_ENV_BRDF_SIZE, 1, 1);
    r->env_brdf_view = box_env_view(r->env_brdf, WGPUTextureViewDimension_2D, 1, 1);

    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMaxClamp = (float)BOX_ENV_MIPS,
        .maxAnisotropy = 1
    };
    r->env_sampler = wgpuDeviceCreateSampler(r->device, &sampler_desc);

    WGPUBufferDescriptor buffer_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)BOX_ENV_SLOTS * BOX_ENV_PASSES * BOX_ENV_STRIDE
    };
    r->env_uniform = wgpuDeviceCreateBuffer(r->device, &buffer_desc);
}

/** Create the uniform bind group the gradient and BRDF passes read.
 *
 * @param r The renderer
 */
static void box_env_groups(
    box_renderer_t *r)
{
    WGPUBindGroupEntry uniform = {
        .binding = 0,
        .buffer = r->env_uniform,
        .size = sizeof(box_env_uniform_t)
    };
    WGPUBindGroupDescriptor pass_desc = {
        .layout = r->env_pass_layout,
        .entryCount = 1,
        .entries = &uniform
    };
    r->env_pass_group = wgpuDeviceCreateBindGroup(r->device, &pass_desc);
}

void box_environment_group(
    box_renderer_t *r)
{
    box_gpu_env_t *e = &r->envs[r->v->env_slot];
    WGPUBindGroupEntry mesh_entries[BOX_MESH_BINDINGS] = {
        { .binding = 0, .textureView = e->irradiance_view },
        { .binding = 1, .textureView = e->specular_view },
        { .binding = 2, .textureView = r->env_brdf_view },
        { .binding = 3, .sampler = r->env_sampler }
    };
    box_texture_bind_group(r, mesh_entries + BOX_ENV_BINDINGS, BOX_ENV_BINDINGS);
    box_transparent_bind_group(r, mesh_entries + BOX_SCENE_BINDING, BOX_SCENE_BINDING);
    WGPUBindGroupDescriptor mesh_desc = {
        .layout = r->env_layout,
        .entryCount = BOX_MESH_BINDINGS,
        .entries = mesh_entries
    };
    if (r->v->env_group) {
        wgpuBindGroupRelease(r->v->env_group);
    }

    r->v->env_group = wgpuDeviceCreateBindGroup(r->device, &mesh_desc);
    r->v->env_dirty = false;
}

void box_environment_rebuild(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < r->views.count; i ++) {
        ((box_gpu_view_t*)box_vec_at(&r->views, i))->env_dirty = true;
    }
}

/** Record a full screen pass that fills one environment target.
 *
 * @param encoder The command encoder
 * @param pipeline The pipeline of the pass
 * @param group The bind group of the pass
 * @param target The render target view
 * @param index Index of the pass in the uniform buffer
 */
static void box_env_pass(
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group,
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
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Integrate the split sum BRDF into its lookup table, once per device.
 *
 * @param r The renderer
 */
static void box_env_integrate_brdf(
    box_renderer_t *r)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    box_env_pass(encoder, r->env_brdf_pipeline, r->env_pass_group, r->env_brdf_view, 0);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
}

/** Write the uniforms of every prefilter pass: the radiance, its face and roughness.
 *
 * @param r The renderer
 * @param slot The environment entry the passes fill
 * @param view The view the environment comes from
 */
static void box_env_uniforms(
    box_renderer_t *r,
    uint32_t slot,
    const box_view_t *view)
{
    box_env_uniform_t u = {{0}};
    int32_t channel;
    for (channel = 0; channel < 3; channel ++) {
        u.sky[channel] = view->env_sky[channel] * view->env_intensity;
        u.ground[channel] = view->env_ground[channel] * view->env_intensity;
    }

    uint32_t base = slot * BOX_ENV_PASSES;
    uint32_t index;
    for (index = 0; index < BOX_ENV_PASSES; index ++) {
        uint32_t stage = index / BOX_ENV_FACES;
        u.params[0] = (float)(index % BOX_ENV_FACES);
        u.params[1] = 0;
        if (stage >= 1 && stage <= BOX_ENV_MIPS) {
            u.params[1] = (float)(stage - 1) / (float)(BOX_ENV_MIPS - 1);
        }

        wgpuQueueWriteBuffer(r->gpu_queue, r->env_uniform,
            (uint64_t)(base + index) * BOX_ENV_STRIDE, &u, sizeof(box_env_uniform_t));
    }
}

/** Record the passes that fill one cubemap from a pipeline.
 *
 * @param encoder The command encoder
 * @param texture The cubemap to fill
 * @param pipeline The pipeline of the passes
 * @param group The bind group of the passes
 * @param mip Mip level to fill
 * @param first Index of the first pass in the uniform buffer
 */
static void box_env_cube(
    WGPUCommandEncoder encoder,
    WGPUTexture texture,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group,
    uint32_t mip,
    uint32_t first)
{
    uint32_t face;
    for (face = 0; face < BOX_ENV_FACES; face ++) {
        WGPUTextureView target = box_env_face(texture, face, mip);
        box_env_pass(encoder, pipeline, group, target, first + face);
        wgpuTextureViewRelease(target);
    }
}

void box_environment_init(
    box_renderer_t *r)
{
    box_env_layouts(r);
    box_env_resources(r);
    box_env_groups(r);

    WGPUShaderModule source = box_shader_module(r, BOX_SHADER_ENV);
    WGPUShaderModule filter = box_shader_module(r, BOX_SHADER_ENV_FILTER);
    r->env_gradient_pipeline =
        box_env_pipeline(r, source, "fs_gradient", r->env_pass_layout);
    r->env_brdf_pipeline = box_env_pipeline(r, source, "fs_brdf", r->env_pass_layout);
    r->env_specular_pipeline =
        box_env_pipeline(r, filter, "fs_specular", r->env_filter_layout);
    r->env_irradiance_pipeline =
        box_env_pipeline(r, filter, "fs_irradiance", r->env_filter_layout);
    wgpuShaderModuleRelease(source);
    wgpuShaderModuleRelease(filter);

    box_env_integrate_brdf(r);
}

/** Find the entry that holds a generation, or the one to prefilter it into.
 *
 * The entry a view bound most recently is kept, so the views of a frame keep
 * their environment and the entry no view has used for the longest is reused.
 *
 * @param r The renderer
 * @param generation The environment generation of the view
 * @param found Set to true when the entry already holds the generation
 * @return The entry index
 */
static uint32_t box_env_slot(
    box_renderer_t *r,
    uint32_t generation,
    bool *found)
{
    uint32_t result = 0;
    uint32_t oldest = 0xFFFFFFFF;
    uint32_t i;
    *found = false;
    for (i = 0; i < BOX_ENV_SLOTS && !*found; i ++) {
        if (r->envs[i].source && r->envs[i].generation == generation) {
            result = i;
            *found = true;
        } else if (r->envs[i].used < oldest) {
            oldest = r->envs[i].used;
            result = i;
        }
    }

    return result;
}

void box_environment_update(
    box_renderer_t *r,
    const box_view_t *view,
    WGPUCommandEncoder encoder)
{
    bool found = false;
    uint32_t slot = box_env_slot(r, view->env_generation, &found);
    box_gpu_env_t *e = &r->envs[slot];
    if (!found) {
        if (!e->source) {
            box_env_slot_init(r, e);
        }

        e->generation = view->env_generation;
        box_env_uniforms(r, slot, view);
        uint32_t base = slot * BOX_ENV_PASSES;
        box_env_cube(encoder, e->source, r->env_gradient_pipeline, r->env_pass_group, 0, base);

        uint32_t mip;
        for (mip = 0; mip < BOX_ENV_MIPS; mip ++) {
            box_env_cube(encoder, e->specular, r->env_specular_pipeline,
                e->filter_group, mip, base + (mip + 1) * BOX_ENV_FACES);
        }

        box_env_cube(encoder, e->irradiance, r->env_irradiance_pipeline,
            e->filter_group, 0, base + (BOX_ENV_MIPS + 1) * BOX_ENV_FACES);
    }

    e->used = ++ r->env_clock;
    if (r->v->env_slot != slot || r->v->env_dirty || !r->v->env_group) {
        r->v->env_slot = slot;
        box_environment_group(r);
    }
}

void box_environment_fini(
    box_renderer_t *r)
{
    uint32_t slot;
    for (slot = 0; slot < BOX_ENV_SLOTS; slot ++) {
        box_env_slot_fini(&r->envs[slot]);
    }

    if (r->env_pass_group) wgpuBindGroupRelease(r->env_pass_group);
    if (r->env_layout) wgpuBindGroupLayoutRelease(r->env_layout);
    if (r->env_pass_layout) wgpuBindGroupLayoutRelease(r->env_pass_layout);
    if (r->env_filter_layout) wgpuBindGroupLayoutRelease(r->env_filter_layout);
    if (r->env_gradient_pipeline) wgpuRenderPipelineRelease(r->env_gradient_pipeline);
    if (r->env_specular_pipeline) wgpuRenderPipelineRelease(r->env_specular_pipeline);
    if (r->env_irradiance_pipeline) wgpuRenderPipelineRelease(r->env_irradiance_pipeline);
    if (r->env_brdf_pipeline) wgpuRenderPipelineRelease(r->env_brdf_pipeline);
    if (r->env_sampler) wgpuSamplerRelease(r->env_sampler);
    if (r->env_uniform) wgpuBufferRelease(r->env_uniform);
    if (r->env_brdf_view) wgpuTextureViewRelease(r->env_brdf_view);
    if (r->env_brdf) wgpuTextureRelease(r->env_brdf);
    r->env_pass_group = NULL;
    r->env_layout = NULL;
    r->env_pass_layout = NULL;
    r->env_filter_layout = NULL;
    r->env_gradient_pipeline = NULL;
    r->env_specular_pipeline = NULL;
    r->env_irradiance_pipeline = NULL;
    r->env_brdf_pipeline = NULL;
    r->env_sampler = NULL;
    r->env_uniform = NULL;
    r->env_brdf_view = NULL;
    r->env_brdf = NULL;
}
