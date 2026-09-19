/**
 * @file renderer/modulator.c
 * @brief Uniform block holding the parameter block of every light modulator.
 *
 * The plugin functions themselves are composed into the mesh shader by the
 * shader module of the game side, so the renderer only keeps the parameters
 * they read, one sixteen float block per registration.
 */

#include "renderer_internal.h"

void box_modulators_init(
    box_renderer_t *r)
{
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_MODULATOR_UNIFORM_SIZE
    };
    r->modulator_buffer = wgpuDeviceCreateBuffer(r->device, &desc);
}

void box_modulators_apply(
    box_renderer_t *r,
    const box_packet_t *p)
{
    if (p->modulator_count) {
        wgpuQueueWriteBuffer(r->gpu_queue, r->modulator_buffer, 0, p->modulator_params,
            sizeof(p->modulator_params));
    }
}

void box_modulators_fini(
    box_renderer_t *r)
{
    if (r->modulator_buffer) {
        wgpuBufferRelease(r->modulator_buffer);
        r->modulator_buffer = NULL;
    }
}
