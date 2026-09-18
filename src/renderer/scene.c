/**
 * @file renderer/scene.c
 * @brief GPU mesh table, static instance buffers and the shared dynamic instance buffer.
 */

#include "renderer_internal.h"
#include <stdio.h>

/** Create a GPU buffer.
 *
 * @param r The renderer
 * @param usage Buffer usage flags
 * @param size Size in bytes
 * @return The buffer
 */
static WGPUBuffer box_scene_buffer(
    box_renderer_t *r,
    WGPUBufferUsage usage,
    uint64_t size)
{
    WGPUBufferDescriptor desc = { .usage = usage, .size = size };

    return wgpuDeviceCreateBuffer(r->device, &desc);
}

/** Get the mesh entry for a handle, growing the table as needed.
 *
 * @param r The renderer
 * @param handle Mesh handle
 * @return The mesh entry
 */
static box_gpu_mesh_t* box_scene_mesh(
    box_renderer_t *r,
    uint32_t handle)
{
    while (r->meshes.count <= handle) {
        box_gpu_mesh_t *m = box_vec_push(&r->meshes);
        memset(m, 0, sizeof(box_gpu_mesh_t));
    }

    return box_vec_at(&r->meshes, handle);
}

/** Release the buffers of a mesh entry.
 *
 * @param m The mesh entry
 */
static void box_scene_mesh_release(
    box_gpu_mesh_t *m)
{
    box_bake_mesh_release(m);
    if (m->vertices) wgpuBufferRelease(m->vertices);
    if (m->indices) wgpuBufferRelease(m->indices);
    if (m->statics) wgpuBufferRelease(m->statics);
    memset(m, 0, sizeof(box_gpu_mesh_t));
}

/** Upload a mesh from a create command and free the command's arrays.
 *
 * @param r The renderer
 * @param cmd The create command
 */
static void box_scene_mesh_create(
    box_renderer_t *r,
    box_mesh_cmd_t *cmd)
{
    box_gpu_mesh_t *m = box_scene_mesh(r, cmd->handle);
    box_scene_mesh_release(m);

    uint64_t vertex_size = (uint64_t)cmd->vertex_count * BOX_VERTEX_FLOATS * sizeof(float);
    uint64_t index_size = (uint64_t)cmd->index_count * sizeof(uint32_t);
    m->vertices = box_scene_buffer(r, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
        vertex_size);
    m->indices = box_scene_buffer(r, WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, index_size);
    wgpuQueueWriteBuffer(r->gpu_queue, m->vertices, 0, cmd->vertices, (size_t)vertex_size);
    wgpuQueueWriteBuffer(r->gpu_queue, m->indices, 0, cmd->indices, (size_t)index_size);
    m->index_count = cmd->index_count;
    m->alive = true;

    free(cmd->vertices);
    free(cmd->indices);
    cmd->vertices = NULL;
    cmd->indices = NULL;
}

/** Grow the static instance buffer of a mesh, preserving its contents.
 *
 * @param r The renderer
 * @param m The mesh entry
 * @param capacity Required instance capacity
 */
static void box_scene_grow_statics(
    box_renderer_t *r,
    box_gpu_mesh_t *m,
    uint32_t capacity)
{
    uint32_t grown = m->static_capacity ? m->static_capacity * 2 : 64;
    if (grown < capacity) {
        grown = capacity;
    }

    WGPUBufferUsage usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst |
        WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = box_scene_buffer(r, usage, (uint64_t)grown * sizeof(box_instance_t));
    if (m->statics) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, m->statics, 0, buffer, 0,
            (uint64_t)m->static_capacity * sizeof(box_instance_t));
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd);
        wgpuCommandEncoderRelease(encoder);
        wgpuBufferRelease(m->statics);
    }

    m->statics = buffer;
    m->static_capacity = grown;
}

/** Upload the dynamic instances of a packet into the shared buffer.
 *
 * @param r The renderer
 * @param p The packet
 */
static void box_scene_upload_dynamics(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t count = p->dynamic_instances.count;
    if (!count) {
        goto done;
    }

    if (count > r->dynamic_capacity) {
        uint32_t grown = r->dynamic_capacity ? r->dynamic_capacity * 2 : 1024;
        if (grown < count) {
            grown = count;
        }

        if (r->dynamics) wgpuBufferRelease(r->dynamics);
        r->dynamics = box_scene_buffer(r, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            (uint64_t)grown * sizeof(box_instance_t));
        r->dynamic_capacity = grown;
    }

    wgpuQueueWriteBuffer(r->gpu_queue, r->dynamics, 0, p->dynamic_instances.data,
        (size_t)count * sizeof(box_instance_t));
done:
    return;
}

void box_scene_apply(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->mesh_destroy.count; i ++) {
        uint32_t handle = *(uint32_t*)box_vec_at(&p->mesh_destroy, i);
        box_scene_mesh_release(box_scene_mesh(r, handle));
    }

    for (i = 0; i < p->mesh_create.count; i ++) {
        box_scene_mesh_create(r, box_vec_at(&p->mesh_create, i));
    }

    for (i = 0; i < p->static_counts.count; i ++) {
        box_static_count_t *c = box_vec_at(&p->static_counts, i);
        box_gpu_mesh_t *m = box_scene_mesh(r, c->mesh);
        if (c->count > m->static_capacity) {
            box_scene_grow_statics(r, m, c->count);
        }

        m->static_count = c->count;
    }

    for (i = 0; i < p->static_updates.count; i ++) {
        box_static_update_t *u = box_vec_at(&p->static_updates, i);
        box_gpu_mesh_t *m = box_scene_mesh(r, u->mesh);
        if (u->slot >= m->static_capacity) {
            box_scene_grow_statics(r, m, u->slot + 1);
        }

        wgpuQueueWriteBuffer(r->gpu_queue, m->statics,
            (uint64_t)u->slot * sizeof(box_instance_t), &u->data, sizeof(box_instance_t));
    }

    box_scene_upload_dynamics(r, p);
}

void box_scene_draw(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPURenderPassEncoder pass)
{
    uint32_t i;
    for (i = 0; i < r->meshes.count; i ++) {
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        if (!m->alive || !m->static_count) {
            continue;
        }

        wgpuRenderPassEncoderSetBindGroup(pass, 3, box_bake_group(r, i, true), 0, NULL);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m->statics, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m->indices, WGPUIndexFormat_Uint32, 0,
            WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, m->static_count, 0, 0, 0);
    }

    for (i = 0; i < p->dynamic_batches.count; i ++) {
        box_dynamic_batch_t *b = box_vec_at(&p->dynamic_batches, i);
        box_gpu_mesh_t *m = box_scene_mesh(r, b->mesh);
        if (!m->alive || !b->count) {
            continue;
        }

        wgpuRenderPassEncoderSetBindGroup(pass, 3, box_bake_group(r, b->mesh, false), 0, NULL);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, r->dynamics,
            (uint64_t)b->first * sizeof(box_instance_t),
            (uint64_t)b->count * sizeof(box_instance_t));
        wgpuRenderPassEncoderSetIndexBuffer(pass, m->indices, WGPUIndexFormat_Uint32, 0,
            WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, b->count, 0, 0, 0);
    }
}

void box_scene_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < r->meshes.count; i ++) {
        box_scene_mesh_release(box_vec_at(&r->meshes, i));
    }

    box_vec_fini(&r->meshes);
    if (r->dynamics) wgpuBufferRelease(r->dynamics);
    r->dynamics = NULL;
    r->dynamic_capacity = 0;
}
