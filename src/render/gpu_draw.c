/**
 * @file render/gpu_draw.c
 * @brief Meshes, buffers and the forward pass of the webgpu backend.
 *
 * Every view of a target renders in order into its color texture with a depth buffer that is
 * cleared to 0 and compared with greater, and draws the batches of the scene of its camera.
 */

#include "gpu.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

/** Instance attributes of the forward pass, vec4 each */
#define BOX_GPU_INSTANCE_ATTRS ((int32_t)(sizeof(box_render_instance_t) / sizeof(vec4)))

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static double box_gpu_linear(
    uint8_t value)
{
    double c = (double)value / 255.0;

    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/** Create a buffer.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_buffer_new(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    WGPUBufferDescriptor desc = {
        .usage = usage | WGPUBufferUsage_CopyDst,
        .size = (size + 3) & ~(uint64_t)3
    };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

void box_gpu_draw_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[5] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_view_uniforms_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Comparison }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 5, .entries = entries };
    gpu->view_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    WGPUBindGroupLayout groups[2] = { gpu->view_layout, gpu->material_layout };
    gpu->mesh_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    ecs_map_init(&gpu->meshes, NULL);
    ecs_map_init(&gpu->buffers, NULL);
    ecs_vec_init_t(NULL, &gpu->view_data, uint8_t, 0);
    box_gpu_light_init(gpu);
    box_gpu_shadow_init(gpu);
}

/** Release the buffers of a mesh and the mesh.
 *
 * @param gpu The backend
 * @param m The mesh
 */
static void box_gpu_mesh_free(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m)
{
    wgpuBufferRelease(m->vertices);
    wgpuBufferRelease(m->indices);
    ecs_os_free(m);
    box_gpu_count(gpu, BOX_GPU_STAT_MESH_DESTROYED);
}

/** Release a buffer and its handle.
 *
 * @param gpu The backend
 * @param b The buffer
 */
static void box_gpu_buffer_free(
    box_gpu_t *gpu,
    box_gpu_buffer_t *b)
{
    wgpuBufferRelease(b->buffer);
    ecs_os_free(b);
    box_gpu_count(gpu, BOX_GPU_STAT_BUFFER_DESTROYED);
}

void box_gpu_forward_reset(
    box_gpu_t *gpu)
{
    int32_t i;
    for (i = 0; i < gpu->forward_count; i ++) {
        if (gpu->forward[i].pipeline) {
            wgpuRenderPipelineRelease(gpu->forward[i].pipeline);
        }

        gpu->forward[i].pipeline = NULL;
    }

    gpu->forward_count = 0;
}

void box_gpu_draw_fini(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->meshes);
    while (ecs_map_next(&it)) {
        box_gpu_mesh_free(gpu, ecs_map_ptr(&it));
    }

    it = ecs_map_iter(&gpu->buffers);
    while (ecs_map_next(&it)) {
        box_gpu_buffer_free(gpu, ecs_map_ptr(&it));
    }

    ecs_map_fini(&gpu->meshes);
    ecs_map_fini(&gpu->buffers);
    ecs_vec_fini_t(NULL, &gpu->view_data, uint8_t);
    box_gpu_forward_reset(gpu);
    box_gpu_light_fini(gpu);
    box_gpu_shadow_fini(gpu);
    if (gpu->view_buffer) {
        wgpuBufferRelease(gpu->view_buffer);
        gpu->view_buffer = NULL;
    }

    if (gpu->mesh_layout) {
        wgpuPipelineLayoutRelease(gpu->mesh_layout);
        gpu->mesh_layout = NULL;
    }

    if (gpu->view_layout) {
        wgpuBindGroupLayoutRelease(gpu->view_layout);
        gpu->view_layout = NULL;
    }
}

void box_gpu_mesh_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_mesh_desc_t *desc)
{
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, handle);
    if (m) {
        ecs_map_remove(&gpu->meshes, handle);
        box_gpu_mesh_free(gpu, m);
    }

    if (op == BOX_RENDER_DESTROY || !desc || !desc->vertex_count || !desc->index_count) {
        goto done;
    }

    uint64_t vertex_size = (uint64_t)desc->vertex_count * sizeof(box_vertex_t);
    uint64_t index_size = (uint64_t)desc->index_count * sizeof(uint32_t);
    const uint8_t *vertices = (const uint8_t*)(desc + 1);
    m = ecs_os_calloc_t(box_gpu_mesh_t);
    m->vertices = box_gpu_buffer_new(gpu, vertex_size, WGPUBufferUsage_Vertex);
    m->indices = box_gpu_buffer_new(gpu, index_size, WGPUBufferUsage_Index);
    m->index_count = desc->index_count;
    wgpuQueueWriteBuffer(gpu->queue, m->vertices, 0, vertices, (size_t)vertex_size);
    wgpuQueueWriteBuffer(gpu->queue, m->indices, 0, vertices + vertex_size, (size_t)index_size);
    ecs_map_insert_ptr(&gpu->meshes, handle, m);
    box_gpu_count(gpu, BOX_GPU_STAT_MESH_CREATED);
done:
    return;
}

void box_gpu_buffer_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_buffer_desc_t *desc)
{
    box_gpu_buffer_t *b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, handle);
    WGPUBufferUsage usage = 0;
    if (b) {
        ecs_map_remove(&gpu->buffers, handle);
        box_gpu_buffer_free(gpu, b);
    }

    if (op == BOX_RENDER_DESTROY || !desc || !desc->size) {
        goto done;
    }

    if (desc->usage & BOX_RENDER_BUFFER_INSTANCE) {
        usage |= WGPUBufferUsage_Vertex;
    }

    if (desc->usage & BOX_RENDER_BUFFER_STORAGE) {
        usage |= WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    }

    b = ecs_os_calloc_t(box_gpu_buffer_t);
    b->buffer = box_gpu_buffer_new(gpu, desc->size, usage);
    b->size = desc->size;
    ecs_map_insert_ptr(&gpu->buffers, handle, b);
    box_gpu_count(gpu, BOX_GPU_STAT_BUFFER_CREATED);
done:
    return;
}

void box_gpu_upload(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->uploads);
    for (i = 0; i < count; i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &packet->uploads, box_render_upload_t, i);
        box_gpu_buffer_t *b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, u->buffer);
        if (!b || (uint64_t)u->offset + u->size > b->size || (u->offset | u->size) & 3) {
            ecs_err("upload of %u bytes at %u does not fit buffer %u",
                u->size, u->offset, u->buffer);
            box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
            continue;
        }

        wgpuQueueWriteBuffer(gpu->queue, b->buffer, u->offset,
            box_render_packet_data(packet, u->data), u->size);
    }
}

/** Get the blend state of a blend mode.
 *
 * The fragment shader shapes its output for the equation: multiply and min mix toward
 * white by the alpha, screen and max scale by it.
 *
 * @param blend The BlendMode
 * @param out Receives the blend state
 */
static void box_gpu_blend_state(
    uint32_t blend,
    WGPUBlendState *out)
{
    WGPUBlendComponent color = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_SrcAlpha,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
    };
    WGPUBlendComponent alpha = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
    };
    switch (blend) {
    case PremultipliedAlpha:
        color.srcFactor = WGPUBlendFactor_One;
        break;
    case Additive:
        color.dstFactor = WGPUBlendFactor_One;
        alpha.dstFactor = WGPUBlendFactor_One;
        alpha.srcFactor = WGPUBlendFactor_Zero;
        break;
    case Multiply:
        color.srcFactor = WGPUBlendFactor_Dst;
        color.dstFactor = WGPUBlendFactor_Zero;
        break;
    case Screen:
        color.srcFactor = WGPUBlendFactor_One;
        color.dstFactor = WGPUBlendFactor_OneMinusSrc;
        break;
    case Subtractive:
        color.operation = WGPUBlendOperation_ReverseSubtract;
        color.dstFactor = WGPUBlendFactor_One;
        alpha.srcFactor = WGPUBlendFactor_Zero;
        alpha.dstFactor = WGPUBlendFactor_One;
        break;
    case Min:
    case Max:
        color.operation = blend == Min ? WGPUBlendOperation_Min : WGPUBlendOperation_Max;
        color.srcFactor = WGPUBlendFactor_One;
        color.dstFactor = WGPUBlendFactor_One;
        break;
    default:
        break;
    }

    out->color = color;
    out->alpha = alpha;
}

/** Create the forward pipeline of a shader, color format, blend mode and back face mode.
 *
 * The 2D shape shader reads the shape words of an instance as integers, blends its edges in
 * the opaque pass too and draws over equal depths, so coplanar shapes draw in order.
 *
 * @param gpu The backend
 * @param handle Handle of the shader, the mesh or the 2D shape shader
 * @param shader The shader module
 * @param format The format of the color target
 * @param blend The BlendMode, Opaque writes depth and does not blend
 * @param back_face The BackFaceMode, BackFaceCull culls the back faces
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_forward_create(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule shader,
    WGPUTextureFormat format,
    uint32_t blend,
    uint32_t back_face)
{
    bool shape2d = handle == BOX_RENDER_SHADER_SHAPE2D;
    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute instance_attrs[BOX_GPU_INSTANCE_ATTRS];
    WGPUBlendState blend_state;
    int32_t i;
    for (i = 0; i < BOX_GPU_INSTANCE_ATTRS; i ++) {
        instance_attrs[i] = (WGPUVertexAttribute){
            .format = WGPUVertexFormat_Float32x4,
            .offset = (uint64_t)i * 16,
            .shaderLocation = (uint32_t)(3 + i)
        };
    }

    if (shape2d) {
        instance_attrs[6].format = WGPUVertexFormat_Uint32x4;
    }

    WGPUVertexBufferLayout buffers[2] = {
        {
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = sizeof(box_vertex_t),
            .attributeCount = 3,
            .attributes = vertex_attrs
        },
        {
            .stepMode = WGPUVertexStepMode_Instance,
            .arrayStride = sizeof(box_render_instance_t),
            .attributeCount = BOX_GPU_INSTANCE_ATTRS,
            .attributes = instance_attrs
        }
    };
    box_gpu_blend_state(shape2d && blend == Opaque ? AlphaBlend : blend, &blend_state);
    WGPUColorTargetState target = {
        .format = format,
        .blend = blend == Opaque && !shape2d ? NULL : &blend_state,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUConstantEntry constant = {
        .key = { .data = "blend_mode", .length = WGPU_STRLEN },
        .value = (double)blend
    };
    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .constantCount = 1,
        .constants = &constant,
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = blend == Opaque ? WGPUOptionalBool_True : WGPUOptionalBool_False,
        .depthCompare = shape2d
            ? WGPUCompareFunction_GreaterEqual
            : WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = gpu->mesh_layout,
        .vertex = {
            .module = shader,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN },
            .bufferCount = 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = back_face == BackFaceCull ? WGPUCullMode_Back : WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };

    return wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
}

/** Get the forward pipeline of a color format and the state of a batch, created on first use.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @param b The batch
 * @return The pipeline, NULL without a mesh shader or when too many pipelines are in use
 */
static WGPURenderPipeline box_gpu_forward_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_batch_t *b)
{
    WGPURenderPipeline result = NULL;
    WGPUShaderModule shader = box_gpu_shader_get(gpu, b->pipeline);
    box_gpu_forward_t *f;
    int32_t i;
    for (i = 0; i < gpu->forward_count; i ++) {
        f = &gpu->forward[i];
        if (f->shader == b->pipeline && f->format == format && f->blend == b->blend &&
            f->back_face == b->back_face)
        {
            result = f->pipeline;
            goto done;
        }
    }

    if (!shader || gpu->forward_count == BOX_GPU_FORWARD_PIPELINES || b->blend > Max) {
        goto done;
    }

    result = box_gpu_forward_create(gpu, b->pipeline, shader, format, b->blend, b->back_face);
    f = &gpu->forward[gpu->forward_count ++];
    f->shader = b->pipeline;
    f->format = format;
    f->blend = b->blend;
    f->back_face = b->back_face;
    f->pipeline = result;
done:
    return result;
}

/** Make the view buffer hold the uniforms of a number of views.
 *
 * @param gpu The backend
 * @param count Views of the target
 */
static void box_gpu_view_reserve(
    box_gpu_t *gpu,
    int32_t count)
{
    int32_t capacity = gpu->view_capacity ? gpu->view_capacity : 4;
    if (gpu->view_buffer && count <= gpu->view_capacity) {
        goto done;
    }

    while (capacity < count) {
        capacity *= 2;
    }

    if (gpu->view_buffer) {
        wgpuBufferRelease(gpu->view_buffer);
    }

    gpu->view_capacity = capacity;
    gpu->view_buffer = box_gpu_buffer_new(gpu,
        (uint64_t)capacity * BOX_GPU_VIEW_STRIDE, WGPUBufferUsage_Uniform);
done:
    return;
}

/** Check whether a target renders a view.
 *
 * @param frame The target of the packet
 * @param v The view
 * @return Whether the view is assigned to the target, or, when neither is exclusive, the view
 * is under the view root of the target or the target has none
 */
static bool box_gpu_target_has_view(
    const box_render_target_t *frame,
    const box_render_view_t *v)
{
    bool assigned = v->target || frame->exclusive;

    return assigned
        ? v->target == frame->handle
        : !frame->view_root || v->parent == frame->view_root;
}

/** Write the uniforms of the views a target renders into the view buffer.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param frame The target of the packet
 * @return The number of views the target renders
 */
static int32_t box_gpu_view_write(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_target_t *frame)
{
    int32_t i, result = 0, count = ecs_vec_count(&packet->views);
    ecs_vec_set_count_t(NULL, &gpu->view_data, uint8_t, count * BOX_GPU_VIEW_STRIDE);
    for (i = 0; i < count; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, i);
        if (!box_gpu_target_has_view(frame, v)) {
            continue;
        }

        box_gpu_view_uniforms_t *u = (box_gpu_view_uniforms_t*)ecs_vec_get_t(
            &gpu->view_data, uint8_t, result * BOX_GPU_VIEW_STRIDE);
        mat4 proj;
        ecs_os_memset_t(u, 0, box_gpu_view_uniforms_t);
        box_camera_projection(&v->camera_data,
            (float)t->desc.width, (float)t->desc.height, proj);
        glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), u->view_proj);
        glm_vec3_copy(ECS_CONST_CAST(float*, v->camera_transform[3]), u->camera);
        u->camera[3] = 1;
        u->viewport[0] = (float)t->desc.render_width;
        u->viewport[1] = (float)t->desc.render_height;
        u->viewport[2] = 1.0f / (float)(t->desc.render_width > 0 ? t->desc.render_width : 1);
        u->viewport[3] = 1.0f / (float)(t->desc.render_height > 0 ? t->desc.render_height : 1);
        box_gpu_light_uniforms(gpu, v, proj, u);
        box_gpu_shadow_uniforms(gpu, v, proj, result, u);
        result ++;
    }

    if (result) {
        box_gpu_shadow_upload(gpu, result);
        box_gpu_view_reserve(gpu, result);
        wgpuQueueWriteBuffer(gpu->queue, gpu->view_buffer, 0,
            ecs_vec_first(&gpu->view_data), (size_t)result * BOX_GPU_VIEW_STRIDE);
    }

    return result;
}

/** Get the phase a batch draws in: opaque meshes, then opaque 2D shapes, then blended.
 *
 * The 2D shapes blend their anti-aliased edges, so they draw over the opaque meshes behind.
 *
 * @param b The batch
 * @return The phase, -1 for a pipeline the forward pass does not draw
 */
static int32_t box_gpu_draw_phase(
    const box_render_batch_t *b)
{
    int32_t result = -1;
    if (b->pipeline == BOX_RENDER_SHADER_MESH) {
        result = 0;
    } else if (b->pipeline == BOX_RENDER_SHADER_SHAPE2D) {
        result = 1;
    }

    if (result != -1 && b->blend != Opaque) {
        result = 2;
    }

    return result;
}

/** Record the draws of the batches a view renders, in packet order within each phase.
 *
 * @param gpu The backend
 * @param pass The render pass of the view
 * @param format The format of the color target
 * @param packet The packet
 * @param index Index of the view in the packet
 */
static void box_gpu_draw_batches(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    int32_t index)
{
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup material = NULL;
    int32_t i, count = ecs_vec_count(&packet->batches);
    for (i = 0; i < count * 3; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, i % count);
        if ((b->view != -1 && b->view != index) || b->scene != v->scene ||
            box_gpu_draw_phase(b) != i / count || !b->instance_count)
        {
            continue;
        }

        box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
        box_gpu_buffer_t *inst = ecs_map_get_deref(
            &gpu->buffers, box_gpu_buffer_t, b->instances);
        WGPURenderPipeline pipeline = box_gpu_forward_pipeline(gpu, format, b);
        if (!m || !inst || !pipeline) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup group = box_gpu_material_group(gpu, b, packet->anisotropy);
        if (group != material) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, group, 0, NULL);
            material = group;
        }

        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, inst->buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(
            pass, m->indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(
            pass, m->index_count, b->instance_count, 0, 0, b->first_instance);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
    }
}

void box_gpu_draw_views(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_target_t *frame,
    WGPUCommandEncoder encoder,
    WGPUTextureView color)
{
    int32_t i, slot = 0, count = ecs_vec_count(&packet->views);
    if (!box_gpu_view_write(gpu, t, packet, frame)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, i);
        if (!box_gpu_target_has_view(frame, v)) {
            continue;
        }

        WGPURenderPassColorAttachment attachment = {
            .view = color,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = v->clear.enabled ? WGPULoadOp_Clear : WGPULoadOp_Load,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = {
                box_gpu_linear(v->clear.color.r),
                box_gpu_linear(v->clear.color.g),
                box_gpu_linear(v->clear.color.b),
                (double)v->clear.color.a / 255.0
            }
        };
        WGPURenderPassDepthStencilAttachment depth = {
            .view = t->depth_view,
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Discard,
            .depthClearValue = 0.0f
        };
        WGPURenderPassDescriptor desc = {
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
            .depthStencilAttachment = &depth
        };
        uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
        box_gpu_light_cluster(gpu, encoder, v, slot);
        box_gpu_shadow_render(gpu, encoder, packet, i, slot);
        WGPUBindGroup group = box_gpu_light_group(gpu, v, slot);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        box_gpu_draw_batches(gpu, pass, t->format, packet, i);

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(group);
        box_gpu_count(gpu, BOX_GPU_STAT_PASS);
        slot ++;
    }

done:
    return;
}

#endif
