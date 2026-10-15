/**
 * @file render/gpu_shader.c
 * @brief Shader modules of the webgpu backend, compiled from preprocessed WGSL by handle.
 *
 * A module replaces the module of its handle once its compile outcome arrives: right away
 * natively, from the event loop in the browser. A module that fails keeps the previous one.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

typedef struct box_gpu_compile_t box_gpu_compile_t;

/** A shader handle: the module others read and the compiles whose outcome has not arrived */
typedef struct box_gpu_shader_slot_t {
    box_gpu_shader_t shader;        /**< Module and revision, read as box_gpu_shader_t */
    box_gpu_compile_t *pending;     /**< Compiles waiting for their outcome, oldest first */
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
};

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
 * Detached compiles free themselves when their outcome arrives.
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
        c = next;
    }

    if (slot->shader.module) {
        wgpuShaderModuleRelease(slot->shader.module);
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_DESTROYED);
    }

    ecs_os_free(slot);
}

/** Make a compiled module the module of its handle, releasing the previous one.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @param module The module
 */
static void box_gpu_shader_install(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule module)
{
    box_gpu_shader_slot_t *slot = box_gpu_shader_slot(gpu, handle);
    if (slot->shader.module) {
        wgpuShaderModuleRelease(slot->shader.module);
    } else {
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_CREATED);
    }

    slot->shader.module = module;
    slot->shader.revision ++;
    if (handle == BOX_RENDER_SHADER_BLIT) {
        box_gpu_blit_reset(gpu);
    } else if (handle == BOX_RENDER_SHADER_MESH || handle == BOX_RENDER_SHADER_SHAPE2D) {
        box_gpu_forward_reset(gpu);
    }
}

/** Apply the outcome of a compile: install its module or report its error.
 *
 * @param compile The compile, its outcome arrived and its backend exists
 */
static void box_gpu_compile_finish(
    box_gpu_compile_t *compile)
{
    box_gpu_t *gpu = compile->gpu;
    box_gpu_shader_slot_t *slot = compile->slot;
    if (slot) {
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
        box_gpu_shader_install(gpu, compile->handle, compile->module);
        compile->module = NULL;
    }

    if (slot && !slot->shader.module && !slot->pending) {
        ecs_map_remove(&gpu->shaders, compile->handle);
        ecs_os_free(slot);
    }
}

/** Take the outcome of a compile; one that waits is finished and freed here.
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
    compile->arrived = true;
    compile->failed = failed;
    if (failed && message) {
        compile->message = flecs_asprintf("%.*s", len, message);
    }

    if (compile->slot || !compile->gpu) {
        if (compile->gpu) {
            box_gpu_compile_finish(compile);
        }

        box_gpu_compile_free(compile);
    }
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

/** Request the compile outcome of a module from the browser.
 *
 * @param gpu The backend
 * @param compile The compile
 */
static void box_gpu_compile_request(
    box_gpu_t *gpu,
    box_gpu_compile_t *compile)
{
    (void)gpu;
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

/** Take the error the scope around a module creation caught.
 *
 * @param status Whether the scope was popped
 * @param type Kind of error, NoError when there was none
 * @param message Error text
 * @param userdata1 The box_gpu_compile_t
 * @param userdata2 Unused
 */
static void box_gpu_shader_on_scope(
    WGPUPopErrorScopeStatus status,
    WGPUErrorType type,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    int len = 0;
    bool failed = status == WGPUPopErrorScopeStatus_Success && type != WGPUErrorType_NoError;
    (void)userdata2;
    if (failed && message.data) {
        len = message.length == WGPU_STRLEN
            ? ecs_os_strlen(message.data)
            : (int)message.length;
    }

    box_gpu_compile_arrive(userdata1, failed, failed ? message.data : NULL, len);
}

/** Pop the error scope around the creation of a module, which wgpu-native reports at once.
 *
 * @param gpu The backend
 * @param compile The compile
 */
static void box_gpu_compile_request(
    box_gpu_t *gpu,
    box_gpu_compile_t *compile)
{
    wgpuDevicePopErrorScope(gpu->device, (WGPUPopErrorScopeCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_gpu_shader_on_scope,
        .userdata1 = compile
    });
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
    box_gpu_shader_slot_t *slot;
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
#ifndef __EMSCRIPTEN__
    wgpuDevicePushErrorScope(gpu->device, WGPUErrorFilter_Validation);
#endif
    compile->module = wgpuDeviceCreateShaderModule(gpu->device, &desc);
    box_gpu_compile_request(gpu, compile);
    if (compile->arrived) {
        box_gpu_compile_finish(compile);
        box_gpu_compile_free(compile);
        goto done;
    }

    slot = box_gpu_shader_slot(gpu, handle);
    box_gpu_compile_t **tail = &slot->pending;
    while (*tail) {
        tail = &(*tail)->next;
    }

    *tail = compile;
    compile->slot = slot;
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

/** Release a shader and its handle.
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
