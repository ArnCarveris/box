/**
 * @file render/gpu_texture_graph_read.c
 * @brief Intermediates, readbacks and timestamps of the texture graphs of the webgpu backend.
 *
 * Intermediates come from a pool shared by every graph. Readbacks copy an image or a
 * materialized node after an evaluation and map without waiting, as the cull counters do.
 */

#include "gpu_texture_graph.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

/** Deliver a readback to the main thread, with its texels or failed.
 *
 * @param gpu The backend
 * @param read The readback, its texels are taken
 */
static void box_gpu_tg_deliver(
    box_gpu_t *gpu,
    box_render_texture_read_t *read)
{
    ecs_os_mutex_lock(gpu->lock);
    *ecs_vec_append_t(NULL, &gpu->graph_reads, box_render_texture_read_t) = *read;
    ecs_os_mutex_unlock(gpu->lock);
    read->texels = NULL;
    gpu->pending.texture_graph_reads ++;
}

/** Get the nanoseconds of one tick of the timestamps.
 *
 * @param gpu The backend
 * @return The period, 1 in the browser, which resolves timestamps in nanoseconds
 */
static double box_gpu_tg_period(
    box_gpu_t *gpu)
{
#ifdef __EMSCRIPTEN__
    (void)gpu;

    return 1.0;
#else
    return (double)wgpuQueueGetTimestampPeriod(gpu->queue);
#endif
}

/** Record the outcome of the map of a timestamp readback.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_tg_stamp_t
 * @param userdata2 Unused
 */
static void box_gpu_tg_on_stamp(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_tg_stamp_t *st = userdata1;
    (void)message;
    (void)userdata2;
    if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        st->state = status == WGPUMapAsyncStatus_Success
            ? BOX_GPU_TG_STAMP_MAPPED : BOX_GPU_TG_STAMP_FAILED;
    }
}

/** Record the outcome of the map of a readback.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_tg_read_t
 * @param userdata2 Unused
 */
static void box_gpu_tg_on_read(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_tg_read_t *r = userdata1;
    (void)message;
    (void)userdata2;
    if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        r->state = status == WGPUMapAsyncStatus_Success
            ? BOX_GPU_TG_READ_MAPPED : BOX_GPU_TG_READ_FAILED;
    }
}

/** Convert a half float.
 *
 * @param h The bits
 * @return The value
 */
static float box_gpu_tg_half(
    uint16_t h)
{
    uint32_t exponent = (h >> 10) & 31u, mantissa = h & 1023u;
    float result;
    if (!exponent) {
        result = ldexpf((float)mantissa, -24);
    } else if (exponent == 31u) {
        result = mantissa ? NAN : INFINITY;
    } else {
        result = ldexpf((float)(mantissa | 1024u), (int)exponent - 25);
    }

    return h & 0x8000u ? -result : result;
}

/** Turn the mapped copy of a readback into four floats per texel.
 *
 * @param r The readback, mapped
 * @return The texels, freed by the caller
 */
static float* box_gpu_tg_texels(
    box_gpu_tg_read_t *r)
{
    uint32_t w = (uint32_t)r->read.width, h = (uint32_t)r->read.height, x, y;
    const uint8_t *data = wgpuBufferGetConstMappedRange(r->buffer, 0,
        (size_t)r->stride * h);
    float *result = data ? ecs_os_malloc_n(float, (int32_t)(w * h * 4)) : NULL;
    for (y = 0; result && y < h; y ++) {
        const uint8_t *row = data + (size_t)y * r->stride;
        for (x = 0; x < w; x ++) {
            float *out = &result[((size_t)y * w + x) * 4];
            if (r->format == WGPUTextureFormat_R32Float) {
                ecs_os_memcpy(&out[0], row + x * 4, 4);
                out[1] = 0;
                out[2] = 0;
                out[3] = 1;
            } else if (r->format == WGPUTextureFormat_RGBA32Float) {
                ecs_os_memcpy(out, row + x * 16, 16);
            } else if (r->format == WGPUTextureFormat_RGBA16Float) {
                const uint16_t *t = (const uint16_t*)(row + x * 8);
                out[0] = box_gpu_tg_half(t[0]);
                out[1] = box_gpu_tg_half(t[1]);
                out[2] = box_gpu_tg_half(t[2]);
                out[3] = box_gpu_tg_half(t[3]);
            } else if (r->format == BOX_GPU_TEXTURE_SINGLE) {
                out[0] = (float)row[x] / 255.0f;
                out[1] = 0;
                out[2] = 0;
                out[3] = 1;
            } else {
                out[0] = (float)row[x * 4] / 255.0f;
                out[1] = (float)row[x * 4 + 1] / 255.0f;
                out[2] = (float)row[x * 4 + 2] / 255.0f;
                out[3] = (float)row[x * 4 + 3] / 255.0f;
            }
        }
    }

    return result;
}

/** Record the copy of a readback from a texture into a buffer it maps after the submit.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param r The readback
 * @param texture The texture, level 0 is copied
 * @param format Its format
 * @param offset First column and row copied
 * @param width Width copied
 * @param height Height copied
 */
static void box_gpu_tg_copy(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    box_gpu_tg_read_t *r,
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t offset,
    uint32_t width,
    uint32_t height)
{
    uint32_t row = width * box_gpu_tg_texel_size(format);
    r->stride = (row + BOX_GPU_ROW_ALIGN - 1) / BOX_GPU_ROW_ALIGN * BOX_GPU_ROW_ALIGN;
    r->format = format;
    r->read.width = (int32_t)width;
    r->read.height = (int32_t)height;
    r->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)r->stride * height
    });
    wgpuCommandEncoderCopyTextureToBuffer(encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = texture,
            .origin = { offset, offset, 0 },
            .aspect = WGPUTextureAspect_All
        },
        &(WGPUTexelCopyBufferInfo){
            .layout = { .bytesPerRow = r->stride, .rowsPerImage = height },
            .buffer = r->buffer
        },
        &(WGPUExtent3D){ width, height, 1 });
    r->state = BOX_GPU_TG_READ_COPIED;
}

void box_gpu_tg_read_fail(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    int32_t index)
{
    box_gpu_tg_read_t *r = *ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, index);
    r->read.texels = NULL;
    box_gpu_tg_deliver(gpu, &r->read);
    if (r->buffer) {
        wgpuBufferRelease(r->buffer);
    }

    ecs_os_free(r);
    ecs_vec_remove_ordered_t(&tg->reads, box_gpu_tg_read_t*, index);
}

box_gpu_tg_pooled_t* box_gpu_tg_pool_take(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format)
{
    box_gpu_tg_pooled_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&tg->pool) && !result; i ++) {
        box_gpu_tg_pooled_t *p = *ecs_vec_get_t(&tg->pool, box_gpu_tg_pooled_t*, i);
        if (!p->used && p->width == width && p->height == height && p->format == format) {
            result = p;
        }
    }

    if (!result) {
        result = ecs_os_calloc_t(box_gpu_tg_pooled_t);
        result->width = width;
        result->height = height;
        result->format = format;
        result->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
            .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
                WGPUTextureUsage_CopySrc,
            .dimension = WGPUTextureDimension_2D,
            .size = { width, height, 1 },
            .format = format,
            .mipLevelCount = 1,
            .sampleCount = 1
        });
        result->view = wgpuTextureCreateView(result->texture, NULL);
        *ecs_vec_append_t(NULL, &tg->pool, box_gpu_tg_pooled_t*) = result;
        gpu->texture_graph_pool_bytes += (uint64_t)width * height *
            box_gpu_tg_texel_size(format);
    }

    result->used = true;
    result->last = tg->frame;

    return result;
}

void box_gpu_tg_pool_put(
    box_gpu_tg_pooled_t *p)
{
    if (p) {
        p->used = false;
    }
}

void box_gpu_tg_pool_sweep(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    bool all)
{
    int32_t i, kept = 0, count = ecs_vec_count(&tg->pool);
    for (i = 0; i < count; i ++) {
        box_gpu_tg_pooled_t *p = *ecs_vec_get_t(&tg->pool, box_gpu_tg_pooled_t*, i);
        if (!p->used && (all || tg->frame - p->last > BOX_GPU_TG_POOL_KEEP)) {
            gpu->texture_graph_pool_bytes -= (uint64_t)p->width * p->height *
                box_gpu_tg_texel_size(p->format);
            wgpuTextureViewRelease(p->view);
            wgpuTextureRelease(p->texture);
            ecs_os_free(p);
            continue;
        }

        *ecs_vec_get_t(&tg->pool, box_gpu_tg_pooled_t*, kept ++) = p;
    }

    ecs_vec_set_count_t(NULL, &tg->pool, box_gpu_tg_pooled_t*, kept);
}

void box_gpu_tg_poll(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg)
{
    box_render_readback_delivery_t delivery;
    int32_t i;
    bool mapping = false;
    for (i = 0; i < ecs_vec_count(&tg->reads); i ++) {
        mapping = mapping || (*ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i))->state ==
            BOX_GPU_TG_READ_MAPPING;
    }

    for (i = 0; i < BOX_GPU_TG_STAMPS; i ++) {
        mapping = mapping || tg->stamps[i].state == BOX_GPU_TG_STAMP_MAPPING;
    }

#ifndef __EMSCRIPTEN__
    if (mapping) {
        wgpuDevicePoll(gpu->device, false, NULL);
    }
#else
    (void)mapping;
#endif

    ecs_os_mutex_lock(gpu->lock);
    delivery = gpu->readbacks;
    ecs_os_mutex_unlock(gpu->lock);
    for (i = 0; i < ecs_vec_count(&tg->reads); i ++) {
        box_gpu_tg_read_t *r = *ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i);
        if (r->state == BOX_GPU_TG_READ_FAILED) {
            box_gpu_tg_read_fail(gpu, tg, i --);
        } else if (r->state == BOX_GPU_TG_READ_MAPPED &&
            delivery != BOX_RENDER_READBACK_HOLD)
        {
            r->read.texels = box_gpu_tg_texels(r);
            wgpuBufferUnmap(r->buffer);
            wgpuBufferRelease(r->buffer);
            r->buffer = NULL;
            box_gpu_tg_deliver(gpu, &r->read);
            ecs_os_free(r);
            ecs_vec_remove_ordered_t(&tg->reads, box_gpu_tg_read_t*, i --);
        }
    }

    for (i = 0; i < BOX_GPU_TG_STAMPS; i ++) {
        box_gpu_tg_stamp_t *st = &tg->stamps[i];
        if (st->state == BOX_GPU_TG_STAMP_MAPPED) {
            const uint64_t *t = wgpuBufferGetConstMappedRange(st->buffer, 0,
                2 * sizeof(uint64_t));
            double ns = t && t[1] > t[0] ? (double)(t[1] - t[0]) * box_gpu_tg_period(gpu) : 0;
            if (ns > 0 && ns < BOX_GPU_TG_MEASURE_MAX) {
                gpu->pending.texture_graph_ns += (int64_t)ns;
                gpu->pending.texture_graph_timed ++;
            }

            wgpuBufferUnmap(st->buffer);
            st->state = BOX_GPU_TG_STAMP_FREE;
        } else if (st->state == BOX_GPU_TG_STAMP_FAILED) {
            st->state = BOX_GPU_TG_STAMP_FREE;
        }
    }
}

void box_gpu_tg_requests(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg)
{
    ecs_vec_t requests;
    int32_t i, j;
    ecs_os_mutex_lock(gpu->lock);
    requests = gpu->graph_requests;
    ecs_vec_init_t(NULL, &gpu->graph_requests, box_render_texture_read_t, 0);
    ecs_os_mutex_unlock(gpu->lock);
    for (i = 0; i < ecs_vec_count(&requests); i ++) {
        box_render_texture_read_t *req = ecs_vec_get_t(&requests, box_render_texture_read_t, i);
        box_gpu_tg_graph_t *graph = ecs_map_get_deref(&tg->graphs, box_gpu_tg_graph_t,
            req->handle);
        box_gpu_tg_read_t *r;
        if (!graph || req->index >= (int32_t)graph->desc.node_count) {
            req->texels = NULL;
            box_gpu_tg_deliver(gpu, req);
            continue;
        }

        if (req->index == (int32_t)graph->desc.node_count - 1) {
            req->index = -1;
        }

        r = ecs_os_calloc_t(box_gpu_tg_read_t);
        r->read = *req;
        r->read.texels = NULL;
        r->state = BOX_GPU_TG_READ_WAITING;
        *ecs_vec_append_t(NULL, &tg->reads, box_gpu_tg_read_t*) = r;
        if (req->index >= 0) {
            bool found = false;
            int32_t *forced = ecs_vec_first(&graph->forced);
            int32_t count = ecs_vec_count(&graph->forced);
            for (j = 0; j < count; j ++) {
                found = found || forced[j] == req->index;
            }

            if (!found) {
                *ecs_vec_append_t(NULL, &graph->forced, int32_t) = req->index;
                forced = ecs_vec_first(&graph->forced);
                for (j = count; j > 0 && forced[j - 1] > forced[j]; j --) {
                    int32_t swap = forced[j];
                    forced[j] = forced[j - 1];
                    forced[j - 1] = swap;
                }

                graph->key = 0;
            }
        }
    }

    ecs_vec_fini_t(NULL, &requests, box_render_texture_read_t);
}

bool box_gpu_tg_node_reads(
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&tg->reads) && !result; i ++) {
        const box_gpu_tg_read_t *r = *ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i);
        result = r->read.handle == graph->handle && r->read.index >= 0 &&
            r->state == BOX_GPU_TG_READ_WAITING;
    }

    return result;
}

void box_gpu_tg_node_reads_fail(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph)
{
    int32_t i;
    for (i = ecs_vec_count(&tg->reads) - 1; i >= 0; i --) {
        const box_gpu_tg_read_t *r = *ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i);
        if (r->read.handle == graph->handle && r->read.index >= 0 &&
            r->state == BOX_GPU_TG_READ_WAITING)
        {
            box_gpu_tg_read_fail(gpu, tg, i);
        }
    }
}

void box_gpu_tg_copies(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    WGPUCommandEncoder encoder,
    const box_gpu_tg_eval_t *evals,
    int32_t eval_count)
{
    int32_t i, e;
    for (i = 0; i < ecs_vec_count(&tg->reads); i ++) {
        box_gpu_tg_read_t *r = *ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i);
        box_gpu_tg_graph_t *graph = ecs_map_get_deref(&tg->graphs, box_gpu_tg_graph_t,
            r->read.handle);
        const box_gpu_tg_eval_t *ev = NULL;
        box_gpu_texture_t *tex;
        if (r->state != BOX_GPU_TG_READ_WAITING || !graph) {
            continue;
        }

        for (e = 0; e < eval_count && !ev; e ++) {
            ev = evals[e].graph == graph && (evals[e].stored || evals[e].key >= 0) &&
                evals[e].key != BOX_GPU_TG_BLEND ? &evals[e] : NULL;
        }

        if (r->read.index >= 0) {
            box_gpu_tg_pooled_t *p = ev && r->read.index < graph->current->node_count
                ? ev->textures[r->read.index] : NULL;
            if (p) {
                uint32_t apron = graph->desc.apron;
                box_gpu_tg_copy(gpu, encoder, r, p->texture, p->format, apron,
                    p->width - 2 * apron, p->height - 2 * apron);
            }

            continue;
        }

        tex = ev || graph->written
            ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, graph->handle) : NULL;
        if (tex) {
            box_gpu_tg_copy(gpu, encoder, r, tex->texture,
                box_gpu_tg_image_format(graph->desc.format), 0,
                wgpuTextureGetWidth(tex->texture),
                wgpuTextureGetHeight(tex->texture));
        }
    }
}

void box_gpu_tg_stamps_init(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg)
{
    int32_t i;
    if (!gpu->timestamps || tg->queries) {
        goto done;
    }

    tg->queries = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
        .type = WGPUQueryType_Timestamp,
        .count = 2 * BOX_GPU_TG_STAMPS
    });
    tg->resolve = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = BOX_GPU_TG_STAMP_STRIDE * BOX_GPU_TG_STAMPS
    });
    for (i = 0; i < BOX_GPU_TG_STAMPS; i ++) {
        tg->stamps[i].buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = 2 * sizeof(uint64_t)
        });
    }

done:
    return;
}

void box_gpu_tg_stamps_resolve(
    box_gpu_texgraphs_t *tg,
    WGPUCommandEncoder encoder)
{
    int32_t i;
    for (i = 0; tg->queries && i < BOX_GPU_TG_STAMPS; i ++) {
        uint64_t offset = (uint64_t)i * BOX_GPU_TG_STAMP_STRIDE;
        if (tg->stamps[i].state == BOX_GPU_TG_STAMP_WRITTEN) {
            wgpuCommandEncoderResolveQuerySet(encoder, tg->queries, (uint32_t)i * 2, 2,
                tg->resolve, offset);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, tg->resolve, offset,
                tg->stamps[i].buffer, 0, 2 * sizeof(uint64_t));
            tg->stamps[i].state = BOX_GPU_TG_STAMP_COPIED;
        }
    }
}

void box_gpu_tg_map(
    box_gpu_texgraphs_t *tg)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&tg->reads); i ++) {
        box_gpu_tg_read_t *r = *ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i);
        if (r->state == BOX_GPU_TG_READ_COPIED) {
            r->state = BOX_GPU_TG_READ_MAPPING;
            wgpuBufferMapAsync(r->buffer, WGPUMapMode_Read, 0,
                (size_t)r->stride * (size_t)r->read.height, (WGPUBufferMapCallbackInfo){
                    .mode = WGPUCallbackMode_AllowSpontaneous,
                    .callback = box_gpu_tg_on_read,
                    .userdata1 = r
                });
        }
    }

    for (i = 0; i < BOX_GPU_TG_STAMPS; i ++) {
        if (tg->stamps[i].state == BOX_GPU_TG_STAMP_COPIED) {
            tg->stamps[i].state = BOX_GPU_TG_STAMP_MAPPING;
            wgpuBufferMapAsync(tg->stamps[i].buffer, WGPUMapMode_Read, 0,
                2 * sizeof(uint64_t), (WGPUBufferMapCallbackInfo){
                    .mode = WGPUCallbackMode_AllowSpontaneous,
                    .callback = box_gpu_tg_on_stamp,
                    .userdata1 = &tg->stamps[i]
                });
        }
    }
}

bool box_gpu_tg_busy(
    const box_gpu_texgraphs_t *tg)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < BOX_GPU_TG_STAMPS; i ++) {
        result = result || tg->stamps[i].state == BOX_GPU_TG_STAMP_WRITTEN;
    }

    for (i = 0; i < ecs_vec_count(&tg->reads); i ++) {
        result = result || (*ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i))->state ==
            BOX_GPU_TG_READ_WAITING;
    }

    return result;
}

bool box_render_gpu_texture_graph_results(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    box_render_gpu_stats_t stats;
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool result = box_render_gpu_stats(world, &stats);
    int32_t i;
    if (result) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        for (i = 0; i < ecs_vec_count(&gpu->graph_results); i ++) {
            *ecs_vec_append_t(NULL, out, box_render_texture_graph_result_t) =
                *ecs_vec_get_t(&gpu->graph_results, box_render_texture_graph_result_t, i);
        }

        ecs_vec_clear(&gpu->graph_results);
        ecs_os_mutex_unlock(gpu->lock);
    }

    return result;
}

bool box_render_gpu_texture_graph_read(
    ecs_world_t *world,
    uint32_t handle,
    int32_t node,
    uint32_t ticket)
{
    box_render_gpu_stats_t stats;
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool result = box_render_gpu_stats(world, &stats);
    if (result) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        *ecs_vec_append_t(NULL, &gpu->graph_requests, box_render_texture_read_t) =
            (box_render_texture_read_t){
                .handle = handle,
                .index = node < 0 ? -1 : node,
                .ticket = ticket
            };
        ecs_os_mutex_unlock(gpu->lock);
    }

    return result;
}

bool box_render_gpu_texture_graph_reads(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    box_render_gpu_stats_t stats;
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool result = box_render_gpu_stats(world, &stats);
    int32_t i;
    if (result) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        for (i = 0; i < ecs_vec_count(&gpu->graph_reads); i ++) {
            *ecs_vec_append_t(NULL, out, box_render_texture_read_t) =
                *ecs_vec_get_t(&gpu->graph_reads, box_render_texture_read_t, i);
        }

        ecs_vec_clear(&gpu->graph_reads);
        ecs_os_mutex_unlock(gpu->lock);
    }

    return result;
}

#else


bool box_render_gpu_texture_graph_results(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    (void)world;
    (void)out;

    return false;
}

bool box_render_gpu_texture_graph_read(
    ecs_world_t *world,
    uint32_t handle,
    int32_t node,
    uint32_t ticket)
{
    (void)world;
    (void)handle;
    (void)node;
    (void)ticket;

    return false;
}

bool box_render_gpu_texture_graph_reads(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    (void)world;
    (void)out;

    return false;
}


#endif
