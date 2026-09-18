/**
 * @file renderer/frame.c
 * @brief Renders one packet: clears the target and draws the placeholder triangle.
 */

#include "renderer_internal.h"
#include <stdio.h>

/** WGSL source of the placeholder triangle */
static const char *box_triangle_wgsl =
    "struct Out { @builtin(position) pos: vec4<f32>, @location(0) color: vec3<f32> };\n"
    "@vertex fn vs_main(@builtin(vertex_index) i: u32) -> Out {\n"
    "  var p = array<vec2<f32>, 3>(vec2(0.0, 0.6), vec2(-0.6, -0.5), vec2(0.6, -0.5));\n"
    "  var c = array<vec3<f32>, 3>(vec3(1.0, 0.3, 0.2), vec3(0.2, 1.0, 0.3), vec3(0.2, 0.4, 1.0));\n"
    "  var out: Out;\n"
    "  out.pos = vec4(p[i], 0.5, 1.0);\n"
    "  out.color = c[i];\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(in: Out) -> @location(0) vec4<f32> {\n"
    "  return vec4(in.color, 1.0);\n"
    "}\n";

void box_frame_init(
    box_renderer_t *r)
{
    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = box_triangle_wgsl, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor shader_desc = { .nextInChain = (WGPUChainedStruct*)&wgsl };
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(r->device, &shader_desc);

    WGPUColorTargetState target = { .format = r->format, .writeMask = WGPUColorWriteMask_All };
    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always },
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };
    WGPURenderPipelineDescriptor desc = {
        .vertex = { .module = shader, .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN } },
        .fragment = &fragment,
        .depthStencil = &depth,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    r->pipeline = wgpuDeviceCreateRenderPipeline(r->device, &desc);
    wgpuShaderModuleRelease(shader);
}

/** Acquire the texture to render into: the next surface texture or the offscreen one.
 *
 * @param r The renderer
 * @return The texture, or NULL when the surface is not ready
 */
static WGPUTexture box_frame_target(
    box_renderer_t *r)
{
    if (!r->surface) {
        return r->offscreen;
    }
    WGPUSurfaceTexture st = {0};
    wgpuSurfaceGetCurrentTexture(r->surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        if (st.texture) wgpuTextureRelease(st.texture);
        return NULL;
    }
    return st.texture;
}

void box_frame_render(
    box_renderer_t *r,
    const box_packet_t *p)
{
    box_device_resize(r, p->width, p->height);

    WGPUTexture target = box_frame_target(r);
    if (!target) {
        return;
    }

    WGPUTextureView view = wgpuTextureCreateView(target, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);

    WGPURenderPassColorAttachment color = {
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { p->clear[0], p->clear[1], p->clear[2], p->clear[3] }
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = r->depth_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color,
        .depthStencilAttachment = &depth
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, r->pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    if (p->capture_path[0]) {
        box_capture_texture(r, target, p->capture_path);
    }

    if (r->surface) {
        wgpuSurfacePresent(r->surface);
        wgpuTextureRelease(target);
    }
    wgpuTextureViewRelease(view);
}
