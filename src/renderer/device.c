/**
 * @file renderer/device.c
 * @brief WebGPU instance, surface, adapter, device and render target creation.
 */

#include "renderer_internal.h"
#include <stdio.h>
#include <string.h>

/** Print a WebGPU string view to stderr with a prefix.
 *
 * @param prefix Text before the message
 * @param message The message
 */
static void box_device_report(
    const char *prefix,
    WGPUStringView message)
{
    if (message.data) {
        int len = message.length == WGPU_STRLEN ? (int)strlen(message.data) : (int)message.length;
        fprintf(stderr, "box: %s: %.*s\n", prefix, len, message.data);
    } else {
        fprintf(stderr, "box: %s\n", prefix);
    }
}

/** Store the adapter from an adapter request.
 *
 * @param status Request outcome
 * @param adapter The adapter on success
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 Unused
 */
static void box_on_adapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)userdata2;
    box_renderer_t *r = userdata1;
    if (status == WGPURequestAdapterStatus_Success) {
        r->adapter = adapter;
    } else {
        box_device_report("adapter request failed", message);
        r->failed = true;
    }
}

/** Store the device from a device request.
 *
 * @param status Request outcome
 * @param device The device on success
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 Unused
 */
static void box_on_device(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)userdata2;
    box_renderer_t *r = userdata1;
    if (status == WGPURequestDeviceStatus_Success) {
        r->device = device;
    } else {
        box_device_report("device request failed", message);
        r->failed = true;
    }
}

/** Report uncaptured device errors.
 *
 * @param device The device
 * @param type Error category
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 Unused
 */
static void box_on_device_error(
    WGPUDevice const *device,
    WGPUErrorType type,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)device; (void)type; (void)userdata2;
    box_renderer_t *r = userdata1;
    if (r) {
        r->device_error = true;
    }

    box_device_report("device error", message);
}

/** Create the window surface from the platform surface source.
 *
 * @param r The renderer
 */
static void box_device_create_surface(
    box_renderer_t *r)
{
#ifdef BOX_PLATFORM_WEB
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source = {
        .chain = { .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
        .selector = { .data = r->desc.canvas_selector, .length = WGPU_STRLEN }
    };
    bool has_source = r->desc.canvas_selector != NULL;
#else
    WGPUSurfaceSourceMetalLayer source = {
        .chain = { .sType = WGPUSType_SurfaceSourceMetalLayer },
        .layer = r->desc.metal_layer
    };
    bool has_source = r->desc.metal_layer != NULL;
#endif
    if (has_source) {
        WGPUSurfaceDescriptor desc = { .nextInChain = (WGPUChainedStruct*)&source };
        r->surface = wgpuInstanceCreateSurface(r->instance, &desc);
    }
}

/** Configure the surface for the current size and format.
 *
 * @param r The renderer
 */
static void box_device_configure_surface(
    box_renderer_t *r)
{
    WGPUSurfaceConfiguration config = {
        .device = r->device,
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
        .format = r->format,
        .width = r->width,
        .height = r->height,
        .presentMode = r->desc.vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate,
        .alphaMode = WGPUCompositeAlphaMode_Auto
    };
    wgpuSurfaceConfigure(r->surface, &config);
}

/** Pick the color format: the surface's preferred format or BGRA8 offscreen.
 *
 * @param r The renderer
 */
static void box_device_select_format(
    box_renderer_t *r)
{
    r->format = WGPUTextureFormat_BGRA8Unorm;
    WGPUSurfaceCapabilities caps = {0};
    if (r->surface &&
        wgpuSurfaceGetCapabilities(r->surface, r->adapter, &caps) == WGPUStatus_Success &&
        caps.formatCount)
    {
        r->format = caps.formats[0];
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }
}

/** Finish initialization once the device exists: targets and surface config.
 *
 * @param r The renderer
 */
static void box_device_complete(
    box_renderer_t *r)
{
    r->gpu_queue = wgpuDeviceGetQueue(r->device);
    box_device_select_format(r);
    box_device_resize(r, r->desc.width, r->desc.height);
}

#ifdef BOX_PLATFORM_WEB
/** Device callback on web: complete initialization and mark the renderer ready.
 *
 * @param status Request outcome
 * @param device The device on success
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 Unused
 */
static void box_on_device_web(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_on_device(status, device, message, userdata1, userdata2);
    box_renderer_t *r = userdata1;
    if (r->device) {
        box_device_complete(r);
        box_frame_init(r);
        box_renderer_set_ready(r, true);
    }
}

/** Adapter callback on web: request the device.
 *
 * @param status Request outcome
 * @param adapter The adapter on success
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 Unused
 */
static void box_on_adapter_web(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_on_adapter(status, adapter, message, userdata1, userdata2);
    box_renderer_t *r = userdata1;
    if (r->adapter) {
        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.uncapturedErrorCallbackInfo.callback = box_on_device_error;
        desc.uncapturedErrorCallbackInfo.userdata1 = r;
        WGPURequestDeviceCallbackInfo info = {
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .callback = box_on_device_web,
            .userdata1 = r
        };
        wgpuAdapterRequestDevice(r->adapter, &desc, info);
    }
}
#endif

int box_device_init(
    box_renderer_t *r)
{
    int result = 0;
    r->instance = wgpuCreateInstance(NULL);
    if (!r->instance) {
        fprintf(stderr, "box: failed to create WebGPU instance\n");
        result = -1;
        goto done;
    }

    box_device_create_surface(r);

    WGPURequestAdapterOptions options = { .compatibleSurface = r->surface };
#ifdef BOX_PLATFORM_WEB
    WGPURequestAdapterCallbackInfo adapter_info = {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_on_adapter_web,
        .userdata1 = r
    };
    wgpuInstanceRequestAdapter(r->instance, &options, adapter_info);
#else
    WGPURequestAdapterCallbackInfo adapter_info = {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_on_adapter,
        .userdata1 = r
    };
    wgpuInstanceRequestAdapter(r->instance, &options, adapter_info);
    if (!r->adapter) {
        result = -1;
        goto done;
    }

    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.uncapturedErrorCallbackInfo.callback = box_on_device_error;
    desc.uncapturedErrorCallbackInfo.userdata1 = r;
    WGPURequestDeviceCallbackInfo device_info = {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_on_device,
        .userdata1 = r
    };
    wgpuAdapterRequestDevice(r->adapter, &desc, device_info);
    if (!r->device) {
        result = -1;
        goto done;
    }

    box_device_complete(r);
#endif
done:
    return result;
}

void box_device_fini(
    box_renderer_t *r)
{
    box_view_fini(r);
    box_target_fini(r);
    box_scene_fini(r);
    box_lights_fini(r);
    box_transparent_fini(r);
    box_environment_fini(r);
    box_texture_fini(r);
    box_bake_fini(r);
    box_cull_fini(r);
    box_layers_fini(r);
    if (r->pipeline) wgpuRenderPipelineRelease(r->pipeline);
    if (r->mesh_layout) wgpuPipelineLayoutRelease(r->mesh_layout);
    if (r->mesh_shader) wgpuShaderModuleRelease(r->mesh_shader);
    if (r->view_layout) wgpuBindGroupLayoutRelease(r->view_layout);
    if (r->offscreen_view) wgpuTextureViewRelease(r->offscreen_view);
    if (r->offscreen) wgpuTextureRelease(r->offscreen);
    if (r->gpu_queue) wgpuQueueRelease(r->gpu_queue);
    if (r->device) wgpuDeviceRelease(r->device);
    if (r->adapter) wgpuAdapterRelease(r->adapter);
    if (r->surface) wgpuSurfaceRelease(r->surface);
    if (r->instance) wgpuInstanceRelease(r->instance);
    memset(&r->instance, 0, sizeof(*r) - offsetof(box_renderer_t, instance));
}

void box_device_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height)
{
    bool unchanged = width == r->width && height == r->height;
    if (unchanged || !width || !height) {
        goto done;
    }

    r->width = width;
    r->height = height;
    if (r->surface) {
        box_device_configure_surface(r);
        goto done;
    }

    if (r->offscreen_view) wgpuTextureViewRelease(r->offscreen_view);
    if (r->offscreen) wgpuTextureRelease(r->offscreen);
    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = r->format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->offscreen = wgpuDeviceCreateTexture(r->device, &color_desc);
    r->offscreen_view = wgpuTextureCreateView(r->offscreen, NULL);
done:
    return;
}
