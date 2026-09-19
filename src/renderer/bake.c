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

/** Probes a probe buffer starts out with */
#define BOX_BAKE_CAPACITY (64)

/** Part of its size the scene bounds are padded with before the grid is built */
#define BOX_BAKE_PADDING (0.05f)

/** Bytes of one probe in the GPU storage buffer */
#define BOX_BAKE_PROBE_SIZE ((uint64_t)sizeof(box_probe_t))

/** Jobs the first selection pass of a frame orders by distance */
#define BOX_BAKE_ORDER_RUN (64)

box_bake_mesh_t* box_bake_state_mesh(
    box_bake_state_t *s,
    uint32_t mesh)
{
    while (s->meshes.count <= mesh) {
        box_bake_mesh_t *entry = box_vec_push(&s->meshes);
        memset(entry, 0, sizeof(box_bake_mesh_t));
        box_vec_init(&entry->descs, sizeof(box_probe_desc_t));
        box_vec_init(&entry->dirty, sizeof(uint8_t));
        box_vec_init(&entry->jobs, sizeof(box_bake_slot_t));
        entry->upload_lo = UINT32_MAX;
        entry->stale_lo = UINT32_MAX;
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
        m->stale_lo = UINT32_MAX;
        m->stale_hi = 0;
        m->probe_count = 0;
        m->pool_dirty = false;
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

/** Check whether two world points lie closer together than a distance.
 *
 * @param a The first point
 * @param b The second point
 * @param reach The distance
 * @return true when the points are closer than the distance
 */
static bool box_bake_near(
    const float a[3],
    const float b[3],
    float reach)
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return dx * dx + dy * dy + dz * dz < reach * reach;
}

/** Check whether a static light or emitter of a packet reaches a bounding sphere.
 *
 * A light can only be occluded by geometry it reaches itself, so an instance no
 * light reaches changes nothing outside its own probes when it moves.
 *
 * @param p The packet
 * @param center World center of the sphere
 * @param radius Radius of the sphere
 * @return true when a static light or emitter reaches the sphere
 */
static bool box_bake_reaches(
    const box_packet_t *p,
    const float center[3],
    float radius)
{
    bool result = false;
    uint32_t i;
    for (i = 0; i < p->bake_lights.count && !result; i ++) {
        const box_light_t *light = box_vec_at(&p->bake_lights, i);
        result = box_bake_near(light->position, center, light->range + radius);
    }

    for (i = 0; i < p->emitters.count && !result; i ++) {
        const box_emitter_t *emitter = box_vec_at(&p->emitters, i);
        result = box_bake_near(emitter->position, center, emitter->range + radius);
    }

    return result;
}

/** Widen the upload range of a mesh with a descriptor, or park it as stale.
 *
 * The probes of an instance no light reaches stay zero whatever its descriptor
 * holds, so its upload waits until a light or emitter comes into range.
 *
 * @param m The mesh entry
 * @param slot The descriptor
 * @param lit A static light or emitter reaches the instance
 */
static void box_bake_touch(
    box_bake_mesh_t *m,
    uint32_t slot,
    bool lit)
{
    uint32_t *lo = lit ? &m->upload_lo : &m->stale_lo;
    uint32_t *hi = lit ? &m->upload_hi : &m->stale_hi;
    *lo = slot < *lo ? slot : *lo;
    *hi = slot + 1 > *hi ? slot + 1 : *hi;
}

/** Move the descriptors parked while unlit into the upload range of every mesh.
 *
 * @param s The state
 */
static void box_bake_flush(
    box_bake_state_t *s)
{
    uint32_t i;
    for (i = 0; i < s->meshes.count; i ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, i);
        if (m->stale_lo >= m->stale_hi) {
            continue;
        }

        m->upload_lo = m->stale_lo < m->upload_lo ? m->stale_lo : m->upload_lo;
        m->upload_hi = m->stale_hi > m->upload_hi ? m->stale_hi : m->upload_hi;
        m->stale_lo = UINT32_MAX;
        m->stale_hi = 0;
        s->grid_dirty = true;
    }
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
    box_bake_flush(s);

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
 * @param s The state
 * @param m The mesh entry
 * @param inst The instance data
 * @param out The descriptor to fill, its pool index left at zero
 */
static void box_bake_desc(
    const box_bake_state_t *s,
    const box_bake_mesh_t *m,
    const box_instance_t *inst,
    box_probe_desc_t *out)
{
    memset(out, 0, sizeof(box_probe_desc_t));

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

    uint32_t dim[3];
    box_probe_dims(out->extent, s->spacing, s->probe_max, dim);
    for (axis = 0; axis < 3; axis ++) {
        for (row = 0; row < 3; row ++) {
            out->span[axis][row] = inst->transform[row * 4 + axis] * half[axis] * 2.0f;
        }

        out->grid[axis + 1] = dim[axis];
    }
}

/** Lay the probe pool of a mesh out again and queue every instance whose probes moved.
 *
 * @param s The state
 * @param mesh Mesh handle
 */
static void box_bake_layout(
    box_bake_state_t *s,
    uint32_t mesh)
{
    box_bake_mesh_t *m = box_vec_at(&s->meshes, mesh);
    uint32_t base = 0;
    uint32_t slot;
    for (slot = 0; slot < m->descs.count; slot ++) {
        box_probe_desc_t *d = box_vec_at(&m->descs, slot);
        uint32_t count = box_probe_count(d);
        if (count && d->grid[0] != base) {
            d->grid[0] = base;
            m->upload_lo = slot < m->upload_lo ? slot : m->upload_lo;
            m->upload_hi = slot + 1 > m->upload_hi ? slot + 1 : m->upload_hi;
            box_bake_mark(s, mesh, slot);
        }

        base += count;
    }

    m->probe_count = base;
    m->pool_dirty = false;
}

/** Lay the pool of every mesh whose probe grids changed out again.
 *
 * @param s The state
 */
static void box_bake_layout_all(
    box_bake_state_t *s)
{
    uint32_t mesh;
    for (mesh = 0; mesh < s->meshes.count; mesh ++) {
        if (((box_bake_mesh_t*)box_vec_at(&s->meshes, mesh))->pool_dirty) {
            box_bake_layout(s, mesh);
        }
    }
}

/** Give every instance the probe grid of the current settings.
 *
 * @param s The state
 */
static void box_bake_respace(
    box_bake_state_t *s)
{
    uint32_t mesh;
    for (mesh = 0; mesh < s->meshes.count; mesh ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, mesh);
        uint32_t slot;
        for (slot = 0; slot < m->descs.count; slot ++) {
            box_probe_desc_t *d = box_vec_at(&m->descs, slot);
            if (!box_probe_count(d)) {
                continue;
            }

            uint32_t dim[3];
            box_probe_dims(d->extent, s->spacing, s->probe_max, dim);
            int axis;
            for (axis = 0; axis < 3; axis ++) {
                d->grid[axis + 1] = dim[axis];
            }
        }

        m->pool_dirty = true;
        m->upload_lo = 0;
        m->upload_hi = m->descs.count;
    }

    box_bake_mark_all(s);
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
            if (d->extent[3] == 0) {
                continue;
            }

            if (box_bake_reaches(p, d->position, d->position[3])) {
                box_bake_push_sphere(s, d->position, s->influence + d->position[3]);
                s->grid_dirty = true;
            }

            m->pool_dirty = true;
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
        box_bake_desc(s, m, &u->data, &next);
        box_probe_desc_t *cur = box_vec_at(&m->descs, u->slot);
        next.grid[0] = cur->grid[0];
        if (memcmp(cur, &next, sizeof(box_probe_desc_t))) {
            bool lit = box_bake_reaches(p, next.position, next.position[3]);
            if (cur->extent[3] != 0) {
                lit = lit || box_bake_reaches(p, cur->position, cur->position[3]);
            }

            if (lit) {
                if (cur->extent[3] != 0) {
                    box_bake_push_sphere(s, cur->position, s->influence + cur->position[3]);
                }

                box_bake_push_sphere(s, next.position, s->influence + next.position[3]);
                s->grid_dirty = true;
            }

            bool grid_same = cur->extent[3] != 0 &&
                !memcmp(cur->grid, next.grid, sizeof(cur->grid));
            m->pool_dirty |= box_probe_count(cur) != box_probe_count(&next);
            *cur = next;
            box_bake_bounds_add(s, &next);
            box_bake_touch(m, u->slot, lit || !grid_same);
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

/** Order two changed regions by the x coordinate of their center.
 *
 * @param a The first region
 * @param b The second region
 * @return -1, 0 or 1 as the first center is smaller, equal or larger
 */
static int box_bake_sphere_cmp(
    const void *a,
    const void *b)
{
    const box_bake_sphere_t *x = a;
    const box_bake_sphere_t *y = b;
    int result = 0;
    if (x->center[0] < y->center[0]) {
        result = -1;
    } else if (x->center[0] > y->center[0]) {
        result = 1;
    }

    return result;
}

/** First region of a sorted array whose center reaches a coordinate.
 *
 * @param spheres The regions, sorted by the x coordinate of their center
 * @param count Regions in the array
 * @param x The coordinate
 * @return The index of the first region, count when none does
 */
static uint32_t box_bake_sphere_first(
    const box_bake_sphere_t *spheres,
    uint32_t count,
    float x)
{
    uint32_t lo = 0;
    uint32_t hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (spheres[mid].center[0] < x) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

/** Test a probe against the regions that changed this frame near it.
 *
 * @param spheres The regions, sorted by the x coordinate of their center
 * @param count Regions in the array
 * @param first Index of the first region the probe can reach
 * @param d The probe descriptor
 * @return true when a changed region reaches the probe
 */
static bool box_bake_touched(
    const box_bake_sphere_t *spheres,
    uint32_t count,
    uint32_t first,
    const box_probe_desc_t *d)
{
    bool result = false;
    uint32_t i;
    for (i = first; i < count && !result; i ++) {
        const box_bake_sphere_t *sphere = &spheres[i];
        float reach = sphere->radius + d->position[3];
        float dx = sphere->center[0] - d->position[0];
        if (dx > reach) {
            break;
        }

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
    if (!s->changed.count) {
        goto done;
    }

    box_bake_flush(s);

    box_bake_sphere_t *spheres = s->changed.data;
    uint32_t count = s->changed.count;
    qsort(spheres, count, sizeof(box_bake_sphere_t), box_bake_sphere_cmp);

    float widest = 0;
    uint32_t i;
    for (i = 0; i < count; i ++) {
        widest = spheres[i].radius > widest ? spheres[i].radius : widest;
    }

    uint32_t mesh;
    for (mesh = 0; mesh < s->meshes.count; mesh ++) {
        box_bake_mesh_t *m = box_vec_at(&s->meshes, mesh);
        uint32_t slot;
        for (slot = 0; slot < m->descs.count; slot ++) {
            const box_probe_desc_t *d = box_vec_at(&m->descs, slot);
            if (d->extent[3] == 0) {
                continue;
            }

            float reach = widest + d->position[3];
            uint32_t first = box_bake_sphere_first(spheres, count, d->position[0] - reach);
            if (box_bake_touched(spheres, count, first, d)) {
                box_bake_mark(s, mesh, slot);
            }
        }
    }

    box_vec_clear(&s->changed);
done:
    return;
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

/** Count the probes in the pool of every mesh.
 *
 * @param s The state
 */
static void box_bake_count(
    box_bake_state_t *s)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < s->meshes.count; i ++) {
        result += ((box_bake_mesh_t*)box_vec_at(&s->meshes, i))->probe_count;
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

    bool respace = s->enabled &&
        (s->spacing != p->bake.spacing || s->probe_max != p->bake.probe_max);
    s->enabled = true;
    s->spacing = p->bake.spacing;
    s->probe_max = p->bake.probe_max;
    s->eye[0] = p->view.eye[0];
    s->eye[1] = p->view.eye[1];
    s->eye[2] = p->view.eye[2];
    if (respace) {
        box_bake_respace(s);
    }

    box_bake_apply_meshes(s, p);
    box_bake_influence(s, p);
    box_bake_apply_counts(s, p);
    box_bake_apply_updates(s, p);
    box_bake_layout_all(s);
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

/** Exchange two bake jobs.
 *
 * @param a The first job
 * @param b The second job
 */
static void box_bake_job_swap(
    box_bake_job_t *a,
    box_bake_job_t *b)
{
    box_bake_job_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/** Advance the generator the pivot of a selection step is drawn with.
 *
 * @param state The generator state
 * @return The advanced state
 */
static uint32_t box_bake_job_rand(
    uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;

    return *state;
}

/** Group a range of jobs into the ones nearer, equal and further than a pivot.
 *
 * @param jobs The job array
 * @param lo First job of the range
 * @param hi Job past the range
 * @param pivot The distance to group around
 * @param out Receives the first equal job and the first further one
 */
static void box_bake_job_group(
    box_bake_job_t *jobs,
    uint32_t lo,
    uint32_t hi,
    float pivot,
    uint32_t out[2])
{
    uint32_t less = lo;
    uint32_t i = lo;
    uint32_t more = hi;
    while (i < more) {
        if (jobs[i].distance < pivot) {
            box_bake_job_swap(&jobs[less ++], &jobs[i ++]);
        } else if (jobs[i].distance > pivot) {
            box_bake_job_swap(&jobs[i], &jobs[-- more]);
        } else {
            i ++;
        }
    }

    out[0] = less;
    out[1] = more;
}

/** Move the jobs nearest to the camera to the front of a range.
 *
 * Grouping around a drawn pivot walks an ever smaller part of the range until
 * the split point falls on a group boundary, which costs a linear pass instead
 * of ordering a dirty set of fifty thousand instances outright. The pivot is
 * drawn rather than sampled at fixed places, because a group leaves the jobs
 * behind it in an order a fixed sample would keep picking the extreme of.
 *
 * @param jobs The job array
 * @param count Jobs in the array
 * @param split Jobs that end up in front, at most count
 */
static void box_bake_job_select(
    box_bake_job_t *jobs,
    uint32_t count,
    uint32_t split)
{
    uint32_t seed = count * 2654435761u + split;
    uint32_t lo = 0;
    uint32_t hi = count;
    while (split > lo && split < hi) {
        uint32_t pick = lo + box_bake_job_rand(&seed) % (hi - lo);
        uint32_t bound[2];
        box_bake_job_group(jobs, lo, hi, jobs[pick].distance, bound);
        if (split < bound[0]) {
            hi = bound[0];
        } else if (split > bound[1]) {
            lo = bound[1];
        } else {
            lo = split;
        }
    }
}

/** Probes a run of jobs holds together.
 *
 * @param s The state
 * @param jobs The first job of the run
 * @param count Jobs in the run
 * @return The probes of the run
 */
static uint32_t box_bake_job_probes(
    box_bake_state_t *s,
    const box_bake_job_t *jobs,
    uint32_t count)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < count; i ++) {
        result += box_probe_count(box_bake_job_probe(s, &jobs[i]));
    }

    return result;
}

/** Select the next run of nearest jobs behind the ones already selected.
 *
 * The budget counts probes and an instance holds a grid of them, so how many
 * jobs a frame takes is not known before they are walked. A first run of a few
 * jobs measures the probes an instance carries and the run behind it is sized
 * from that rate, which lets a frame that drains a large dirty set select
 * twice instead of ordering the set outright. Only a run that reaches past the
 * budget is sorted inside itself: an earlier run is taken whole whatever its
 * order, and every job behind a run is further away than all of it.
 *
 * @param s The state
 * @param jobs The job array
 * @param count Jobs in the array
 * @param ready Jobs already selected and taken at the front
 * @param left Probes the budget still has room for
 * @param probes Probes the jobs already taken hold
 * @return The jobs selected at the front after the call
 */
static uint32_t box_bake_job_order(
    box_bake_state_t *s,
    box_bake_job_t *jobs,
    uint32_t count,
    uint32_t ready,
    uint32_t left,
    uint32_t probes)
{
    uint32_t step = BOX_BAKE_ORDER_RUN;
    if (probes) {
        uint64_t rate = ((uint64_t)left * ready + probes - 1) / probes;
        step = rate && rate < count ? (uint32_t)rate : count;
    }

    uint32_t result = count - ready < step ? count : ready + step;
    box_bake_job_select(jobs + ready, count - ready, result - ready);
    if (box_bake_job_probes(s, jobs + ready, result - ready) > left) {
        qsort(jobs + ready, result - ready, sizeof(box_bake_job_t), box_bake_job_cmp);
    }

    return result;
}

/** Give every job its squared distance to the camera and drop the dead ones.
 *
 * @param s The state
 * @return The jobs left in the dirty set
 */
static uint32_t box_bake_job_measure(
    box_bake_state_t *s)
{
    box_bake_job_t *jobs = s->dirty.data;
    uint32_t live = 0;
    uint32_t i;
    for (i = 0; i < s->dirty.count; i ++) {
        const box_probe_desc_t *d = box_bake_job_probe(s, &jobs[i]);
        if (!d) {
            box_bake_mesh_t *m = box_bake_state_mesh(s, jobs[i].mesh);
            if (jobs[i].slot < m->dirty.count) {
                *(uint8_t*)box_vec_at(&m->dirty, jobs[i].slot) = 0;
            }

            continue;
        }

        float dx = d->position[0] - s->eye[0];
        float dy = d->position[1] - s->eye[1];
        float dz = d->position[2] - s->eye[2];
        jobs[live] = jobs[i];
        jobs[live].distance = dx * dx + dy * dy + dz * dz;
        live ++;
    }

    return live;
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
    uint32_t live = box_bake_job_measure(s);
    uint32_t ready = 0;
    uint32_t taken = 0;
    while (taken < live && result < budget) {
        if (taken == ready) {
            ready = box_bake_job_order(s, jobs, live, ready, budget - result, result);
        }

        box_bake_mesh_t *m = box_bake_state_mesh(s, jobs[taken].mesh);
        const box_probe_desc_t *d = box_bake_job_probe(s, &jobs[taken]);
        uint32_t count = box_probe_count(d);
        uint32_t probe;
        for (probe = 0; probe < count; probe ++) {
            box_bake_slot_t *job = box_vec_push(&m->jobs);
            job->slot = jobs[taken].slot;
            job->index = probe;
        }

        *(uint8_t*)box_vec_at(&m->dirty, jobs[taken].slot) = 0;
        result += count;
        taken ++;
    }

    s->dirty.count = live - taken;
    if (s->dirty.count) {
        memmove(jobs, jobs + taken, (size_t)s->dirty.count * sizeof(box_bake_job_t));
    }
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
 * @param handle Shader slot of the pass
 * @param entry Name of the compute entry point
 * @param globals Layout of the first bind group
 * @param meshes Layout of the second bind group
 * @return The pipeline
 */
static WGPUComputePipeline box_bake_pipeline(
    box_renderer_t *r,
    uint32_t handle,
    const char *entry,
    WGPUBindGroupLayout globals,
    WGPUBindGroupLayout meshes)
{
    WGPUShaderModule shader = box_shader_module(r, handle);

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

    WGPUBindGroupLayoutEntry probe_entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutDescriptor probe_desc = { .entryCount = 2, .entries = probe_entries };
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

/** Create or grow the probe buffers of one mesh.
 *
 * @param r The renderer
 * @param m The mesh entry
 * @param descs Descriptors the descriptor buffer must hold
 * @param probes Probes the probe pool must hold
 * @param jobs Jobs the job buffer must hold
 * @return true when a buffer was recreated
 */
static bool box_bake_mesh_grow(
    box_renderer_t *r,
    box_gpu_mesh_t *m,
    uint32_t descs,
    uint32_t probes,
    uint32_t jobs)
{
    bool result = false;
    if (descs > m->desc_capacity) {
        uint32_t grown = m->desc_capacity ? m->desc_capacity * 2 : BOX_BAKE_CAPACITY;
        grown = grown < descs ? descs : grown;
        if (m->probe_descs) wgpuBufferRelease(m->probe_descs);
        m->probe_descs = box_bake_buffer(r, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            (uint64_t)grown * sizeof(box_probe_desc_t));
        m->desc_capacity = grown;
        result = true;
    }

    if (probes > m->probe_capacity) {
        uint32_t grown = m->probe_capacity ? m->probe_capacity * 2 : BOX_BAKE_CAPACITY;
        grown = grown < probes ? probes : grown;
        WGPUBuffer buffer = box_bake_buffer(r,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
            (uint64_t)grown * BOX_BAKE_PROBE_SIZE);
        if (m->probes) {
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, m->probes, 0, buffer, 0,
                (uint64_t)m->probe_capacity * BOX_BAKE_PROBE_SIZE);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
            wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd);
            wgpuCommandEncoderRelease(encoder);
            wgpuBufferRelease(m->probes);
        }

        m->probes = buffer;
        m->probe_capacity = grown;
        result = true;
    }

    if (jobs > m->job_capacity) {
        uint32_t grown = m->job_capacity ? m->job_capacity * 2 : BOX_BAKE_CAPACITY;
        grown = grown < jobs ? jobs : grown;
        if (m->probe_jobs) wgpuBufferRelease(m->probe_jobs);
        m->probe_jobs = box_bake_buffer(r,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            (uint64_t)grown * sizeof(box_bake_slot_t));
        m->job_capacity = grown;
        result = true;
    }

    if (!m->probe_info) {
        m->probe_info = box_bake_buffer(r,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, sizeof(box_bake_info_t));
    }

    return result;
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
    uint64_t desc_size = (uint64_t)m->desc_capacity * sizeof(box_probe_desc_t);
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = m->probe_descs, .size = desc_size },
        { .binding = 1, .buffer = m->probe_info, .size = sizeof(box_bake_info_t) },
        { .binding = 2, .buffer = m->probes, .size = probe_size },
        { .binding = 3, .buffer = m->probe_jobs,
            .size = (uint64_t)m->job_capacity * sizeof(box_bake_slot_t) }
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

    WGPUBindGroupEntry probe_entries[2] = {
        { .binding = 0, .buffer = m->probes, .size = probe_size },
        { .binding = 1, .buffer = m->probe_descs, .size = desc_size }
    };
    WGPUBindGroupDescriptor probe_desc = {
        .layout = r->probe_layout,
        .entryCount = 2,
        .entries = probe_entries
    };
    m->probe_group = wgpuDeviceCreateBindGroup(r->device, &probe_desc);
}

/** Upload the probe descriptors, counts and job lists of every mesh.
 *
 * @param r The renderer
 */
static void box_bake_meshes(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < r->bake.meshes.count && i < r->meshes.count; i ++) {
        box_bake_mesh_t *entry = box_vec_at(&r->bake.meshes, i);
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        if (!m->alive || !entry->descs.count) {
            continue;
        }

        if (box_bake_mesh_grow(r, m, entry->descs.count, entry->probe_count,
            entry->jobs.count) || !m->bake_group)
        {
            box_bake_mesh_groups(r, m);
            entry->upload_lo = 0;
            entry->upload_hi = entry->descs.count;
            entry->stale_lo = UINT32_MAX;
            entry->stale_hi = 0;
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
                (size_t)entry->jobs.count * sizeof(box_bake_slot_t));
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
    r->desc_dummy = box_bake_buffer(r, storage, sizeof(box_probe_desc_t));

    box_probe_t zero = {{0}};
    wgpuQueueWriteBuffer(r->gpu_queue, r->probe_dummy, 0, &zero, sizeof(box_probe_t));

    box_probe_desc_t empty;
    memset(&empty, 0, sizeof(box_probe_desc_t));
    wgpuQueueWriteBuffer(r->gpu_queue, r->desc_dummy, 0, &empty, sizeof(box_probe_desc_t));

    WGPUBindGroupEntry dummy[2] = {
        { .binding = 0, .buffer = r->probe_dummy, .size = BOX_BAKE_PROBE_SIZE },
        { .binding = 1, .buffer = r->desc_dummy, .size = sizeof(box_probe_desc_t) }
    };
    WGPUBindGroupDescriptor dummy_desc = {
        .layout = r->probe_layout,
        .entryCount = 2,
        .entries = dummy
    };
    r->probe_dummy_group = wgpuDeviceCreateBindGroup(r->device, &dummy_desc);

    box_bake_groups(r);
    r->grid_pipeline = box_bake_pipeline(r, BOX_SHADER_BAKE_GRID, "cs_grid", r->grid_layout,
        r->grid_mesh_layout);
    r->bake_pipeline = box_bake_pipeline(r, BOX_SHADER_BAKE, "cs_bake", r->bake_layout,
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
    box_bake_meshes(r);

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
    m->desc_capacity = 0;
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
    if (r->desc_dummy) wgpuBufferRelease(r->desc_dummy);
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
    r->desc_dummy = NULL;
    r->bake_light_capacity = 0;
    r->bake_emitter_capacity = 0;
    r->occupancy_words = 0;
}
