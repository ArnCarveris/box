/**
 * @file renderer/lut.c
 * @brief Display transform lookup cube of the tone mapping pass.
 *
 * The cube arrives as one resource command the first time a pass selects the
 * operator and becomes a 3D texture of RGB9E5 texels, which is filterable, so
 * the pass reads it with the trilinear sampler every effect already binds. A
 * frame that never asked for one binds the single texel dummy instead.
 */

#include "renderer_internal.h"

/** Format the texels of a lookup cube are stored in */
#define BOX_LUT_FORMAT WGPUTextureFormat_RGB9E5Ufloat

void box_lut_init(
    box_renderer_t *r)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = { 1, 1, 1 },
        .format = BOX_LUT_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->lut_dummy = wgpuDeviceCreateTexture(r->device, &desc);
    WGPUTextureViewDescriptor view = {
        .format = BOX_LUT_FORMAT,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1
    };
    r->lut_dummy_view = wgpuTextureCreateView(r->lut_dummy, &view);
}

/** Replace the lookup cube of the renderer with the texels of one command.
 *
 * @param r The renderer
 * @param cmd The command
 */
static void box_lut_upload(
    box_renderer_t *r,
    box_lut_cmd_t *cmd)
{
    if (r->lut_view) {
        wgpuTextureViewRelease(r->lut_view);
        wgpuTextureRelease(r->lut_texture);
        r->lut_view = NULL;
        r->lut_texture = NULL;
    }

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = { cmd->dim, cmd->dim, cmd->dim },
        .format = BOX_LUT_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->lut_texture = wgpuDeviceCreateTexture(r->device, &desc);

    WGPUTexelCopyTextureInfo destination = { .texture = r->lut_texture };
    WGPUTexelCopyBufferLayout layout = {
        .bytesPerRow = cmd->dim * (uint32_t)sizeof(uint32_t),
        .rowsPerImage = cmd->dim
    };
    WGPUExtent3D extent = { cmd->dim, cmd->dim, cmd->dim };
    size_t bytes = (size_t)cmd->dim * cmd->dim * cmd->dim * sizeof(uint32_t);
    wgpuQueueWriteTexture(r->gpu_queue, &destination, cmd->texels, bytes, &layout, &extent);

    WGPUTextureViewDescriptor view = {
        .format = BOX_LUT_FORMAT,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1
    };
    r->lut_view = wgpuTextureCreateView(r->lut_texture, &view);
}

void box_lut_apply(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->lut_create.count; i ++) {
        box_lut_cmd_t *cmd = box_vec_at(&p->lut_create, i);
        if (!cmd->texels || !cmd->dim) {
            continue;
        }

        box_lut_upload(r, cmd);
        free(cmd->texels);
        cmd->texels = NULL;
    }
}

WGPUTextureView box_lut_view(
    box_renderer_t *r)
{
    return r->lut_view ? r->lut_view : r->lut_dummy_view;
}

void box_lut_fini(
    box_renderer_t *r)
{
    if (r->lut_view) wgpuTextureViewRelease(r->lut_view);
    if (r->lut_texture) wgpuTextureRelease(r->lut_texture);
    if (r->lut_dummy_view) wgpuTextureViewRelease(r->lut_dummy_view);
    if (r->lut_dummy) wgpuTextureRelease(r->lut_dummy);
    r->lut_view = NULL;
    r->lut_texture = NULL;
    r->lut_dummy_view = NULL;
    r->lut_dummy = NULL;
}
