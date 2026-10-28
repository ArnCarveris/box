/**
 * @file render/gpu_shadow.c
 * @brief Cascaded shadow maps of the webgpu backend.
 *
 * Before a view with shadows draws, the opaque casters the cull pass kept in each cascade render
 * with etc/shaders/shadow.wgsl into that cascade of the shared shadow map, depth only, reversed
 * Z; alpha tested casters discard below their cutoff there, so their shadows have the holes.
 *
 * The forward pass then samples the map through a comparison sampler. Probe captures use a
 * map of their own with one cascade around the static bounds of the scene.
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
    WGPUBindGroupLayout groups[3] = {
        gpu->shadow_layout, gpu->instance_layout, gpu->material_layout
    };
    gpu->shadow_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    gpu->shadow_cutout_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 3,
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

    if (gpu->shadow_cutout) {
        wgpuRenderPipelineRelease(gpu->shadow_cutout);
        gpu->shadow_cutout = NULL;
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

    if (gpu->shadow_cutout_layout) {
        wgpuPipelineLayoutRelease(gpu->shadow_cutout_layout);
        gpu->shadow_cutout_layout = NULL;
    }

    if (gpu->shadow_layout) {
        wgpuBindGroupLayoutRelease(gpu->shadow_layout);
        gpu->shadow_layout = NULL;
    }
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
        glm_mat4_copy(cascade.matrix, u->shadow_matrix[i]);
        ecs_os_memcpy(ecs_vec_get_t(&gpu->shadow_data, uint8_t,
            (slot * BOX_RENDER_SHADOW_CASCADES + i) * BOX_GPU_CASCADE_STRIDE),
            cascade.matrix, sizeof(mat4));
        u->shadow_split[i] = s->splits[i];
        u->shadow_bias[i] = s->depth_bias * cascade.texel / cascade.depth_range;
        u->shadow_offset[i] = s->normal_offset * cascade.texel;
    }

    u->shadow_params[0] = distance - s->fade * (distance - last);
    u->shadow_params[1] = distance;
    u->shadow_params[2] = 1.0f / (float)s->resolution;
    u->shadow_config[0] = (uint32_t)n;
    u->shadow_config[1] = (uint32_t)s->filter_size;
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

/** Create a shadow pipeline from the shadow shader.
 *
 * The plain pipeline only reads the position and has no fragment stage; the alpha tested one
 * also reads the normal and UV of the vertices, and its fragment stage discards below the cutoff.
 *
 * @param gpu The backend
 * @param module The shadow shader module
 * @param cutout Whether to create the alpha tested pipeline
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_shadow_create(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    bool cutout)
{
    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    WGPUVertexBufferLayout buffers[2] = {
        {
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = sizeof(box_vertex_t),
            .attributeCount = cutout ? 3 : 1,
            .attributes = vertex_attrs
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
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs_cutout", .length = WGPU_STRLEN }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = cutout ? gpu->shadow_cutout_layout : gpu->shadow_pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = cutout ? "vs_cutout" : "vs", .length = WGPU_STRLEN },
            .bufferCount = 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = cutout ? &fragment : NULL
    };

    return wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
}

/** Get the shadow pipelines, created again when the shadow shader was recompiled.
 *
 * @param gpu The backend
 * @return The plain pipeline, NULL while the shader has no module; the alpha tested one is
 * gpu->shadow_cutout
 */
static WGPURenderPipeline box_gpu_shadow_pipeline(
    box_gpu_t *gpu)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_SHADOW);
    if (!s || !s->module) {
        goto done;
    }

    if (gpu->shadow && gpu->shadow_revision == s->revision) {
        goto done;
    }

    if (gpu->shadow) {
        wgpuRenderPipelineRelease(gpu->shadow);
    }

    if (gpu->shadow_cutout) {
        wgpuRenderPipelineRelease(gpu->shadow_cutout);
    }

    gpu->shadow = box_gpu_shadow_create(gpu, s->module, false);
    gpu->shadow_cutout = box_gpu_shadow_create(gpu, s->module, true);
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
 * Plain casters draw with the plain pipeline; alpha tested ones with the alpha tested pipeline
 * and the material group of their batch, whose albedo texture they sample.
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
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup instances = NULL, material = NULL;
    int32_t i;
    for (i = 0; i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        bool cutout = b->cutout && gpu->shadow_cutout;
        WGPURenderPipeline pipeline = cutout ? gpu->shadow_cutout : gpu->shadow;
        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound = pipeline;
        }

        if (cutout) {
            WGPUBindGroup group = box_gpu_material_group(gpu, b, packet->anisotropy);
            if (group != material) {
                wgpuRenderPassEncoderSetBindGroup(pass, 2, group, 0, NULL);
                material = group;
            }
        }

        box_gpu_cull_draw(gpu, pass, packet, d, 1, false, &instances);
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
    float extent = radius * res / (res - 2.0f);
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
