/**
 * @file render/gpu_shader.c
 * @brief Shader modules of the webgpu backend, compiled from preprocessed WGSL by handle.
 *
 * A module replaces the module of its handle once its outcome arrives (at once natively, from
 * the event loop in the browser) if no newer compile of the handle started; a module that
 * fails keeps the previous one. box_render_gpu_compile_delivery delays native outcomes.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

typedef struct box_gpu_compile_t box_gpu_compile_t;

/** A shader handle: the module others read and the compiles whose outcome has not arrived */
typedef struct box_gpu_shader_slot_t {
    box_gpu_shader_t shader;        /**< Module and revision, read as box_gpu_shader_t */
    box_gpu_compile_t *pending;     /**< Compiles waiting for their outcome, oldest first */
    int32_t requested;              /**< Code revision of the newest compile started */
    int32_t installed;              /**< Code revision of the installed module */
    char *label;                    /**< Label of the installed module, NULL when none */
} box_gpu_shader_slot_t;

/** A module whose compile outcome is awaited, owned by the callback once it waits */
struct box_gpu_compile_t {
    box_gpu_t *gpu;                 /**< The backend, NULL once the handle or backend is gone */
    box_gpu_shader_slot_t *slot;    /**< Slot the compile waits in, NULL until it waits */
    box_gpu_compile_t *next;        /**< Next compile waiting in the slot */
    WGPUShaderModule module;        /**< The module, NULL once it is installed or released */
    char *label;                    /**< Label of the shader */
    char *message;                  /**< Error text, NULL when none arrived */
    uint32_t handle;                /**< Handle of the shader */
    int32_t revision;               /**< Revision of the code */
    bool failed;                    /**< The module did not compile */
    bool arrived;                   /**< The outcome arrived */
    bool held;                      /**< The outcome arrived and waits for the next packet */
};

/** Get when compile outcomes apply.
 *
 * @param gpu The backend
 * @return The delivery box_render_gpu_compile_delivery set
 */
static box_render_compile_delivery_t box_gpu_shader_delivery(
    box_gpu_t *gpu)
{
    ecs_os_mutex_lock(gpu->lock);
    box_render_compile_delivery_t result = gpu->delivery;
    ecs_os_mutex_unlock(gpu->lock);

    return result;
}

/** Free a compile and the module it still holds.
 *
 * @param compile The compile
 */
static void box_gpu_compile_free(
    box_gpu_compile_t *compile)
{
    if (compile->module) {
        wgpuShaderModuleRelease(compile->module);
    }

    ecs_os_free(compile->label);
    ecs_os_free(compile->message);
    ecs_os_free(compile);
}

/** Get the slot of a handle, creating it without a module when it does not exist.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @return The slot
 */
static box_gpu_shader_slot_t* box_gpu_shader_slot(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_shader_slot_t *slot = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_slot_t,
        handle);
    if (!slot) {
        slot = ecs_os_calloc_t(box_gpu_shader_slot_t);
        ecs_map_insert_ptr(&gpu->shaders, handle, slot);
    }

    return slot;
}

/** Release the module of a slot and detach the compiles that wait in it.
 *
 * Detached compiles free themselves when their outcome arrives; held ones, whose outcome
 * already arrived, are freed here.
 *
 * @param gpu The backend
 * @param slot The slot, freed
 */
static void box_gpu_shader_slot_free(
    box_gpu_t *gpu,
    box_gpu_shader_slot_t *slot)
{
    box_gpu_compile_t *c = slot->pending;
    while (c) {
        box_gpu_compile_t *next = c->next;
        c->gpu = NULL;
        c->slot = NULL;
        c->next = NULL;
        if (c->held) {
            box_gpu_compile_free(c);
        }

        c = next;
    }

    if (slot->shader.module) {
        wgpuShaderModuleRelease(slot->shader.module);
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_DESTROYED);
    }

    ecs_os_free(slot->label);
    ecs_os_free(slot);
}

/** Make a compiled module the module of its handle, releasing the previous one.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @param module The module
 * @param revision Code revision of the module
 * @param label Label of the module
 */
static void box_gpu_shader_install(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule module,
    int32_t revision,
    const char *label)
{
    box_gpu_shader_slot_t *slot = box_gpu_shader_slot(gpu, handle);
    if (slot->shader.module) {
        wgpuShaderModuleRelease(slot->shader.module);
    } else {
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_CREATED);
    }

    slot->shader.module = module;
    slot->shader.revision ++;
    slot->installed = revision;
    ecs_os_free(slot->label);
    slot->label = ecs_os_strdup(label);
    if (handle == BOX_RENDER_SHADER_BLIT) {
        box_gpu_blit_reset(gpu);
    } else if (box_render_is_mesh(handle) || handle == BOX_RENDER_SHADER_SHAPE2D) {
        box_gpu_forward_reset(gpu, true);
    }
}

/** Apply the outcome of a compile: install its module or report its error.
 *
 * A module of a compile older than the newest one started for the handle is released, so a
 * late outcome never replaces newer code.
 *
 * @param compile The compile, its outcome arrived and its backend exists
 */
static void box_gpu_compile_finish(
    box_gpu_compile_t *compile)
{
    box_gpu_t *gpu = compile->gpu;
    box_gpu_shader_slot_t *slot = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_slot_t,
        compile->handle);
    if (compile->slot) {
        box_gpu_compile_t **ptr = &slot->pending;
        while (*ptr != compile) {
            ptr = &(*ptr)->next;
        }

        *ptr = compile->next;
        compile->slot = NULL;
    }

    if (compile->failed || !compile->module) {
        const char *message = compile->message
            ? compile->message : "shader module creation failed";
        ecs_err("shader '%s' failed to compile: %s", compile->label, message);
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_ERROR);
        box_gpu_shader_report(gpu, compile->handle, compile->revision, message,
            compile->label);
    } else {
        box_gpu_count(gpu, BOX_GPU_STAT_COMPILED);
        box_gpu_shader_report(gpu, compile->handle, compile->revision, NULL, compile->label);
        if (compile->revision == slot->requested) {
            box_gpu_shader_install(gpu, compile->handle, compile->module, compile->revision,
                compile->label);
            compile->module = NULL;
        }
    }

    if (slot && !slot->shader.module && !slot->pending) {
        ecs_map_remove(&gpu->shaders, compile->handle);
        ecs_os_free(slot->label);
        ecs_os_free(slot);
    }
}

/** Take the outcome of a compile; one that waits is finished and freed here.
 *
 * With a delayed box_render_gpu_compile_delivery an outcome arriving during the compile is held
 * until box_gpu_shader_deliver, the way the browser delivers it after the frame.
 *
 * @param compile The compile
 * @param failed Whether the module failed to compile
 * @param message Error text, NULL for none
 * @param len Length of the error text
 */
static void box_gpu_compile_arrive(
    box_gpu_compile_t *compile,
    bool failed,
    const char *message,
    int len)
{
    compile->failed = failed;
    if (failed && message) {
        compile->message = flecs_asprintf("%.*s", len, message);
    }

    if (!compile->slot && compile->gpu &&
        box_gpu_shader_delivery(compile->gpu) != BOX_RENDER_COMPILE_AT_ONCE)
    {
        compile->held = true;
        goto done;
    }

    compile->arrived = true;
    if (compile->slot || !compile->gpu) {
        if (compile->gpu) {
            box_gpu_compile_finish(compile);
            box_gpu_stats_publish(compile->gpu);
        }

        box_gpu_compile_free(compile);
    }

done:
    return;
}

#ifdef __EMSCRIPTEN__

/** Take the compilation messages of a module, which the browser produces asynchronously.
 *
 * The first error with its line becomes the outcome, formatted like a naga error so that
 * box_gpu_shader_report finds the line.
 *
 * @param status Whether the messages were produced
 * @param info The messages
 * @param userdata1 The box_gpu_compile_t
 * @param userdata2 Unused
 */
static void box_gpu_shader_on_info(
    WGPUCompilationInfoRequestStatus status,
    const WGPUCompilationInfo *info,
    void *userdata1,
    void *userdata2)
{
    box_gpu_compile_t *compile = userdata1;
    const WGPUCompilationMessage *error = NULL;
    char *message = NULL;
    bool failed = status != WGPUCompilationInfoRequestStatus_Success;
    size_t i;
    (void)userdata2;
    for (i = 0; !failed && info && i < info->messageCount; i ++) {
        if (info->messages[i].type == WGPUCompilationMessageType_Error) {
            error = &info->messages[i];
            failed = true;
        }
    }

    if (error) {
        int len = error->message.data
            ? (error->message.length == WGPU_STRLEN
                ? ecs_os_strlen(error->message.data) : (int)error->message.length)
            : 0;
        const char *text = error->message.data ? error->message.data : "";
        message = error->lineNum
            ? flecs_asprintf("wgsl:%u: error: %.*s", (uint32_t)error->lineNum, len, text)
            : flecs_asprintf("error: %.*s", len, text);
    }

    box_gpu_compile_arrive(compile, failed, message, message ? ecs_os_strlen(message) : 0);
    ecs_os_free(message);
}

/** Create the module of a compile and request its outcome from the browser.
 *
 * @param gpu The backend
 * @param compile The compile
 * @param desc The module
 */
static void box_gpu_compile_create(
    box_gpu_t *gpu,
    box_gpu_compile_t *compile,
    const WGPUShaderModuleDescriptor *desc)
{
    compile->module = wgpuDeviceCreateShaderModule(gpu->device, desc);
    if (!compile->module) {
        box_gpu_compile_arrive(compile, true, NULL, 0);
        goto done;
    }

    wgpuShaderModuleGetCompilationInfo(compile->module, (WGPUCompilationInfoCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_gpu_shader_on_info,
        .userdata1 = compile
    });
done:
    return;
}

#else

/** Create the module of a compile, whose errors wgpu-native reports at once on this thread.
 *
 * The errors are captured on the thread rather than with an error scope, which belongs to the
 * device and would also catch the errors of the pipelines the workers create meanwhile.
 *
 * @param gpu The backend
 * @param compile The compile
 * @param desc The module
 */
static void box_gpu_compile_create(
    box_gpu_t *gpu,
    box_gpu_compile_t *compile,
    const WGPUShaderModuleDescriptor *desc)
{
    box_gpu_error_capture_t capture = {0};
    box_gpu_error_capture_begin(&capture);
    compile->module = wgpuDeviceCreateShaderModule(gpu->device, desc);
    box_gpu_error_capture_end();
    box_gpu_compile_arrive(compile, capture.failed, capture.message,
        capture.message ? ecs_os_strlen(capture.message) : 0);
    ecs_os_free(capture.message);
}

#endif

/** Compile a module from preprocessed code; it replaces the module of its handle once its
 * outcome arrives, and a failed module is reported and released.
 *
 * @param gpu The backend
 * @param code The code
 * @param label The label of the module
 * @param handle Handle of the shader
 * @param revision Revision of the code
 */
static void box_gpu_shader_compile(
    box_gpu_t *gpu,
    const char *code,
    const char *label,
    uint32_t handle,
    int32_t revision)
{
    box_gpu_compile_t *compile = ecs_os_calloc_t(box_gpu_compile_t);
    box_gpu_shader_slot_t *slot = box_gpu_shader_slot(gpu, handle);
    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = code, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor desc = {
        .nextInChain = &wgsl.chain,
        .label = { .data = label, .length = WGPU_STRLEN }
    };
    compile->gpu = gpu;
    compile->label = ecs_os_strdup(label);
    compile->handle = handle;
    compile->revision = revision;
    slot->requested = revision;
    box_gpu_compile_create(gpu, compile, &desc);
    if (compile->arrived) {
        box_gpu_compile_finish(compile);
        box_gpu_compile_free(compile);
        goto done;
    }

    box_gpu_compile_t **tail = &slot->pending;
    while (*tail) {
        tail = &(*tail)->next;
    }

    *tail = compile;
    compile->slot = slot;
done:
    return;
}

void box_gpu_shader_deliver(
    box_gpu_t *gpu)
{
    box_render_compile_delivery_t delivery = box_gpu_shader_delivery(gpu);
    ecs_vec_t held;
    int32_t i, count;
    if (!ecs_map_is_init(&gpu->shaders) || delivery == BOX_RENDER_COMPILE_HOLD) {
        goto done;
    }

    ecs_vec_init_t(NULL, &held, box_gpu_compile_t*, 0);
    ecs_map_iter_t it = ecs_map_iter(&gpu->shaders);
    while (ecs_map_next(&it)) {
        box_gpu_shader_slot_t *slot = ecs_map_ptr(&it);
        box_gpu_compile_t *c;
        for (c = slot->pending; c; c = c->next) {
            if (c->held) {
                *ecs_vec_append_t(NULL, &held, box_gpu_compile_t*) = c;
            }
        }
    }

    count = ecs_vec_count(&held);
    for (i = 0; i < count; i ++) {
        int32_t at = delivery == BOX_RENDER_COMPILE_NEWEST_FIRST ? count - 1 - i : i;
        box_gpu_compile_t *c = *ecs_vec_get_t(&held, box_gpu_compile_t*, at);
        c->held = false;
        c->arrived = true;
        box_gpu_compile_finish(c);
        box_gpu_compile_free(c);
    }

    ecs_vec_fini_t(NULL, &held, box_gpu_compile_t*);
done:
    return;
}

void box_gpu_shader_report(
    box_gpu_t *gpu,
    uint32_t handle,
    int32_t revision,
    const char *message,
    const char *label)
{
    box_render_shader_result_t result = { .handle = handle, .revision = revision };
    if (message) {
        const char *at = strstr(message, "wgsl:");
        const char *text = strstr(message, "error: ");
        int32_t len;
        text = text ? text + 7 : message;
        len = (int32_t)strcspn(text, "\n");
        if (at) {
            result.message = flecs_asprintf("%s:%d: %.*s", label, atoi(at + 5), len, text);
        } else {
            result.message = flecs_asprintf("%s: %.*s", label, len, text);
        }
    }

    ecs_os_mutex_lock(gpu->lock);
    *ecs_vec_append_t(NULL, &gpu->results, box_render_shader_result_t) = result;
    ecs_os_mutex_unlock(gpu->lock);
}

/** Release a shader, its handle and the blit or forward pipelines made with it.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 */
static void box_gpu_shader_release(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_shader_slot_t *slot = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_slot_t,
        handle);
    if (slot) {
        ecs_map_remove(&gpu->shaders, handle);
        box_gpu_shader_slot_free(gpu, slot);
    }

    if (handle == BOX_RENDER_SHADER_BLIT) {
        box_gpu_blit_reset(gpu);
    } else if (box_render_is_mesh(handle) || handle == BOX_RENDER_SHADER_SHAPE2D) {
        box_gpu_forward_reset(gpu, false);
    }
}

void box_gpu_shader_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_shader_desc_t *desc)
{
    if (op == BOX_RENDER_DESTROY) {
        box_gpu_shader_release(gpu, handle);
        goto done;
    }

    if (!desc || !desc->code_size || !desc->label_size) {
        goto done;
    }

    if (handle == BOX_RENDER_SHADER_SHADOW_CLIP && !gpu->clip_distances) {
        goto done;
    }

    const char *code = (const char*)(desc + 1);
    box_gpu_shader_compile(gpu, code, code + desc->code_size, handle, desc->revision);
done:
    return;
}

WGPUShaderModule box_gpu_shader_get(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_shader_t *s = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_t, handle);

    return s ? s->module : NULL;
}

const char* box_gpu_shader_label(
    box_gpu_t *gpu,
    WGPUShaderModule module)
{
    const char *result = NULL;
    ecs_map_iter_t it = ecs_map_iter(&gpu->shaders);
    while (module && ecs_map_next(&it)) {
        box_gpu_shader_slot_t *slot = ecs_map_ptr(&it);
        if (slot->shader.module == module) {
            result = slot->label;
            break;
        }
    }

    return result;
}

void box_gpu_shader_pipeline_failed(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    const char *message)
{
    bool found = false;
    ecs_map_iter_t it = ecs_map_iter(&gpu->shaders);
    while (module && !found && ecs_map_next(&it)) {
        box_gpu_shader_slot_t *slot = ecs_map_ptr(&it);
        found = slot->shader.module == module;
        if (found) {
            box_gpu_shader_report(gpu, (uint32_t)ecs_map_key(&it), slot->installed, message,
                slot->label);
        }
    }
}

void box_gpu_shader_fini(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->shaders);
    while (ecs_map_next(&it)) {
        box_gpu_shader_slot_free(gpu, ecs_map_ptr(&it));
    }

    ecs_map_clear(&gpu->shaders);
}

void box_gpu_blit_reset(
    box_gpu_t *gpu)
{
    int32_t i;
    for (i = 0; i < gpu->blit_count; i ++) {
        if (gpu->blits[i].pipeline) {
            wgpuRenderPipelineRelease(gpu->blits[i].pipeline);
        }

        gpu->blits[i].pipeline = NULL;
    }

    gpu->blit_count = 0;
}

#endif
