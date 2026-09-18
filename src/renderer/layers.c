/**
 * @file renderer/layers.c
 * @brief Composes the mesh shader out of the registered material layers.
 *
 * A packet that carries a new registration list recreates the shader module and
 * the mesh pipelines; the previous ones keep drawing when the source is invalid.
 */

#include "renderer_internal.h"
#include <stdio.h>

/** Longest generated line of the apply function, the dispatch of one layer */
#define BOX_LAYER_LINE_MAX (128)

/** Append text to a growing buffer and advance its cursor.
 *
 * @param buffer The buffer, large enough for the text
 * @param cursor Bytes already written, raised by the length of the text
 * @param text The text to append
 */
static void box_layers_append(
    char *buffer,
    size_t *cursor,
    const char *text)
{
    size_t len = strlen(text);
    memcpy(buffer + *cursor, text, len);
    *cursor += len;
}

/** Build the WGSL of the mesh shader with the layers of the renderer in it.
 *
 * @param r The renderer
 * @return The source, released by the caller
 */
static char* box_layers_source(
    box_renderer_t *r)
{
    size_t size = strlen(box_mesh_shader_head) + strlen(box_mesh_shader_tail) + 64;
    uint32_t i;
    for (i = 0; i < r->layer_count; i ++) {
        size += strlen(r->layer_wgsl[i]) + 2 * BOX_LAYER_LINE_MAX;
    }

    char *result = malloc(size);
    size_t cursor = 0;
    char line[BOX_LAYER_LINE_MAX];
    box_layers_append(result, &cursor, box_mesh_shader_head);

    for (i = 0; i < r->layer_count; i ++) {
        snprintf(line, sizeof(line),
            "fn layer_%u(s: ptr<function, Surface>, p: LayerParams) {\n", i);
        box_layers_append(result, &cursor, line);
        box_layers_append(result, &cursor, r->layer_wgsl[i]);
        box_layers_append(result, &cursor, "}\n");
    }

    box_layers_append(result, &cursor, "fn apply_layers(s: ptr<function, Surface>, mask: u32) {\n");
    for (i = 0; i < r->layer_count; i ++) {
        snprintf(line, sizeof(line),
            "  if ((mask & %uu) != 0u) { layer_%u(s, layer_params.p[%u]); }\n", 1u << i, i, i);
        box_layers_append(result, &cursor, line);
    }

    box_layers_append(result, &cursor, "}\n");
    box_layers_append(result, &cursor, box_mesh_shader_tail);
    result[cursor] = '\0';

    return result;
}

WGPUShaderModule box_layers_shader(
    box_renderer_t *r)
{
    char *source = box_layers_source(r);
    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = source, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor desc = { .nextInChain = (WGPUChainedStruct*)&wgsl };
    r->device_error = false;
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(r->device, &desc);
    if (r->device_error && result) {
        wgpuShaderModuleRelease(result);
        result = NULL;
    }

    free(source);

    return result;
}

/** Recompose the shader and rebuild every mesh pipeline from it.
 *
 * @param r The renderer
 * @return True when the new source compiled into a pipeline
 */
static bool box_layers_rebuild(
    box_renderer_t *r)
{
    WGPUShaderModule module = box_layers_shader(r);
    bool result = module != NULL;
    if (!result) {
        goto done;
    }

    WGPUShaderModule previous = r->mesh_shader;
    r->mesh_shader = module;
    r->device_error = false;
    WGPURenderPipeline pipeline = box_frame_pipeline(r, sizeof(box_instance_t), 0,
        WGPUCullMode_Back, true, NULL);
    result = pipeline != NULL && !r->device_error;
    if (result) {
        box_transparent_reset(r);
        wgpuRenderPipelineRelease(r->pipeline);
        wgpuShaderModuleRelease(previous);
        r->pipeline = pipeline;
    } else {
        r->mesh_shader = previous;
        wgpuShaderModuleRelease(module);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

done:
    return result;
}

/** Replace the layer sources of the renderer with the ones a packet carries.
 *
 * @param r The renderer
 * @param p The packet, whose sources move into the renderer
 */
static void box_layers_store(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < r->layer_count; i ++) {
        free(r->layer_wgsl[i]);
        r->layer_wgsl[i] = NULL;
    }

    r->layer_count = p->layer_create.count < BOX_LAYER_MAX ?
        p->layer_create.count : BOX_LAYER_MAX;
    for (i = 0; i < r->layer_count; i ++) {
        box_layer_cmd_t *cmd = box_vec_at(&p->layer_create, i);
        r->layer_wgsl[i] = cmd->wgsl;
        cmd->wgsl = NULL;
    }
}

void box_layers_init(
    box_renderer_t *r)
{
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_LAYER_UNIFORM_SIZE
    };
    r->layer_buffer = wgpuDeviceCreateBuffer(r->device, &desc);
}

void box_layers_apply(
    box_renderer_t *r,
    const box_packet_t *p)
{
    if (p->layers_changed) {
        box_layers_store(r, p);
        if (!box_layers_rebuild(r) && !r->layer_failed) {
            fprintf(stderr, "box: material layers do not compile, keeping the previous shader\n");
            r->layer_failed = true;
        }
    }

    if (p->layer_count) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->layer_buffer, 0, p->layer_params,
            sizeof(p->layer_params));
    }
}

void box_layers_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < r->layer_count; i ++) {
        free(r->layer_wgsl[i]);
        r->layer_wgsl[i] = NULL;
    }

    r->layer_count = 0;
    if (r->layer_buffer) {
        wgpuBufferRelease(r->layer_buffer);
        r->layer_buffer = NULL;
    }
}
