/**
 * @file renderer/frame.c
 * @brief Builds the mesh pipeline and renders one packet into the current target.
 */

#include "renderer_internal.h"
#include <stdio.h>

/** Create the view uniform buffer, its bind group layout and bind group.
 *
 * @param r The renderer
 */
static void box_frame_init_view(
    box_renderer_t *r)
{
    WGPUBufferDescriptor buffer_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(box_view_t)
    };
    r->view_buffer = wgpuDeviceCreateBuffer(r->device, &buffer_desc);

    WGPUBindGroupLayoutEntry layout_entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
        .buffer = { .type = WGPUBufferBindingType_Uniform, .minBindingSize = sizeof(box_view_t) }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 1, .entries = &layout_entry };
    r->view_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUBindGroupEntry group_entry = {
        .binding = 0,
        .buffer = r->view_buffer,
        .size = sizeof(box_view_t)
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->view_layout,
        .entryCount = 1,
        .entries = &group_entry
    };
    r->view_group = wgpuDeviceCreateBindGroup(r->device, &group_desc);
}

void box_frame_init(
    box_renderer_t *r)
{
    box_vec_init(&r->meshes, sizeof(box_gpu_mesh_t));
    box_frame_init_view(r);
    box_lights_init(r);
    box_texture_init(r);
    box_environment_init(r);
    box_bake_init(r);

    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = box_mesh_shader_wgsl, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor shader_desc = { .nextInChain = (WGPUChainedStruct*)&wgsl };
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(r->device, &shader_desc);

    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute instance_attrs[9] = {
        { .format = WGPUVertexFormat_Float32x4, .offset = 0, .shaderLocation = 3 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 16, .shaderLocation = 4 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 32, .shaderLocation = 5 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 48, .shaderLocation = 6 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 64, .shaderLocation = 7 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 80, .shaderLocation = 8 },
        { .format = WGPUVertexFormat_Uint32x2, .offset = 96, .shaderLocation = 9 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 104, .shaderLocation = 10 },
        { .format = WGPUVertexFormat_Uint32, .offset = 120, .shaderLocation = 11 }
    };
    WGPUVertexBufferLayout layouts[2] = {
        {
            .arrayStride = BOX_VERTEX_FLOATS * sizeof(float),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 3,
            .attributes = vertex_attrs
        }, {
            .arrayStride = sizeof(box_instance_t),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = 9,
            .attributes = instance_attrs
        }
    };

    WGPUBindGroupLayout group_layouts[4] = {
        r->view_layout, r->light_layout, r->env_layout, r->probe_layout };
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 4,
        .bindGroupLayouts = group_layouts
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

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
        .layout = pipeline_layout,
        .vertex = {
            .module = shader,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN },
            .bufferCount = 2,
            .buffers = layouts
        },
        .fragment = &fragment,
        .depthStencil = &depth,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_Back,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    r->pipeline = wgpuDeviceCreateRenderPipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
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
    WGPUTexture result = r->offscreen;
    if (r->surface) {
        WGPUSurfaceTexture st = {0};
        wgpuSurfaceGetCurrentTexture(r->surface, &st);
        bool ok = st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
            st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
        result = st.texture;
        if (!ok && st.texture) {
            wgpuTextureRelease(st.texture);
            result = NULL;
        }
    }

    return result;
}

/** Upload the packet's view uniforms.
 *
 * @param r The renderer
 * @param p The packet
 */
static void box_frame_write_view(
    box_renderer_t *r,
    const box_packet_t *p)
{
    box_view_t view = p->view;
    bool srgb = r->format == WGPUTextureFormat_BGRA8UnormSrgb ||
        r->format == WGPUTextureFormat_RGBA8UnormSrgb;
    view.gamma_encode = srgb ? 0.0f : 1.0f;
    wgpuQueueWriteBuffer(r->gpu_queue, r->view_buffer, 0, &view, sizeof(box_view_t));
}

void box_frame_render(
    box_renderer_t *r,
    const box_packet_t *p)
{
    box_device_resize(r, p->width, p->height);
    box_texture_apply(r, p);
    box_scene_apply(r, p);
    if (p->has_view) {
        box_lights_update(r, p);
    }

    WGPUTexture target = box_frame_target(r);
    if (!target) {
        goto done;
    }

    if (p->has_view) {
        box_frame_write_view(r, p);
    }

    WGPUTextureView view = wgpuTextureCreateView(target, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    if (p->has_view) {
        box_environment_update(r, p, encoder);
        box_lights_dispatch(r, p, encoder);
        box_bake_update(r, p, encoder);
    }

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
    if (p->has_view) {
        wgpuRenderPassEncoderSetPipeline(pass, r->pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, r->view_group, 0, NULL);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, r->light_group, 0, NULL);
        wgpuRenderPassEncoderSetBindGroup(pass, 2, r->env_group, 0, NULL);
        wgpuRenderPassEncoderSetBindGroup(pass, 3, r->probe_dummy_group, 0, NULL);
        box_scene_draw(r, p, pass);
    }

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
#ifndef BOX_PLATFORM_WEB
        wgpuSurfacePresent(r->surface);
#endif
        wgpuTextureRelease(target);
    }

    wgpuTextureViewRelease(view);
done:
    return;
}
