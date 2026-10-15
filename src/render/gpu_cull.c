/**
 * @file render/gpu_cull.c
 * @brief Cull pass of the webgpu backend: frustum, size and occlusion culling into indirect draws.
 *
 * Every view and shadow cascade is a cull view: etc/shaders/cull.wgsl writes the visible slots
 * and indirect draw arguments of its draws, which the forward and shadow passes draw. A view
 * that culls by occlusion has an early and a late phase (gpu_hiz.c); cascades use the frustum.
 *
 * Blended draws are culled in slot order, keeping their back to front sort. The counters of
 * every view are copied back without waiting and become its CullStatus on the main thread.
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

/** Words of the counters of a cull view: drawn, frustum culled and two unused */
#define BOX_GPU_CULL_COUNTERS (4)

/** Cull passes between two releases of the unused histories and pyramids */
#define BOX_GPU_CULL_PURGE (64)

/** Phase of a dispatch without occlusion: draw what is in the frustum */
#define BOX_GPU_CULL_PHASE_ALL (0)

/** Phase that draws what is in the frustum and was drawn the previous frame */
#define BOX_GPU_CULL_PHASE_EARLY (1)

/** Phase that draws what is in the frustum, not hidden and not drawn early */
#define BOX_GPU_CULL_PHASE_LATE (2)

/** State of a counter copy */
typedef enum box_gpu_cull_readback_state_t {
    BOX_GPU_READBACK_FREE,          /**< Unused */
    BOX_GPU_READBACK_COPIED,        /**< The copy is recorded, mapped once it was submitted */
    BOX_GPU_READBACK_MAPPING,       /**< Waiting for the map */
    BOX_GPU_READBACK_MAPPED,        /**< Mapped, the counters can be read */
    BOX_GPU_READBACK_FAILED         /**< The map failed */
} box_gpu_cull_readback_state_t;

void box_gpu_cull_init(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    WGPUBindGroupLayoutEntry instance = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Compute,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    };
    WGPUBindGroupLayoutEntry entries[6] = {
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
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    int32_t i;
    gpu->instance_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 1, .entries = &instance });
    c->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = entries });
    box_gpu_hiz_init(gpu);
    WGPUBindGroupLayout groups[3] = { c->layout, gpu->instance_layout, c->history_layout };
    c->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 3,
            .bindGroupLayouts = groups
        });
    for (i = 0; i < BOX_GPU_CULL_READBACKS; i ++) {
        ecs_vec_init_t(NULL, &c->readbacks[i].records, box_gpu_cull_record_t, 0);
    }

    ecs_vec_init_t(NULL, &c->status, box_render_cull_status_t, 0);
    ecs_vec_init_t(NULL, &c->views, box_gpu_cull_view_t, 0);
    ecs_vec_init_t(NULL, &c->draws, box_gpu_cull_draw_t, 0);
    ecs_vec_init_t(NULL, &c->dispatches, box_gpu_cull_dispatch_t, 0);
    ecs_vec_init_t(NULL, &c->uniform_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &c->job_data, box_gpu_cull_job_t, 0);
    ecs_vec_init_t(NULL, &c->pages, box_gpu_cull_page_t, 0);
    c->window = BOX_GPU_CULL_WINDOW;
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
    int32_t i;
    for (i = 0; i < BOX_GPU_CULL_READBACKS; i ++) {
        box_gpu_cull_readback_t *r = &c->readbacks[i];
        if (r->state == BOX_GPU_READBACK_MAPPED) {
            wgpuBufferUnmap(r->buffer);
        }

        box_gpu_cull_release(&r->buffer);
        r->state = BOX_GPU_READBACK_FREE;
        ecs_vec_fini_t(NULL, &r->records, box_gpu_cull_record_t);
    }

    ecs_os_mutex_lock(gpu->lock);
    ecs_vec_fini_t(NULL, &c->status, box_render_cull_status_t);
    ecs_os_mutex_unlock(gpu->lock);
    box_gpu_hiz_fini(gpu);
    ecs_vec_fini_t(NULL, &c->views, box_gpu_cull_view_t);
    ecs_vec_fini_t(NULL, &c->draws, box_gpu_cull_draw_t);
    ecs_vec_fini_t(NULL, &c->dispatches, box_gpu_cull_dispatch_t);
    ecs_vec_fini_t(NULL, &c->uniform_data, uint8_t);
    ecs_vec_fini_t(NULL, &c->job_data, box_gpu_cull_job_t);
    ecs_vec_fini_t(NULL, &c->arg_data, uint32_t);
    ecs_vec_fini_t(NULL, &c->order, uint64_t);
    for (i = 0; i < ecs_vec_count(&c->pages); i ++) {
        box_gpu_cull_page_t *pg = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, i);
        if (pg->group) {
            wgpuBindGroupRelease(pg->group);
        }

        box_gpu_cull_release(&pg->visible);
        box_gpu_cull_release(&pg->lists);
        ecs_vec_fini_t(NULL, &pg->list_data, uint32_t);
    }

    ecs_vec_fini_t(NULL, &c->pages, box_gpu_cull_page_t);
    c->bound = false;
    box_gpu_cull_release(&c->uniforms);
    box_gpu_cull_release(&c->jobs);
    box_gpu_cull_release(&c->args);
    box_gpu_cull_release(&c->counters);
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
    box_gpu_hiz_pipelines(gpu, s->module);
    c->revision = s->revision;
done:
    return s && s->module && c->parallel && c->ordered;
}

/** Record the outcome of the map of a counter copy.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_cull_readback_t
 * @param userdata2 Unused
 */
static void box_gpu_cull_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_cull_readback_t *r = userdata1;
    (void)message;
    (void)userdata2;
    r->state = status == WGPUMapAsyncStatus_Success
        ? BOX_GPU_READBACK_MAPPED
        : BOX_GPU_READBACK_FAILED;
}

/** Turn the counters of a mapped copy into the status of its views.
 *
 * A view whose status is from a newer frame keeps it: copies finish mapping in any order, and
 * CullStatus never steps back to an older frame.
 *
 * @param gpu The backend
 * @param r The copy, mapped
 */
static void box_gpu_cull_publish(
    box_gpu_t *gpu,
    box_gpu_cull_readback_t *r)
{
    box_gpu_cull_t *c = &gpu->cull;
    const uint32_t *counters = wgpuBufferGetConstMappedRange(r->buffer, 0, (size_t)r->used);
    int32_t i, j, count = ecs_vec_count(&r->records);
    ecs_os_mutex_lock(gpu->lock);
    for (i = 0; counters && i < count; i ++) {
        const box_gpu_cull_record_t *rec = ecs_vec_get_t(
            &r->records, box_gpu_cull_record_t, i);
        const uint32_t *words = &counters[rec->counter * BOX_GPU_CULL_COUNTERS];
        box_render_cull_status_t *st = NULL;
        for (j = 0; j < ecs_vec_count(&c->status); j ++) {
            st = ecs_vec_get_t(&c->status, box_render_cull_status_t, j);
            if (st->view == rec->view) {
                break;
            }

            st = NULL;
        }

        if (!st) {
            st = ecs_vec_append_t(NULL, &c->status, box_render_cull_status_t);
            st->view = rec->view;
        } else if (st->status.frame > r->frame) {
            continue;
        }

        int32_t visible = (int32_t)words[0], frustum = (int32_t)words[1];
        int32_t hidden = rec->tested - visible - frustum;
        st->status = (CullStatus){
            .frame = r->frame,
            .instances = rec->tested,
            .visible = visible,
            .frustum_culled = frustum,
            .occlusion_culled = hidden > 0 ? hidden : 0
        };
    }

    ecs_os_mutex_unlock(gpu->lock);
}

/** Map the counter copies that were submitted and publish the ones that arrived.
 *
 * Mapped copies publish in slot order, or as box_render_gpu_readback_delivery asks.
 *
 * @param gpu The backend
 */
static void box_gpu_cull_readback_poll(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_cull_readback_t *mapped[BOX_GPU_CULL_READBACKS];
    int32_t i, j, count = 0;
    ecs_os_mutex_lock(gpu->lock);
    box_render_readback_delivery_t delivery = gpu->readbacks;
    ecs_os_mutex_unlock(gpu->lock);
#ifndef __EMSCRIPTEN__
    for (i = 0; i < BOX_GPU_CULL_READBACKS; i ++) {
        if (c->readbacks[i].state == BOX_GPU_READBACK_MAPPING) {
            wgpuDevicePoll(gpu->device, false, NULL);
            break;
        }
    }
#endif

    for (i = 0; i < BOX_GPU_CULL_READBACKS; i ++) {
        box_gpu_cull_readback_t *r = &c->readbacks[i];
        if (r->state == BOX_GPU_READBACK_MAPPED && delivery != BOX_RENDER_READBACK_HOLD) {
            for (j = count; j > 0 && delivery == BOX_RENDER_READBACK_NEWEST_FIRST &&
                mapped[j - 1]->frame < r->frame; j --)
            {
                mapped[j] = mapped[j - 1];
            }

            mapped[j] = r;
            count ++;
        } else if (r->state == BOX_GPU_READBACK_FAILED) {
            r->state = BOX_GPU_READBACK_FREE;
        } else if (r->state == BOX_GPU_READBACK_COPIED) {
            r->state = BOX_GPU_READBACK_MAPPING;
            wgpuBufferMapAsync(r->buffer, WGPUMapMode_Read, 0, (size_t)r->used,
                (WGPUBufferMapCallbackInfo){
                    .mode = WGPUCallbackMode_AllowSpontaneous,
                    .callback = box_gpu_cull_on_map,
                    .userdata1 = r
                });
        }
    }

    for (i = 0; i < count; i ++) {
        box_gpu_cull_publish(gpu, mapped[i]);
        wgpuBufferUnmap(mapped[i]->buffer);
        mapped[i]->state = BOX_GPU_READBACK_FREE;
    }
}

void box_gpu_cull_begin(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_cull_t *c = &gpu->cull;
    c->frame = packet->frame;
    c->stamp ++;
    ecs_os_mutex_lock(gpu->lock);
    c->window = gpu->cull_window;
    ecs_os_mutex_unlock(gpu->lock);
    box_gpu_cull_readback_poll(gpu);
    if (!(c->stamp % BOX_GPU_CULL_PURGE)) {
        box_gpu_hiz_purge(gpu);
    }

    ecs_vec_clear(&c->views);
    ecs_vec_clear(&c->draws);
    ecs_vec_clear(&c->dispatches);
    ecs_vec_clear(&c->uniform_data);
    ecs_vec_clear(&c->job_data);
    ecs_vec_clear(&c->arg_data);
    c->page_count = 0;
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
    bool pipeline = b->pipeline == BOX_RENDER_SHADER_MESH ||
        (cascade < 0 && b->pipeline == BOX_RENDER_SHADER_SHAPE2D);
    if (b->scene != v->scene || !pipeline || !b->instance_count) {
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
 * @param instances Instances of the one job of an unordered dispatch, 0 for an ordered one
 * @param phase BOX_GPU_CULL_PHASE_ALL, _EARLY or _LATE
 * @param page Page that holds the visible slots of its jobs
 * @param history History page and pyramid of its jobs, NULL for those of the cull view
 */
static void box_gpu_cull_dispatch(
    box_gpu_cull_t *c,
    const box_gpu_cull_uniforms_t *base,
    uint32_t buffer,
    uint32_t first,
    uint32_t count,
    uint32_t instances,
    uint32_t phase,
    uint32_t page,
    WGPUBindGroup history)
{
    int32_t offset = ecs_vec_count(&c->uniform_data);
    uint32_t work = instances ? instances : count;
    ecs_vec_grow_t(NULL, &c->uniform_data, uint8_t, BOX_GPU_CULL_STRIDE);
    box_gpu_cull_uniforms_t *u = (box_gpu_cull_uniforms_t*)ecs_vec_get_t(
        &c->uniform_data, uint8_t, offset);
    *u = *base;
    u->jobs[0] = first;
    u->jobs[1] = count;
    u->jobs[2] = phase;
    box_gpu_cull_dispatch_t *d = ecs_vec_append_t(NULL, &c->dispatches, box_gpu_cull_dispatch_t);
    d->buffer = buffer;
    d->groups = (work + BOX_GPU_CULL_GROUP - 1) / BOX_GPU_CULL_GROUP;
    d->ordered = !instances;
    d->late = phase == BOX_GPU_CULL_PHASE_LATE;
    d->page = page;
    d->history = history;
}

/** Start the next page of visible slots of the target.
 *
 * @param c The cull state
 * @return The page, empty
 */
static box_gpu_cull_page_t* box_gpu_cull_page_next(
    box_gpu_cull_t *c)
{
    box_gpu_cull_page_t *result;
    if (c->page_count == ecs_vec_count(&c->pages)) {
        result = ecs_vec_append_t(NULL, &c->pages, box_gpu_cull_page_t);
        ecs_os_memset_t(result, 0, box_gpu_cull_page_t);
        ecs_vec_init_t(NULL, &result->list_data, uint32_t, 0);
    } else {
        result = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, c->page_count);
    }

    ecs_vec_clear(&result->list_data);
    result->used = 0;
    c->page_count ++;

    return result;
}

/** Append the job of a draw: the slot range it tests and the mesh bounds.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param draw The draw
 * @param history First history word of its instance buffer, UINT32_MAX for none
 * @return Index of the job
 */
static uint32_t box_gpu_cull_job(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_gpu_cull_draw_t *draw,
    uint32_t history)
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
    if (b->listed) {
        box_gpu_cull_page_t *pg = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t,
            (int32_t)draw->page);
        job->first = (uint32_t)ecs_vec_count(&pg->list_data);
        job->list = 1;
        ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &pg->list_data, uint32_t, (int32_t)draw->count),
            box_render_packet_data(packet, b->slots), uint32_t, (int32_t)draw->count);
    }

    job->args = draw->args;
    job->base = draw->base;
    job->history = history;
    job->late = draw->late;
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
 * @param phase Phase the blended draws are culled in
 */
static void box_gpu_cull_ordered(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_gpu_cull_uniforms_t *base,
    int32_t first,
    uint32_t phase)
{
    box_gpu_cull_t *c = &gpu->cull;
    int32_t i, count = ecs_vec_count(&c->order);
    uint64_t *order = ecs_vec_first(&c->order);
    box_gpu_cull_draw_t *draws = ecs_vec_first(&c->draws);
    uint32_t job_first = 0, jobs = 0, buffer = 0, page = 0;
    qsort(order, (size_t)count, sizeof(uint64_t), box_gpu_cull_order_cmp);
    for (i = 0; i < count; i ++) {
        uint32_t handle = (uint32_t)(order[i] >> 32);
        const box_gpu_cull_draw_t *draw = &draws[first + (int32_t)(uint32_t)order[i]];
        if (jobs && (handle != buffer || draw->page != page)) {
            box_gpu_cull_dispatch(c, base, buffer, job_first, jobs, 0, phase, page, NULL);
            jobs = 0;
        }

        uint32_t job = box_gpu_cull_job(gpu, packet, draw, UINT32_MAX);
        if (!jobs) {
            job_first = job;
            buffer = handle;
            page = draw->page;
        }

        jobs ++;
    }

    if (jobs) {
        box_gpu_cull_dispatch(c, base, buffer, job_first, jobs, 0, phase, page, NULL);
    }
}

/** Add the draw of a batch to the cull view being added, with its job and dispatches.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param batch Index of the batch in the packet
 * @param base The uniforms of the cull view
 * @param history History of a view that culls by occlusion, NULL for none
 */
static void box_gpu_cull_draw_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t batch,
    const box_gpu_cull_uniforms_t *base,
    box_gpu_cull_history_t *history)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, batch);
    const box_gpu_cull_view_t *cv = ecs_vec_last_t(&c->views, box_gpu_cull_view_t);
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
    bool late = history && b->view == -1 && b->pipeline == BOX_RENDER_SHADER_MESH;
    uint32_t slots = b->instance_count * (late ? 2 : 1);
    box_gpu_cull_page_t *pg = c->page_count
        ? ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, c->page_count - 1) : NULL;
    if (!pg || (pg->used && (uint64_t)pg->used + slots > c->window)) {
        pg = box_gpu_cull_page_next(c);
    }

    int32_t draw = ecs_vec_count(&c->draws), i;
    box_gpu_cull_draw_t *d = ecs_vec_append_t(NULL, &c->draws, box_gpu_cull_draw_t);
    d->batch = batch;
    d->args = (uint32_t)(ecs_vec_count(&c->arg_data) / BOX_GPU_CULL_ARGS);
    d->late = late ? d->args + 1 : UINT32_MAX;
    d->page = (uint32_t)c->page_count - 1;
    d->base = pg->used;
    d->count = b->instance_count;
    pg->used += slots;
    for (i = 0; i < (late ? 2 : 1); i ++) {
        uint32_t *args = ecs_vec_grow_t(NULL, &c->arg_data, uint32_t, BOX_GPU_CULL_ARGS);
        args[0] = m->index_count;
        args[1] = 0;
        args[2] = 0;
        args[3] = 0;
        args[4] = 0;
    }

    if (b->view != -1) {
        *ecs_vec_append_t(NULL, &c->order, uint64_t) =
            ((uint64_t)b->instances << 32) | (uint32_t)(draw - cv->first_draw);
    } else if (history && !late) {
        uint32_t job = box_gpu_cull_job(gpu, packet, d, UINT32_MAX);
        box_gpu_cull_dispatch(c, base, b->instances, job, 1, d->count,
            BOX_GPU_CULL_PHASE_LATE, d->page, NULL);
    } else if (late) {
        uint32_t job = box_gpu_cull_job(gpu, packet, d,
            box_gpu_hiz_offset(history, b->instances));
        WGPUBindGroup group = box_gpu_hiz_group(gpu, history, cv->hiz,
            box_gpu_hiz_page(history, b->instances));
        box_gpu_cull_dispatch(c, base, b->instances, job, 1, d->count,
            BOX_GPU_CULL_PHASE_EARLY, d->page, group);
        box_gpu_cull_dispatch(c, base, b->instances, job, 1, d->count,
            BOX_GPU_CULL_PHASE_LATE, d->page, group);
    } else {
        uint32_t job = box_gpu_cull_job(gpu, packet, d, UINT32_MAX);
        box_gpu_cull_dispatch(c, base, b->instances, job, 1, d->count,
            BOX_GPU_CULL_PHASE_ALL, d->page, NULL);
    }
}

/** Get the phase a batch draws in: opaque meshes, then opaque 2D shapes, then blended.
 *
 * The 2D shapes blend their anti-aliased edges, so they draw over the opaque meshes behind.
 *
 * @param b The batch
 * @return The phase
 */
static int32_t box_gpu_cull_phase(
    const box_render_batch_t *b)
{
    int32_t result = b->pipeline == BOX_RENDER_SHADER_SHAPE2D ? 1 : 0;
    if (b->view != -1) {
        result = 2;
    }

    return result;
}

/** Add a cull view with its draws, jobs and dispatches.
 *
 * Draws are added by phase, in packet order within a phase.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param index Index of the view in the packet
 * @param slot Slot of the view in the view buffer
 * @param cascade Cascade, -1 for the view itself
 * @param base The uniforms of the cull view
 * @param history History of a view that culls by occlusion, NULL for none
 * @param hiz Pyramid of a view that culls by occlusion, NULL for none
 */
static void box_gpu_cull_view_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    int32_t cascade,
    box_gpu_cull_uniforms_t *base,
    box_gpu_cull_history_t *history,
    box_gpu_hiz_t *hiz)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    int32_t i, count = ecs_vec_count(&packet->batches);
    base->jobs[3] = (uint32_t)ecs_vec_count(&c->views);
    box_gpu_cull_view_t *cv = ecs_vec_append_t(NULL, &c->views, box_gpu_cull_view_t);
    cv->view = v->view;
    cv->slot = slot;
    cv->cascade = cascade;
    cv->first_draw = ecs_vec_count(&c->draws);
    cv->first_dispatch = ecs_vec_count(&c->dispatches);
    cv->counter = base->jobs[3];
    cv->hiz = hiz;
    cv->report = cascade < 0 && !v->target && v->view;
    cv->group = history ? box_gpu_hiz_group(gpu, history, hiz, 0) : c->empty_group;
    ecs_vec_clear(&c->order);
    for (i = 0; i < count * 3; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, i % count);
        if (box_gpu_cull_phase(b) == i / count &&
            box_gpu_cull_draws(gpu, packet, b, index, cascade))
        {
            box_gpu_cull_draw_add(gpu, packet, i % count, base, history);
        }
    }

    cv = ecs_vec_last_t(&c->views, box_gpu_cull_view_t);
    cv->draw_count = ecs_vec_count(&c->draws) - cv->first_draw;
    if (ecs_vec_count(&c->order)) {
        box_gpu_cull_ordered(gpu, packet, base, cv->first_draw,
            history ? BOX_GPU_CULL_PHASE_LATE : BOX_GPU_CULL_PHASE_ALL);
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
    box_gpu_cull_history_t *history = NULL;
    box_gpu_hiz_t *hiz = NULL;
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

    if (v->cull.enabled && v->cull.occlusion) {
        hiz = box_gpu_hiz_get(gpu, t->desc.render_width, t->desc.render_height);
        history = box_gpu_hiz_history(gpu, v->view, t->handle);
        box_gpu_hiz_ranges(gpu, history, packet, index);
        glm_mat4_copy(ECS_CONST_CAST(vec4*, u->view_proj), base.view_proj);
        base.hiz[0] = hiz->width;
        base.hiz[1] = hiz->height;
        base.hiz[2] = hiz->levels;
    }

    box_gpu_cull_view_add(gpu, packet, index, slot, -1, &base, history, hiz);
    for (i = 0; i < (int32_t)u->shadow_config[0]; i ++) {
        box_gpu_cull_uniforms_t cascade;
        ecs_os_memset_t(&cascade, 0, box_gpu_cull_uniforms_t);
        for (j = 0; j < 6; j ++) {
            cascade.planes[j][3] = 1.0f;
        }

        if (v->cull.enabled) {
            box_gpu_cull_planes(ECS_CONST_CAST(vec4*, u->shadow_matrix[i]), cascade.planes);
        }

        box_gpu_cull_view_add(gpu, packet, index, slot, i, &cascade, NULL, NULL);
    }
}

void box_gpu_cull_reset(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    ecs_vec_clear(&c->views);
    ecs_vec_clear(&c->draws);
    ecs_vec_clear(&c->dispatches);
    ecs_vec_clear(&c->uniform_data);
    ecs_vec_clear(&c->job_data);
    ecs_vec_clear(&c->arg_data);
    c->page_count = 0;
}

/** Check whether a probe capture draws a batch.
 *
 * @param gpu The backend
 * @param b The batch
 * @param scene Scene of the captures
 * @param cascade Cascade of the cull view, -1 for a face
 * @return Whether the batch has opaque static meshes of the scene the captures draw, and for
 * the cascade casts shadows, and the backend has its mesh and storage instance buffer
 */
static bool box_gpu_cull_capture_draws(
    box_gpu_t *gpu,
    const box_render_batch_t *b,
    ecs_entity_t scene,
    int32_t cascade)
{
    bool result = b->scene == scene && b->bake && b->view == -1 && b->blend == Opaque &&
        b->pipeline == BOX_RENDER_SHADER_MESH && b->instance_count &&
        (cascade < 0 || b->shadow);
    if (result) {
        box_gpu_buffer_t *inst = ecs_map_get_deref(
            &gpu->buffers, box_gpu_buffer_t, b->instances);
        result = inst && (inst->usage & BOX_RENDER_BUFFER_STORAGE) &&
            ecs_map_get(&gpu->meshes, b->mesh) != NULL;
    }

    return result;
}

void box_gpu_cull_capture_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    int32_t slot,
    int32_t cascade,
    mat4 view_proj,
    float pixels)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_cull_uniforms_t base;
    int32_t i, count = ecs_vec_count(&packet->batches);
    ecs_os_memset_t(&base, 0, box_gpu_cull_uniforms_t);
    box_gpu_cull_planes(view_proj, base.planes);
    for (i = 0; i < 4; i ++) {
        base.clip_w[i] = view_proj[i][3];
    }

    base.size[0] = pixels;
    base.size[1] = pixels > 0 ? BOX_RENDER_CULL_MIN_DIMENSION : 0.0f;
    base.size[2] = 1.0f;
    base.jobs[3] = (uint32_t)ecs_vec_count(&c->views);
    box_gpu_cull_view_t *cv = ecs_vec_append_t(NULL, &c->views, box_gpu_cull_view_t);
    ecs_os_memset_t(cv, 0, box_gpu_cull_view_t);
    cv->slot = slot;
    cv->cascade = cascade;
    cv->first_draw = ecs_vec_count(&c->draws);
    cv->first_dispatch = ecs_vec_count(&c->dispatches);
    cv->counter = base.jobs[3];
    cv->group = c->empty_group;
    for (i = 0; i < count; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        if (box_gpu_cull_capture_draws(gpu, b, scene, cascade)) {
            box_gpu_cull_draw_add(gpu, packet, i, &base, NULL);
        }
    }

    cv = ecs_vec_last_t(&c->views, box_gpu_cull_view_t);
    cv->draw_count = ecs_vec_count(&c->draws) - cv->first_draw;
    cv->dispatch_count = ecs_vec_count(&c->dispatches) - cv->first_dispatch;
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

/** Make the buffers of a page hold its slots and lists, upload its lists and bind it.
 *
 * @param gpu The backend
 * @param pg The page
 * @param grown Whether a buffer the pages share was created again
 */
static void box_gpu_cull_page_upload(
    box_gpu_t *gpu,
    box_gpu_cull_page_t *pg,
    bool grown)
{
    box_gpu_cull_t *c = &gpu->cull;
    int32_t lists = ecs_vec_count(&pg->list_data);
    grown |= box_gpu_cull_reserve(gpu, &pg->visible, &pg->visible_size,
        (uint64_t)pg->used * sizeof(uint32_t), WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex);
    grown |= box_gpu_cull_reserve(gpu, &pg->lists, &pg->lists_size,
        (uint64_t)lists * sizeof(uint32_t), WGPUBufferUsage_Storage);
    if (grown || !pg->group) {
        WGPUBindGroupEntry entries[6] = {
            {
                .binding = 0, .buffer = c->uniforms,
                .size = sizeof(box_gpu_cull_uniforms_t)
            },
            { .binding = 1, .buffer = c->jobs, .size = c->jobs_size },
            { .binding = 2, .buffer = pg->visible, .size = pg->visible_size },
            { .binding = 3, .buffer = c->args, .size = c->args_size },
            { .binding = 4, .buffer = c->counters, .size = c->counters_size },
            { .binding = 5, .buffer = pg->lists, .size = pg->lists_size }
        };
        if (pg->group) {
            wgpuBindGroupRelease(pg->group);
        }

        pg->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = c->layout,
            .entryCount = 6,
            .entries = entries
        });
    }

    if (lists) {
        wgpuQueueWriteBuffer(gpu->queue, pg->lists, 0, ecs_vec_first(&pg->list_data),
            (size_t)lists * sizeof(uint32_t));
    }
}

void box_gpu_cull_upload(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    static const uint8_t zeros[BOX_GPU_CULL_MIN_SIZE] = {0};
    uint64_t counters = (uint64_t)ecs_vec_count(&c->views) * BOX_GPU_CULL_COUNTERS *
        sizeof(uint32_t), offset;
    bool grown = false;
    int32_t i;
    if (!ecs_vec_count(&c->dispatches)) {
        goto done;
    }

    grown |= box_gpu_cull_reserve(gpu, &c->uniforms, &c->uniforms_size,
        (uint64_t)ecs_vec_count(&c->uniform_data), WGPUBufferUsage_Uniform);
    grown |= box_gpu_cull_reserve(gpu, &c->jobs, &c->jobs_size,
        (uint64_t)ecs_vec_count(&c->job_data) * sizeof(box_gpu_cull_job_t),
        WGPUBufferUsage_Storage);
    grown |= box_gpu_cull_reserve(gpu, &c->args, &c->args_size,
        (uint64_t)ecs_vec_count(&c->arg_data) * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect | WGPUBufferUsage_CopySrc);
    grown |= box_gpu_cull_reserve(gpu, &c->counters, &c->counters_size, counters,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    for (i = 0; i < c->page_count; i ++) {
        box_gpu_cull_page_upload(gpu, ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, i), grown);
    }

    c->bound = true;

    wgpuQueueWriteBuffer(gpu->queue, c->uniforms, 0, ecs_vec_first(&c->uniform_data),
        (size_t)ecs_vec_count(&c->uniform_data));
    wgpuQueueWriteBuffer(gpu->queue, c->jobs, 0, ecs_vec_first(&c->job_data),
        (size_t)ecs_vec_count(&c->job_data) * sizeof(box_gpu_cull_job_t));
    wgpuQueueWriteBuffer(gpu->queue, c->args, 0, ecs_vec_first(&c->arg_data),
        (size_t)ecs_vec_count(&c->arg_data) * sizeof(uint32_t));
    for (offset = 0; offset < counters; offset += sizeof(zeros)) {
        uint64_t size = counters - offset;
        wgpuQueueWriteBuffer(gpu->queue, c->counters, offset, zeros,
            (size_t)(size < sizeof(zeros) ? size : sizeof(zeros)));
    }

done:
    return;
}

/** Record the dispatches of the views of a slot in one phase.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param slot Slot of the view in the view buffer, -1 for every cull view
 * @param late Record the late dispatches instead of the others
 * @param writes Timestamps the compute pass writes, NULL for none
 */
static void box_gpu_cull_dispatches(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    int32_t slot,
    bool late,
    const WGPUPassTimestampWrites *writes)
{
    box_gpu_cull_t *c = &gpu->cull;
    WGPUComputePassEncoder pass = NULL;
    WGPUComputePipeline bound = NULL;
    int32_t i, d, count = ecs_vec_count(&c->views);
    if (!c->bound || !box_gpu_cull_pipelines(gpu)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_gpu_cull_view_t *cv = ecs_vec_get_t(&c->views, box_gpu_cull_view_t, i);
        if (slot != -1 && cv->slot != slot) {
            continue;
        }

        for (d = cv->first_dispatch; d < cv->first_dispatch + cv->dispatch_count; d ++) {
            const box_gpu_cull_dispatch_t *dp = ecs_vec_get_t(
                &c->dispatches, box_gpu_cull_dispatch_t, d);
            box_gpu_buffer_t *b = ecs_map_get_deref(
                &gpu->buffers, box_gpu_buffer_t, dp->buffer);
            WGPUComputePipeline pipeline = dp->ordered ? c->ordered : c->parallel;
            uint32_t offset = (uint32_t)d * BOX_GPU_CULL_STRIDE;
            if (dp->late != late) {
                continue;
            }

            if (!pass) {
                pass = wgpuCommandEncoderBeginComputePass(encoder,
                    &(WGPUComputePassDescriptor){ .timestampWrites = writes });
            }

            if (pipeline != bound) {
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                bound = pipeline;
            }

            wgpuComputePassEncoderSetBindGroup(pass, 0, ecs_vec_get_t(&c->pages,
                box_gpu_cull_page_t, (int32_t)dp->page)->group, 1, &offset);
            wgpuComputePassEncoderSetBindGroup(pass, 1, box_gpu_instance_group(gpu, b), 0, NULL);
            wgpuComputePassEncoderSetBindGroup(pass, 2, dp->history ? dp->history : cv->group,
                0, NULL);
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

void box_gpu_cull_record(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    int32_t slot)
{
    box_gpu_cull_dispatches(gpu, encoder, slot, false, NULL);
}

bool box_gpu_cull_record_all(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const WGPUPassTimestampWrites *writes)
{
    box_gpu_cull_dispatches(gpu, encoder, -1, false, writes);

    return ecs_vec_count(&gpu->cull.dispatches) && gpu->cull.bound && gpu->cull.parallel;
}

const box_gpu_cull_view_t* box_gpu_cull_view(
    box_gpu_t *gpu,
    int32_t slot,
    int32_t cascade)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_gpu_cull_view_t *result = NULL;
    int32_t i, count = ecs_vec_count(&c->views);
    if (!c->bound || !c->parallel) {
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

bool box_gpu_cull_occludes(
    box_gpu_t *gpu,
    int32_t slot)
{
    bool ready = box_gpu_cull_pipelines(gpu) && gpu->cull.hiz_first && gpu->cull.hiz_reduce;
    const box_gpu_cull_view_t *cv = ready ? box_gpu_cull_view(gpu, slot, -1) : NULL;

    return cv && cv->hiz;
}

void box_gpu_cull_record_late(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    WGPUTextureView depth,
    int32_t slot)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    if (!cv || !cv->hiz) {
        goto done;
    }

    box_gpu_hiz_build(gpu, encoder, cv->hiz, depth);
    box_gpu_cull_dispatches(gpu, encoder, slot, true, NULL);
done:
    return;
}

/** Find a free counter copy with room for a number of bytes.
 *
 * @param gpu The backend
 * @param size Bytes it must hold
 * @return The copy, NULL when every copy is in flight
 */
static box_gpu_cull_readback_t* box_gpu_cull_readback(
    box_gpu_t *gpu,
    uint64_t size)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_cull_readback_t *result = NULL;
    int32_t i;
    for (i = 0; i < BOX_GPU_CULL_READBACKS; i ++) {
        if (c->readbacks[i].state == BOX_GPU_READBACK_FREE) {
            result = &c->readbacks[i];
            break;
        }
    }

    if (!result || (result->buffer && result->size >= size)) {
        goto done;
    }

    box_gpu_cull_release(&result->buffer);
    result->size = BOX_GPU_CULL_COUNTERS * sizeof(uint32_t) * 16;
    while (result->size < size) {
        result->size *= 2;
    }

    result->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
        .size = result->size
    });
done:
    return result;
}

void box_gpu_cull_finish(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder)
{
    box_gpu_cull_t *c = &gpu->cull;
    int32_t i, d, count = ecs_vec_count(&c->views);
    uint64_t size = (uint64_t)count * BOX_GPU_CULL_COUNTERS * sizeof(uint32_t);
    box_gpu_cull_readback_t *r = NULL;
    if (!count || !c->bound || !c->parallel) {
        goto done;
    }

    r = box_gpu_cull_readback(gpu, size);
    if (!r) {
        goto done;
    }

    ecs_vec_clear(&r->records);
    for (i = 0; i < count; i ++) {
        const box_gpu_cull_view_t *cv = ecs_vec_get_t(&c->views, box_gpu_cull_view_t, i);
        if (!cv->report) {
            continue;
        }

        box_gpu_cull_record_t *rec = ecs_vec_append_t(
            NULL, &r->records, box_gpu_cull_record_t);
        rec->view = cv->view;
        rec->counter = cv->counter;
        rec->tested = 0;
        for (d = cv->first_draw; d < cv->first_draw + cv->draw_count; d ++) {
            rec->tested += (int32_t)ecs_vec_get_t(&c->draws, box_gpu_cull_draw_t, d)->count;
        }
    }

    if (!ecs_vec_count(&r->records)) {
        goto done;
    }

    wgpuCommandEncoderCopyBufferToBuffer(encoder, c->counters, 0, r->buffer, 0, size);
    r->used = size;
    r->frame = c->frame;
    r->state = BOX_GPU_READBACK_COPIED;
done:
    return;
}

bool box_gpu_cull_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_gpu_cull_draw_t *draw,
    uint32_t group,
    bool late)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_batch_t *b = ecs_vec_get_t(
        &packet->batches, box_render_batch_t, draw->batch);
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
    box_gpu_buffer_t *inst = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, b->instances);
    WGPUBindGroup instances = inst ? box_gpu_instance_group(gpu, inst) : NULL;
    uint32_t args = late ? draw->late : draw->args;
    uint32_t base = late ? draw->base + draw->count : draw->base;
    bool result = m && instances && args != UINT32_MAX;
    if (!result) {
        goto done;
    }

    wgpuRenderPassEncoderSetBindGroup(pass, group, instances, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, ecs_vec_get_t(&c->pages,
        box_gpu_cull_page_t, (int32_t)draw->page)->visible,
        (uint64_t)base * sizeof(uint32_t), (uint64_t)draw->count * sizeof(uint32_t));
    wgpuRenderPassEncoderSetIndexBuffer(
        pass, m->indices, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexedIndirect(pass, c->args,
        (uint64_t)args * BOX_GPU_CULL_ARGS * sizeof(uint32_t));
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
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_cull_stats needs RenderSettings.main_thread and box_render_sync");
    if (!renders) {
        goto done;
    }

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
            if (draw->late != UINT32_MAX) {
                kept += (int32_t)counts[draw->late * BOX_GPU_CULL_ARGS + 1];
            }

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

bool box_render_gpu_cull_status(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    box_render_gpu_stats_t stats;
    bool result = box_render_gpu_stats(world, &stats);
    ecs_vec_clear(out);
    if (!result) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    ecs_os_mutex_lock(gpu->lock);
    int32_t count = ecs_vec_count(&gpu->cull.status);
    if (count) {
        ecs_vec_set_count_t(NULL, out, box_render_cull_status_t, count);
        ecs_os_memcpy_n(ecs_vec_first(out), ecs_vec_first(&gpu->cull.status),
            box_render_cull_status_t, count);
        ecs_vec_clear(&gpu->cull.status);
    }

    ecs_os_mutex_unlock(gpu->lock);
done:
    return result;
}

#else

bool box_render_gpu_cull_status(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    (void)world;
    ecs_vec_clear(out);

    return false;
}

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

/** Write the CullStatus of the views whose counters the GPU backend read back.
 *
 * @param it The iterator of the system, its ctx a vector of box_render_cull_status_t
 */
static void BoxRenderCullStatus(
    ecs_iter_t *it)
{
    ecs_vec_t *statuses = it->ctx;
    int32_t i;
    if (!box_render_gpu_cull_status(it->world, statuses)) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(statuses); i ++) {
        const box_render_cull_status_t *st = ecs_vec_get_t(
            statuses, box_render_cull_status_t, i);
        if (ecs_is_alive(it->world, st->view) && ecs_has(it->world, st->view, View)) {
            ecs_set_ptr(it->stage, st->view, CullStatus, &st->status);
        }
    }

done:
    return;
}

/** Free the vector of the status system.
 *
 * @param ctx The vector
 */
static void box_render_cull_status_free(
    void *ctx)
{
    ecs_vec_t *statuses = ctx;
    ecs_vec_fini_t(NULL, statuses, box_render_cull_status_t);
    ecs_os_free(statuses);
}

void box_render_cull_status_init(
    ecs_world_t *world)
{
    ecs_vec_t *statuses = ecs_os_calloc_t(ecs_vec_t);
    ecs_vec_init_t(NULL, statuses, box_render_cull_status_t, 0);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderCullStatus" }),
        .phase = EcsOnLoad,
        .run = BoxRenderCullStatus,
        .ctx = statuses,
        .ctx_free = box_render_cull_status_free
    });
}
