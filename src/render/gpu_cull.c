/**
 * @file render/gpu_cull.c
 * @brief Cull pass of the webgpu backend: frustum, size and occlusion culling into indirect draws.
 *
 * A view and its shadow cascades are a cull group (gpu_cull_group.c): cull.wgsl tests each
 * instance once against every frustum of the group and compacts the visible slots of each one
 * in slot order. A view that culls by occlusion adds a late phase over itself (gpu_hiz.c).
 *
 * Separate View entities do not share a cull pass: each view records its cull right before its
 * own shadow and forward passes, with its own history, pyramid and late phase; views of one scene
 * in one target (split screen) are rare and would only share the early test.
 */

#include "gpu.h"
#include <stdlib.h>

#ifdef BOX_RENDER_WGPU

/** Smallest size of a cull buffer in bytes */
#define BOX_GPU_CULL_MIN_SIZE (4096)

/** Timestamps of the cull passes of one target at most, a begin and an end per pass */
#define BOX_GPU_CULL_STAMPS (64)

/** Workgroups along x of a cull_write dispatch at most, the WebGPU dispatch size limit */
#define BOX_GPU_CULL_WRITE_ROW (65535)

/** Words of the counters of a cull view: drawn, frustum culled and two unused */
#define BOX_GPU_CULL_COUNTERS (4)

/** Cull passes between two releases of the unused histories and pyramids */
#define BOX_GPU_CULL_PURGE (64)

/** Words of an entry of a cull table: job, first scan word, workgroups, draw arguments, first
 * visible entry, counters and two unused, as ENTRY_WORDS of cull.wgsl */
#define BOX_GPU_CULL_ENTRY_WORDS (8)

/** Words of a workgroup of the cull_instances list: job, first scan word, workgroups of the job
 * and the workgroup in the job, as LIST_WORDS of cull.wgsl */
#define BOX_GPU_CULL_LIST_WORDS (4)

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
    WGPUBindGroupLayoutEntry entries[8] = {
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
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = BOX_GPU_CULL_FRUSTUMS_SIZE
            }
        }
    };
    int32_t i;
    box_gpu_instance_layouts_init(gpu);
    gpu->no_assets = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage,
        .size = sizeof(vec4)
    });
    gpu->no_surfaces = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage,
        .size = sizeof(box_render_surface_t)
    });
    c->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 8, .entries = entries });
    box_gpu_hiz_init(gpu);
    WGPUBindGroupLayout groups[3] = { c->layout, gpu->transform_layout, c->history_layout };
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
    ecs_vec_init_t(NULL, &c->tables, box_gpu_cull_table_t, 0);
    ecs_vec_init_t(NULL, &c->runs, box_gpu_cull_run_t, 0);
    ecs_vec_init_t(NULL, &c->groups, box_gpu_cull_group_t, 0);
    ecs_vec_init_t(NULL, &c->entries, int32_t, 0);
    ecs_vec_init_t(NULL, &c->masks, uint32_t, 0);
    ecs_vec_init_t(NULL, &c->frustums, box_gpu_cull_desc_t, 0);
    if (gpu->timestamps) {
        c->stamps = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
            .type = WGPUQueryType_Timestamp,
            .count = BOX_GPU_CULL_STAMPS
        });
        c->stamp_resolve = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
            .size = BOX_GPU_CULL_STAMPS * sizeof(uint64_t)
        });
    }

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

/** Release a compute pipeline when it exists.
 *
 * @param pipeline The pipeline, set to NULL
 */
static void box_gpu_cull_pipeline_release(
    WGPUComputePipeline *pipeline)
{
    if (*pipeline) {
        wgpuComputePipelineRelease(*pipeline);
        *pipeline = NULL;
    }
}

/** Create a compute pipeline of an entry point of the cull shader.
 *
 * @param gpu The backend
 * @param module The cull shader
 * @param entry Name of the entry point
 * @return The pipeline
 */
static WGPUComputePipeline box_gpu_cull_pipeline(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    const char *entry)
{
    return wgpuDeviceCreateComputePipeline(gpu->device, &(WGPUComputePipelineDescriptor){
        .layout = gpu->cull.pipeline_layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    });
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
        box_gpu_cull_release(&pg->scan);
        ecs_vec_fini_t(NULL, &pg->list_data, uint32_t);
        ecs_vec_fini_t(NULL, &pg->table_data, uint32_t);
    }

    ecs_vec_fini_t(NULL, &c->pages, box_gpu_cull_page_t);
    ecs_vec_fini_t(NULL, &c->tables, box_gpu_cull_table_t);
    ecs_vec_fini_t(NULL, &c->runs, box_gpu_cull_run_t);
    ecs_vec_fini_t(NULL, &c->groups, box_gpu_cull_group_t);
    ecs_vec_fini_t(NULL, &c->entries, int32_t);
    ecs_vec_fini_t(NULL, &c->masks, uint32_t);
    ecs_vec_fini_t(NULL, &c->frustums, box_gpu_cull_desc_t);
    if (c->stamps) {
        wgpuQuerySetRelease(c->stamps);
        c->stamps = NULL;
    }

    box_gpu_cull_release(&c->stamp_resolve);
    c->bound = false;
    box_gpu_cull_release(&c->uniforms);
    box_gpu_cull_release(&c->jobs);
    box_gpu_cull_release(&c->args);
    box_gpu_cull_release(&c->counters);
    if (c->parallel) {
        wgpuComputePipelineRelease(c->parallel);
        c->parallel = NULL;
    }

    box_gpu_cull_pipeline_release(&c->ordered);
    box_gpu_cull_pipeline_release(&c->scan);
    box_gpu_cull_pipeline_release(&c->write);
    if (c->pipeline_layout) {
        wgpuPipelineLayoutRelease(c->pipeline_layout);
        c->pipeline_layout = NULL;
    }

    if (c->layout) {
        wgpuBindGroupLayoutRelease(c->layout);
        c->layout = NULL;
    }

    box_gpu_instance_layouts_fini(gpu);

    box_gpu_cull_release(&gpu->no_assets);
    box_gpu_cull_release(&gpu->no_surfaces);
}

/** Get the cull pipelines, created again when the cull shader was recompiled.
 *
 * @param gpu The backend
 * @return Whether every cull pipeline exists
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

    box_gpu_cull_pipeline_release(&c->parallel);
    box_gpu_cull_pipeline_release(&c->ordered);
    box_gpu_cull_pipeline_release(&c->scan);
    box_gpu_cull_pipeline_release(&c->write);
    c->parallel = box_gpu_cull_pipeline(gpu, s->module, "cull_instances");
    c->ordered = box_gpu_cull_pipeline(gpu, s->module, "cull_ordered");
    c->scan = box_gpu_cull_pipeline(gpu, s->module, "cull_scan");
    c->write = box_gpu_cull_pipeline(gpu, s->module, "cull_write");
    box_gpu_hiz_pipelines(gpu, s->module);
    c->revision = s->revision;
done:
    return s && s->module && c->parallel && c->ordered && c->scan && c->write;
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

/** Get the nanoseconds of one tick of the timestamps.
 *
 * @param gpu The backend
 * @return The period, 1 in the browser, which resolves timestamps in nanoseconds
 */
static double box_gpu_cull_period(
    box_gpu_t *gpu)
{
#ifdef __EMSCRIPTEN__
    (void)gpu;

    return 1.0;
#else
    return (double)wgpuQueueGetTimestampPeriod(gpu->queue);
#endif
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
    if (counters && r->stamps) {
        const uint64_t *t = (const uint64_t*)ECS_OFFSET(counters, r->stamp_offset);
        uint64_t ticks = 0;
        for (i = 0; i < (int32_t)r->stamps; i ++) {
            ticks += t[i * 2 + 1] > t[i * 2] ? t[i * 2 + 1] - t[i * 2] : 0;
        }

        gpu->pending.cull_ns += (int64_t)((double)ticks * box_gpu_cull_period(gpu));
        gpu->pending.cull_passes += (int32_t)r->stamps;
    }
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
            for (j = count; j > 0 && delivery >= BOX_RENDER_READBACK_NEWEST_FIRST &&
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

    if (delivery == BOX_RENDER_READBACK_ONE_NEWEST && count > 1) {
        count = 1;
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
    c->stamp_count = 0;
    ecs_os_mutex_lock(gpu->lock);
    c->window = gpu->cull_window;
    c->copies = gpu->cull_copies;
    ecs_os_mutex_unlock(gpu->lock);
    box_gpu_cull_readback_poll(gpu);
    if (!(c->stamp % BOX_GPU_CULL_PURGE)) {
        box_gpu_hiz_purge(gpu);
        box_gpu_light_purge(gpu);
    }

    box_gpu_cull_reset(gpu);
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
    if ((int64_t)pg->used > gpu->pending.cull_page_slots) {
        gpu->pending.cull_page_slots = (int32_t)pg->used;
    }

    grown |= box_gpu_cull_reserve(gpu, &pg->visible, &pg->visible_size,
        (uint64_t)pg->used * sizeof(uint32_t), WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex);
    grown |= box_gpu_cull_reserve(gpu, &pg->lists, &pg->lists_size,
        (uint64_t)lists * sizeof(uint32_t), WGPUBufferUsage_Storage);
    grown |= box_gpu_cull_reserve(gpu, &pg->scan, &pg->scan_size,
        (uint64_t)pg->scan_used * sizeof(uint32_t), WGPUBufferUsage_Storage);
    if (grown || !pg->group) {
        WGPUBindGroupEntry entries[8] = {
            {
                .binding = 0, .buffer = c->uniforms,
                .size = sizeof(box_gpu_cull_uniforms_t)
            },
            { .binding = 1, .buffer = c->jobs, .size = c->jobs_size },
            { .binding = 2, .buffer = pg->visible, .size = pg->visible_size },
            { .binding = 3, .buffer = c->args, .size = c->args_size },
            { .binding = 4, .buffer = c->counters, .size = c->counters_size },
            { .binding = 5, .buffer = pg->lists, .size = pg->lists_size },
            { .binding = 6, .buffer = pg->scan, .size = pg->scan_size },
            { .binding = 7, .buffer = c->uniforms, .size = BOX_GPU_CULL_FRUSTUMS_SIZE }
        };
        if (pg->group) {
            wgpuBindGroupRelease(pg->group);
        }

        pg->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = c->layout,
            .entryCount = 8,
            .entries = entries
        });
    }

    if (lists) {
        wgpuQueueWriteBuffer(gpu->queue, pg->lists, 0, ecs_vec_first(&pg->list_data),
            (size_t)lists * sizeof(uint32_t));
    }

    if (ecs_vec_count(&pg->table_data)) {
        wgpuQueueWriteBuffer(gpu->queue, pg->scan, (uint64_t)pg->table_base * sizeof(uint32_t),
            ecs_vec_first(&pg->table_data),
            (size_t)ecs_vec_count(&pg->table_data) * sizeof(uint32_t));
    }
}

/** Add a run of a table: the cull_instances dispatch of its next parallel jobs.
 *
 * @param c The cull state
 * @param table The table
 * @param buffer Instance page of its jobs
 * @param history History page and pyramid of its jobs, NULL for those of the group
 * @param list First word of its part of the workgroup list
 * @return Index of the run
 */
static int32_t box_gpu_cull_run_add(
    box_gpu_cull_t *c,
    const box_gpu_cull_table_t *table,
    uint32_t buffer,
    WGPUBindGroup history,
    uint32_t list)
{
    int32_t result = ecs_vec_count(&c->runs);
    box_gpu_cull_run_t *r = ecs_vec_append_t(NULL, &c->runs, box_gpu_cull_run_t);
    r->group = table->group;
    r->late = table->late;
    r->page = table->page;
    r->uniform = list;
    r->buffer = buffer;
    r->history = history;
    r->groups = 0;

    return result;
}

/** Check whether a dispatch is a parallel job of a phase and page.
 *
 * @param dp The dispatch
 * @param late The phase
 * @param page The page
 * @return Whether it belongs to the table of the phase and page
 */
static bool box_gpu_cull_tabled(
    const box_gpu_cull_dispatch_t *dp,
    bool late,
    uint32_t page)
{
    return !dp->ordered && dp->late == late && dp->page == page;
}

/** Add the table of the parallel jobs of a cull group, phase and page.
 *
 * Appends to the table data of the page an entry per (job, frustum) (job, scan words,
 * workgroups, arguments and visible entry of the phase, counters), a cull_write list entry per
 * workgroup of each, a cull_instances list entry per workgroup of each job, and the uniforms.
 *
 * The consecutive jobs that read the same instance page and history are a run, one
 * cull_instances dispatch over its part of the list with its own copy of those uniforms. A job
 * without history joins any run of its instance page.
 *
 * @param gpu The backend
 * @param index Index of the cull group
 * @param late The phase
 * @param page The page
 */
static void box_gpu_cull_table(
    box_gpu_t *gpu,
    int32_t index,
    bool late,
    uint32_t page)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_gpu_cull_group_t *grp = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, index);
    box_gpu_cull_page_t *pg = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, (int32_t)page);
    box_gpu_cull_table_t table = { .group = index, .late = late, .page = page };
    int32_t d, g, k, f, first = -1, end = grp->first_dispatch + grp->dispatch_count;
    uint32_t entries = pg->table_base + (uint32_t)ecs_vec_count(&pg->table_data);
    for (d = grp->first_dispatch; d < end; d ++) {
        const box_gpu_cull_dispatch_t *dp = ecs_vec_get_t(
            &c->dispatches, box_gpu_cull_dispatch_t, d);
        if (!box_gpu_cull_tabled(dp, late, page)) {
            continue;
        }

        for (f = 0, k = 0; f < BOX_GPU_CULL_GROUP_VIEWS; f ++) {
            if (!((dp->mask >> f) & 1u)) {
                continue;
            }

            const box_gpu_cull_draw_t *draw = ecs_vec_get_t(&c->draws, box_gpu_cull_draw_t,
                *ecs_vec_get_t(&c->entries, int32_t, dp->first_entry + k));
            bool second = late && draw->late != UINT32_MAX;
            uint32_t *e = ecs_vec_grow_t(NULL, &pg->table_data, uint32_t,
                BOX_GPU_CULL_ENTRY_WORDS);
            e[0] = dp->first;
            e[1] = dp->scan + (uint32_t)k * dp->groups * BOX_GPU_CULL_SCAN_WORDS;
            e[2] = dp->groups;
            e[3] = second ? draw->late : draw->args;
            e[4] = second ? draw->base + draw->count : draw->base;
            e[5] = ecs_vec_get_t(&c->views, box_gpu_cull_view_t, grp->first_view + f)->counter;
            e[6] = 0;
            e[7] = 0;
            table.entries ++;
            k ++;
        }

        first = first < 0 ? d : first;
        table.buffer = dp->buffer;
    }

    if (first < 0) {
        goto done;
    }

    uint32_t list = pg->table_base + (uint32_t)ecs_vec_count(&pg->table_data);
    uint32_t entry = entries;
    for (d = first; d < end; d ++) {
        const box_gpu_cull_dispatch_t *dp = ecs_vec_get_t(
            &c->dispatches, box_gpu_cull_dispatch_t, d);
        int32_t frustums = (int32_t)box_gpu_cull_bits(dp->mask);
        if (!box_gpu_cull_tabled(dp, late, page)) {
            continue;
        }

        for (k = 0; k < frustums; k ++) {
            uint32_t scan = dp->scan + (uint32_t)k * dp->groups * BOX_GPU_CULL_SCAN_WORDS;
            uint32_t *l = ecs_vec_grow_t(NULL, &pg->table_data, uint32_t,
                (int32_t)dp->groups * 2);
            for (g = 0; g < (int32_t)dp->groups; g ++) {
                l[g * 2] = scan + (uint32_t)g * BOX_GPU_CULL_SCAN_WORDS;
                l[g * 2 + 1] = entry;
            }

            table.groups += dp->groups;
            entry += BOX_GPU_CULL_ENTRY_WORDS;
        }
    }

    uint32_t jobs = pg->table_base + (uint32_t)ecs_vec_count(&pg->table_data), listed = 0;
    int32_t first_run = ecs_vec_count(&c->runs), run = -1, r;
    for (d = first; d < end; d ++) {
        const box_gpu_cull_dispatch_t *dp = ecs_vec_get_t(
            &c->dispatches, box_gpu_cull_dispatch_t, d);
        if (!box_gpu_cull_tabled(dp, late, page)) {
            continue;
        }

        box_gpu_cull_run_t *last = run < 0 ? NULL
            : ecs_vec_get_t(&c->runs, box_gpu_cull_run_t, run);
        if (!last || last->buffer != dp->buffer ||
            (dp->history && last->history && last->history != dp->history))
        {
            run = box_gpu_cull_run_add(c, &table, dp->buffer, dp->history,
                jobs + listed * BOX_GPU_CULL_LIST_WORDS);
        } else if (dp->history && !last->history) {
            last->history = dp->history;
        }

        ecs_vec_get_t(&c->runs, box_gpu_cull_run_t, run)->groups += dp->groups;
        uint32_t *l = ecs_vec_grow_t(NULL, &pg->table_data, uint32_t,
            (int32_t)dp->groups * BOX_GPU_CULL_LIST_WORDS);
        for (g = 0; g < (int32_t)dp->groups; g ++) {
            l[g * BOX_GPU_CULL_LIST_WORDS] = dp->first;
            l[g * BOX_GPU_CULL_LIST_WORDS + 1] = dp->scan;
            l[g * BOX_GPU_CULL_LIST_WORDS + 2] = dp->groups;
            l[g * BOX_GPU_CULL_LIST_WORDS + 3] = (uint32_t)g;
        }

        listed += dp->groups;
    }

    box_gpu_cull_uniforms_t u = grp->base;
    WGPUBindGroup history = grp->history;
    u.jobs[2] = late ? BOX_GPU_CULL_PHASE_LATE
        : grp->occlusion ? BOX_GPU_CULL_PHASE_EARLY : BOX_GPU_CULL_PHASE_ALL;
    u.scan[0] = entries;
    u.scan[1] = table.entries;
    u.scan[2] = list;
    u.scan[3] = table.groups;
    table.uniform = box_gpu_cull_uniform(c, &u, sizeof(u));
    *ecs_vec_append_t(NULL, &c->tables, box_gpu_cull_table_t) = table;
    for (r = first_run; r < ecs_vec_count(&c->runs); r ++) {
        box_gpu_cull_run_t *rn = ecs_vec_get_t(&c->runs, box_gpu_cull_run_t, r);
        box_gpu_cull_uniforms_t ru = u;
        ru.scan[2] = rn->uniform;
        ru.scan[3] = rn->groups;
        rn->uniform = box_gpu_cull_uniform(c, &ru, sizeof(ru));
        rn->history = rn->history ? rn->history : history;
    }
done:
    return;
}

/** Add the tables of every cull group, phase and page of the target.
 *
 * @param gpu The backend
 */
static void box_gpu_cull_tables(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    int32_t g, p, late;
    ecs_vec_clear(&c->tables);
    ecs_vec_clear(&c->runs);
    for (p = 0; p < c->page_count; p ++) {
        box_gpu_cull_page_t *pg = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, p);
        ecs_vec_clear(&pg->table_data);
        pg->table_base = pg->scan_used;
    }

    for (g = 0; g < ecs_vec_count(&c->groups); g ++) {
        for (late = 0; late < 2; late ++) {
            for (p = 0; p < c->page_count; p ++) {
                box_gpu_cull_table(gpu, g, late != 0, (uint32_t)p);
            }
        }
    }

    for (p = 0; p < c->page_count; p ++) {
        box_gpu_cull_page_t *pg = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, p);
        pg->scan_used += (uint32_t)ecs_vec_count(&pg->table_data);
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
    if (!ecs_vec_count(&c->views)) {
        goto done;
    }

    box_gpu_cull_tables(gpu);
    uint64_t uniforms = (uint64_t)ecs_vec_count(&c->uniform_data);
    grown |= box_gpu_cull_reserve(gpu, &c->uniforms, &c->uniforms_size,
        uniforms > BOX_GPU_CULL_FRUSTUMS_SIZE ? uniforms : BOX_GPU_CULL_FRUSTUMS_SIZE,
        WGPUBufferUsage_Uniform);
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
    if (ecs_vec_count(&c->dispatches)) {
        wgpuQueueWriteBuffer(gpu->queue, c->uniforms, 0, ecs_vec_first(&c->uniform_data),
            (size_t)ecs_vec_count(&c->uniform_data));
        wgpuQueueWriteBuffer(gpu->queue, c->jobs, 0, ecs_vec_first(&c->job_data),
            (size_t)ecs_vec_count(&c->job_data) * sizeof(box_gpu_cull_job_t));
        wgpuQueueWriteBuffer(gpu->queue, c->args, 0, ecs_vec_first(&c->arg_data),
            (size_t)ecs_vec_count(&c->arg_data) * sizeof(uint32_t));
    }

    for (offset = 0; offset < counters; offset += sizeof(zeros)) {
        uint64_t size = counters - offset;
        wgpuQueueWriteBuffer(gpu->queue, c->counters, offset, zeros,
            (size_t)(size < sizeof(zeros) ? size : sizeof(zeros)));
    }

done:
    return;
}

/** Bind the groups of a cull dispatch and dispatch it.
 *
 * The instance page is set only when it differs from the one bound last in the pass. A dispatch
 * that reads no instances keeps the page bound last, if any.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param page Page whose bind group is group 0
 * @param uniform Byte offset of the uniforms of the dispatch
 * @param frustums Byte offset of the frustums of its cull group
 * @param buffer Instance page bound as group 1
 * @param any Whether any page will do, for a dispatch that reads no instances
 * @param history Group 2
 * @param groups Workgroups along x and y
 * @param bound Instance bind group bound last in the pass, NULL for none, updated
 * @return Whether the instance page exists and the dispatch was recorded
 */
static bool box_gpu_cull_run(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    uint32_t page,
    uint32_t uniform,
    uint32_t frustums,
    uint32_t buffer,
    bool any,
    WGPUBindGroup history,
    const uint32_t *groups,
    WGPUBindGroup *bound)
{
    box_gpu_cull_t *c = &gpu->cull;
    WGPUBindGroup instances = any ? *bound : NULL;
    uint32_t offsets[2] = { uniform, frustums };
    if (!instances) {
        instances = box_gpu_instance_group(gpu, buffer, 0, NULL, false);
    }

    if (!instances) {
        goto done;
    }

    wgpuComputePassEncoderSetBindGroup(pass, 0, ecs_vec_get_t(&c->pages,
        box_gpu_cull_page_t, (int32_t)page)->group, 2, offsets);
    if (instances != *bound) {
        wgpuComputePassEncoderSetBindGroup(pass, 1, instances, 0, NULL);
        box_gpu_count(gpu, BOX_GPU_STAT_INSTANCE_BIND);
        *bound = instances;
    }

    wgpuComputePassEncoderSetBindGroup(pass, 2, history, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups[0], groups[1], 1);
done:
    return instances != NULL;
}

/** Begin the compute pass of the cull dispatches when it is not begun yet.
 *
 * @param encoder The encoder of the frame
 * @param pass The pass, set when it begins
 * @param writes Timestamps the compute pass writes, NULL for none
 */
static void box_gpu_cull_pass(
    WGPUCommandEncoder encoder,
    WGPUComputePassEncoder *pass,
    const WGPUPassTimestampWrites *writes)
{
    if (!*pass) {
        *pass = box_gpu_compute_begin(encoder, writes);
    }
}

/** Get the workgroups along x and y of a dispatch over a number of workgroups.
 *
 * @param count Workgroups
 * @param out Receives the workgroups along x, at most the WebGPU dispatch size limit, and y
 */
static void box_gpu_cull_rows(
    uint32_t count,
    uint32_t *out)
{
    out[0] = count < BOX_GPU_CULL_WRITE_ROW ? count : BOX_GPU_CULL_WRITE_ROW;
    out[1] = (count + BOX_GPU_CULL_WRITE_ROW - 1) / BOX_GPU_CULL_WRITE_ROW;
}

/** Check whether a cull group belongs to a slot.
 *
 * @param c The cull state
 * @param group Index of the cull group
 * @param slot Slot of the view in the view buffer, -1 for every group
 * @return Whether the group is recorded for the slot
 */
static bool box_gpu_cull_slot(
    box_gpu_cull_t *c,
    int32_t group,
    int32_t slot)
{
    return slot == -1 ||
        ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, group)->slot == slot;
}

/** Record the dispatches of the cull groups of a slot in one phase.
 *
 * Every ordered dispatch has its cull_ordered dispatch, every run of parallel jobs its
 * cull_instances dispatch; then one cull_scan and one cull_write dispatch per table (cull
 * group, phase and page) finish the (job, frustum) entries of its jobs.
 *
 * The draws keep the order of their slots at a number of dispatches that does not grow with
 * them nor with the frustums of a group. The dispatches of the cull of views count in the
 * stats of the backend.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param slot Slot of the view in the view buffer, -1 for every cull group
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
    WGPUBindGroup instances = NULL;
    bool views = slot != -1;
    int32_t stage, i, d;
    if (!c->bound || !box_gpu_cull_pipelines(gpu)) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&c->groups); i ++) {
        const box_gpu_cull_group_t *g = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, i);
        if (!box_gpu_cull_slot(c, i, slot)) {
            continue;
        }

        for (d = g->first_dispatch; d < g->first_dispatch + g->dispatch_count; d ++) {
            const box_gpu_cull_dispatch_t *dp = ecs_vec_get_t(
                &c->dispatches, box_gpu_cull_dispatch_t, d);
            uint32_t groups[2] = { dp->groups, 1 };
            if (dp->late != late || !dp->ordered) {
                continue;
            }

            box_gpu_cull_pass(encoder, &pass, writes);
            if (c->ordered != bound) {
                wgpuComputePassEncoderSetPipeline(pass, c->ordered);
                bound = c->ordered;
            }

            if (box_gpu_cull_run(gpu, pass, dp->page, dp->uniform, g->frustums, dp->buffer,
                false, dp->history ? dp->history : g->history, groups, &instances) && views)
            {
                box_gpu_count(gpu, BOX_GPU_STAT_CULL_DISPATCH);
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&c->runs); i ++) {
        const box_gpu_cull_run_t *r = ecs_vec_get_t(&c->runs, box_gpu_cull_run_t, i);
        const box_gpu_cull_group_t *g = ecs_vec_get_t(
            &c->groups, box_gpu_cull_group_t, r->group);
        uint32_t groups[2];
        if (!box_gpu_cull_slot(c, r->group, slot) || r->late != late) {
            continue;
        }

        box_gpu_cull_rows(r->groups, groups);
        box_gpu_cull_pass(encoder, &pass, writes);
        if (c->parallel != bound) {
            wgpuComputePassEncoderSetPipeline(pass, c->parallel);
            bound = c->parallel;
        }

        if (box_gpu_cull_run(gpu, pass, r->page, r->uniform, g->frustums, r->buffer, false,
            r->history, groups, &instances) && views)
        {
            box_gpu_count(gpu, BOX_GPU_STAT_CULL_DISPATCH);
            box_gpu_count(gpu, BOX_GPU_STAT_CULL_INSTANCES);
        }
    }

    for (stage = 0; stage < 2; stage ++) {
        WGPUComputePipeline pipeline = stage ? c->write : c->scan;
        for (i = 0; i < ecs_vec_count(&c->tables); i ++) {
            const box_gpu_cull_table_t *t = ecs_vec_get_t(&c->tables, box_gpu_cull_table_t, i);
            const box_gpu_cull_group_t *g = ecs_vec_get_t(
                &c->groups, box_gpu_cull_group_t, t->group);
            uint32_t groups[2];
            if (!box_gpu_cull_slot(c, t->group, slot) || t->late != late) {
                continue;
            }

            box_gpu_cull_rows(stage ? t->groups : t->entries, groups);
            box_gpu_cull_pass(encoder, &pass, writes);
            if (pipeline != bound) {
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                bound = pipeline;
            }

            if (box_gpu_cull_run(gpu, pass, t->page, t->uniform, g->frustums, t->buffer, true,
                g->history, groups, &instances) && views)
            {
                box_gpu_count(gpu, BOX_GPU_STAT_CULL_DISPATCH);
            }
        }
    }

    if (pass) {
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

done:
    return;
}

/** Get the timestamps of the next cull pass of a slot and phase, when it records one.
 *
 * @param gpu The backend
 * @param slot Slot of the view in the view buffer
 * @param late The phase
 * @param out Receives the timestamps
 * @return The timestamps, NULL without timestamp queries, room or dispatches to time
 */
static const WGPUPassTimestampWrites* box_gpu_cull_timed(
    box_gpu_t *gpu,
    int32_t slot,
    bool late,
    WGPUPassTimestampWrites *out)
{
    box_gpu_cull_t *c = &gpu->cull;
    const WGPUPassTimestampWrites *result = NULL;
    int32_t i, d;
    if (!c->stamps || !c->bound || !c->parallel || c->stamp_count + 2 > BOX_GPU_CULL_STAMPS) {
        goto done;
    }

    for (i = 0; !result && i < ecs_vec_count(&c->groups); i ++) {
        const box_gpu_cull_group_t *g = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, i);
        for (d = 0; g->slot == slot && !result && d < g->dispatch_count; d ++) {
            if (ecs_vec_get_t(&c->dispatches, box_gpu_cull_dispatch_t,
                g->first_dispatch + d)->late == late)
            {
                *out = (WGPUPassTimestampWrites){
                    .querySet = c->stamps,
                    .beginningOfPassWriteIndex = c->stamp_count,
                    .endOfPassWriteIndex = c->stamp_count + 1
                };
                c->stamp_count += 2;
                result = out;
            }
        }
    }

done:
    return result;
}

void box_gpu_cull_record(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    int32_t slot)
{
    WGPUPassTimestampWrites writes;
    box_gpu_cull_dispatches(gpu, encoder, slot, false,
        box_gpu_cull_timed(gpu, slot, false, &writes));
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

    WGPUPassTimestampWrites writes;
    box_gpu_hiz_build(gpu, encoder, cv->hiz, depth);
    box_gpu_cull_dispatches(gpu, encoder, slot, true,
        box_gpu_cull_timed(gpu, slot, true, &writes));
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

    uint64_t stamp_offset = (size + 7) & ~(uint64_t)7;
    r = box_gpu_cull_readback(gpu, stamp_offset + c->stamp_count * sizeof(uint64_t));
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
    r->stamps = 0;
    if (c->stamp_count) {
        wgpuCommandEncoderResolveQuerySet(encoder, c->stamps, 0, c->stamp_count,
            c->stamp_resolve, 0);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, c->stamp_resolve, 0, r->buffer,
            stamp_offset, c->stamp_count * sizeof(uint64_t));
        r->stamp_offset = stamp_offset;
        r->stamps = c->stamp_count / 2;
        r->used = stamp_offset + c->stamp_count * sizeof(uint64_t);
    }

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
    bool late,
    bool shading,
    WGPUBindGroup *bound)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_batch_t *b = ecs_vec_get_t(
        &packet->batches, box_render_batch_t, draw->batch);
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
    WGPUBindGroup instances = m
        ? box_gpu_instance_group(gpu, b->instances, b->mesh, m, shading) : NULL;
    uint32_t args = late ? draw->late : draw->args;
    uint32_t base = late ? draw->base + draw->count : draw->base;
    bool result = m && instances && args != UINT32_MAX;
    if (!result) {
        goto done;
    }

    if (instances != *bound) {
        wgpuRenderPassEncoderSetBindGroup(pass, group, instances, 0, NULL);
        box_gpu_count(gpu, BOX_GPU_STAT_INSTANCE_BIND);
        *bound = instances;
    }

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

/** Read back what the frustums of the cull pass of a view kept in the last rendered target.
 *
 * @param world The world
 * @param view Entity with the View
 * @param cascade The one frustum to read, -1 for the view; ignored when all is set
 * @param all Read every frustum of the view, the view itself in tested and visible and the
 * cascades in the casters counts, instead of one frustum in tested and visible
 * @param out Receives the counts
 * @return Whether the last rendered target culled the frustums
 */
static bool box_gpu_cull_stats_read(
    ecs_world_t *world,
    ecs_entity_t view,
    int32_t cascade,
    bool all,
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
        if (cv->view != view || (!all && cv->cascade != cascade)) {
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

            if (cv->cascade < 0 || !all) {
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

bool box_render_gpu_cull_stats(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_cull_stats_t *out)
{
    return box_gpu_cull_stats_read(world, view, -1, true, out);
}

bool box_render_gpu_cull_frustum(
    ecs_world_t *world,
    ecs_entity_t view,
    int32_t cascade,
    box_render_cull_stats_t *out)
{
    return box_gpu_cull_stats_read(world, view, cascade, false, out);
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

bool box_render_gpu_cull_frustum(
    ecs_world_t *world,
    ecs_entity_t view,
    int32_t cascade,
    box_render_cull_stats_t *out)
{
    (void)world;
    (void)view;
    (void)cascade;
    ecs_os_memset_t(out, 0, box_render_cull_stats_t);

    return false;
}

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
 * A status older than the frame a view's CullStatus already has is dropped: copies finish in
 * any order and a take empties the backend's list between two of them.
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
        const CullStatus *cur = ecs_is_alive(it->world, st->view)
            ? ecs_get(it->world, st->view, CullStatus) : NULL;
        if (ecs_is_alive(it->world, st->view) && ecs_has(it->world, st->view, View) &&
            (!cur || cur->frame <= st->status.frame))
        {
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
