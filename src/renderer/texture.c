/**
 * @file renderer/texture.c
 * @brief One 2D array texture per size class, grown by copying, and their samplers.
 *
 * A texture handle names a size class and a layer inside it, so an instanced
 * batch can bind textures of different materials without changing bind groups.
 * The mip chain is built on the CPU by the texture module and uploaded level by
 * level; destroy commands only return layers to the game side free list, the
 * array texture keeps the layer for the next texture of that class.
 */

#include "renderer_internal.h"

/** Format every array texture class uses, display encoding is decoded by the shader */
#define BOX_TEXTURE_FORMAT (WGPUTextureFormat_RGBA8Unorm)

/** Layers a class allocates the first time it is used */
#define BOX_TEXTURE_LAYERS_MIN (4)

/** Create an array texture of a size class.
 *
 * @param r The renderer
 * @param size Edge length in pixels
 * @param layers Array layers
 * @return The texture
 */
static WGPUTexture box_texture_array(
    box_renderer_t *r,
    uint32_t size,
    uint32_t layers)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { size, size, layers },
        .format = BOX_TEXTURE_FORMAT,
        .mipLevelCount = box_texture_mips(size),
        .sampleCount = 1
    };

    return wgpuDeviceCreateTexture(r->device, &desc);
}

/** Create the array view a shader samples a class through.
 *
 * @param texture The array texture
 * @param size Edge length in pixels
 * @param layers Array layers
 * @return The view
 */
static WGPUTextureView box_texture_view(
    WGPUTexture texture,
    uint32_t size,
    uint32_t layers)
{
    WGPUTextureViewDescriptor desc = {
        .format = BOX_TEXTURE_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .mipLevelCount = box_texture_mips(size),
        .arrayLayerCount = layers,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &desc);
}

/** Copy every mip level of the layers an array texture already holds.
 *
 * @param r The renderer
 * @param src The old array texture
 * @param dst The new array texture
 * @param size Edge length in pixels
 * @param layers Layers to copy
 */
static void box_texture_copy(
    box_renderer_t *r,
    WGPUTexture src,
    WGPUTexture dst,
    uint32_t size,
    uint32_t layers)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(r->device, NULL);
    uint32_t mips = box_texture_mips(size);
    uint32_t mip;
    for (mip = 0; mip < mips; mip ++) {
        WGPUTexelCopyTextureInfo from = { .texture = src, .mipLevel = mip };
        WGPUTexelCopyTextureInfo to = { .texture = dst, .mipLevel = mip };
        WGPUExtent3D extent = { size >> mip, size >> mip, layers };
        wgpuCommandEncoderCopyTextureToTexture(encoder, &from, &to, &extent);
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(r->gpu_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
}

/** Make sure the array texture of a class holds a number of layers.
 *
 * @param r The renderer
 * @param cls The size class
 * @param layers Layers the class must hold
 */
static void box_texture_grow(
    box_renderer_t *r,
    uint32_t cls,
    uint32_t layers)
{
    box_gpu_texclass_t *c = &r->texclass[cls];
    if (c->layers >= layers) {
        goto done;
    }

    uint32_t size = box_texture_class_size(cls);
    uint32_t grown = c->layers ? c->layers * 2 : BOX_TEXTURE_LAYERS_MIN;
    if (grown < layers) {
        grown = layers;
    }

    WGPUTexture texture = box_texture_array(r, size, grown);
    if (c->texture) {
        box_texture_copy(r, c->texture, texture, size, c->layers);
        wgpuTextureViewRelease(c->view);
        wgpuTextureRelease(c->texture);
    }

    c->texture = texture;
    c->view = box_texture_view(texture, size, grown);
    c->layers = grown;
    r->texture_dirty = true;
done:
    return;
}

/** Upload every mip level of one texture into its layer.
 *
 * @param r The renderer
 * @param cmd The create command
 */
static void box_texture_write(
    box_renderer_t *r,
    const box_texture_cmd_t *cmd)
{
    uint32_t cls = box_texture_class(cmd->handle);
    uint32_t layer = box_texture_layer(cmd->handle);
    box_texture_grow(r, cls, layer + 1);

    const uint8_t *src = cmd->pixels;
    uint32_t mip;
    for (mip = 0; mip < cmd->mips; mip ++) {
        uint32_t level = cmd->width >> mip;
        WGPUTexelCopyTextureInfo dst = {
            .texture = r->texclass[cls].texture,
            .mipLevel = mip,
            .origin = { 0, 0, layer }
        };
        WGPUTexelCopyBufferLayout layout = {
            .bytesPerRow = level * 4,
            .rowsPerImage = level
        };
        WGPUExtent3D extent = { level, level, 1 };
        wgpuQueueWriteTexture(r->gpu_queue, &dst, src, (size_t)level * level * 4,
            &layout, &extent);
        src += (size_t)level * level * 4;
    }
}

void box_texture_bind_layout(
    WGPUBindGroupLayoutEntry *out,
    uint32_t base)
{
    uint32_t cls;
    for (cls = 0; cls < BOX_TEXTURE_CLASSES; cls ++) {
        out[cls] = (WGPUBindGroupLayoutEntry){
            .binding = base + cls,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        };
        out[BOX_TEXTURE_CLASSES + cls] = (WGPUBindGroupLayoutEntry){
            .binding = base + BOX_TEXTURE_CLASSES + cls,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        };
    }
}

void box_texture_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base)
{
    uint32_t cls;
    for (cls = 0; cls < BOX_TEXTURE_CLASSES; cls ++) {
        out[cls] = (WGPUBindGroupEntry){
            .binding = base + cls,
            .textureView = r->texclass[cls].view ?
                r->texclass[cls].view : r->texture_dummy_view
        };
        out[BOX_TEXTURE_CLASSES + cls] = (WGPUBindGroupEntry){
            .binding = base + BOX_TEXTURE_CLASSES + cls,
            .sampler = r->texclass[cls].sampler
        };
    }
}

void box_texture_init(
    box_renderer_t *r)
{
    uint32_t anisotropy = r->desc.anisotropy ? r->desc.anisotropy : 1;
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_Repeat,
        .addressModeV = WGPUAddressMode_Repeat,
        .addressModeW = WGPUAddressMode_Repeat,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .maxAnisotropy = (uint16_t)anisotropy
    };

    uint32_t cls;
    for (cls = 0; cls < BOX_TEXTURE_CLASSES; cls ++) {
        sampler_desc.lodMaxClamp = (float)box_texture_mips(box_texture_class_size(cls));
        r->texclass[cls].sampler = wgpuDeviceCreateSampler(r->device, &sampler_desc);
    }

    WGPUTextureDescriptor dummy_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = BOX_TEXTURE_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->texture_dummy = wgpuDeviceCreateTexture(r->device, &dummy_desc);
    r->texture_dummy_view = box_texture_view(r->texture_dummy, 1, 1);

    r->texture_dirty = false;
}

void box_texture_apply(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->texture_create.count; i ++) {
        box_texture_cmd_t *cmd = box_vec_at(&p->texture_create, i);
        box_texture_write(r, cmd);
        free(cmd->pixels);
        cmd->pixels = NULL;
    }

    if (r->texture_dirty) {
        box_environment_rebuild(r);
        r->texture_dirty = false;
    }
}

void box_texture_fini(
    box_renderer_t *r)
{
    uint32_t cls;
    for (cls = 0; cls < BOX_TEXTURE_CLASSES; cls ++) {
        if (r->texclass[cls].view) wgpuTextureViewRelease(r->texclass[cls].view);
        if (r->texclass[cls].texture) wgpuTextureRelease(r->texclass[cls].texture);
        if (r->texclass[cls].sampler) wgpuSamplerRelease(r->texclass[cls].sampler);
        memset(&r->texclass[cls], 0, sizeof(box_gpu_texclass_t));
    }

    if (r->texture_dummy_view) wgpuTextureViewRelease(r->texture_dummy_view);
    if (r->texture_dummy) wgpuTextureRelease(r->texture_dummy);
    r->texture_dummy_view = NULL;
    r->texture_dummy = NULL;
}
