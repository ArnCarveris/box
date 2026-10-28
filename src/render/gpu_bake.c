/**
 * @file render/gpu_bake.c
 * @brief Probe and reflection captures of the webgpu backend, which bake the irradiance probe
 * volumes and fill the reflection cubemaps.
 *
 * Six faces per probe are shaded and projected onto SH by bake_sh.wgsl, or stored in a cubemap
 * slot. A chunk is one command buffer: a cull that lists the instances of each face, a render
 * pass that replays a render bundle per face into an atlas tile, and the projection.
 *
 * Reflection captures store, prefilter and project their faces in that command buffer.
 */

#include "gpu_bake.h"
#include <float.h>
#include <stddef.h>

#ifdef BOX_RENDER_WGPU

/** Visible slots the probes of a chunk may use at most */
#define BOX_GPU_BAKE_VISIBLE (1 << 22)

/** Texels of the face atlas at most */
#define BOX_GPU_BAKE_TEXELS (1 << 22)

/** Words of the cluster buffer before its light list: entries, offset and count of the cluster */
#define BOX_GPU_BAKE_CLUSTER_HEADER (3)

/** Words the cluster buffer holds before it first grows */
#define BOX_GPU_BAKE_CLUSTER_WORDS (BOX_GPU_BAKE_CLUSTER_HEADER + 64)

/** Near plane of the faces in world units */
#define BOX_GPU_BAKE_NEAR (0.01f)

/** Bytes between the cull uniforms of two batches, the dynamic offset alignment of webgpu */
#define BOX_GPU_BAKE_JOB_STRIDE (256)

/** Words of the indexed indirect draw arguments of one draw */
#define BOX_GPU_BAKE_ARGS (5)

/** Chunks submitted at most before the queue is waited for */
#define BOX_GPU_BAKE_IN_FLIGHT (4)

/** Depth bias of the capture cascade in texels, as the View default */
#define BOX_GPU_BAKE_SHADOW_BIAS (2.0f)

/** Normal offset of the capture cascade in texels at grazing light, as the View default */
#define BOX_GPU_BAKE_SHADOW_OFFSET (2.0f)

/** Width of the tent filter of the capture cascade in texels, as the View default */
#define BOX_GPU_BAKE_SHADOW_FILTER (3)

/** View depth beyond every capture, where the capture cascade would end */
#define BOX_GPU_BAKE_FAR (1e30f)

/** Axis a face looks along and its up axis, the right axis is their cross product */
static const float box_gpu_bake_faces[6][2][3] = {
    {{1, 0, 0}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 1, 0}},
    {{0, 1, 0}, {0, 0, -1}},
    {{0, -1, 0}, {0, 0, 1}},
    {{0, 0, 1}, {0, 1, 0}},
    {{0, 0, -1}, {0, 1, 0}}
};

/** Create a buffer that is written from the CPU.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_bake_buffer(
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

/** Release a texture view when it exists.
 *
 * @param view The view, set to NULL
 */
static void box_gpu_bake_view_release(
    WGPUTextureView *view)
{
    if (*view) {
        wgpuTextureViewRelease(*view);
        *view = NULL;
    }
}

/** Release the color and depth atlas of a kind of captures.
 *
 * @param s The atlas and bundles of the kind
 */
static void box_gpu_bake_atlas_free(
    box_gpu_bake_set_t *s)
{
    box_gpu_bake_view_release(&s->color_view);
    box_gpu_bake_view_release(&s->depth_view);
    box_gpu_bake_view_release(&s->depth_read);
    if (s->color) {
        wgpuTextureRelease(s->color);
        s->color = NULL;
    }

    if (s->depth) {
        wgpuTextureRelease(s->depth);
        s->depth = NULL;
    }

    s->size = 0;
    s->probes = 0;
}

/** Take or drop a reference to everything the render bundles of the faces bind, so no handle
 * they were made with is reused for another object while they exist.
 *
 * @param s The atlas and bundles of a kind
 * @param hold Take the references, drop them otherwise
 */
static void box_gpu_bake_hold(
    box_gpu_bake_set_t *s,
    bool hold)
{
    const box_gpu_bake_key_t *k = &s->key;
    int32_t d, count = ecs_vec_count(&s->key_draws);
    WGPUBuffer buffers[4] = { k->lights, k->clusters, k->slots, k->args };
    for (d = 0; d < 4; d ++) {
        if (buffers[d] && hold) {
            wgpuBufferAddRef(buffers[d]);
        } else if (buffers[d]) {
            wgpuBufferRelease(buffers[d]);
        }
    }

    if (k->shadow && hold) {
        wgpuTextureViewAddRef(k->shadow);
    } else if (k->shadow) {
        wgpuTextureViewRelease(k->shadow);
    }

    if (k->ambient && hold) {
        wgpuBindGroupAddRef(k->ambient);
    } else if (k->ambient) {
        wgpuBindGroupRelease(k->ambient);
    }

    for (d = 0; d < count; d ++) {
        const box_gpu_bake_draw_t *draw = ecs_vec_get_t(&s->key_draws, box_gpu_bake_draw_t, d);
        if (!draw->pipeline) {
            continue;
        }

        if (hold) {
            wgpuRenderPipelineAddRef(draw->pipeline);
            wgpuBindGroupAddRef(draw->material);
            wgpuBindGroupAddRef(draw->instances);
            wgpuBufferAddRef(draw->vertices);
            wgpuBufferAddRef(draw->indices);
        } else {
            wgpuRenderPipelineRelease(draw->pipeline);
            wgpuBindGroupRelease(draw->material);
            wgpuBindGroupRelease(draw->instances);
            wgpuBufferRelease(draw->vertices);
            wgpuBufferRelease(draw->indices);
        }
    }
}

/** Release the render bundles of the faces of a kind and what they hold.
 *
 * @param s The atlas and bundles of the kind
 */
static void box_gpu_bake_bundles_free(
    box_gpu_bake_set_t *s)
{
    int32_t i;
    for (i = 0; i < s->bundle_count; i ++) {
        wgpuRenderBundleRelease(s->bundles[i]);
    }

    if (s->faces_group) {
        wgpuBindGroupRelease(s->faces_group);
    }

    if (s->bundles) {
        box_gpu_bake_hold(s, false);
    }

    ecs_os_free(s->bundles);
    s->bundles = NULL;
    s->bundle_count = 0;
    s->faces_group = NULL;
    ecs_os_memset_t(&s->key, 0, box_gpu_bake_key_t);
    ecs_vec_clear(&s->key_draws);
}

/** Release the capture state.
 *
 * @param b The capture state
 */
static void box_gpu_bake_state_free(
    box_gpu_bake_t *b)
{
    int32_t i;
    for (i = 0; i < BOX_GPU_BAKE_KINDS; i ++) {
        box_gpu_bake_atlas_free(&b->sets[i]);
        box_gpu_bake_bundles_free(&b->sets[i]);
        ecs_vec_fini_t(NULL, &b->sets[i].key_draws, box_gpu_bake_draw_t);
        ecs_vec_fini_t(NULL, &b->captured[i], box_render_bake_result_t);
    }

    ecs_vec_fini_t(NULL, &b->view_data, uint8_t);
    ecs_vec_fini_t(NULL, &b->light_data, uint32_t);
    box_gpu_bake_queries_fini(b);
    if (b->project) {
        wgpuComputePipelineRelease(b->project);
    }

    if (b->cull) {
        wgpuComputePipelineRelease(b->cull);
    }

    if (b->cull_group) {
        wgpuBindGroupRelease(b->cull_group);
    }

    WGPUBuffer buffers[4] = { b->jobs, b->slots, b->args, b->lists };
    for (i = 0; i < 4; i ++) {
        if (buffers[i]) {
            wgpuBufferRelease(buffers[i]);
        }
    }

    ecs_vec_fini_t(NULL, &b->draws, box_gpu_bake_draw_t);
    ecs_vec_fini_t(NULL, &b->job_data, uint8_t);
    ecs_vec_fini_t(NULL, &b->list_data, uint32_t);
    wgpuPipelineLayoutRelease(b->cull_pipeline_layout);
    wgpuBindGroupLayoutRelease(b->cull_layout);
    wgpuBufferRelease(b->uniforms);
    wgpuBufferRelease(b->params);
    wgpuBufferRelease(b->clusters);
    wgpuPipelineLayoutRelease(b->pipeline_layout);
    wgpuBindGroupLayoutRelease(b->layout);
    ecs_os_free(b);
}

/** Create the capture state: layouts, buffers and the timestamp queries.
 *
 * @param gpu The backend
 * @return The state
 */
static box_gpu_bake_t* box_gpu_bake_state_new(
    box_gpu_t *gpu)
{
    box_gpu_bake_t *b = ecs_os_calloc_t(box_gpu_bake_t);
    WGPUBindGroupLayoutEntry entries[6] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_bake_params_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 9,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }
    };
    b->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = entries });
    b->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &b->layout
        });
    WGPUBindGroupLayoutEntry cull[5] = {
        entries[0],
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_bake_job_t)
            }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    b->cull_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 5, .entries = cull });
    WGPUBindGroupLayout cull_groups[2] = { b->cull_layout, gpu->transform_layout };
    b->cull_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = cull_groups
        });
    b->uniforms = box_gpu_bake_buffer(gpu,
        (uint64_t)6 * BOX_GPU_BAKE_CHUNK * BOX_GPU_VIEW_STRIDE, WGPUBufferUsage_Uniform);
    b->params = box_gpu_bake_buffer(gpu, sizeof(box_gpu_bake_params_t), WGPUBufferUsage_Uniform);
    b->clusters = box_gpu_bake_buffer(gpu, BOX_GPU_BAKE_CLUSTER_WORDS * sizeof(uint32_t),
        WGPUBufferUsage_Storage);
    ecs_vec_init_t(NULL, &b->view_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &b->light_data, uint32_t, 0);
    int32_t i;
    for (i = 0; i < BOX_GPU_BAKE_KINDS; i ++) {
        ecs_vec_init_t(NULL, &b->captured[i], box_render_bake_result_t, 0);
        ecs_vec_init_t(NULL, &b->sets[i].key_draws, box_gpu_bake_draw_t, 0);
    }

    ecs_vec_init_t(NULL, &b->draws, box_gpu_bake_draw_t, 0);
    ecs_vec_init_t(NULL, &b->job_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &b->list_data, uint32_t, 0);
    box_gpu_bake_queries_init(gpu, b);

    return b;
}

/** Create a compute pipeline of an entry point of the bake shader, releasing the one it
 * replaces.
 *
 * @param gpu The backend
 * @param pipeline The pipeline, replaced
 * @param layout The pipeline layout
 * @param module The shader module
 * @param entry The entry point
 */
static void box_gpu_bake_compute(
    box_gpu_t *gpu,
    WGPUComputePipeline *pipeline,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    const char *entry)
{
    if (*pipeline) {
        wgpuComputePipelineRelease(*pipeline);
    }

    *pipeline = wgpuDeviceCreateComputePipeline(gpu->device, &(WGPUComputePipelineDescriptor){
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    });
}

/** Get the projection pipeline, created again with the cull pipeline and the reflection store
 * and mip pipelines when the bake shader was recompiled.
 *
 * @param gpu The backend
 * @param b The capture state
 * @return The pipeline, NULL while the shader has no module
 */
static WGPUComputePipeline box_gpu_bake_pipeline(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_BAKE);
    if (!s || !s->module || (b->project && b->revision == s->revision)) {
        goto done;
    }

    box_gpu_bake_compute(gpu, &b->project, b->pipeline_layout, s->module, "project");
    box_gpu_bake_compute(gpu, &b->cull, b->cull_pipeline_layout, s->module, "cull");
    box_gpu_reflect_pipelines(gpu, s->module);
    b->revision = s->revision;
done:
    return s && s->module && b->cull ? b->project : NULL;
}

/** Make a buffer of the cull hold a number of bytes, growing it by doubling.
 *
 * @param gpu The backend
 * @param buffer The buffer
 * @param size Bytes of the buffer, updated
 * @param needed Bytes it must hold
 * @param usage Usage of the buffer
 * @return Whether the buffer was created
 */
static bool box_gpu_bake_reserve(
    box_gpu_t *gpu,
    WGPUBuffer *buffer,
    uint64_t *size,
    uint64_t needed,
    WGPUBufferUsage usage)
{
    uint64_t capacity = *size ? *size : 256;
    bool result = false;
    if (*buffer && needed <= *size) {
        goto done;
    }

    while (capacity < needed) {
        capacity *= 2;
    }

    if (*buffer) {
        wgpuBufferRelease(*buffer);
    }

    *buffer = box_gpu_bake_buffer(gpu, capacity, usage);
    *size = capacity;
    result = true;
done:
    return result;
}

/** List the batches the faces of the captures of a scene draw, write the cull uniforms of each
 * and make the slots and arguments hold every face of a chunk.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param scene Scene of the captures
 * @param chunk Probes per chunk
 */
static void box_gpu_bake_draws(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    int32_t chunk)
{
    int32_t i, d, count = ecs_vec_count(&packet->batches);
    bool grown = false;
    ecs_vec_clear(&b->draws);
    ecs_vec_clear(&b->list_data);
    b->stride = 0;
    for (i = 0; i < count; i ++) {
        const box_render_batch_t *rb = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        if (box_gpu_cull_capture_draws(gpu, rb, scene, -1)) {
            box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, rb->mesh);
            *ecs_vec_append_t(NULL, &b->draws, box_gpu_bake_draw_t) = (box_gpu_bake_draw_t){
                .batch = i,
                .region = b->stride,
                .count = rb->instance_count,
                .pipeline = box_gpu_forward_pipeline(gpu, BOX_GPU_HDR_FORMAT, rb, false),
                .material = box_gpu_material_group(gpu, rb, packet->anisotropy),
                .instances = box_gpu_instance_group(gpu, rb->instances, rb->mesh, m, true),
                .page = box_gpu_instance_group(gpu, rb->instances, 0, NULL, false),
                .vertices = m->vertices,
                .indices = m->indices
            };
            b->stride += rb->instance_count;
        }
    }

    int32_t draws = ecs_vec_count(&b->draws);
    ecs_vec_set_count_t(NULL, &b->job_data, uint8_t, draws * BOX_GPU_BAKE_JOB_STRIDE);
    for (d = 0; d < draws; d ++) {
        const box_gpu_bake_draw_t *draw = ecs_vec_get_t(&b->draws, box_gpu_bake_draw_t, d);
        const box_render_batch_t *rb = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, draw->batch);
        box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, rb->mesh);
        box_gpu_bake_job_t *job = (box_gpu_bake_job_t*)ecs_vec_get_t(
            &b->job_data, uint8_t, d * BOX_GPU_BAKE_JOB_STRIDE);
        ecs_os_memset_t(job, 0, box_gpu_bake_job_t);
        job->first = rb->first_instance;
        job->count = draw->count;
        job->draw = (uint32_t)d;
        job->index_count = m->index_count;
        job->region = draw->region;
        job->draws = (uint32_t)draws;
        job->stride = b->stride;
        glm_vec3_copy(m->center, job->center);
        glm_vec3_copy(m->extent, job->extent);
        if (rb->listed) {
            job->first = (uint32_t)ecs_vec_count(&b->list_data);
            job->list = 1;
            ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &b->list_data, uint32_t, (int32_t)draw->count),
                box_render_packet_data(packet, rb->slots), uint32_t, (int32_t)draw->count);
        }
    }

    uint64_t faces = (uint64_t)chunk * 6;
    int32_t lists = ecs_vec_count(&b->list_data);
    grown |= box_gpu_bake_reserve(gpu, &b->jobs, &b->jobs_size,
        (uint64_t)glm_max(draws, 1) * BOX_GPU_BAKE_JOB_STRIDE, WGPUBufferUsage_Uniform);
    grown |= box_gpu_bake_reserve(gpu, &b->slots, &b->slots_size,
        faces * glm_max(b->stride, 1) * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex);
    grown |= box_gpu_bake_reserve(gpu, &b->args, &b->args_size,
        faces * (uint64_t)glm_max(draws, 1) * BOX_GPU_BAKE_ARGS * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect);
    grown |= box_gpu_bake_reserve(gpu, &b->lists, &b->lists_size,
        (uint64_t)glm_max(lists, 1) * sizeof(uint32_t), WGPUBufferUsage_Storage);
    if (draws) {
        wgpuQueueWriteBuffer(gpu->queue, b->jobs, 0, ecs_vec_first(&b->job_data),
            (size_t)draws * BOX_GPU_BAKE_JOB_STRIDE);
    }

    if (lists) {
        wgpuQueueWriteBuffer(gpu->queue, b->lists, 0, ecs_vec_first(&b->list_data),
            (size_t)lists * sizeof(uint32_t));
    }

    if (grown && b->cull_group) {
        wgpuBindGroupRelease(b->cull_group);
        b->cull_group = NULL;
    }

    if (!b->cull_group) {
        WGPUBindGroupEntry entries[5] = {
            { .binding = 0, .buffer = b->params, .size = sizeof(box_gpu_bake_params_t) },
            { .binding = 5, .buffer = b->jobs, .size = sizeof(box_gpu_bake_job_t) },
            { .binding = 6, .buffer = b->slots, .size = b->slots_size },
            { .binding = 7, .buffer = b->args, .size = b->args_size },
            { .binding = 8, .buffer = b->lists, .size = b->lists_size }
        };
        b->cull_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = b->cull_layout,
            .entryCount = 5,
            .entries = entries
        });
    }
}

/** Record the cull of the faces of a chunk: a dispatch per batch, a workgroup per probe.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param count Probes of the chunk
 * @param writes Timestamp the pass writes, NULL for none
 * @return Whether the pass was recorded
 */
static bool box_gpu_bake_cull(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    int32_t d, draws = ecs_vec_count(&b->draws);
    if (!draws) {
        goto done;
    }

    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,
        &(WGPUComputePassDescriptor){ .timestampWrites = writes });
    WGPUBindGroup page = NULL;
    wgpuComputePassEncoderSetPipeline(pass, b->cull);
    for (d = 0; d < draws; d ++) {
        const box_gpu_bake_draw_t *draw = ecs_vec_get_t(&b->draws, box_gpu_bake_draw_t, d);
        uint32_t offset = (uint32_t)(d * BOX_GPU_BAKE_JOB_STRIDE);
        wgpuComputePassEncoderSetBindGroup(pass, 0, b->cull_group, 1, &offset);
        if (draw->page != page) {
            wgpuComputePassEncoderSetBindGroup(pass, 1, draw->page, 0, NULL);
            box_gpu_count(gpu, BOX_GPU_STAT_INSTANCE_BIND);
            page = draw->page;
        }

        wgpuComputePassEncoderDispatchWorkgroups(pass, (uint32_t)count, 1, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
done:
    return draws > 0;
}

/** Make the atlas of a kind hold the faces of a chunk at a face size: six tiles across, one row
 * per probe.
 *
 * @param gpu The backend
 * @param b The atlas and bundles of the kind
 * @param size Face edge in texels
 * @param probes Probes of a chunk
 */
static void box_gpu_bake_atlas(
    box_gpu_t *gpu,
    box_gpu_bake_set_t *b,
    int32_t size,
    int32_t probes)
{
    WGPUExtent3D extent = { (uint32_t)(size * 6), (uint32_t)(size * probes), 1 };
    if (b->color && b->size == size && b->probes >= probes) {
        goto done;
    }

    box_gpu_bake_atlas_free(b);
    b->color = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = extent,
        .format = BOX_GPU_HDR_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    b->color_view = wgpuTextureCreateView(b->color, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_HDR_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });
    b->depth = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = extent,
        .format = BOX_GPU_DEPTH_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    b->depth_view = wgpuTextureCreateView(b->depth, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_DEPTH_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });
    b->depth_read = wgpuTextureCreateView(b->depth, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_DEPTH_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_DepthOnly
    });
    b->size = size;
    b->probes = probes;
done:
    return;
}

/** Finish an encoder, submit it and release it.
 *
 * @param gpu The backend
 * @param encoder The encoder
 */
void box_gpu_bake_submit(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder)
{
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

/** Write the uniforms of a face of a probe.
 *
 * The sun is shadowed by the capture cascade, which ends beyond every capture. The point and
 * spot lights are one cluster that lists them all, see box_gpu_bake_lights.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param size Face edge in texels
 * @param rv View with the lights of the faces, its view matrix is set here
 * @param probe The probe
 * @param slot Slot of the face
 * @param cascade The capture cascade, NULL without a sun
 */
static void box_gpu_bake_face(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t size,
    box_render_view_t *rv,
    const box_render_probe_capture_t *probe,
    int32_t slot,
    const box_render_cascade_t *cascade)
{
    const float (*face)[3] = box_gpu_bake_faces[slot % 6];
    box_gpu_view_uniforms_t *u = (box_gpu_view_uniforms_t*)ecs_vec_get_t(
        &b->view_data, uint8_t, slot * BOX_GPU_VIEW_STRIDE);
    vec3 eye, center;
    mat4 proj;
    float edge = (float)size;
    glm_vec3_copy(ECS_CONST_CAST(float*, probe->position), eye);
    glm_vec3_add(eye, ECS_CONST_CAST(float*, face[0]), center);
    glm_lookat(eye, center, ECS_CONST_CAST(float*, face[1]), rv->view_matrix);
    box_camera_projection(&rv->camera_data, edge, edge, proj);
    ecs_os_memset_t(u, 0, box_gpu_view_uniforms_t);
    glm_mat4_mul(proj, rv->view_matrix, u->view_proj);
    glm_vec3_copy(eye, u->camera);
    u->camera[3] = 1;
    u->viewport[0] = edge;
    u->viewport[1] = edge;
    u->viewport[2] = 1.0f / edge;
    u->viewport[3] = 1.0f / edge;
    box_gpu_light_uniforms(gpu, rv, proj, u);
    u->grid[0] = 1;
    u->grid[1] = 1;
    u->grid[2] = 1;
    if (cascade) {
        box_gpu_shadow_cascade_uniforms(u, 0, cascade, BOX_GPU_BAKE_SHADOW_BIAS,
            BOX_GPU_BAKE_SHADOW_OFFSET, BOX_GPU_CAPTURE_SHADOW_RESOLUTION);
        u->shadow_params[0] = BOX_GPU_BAKE_FAR;
        u->shadow_params[1] = BOX_GPU_BAKE_FAR;
        box_gpu_shadow_filter_uniforms(u, 1, BOX_GPU_BAKE_SHADOW_FILTER,
            BOX_GPU_BAKE_SHADOW_OFFSET, 0.0f, BOX_GPU_CAPTURE_SHADOW_RESOLUTION);
    }
}

/** Write the cluster buffer of the faces of a chunk: one cluster that lists every point and spot
 * light the faces shade with, in the layout of the cluster pass (etc/shaders/cluster.wgsl).
 *
 * A face is a few texels across, so a cluster grid would cull next to nothing for the cost of
 * a cluster pass per face; lights beyond their range or cone add nothing either way.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param u Uniforms of a face of the chunk, which every face shares the lights of
 */
static void box_gpu_bake_lights(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_gpu_view_uniforms_t *u)
{
    uint32_t i, first = u->lights[0], count = u->lights[1] > first ? u->lights[1] - first : 0;
    uint64_t bytes = (uint64_t)(BOX_GPU_BAKE_CLUSTER_HEADER + count) * sizeof(uint32_t);
    ecs_vec_set_count_t(NULL, &b->light_data, uint32_t,
        BOX_GPU_BAKE_CLUSTER_HEADER + (int32_t)count);
    uint32_t *words = ecs_vec_first(&b->light_data);
    words[0] = count;
    words[1] = 0;
    words[2] = count;
    for (i = 0; i < count; i ++) {
        words[BOX_GPU_BAKE_CLUSTER_HEADER + i] = first + i;
    }

    if (bytes > wgpuBufferGetSize(b->clusters)) {
        wgpuBufferRelease(b->clusters);
        b->clusters = box_gpu_bake_buffer(gpu, bytes * 2, WGPUBufferUsage_Storage);
    }

    wgpuQueueWriteBuffer(gpu->queue, b->clusters, 0, words, (size_t)bytes);
}

/** Check whether two lists of the batches the faces draw bind and draw the same.
 *
 * @param a The first list
 * @param b The second list
 * @return Whether every batch has the same slots, pipeline, groups and buffers
 */
static bool box_gpu_bake_draws_equal(
    const ecs_vec_t *a,
    const ecs_vec_t *b)
{
    int32_t d, count = ecs_vec_count(a);
    bool result = count == ecs_vec_count(b);
    for (d = 0; result && d < count; d ++) {
        const box_gpu_bake_draw_t *x = ecs_vec_get_t(a, box_gpu_bake_draw_t, d);
        const box_gpu_bake_draw_t *y = ecs_vec_get_t(b, box_gpu_bake_draw_t, d);
        result = x->region == y->region && x->count == y->count &&
            x->pipeline == y->pipeline && x->material == y->material &&
            x->instances == y->instances && x->vertices == y->vertices &&
            x->indices == y->indices;
    }

    return result;
}

/** Record the render bundle of one face of a chunk: its view uniforms, the ambient term and
 * the draws of the instances the cull lists for it.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param s The atlas and bundles of the kind of the captures
 * @param face Face of the chunk, six per probe
 * @return The bundle
 */
static WGPURenderBundle box_gpu_bake_bundle(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_gpu_bake_set_t *s,
    int32_t face)
{
    WGPUTextureFormat format = BOX_GPU_HDR_FORMAT;
    WGPURenderBundleEncoder e = wgpuDeviceCreateRenderBundleEncoder(gpu->device,
        &(WGPURenderBundleEncoderDescriptor){
            .colorFormatCount = 1,
            .colorFormats = &format,
            .depthStencilFormat = BOX_GPU_DEPTH_FORMAT,
            .sampleCount = 1
        });
    WGPURenderPipeline pipeline = NULL;
    WGPUBindGroup material = NULL, instances = NULL;
    uint32_t offset = (uint32_t)(face * BOX_GPU_VIEW_STRIDE);
    int32_t d, draws = ecs_vec_count(&b->draws);
    wgpuRenderBundleEncoderSetBindGroup(e, 0, s->faces_group, 1, &offset);
    wgpuRenderBundleEncoderSetBindGroup(e, 3, s->key.ambient, 0, NULL);
    for (d = 0; d < draws; d ++) {
        const box_gpu_bake_draw_t *draw = ecs_vec_get_t(&b->draws, box_gpu_bake_draw_t, d);
        uint64_t first = (uint64_t)face * b->stride + draw->region;
        uint64_t args = (uint64_t)face * (uint64_t)draws + (uint64_t)d;
        if (!draw->pipeline) {
            continue;
        }

        if (draw->pipeline != pipeline) {
            wgpuRenderBundleEncoderSetPipeline(e, draw->pipeline);
            pipeline = draw->pipeline;
        }

        if (draw->material != material) {
            wgpuRenderBundleEncoderSetBindGroup(e, 1, draw->material, 0, NULL);
            material = draw->material;
        }

        if (draw->instances != instances) {
            wgpuRenderBundleEncoderSetBindGroup(e, 2, draw->instances, 0, NULL);
            box_gpu_count(gpu, BOX_GPU_STAT_INSTANCE_BIND);
            instances = draw->instances;
        }

        wgpuRenderBundleEncoderSetVertexBuffer(e, 0, draw->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderBundleEncoderSetVertexBuffer(e, 1, b->slots, first * sizeof(uint32_t),
            (uint64_t)draw->count * sizeof(uint32_t));
        wgpuRenderBundleEncoderSetIndexBuffer(
            e, draw->indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderBundleEncoderDrawIndexedIndirect(e, b->args,
            args * BOX_GPU_BAKE_ARGS * sizeof(uint32_t));
    }

    WGPURenderBundle result = wgpuRenderBundleEncoderFinish(e, NULL);
    wgpuRenderBundleEncoderRelease(e);

    return result;
}

/** Make the render bundles of the faces of a chunk, again only when what they bind changed:
 * the batches the faces draw, the lights, the shadow map, the ambient term or the buffers of
 * the cull. Static content records them once for every chunk of every frame.
 *
 * Each kind of captures keeps its own, so bakes and reflections in turn reuse theirs.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param s The atlas and bundles of the kind of the captures
 * @param rv View with the lights of the faces
 * @param shadow Array view of the shadow map the faces sample
 * @param ambient Volume group the faces shade their ambient term with
 * @param faces Faces of the chunk
 */
static void box_gpu_bake_bundles(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    box_gpu_bake_set_t *s,
    const box_render_view_t *rv,
    WGPUTextureView shadow,
    WGPUBindGroup ambient,
    int32_t faces)
{
    box_gpu_bake_key_t key;
    int32_t i, count = glm_max(faces, s->probes * 6);
    ecs_os_memset_t(&key, 0, box_gpu_bake_key_t);
    key.lights = box_gpu_light_binding(gpu, rv, &key.lights_size);
    key.clusters = b->clusters;
    key.shadow = shadow;
    key.ambient = ambient;
    key.slots = b->slots;
    key.args = b->args;
    key.stride = b->stride;
    if (s->bundles && s->bundle_count >= faces &&
        !memcmp(&key, &s->key, sizeof(box_gpu_bake_key_t)) &&
        box_gpu_bake_draws_equal(&b->draws, &s->key_draws))
    {
        goto done;
    }

    box_gpu_bake_bundles_free(s);
    s->key = key;
    ecs_vec_set_count_t(NULL, &s->key_draws, box_gpu_bake_draw_t, ecs_vec_count(&b->draws));
    if (ecs_vec_count(&b->draws)) {
        ecs_os_memcpy_n(ecs_vec_first(&s->key_draws), ecs_vec_first(&b->draws),
            box_gpu_bake_draw_t, ecs_vec_count(&b->draws));
    }

    box_gpu_bake_hold(s, true);
    s->faces_group = box_gpu_light_group_with(gpu, NULL, rv, b->uniforms, b->clusters, shadow);
    s->bundles = ecs_os_calloc_n(WGPURenderBundle, count);
    s->bundle_count = count;
    for (i = 0; i < count; i ++) {
        s->bundles[i] = box_gpu_bake_bundle(gpu, b, s, i);
    }

done:
    return;
}

/** Record the render pass that draws the faces of a chunk into their tiles of the atlas.
 *
 * Face f of probe p is the tile at column f and row p; each face replays its render bundle,
 * which draws the instances the cull listed for it with its own view uniforms.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param s The atlas and bundles of the kind of the captures
 * @param encoder The encoder of the chunk
 * @param rv View with the lights of the faces
 * @param faces Faces of the chunk
 * @param shadow Array view of the shadow map the faces sample
 * @param ambient Volume group the faces shade their ambient term with
 */
static void box_gpu_bake_faces_render(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    box_gpu_bake_set_t *s,
    WGPUCommandEncoder encoder,
    const box_render_view_t *rv,
    int32_t faces,
    WGPUTextureView shadow,
    WGPUBindGroup ambient)
{
    uint32_t size = (uint32_t)s->size;
    int32_t i, d, draws = 0;
    WGPURenderPassColorAttachment color = {
        .view = s->color_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0, 0, 0, 0 }
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = s->depth_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    box_gpu_bake_bundles(gpu, b, s, rv, shadow, ambient, faces);
    for (d = 0; d < ecs_vec_count(&b->draws); d ++) {
        draws += ecs_vec_get_t(&b->draws, box_gpu_bake_draw_t, d)->pipeline ? 1 : 0;
    }

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,
        &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color,
            .depthStencilAttachment = &depth
        });
    for (i = 0; i < faces; i ++) {
        uint32_t x = (uint32_t)(i % 6) * size, y = (uint32_t)(i / 6) * size;
        wgpuRenderPassEncoderSetViewport(pass, (float)x, (float)y, (float)size, (float)size,
            0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, x, y, size, size);
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &s->bundles[i]);
        for (d = 0; d < draws; d ++) {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Write the uniforms the cull and the projection or store of a chunk share.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param pass The captures
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 */
static void box_gpu_bake_params(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_gpu_bake_pass_t *pass,
    const box_render_probe_capture_t *probes,
    int32_t count)
{
    const box_render_bake_batch_t *batch = pass->bake;
    box_gpu_buffer_t *baked = batch && batch->baked
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, batch->baked)
        : NULL;
    uint32_t capacity = baked ? baked->size / (uint32_t)sizeof(box_render_light_t) : 0;
    box_gpu_bake_params_t params = {
        .size = { (uint32_t)pass->size, (uint32_t)count, pass->environment ? 1u : 0u, 0 },
        .near_ = {
            BOX_GPU_BAKE_NEAR, (float)pass->size * 0.5f, BOX_RENDER_CULL_MIN_DIMENSION,
            pass->spacing
        }
    };
    int32_t i;
    if (batch) {
        params.lights[0] = glm_min(batch->baked_count, capacity);
        params.lights[1] = glm_min(batch->baked_directional, params.lights[0]);
    }

    glm_vec4_copy(ECS_CONST_CAST(float*, pass->sky), params.sky);
    glm_vec4_copy(ECS_CONST_CAST(float*, pass->ground), params.ground);
    for (i = 0; i < count; i ++) {
        params.probes[i] = probes[i].index;
        glm_vec3_copy(ECS_CONST_CAST(float*, probes[i].position), params.positions[i]);
    }

    wgpuQueueWriteBuffer(gpu->queue, b->params, 0, &params, sizeof(params));
}

/** Where the projection of a chunk writes: the SH and visibility of probes indexed by
 * params.probes, and the Baked lights it adds */
typedef struct box_gpu_bake_target_t {
    WGPUBuffer sh;                  /**< BOX_GPU_PROBE_SIZE per probe */
    WGPUBuffer visibility;          /**< BOX_GPU_VISIBILITY_SIZE per probe */
    uint32_t count;                 /**< Probes both buffers hold */
    WGPUBuffer lights;              /**< Baked lights, gpu->no_lights for none */
    uint64_t lights_size;           /**< Bytes of the light buffer */
} box_gpu_bake_target_t;

/** Record the projection of the faces of a chunk in the atlas of a kind of captures onto the
 * SH of its probes.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param pipeline The projection pipeline
 * @param kind box_gpu_bake_kind_t of the atlas
 * @param target Where the projection writes
 * @param count Probes of the chunk
 * @param writes Timestamp the pass writes, NULL for none
 */
static void box_gpu_bake_project_record(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    WGPUComputePipeline pipeline,
    int32_t kind,
    const box_gpu_bake_target_t *target,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    const box_gpu_bake_set_t *s = &b->sets[kind];
    WGPUBindGroupEntry entries[6] = {
        { .binding = 0, .buffer = b->params, .size = sizeof(box_gpu_bake_params_t) },
        { .binding = 1, .textureView = s->color_view },
        {
            .binding = 2,
            .buffer = target->sh,
            .size = (uint64_t)target->count * BOX_GPU_PROBE_SIZE
        },
        { .binding = 3, .textureView = s->depth_read },
        { .binding = 4, .buffer = target->lights, .size = target->lights_size },
        {
            .binding = 9,
            .buffer = target->visibility,
            .size = (uint64_t)target->count * BOX_GPU_VISIBILITY_SIZE
        }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = b->layout,
        .entryCount = 6,
        .entries = entries
    });
    WGPUComputePassEncoder cpass = wgpuCommandEncoderBeginComputePass(encoder,
        &(WGPUComputePassDescriptor){ .timestampWrites = writes });
    wgpuComputePassEncoderSetPipeline(cpass, pipeline);
    wgpuComputePassEncoderSetBindGroup(cpass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(cpass, (uint32_t)count, 1, 1);
    wgpuComputePassEncoderEnd(cpass);
    wgpuComputePassEncoderRelease(cpass);
    wgpuBindGroupRelease(group);
}

/** Record the projection of the faces of a chunk onto the SH of its probes.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param pipeline The projection pipeline
 * @param pass The captures of a bake batch
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 * @param writes Timestamp the pass writes, NULL for none
 */
static void box_gpu_bake_project(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    WGPUComputePipeline pipeline,
    const box_gpu_bake_pass_t *pass,
    const box_render_probe_capture_t *probes,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    const box_render_bake_batch_t *batch = pass->bake;
    box_gpu_volume_t *volume = pass->volume;
    box_gpu_buffer_t *baked = batch->baked
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, batch->baked)
        : NULL;
    int32_t i;
    box_gpu_bake_project_record(gpu, b, encoder, pipeline, BOX_GPU_BAKE_PROBES,
        &(box_gpu_bake_target_t){
            .sh = volume->probes[batch->target],
            .visibility = volume->visibility,
            .count = volume->count,
            .lights = baked ? baked->buffer : gpu->no_lights,
            .lights_size = baked ? baked->size : sizeof(box_render_light_t)
        }, count, writes);
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.probes += count;
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &b->captured[BOX_GPU_BAKE_PROBES], box_render_bake_result_t) =
            (box_render_bake_result_t){
                .volume = batch->volume,
                .generation = batch->generation,
                .pass = batch->pass,
                .probe = probes[i].index
            };
    }

    ecs_os_mutex_unlock(gpu->lock);
}

/** Record the store of the faces of a chunk of reflection captures into their cubemap slots,
 * the prefilter of their mips and the projection onto their SH, and report the probes it
 * stored.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param pipeline The projection pipeline
 * @param pass The captures of a reflection batch
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 * @param writes Timestamp the last pass writes, NULL for none
 * @return Whether the store was recorded
 */
static bool box_gpu_bake_store(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    WGPUComputePipeline pipeline,
    const box_gpu_bake_pass_t *pass,
    const box_render_probe_capture_t *probes,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    const box_render_reflect_batch_t *batch = pass->reflect;
    const box_gpu_reflect_t *cube = pass->cube;
    uint32_t ticket = batch->ticket + (uint32_t)(probes - pass->probes);
    int32_t i;
    bool result = box_gpu_reflect_store(gpu, encoder, cube, b->params,
        sizeof(box_gpu_bake_params_t), b->sets[BOX_GPU_BAKE_REFLECT].color_view,
        pass->environment ? pass->environment->cube : NULL, count, NULL);
    if (!result) {
        goto done;
    }

    box_gpu_bake_project_record(gpu, b, encoder, pipeline, BOX_GPU_BAKE_REFLECT,
        &(box_gpu_bake_target_t){
            .sh = cube->sh,
            .visibility = cube->visibility,
            .count = cube->cubes,
            .lights = gpu->no_lights,
            .lights_size = sizeof(box_render_light_t)
        }, count, writes);

    ecs_os_mutex_lock(gpu->lock);
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &b->captured[BOX_GPU_BAKE_REFLECT], box_render_bake_result_t) =
            (box_render_bake_result_t){
                .volume = batch->texture,
                .generation = ticket + (uint32_t)i,
                .probe = probes[i].index
            };
    }

    ecs_os_mutex_unlock(gpu->lock);
done:
    return result;
}

/** Get the probes of a chunk: as many as the atlas, slots and visible buffer allow.
 *
 * @param packet The packet
 * @param scene Scene of the captures
 * @param size Face edge of the captures in texels
 * @return Probes per chunk, at least 1
 */
static int32_t box_gpu_bake_chunk(
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    int32_t size)
{
    int64_t instances = 0, texels = (int64_t)size * size * 6;
    int32_t i, result = BOX_GPU_BAKE_CHUNK;
    for (i = 0; i < ecs_vec_count(&packet->batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        if (b->scene == scene && b->bake && b->view == -1) {
            instances += b->instance_count;
        }
    }

    if (instances * 6 * result > BOX_GPU_BAKE_VISIBLE) {
        result = (int32_t)(BOX_GPU_BAKE_VISIBLE / (instances * 6));
    }

    if (texels * result > BOX_GPU_BAKE_TEXELS) {
        result = (int32_t)(BOX_GPU_BAKE_TEXELS / texels);
    }

    return result < 1 ? 1 : result;
}

/** Check whether the probes of a chunk fit what the captures write into.
 *
 * @param pass The captures
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 * @return Whether every probe has an index in the volume or a slot in the cubemap array
 */
static bool box_gpu_bake_fits(
    const box_gpu_bake_pass_t *pass,
    const box_render_probe_capture_t *probes,
    int32_t count)
{
    uint32_t limit = pass->volume ? pass->volume->count : pass->cube ? pass->cube->cubes : 0;
    bool result = pass->ambient != NULL &&
        (!pass->cube || (int32_t)pass->cube->resolution == pass->size);
    int32_t i;
    for (i = 0; result && i < count; i ++) {
        result = probes[i].index < limit;
    }

    return result;
}

/** Capture one chunk of probes of a batch in one command buffer.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param pass The captures
 * @param first First probe of the chunk in the batch
 * @param count Probes of the chunk
 * @param shadow Receives the shadow map the chunk sampled, rendered by the first chunk
 * @param timer The timestamps of the packet
 * @param last Whether the chunk is the last of the packet
 */
static void box_gpu_bake_chunk_capture(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    box_gpu_bake_pass_t *pass,
    int32_t first,
    int32_t count,
    WGPUTextureView *shadow,
    box_gpu_bake_timer_t *timer,
    bool last)
{
    WGPUComputePipeline pipeline = box_gpu_bake_pipeline(gpu, b);
    const box_render_probe_capture_t *probes = &pass->probes[first];
    const box_render_cascade_t *cascade = pass->sun ? &pass->cascade : NULL;
    int32_t i, faces = count * 6;
    bool caster = cascade && !*shadow;
    if (!pipeline || !box_gpu_bake_fits(pass, probes, count)) {
        goto done;
    }

    ecs_vec_set_count_t(NULL, &b->view_data, uint8_t, faces * BOX_GPU_VIEW_STRIDE);
    for (i = 0; i < faces; i ++) {
        box_gpu_bake_face(gpu, b, pass->size, &pass->rv, &probes[i / 6], i, cascade);
    }

    box_gpu_bake_lights(gpu, b, ecs_vec_first(&b->view_data));
    box_gpu_bake_params(gpu, b, pass, probes, count);
    if (caster) {
        box_gpu_cull_reset(gpu);
        box_gpu_cull_capture_add(gpu, packet, pass->scene, 0, 0,
            ECS_CONST_CAST(vec4*, cascade->matrix), 0);
        box_gpu_cull_upload(gpu);
    }

    wgpuQueueWriteBuffer(gpu->queue, b->uniforms, 0, ecs_vec_first(&b->view_data),
        (size_t)faces * BOX_GPU_VIEW_STRIDE);
    uint32_t query = (uint32_t)timer->readback * 2;
    WGPUPassTimestampWrites begin = {
        .querySet = b->queries,
        .beginningOfPassWriteIndex = query,
        .endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED
    };
    WGPUPassTimestampWrites end = {
        .querySet = b->queries,
        .beginningOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED,
        .endOfPassWriteIndex = query + 1
    };
    bool timed = timer->readback != -1;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    if (pass->environment) {
        wgpuCommandEncoderCopyBufferToBuffer(encoder, pass->environment->sh, 0, b->params,
            offsetof(box_gpu_bake_params_t, environment), BOX_GPU_PROBE_SIZE);
    }

    if (box_gpu_bake_cull(gpu, b, encoder, count,
        timed && !timer->begun ? &begin : NULL))
    {
        timer->begun |= timed;
    }

    if (caster) {
        box_gpu_cull_record_all(gpu, encoder, NULL);
        *shadow = box_gpu_shadow_capture(gpu, encoder, packet, 0, cascade);
    }

    timed = timed && timer->begun && last;
    box_gpu_bake_faces_render(gpu, b, &b->sets[pass->kind], encoder, &pass->rv, faces,
        *shadow ? *shadow : gpu->shadow_empty_view, pass->ambient);
    if (pass->bake) {
        box_gpu_bake_project(gpu, b, encoder, pipeline, pass, probes, count,
            timed ? &end : NULL);
    } else {
        timed = box_gpu_bake_store(gpu, b, encoder, pipeline, pass, probes, count,
            timed ? &end : NULL) && timed;
    }

    timer->ended |= timed;
    box_gpu_bake_submit(gpu, encoder);
#ifndef __EMSCRIPTEN__
    if (++ b->in_flight >= BOX_GPU_BAKE_IN_FLIGHT) {
        wgpuDevicePoll(gpu->device, true, NULL);
        b->in_flight = 0;
    }
#endif
done:
    return;
}

/** Get the environment the texels of a batch nothing covers see.
 *
 * @param gpu The backend
 * @param handle Handle of the environment of the batch, 0 for none
 * @return The environment once it was processed, NULL for the hemisphere
 */
static const box_gpu_environment_t* box_gpu_bake_environment(
    box_gpu_t *gpu,
    uint32_t handle)
{
    const box_gpu_environment_t *result = handle ? box_gpu_environment_get(gpu, handle) : NULL;

    return result && result->processed ? result : NULL;
}

/** Fit the capture cascade of the sun of the faces of a batch to static bounds.
 *
 * @param pass The captures, with the lights of their faces
 * @param min Minimum corner of the static bounds of the scene
 * @param max Maximum corner of the static bounds of the scene
 */
static void box_gpu_bake_sun(
    box_gpu_bake_pass_t *pass,
    const float *min,
    const float *max)
{
    pass->sun = pass->rv.lights.directional && glm_vec3_norm(pass->rv.lights.sun) > 1e-6f;
    if (pass->sun) {
        vec3 direction;
        glm_vec3_normalize_to(pass->rv.lights.sun, direction);
        box_gpu_shadow_capture_fit(min, max, direction, &pass->cascade);
    }
}

/** Describe the captures of a bake batch: lit by its lights over a black ambient term or the
 * probe buffer it reads, projected onto the SH of its volume.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param batch The bake batch
 * @param out Receives the captures
 */
static void box_gpu_bake_pass_probes(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch,
    box_gpu_bake_pass_t *out)
{
    ecs_os_memset_t(out, 0, box_gpu_bake_pass_t);
    out->kind = BOX_GPU_BAKE_PROBES;
    out->bake = batch;
    out->scene = batch->scene;
    out->size = batch->size;
    out->volume = ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, batch->volume);
    out->spacing = out->volume ? out->volume->spacing : 1.0f;
    out->probes = ecs_vec_get_t(&packet->probes, box_render_probe_capture_t, batch->first);
    out->count = batch->count;
    if (out->volume) {
        out->ambient = batch->source == BOX_RENDER_BAKE_BLACK
            ? gpu->no_volume->group[0]
            : out->volume->group[batch->source];
    }

    glm_vec4_copy(ECS_CONST_CAST(float*, batch->lights.ambient_sky), out->sky);
    glm_vec4_copy(ECS_CONST_CAST(float*, batch->lights.ambient_ground), out->ground);
    out->environment = box_gpu_bake_environment(gpu, batch->environment);
    out->rv.scene = batch->scene;
    out->rv.lights = batch->lights;
    out->rv.lights.enabled = true;
    glm_vec4_zero(out->rv.lights.ambient_sky);
    glm_vec4_zero(out->rv.lights.ambient_ground);
    out->rv.camera_data = (Camera){ .fov = 90, .near_ = BOX_GPU_BAKE_NEAR };
    box_gpu_bake_sun(out, batch->bounds_min, batch->bounds_max);
}

/** Describe the captures of a reflection batch: lit as the views of their scene are, with the
 * ambient term of the probe buffer the views show or the hemisphere, stored in a cubemap array.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param batch The reflection batch
 * @param out Receives the captures
 */
static void box_gpu_bake_pass_reflect(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_reflect_batch_t *batch,
    box_gpu_bake_pass_t *out)
{
    box_gpu_volume_t *volume = batch->volume
        ? ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, batch->volume)
        : NULL;
    ecs_os_memset_t(out, 0, box_gpu_bake_pass_t);
    out->kind = BOX_GPU_BAKE_REFLECT;
    out->reflect = batch;
    out->scene = batch->scene;
    out->size = batch->size;
    out->cube = ecs_map_get_deref(&gpu->reflections, box_gpu_reflect_t, batch->texture);
    out->spacing = 1.0f;
    out->probes = ecs_vec_get_t(&packet->probes, box_render_probe_capture_t, batch->first);
    out->count = batch->count;
    out->ambient = volume && batch->volume_buffer < 2
        ? volume->group[batch->volume_buffer]
        : gpu->no_volume->group[0];
    glm_vec4_copy(ECS_CONST_CAST(float*, batch->lights.ambient_sky), out->sky);
    glm_vec4_copy(ECS_CONST_CAST(float*, batch->lights.ambient_ground), out->ground);
    out->environment = box_gpu_bake_environment(gpu, batch->environment);
    out->rv.scene = batch->scene;
    out->rv.lights = batch->lights;
    out->rv.lights.enabled = true;
    out->rv.camera_data = (Camera){ .fov = 90, .near_ = BOX_GPU_BAKE_NEAR };
    box_gpu_bake_sun(out, batch->bounds_min, batch->bounds_max);
}

/** Capture the probes of a batch, chunk by chunk.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param pass The captures of the batch
 * @param timer The timestamps of the kind of the batch in the packet
 * @param last Whether the batch is the last of its kind in the packet
 */
static void box_gpu_bake_batch(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    box_gpu_bake_pass_t *pass,
    box_gpu_bake_timer_t *timer,
    bool last)
{
    WGPUTextureView shadow = NULL;
    int32_t first, chunk = box_gpu_bake_chunk(packet, pass->scene, pass->size);
    if (pass->cube) {
        int32_t layers = gpu->stats.texture_layers > 0
            ? gpu->stats.texture_layers
            : BOX_RENDER_REFLECT_LAYERS;
        chunk = glm_imin(chunk, glm_imax(layers / 6, 1));
    }

    box_gpu_bake_atlas(gpu, &b->sets[pass->kind], pass->size, chunk);
    box_gpu_bake_draws(gpu, b, packet, pass->scene, chunk);
    for (first = 0; first < pass->count; first += chunk) {
        int32_t count = pass->count - first < chunk ? pass->count - first : chunk;
        box_gpu_bake_chunk_capture(gpu, b, packet, pass, first, count, &shadow, timer,
            last && first + count >= pass->count);
    }
}

void box_gpu_bake_fini(
    box_gpu_t *gpu)
{
    if (gpu->bake) {
        box_gpu_bake_t *b = gpu->bake;
        ecs_os_mutex_lock(gpu->lock);
        gpu->bake = NULL;
        ecs_os_mutex_unlock(gpu->lock);
        box_gpu_bake_state_free(b);
    }
}

box_gpu_bake_t* box_gpu_bake_state(
    box_gpu_t *gpu)
{
    if (!gpu->bake) {
        box_gpu_bake_t *created = box_gpu_bake_state_new(gpu);
        ecs_os_mutex_lock(gpu->lock);
        gpu->bake = created;
        ecs_os_mutex_unlock(gpu->lock);
    }

    return gpu->bake;
}

bool box_gpu_bake_ready(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    return box_gpu_bake_pipeline(gpu, b) && box_gpu_reflect_ready(gpu);
}

/** Check whether the backend can capture a batch of a packet.
 *
 * @param packet The packet
 * @param kind box_gpu_bake_kind_t of the batch
 * @param index Index of the batch among the batches of its kind
 * @return Whether the batch has probes, a face size and its probes are in the packet
 */
static bool box_gpu_bake_batch_valid(
    const box_render_packet_t *packet,
    int32_t kind,
    int32_t index)
{
    int32_t first, count, size;
    bool result = true;
    if (kind == BOX_GPU_BAKE_PROBES) {
        const box_render_bake_batch_t *batch = ecs_vec_get_t(
            &packet->bakes, box_render_bake_batch_t, index);
        result = (batch->source == BOX_RENDER_BAKE_BLACK || batch->source == 0 ||
            batch->source == 1) && (batch->target == 0 || batch->target == 1);
        first = batch->first;
        count = batch->count;
        size = batch->size;
    } else {
        const box_render_reflect_batch_t *batch = ecs_vec_get_t(
            &packet->reflections, box_render_reflect_batch_t, index);
        first = batch->first;
        count = batch->count;
        size = batch->size;
    }

    return result && count > 0 && size > 0 && first >= 0 &&
        first + count <= ecs_vec_count(&packet->probes);
}

/** Capture the batches of one kind of a packet in order, measured together.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param kind box_gpu_bake_kind_t of the batches
 */
static void box_gpu_bake_run(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t kind)
{
    const ecs_vec_t *batches = kind == BOX_GPU_BAKE_PROBES ? &packet->bakes : &packet->reflections;
    int32_t i, count = ecs_vec_count(batches), probes = 0, last = -1;
    box_gpu_bake_timer_t timer = { .readback = -1 };
    box_gpu_bake_pass_t pass;
    uint64_t start = ecs_os_now();
    for (i = 0; i < count; i ++) {
        if (box_gpu_bake_batch_valid(packet, kind, i)) {
            last = i;
        }
    }

    if (last == -1) {
        goto done;
    }

    box_gpu_bake_t *b = box_gpu_bake_state(gpu);
    timer.readback = box_gpu_bake_readback(b);
    for (i = 0; i <= last; i ++) {
        if (!box_gpu_bake_batch_valid(packet, kind, i)) {
            continue;
        }

        if (kind == BOX_GPU_BAKE_PROBES) {
            box_gpu_bake_pass_probes(gpu, packet,
                ecs_vec_get_t(&packet->bakes, box_render_bake_batch_t, i), &pass);
        } else {
            box_gpu_bake_pass_reflect(gpu, packet,
                ecs_vec_get_t(&packet->reflections, box_render_reflect_batch_t, i), &pass);
        }

        box_gpu_bake_batch(gpu, b, packet, &pass, &timer, i == last);
        probes += pass.count;
    }

    if (timer.ended) {
        b->readbacks[timer.readback].probes = probes;
        b->readbacks[timer.readback].kind = kind;
        b->readbacks[timer.readback].state = BOX_GPU_BAKE_WRITTEN;
    } else if (!b->queries) {
        box_gpu_bake_done_measure(gpu, b, kind, start, probes);
    }

done:
    return;
}

void box_gpu_bake_capture(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    if (gpu->bake) {
        box_gpu_bake_readback_poll(gpu, gpu->bake);
        box_gpu_bake_resolve(gpu, gpu->bake);
    }

    box_gpu_bake_run(gpu, packet, BOX_GPU_BAKE_PROBES);
    box_gpu_bake_run(gpu, packet, BOX_GPU_BAKE_REFLECT);
}

#endif
