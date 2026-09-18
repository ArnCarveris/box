/**
 * @file renderer/layers.c
 * @brief Uniform block holding the scene global parameters of the material layers.
 *
 * The bodies of the layers are composed into the mesh shader by the shader
 * module of the game side, so the renderer only keeps the parameters they read.
 */

#include "renderer_internal.h"

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
    if (p->layer_count) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->layer_buffer, 0, p->layer_params,
            sizeof(p->layer_params));
    }
}

void box_layers_fini(
    box_renderer_t *r)
{
    if (r->layer_buffer) {
        wgpuBufferRelease(r->layer_buffer);
        r->layer_buffer = NULL;
    }
}
