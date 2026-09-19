/**
 * @file renderer/overlay.c
 * @brief Frame time ring of the renderer and the pass that draws the FPS panel.
 *
 * The ring is fed with the interval between the packets the renderer processes,
 * so the bars show the pacing the frame actually had. A panel is a handful of
 * rectangles in the pixels of its view, which one instanced draw turns into the
 * background, the two reference lines and the bars. The frame rate itself is
 * published as regular UI text by box.monitor, not drawn here. The pass runs
 * after the effect chain and before the capture, so it appears in a screenshot
 * the same way it appears on screen.
 */

#include "renderer_internal.h"

/** Color of every frame time class, display encoded */
static const float box_fps_colors[3][3] = {
    { 0.29f, 0.84f, 0.38f },
    { 0.95f, 0.78f, 0.25f },
    { 0.91f, 0.28f, 0.26f }
};

/** Color the panel background is filled with, before its opacity */
static const float box_overlay_back[3] = { 0.04f, 0.04f, 0.05f };

/** Color of the sixty and thirty frames per second reference lines */
static const float box_overlay_line[4] = { 0.55f, 0.55f, 0.60f, 0.55f };

void box_fps_ring_push(
    box_fps_ring_t *ring,
    float time_ms)
{
    if (time_ms < 0.0f) {
        goto done;
    }

    ring->times_ms[ring->next] = time_ms;
    ring->next = (ring->next + 1) % BOX_FPS_HISTORY;
    if (ring->count < BOX_FPS_HISTORY) {
        ring->count ++;
    }

done:
    return;
}

float box_fps_ring_get(
    const box_fps_ring_t *ring,
    int32_t back)
{
    float result = 0.0f;
    if (back >= 0 && back < ring->count) {
        int32_t slot = ring->next - 1 - back;
        while (slot < 0) {
            slot += BOX_FPS_HISTORY;
        }

        result = ring->times_ms[slot];
    }

    return result;
}

int32_t box_fps_ring_window(
    const box_fps_ring_t *ring,
    float window_ms)
{
    int32_t result = 0;
    float total = 0.0f;
    while (result < ring->count && total < window_ms) {
        total += box_fps_ring_get(ring, result);
        result ++;
    }

    return result;
}

void box_fps_ring_stats(
    const box_fps_ring_t *ring,
    float window_ms,
    box_fps_stats_t *out)
{
    int32_t frames = box_fps_ring_window(ring, window_ms);
    float total = 0.0f;
    float worst = 0.0f;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        float time_ms = box_fps_ring_get(ring, i);
        total += time_ms;
        if (time_ms > worst) {
            worst = time_ms;
        }
    }

    out->frames = frames;
    out->mean_ms = frames ? total / (float)frames : 0.0f;
    out->worst_ms = worst;
    out->fps = total > 0.0f ? (float)frames * 1000.0f / total : 0.0f;
}

int32_t box_fps_class(
    float time_ms)
{
    int32_t result = BOX_FPS_RED;
    if (time_ms <= BOX_FPS_GREEN_MS + BOX_FPS_EPSILON_MS) {
        result = BOX_FPS_GREEN;
    } else if (time_ms <= BOX_FPS_YELLOW_MS + BOX_FPS_EPSILON_MS) {
        result = BOX_FPS_YELLOW;
    }

    return result;
}

void box_fps_class_color(
    int32_t cls,
    float *out)
{
    int32_t slot = cls;
    if (slot < BOX_FPS_GREEN || slot > BOX_FPS_RED) {
        slot = BOX_FPS_RED;
    }

    out[0] = box_fps_colors[slot][0];
    out[1] = box_fps_colors[slot][1];
    out[2] = box_fps_colors[slot][2];
}

/** Append one rectangle of a panel when the output still has room.
 *
 * @param out The output array
 * @param count Rectangles already written
 * @param max Rectangles the output holds
 * @param rect Left, top, width and height in pixels
 * @param color Display encoded RGBA the rectangle is filled with
 * @return The number of rectangles after the append
 */
static int32_t box_overlay_push(
    box_overlay_rect_t *out,
    int32_t count,
    int32_t max,
    const float *rect,
    const float *color)
{
    if (count >= max) {
        goto done;
    }

    memcpy(out[count].rect, rect, 4 * sizeof(float));
    memcpy(out[count].color, color, 4 * sizeof(float));
    count ++;
done:
    return count;
}

/** Append the background of a panel and the two reference lines of its chart.
 *
 * @param layout Size and corner of the panel
 * @param panel Left, top, width and height of the panel in pixels
 * @param chart Left, top, width and height of the chart in pixels
 * @param out The output array
 * @param max Rectangles the output holds
 * @return The number of rectangles written
 */
static int32_t box_overlay_panel(
    const box_overlay_layout_t *layout,
    const float *panel,
    const float *chart,
    box_overlay_rect_t *out,
    int32_t max)
{
    float back[4] = { box_overlay_back[0], box_overlay_back[1], box_overlay_back[2],
        layout->opacity };
    int32_t count = box_overlay_push(out, 0, max, panel, back);
    float thickness = chart[3] / BOX_OVERLAY_CHART_H;
    float bottom = chart[1] + chart[3];
    float green = bottom - chart[3] * (BOX_FPS_GREEN_MS / BOX_FPS_BAR_MS);
    float yellow = bottom - chart[3] * (BOX_FPS_YELLOW_MS / BOX_FPS_BAR_MS);
    float green_rect[4] = { chart[0], green, chart[2], thickness };
    float yellow_rect[4] = { chart[0], yellow, chart[2], thickness };
    count = box_overlay_push(out, count, max, green_rect, box_overlay_line);
    count = box_overlay_push(out, count, max, yellow_rect, box_overlay_line);

    return count;
}

/** Append one bar per frame of the window, the newest frame on the right.
 *
 * @param ring The ring the frame times come from
 * @param chart Left, top, width and height of the chart in pixels
 * @param out The output array
 * @param count Rectangles already written
 * @param max Rectangles the output holds
 * @return The number of rectangles after the bars
 */
static int32_t box_overlay_bars(
    const box_fps_ring_t *ring,
    const float *chart,
    box_overlay_rect_t *out,
    int32_t count,
    int32_t max)
{
    int32_t frames = box_fps_ring_window(ring, BOX_FPS_WINDOW_MS);
    if (frames > BOX_OVERLAY_BARS) {
        frames = BOX_OVERLAY_BARS;
    }

    float width = chart[2] / (float)BOX_OVERLAY_BARS;
    float bottom = chart[1] + chart[3];
    int32_t i;
    for (i = 0; i < frames; i ++) {
        float time_ms = box_fps_ring_get(ring, i);
        float fraction = time_ms / BOX_FPS_BAR_MS;
        if (fraction > 1.0f) {
            fraction = 1.0f;
        }

        float height = fraction * chart[3];
        if (height < width) {
            height = width;
        }

        float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        box_fps_class_color(box_fps_class(time_ms), color);
        float rect[4] = { chart[0] + chart[2] - (float)(i + 1) * width, bottom - height,
            width, height };
        count = box_overlay_push(out, count, max, rect, color);
    }

    return count;
}

int32_t box_overlay_build(
    const box_fps_ring_t *ring,
    const box_overlay_layout_t *layout,
    box_overlay_rect_t *out,
    int32_t max)
{
    float scale = layout->scale > 0.0f ? layout->scale : 1.0f;
    float pad = BOX_OVERLAY_PAD * scale;
    float margin = BOX_OVERLAY_MARGIN * scale;
    float panel[4] = { margin, margin, BOX_OVERLAY_WIDTH * scale,
        BOX_OVERLAY_HEIGHT * scale };
    if (layout->corner == BOX_OVERLAY_TOP_RIGHT ||
        layout->corner == BOX_OVERLAY_BOTTOM_RIGHT)
    {
        panel[0] = layout->width - panel[2] - margin;
    }

    if (layout->corner == BOX_OVERLAY_BOTTOM_LEFT ||
        layout->corner == BOX_OVERLAY_BOTTOM_RIGHT)
    {
        panel[1] = layout->height - panel[3] - margin;
    }

    float chart[4] = { panel[0] + pad,
        panel[1] + pad + BOX_OVERLAY_LABEL_H * scale + pad,
        BOX_OVERLAY_BARS * BOX_OVERLAY_BAR_W * scale, BOX_OVERLAY_CHART_H * scale };
    int32_t count = box_overlay_panel(layout, panel, chart, out, max);
    count = box_overlay_bars(ring, chart, out, count, max);

    return count;
}

/** Buffers and bind group of one view, created the first time it draws a panel.
 *
 * @param r The renderer
 * @param index Index of the view in the packet
 * @return The entry, whose group is NULL when the device refused a buffer
 */
static box_gpu_overlay_t* box_overlay_entry(
    box_renderer_t *r,
    uint32_t index)
{
    while (r->overlays.count <= index) {
        box_gpu_overlay_t *created = box_vec_push(&r->overlays);
        memset(created, 0, sizeof(box_gpu_overlay_t));
    }

    box_gpu_overlay_t *result = box_vec_at(&r->overlays, index);
    if (result->group) {
        goto done;
    }

    WGPUBufferDescriptor uniform_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(box_overlay_uniform_t)
    };
    result->uniform = wgpuDeviceCreateBuffer(r->device, &uniform_desc);
    WGPUBufferDescriptor rect_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = BOX_OVERLAY_RECT_MAX * sizeof(box_overlay_rect_t)
    };
    result->rects = wgpuDeviceCreateBuffer(r->device, &rect_desc);
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .buffer = result->uniform, .size = sizeof(box_overlay_uniform_t) },
        { .binding = 1, .buffer = result->rects,
            .size = BOX_OVERLAY_RECT_MAX * sizeof(box_overlay_rect_t) }
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->overlay_layout, .entryCount = 2, .entries = entries };
    result->group = wgpuDeviceCreateBindGroup(r->device, &group_desc);
done:
    return result;
}

void box_overlay_init(
    box_renderer_t *r)
{
    if (!r->overlays.size) {
        box_vec_init(&r->overlays, sizeof(box_gpu_overlay_t));
    }

    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_overlay_uniform_t)
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(box_overlay_rect_t)
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 2, .entries = entries };
    r->overlay_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);
    WGPUPipelineLayoutDescriptor pipeline_desc = {
        .bindGroupLayoutCount = 1, .bindGroupLayouts = &r->overlay_layout };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_desc);
    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_OVERLAY);
    if (module) {
        WGPUBlendState blend = {
            .color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
                WGPUBlendFactor_OneMinusSrcAlpha },
            .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
                WGPUBlendFactor_OneMinusSrcAlpha }
        };
        WGPUColorTargetState target = {
            .format = r->format, .blend = &blend, .writeMask = WGPUColorWriteMask_All };
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
            .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
            .multisample = { .count = 1, .mask = 0xFFFFFFFF }
        };
        r->overlay_pipeline = wgpuDeviceCreateRenderPipeline(r->device, &desc);
        wgpuShaderModuleRelease(module);
    }

    wgpuPipelineLayoutRelease(pipeline_layout);
}

void box_overlay_frame(
    box_renderer_t *r,
    const box_packet_t *p)
{
    float interval = p->delta * 1000.0f;
    if (r->overlay_timed) {
        interval = (float)((p->time - r->overlay_time) * 1000.0);
    }

    r->overlay_time = p->time;
    r->overlay_timed = true;
    box_fps_ring_push(&r->overlay_ring, interval);
}

void box_overlay_record(
    box_renderer_t *r,
    const box_view_t *view,
    uint32_t index,
    WGPUCommandEncoder encoder,
    WGPUTexture image)
{
    if (!view->overlay || !r->overlay_pipeline || !image) {
        goto done;
    }

    box_overlay_layout_t layout = {
        .width = (float)view->width,
        .height = (float)view->height,
        .scale = view->overlay_scale,
        .opacity = view->overlay_opacity,
        .corner = (int32_t)view->overlay_corner
    };
    box_overlay_rect_t rects[BOX_OVERLAY_RECT_MAX];
    int32_t count = box_overlay_build(&r->overlay_ring, &layout, rects, BOX_OVERLAY_RECT_MAX);
    if (count <= 0) {
        goto done;
    }

    box_gpu_overlay_t *entry = box_overlay_entry(r, index);
    if (!entry->group) {
        goto done;
    }

    box_overlay_uniform_t uniform;
    memset(&uniform, 0, sizeof(uniform));
    uniform.size[0] = layout.width;
    uniform.size[1] = layout.height;
    uniform.size[2] = layout.width > 0.0f ? 1.0f / layout.width : 0.0f;
    uniform.size[3] = layout.height > 0.0f ? 1.0f / layout.height : 0.0f;
    uniform.flags[0] = box_renderer_srgb(r) ? 1.0f : 0.0f;
    wgpuQueueWriteBuffer(r->gpu_queue, entry->uniform, 0, &uniform, sizeof(uniform));
    wgpuQueueWriteBuffer(r->gpu_queue, entry->rects, 0, rects,
        (size_t)count * sizeof(box_overlay_rect_t));

    WGPUTextureView attachment = wgpuTextureCreateView(image, NULL);
    WGPURenderPassColorAttachment color = {
        .view = attachment,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &pass_desc, "overlay");
    wgpuRenderPassEncoderSetPipeline(pass, r->overlay_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, entry->group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 6, (uint32_t)count, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuTextureViewRelease(attachment);
done:
    return;
}

void box_overlay_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < r->overlays.count; i ++) {
        box_gpu_overlay_t *entry = box_vec_at(&r->overlays, i);
        if (entry->group) wgpuBindGroupRelease(entry->group);
        if (entry->rects) wgpuBufferRelease(entry->rects);
        if (entry->uniform) wgpuBufferRelease(entry->uniform);
    }

    box_vec_fini(&r->overlays);
    if (r->overlay_pipeline) wgpuRenderPipelineRelease(r->overlay_pipeline);
    if (r->overlay_layout) wgpuBindGroupLayoutRelease(r->overlay_layout);
    r->overlay_pipeline = NULL;
    r->overlay_layout = NULL;
}
