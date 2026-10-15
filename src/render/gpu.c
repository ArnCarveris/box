/**
 * @file render/gpu.c
 * @brief The webgpu backend: instance, adapter and device on the rendering thread.
 *
 * init creates the device and the objects the targets share, render applies the target
 * resources of a packet and renders its targets, fini releases everything init created.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else

/** Log a webgpu message.
 *
 * @param prefix What the message is about
 * @param message The message
 */
static void box_gpu_report(
    const char *prefix,
    WGPUStringView message)
{
    int len = 0;
    if (message.data) {
        len = message.length == WGPU_STRLEN
            ? ecs_os_strlen(message.data)
            : (int)message.length;
    }

    ecs_err("%s: %.*s", prefix, len, message.data ? message.data : "");
}

/** Forward the warnings and errors of wgpu to the log.
 *
 * @param level Severity of the message
 * @param message The message
 * @param userdata Unused
 */
static void box_gpu_log(
    WGPULogLevel level,
    WGPUStringView message,
    void *userdata)
{
    (void)userdata;
    box_gpu_report(level == WGPULogLevel_Error ? "wgpu error" : "wgpu warning", message);
}

/** Keep the adapter of an adapter request.
 *
 * @param status Outcome of the request
 * @param adapter The adapter on success
 * @param message Error text
 * @param userdata1 The backend
 * @param userdata2 Unused
 */
static void box_gpu_on_adapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_t *gpu = userdata1;
    (void)userdata2;
    if (status == WGPURequestAdapterStatus_Success) {
        gpu->adapter = adapter;
    } else {
        box_gpu_report("no webgpu adapter", message);
    }
}

/** Keep the device of a device request.
 *
 * @param status Outcome of the request
 * @param device The device on success
 * @param message Error text
 * @param userdata1 The backend
 * @param userdata2 Unused
 */
static void box_gpu_on_device(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_t *gpu = userdata1;
    (void)userdata2;
    if (status == WGPURequestDeviceStatus_Success) {
        gpu->device = device;
    } else {
        box_gpu_report("no webgpu device", message);
    }
}

/** Count and log an error no error scope caught, unless the device is lost: releasing what
 * belonged to a lost device reports errors, and the ring recovers from the loss itself.
 *
 * @param device The device
 * @param type Kind of error
 * @param message Error text
 * @param userdata1 The backend
 * @param userdata2 Unused
 */
static void box_gpu_on_error(
    WGPUDevice const *device,
    WGPUErrorType type,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)device;
    (void)type;
    (void)userdata2;
    if (!((box_gpu_t*)userdata1)->lost) {
        box_gpu_count(userdata1, BOX_GPU_STAT_ERRORS);
        box_gpu_report("webgpu device error", message);
    }
}

/** Flag the loss of the device for the ring, and count and log it unless it was destroyed.
 *
 * @param device The device
 * @param reason Why it was lost
 * @param message Error text
 * @param userdata1 The backend
 * @param userdata2 Unused
 */
static void box_gpu_on_lost(
    WGPUDevice const *device,
    WGPUDeviceLostReason reason,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)device;
    (void)userdata2;
    if (reason != WGPUDeviceLostReason_CallbackCancelled) {
        ((box_gpu_t*)userdata1)->lost = true;
    }

    if (reason != WGPUDeviceLostReason_Destroyed &&
        reason != WGPUDeviceLostReason_CallbackCancelled)
    {
        box_gpu_count(userdata1, BOX_GPU_STAT_ERRORS);
        box_gpu_report("webgpu device lost", message);
    }
}

#endif

/** Create the layouts and sampler the blit pipelines share.
 *
 * @param gpu The backend
 */
static void box_gpu_blit_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 2, .entries = entries };
    WGPUSamplerDescriptor sampler = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 32,
        .maxAnisotropy = 1
    };
    gpu->blit_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    gpu->blit_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu->blit_layout
        });
    gpu->sampler = wgpuDeviceCreateSampler(gpu->device, &sampler);
}

#ifdef __EMSCRIPTEN__

/** Check whether the page handed the module a device, see apps/boxrun/shell.html.
 *
 * @return Whether Module.preinitializedWebGPUDevice is set
 */
EM_JS(bool, box_gpu_web_has_device, (void), {
    return !!Module['preinitializedWebGPUDevice'];
});

/** Take the device the page requested before the module started.
 *
 * The browser requests adapters and devices asynchronously and a frame cannot wait for a
 * promise, so the shell page requests the device and passes it as preinitializedWebGPUDevice.
 *
 * @param gpu The backend
 */
static void box_gpu_device_init(
    box_gpu_t *gpu)
{
    if (!box_gpu_web_has_device()) {
        ecs_err("no webgpu device: the page did not pass preinitializedWebGPUDevice");
        goto done;
    }

    gpu->device = emscripten_webgpu_get_device();
    gpu->queue = wgpuDeviceGetQueue(gpu->device);
    gpu->timestamps = wgpuDeviceHasFeature(gpu->device, WGPUFeatureName_TimestampQuery);
done:
    return;
}

/** Create the instance and take the device the page requested.
 *
 * @param gpu The backend
 * @return 0 when the device exists, nonzero when there is none
 */
static int box_gpu_create(
    box_gpu_t *gpu)
{
    int result = -1;
    gpu->instance = wgpuCreateInstance(NULL);
    if (!gpu->instance) {
        ecs_err("cannot create a webgpu instance");
        goto done;
    }

    box_gpu_device_init(gpu);
    if (gpu->device) {
        result = 0;
    }

done:
    return result;
}

#else

/** Request the device from the adapter and get its queue.
 *
 * @param gpu The backend
 */
static void box_gpu_device_init(
    box_gpu_t *gpu)
{
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    WGPUFeatureName timestamps = WGPUFeatureName_TimestampQuery;
    if (wgpuAdapterHasFeature(gpu->adapter, timestamps)) {
        desc.requiredFeatureCount = 1;
        desc.requiredFeatures = &timestamps;
    }

    desc.uncapturedErrorCallbackInfo.callback = box_gpu_on_error;
    desc.uncapturedErrorCallbackInfo.userdata1 = gpu;
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    desc.deviceLostCallbackInfo.callback = box_gpu_on_lost;
    desc.deviceLostCallbackInfo.userdata1 = gpu;
    wgpuAdapterRequestDevice(gpu->adapter, &desc, (WGPURequestDeviceCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_gpu_on_device,
        .userdata1 = gpu
    });
    if (gpu->device) {
        gpu->queue = wgpuDeviceGetQueue(gpu->device);
        gpu->timestamps = wgpuDeviceHasFeature(gpu->device, timestamps);
    }
}

/** Create the instance, adapter and device.
 *
 * @param gpu The backend
 * @return 0 when the device exists, nonzero when it could not be created
 */
static int box_gpu_create(
    box_gpu_t *gpu)
{
    int result = -1;
    wgpuSetLogCallback(box_gpu_log, NULL);
    wgpuSetLogLevel(WGPULogLevel_Warn);
    gpu->instance = wgpuCreateInstance(NULL);
    if (!gpu->instance) {
        ecs_err("cannot create a webgpu instance");
        goto done;
    }

    WGPURequestAdapterOptions options = {
        .powerPreference = WGPUPowerPreference_HighPerformance
    };
    wgpuInstanceRequestAdapter(gpu->instance, &options, (WGPURequestAdapterCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_gpu_on_adapter,
        .userdata1 = gpu
    });
    if (!gpu->adapter) {
        goto done;
    }

    box_gpu_device_init(gpu);
    if (gpu->device) {
        result = 0;
    }

done:
    return result;
}

#endif

/** Create the device and the objects the targets share on the thread that renders.
 *
 * @param ctx The backend
 * @return 0 when the device exists, nonzero when it could not be created
 */
static int box_gpu_init(
    void *ctx)
{
    box_gpu_t *gpu = ctx;
    int result = box_gpu_create(gpu);
    gpu->fence_done = 0;
    gpu->lost = false;
    gpu->thread = ecs_os_thread_self();
    if (result) {
        goto done;
    }

    box_gpu_blit_init(gpu);
    box_gpu_texture_init(gpu);
    box_gpu_draw_init(gpu);
    box_gpu_scatter_init(gpu);
    ecs_map_init(&gpu->targets, NULL);
    ecs_map_init(&gpu->shaders, NULL);
done:
    box_gpu_stats_publish(gpu);
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.ready = result == 0;
    gpu->stats.failed = result != 0;
    ecs_os_mutex_unlock(gpu->lock);

    return result;
}

/** Render the targets of a packet: the ones materials sample first, then the others, each
 * group in packet order.
 *
 * @param gpu The backend
 * @param packet The packet
 */
static void box_gpu_targets_render(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, pass, count = ecs_vec_count(&packet->targets);
    for (pass = 0; pass < 2; pass ++) {
        for (i = 0; i < count; i ++) {
            const box_render_target_t *frame = ecs_vec_get_t(
                &packet->targets, box_render_target_t, i);
            box_gpu_target_t *t = ecs_map_get_deref(
                &gpu->targets, box_gpu_target_t, frame->handle);
            bool texture = t && t->desc.texture;
            if (texture == (pass == 0)) {
                box_gpu_target_render(gpu, packet, frame);
            }
        }
    }
}

/** Apply the resources of a packet, capture its probes and render its targets.
 *
 * @param ctx The backend
 * @param packet The packet
 */
static void box_gpu_render(
    void *ctx,
    const box_render_packet_t *packet)
{
    box_gpu_t *gpu = ctx;
    int32_t i, count = ecs_vec_count(&packet->resources);
    box_gpu_shader_deliver(gpu);
    for (i = 0; i < count; i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &packet->resources, box_render_resource_t, i);
        const box_render_target_desc_t *desc = NULL;
        const void *data = r->op != BOX_RENDER_DESTROY && r->size
            ? box_render_packet_data(packet, r->data)
            : NULL;
        if (r->kind == BOX_RENDER_SHADER) {
            box_gpu_shader_apply(gpu, r->op, r->handle,
                r->size >= sizeof(box_render_shader_desc_t) ? data : NULL);
        } else if (r->kind == BOX_RENDER_MESH) {
            box_gpu_mesh_apply(gpu, r->op, r->handle,
                r->size >= sizeof(box_render_mesh_desc_t) ? data : NULL);
        } else if (r->kind == BOX_RENDER_BUFFER) {
            box_gpu_buffer_apply(gpu, r->op, r->handle,
                r->size >= sizeof(box_render_buffer_desc_t) ? data : NULL);
        } else if (r->kind == BOX_RENDER_TEXTURE) {
            box_gpu_texture_apply(gpu, r->op, r->handle,
                r->size >= sizeof(box_render_texture_desc_t) ? data : NULL);
        } else if (r->kind == BOX_RENDER_VOLUME) {
            box_gpu_bake_apply(gpu, r->op, r->handle,
                r->size >= sizeof(box_render_volume_desc_t) ? data : NULL, r->size);
        }

        if (r->kind != BOX_RENDER_TARGET) {
            continue;
        }

        if (r->op != BOX_RENDER_DESTROY && r->size >= sizeof(box_render_target_desc_t)) {
            desc = box_render_packet_data(packet, r->data);
        }

        if (r->op == BOX_RENDER_DESTROY || desc) {
            box_gpu_target_apply(gpu, r->op, r->handle, desc);
        }
    }

    box_gpu_texture_mips(gpu, packet);
    box_gpu_upload(gpu, packet);
    box_gpu_scatter(gpu, packet);
    box_gpu_bake_capture(gpu, packet);
    box_gpu_targets_render(gpu, packet);

    box_gpu_effects_sweep(gpu, packet);
    box_gpu_stats_publish(gpu);
}

#ifndef __EMSCRIPTEN__
/** Remember that the queue finished the work of a fence.
 *
 * @param status Whether the work completed
 * @param message Unused
 * @param userdata1 The backend
 * @param userdata2 The fence
 */
static void box_gpu_fence_done(
    WGPUQueueWorkDoneStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_t *gpu = userdata1;
    uint64_t fence = (uint64_t)(uintptr_t)userdata2;
    (void)message;
    if (status == WGPUQueueWorkDoneStatus_Success && fence > gpu->fence_done) {
        gpu->fence_done = fence;
    }
}
#endif

/** Mark the work the backend submitted so far, so a wait can block until the GPU finished it.
 *
 * @param ctx The backend
 * @return The fence, 0 when the platform cannot wait for one
 */
static uint64_t box_gpu_fence(
    void *ctx)
{
    box_gpu_t *gpu = ctx;
    uint64_t result = 0;
#ifndef __EMSCRIPTEN__
    WGPUCommandBuffer none = NULL;
    if (gpu->queue) {
        result = (uint64_t)wgpuQueueSubmitForIndex(gpu->queue, 0, &none) + 1;
        wgpuQueueOnSubmittedWorkDone(gpu->queue, (WGPUQueueWorkDoneCallbackInfo){
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .callback = box_gpu_fence_done,
            .userdata1 = gpu,
            .userdata2 = (void*)(uintptr_t)result
        });
    }
#else
    (void)gpu;
#endif

    return result;
}

/** Block until the GPU finished the work of a fence.
 *
 * @param ctx The backend
 * @param fence The fence, 0 returns at once
 */
static void box_gpu_wait(
    void *ctx,
    uint64_t fence)
{
    box_gpu_t *gpu = ctx;
#ifndef __EMSCRIPTEN__
    WGPUSubmissionIndex index = (WGPUSubmissionIndex)(fence - 1);
    if (fence && gpu->device) {
        wgpuDevicePoll(gpu->device, true, &index);
    }
#else
    (void)gpu;
    (void)fence;
#endif
}

/** Check without blocking whether the GPU finished the work of a fence.
 *
 * @param ctx The backend
 * @param fence The fence, 0 is always done
 * @return Whether the work is done
 */
static bool box_gpu_done(
    void *ctx,
    uint64_t fence)
{
    box_gpu_t *gpu = ctx;
#ifndef __EMSCRIPTEN__
    if (fence > gpu->fence_done && gpu->device) {
        wgpuDevicePoll(gpu->device, false, NULL);
    }
#endif

    return fence <= gpu->fence_done || !gpu->device;
}

/** Destroy the device when box_render_gpu_lose_device asked for it, which loses it, and check
 * whether the device was lost.
 *
 * @param ctx The backend
 * @return Whether the device was lost, so the ring recreates it
 */
static bool box_gpu_lost(
    void *ctx)
{
    box_gpu_t *gpu = ctx;
    bool lose;
    ecs_os_mutex_lock(gpu->lock);
    lose = gpu->lose;
    gpu->lose = false;
    ecs_os_mutex_unlock(gpu->lock);
#ifndef __EMSCRIPTEN__
    if (lose && gpu->device) {
        wgpuDeviceDestroy(gpu->device);
        gpu->lost = true;
    }
#else
    (void)lose;
#endif

    return gpu->lost;
}

/** Release a webgpu object when it exists.
 *
 * @param obj The object
 * @param release The release function of its type
 */
#define BOX_GPU_RELEASE(obj, release) \
    if (obj) { \
        release(obj); \
        obj = NULL; \
    }

/** Release everything init created.
 *
 * @param ctx The backend
 */
static void box_gpu_fini(
    void *ctx)
{
    box_gpu_t *gpu = ctx;
    if (gpu->device) {
        box_gpu_target_fini(gpu);
        ecs_map_fini(&gpu->targets);
        box_gpu_shader_fini(gpu);
        ecs_map_fini(&gpu->shaders);
        box_gpu_draw_fini(gpu);
        box_gpu_texture_fini(gpu);
        box_gpu_scatter_fini(gpu);
    }

    box_gpu_blit_reset(gpu);
    BOX_GPU_RELEASE(gpu->sampler, wgpuSamplerRelease);
    BOX_GPU_RELEASE(gpu->blit_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_RELEASE(gpu->blit_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_RELEASE(gpu->queue, wgpuQueueRelease);
    BOX_GPU_RELEASE(gpu->device, wgpuDeviceRelease);
    BOX_GPU_RELEASE(gpu->adapter, wgpuAdapterRelease);
    BOX_GPU_RELEASE(gpu->instance, wgpuInstanceRelease);
    box_gpu_stats_publish(gpu);
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.ready = false;
    ecs_os_mutex_unlock(gpu->lock);
}

/** Free the backend state on the main thread.
 *
 * @param ctx The backend
 */
static void box_gpu_free(
    void *ctx)
{
    box_gpu_t *gpu = ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&gpu->results); i ++) {
        ecs_os_free(ecs_vec_get_t(&gpu->results, box_render_shader_result_t, i)->message);
    }

    ecs_vec_fini_t(NULL, &gpu->results, box_render_shader_result_t);
    ecs_os_mutex_free(gpu->lock);
    ecs_os_free(gpu);
}

void box_gpu_count(
    box_gpu_t *gpu,
    box_gpu_stat_t stat)
{
    switch (stat) {
    case BOX_GPU_STAT_CREATED:
        gpu->pending.created ++;
        gpu->pending.targets ++;
        break;
    case BOX_GPU_STAT_CONFIGURED:
        gpu->pending.configured ++;
        break;
    case BOX_GPU_STAT_DESTROYED:
        gpu->pending.destroyed ++;
        gpu->pending.targets --;
        break;
    case BOX_GPU_STAT_FRAMES:
        gpu->pending.frames ++;
        break;
    case BOX_GPU_STAT_CAPTURES:
        gpu->pending.captures ++;
        break;
    case BOX_GPU_STAT_ERRORS:
        gpu->pending.errors ++;
        break;
    case BOX_GPU_STAT_SHADER_CREATED:
        gpu->pending.shaders ++;
        break;
    case BOX_GPU_STAT_SHADER_DESTROYED:
        gpu->pending.shaders --;
        break;
    case BOX_GPU_STAT_COMPILED:
        gpu->pending.compiles ++;
        break;
    case BOX_GPU_STAT_SHADER_ERROR:
        gpu->pending.shader_errors ++;
        break;
    case BOX_GPU_STAT_MESH_CREATED:
        gpu->pending.meshes ++;
        break;
    case BOX_GPU_STAT_MESH_DESTROYED:
        gpu->pending.meshes --;
        break;
    case BOX_GPU_STAT_BUFFER_CREATED:
        gpu->pending.buffers ++;
        break;
    case BOX_GPU_STAT_BUFFER_DESTROYED:
        gpu->pending.buffers --;
        break;
    case BOX_GPU_STAT_PASS:
        gpu->pending.passes ++;
        break;
    case BOX_GPU_STAT_DRAW:
        gpu->pending.draws ++;
        break;
    case BOX_GPU_STAT_SCATTER:
        gpu->pending.scatters ++;
        break;
    }
}

void box_gpu_stats_publish(
    box_gpu_t *gpu)
{
    box_render_gpu_stats_t *p = &gpu->pending;
    box_render_gpu_stats_t *s = &gpu->stats;
    ecs_os_mutex_lock(gpu->lock);
    s->targets += p->targets;
    s->created += p->created;
    s->configured += p->configured;
    s->destroyed += p->destroyed;
    s->frames += p->frames;
    s->captures += p->captures;
    s->errors += p->errors;
    s->shaders += p->shaders;
    s->compiles += p->compiles;
    s->shader_errors += p->shader_errors;
    s->meshes += p->meshes;
    s->buffers += p->buffers;
    s->passes += p->passes;
    s->draws += p->draws;
    s->scatters += p->scatters;
    s->scattered += p->scattered;
    ecs_os_mutex_unlock(gpu->lock);
    ecs_os_memset_t(p, 0, box_render_gpu_stats_t);
}

box_render_backend_t box_render_gpu_backend(void)
{
    box_gpu_t *gpu = ecs_os_calloc_t(box_gpu_t);
    gpu->lock = ecs_os_mutex_new();
    gpu->cull_window = BOX_GPU_CULL_WINDOW;
    ecs_vec_init_t(NULL, &gpu->results, box_render_shader_result_t, 0);
    box_render_backend_t result = {
        .ctx = gpu,
        .init = box_gpu_init,
        .render = box_gpu_render,
        .fini = box_gpu_fini,
        .free = box_gpu_free,
        .fence = box_gpu_fence,
        .wait = box_gpu_wait,
        .done = box_gpu_done,
        .flush = box_gpu_capture_flush,
        .lost = box_gpu_lost
    };

    return result;
}

bool box_render_gpu_stats(
    ecs_world_t *world,
    box_render_gpu_stats_t *stats)
{
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool result = backend->render == box_gpu_render;
    ecs_os_memset_t(stats, 0, box_render_gpu_stats_t);
    if (result) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        *stats = gpu->stats;
        ecs_os_mutex_unlock(gpu->lock);
    }

    return result;
}

bool box_render_gpu_compile_delivery(
    ecs_world_t *world,
    box_render_compile_delivery_t delivery)
{
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool result = backend->render == box_gpu_render;
    if (result) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        gpu->delivery = delivery;
        ecs_os_mutex_unlock(gpu->lock);
    }

    return result;
}

bool box_render_gpu_capture_pending(
    ecs_world_t *world,
    const char *path)
{
    const box_render_backend_t *backend = box_render_get_backend(world);

    return backend->render == box_gpu_render && box_gpu_capture_pending(backend->ctx, path);
}

void box_render_gpu_lose_device(
    ecs_world_t *world)
{
    const box_render_backend_t *backend = box_render_get_backend(world);
    if (backend->render == box_gpu_render) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        gpu->lose = true;
        ecs_os_mutex_unlock(gpu->lock);
    }
}

bool box_render_gpu_cull_window(
    ecs_world_t *world,
    uint32_t slots)
{
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool result = backend->render == box_gpu_render && slots > 0;
    if (result) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        gpu->cull_window = slots;
        ecs_os_mutex_unlock(gpu->lock);
    }

    return result;
}

bool box_render_gpu_readback_delivery(
    ecs_world_t *world,
    box_render_readback_delivery_t delivery)
{
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool result = backend->render == box_gpu_render;
    if (result) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        gpu->readbacks = delivery;
        ecs_os_mutex_unlock(gpu->lock);
    }

    return result;
}

bool box_render_gpu_shader_result(
    ecs_world_t *world,
    box_render_shader_result_t *result)
{
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool found = false;
    if (backend->render == box_gpu_render) {
        box_gpu_t *gpu = backend->ctx;
        ecs_os_mutex_lock(gpu->lock);
        if (ecs_vec_count(&gpu->results)) {
            *result = *ecs_vec_first_t(&gpu->results, box_render_shader_result_t);
            ecs_vec_remove_ordered_t(&gpu->results, box_render_shader_result_t, 0);
            found = true;
        }

        ecs_os_mutex_unlock(gpu->lock);
    }

    return found;
}

#else

box_render_backend_t box_render_gpu_backend(void)
{
    box_render_backend_t result = box_render_null_backend(box_render_null_new());
    result.free = box_render_null_free;

    return result;
}

bool box_render_gpu_stats(
    ecs_world_t *world,
    box_render_gpu_stats_t *stats)
{
    (void)world;
    ecs_os_memset_t(stats, 0, box_render_gpu_stats_t);

    return false;
}

bool box_render_gpu_compile_delivery(
    ecs_world_t *world,
    box_render_compile_delivery_t delivery)
{
    (void)world;
    (void)delivery;

    return false;
}

bool box_render_gpu_capture_pending(
    ecs_world_t *world,
    const char *path)
{
    (void)world;
    (void)path;

    return false;
}

bool box_render_gpu_cull_window(
    ecs_world_t *world,
    uint32_t slots)
{
    (void)world;
    (void)slots;

    return false;
}

bool box_render_gpu_readback_delivery(
    ecs_world_t *world,
    box_render_readback_delivery_t delivery)
{
    (void)world;
    (void)delivery;

    return false;
}

void box_render_gpu_lose_device(
    ecs_world_t *world)
{
    (void)world;
}

bool box_render_gpu_shader_result(
    ecs_world_t *world,
    box_render_shader_result_t *result)
{
    (void)world;
    (void)result;

    return false;
}

#endif
