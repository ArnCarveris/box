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
                .minBindingSize = sizeof(box_view_t)
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

    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = box_cluster_shader_wgsl, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor shader_desc = { .nextInChain = (WGPUChainedStruct*)&wgsl };
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(r->device, &shader_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->cluster_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

    WGPUComputePipelineDescriptor desc = {
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
    if (r->cluster_group) wgpuBindGroupRelease(r->cluster_group);
    if (r->light_group) wgpuBindGroupRelease(r->light_group);

    uint64_t light_size = (uint64_t)r->light_capacity * sizeof(box_light_t);
    uint64_t cluster_size = (uint64_t)box_cluster_count(&r->grid) * 2 * sizeof(uint32_t);
    uint64_t index_size = (uint64_t)box_cluster_count(&r->grid) *
        r->grid.max_lights * sizeof(uint32_t);

    WGPUBindGroupEntry compute_entries[4] = {
        { .binding = 0, .buffer = r->view_buffer, .size = sizeof(box_view_t) },
        { .binding = 1, .buffer = r->lights, .size = light_size },
        { .binding = 2, .buffer = r->clusters, .size = cluster_size },
        { .binding = 3, .buffer = r->light_indices, .size = index_size }
    };
    WGPUBindGroupDescriptor compute_desc = {
        .layout = r->cluster_layout,
        .entryCount = 4,
        .entries = compute_entries
    };
    r->cluster_group = wgpuDeviceCreateBindGroup(r->device, &compute_desc);

    WGPUBindGroupEntry light_entries[3] = {
        { .binding = 0, .buffer = r->lights, .size = light_size },
        { .binding = 1, .buffer = r->clusters, .size = cluster_size },
        { .binding = 2, .buffer = r->light_indices, .size = index_size }
    };
    WGPUBindGroupDescriptor light_desc = {
        .layout = r->light_layout,
        .entryCount = 3,
        .entries = light_entries
    };
    r->light_group = wgpuDeviceCreateBindGroup(r->device, &light_desc);
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
    uint32_t grown = r->light_capacity ? r->light_capacity * 2 : BOX_LIGHT_CAPACITY;
    if (grown < count) {
        grown = count;
    }

    if (r->lights) wgpuBufferRelease(r->lights);
    r->lights = box_lights_buffer(r, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        (uint64_t)grown * sizeof(box_light_t));
    r->light_capacity = grown;
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
    if (r->clusters) wgpuBufferRelease(r->clusters);
    if (r->light_indices) wgpuBufferRelease(r->light_indices);

    r->grid = *grid;
    uint32_t count = box_cluster_count(grid);
    r->clusters = box_lights_buffer(r, WGPUBufferUsage_Storage,
        (uint64_t)count * 2 * sizeof(uint32_t));
    r->light_indices = box_lights_buffer(r, WGPUBufferUsage_Storage,
        (uint64_t)count * grid->max_lights * sizeof(uint32_t));
}

void box_lights_init(
    box_renderer_t *r)
{
    box_lights_pipeline(r);
    box_lights_layout(r);
    box_lights_grow(r, 0);
}

void box_lights_update(
    box_renderer_t *r,
    const box_packet_t *p)
{
    const box_cluster_t *grid = &p->view.cluster;
    assert(grid->x && grid->y && grid->z && grid->max_lights);

    bool rebuild = false;
    if (p->lights.count > r->light_capacity) {
        box_lights_grow(r, p->lights.count);
        rebuild = true;
    }

    if (grid->x != r->grid.x || grid->y != r->grid.y || grid->z != r->grid.z ||
        grid->max_lights != r->grid.max_lights)
    {
        box_lights_grid(r, grid);
        rebuild = true;
    }

    if (rebuild) {
        box_lights_groups(r);
    }

    if (p->lights.count) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->lights, 0, p->lights.data,
            (size_t)p->lights.count * sizeof(box_light_t));
    }
}

void box_lights_dispatch(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder)
{
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, r->cluster_pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, r->cluster_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,
        (p->view.cluster.x + BOX_CLUSTER_GROUP - 1) / BOX_CLUSTER_GROUP,
        (p->view.cluster.y + BOX_CLUSTER_GROUP - 1) / BOX_CLUSTER_GROUP,
        (p->view.cluster.z + BOX_CLUSTER_GROUP - 1) / BOX_CLUSTER_GROUP);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}

void box_lights_fini(
    box_renderer_t *r)
{
    if (r->cluster_group) wgpuBindGroupRelease(r->cluster_group);
    if (r->light_group) wgpuBindGroupRelease(r->light_group);
    if (r->cluster_pipeline) wgpuComputePipelineRelease(r->cluster_pipeline);
    if (r->cluster_layout) wgpuBindGroupLayoutRelease(r->cluster_layout);
    if (r->light_layout) wgpuBindGroupLayoutRelease(r->light_layout);
    if (r->lights) wgpuBufferRelease(r->lights);
    if (r->clusters) wgpuBufferRelease(r->clusters);
    if (r->light_indices) wgpuBufferRelease(r->light_indices);
    r->cluster_group = NULL;
    r->light_group = NULL;
    r->cluster_pipeline = NULL;
    r->cluster_layout = NULL;
    r->light_layout = NULL;
    r->lights = NULL;
    r->clusters = NULL;
    r->light_indices = NULL;
    r->light_capacity = 0;
    memset(&r->grid, 0, sizeof(box_cluster_t));
}
