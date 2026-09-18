/**
 * @file renderer/bake.c
 * @brief Probe bookkeeping, the static occupancy grid and the GPU bake passes.
 *
 * Probes hold order 1 spherical harmonics per static instance slot. One compute
 * pass fills a voxel grid with the bounds of the static scene, a second bakes
 * the probes a packet invalidated, at most a budget of them per frame.
 */

#include "renderer_internal.h"
#include <stdlib.h>
#include <assert.h>

/** Probes one compute workgroup covers */
#define BOX_BAKE_GROUP (64)

/** Probes a probe buffer starts out with */
#define BOX_BAKE_CAPACITY (64)

/** Part of its size the scene bounds are padded with before the grid is built */
#define BOX_BAKE_PADDING (0.05f)

/** Bytes of one probe in the GPU storage buffer */
#define BOX_BAKE_PROBE_SIZE ((uint64_t)sizeof(box_probe_t))

box_bake_mesh_t* box_bake_state_mesh(
    box_bake_state_t *s,
    uint32_t mesh)
{
    while (s->meshes.count <= mesh) {
        box_bake_mesh_t *entry = box_vec_push(&s->meshes);
        memset(entry, 0, sizeof(box_bake_mesh_t));
        box_vec_init(&entry->descs, sizeof(box_probe_desc_t));
        box_vec_init(&entry->dirty, sizeof(uint8_t));
        box_vec_init(&entry->jobs, sizeof(uint32_t));
        entry->upload_lo = UINT32_MAX;
    }

    return box_vec_at(&s->meshes, mesh);
}

void box_bake_state_init(
    box_bake_state_t *s)
{
    memset(s, 0, sizeof(box_bake_state_t));
    box_vec_init(&s->meshes, sizeof(box_bake_mesh_t));
    box_vec_init(&s->dirty, sizeof(box_bake_job_t));
    box_vec_init(&s->lights, sizeof(box_light_t));
    box_vec_init(&s->emitters, sizeof(box_emitter_t));
    box_vec_init(&s->changed, sizeof(box_bake_sphere_t));
    s->grid_dirty = true;
}

void box_bake_state_fini(
    box_bake_state_t *s)
{
    uint32_t i;
    for (i = 0; i < s->meshes.count; i ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, i);
        box_vec_fini(&m->descs);
        box_vec_fini(&m->dirty);
        box_vec_fini(&m->jobs);
    }

    box_vec_fini(&s->meshes);
    box_vec_fini(&s->dirty);
    box_vec_fini(&s->lights);
    box_vec_fini(&s->emitters);
    box_vec_fini(&s->changed);
}

/** Drop every probe, the dirty set and the previous inputs.
 *
 * @param s The state
 */
static void box_bake_reset(
    box_bake_state_t *s)
{
    uint32_t i;
    for (i = 0; i < s->meshes.count; i ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, i);
        box_vec_clear(&m->descs);
        box_vec_clear(&m->dirty);
        box_vec_clear(&m->jobs);
        m->upload_lo = UINT32_MAX;
        m->upload_hi = 0;
    }

    box_vec_clear(&s->dirty);
    box_vec_clear(&s->lights);
    box_vec_clear(&s->emitters);
    box_vec_clear(&s->changed);
    s->probe_count = 0;
    s->has_bounds = false;
    s->grid_dirty = true;
}

/** Copy the elements of one vector over another.
 *
 * @param dst The destination
 * @param src The source, holding elements of the same size
 */
static void box_bake_copy(
    box_vec_t *dst,
    const box_vec_t *src)
{
    assert(dst->size == src->size);
    box_vec_clear(dst);
    box_vec_reserve(dst, src->count);
    if (src->count) {
        memcpy(dst->data, src->data, (size_t)src->count * src->size);
    }

    dst->count = src->count;
}

/** Queue a probe for baking unless it already is.
 *
 * @param s The state
 * @param mesh Mesh handle
 * @param slot Static instance slot
 */
static void box_bake_mark(
    box_bake_state_t *s,
    uint32_t mesh,
    uint32_t slot)
{
    box_bake_mesh_t *m = box_bake_state_mesh(s, mesh);
    assert(slot < m->dirty.count);
    uint8_t *flag = box_vec_at(&m->dirty, slot);
    if (!*flag) {
        *flag = 1;
        box_bake_job_t *job = box_vec_push(&s->dirty);
        job->mesh = mesh;
        job->slot = slot;
        job->distance = 0;
    }
}

/** Queue every probe of every mesh for baking.
 *
 * @param s The state
 */
static void box_bake_mark_all(
    box_bake_state_t *s)
{
    uint32_t mesh;
    for (mesh = 0; mesh < s->meshes.count; mesh ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, mesh);
        uint32_t slot;
        for (slot = 0; slot < m->descs.count; slot ++) {
            const box_probe_desc_t *d = box_vec_at(&m->descs, slot);
            if (d->extent[3] != 0) {
                box_bake_mark(s, mesh, slot);
            }
        }
    }
}

/** Remember a region that a change invalidated.
 *
 * @param s The state
 * @param center World center of the region
 * @param radius Radius of the region
 */
static void box_bake_push_sphere(
    box_bake_state_t *s,
    const float center[3],
    float radius)
{
    box_bake_sphere_t *sphere = box_vec_push(&s->changed);
    sphere->center[0] = center[0];
    sphere->center[1] = center[1];
    sphere->center[2] = center[2];
    sphere->radius = radius;
}

/** Build the probe descriptor of an instance from its transform and mesh bounds.
 *
 * @param m The mesh entry
 * @param inst The instance data
 * @param out The descriptor to fill
 */
static void box_bake_desc(
    const box_bake_mesh_t *m,
    const box_instance_t *inst,
    box_probe_desc_t *out)
{
    float center[3];
    float half[3];
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        center[axis] = (m->bounds_min[axis] + m->bounds_max[axis]) * 0.5f;
        half[axis] = (m->bounds_max[axis] - m->bounds_min[axis]) * 0.5f;
    }

    float radius = 0;
    int row;
    for (row = 0; row < 3; row ++) {
        const float *t = inst->transform + row * 4;
        out->position[row] = t[0] * center[0] + t[1] * center[1] + t[2] * center[2] + t[3];
        float extent = fabsf(t[0]) * half[0] + fabsf(t[1]) * half[1] + fabsf(t[2]) * half[2];
        out->extent[row] = extent;
        radius += extent * extent;
    }

    out->position[3] = sqrtf(radius);
    out->extent[3] = 1.0f;
}

/** Grow the static scene bounds with the bounds of a probe.
 *
 * @param s The state
 * @param d The probe descriptor
 */
static void box_bake_bounds_add(
    box_bake_state_t *s,
    const box_probe_desc_t *d)
{
    int axis;
    for (axis = 0; axis < 3; axis ++) {
        float lo = d->position[axis] - d->extent[axis];
        float hi = d->position[axis] + d->extent[axis];
        s->bounds_min[axis] = s->has_bounds && s->bounds_min[axis] < lo ? s->bounds_min[axis] : lo;
        s->bounds_max[axis] = s->has_bounds && s->bounds_max[axis] > hi ? s->bounds_max[axis] : hi;
    }

    s->has_bounds = true;
}

/** Take the mesh bounds of the packet's new meshes.
 *
 * @param s The state
 * @param p The packet
 */
static void box_bake_apply_meshes(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->mesh_create.count; i ++) {
        const box_mesh_cmd_t *cmd = box_vec_at(&p->mesh_create, i);
        box_bake_mesh_t *m = box_bake_state_mesh(s, cmd->handle);
        memcpy(m->bounds_min, cmd->bounds_min, sizeof(m->bounds_min));
        memcpy(m->bounds_max, cmd->bounds_max, sizeof(m->bounds_max));
    }
}

/** Take the largest range any static light or emitter reaches.
 *
 * @param s The state
 * @param p The packet
 */
static void box_bake_influence(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    float result = 0;
    uint32_t i;
    for (i = 0; i < p->bake_lights.count; i ++) {
        const box_light_t *light = box_vec_at(&p->bake_lights, i);
        result = light->range > result ? light->range : result;
    }

    for (i = 0; i < p->emitters.count; i ++) {
        const box_emitter_t *emitter = box_vec_at(&p->emitters, i);
        result = emitter->range > result ? emitter->range : result;
    }

    s->influence = result;
}

/** Resize the probes of every mesh whose static count changed.
 *
 * @param s The state
 * @param p The packet
 */
static void box_bake_apply_counts(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->static_counts.count; i ++) {
        const box_static_count_t *c = box_vec_at(&p->static_counts, i);
        box_bake_mesh_t *m = box_bake_state_mesh(s, c->mesh);
        uint32_t slot;
        for (slot = c->count; slot < m->descs.count; slot ++) {
            const box_probe_desc_t *d = box_vec_at(&m->descs, slot);
            if (d->extent[3] != 0) {
                box_bake_push_sphere(s, d->position, s->influence + d->position[3]);
                s->grid_dirty = true;
            }
        }

        while (m->descs.count < c->count) {
            memset(box_vec_push(&m->descs), 0, sizeof(box_probe_desc_t));
            *(uint8_t*)box_vec_push(&m->dirty) = 0;
        }

        m->descs.count = c->count;
        m->dirty.count = c->count;
    }
}

/** Update the probe descriptors of the static instances a packet changed.
 *
 * @param s The state
 * @param p The packet
 */
static void box_bake_apply_updates(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->static_updates.count; i ++) {
        const box_static_update_t *u = box_vec_at(&p->static_updates, i);
        box_bake_mesh_t *m = box_bake_state_mesh(s, u->mesh);
        while (m->descs.count <= u->slot) {
            memset(box_vec_push(&m->descs), 0, sizeof(box_probe_desc_t));
            *(uint8_t*)box_vec_push(&m->dirty) = 0;
        }

        box_probe_desc_t next;
        box_bake_desc(m, &u->data, &next);
        box_probe_desc_t *cur = box_vec_at(&m->descs, u->slot);
        if (memcmp(cur, &next, sizeof(box_probe_desc_t))) {
            if (cur->extent[3] != 0) {
                box_bake_push_sphere(s, cur->position, s->influence + cur->position[3]);
            }

            box_bake_push_sphere(s, next.position, s->influence + next.position[3]);
            *cur = next;
            box_bake_bounds_add(s, &next);
            m->upload_lo = u->slot < m->upload_lo ? u->slot : m->upload_lo;
            m->upload_hi = u->slot + 1 > m->upload_hi ? u->slot + 1 : m->upload_hi;
            s->grid_dirty = true;
        }

        box_bake_mark(s, u->mesh, u->slot);
    }
}

/** Invalidate the reach of every light that changed since the previous packet.
 *
 * @param s The state
 * @param p The packet
 */
static void box_bake_diff_lights(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    uint32_t shared = p->bake_lights.count < s->lights.count ?
        p->bake_lights.count : s->lights.count;
    uint32_t i;
    for (i = 0; i < shared; i ++) {
        const box_light_t *now = box_vec_at(&p->bake_lights, i);
        const box_light_t *was = box_vec_at(&s->lights, i);
        if (!memcmp(now, was, sizeof(box_light_t))) {
            continue;
        }

        box_bake_push_sphere(s, was->position, was->range);
        box_bake_push_sphere(s, now->position, now->range);
    }

    for (i = shared; i < p->bake_lights.count; i ++) {
        const box_light_t *now = box_vec_at(&p->bake_lights, i);
        box_bake_push_sphere(s, now->position, now->range);
    }

    for (i = shared; i < s->lights.count; i ++) {
        const box_light_t *was = box_vec_at(&s->lights, i);
        box_bake_push_sphere(s, was->position, was->range);
    }
}

/** Invalidate the reach of every emitter that changed since the previous packet.
 *
 * @param s The state
 * @param p The packet
 */
static void box_bake_diff_emitters(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    uint32_t shared = p->emitters.count < s->emitters.count ?
        p->emitters.count : s->emitters.count;
    uint32_t i;
    for (i = 0; i < shared; i ++) {
        const box_emitter_t *now = box_vec_at(&p->emitters, i);
        const box_emitter_t *was = box_vec_at(&s->emitters, i);
        if (!memcmp(now, was, sizeof(box_emitter_t))) {
            continue;
        }

        box_bake_push_sphere(s, was->position, was->range);
        box_bake_push_sphere(s, now->position, now->range);
    }

    for (i = shared; i < p->emitters.count; i ++) {
        const box_emitter_t *now = box_vec_at(&p->emitters, i);
        box_bake_push_sphere(s, now->position, now->range);
    }

    for (i = shared; i < s->emitters.count; i ++) {
        const box_emitter_t *was = box_vec_at(&s->emitters, i);
        box_bake_push_sphere(s, was->position, was->range);
    }
}

/** Test a probe against every region that changed this frame.
 *
 * @param s The state
 * @param d The probe descriptor
 * @return true when a changed region reaches the probe
 */
static bool box_bake_touched(
    const box_bake_state_t *s,
    const box_probe_desc_t *d)
{
    bool result = false;
    uint32_t i;
    for (i = 0; i < s->changed.count && !result; i ++) {
        const box_bake_sphere_t *sphere = box_vec_at(&s->changed, i);
        float reach = sphere->radius + d->position[3];
        float dx = sphere->center[0] - d->position[0];
        float dy = sphere->center[1] - d->position[1];
        float dz = sphere->center[2] - d->position[2];
        result = dx * dx + dy * dy + dz * dz <= reach * reach;
    }

    return result;
}

/** Queue every probe a changed region reaches and drop the regions.
 *
 * @param s The state
 */
static void box_bake_mark_changed(
    box_bake_state_t *s)
{
    uint32_t mesh;
    for (mesh = 0; mesh < s->meshes.count && s->changed.count; mesh ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, mesh);
        uint32_t slot;
        for (slot = 0; slot < m->descs.count; slot ++) {
            const box_probe_desc_t *d = box_vec_at(&m->descs, slot);
            if (d->extent[3] == 0) {
                continue;
            }

            if (box_bake_touched(s, d)) {
                box_bake_mark(s, mesh, slot);
            }
        }
    }

    box_vec_clear(&s->changed);
}

/** Rebuild the voxel grid when the scene outgrew it or the settings changed.
 *
 * @param s The state
 * @param p The packet
 */
static void box_bake_update_grid(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    uint32_t dim[3] = { p->bake.x, p->bake.y, p->bake.z };
    bool rebuild = !s->grid.dim[0] || dim[0] != s->grid.dim[0] ||
        dim[1] != s->grid.dim[1] || dim[2] != s->grid.dim[2];
    int axis;
    for (axis = 0; axis < 3 && !rebuild; axis ++) {
        float hi = s->grid.min[axis] + s->grid.cell[axis] * (float)s->grid.dim[axis];
        rebuild = s->bounds_min[axis] < s->grid.min[axis] || s->bounds_max[axis] > hi;
    }

    if (!s->has_bounds || !rebuild) {
        goto done;
    }

    float min[3];
    float max[3];
    for (axis = 0; axis < 3; axis ++) {
        float pad = (s->bounds_max[axis] - s->bounds_min[axis]) * BOX_BAKE_PADDING;
        pad = pad > BOX_BAKE_EPSILON ? pad : BOX_BAKE_EPSILON;
        min[axis] = s->bounds_min[axis] - pad;
        max[axis] = s->bounds_max[axis] + pad;
    }

    box_voxel_grid_build(min, max, dim, &s->grid);
    s->grid_dirty = true;
    box_bake_mark_all(s);
done:
    return;
}

/** Count the probes of every mesh.
 *
 * @param s The state
 */
static void box_bake_count(
    box_bake_state_t *s)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < s->meshes.count; i ++) {
        result += ((box_bake_mesh_t*)box_vec_at(&s->meshes, i))->descs.count;
    }

    s->probe_count = result;
}

void box_bake_state_apply(
    box_bake_state_t *s,
    const box_packet_t *p)
{
    if (!p->bake.enabled) {
        if (s->enabled) {
            box_bake_reset(s);
        }

        s->enabled = false;
        goto done;
    }

    s->enabled = true;
    s->eye[0] = p->view.eye[0];
    s->eye[1] = p->view.eye[1];
    s->eye[2] = p->view.eye[2];
    box_bake_apply_meshes(s, p);
    box_bake_influence(s, p);
    box_bake_apply_counts(s, p);
    box_bake_apply_updates(s, p);
    box_bake_diff_lights(s, p);
    box_bake_diff_emitters(s, p);
    box_bake_mark_changed(s);
    box_bake_update_grid(s, p);
    box_bake_copy(&s->lights, &p->bake_lights);
    box_bake_copy(&s->emitters, &p->emitters);
    box_bake_count(s);
done:
    return;
}

/** Order two bake jobs by their distance to the camera.
 *
 * @param a The first job
 * @param b The second job
 * @return -1, 0 or 1 as the first job is nearer, equal or further
 */
static int box_bake_job_cmp(
    const void *a,
    const void *b)
{
    const box_bake_job_t *x = a;
    const box_bake_job_t *y = b;
    int result = 0;
    if (x->distance < y->distance) {
        result = -1;
    } else if (x->distance > y->distance) {
        result = 1;
    }

    return result;
}

/** Probe a job points at, NULL when its slot no longer holds one.
 *
 * @param s The state
 * @param job The job
 * @return The descriptor, or NULL
 */
static const box_probe_desc_t* box_bake_job_probe(
    box_bake_state_t *s,
    const box_bake_job_t *job)
{
    const box_probe_desc_t *result = NULL;
    box_bake_mesh_t *m = box_bake_state_mesh(s, job->mesh);
    if (job->slot < m->descs.count) {
        const box_probe_desc_t *d = box_vec_at(&m->descs, job->slot);
        result = d->extent[3] != 0 ? d : NULL;
    }

    return result;
}

uint32_t box_bake_state_take(
    box_bake_state_t *s,
    uint32_t budget)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < s->meshes.count; i ++) {
        box_vec_clear(&((box_bake_mesh_t*)box_vec_at(&s->meshes, i))->jobs);
    }

    if (!s->dirty.count || !budget) {
        goto done;
    }

    box_bake_job_t *jobs = s->dirty.data;
    for (i = 0; i < s->dirty.count; i ++) {
        const box_probe_desc_t *d = box_bake_job_probe(s, &jobs[i]);
        float dx = d ? d->position[0] - s->eye[0] : 0.0f;
        float dy = d ? d->position[1] - s->eye[1] : 0.0f;
        float dz = d ? d->position[2] - s->eye[2] : 0.0f;
        jobs[i].distance = dx * dx + dy * dy + dz * dz;
    }

    qsort(jobs, s->dirty.count, sizeof(box_bake_job_t), box_bake_job_cmp);

    uint32_t kept = 0;
    for (i = 0; i < s->dirty.count; i ++) {
        box_bake_mesh_t *m = box_bake_state_mesh(s, jobs[i].mesh);
        if (!box_bake_job_probe(s, &jobs[i])) {
            if (jobs[i].slot < m->dirty.count) {
                *(uint8_t*)box_vec_at(&m->dirty, jobs[i].slot) = 0;
            }

            continue;
        }

        if (result < budget) {
            *(uint32_t*)box_vec_push(&m->jobs) = jobs[i].slot;
            *(uint8_t*)box_vec_at(&m->dirty, jobs[i].slot) = 0;
            result ++;
            continue;
        }

        jobs[kept ++] = jobs[i];
    }

    s->dirty.count = kept;
done:
    return result;
}

/** Create a GPU buffer.
 *
 * @param r The renderer
 * @param usage Buffer usage flags
 * @param size Size in bytes
 * @return The buffer
 */
static WGPUBuffer box_bake_buffer(
    box_renderer_t *r,
    WGPUBufferUsage usage,
    uint64_t size)
{
    WGPUBufferDescriptor desc = { .usage = usage, .size = size };

    return wgpuDeviceCreateBuffer(r->device, &desc);
}

/** Compile a WGSL source into a compute pipeline over two bind group layouts.
 *
 * @param r The renderer
 * @param source The WGSL source
 * @param entry Name of the compute entry point
 * @param globals Layout of the first bind group
 * @param meshes Layout of the second bind group
 * @return The pipeline
 */
static WGPUComputePipeline box_bake_pipeline(
    box_renderer_t *r,
    const char *source,
    const char *entry,
    WGPUBindGroupLayout globals,
    WGPUBindGroupLayout meshes)
{
    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = source, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor shader_desc = { .nextInChain = (WGPUChainedStruct*)&wgsl };
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(r->device, &shader_desc);

    WGPUBindGroupLayout layouts[2] = { globals, meshes };
    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 2,
        .bindGroupLayouts = layouts
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device, &layout_desc);

    WGPUComputePipelineDescriptor desc = {
        .layout = layout,
        .compute = {
            .module = shader,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    };
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(shader);

    return result;
}

/** Create the bind group layouts of the bake, grid and mesh shader probes.
 *
 * @param r The renderer
 */
static void box_bake_layouts(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry bake_entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_bake_uniform_t)
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutDescriptor bake_desc = { .entryCount = 4, .entries = bake_entries };
    r->bake_layout = wgpuDeviceCreateBindGroupLayout(r->device, &bake_desc);

    WGPUBindGroupLayoutEntry grid_entries[2] = {
        bake_entries[0], {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }
    };
    WGPUBindGroupLayoutDescriptor grid_desc = { .entryCount = 2, .entries = grid_entries };
    r->grid_layout = wgpuDeviceCreateBindGroupLayout(r->device, &grid_desc);

    WGPUBindGroupLayoutEntry mesh_entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_bake_info_t)
            }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutDescriptor mesh_desc = { .entryCount = 4, .entries = mesh_entries };
    r->bake_mesh_layout = wgpuDeviceCreateBindGroupLayout(r->device, &mesh_desc);

    WGPUBindGroupLayoutDescriptor grid_mesh_desc = { .entryCount = 2, .entries = mesh_entries };
    r->grid_mesh_layout = wgpuDeviceCreateBindGroupLayout(r->device, &grid_mesh_desc);

    WGPUBindGroupLayoutEntry probe_entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    };
    WGPUBindGroupLayoutDescriptor probe_desc = { .entryCount = 1, .entries = &probe_entry };
    r->probe_layout = wgpuDeviceCreateBindGroupLayout(r->device, &probe_desc);
}

/** Rebuild the global bind groups over the current buffers.
 *
 * @param r The renderer
 */
static void box_bake_groups(
    box_renderer_t *r)
{
    if (r->bake_group) wgpuBindGroupRelease(r->bake_group);
    if (r->grid_group) wgpuBindGroupRelease(r->grid_group);

    uint64_t occupancy_size = (uint64_t)r->occupancy_words * sizeof(uint32_t);
    WGPUBindGroupEntry bake_entries[4] = {
        { .binding = 0, .buffer = r->bake_uniform, .size = sizeof(box_bake_uniform_t) },
        { .binding = 1, .buffer = r->bake_lights,
            .size = (uint64_t)r->bake_light_capacity * sizeof(box_light_t) },
        { .binding = 2, .buffer = r->bake_emitters,
            .size = (uint64_t)r->bake_emitter_capacity * sizeof(box_emitter_t) },
        { .binding = 3, .buffer = r->occupancy, .size = occupancy_size }
    };
    WGPUBindGroupDescriptor bake_desc = {
        .layout = r->bake_layout,
        .entryCount = 4,
        .entries = bake_entries
    };
    r->bake_group = wgpuDeviceCreateBindGroup(r->device, &bake_desc);

    WGPUBindGroupEntry grid_entries[2] = {
        bake_entries[0],
        { .binding = 1, .buffer = r->occupancy, .size = occupancy_size }
    };
    WGPUBindGroupDescriptor grid_desc = {
        .layout = r->grid_layout,
        .entryCount = 2,
        .entries = grid_entries
    };
    r->grid_group = wgpuDeviceCreateBindGroup(r->device, &grid_desc);
}

/** Grow the global light, emitter and occupancy buffers for a packet.
 *
 * @param r The renderer
 * @param p The packet
 * @return true when a buffer was recreated
 */
static bool box_bake_grow(
    box_renderer_t *r,
    const box_packet_t *p)
{
    bool result = false;
    WGPUBufferUsage storage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    if (p->bake_lights.count > r->bake_light_capacity) {
        uint32_t grown = r->bake_light_capacity ? r->bake_light_capacity * 2 : BOX_BAKE_CAPACITY;
        grown = grown < p->bake_lights.count ? p->bake_lights.count : grown;
        if (r->bake_lights) wgpuBufferRelease(r->bake_lights);
        r->bake_lights = box_bake_buffer(r, storage, (uint64_t)grown * sizeof(box_light_t));
        r->bake_light_capacity = grown;
        result = true;
    }

    if (p->emitters.count > r->bake_emitter_capacity) {
        uint32_t grown = r->bake_emitter_capacity ?
            r->bake_emitter_capacity * 2 : BOX_BAKE_CAPACITY;
        grown = grown < p->emitters.count ? p->emitters.count : grown;
        if (r->bake_emitters) wgpuBufferRelease(r->bake_emitters);
        r->bake_emitters = box_bake_buffer(r, storage, (uint64_t)grown * sizeof(box_emitter_t));
        r->bake_emitter_capacity = grown;
        result = true;
    }

    uint32_t words = box_voxel_words(&r->bake.grid);
    words = words ? words : 1;
    if (words != r->occupancy_words) {
        if (r->occupancy) wgpuBufferRelease(r->occupancy);
        r->occupancy = box_bake_buffer(r, storage, (uint64_t)words * sizeof(uint32_t));
        r->occupancy_words = words;
        r->bake.grid_dirty = true;
        result = true;
    }

    return result;
}

/** Upload the uniforms, the static lights and the emitters of a packet.
 *
 * @param r The renderer
 * @param p The packet
 */
static void box_bake_upload(
    box_renderer_t *r,
    const box_packet_t *p)
{
    box_bake_uniform_t u = {{0}};
    int axis;
    for (axis = 0; axis < 4; axis ++) {
        u.grid_min[axis] = r->bake.grid.min[axis];
        u.grid_cell[axis] = r->bake.grid.cell[axis];
        u.grid_dim[axis] = r->bake.grid.dim[axis];
    }

    u.light_count = p->bake_lights.count;
    u.emitter_count = p->emitters.count;
    u.steps = p->bake.steps;
    wgpuQueueWriteBuffer(r->gpu_queue, r->bake_uniform, 0, &u, sizeof(box_bake_uniform_t));

    if (p->bake_lights.count) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->bake_lights, 0, p->bake_lights.data,
            (size_t)p->bake_lights.count * sizeof(box_light_t));
    }

    if (p->emitters.count) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->bake_emitters, 0, p->emitters.data,
            (size_t)p->emitters.count * sizeof(box_emitter_t));
    }
}

/** Create or grow the probe buffers and bind groups of one mesh.
 *
 * @param r The renderer
 * @param m The mesh entry
 * @param capacity Probes the buffers must hold
 * @param jobs Jobs the job buffer must hold
 */
static void box_bake_mesh_grow(
    box_renderer_t *r,
    box_gpu_mesh_t *m,
    uint32_t capacity,
    uint32_t jobs)
{
    if (capacity > m->probe_capacity) {
        uint32_t grown = m->probe_capacity ? m->probe_capacity * 2 : BOX_BAKE_CAPACITY;
        grown = grown < capacity ? capacity : grown;
        WGPUBuffer descs = box_bake_buffer(r,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            (uint64_t)grown * sizeof(box_probe_desc_t));
        WGPUBuffer probes = box_bake_buffer(r,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
            (uint64_t)grown * BOX_BAKE_PROBE_SIZE);
        if (m->probes) {
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, m->probes, 0, probes, 0,
                (uint64_t)m->probe_capacity * BOX_BAKE_PROBE_SIZE);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
            wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd);
            wgpuCommandEncoderRelease(encoder);
            wgpuBufferRelease(m->probes);
            wgpuBufferRelease(m->probe_descs);
        }

        m->probe_descs = descs;
        m->probes = probes;
        m->probe_capacity = grown;
    }

    if (jobs > m->job_capacity) {
        if (m->probe_jobs) wgpuBufferRelease(m->probe_jobs);
        m->probe_jobs = box_bake_buffer(r,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            (uint64_t)jobs * sizeof(uint32_t));
        m->job_capacity = jobs;
    }

    if (!m->probe_info) {
        m->probe_info = box_bake_buffer(r,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, sizeof(box_bake_info_t));
    }
}

/** Rebuild the bake and draw bind groups of one mesh.
 *
 * @param r The renderer
 * @param m The mesh entry
 */
static void box_bake_mesh_groups(
    box_renderer_t *r,
    box_gpu_mesh_t *m)
{
    if (m->bake_group) wgpuBindGroupRelease(m->bake_group);
    if (m->grid_group) wgpuBindGroupRelease(m->grid_group);
    if (m->probe_group) wgpuBindGroupRelease(m->probe_group);

    uint64_t probe_size = (uint64_t)m->probe_capacity * BOX_BAKE_PROBE_SIZE;
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = m->probe_descs,
            .size = (uint64_t)m->probe_capacity * sizeof(box_probe_desc_t) },
        { .binding = 1, .buffer = m->probe_info, .size = sizeof(box_bake_info_t) },
        { .binding = 2, .buffer = m->probes, .size = probe_size },
        { .binding = 3, .buffer = m->probe_jobs,
            .size = (uint64_t)m->job_capacity * sizeof(uint32_t) }
    };
    WGPUBindGroupDescriptor bake_desc = {
        .layout = r->bake_mesh_layout,
        .entryCount = 4,
        .entries = entries
    };
    m->bake_group = wgpuDeviceCreateBindGroup(r->device, &bake_desc);

    WGPUBindGroupDescriptor grid_desc = {
        .layout = r->grid_mesh_layout,
        .entryCount = 2,
        .entries = entries
    };
    m->grid_group = wgpuDeviceCreateBindGroup(r->device, &grid_desc);

    WGPUBindGroupEntry probe_entry = { .binding = 0, .buffer = m->probes, .size = probe_size };
    WGPUBindGroupDescriptor probe_desc = {
        .layout = r->probe_layout,
        .entryCount = 1,
        .entries = &probe_entry
    };
    m->probe_group = wgpuDeviceCreateBindGroup(r->device, &probe_desc);
}

/** Upload the probe descriptors, counts and job lists of every mesh.
 *
 * @param r The renderer
 * @param budget Jobs a mesh job buffer must hold
 */
static void box_bake_meshes(
    box_renderer_t *r,
    uint32_t budget)
{
    uint32_t i;
    for (i = 0; i < r->bake.meshes.count && i < r->meshes.count; i ++) {
        box_bake_mesh_t *entry = box_vec_at(&r->bake.meshes, i);
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        if (!m->alive || !entry->descs.count) {
            continue;
        }

        uint32_t capacity = m->probe_capacity;
        box_bake_mesh_grow(r, m, entry->descs.count, budget);
        if (capacity != m->probe_capacity || !m->bake_group) {
            box_bake_mesh_groups(r, m);
            entry->upload_lo = 0;
            entry->upload_hi = entry->descs.count;
        }

        if (entry->upload_lo < entry->upload_hi) {
            uint32_t first = entry->upload_lo;
            uint32_t count = entry->upload_hi - first;
            wgpuQueueWriteBuffer(r->gpu_queue, m->probe_descs,
                (uint64_t)first * sizeof(box_probe_desc_t),
                box_vec_at(&entry->descs, first), (size_t)count * sizeof(box_probe_desc_t));
        }

        entry->upload_lo = UINT32_MAX;
        entry->upload_hi = 0;

        box_bake_info_t info = { entry->descs.count, entry->jobs.count, {0, 0} };
        wgpuQueueWriteBuffer(r->gpu_queue, m->probe_info, 0, &info, sizeof(box_bake_info_t));
        if (entry->jobs.count) {
            wgpuQueueWriteBuffer(r->gpu_queue, m->probe_jobs, 0, entry->jobs.data,
                (size_t)entry->jobs.count * sizeof(uint32_t));
        }
    }
}

/** Record a compute pass that runs one pipeline over every mesh with probes.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param pipeline The pipeline to run
 * @param globals The global bind group
 * @param bake True to dispatch over the job lists, false over every probe
 */
static void box_bake_pass(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUComputePipeline pipeline,
    WGPUBindGroup globals,
    bool bake)
{
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, globals, 0, NULL);

    uint32_t i;
    for (i = 0; i < r->bake.meshes.count && i < r->meshes.count; i ++) {
        box_bake_mesh_t *entry = box_vec_at(&r->bake.meshes, i);
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        uint32_t count = bake ? entry->jobs.count : entry->descs.count;
        if (!m->alive || !m->bake_group || !count) {
            continue;
        }

        wgpuComputePassEncoderSetBindGroup(pass, 1, bake ? m->bake_group : m->grid_group,
            0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass,
            (count + BOX_BAKE_GROUP - 1) / BOX_BAKE_GROUP, 1, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}

void box_bake_init(
    box_renderer_t *r)
{
    box_bake_state_init(&r->bake);
    box_bake_layouts(r);

    WGPUBufferUsage storage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    r->bake_uniform = box_bake_buffer(r,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, sizeof(box_bake_uniform_t));
    r->bake_lights = box_bake_buffer(r, storage,
        (uint64_t)BOX_BAKE_CAPACITY * sizeof(box_light_t));
    r->bake_light_capacity = BOX_BAKE_CAPACITY;
    r->bake_emitters = box_bake_buffer(r, storage,
        (uint64_t)BOX_BAKE_CAPACITY * sizeof(box_emitter_t));
    r->bake_emitter_capacity = BOX_BAKE_CAPACITY;
    r->occupancy = box_bake_buffer(r, storage, sizeof(uint32_t));
    r->occupancy_words = 1;
    r->probe_dummy = box_bake_buffer(r, storage, BOX_BAKE_PROBE_SIZE);

    box_probe_t zero = {{0}};
    wgpuQueueWriteBuffer(r->gpu_queue, r->probe_dummy, 0, &zero, sizeof(box_probe_t));

    WGPUBindGroupEntry dummy = { .binding = 0, .buffer = r->probe_dummy,
        .size = BOX_BAKE_PROBE_SIZE };
    WGPUBindGroupDescriptor dummy_desc = {
        .layout = r->probe_layout,
        .entryCount = 1,
        .entries = &dummy
    };
    r->probe_dummy_group = wgpuDeviceCreateBindGroup(r->device, &dummy_desc);

    box_bake_groups(r);
    r->grid_pipeline = box_bake_pipeline(r, box_bake_grid_wgsl, "cs_grid", r->grid_layout,
        r->grid_mesh_layout);
    r->bake_pipeline = box_bake_pipeline(r, box_bake_shader_wgsl, "cs_bake", r->bake_layout,
        r->bake_mesh_layout);
}

void box_bake_update(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder)
{
    box_bake_state_apply(&r->bake, p);
    if (!r->bake.enabled) {
        goto done;
    }

    assert(p->bake.budget);
    uint32_t budget = p->bake.budget;
    uint32_t taken = box_bake_state_take(&r->bake, budget);
    if (box_bake_grow(r, p)) {
        box_bake_groups(r);
    }

    box_bake_upload(r, p);
    box_bake_meshes(r, budget);

    if (r->bake.grid_dirty) {
        wgpuCommandEncoderClearBuffer(encoder, r->occupancy, 0,
            (uint64_t)r->occupancy_words * sizeof(uint32_t));
        box_bake_pass(r, encoder, r->grid_pipeline, r->grid_group, false);
        r->bake.grid_dirty = false;
    }

    if (taken) {
        box_bake_pass(r, encoder, r->bake_pipeline, r->bake_group, true);
    }

done:
    return;
}

WGPUBindGroup box_bake_group(
    box_renderer_t *r,
    uint32_t mesh,
    bool baked)
{
    WGPUBindGroup result = r->probe_dummy_group;
    if (baked && mesh < r->meshes.count) {
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, mesh);
        result = m->probe_group ? m->probe_group : result;
    }

    return result;
}

void box_bake_mesh_release(
    box_gpu_mesh_t *m)
{
    if (m->bake_group) wgpuBindGroupRelease(m->bake_group);
    if (m->grid_group) wgpuBindGroupRelease(m->grid_group);
    if (m->probe_group) wgpuBindGroupRelease(m->probe_group);
    if (m->probe_descs) wgpuBufferRelease(m->probe_descs);
    if (m->probes) wgpuBufferRelease(m->probes);
    if (m->probe_jobs) wgpuBufferRelease(m->probe_jobs);
    if (m->probe_info) wgpuBufferRelease(m->probe_info);
    m->bake_group = NULL;
    m->grid_group = NULL;
    m->probe_group = NULL;
    m->probe_descs = NULL;
    m->probes = NULL;
    m->probe_jobs = NULL;
    m->probe_info = NULL;
    m->probe_capacity = 0;
    m->job_capacity = 0;
}

void box_bake_fini(
    box_renderer_t *r)
{
    box_bake_state_fini(&r->bake);
    if (r->bake_group) wgpuBindGroupRelease(r->bake_group);
    if (r->grid_group) wgpuBindGroupRelease(r->grid_group);
    if (r->probe_dummy_group) wgpuBindGroupRelease(r->probe_dummy_group);
    if (r->bake_layout) wgpuBindGroupLayoutRelease(r->bake_layout);
    if (r->grid_layout) wgpuBindGroupLayoutRelease(r->grid_layout);
    if (r->bake_mesh_layout) wgpuBindGroupLayoutRelease(r->bake_mesh_layout);
    if (r->grid_mesh_layout) wgpuBindGroupLayoutRelease(r->grid_mesh_layout);
    if (r->probe_layout) wgpuBindGroupLayoutRelease(r->probe_layout);
    if (r->bake_pipeline) wgpuComputePipelineRelease(r->bake_pipeline);
    if (r->grid_pipeline) wgpuComputePipelineRelease(r->grid_pipeline);
    if (r->bake_uniform) wgpuBufferRelease(r->bake_uniform);
    if (r->bake_lights) wgpuBufferRelease(r->bake_lights);
    if (r->bake_emitters) wgpuBufferRelease(r->bake_emitters);
    if (r->occupancy) wgpuBufferRelease(r->occupancy);
    if (r->probe_dummy) wgpuBufferRelease(r->probe_dummy);
    r->bake_group = NULL;
    r->grid_group = NULL;
    r->probe_dummy_group = NULL;
    r->bake_layout = NULL;
    r->grid_layout = NULL;
    r->bake_mesh_layout = NULL;
    r->grid_mesh_layout = NULL;
    r->probe_layout = NULL;
    r->bake_pipeline = NULL;
    r->grid_pipeline = NULL;
    r->bake_uniform = NULL;
    r->bake_lights = NULL;
    r->bake_emitters = NULL;
    r->occupancy = NULL;
    r->probe_dummy = NULL;
    r->bake_light_capacity = 0;
    r->bake_emitter_capacity = 0;
    r->occupancy_words = 0;
}
