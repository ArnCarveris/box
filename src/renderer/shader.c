/**
 * @file renderer/shader.c
 * @brief WGSL sources the renderer compiles its pipelines from.
 *
 * Every shader arrives as a resource command with a handle and the source the
 * game side preprocessed, so the renderer never reads a file and never composes
 * a shader itself. The builtin slots are known up front, which is why the first
 * packet is also what brings the renderer up: it carries them all.
 *
 * A source that arrives again after the pipelines exist is compiled into a new
 * module and the pipelines of that slot are rebuilt; a module that does not
 * compile leaves the previous pipelines drawing and is reported once.
 */

#include "renderer_internal.h"
#include <stdio.h>

WGPUShaderModule box_shader_module(
    box_renderer_t *r,
    uint32_t handle)
{
    WGPUShaderModule result = NULL;
    const char *source = box_shader_source(r, handle);
    if (!source) {
        goto done;
    }

    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = source, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor desc = { .nextInChain = (WGPUChainedStruct*)&wgsl };
    r->device_error = false;
    result = wgpuDeviceCreateShaderModule(r->device, &desc);
    if (r->device_error && result) {
        wgpuShaderModuleRelease(result);
        result = NULL;
    }

done:
    return result;
}

const char* box_shader_source(
    box_renderer_t *r,
    uint32_t handle)
{
    const char *result = NULL;
    if (handle < BOX_SHADER_BUILTIN_MAX) {
        result = r->shader_src[handle];
    }

    return result;
}

bool box_shader_ready(
    box_renderer_t *r)
{
    bool result = true;
    uint32_t i;
    for (i = 0; i < BOX_SHADER_BUILTIN_MAX && result; i ++) {
        result = r->shader_src[i] != NULL;
    }

    return result;
}

/** Rebuild the pipelines a builtin shader slot feeds.
 *
 * The mesh slot keeps its previous module and pipelines when the new source
 * fails to compile, so a broken reload does not stop the frame. The other slots
 * own no state besides their pipelines, which their own module recreates.
 *
 * @param r The renderer
 * @param handle The shader slot that changed
 */
static void box_shader_rebuild(
    box_renderer_t *r,
    uint32_t handle)
{
    if (handle == BOX_SHADER_MESH) {
        if (!box_frame_rebuild(r) && !r->shader_failed) {
            fprintf(stderr, "box: mesh shader does not compile, keeping the previous one\n");
            r->shader_failed = true;
        }

        goto done;
    }

    WGPUShaderModule probe = box_shader_module(r, handle);
    if (!probe) {
        if (!r->shader_failed) {
            fprintf(stderr, "box: shader %u does not compile, keeping the previous one\n",
                handle);
            r->shader_failed = true;
        }

        goto done;
    }

    wgpuShaderModuleRelease(probe);
    box_view_fini(r);
    switch (handle) {
    case BOX_SHADER_CLUSTER: box_lights_fini(r); box_lights_init(r); break;
    case BOX_SHADER_CULL: box_cull_fini(r); box_cull_init(r); break;
    case BOX_SHADER_HIZ_COPY:
    case BOX_SHADER_HIZ_REDUCE:
    case BOX_SHADER_HIZ: box_hiz_fini(r); box_hiz_init(r); break;
    case BOX_SHADER_SHADOW: box_shadow_fini(r); box_shadow_init(r); break;
    case BOX_SHADER_ENV:
    case BOX_SHADER_ENV_FILTER: box_environment_fini(r); box_environment_init(r); break;
    case BOX_SHADER_BAKE_GRID:
    case BOX_SHADER_BAKE: box_bake_fini(r); box_bake_init(r); break;
    case BOX_SHADER_BLUR: box_transparent_fini(r); box_transparent_init(r); break;
    default: break;
    }

done:
    return;
}

void box_shader_apply(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->shader_create.count; i ++) {
        box_shader_cmd_t *cmd = box_vec_at(&p->shader_create, i);
        if (cmd->handle >= BOX_SHADER_BUILTIN_MAX) {
            free(cmd->source);
            cmd->source = NULL;
            continue;
        }

        free(r->shader_src[cmd->handle]);
        r->shader_src[cmd->handle] = cmd->source;
        cmd->source = NULL;
        if (r->initialized) {
            box_shader_rebuild(r, cmd->handle);
        }
    }
}

void box_shader_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < BOX_SHADER_BUILTIN_MAX; i ++) {
        free(r->shader_src[i]);
        r->shader_src[i] = NULL;
    }
}
