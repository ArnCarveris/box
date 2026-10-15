/**
 * @file render/gpu_light.c
 * @brief Light uniforms, the cluster pass and the light bindings of the forward pass.
 *
 * Every view with point or spot lights runs etc/shaders/cluster.wgsl before it draws: it lists,
 * per cluster of the view frustum, the lights that reach it in one shared light index list, so
 * a fragment only shades the lights of its own cluster.
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

/** Words before the light index list: the allocated entries, then offset and count per cluster */
#define BOX_GPU_CLUSTER_HEADER (1 + 2 * BOX_GPU_CLUSTER_COUNT)

/** Largest cluster buffer, the default maxStorageBufferBindingSize of WebGPU */
#define BOX_GPU_CLUSTER_BYTES_MAX ((uint64_t)128 * 1024 * 1024)

/** Point and spot lights the cluster buffer of box_gpu_light_clusters_new lists in every cluster */
#define BOX_GPU_CLUSTER_SHARED_LIGHTS (128)

/** The cluster pass warned that a light index list cannot hold every light of every cluster */
static bool box_gpu_cluster_warned;

/** Get the bytes of a cluster buffer whose light index list holds every light in every cluster.
 *
 * @param lights Point and spot lights
 * @return The bytes, at most BOX_GPU_CLUSTER_BYTES_MAX
 */
static uint64_t box_gpu_cluster_bytes(
    uint64_t lights)
{
    uint64_t result = (BOX_GPU_CLUSTER_HEADER + BOX_GPU_CLUSTER_COUNT * lights) * sizeof(uint32_t);

    return result < BOX_GPU_CLUSTER_BYTES_MAX ? result : BOX_GPU_CLUSTER_BYTES_MAX;
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
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = bytes
    };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

void box_gpu_light_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[3] = {
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
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 3, .entries = entries };
    WGPUBufferDescriptor empty = {
        .usage = WGPUBufferUsage_Storage,
        .size = sizeof(box_render_light_t)
    };
    gpu->cluster_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    gpu->cluster_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu->cluster_layout
        });
    gpu->no_lights = wgpuDeviceCreateBuffer(gpu->device, &empty);
    ecs_vec_init_t(NULL, &gpu->clusters, WGPUBuffer, 0);
}

void box_gpu_light_fini(
    box_gpu_t *gpu)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&gpu->clusters); i ++) {
        wgpuBufferRelease(*ecs_vec_get_t(&gpu->clusters, WGPUBuffer, i));
    }

    ecs_vec_fini_t(NULL, &gpu->clusters, WGPUBuffer);
    if (gpu->cluster) {
        wgpuComputePipelineRelease(gpu->cluster);
        gpu->cluster = NULL;
    }

    if (gpu->no_lights) {
        wgpuBufferRelease(gpu->no_lights);
        gpu->no_lights = NULL;
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
    u->grid[3] = 0;
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

/** Get the cluster buffer of a view slot, sized for the lights of the view.
 *
 * The light index list holds the next power of two of the lights in every cluster, so lights
 * that come and go do not recreate it each frame; it shrinks when it is four times too large.
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
    uint64_t lights = box_gpu_cluster_lights(gpu, v), rounded = 1;
    while (rounded < lights) {
        rounded <<= 1;
    }

    uint64_t needed = box_gpu_cluster_bytes(lights);
    uint64_t target = box_gpu_cluster_bytes(rounded);
    while (ecs_vec_count(&gpu->clusters) <= slot) {
        *ecs_vec_append_t(NULL, &gpu->clusters, WGPUBuffer) =
            box_gpu_cluster_buffer_new(gpu, target);
    }

    WGPUBuffer *result = ecs_vec_get_t(&gpu->clusters, WGPUBuffer, slot);
    uint64_t size = wgpuBufferGetSize(*result);
    if (size < needed || size > 4 * target) {
        wgpuBufferRelease(*result);
        *result = box_gpu_cluster_buffer_new(gpu, target);
    }

    return *result;
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

/** Get the light buffer a view binds.
 *
 * @param gpu The backend
 * @param v The view
 * @param size Receives the bytes of the buffer
 * @return The light buffer of the view, or the empty buffer when the backend lacks it
 */
static WGPUBuffer box_gpu_light_binding(
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
    box_gpu_buffer_t *b = box_gpu_light_buffer(gpu, v);
    WGPUComputePipeline pipeline = NULL;
    if (!v->lights.enabled || !b || v->lights.count <= v->lights.directional) {
        goto done;
    }

    pipeline = box_gpu_cluster_pipeline(gpu);
    if (!pipeline) {
        goto done;
    }

    uint64_t size = wgpuBufferGetSize(clusters);
    uint64_t lights = box_gpu_cluster_lights(gpu, v);
    if (size < box_gpu_cluster_bytes(lights) && !box_gpu_cluster_warned) {
        ecs_warn("the light index list of a view holds %u entries, %u lights in every cluster "
            "need more: a cluster may not list every light that reaches it",
            (uint32_t)(size / sizeof(uint32_t) - BOX_GPU_CLUSTER_HEADER),
            (uint32_t)lights);
        box_gpu_cluster_warned = true;
    }

    WGPUBindGroupEntry entries[3] = {
        { .binding = 0, .buffer = uniforms, .size = sizeof(box_gpu_view_uniforms_t) },
        { .binding = 1, .buffer = b->buffer, .size = b->size },
        { .binding = 2, .buffer = clusters, .size = size }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->cluster_layout,
        .entryCount = 3,
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
    if (v->lights.enabled && v->lights.count > v->lights.directional) {
        box_gpu_light_cluster_with(gpu, encoder, v, gpu->view_buffer,
            (uint32_t)(slot * BOX_GPU_VIEW_STRIDE), box_gpu_cluster_buffer(gpu, slot, v));
    }
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
    WGPUBindGroupEntry entries[5] = {
        { .binding = 0, .buffer = uniforms, .size = sizeof(box_gpu_view_uniforms_t) },
        { .binding = 1, .buffer = lights, .size = size },
        { .binding = 2, .buffer = clusters, .size = wgpuBufferGetSize(clusters) },
        { .binding = 3, .textureView = shadow },
        { .binding = 4, .sampler = gpu->shadow_sampler }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->view_layout,
        .entryCount = 5,
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

WGPUBuffer box_gpu_light_clusters_new(
    box_gpu_t *gpu)
{
    return box_gpu_cluster_buffer_new(gpu, box_gpu_cluster_bytes(BOX_GPU_CLUSTER_SHARED_LIGHTS));
}

#endif
