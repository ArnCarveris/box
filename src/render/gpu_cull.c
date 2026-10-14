/**
 * @file render/gpu_cull.c
 * @brief Cull pass of the webgpu backend: per view frustum and size culling into indirect draws.
 *
 * Every view of a target and every cascade of its shadows is a cull view. Before a view renders,
 * etc/shaders/cull.wgsl tests the instances of its draws against its frustum and smallest
 * projected size and writes the visible slots and the indirect draw arguments of each draw.
 *
 * The forward and shadow passes draw indirectly, reading the instance data by visible slot.
 * Blended draws, sorted back to front on the main thread, are culled in slot order.
 */

#include "gpu.h"
#include <stdlib.h>

#ifdef BOX_RENDER_WGPU

/** Bytes between the uniforms of two dispatches, the dynamic offset alignment of webgpu */
#define BOX_GPU_CULL_STRIDE (256)

/** Invocations of a workgroup of the cull shader */
#define BOX_GPU_CULL_GROUP (64)

/** Words of the indexed indirect draw arguments of one draw */
#define BOX_GPU_CULL_ARGS (5)

/** Smallest size of a cull buffer in bytes */
#define BOX_GPU_CULL_MIN_SIZE (4096)

void box_gpu_cull_init(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    WGPUBindGroupLayoutEntry instance = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Compute,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    };
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_cull_uniforms_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }
    };
    gpu->instance_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 1, .entries = &instance });
    c->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 4, .entries = entries });
    WGPUBindGroupLayout groups[2] = { c->layout, gpu->instance_layout };
    c->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    ecs_vec_init_t(NULL, &c->views, box_gpu_cull_view_t, 0);
    ecs_vec_init_t(NULL, &c->draws, box_gpu_cull_draw_t, 0);
    ecs_vec_init_t(NULL, &c->dispatches, box_gpu_cull_dispatch_t, 0);
    ecs_vec_init_t(NULL, &c->uniform_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &c->job_data, box_gpu_cull_job_t, 0);
    ecs_vec_init_t(NULL, &c->arg_data, uint32_t, 0);
    ecs_vec_init_t(NULL, &c->order, uint64_t, 0);
}

/** Release a buffer when it exists.
 *
 * @param buffer The buffer, set to NULL
 */
static void box_gpu_cull_release(
    WGPUBuffer *buffer)
{
    if (*buffer) {
        wgpuBufferRelease(*buffer);
        *buffer = NULL;
    }
}

void box_gpu_cull_fini(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    ecs_vec_fini_t(NULL, &c->views, box_gpu_cull_view_t);
    ecs_vec_fini_t(NULL, &c->draws, box_gpu_cull_draw_t);
    ecs_vec_fini_t(NULL, &c->dispatches, box_gpu_cull_dispatch_t);
    ecs_vec_fini_t(NULL, &c->uniform_data, uint8_t);
    ecs_vec_fini_t(NULL, &c->job_data, box_gpu_cull_job_t);
    ecs_vec_fini_t(NULL, &c->arg_data, uint32_t);
    ecs_vec_fini_t(NULL, &c->order, uint64_t);
    if (c->group) {
        wgpuBindGroupRelease(c->group);
        c->group = NULL;
    }

    box_gpu_cull_release(&c->uniforms);
    box_gpu_cull_release(&c->jobs);
    box_gpu_cull_release(&c->visible);
    box_gpu_cull_release(&c->args);
    if (c->parallel) {
        wgpuComputePipelineRelease(c->parallel);
        c->parallel = NULL;
    }

    if (c->ordered) {
        wgpuComputePipelineRelease(c->ordered);
        c->ordered = NULL;
    }

    if (c->pipeline_layout) {
        wgpuPipelineLayoutRelease(c->pipeline_layout);
        c->pipeline_layout = NULL;
    }

    if (c->layout) {
        wgpuBindGroupLayoutRelease(c->layout);
        c->layout = NULL;
    }

    if (gpu->instance_layout) {
        wgpuBindGroupLayoutRelease(gpu->instance_layout);
        gpu->instance_layout = NULL;
    }
}

WGPUBindGroup box_gpu_instance_group(
    box_gpu_t *gpu,
    box_gpu_buffer_t *b)
{
    if (b->instances || !(b->usage & BOX_RENDER_BUFFER_STORAGE)) {
        goto done;
    }

    WGPUBindGroupEntry entry = { .binding = 0, .buffer = b->buffer, .size = b->size };
    b->instances = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->instance_layout,
        .entryCount = 1,
        .entries = &entry
    });
done:
    return b->instances;
}

/** Get the cull pipelines, created again when the cull shader was recompiled.
 *
 * @param gpu The backend
 * @return Whether both pipelines exist
 */
static bool box_gpu_cull_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_CULL);
    if (!s || !s->module) {
        goto done;
    }

    if (c->parallel && c->revision == s->revision) {
        goto done;
    }

    if (c->parallel) {
        wgpuComputePipelineRelease(c->parallel);
    }

    if (c->ordered) {
        wgpuComputePipelineRelease(c->ordered);
    }

    c->parallel = wgpuDeviceCreateComputePipeline(gpu->device,
        &(WGPUComputePipelineDescriptor){
            .layout = c->pipeline_layout,
            .compute = {
                .module = s->module,
                .entryPoint = { .data = "cull_instances", .length = WGPU_STRLEN }
            }
        });
    c->ordered = wgpuDeviceCreateComputePipeline(gpu->device,
        &(WGPUComputePipelineDescriptor){
            .layout = c->pipeline_layout,
            .compute = {
                .module = s->module,
                .entryPoint = { .data = "cull_ordered", .length = WGPU_STRLEN }
            }
        });
    c->revision = s->revision;
done:
    return s && s->module && c->parallel && c->ordered;
}

void box_gpu_cull_begin(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    ecs_vec_clear(&c->views);
    ecs_vec_clear(&c->draws);
    ecs_vec_clear(&c->dispatches);
    ecs_vec_clear(&c->uniform_data);
    ecs_vec_clear(&c->job_data);
    ecs_vec_clear(&c->arg_data);
    c->visible_count = 0;
}

/** Write the frustum planes of a clip space matrix, normalized and in world space.
 *
 * Clip space keeps -w <= x, y <= w and 0 <= z <= w; a plane without a direction, the far
 * plane of an infinite projection, becomes one that keeps everything.
 *
 * @param m World to clip space
 * @param out Receives the six planes
 */
static void box_gpu_cull_planes(
    mat4 m,
    vec4 *out)
{
    int32_t i, j;
    for (i = 0; i < 6; i ++) {
        int32_t row = i / 2;
        float sign = (i & 1) ? -1.0f : 1.0f;
        for (j = 0; j < 4; j ++) {
            out[i][j] = i < 4 ? m[j][3] + sign * m[j][row] : 0.0f;
        }
    }

    for (j = 0; j < 4; j ++) {
        out[4][j] = m[j][3] - m[j][2];
        out[5][j] = m[j][2];
    }

    for (i = 0; i < 6; i ++) {
        float len = sqrtf(out[i][0] * out[i][0] + out[i][1] * out[i][1] +
            out[i][2] * out[i][2]);
        if (len < 1e-12f) {
            glm_vec4_copy((vec4){0, 0, 0, 1}, out[i]);
        } else {
            glm_vec4_scale(out[i], 1.0f / len, out[i]);
        }
    }
}

/** Check whether a cull view draws a batch.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param b The batch
 * @param index Index of the view in the packet
 * @param cascade Cascade of the cull view, -1 for the view itself
 * @return Whether the backend has the mesh and the storage instance buffer of the batch and
 * the view draws it: its own or every view's batches of its scene, only opaque casters in a
 * cascade
 */
static bool box_gpu_cull_draws(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b,
    int32_t index,
    int32_t cascade)
{
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    bool result = false;
    if (b->scene != v->scene || b->pipeline != BOX_RENDER_SHADER_MESH || !b->instance_count) {
        goto done;
    }

    if (cascade < 0 ? (b->view != -1 && b->view != index)
        : (b->view != -1 || b->blend != Opaque || !b->shadow))
    {
        goto done;
    }

    box_gpu_buffer_t *inst = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, b->instances);
    result = inst && (inst->usage & BOX_RENDER_BUFFER_STORAGE) &&
        ecs_map_get(&gpu->meshes, b->mesh) != NULL;
done:
    return result;
}

/** Append the uniforms of a dispatch of a cull view.
 *
 * @param c The cull state
 * @param base The uniforms of the cull view
 * @param buffer Handle of the instance buffer of its jobs
 * @param first First job of the dispatch
 * @param count Jobs of the dispatch
 * @param ordered Whether one invocation tests one job, else one instance of the one job
 * @param instances Instances of the one job of an unordered dispatch
 */
static void box_gpu_cull_dispatch(
    box_gpu_cull_t *c,
    const box_gpu_cull_uniforms_t *base,
    uint32_t buffer,
    uint32_t first,
    uint32_t count,
    bool ordered,
    uint32_t instances)
{
    int32_t offset = ecs_vec_count(&c->uniform_data);
    uint32_t work = ordered ? count : instances;
    ecs_vec_grow_t(NULL, &c->uniform_data, uint8_t, BOX_GPU_CULL_STRIDE);
    box_gpu_cull_uniforms_t *u = (box_gpu_cull_uniforms_t*)ecs_vec_get_t(
        &c->uniform_data, uint8_t, offset);
    *u = *base;
    u->jobs[0] = first;
    u->jobs[1] = count;
    box_gpu_cull_dispatch_t *d = ecs_vec_append_t(NULL, &c->dispatches, box_gpu_cull_dispatch_t);
    d->buffer = buffer;
    d->groups = (work + BOX_GPU_CULL_GROUP - 1) / BOX_GPU_CULL_GROUP;
    d->ordered = ordered;
}

/** Append the job of a draw: the slot range it tests and the mesh bounds.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param draw The draw
 * @return Index of the job
 */
static uint32_t box_gpu_cull_job(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_gpu_cull_draw_t *draw)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_batch_t *b = ecs_vec_get_t(
        &packet->batches, box_render_batch_t, draw->batch);
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
    uint32_t result = (uint32_t)ecs_vec_count(&c->job_data);
    box_gpu_cull_job_t *job = ecs_vec_append_t(NULL, &c->job_data, box_gpu_cull_job_t);
    ecs_os_memset_t(job, 0, box_gpu_cull_job_t);
    job->first = b->first_instance;
    job->count = draw->count;
    job->args = draw->args;
    job->base = draw->base;
    glm_vec3_copy(m->center, job->center);
    glm_vec3_copy(m->extent, job->extent);

    return result;
}

/** Order keys of draws: the instance buffer handle, then the draw.
 *
 * @param a The first key
 * @param b The second key
 * @return Negative when a comes first
 */
static int box_gpu_cull_order_cmp(
    const void *a,
    const void *b)
{
    uint64_t ka = *(const uint64_t*)a, kb = *(const uint64_t*)b;

    return (ka > kb) - (ka < kb);
}

/** Add the dispatches of the blended draws of a view, one per instance buffer.
 *
 * Each draw is a job of an ordered dispatch, which keeps the back to front order of its slots.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param base The uniforms of the cull view
 * @param first First draw of the cull view
 */
static void box_gpu_cull_ordered(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_gpu_cull_uniforms_t *base,
    int32_t first)
{
    box_gpu_cull_t *c = &gpu->cull;
    int32_t i, count = ecs_vec_count(&c->order);
    uint64_t *order = ecs_vec_first(&c->order);
    box_gpu_cull_draw_t *draws = ecs_vec_first(&c->draws);
    uint32_t job_first = 0, jobs = 0, buffer = 0;
    qsort(order, (size_t)count, sizeof(uint64_t), box_gpu_cull_order_cmp);
    for (i = 0; i < count; i ++) {
        uint32_t handle = (uint32_t)(order[i] >> 32);
        if (jobs && handle != buffer) {
            box_gpu_cull_dispatch(c, base, buffer, job_first, jobs, true, 0);
            jobs = 0;
        }

        uint32_t job = box_gpu_cull_job(gpu, packet,
            &draws[first + (int32_t)(uint32_t)order[i]]);
        if (!jobs) {
            job_first = job;
            buffer = handle;
        }

        jobs ++;
    }

    if (jobs) {
        box_gpu_cull_dispatch(c, base, buffer, job_first, jobs, true, 0);
    }
}

/** Add a cull view with its draws, jobs and dispatches.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param index Index of the view in the packet
 * @param slot Slot of the view in the view buffer
 * @param cascade Cascade, -1 for the view itself
 * @param base The uniforms of the cull view
 */
static void box_gpu_cull_view_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    int32_t cascade,
    const box_gpu_cull_uniforms_t *base)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    int32_t i, count = ecs_vec_count(&packet->batches);
    box_gpu_cull_view_t *cv = ecs_vec_append_t(NULL, &c->views, box_gpu_cull_view_t);
    cv->view = v->view;
    cv->slot = slot;
    cv->cascade = cascade;
    cv->first_draw = ecs_vec_count(&c->draws);
    cv->first_dispatch = ecs_vec_count(&c->dispatches);
    ecs_vec_clear(&c->order);
    for (i = 0; i < count; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        if (!box_gpu_cull_draws(gpu, packet, b, index, cascade)) {
            continue;
        }

        box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
        int32_t draw = ecs_vec_count(&c->draws);
        box_gpu_cull_draw_t *d = ecs_vec_append_t(NULL, &c->draws, box_gpu_cull_draw_t);
        d->batch = i;
        d->args = (uint32_t)(ecs_vec_count(&c->arg_data) / BOX_GPU_CULL_ARGS);
        d->base = c->visible_count;
        d->count = b->instance_count;
        c->visible_count += b->instance_count;
        uint32_t *args = ecs_vec_grow_t(NULL, &c->arg_data, uint32_t, BOX_GPU_CULL_ARGS);
        args[0] = m->index_count;
        args[1] = 0;
        args[2] = 0;
        args[3] = 0;
        args[4] = 0;
        if (b->view == -1) {
            uint32_t job = box_gpu_cull_job(gpu, packet, d);
            box_gpu_cull_dispatch(c, base, b->instances, job, 1, false, d->count);
        } else {
            *ecs_vec_append_t(NULL, &c->order, uint64_t) =
                ((uint64_t)b->instances << 32) | (uint32_t)(draw - cv->first_draw);
        }
    }

    cv = ecs_vec_last_t(&c->views, box_gpu_cull_view_t);
    cv->draw_count = ecs_vec_count(&c->draws) - cv->first_draw;
    if (ecs_vec_count(&c->order)) {
        box_gpu_cull_ordered(gpu, packet, base, cv->first_draw);
    }

    cv = ecs_vec_last_t(&c->views, box_gpu_cull_view_t);
    cv->dispatch_count = ecs_vec_count(&c->dispatches) - cv->first_dispatch;
}

void box_gpu_cull_add(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    mat4 proj,
    const box_gpu_view_uniforms_t *u)
{
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    box_gpu_cull_uniforms_t base;
    int32_t i, j;
    ecs_os_memset_t(&base, 0, box_gpu_cull_uniforms_t);
    for (i = 0; i < 6; i ++) {
        base.planes[i][3] = 1.0f;
    }

    if (v->cull.enabled) {
        box_gpu_cull_planes(ECS_CONST_CAST(vec4*, u->view_proj), base.planes);
        for (j = 0; j < 4; j ++) {
            base.clip_w[j] = u->view_proj[j][3];
        }

        base.size[0] = proj[1][1] * (float)t->desc.render_height * 0.5f;
        base.size[1] = v->cull.min_dimension > 0 ? v->cull.min_dimension : 0.0f;
        base.size[2] = v->camera_data.ortho ? 0.0f : 1.0f;
    }

    box_gpu_cull_view_add(gpu, packet, index, slot, -1, &base);
    for (i = 0; i < (int32_t)u->shadow_config[0]; i ++) {
        box_gpu_cull_uniforms_t cascade;
        ecs_os_memset_t(&cascade, 0, box_gpu_cull_uniforms_t);
        for (j = 0; j < 6; j ++) {
            cascade.planes[j][3] = 1.0f;
        }

        if (v->cull.enabled) {
            box_gpu_cull_planes(ECS_CONST_CAST(vec4*, u->shadow_matrix[i]), cascade.planes);
        }

        box_gpu_cull_view_add(gpu, packet, index, slot, i, &cascade);
    }
}

/** Make a cull buffer hold a number of bytes, growing it by doubling.
 *
 * @param gpu The backend
 * @param buffer The buffer
 * @param size Bytes of the buffer, updated
 * @param needed Bytes it must hold
 * @param usage Usage of the buffer
 * @return Whether the buffer was created
 */
static bool box_gpu_cull_reserve(
    box_gpu_t *gpu,
    WGPUBuffer *buffer,
    uint64_t *size,
    uint64_t needed,
    WGPUBufferUsage usage)
{
    uint64_t capacity = *size ? *size : BOX_GPU_CULL_MIN_SIZE;
    bool result = false;
    if (*buffer && needed <= *size) {
        goto done;
    }

    while (capacity < needed) {
        capacity *= 2;
    }

    box_gpu_cull_release(buffer);
    *buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = usage | WGPUBufferUsage_CopyDst,
        .size = capacity
    });
    *size = capacity;
    result = true;
done:
    return result;
}

void box_gpu_cull_upload(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    bool grown = false;
    if (!ecs_vec_count(&c->dispatches)) {
        goto done;
    }

    grown |= box_gpu_cull_reserve(gpu, &c->uniforms, &c->uniforms_size,
        (uint64_t)ecs_vec_count(&c->uniform_data), WGPUBufferUsage_Uniform);
    grown |= box_gpu_cull_reserve(gpu, &c->jobs, &c->jobs_size,
        (uint64_t)ecs_vec_count(&c->job_data) * sizeof(box_gpu_cull_job_t),
        WGPUBufferUsage_Storage);
    grown |= box_gpu_cull_reserve(gpu, &c->visible, &c->visible_size,
        (uint64_t)c->visible_count * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex);
    grown |= box_gpu_cull_reserve(gpu, &c->args, &c->args_size,
        (uint64_t)ecs_vec_count(&c->arg_data) * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect | WGPUBufferUsage_CopySrc);
    if (grown || !c->group) {
        WGPUBindGroupEntry entries[4] = {
            {
                .binding = 0, .buffer = c->uniforms,
                .size = sizeof(box_gpu_cull_uniforms_t)
            },
            { .binding = 1, .buffer = c->jobs, .size = c->jobs_size },
            { .binding = 2, .buffer = c->visible, .size = c->visible_size },
            { .binding = 3, .buffer = c->args, .size = c->args_size }
        };
        if (c->group) {
            wgpuBindGroupRelease(c->group);
        }

        c->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = c->layout,
            .entryCount = 4,
            .entries = entries
        });
    }

    wgpuQueueWriteBuffer(gpu->queue, c->uniforms, 0, ecs_vec_first(&c->uniform_data),
        (size_t)ecs_vec_count(&c->uniform_data));
    wgpuQueueWriteBuffer(gpu->queue, c->jobs, 0, ecs_vec_first(&c->job_data),
        (size_t)ecs_vec_count(&c->job_data) * sizeof(box_gpu_cull_job_t));
    wgpuQueueWriteBuffer(gpu->queue, c->args, 0, ecs_vec_first(&c->arg_data),
        (size_t)ecs_vec_count(&c->arg_data) * sizeof(uint32_t));
done:
    return;
}

void box_gpu_cull_record(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    int32_t slot)
{
    box_gpu_cull_t *c = &gpu->cull;
    WGPUComputePassEncoder pass = NULL;
    WGPUComputePipeline bound = NULL;
    int32_t i, d, count = ecs_vec_count(&c->views);
    if (!c->group || !box_gpu_cull_pipelines(gpu)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_gpu_cull_view_t *cv = ecs_vec_get_t(&c->views, box_gpu_cull_view_t, i);
        if (cv->slot != slot) {
            continue;
        }

        for (d = cv->first_dispatch; d < cv->first_dispatch + cv->dispatch_count; d ++) {
            const box_gpu_cull_dispatch_t *dp = ecs_vec_get_t(
                &c->dispatches, box_gpu_cull_dispatch_t, d);
            box_gpu_buffer_t *b = ecs_map_get_deref(
                &gpu->buffers, box_gpu_buffer_t, dp->buffer);
            WGPUComputePipeline pipeline = dp->ordered ? c->ordered : c->parallel;
            uint32_t offset = (uint32_t)d * BOX_GPU_CULL_STRIDE;
            if (!pass) {
                pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
            }

            if (pipeline != bound) {
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                bound = pipeline;
            }

            wgpuComputePassEncoderSetBindGroup(pass, 0, c->group, 1, &offset);
            wgpuComputePassEncoderSetBindGroup(pass, 1, box_gpu_instance_group(gpu, b), 0, NULL);
            wgpuComputePassEncoderDispatchWorkgroups(pass, dp->groups, 1, 1);
        }
    }

    if (pass) {
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

done:
    return;
}

const box_gpu_cull_view_t* box_gpu_cull_view(
    box_gpu_t *gpu,
    int32_t slot,
    int32_t cascade)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_gpu_cull_view_t *result = NULL;
    int32_t i, count = ecs_vec_count(&c->views);
    if (!c->group || !c->parallel) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_gpu_cull_view_t *cv = ecs_vec_get_t(&c->views, box_gpu_cull_view_t, i);
        if (cv->slot == slot && cv->cascade == cascade) {
            result = cv;
            break;
        }
    }

done:
    return result;
}

bool box_gpu_cull_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_gpu_cull_draw_t *draw,
    uint32_t group)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_batch_t *b = ecs_vec_get_t(
        &packet->batches, box_render_batch_t, draw->batch);
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
    box_gpu_buffer_t *inst = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, b->instances);
    WGPUBindGroup instances = inst ? box_gpu_instance_group(gpu, inst) : NULL;
    bool result = m && instances;
    if (!result) {
        goto done;
    }

    wgpuRenderPassEncoderSetBindGroup(pass, group, instances, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, c->visible,
        (uint64_t)draw->base * sizeof(uint32_t), (uint64_t)draw->count * sizeof(uint32_t));
    wgpuRenderPassEncoderSetIndexBuffer(
        pass, m->indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexedIndirect(pass, c->args,
        (uint64_t)draw->args * BOX_GPU_CULL_ARGS * sizeof(uint32_t));
done:
    return result;
}

bool box_render_gpu_cull_stats(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_cull_stats_t *out)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    ecs_vec_t args = {0};
    int32_t i, d;
    ecs_os_memset_t(out, 0, box_render_cull_stats_t);
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    box_gpu_cull_t *c = &gpu->cull;
    int32_t words = ecs_vec_count(&c->arg_data);
    if (!c->args || !words) {
        goto done;
    }

    ecs_vec_init_t(NULL, &args, uint32_t, words);
    ecs_vec_set_count_t(NULL, &args, uint32_t, words);
    uint32_t *counts = ecs_vec_first(&args);
    if (!box_gpu_read(gpu, c->args, 0, (uint32_t)words * sizeof(uint32_t), counts)) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&c->views); i ++) {
        const box_gpu_cull_view_t *cv = ecs_vec_get_t(&c->views, box_gpu_cull_view_t, i);
        if (cv->view != view) {
            continue;
        }

        result = true;
        for (d = cv->first_draw; d < cv->first_draw + cv->draw_count; d ++) {
            const box_gpu_cull_draw_t *draw = ecs_vec_get_t(
                &c->draws, box_gpu_cull_draw_t, d);
            int32_t kept = (int32_t)counts[draw->args * BOX_GPU_CULL_ARGS + 1];
            if (cv->cascade < 0) {
                out->tested += (int32_t)draw->count;
                out->visible += kept;
            } else {
                out->casters_tested += (int32_t)draw->count;
                out->casters_visible += kept;
            }
        }
    }

done:
    ecs_vec_fini_t(NULL, &args, uint32_t);

    return result;
}

#else

bool box_render_gpu_cull_stats(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_cull_stats_t *out)
{
    (void)world;
    (void)view;
    ecs_os_memset_t(out, 0, box_render_cull_stats_t);

    return false;
}

#endif
