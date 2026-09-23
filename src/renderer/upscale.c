/**
 * @file renderer/upscale.c
 * @brief Renders the surface views into a smaller image and blits it to the window.
 *
 * When the scaled size differs from the target size the frame is drawn into a
 * scaled texture, then one full screen pass magnifies it onto the swap chain with
 * a linear sampler. Captures read the window sized image.
 */

#include "renderer_internal.h"

void box_upscale_init(
    box_renderer_t *r)
{
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 1.0f,
        .maxAnisotropy = 1
    };
    r->upscale_sampler = wgpuDeviceCreateSampler(r->device, &sampler_desc);

    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 2, .entries = entries };
    r->upscale_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->upscale_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_BLUR);
    if (module) {
        WGPUColorTargetState target = {
            .format = r->format, .writeMask = WGPUColorWriteMask_All };
        WGPUFragmentState fragment = {
            .module = module,
            .entryPoint = { .data = "fs_blit", .length = WGPU_STRLEN },
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
        r->upscale_pipeline = wgpuDeviceCreateRenderPipeline(r->device, &desc);
        wgpuShaderModuleRelease(module);
    }

    wgpuPipelineLayoutRelease(pipeline_layout);
}

/** Give the renderer a scaled image of a size, keeping the one it has.
 *
 * @param r The renderer
 * @param width Width of the image in pixels
 * @param height Height of the image in pixels
 */
static void box_upscale_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height)
{
    if (r->upscale && r->upscale_width == width && r->upscale_height == height) {
        goto done;
    }

    if (r->upscale_group) wgpuBindGroupRelease(r->upscale_group);
    if (r->upscale_view) wgpuTextureViewRelease(r->upscale_view);
    if (r->upscale) wgpuTextureRelease(r->upscale);
    r->upscale_group = NULL;
    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = r->format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->upscale = wgpuDeviceCreateTexture(r->device, &color_desc);
    r->upscale_view = wgpuTextureCreateView(r->upscale, NULL);
    r->upscale_width = width;
    r->upscale_height = height;

    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .textureView = r->upscale_view },
        { .binding = 1, .sampler = r->upscale_sampler }
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->upscale_layout, .entryCount = 2, .entries = entries };
    r->upscale_group = wgpuDeviceCreateBindGroup(r->device, &group_desc);
done:
    return;
}

WGPUTexture box_upscale_begin(
    box_renderer_t *r,
    const box_packet_t *p)
{
    WGPUTexture result = NULL;
    if (!r->upscale_pipeline || !p->scaled_width || !p->scaled_height) {
        goto done;
    }

    if (p->scaled_width == p->width && p->scaled_height == p->height) {
        goto done;
    }

    box_upscale_resize(r, p->scaled_width, p->scaled_height);
    result = r->upscale;
done:
    return result;
}

void box_upscale_blit(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTexture dst)
{
    if (!r->upscale_group || !dst) {
        goto done;
    }

    WGPUTextureView attachment = wgpuTextureCreateView(dst, NULL);
    WGPURenderPassColorAttachment color = {
        .view = attachment,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0.0, 0.0, 0.0, 1.0 }
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "upscale");
    wgpuRenderPassEncoderSetPipeline(pass, r->upscale_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->upscale_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuTextureViewRelease(attachment);
done:
    return;
}

void box_upscale_fini(
    box_renderer_t *r)
{
    if (r->upscale_group) wgpuBindGroupRelease(r->upscale_group);
    if (r->upscale_view) wgpuTextureViewRelease(r->upscale_view);
    if (r->upscale) wgpuTextureRelease(r->upscale);
    if (r->upscale_pipeline) wgpuRenderPipelineRelease(r->upscale_pipeline);
    if (r->upscale_layout) wgpuBindGroupLayoutRelease(r->upscale_layout);
    if (r->upscale_sampler) wgpuSamplerRelease(r->upscale_sampler);
    r->upscale_group = NULL;
    r->upscale_view = NULL;
    r->upscale = NULL;
    r->upscale_pipeline = NULL;
    r->upscale_layout = NULL;
    r->upscale_sampler = NULL;
    r->upscale_width = 0;
    r->upscale_height = 0;
}
