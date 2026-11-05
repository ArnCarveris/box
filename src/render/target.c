/**
 * @file render/target.c
 * @brief Window and offscreen targets of the webgpu backend: configuration, views and present.
 *
 * Views render into the scene texture at the render size, which the blit scales to the
 * presented image; at a resolution scale of one the views render into the image directly.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

WGPURenderPipeline box_gpu_blit_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    WGPURenderPipeline result = NULL;
    WGPUShaderModule shader = box_gpu_shader_get(gpu, BOX_RENDER_SHADER_BLIT);
    int32_t i;
    for (i = 0; i < gpu->blit_count; i ++) {
        if (gpu->blits[i].format == format) {
            result = gpu->blits[i].pipeline;
            goto done;
        }
    }

    if (!shader) {
        goto done;
    }

    if (gpu->blit_count == BOX_GPU_BLIT_FORMATS) {
        ecs_err("too many target formats for the blit pipeline");
        goto done;
    }

    WGPUColorTargetState target = { .format = format, .writeMask = WGPUColorWriteMask_All };
    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = gpu->blit_pipeline_layout,
        .vertex = {
            .module = shader,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };
    result = box_gpu_render_pipeline(gpu, &desc);
    gpu->blits[gpu->blit_count].format = format;
    gpu->blits[gpu->blit_count].pipeline = result;
    gpu->blit_count ++;
done:
    return result;
}

/** Create a 2D color texture.
 *
 * @param gpu The backend
 * @param width Width in pixels
 * @param height Height in pixels
 * @param format Color format
 * @param usage Usage of the texture
 * @return The texture
 */
static WGPUTexture box_gpu_texture(
    box_gpu_t *gpu,
    int32_t width,
    int32_t height,
    WGPUTextureFormat format,
    WGPUTextureUsage usage)
{
    WGPUTextureDescriptor desc = {
        .usage = usage,
        .dimension = WGPUTextureDimension_2D,
        .size = { (uint32_t)width, (uint32_t)height, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    return wgpuDeviceCreateTexture(gpu->device, &desc);
}

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>

/** Selector of the canvas element of the shell page */
#define BOX_GPU_CANVAS "#canvas"

/** Check whether the page has the canvas, which a run under node does not.
 *
 * @return Whether the element exists
 */
EM_JS(bool, box_gpu_canvas_exists, (void), {
    return typeof document !== 'undefined' && document.querySelector('#canvas') !== null;
});

/** Get the format without sRGB encoding that a canvas is configured with.
 *
 * @param format The sRGB format the views render into
 * @return The canvas format
 */
static WGPUTextureFormat box_gpu_canvas_format(
    WGPUTextureFormat format)
{
    WGPUTextureFormat result = format;
    if (format == WGPUTextureFormat_BGRA8UnormSrgb) {
        result = WGPUTextureFormat_BGRA8Unorm;
    } else if (format == WGPUTextureFormat_RGBA8UnormSrgb) {
        result = WGPUTextureFormat_RGBA8Unorm;
    }

    return result;
}

/** Get the sRGB variant of a canvas format, which the views and the blit render into.
 *
 * @param format The canvas format
 * @return The sRGB format, the format itself when it has none
 */
static WGPUTextureFormat box_gpu_canvas_srgb(
    WGPUTextureFormat format)
{
    WGPUTextureFormat result = format;
    if (format == WGPUTextureFormat_BGRA8Unorm) {
        result = WGPUTextureFormat_BGRA8UnormSrgb;
    } else if (format == WGPUTextureFormat_RGBA8Unorm) {
        result = WGPUTextureFormat_RGBA8UnormSrgb;
    }

    return result;
}

/** Create the surface of the page canvas and pick its format.
 *
 * Canvases only take formats without sRGB encoding, so the canvas is configured with the
 * preferred format and the views render into an sRGB view of its image, which is also a copy
 * source for the opaque snapshot of transmission and captures.
 *
 * A canvas takes copy source usage with any of its formats, and emdawnwebgpu reports no
 * usages in its capabilities, so the usage is not taken from them.
 *
 * @param gpu The backend
 * @param t The target
 */
static void box_gpu_canvas_surface(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source = {
        .chain = { .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
        .selector = { .data = BOX_GPU_CANVAS, .length = WGPU_STRLEN }
    };
    WGPUSurfaceDescriptor desc = { .nextInChain = &source.chain };
    WGPUSurfaceCapabilities caps = {0};
    t->surface = wgpuInstanceCreateSurface(gpu->instance, &desc);
    t->format = WGPUTextureFormat_BGRA8UnormSrgb;
    t->usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    if (wgpuSurfaceGetCapabilities(t->surface, NULL, &caps) == WGPUStatus_Success) {
        if (caps.formatCount) {
            t->format = box_gpu_canvas_srgb(caps.formats[0]);
        }

        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }

    gpu->canvas = t->handle;
}

/** Size the canvas to the target and configure it with the sRGB view of its format.
 *
 * @param t The target
 * @param config The configuration, changed in place
 */
static void box_gpu_canvas_configure(
    box_gpu_target_t *t,
    WGPUSurfaceConfiguration *config)
{
    emscripten_set_canvas_element_size(BOX_GPU_CANVAS, t->desc.width, t->desc.height);
    config->format = box_gpu_canvas_format(t->format);
    config->viewFormatCount = 1;
    config->viewFormats = &t->format;
    config->alphaMode = WGPUCompositeAlphaMode_Opaque;
    config->presentMode = WGPUPresentMode_Fifo;
}

/** Create the view a frame renders into, the sRGB view of a canvas image.
 *
 * @param t The target
 * @param texture The image
 * @return The view
 */
static WGPUTextureView box_gpu_target_view(
    const box_gpu_target_t *t,
    WGPUTexture texture)
{
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = t->format;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.mipLevelCount = 1;
    desc.arrayLayerCount = 1;

    return wgpuTextureCreateView(texture, t->surface ? &desc : NULL);
}

/** Whether the browser build warned that it does not capture */
static bool box_gpu_capture_warned = false;

/** Check whether a frame records a capture, which the browser build never does: its readback
 * would have to wait for the event loop inside the frame.
 *
 * @param frame The target of the packet
 * @return false, after warning once when the frame asked for a capture
 */
static bool box_gpu_target_captures(
    const box_render_target_t *frame)
{
    if (frame->capture_size && !box_gpu_capture_warned) {
        ecs_warn("captures are not available in the browser");
        box_gpu_capture_warned = true;
    }

    return false;
}

#else

/** Check whether a frame records a capture.
 *
 * @param frame The target of the packet
 * @return Whether the frame has a capture path
 */
static bool box_gpu_target_captures(
    const box_render_target_t *frame)
{
    return frame->capture_size != 0;
}

/** Create the view a frame renders into.
 *
 * @param t The target
 * @param texture The image
 * @return The view
 */
static WGPUTextureView box_gpu_target_view(
    const box_gpu_target_t *t,
    WGPUTexture texture)
{
    (void)t;

    return wgpuTextureCreateView(texture, NULL);
}

#endif

/** Release the textures of a target, keeping its window surface. The cached material bind
 * groups of a target that materials sample are released with them.
 *
 * @param gpu The backend
 * @param t The target
 */
static void box_gpu_target_release(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    if (t->output_view) {
        box_gpu_materials_clear(gpu);
    }

    if (t->blit_group) {
        wgpuBindGroupRelease(t->blit_group);
    }

    if (t->scene_view) {
        wgpuTextureViewRelease(t->scene_view);
    }

    if (t->scene) {
        wgpuTextureRelease(t->scene);
    }

    if (t->depth_view) {
        wgpuTextureViewRelease(t->depth_view);
    }

    if (t->depth) {
        wgpuTextureRelease(t->depth);
    }

    if (t->output_view) {
        wgpuTextureViewRelease(t->output_view);
    }

    if (t->output) {
        wgpuTextureRelease(t->output);
    }

    t->output_view = NULL;
    t->blit_group = NULL;
    t->scene_view = NULL;
    t->scene = NULL;
    t->output = NULL;
    t->depth_view = NULL;
    t->depth = NULL;
}

/** Create the scene texture of a target whose render size differs from its image.
 *
 * @param gpu The backend
 * @param t The target
 */
static void box_gpu_target_scene(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment |
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
    t->scene = box_gpu_texture(
        gpu, t->desc.render_width, t->desc.render_height, t->format, usage);
    t->scene_view = wgpuTextureCreateView(t->scene, NULL);
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .textureView = t->scene_view },
        { .binding = 1, .sampler = gpu->sampler }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->blit_layout,
        .entryCount = 2,
        .entries = entries
    };
    t->blit_group = wgpuDeviceCreateBindGroup(gpu->device, &desc);
}

/** Make the textures of a target match its description.
 *
 * Its depth can be copied: a view with transmissive draws copies it next to its opaque
 * snapshot (box_gpu_transmission_record).
 *
 * @param gpu The backend
 * @param t The target
 */
static void box_gpu_target_configure(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    box_render_target_desc_t *d = &t->desc;
    box_gpu_target_release(gpu, t);
    d->width = d->width > 0 ? d->width : 1;
    d->height = d->height > 0 ? d->height : 1;
    d->render_width = d->render_width > 0 ? d->render_width : d->width;
    d->render_height = d->render_height > 0 ? d->render_height : d->height;
    d->point_width = d->point_width > 0 ? d->point_width : d->width;
    d->point_height = d->point_height > 0 ? d->point_height : d->height;
    if (t->surface) {
        WGPUSurfaceConfiguration config = {
            .device = gpu->device,
            .format = t->format,
            .usage = t->usage,
            .width = (uint32_t)d->width,
            .height = (uint32_t)d->height,
            .alphaMode = WGPUCompositeAlphaMode_Auto,
            .presentMode = d->vsync || !t->immediate
                ? WGPUPresentMode_Fifo
                : WGPUPresentMode_Immediate
        };
#ifdef __EMSCRIPTEN__
        box_gpu_canvas_configure(t, &config);
#endif
        wgpuSurfaceConfigure(t->surface, &config);
    } else {
        t->output = box_gpu_texture(gpu, d->width, d->height, t->format, t->usage);
        if (d->texture) {
            t->output_view = wgpuTextureCreateView(t->output, NULL);
            box_gpu_materials_clear(gpu);
        }
    }

    if (d->render_width != d->width || d->render_height != d->height) {
        box_gpu_target_scene(gpu, t);
    }

    t->depth = box_gpu_texture(gpu, d->render_width, d->render_height, BOX_GPU_DEPTH_FORMAT,
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
        WGPUTextureUsage_CopySrc);
    t->depth_view = wgpuTextureCreateView(t->depth, NULL);

    box_gpu_count(gpu, BOX_GPU_STAT_CONFIGURED);
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.width = d->width;
    gpu->stats.height = d->height;
    gpu->stats.render_width = d->render_width;
    gpu->stats.render_height = d->render_height;
    ecs_os_mutex_unlock(gpu->lock);
}

#ifndef __EMSCRIPTEN__
/** Create the webgpu surface of a window target and pick its format and usage.
 *
 * @param gpu The backend
 * @param t The target, with the layer of the window in its description
 */
static void box_gpu_target_surface(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    WGPUSurfaceSourceMetalLayer source = {
        .chain = { .sType = WGPUSType_SurfaceSourceMetalLayer },
        .layer = t->desc.layer
    };
    WGPUSurfaceDescriptor desc = { .nextInChain = &source.chain };
    WGPUSurfaceCapabilities caps = {0};
    size_t i;
    t->surface = wgpuInstanceCreateSurface(gpu->instance, &desc);
    t->format = WGPUTextureFormat_BGRA8UnormSrgb;
    t->usage = WGPUTextureUsage_RenderAttachment;
    if (wgpuSurfaceGetCapabilities(t->surface, gpu->adapter, &caps) != WGPUStatus_Success) {
        goto done;
    }

    if (caps.formatCount) {
        t->format = caps.formats[0];
    }

    for (i = 0; i < caps.formatCount; i ++) {
        if (caps.formats[i] == WGPUTextureFormat_BGRA8UnormSrgb) {
            t->format = caps.formats[i];
        }
    }

    for (i = 0; i < caps.presentModeCount; i ++) {
        t->immediate |= caps.presentModes[i] == WGPUPresentMode_Immediate;
    }

    t->usage |= caps.usages & WGPUTextureUsage_CopySrc;
    wgpuSurfaceCapabilitiesFreeMembers(caps);
done:
    return;
}

/** Give a target the surface of its window when it has one.
 *
 * @param gpu The backend
 * @param t The target
 * @return Whether the target presents to a window
 */
static bool box_gpu_target_window(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    bool result = t->desc.layer != NULL;
    if (result) {
        box_gpu_target_surface(gpu, t);
    }

    return result;
}

#else

/** Give the first target the surface of the page canvas, which is the window in the browser.
 *
 * @param gpu The backend
 * @param t The target
 * @return Whether the target presents to the canvas
 */
static bool box_gpu_target_window(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    bool result = !gpu->canvas && box_gpu_canvas_exists();
    if (result) {
        box_gpu_canvas_surface(gpu, t);
    }

    return result;
}

#endif

/** Release a target and everything it owns.
 *
 * @param gpu The backend
 * @param t The target
 */
static void box_gpu_target_free(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    box_gpu_target_release(gpu, t);
    box_gpu_transmission_release(t);
    box_gpu_capture_fini(t);
    if (t->surface) {
        wgpuSurfaceUnconfigure(t->surface);
        wgpuSurfaceRelease(t->surface);
    }

    if (gpu->canvas == t->handle) {
        gpu->canvas = 0;
    }

    ecs_os_free(t);
    box_gpu_count(gpu, BOX_GPU_STAT_DESTROYED);
}

/** Create a target from its description.
 *
 * @param gpu The backend
 * @param handle Handle of the target
 * @param desc Size and kind of the target
 */
static void box_gpu_target_create(
    box_gpu_t *gpu,
    uint32_t handle,
    const box_render_target_desc_t *desc)
{
    box_gpu_target_t *t = ecs_os_calloc_t(box_gpu_target_t);
    t->handle = handle;
    t->desc = *desc;
    if (!box_gpu_target_window(gpu, t)) {
        t->format = BOX_GPU_OFFSCREEN_FORMAT;
        t->usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
            WGPUTextureUsage_TextureBinding;
    }

    ecs_map_insert_ptr(&gpu->targets, handle, t);
    box_gpu_count(gpu, BOX_GPU_STAT_CREATED);
    box_gpu_target_configure(gpu, t);
}

void box_gpu_target_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_target_desc_t *desc)
{
    box_gpu_target_t *t = ecs_map_get_deref(&gpu->targets, box_gpu_target_t, handle);
    bool recreate = op == BOX_RENDER_CREATE || (t && desc && t->desc.layer != desc->layer);
    if (t && (op == BOX_RENDER_DESTROY || recreate)) {
        ecs_map_remove(&gpu->targets, handle);
        box_gpu_target_free(gpu, t);
        t = NULL;
    }

    if (op == BOX_RENDER_DESTROY) {
        goto done;
    }

    if (!t) {
        box_gpu_target_create(gpu, handle, desc);
        goto done;
    }

    t->desc = *desc;
    box_gpu_target_configure(gpu, t);
done:
    return;
}

/** Clear a texture view with a render pass.
 *
 * @param encoder The encoder of the frame
 * @param view The view
 * @param color The linear clear color
 */
static void box_gpu_clear(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    const float *color)
{
    WGPURenderPassColorAttachment attachment = {
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { color[0], color[1], color[2], color[3] }
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Scale the scene texture of a target to its presented image.
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param view The presented image
 */
static void box_gpu_blit(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    WGPUTextureView view)
{
    WGPURenderPipeline pipeline = box_gpu_blit_pipeline(gpu, t->format);
    WGPURenderPassColorAttachment attachment = {
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0, 0, 0, 1 }
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    if (pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, t->blit_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Get the image a target presents this frame.
 *
 * @param gpu The backend
 * @param t The target
 * @return The texture, NULL when the window has no image this frame
 */
static WGPUTexture box_gpu_target_acquire(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    WGPUSurfaceTexture image = {0};
    WGPUTexture result = t->output;
    if (!t->surface) {
        goto done;
    }

    wgpuSurfaceGetCurrentTexture(t->surface, &image);
    if (image.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
        image.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        result = image.texture;
        goto done;
    }

    if (image.texture) {
        wgpuTextureRelease(image.texture);
    }

    if (image.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
        image.status == WGPUSurfaceGetCurrentTextureStatus_Lost)
    {
        box_gpu_target_configure(gpu, t);
    }

done:
    return result;
}

/** Draw the views of a frame into the scene texture of a target and submit the work before the
 * target acquires its image. Acquiring a window image blocks until the display frees one, so
 * the GPU renders the frame while the render thread waits instead of after it.
 *
 * @param gpu The backend
 * @param t The target, with a scene texture
 * @param packet The packet
 * @param frame The target of the packet
 */
static void box_gpu_target_scene_draw(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_target_t *frame)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    WGPUCommandBuffer commands;
    box_gpu_clear(encoder, t->scene_view, frame->clear);
    box_gpu_draw_views(gpu, t, packet, frame, encoder, t->scene_view, t->scene);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

void box_gpu_target_render(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_target_t *frame)
{
    box_gpu_target_t *t = ecs_map_get_deref(&gpu->targets, box_gpu_target_t, frame->handle);
    bool drawn = false;
    gpu->rendering = frame->handle;
    if (t && t->scene_view) {
        box_gpu_target_scene_draw(gpu, t, packet, frame);
        drawn = true;
    }

    WGPUTexture texture = t ? box_gpu_target_acquire(gpu, t) : NULL;
    if (!texture) {
        goto done;
    }

    WGPUTextureView view = box_gpu_target_view(t, texture);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    if (drawn) {
        box_gpu_blit(gpu, t, encoder, view);
    } else {
        box_gpu_clear(encoder, view, frame->clear);
        box_gpu_draw_views(gpu, t, packet, frame, encoder, view, texture);
    }

    if (box_gpu_target_captures(frame)) {
        box_gpu_capture_encode(gpu, t, encoder, texture);
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    if (t->surface) {
#ifndef __EMSCRIPTEN__
        wgpuSurfacePresent(t->surface);
#endif
        wgpuTextureRelease(texture);
    }

    if (box_gpu_target_captures(frame)) {
        box_gpu_capture_write(gpu, t, box_render_packet_data(packet, frame->capture));
    }

    box_gpu_count(gpu, BOX_GPU_STAT_FRAMES);
done:
    gpu->rendering = 0;
}

void box_gpu_target_fini(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->targets);
    while (ecs_map_next(&it)) {
        box_gpu_target_free(gpu, ecs_map_ptr(&it));
    }

    ecs_map_clear(&gpu->targets);
}

#endif
