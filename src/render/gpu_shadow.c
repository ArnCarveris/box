/**
 * @file render/gpu_shadow.c
 * @brief Cascaded shadow maps of the webgpu backend.
 *
 * Before a view with shadows draws, the opaque casters the cull pass kept in each cascade
 * render with etc/shaders/shadow.wgsl into the shared shadow map, depth only, reversed Z. Probe
 * captures use a map of their own with one cascade around the static bounds of the scene.
 *
 * The forward pass samples the map with etc/shaders/include/shadow.wgsl: the first cascade
 * whose box, shrunk by the reach of the filter and the normal offset, contains a position.
 */

#include "gpu.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

void box_gpu_shadow_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = true,
            .minBindingSize = sizeof(mat4)
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 1, .entries = &entry };
    WGPUSamplerDescriptor sampler = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 1.0f,
        .compare = WGPUCompareFunction_GreaterEqual,
        .maxAnisotropy = 1
    };
    WGPUTextureDescriptor empty = {
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = BOX_GPU_SHADOW_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTextureViewDescriptor view = {
        .format = BOX_GPU_SHADOW_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_DepthOnly
    };
    gpu->shadow_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    WGPUBindGroupLayout groups[2] = { gpu->shadow_layout, gpu->instance_layout };
    gpu->shadow_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    gpu->shadow_sampler = wgpuDeviceCreateSampler(gpu->device, &sampler);
    gpu->shadow_empty = wgpuDeviceCreateTexture(gpu->device, &empty);
    gpu->shadow_empty_view = wgpuTextureCreateView(gpu->shadow_empty, &view);
    ecs_vec_init_t(NULL, &gpu->shadow_data, uint8_t, 0);
}

/** Release the shadow map and the views of its cascades.
 *
 * @param gpu The backend
 */
static void box_gpu_shadow_map_free(
    box_gpu_t *gpu)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_SHADOW_CASCADES; i ++) {
        if (gpu->shadow_layers[i]) {
            wgpuTextureViewRelease(gpu->shadow_layers[i]);
            gpu->shadow_layers[i] = NULL;
        }
    }

    if (gpu->shadow_map_view) {
        wgpuTextureViewRelease(gpu->shadow_map_view);
        gpu->shadow_map_view = NULL;
    }

    if (gpu->shadow_map) {
        wgpuTextureRelease(gpu->shadow_map);
        gpu->shadow_map = NULL;
    }

    gpu->shadow_resolution = 0;
    gpu->shadow_cascades = 0;
}

/** Release the shadow map of the probe captures and its uniform buffer.
 *
 * @param gpu The backend
 */
static void box_gpu_shadow_capture_free(
    box_gpu_t *gpu)
{
    if (gpu->capture_shadow_group) {
        wgpuBindGroupRelease(gpu->capture_shadow_group);
        gpu->capture_shadow_group = NULL;
    }

    if (gpu->capture_shadow_buffer) {
        wgpuBufferRelease(gpu->capture_shadow_buffer);
        gpu->capture_shadow_buffer = NULL;
    }

    if (gpu->capture_shadow_layer) {
        wgpuTextureViewRelease(gpu->capture_shadow_layer);
        gpu->capture_shadow_layer = NULL;
    }

    if (gpu->capture_shadow_view) {
        wgpuTextureViewRelease(gpu->capture_shadow_view);
        gpu->capture_shadow_view = NULL;
    }

    if (gpu->capture_shadow) {
        wgpuTextureRelease(gpu->capture_shadow);
        gpu->capture_shadow = NULL;
    }
}

void box_gpu_shadow_fini(
    box_gpu_t *gpu)
{
    box_gpu_shadow_map_free(gpu);
    box_gpu_shadow_capture_free(gpu);
    ecs_vec_fini_t(NULL, &gpu->shadow_data, uint8_t);
    if (gpu->shadow) {
        wgpuRenderPipelineRelease(gpu->shadow);
        gpu->shadow = NULL;
    }

    if (gpu->shadow_buffer) {
        wgpuBufferRelease(gpu->shadow_buffer);
        gpu->shadow_buffer = NULL;
    }

    if (gpu->shadow_empty_view) {
        wgpuTextureViewRelease(gpu->shadow_empty_view);
        gpu->shadow_empty_view = NULL;
    }

    if (gpu->shadow_empty) {
        wgpuTextureRelease(gpu->shadow_empty);
        gpu->shadow_empty = NULL;
    }

    if (gpu->shadow_sampler) {
        wgpuSamplerRelease(gpu->shadow_sampler);
        gpu->shadow_sampler = NULL;
    }

    if (gpu->shadow_pipeline_layout) {
        wgpuPipelineLayoutRelease(gpu->shadow_pipeline_layout);
        gpu->shadow_pipeline_layout = NULL;
    }

    if (gpu->shadow_layout) {
        wgpuBindGroupLayoutRelease(gpu->shadow_layout);
        gpu->shadow_layout = NULL;
    }
}

float box_gpu_shadow_margin(
    int32_t filter_size,
    float normal_offset)
{
    return 0.5f * (float)filter_size + 1.0f + glm_max(normal_offset, 0.0f);
}

void box_gpu_shadow_cascade_uniforms(
    box_gpu_view_uniforms_t *u,
    int32_t index,
    const box_render_cascade_t *cascade,
    float depth_bias,
    float normal_offset,
    int32_t resolution)
{
    float extent = 0.5f * cascade->texel * (float)resolution;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, cascade->matrix), u->shadow_matrix[index]);
    u->shadow_depth[index] = cascade->depth_range / glm_max(extent, 1e-6f);
    u->shadow_bias[index] = depth_bias * cascade->texel / cascade->depth_range;
    u->shadow_offset[index] = normal_offset * cascade->texel;
}

void box_gpu_shadow_filter_uniforms(
    box_gpu_view_uniforms_t *u,
    int32_t cascades,
    int32_t filter_size,
    float normal_offset,
    float blend,
    int32_t resolution)
{
    float texels = box_gpu_shadow_margin(filter_size, normal_offset);
    u->shadow_params[2] = 1.0f / (float)resolution;
    u->shadow_edge[0] = glm_min(2.0f * texels / (float)resolution, 0.5f);
    u->shadow_edge[1] = blend;
    u->shadow_config[0] = (uint32_t)cascades;
    u->shadow_config[1] = (uint32_t)filter_size;
}

void box_gpu_shadow_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    mat4 proj,
    int32_t slot,
    box_gpu_view_uniforms_t *u)
{
    const box_render_view_shadows_t *s = &v->shadows;
    box_render_cascade_t cascade;
    int32_t i, n = s->cascades;
    if (!s->enabled || n < 1 || n > BOX_RENDER_SHADOW_CASCADES) {
        goto done;
    }

    int32_t size = (slot + 1) * BOX_RENDER_SHADOW_CASCADES * BOX_GPU_CASCADE_STRIDE;
    if (ecs_vec_count(&gpu->shadow_data) < size) {
        ecs_vec_set_count_t(NULL, &gpu->shadow_data, uint8_t, size);
    }

    float distance = s->splits[n - 1];
    float last = n > 1 ? s->splits[n - 2] : s->near_;
    for (i = 0; i < n; i ++) {
        box_render_shadow_cascade(ECS_CONST_CAST(vec4*, v->camera_transform), proj,
            i ? s->splits[i - 1] : s->near_, s->splits[i], s->direction, s->resolution,
            distance, &cascade);
        ecs_os_memcpy(ecs_vec_get_t(&gpu->shadow_data, uint8_t,
            (slot * BOX_RENDER_SHADOW_CASCADES + i) * BOX_GPU_CASCADE_STRIDE),
            cascade.matrix, sizeof(mat4));
        box_gpu_shadow_cascade_uniforms(u, i, &cascade, s->depth_bias, s->normal_offset,
            s->resolution);
    }

    u->shadow_params[0] = distance - s->fade * (distance - last);
    u->shadow_params[1] = distance;
    box_gpu_shadow_filter_uniforms(u, n, s->filter_size, s->normal_offset, s->blend,
        s->resolution);
done:
    return;
}

void box_gpu_shadow_upload(
    box_gpu_t *gpu,
    int32_t count)
{
    int32_t capacity = gpu->shadow_capacity ? gpu->shadow_capacity : 4;
    int32_t size = ecs_vec_count(&gpu->shadow_data);
    int32_t used = count * BOX_RENDER_SHADOW_CASCADES * BOX_GPU_CASCADE_STRIDE;
    if (!size) {
        goto done;
    }

    if (!gpu->shadow_buffer || count > gpu->shadow_capacity) {
        while (capacity < count) {
            capacity *= 2;
        }

        if (gpu->shadow_buffer) {
            wgpuBufferRelease(gpu->shadow_buffer);
        }

        gpu->shadow_capacity = capacity;
        gpu->shadow_buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)capacity * BOX_RENDER_SHADOW_CASCADES * BOX_GPU_CASCADE_STRIDE
        });
    }

    wgpuQueueWriteBuffer(gpu->queue, gpu->shadow_buffer, 0, ecs_vec_first(&gpu->shadow_data),
        (size_t)(used < size ? used : size));
done:
    return;
}

/** Get the shadow pipeline, created again when the shadow shader was recompiled.
 *
 * @param gpu The backend
 * @return The pipeline, NULL while the shader has no module
 */
static WGPURenderPipeline box_gpu_shadow_pipeline(
    box_gpu_t *gpu)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_SHADOW);
    WGPUVertexAttribute vertex_attr = {
        .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0
    };
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    if (!s || !s->module) {
        goto done;
    }

    if (gpu->shadow && gpu->shadow_revision == s->revision) {
        goto done;
    }

    WGPUVertexBufferLayout buffers[2] = {
        {
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = sizeof(box_vertex_t),
            .attributeCount = 1,
            .attributes = &vertex_attr
        },
        {
            .stepMode = WGPUVertexStepMode_Instance,
            .arrayStride = sizeof(uint32_t),
            .attributeCount = 1,
            .attributes = &slot_attr
        }
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_SHADOW_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = gpu->shadow_pipeline_layout,
        .vertex = {
            .module = s->module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN },
            .bufferCount = 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    if (gpu->shadow) {
        wgpuRenderPipelineRelease(gpu->shadow);
    }

    gpu->shadow = wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
    gpu->shadow_revision = s->revision;
done:
    return s && s->module ? gpu->shadow : NULL;
}

/** Make the shadow map hold the cascades of a view at its resolution.
 *
 * @param gpu The backend
 * @param s The shadows of the view
 */
static void box_gpu_shadow_map_ensure(
    box_gpu_t *gpu,
    const box_render_view_shadows_t *s)
{
    int32_t i;
    if (gpu->shadow_map && gpu->shadow_resolution == s->resolution &&
        gpu->shadow_cascades == s->cascades)
    {
        goto done;
    }

    box_gpu_shadow_map_free(gpu);
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { (uint32_t)s->resolution, (uint32_t)s->resolution, (uint32_t)s->cascades },
        .format = BOX_GPU_SHADOW_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    gpu->shadow_map = wgpuDeviceCreateTexture(gpu->device, &desc);
    gpu->shadow_map_view = wgpuTextureCreateView(gpu->shadow_map, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_SHADOW_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .mipLevelCount = 1,
        .arrayLayerCount = (uint32_t)s->cascades,
        .aspect = WGPUTextureAspect_DepthOnly
    });
    for (i = 0; i < s->cascades; i ++) {
        gpu->shadow_layers[i] = wgpuTextureCreateView(gpu->shadow_map,
            &(WGPUTextureViewDescriptor){
                .format = BOX_GPU_SHADOW_FORMAT,
                .dimension = WGPUTextureViewDimension_2D,
                .mipLevelCount = 1,
                .baseArrayLayer = (uint32_t)i,
                .arrayLayerCount = 1,
                .aspect = WGPUTextureAspect_DepthOnly
            });
    }

    gpu->shadow_resolution = s->resolution;
    gpu->shadow_cascades = s->cascades;
done:
    return;
}

/** Record the indirect draws of the shadow casters the cull pass kept in a cascade.
 *
 * @param gpu The backend
 * @param pass The shadow pass of the cascade
 * @param packet The packet
 * @param cv The cull view of the cascade
 */
static void box_gpu_shadow_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_gpu_cull_view_t *cv)
{
    WGPUBindGroup instances = NULL;
    int32_t i;
    for (i = 0; i < cv->draw_count; i ++) {
        box_gpu_cull_draw(gpu, pass, packet, ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i), 1, false, &instances);
    }
}

void box_gpu_shadow_render(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot)
{
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    WGPURenderPipeline pipeline = NULL;
    int32_t i;
    if (!v->shadows.enabled || box_gpu_shadow_binding(gpu, v) != gpu->shadow_map_view) {
        goto done;
    }

    pipeline = box_gpu_shadow_pipeline(gpu);
    if (!pipeline || !gpu->shadow_buffer) {
        goto done;
    }

    WGPUBindGroupEntry entry = {
        .binding = 0, .buffer = gpu->shadow_buffer, .size = sizeof(mat4)
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->shadow_layout,
        .entryCount = 1,
        .entries = &entry
    });
    for (i = 0; i < v->shadows.cascades; i ++) {
        const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, i);
        uint32_t offset = (uint32_t)((slot * BOX_RENDER_SHADOW_CASCADES + i) *
            BOX_GPU_CASCADE_STRIDE);
        WGPURenderPassDepthStencilAttachment depth = {
            .view = gpu->shadow_layers[i],
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Store,
            .depthClearValue = 0.0f
        };
        WGPURenderPassDescriptor desc = { .depthStencilAttachment = &depth };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        if (cv) {
            box_gpu_shadow_draw(gpu, pass, packet, cv);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    wgpuBindGroupRelease(group);
done:
    return;
}

void box_gpu_shadow_capture_fit(
    const float *min,
    const float *max,
    const float *direction,
    box_render_cascade_t *out)
{
    vec3 center, forward, right, up, pole = {0, 1, 0};
    float res = (float)BOX_GPU_CAPTURE_SHADOW_RESOLUTION;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        center[i] = (min[i] + max[i]) * 0.5f;
        forward[i] = -direction[i];
    }

    float radius = glm_max(glm_vec3_distance(ECS_CONST_CAST(float*, min),
        ECS_CONST_CAST(float*, max)) * 0.5f, 1e-3f);
    float extent = radius * res / (res - 2.0f * (float)BOX_GPU_CAPTURE_SHADOW_MARGIN);
    if (glm_vec3_norm(forward) < 1e-6f) {
        glm_vec3_copy((vec3){0, -1, 0}, forward);
    }

    glm_vec3_normalize(forward);
    if (fabsf(forward[1]) > 0.99f) {
        glm_vec3_copy((vec3){0, 0, 1}, pole);
    }

    glm_vec3_crossn(forward, pole, right);
    glm_vec3_cross(right, forward, up);
    float cx = glm_vec3_dot(right, center);
    float cy = glm_vec3_dot(up, center);
    float cz = glm_vec3_dot(forward, center);
    float d_near = cz - extent;
    float d_far = cz + extent;
    float range = d_far - d_near;
    glm_mat4_zero(out->matrix);
    for (i = 0; i < 3; i ++) {
        out->matrix[i][0] = right[i] / extent;
        out->matrix[i][1] = up[i] / extent;
        out->matrix[i][2] = -forward[i] / range;
    }

    out->matrix[3][0] = -cx / extent;
    out->matrix[3][1] = -cy / extent;
    out->matrix[3][2] = d_far / range;
    out->matrix[3][3] = 1.0f;
    out->texel = 2.0f * extent / res;
    out->depth_range = range;
}

/** Create the shadow map of the probe captures, its uniform buffer and bind group.
 *
 * @param gpu The backend
 */
static void box_gpu_shadow_capture_ensure(
    box_gpu_t *gpu)
{
    uint32_t res = BOX_GPU_CAPTURE_SHADOW_RESOLUTION;
    if (gpu->capture_shadow) {
        goto done;
    }

    gpu->capture_shadow = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { res, res, 1 },
        .format = BOX_GPU_SHADOW_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    gpu->capture_shadow_view = wgpuTextureCreateView(gpu->capture_shadow,
        &(WGPUTextureViewDescriptor){
            .format = BOX_GPU_SHADOW_FORMAT,
            .dimension = WGPUTextureViewDimension_2DArray,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_DepthOnly
        });
    gpu->capture_shadow_layer = wgpuTextureCreateView(gpu->capture_shadow,
        &(WGPUTextureViewDescriptor){
            .format = BOX_GPU_SHADOW_FORMAT,
            .dimension = WGPUTextureViewDimension_2D,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_DepthOnly
        });
    gpu->capture_shadow_buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_GPU_CASCADE_STRIDE
    });
    WGPUBindGroupEntry entry = {
        .binding = 0, .buffer = gpu->capture_shadow_buffer, .size = sizeof(mat4)
    };
    gpu->capture_shadow_group = wgpuDeviceCreateBindGroup(gpu->device,
        &(WGPUBindGroupDescriptor){
            .layout = gpu->shadow_layout,
            .entryCount = 1,
            .entries = &entry
        });
done:
    return;
}

WGPUTextureView box_gpu_shadow_capture(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    int32_t slot,
    const box_render_cascade_t *cascade)
{
    WGPURenderPipeline pipeline = box_gpu_shadow_pipeline(gpu);
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, 0);
    WGPUTextureView result = gpu->shadow_empty_view;
    uint32_t offset = 0;
    if (!pipeline) {
        goto done;
    }

    box_gpu_shadow_capture_ensure(gpu);
    wgpuQueueWriteBuffer(gpu->queue, gpu->capture_shadow_buffer, 0, cascade->matrix,
        sizeof(mat4));
    WGPURenderPassDepthStencilAttachment depth = {
        .view = gpu->capture_shadow_layer,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor desc = { .depthStencilAttachment = &depth };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, gpu->capture_shadow_group, 1, &offset);
    if (cv) {
        box_gpu_shadow_draw(gpu, pass, packet, cv);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    result = gpu->capture_shadow_view;
done:
    return result;
}

WGPUTextureView box_gpu_shadow_binding(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    WGPUTextureView result = gpu->shadow_empty_view;
    if (!v->shadows.enabled || v->shadows.cascades < 1 ||
        v->shadows.cascades > BOX_RENDER_SHADOW_CASCADES)
    {
        goto done;
    }

    box_gpu_shadow_map_ensure(gpu, &v->shadows);
    result = gpu->shadow_map_view;
done:
    return result;
}

#endif
