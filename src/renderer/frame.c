/**
 * @file renderer/frame.c
 * @brief Builds the mesh pipeline and renders one packet into the current target.
 */

#include "renderer_internal.h"
#include <stdio.h>

/** Create the bind group layout the view and layer uniforms are bound through.
 *
 * @param r The renderer
 */
static void box_frame_init_view(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry layout_entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_VIEW_UNIFORM_SIZE
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
    box_vec_init(&r->views, sizeof(box_gpu_view_t));
    box_vec_init(&r->targets, sizeof(box_gpu_target_t));
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

/** Acquire the texture the surface views render into.
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

/** Upload the uniforms of one view.
 *
 * @param r The renderer
 * @param view The view
 */
static void box_frame_write_view(
    box_renderer_t *r,
    const box_view_t *view)
{
    box_view_t uniform = *view;
    bool srgb = r->format == WGPUTextureFormat_BGRA8UnormSrgb ||
        r->format == WGPUTextureFormat_RGBA8UnormSrgb;
    uniform.gamma_encode = srgb ? 0.0f : 1.0f;
    wgpuQueueWriteBuffer(r->gpu_queue, r->v->view_buffer, 0, &uniform, BOX_VIEW_UNIFORM_SIZE);
}

/** Record the pass that draws the sorted instances of a view over its image.
 *
 * The opaque target is copied and its mip chain filled first, so the blended
 * draws can read what is behind them at the blur their roughness asks for.
 *
 * @param r The renderer
 * @param view The view
 * @param p The packet
 * @param encoder The command encoder
 * @param target The texture the opaque pass rendered into
 * @param attachment The render target view of that texture
 */
static void box_frame_sorted(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPUCommandEncoder encoder,
    WGPUTexture target,
    WGPUTextureView attachment)
{
    if (r->v->scene_needed) {
        box_transparent_prepare(r, encoder, target);
    }

    WGPURenderPassColorAttachment color = {
        .view = attachment,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = r->v->depth_view,
        .depthLoadOp = WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color,
        .depthStencilAttachment = &depth
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->view_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, r->v->light_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 2, r->v->env_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, r->probe_dummy_group, 0, NULL);
    box_transparent_draw(r, view, p, pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Check whether an earlier view of the frame already drew into a target.
 *
 * @param p The packet
 * @param index Index of the view
 * @return true when the target still holds what the previous frame left
 */
static bool box_frame_first_use(
    const box_packet_t *p,
    uint32_t index)
{
    bool result = true;
    uint32_t target = ((box_view_t*)box_vec_at(&p->views, index))->target;
    uint32_t i;
    for (i = 0; i < index && result; i ++) {
        result = ((box_view_t*)box_vec_at(&p->views, i))->target != target;
    }

    return result;
}

/** Record every pass of one view into its target.
 *
 * @param r The renderer
 * @param p The packet
 * @param index Index of the view in the packet
 * @param encoder The command encoder
 * @param surface The texture the views of the surface draw into
 */
static void box_frame_view(
    box_renderer_t *r,
    const box_packet_t *p,
    uint32_t index,
    WGPUCommandEncoder encoder,
    WGPUTexture surface)
{
    const box_view_t *view = box_vec_at(&p->views, index);
    WGPUTexture target = surface;
    if (view->target) {
        box_gpu_target_t *t = box_target_get(r, view->target);
        if (!t) {
            goto done;
        }

        target = t->color;
    }

    box_view_select(r, index);
    box_view_prepare(r, view);
    box_transparent_needed(r, view, p);
    box_lights_update(r, view, p);
    box_frame_write_view(r, view);
    box_environment_update(r, view, encoder);
    box_lights_dispatch(r, view, encoder);
    if (!index) {
        box_bake_update(r, p, encoder);
    }

    WGPUTextureView attachment = wgpuTextureCreateView(target, NULL);
    bool clear = box_frame_first_use(p, index);
    WGPURenderPassColorAttachment color = {
        .view = attachment,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { view->clear[0], view->clear[1], view->clear[2], view->clear[3] }
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = r->v->depth_view,
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
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->view_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, r->v->light_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 2, r->v->env_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, r->probe_dummy_group, 0, NULL);
    box_scene_draw(r, p, pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    if (view->transparent_count) {
        box_frame_sorted(r, view, p, encoder, target, attachment);
    }

    wgpuTextureViewRelease(attachment);
done:
    return;
}

/** Texture the capture of the frame reads.
 *
 * @param r The renderer
 * @param p The packet
 * @param surface The texture the views of the surface drew into
 * @return The texture to read back
 */
static WGPUTexture box_frame_capture_target(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUTexture surface)
{
    WGPUTexture result = surface;
    if (p->capture_view < p->views.count) {
        uint32_t handle = ((box_view_t*)box_vec_at(&p->views, p->capture_view))->target;
        box_gpu_target_t *t = box_target_get(r, handle);
        if (t) {
            result = t->color;
        }
    }

    return result;
}

void box_frame_render(
    box_renderer_t *r,
    const box_packet_t *p)
{
    box_device_resize(r, p->width, p->height);
    box_layers_apply(r, p);
    box_texture_apply(r, p);
    box_target_apply(r, p);
    box_scene_apply(r, p);
    box_transparent_upload(r, p);

    WGPUTexture surface = box_frame_target(r);
    if (!surface) {
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    uint32_t i;
    for (i = 0; i < p->views.count; i ++) {
        box_frame_view(r, p, i, encoder, surface);
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    if (p->capture_path[0]) {
        box_capture_texture(r, box_frame_capture_target(r, p, surface), p->capture_path);
    }

    if (r->surface) {
#ifndef BOX_PLATFORM_WEB
        wgpuSurfacePresent(r->surface);
#endif
        wgpuTextureRelease(surface);
    }

done:
    return;
}
