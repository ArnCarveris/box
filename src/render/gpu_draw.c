/**
 * @file render/gpu_draw.c
 * @brief Meshes, buffers and the forward pass of the webgpu backend.
 *
 * Every view of a target renders in order into its color texture with a depth buffer that is
 * cleared to 0 and compared with greater, and draws the batches of its scene indirectly.
 *
 * A view that culls by occlusion draws twice: what its early cull phase kept, then what the
 * late phase found visible behind the depth of the first draw, with the blended draws. A view
 * that renders the atmosphere of its scene draws the sky after its last opaque 3D mesh.
 */

#include "gpu.h"
#include <math.h>
#include <string.h>

#ifdef BOX_RENDER_WGPU

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
    WGPUBindGroupLayoutEntry entries[12] = {
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
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_CubeArray
            }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 9,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_3D
            }
        },
        {
            .binding = 10,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 11,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 12, .entries = entries };
    box_gpu_cull_init(gpu);
    box_gpu_volume_init(gpu);
    box_gpu_reflect_init(gpu);
    gpu->view_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    WGPUBindGroupLayout groups[4] = {
        gpu->view_layout, gpu->material_layout, gpu->instance_layout, gpu->volume_layout
    };
    gpu->mesh_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 4,
            .bindGroupLayouts = groups
        });
    ecs_map_init(&gpu->meshes, NULL);
    ecs_map_init(&gpu->buffers, NULL);
    ecs_map_init(&gpu->forward, NULL);
    ecs_vec_init_t(NULL, &gpu->view_data, uint8_t, 0);
    box_gpu_light_init(gpu);
    box_gpu_shadow_init(gpu);
    box_gpu_effects_init(gpu);
    box_gpu_atmosphere_init(gpu);
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
    if (b->instances) {
        wgpuBindGroupRelease(b->instances);
    }

    wgpuBufferRelease(b->buffer);
    ecs_os_free(b);
    box_gpu_count(gpu, BOX_GPU_STAT_BUFFER_DESTROYED);
}

void box_gpu_forward_reset(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it;
    if (!ecs_map_is_init(&gpu->forward)) {
        goto done;
    }

    it = ecs_map_iter(&gpu->forward);
    while (ecs_map_next(&it)) {
        WGPURenderPipeline pipeline = ecs_map_ptr(&it);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

    ecs_map_clear(&gpu->forward);
done:
    return;
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
    ecs_map_fini(&gpu->forward);
    box_gpu_light_fini(gpu);
    box_gpu_shadow_fini(gpu);
    box_gpu_cull_fini(gpu);
    box_gpu_bake_fini(gpu);
    box_gpu_volume_fini(gpu);
    box_gpu_reflect_fini(gpu);
    box_gpu_effects_fini(gpu);
    box_gpu_atmosphere_fini(gpu);
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

/** Compute the box around the vertex positions of a mesh.
 *
 * @param m The mesh
 * @param vertices The vertices
 * @param count Number of vertices
 */
static void box_gpu_mesh_bounds(
    box_gpu_mesh_t *m,
    const box_vertex_t *vertices,
    uint32_t count)
{
    vec3 lo, hi;
    uint32_t i;
    glm_vec3_copy(ECS_CONST_CAST(float*, vertices[0].position), lo);
    glm_vec3_copy(lo, hi);
    for (i = 1; i < count; i ++) {
        glm_vec3_minv(lo, ECS_CONST_CAST(float*, vertices[i].position), lo);
        glm_vec3_maxv(hi, ECS_CONST_CAST(float*, vertices[i].position), hi);
    }

    glm_vec3_center(lo, hi, m->center);
    glm_vec3_sub(hi, m->center, m->extent);
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
    box_gpu_mesh_bounds(m, (const box_vertex_t*)vertices, desc->vertex_count);
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
    b->usage = desc->usage;
    b->version = ++ gpu->buffer_writes;
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
        b->version = ++ gpu->buffer_writes;
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
 * The 2D shape shader blends its edges in the opaque pass too and draws over equal depths, so
 * coplanar shapes draw in order.
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
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    WGPUBlendState blend_state;
    WGPUVertexBufferLayout buffers[2] = {
        {
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = sizeof(box_vertex_t),
            .attributeCount = 3,
            .attributes = vertex_attrs
        },
        {
            .stepMode = WGPUVertexStepMode_Instance,
            .arrayStride = sizeof(uint32_t),
            .attributeCount = 1,
            .attributes = &slot_attr
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

#ifndef __EMSCRIPTEN__
/** Keep the error a scope around a pipeline creation caught, as one line: the causes wgpu
 * lists below its "Caused by:" line joined by ": ".
 *
 * @param status Whether the scope was popped
 * @param type Kind of error, NoError when there was none
 * @param message Error text
 * @param userdata1 The char* that receives the error
 * @param userdata2 Unused
 */
static void box_gpu_forward_on_scope(
    WGPUPopErrorScopeStatus status,
    WGPUErrorType type,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    const char *text = message.data ? message.data : "";
    const char *end = text + (message.length == WGPU_STRLEN
        ? ecs_os_strlen(text) : (ecs_size_t)message.length);
    const char *line = text;
    int32_t lines = 0;
    (void)userdata2;
    if (status != WGPUPopErrorScopeStatus_Success || type == WGPUErrorType_NoError) {
        goto done;
    }

    while (line < end) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        const char *stop = next ? next : end;
        while (line < stop && (*line == ' ' || *line == '\t')) {
            line ++;
        }

        bool skip = line == stop || !strncmp(line, "Validation Error", 16) ||
            !strncmp(line, "Caused by:", 10) || !strncmp(line, "In wgpu", 7);
        if (!skip) {
            ecs_strbuf_append(&buf, "%s%.*s", lines ++ ? ": " : "", (int)(stop - line), line);
        }

        line = next ? next + 1 : end;
    }

    if (!lines) {
        ecs_strbuf_appendstr(&buf, "the pipeline cannot be created");
    }

    *(char**)userdata1 = ecs_strbuf_get(&buf);
done:
    ecs_strbuf_reset(&buf);
}
#endif

char* box_gpu_forward_check(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule shader)
{
    char *result = NULL;
#ifndef __EMSCRIPTEN__
    WGPURenderPipeline pipeline;
    if (!gpu->device || !gpu->mesh_layout) {
        goto done;
    }

    wgpuDevicePushErrorScope(gpu->device, WGPUErrorFilter_Validation);
    pipeline = box_gpu_forward_create(
        gpu, handle, shader, WGPUTextureFormat_RGBA8Unorm, Opaque, BackFaceCull);
    wgpuDevicePopErrorScope(gpu->device, (WGPUPopErrorScopeCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_gpu_forward_on_scope,
        .userdata1 = &result
    });
    if (pipeline) {
        wgpuRenderPipelineRelease(pipeline);
    }

done:
#else
    (void)gpu;
    (void)handle;
    (void)shader;
#endif

    return result;
}

/** Pack the state a forward pipeline is created for into its cache key.
 *
 * @param shader Shader handle
 * @param format Format of the color target, below 2^24
 * @param blend BlendMode, below 16
 * @param back_face BackFaceMode, of which the pipeline only tells BackFaceCull from the rest
 * @return The key, distinct for every distinct pipeline
 */
static uint64_t box_gpu_forward_key(
    uint32_t shader,
    WGPUTextureFormat format,
    uint32_t blend,
    uint32_t back_face)
{
    ecs_assert((uint32_t)format < (1u << 24), ECS_INTERNAL_ERROR, NULL);

    return ((uint64_t)shader << 32) | ((uint64_t)format << 8) | ((uint64_t)blend << 4) |
        (back_face == BackFaceCull ? 0u : 1u);
}

WGPURenderPipeline box_gpu_forward_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_batch_t *b)
{
    WGPURenderPipeline result = NULL;
    WGPUShaderModule shader;
    ecs_map_val_t *cached;
    uint64_t key;
    if (b->blend > Max) {
        goto done;
    }

    shader = box_gpu_shader_get(gpu, b->pipeline);
    if (!shader) {
        goto done;
    }

    key = box_gpu_forward_key(b->pipeline, format, b->blend, b->back_face);
    cached = ecs_map_get(&gpu->forward, key);
    if (cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    result = box_gpu_forward_create(gpu, b->pipeline, shader, format, b->blend, b->back_face);
    ecs_map_insert_ptr(&gpu->forward, key, result);
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
 * is under the view root of the target, or the target has none and no surface claims the view
 */
static bool box_gpu_target_has_view(
    const box_render_target_t *frame,
    const box_render_view_t *v)
{
    bool assigned = v->target || frame->exclusive;

    bool root = frame->view_root ? v->parent == frame->view_root : !v->claimed;

    return assigned ? v->target == frame->handle : root;
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
    box_gpu_cull_begin(gpu, packet);
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
            (float)t->desc.point_width, (float)t->desc.point_height, proj);
        glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), u->view_proj);
        glm_vec3_copy(ECS_CONST_CAST(float*, v->camera_transform[3]), u->camera);
        u->camera[3] = 1;
        u->viewport[0] = (float)t->desc.render_width;
        u->viewport[1] = (float)t->desc.render_height;
        u->viewport[2] = 1.0f / (float)(t->desc.render_width > 0 ? t->desc.render_width : 1);
        u->viewport[3] = 1.0f / (float)(t->desc.render_height > 0 ? t->desc.render_height : 1);
        box_gpu_light_uniforms(gpu, v, proj, u);
        box_gpu_shadow_uniforms(gpu, v, proj, result, u);
        box_gpu_atmosphere_uniforms(gpu, v, u);
        box_gpu_cull_add(gpu, t, packet, i, result, proj, u);
        result ++;
    }

    if (result) {
        box_gpu_cull_upload(gpu);
        box_gpu_shadow_upload(gpu, result);
        box_gpu_view_reserve(gpu, result);
        wgpuQueueWriteBuffer(gpu->queue, gpu->view_buffer, 0,
            ecs_vec_first(&gpu->view_data), (size_t)result * BOX_GPU_VIEW_STRIDE);
    }

    return result;
}

/** Record the sky of a view in a pass of a phase that draws the background, then bind the
 * groups of the forward pass again, which the sky pipeline does not share.
 *
 * @param gpu The backend
 * @param t The target
 * @param pass The render pass of the view
 * @param format The format of the color target
 * @param v The view
 * @param group The bind group of the view
 * @param offset Offset of the uniforms of the view in the view buffer
 * @return Whether the sky was drawn, so the pipeline and material bound before are not
 */
static bool box_gpu_draw_sky(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    WGPUBindGroup group,
    uint32_t offset)
{
    bool result = box_gpu_atmosphere_draw_sky(gpu, t, pass, format, v);
    if (result) {
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        wgpuRenderPassEncoderSetBindGroup(pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    }

    return result;
}

/** Record the indirect draws of the batches a view renders in a pass, in packet order.
 *
 * The sky of a view that renders its atmosphere draws after the opaque 3D meshes and before
 * the 2D shapes and the blended draws, in the phases that draw those.
 *
 * @param gpu The backend
 * @param t The target
 * @param pass The render pass of the view
 * @param format The format of the color target
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 * @param phase 0 for every draw of a view without occlusion, 1 for the opaque meshes of the
 * early phase, 2 for what the late phase kept, the 2D shapes and the blended draws
 */
static void box_gpu_draw_batches(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot,
    int32_t phase)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup material = NULL;
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    bool sky = phase == 1 || !box_gpu_atmosphere_sky(gpu, v);
    int32_t i;
    for (i = 0; cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        WGPURenderPipeline pipeline = box_gpu_forward_pipeline(gpu, format, b);
        bool mesh = b->view == -1 && b->pipeline == BOX_RENDER_SHADER_MESH;
        if (!sky && !mesh) {
            sky = true;
            if (box_gpu_draw_sky(gpu, t, pass, format, v, group, offset)) {
                bound = NULL;
                material = NULL;
            }
        }

        if (!pipeline || (phase == 1 && !mesh)) {
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

        if (box_gpu_cull_draw(gpu, pass, packet, d, 2, phase == 2 && mesh)) {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }
    }

    if (!sky) {
        box_gpu_draw_sky(gpu, t, pass, format, v, group, offset);
    }
}

/** Record a render pass of a view and its draws of one phase.
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param color The texture view the view renders into
 * @param hdr The HDR image of a view with effects, NULL to render into color
 * @param packet The packet
 * @param v The view
 * @param slot Slot of the view in the view buffer
 * @param phase 0 for the only pass, 1 for the early and 2 for the late pass of a view that
 * culls by occlusion
 */
static void box_gpu_draw_pass(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    WGPUTextureView color,
    WGPUTextureView hdr,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t slot,
    int32_t phase)
{
    bool clear = (v->clear.enabled || hdr) && phase < 2;
    WGPURenderPassColorAttachment attachment = {
        .view = hdr ? hdr : color,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = {
            box_gpu_linear(v->clear.color.r),
            box_gpu_linear(v->clear.color.g),
            box_gpu_linear(v->clear.color.b),
            (double)v->clear.color.a / 255.0
        }
    };
    if (hdr && !v->clear.enabled) {
        attachment.clearValue = (WGPUColor){0};
    }

    WGPURenderPassDepthStencilAttachment depth = {
        .view = t->depth_view,
        .depthLoadOp = phase < 2 ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .depthStoreOp = phase == 1 || hdr ? WGPUStoreOp_Store : WGPUStoreOp_Discard,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment,
        .depthStencilAttachment = &depth
    };
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    WGPUBindGroup group = box_gpu_light_group(gpu, t, v, slot);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    box_gpu_draw_batches(gpu, t, pass, hdr ? BOX_GPU_HDR_FORMAT : t->format, packet, v, group,
        slot, phase);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
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

        WGPUTextureView hdr = box_gpu_effects_begin(gpu, t, packet, v);
        bool occludes = box_gpu_cull_occludes(gpu, slot);
        box_gpu_atmosphere_view(gpu, t, encoder, packet, v);
        box_gpu_light_cluster(gpu, t, encoder, v, slot);
        box_gpu_cull_record(gpu, encoder, slot);
        box_gpu_shadow_render(gpu, encoder, packet, i, slot);
        box_gpu_draw_pass(gpu, t, encoder, color, hdr, packet, v, slot, occludes ? 1 : 0);
        if (occludes) {
            box_gpu_cull_record_late(gpu, encoder, t->depth_view, slot);
            box_gpu_draw_pass(gpu, t, encoder, color, hdr, packet, v, slot, 2);
        }

        if (hdr) {
            box_gpu_effects_run(gpu, t, encoder, packet, v, color);
        }

        box_gpu_count(gpu, BOX_GPU_STAT_PASS);
        slot ++;
    }

    box_gpu_cull_finish(gpu, encoder);
done:
    return;
}

#endif
