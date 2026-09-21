/**
 * @file renderer/shadow.c
 * @brief Cascaded shadow maps: the depth array of a view, its caster lists and its passes.
 *
 * Every view owns a depth array with one layer per cascade and a second array
 * that caches the static casters of every cascade. A frame copies the cached
 * layer into the working layer and draws the dynamic casters on top, and it
 * renders at most one dirty static layer, the nearest one first.
 */

#include "renderer_internal.h"
#include <stddef.h>
#include <string.h>

/** Instances the caster buffer of a view starts out with */
#define BOX_SHADOW_CAPACITY (256)

/** Create a GPU buffer.
 *
 * @param r The renderer
 * @param usage Buffer usage flags
 * @param size Size in bytes
 * @return The buffer
 */
static WGPUBuffer box_shadow_buffer(
    box_renderer_t *r,
    WGPUBufferUsage usage,
    uint64_t size)
{
    WGPUBufferDescriptor desc = { .usage = usage, .size = size };

    return wgpuDeviceCreateBuffer(r->device, &desc);
}

/** Create a depth array texture of one shadow map.
 *
 * @param r The renderer
 * @param size Edge of one cascade in texels
 * @param layers Cascades the array holds
 * @return The texture
 */
static WGPUTexture box_shadow_texture(
    box_renderer_t *r,
    uint32_t size,
    uint32_t layers)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { size, size, layers },
        .format = WGPUTextureFormat_Depth32Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    return wgpuDeviceCreateTexture(r->device, &desc);
}

/** Create the view of one layer of a depth array, as a render attachment.
 *
 * @param texture The depth array
 * @param layer The array layer
 * @return The view
 */
static WGPUTextureView box_shadow_layer_view(
    WGPUTexture texture,
    uint32_t layer)
{
    WGPUTextureViewDescriptor desc = {
        .format = WGPUTextureFormat_Depth32Float,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = layer,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &desc);
}

/** Create the array view a shader samples a depth array through.
 *
 * @param texture The depth array
 * @param layers Cascades the array holds
 * @return The view
 */
static WGPUTextureView box_shadow_array_view(
    WGPUTexture texture,
    uint32_t layers)
{
    WGPUTextureViewDescriptor desc = {
        .format = WGPUTextureFormat_Depth32Float,
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = layers,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &desc);
}

/** Create the depth only pipeline that draws casters into a cascade.
 *
 * @param r The renderer
 */
static void box_shadow_pipeline(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = true,
            .minBindingSize = 16 * sizeof(float)
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 1, .entries = &entry };
    r->shadow_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    r->shadow_shader = box_shader_module(r, BOX_SHADER_SHADOW);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->shadow_layout
    };
    r->shadow_pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

    WGPUVertexAttribute vertex_attr = {
        .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0
    };
    WGPUVertexAttribute instance_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x4, .offset = 0, .shaderLocation = 3 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 16, .shaderLocation = 4 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 32, .shaderLocation = 5 }
    };
    WGPUVertexBufferLayout layouts[2] = {
        {
            .arrayStride = BOX_VERTEX_FLOATS * sizeof(float),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 1,
            .attributes = &vertex_attr
        }, {
            .arrayStride = sizeof(box_instance_t),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = 3,
            .attributes = instance_attrs
        }
    };
    WGPUDepthStencilState depth = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always },
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = r->shadow_pipeline_layout,
        .vertex = {
            .module = r->shadow_shader,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN },
            .bufferCount = 2,
            .buffers = layouts
        },
        .depthStencil = &depth,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    r->shadow_pipeline = wgpuDeviceCreateRenderPipeline(r->device, &desc);
}

void box_shadow_init(
    box_renderer_t *r)
{
    box_vec_init(&r->shadow_jobs, sizeof(box_cull_job_t));
    box_shadow_pipeline(r);

    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 1.0f,
        .compare = WGPUCompareFunction_Greater,
        .maxAnisotropy = 1
    };
    r->shadow_sampler = wgpuDeviceCreateSampler(r->device, &sampler_desc);
    r->shadow_dummy = box_shadow_texture(r, 1, 1);
    r->shadow_dummy_view = box_shadow_array_view(r->shadow_dummy, 1);
}

void box_shadow_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base)
{
    WGPUTextureView view = r->v && r->v->shadow_sample ?
        r->v->shadow_sample : r->shadow_dummy_view;
    out[0] = (WGPUBindGroupEntry){ .binding = base, .textureView = view };
    out[1] = (WGPUBindGroupEntry){ .binding = base + 1, .sampler = r->shadow_sampler };
}

void box_shadow_frame(
    box_renderer_t *r,
    const box_packet_t *p)
{
    r->shadow_slots = 0;
    r->shadow_arg_count = 0;
    r->shadow_rebuilds = 0;

    uint32_t jobs = (p->views.count + 1) * (r->meshes.count + 1);
    if (jobs <= r->shadow_uniform_capacity) {
        goto done;
    }

    if (r->shadow_cull_uniform) wgpuBufferRelease(r->shadow_cull_uniform);
    if (r->shadow_args) wgpuBufferRelease(r->shadow_args);
    r->shadow_cull_uniform = box_shadow_buffer(r,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        (uint64_t)jobs * BOX_CULL_UNIFORM_STRIDE);
    r->shadow_args = box_shadow_buffer(r, WGPUBufferUsage_Indirect | WGPUBufferUsage_Storage |
        WGPUBufferUsage_CopyDst, (uint64_t)jobs * BOX_CULL_ARG_WORDS * sizeof(uint32_t));
    r->shadow_uniform_capacity = jobs;
done:
    return;
}

/** Create or resize the depth arrays and the uniform of the current view.
 *
 * @param r The renderer
 * @param view The view being drawn
 */
static void box_shadow_textures(
    box_renderer_t *r,
    const box_view_t *view)
{
    box_gpu_view_t *v = r->v;
    if (v->shadow_size == view->shadow_resolution && v->shadow_count == view->shadow_cascades) {
        goto done;
    }

    box_shadow_view_fini(v);
    v->shadow_size = view->shadow_resolution;
    v->shadow_count = view->shadow_cascades;
    v->shadow_depth = box_shadow_texture(r, v->shadow_size, v->shadow_count);
    v->shadow_static = box_shadow_texture(r, v->shadow_size, v->shadow_count);
    v->shadow_sample = box_shadow_array_view(v->shadow_depth, v->shadow_count);

    uint32_t i;
    for (i = 0; i < v->shadow_count; i ++) {
        v->shadow_attach[i] = box_shadow_layer_view(v->shadow_depth, i);
        v->shadow_cached[i] = box_shadow_layer_view(v->shadow_static, i);
    }

    v->shadow_uniform = box_shadow_buffer(r, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        BOX_SHADOW_CASCADE_MAX * BOX_CULL_UNIFORM_STRIDE);
    WGPUBindGroupEntry entry = {
        .binding = 0, .buffer = v->shadow_uniform, .size = 16 * sizeof(float)
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->shadow_layout, .entryCount = 1, .entries = &entry
    };
    v->shadow_group = wgpuDeviceCreateBindGroup(r->device, &desc);
    box_view_group(r);
done:
    return;
}

/** Mark the cascades whose cached static layer no longer holds the scene.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param p The packet
 */
static void box_shadow_invalidate(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p)
{
    box_gpu_view_t *v = r->v;
    box_shadow_cache_view(&v->shadow_cache, view);
    if (p->static_counts.count) {
        box_shadow_cache_all(&v->shadow_cache, view);
        goto done;
    }

    uint32_t i;
    for (i = 0; i < p->static_updates.count; i ++) {
        box_static_update_t *u = box_vec_at(&p->static_updates, i);
        box_shadow_cache_delta(&v->shadow_cache, view, u->data.bounds);
    }

done:
    return;
}

/** Collect the slot ranges of a mesh that cast the shadows of a view.
 *
 * @param r The renderer
 * @param m The mesh entry
 * @param view The view being drawn
 */
static void box_shadow_ranges(
    box_renderer_t *r,
    const box_gpu_mesh_t *m,
    const box_view_t *view)
{
    box_vec_clear(&r->cull_ranges);
    if (!m->groups.count) {
        if (box_view_casts(view, 0)) {
            box_cull_range_t *all = box_vec_push(&r->cull_ranges);
            all->first = 0;
            all->count = m->static_count;
        }

        goto done;
    }

    if (view->caster_mask == BOX_VIEW_LAYER_ALL) {
        box_cull_range_t *all = box_vec_push(&r->cull_ranges);
        all->first = 0;
        all->count = m->static_count;
        goto done;
    }

    box_cull_range_t *open = NULL;
    uint32_t g;
    for (g = 0; g < m->groups.count; g ++) {
        box_static_group_t *run = box_vec_at(&m->groups, g);
        if (!box_view_casts(view, run->mask)) {
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

/** Copy the casters of a range that the cascade frustum keeps into the staging array.
 *
 * @param f The frustum of the cascade
 * @param src The instances to test
 * @param count Instances in the array
 * @param out The staging array of the view
 * @return Instances the frustum kept
 */
static uint32_t box_shadow_compact(
    const box_frustum_t *f,
    const box_instance_t *src,
    uint32_t count,
    box_vec_t *out)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < count; i ++) {
        if (!box_instance_caster(&src[i]) ||
            !box_frustum_sphere(f, src[i].bounds, src[i].bounds[3]))
        {
            continue;
        }

        *(box_instance_t*)box_vec_push(out) = src[i];
        result ++;
    }

    return result;
}

/** Append a CPU culled caster draw of one mesh to the shadow draw list.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @param src The instances to test
 * @param count Instances in the array
 * @param cascade The cascade the draw belongs to
 * @param statics The draw fills the cached static layer
 * @param f The frustum of the cascade
 */
static void box_shadow_draw_cpu(
    box_renderer_t *r,
    uint32_t mesh,
    const box_instance_t *src,
    uint32_t count,
    uint32_t cascade,
    uint32_t statics,
    const box_frustum_t *f)
{
    uint32_t stage = r->v->shadow_staging.count;
    uint32_t kept = box_shadow_compact(f, src, count, &r->v->shadow_staging);
    if (!kept) {
        goto done;
    }

    box_shadow_draw_t *draw = box_vec_push(&r->v->shadow_draws);
    draw->mesh = mesh;
    draw->first = 0;
    draw->args = BOX_CULL_NO_ARGS;
    draw->cascade = cascade;
    draw->statics = statics;
    draw->stage = stage;
    draw->count = kept;
    draw->limit = kept;
done:
    return;
}

/** Append the compute culled caster draw of one static mesh and its dispatches.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @param cascade The cascade the draw belongs to
 */
static void box_shadow_draw_gpu(
    box_renderer_t *r,
    uint32_t mesh,
    uint32_t cascade)
{
    uint32_t index = r->v->shadow_draws.count;
    box_shadow_draw_t *draw = box_vec_push(&r->v->shadow_draws);
    draw->mesh = mesh;
    draw->first = 0;
    draw->count = 0;
    draw->limit = 0;
    draw->cascade = cascade;
    draw->statics = 1;
    draw->stage = 0;
    draw->args = r->shadow_arg_count * BOX_CULL_ARG_WORDS * (uint32_t)sizeof(uint32_t);

    uint32_t i;
    for (i = 0; i < r->cull_ranges.count; i ++) {
        if (r->shadow_slots >= r->shadow_uniform_capacity) {
            break;
        }

        box_cull_range_t *range = box_vec_at(&r->cull_ranges, i);
        box_cull_job_t *job = box_vec_push(&r->shadow_jobs);
        job->mesh = mesh;
        job->first = range->first;
        job->count = range->count;
        job->slot = r->shadow_slots ++;
        job->draw = index;
        draw = box_vec_at(&r->v->shadow_draws, index);
        draw->limit += range->count;
    }

    r->shadow_arg_count ++;
}

/** Build the caster draws of the static layer of the cascade this frame renders.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param cascade The cascade whose static layer is rendered
 */
static void box_shadow_statics(
    box_renderer_t *r,
    const box_view_t *view,
    uint32_t cascade)
{
    box_frustum_t frustum;
    box_frustum_of_clip(view->shadow_matrix + cascade * 16, &frustum);

    uint32_t i;
    for (i = 0; i < r->meshes.count; i ++) {
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, i);
        if (!m->alive || !m->static_count) {
            continue;
        }

        box_shadow_ranges(r, m, view);
        if (!r->cull_ranges.count) {
            continue;
        }

        bool cpu = m->mirror_valid && m->mirror.count >= m->static_count &&
            m->static_count <= view->cull_threshold;
        if (!cpu) {
            box_shadow_draw_gpu(r, i, cascade);
            continue;
        }

        const box_instance_t *mirror = box_vec_at(&m->mirror, 0);
        uint32_t range;
        for (range = 0; range < r->cull_ranges.count; range ++) {
            box_cull_range_t *span = box_vec_at(&r->cull_ranges, range);
            box_shadow_draw_cpu(r, i, mirror + span->first, span->count, cascade,
                1, &frustum);
        }
    }
}

/** Build the caster draws of the dynamic and sorted instances of one cascade.
 *
 * @param r The renderer
 * @param view The view being drawn
 * @param p The packet
 * @param cascade The cascade the draws belong to
 */
static void box_shadow_dynamics(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    uint32_t cascade)
{
    box_frustum_t frustum;
    box_frustum_of_clip(box_shadow_cache_matrix(&r->v->shadow_cache, view, cascade,
        r->v->shadow_rebuild), &frustum);

    uint32_t last = view->dynamic_first + view->dynamic_count;
    uint32_t i;
    for (i = view->dynamic_first; i < last; i ++) {
        box_dynamic_batch_t *b = box_vec_at(&p->dynamic_batches, i);
        if (!b->count || b->mesh >= r->meshes.count) {
            continue;
        }

        box_shadow_draw_cpu(r, b->mesh, box_vec_at(&p->dynamic_instances, b->first), b->count,
            cascade, 0, &frustum);
    }

    last = view->transparent_first + view->transparent_count;
    for (i = view->transparent_first; i < last; i ++) {
        box_transparent_t *entry = box_vec_at(&p->transparent, i);
        if (entry->mesh >= r->meshes.count) {
            continue;
        }

        box_shadow_draw_cpu(r, entry->mesh, &entry->data, 1, cascade, 0, &frustum);
    }
}

/** Give every caster draw its slice of the caster buffer of the view.
 *
 * @param r The renderer
 * @param limit Instances the caster buffer of the view may hold at most
 * @return Instances the caster buffer must hold
 */
static uint32_t box_shadow_reserve(
    box_renderer_t *r,
    uint32_t limit)
{
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < r->v->shadow_draws.count; i ++) {
        box_shadow_draw_t *draw = box_vec_at(&r->v->shadow_draws, i);
        if (draw->args != BOX_CULL_NO_ARGS) {
            continue;
        }

        draw->count = draw->count > limit - result ? limit - result : draw->count;
        draw->limit = draw->count;
        draw->first = result;
        result += draw->count;
    }

    for (i = 0; i < r->v->shadow_draws.count; i ++) {
        box_shadow_draw_t *draw = box_vec_at(&r->v->shadow_draws, i);
        if (draw->args == BOX_CULL_NO_ARGS) {
            continue;
        }

        draw->limit = draw->limit > limit - result ? limit - result : draw->limit;
        draw->first = result;
        result += draw->limit;
    }

    return result;
}

/** Grow the caster instance buffer of the current view.
 *
 * @param r The renderer
 * @param count Instances the buffer must hold
 */
static void box_shadow_grow(
    box_renderer_t *r,
    uint32_t count)
{
    uint32_t limit = box_device_instances(r, true);
    if (count <= r->v->shadow_capacity || r->v->shadow_capacity >= limit) {
        goto done;
    }

    uint64_t doubled = r->v->shadow_capacity ?
        (uint64_t)r->v->shadow_capacity * 2 : BOX_SHADOW_CAPACITY;
    if (doubled < count) {
        doubled = count;
    }

    uint32_t grown = doubled > limit ? limit : (uint32_t)doubled;
    if (r->v->shadow_instances) wgpuBufferRelease(r->v->shadow_instances);
    r->v->shadow_instances = box_shadow_buffer(r, WGPUBufferUsage_Vertex |
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        (uint64_t)grown * sizeof(box_instance_t));
    r->v->shadow_capacity = grown;
done:
    return;
}

/** Upload the casters of every CPU culled draw and the uniforms of every dispatch.
 *
 * @param r The renderer
 * @param view The view being drawn
 */
static void box_shadow_upload(
    box_renderer_t *r,
    const box_view_t *view)
{
    uint64_t stride = sizeof(box_instance_t);
    uint32_t i;
    for (i = 0; i < r->v->shadow_draws.count; i ++) {
        box_shadow_draw_t *draw = box_vec_at(&r->v->shadow_draws, i);
        box_gpu_mesh_t *m = box_vec_at(&r->meshes, draw->mesh);
        if (draw->args != BOX_CULL_NO_ARGS) {
            uint32_t args[BOX_CULL_ARG_WORDS] = { m->index_count, 0, 0, 0, 0 };
            wgpuQueueWriteBuffer(r->gpu_queue, r->shadow_args, draw->args, args, sizeof(args));
            continue;
        }

        if (draw->count) {
            wgpuQueueWriteBuffer(r->gpu_queue, r->v->shadow_instances,
                (uint64_t)draw->first * stride, box_vec_at(&r->v->shadow_staging, draw->stage),
                (size_t)draw->count * stride);
        }
    }

    for (i = 0; i < r->shadow_jobs.count; i ++) {
        box_cull_job_t *job = box_vec_at(&r->shadow_jobs, i);
        box_shadow_draw_t *draw = box_vec_at(&r->v->shadow_draws, job->draw);
        box_frustum_t frustum;
        box_frustum_of_clip(box_shadow_cache_matrix(&r->v->shadow_cache, view,
            draw->cascade, r->v->shadow_rebuild), &frustum);
        box_cull_uniform_t uniform;
        memset(&uniform, 0, sizeof(uniform));
        memcpy(uniform.planes, frustum.planes, sizeof(uniform.planes));
        uniform.first = job->first;
        uniform.count = job->count;
        uniform.base = draw->first;
        uniform.arg = draw->args / (BOX_CULL_ARG_WORDS * (uint32_t)sizeof(uint32_t));
        uniform.caster = 1;
        wgpuQueueWriteBuffer(r->gpu_queue, r->shadow_cull_uniform,
            (uint64_t)job->slot * BOX_CULL_UNIFORM_STRIDE, &uniform, sizeof(uniform));
    }
}

/** Upload the matrix of every cascade to the shadow passes and to the view uniform.
 *
 * A cascade that keeps its layer this frame is drawn and sampled with the
 * matrix that layer was rendered with, which replaces the matrix of the view
 * the frame wrote into the uniform of the view before the pick.
 *
 * @param r The renderer
 * @param view The view being drawn
 */
static void box_shadow_matrices(
    box_renderer_t *r,
    const box_view_t *view)
{
    box_gpu_view_t *v = r->v;
    uint32_t i;
    for (i = 0; i < view->shadow_cascades; i ++) {
        const float *m = box_shadow_cache_matrix(&v->shadow_cache, view, i, v->shadow_rebuild);
        wgpuQueueWriteBuffer(r->gpu_queue, v->shadow_uniform,
            (uint64_t)i * BOX_CULL_UNIFORM_STRIDE, m, 16 * sizeof(float));
        if (m != view->shadow_matrix + i * 16) {
            wgpuQueueWriteBuffer(r->gpu_queue, v->view_buffer,
                offsetof(box_view_t, shadow_matrix) + (uint64_t)i * 16 * sizeof(float), m,
                16 * sizeof(float));
        }
    }
}

void box_shadow_prepare(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p)
{
    box_gpu_view_t *v = r->v;
    box_vec_clear(&v->shadow_draws);
    box_vec_clear(&v->shadow_staging);
    box_vec_clear(&r->shadow_jobs);
    v->shadow_rebuild = 0;
    if (!view->shadow_cascades) {
        goto done;
    }

    box_shadow_textures(r, view);
    box_shadow_invalidate(r, view, p);
    v->shadow_rebuild = box_shadow_cache_pick(&v->shadow_cache, view);
    box_shadow_matrices(r, view);

    uint32_t i;
    for (i = 0; i < view->shadow_cascades; i ++) {
        if (v->shadow_rebuild & (1u << i)) {
            r->shadow_rebuilds ++;
            box_shadow_statics(r, view, i);
        }

        box_shadow_dynamics(r, view, p, i);
    }

    uint32_t total = box_shadow_reserve(r, box_device_instances(r, true));
    box_shadow_grow(r, total ? total : 1);
    box_shadow_upload(r, view);
done:
    return;
}

/** Build or reuse the bind group of the shadow culling dispatches of one mesh.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @return The bind group
 */
static WGPUBindGroup box_shadow_cull_group(
    box_renderer_t *r,
    uint32_t mesh)
{
    while (r->v->shadow_culls.count <= mesh) {
        box_gpu_cull_t *created = box_vec_push(&r->v->shadow_culls);
        memset(created, 0, sizeof(box_gpu_cull_t));
    }

    box_gpu_cull_t *entry = box_vec_at(&r->v->shadow_culls, mesh);
    box_gpu_mesh_t *m = box_vec_at(&r->meshes, mesh);
    if (entry->group && entry->src == m->statics && entry->dst == r->v->shadow_instances &&
        entry->args == r->shadow_args)
    {
        goto done;
    }

    if (entry->group) wgpuBindGroupRelease(entry->group);
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = r->shadow_cull_uniform, .size = sizeof(box_cull_uniform_t) },
        { .binding = 1, .buffer = m->statics, .size = WGPU_WHOLE_SIZE },
        { .binding = 2, .buffer = r->v->shadow_instances, .size = WGPU_WHOLE_SIZE },
        { .binding = 3, .buffer = r->shadow_args, .size = WGPU_WHOLE_SIZE }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->cull_layout,
        .entryCount = 4,
        .entries = entries
    };
    entry->group = wgpuDeviceCreateBindGroup(r->device, &desc);
    entry->src = m->statics;
    entry->dst = r->v->shadow_instances;
    entry->args = r->shadow_args;
done:
    return entry->group;
}

void box_shadow_dispatch(
    box_renderer_t *r,
    WGPUCommandEncoder encoder)
{
    if (!r->shadow_jobs.count) {
        goto done;
    }

    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "shadow.cull");
    wgpuComputePassEncoderSetPipeline(pass, r->cull_pipeline);
    uint32_t i;
    for (i = 0; i < r->shadow_jobs.count; i ++) {
        box_cull_job_t *job = box_vec_at(&r->shadow_jobs, i);
        uint32_t offset = job->slot * BOX_CULL_UNIFORM_STRIDE;
        wgpuComputePassEncoderSetBindGroup(pass, 0, box_shadow_cull_group(r, job->mesh), 1,
            &offset);
        wgpuComputePassEncoderDispatchWorkgroups(pass,
            (job->count + BOX_CULL_GROUP - 1) / BOX_CULL_GROUP, 1, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
done:
    return;
}

/** Record the caster draws of one cascade into a depth layer.
 *
 * @param r The renderer
 * @param cascade The cascade
 * @param statics Record the draws of the cached static layer
 * @param attachment The depth layer the draws write
 * @param clear The layer is cleared instead of loaded
 * @param encoder The command encoder
 */
static void box_shadow_pass(
    box_renderer_t *r,
    uint32_t cascade,
    uint32_t statics,
    WGPUTextureView attachment,
    bool clear,
    WGPUCommandEncoder encoder)
{
    WGPURenderPassDepthStencilAttachment depth = {
        .view = attachment,
        .depthLoadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor desc = { .depthStencilAttachment = &depth };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "shadow");
    wgpuRenderPassEncoderSetPipeline(pass, r->shadow_pipeline);
    uint32_t offset = cascade * BOX_CULL_UNIFORM_STRIDE;
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->shadow_group, 1, &offset);

    uint64_t stride = sizeof(box_instance_t);
    uint32_t i;
    for (i = 0; i < r->v->shadow_draws.count; i ++) {
        box_shadow_draw_t *draw = box_vec_at(&r->v->shadow_draws, i);
        if (draw->cascade != cascade || draw->statics != statics || !draw->limit) {
            continue;
        }

        box_gpu_mesh_t *m = box_vec_at(&r->meshes, draw->mesh);
        if (!m->alive) {
            continue;
        }

        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, r->v->shadow_instances,
            (uint64_t)draw->first * stride, (uint64_t)draw->limit * stride);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m->indices, WGPUIndexFormat_Uint32, 0,
            WGPU_WHOLE_SIZE);
        if (draw->args == BOX_CULL_NO_ARGS) {
            wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, draw->count, 0, 0, 0);
        } else {
            wgpuRenderPassEncoderDrawIndexedIndirect(pass, r->shadow_args, draw->args);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Copy the cached static layer of a cascade into its working layer.
 *
 * @param r The renderer
 * @param cascade The cascade
 * @param encoder The command encoder
 */
static void box_shadow_copy(
    box_renderer_t *r,
    uint32_t cascade,
    WGPUCommandEncoder encoder)
{
    WGPUTexelCopyTextureInfo src = {
        .texture = r->v->shadow_static,
        .mipLevel = 0,
        .origin = { 0, 0, cascade },
        .aspect = WGPUTextureAspect_All
    };
    WGPUTexelCopyTextureInfo dst = {
        .texture = r->v->shadow_depth,
        .mipLevel = 0,
        .origin = { 0, 0, cascade },
        .aspect = WGPUTextureAspect_All
    };
    WGPUExtent3D size = { r->v->shadow_size, r->v->shadow_size, 1 };
    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &size);
}

/** Check whether a cascade holds a dynamic caster draw this frame.
 *
 * @param r The renderer
 * @param cascade The cascade
 * @return True when the working layer needs the dynamic pass
 */
static bool box_shadow_has_dynamic(
    box_renderer_t *r,
    uint32_t cascade)
{
    bool result = false;
    uint32_t i;
    for (i = 0; i < r->v->shadow_draws.count && !result; i ++) {
        box_shadow_draw_t *draw = box_vec_at(&r->v->shadow_draws, i);
        result = draw->cascade == cascade && !draw->statics && draw->limit;
    }

    return result;
}

void box_shadow_record(
    box_renderer_t *r,
    const box_view_t *view,
    WGPUCommandEncoder encoder)
{
    box_gpu_view_t *v = r->v;
    if (!view->shadow_cascades || !v->shadow_depth) {
        goto done;
    }

    uint32_t i;
    for (i = 0; i < view->shadow_cascades; i ++) {
        if (v->shadow_rebuild & (1u << i)) {
            box_shadow_pass(r, i, 1, v->shadow_cached[i], true, encoder);
            box_shadow_cache_done(&v->shadow_cache, view, i);
        }
    }

    for (i = 0; i < view->shadow_cascades; i ++) {
        bool dynamic = box_shadow_has_dynamic(r, i);
        if (!v->shadow_cache.valid[i]) {
            box_shadow_pass(r, i, 0, v->shadow_attach[i], true, encoder);
            v->shadow_cache.stale[i] = true;
            continue;
        }

        if (v->shadow_cache.stale[i]) {
            box_shadow_copy(r, i, encoder);
            v->shadow_cache.stale[i] = false;
        }

        if (dynamic) {
            box_shadow_pass(r, i, 0, v->shadow_attach[i], false, encoder);
            v->shadow_cache.stale[i] = true;
        }
    }

done:
    return;
}

void box_shadow_view_fini(
    box_gpu_view_t *v)
{
    uint32_t i;
    for (i = 0; i < BOX_SHADOW_CASCADE_MAX; i ++) {
        if (v->shadow_attach[i]) wgpuTextureViewRelease(v->shadow_attach[i]);
        if (v->shadow_cached[i]) wgpuTextureViewRelease(v->shadow_cached[i]);
        v->shadow_attach[i] = NULL;
        v->shadow_cached[i] = NULL;
    }

    for (i = 0; i < v->shadow_culls.count; i ++) {
        box_gpu_cull_t *entry = box_vec_at(&v->shadow_culls, i);
        if (entry->group) wgpuBindGroupRelease(entry->group);
    }

    box_vec_clear(&v->shadow_culls);
    if (v->shadow_sample) wgpuTextureViewRelease(v->shadow_sample);
    if (v->shadow_depth) wgpuTextureRelease(v->shadow_depth);
    if (v->shadow_static) wgpuTextureRelease(v->shadow_static);
    if (v->shadow_group) wgpuBindGroupRelease(v->shadow_group);
    if (v->shadow_uniform) wgpuBufferRelease(v->shadow_uniform);
    if (v->shadow_instances) wgpuBufferRelease(v->shadow_instances);
    v->shadow_sample = NULL;
    v->shadow_depth = NULL;
    v->shadow_static = NULL;
    v->shadow_group = NULL;
    v->shadow_uniform = NULL;
    v->shadow_instances = NULL;
    v->shadow_capacity = 0;
    v->shadow_size = 0;
    v->shadow_count = 0;
    memset(&v->shadow_cache, 0, sizeof(v->shadow_cache));
}

void box_shadow_fini(
    box_renderer_t *r)
{
    if (r->shadow_pipeline) wgpuRenderPipelineRelease(r->shadow_pipeline);
    if (r->shadow_pipeline_layout) wgpuPipelineLayoutRelease(r->shadow_pipeline_layout);
    if (r->shadow_shader) wgpuShaderModuleRelease(r->shadow_shader);
    if (r->shadow_layout) wgpuBindGroupLayoutRelease(r->shadow_layout);
    if (r->shadow_sampler) wgpuSamplerRelease(r->shadow_sampler);
    if (r->shadow_dummy_view) wgpuTextureViewRelease(r->shadow_dummy_view);
    if (r->shadow_dummy) wgpuTextureRelease(r->shadow_dummy);
    if (r->shadow_cull_uniform) wgpuBufferRelease(r->shadow_cull_uniform);
    if (r->shadow_args) wgpuBufferRelease(r->shadow_args);
    box_vec_fini(&r->shadow_jobs);
    r->shadow_pipeline = NULL;
    r->shadow_pipeline_layout = NULL;
    r->shadow_shader = NULL;
    r->shadow_layout = NULL;
    r->shadow_sampler = NULL;
    r->shadow_dummy_view = NULL;
    r->shadow_dummy = NULL;
    r->shadow_cull_uniform = NULL;
    r->shadow_args = NULL;
}
