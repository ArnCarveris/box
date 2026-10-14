/**
 * @file render/gpu.c
 * @brief The webgpu backend: instance, adapter and device on the rendering thread.
 *
 * init creates the device and the objects the targets share, render applies the target
 * resources of a packet and renders its targets, fini releases everything init created.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

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

/** Count and log an error no error scope caught.
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
    box_gpu_count(userdata1, BOX_GPU_STAT_ERRORS);
    box_gpu_report("webgpu device error", message);
}

/** Count and log the loss of the device, unless the backend released it.
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
    if (reason != WGPUDeviceLostReason_Destroyed &&
        reason != WGPUDeviceLostReason_CallbackCancelled)
    {
        box_gpu_count(userdata1, BOX_GPU_STAT_ERRORS);
        box_gpu_report("webgpu device lost", message);
    }
}

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

/** Request the device from the adapter and get its queue.
 *
 * @param gpu The backend
 */
static void box_gpu_device_init(
    box_gpu_t *gpu)
{
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
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
    }
}

/** Create the instance, adapter and device on the thread that renders.
 *
 * @param ctx The backend
 * @return 0 when the device exists, nonzero when it could not be created
 */
static int box_gpu_init(
    void *ctx)
{
    box_gpu_t *gpu = ctx;
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
    if (!gpu->device) {
        goto done;
    }

    box_gpu_blit_init(gpu);
    box_gpu_draw_init(gpu);
    box_gpu_scatter_init(gpu);
    ecs_map_init(&gpu->targets, NULL);
    ecs_map_init(&gpu->shaders, NULL);
    result = 0;
done:
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.ready = result == 0;
    gpu->stats.failed = result != 0;
    ecs_os_mutex_unlock(gpu->lock);

    return result;
}

/** Apply the target and shader resources of a packet and render its targets.
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

    box_gpu_upload(gpu, packet);
    box_gpu_scatter(gpu, packet);
    count = ecs_vec_count(&packet->targets);
    for (i = 0; i < count; i ++) {
        box_gpu_target_render(gpu, packet,
            ecs_vec_get_t(&packet->targets, box_render_target_t, i));
    }
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
    ecs_os_mutex_lock(gpu->lock);
    switch (stat) {
    case BOX_GPU_STAT_CREATED:
        gpu->stats.created ++;
        gpu->stats.targets ++;
        break;
    case BOX_GPU_STAT_CONFIGURED:
        gpu->stats.configured ++;
        break;
    case BOX_GPU_STAT_DESTROYED:
        gpu->stats.destroyed ++;
        gpu->stats.targets --;
        break;
    case BOX_GPU_STAT_FRAMES:
        gpu->stats.frames ++;
        break;
    case BOX_GPU_STAT_CAPTURES:
        gpu->stats.captures ++;
        break;
    case BOX_GPU_STAT_ERRORS:
        gpu->stats.errors ++;
        break;
    case BOX_GPU_STAT_SHADER_CREATED:
        gpu->stats.shaders ++;
        break;
    case BOX_GPU_STAT_SHADER_DESTROYED:
        gpu->stats.shaders --;
        break;
    case BOX_GPU_STAT_COMPILED:
        gpu->stats.compiles ++;
        break;
    case BOX_GPU_STAT_SHADER_ERROR:
        gpu->stats.shader_errors ++;
        break;
    case BOX_GPU_STAT_MESH_CREATED:
        gpu->stats.meshes ++;
        break;
    case BOX_GPU_STAT_MESH_DESTROYED:
        gpu->stats.meshes --;
        break;
    case BOX_GPU_STAT_BUFFER_CREATED:
        gpu->stats.buffers ++;
        break;
    case BOX_GPU_STAT_BUFFER_DESTROYED:
        gpu->stats.buffers --;
        break;
    case BOX_GPU_STAT_PASS:
        gpu->stats.passes ++;
        break;
    case BOX_GPU_STAT_DRAW:
        gpu->stats.draws ++;
        break;
    case BOX_GPU_STAT_SCATTER:
        gpu->stats.scatters ++;
        break;
    }

    ecs_os_mutex_unlock(gpu->lock);
}

box_render_backend_t box_render_gpu_backend(void)
{
    box_gpu_t *gpu = ecs_os_calloc_t(box_gpu_t);
    gpu->lock = ecs_os_mutex_new();
    ecs_vec_init_t(NULL, &gpu->results, box_render_shader_result_t, 0);
    box_render_backend_t result = {
        .ctx = gpu,
        .init = box_gpu_init,
        .render = box_gpu_render,
        .fini = box_gpu_fini,
        .free = box_gpu_free
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

bool box_render_gpu_shader_result(
    ecs_world_t *world,
    box_render_shader_result_t *result)
{
    (void)world;
    (void)result;

    return false;
}

#endif
