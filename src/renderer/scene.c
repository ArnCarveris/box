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

box_gpu_mesh_t* box_scene_mesh(
    box_renderer_t *r,
    uint32_t handle)
{
    while (r->meshes.count <= handle) {
        box_gpu_mesh_t *m = box_vec_push(&r->meshes);
        memset(m, 0, sizeof(box_gpu_mesh_t));
        box_vec_init(&m->groups, sizeof(box_static_group_t));
        box_vec_init(&m->mirror, sizeof(box_instance_t));
        box_vec_init(&m->transition_index, sizeof(uint32_t));
        box_vec_init(&m->transition_slots, sizeof(uint32_t));
        m->mirror_valid = true;
        m->transition_sorted = true;
    }

    return box_vec_at(&r->meshes, handle);
}

/** Release the buffers of a mesh entry.
 *
 * @param r The renderer
 * @param m The mesh entry
 */
static void box_scene_mesh_release(
    box_renderer_t *r,
    box_gpu_mesh_t *m)
{
    box_transition_clear(r, m);
    if (m->vertices) wgpuBufferRelease(m->vertices);
    if (m->indices) wgpuBufferRelease(m->indices);
    if (m->statics) wgpuBufferRelease(m->statics);
    box_vec_fini(&m->groups);
    box_vec_fini(&m->mirror);
    box_vec_fini(&m->cutout_slots);
    box_vec_fini(&m->transition_index);
    box_vec_fini(&m->transition_slots);
    memset(m, 0, sizeof(box_gpu_mesh_t));
    box_vec_init(&m->groups, sizeof(box_static_group_t));
    box_vec_init(&m->mirror, sizeof(box_instance_t));
    box_vec_init(&m->transition_index, sizeof(uint32_t));
    box_vec_init(&m->transition_slots, sizeof(uint32_t));
    m->mirror_valid = true;
    m->transition_sorted = true;
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
    box_scene_mesh_release(r, m);

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
    uint32_t limit = box_device_instances(r, true);
    if (m->static_capacity >= limit) {
        goto done;
    }

    uint64_t doubled = m->static_capacity ? (uint64_t)m->static_capacity * 2 : 64;
    if (doubled < capacity) {
        doubled = capacity;
    }

    uint32_t grown = doubled > limit ? limit : (uint32_t)doubled;
    WGPUBufferUsage usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst |
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
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
done:
    return;
}

void box_scene_mirror(
    box_gpu_mesh_t *m,
    uint32_t slot,
    const box_instance_t *data)
{
    m->cutout_slots.size = sizeof(uint8_t);
    while (m->cutout_slots.count <= slot) {
        *(uint8_t*)box_vec_push(&m->cutout_slots) = 0;
    }

    uint8_t *cutout = box_vec_at(&m->cutout_slots, slot);
    m->cutout_count -= *cutout;
    *cutout = (data->textures[0] & 0xFFFFu) != 0 ||
        (data->uv_mode & ((1u << BOX_MODE_CLIP_SHIFT) | (1u << BOX_MODE_SHAPE_SHIFT))) != 0;
    m->cutout_count += *cutout;
    if (!m->mirror_valid) {
        goto done;
    }

    if (slot >= BOX_CULL_MIRROR_MAX) {
        box_vec_clear(&m->mirror);
        m->mirror_valid = false;
        goto done;
    }

    while (m->mirror.count <= slot) {
        memset(box_vec_push(&m->mirror), 0, sizeof(box_instance_t));
    }

    *(box_instance_t*)box_vec_at(&m->mirror, slot) = *data;
done:
    return;
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
    uint32_t limit = box_device_instances(r, false);
    uint32_t count = p->dynamic_instances.count;
    count = count > limit ? limit : count;
    if (!count) {
        goto done;
    }

    if (count > r->dynamic_capacity) {
        uint64_t doubled = r->dynamic_capacity ? (uint64_t)r->dynamic_capacity * 2 : 1024;
        if (doubled < count) {
            doubled = count;
        }

        uint32_t grown = doubled > limit ? limit : (uint32_t)doubled;
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

void box_scene_flush_statics(
    box_renderer_t *r,
    uint32_t mesh,
    uint32_t first)
{
    if (!r->static_staging.count) {
        goto done;
    }

    box_gpu_mesh_t *m = box_scene_mesh(r, mesh);
    wgpuQueueWriteBuffer(r->gpu_queue, m->statics,
        (uint64_t)first * sizeof(box_instance_t), box_vec_at(&r->static_staging, 0),
        (size_t)r->static_staging.count * sizeof(box_instance_t));
    box_vec_clear(&r->static_staging);
done:
    return;
}

/** Apply the static instance updates of a packet, one write per run of slots.
 *
 * A scene of a million instances sends a million updates on the frame it loads,
 * which is a second of queue calls when every slot is written on its own.
 *
 * @param r The renderer
 * @param p The packet
 */
static void box_scene_write_statics(
    box_renderer_t *r,
    box_packet_t *p)
{
    uint32_t mesh = 0xFFFFFFFFu;
    uint32_t first = 0;
    uint32_t i;
    for (i = 0; i < p->static_updates.count; i ++) {
        box_static_update_t *u = box_vec_at(&p->static_updates, i);
        box_gpu_mesh_t *m = box_scene_mesh(r, u->mesh);
        if (u->slot >= m->static_capacity) {
            box_scene_flush_statics(r, mesh, first);
            box_scene_grow_statics(r, m, u->slot + 1);
        }

        if (u->slot >= m->static_capacity) {
            continue;
        }

        if (u->mesh != mesh || u->slot != first + r->static_staging.count) {
            box_scene_flush_statics(r, mesh, first);
            mesh = u->mesh;
            first = u->slot;
        }

        *(box_instance_t*)box_vec_push(&r->static_staging) = u->data;
        box_scene_mirror(m, u->slot, &u->data);
        if (!u->transition || u->transition > p->transitions.count) {
            box_transition_stop(r, m, u->slot);
        } else {
            box_transition_start(r, m, p, box_vec_at(&p->transitions, u->transition - 1),
                &u->data);
        }

        if (r->static_staging.count >= BOX_SCENE_UPLOAD_RUN) {
            box_scene_flush_statics(r, mesh, first);
            first += BOX_SCENE_UPLOAD_RUN;
        }
    }

    box_scene_flush_statics(r, mesh, first);
}

void box_scene_apply(
    box_renderer_t *r,
    box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->mesh_destroy.count; i ++) {
        uint32_t handle = *(uint32_t*)box_vec_at(&p->mesh_destroy, i);
        box_scene_mesh_release(r, box_scene_mesh(r, handle));
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

        m->static_count = c->count > m->static_capacity ? m->static_capacity : c->count;
        while (m->cutout_slots.count > m->static_count) {
            m->cutout_count -= *(uint8_t*)box_vec_at(&m->cutout_slots, --m->cutout_slots.count);
        }
        while (m->transition_index.count > m->static_count) {
            box_transition_stop(r, m, m->transition_index.count - 1);
            m->transition_index.count --;
        }

        if (c->count > BOX_CULL_MIRROR_MAX && m->mirror_valid) {
            box_vec_clear(&m->mirror);
            m->mirror_valid = false;
        }
    }

    uint32_t grouped = 0xFFFFFFFFu;
    for (i = 0; i < p->static_groups.count; i ++) {
        box_static_group_t *g = box_vec_at(&p->static_groups, i);
        box_gpu_mesh_t *m = box_scene_mesh(r, g->mesh);
        if (g->mesh != grouped) {
            box_vec_clear(&m->groups);
            grouped = g->mesh;
        }

        if (g->count) {
            *(box_static_group_t*)box_vec_push(&m->groups) = *g;
        }
    }

    box_scene_write_statics(r, p);
    box_transition_frame(r, p);

    if (!box_cull_packet(p)) {
        box_scene_upload_dynamics(r, p);
    }
}

/** Record the static draws of one mesh, one per run of slots the view shows.
 *
 * A mesh without runs holds slots of no layer only, which a strict view skips
 * and every other view draws in a single call; so is a mesh whose runs a view
 * shows all of, because its runs cover its slots without a gap.
 *
 * @param m The mesh entry
 * @param view The view being drawn
 * @param pass The render pass
 */
static void box_scene_draw_statics(
    box_gpu_mesh_t *m,
    const box_view_t *view,
    WGPURenderPassEncoder pass)
{
    uint64_t stride = sizeof(box_instance_t);
    if (!m->groups.count) {
        if (box_view_shows(view, 0)) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m->statics, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, m->static_count, 0, 0, 0);
        }

        goto done;
    }

    if (view->layer_mask == BOX_VIEW_LAYER_ALL) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m->statics, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, m->static_count, 0, 0, 0);
        goto done;
    }

    uint32_t first = 0;
    uint32_t count = 0;
    uint32_t g;
    for (g = 0; g <= m->groups.count; g ++) {
        box_static_group_t *run = g < m->groups.count ? box_vec_at(&m->groups, g) : NULL;
        bool shown = run && box_view_shows(view, run->mask);
        if (shown && count && run->first == first + count) {
            count += run->count;
            continue;
        }

        if (count) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m->statics, (uint64_t)first * stride,
                (uint64_t)count * stride);
            wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, count, 0, 0, 0);
            count = 0;
        }

        if (shown) {
            first = run->first;
            count = run->count;
        }
    }

done:
    return;
}

void box_scene_draw(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPURenderPassEncoder pass,
    WGPURenderPipeline pipeline)
{
    uint32_t i;
    for (i = 0; i < r->meshes.count; i ++) {
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        if (!m->alive || !m->static_count) {
            continue;
        }

        wgpuRenderPassEncoderSetPipeline(pass, pipeline ? pipeline :
            box_frame_opaque_pipeline(r, !m->cutout_count));
        wgpuRenderPassEncoderSetBindGroup(pass, 3, box_bake_group(r, i, true), 0, NULL);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m->indices, WGPUIndexFormat_Uint32, 0,
            WGPU_WHOLE_SIZE);
        box_scene_draw_statics(m, view, pass);
    }

    uint32_t last = view->dynamic_first + view->dynamic_count;
    for (i = view->dynamic_first; i < last; i ++) {
        box_dynamic_batch_t *b = box_vec_at(&p->dynamic_batches, i);
        box_gpu_mesh_t *m = box_scene_mesh(r, b->mesh);
        if (!m->alive || !b->count) {
            continue;
        }

        wgpuRenderPassEncoderSetPipeline(pass, pipeline ? pipeline :
            box_frame_opaque_pipeline(r, false));
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
        box_scene_mesh_release(r, box_vec_at(&r->meshes, i));
    }

    box_vec_fini(&r->meshes);
    box_vec_fini(&r->static_staging);
    box_transition_pool_fini(&r->transitions);
    if (r->dynamics) wgpuBufferRelease(r->dynamics);
    r->dynamics = NULL;
    r->dynamic_capacity = 0;
}
