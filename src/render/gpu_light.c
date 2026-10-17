/**
 * @file render/gpu_light.c
 * @brief Light uniforms, the cluster pass and the light and reflection probe bindings of the
 * forward pass.
 *
 * Every view with point or spot lights or reflection probes runs etc/shaders/cluster.wgsl before
 * it draws: it lists, per cluster of the view frustum, the lights that reach it and the probes
 * that overlap it in one shared index list.
 *
 * The list starts from a budget and grows to what the pass allocated, read back a few frames
 * later.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Clusters across the image */
#define BOX_GPU_CLUSTER_X (16)

/** Clusters down the image */
#define BOX_GPU_CLUSTER_Y (9)

/** Depth slices of the view frustum */
#define BOX_GPU_CLUSTER_Z (24)

/** Clusters per axis of one workgroup of the cluster shader */
#define BOX_GPU_CLUSTER_GROUP (4)

/** Clusters of a view */
#define BOX_GPU_CLUSTER_COUNT ((uint64_t)BOX_GPU_CLUSTER_X * BOX_GPU_CLUSTER_Y * BOX_GPU_CLUSTER_Z)

/** Words before the index list: the allocated entries, then offset and count word per cluster */
#define BOX_GPU_CLUSTER_HEADER (1 + 2 * BOX_GPU_CLUSTER_COUNT)

/** Largest cluster buffer, the default maxStorageBufferBindingSize of WebGPU */
#define BOX_GPU_CLUSTER_BYTES_MAX ((uint64_t)128 * 1024 * 1024)

/** Point and spot lights per cluster the light index list of a view starts from at most */
#define BOX_GPU_CLUSTER_BUDGET_LIGHTS (32)

/** Reflection probes a cluster lists at most, CLUSTER_PROBES of etc/shaders/include/lights.wgsl */
#define BOX_GPU_CLUSTER_PROBES (8)

/** Entries the index list holds at most within BOX_GPU_CLUSTER_BYTES_MAX */
#define BOX_GPU_CLUSTER_ENTRIES_MAX (BOX_GPU_CLUSTER_BYTES_MAX / sizeof(uint32_t) - \
    BOX_GPU_CLUSTER_HEADER)

/** State of the read back of the entries a cluster pass allocated */
typedef enum box_gpu_cluster_readback_t {
    BOX_GPU_CLUSTER_FREE,       /**< No copy in flight */
    BOX_GPU_CLUSTER_COPIED,     /**< The copy is recorded, mapped once a later frame starts */
    BOX_GPU_CLUSTER_MAPPING,    /**< The copy is being mapped */
    BOX_GPU_CLUSTER_MAPPED,     /**< The copy can be read */
    BOX_GPU_CLUSTER_FAILED      /**< The map failed */
} box_gpu_cluster_readback_t;

/** Get the bytes of a cluster buffer.
 *
 * @param entries Entries of its index list
 * @return The bytes
 */
static uint64_t box_gpu_cluster_bytes(
    uint64_t entries)
{
    return (BOX_GPU_CLUSTER_HEADER + entries) * sizeof(uint32_t);
}

/** Get the reflection probes a cluster lists at most.
 *
 * @param probes Reflection probes the clusters of a view list
 * @return The probes, at most BOX_GPU_CLUSTER_PROBES
 */
static uint64_t box_gpu_cluster_probes(
    uint64_t probes)
{
    return probes < BOX_GPU_CLUSTER_PROBES ? probes : BOX_GPU_CLUSTER_PROBES;
}

/** Get the entries an index list holds when every light reaches every cluster and every
 * cluster lists its most probes.
 *
 * @param lights Point and spot lights
 * @param probes Reflection probes the clusters list
 * @return The entries, at most BOX_GPU_CLUSTER_ENTRIES_MAX
 */
static uint64_t box_gpu_cluster_limit(
    uint64_t lights,
    uint64_t probes)
{
    uint64_t result = BOX_GPU_CLUSTER_COUNT * (lights + box_gpu_cluster_probes(probes));

    return result < BOX_GPU_CLUSTER_ENTRIES_MAX ? result : BOX_GPU_CLUSTER_ENTRIES_MAX;
}

/** Get the entries an index list starts from.
 *
 * @param lights Point and spot lights
 * @param probes Reflection probes the clusters list
 * @return BOX_GPU_CLUSTER_BUDGET_LIGHTS or fewer lights and the most probes in every cluster
 */
static uint64_t box_gpu_cluster_budget(
    uint64_t lights,
    uint64_t probes)
{
    return box_gpu_cluster_limit(
        lights < BOX_GPU_CLUSTER_BUDGET_LIGHTS ? lights : BOX_GPU_CLUSTER_BUDGET_LIGHTS, probes);
}

/** Round entries up to a power of two, within a limit.
 *
 * @param entries Entries
 * @param limit Largest result
 * @return The entries rounded up, at most limit
 */
static uint64_t box_gpu_cluster_round(
    uint64_t entries,
    uint64_t limit)
{
    uint64_t result = 1;
    while (result < entries) {
        result <<= 1;
    }

    return result < limit ? result : limit;
}

/** Create a cluster buffer.
 *
 * @param gpu The backend
 * @param bytes Bytes of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_cluster_buffer_new(
    box_gpu_t *gpu,
    uint64_t bytes)
{
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
        .size = bytes
    };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

void box_gpu_light_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_view_uniforms_t)
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
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 4, .entries = entries };
    WGPUBufferDescriptor empty = {
        .usage = WGPUBufferUsage_Storage,
        .size = sizeof(box_render_light_t)
    };
    WGPUBufferDescriptor no_probes = {
        .usage = WGPUBufferUsage_Storage,
        .size = sizeof(box_render_reflection_probe_t)
    };
    gpu->cluster_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    gpu->cluster_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu->cluster_layout
        });
    gpu->no_lights = wgpuDeviceCreateBuffer(gpu->device, &empty);
    gpu->no_probes = wgpuDeviceCreateBuffer(gpu->device, &no_probes);
    ecs_vec_init_t(NULL, &gpu->clusters, box_gpu_cluster_t, 0);
}

void box_gpu_light_fini(
    box_gpu_t *gpu)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&gpu->clusters); i ++) {
        box_gpu_cluster_t *c = ecs_vec_get_t(&gpu->clusters, box_gpu_cluster_t, i);
        if (c->buffer) {
            wgpuBufferRelease(c->buffer);
        }

        if (c->readback) {
            wgpuBufferRelease(c->readback);
        }
    }

    ecs_vec_fini_t(NULL, &gpu->clusters, box_gpu_cluster_t);
    if (gpu->cluster) {
        wgpuComputePipelineRelease(gpu->cluster);
        gpu->cluster = NULL;
    }

    if (gpu->no_lights) {
        wgpuBufferRelease(gpu->no_lights);
        gpu->no_lights = NULL;
    }

    if (gpu->no_probes) {
        wgpuBufferRelease(gpu->no_probes);
        gpu->no_probes = NULL;
    }

    if (gpu->cluster_pipeline_layout) {
        wgpuPipelineLayoutRelease(gpu->cluster_pipeline_layout);
        gpu->cluster_pipeline_layout = NULL;
    }

    if (gpu->cluster_layout) {
        wgpuBindGroupLayoutRelease(gpu->cluster_layout);
        gpu->cluster_layout = NULL;
    }
}

/** Get the light buffer of a view.
 *
 * @param gpu The backend
 * @param v The view
 * @return The buffer, NULL when the backend does not have it
 */
static box_gpu_buffer_t* box_gpu_light_buffer(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    return v->lights.buffer
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, v->lights.buffer)
        : NULL;
}

/** Get the reflection probe buffer of a view.
 *
 * @param gpu The backend
 * @param v The view
 * @return The buffer, NULL when the view has none or the backend does not have it
 */
static box_gpu_buffer_t* box_gpu_probe_buffer(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    return v->reflections
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, v->reflections)
        : NULL;
}

/** Count the reflection probes the clusters of a view list.
 *
 * @param gpu The backend
 * @param v The view
 * @return The probes before the scene probe that the reflection probe buffer holds, 0 when
 *         the view is unlit
 */
static uint32_t box_gpu_cluster_probe_count(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    box_gpu_buffer_t *b = box_gpu_probe_buffer(gpu, v);
    uint32_t capacity = b ? b->size / (uint32_t)sizeof(box_render_reflection_probe_t) : 0;

    return v->lights.enabled ? glm_min(v->reflection_listed, capacity) : 0;
}

void box_gpu_light_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    mat4 proj,
    box_gpu_view_uniforms_t *u)
{
    const Camera *c = &v->camera_data;
    const box_render_view_lights_t *l = &v->lights;
    box_gpu_buffer_t *b = box_gpu_light_buffer(gpu, v);
    uint32_t capacity = b ? b->size / (uint32_t)sizeof(box_render_light_t) : 0;
    float near_ = glm_max(c->near_, 1e-3f);
    float far_ = c->far_ > near_ ? c->far_ : glm_max(BOX_CAMERA_FAR, near_ * 2.0f);
    glm_mat4_copy(ECS_CONST_CAST(vec4*, v->view_matrix), u->view);
    glm_vec4_copy(ECS_CONST_CAST(float*, l->ambient_sky), u->ambient_sky);
    glm_vec4_copy(ECS_CONST_CAST(float*, l->ambient_ground), u->ambient_ground);
    u->cluster[0] = proj[0][0] != 0 ? 1.0f / proj[0][0] : 1.0f;
    u->cluster[1] = proj[1][1] != 0 ? 1.0f / proj[1][1] : 1.0f;
    u->cluster[2] = near_;
    u->cluster[3] = far_;
    u->lights[1] = glm_min(l->count, capacity);
    u->lights[0] = glm_min(l->directional, u->lights[1]);
    u->lights[2] = l->enabled ? 1u : 0u;
    u->lights[3] = c->ortho ? 1u : 0u;
    u->grid[0] = BOX_GPU_CLUSTER_X;
    u->grid[1] = BOX_GPU_CLUSTER_Y;
    u->grid[2] = BOX_GPU_CLUSTER_Z;
    u->grid[3] = box_gpu_cluster_probe_count(gpu, v);
    if (box_gpu_reflect_of(gpu, v)) {
        box_gpu_buffer_t *p = box_gpu_probe_buffer(gpu, v);
        uint32_t probes = p->size / (uint32_t)sizeof(box_render_reflection_probe_t);
        u->reflection[0] = glm_min(v->reflection_count, probes);
    }
}

/** Count the point and spot lights a view shades with.
 *
 * @param gpu The backend
 * @param v The view
 * @return The lights after the directional ones that the light buffer holds
 */
static uint64_t box_gpu_cluster_lights(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    box_gpu_buffer_t *b = box_gpu_light_buffer(gpu, v);
    uint32_t capacity = b ? b->size / (uint32_t)sizeof(box_render_light_t) : 0;
    uint32_t count = glm_min(v->lights.count, capacity);

    return count > v->lights.directional ? count - v->lights.directional : 0;
}

/** Check whether a view runs the cluster pass.
 *
 * @param gpu The backend
 * @param v The view
 * @return Whether the view is lit and has point or spot lights or probes the clusters list
 */
static bool box_gpu_cluster_runs(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    return v->lights.enabled &&
        (box_gpu_cluster_lights(gpu, v) || box_gpu_cluster_probe_count(gpu, v));
}

/** Get the cluster state of a view slot, created on first use.
 *
 * @param gpu The backend
 * @param slot Slot of the view in the view buffer
 * @return The state
 */
static box_gpu_cluster_t* box_gpu_cluster_slot(
    box_gpu_t *gpu,
    int32_t slot)
{
    while (ecs_vec_count(&gpu->clusters) <= slot) {
        *ecs_vec_append_t(NULL, &gpu->clusters, box_gpu_cluster_t) = (box_gpu_cluster_t){0};
    }

    return ecs_vec_get_t(&gpu->clusters, box_gpu_cluster_t, slot);
}

/** Replace the cluster buffer of a view slot with one of another size.
 *
 * @param gpu The backend
 * @param c The cluster state of the slot
 * @param entries Entries of the new index list
 */
static void box_gpu_cluster_resize(
    box_gpu_t *gpu,
    box_gpu_cluster_t *c,
    uint64_t entries)
{
    if (c->buffer) {
        wgpuBufferRelease(c->buffer);
    }

    c->buffer = box_gpu_cluster_buffer_new(gpu, box_gpu_cluster_bytes(entries));
    c->entries = entries;
}

/** Receive the map of the read back of a cluster pass.
 *
 * @param status Whether the map succeeded
 * @param message Unused
 * @param userdata1 The backend
 * @param userdata2 Slot of the view, as an integer
 */
static void box_gpu_cluster_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_t *gpu = userdata1;
    box_gpu_cluster_t *c = ecs_vec_get_t(
        &gpu->clusters, box_gpu_cluster_t, (int32_t)(intptr_t)userdata2);
    (void)message;
    c->state = status == WGPUMapAsyncStatus_Success
        ? BOX_GPU_CLUSTER_MAPPED
        : BOX_GPU_CLUSTER_FAILED;
}

/** Resize the index list of a slot to the entries its last read back pass allocated.
 *
 * A list that was too small grows to the next power of two and reports the cut, since the
 * clusters past its end missed lights or probes until then; a list four times too large
 * shrinks.
 *
 * @param gpu The backend
 * @param c The cluster state of the slot
 * @param allocated Entries the pass allocated
 * @param lights Point and spot lights of the view
 * @param probes Reflection probes the clusters of the view list
 */
static void box_gpu_cluster_fit(
    box_gpu_t *gpu,
    box_gpu_cluster_t *c,
    uint64_t allocated,
    uint64_t lights,
    uint64_t probes)
{
    uint64_t limit = box_gpu_cluster_limit(lights, probes);
    uint64_t budget = box_gpu_cluster_budget(lights, probes);
    bool cut = allocated > c->entries;
    if (cut) {
        uint64_t grown = box_gpu_cluster_round(allocated, limit);
        if (!c->cut) {
            ecs_warn("the clusters of a view needed %u light and probe index entries but held "
                "%u, so some clusters missed lights or reflection probes; the list %s %u "
                "entries", (uint32_t)allocated,
                (uint32_t)c->entries, grown > c->entries ? "grows to" : "stays at its largest,",
                (uint32_t)(grown > c->entries ? grown : c->entries));
        }

        if (grown > c->entries) {
            box_gpu_cluster_resize(gpu, c, grown);
        }
    } else if (allocated * 4 < c->entries) {
        uint64_t shrunk = box_gpu_cluster_round(allocated * 2, limit);
        shrunk = shrunk > budget ? shrunk : budget;
        if (shrunk < c->entries) {
            box_gpu_cluster_resize(gpu, c, shrunk);
        }
    }

    c->cut = cut;
}

/** Advance the read back of the entries the cluster pass of a slot allocated.
 *
 * @param gpu The backend
 * @param c The cluster state of the slot
 * @param slot Slot of the view
 * @param lights Point and spot lights of the view
 * @param probes Reflection probes the clusters of the view list
 */
static void box_gpu_cluster_poll(
    box_gpu_t *gpu,
    box_gpu_cluster_t *c,
    int32_t slot,
    uint64_t lights,
    uint64_t probes)
{
#ifndef __EMSCRIPTEN__
    if (c->state == BOX_GPU_CLUSTER_MAPPING) {
        wgpuDevicePoll(gpu->device, false, NULL);
    }
#endif

    if (c->state == BOX_GPU_CLUSTER_MAPPED) {
        const uint32_t *allocated = wgpuBufferGetConstMappedRange(
            c->readback, 0, sizeof(uint32_t));
        uint64_t value = allocated ? *allocated : 0;
        wgpuBufferUnmap(c->readback);
        c->state = BOX_GPU_CLUSTER_FREE;
        box_gpu_cluster_fit(gpu, c, value, lights, probes);
    } else if (c->state == BOX_GPU_CLUSTER_FAILED) {
        c->state = BOX_GPU_CLUSTER_FREE;
    } else if (c->state == BOX_GPU_CLUSTER_COPIED && gpu->cull.frame > c->frame) {
        c->state = BOX_GPU_CLUSTER_MAPPING;
        wgpuBufferMapAsync(c->readback, WGPUMapMode_Read, 0, sizeof(uint32_t),
            (WGPUBufferMapCallbackInfo){
                .mode = WGPUCallbackMode_AllowSpontaneous,
                .callback = box_gpu_cluster_on_map,
                .userdata1 = gpu,
                .userdata2 = (void*)(intptr_t)slot
            });
    }
}

/** Get the cluster buffer of a view slot, holding at least the budget of the view's lights and
 * probes.
 *
 * @param gpu The backend
 * @param slot Slot of the view in the view buffer
 * @param v The view
 * @return The buffer
 */
static WGPUBuffer box_gpu_cluster_buffer(
    box_gpu_t *gpu,
    int32_t slot,
    const box_render_view_t *v)
{
    box_gpu_cluster_t *c = box_gpu_cluster_slot(gpu, slot);
    uint64_t lights = box_gpu_cluster_lights(gpu, v);
    uint64_t probes = box_gpu_cluster_probe_count(gpu, v);
    uint64_t budget = box_gpu_cluster_budget(lights, probes);
    uint64_t limit = box_gpu_cluster_limit(lights, probes);
    if (!c->buffer || c->entries < budget) {
        box_gpu_cluster_resize(gpu, c, budget);
    } else if (c->entries > limit) {
        box_gpu_cluster_resize(gpu, c, limit);
    }

    return c->buffer;
}

/** Get the cluster pipeline, created again when the cluster shader was recompiled.
 *
 * @param gpu The backend
 * @return The pipeline, NULL while the shader has no module
 */
static WGPUComputePipeline box_gpu_cluster_pipeline(
    box_gpu_t *gpu)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_CLUSTER);
    if (!s || !s->module) {
        goto done;
    }

    if (gpu->cluster && gpu->cluster_revision == s->revision) {
        goto done;
    }

    if (gpu->cluster) {
        wgpuComputePipelineRelease(gpu->cluster);
    }

    WGPUComputePipelineDescriptor desc = {
        .layout = gpu->cluster_pipeline_layout,
        .compute = {
            .module = s->module,
            .entryPoint = { .data = "main", .length = WGPU_STRLEN }
        }
    };
    gpu->cluster = wgpuDeviceCreateComputePipeline(gpu->device, &desc);
    gpu->cluster_revision = s->revision;
done:
    return s && s->module ? gpu->cluster : NULL;
}

WGPUBuffer box_gpu_light_binding(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    uint64_t *size)
{
    box_gpu_buffer_t *b = box_gpu_light_buffer(gpu, v);
    *size = b ? b->size : sizeof(box_render_light_t);

    return b ? b->buffer : gpu->no_lights;
}

void box_gpu_light_cluster_with(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v,
    WGPUBuffer uniforms,
    uint32_t offset,
    WGPUBuffer clusters)
{
    box_gpu_buffer_t *p = box_gpu_cluster_probe_count(gpu, v)
        ? box_gpu_probe_buffer(gpu, v)
        : NULL;
    WGPUComputePipeline pipeline = NULL;
    uint64_t light_size = 0;
    if (!box_gpu_cluster_runs(gpu, v)) {
        goto done;
    }

    pipeline = box_gpu_cluster_pipeline(gpu);
    if (!pipeline) {
        goto done;
    }

    WGPUBuffer lights = box_gpu_light_binding(gpu, v, &light_size);
    uint64_t size = wgpuBufferGetSize(clusters);
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = uniforms, .size = sizeof(box_gpu_view_uniforms_t) },
        { .binding = 1, .buffer = lights, .size = light_size },
        { .binding = 2, .buffer = clusters, .size = size },
        {
            .binding = 3,
            .buffer = p ? p->buffer : gpu->no_probes,
            .size = p ? p->size : sizeof(box_render_reflection_probe_t)
        }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->cluster_layout,
        .entryCount = 4,
        .entries = entries
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    wgpuCommandEncoderClearBuffer(encoder, clusters, 0, sizeof(uint32_t));
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuComputePassEncoderDispatchWorkgroups(pass,
        (BOX_GPU_CLUSTER_X + BOX_GPU_CLUSTER_GROUP - 1) / BOX_GPU_CLUSTER_GROUP,
        (BOX_GPU_CLUSTER_Y + BOX_GPU_CLUSTER_GROUP - 1) / BOX_GPU_CLUSTER_GROUP,
        (BOX_GPU_CLUSTER_Z + BOX_GPU_CLUSTER_GROUP - 1) / BOX_GPU_CLUSTER_GROUP);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
done:
    return;
}

void box_gpu_light_cluster(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v,
    int32_t slot)
{
    box_gpu_cluster_t *c = NULL;
    if (!box_gpu_cluster_runs(gpu, v)) {
        goto done;
    }

    c = box_gpu_cluster_slot(gpu, slot);
    box_gpu_cluster_poll(gpu, c, slot, box_gpu_cluster_lights(gpu, v),
        box_gpu_cluster_probe_count(gpu, v));
    box_gpu_light_cluster_with(gpu, encoder, v, gpu->view_buffer,
        (uint32_t)(slot * BOX_GPU_VIEW_STRIDE), box_gpu_cluster_buffer(gpu, slot, v));
    if (c->state != BOX_GPU_CLUSTER_FREE) {
        goto done;
    }

    if (!c->readback) {
        c->readback = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = sizeof(uint32_t)
        });
    }

    wgpuCommandEncoderCopyBufferToBuffer(encoder, c->buffer, 0, c->readback, 0,
        sizeof(uint32_t));
    c->state = BOX_GPU_CLUSTER_COPIED;
    c->frame = gpu->cull.frame;
done:
    return;
}

WGPUBindGroup box_gpu_light_group_with(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    WGPUBuffer uniforms,
    WGPUBuffer clusters,
    WGPUTextureView shadow)
{
    uint64_t size = 0;
    WGPUBuffer lights = box_gpu_light_binding(gpu, v, &size);
    const box_gpu_reflect_t *cube = box_gpu_reflect_of(gpu, v);
    box_gpu_buffer_t *p = cube ? box_gpu_probe_buffer(gpu, v) : NULL;
    cube = cube ? cube : gpu->no_reflection;
    WGPUBindGroupEntry entries[9] = {
        { .binding = 0, .buffer = uniforms, .size = sizeof(box_gpu_view_uniforms_t) },
        { .binding = 1, .buffer = lights, .size = size },
        { .binding = 2, .buffer = clusters, .size = wgpuBufferGetSize(clusters) },
        { .binding = 3, .textureView = shadow },
        { .binding = 4, .sampler = gpu->shadow_sampler },
        {
            .binding = 5,
            .buffer = p ? p->buffer : gpu->no_probes,
            .size = p ? p->size : sizeof(box_render_reflection_probe_t)
        },
        { .binding = 6, .textureView = cube->cube },
        { .binding = 7, .sampler = gpu->reflect_sampler },
        { .binding = 8, .buffer = cube->sh, .size = wgpuBufferGetSize(cube->sh) }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->view_layout,
        .entryCount = 9,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(gpu->device, &desc);
}

WGPUBindGroup box_gpu_light_group(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    int32_t slot)
{
    return box_gpu_light_group_with(gpu, v, gpu->view_buffer,
        box_gpu_cluster_buffer(gpu, slot, v), box_gpu_shadow_binding(gpu, v));
}

#endif
