/**
 * @file render/gpu_cull_group.c
 * @brief Cull groups of the webgpu backend: the frustums one pass of the cull shader tests.
 *
 * A group is a scene and up to BOX_GPU_CULL_GROUP_VIEWS frustums (a view and its cascades, the
 * tiles of a shadow atlas or the probe captures), each a cull view with its own draws; more
 * are split into further groups. A batch several frustums draw is one job with their mask.
 *
 * Its draws share a page of visible slots, so the scan words of its job are in one buffer.
 * Blended draws are culled in slot order, keeping their back to front sort; only a view has them.
 */

#include "gpu.h"
#include <stdlib.h>

#ifdef BOX_RENDER_WGPU

/** Write the frustum planes of a clip space matrix, normalized and in world space.
 *
 * Clip space keeps -w <= x, y <= w and 0 <= z <= w; a plane without a direction, the far
 * plane of an infinite projection, becomes one that keeps everything.
 *
 * @param m World to clip space
 * @param out Receives the six planes
 */
static void box_gpu_cull_planes(
    mat4 m,
    vec4 *out)
{
    int32_t i, j;
    for (i = 0; i < 6; i ++) {
        int32_t row = i / 2;
        float sign = (i & 1) ? -1.0f : 1.0f;
        for (j = 0; j < 4; j ++) {
            out[i][j] = i < 4 ? m[j][3] + sign * m[j][row] : 0.0f;
        }
    }

    for (j = 0; j < 4; j ++) {
        out[4][j] = m[j][3] - m[j][2];
        out[5][j] = m[j][2];
    }

    for (i = 0; i < 6; i ++) {
        float len = sqrtf(out[i][0] * out[i][0] + out[i][1] * out[i][1] +
            out[i][2] * out[i][2]);
        if (len < 1e-12f) {
            glm_vec4_copy((vec4){0, 0, 0, 1}, out[i]);
        } else {
            glm_vec4_scale(out[i], 1.0f / len, out[i]);
        }
    }
}

uint32_t box_gpu_cull_bits(
    uint32_t mask)
{
    uint32_t result = 0;
    while (mask) {
        mask &= mask - 1;
        result ++;
    }

    return result;
}

uint32_t box_gpu_cull_uniform(
    box_gpu_cull_t *c,
    const void *data,
    uint32_t size)
{
    uint32_t result = (uint32_t)ecs_vec_count(&c->uniform_data);
    uint32_t stride = (size + BOX_GPU_CULL_STRIDE - 1) / BOX_GPU_CULL_STRIDE * BOX_GPU_CULL_STRIDE;
    uint8_t *dst = ecs_vec_grow_t(NULL, &c->uniform_data, uint8_t, (int32_t)stride);
    ecs_os_memset(dst, 0, (ecs_size_t)stride);
    ecs_os_memcpy(dst, data, (ecs_size_t)size);

    return result;
}

/** Check whether a frustum of a cull group draws a batch, before checking the backend has it.
 *
 * @param gpu The backend
 * @param b The batch
 * @param scene Scene of the group
 * @param f The frustum
 * @return Whether the batch is of the scene and the frustum draws it: the batches of every view
 * and those of its own view, only opaque casters for a frustum of casters, the baked batches
 * for probe captures
 */
static bool box_gpu_cull_wants(
    box_gpu_t *gpu,
    const box_render_batch_t *b,
    ecs_entity_t scene,
    const box_gpu_cull_desc_t *f)
{
    bool result = false;
    bool casters = (f->flags & BOX_GPU_CULL_CASTERS) != 0;
    bool pipeline = box_render_is_mesh(b->pipeline) ||
        (!casters && b->pipeline == BOX_RENDER_SHADER_SHAPE2D);
    if (f->flags & BOX_GPU_CULL_EMPTY) {
        goto done;
    }

    if (f->flags & BOX_GPU_CULL_BAKE) {
        result = box_gpu_cull_capture_draws(gpu, b, scene, casters ? 0 : -1);
        goto done;
    }

    if (f->flags & BOX_GPU_CULL_REFLECT) {
        result = b->scene == scene && box_render_is_mesh(b->pipeline) && b->instance_count &&
            !b->transmissive && !b->water && (b->view == -1 || b->view == f->index);
        goto done;
    }

    if (b->scene != scene || !pipeline || !b->instance_count) {
        goto done;
    }

    result = casters ? (b->view == -1 && b->blend == Opaque && b->shadow)
        : (b->view == -1 || b->view == f->index);
done:
    return result;
}

/** Check whether the cull pass keeps no instance of a batch in a frustum that culls batches by
 * their bounds (BOX_GPU_CULL_BOUNDS), so the frustum can skip the draws of the batch. Batches of
 * one view are always drawn, as their dispatches keep the order of their slots.
 *
 * The world box of the batch holds the world box the cull pass computes for each instance, so
 * when it is outside a plane of the frustum every instance is (visible of cull.wgsl).
 *
 * With a smallest size, no instance passes it when the nearest w of the box is in front of the
 * largest radius of an instance and that radius is below the size at that w. Padding and a
 * margin make rounding keep batches, never skip them.
 *
 * @param b The batch
 * @param f The frustum
 * @return Whether the frustum keeps no instance of the batch, false for a batch without bounds
 */
static bool box_gpu_cull_outside(
    const box_render_batch_t *b,
    const box_gpu_cull_desc_t *f)
{
    bool result = false;
    vec3 center, extent;
    int32_t i;
    if (!(f->flags & BOX_GPU_CULL_BOUNDS) || !b->bounds || b->view != -1) {
        goto done;
    }

    for (i = 0; i < 3; i ++) {
        center[i] = (b->min[i] + b->max[i]) * 0.5f;
        extent[i] = (b->max[i] - b->min[i]) * 0.5f;
    }

    for (i = 0; i < 6 && !result; i ++) {
        const float *p = f->frustum.planes[i];
        float d = p[0] * center[0] + p[1] * center[1] + p[2] * center[2] + p[3] +
            fabsf(p[0]) * extent[0] + fabsf(p[1]) * extent[1] + fabsf(p[2]) * extent[2];
        result = d < 0.0f;
    }

    const float *size = f->frustum.size;
    if (!result && size[1] > 0.0f && size[0] > 0.0f) {
        const float *cw = f->frustum.clip_w;
        float w = cw[0] * center[0] + cw[1] * center[1] + cw[2] * center[2] + cw[3] -
            fabsf(cw[0]) * extent[0] - fabsf(cw[1]) * extent[1] - fabsf(cw[2]) * extent[2];
        float front = w * 0.999f;
        result = w > 0.0f && front > b->radius * size[2] &&
            2.0f * b->radius * size[0] < size[1] * front;
    }

done:
    return result;
}

/** Check whether a view of a packet in a scene culls batches by their bounds, for the frustums
 * of the scene that belong to no view: the tiles of its shadow atlas and its captures.
 *
 * @param packet The packet
 * @param scene The scene
 * @return Whether one does
 */
static bool box_gpu_cull_scene_bounds(
    const box_render_packet_t *packet,
    ecs_entity_t scene)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&packet->views) && !result; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, i);
        result = v->scene == scene && v->cull.bounds;
    }

    return result;
}

/** Check whether the backend has what the cull pass of a batch needs.
 *
 * @param gpu The backend
 * @param b The batch
 * @return Whether the backend has the mesh and the storage instance page of the batch
 */
static bool box_gpu_cull_ready(
    box_gpu_t *gpu,
    const box_render_batch_t *b)
{
    box_gpu_buffer_t *inst = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, b->instances);

    return inst && (inst->usage & BOX_RENDER_BUFFER_STORAGE) &&
        ecs_map_get(&gpu->meshes, b->mesh) != NULL;
}

/** Start the next page of visible slots of the target.
 *
 * @param c The cull state
 * @return The page, empty
 */
static box_gpu_cull_page_t* box_gpu_cull_page_next(
    box_gpu_cull_t *c)
{
    box_gpu_cull_page_t *result;
    if (c->page_count == ecs_vec_count(&c->pages)) {
        result = ecs_vec_append_t(NULL, &c->pages, box_gpu_cull_page_t);
        ecs_os_memset_t(result, 0, box_gpu_cull_page_t);
        ecs_vec_init_t(NULL, &result->list_data, uint32_t, 0);
    } else {
        result = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, c->page_count);
    }

    ecs_vec_clear(&result->list_data);
    ecs_vec_clear(&result->table_data);
    result->used = 0;
    result->scan_used = 0;
    c->page_count ++;

    return result;
}

/** Append the job of a batch: the slot range it tests, the mesh bounds and the frustums that
 * draw it.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param draw A draw of the batch, whose arguments and visible slots an ordered job writes
 * @param history First history word of its instance page, UINT32_MAX for none
 * @param mask Frustums of the group that draw a parallel job
 * @return Index of the job
 */
static uint32_t box_gpu_cull_job(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_gpu_cull_draw_t *draw,
    uint32_t history,
    uint32_t mask)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_batch_t *b = ecs_vec_get_t(
        &packet->batches, box_render_batch_t, draw->batch);
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
    uint32_t result = (uint32_t)ecs_vec_count(&c->job_data);
    box_gpu_cull_job_t *job = ecs_vec_append_t(NULL, &c->job_data, box_gpu_cull_job_t);
    ecs_os_memset_t(job, 0, box_gpu_cull_job_t);
    job->first = b->first_instance;
    job->count = draw->count;
    if (b->listed) {
        box_gpu_cull_page_t *pg = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t,
            (int32_t)draw->page);
        job->first = (uint32_t)ecs_vec_count(&pg->list_data);
        job->list = 1;
        ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &pg->list_data, uint32_t, (int32_t)draw->count),
            box_render_packet_data(packet, b->slots), uint32_t, (int32_t)draw->count);
    }

    job->args = draw->args;
    job->base = draw->base;
    job->history = history;
    job->mask = mask;
    job->fixture = b->fixture ? 1u : 0u;
    if (c->exclude_page && b->instances == c->exclude_page) {
        job->exclude = c->exclude_slot + 1u;
    }

    if (b->bounded) {
        glm_vec3_copy(ECS_CONST_CAST(float*, b->center), job->center);
        glm_vec3_copy(ECS_CONST_CAST(float*, b->extent), job->extent);
    } else {
        glm_vec3_copy(m->center, job->center);
        glm_vec3_copy(m->extent, job->extent);
    }

    return result;
}

/** Append the parallel dispatch of a job of a group and reserve its scan words: those of every
 * workgroup for each frustum that draws it.
 *
 * @param c The cull state
 * @param group Index of the cull group
 * @param buffer Handle of the instance page of the job
 * @param job The job
 * @param count Instances of the job
 * @param page Page that holds the visible slots of its draws
 * @param history History page and pyramid of the job, NULL for those of the group
 * @param mask Frustums of the group that draw the job
 * @param late Whether it runs in the late phase
 * @param draws The draw of each frustum of the mask, in the order of their bits
 */
static void box_gpu_cull_parallel(
    box_gpu_cull_t *c,
    int32_t group,
    uint32_t buffer,
    uint32_t job,
    uint32_t count,
    uint32_t page,
    WGPUBindGroup history,
    uint32_t mask,
    bool late,
    const int32_t *draws)
{
    uint32_t frustums = box_gpu_cull_bits(mask);
    box_gpu_cull_page_t *pg = ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, (int32_t)page);
    box_gpu_cull_dispatch_t *d = ecs_vec_append_t(NULL, &c->dispatches, box_gpu_cull_dispatch_t);
    ecs_os_memset_t(d, 0, box_gpu_cull_dispatch_t);
    d->group = group;
    d->buffer = buffer;
    d->groups = (count + BOX_GPU_CULL_GROUP - 1) / BOX_GPU_CULL_GROUP;
    d->ordered = false;
    d->late = late;
    d->page = page;
    d->history = history;
    d->first = job;
    d->mask = mask;
    d->first_entry = ecs_vec_count(&c->entries);
    d->scan = pg->scan_used;
    pg->scan_used += d->groups * BOX_GPU_CULL_SCAN_WORDS * frustums;
    ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &c->entries, int32_t, (int32_t)frustums),
        draws, int32_t, (int32_t)frustums);
}

/** Append an ordered dispatch over consecutive jobs of one frustum of a group.
 *
 * @param c The cull state
 * @param group Index of the cull group
 * @param frustum The frustum in the group
 * @param buffer Handle of the instance page of its jobs
 * @param first First job
 * @param count Jobs
 * @param phase BOX_GPU_CULL_PHASE_ALL or _LATE
 * @param page Page that holds the visible slots of its jobs
 */
static void box_gpu_cull_ordered_dispatch(
    box_gpu_cull_t *c,
    int32_t group,
    int32_t frustum,
    uint32_t buffer,
    uint32_t first,
    uint32_t count,
    uint32_t phase,
    uint32_t page)
{
    const box_gpu_cull_group_t *g = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, group);
    box_gpu_cull_uniforms_t u = g->base;
    u.jobs[0] = first;
    u.jobs[1] = count;
    u.jobs[2] = phase;
    u.jobs[3] = (uint32_t)frustum;
    uint32_t uniform = box_gpu_cull_uniform(c, &u, sizeof(u));
    box_gpu_cull_dispatch_t *d = ecs_vec_append_t(NULL, &c->dispatches, box_gpu_cull_dispatch_t);
    ecs_os_memset_t(d, 0, box_gpu_cull_dispatch_t);
    d->group = group;
    d->buffer = buffer;
    d->groups = count;
    d->ordered = true;
    d->late = phase == BOX_GPU_CULL_PHASE_LATE;
    d->page = page;
    d->first = first;
    d->uniform = uniform;
    d->mask = 1u << frustum;
    d->first_entry = -1;
}

/** Order keys of draws: the instance page handle, then the draw.
 *
 * @param a The first key
 * @param b The second key
 * @return Negative when a comes first
 */
static int box_gpu_cull_order_cmp(
    const void *a,
    const void *b)
{
    uint64_t ka = *(const uint64_t*)a, kb = *(const uint64_t*)b;

    return (ka > kb) - (ka < kb);
}

/** Add the dispatches of the blended draws of a frustum of a group, one per instance page.
 *
 * Each draw is a job of an ordered dispatch, which keeps the back to front order of its slots.
 * Only a frustum that draws the batches of its own view has them.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param group Index of the cull group
 * @param frustum The frustum in the group
 * @param phase Phase the blended draws are culled in
 */
static void box_gpu_cull_ordered(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t group,
    int32_t frustum,
    uint32_t phase)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_gpu_cull_group_t *g = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, group);
    const box_gpu_cull_view_t *cv = ecs_vec_get_t(
        &c->views, box_gpu_cull_view_t, g->first_view + frustum);
    box_gpu_cull_draw_t *draws = ecs_vec_first(&c->draws);
    int32_t i, first = cv->first_draw, count;
    uint32_t job_first = 0, jobs = 0, buffer = 0, page = 0;
    ecs_vec_clear(&c->order);
    for (i = first; i < first + cv->draw_count; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, draws[i].batch);
        if (b->view != -1) {
            *ecs_vec_append_t(NULL, &c->order, uint64_t) =
                ((uint64_t)b->instances << 32) | (uint32_t)(i - first);
        }
    }

    count = ecs_vec_count(&c->order);
    uint64_t *order = ecs_vec_first(&c->order);
    if (!count) {
        goto done;
    }

    qsort(order, (size_t)count, sizeof(uint64_t), box_gpu_cull_order_cmp);
    for (i = 0; i < count; i ++) {
        uint32_t handle = (uint32_t)(order[i] >> 32);
        const box_gpu_cull_draw_t *draw = &draws[first + (int32_t)(uint32_t)order[i]];
        if (jobs && (handle != buffer || draw->page != page)) {
            box_gpu_cull_ordered_dispatch(c, group, frustum, buffer, job_first, jobs, phase,
                page);
            jobs = 0;
        }

        uint32_t job = box_gpu_cull_job(gpu, packet, draw, UINT32_MAX, 0);
        if (!jobs) {
            job_first = job;
            buffer = handle;
            page = draw->page;
        }

        jobs ++;
    }

    box_gpu_cull_ordered_dispatch(c, group, frustum, buffer, job_first, jobs, phase, page);
done:
    return;
}

/** Add the draws of a batch to some of the frustums of the group being added that draw it,
 * with their jobs and dispatches.
 *
 * All these draws are in one page, so the scan words of their job are too. The early phase, or
 * the only one, tests every frustum of the mask in one job; a view that culls by occlusion
 * tests it again alone in a late job, the only job of its opaque 2D shapes.
 *
 * Blended and per view draws get their ordered dispatches once the group has all its draws.
 * The frustum with a history keeps its job for a batch it skips by its bounds
 * (box_gpu_cull_group_add), with a draw that skips.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param group Index of the cull group
 * @param batch Index of the batch in the packet
 * @param mask Frustums of the group that draw it, whose visible slots fit one page
 * @param filled Draws added to each frustum, updated
 * @param history History of the frustum with one, NULL for none
 * @param hist The frustum with a history, -1 for none
 */
static void box_gpu_cull_batch_part(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t group,
    int32_t batch,
    uint32_t mask,
    int32_t *filled,
    box_gpu_cull_history_t *history,
    int32_t hist)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, batch);
    const box_gpu_cull_group_t *g = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, group);
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, b->mesh);
    bool view = hist >= 0 && ((mask >> hist) & 1u);
    bool late = view && b->view == -1 && box_render_is_mesh(b->pipeline);
    uint32_t skips = *ecs_vec_get_t(&c->skips, uint32_t, batch);
    int32_t draws[BOX_GPU_CULL_GROUP_VIEWS], first[BOX_GPU_CULL_GROUP_VIEWS];
    int32_t f, i, k = 0, n = 0;
    uint64_t slots = 0;
    for (f = 0; f < g->view_count; f ++) {
        if ((mask >> f) & 1u) {
            slots += (uint64_t)b->instance_count * ((late && f == hist) ? 2u : 1u);
        }
    }

    box_gpu_cull_page_t *pg = c->page_count
        ? ecs_vec_get_t(&c->pages, box_gpu_cull_page_t, c->page_count - 1) : NULL;
    if (!pg || (pg->used && (uint64_t)pg->used + slots > c->window)) {
        pg = box_gpu_cull_page_next(c);
    }

    ecs_assert(pg->used + slots <= c->window || box_gpu_cull_bits(mask) == 1,
        ECS_INTERNAL_ERROR, NULL);

    for (f = 0; f < g->view_count; f ++) {
        if (!((mask >> f) & 1u)) {
            continue;
        }

        const box_gpu_cull_view_t *cv = ecs_vec_get_t(
            &c->views, box_gpu_cull_view_t, g->first_view + f);
        bool twice = late && f == hist;
        int32_t index = cv->first_draw + filled[f];
        filled[f] ++;
        box_gpu_cull_draw_t *d = ecs_vec_get_t(&c->draws, box_gpu_cull_draw_t, index);
        d->batch = batch;
        d->skip = (skips >> f) & 1u;
        d->args = (uint32_t)(ecs_vec_count(&c->arg_data) / BOX_GPU_CULL_ARGS);
        d->late = twice ? d->args + 1 : UINT32_MAX;
        d->page = (uint32_t)c->page_count - 1;
        d->base = pg->used;
        d->count = b->instance_count;
        pg->used += b->instance_count * (twice ? 2u : 1u);
        for (i = 0; i < (twice ? 2 : 1); i ++) {
            uint32_t *args = ecs_vec_grow_t(NULL, &c->arg_data, uint32_t, BOX_GPU_CULL_ARGS);
            args[0] = m->index_count;
            args[1] = 0;
            args[2] = 0;
            args[3] = 0;
            args[4] = 0;
        }

        draws[k ++] = index;
        if (!view || late || f != hist) {
            first[n ++] = index;
        }
    }

    if (b->view != -1) {
        goto done;
    }

    const box_gpu_cull_draw_t *d = ecs_vec_get_t(&c->draws, box_gpu_cull_draw_t, draws[0]);
    uint32_t page = d->page;
    uint32_t offset = late ? box_gpu_hiz_offset(history, b->instances) : UINT32_MAX;
    WGPUBindGroup group_history = NULL;
    if (late) {
        const box_gpu_cull_view_t *cv = ecs_vec_get_t(
            &c->views, box_gpu_cull_view_t, g->first_view + hist);
        group_history = box_gpu_hiz_group(gpu, history, cv->hiz,
            box_gpu_hiz_page(history, b->instances));
    }

    uint32_t early = view && !late ? mask & ~(1u << hist) : mask;
    if (early) {
        uint32_t job = box_gpu_cull_job(gpu, packet, d, view ? offset : UINT32_MAX, early);
        box_gpu_cull_parallel(c, group, b->instances, job, b->instance_count, page,
            view ? group_history : NULL, early, false, first);
    }

    if (view) {
        int32_t own = draws[box_gpu_cull_bits(mask & ((1u << hist) - 1u))];
        uint32_t job = box_gpu_cull_job(gpu, packet,
            ecs_vec_get_t(&c->draws, box_gpu_cull_draw_t, own), offset, 1u << hist);
        box_gpu_cull_parallel(c, group, b->instances, job, b->instance_count, page,
            group_history, 1u << hist, true, &own);
    }

done:
    return;
}

/** Add the draws of a batch to frustums of the group being added that skip it by its bounds
 * without a job: draws that skip, without arguments, so passes still request its pipelines.
 *
 * @param gpu The backend
 * @param group Index of the cull group
 * @param batch Index of the batch in the packet
 * @param mask Frustums of the group that skip it without a job
 * @param filled Draws added to each frustum, updated
 */
static void box_gpu_cull_batch_skip(
    box_gpu_t *gpu,
    int32_t group,
    int32_t batch,
    uint32_t mask,
    int32_t *filled)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_gpu_cull_group_t *g = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, group);
    int32_t f;
    for (f = 0; f < g->view_count; f ++) {
        if (!((mask >> f) & 1u)) {
            continue;
        }

        const box_gpu_cull_view_t *cv = ecs_vec_get_t(
            &c->views, box_gpu_cull_view_t, g->first_view + f);
        box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &c->draws, box_gpu_cull_draw_t, cv->first_draw + filled[f]);
        filled[f] ++;
        *d = (box_gpu_cull_draw_t){
            .batch = batch,
            .args = UINT32_MAX,
            .late = UINT32_MAX,
            .skip = true
        };
    }
}

/** Add the draws of a batch to the frustums of the group being added that draw it, with its
 * jobs and dispatches.
 *
 * Its draws in all the frustums of the mask go on one page when they fit the window. A large
 * batch drawn by many frustums is split over parts of the mask instead, each a job on a page
 * of its own, so that no page holds more than the window.
 *
 * The frustum with a history counts twice for its late phase. A part of one frustum may still
 * exceed a lowered window.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param group Index of the cull group
 * @param batch Index of the batch in the packet
 * @param mask Frustums of the group that draw it
 * @param filled Draws added to each frustum, updated
 * @param history History of the frustum with one, NULL for none
 * @param hist The frustum with a history, -1 for none
 */
static void box_gpu_cull_batch_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t group,
    int32_t batch,
    uint32_t mask,
    int32_t *filled,
    box_gpu_cull_history_t *history,
    int32_t hist)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, batch);
    const box_gpu_cull_group_t *g = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, group);
    bool late = hist >= 0 && ((mask >> hist) & 1u) && b->view == -1 &&
        box_render_is_mesh(b->pipeline);
    uint32_t part = 0;
    uint64_t slots = 0;
    int32_t f;
    for (f = 0; f < g->view_count; f ++) {
        if (!((mask >> f) & 1u)) {
            continue;
        }

        uint64_t own = (uint64_t)b->instance_count * ((late && f == hist) ? 2u : 1u);
        if (part && slots + own > c->window) {
            box_gpu_cull_batch_part(gpu, packet, group, batch, part, filled, history, hist);
            part = 0;
            slots = 0;
        }

        part |= 1u << f;
        slots += own;
    }

    box_gpu_cull_batch_part(gpu, packet, group, batch, part, filled, history, hist);
}

/** Phases of the draws of a cull view, see box_gpu_cull_phase */
#define BOX_GPU_CULL_DRAW_PHASES (4)

/** Get the phase a batch draws in: plain opaque meshes, then alpha tested opaque meshes, then
 * opaque 2D shapes, then blended.
 *
 * The alpha tested meshes discard fragments, which turns off early depth rejection for their
 * pipelines, so they draw after the plain ones and are rejected by the depth those wrote. The 2D
 * shapes blend their anti-aliased edges, so they draw over the opaque meshes behind.
 *
 * @param b The batch
 * @return The phase
 */
static int32_t box_gpu_cull_phase(
    const box_render_batch_t *b)
{
    int32_t result = b->cutout ? 1 : 0;
    if (b->pipeline == BOX_RENDER_SHADER_SHAPE2D) {
        result = 2;
    }

    if (b->view != -1) {
        result = 3;
    }

    return result;
}

/** Get the order of the draws of a cull group: by phase, the opaque meshes by pipeline state and
 * then by material group, else in packet order.
 *
 * @param a The first batch
 * @param b The second batch
 * @return Negative when a draws first
 */
static int box_gpu_cull_sort_cmp(
    const void *a,
    const void *b)
{
    const box_gpu_cull_sort_t *sa = a, *sb = b;
    int result = (sa->phase > sb->phase) - (sa->phase < sb->phase);
    if (!result) {
        result = (sa->state > sb->state) - (sa->state < sb->state);
    }

    if (!result) {
        result = (sa->material > sb->material) - (sa->material < sb->material);
    }

    if (!result) {
        result = (sa->batch > sb->batch) - (sa->batch < sb->batch);
    }

    return result;
}

/** Describe a batch a cull group draws for the order of its draws.
 *
 * @param b The batch
 * @param batch Index of the batch in the packet
 * @param out Receives the description
 */
static void box_gpu_cull_sort_key(
    const box_render_batch_t *b,
    int32_t batch,
    box_gpu_cull_sort_t *out)
{
    uint64_t h = (uint64_t)b->wrap * 0x9E3779B97F4A7C15ull + b->blend_layers;
    int32_t i;
    out->phase = box_gpu_cull_phase(b);
    out->batch = batch;
    out->state = 0;
    out->material = 0;
    if (out->phase > 1) {
        goto done;
    }

    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        h = (h ^ (h >> 31)) * 0x9E3779B97F4A7C15ull + b->textures[i];
    }

    out->state = ((uint64_t)b->pipeline << 40) | ((uint64_t)b->texture_blend << 39) |
        ((uint64_t)b->surface << 38) | ((uint64_t)(b->back_face != BackFaceCull) << 37) |
        b->features;
    out->material = h;
done:
    return;
}

/** Add a cull group: frustums of a scene that one pass tests together, with their cull views,
 * draws, jobs and dispatches.
 *
 * Each frustum is a cull view with its own draws, added by phase, so the alpha tested casters
 * follow the plain ones. The opaque meshes are grouped by pipeline state and material group
 * within their phase (box_gpu_cull_sort_cmp), the rest keep packet order.
 *
 * A batch is one job with the mask of the frustums that draw it, less those that skip it by
 * its bounds (box_gpu_cull_outside) but for the one with a history, so the history is written
 * as without the bounds. Each keeps a draw that skips, so its passes still request the batch.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param scene Scene the frustums draw
 * @param frustums The frustums
 * @param count Frustums, at most BOX_GPU_CULL_GROUP_VIEWS
 * @param base Uniforms of the view, its view projection and pyramid for the occlusion test
 * @param history History of the frustum with BOX_GPU_CULL_HISTORY, NULL for none
 * @param hiz Pyramid of that frustum, NULL for none
 */
static void box_gpu_cull_group_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    const box_gpu_cull_desc_t *frustums,
    int32_t count,
    const box_gpu_cull_uniforms_t *base,
    box_gpu_cull_history_t *history,
    box_gpu_hiz_t *hiz)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_cull_frustum_t block[BOX_GPU_CULL_GROUP_VIEWS];
    int32_t counts[BOX_GPU_CULL_GROUP_VIEWS] = {0}, filled[BOX_GPU_CULL_GROUP_VIEWS] = {0};
    int32_t group = ecs_vec_count(&c->groups), hist = -1, total = 0, f, i;
    int32_t batches = ecs_vec_count(&packet->batches), first = ecs_vec_count(&c->draws);
    ecs_assert(count > 0 && count <= BOX_GPU_CULL_GROUP_VIEWS, ECS_INTERNAL_ERROR, NULL);
    ecs_os_memset(block, 0, (ecs_size_t)sizeof(block));
    box_gpu_cull_group_t *g = ecs_vec_append_t(NULL, &c->groups, box_gpu_cull_group_t);
    ecs_os_memset_t(g, 0, box_gpu_cull_group_t);
    g->slot = frustums[0].slot;
    g->first_view = ecs_vec_count(&c->views);
    g->view_count = count;
    g->first_dispatch = ecs_vec_count(&c->dispatches);
    g->base = *base;
    g->history = c->empty_group;
    for (f = 0; f < count; f ++) {
        const box_gpu_cull_desc_t *fd = &frustums[f];
        box_gpu_cull_view_t *cv = ecs_vec_append_t(NULL, &c->views, box_gpu_cull_view_t);
        ecs_os_memset_t(cv, 0, box_gpu_cull_view_t);
        cv->view = fd->view;
        cv->slot = fd->slot;
        cv->cascade = fd->cascade;
        cv->group_index = group;
        cv->frustum = f;
        cv->flags = fd->flags & ~BOX_GPU_CULL_HISTORY;
        cv->counter = (uint32_t)(g->first_view + f);
        cv->group = c->empty_group;
        cv->report = fd->report;
        if ((fd->flags & BOX_GPU_CULL_HISTORY) && history && hiz && hist < 0) {
            hist = f;
            cv->flags |= BOX_GPU_CULL_HISTORY;
            cv->hiz = hiz;
            cv->group = box_gpu_hiz_group(gpu, history, hiz, 0);
            g->history = cv->group;
            g->occlusion = true;
        }

        block[f] = fd->frustum;
        block[f].flags[0] = cv->counter;
        block[f].flags[1] = cv->flags;
        block[f].flags[2] = 0;
        block[f].flags[3] = 0;
    }

    g->frustums = box_gpu_cull_uniform(c, block, (uint32_t)sizeof(block));
    ecs_vec_set_count_t(NULL, &c->masks, uint32_t, batches);
    ecs_vec_set_count_t(NULL, &c->skips, uint32_t, batches);
    uint32_t *masks = ecs_vec_first(&c->masks);
    uint32_t *skips = ecs_vec_first(&c->skips);
    for (i = 0; i < batches; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        uint32_t mask = 0, skip = 0;
        for (f = 0; f < count; f ++) {
            if (box_gpu_cull_wants(gpu, b, scene, &frustums[f])) {
                mask |= 1u << f;
                if (box_gpu_cull_outside(b, &frustums[f])) {
                    skip |= 1u << f;
                }
            }
        }

        if (mask && !box_gpu_cull_ready(gpu, b)) {
            mask = 0;
            skip = 0;
        }

        for (f = 0; skip && f < count; f ++) {
            if (!((skip >> f) & 1u)) {
                continue;
            }

            box_gpu_cull_view_t *cv = ecs_vec_get_t(
                &c->views, box_gpu_cull_view_t, g->first_view + f);
            cv->skipped ++;
            if (f != hist) {
                mask &= ~(1u << f);
                cv->skipped_untested += (int32_t)b->instance_count;
            }
        }

        masks[i] = mask;
        skips[i] = skip;
        for (f = 0; f < count; f ++) {
            counts[f] += (int32_t)(((mask | skip) >> f) & 1u);
        }
    }

    for (f = 0; f < count; f ++) {
        box_gpu_cull_view_t *cv = ecs_vec_get_t(
            &c->views, box_gpu_cull_view_t, g->first_view + f);
        cv->first_draw = first + total;
        cv->draw_count = counts[f];
        total += counts[f];
    }

    if (total) {
        ecs_vec_grow_t(NULL, &c->draws, box_gpu_cull_draw_t, total);
    }

    ecs_vec_clear(&c->sort);
    for (i = 0; i < batches; i ++) {
        if (masks[i] | skips[i]) {
            box_gpu_cull_sort_key(ecs_vec_get_t(&packet->batches, box_render_batch_t, i), i,
                ecs_vec_append_t(NULL, &c->sort, box_gpu_cull_sort_t));
        }
    }

    int32_t sorted = ecs_vec_count(&c->sort);
    box_gpu_cull_sort_t *order = ecs_vec_first(&c->sort);
    if (sorted > 1) {
        qsort(order, (size_t)sorted, sizeof(box_gpu_cull_sort_t), box_gpu_cull_sort_cmp);
    }

    for (i = 0; i < sorted; i ++) {
        int32_t batch = order[i].batch;
        box_gpu_cull_batch_skip(gpu, group, batch, skips[batch] & ~masks[batch], filled);
        if (masks[batch]) {
            box_gpu_cull_batch_add(gpu, packet, group, batch, masks[batch], filled, history,
                hist);
        }
    }

    for (f = 0; f < count; f ++) {
        if (!(frustums[f].flags & (BOX_GPU_CULL_CASTERS | BOX_GPU_CULL_BAKE))) {
            box_gpu_cull_ordered(gpu, packet, group, f,
                f == hist ? BOX_GPU_CULL_PHASE_LATE : BOX_GPU_CULL_PHASE_ALL);
        }
    }

    g = ecs_vec_get_t(&c->groups, box_gpu_cull_group_t, group);
    g->dispatch_count = ecs_vec_count(&c->dispatches) - g->first_dispatch;
}

/** Add the frustums of a scene as cull groups of at most BOX_GPU_CULL_GROUP_VIEWS frustums,
 * each tested in a pass of its own. No frustum is dropped.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param scene Scene the frustums draw
 * @param frustums The frustums
 * @param count Frustums
 * @param base Uniforms of the view, its view projection and pyramid for the occlusion test
 * @param history History of the frustum with BOX_GPU_CULL_HISTORY, NULL for none
 * @param hiz Pyramid of that frustum, NULL for none
 */
static void box_gpu_cull_groups_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    const box_gpu_cull_desc_t *frustums,
    int32_t count,
    const box_gpu_cull_uniforms_t *base,
    box_gpu_cull_history_t *history,
    box_gpu_hiz_t *hiz)
{
    int32_t i;
    for (i = 0; i < count; i += BOX_GPU_CULL_GROUP_VIEWS) {
        int32_t n = count - i < BOX_GPU_CULL_GROUP_VIEWS ? count - i : BOX_GPU_CULL_GROUP_VIEWS;
        box_gpu_cull_group_add(gpu, packet, scene, &frustums[i], n, base, history, hiz);
    }
}

/** Get a frustum that keeps everything: planes without a direction and no smallest size.
 *
 * @param out Receives the frustum
 */
static void box_gpu_cull_frustum_all(
    box_gpu_cull_frustum_t *out)
{
    int32_t i;
    ecs_os_memset_t(out, 0, box_gpu_cull_frustum_t);
    for (i = 0; i < 6; i ++) {
        out->planes[i][3] = 1.0f;
    }
}

void box_gpu_cull_add(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    mat4 proj,
    const box_gpu_view_uniforms_t *u)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    box_gpu_cull_uniforms_t base;
    box_gpu_cull_desc_t view;
    box_gpu_cull_history_t *history = NULL;
    box_gpu_hiz_t *hiz = NULL;
    bool draws = box_camera_has_depth(&v->camera_data);
    int32_t i, j, cascades = draws ? (int32_t)u->shadow_config[0] : 0;
    int32_t count = cascades ? cascades + c->copies : 0;
    uint32_t renders = box_gpu_shadow_renders(gpu, slot);
    ecs_os_memset_t(&base, 0, box_gpu_cull_uniforms_t);
    ecs_os_memset_t(&view, 0, box_gpu_cull_desc_t);
    view.view = v->view;
    view.slot = slot;
    view.cascade = -1;
    view.index = index;
    view.flags = draws ? 0 : BOX_GPU_CULL_EMPTY;
    view.report = !v->target && v->view;
    box_gpu_cull_frustum_all(&view.frustum);
    if (v->cull.enabled) {
        box_gpu_cull_planes(ECS_CONST_CAST(vec4*, u->view_proj), view.frustum.planes);
        for (j = 0; j < 4; j ++) {
            view.frustum.clip_w[j] = u->view_proj[j][3];
        }

        view.frustum.size[0] = proj[1][1] * (float)t->desc.render_height * 0.5f;
        view.frustum.size[1] = v->cull.min_dimension > 0 ? v->cull.min_dimension : 0.0f;
        view.frustum.size[2] = v->camera_data.ortho ? 0.0f : 1.0f;
        view.flags |= view.frustum.size[1] > 0 ? BOX_GPU_CULL_SIZE : 0;
        view.flags |= v->cull.bounds && draws ? BOX_GPU_CULL_BOUNDS : 0;
    }

    if (v->cull.enabled && v->cull.occlusion) {
        hiz = box_gpu_hiz_get(gpu, t->desc.render_width, t->desc.render_height);
        history = box_gpu_hiz_history(gpu, v->view, t->handle);
        box_gpu_hiz_ranges(gpu, history, packet, index);
        glm_mat4_copy(ECS_CONST_CAST(vec4*, u->view_proj), base.view_proj);
        base.hiz[0] = hiz->width;
        base.hiz[1] = hiz->height;
        base.hiz[2] = hiz->levels;
        view.flags |= BOX_GPU_CULL_HISTORY;
    }

    ecs_vec_clear(&c->frustums);
    *ecs_vec_append_t(NULL, &c->frustums, box_gpu_cull_desc_t) = view;
    for (i = 0; i < count; i ++) {
        if (i < cascades && !(renders & (1u << i))) {
            continue;
        }

        box_gpu_cull_desc_t *cascade = ecs_vec_append_t(NULL, &c->frustums, box_gpu_cull_desc_t);
        ecs_os_memset_t(cascade, 0, box_gpu_cull_desc_t);
        cascade->view = v->view;
        cascade->slot = slot;
        cascade->cascade = i;
        cascade->index = index;
        cascade->flags = BOX_GPU_CULL_CASTERS;
        box_gpu_cull_frustum_all(&cascade->frustum);
        if (v->cull.enabled) {
            cascade->flags |= v->cull.bounds ? BOX_GPU_CULL_BOUNDS : 0;
            vec4 *m = ECS_CONST_CAST(vec4*, u->shadow_matrix[i < cascades ? i : 0]);
            box_gpu_cull_planes(m, cascade->frustum.planes);
            if (v->shadows.min_texels > 0) {
                cascade->frustum.clip_w[3] = 1.0f;
                cascade->frustum.size[0] = 0.5f * (float)v->shadows.resolution *
                    glm_vec3_norm((vec3){m[0][0], m[1][0], m[2][0]});
                cascade->frustum.size[1] = v->shadows.min_texels;
                cascade->flags |= BOX_GPU_CULL_SIZE;
            }
        }
    }

    box_gpu_cull_groups_add(gpu, packet, v->scene, ecs_vec_first(&c->frustums),
        ecs_vec_count(&c->frustums), &base, history, hiz);
}

void box_gpu_cull_reflect_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    mat4 view_proj,
    float pixels,
    uint32_t exclude_page,
    uint32_t exclude_slot)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    box_gpu_cull_uniforms_t base;
    box_gpu_cull_desc_t f;
    int32_t j;
    ecs_os_memset_t(&base, 0, box_gpu_cull_uniforms_t);
    ecs_os_memset_t(&f, 0, box_gpu_cull_desc_t);
    f.view = v->view;
    f.slot = slot;
    f.cascade = -1;
    f.index = index;
    f.flags = BOX_GPU_CULL_REFLECT;
    box_gpu_cull_frustum_all(&f.frustum);
    if (v->cull.enabled) {
        box_gpu_cull_planes(view_proj, f.frustum.planes);
        for (j = 0; j < 4; j ++) {
            f.frustum.clip_w[j] = view_proj[j][3];
        }

        f.frustum.size[0] = pixels;
        f.frustum.size[1] = v->cull.min_dimension > 0 ? v->cull.min_dimension : 0.0f;
        f.frustum.size[2] = 1.0f;
        f.flags |= f.frustum.size[1] > 0 ? BOX_GPU_CULL_SIZE : 0;
        f.flags |= v->cull.bounds ? BOX_GPU_CULL_BOUNDS : 0;
    }

    c->exclude_page = exclude_page;
    c->exclude_slot = exclude_slot;
    box_gpu_cull_group_add(gpu, packet, v->scene, &f, 1, &base, NULL, NULL);
    c->exclude_page = 0;
    c->exclude_slot = 0;
}

void box_gpu_cull_casters_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    int32_t slot,
    const box_render_shadow_tile_t *tiles,
    const bool *renders,
    int32_t count)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_cull_uniforms_t base;
    uint32_t bounds = box_gpu_cull_scene_bounds(packet, scene) ? BOX_GPU_CULL_BOUNDS : 0;
    int32_t i, o;
    ecs_os_memset_t(&base, 0, box_gpu_cull_uniforms_t);
    ecs_vec_clear(&c->frustums);
    for (i = 0; i < count; i ++) {
        bool octahedral = tiles[i].projection == BOX_RENDER_SHADOW_OCTAHEDRAL;
        if (renders && !renders[i]) {
            continue;
        }

        for (o = 0; o < (octahedral ? BOX_RENDER_SHADOW_OCTANTS : 1); o ++) {
            box_gpu_cull_desc_t *f = ecs_vec_append_t(NULL, &c->frustums, box_gpu_cull_desc_t);
            ecs_os_memset_t(f, 0, box_gpu_cull_desc_t);
            f->slot = slot;
            f->cascade = BOX_GPU_ATLAS_FRUSTUM(i, o);
            f->index = -1;
            f->flags = BOX_GPU_CULL_CASTERS | bounds;
            box_gpu_cull_frustum_all(&f->frustum);
            if (octahedral) {
                box_render_shadow_octant_planes(&tiles[i], o, f->frustum.planes);
            } else {
                box_gpu_cull_planes(ECS_CONST_CAST(vec4*, tiles[i].matrix), f->frustum.planes);
            }

            glm_vec3_copy(ECS_CONST_CAST(float*, tiles[i].position), f->frustum.light);
            f->frustum.light[3] = 1.0f;
        }
    }

    if (ecs_vec_count(&c->frustums)) {
        box_gpu_cull_groups_add(gpu, packet, scene, ecs_vec_first(&c->frustums),
            ecs_vec_count(&c->frustums), &base, NULL, NULL);
    }
}

void box_gpu_cull_reset(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    ecs_vec_clear(&c->views);
    ecs_vec_clear(&c->groups);
    ecs_vec_clear(&c->draws);
    ecs_vec_clear(&c->dispatches);
    ecs_vec_clear(&c->entries);
    ecs_vec_clear(&c->uniform_data);
    ecs_vec_clear(&c->job_data);
    ecs_vec_clear(&c->arg_data);
    c->page_count = 0;
    ecs_vec_clear(&c->tables);
    ecs_vec_clear(&c->runs);
}

bool box_gpu_cull_capture_draws(
    box_gpu_t *gpu,
    const box_render_batch_t *b,
    ecs_entity_t scene,
    int32_t cascade)
{
    bool result = b->scene == scene && b->bake && b->view == -1 && b->blend == Opaque &&
        box_render_is_mesh(b->pipeline) && b->instance_count &&
        (cascade < 0 || b->shadow);
    if (result) {
        result = box_gpu_cull_ready(gpu, b);
    }

    return result;
}

void box_gpu_cull_capture_add(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    ecs_entity_t scene,
    int32_t slot,
    int32_t cascade,
    mat4 view_proj,
    float pixels)
{
    box_gpu_cull_uniforms_t base;
    box_gpu_cull_desc_t f;
    int32_t i;
    ecs_os_memset_t(&base, 0, box_gpu_cull_uniforms_t);
    ecs_os_memset_t(&f, 0, box_gpu_cull_desc_t);
    f.slot = slot;
    f.cascade = cascade;
    f.index = -1;
    f.flags = BOX_GPU_CULL_BAKE | (cascade >= 0 ? BOX_GPU_CULL_CASTERS : 0) |
        (pixels > 0 ? BOX_GPU_CULL_SIZE : 0) |
        (box_gpu_cull_scene_bounds(packet, scene) ? BOX_GPU_CULL_BOUNDS : 0);
    box_gpu_cull_planes(view_proj, f.frustum.planes);
    for (i = 0; i < 4; i ++) {
        f.frustum.clip_w[i] = view_proj[i][3];
    }

    f.frustum.size[0] = pixels;
    f.frustum.size[1] = pixels > 0 ? BOX_RENDER_CULL_MIN_DIMENSION : 0.0f;
    f.frustum.size[2] = 1.0f;
    box_gpu_cull_group_add(gpu, packet, scene, &f, 1, &base, NULL, NULL);
}

#endif
