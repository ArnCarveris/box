/**
 * @file renderer/lights.c
 * @brief Light storage buffer, cluster grid buffers and the cluster assignment pass.
 *
 * The compute pass fills a light index list and a per cluster offset and count
 * that the mesh fragment shader reads; both are rebuilt when the grid changes.
 */

#include "renderer_internal.h"
#include <assert.h>

/** Clusters one compute workgroup covers on each axis */
#define BOX_CLUSTER_GROUP (4)

/** Lights the storage buffer starts out with */
#define BOX_LIGHT_CAPACITY (64)

/** Create a GPU buffer.
 *
 * @param r The renderer
 * @param usage Buffer usage flags
 * @param size Size in bytes
 * @return The buffer
 */
static WGPUBuffer box_lights_buffer(
    box_renderer_t *r,
    WGPUBufferUsage usage,
    uint64_t size)
{
    WGPUBufferDescriptor desc = { .usage = usage, .size = size };

    return wgpuDeviceCreateBuffer(r->device, &desc);
}

/** Create the compute pipeline that assigns lights to clusters.
 *
 * @param r The renderer
 */
static void box_lights_pipeline(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_VIEW_UNIFORM_SIZE
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 4, .entries = entries };
    r->cluster_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUShaderModule shader = box_shader_module(r, BOX_SHADER_CLUSTER);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->cluster_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

    WGPUComputePipelineDescriptor desc = {
        .label = BOX_GPU_LABEL("light clusters"),
        .layout = pipeline_layout,
        .compute = {
            .module = shader,
            .entryPoint = { .data = "cs_main", .length = WGPU_STRLEN }
        }
    };
    r->cluster_pipeline = wgpuDeviceCreateComputePipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader);
}

/** Create the layout of the light bind group the mesh shader reads.
 *
 * @param r The renderer
 */
static void box_lights_layout(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutDescriptor desc = { .entryCount = 3, .entries = entries };
    r->light_layout = wgpuDeviceCreateBindGroupLayout(r->device, &desc);
}

/** Rebuild the compute and fragment bind groups over the current buffers.
 *
 * @param r The renderer
 */
static void box_lights_groups(
    box_renderer_t *r)
{
    if (r->v->cluster_group) wgpuBindGroupRelease(r->v->cluster_group);
    if (r->v->light_group) wgpuBindGroupRelease(r->v->light_group);

    uint64_t light_size = (uint64_t)r->v->light_capacity * sizeof(box_light_t);
    uint64_t cluster_size = (uint64_t)box_cluster_count(&r->v->grid) * 4 * sizeof(uint32_t);
    uint64_t index_size = (uint64_t)box_cluster_count(&r->v->grid) *
        r->v->grid.max_lights * 2 * sizeof(uint32_t);

    WGPUBindGroupEntry compute_entries[4] = {
        { .binding = 0, .buffer = r->v->view_buffer, .size = BOX_VIEW_UNIFORM_SIZE },
        { .binding = 1, .buffer = r->v->lights, .size = light_size },
        { .binding = 2, .buffer = r->v->clusters, .size = cluster_size },
        { .binding = 3, .buffer = r->v->light_indices, .size = index_size }
    };
    WGPUBindGroupDescriptor compute_desc = {
        .layout = r->cluster_layout,
        .entryCount = 4,
        .entries = compute_entries
    };
    r->v->cluster_group = wgpuDeviceCreateBindGroup(r->device, &compute_desc);

    WGPUBindGroupEntry light_entries[3] = {
        { .binding = 0, .buffer = r->v->lights, .size = light_size },
        { .binding = 1, .buffer = r->v->clusters, .size = cluster_size },
        { .binding = 2, .buffer = r->v->light_indices, .size = index_size }
    };
    WGPUBindGroupDescriptor light_desc = {
        .layout = r->light_layout,
        .entryCount = 3,
        .entries = light_entries
    };
    r->v->light_group = wgpuDeviceCreateBindGroup(r->device, &light_desc);
}

/** Grow the light storage buffer to hold a number of lights.
 *
 * @param r The renderer
 * @param count Lights the buffer must hold
 */
static void box_lights_grow(
    box_renderer_t *r,
    uint32_t count)
{
    uint32_t grown = r->v->light_capacity ? r->v->light_capacity * 2 : BOX_LIGHT_CAPACITY;
    if (grown < count) {
        grown = count;
    }

    if (r->v->lights) wgpuBufferRelease(r->v->lights);
    r->v->lights = box_lights_buffer(r, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        (uint64_t)grown * sizeof(box_light_t));
    r->v->light_capacity = grown;
}

/** Create the cluster buffers for a grid.
 *
 * @param r The renderer
 * @param grid The grid settings
 */
static void box_lights_grid(
    box_renderer_t *r,
    const box_cluster_t *grid)
{
    if (r->v->clusters) wgpuBufferRelease(r->v->clusters);
    if (r->v->light_indices) wgpuBufferRelease(r->v->light_indices);

    r->v->grid = *grid;
    uint32_t count = box_cluster_count(grid);
    r->v->clusters = box_lights_buffer(r, WGPUBufferUsage_Storage,
        (uint64_t)count * 4 * sizeof(uint32_t));
    r->v->light_indices = box_lights_buffer(r, WGPUBufferUsage_Storage,
        (uint64_t)count * grid->max_lights * 2 * sizeof(uint32_t));
}

void box_lights_init(
    box_renderer_t *r)
{
    box_lights_pipeline(r);
    box_lights_layout(r);
}

void box_lights_view_init(
    box_renderer_t *r)
{
    box_vec_init(&r->v->light_snapshot, sizeof(box_light_t));
    box_lights_grow(r, 0);
}

void box_lights_view_fini(
    box_gpu_view_t *v)
{
    if (v->cluster_group) wgpuBindGroupRelease(v->cluster_group);
    if (v->light_group) wgpuBindGroupRelease(v->light_group);
    if (v->lights) wgpuBufferRelease(v->lights);
    if (v->clusters) wgpuBufferRelease(v->clusters);
    if (v->light_indices) wgpuBufferRelease(v->light_indices);
    v->cluster_group = NULL;
    v->light_group = NULL;
    v->lights = NULL;
    v->clusters = NULL;
    v->light_indices = NULL;
    v->light_capacity = 0;
    box_vec_fini(&v->light_snapshot);
    memset(&v->grid, 0, sizeof(box_cluster_t));
}

void box_lights_update(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p)
{
    const box_cluster_t *grid = &view->cluster;
    assert(grid->x && grid->y && grid->z && grid->max_lights);

    bool rebuild = false;
    if (view->light_count > r->v->light_capacity) {
        box_lights_grow(r, view->light_count);
        rebuild = true;
    }

    if (grid->x != r->v->grid.x || grid->y != r->v->grid.y || grid->z != r->v->grid.z ||
        grid->max_lights != r->v->grid.max_lights)
    {
        box_lights_grid(r, grid);
        rebuild = true;
    }

    if (rebuild) {
        box_lights_groups(r);
    }

    box_vec_t *snapshot = &r->v->light_snapshot;
    const void *lights = view->light_count ? box_vec_at(&p->lights, view->light_first) : NULL;
    size_t bytes = (size_t)view->light_count * sizeof(box_light_t);
    bool changed = snapshot->count != view->light_count ||
        (bytes && memcmp(snapshot->data, lights, bytes));
    r->v->cluster_dirty = rebuild || changed ||
        grid->near_ != r->v->grid.near_ || grid->far_ != r->v->grid.far_ ||
        memcmp(r->v->cluster_view, view->view, sizeof(r->v->cluster_view)) ||
        memcmp(r->v->cluster_proj, view->proj, sizeof(r->v->cluster_proj));
    if ((changed || rebuild) && bytes) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->v->lights, 0, lights, bytes);
        box_vec_reserve(snapshot, view->light_count);
        memcpy(snapshot->data, lights, bytes);
    }

    snapshot->count = view->light_count;
    r->v->grid = *grid;
    memcpy(r->v->cluster_view, view->view, sizeof(r->v->cluster_view));
    memcpy(r->v->cluster_proj, view->proj, sizeof(r->v->cluster_proj));
}

void box_lights_dispatch(
    box_renderer_t *r,
    const box_view_t *view,
    WGPUCommandEncoder encoder)
{
    if (!r->v->cluster_group || !r->v->cluster_dirty) {
        goto done;
    }

    WGPUComputePassDescriptor desc = { .label = BOX_GPU_LABEL("light clusters") };
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &desc);
    wgpuComputePassEncoderSetPipeline(pass, r->cluster_pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, r->v->cluster_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,
        (view->cluster.x + BOX_CLUSTER_GROUP - 1) / BOX_CLUSTER_GROUP,
        (view->cluster.y + BOX_CLUSTER_GROUP - 1) / BOX_CLUSTER_GROUP,
        (view->cluster.z + BOX_CLUSTER_GROUP - 1) / BOX_CLUSTER_GROUP);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
done:
    return;
}

void box_lights_fini(
    box_renderer_t *r)
{
    if (r->cluster_pipeline) wgpuComputePipelineRelease(r->cluster_pipeline);
    if (r->cluster_layout) wgpuBindGroupLayoutRelease(r->cluster_layout);
    if (r->light_layout) wgpuBindGroupLayoutRelease(r->light_layout);
    r->cluster_pipeline = NULL;
    r->cluster_layout = NULL;
    r->light_layout = NULL;
}
