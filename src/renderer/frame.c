/**
 * @file renderer/frame.c
 * @brief Builds the mesh pipeline and renders one packet into the current target.
 */

#include "renderer_internal.h"
#include <math.h>
#include <stdio.h>

/** Create the bind group layout the view and layer uniforms are bound through.
 *
 * @param r The renderer
 */
static void box_frame_init_view(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry layout_entries[4] = {
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
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Comparison }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 4, .entries = layout_entries };
    r->view_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);
}

/** Blend the coverage of a sorted draw is folded into the normal attachment with.
 *
 * The color equation keeps what is left of the surface behind the fragment and
 * the alpha equation clears the channel, so the write mask of the target turns
 * it into `indirect *= 1 - coverage` for the diffuse fraction and `0` for the
 * specular one.
 */
static const WGPUBlendState box_frame_cover_blend = {
    .color = { WGPUBlendOperation_Add, WGPUBlendFactor_Zero,
        WGPUBlendFactor_OneMinusSrcAlpha },
    .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_Zero }
};

WGPURenderPipeline box_frame_pipeline(
    box_renderer_t *r,
    uint64_t stride,
    uint64_t offset,
    WGPUCullMode cull,
    bool depth_write,
    const WGPUBlendState *blend,
    bool hdr,
    uint32_t normals)
{
    WGPUVertexAttribute vertex_attrs[5] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 },
        { .format = WGPUVertexFormat_Unorm8x4, .offset = BOX_VERTEX_LOCAL_OFFSET,
            .shaderLocation = 12 },
        { .format = WGPUVertexFormat_Unorm8x4, .offset = BOX_VERTEX_COLOR_OFFSET,
            .shaderLocation = 13 }
    };
    WGPUVertexAttribute instance_attrs[10] = {
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 0, .shaderLocation = 3 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 16, .shaderLocation = 4 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 32, .shaderLocation = 5 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 48, .shaderLocation = 6 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 64, .shaderLocation = 7 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 80, .shaderLocation = 8 },
        { .format = WGPUVertexFormat_Uint32x2, .offset = offset + 96, .shaderLocation = 9 },
        { .format = WGPUVertexFormat_Float32x4, .offset = offset + 104, .shaderLocation = 10 },
        { .format = WGPUVertexFormat_Uint32, .offset = offset + 120, .shaderLocation = 11 },
        { .format = WGPUVertexFormat_Uint32x4,
            .offset = offset + BOX_INSTANCE_SHAPE * sizeof(uint32_t), .shaderLocation = 14 }
    };
    WGPUVertexBufferLayout layouts[2] = {
        {
            .arrayStride = BOX_VERTEX_FLOATS * sizeof(float),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 5,
            .attributes = vertex_attrs
        }, {
            .arrayStride = stride,
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = 10,
            .attributes = instance_attrs
        }
    };

    bool cover = normals == BOX_NORMALS_COVER || normals == BOX_NORMALS_GROUP_COVER;
    bool lone = normals == BOX_NORMALS_NONE || normals == BOX_NORMALS_GROUP;
    WGPUColorTargetState targets[2] = {
        {
            .format = hdr ? BOX_EFFECT_HDR_FORMAT : r->format,
            .blend = blend,
            .writeMask = WGPUColorWriteMask_All
        }, {
            .format = BOX_EFFECT_NORMAL_FORMAT,
            .blend = cover ? &box_frame_cover_blend : NULL,
            .writeMask = cover ?
                WGPUColorWriteMask_Blue | WGPUColorWriteMask_Alpha : WGPUColorWriteMask_All
        }
    };
    const char *entries[] = { "fs_main", "fs_normal", "fs_cover", "fs_group",
        "fs_group_cover" };
    WGPUFragmentState fragment = {
        .module = r->mesh_shader,
        .entryPoint = { .data = entries[normals], .length = WGPU_STRLEN },
        .targetCount = lone ? 1u : 2u,
        .targets = targets
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

void box_frame_resources(
    box_renderer_t *r)
{
    box_vec_init(&r->meshes, sizeof(box_gpu_mesh_t));
    box_vec_init(&r->static_staging, sizeof(box_instance_t));
    box_vec_init(&r->transitions, sizeof(box_transition_entry_t));
    box_vec_init(&r->views, sizeof(box_gpu_view_t));
    box_vec_init(&r->targets, sizeof(box_gpu_target_t));
}

void box_frame_init(
    box_renderer_t *r)
{
    box_layers_init(r);
    box_shadow_init(r);
    box_frame_init_view(r);
    box_lights_init(r);
    box_texture_init(r);
    box_transparent_init(r);
    box_environment_init(r);
    box_sky_init(r);
    box_bake_init(r);
    box_cull_init(r);
    box_hiz_init(r);

    r->mesh_shader = box_shader_module(r, BOX_SHADER_MESH);

    WGPUBindGroupLayout group_layouts[4] = {
        r->view_layout, r->light_layout, r->env_layout, r->probe_layout };
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 4,
        .bindGroupLayouts = group_layouts
    };
    r->mesh_layout = wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    r->pipeline[0] = box_frame_pipeline(r, sizeof(box_instance_t), 0, WGPUCullMode_Back,
        true, NULL, false, BOX_NORMALS_NONE);
    box_effect_init(r);
    box_overlay_init(r);
}

/** Mesh pipeline that writes the world normals into a second color attachment.
 *
 * The variant is built the first time a chain asks for normals and kept until the
 * mesh shader changes, because a scene without such an effect never needs it.
 *
 * @param r The renderer
 * @param hdr The pass writes the float scene target of a view with a chain
 * @return The pipeline, NULL when the mesh shader has no normal stage
 */
static WGPURenderPipeline box_frame_normal_pipeline(
    box_renderer_t *r,
    bool hdr)
{
    uint32_t slot = hdr ? 1 : 0;
    if (r->normal_pipeline[slot]) {
        goto done;
    }

    WGPUVertexAttribute vertex_attrs[5] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 },
        { .format = WGPUVertexFormat_Unorm8x4, .offset = BOX_VERTEX_LOCAL_OFFSET,
            .shaderLocation = 12 },
        { .format = WGPUVertexFormat_Unorm8x4, .offset = BOX_VERTEX_COLOR_OFFSET,
            .shaderLocation = 13 }
    };
    WGPUVertexAttribute instance_attrs[10] = {
        { .format = WGPUVertexFormat_Float32x4, .offset = 0, .shaderLocation = 3 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 16, .shaderLocation = 4 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 32, .shaderLocation = 5 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 48, .shaderLocation = 6 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 64, .shaderLocation = 7 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 80, .shaderLocation = 8 },
        { .format = WGPUVertexFormat_Uint32x2, .offset = 96, .shaderLocation = 9 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 104, .shaderLocation = 10 },
        { .format = WGPUVertexFormat_Uint32, .offset = 120, .shaderLocation = 11 },
        { .format = WGPUVertexFormat_Uint32x4,
            .offset = BOX_INSTANCE_SHAPE * sizeof(uint32_t), .shaderLocation = 14 }
    };
    WGPUVertexBufferLayout layouts[2] = {
        {
            .arrayStride = BOX_VERTEX_FLOATS * sizeof(float),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 5,
            .attributes = vertex_attrs
        }, {
            .arrayStride = sizeof(box_instance_t),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = 10,
            .attributes = instance_attrs
        }
    };
    WGPUColorTargetState targets[2] = {
        { .format = hdr ? BOX_EFFECT_HDR_FORMAT : r->format,
            .writeMask = WGPUColorWriteMask_All },
        { .format = BOX_EFFECT_NORMAL_FORMAT, .writeMask = WGPUColorWriteMask_All }
    };
    WGPUFragmentState fragment = {
        .module = r->mesh_shader,
        .entryPoint = { .data = "fs_normal", .length = WGPU_STRLEN },
        .targetCount = 2,
        .targets = targets
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
            .cullMode = WGPUCullMode_Back,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    r->normal_pipeline[slot] = wgpuDeviceCreateRenderPipeline(r->device, &desc);
done:
    return r->normal_pipeline[slot];
}

/** Mesh pipeline of the pass that draws the opaque instances of a view.
 *
 * @param r The renderer
 * @param hdr The pass writes the float scene target of a view with a chain
 * @return The pipeline, NULL when the float variant did not compile
 */
static WGPURenderPipeline box_frame_mesh_pipeline(
    box_renderer_t *r,
    bool hdr)
{
    uint32_t slot = hdr ? 1 : 0;
    if (!r->pipeline[slot]) {
        r->pipeline[slot] = box_frame_pipeline(r, sizeof(box_instance_t), 0,
            WGPUCullMode_Back, true, NULL, hdr, BOX_NORMALS_NONE);
    }

    return r->pipeline[slot];
}

bool box_frame_rebuild(
    box_renderer_t *r)
{
    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_MESH);
    bool result = module != NULL;
    if (!result) {
        goto done;
    }

    WGPUShaderModule previous = r->mesh_shader;
    r->mesh_shader = module;
    r->device_error = false;
    WGPURenderPipeline pipeline = box_frame_pipeline(r, sizeof(box_instance_t), 0,
        WGPUCullMode_Back, true, NULL, false, BOX_NORMALS_NONE);
    result = pipeline != NULL && !r->device_error;
    if (result) {
        box_transparent_reset(r);
        wgpuShaderModuleRelease(previous);
        uint32_t slot;
        for (slot = 0; slot < BOX_EFFECT_FORMATS; slot ++) {
            if (r->pipeline[slot]) {
                wgpuRenderPipelineRelease(r->pipeline[slot]);
            }

            if (r->normal_pipeline[slot]) {
                wgpuRenderPipelineRelease(r->normal_pipeline[slot]);
            }

            r->pipeline[slot] = NULL;
            r->normal_pipeline[slot] = NULL;
        }

        r->pipeline[0] = pipeline;
    } else {
        r->mesh_shader = previous;
        wgpuShaderModuleRelease(module);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

done:
    return result;
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
    uniform.gamma_encode = box_view_gamma(r->effect_scene != NULL, box_renderer_srgb(r));
    wgpuQueueWriteBuffer(r->gpu_queue, r->v->view_buffer, 0, &uniform, BOX_VIEW_UNIFORM_SIZE);
}

/** Record the pass that draws the sorted instances of a view over its image.
 *
 * The opaque target is copied and its blur chain filled first, so the blended
 * draws can read what is behind them at the blur their roughness asks for.
 *
 * The pass keeps the normal attachment of the opaque pass when the chain asked
 * for one, so a draw folds its coverage into the indirect fractions of the
 * pixels it blends over and the occlusion and reflection passes stop treating
 * them as the surface behind.
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

    WGPURenderPassColorAttachment color[2] = {
        {
            .view = attachment,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = WGPULoadOp_Load,
            .storeOp = WGPUStoreOp_Store
        }
    };
    uint32_t attachments = 1;
    if (r->effect_normal) {
        color[1].view = r->effect_normal->view;
        color[1].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color[1].loadOp = WGPULoadOp_Load;
        color[1].storeOp = WGPUStoreOp_Store;
        attachments = 2;
    }

    WGPURenderPassDepthStencilAttachment depth = {
        .view = r->v->depth_view,
        .depthLoadOp = WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = attachments,
        .colorAttachments = color,
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

/** Record and submit the occlusion pass that keeps what the previous frame saw.
 *
 * The dispatch goes into a command buffer of its own because it samples the
 * depth pyramid that the passes of this frame render into later on, and a
 * texture read that a command buffer records before the render pass that fills
 * it reads nothing.
 *
 * @param r The renderer
 */
static void box_frame_first_pass(
    box_renderer_t *r)
{
    if (!r->v->hiz_active) {
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    box_hiz_dispatch(r, encoder, BOX_HIZ_PASS_VISIBLE);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
}

/** Build the depth pyramid of the current view and draw what it uncovers.
 *
 * The first pass of the view drew the instances the previous frame drew, so its
 * depth target is what the pyramid is built from; the occlusion pass then tests
 * the instances that pass left out and this pass draws the ones it accepted
 * over the same color and depth targets.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param attachment The render target view the view draws into
 */
static void box_frame_uncovered(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTextureView attachment)
{
    box_hiz_build(r, encoder);
    box_hiz_dispatch(r, encoder, BOX_HIZ_PASS_NEW);

    WGPURenderPassColorAttachment color[2] = {
        {
            .view = attachment,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = WGPULoadOp_Load,
            .storeOp = WGPUStoreOp_Store
        }
    };
    uint32_t attachments = 1;
    if (r->effect_normal) {
        color[1].view = r->effect_normal->view;
        color[1].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color[1].loadOp = WGPULoadOp_Load;
        color[1].storeOp = WGPUStoreOp_Store;
        attachments = 2;
    }

    WGPURenderPassDepthStencilAttachment depth = {
        .view = r->v->depth_view,
        .depthLoadOp = WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = attachments,
        .colorAttachments = color,
        .depthStencilAttachment = &depth
    };
    bool hdr = r->effect_scene != NULL;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetPipeline(pass, attachments == 2 ?
        box_frame_normal_pipeline(r, hdr) : box_frame_mesh_pipeline(r, hdr));
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->view_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, r->v->light_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 2, r->v->env_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, r->probe_dummy_group, 0, NULL);
    box_cull_draw(r, pass, BOX_HIZ_PASS_NEW);
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

/** Clear color the scene passes of a view start from.
 *
 * The clear color of a view is given in the space its target is shown in, which
 * is what the 8 bit path writes it into. The float scene target holds linear
 * light and the chain encodes once at the end, so the clear is decoded here and
 * the background of a view with a chain stays what it was without one.
 *
 * @param r The renderer
 * @param view The view
 * @param hdr The scene passes write the float target
 * @param out Receives the clear color of the attachment
 */
static void box_frame_clear(
    box_renderer_t *r,
    const box_view_t *view,
    bool hdr,
    float *out)
{
    bool decode = hdr && !box_renderer_srgb(r);
    uint32_t i;
    for (i = 0; i < 3; i ++) {
        float c = view->clear[i] > 0.0f ? view->clear[i] : 0.0f;
        out[i] = decode ? powf(c, 2.2f) : c;
    }

    out[3] = view->clear[3];
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
    uint32_t passes = box_effect_prepare(r, view, p);
    box_view_prepare(r, view);
    WGPUTexture image = target;
    if (passes && r->effect_scene) {
        target = r->effect_scene->texture;
    }

    box_transparent_needed(r, view);
    box_lights_update(r, view, p);
    box_frame_write_view(r, view);
    box_cull_view(r, view, p);
    box_shadow_prepare(r, view, p);
    box_sky_update(r, view, p, encoder);
    box_environment_update(r, view, encoder);
    box_lights_dispatch(r, view, encoder);
    box_cull_dispatch(r, encoder);
    box_frame_first_pass(r);
    box_shadow_dispatch(r, encoder);
    box_shadow_record(r, view, encoder);
    if (!index) {
        box_bake_update(r, p, encoder);
    }

    WGPUTextureView attachment = wgpuTextureCreateView(target, NULL);
    bool hdr = r->effect_scene != NULL;
    bool clear = passes ? true : box_frame_first_use(p, index);
    float background[4];
    box_frame_clear(r, view, hdr, background);
    WGPURenderPassColorAttachment color[2] = {
        {
            .view = attachment,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = { background[0], background[1], background[2], background[3] }
        }
    };
    uint32_t attachments = 1;
    if (r->effect_normal) {
        color[1].view = r->effect_normal->view;
        color[1].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color[1].loadOp = WGPULoadOp_Clear;
        color[1].storeOp = WGPUStoreOp_Store;
        attachments = 2;
    }

    WGPURenderPassDepthStencilAttachment depth = {
        .view = r->v->depth_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = attachments,
        .colorAttachments = color,
        .depthStencilAttachment = &depth
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, attachments == 2 ?
        box_frame_normal_pipeline(r, hdr) : box_frame_mesh_pipeline(r, hdr));
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->view_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, r->v->light_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 2, r->v->env_group, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, r->probe_dummy_group, 0, NULL);
    if (r->v->culled) {
        box_cull_draw(r, pass, BOX_HIZ_PASS_VISIBLE);
    } else {
        box_scene_draw(r, view, p, pass);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    if (r->v->hiz_active) {
        box_frame_uncovered(r, encoder, attachment);
    }

    box_sky_record(r, view, p, encoder, attachment);
    if (view->transparent_count) {
        box_frame_sorted(r, view, p, encoder, target, attachment);
    }

    box_lines_record(r, encoder, attachment, hdr);
    if (passes) {
        box_effect_motion(r, view, index, encoder);
        WGPUTextureView final = wgpuTextureCreateView(surface, NULL);
        if (view->target) {
            wgpuTextureViewRelease(final);
            final = wgpuTextureCreateView(box_target_get(r, view->target)->color, NULL);
        }

        box_effect_run(r, view, p, encoder, final);
        wgpuTextureViewRelease(final);
    }

    box_overlay_record(r, view, index, encoder, image);
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
    box_packet_t *p)
{
    box_overlay_frame(r, p);
    box_device_resize(r, p->width, p->height);
    box_shader_apply(r, p);
    if (!r->initialized && box_shader_ready(r)) {
        box_frame_init(r);
        r->initialized = true;
    }

    if (!r->initialized) {
        box_target_apply(r, p);
        box_scene_apply(r, p);
        goto done;
    }

    box_layers_apply(r, p);
    box_texture_apply(r, p);
    box_target_apply(r, p);
    box_scene_apply(r, p);
    box_cull_frame(r, p);
    box_shadow_frame(r, p);
    box_transparent_upload(r, p);
    box_lines_upload(r, p);
    box_effect_frame(r, p);

    WGPUTexture surface = box_frame_target(r);
    if (!surface) {
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    uint32_t i;
    for (i = 0; i < p->views.count; i ++) {
        box_frame_view(r, p, i, encoder, surface);
    }

    box_cull_record(r, encoder);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    box_cull_publish(r);

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
