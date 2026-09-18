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

    WGPUBindGroupLayoutEntry layout_entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_view_t)
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_LAYER_UNIFORM_SIZE
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 2, .entries = layout_entries };
    r->view_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUBindGroupEntry group_entries[2] = {
        { .binding = 0, .buffer = r->view_buffer, .size = sizeof(box_view_t) },
        { .binding = 1, .buffer = r->layer_buffer, .size = BOX_LAYER_UNIFORM_SIZE }
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->view_layout,
        .entryCount = 2,
        .entries = group_entries
    };
    r->view_group = wgpuDeviceCreateBindGroup(r->device, &group_desc);
}

WGPURenderPipeline box_frame_pipeline(
    box_renderer_t *r,
    uint64_t stride,
    uint64_t offset,
    WGPUCullMode cull,
    bool depth_write,
    const WGPUBlendState *blend)
{
    WGPUVertexAttribute vertex_attrs[4] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 },
        { .format = WGPUVertexFormat_Unorm8x4, .offset = BOX_VERTEX_LOCAL_OFFSET,
            .shaderLocation = 12 }
    };
    WGPUVertexAttribute instance_attrs[9] = {
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 0, .shaderLocation = 3 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 16, .shaderLocation = 4 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 32, .shaderLocation = 5 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 48, .shaderLocation = 6 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 64, .shaderLocation = 7 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 80, .shaderLocation = 8 },
        { .format = WGPUVertexFormat_Uint32x2, .offset = offset + 96, .shaderLocation = 9 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 104, .shaderLocation = 10 },
        { .format = WGPUVertexFormat_Uint32, .offset = offset + 120, .shaderLocation = 11 }
    };
    WGPUVertexBufferLayout layouts[2] = {
        {
            .arrayStride = BOX_VERTEX_FLOATS * sizeof(float),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 4,
            .attributes = vertex_attrs
        }, {
            .arrayStride = stride,
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = 9,
            .attributes = instance_attrs
        }
    };

    WGPUColorTargetState target = {
        .format = r->format,
        .blend = blend,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = r->mesh_shader,
        .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = depth_write ? WGPUOptionalBool_True : WGPUOptionalBool_False,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always },
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = r->mesh_layout,
        .vertex = {
            .module = r->mesh_shader,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN },
            .bufferCount = 2,
            .buffers = layouts
        },
        .fragment = &fragment,
        .depthStencil = &depth,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = cull,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };

    return wgpuDeviceCreateRenderPipeline(r->device, &desc);
}

void box_frame_init(
    box_renderer_t *r)
{
    box_vec_init(&r->meshes, sizeof(box_gpu_mesh_t));
    box_layers_init(r);
    box_frame_init_view(r);
    box_lights_init(r);
    box_texture_init(r);
    box_transparent_init(r);
    box_environment_init(r);
    box_bake_init(r);

    r->mesh_shader = box_layers_shader(r);

    WGPUBindGroupLayout group_layouts[4] = {
        r->view_layout, r->light_layout, r->env_layout, r->probe_layout };
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 4,
        .bindGroupLayouts = group_layouts
    };
    r->mesh_layout = wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    r->pipeline = box_frame_pipeline(r, sizeof(box_instance_t), 0, WGPUCullMode_Back,
        true, NULL);
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

/** Record the pass that draws the sorted instances over the opaque image.
 *
 * The opaque target is copied and its mip chain filled first, so the blended
 * draws can read what is behind them at the blur their roughness asks for.
 *
 * @param r The renderer
 * @param p The packet
 * @param encoder The command encoder
 * @param target The texture the opaque pass rendered into
 * @param view The render target view of that texture
 */
static void box_frame_sorted(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTexture target,
    WGPUTextureView view)
{
    if (r->scene_needed) {
        box_transparent_prepare(r, encoder, target);
    }

    WGPURenderPassColorAttachment color = {
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = r->depth_view,
        .depthLoadOp = WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color,
        .depthStencilAttachment = &depth
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->view_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, r->light_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 2, r->env_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, r->probe_dummy_group, 0, NULL);
    box_transparent_draw(r, p, pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void box_frame_render(
    box_renderer_t *r,
    const box_packet_t *p)
{
    box_device_resize(r, p->width, p->height);
    box_layers_apply(r, p);
    box_texture_apply(r, p);
    box_scene_apply(r, p);
    box_transparent_upload(r, p);
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
    if (p->has_view && p->transparent.count) {
        box_frame_sorted(r, p, encoder, target, view);
    }

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
