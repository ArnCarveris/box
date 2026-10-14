/**
 * @file render/gpu_shader.c
 * @brief Shader modules of the webgpu backend, compiled from preprocessed WGSL by handle.
 *
 * A module that fails to compile keeps the previous module of its handle; recompiling the
 * blit shader releases the blit pipelines so they are created again with the new module.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Outcome of the error scope around the creation of a module */
typedef struct box_gpu_compile_t {
    const char *label;  /**< Label of the shader */
    bool failed;        /**< The scope caught an error */
} box_gpu_compile_t;

/** Report an error the scope around a module creation caught.
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
    box_gpu_compile_t *compile = userdata1;
    int len = 0;
    (void)userdata2;
    if (status != WGPUPopErrorScopeStatus_Success || type == WGPUErrorType_NoError) {
        goto done;
    }

    if (message.data) {
        len = message.length == WGPU_STRLEN
            ? ecs_os_strlen(message.data)
            : (int)message.length;
    }

    compile->failed = true;
    ecs_err("shader '%s' failed to compile: %.*s", compile->label, len,
        message.data ? message.data : "");
done:
    return;
}

/** Compile a module from preprocessed code.
 *
 * @param gpu The backend
 * @param code The code
 * @param label The label of the module
 * @return The module, NULL when it failed to compile
 */
static WGPUShaderModule box_gpu_shader_compile(
    box_gpu_t *gpu,
    const char *code,
    const char *label)
{
    box_gpu_compile_t compile = { .label = label };
    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = code, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor desc = {
        .nextInChain = &wgsl.chain,
        .label = { .data = label, .length = WGPU_STRLEN }
    };
    wgpuDevicePushErrorScope(gpu->device, WGPUErrorFilter_Validation);
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(gpu->device, &desc);
    wgpuDevicePopErrorScope(gpu->device, (WGPUPopErrorScopeCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_gpu_shader_on_scope,
        .userdata1 = &compile
    });
    if (compile.failed || !result) {
        if (result) {
            wgpuShaderModuleRelease(result);
        }

        result = NULL;
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_ERROR);
    } else {
        box_gpu_count(gpu, BOX_GPU_STAT_COMPILED);
    }

    return result;
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
    box_gpu_shader_t *s = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_t, handle);
    if (s) {
        wgpuShaderModuleRelease(s->module);
        ecs_map_remove(&gpu->shaders, handle);
        ecs_os_free(s);
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_DESTROYED);
    }
}

void box_gpu_shader_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_shader_desc_t *desc)
{
    box_gpu_shader_t *s;
    if (op == BOX_RENDER_DESTROY) {
        box_gpu_shader_release(gpu, handle);
        goto done;
    }

    if (!desc || !desc->code_size || !desc->label_size) {
        goto done;
    }

    const char *code = (const char*)(desc + 1);
    WGPUShaderModule module = box_gpu_shader_compile(gpu, code, code + desc->code_size);
    if (!module) {
        goto done;
    }

    s = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_t, handle);
    if (s) {
        wgpuShaderModuleRelease(s->module);
    } else {
        s = ecs_os_calloc_t(box_gpu_shader_t);
        ecs_map_insert_ptr(&gpu->shaders, handle, s);
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_CREATED);
    }

    s->module = module;
    s->revision ++;
    if (handle == BOX_RENDER_SHADER_BLIT) {
        box_gpu_blit_reset(gpu);
    }

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
        box_gpu_shader_t *s = ecs_map_ptr(&it);
        wgpuShaderModuleRelease(s->module);
        ecs_os_free(s);
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_DESTROYED);
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
