/**
 * @file renderer/lines.c
 * @brief Line list pass: one dynamic vertex buffer of colored segments per frame.
 *
 * One draw per view renders the world space segments of the packet, depth tested
 * but not depth written, using the view uniform of the mesh pass. Nothing is
 * allocated or recorded for a packet without lines.
 */

#include "renderer_internal.h"

/** Build the line list pipeline for one target format.
 *
 * @param r The renderer
 * @param hdr The passes of the view write the float scene target
 * @return The pipeline, NULL when the line shader has no module
 */
static WGPURenderPipeline box_lines_pipeline(
    box_renderer_t *r,
    bool hdr)
{
    uint32_t slot = hdr ? 1 : 0;
    if (r->lines_tried[slot]) {
        goto done;
    }

    r->lines_tried[slot] = true;
    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_LINES);
    if (!module) {
        goto done;
    }

    WGPUVertexAttribute attrs[2] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Unorm8x4, .offset = 12, .shaderLocation = 1 }
    };
    WGPUVertexBufferLayout layout = {
        .arrayStride = sizeof(box_line_vertex_t),
        .stepMode = WGPUVertexStepMode_Vertex,
        .attributeCount = 2,
        .attributes = attrs
    };
    WGPUBlendState blend = {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
            WGPUBlendFactor_OneMinusSrcAlpha },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
            WGPUBlendFactor_OneMinusSrcAlpha }
    };
    WGPUColorTargetState target = {
        .format = hdr ? BOX_EFFECT_HDR_FORMAT : r->format,
        .blend = &blend,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = WGPUOptionalBool_False,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always },
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };
    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 1, .bindGroupLayouts = &r->view_layout };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &layout_desc);
    WGPURenderPipelineDescriptor desc = {
        .label = BOX_GPU_LABEL("lines"),
        .layout = pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN },
            .bufferCount = 1,
            .buffers = &layout
        },
        .fragment = &fragment,
        .depthStencil = &depth,
        .primitive = { .topology = WGPUPrimitiveTopology_LineList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    r->lines_pipeline[slot] = wgpuDeviceCreateRenderPipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(module);
done:
    return r->lines_pipeline[slot];
}

void box_lines_upload(
    box_renderer_t *r,
    const box_packet_t *p)
{
    r->lines_count = 0;
    if (!p->lines.count) {
        goto done;
    }

    if (p->lines.count > r->lines_capacity) {
        uint32_t capacity = r->lines_capacity ? r->lines_capacity : 256;
        while (capacity < p->lines.count) {
            capacity *= 2;
        }

        if (r->lines_buffer) {
            wgpuBufferRelease(r->lines_buffer);
        }

        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)capacity * sizeof(box_line_vertex_t)
        };
        r->lines_buffer = wgpuDeviceCreateBuffer(r->device, &desc);
        r->lines_capacity = capacity;
    }

    wgpuQueueWriteBuffer(r->gpu_queue, r->lines_buffer, 0, p->lines.data,
        (size_t)p->lines.count * sizeof(box_line_vertex_t));
    r->lines_count = p->lines.count;
done:
    return;
}

void box_lines_record(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTextureView attachment,
    bool hdr)
{
    if (!r->lines_count || !r->v->view_group) {
        goto done;
    }

    WGPURenderPipeline pipeline = box_lines_pipeline(r, hdr);
    if (!pipeline) {
        goto done;
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
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "lines");
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->view_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, r->lines_buffer, 0,
        (uint64_t)r->lines_count * sizeof(box_line_vertex_t));
    wgpuRenderPassEncoderDraw(pass, r->lines_count, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
done:
    return;
}

void box_lines_fini(
    box_renderer_t *r)
{
    uint32_t slot;
    for (slot = 0; slot < BOX_EFFECT_FORMATS; slot ++) {
        if (r->lines_pipeline[slot]) {
            wgpuRenderPipelineRelease(r->lines_pipeline[slot]);
        }

        r->lines_pipeline[slot] = NULL;
        r->lines_tried[slot] = false;
    }

    if (r->lines_buffer) {
        wgpuBufferRelease(r->lines_buffer);
    }

    r->lines_buffer = NULL;
    r->lines_capacity = 0;
    r->lines_count = 0;
}
