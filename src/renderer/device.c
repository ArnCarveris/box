/**
 * @file renderer/device.c
 * @brief WebGPU instance, surface, adapter, device and render target creation.
 */

#include "renderer_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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
        r->device_errors ++;
    }

    box_device_report("device error", message);
}

/** Ask the adapter for the limits the device should be created with.
 *
 * The defaults cap a buffer at 256 MiB and a storage binding at 128 MiB, which
 * a scene of a million instances passes; the adapter usually allows far more,
 * and what it allows is the ceiling the instance buffers grow against.
 *
 * @param r The renderer
 * @return The limits to require, NULL when the adapter reports none
 */
static const WGPULimits* box_device_limits(
    box_renderer_t *r)
{
    const WGPULimits *result = NULL;
    r->limits = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(r->adapter, &r->limits) == WGPUStatus_Success) {
        result = &r->limits;
    } else {
        r->limits.maxBufferSize = BOX_DEVICE_BUFFER_DEFAULT;
        r->limits.maxStorageBufferBindingSize = BOX_DEVICE_STORAGE_DEFAULT;
    }

    return result;
}

#ifndef BOX_PLATFORM_WEB
/** Features the device asks the adapter for, which the adapter has to have.
 *
 * The timestamp query is the only optional feature the renderer uses, and a
 * device without it renders the same frames and reports no pass times.
 *
 * @param r The renderer
 * @param out Receives the features to require
 * @return The number of features
 */
static size_t box_device_features(
    box_renderer_t *r,
    WGPUFeatureName *out)
{
    size_t result = 0;
    if (wgpuAdapterHasFeature(r->adapter, WGPUFeatureName_TimestampQuery)) {
        out[result] = WGPUFeatureName_TimestampQuery;
        result ++;
    }

    return result;
}
#endif

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
#elif defined(_WIN32)
    WGPUSurfaceSourceWindowsHWND source = {
        .chain = { .sType = WGPUSType_SurfaceSourceWindowsHWND },
        .hinstance = GetModuleHandleW(NULL),
        .hwnd = r->desc.metal_layer
    };
    bool has_source = r->desc.metal_layer != NULL;
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
#ifndef BOX_PLATFORM_WEB
    r->timing_supported = wgpuDeviceHasFeature(r->device, WGPUFeatureName_TimestampQuery);
#endif
    box_device_select_format(r);
    box_device_resize(r, r->desc.width, r->desc.height);
    box_frame_resources(r);
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
        desc.requiredLimits = box_device_limits(r);
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

/** Turn a GPU choice that names no adapter into the power preference it asks for.
 *
 * @param gpu The GPU choice: NULL, "default", "high-performance", "low-power" or a name
 * @param out Receives the power preference, undefined for no choice or a name
 * @return Whether the choice is a power preference or none, false when it is a name
 */
static bool box_device_power_preference(
    const char *gpu,
    WGPUPowerPreference *out)
{
    bool result = true;
    out[0] = WGPUPowerPreference_Undefined;
    if (!gpu || !gpu[0] || !strcmp(gpu, "default")) {
        result = true;
    } else if (!strcmp(gpu, "high-performance")) {
        out[0] = WGPUPowerPreference_HighPerformance;
    } else if (!strcmp(gpu, "low-power")) {
        out[0] = WGPUPowerPreference_LowPower;
    } else {
        result = false;
    }

    return result;
}

#ifndef BOX_PLATFORM_WEB
/** Name of a WebGPU backend.
 *
 * @param backend The backend
 * @return Its name
 */
static const char* box_device_backend_name(
    WGPUBackendType backend)
{
    const char *result = "other";
    switch (backend) {
    case WGPUBackendType_D3D12: result = "D3D12"; break;
    case WGPUBackendType_Metal: result = "Metal"; break;
    case WGPUBackendType_Vulkan: result = "Vulkan"; break;
    case WGPUBackendType_OpenGL: result = "OpenGL"; break;
    case WGPUBackendType_OpenGLES: result = "OpenGLES"; break;
    default: break;
    }

    return result;
}

/** Name of an adapter type.
 *
 * @param type The adapter type
 * @return Its name
 */
static const char* box_device_type_name(
    WGPUAdapterType type)
{
    const char *result = "unknown";
    switch (type) {
    case WGPUAdapterType_DiscreteGPU: result = "discrete"; break;
    case WGPUAdapterType_IntegratedGPU: result = "integrated"; break;
    case WGPUAdapterType_CPU: result = "cpu"; break;
    default: break;
    }

    return result;
}

/** Bytes of a WebGPU string view.
 *
 * @param view The string view
 * @return Its length, 0 for a view without data
 */
static size_t box_device_view_len(
    WGPUStringView view)
{
    size_t result = 0;
    if (view.data) {
        result = view.length == WGPU_STRLEN ? strlen(view.data) : view.length;
    }

    return result;
}

/** Test whether a WebGPU string view contains a text, ignoring case.
 *
 * @param view The string view
 * @param text The text to look for
 * @return Whether the text occurs in the view
 */
static bool box_device_view_contains(
    WGPUStringView view,
    const char *text)
{
    size_t len = box_device_view_len(view);
    size_t text_len = strlen(text);
    bool result = false;
    size_t i, j;
    for (i = 0; !result && i + text_len <= len; i ++) {
        j = 0;
        while (j < text_len &&
            tolower((unsigned char)view.data[i + j]) == tolower((unsigned char)text[j]))
        {
            j ++;
        }

        result = j == text_len;
    }

    return result;
}

/** Print the name, backend and type of an adapter to stderr.
 *
 * @param prefix Text before the adapter
 * @param adapter The adapter
 */
static void box_device_print_adapter(
    const char *prefix,
    WGPUAdapter adapter)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success) {
        fprintf(stderr, "box: %s%.*s (%s, %s)\n", prefix,
            (int)box_device_view_len(info.device), info.device.data ? info.device.data : "",
            box_device_backend_name(info.backendType), box_device_type_name(info.adapterType));
    }

    wgpuAdapterInfoFreeMembers(info);
}

/** Test whether an adapter can present to the surface of the renderer.
 *
 * @param r The renderer
 * @param adapter The adapter
 * @return Whether it can, true when the renderer draws offscreen
 */
static bool box_device_supports_surface(
    box_renderer_t *r,
    WGPUAdapter adapter)
{
    bool result = true;
    if (r->surface) {
        WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
        result = wgpuSurfaceGetCapabilities(r->surface, adapter, &caps) == WGPUStatus_Success &&
            caps.formatCount > 0;
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }

    return result;
}

/** Find the first adapter whose name contains the GPU choice and that can present
 * to the surface. When none does, the adapters there are printed to stderr.
 *
 * @param r The renderer
 * @param gpu Part of the adapter name, compared without case
 * @return The adapter, NULL when none matches
 */
static WGPUAdapter box_device_find_adapter(
    box_renderer_t *r,
    const char *gpu)
{
    WGPUAdapter result = NULL;
    size_t i, count = wgpuInstanceEnumerateAdapters(r->instance, NULL, NULL);
    WGPUAdapter *adapters = calloc(count ? count : 1, sizeof(WGPUAdapter));
    count = wgpuInstanceEnumerateAdapters(r->instance, NULL, adapters);
    for (i = 0; i < count && !result; i ++) {
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        if (wgpuAdapterGetInfo(adapters[i], &info) == WGPUStatus_Success &&
            box_device_view_contains(info.device, gpu) &&
            box_device_supports_surface(r, adapters[i]))
        {
            result = adapters[i];
        }

        wgpuAdapterInfoFreeMembers(info);
    }

    if (!result) {
        fprintf(stderr, "box: no GPU matches '%s'; the adapters are:\n", gpu);
        for (i = 0; i < count; i ++) {
            box_device_print_adapter("  ", adapters[i]);
        }
    }

    for (i = 0; i < count; i ++) {
        if (adapters[i] != result) {
            wgpuAdapterRelease(adapters[i]);
        }
    }

    free(adapters);

    return result;
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
    bool by_power = box_device_power_preference(r->desc.gpu, &options.powerPreference);
#ifdef BOX_PLATFORM_WEB
    (void)by_power;
    WGPURequestAdapterCallbackInfo adapter_info = {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_on_adapter_web,
        .userdata1 = r
    };
    wgpuInstanceRequestAdapter(r->instance, &options, adapter_info);
#else
    if (by_power) {
        WGPURequestAdapterCallbackInfo adapter_info = {
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .callback = box_on_adapter,
            .userdata1 = r
        };
        wgpuInstanceRequestAdapter(r->instance, &options, adapter_info);
    } else {
        r->adapter = box_device_find_adapter(r, r->desc.gpu);
    }

    if (!r->adapter) {
        result = -1;
        goto done;
    }

    if (r->desc.gpu) {
        box_device_print_adapter("gpu ", r->adapter);
    }

    WGPUFeatureName features[1];
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.requiredLimits = box_device_limits(r);
    desc.requiredFeatures = features;
    desc.requiredFeatureCount = box_device_features(r, features);
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

uint32_t box_device_instances(
    const box_renderer_t *r,
    bool storage)
{
    uint64_t bytes = r->limits.maxBufferSize;
    if (storage && r->limits.maxStorageBufferBindingSize < bytes) {
        bytes = r->limits.maxStorageBufferBindingSize;
    }

    uint64_t result = bytes / sizeof(box_instance_t);
    if (result > BOX_DEVICE_INSTANCES_MAX) {
        result = BOX_DEVICE_INSTANCES_MAX;
    }

    return (uint32_t)result;
}

void box_device_fini(
    box_renderer_t *r)
{
    box_timing_fini(r);
    box_view_fini(r);
    box_target_fini(r);
    box_scene_fini(r);
    box_lights_fini(r);
    box_transparent_fini(r);
    box_environment_fini(r);
    box_sky_fini(r);
    box_particle_fini(r);
    box_texture_fini(r);
    box_bake_fini(r);
    box_cull_fini(r);
    box_shadow_fini(r);
    box_layers_fini(r);
    box_modulators_fini(r);
    box_effect_fini(r);
    box_upscale_fini(r);
    box_lines_fini(r);
    box_shader_fini(r);
    uint32_t slot;
    for (slot = 0; slot < BOX_EFFECT_FORMATS; slot ++) {
        if (r->pipeline[slot]) wgpuRenderPipelineRelease(r->pipeline[slot]);
        if (r->normal_pipeline[slot]) wgpuRenderPipelineRelease(r->normal_pipeline[slot]);
        if (r->occluded_pipeline[slot]) wgpuRenderPipelineRelease(r->occluded_pipeline[slot]);
        for (uint32_t normal = 0; normal < 2; normal ++) {
            if (r->solid_pipeline[slot][normal]) {
                wgpuRenderPipelineRelease(r->solid_pipeline[slot][normal]);
            }
        }
    }

    if (r->mesh_layout) wgpuPipelineLayoutRelease(r->mesh_layout);
    if (r->mesh_shader) wgpuShaderModuleRelease(r->mesh_shader);
    if (r->view_layout) wgpuBindGroupLayoutRelease(r->view_layout);
    if (r->offscreen_view) wgpuTextureViewRelease(r->offscreen_view);
    if (r->offscreen) wgpuTextureRelease(r->offscreen);
    box_count_fini(r);
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
