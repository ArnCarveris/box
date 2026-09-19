/**
 * @file renderer/hiz.c
 * @brief Depth pyramids of a view and the occlusion pass that tests against them.
 *
 * A view that occlusion culls draws its static batches in two passes. The first
 * draws what the pyramid of the previous frame accepted, the depth of that pass
 * is reduced into the pyramid of this frame with the reverse Z rule of hiz.h,
 * and the second draws what the new pyramid accepts and the first pass did not
 * already draw. Nothing is ever dropped on the depth of an older frame, so a
 * camera may move as fast as it likes; the cost is one extra draw group.
 *
 * The split between the passes is recomputed instead of remembered: both passes
 * run the same test against the previous pyramid with the matrix of that frame,
 * which is what tells them whether the first pass drew an instance, so a view
 * keeps its two pyramids and nothing else between frames.
 *
 * A pyramid is a color mip chain filled by render passes rather than a storage
 * texture, because r32float storage textures are an optional feature that
 * wgpu-native and emdawnwebgpu do not both offer.
 */

#include "renderer_internal.h"
#include <string.h>

/** Format the levels of a depth pyramid are kept in */
#define BOX_HIZ_FORMAT (WGPUTextureFormat_R32Float)

/** Pyramids a view keeps, the one of this frame and the one of the previous */
#define BOX_HIZ_SLOTS (2)

/** Create the layout and the pipeline of one pass of a depth pyramid.
 *
 * @param r The renderer
 * @param handle Shader slot of the pass
 * @param sample Sample type of the texture the pass reads
 * @param out_layout The bind group layout to fill
 * @return The pipeline
 */
static WGPURenderPipeline box_hiz_level_pipeline(
    box_renderer_t *r,
    uint32_t handle,
    WGPUTextureSampleType sample,
    WGPUBindGroupLayout *out_layout)
{
    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .texture = { .sampleType = sample, .viewDimension = WGPUTextureViewDimension_2D }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 1, .entries = &entry };
    *out_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUShaderModule module = box_shader_module(r, handle);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = out_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

    WGPUColorTargetState target = {
        .format = BOX_HIZ_FORMAT,
        .writeMask = WGPUColorWriteMask_Red
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN }
        },
        .fragment = &fragment,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(module);

    return result;
}

/** Create the compute pipeline of one of the occlusion passes.
 *
 * @param r The renderer
 * @param module The module holding both entry points
 * @param entry Name of the compute entry point
 * @return The pipeline
 */
static WGPUComputePipeline box_hiz_pass_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    const char *entry)
{
    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->hiz_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

    WGPUComputePipelineDescriptor desc = {
        .layout = pipeline_layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    };
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);

    return result;
}

/** Create the bind group layout the occlusion passes share.
 *
 * @param r The renderer
 */
static void box_hiz_cull_layout(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[7] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_hiz_uniform_t)
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
        }, {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }, {
            .binding = 5,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 6,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 7, .entries = entries };
    r->hiz_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);
}

void box_hiz_init(
    box_renderer_t *r)
{
    box_vec_init(&r->hiz_jobs, sizeof(box_cull_job_t));
    r->hiz_copy = box_hiz_level_pipeline(r, BOX_SHADER_HIZ_COPY, WGPUTextureSampleType_Depth,
        &r->hiz_depth_layout);
    r->hiz_reduce = box_hiz_level_pipeline(r, BOX_SHADER_HIZ_REDUCE,
        WGPUTextureSampleType_UnfilterableFloat, &r->hiz_level_layout);
    box_hiz_cull_layout(r);

    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_HIZ);
    r->hiz_first = box_hiz_pass_pipeline(r, module, "cs_first");
    r->hiz_second = box_hiz_pass_pipeline(r, module, "cs_second");
    wgpuShaderModuleRelease(module);

    WGPUBufferDescriptor stats_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
        .size = BOX_HIZ_STAT_WORDS * sizeof(uint32_t)
    };
    r->hiz_stats = wgpuDeviceCreateBuffer(r->device, &stats_desc);
}

void box_hiz_frame(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t zero[BOX_HIZ_STAT_WORDS] = {0};
    r->hiz_slots = 0;
    r->hiz_used = false;
    wgpuQueueWriteBuffer(r->gpu_queue, r->hiz_stats, 0, zero, sizeof(zero));

    uint32_t jobs = (p->views.count + 1) * (r->meshes.count + 1) * BOX_HIZ_SLOTS;
    if (jobs <= r->hiz_uniform_capacity) {
        goto done;
    }

    if (r->hiz_uniform) wgpuBufferRelease(r->hiz_uniform);
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)jobs * BOX_HIZ_UNIFORM_STRIDE
    };
    r->hiz_uniform = wgpuDeviceCreateBuffer(r->device, &desc);
    r->hiz_uniform_capacity = jobs;
done:
    return;
}

void box_hiz_resize(
    box_gpu_view_t *v)
{
    uint32_t slot;
    for (slot = 0; slot < BOX_HIZ_SLOTS; slot ++) {
        uint32_t level;
        for (level = 0; level < v->hiz_levels; level ++) {
            if (v->hiz_mips[slot][level]) wgpuTextureViewRelease(v->hiz_mips[slot][level]);
            if (v->hiz_source[slot][level]) wgpuBindGroupRelease(v->hiz_source[slot][level]);
            v->hiz_mips[slot][level] = NULL;
            v->hiz_source[slot][level] = NULL;
        }

        if (v->hiz_view[slot]) wgpuTextureViewRelease(v->hiz_view[slot]);
        if (v->hiz[slot]) wgpuTextureRelease(v->hiz[slot]);
        v->hiz_view[slot] = NULL;
        v->hiz[slot] = NULL;
    }

    v->hiz_levels = 0;
    v->hiz_history = false;
}

/** Create the bind group one pass of a pyramid reads its source through.
 *
 * @param r The renderer
 * @param layout Layout of the pass
 * @param source The texture view the pass reads
 * @return The bind group
 */
static WGPUBindGroup box_hiz_source_group(
    box_renderer_t *r,
    WGPUBindGroupLayout layout,
    WGPUTextureView source)
{
    WGPUBindGroupEntry entry = { .binding = 0, .textureView = source };
    WGPUBindGroupDescriptor desc = { .layout = layout, .entryCount = 1, .entries = &entry };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

/** Create one of the two pyramids of the current view and the views of its levels.
 *
 * @param r The renderer
 * @param slot The pyramid of the view
 * @param levels Levels the pyramid holds
 */
static void box_hiz_create(
    box_renderer_t *r,
    uint32_t slot,
    uint32_t levels)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { box_hiz_half(r->v->width), box_hiz_half(r->v->height), 1 },
        .format = BOX_HIZ_FORMAT,
        .mipLevelCount = levels,
        .sampleCount = 1
    };
    r->v->hiz[slot] = wgpuDeviceCreateTexture(r->device, &desc);

    WGPUTextureViewDescriptor chain_desc = {
        .format = BOX_HIZ_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = levels,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };
    r->v->hiz_view[slot] = wgpuTextureCreateView(r->v->hiz[slot], &chain_desc);

    uint32_t level;
    for (level = 0; level < levels; level ++) {
        WGPUTextureViewDescriptor level_desc = {
            .format = BOX_HIZ_FORMAT,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = level,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };
        r->v->hiz_mips[slot][level] = wgpuTextureCreateView(r->v->hiz[slot], &level_desc);
    }

    r->v->hiz_source[slot][0] = box_hiz_source_group(r, r->hiz_depth_layout,
        r->v->depth_sample);
    for (level = 1; level < levels; level ++) {
        r->v->hiz_source[slot][level] = box_hiz_source_group(r, r->hiz_level_layout,
            r->v->hiz_mips[slot][level - 1]);
    }
}

void box_hiz_prepare(
    box_renderer_t *r,
    const box_view_t *view)
{
    r->v->hiz_read_want = view->lod_enabled != 0;
    uint32_t levels = box_hiz_levels(box_hiz_half(r->v->width), box_hiz_half(r->v->height),
        view->hiz_levels);
    if (r->v->hiz[0] && r->v->hiz_levels == levels) {
        goto done;
    }

    box_hiz_resize(r->v);
    r->v->hiz_levels = levels;
    box_hiz_create(r, 0, levels);
    box_hiz_create(r, 1, levels);
done:
    r->v->hiz_slot = 1 - r->v->hiz_slot;
}

/** Entry of the occlusion resources of one mesh in the current view.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @return The entry
 */
static box_hiz_mesh_t* box_hiz_entry(
    box_renderer_t *r,
    uint32_t mesh)
{
    while (r->v->hiz_meshes.count <= mesh) {
        box_hiz_mesh_t *created = box_vec_push(&r->v->hiz_meshes);
        memset(created, 0, sizeof(box_hiz_mesh_t));
    }

    return box_vec_at(&r->v->hiz_meshes, mesh);
}

void box_hiz_job(
    box_renderer_t *r,
    uint32_t mesh,
    uint32_t first,
    uint32_t count,
    uint32_t draw,
    uint32_t pass)
{
    box_cull_job_t *job = box_vec_push(&r->hiz_jobs);
    job->mesh = mesh;
    job->first = first;
    job->count = count;
    job->slot = r->hiz_slots ++;
    job->draw = draw;
    job->pass = pass;
    r->v->hiz_active = true;
    r->hiz_used = true;
}

void box_hiz_upload(
    box_renderer_t *r,
    const box_view_t *view,
    const box_frustum_t *f)
{
    float clip[16];
    box_frustum_mul(view->proj, view->view, clip);

    uint32_t i;
    for (i = 0; i < r->hiz_jobs.count; i ++) {
        box_cull_job_t *job = box_vec_at(&r->hiz_jobs, i);
        box_cull_draw_t *draw = box_vec_at(&r->v->draws, job->draw);
        box_hiz_uniform_t uniform;
        memcpy(uniform.clip, clip, sizeof(clip));
        memcpy(uniform.prev, r->v->hiz_clip, sizeof(uniform.prev));
        memcpy(uniform.planes, f->planes, sizeof(uniform.planes));
        uniform.range[0] = job->first;
        uniform.range[1] = job->count;
        uniform.range[2] = draw->first;
        uniform.range[3] = draw->args / (BOX_CULL_ARG_WORDS * (uint32_t)sizeof(uint32_t));
        uniform.screen[0] = box_hiz_half(r->v->width);
        uniform.screen[1] = box_hiz_half(r->v->height);
        uniform.screen[2] = r->v->hiz_levels;
        uniform.screen[3] = 0;
        wgpuQueueWriteBuffer(r->gpu_queue, r->hiz_uniform,
            (uint64_t)job->slot * BOX_HIZ_UNIFORM_STRIDE, &uniform, sizeof(uniform));
    }

    memcpy(r->v->hiz_clip, clip, sizeof(clip));
}

/** Record one pass of a pyramid, which fills one of its levels.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param pipeline The pipeline of the pass
 * @param level The level the pass writes
 */
static void box_hiz_level_pass(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    uint32_t level)
{
    uint32_t slot = r->v->hiz_slot;
    WGPURenderPassColorAttachment color = {
        .view = r->v->hiz_mips[slot][level],
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "depth");
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, r->v->hiz_source[slot][level], 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Level of the pyramid of the current view that fits the grid of the traversal.
 *
 * @param r The renderer
 * @return The level, the last one when no level is small enough
 */
static uint32_t box_hiz_read_level(
    const box_renderer_t *r)
{
    uint32_t width = box_hiz_half(r->v->width);
    uint32_t height = box_hiz_half(r->v->height);
    uint32_t result = 0;
    while (result + 1 < r->v->hiz_levels) {
        if (box_hiz_extent(width, result) <= BOX_LOD_DEPTH_MAX &&
            box_hiz_extent(height, result) <= BOX_LOD_DEPTH_MAX)
        {
            break;
        }

        result ++;
    }

    return result;
}

#ifndef BOX_PLATFORM_WEB
/** Record the copy of one level of the pyramid into the readback buffer.
 *
 * @param r The renderer
 * @param encoder The command encoder
 */
static void box_hiz_capture(
    box_renderer_t *r,
    WGPUCommandEncoder encoder)
{
    if (r->v->hiz_read_busy || !r->v->hiz_read_want) {
        goto done;
    }

    uint32_t level = box_hiz_read_level(r);
    uint32_t w = box_hiz_extent(box_hiz_half(r->v->width), level);
    uint32_t h = box_hiz_extent(box_hiz_half(r->v->height), level);
    uint32_t row = ((w * 4u) + 255u) & ~255u;
    uint32_t size = row * h;

    if (w > BOX_LOD_DEPTH_MAX || h > BOX_LOD_DEPTH_MAX) {
        goto done;
    }

    if (size > r->v->hiz_read_size) {
        if (r->v->hiz_read) wgpuBufferRelease(r->v->hiz_read);
        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, .size = size
        };

        r->v->hiz_read = wgpuDeviceCreateBuffer(r->device, &desc);
        r->v->hiz_read_size = size;
    }

    WGPUTexelCopyTextureInfo src = {
        .texture = r->v->hiz[r->v->hiz_slot], .mipLevel = level
    };
    WGPUTexelCopyBufferInfo dst = {
        .buffer = r->v->hiz_read,
        .layout = { .bytesPerRow = row, .rowsPerImage = h }
    };
    WGPUExtent3D extent = { .width = w, .height = h, .depthOrArrayLayers = 1 };

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    memcpy(r->v->hiz_read_clip, r->v->hiz_clip, sizeof(r->v->hiz_read_clip));
    r->v->hiz_read_level = level;
    r->v->hiz_read_row = row;
    r->v->hiz_depth.width = w;
    r->v->hiz_depth.height = h;
    r->v->hiz_read_busy = true;
    r->v->hiz_read_ready = true;
done:
    return;
}

/** Copy the mapped readback of a view into the grid the packet carries.
 *
 * @param status Map outcome
 * @param message Error text
 * @param userdata1 The view
 * @param userdata2 Unused
 */
static void box_hiz_mapped(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)message;
    (void)userdata2;
    box_gpu_view_t *v = userdata1;
    if (status != WGPUMapAsyncStatus_Success) {
        goto done;
    }

    const uint8_t *bytes = wgpuBufferGetConstMappedRange(v->hiz_read, 0, v->hiz_read_size);
    uint32_t y;
    for (y = 0; bytes && y < v->hiz_depth.height; y ++) {
        memcpy(&v->hiz_depth.values[y * v->hiz_depth.width],
            bytes + (size_t)y * v->hiz_read_row, v->hiz_depth.width * sizeof(float));
    }

    if (bytes) {
        memcpy(v->hiz_depth.clip, v->hiz_read_clip, sizeof(v->hiz_depth.clip));
        v->hiz_depth.valid = 1;
    }

    wgpuBufferUnmap(v->hiz_read);
done:
    v->hiz_read_busy = false;
}
#endif

void box_hiz_publish(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    uint32_t i;
    for (i = 0; i < r->views.count; i ++) {
        box_gpu_view_t *v = box_vec_at(&r->views, i);
        if (!v->hiz_read_ready) {
            continue;
        }

        WGPUBufferMapCallbackInfo info = {
            .mode = WGPUCallbackMode_AllowProcessEvents,
            .callback = box_hiz_mapped,
            .userdata1 = v
        };

        v->hiz_read_ready = false;
        wgpuBufferMapAsync(v->hiz_read, WGPUMapMode_Read, 0, v->hiz_read_size, info);
    }
#else
    (void)r;
#endif
}

void box_hiz_depth(
    box_renderer_t *r,
    box_packet_t *p)
{
    memset(&p->lod_depth, 0, sizeof(box_lod_depth_t));
    if (!r->views.count) {
        goto done;
    }

    const box_gpu_view_t *v = box_vec_at(&r->views, 0);
    if (v->hiz_depth.valid) {
        p->lod_depth = v->hiz_depth;
    }

done:
    return;
}

void box_hiz_build(
    box_renderer_t *r,
    WGPUCommandEncoder encoder)
{
    box_hiz_level_pass(r, encoder, r->hiz_copy, 0);

    uint32_t level;
    for (level = 1; level < r->v->hiz_levels; level ++) {
        box_hiz_level_pass(r, encoder, r->hiz_reduce, level);
    }

#ifndef BOX_PLATFORM_WEB
    box_hiz_capture(r, encoder);
#endif
}

/** Build or reuse the bind group of the occlusion dispatches of one mesh.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @return The bind group
 */
static WGPUBindGroup box_hiz_group(
    box_renderer_t *r,
    uint32_t mesh)
{
    box_gpu_mesh_t *m = box_vec_at(&r->meshes, mesh);
    box_hiz_mesh_t *entry = box_hiz_entry(r, mesh);
    WGPUTextureView pyramid = r->v->hiz_view[r->v->hiz_slot];
    WGPUTextureView history = r->v->hiz_view[1 - r->v->hiz_slot];
    if (entry->group && entry->src == m->statics && entry->dst == r->v->visible &&
        entry->args == r->cull_args && entry->pyramid == pyramid &&
        entry->history == history)
    {
        goto done;
    }

    if (entry->group) wgpuBindGroupRelease(entry->group);
    WGPUBindGroupEntry entries[7] = {
        { .binding = 0, .buffer = r->hiz_uniform, .size = sizeof(box_hiz_uniform_t) },
        { .binding = 1, .buffer = m->statics, .size = WGPU_WHOLE_SIZE },
        { .binding = 2, .buffer = r->v->visible, .size = WGPU_WHOLE_SIZE },
        { .binding = 3, .buffer = r->cull_args, .size = WGPU_WHOLE_SIZE },
        { .binding = 4, .buffer = r->hiz_stats, .size = WGPU_WHOLE_SIZE },
        { .binding = 5, .textureView = pyramid },
        { .binding = 6, .textureView = history }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->hiz_layout,
        .entryCount = 7,
        .entries = entries
    };
    entry->group = wgpuDeviceCreateBindGroup(r->device, &desc);
    entry->src = m->statics;
    entry->dst = r->v->visible;
    entry->args = r->cull_args;
    entry->pyramid = pyramid;
    entry->history = history;
done:
    return entry->group;
}

void box_hiz_dispatch(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    uint32_t pass)
{
    if (!r->hiz_jobs.count) {
        goto done;
    }

    WGPUComputePassEncoder encoded = box_timing_compute(r, encoder, "occlusion");
    wgpuComputePassEncoderSetPipeline(encoded,
        pass == BOX_HIZ_PASS_NEW ? r->hiz_second : r->hiz_first);
    uint32_t i;
    for (i = 0; i < r->hiz_jobs.count; i ++) {
        box_cull_job_t *job = box_vec_at(&r->hiz_jobs, i);
        if (job->pass != pass) {
            continue;
        }

        uint32_t offset = job->slot * BOX_HIZ_UNIFORM_STRIDE;
        wgpuComputePassEncoderSetBindGroup(encoded, 0, box_hiz_group(r, job->mesh), 1, &offset);
        wgpuComputePassEncoderDispatchWorkgroups(encoded,
            (job->count + BOX_HIZ_GROUP - 1) / BOX_HIZ_GROUP, 1, 1);
    }

    wgpuComputePassEncoderEnd(encoded);
    wgpuComputePassEncoderRelease(encoded);
done:
    return;
}

void box_hiz_view_fini(
    box_gpu_view_t *v)
{
    uint32_t i;
    for (i = 0; i < v->hiz_meshes.count; i ++) {
        box_hiz_mesh_t *entry = box_vec_at(&v->hiz_meshes, i);
        if (entry->group) wgpuBindGroupRelease(entry->group);
    }

    if (v->hiz_read) {
        wgpuBufferRelease(v->hiz_read);
        v->hiz_read = NULL;
        v->hiz_read_size = 0;
    }

    box_vec_fini(&v->hiz_meshes);
    box_hiz_resize(v);
}

void box_hiz_fini(
    box_renderer_t *r)
{
    if (r->hiz_stats) wgpuBufferRelease(r->hiz_stats);
    if (r->hiz_uniform) wgpuBufferRelease(r->hiz_uniform);
    if (r->hiz_first) wgpuComputePipelineRelease(r->hiz_first);
    if (r->hiz_second) wgpuComputePipelineRelease(r->hiz_second);
    if (r->hiz_layout) wgpuBindGroupLayoutRelease(r->hiz_layout);
    if (r->hiz_copy) wgpuRenderPipelineRelease(r->hiz_copy);
    if (r->hiz_reduce) wgpuRenderPipelineRelease(r->hiz_reduce);
    if (r->hiz_depth_layout) wgpuBindGroupLayoutRelease(r->hiz_depth_layout);
    if (r->hiz_level_layout) wgpuBindGroupLayoutRelease(r->hiz_level_layout);
    box_vec_fini(&r->hiz_jobs);
    r->hiz_stats = NULL;
    r->hiz_uniform = NULL;
    r->hiz_uniform_capacity = 0;
    r->hiz_first = NULL;
    r->hiz_second = NULL;
    r->hiz_layout = NULL;
    r->hiz_copy = NULL;
    r->hiz_reduce = NULL;
    r->hiz_depth_layout = NULL;
    r->hiz_level_layout = NULL;
}
