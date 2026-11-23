/**
 * @file render/gpu_grass.c
 * @brief Grass passes of the webgpu backend: per lit view a compute pass places the blades of
 * the Grass entities of its scene around its camera, and its forward pass draws them with one
 * indirect draw per Grass and level of detail (grass_place.wgsl, grass.wgsl). The depth prepass
 * leaves them out: drawn in both, the blades cost their vertices twice for the few fragments the
 * prepass saves the forward pass. A view that culls by occlusion places its blades after the
 * depth pyramid of what it drew first (gpu_hiz.c) and draws them in its late forward pass, the
 * patches and blades behind it culled.
 *
 * Each Grass has a blade buffer with a range per level and the indirect arguments of its levels,
 * which every view that draws it fills again before its passes, its params in a uniform ring.
 * The blades placed and drawn per level and the GPU time of the passes are read back later.
 */

#include "gpu.h"
#include "gpu_grass.h"
#include <math.h>
#include <stdlib.h>

#ifdef BOX_RENDER_WGPU

WGPUBuffer box_gpu_grass_buffer(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    return wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = usage,
        .size = size
    });
}

/** Create the bind group layouts and pipeline layouts of the passes.
 *
 * @param gpu The backend
 * @param s The grass passes
 */
static void box_gpu_grass_layouts(
    box_gpu_t *gpu,
    box_gpu_grass_t *s)
{
    WGPUBindGroupLayoutEntry place[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_grass_params_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
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
    WGPUBindGroupLayoutEntry draw[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_grass_params_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutEntry hiz = {
        .binding = 0,
        .visibility = WGPUShaderStage_Compute,
        .texture = {
            .sampleType = WGPUTextureSampleType_UnfilterableFloat,
            .viewDimension = WGPUTextureViewDimension_2D
        }
    };
    s->place_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 4, .entries = place });
    s->hiz_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 1, .entries = &hiz });
    s->draw_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = draw });
    WGPUBindGroupLayout place_groups[3] = {
        gpu->terrain_sample_layout, s->place_layout, s->hiz_layout
    };
    s->place_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 3,
            .bindGroupLayouts = place_groups
        });
    WGPUBindGroupLayout draw_groups[2] = { gpu->view_layout, s->draw_layout };
    s->draw_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = draw_groups
        });
    s->empty_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 0 });
    s->empty_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->empty_layout,
        .entryCount = 0
    });
    WGPUBindGroupLayout shadow_groups[2] = { s->empty_layout, s->draw_layout };
    s->shadow_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = shadow_groups
        });
}

/** Create the timestamp queries and their resolve buffer when the device has the feature, and
 * the readbacks of the counts.
 *
 * @param gpu The backend
 * @param s The grass passes
 */
static void box_gpu_grass_readbacks_init(
    box_gpu_t *gpu,
    box_gpu_grass_t *s)
{
    int32_t i;
    s->current = -1;
    if (gpu->timestamps) {
        s->queries = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
            .type = WGPUQueryType_Timestamp,
            .count = BOX_GPU_GRASS_STAMPS * BOX_GPU_GRASS_READBACKS
        });
        s->resolve = box_gpu_grass_buffer(gpu,
            BOX_GPU_GRASS_STAMP_BYTES * BOX_GPU_GRASS_READBACKS,
            WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
    }

    for (i = 0; i < BOX_GPU_GRASS_READBACKS; i ++) {
        s->readbacks[i].buffer = box_gpu_grass_buffer(gpu, BOX_GPU_GRASS_READBACK_BYTES,
            WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    }
}

void box_gpu_grass_init(
    box_gpu_t *gpu)
{
    box_gpu_grass_t *s = ecs_os_calloc_t(box_gpu_grass_t);
    box_gpu_grass_layouts(gpu, s);
    box_gpu_grass_readbacks_init(gpu, s);
    s->stats = box_gpu_grass_buffer(gpu, BOX_GPU_GRASS_STATS,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    s->no_params = box_gpu_grass_buffer(gpu, sizeof(box_gpu_terrain_sample_params_t),
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    s->no_grid = box_gpu_grass_buffer(gpu, 16, WGPUBufferUsage_Storage);
    s->no_pyramid = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = WGPUTextureFormat_R32Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    s->no_pyramid_view = wgpuTextureCreateView(s->no_pyramid, NULL);
    s->no_hiz = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->hiz_layout,
        .entryCount = 1,
        .entries = &(WGPUBindGroupEntry){ .binding = 0, .textureView = s->no_pyramid_view }
    });
    ecs_map_init(&s->entities, NULL);
    gpu->grass = s;
}

/** Release the buffers and groups of a Grass entity and the entity.
 *
 * @param e The entity
 */
static void box_gpu_grass_entity_free(
    box_gpu_grass_entity_t *e)
{
    BOX_GPU_GRASS_RELEASE(e->place_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->draw_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->blades, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(e->counters, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_place_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_draw_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_blades, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_counters, wgpuBufferRelease);
    ecs_os_free(e);
}

void box_gpu_grass_draw_reset(
    box_gpu_grass_t *s)
{
    int32_t i;
    for (i = 0; i < s->draw_count; i ++) {
        wgpuRenderPipelineRelease(s->draws[i].pipeline);
    }

    s->draw_count = 0;
    BOX_GPU_GRASS_RELEASE(s->shadow, wgpuRenderPipelineRelease);
    BOX_GPU_GRASS_RELEASE(s->shadow_point, wgpuRenderPipelineRelease);
}

/** Release the compute pipelines, which are created again with the current place shader.
 *
 * @param s The grass passes
 */
static void box_gpu_grass_place_reset(
    box_gpu_grass_t *s)
{
    BOX_GPU_GRASS_RELEASE(s->reset, wgpuComputePipelineRelease);
    BOX_GPU_GRASS_RELEASE(s->place, wgpuComputePipelineRelease);
    BOX_GPU_GRASS_RELEASE(s->finish, wgpuComputePipelineRelease);
}

void box_gpu_grass_fini(
    box_gpu_t *gpu)
{
    box_gpu_grass_t *s = gpu->grass;
    int32_t i;
    if (!s) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&s->entities);
    while (ecs_map_next(&it)) {
        box_gpu_grass_entity_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&s->entities);
    box_gpu_grass_draw_reset(s);
    box_gpu_grass_place_reset(s);
    for (i = 0; i < BOX_GPU_GRASS_READBACKS; i ++) {
        BOX_GPU_GRASS_RELEASE(s->readbacks[i].buffer, wgpuBufferRelease);
    }

    BOX_GPU_GRASS_RELEASE(s->queries, wgpuQuerySetRelease);
    BOX_GPU_GRASS_RELEASE(s->resolve, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(s->no_terrain, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(s->no_params, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(s->no_grid, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(s->no_hiz, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(s->no_pyramid_view, wgpuTextureViewRelease);
    BOX_GPU_GRASS_RELEASE(s->no_pyramid, wgpuTextureRelease);
    BOX_GPU_GRASS_RELEASE(s->params, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(s->stats, wgpuBufferRelease);
    BOX_GPU_GRASS_RELEASE(s->place_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_GRASS_RELEASE(s->draw_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_GRASS_RELEASE(s->shadow_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_GRASS_RELEASE(s->empty_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(s->empty_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_GRASS_RELEASE(s->place_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_GRASS_RELEASE(s->hiz_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_GRASS_RELEASE(s->draw_layout, wgpuBindGroupLayoutRelease);
    ecs_os_free(s);
    gpu->grass = NULL;
done:
    return;
}

/** Get the nanoseconds of one tick of the timestamps.
 *
 * @param gpu The backend
 * @return The period, 1 in the browser, which resolves timestamps in nanoseconds
 */
static double box_gpu_grass_period(
    box_gpu_t *gpu)
{
#ifdef __EMSCRIPTEN__
    (void)gpu;

    return 1.0;
#else
    return (double)wgpuQueueGetTimestampPeriod(gpu->queue);
#endif
}

/** Record the outcome of the map of a readback.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_grass_readback_t
 * @param userdata2 Unused
 */
static void box_gpu_grass_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_grass_readback_t *r = userdata1;
    (void)message;
    (void)userdata2;
    if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        r->state = status == WGPUMapAsyncStatus_Success
            ? BOX_GPU_GRASS_MAPPED
            : BOX_GPU_GRASS_FAILED;
    }
}

/** Add the counts and the times of the passes of a mapped readback to the counters of the
 * backend.
 *
 * @param gpu The backend
 * @param r The readback
 */
static void box_gpu_grass_measured(
    box_gpu_t *gpu,
    box_gpu_grass_readback_t *r)
{
    const uint32_t *counts = wgpuBufferGetConstMappedRange(r->buffer, 0,
        BOX_GPU_GRASS_READBACK_BYTES);
    double period = box_gpu_grass_period(gpu);
    uint32_t i;
    if (!counts) {
        goto done;
    }

    for (i = 0; i < BOX_GRASS_LODS; i ++) {
        gpu->pending.grass_generated[i] += counts[i];
        gpu->pending.grass_drawn[i] += counts[BOX_GRASS_LODS + i];
    }

    gpu->pending.grass_culled_patches += counts[2 * BOX_GRASS_LODS];
    gpu->pending.grass_culled_blades += counts[2 * BOX_GRASS_LODS + 1];

    gpu->pending.grass_reads ++;
    const uint64_t *t = (const uint64_t*)ECS_OFFSET(counts, BOX_GPU_GRASS_STATS);
    for (i = 0; i + 1 < r->stamps; i += 2) {
        double ns = (double)(t[i + 1] - t[i]) * period;
        if (t[i + 1] > t[i] && ns < BOX_GPU_GRASS_MEASURE_MAX) {
            gpu->pending.grass_ns += (int64_t)ns;
            gpu->pending.grass_timed ++;
        }
    }

done:
    return;
}

/** Advance a readback by a packet: publish a mapped one, map a copied one and resolve the
 * timestamps of one the packet before wrote into it.
 *
 * Metal writes the timestamps of a pass when the pass completed, which a resolve in the same or
 * the next command buffer does not wait for, so the resolve waits for one more packet.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param i Index of the readback
 * @param encoder The encoder of the resolves, created by the first
 */
static void box_gpu_grass_readback_step(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    int32_t i,
    WGPUCommandEncoder *encoder)
{
    box_gpu_grass_readback_t *r = &s->readbacks[i];
    uint64_t offset = (uint64_t)i * BOX_GPU_GRASS_STAMP_BYTES;
    if (r->state == BOX_GPU_GRASS_MAPPED) {
        box_gpu_grass_measured(gpu, r);
        wgpuBufferUnmap(r->buffer);
        r->state = BOX_GPU_GRASS_FREE;
    } else if (r->state == BOX_GPU_GRASS_FAILED) {
        r->state = BOX_GPU_GRASS_FREE;
    } else if (r->state == BOX_GPU_GRASS_COPIED) {
        r->state = BOX_GPU_GRASS_MAPPING;
        wgpuBufferMapAsync(r->buffer, WGPUMapMode_Read, 0, BOX_GPU_GRASS_READBACK_BYTES,
            (WGPUBufferMapCallbackInfo){
                .mode = WGPUCallbackMode_AllowSpontaneous,
                .callback = box_gpu_grass_on_map,
                .userdata1 = r
            });
    } else if (r->state == BOX_GPU_GRASS_WRITTEN) {
        if (r->stamps && s->queries) {
            if (!*encoder) {
                *encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
            }

            wgpuCommandEncoderResolveQuerySet(*encoder, s->queries,
                (uint32_t)i * BOX_GPU_GRASS_STAMPS, r->stamps, s->resolve, offset);
            wgpuCommandEncoderCopyBufferToBuffer(*encoder, s->resolve, offset, r->buffer,
                BOX_GPU_GRASS_STATS, (uint64_t)r->stamps * sizeof(uint64_t));
        }

        r->state = BOX_GPU_GRASS_COPIED;
    }
}

/** Advance the readbacks by a packet and pick one for the packet when it has a Grass.
 *
 * @param gpu The backend
 * @param s The grass passes
 */
static void box_gpu_grass_readbacks(
    box_gpu_t *gpu,
    box_gpu_grass_t *s)
{
    WGPUCommandEncoder encoder = NULL;
    int32_t i;
#ifndef __EMSCRIPTEN__
    for (i = 0; i < BOX_GPU_GRASS_READBACKS; i ++) {
        if (s->readbacks[i].state == BOX_GPU_GRASS_MAPPING) {
            wgpuDevicePoll(gpu->device, false, NULL);
            break;
        }
    }
#endif

    for (i = 0; i < BOX_GPU_GRASS_READBACKS; i ++) {
        box_gpu_grass_readback_step(gpu, s, i, &encoder);
    }

    if (encoder) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

    s->current = -1;
    for (i = 0; s->active && i < BOX_GPU_GRASS_READBACKS; i ++) {
        if (s->readbacks[i].state == BOX_GPU_GRASS_FREE) {
            s->readbacks[i].state = BOX_GPU_GRASS_RECORDING;
            s->readbacks[i].stamps = 0;
            s->current = i;
            break;
        }
    }
}

/** Get the blades a level of a Grass holds: those of the full density within the end of level
 * 0 and those of the thinned field out to the end of each level, the most of them with room.
 *
 * @param g The Grass
 * @return The blades, a multiple of the workgroup size
 */
static uint32_t box_gpu_grass_capacity(
    const box_render_grass_t *g)
{
    double r0 = g->lod[0] > 1.0f ? g->lod[0] : 1.0;
    double r1 = g->lod[1] > r0 ? g->lod[1] : r0;
    double r2 = g->lod[2] > r1 ? g->lod[2] : r1;
    double rho = g->density, pi = 3.14159265358979;
    double near_ = pi * r0 * r0 * rho;
    double mid = 2.0 * pi * rho * r0 * r0 * log(r1 / r0);
    double far_ = 2.0 * pi * rho * r0 * r0 * log(r2 / r1);
    double most = near_ > mid ? near_ : mid;
    most = most > far_ ? most : far_;
    double blades = most * 1.25 + 1024.0;
    uint32_t result = blades > (double)BOX_GPU_GRASS_CAPACITY_MAX
        ? BOX_GPU_GRASS_CAPACITY_MAX : (uint32_t)blades;
    result = result < BOX_GPU_GRASS_CAPACITY_MIN ? BOX_GPU_GRASS_CAPACITY_MIN : result;

    return (result + BOX_GPU_GRASS_GROUP - 1u) / BOX_GPU_GRASS_GROUP * BOX_GPU_GRASS_GROUP;
}

/** Make the bind groups of a Grass entity again when what they bind changed.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param e The entity
 */
static void box_gpu_grass_entity_groups(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    box_gpu_grass_entity_t *e)
{
    uint64_t blades = (uint64_t)e->capacity * BOX_GRASS_LODS * sizeof(box_gpu_grass_blade_t);
    uint64_t shadows = (uint64_t)e->shadow_capacity * BOX_GRASS_LODS *
        sizeof(box_gpu_grass_blade_t);
    if (e->place_group && e->group_params == s->params &&
        (!e->shadow_blades || e->shadow_place_group))
    {
        goto done;
    }

    BOX_GPU_GRASS_RELEASE(e->place_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->draw_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_place_group, wgpuBindGroupRelease);
    BOX_GPU_GRASS_RELEASE(e->shadow_draw_group, wgpuBindGroupRelease);
    WGPUBindGroupEntry place[4] = {
        { .binding = 0, .buffer = s->params, .size = sizeof(box_gpu_grass_params_t) },
        { .binding = 1, .buffer = e->blades, .size = blades },
        { .binding = 2, .buffer = e->counters, .size = BOX_GPU_GRASS_COUNTERS },
        { .binding = 3, .buffer = s->stats, .size = BOX_GPU_GRASS_STATS }
    };
    e->place_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->place_layout,
        .entryCount = 4,
        .entries = place
    });
    e->draw_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->draw_layout,
        .entryCount = 2,
        .entries = place
    });
    if (e->shadow_blades) {
        WGPUBindGroupEntry shadow[4] = {
            { .binding = 0, .buffer = s->params, .size = sizeof(box_gpu_grass_params_t) },
            { .binding = 1, .buffer = e->shadow_blades, .size = shadows },
            { .binding = 2, .buffer = e->shadow_counters, .size = BOX_GPU_GRASS_COUNTERS },
            { .binding = 3, .buffer = s->stats, .size = BOX_GPU_GRASS_STATS }
        };
        e->shadow_place_group = wgpuDeviceCreateBindGroup(gpu->device,
            &(WGPUBindGroupDescriptor){
                .layout = s->place_layout,
                .entryCount = 4,
                .entries = shadow
            });
        e->shadow_draw_group = wgpuDeviceCreateBindGroup(gpu->device,
            &(WGPUBindGroupDescriptor){
                .layout = s->draw_layout,
                .entryCount = 2,
                .entries = shadow
            });
    }

    e->group_params = s->params;
done:
    return;
}

/** Get the buffers of a Grass of a packet, made again when it needs another capacity.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param packet The packet
 * @param g The Grass
 */
static void box_gpu_grass_entity(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    const box_render_packet_t *packet,
    const box_render_grass_t *g)
{
    box_gpu_grass_entity_t *e = ecs_map_get_deref(&s->entities, box_gpu_grass_entity_t,
        g->grass);
    uint32_t capacity = box_gpu_grass_capacity(g);
    if (!e) {
        e = ecs_os_calloc_t(box_gpu_grass_entity_t);
        ecs_map_insert_ptr(&s->entities, g->grass, e);
    }

    e->grass = *g;
    e->frame = packet->frame;
    if (!e->blades || e->capacity != capacity) {
        BOX_GPU_GRASS_RELEASE(e->place_group, wgpuBindGroupRelease);
        BOX_GPU_GRASS_RELEASE(e->draw_group, wgpuBindGroupRelease);
        BOX_GPU_GRASS_RELEASE(e->blades, wgpuBufferRelease);
        e->capacity = capacity;
        e->blades = box_gpu_grass_buffer(gpu,
            (uint64_t)capacity * BOX_GRASS_LODS * sizeof(box_gpu_grass_blade_t),
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        e->recorded = 0;
    }

    if (!e->counters) {
        e->counters = box_gpu_grass_buffer(gpu, BOX_GPU_GRASS_COUNTERS,
            WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect | WGPUBufferUsage_CopySrc);
    }

    box_gpu_grass_entity_shadows(gpu, e, box_gpu_grass_shadow_capacity(g));
}

/** Release the buffers of the Grass entities a packet no longer has.
 *
 * @param s The grass passes
 * @param packet The packet
 */
static void box_gpu_grass_sweep(
    box_gpu_grass_t *s,
    const box_render_packet_t *packet)
{
    ecs_entity_t stale[16];
    int32_t i, count;
    do {
        ecs_map_iter_t it = ecs_map_iter(&s->entities);
        count = 0;
        while (ecs_map_next(&it) && count < 16) {
            box_gpu_grass_entity_t *e = ecs_map_ptr(&it);
            if (e->frame != packet->frame) {
                stale[count ++] = ecs_map_key(&it);
            }
        }

        for (i = 0; i < count; i ++) {
            box_gpu_grass_entity_free(ecs_map_get_deref(&s->entities,
                box_gpu_grass_entity_t, stale[i]));
            ecs_map_remove(&s->entities, stale[i]);
        }
    } while (count);
}

/** Make the ring of params hold those of every Grass in every view of a packet, with those of
 * its shadows in the cascades of the view and in the light tiles, and the group of the sampled
 * terrain of a Grass on a plane.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param packet The packet
 */
static void box_gpu_grass_params_reserve(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    const box_render_packet_t *packet)
{
    uint32_t need = (uint32_t)(ecs_vec_count(&packet->grasses) *
        ((ecs_vec_count(&packet->views) + 1) * (2 + BOX_RENDER_SHADOW_CASCADES) + 1 +
        ecs_vec_count(&packet->shadow_tiles) * BOX_RENDER_SHADOW_OCTANTS));
    s->params_used = 0;
    if (!s->params || s->params_capacity < need) {
        uint32_t capacity = s->params_capacity ? s->params_capacity : 16u;
        while (capacity < need) {
            capacity *= 2u;
        }

        BOX_GPU_GRASS_RELEASE(s->params, wgpuBufferRelease);
        s->params = box_gpu_grass_buffer(gpu,
            (uint64_t)capacity * BOX_GPU_GRASS_PARAMS_STRIDE,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
        s->params_capacity = capacity;
    }

    if (!s->no_terrain) {
        WGPUBindGroupEntry entries[4] = {
            {
                .binding = 0,
                .buffer = s->no_params,
                .size = sizeof(box_gpu_terrain_sample_params_t)
            },
            { .binding = 1, .buffer = s->no_grid, .size = 16 },
            { .binding = 2, .buffer = gpu->no_assets, .size = WGPU_WHOLE_SIZE },
            { .binding = 3, .textureView = gpu->white->linear }
        };
        s->no_terrain = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = gpu->terrain_sample_layout,
            .entryCount = 4,
            .entries = entries
        });
    }
}

void box_gpu_grass_begin(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_grass_t *s = gpu->grass;
    int32_t i, count = ecs_vec_count(&packet->grasses);
    static const uint32_t zero[BOX_GPU_GRASS_STATS / sizeof(uint32_t)] = {0};
    if (!s) {
        goto done;
    }

    s->active = count > 0;
    box_gpu_grass_readbacks(gpu, s);
    for (i = 0; i < count; i ++) {
        box_gpu_grass_entity(gpu, s, packet,
            ecs_vec_get_t(&packet->grasses, box_render_grass_t, i));
    }

    box_gpu_grass_sweep(s, packet);
    if (!s->active) {
        goto done;
    }

    box_gpu_grass_params_reserve(gpu, s, packet);
    ecs_map_iter_t it = ecs_map_iter(&s->entities);
    while (ecs_map_next(&it)) {
        box_gpu_grass_entity_groups(gpu, s, ecs_map_ptr(&it));
    }

    wgpuQueueWriteBuffer(gpu->queue, s->stats, 0, zero, sizeof(zero));
done:
    return;
}

void box_gpu_grass_end(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_grass_t *s = gpu->grass;
    (void)packet;
    if (!s || s->current == -1) {
        goto done;
    }

    box_gpu_grass_readback_t *r = &s->readbacks[s->current];
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, s->stats, 0, r->buffer, 0,
        BOX_GPU_GRASS_STATS);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    r->state = BOX_GPU_GRASS_WRITTEN;
    s->current = -1;
done:
    return;
}

/** Create a compute pipeline of an entry point of the place shader.
 *
 * @param gpu The backend
 * @param module The module
 * @param entry The entry point
 * @return The pipeline, NULL while it is created
 */
static WGPUComputePipeline box_gpu_grass_compute(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    const char *entry)
{
    return box_gpu_compute_pipeline(gpu, &(WGPUComputePipelineDescriptor){
        .layout = gpu->grass->place_pipeline_layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    });
}

bool box_gpu_grass_place_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_grass_t *s = gpu->grass;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_GRASS_PLACE);
    if (!shader || !shader->module) {
        goto done;
    }

    if (s->place_revision != shader->revision) {
        box_gpu_grass_place_reset(s);
        s->place_revision = shader->revision;
    }

    if (!s->reset) {
        s->reset = box_gpu_grass_compute(gpu, shader->module, "reset");
    }

    if (!s->place) {
        s->place = box_gpu_grass_compute(gpu, shader->module, "place");
    }

    if (!s->finish) {
        s->finish = box_gpu_grass_compute(gpu, shader->module, "finish");
    }

done:
    return shader && shader->module && s->reset && s->place && s->finish;
}

/** Create a draw pipeline: the blades as triangle strips, both sides, into a color format,
 * writing their depth.
 *
 * @param gpu The backend
 * @param module The module of the draw shader
 * @param format Format of the color target
 * @return The pipeline, NULL while it is created
 */
static WGPURenderPipeline box_gpu_grass_draw_create(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    WGPUTextureFormat format)
{
    WGPUColorTargetState target = {
        .format = format,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = gpu->grass->draw_pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleStrip,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };

    return box_gpu_render_pipeline(gpu, &desc);
}

/** Get the draw pipeline of a color format, created on first use and again after the draw
 * shader recompiled.
 *
 * @param gpu The backend
 * @param format Format of the color target
 * @return The pipeline, NULL without a draw shader, while it is created or when too many
 * formats are in use
 */
static WGPURenderPipeline box_gpu_grass_draw_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_grass_t *s = gpu->grass;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_GRASS);
    WGPURenderPipeline result = NULL;
    int32_t i;
    if (!shader || !shader->module) {
        goto done;
    }

    if (s->draw_revision != shader->revision) {
        box_gpu_grass_draw_reset(s);
        s->draw_revision = shader->revision;
    }

    for (i = 0; i < s->draw_count; i ++) {
        if (s->draws[i].format == format) {
            result = s->draws[i].pipeline;
            goto done;
        }
    }

    if (s->draw_count < BOX_GPU_GRASS_DRAWS) {
        result = box_gpu_grass_draw_create(gpu, shader->module, format);
    }

    if (result) {
        s->draws[s->draw_count].format = format;
        s->draws[s->draw_count].pipeline = result;
        s->draw_count ++;
    }

done:
    return result;
}

bool box_gpu_grass_drawn(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    box_gpu_grass_t *s = gpu->grass;
    bool result = false;
    int32_t i, count = ecs_vec_count(&packet->grasses);
    if (!s || !s->active || !v->lights.enabled || v->impostor ||
        v->debug.wireframe == WireframeOnly || !box_camera_has_depth(&v->camera_data))
    {
        goto done;
    }

    for (i = 0; i < count && !result; i ++) {
        result = ecs_vec_get_t(&packet->grasses, box_render_grass_t, i)->scene == v->scene;
    }

done:
    return result;
}

/** Split an absolute coordinate into a patch of the grid.
 *
 * @param v The coordinate
 * @return The patch, clamped to int32_t
 */
static int32_t box_gpu_grass_patch(
    double v)
{
    double p = floor(v / (double)BOX_GPU_GRASS_PATCH);
    p = p < (double)INT32_MIN ? (double)INT32_MIN : p > (double)INT32_MAX ? (double)INT32_MAX : p;

    return (int32_t)p;
}

uint32_t box_gpu_grass_params(
    const box_render_packet_t *packet,
    const box_gpu_grass_entity_t *e,
    const float camera[3],
    float distance,
    uint32_t capacity,
    box_gpu_grass_params_t *out)
{
    const box_render_grass_t *g = &e->grass;
    double reach = ceil((double)distance / BOX_GPU_GRASS_PATCH);
    int32_t n = (int32_t)reach * 2 + 1, cells, half;
    n = n > BOX_GPU_GRASS_PATCHES_MAX ? BOX_GPU_GRASS_PATCHES_MAX : n;
    half = n / 2;
    cells = (int32_t)lround(BOX_GPU_GRASS_PATCH * sqrt((double)g->density));
    cells = cells < 1 ? 1 : cells > BOX_GPU_GRASS_CELLS_MAX ? BOX_GPU_GRASS_CELLS_MAX : cells;
    ecs_os_memset_t(out, 0, box_gpu_grass_params_t);
    ecs_os_memcpy_n(out->camera, camera, float, 3);
    out->camera[3] = distance;
    int32_t px = box_gpu_grass_patch(packet->origin[0] + (double)camera[0]) - half;
    int32_t pz = box_gpu_grass_patch(packet->origin[2] + (double)camera[2]) - half;
    out->patches[0] = px;
    out->patches[1] = pz;
    out->patches[2] = n;
    out->patches[3] = cells;
    out->corner[0] = (float)((double)px * BOX_GPU_GRASS_PATCH - packet->origin[0]);
    out->corner[1] = (float)((double)pz * BOX_GPU_GRASS_PATCH - packet->origin[2]);
    out->corner[2] = (float)BOX_GPU_GRASS_PATCH;
    out->corner[3] = (float)BOX_GPU_GRASS_PATCH / (float)cells;
    out->ground[0] = g->ground[0];
    out->ground[1] = g->ground[1];
    out->ground[2] = g->plane;
    out->ground[3] = g->lod[0];
    out->lod[0] = g->lod[0];
    out->lod[1] = g->lod[1];
    out->lod[2] = g->lod[2];
    out->lod[3] = g->lod[2] * 0.85f;
    out->blade[0] = g->height[0];
    out->blade[1] = g->height[1];
    out->blade[2] = g->width[0];
    out->blade[3] = g->width[1];
    ecs_os_memcpy_n(out->base, g->base, float, 3);
    ecs_os_memcpy_n(out->tip, g->tip, float, 3);
    out->config[0] = g->mask >= 0 ? (uint32_t)g->mask + 1u : 0u;
    out->config[1] = g->terrain ? 1u : 0u;
    out->config[2] = g->seed;
    out->config[3] = capacity;
    out->shape[0] = g->clump[0];
    out->shape[1] = g->clump[1];
    out->shape[2] = g->variation;
    out->shape[3] = g->fold;
    out->wind_field[0] = g->wind_direction[0];
    out->wind_field[1] = g->wind_direction[1];
    out->wind_field[2] = g->wind_offset[0];
    out->wind_field[3] = g->wind_offset[1];
    out->wind_strength[0] = g->wind_speed;
    out->wind_strength[1] = g->wind_gust;
    out->wind_strength[2] = g->wind_frequency;
    out->wind_strength[3] = g->wind_sway;
    out->wind_time[0] = g->wind_phase;

    return (uint32_t)n;
}

void box_gpu_grass_pixel(
    const box_gpu_view_uniforms_t *u,
    float out[4])
{
    if (u->lights[3]) {
        out[1] = 2.0f * u->cluster[1] * u->viewport[3];
    } else {
        out[0] = 2.0f * u->cluster[1] * u->viewport[3];
    }
}

/** Fill the params of a Grass in a view: its blades out to where they end, culled by the
 * frustum of the view, at least a pixel wide, and the shadows they cast in the view.
 *
 * @param packet The packet
 * @param e The Grass
 * @param v The view
 * @param u The uniforms of the view
 * @param out Receives the params
 * @return The patches along an edge of the grid
 */
static uint32_t box_gpu_grass_view_params(
    const box_render_packet_t *packet,
    const box_gpu_grass_entity_t *e,
    const box_render_view_t *v,
    const box_gpu_view_uniforms_t *u,
    box_gpu_grass_params_t *out)
{
    uint32_t n = box_gpu_grass_params(packet, e, u->camera, e->grass.lod[2], e->capacity, out);
    ecs_os_memcpy_n(out->view_proj, u->view_proj, float, 16);
    box_gpu_grass_pixel(u, out->pixel);

    if (box_render_grass_cascades(&e->grass, &v->shadows)) {
        out->caster[0] = e->grass.shadow;
        out->caster[1] = box_render_grass_shadow_fade(&e->grass);
    }

    return n;
}

const WGPUPassTimestampWrites* box_gpu_grass_writes(
    box_gpu_grass_t *s,
    WGPUPassTimestampWrites *writes)
{
    box_gpu_grass_readback_t *r = s->current != -1 ? &s->readbacks[s->current] : NULL;
    const WGPUPassTimestampWrites *result = NULL;
    if (r && s->queries && r->stamps + 2 <= BOX_GPU_GRASS_STAMPS) {
        uint32_t first = (uint32_t)s->current * BOX_GPU_GRASS_STAMPS + r->stamps;
        *writes = (WGPUPassTimestampWrites){
            .querySet = s->queries,
            .beginningOfPassWriteIndex = first,
            .endOfPassWriteIndex = first + 1
        };
        result = writes;
        r->stamps += 2;
    }

    return result;
}

WGPUComputePassEncoder box_gpu_grass_pass(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUCommandEncoder encoder)
{
    WGPUPassTimestampWrites writes;
    gpu->pending.grass_passes ++;

    return box_gpu_compute_begin(gpu, encoder, box_gpu_grass_writes(s, &writes), "grass");
}

bool box_gpu_grass_dispatch(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUComputePassEncoder pass,
    const box_gpu_grass_entity_t *e,
    const box_gpu_grass_params_t *params,
    uint32_t n,
    WGPUBindGroup group,
    uint32_t *offset)
{
    bool result = false;
    WGPUBindGroup terrain = e->grass.terrain
        ? box_gpu_terrain_sample_group(gpu, e->grass.terrain) : s->no_terrain;
    if (!terrain || !group || s->params_used >= s->params_capacity) {
        goto done;
    }

    *offset = s->params_used * BOX_GPU_GRASS_PARAMS_STRIDE;
    s->params_used ++;
    wgpuQueueWriteBuffer(gpu->queue, s->params, *offset, params, sizeof(*params));
    wgpuComputePassEncoderSetBindGroup(pass, 0, terrain, 0, NULL);
    wgpuComputePassEncoderSetBindGroup(pass, 1, group, 1, offset);
    wgpuComputePassEncoderSetBindGroup(pass, 2, s->hiz ? s->hiz : s->no_hiz, 0, NULL);
    wgpuComputePassEncoderSetPipeline(pass, s->reset);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderSetPipeline(pass, s->place);
    wgpuComputePassEncoderDispatchWorkgroups(pass, n, n, 1);
    wgpuComputePassEncoderSetPipeline(pass, s->finish);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    result = true;
done:
    return result;
}

/** Record the placement of the blades of a Grass in a compute pass of a view, culled by
 * occlusion against a depth pyramid.
 *
 * @param gpu The backend
 * @param s The grass passes, hiz binding the pyramid
 * @param pass The pass
 * @param packet The packet
 * @param e The Grass
 * @param v The view
 * @param u The uniforms of the view
 * @param hiz The pyramid, NULL to cull by none
 */
static void box_gpu_grass_place_blades(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUComputePassEncoder pass,
    const box_render_packet_t *packet,
    box_gpu_grass_entity_t *e,
    const box_render_view_t *v,
    const box_gpu_view_uniforms_t *u,
    const box_gpu_hiz_t *hiz)
{
    box_gpu_grass_params_t params;
    uint32_t n = box_gpu_grass_view_params(packet, e, v, u, &params), offset = 0;
    if (hiz) {
        params.hiz[0] = hiz->width;
        params.hiz[1] = hiz->height;
        params.hiz[2] = hiz->levels;
    }

    if (box_gpu_grass_dispatch(gpu, s, pass, e, &params, n, e->place_group, &offset)) {
        e->recorded = s->recording;
        e->offset = offset;
        e->late = hiz != NULL;
    }
}

/** Get the uniforms of a view.
 *
 * @param gpu The backend
 * @param slot Slot of the view in the view buffer
 * @return Its uniforms in view_data
 */
static const box_gpu_view_uniforms_t* box_gpu_grass_uniforms(
    box_gpu_t *gpu,
    int32_t slot)
{
    return (const box_gpu_view_uniforms_t*)ecs_vec_get_t(
        &gpu->view_data, uint8_t, slot * BOX_GPU_VIEW_STRIDE);
}

void box_gpu_grass_record(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t slot,
    bool prepass)
{
    box_gpu_grass_t *s = gpu->grass;
    WGPUComputePassEncoder pass = NULL;
    if (s) {
        s->view = NULL;
        s->shadows_late = false;
    }

    if (!box_gpu_grass_drawn(gpu, packet, v) || !box_gpu_grass_place_pipelines(gpu)) {
        goto done;
    }

    const box_gpu_view_uniforms_t *u = box_gpu_grass_uniforms(gpu, slot);
    bool late = box_gpu_cull_occludes(gpu, slot);
    s->recording ++;
    s->view = v;
    s->shadows_late = late && prepass;
    ecs_map_iter_t it = ecs_map_iter(&s->entities);
    while (ecs_map_next(&it)) {
        box_gpu_grass_entity_t *e = ecs_map_ptr(&it);
        if (e->frame != packet->frame || e->grass.scene != v->scene) {
            continue;
        }

        if (late && (s->shadows_late || !e->shadow_place_group ||
            !box_render_grass_cascades(&e->grass, &v->shadows)))
        {
            continue;
        }

        if (!pass) {
            pass = box_gpu_grass_pass(gpu, s, encoder);
        }

        if (!late) {
            box_gpu_grass_place_blades(gpu, s, pass, packet, e, v, u, NULL);
        }

        box_gpu_grass_place_shadows(gpu, s, pass, packet, e, v, u);
    }

    if (pass) {
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

done:
    return;
}

void box_gpu_grass_record_late(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t slot)
{
    box_gpu_grass_t *s = gpu->grass;
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    WGPUComputePassEncoder pass = NULL;
    if (!s || s->view != v || !cv || !cv->hiz || !box_gpu_grass_place_pipelines(gpu)) {
        goto done;
    }

    const box_gpu_view_uniforms_t *u = box_gpu_grass_uniforms(gpu, slot);
    s->hiz = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->hiz_layout,
        .entryCount = 1,
        .entries = &(WGPUBindGroupEntry){ .binding = 0, .textureView = cv->hiz->view }
    });
    ecs_map_iter_t it = ecs_map_iter(&s->entities);
    while (ecs_map_next(&it)) {
        box_gpu_grass_entity_t *e = ecs_map_ptr(&it);
        if (e->frame != packet->frame || e->grass.scene != v->scene) {
            continue;
        }

        if (!pass) {
            pass = box_gpu_grass_pass(gpu, s, encoder);
        }

        box_gpu_grass_place_blades(gpu, s, pass, packet, e, v, u, cv->hiz);
        if (s->shadows_late) {
            box_gpu_grass_place_shadows(gpu, s, pass, packet, e, v, u);
        }
    }

    if (pass) {
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

    BOX_GPU_GRASS_RELEASE(s->hiz, wgpuBindGroupRelease);
done:
    return;
}

bool box_gpu_grass_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureFormat format,
    int32_t phase)
{
    box_gpu_grass_t *s = gpu->grass;
    WGPURenderPipeline pipeline = NULL;
    bool result = false;
    uint32_t l;
    if (!box_gpu_grass_drawn(gpu, packet, v)) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&s->entities);
    while (ecs_map_next(&it)) {
        box_gpu_grass_entity_t *e = ecs_map_ptr(&it);
        if (e->recorded != s->recording || !e->recorded || !e->draw_group ||
            (phase != BOX_GPU_GRASS_ALL && e->late != (phase == BOX_GPU_GRASS_LATE)))
        {
            continue;
        }

        if (!pipeline) {
            pipeline = box_gpu_grass_draw_pipeline(gpu, format);
            if (!pipeline) {
                goto done;
            }

            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        }

        wgpuRenderPassEncoderSetBindGroup(pass, 1, e->draw_group, 1, &e->offset);
        for (l = 0; l < BOX_GRASS_LODS; l ++) {
            wgpuRenderPassEncoderDrawIndirect(pass, e->counters, (uint64_t)l * 16u);
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }

        result = true;
    }

done:
    return result;
}

/** Read back the blades of a level of a Grass.
 *
 * @param gpu The backend
 * @param e The Grass
 * @param level The level
 * @param count Its blades
 * @param out Receives box_render_grass_blade_t
 * @return Whether they were read
 */
static bool box_gpu_grass_read_level(
    box_gpu_t *gpu,
    const box_gpu_grass_entity_t *e,
    uint32_t level,
    uint32_t count,
    ecs_vec_t *out)
{
    bool result = true;
    uint32_t i;
    if (!count) {
        goto done;
    }

    box_gpu_grass_blade_t *blades = ecs_os_malloc_n(box_gpu_grass_blade_t, (int32_t)count);
    result = box_gpu_read(gpu, e->blades,
        level * e->capacity * (uint32_t)sizeof(box_gpu_grass_blade_t),
        count * (uint32_t)sizeof(box_gpu_grass_blade_t), blades);
    for (i = 0; result && i < count; i ++) {
        box_render_grass_blade_t *b = ecs_vec_append_t(NULL, out, box_render_grass_blade_t);
        ecs_os_memcpy_n(b->position, blades[i].position, float, 3);
        b->yaw = blades[i].yaw;
        b->height = blades[i].height;
        b->width = blades[i].width;
        b->tint[0] = 2.0f * (float)(blades[i].tint & 255u) / 255.0f;
        b->tint[1] = 2.0f * (float)((blades[i].tint >> 8) & 255u) / 255.0f;
        b->tint[2] = 2.0f * (float)((blades[i].tint >> 16) & 255u) / 255.0f;
        b->stiffness = (float)((blades[i].tint >> 24) & 15u) / 15.0f;
        b->phase = (float)(blades[i].tint >> 28) / 16.0f;
        b->level = (int32_t)level;
    }

    ecs_os_free(blades);
done:
    return result;
}

bool box_render_gpu_grass_blades(
    ecs_world_t *world,
    ecs_entity_t grass,
    ecs_vec_t *out)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    uint32_t counters[BOX_GPU_GRASS_COUNTERS / sizeof(uint32_t)], l;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    ecs_assert(ecs_os_thread_self() == gpu->thread, ECS_INVALID_OPERATION,
        "box_render_gpu_grass_blades needs RenderSettings.main_thread and box_render_sync");
    box_gpu_grass_entity_t *e = gpu->grass ? ecs_map_get_deref(&gpu->grass->entities,
        box_gpu_grass_entity_t, grass) : NULL;
    if (!e || !e->recorded || !e->counters) {
        goto done;
    }

    result = box_gpu_read(gpu, e->counters, 0, BOX_GPU_GRASS_COUNTERS, counters);
    for (l = 0; result && l < BOX_GRASS_LODS; l ++) {
        uint32_t count = counters[l * 4u + 1u];
        count = count > e->capacity ? e->capacity : count;
        result = box_gpu_grass_read_level(gpu, e, l, count, out);
    }

done:
    return result;
}

#else

bool box_render_gpu_grass_blades(
    ecs_world_t *world,
    ecs_entity_t grass,
    ecs_vec_t *out)
{
    (void)world;
    (void)grass;
    (void)out;

    return false;
}

#endif
