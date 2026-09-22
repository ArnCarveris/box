#include "renderer_internal.h"

#define BOX_EFFECT_TEXTURE_FORMAT WGPUTextureFormat_RGB9E5Ufloat

void box_effect_texture_init(
    box_renderer_t *r)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = { 1, 1, 1 },
        .format = BOX_EFFECT_TEXTURE_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->effect_texture_dummy = wgpuDeviceCreateTexture(r->device, &desc);
    WGPUTextureViewDescriptor view = {
        .format = BOX_EFFECT_TEXTURE_FORMAT,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1
    };
    r->effect_texture_dummy_view = wgpuTextureCreateView(
        r->effect_texture_dummy, &view);
}

static void box_effect_texture_upload(
    box_renderer_t *r,
    box_effect_texture_cmd_t *cmd)
{
    uint32_t handle = cmd->handle;
    if (handle >= BOX_EFFECT_TEXTURE_MAX) {
        return;
    }

    if (r->effect_texture_view[handle]) {
        wgpuTextureViewRelease(r->effect_texture_view[handle]);
        wgpuTextureRelease(r->effect_texture[handle]);
        r->effect_texture_view[handle] = NULL;
        r->effect_texture[handle] = NULL;
    }

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = { cmd->dim, cmd->dim, cmd->dim },
        .format = BOX_EFFECT_TEXTURE_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->effect_texture[handle] = wgpuDeviceCreateTexture(r->device, &desc);

    WGPUTexelCopyTextureInfo destination = {
        .texture = r->effect_texture[handle]
    };
    WGPUTexelCopyBufferLayout layout = {
        .bytesPerRow = cmd->dim * (uint32_t)sizeof(uint32_t),
        .rowsPerImage = cmd->dim
    };
    WGPUExtent3D extent = { cmd->dim, cmd->dim, cmd->dim };
    size_t bytes = (size_t)cmd->dim * cmd->dim * cmd->dim * sizeof(uint32_t);
    wgpuQueueWriteTexture(r->gpu_queue, &destination, cmd->texels, bytes,
        &layout, &extent);

    WGPUTextureViewDescriptor view = {
        .format = BOX_EFFECT_TEXTURE_FORMAT,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1
    };
    r->effect_texture_view[handle] = wgpuTextureCreateView(
        r->effect_texture[handle], &view);
}

void box_effect_texture_apply(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->effect_texture_create.count; i ++) {
        box_effect_texture_cmd_t *cmd = box_vec_at(
            &p->effect_texture_create, i);
        if (cmd->texels && cmd->dim) {
            box_effect_texture_upload(r, cmd);
        }

        free(cmd->texels);
        cmd->texels = NULL;
    }
}

WGPUTextureView box_effect_texture_view(
    box_renderer_t *r,
    uint32_t handle)
{
    if (handle < BOX_EFFECT_TEXTURE_MAX && r->effect_texture_view[handle]) {
        return r->effect_texture_view[handle];
    }

    return r->effect_texture_dummy_view;
}

void box_effect_texture_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < BOX_EFFECT_TEXTURE_MAX; i ++) {
        if (r->effect_texture_view[i]) {
            wgpuTextureViewRelease(r->effect_texture_view[i]);
        }
        if (r->effect_texture[i]) {
            wgpuTextureRelease(r->effect_texture[i]);
        }
        r->effect_texture_view[i] = NULL;
        r->effect_texture[i] = NULL;
    }

    if (r->effect_texture_dummy_view) {
        wgpuTextureViewRelease(r->effect_texture_dummy_view);
    }
    if (r->effect_texture_dummy) {
        wgpuTextureRelease(r->effect_texture_dummy);
    }
    r->effect_texture_dummy_view = NULL;
    r->effect_texture_dummy = NULL;
}
