/**
 * @file renderer/cull.c
 * @brief Per view frustum culling of the static and dynamic instances of a frame.
 *
 * Visible instances are compacted into one buffer. Small static and dynamic batches
 * are culled on the CPU; larger static batches by a compute pass that writes
 * indexed indirect draw arguments.
 *
 * A view that occlusion culls sends its compute culled batches through the two
 * pass scheme of hiz.c instead.
 */

#include "renderer_internal.h"
#include <string.h>

/** Instances the visible buffer of a view starts out with */
#define BOX_CULL_CAPACITY (256)

/** Create a GPU buffer.
 *
 * @param r The renderer
 * @param usage Buffer usage flags
 * @param size Size in bytes
 * @return The buffer
 */
static WGPUBuffer box_cull_buffer(
    box_renderer_t *r,
    WGPUBufferUsage usage,
    uint64_t size)
{
    WGPUBufferDescriptor desc = { .usage = usage, .size = size };

    return wgpuDeviceCreateBuffer(r->device, &desc);
}

/** Create the layout and the pipeline of the culling pass.
 *
 * @param r The renderer
 */
static void box_cull_pipeline(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_cull_uniform_t)
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
    r->cull_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUShaderModule shader = box_shader_module(r, BOX_SHADER_CULL);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->cull_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

    WGPUComputePipelineDescriptor desc = {
        .label = BOX_GPU_LABEL("cull"),
        .layout = pipeline_layout,
        .compute = {
            .module = shader,
            .entryPoint = { .data = "cs_main", .length = WGPU_STRLEN }
        }
    };
    r->cull_pipeline = wgpuDeviceCreateComputePipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader);
}

void box_cull_init(
    box_renderer_t *r)
{
    box_vec_init(&r->cull_jobs, sizeof(box_cull_job_t));
    box_vec_init(&r->cull_ranges, sizeof(box_cull_range_t));
    box_cull_pipeline(r);
}

bool box_cull_packet(
    const box_packet_t *p)
{
    bool result = p->views.count != 0;
    uint32_t i;
    for (i = 0; i < p->views.count && result; i ++) {
        result = ((box_view_t*)box_vec_at(&p->views, i))->cull_enabled != 0;
    }

    return result;
}

void box_cull_frame(
    box_renderer_t *r,
    const box_packet_t *p)
{
    r->cull_slots = 0;
    r->cull_arg_count = 0;
    r->cull_drawn = 0;
    r->cull_culled = 0;
    r->cull_tested = 0;
    box_hiz_frame(r, p);

    uint32_t jobs = (p->views.count + 1) * (r->meshes.count + 1);
    if (jobs <= r->cull_uniform_capacity) {
        goto done;
    }

    if (r->cull_uniform) wgpuBufferRelease(r->cull_uniform);
    if (r->cull_args) wgpuBufferRelease(r->cull_args);
    r->cull_uniform = box_cull_buffer(r, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        (uint64_t)jobs * BOX_CULL_UNIFORM_STRIDE);
    r->cull_args = box_cull_buffer(r, WGPUBufferUsage_Indirect | WGPUBufferUsage_Storage |
        WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
        (uint64_t)jobs * BOX_CULL_ARG_WORDS * sizeof(uint32_t));
    r->cull_uniform_capacity = jobs;
    r->cull_args_capacity = jobs;
done:
    return;
}

/** Grow the visible instance buffer of the current view.
 *
 * @param r The renderer
 * @param count Instances the buffer must hold
 */
static void box_cull_grow(
    box_renderer_t *r,
    uint32_t count)
{
    uint32_t limit = box_device_instances(r, true);
    if (count <= r->v->visible_capacity || r->v->visible_capacity >= limit) {
        goto done;
    }

    uint64_t doubled = r->v->visible_capacity ?
        (uint64_t)r->v->visible_capacity * 2 : BOX_CULL_CAPACITY;
    if (doubled < count) {
        doubled = count;
    }

    uint32_t grown = doubled > limit ? limit : (uint32_t)doubled;
    if (r->v->visible) wgpuBufferRelease(r->v->visible);
    r->v->visible = box_cull_buffer(r, WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage |
        WGPUBufferUsage_CopyDst, (uint64_t)grown * sizeof(box_instance_t));
    r->v->visible_capacity = grown;
done:
    return;
}

/** Collect the slot ranges of a mesh the view shows, coalescing neighbours.
 *
 * @param r The renderer
 * @param m The mesh entry
 * @param view The view being drawn
 */
static void box_cull_ranges_of(
    box_renderer_t *r,
    const box_gpu_mesh_t *m,
    const box_view_t *view)
{
    box_vec_clear(&r->cull_ranges);
    if (!m->groups.count) {
        if (box_view_shows(view, 0)) {
            box_cull_range_t *all = box_vec_push(&r->cull_ranges);
            all->first = 0;
            all->count = m->static_count;
        }

        goto done;
    }

    if (view->layer_mask == BOX_VIEW_LAYER_ALL) {
        box_cull_range_t *all = box_vec_push(&r->cull_ranges);
        all->first = 0;
        all->count = m->static_count;
        goto done;
    }

    box_cull_range_t *open = NULL;
    uint32_t g;
    for (g = 0; g < m->groups.count; g ++) {
        box_static_group_t *run = box_vec_at(&m->groups, g);
        if (!box_view_shows(view, run->mask)) {
            open = NULL;
            continue;
        }

        if (open && run->first == open->first + open->count) {
            open->count += run->count;
            continue;
        }

        open = box_vec_push(&r->cull_ranges);
        open->first = run->first;
        open->count = run->count;
    }

done:
    return;
}

/** Count the instances of a range the frustum keeps.
 *
 * @param f The frustum of the view
 * @param src The instances to test
 * @param count Instances in the array
 * @return Instances the frustum keeps
 */
static uint32_t box_cull_test(
    const box_frustum_t *f,
    const box_instance_t *src,
    uint32_t count)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < count; i ++) {
        result += box_frustum_sphere(f, src[i].bounds, src[i].bounds[3]) ? 1u : 0u;
    }

    return result;
}

/** Copy the instances of a range that the frustum keeps into the staging array.
 *
 * @param f The frustum of the view
 * @param src The instances to test
 * @param count Instances in the array
 * @param out The staging array of the view
 * @return Instances the frustum kept
 */
static uint32_t box_cull_compact(
    const box_frustum_t *f,
    const box_instance_t *src,
    uint32_t count,
    box_vec_t *out)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < count; i ++) {
        if (!box_frustum_sphere(f, src[i].bounds, src[i].bounds[3])) {
            continue;
        }

        *(box_instance_t*)box_vec_push(out) = src[i];
        result ++;
    }

    return result;
}

/** Add the CPU culled draw of one static mesh to the draw list of the view.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @param m The mesh entry
 * @param f The frustum of the view
 */
static void box_cull_static_cpu(
    box_renderer_t *r,
    uint32_t mesh,
    const box_gpu_mesh_t *m,
    const box_frustum_t *f)
{
    box_cull_draw_t *draw = box_vec_push(&r->v->draws);
    draw->mesh = mesh;
    draw->first = 0;
    draw->count = 0;
    draw->args = BOX_CULL_NO_ARGS;
    draw->baked = 1;
    draw->stage = r->v->staging.count;
    draw->pass = BOX_HIZ_PASS_VISIBLE;
    draw->src = NULL;

    const box_instance_t *mirror = box_vec_at(&m->mirror, 0);
    box_cull_range_t *one = box_vec_at(&r->cull_ranges, 0);
    if (r->cull_ranges.count == 1 &&
        box_cull_test(f, mirror + one->first, one->count) == one->count)
    {
        draw->src = mirror + one->first;
        draw->count = one->count;
        goto done;
    }

    uint32_t tested = 0;
    uint32_t i;
    for (i = 0; i < r->cull_ranges.count; i ++) {
        box_cull_range_t *range = box_vec_at(&r->cull_ranges, i);
        draw->count += box_cull_compact(f, mirror + range->first, range->count,
            &r->v->staging);
        tested += range->count;
    }

    r->cull_culled += tested - draw->count;
done:
    draw->limit = draw->count;
    r->cull_drawn += draw->count;
}

/** Add one compute culled draw of a mesh to the draw list of the view.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @param pass One of the BOX_HIZ_PASS constants
 * @return Index of the draw in the draw list
 */
static uint32_t box_cull_indirect(
    box_renderer_t *r,
    uint32_t mesh,
    uint32_t pass)
{
    uint32_t result = r->v->draws.count;
    box_cull_draw_t *draw = box_vec_push(&r->v->draws);
    draw->mesh = mesh;
    draw->first = 0;
    draw->count = 0;
    draw->limit = 0;
    draw->args = r->cull_arg_count * BOX_CULL_ARG_WORDS * (uint32_t)sizeof(uint32_t);
    draw->baked = 1;
    draw->stage = 0;
    draw->pass = pass;
    draw->src = NULL;
    r->cull_arg_count ++;

    return result;
}

/** Add the compute culled draws of one static mesh and the dispatches they need.
 *
 * A view that occlusion culls gets a second draw for the instances its pyramid
 * uncovers, whose dispatch runs after the first draw filled the depth target.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @param occlusion The view tests the mesh against its depth pyramid
 */
static void box_cull_static_gpu(
    box_renderer_t *r,
    uint32_t mesh,
    bool occlusion)
{
    uint32_t index = box_cull_indirect(r, mesh, BOX_HIZ_PASS_VISIBLE);
    uint32_t second = occlusion ? box_cull_indirect(r, mesh, BOX_HIZ_PASS_NEW) : index;

    uint32_t i;
    for (i = 0; i < r->cull_ranges.count; i ++) {
        box_cull_range_t *range = box_vec_at(&r->cull_ranges, i);
        if (occlusion) {
            box_hiz_job(r, mesh, range->first, range->count, index, BOX_HIZ_PASS_VISIBLE);
            box_hiz_job(r, mesh, range->first, range->count, second, BOX_HIZ_PASS_NEW);
            ((box_cull_draw_t*)box_vec_at(&r->v->draws, second))->limit += range->count;
        } else {
            box_cull_job_t *job = box_vec_push(&r->cull_jobs);
            job->mesh = mesh;
            job->first = range->first;
            job->count = range->count;
            job->slot = r->cull_slots ++;
            job->draw = index;
            job->pass = BOX_HIZ_PASS_VISIBLE;
        }

        ((box_cull_draw_t*)box_vec_at(&r->v->draws, index))->limit += range->count;
    }

    r->cull_tested += ((box_cull_draw_t*)box_vec_at(&r->v->draws, index))->limit;
}

/** Cull the dynamic batches the view draws into its staging array.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param p The packet
 * @param f The frustum of the view
 */
static void box_cull_dynamic(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    const box_frustum_t *f)
{
    uint32_t last = view->dynamic_first + view->dynamic_count;
    uint32_t i;
    for (i = view->dynamic_first; i < last; i ++) {
        box_dynamic_batch_t *b = box_vec_at(&p->dynamic_batches, i);
        if (!b->count || b->mesh >= r->meshes.count) {
            continue;
        }

        const box_instance_t *src = box_vec_at(&p->dynamic_instances, b->first);
        box_cull_draw_t *draw = box_vec_push(&r->v->draws);
        draw->mesh = b->mesh;
        draw->first = 0;
        draw->args = BOX_CULL_NO_ARGS;
        draw->baked = 0;
        draw->stage = r->v->staging.count;
        draw->pass = BOX_HIZ_PASS_VISIBLE;
        draw->src = NULL;
        draw->count = box_cull_test(f, src, b->count);
        if (draw->count == b->count) {
            draw->src = src;
        } else {
            draw->count = box_cull_compact(f, src, b->count, &r->v->staging);
        }

        draw->limit = draw->count;
        r->cull_drawn += draw->count;
        r->cull_culled += b->count - draw->count;
    }
}

/** Give every draw of the view its slice of the visible buffer.
 *
 * The CPU culled draws take the front of the buffer, so the compute pass writes
 * behind what the upload of the frame fills.
 *
 * @param r The renderer
 * @param limit Instances the visible buffer of the view may hold at most
 * @return Instances the visible buffer of the view must hold
 */
static uint32_t box_cull_reserve(
    box_renderer_t *r,
    uint32_t limit)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < r->v->draws.count; i ++) {
        box_cull_draw_t *draw = box_vec_at(&r->v->draws, i);
        if (draw->args != BOX_CULL_NO_ARGS) {
            continue;
        }

        draw->count = draw->count > limit - result ? limit - result : draw->count;
        draw->limit = draw->count;
        draw->first = result;
        result += draw->count;
    }

    for (i = 0; i < r->v->draws.count; i ++) {
        box_cull_draw_t *draw = box_vec_at(&r->v->draws, i);
        if (draw->args == BOX_CULL_NO_ARGS) {
            continue;
        }

        draw->limit = draw->limit > limit - result ? limit - result : draw->limit;
        draw->first = result;
        result += draw->limit;
    }

    return result;
}

/** Upload the instances every CPU culled draw of the view kept.
 *
 * A draw the frustum kept whole is written straight out of its source, so only
 * a partly visible range pays for a copy into the staging array.
 *
 * @param r The renderer
 */
static void box_cull_write(
    box_renderer_t *r)
{
    uint64_t stride = sizeof(box_instance_t);
    uint32_t i;
    for (i = 0; i < r->v->draws.count; i ++) {
        box_cull_draw_t *draw = box_vec_at(&r->v->draws, i);
        if (draw->args != BOX_CULL_NO_ARGS || !draw->count) {
            continue;
        }

        const box_instance_t *src = draw->src;
        if (!src) {
            src = box_vec_at(&r->v->staging, draw->stage);
        }

        wgpuQueueWriteBuffer(r->gpu_queue, r->v->visible, (uint64_t)draw->first * stride,
            src, (size_t)draw->count * stride);
    }
}

/** Write the uniform of every dispatch and the arguments of every draw of the view.
 *
 * @param r The renderer
 * @param view The view
 * @param f The frustum of the view
 */
static void box_cull_upload(
    box_renderer_t *r,
    const box_view_t *view,
    const box_frustum_t *f)
{
    float clip[16];
    box_frustum_mul(view->proj, view->view, clip);
    uint32_t i;
    for (i = 0; i < r->v->draws.count; i ++) {
        box_cull_draw_t *draw = box_vec_at(&r->v->draws, i);
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, draw->mesh);
        if (draw->args == BOX_CULL_NO_ARGS) {
            continue;
        }

        uint32_t args[BOX_CULL_ARG_WORDS] = { m->index_count, 0, 0, 0, 0 };
        wgpuQueueWriteBuffer(r->gpu_queue, r->cull_args, draw->args, args, sizeof(args));
    }

    for (i = 0; i < r->cull_jobs.count; i ++) {
        box_cull_job_t *job = box_vec_at(&r->cull_jobs, i);
        box_cull_draw_t *draw = box_vec_at(&r->v->draws, job->draw);
        box_cull_uniform_t uniform;
        memset(&uniform, 0, sizeof(uniform));
        memcpy(uniform.planes, f->planes, sizeof(uniform.planes));
        memcpy(uniform.clip, clip, sizeof(uniform.clip));
        uniform.pixels[0] = (float)view->width;
        uniform.pixels[1] = (float)view->height;
        uniform.pixels[2] = view->min_dimension_px;
        uniform.first = job->first;
        uniform.count = job->count;
        uniform.base = draw->first;
        uniform.arg = draw->args / (BOX_CULL_ARG_WORDS * (uint32_t)sizeof(uint32_t));
        wgpuQueueWriteBuffer(r->gpu_queue, r->cull_uniform,
            (uint64_t)job->slot * BOX_CULL_UNIFORM_STRIDE, &uniform, sizeof(uniform));
    }
}

/** Count the instances a view without culling draws, for the frame statistics.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param p The packet
 */
static void box_cull_count(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < r->meshes.count; i ++) {
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        if (!m->alive || !m->static_count) {
            continue;
        }

        box_cull_ranges_of(r, m, view);
        uint32_t range;
        for (range = 0; range < r->cull_ranges.count; range ++) {
            r->cull_drawn += ((box_cull_range_t*)box_vec_at(&r->cull_ranges, range))->count;
        }
    }

    uint32_t last = view->dynamic_first + view->dynamic_count;
    for (i = view->dynamic_first; i < last; i ++) {
        r->cull_drawn += ((box_dynamic_batch_t*)box_vec_at(&p->dynamic_batches, i))->count;
    }
}

void box_cull_view(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p)
{
    box_vec_clear(&r->v->draws);
    box_vec_clear(&r->v->staging);
    box_vec_clear(&r->cull_jobs);
    box_vec_clear(&r->hiz_jobs);
    r->v->culled = false;
    r->v->hiz_active = false;
    if (!view->cull_enabled) {
        box_cull_count(r, view, p);
        goto done;
    }

    box_frustum_t frustum;
    box_frustum_of_view(view, &frustum);
    bool occlusion = view->occlusion != 0;
    if (occlusion) {
        box_hiz_prepare(r, view);
    }

    uint32_t i;
    for (i = 0; i < r->meshes.count; i ++) {
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        if (!m->alive || !m->static_count) {
            continue;
        }

        box_cull_ranges_of(r, m, view);
        if (!r->cull_ranges.count) {
            continue;
        }

        bool cpu = m->mirror_valid && m->mirror.count >= m->static_count &&
            m->static_count <= view->cull_threshold;
        if (cpu) {
            box_cull_static_cpu(r, i, m, &frustum);
        } else {
            box_cull_static_gpu(r, i, occlusion);
        }
    }

    box_cull_dynamic(r, view, p, &frustum);
    uint32_t total = box_cull_reserve(r, box_device_instances(r, true));
    box_cull_grow(r, total ? total : 1);
    box_cull_write(r);
    box_cull_upload(r, view, &frustum);
    box_hiz_upload(r, view, &frustum);
    r->v->culled = true;
done:
    return;
}

/** Build or reuse the bind group of the culling dispatches of one mesh in the view.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @return The bind group
 */
static WGPUBindGroup box_cull_group(
    box_renderer_t *r,
    uint32_t mesh)
{
    while (r->v->culls.count <= mesh) {
        box_gpu_cull_t *created = box_vec_push(&r->v->culls);
        memset(created, 0, sizeof(box_gpu_cull_t));
    }

    box_gpu_cull_t *entry = box_vec_at(&r->v->culls, mesh);
    box_gpu_mesh_t *m = box_vec_at(&r->meshes, mesh);
    if (entry->group && entry->src == m->statics && entry->dst == r->v->visible &&
        entry->args == r->cull_args)
    {
        goto done;
    }

    if (entry->group) wgpuBindGroupRelease(entry->group);
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = r->cull_uniform, .size = sizeof(box_cull_uniform_t) },
        { .binding = 1, .buffer = m->statics, .size = WGPU_WHOLE_SIZE },
        { .binding = 2, .buffer = r->v->visible, .size = WGPU_WHOLE_SIZE },
        { .binding = 3, .buffer = r->cull_args, .size = WGPU_WHOLE_SIZE }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->cull_layout,
        .entryCount = 4,
        .entries = entries
    };
    entry->group = wgpuDeviceCreateBindGroup(r->device, &desc);
    entry->src = m->statics;
    entry->dst = r->v->visible;
    entry->args = r->cull_args;
done:
    return entry->group;
}

void box_cull_dispatch(
    box_renderer_t *r,
    WGPUCommandEncoder encoder)
{
    if (!r->cull_jobs.count) {
        goto done;
    }

    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "cull");
    wgpuComputePassEncoderSetPipeline(pass, r->cull_pipeline);
    uint32_t i;
    for (i = 0; i < r->cull_jobs.count; i ++) {
        box_cull_job_t *job = box_vec_at(&r->cull_jobs, i);
        uint32_t offset = job->slot * BOX_CULL_UNIFORM_STRIDE;
        wgpuComputePassEncoderSetBindGroup(pass, 0, box_cull_group(r, job->mesh), 1, &offset);
        wgpuComputePassEncoderDispatchWorkgroups(pass,
            (job->count + BOX_CULL_GROUP - 1) / BOX_CULL_GROUP, 1, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
done:
    return;
}

void box_cull_draw(
    box_renderer_t *r,
    WGPURenderPassEncoder pass,
    uint32_t group)
{
    uint64_t stride = sizeof(box_instance_t);
    uint32_t i;
    for (i = 0; i < r->v->draws.count; i ++) {
        box_cull_draw_t *draw = box_vec_at(&r->v->draws, i);
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, draw->mesh);
        if (!m->alive || !draw->limit || draw->pass != group) {
            continue;
        }

        wgpuRenderPassEncoderSetPipeline(pass,
            box_frame_opaque_pipeline(r, draw->baked && !m->cutout_count));
        wgpuRenderPassEncoderSetBindGroup(pass, 3,
            box_bake_group(r, draw->mesh, draw->baked != 0), 0, NULL);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, r->v->visible,
            (uint64_t)draw->first * stride, (uint64_t)draw->limit * stride);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m->indices, WGPUIndexFormat_Uint32, 0,
            WGPU_WHOLE_SIZE);
        if (draw->args == BOX_CULL_NO_ARGS) {
            wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, draw->count, 0, 0, 0);
        } else {
            wgpuRenderPassEncoderDrawIndexedIndirect(pass, r->cull_args, draw->args);
        }
    }
}

#ifndef BOX_PLATFORM_WEB
/** Sum the instance counts the read back draw arguments hold.
 *
 * @param status Map outcome
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 Unused
 */
static void box_cull_mapped(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)message;
    (void)userdata2;
    box_renderer_t *r = userdata1;
    uint32_t words = r->cull_readback_count * BOX_CULL_ARG_WORDS + BOX_HIZ_STAT_WORDS;
    if (status != WGPUMapAsyncStatus_Success) {
        goto done;
    }

    const uint32_t *args = wgpuBufferGetConstMappedRange(r->cull_readback, 0,
        (size_t)words * sizeof(uint32_t));
    uint32_t total = 0;
    uint32_t i;
    for (i = 0; args && i < r->cull_readback_count; i ++) {
        total += args[i * BOX_CULL_ARG_WORDS + 1];
    }

    if (args) {
        r->cull_visible = total;
        r->hiz_tested = args[words - BOX_HIZ_STAT_WORDS];
        r->hiz_occluded = args[words - BOX_HIZ_STAT_WORDS + 1];
    }

    wgpuBufferUnmap(r->cull_readback);
done:
    r->cull_readback_busy = false;
}
#endif

void box_cull_record(
    box_renderer_t *r,
    WGPUCommandEncoder encoder)
{
#ifndef BOX_PLATFORM_WEB
    if (r->cull_readback_busy || !r->cull_arg_count) {
        goto done;
    }

    uint64_t size = (uint64_t)r->cull_arg_count * BOX_CULL_ARG_WORDS * sizeof(uint32_t);
    uint64_t stats = BOX_HIZ_STAT_WORDS * sizeof(uint32_t);
    if (r->cull_arg_count > r->cull_readback_capacity) {
        if (r->cull_readback) wgpuBufferRelease(r->cull_readback);
        r->cull_readback = box_cull_buffer(r, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            size + stats);
        r->cull_readback_capacity = r->cull_arg_count;
    }

    wgpuCommandEncoderCopyBufferToBuffer(encoder, r->cull_args, 0, r->cull_readback, 0, size);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, r->hiz_stats, 0, r->cull_readback, size,
        stats);
    r->cull_readback_count = r->cull_arg_count;
done:
    return;
#else
    (void)r;
    (void)encoder;
#endif
}

void box_cull_publish(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    wgpuDevicePoll(r->device, false, NULL);
    wgpuInstanceProcessEvents(r->instance);
#endif
    uint32_t visible = r->cull_visible < r->cull_tested ? r->cull_visible : r->cull_tested;
    uint32_t kept = r->hiz_used ? r->hiz_tested : visible;
    if (kept > r->cull_tested) {
        kept = r->cull_tested;
    }

    r->stats.drawn = r->cull_drawn + visible;
    r->stats.occluded = r->hiz_used ? r->hiz_occluded : 0;
    r->stats.culled = r->cull_culled + r->cull_tested - kept;
    r->stats.shadow_rebuilds = r->shadow_rebuilds;

#ifndef BOX_PLATFORM_WEB
    if (r->cull_readback_busy || !r->cull_readback_count || !r->cull_readback) {
        goto done;
    }

    size_t size = ((size_t)r->cull_readback_count * BOX_CULL_ARG_WORDS + BOX_HIZ_STAT_WORDS) *
        sizeof(uint32_t);
    WGPUBufferMapCallbackInfo info = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = box_cull_mapped,
        .userdata1 = r
    };
    r->cull_readback_busy = true;
    wgpuBufferMapAsync(r->cull_readback, WGPUMapMode_Read, 0, size, info);
done:
    return;
#endif
}

void box_cull_view_fini(
    box_gpu_view_t *v)
{
    uint32_t i;
    for (i = 0; i < v->culls.count; i ++) {
        box_gpu_cull_t *entry = box_vec_at(&v->culls, i);
        if (entry->group) wgpuBindGroupRelease(entry->group);
    }

    box_vec_fini(&v->culls);
    box_vec_fini(&v->draws);
    box_vec_fini(&v->staging);
    if (v->visible) wgpuBufferRelease(v->visible);
    v->visible = NULL;
    v->visible_capacity = 0;
}

void box_cull_fini(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    while (r->cull_readback_busy) {
        wgpuDevicePoll(r->device, true, NULL);
        wgpuInstanceProcessEvents(r->instance);
    }
#endif
    if (r->cull_readback) wgpuBufferRelease(r->cull_readback);
    r->cull_readback = NULL;
    r->cull_readback_capacity = 0;
    r->cull_readback_count = 0;
    if (r->cull_pipeline) wgpuComputePipelineRelease(r->cull_pipeline);
    if (r->cull_layout) wgpuBindGroupLayoutRelease(r->cull_layout);
    if (r->cull_uniform) wgpuBufferRelease(r->cull_uniform);
    if (r->cull_args) wgpuBufferRelease(r->cull_args);
    box_vec_fini(&r->cull_jobs);
    box_vec_fini(&r->cull_ranges);
    r->cull_pipeline = NULL;
    r->cull_layout = NULL;
    r->cull_uniform = NULL;
    r->cull_args = NULL;
}
