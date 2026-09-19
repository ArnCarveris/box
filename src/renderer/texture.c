/**
 * @file renderer/texture.c
 * @brief The pages of the texture pager, their array textures and their sampler.
 *
 * A texture handle names a size class and a layer inside it, so an instanced
 * batch can bind textures of different materials without changing bind groups.
 * An array texture holds at most the array layer limit of the device, so a size
 * class is a list of pages instead of one texture: the layer of a handle counts
 * across the pages, its high bits naming the page and its low bits the layer in
 * it. Every page is bound at once and the mesh shader picks one with the page
 * bits of its packed slot, which is why the pages of every class share one list
 * of bindings. The mip chain is built on the CPU by the texture module and
 * uploaded level by level; destroy commands only return layers to the game side
 * free list, the page keeps the layer for the next texture of its class.
 */

#include "renderer_internal.h"
#include <stdio.h>

/** Format every array texture class uses, display encoding is decoded by the shader */
#define BOX_TEXTURE_FORMAT (WGPUTextureFormat_RGBA8Unorm)

/** Layers a page allocates the first time it is used */
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

/** Create the array view a shader samples a page through.
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

/** Make sure the array texture of a page holds a number of layers.
 *
 * @param r The renderer
 * @param page The page
 * @param cls The size class of the page
 * @param layers Layers the page must hold
 */
static void box_texture_grow(
    box_renderer_t *r,
    box_gpu_texpage_t *page,
    uint32_t cls,
    uint32_t layers)
{
    if (page->layers >= layers) {
        goto done;
    }

    uint32_t size = box_texture_class_size(cls);
    uint32_t grown = page->layers ? page->layers * 2 : BOX_TEXTURE_LAYERS_MIN;
    if (grown < layers) {
        grown = layers;
    }

    if (grown > r->texture_max_layers) {
        grown = r->texture_max_layers;
    }

    WGPUTexture texture = box_texture_array(r, size, grown);
    if (page->texture) {
        box_texture_copy(r, page->texture, texture, size, page->layers);
        wgpuTextureViewRelease(page->view);
        wgpuTextureRelease(page->texture);
    }

    page->texture = texture;
    page->view = box_texture_view(texture, size, grown);
    page->layers = grown;
    page->cls = cls;
    page->used = true;
done:
    return;
}

/** Whether a layer of a page holds an image or the fallback color.
 *
 * @param page The page
 * @param layer The layer inside the page
 * @return true when the layer was written
 */
static bool box_texture_is_ready(
    const box_gpu_texpage_t *page,
    uint32_t layer)
{
    return (page->ready[layer >> 6] & ((uint64_t)1 << (layer & 63))) != 0;
}

/** Remember that a layer of a page holds an image or the fallback color.
 *
 * @param page The page
 * @param layer The layer inside the page
 */
static void box_texture_set_ready(
    box_gpu_texpage_t *page,
    uint32_t layer)
{
    page->ready[layer >> 6] |= (uint64_t)1 << (layer & 63);
}

/** Write one square RGBA8 level into a layer of a page.
 *
 * @param r The renderer
 * @param page The page
 * @param layer The layer inside the page
 * @param mip The mip level
 * @param level Edge length of the level in pixels
 * @param src The pixels of the level
 */
static void box_texture_write_level(
    box_renderer_t *r,
    box_gpu_texpage_t *page,
    uint32_t layer,
    uint32_t mip,
    uint32_t level,
    const uint8_t *src)
{
    WGPUTexelCopyTextureInfo dst = {
        .texture = page->texture,
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
}

/** Report once that the device holds fewer array layers than a page.
 *
 * @param r The renderer
 */
static void box_texture_report_limit(
    box_renderer_t *r)
{
    if (r->texture_limit_logged) {
        goto done;
    }

    r->texture_limit_logged = true;
    fprintf(stderr, "box: device allows %u texture array layers, a page holds %d: "
        "textures past the limit render with the color of their material\n",
        r->texture_max_layers, BOX_TEXTURE_PAGE_LAYERS);
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
    uint32_t index = box_texture_page(cmd->handle);
    uint32_t layer = box_texture_page_layer(cmd->handle);
    if (index >= BOX_TEXTURE_PAGES || layer >= r->texture_max_layers) {
        box_texture_report_limit(r);
        goto done;
    }

    box_gpu_texpage_t *page = &r->texpage[index];
    box_texture_grow(r, page, cls, layer + 1);
    box_texture_set_ready(page, layer);

    const uint8_t *src = cmd->pixels;
    uint32_t mip;
    for (mip = 0; mip < cmd->mips; mip ++) {
        uint32_t level = cmd->width >> mip;
        box_texture_write_level(r, page, layer, mip, level, src);
        src += (size_t)level * level * 4;
    }
done:
    return;
}

/** Fill the layers of a page no texture was uploaded into.
 *
 * A layer of a page a texture never claimed, or one an image has not landed in
 * yet, is filled with opaque white so a slot that names it samples a neutral
 * color instead of whatever the layer holds.
 *
 * @param r The renderer
 * @param page The page
 */
static void box_texture_fill(
    box_renderer_t *r,
    box_gpu_texpage_t *page)
{
    uint32_t size = box_texture_class_size(page->cls);
    uint32_t mips = box_texture_mips(size);
    uint8_t *fill = NULL;
    uint32_t layer;
    for (layer = 0; layer < page->layers; layer ++) {
        if (box_texture_is_ready(page, layer)) {
            continue;
        }

        if (!fill) {
            fill = malloc((size_t)size * size * 4);
            memset(fill, 0xFF, (size_t)size * size * 4);
        }

        uint32_t mip;
        for (mip = 0; mip < mips; mip ++) {
            box_texture_write_level(r, page, layer, mip, size >> mip, fill);
        }

        box_texture_set_ready(page, layer);
    }

    free(fill);
}

/** Hash the pages the bind group of the frame must name.
 *
 * @param r The renderer
 * @return The hash of the page layout
 */
static uint64_t box_texture_layout(
    const box_renderer_t *r)
{
    uint64_t result = 14695981039346656037u;
    uint32_t i;
    for (i = 0; i < BOX_TEXTURE_PAGES; i ++) {
        const box_gpu_texpage_t *page = &r->texpage[i];
        result = (result ^ page->cls) * 1099511628211u;
        result = (result ^ page->layers) * 1099511628211u;
        result = (result ^ (page->used ? 1u : 0u)) * 1099511628211u;
    }

    return result;
}

void box_texture_bind_layout(
    WGPUBindGroupLayoutEntry *out,
    uint32_t base)
{
    uint32_t page;
    for (page = 0; page < BOX_TEXTURE_PAGES; page ++) {
        out[page] = (WGPUBindGroupLayoutEntry){
            .binding = base + page,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        };
    }

    out[BOX_TEXTURE_PAGES] = (WGPUBindGroupLayoutEntry){
        .binding = base + BOX_TEXTURE_PAGES,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = { .type = WGPUSamplerBindingType_Filtering }
    };
}

void box_texture_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base)
{
    uint32_t page;
    for (page = 0; page < BOX_TEXTURE_PAGES; page ++) {
        out[page] = (WGPUBindGroupEntry){
            .binding = base + page,
            .textureView = r->texpage[page].view ?
                r->texpage[page].view : r->texture_dummy_view
        };
    }

    out[BOX_TEXTURE_PAGES] = (WGPUBindGroupEntry){
        .binding = base + BOX_TEXTURE_PAGES,
        .sampler = r->texture_sampler
    };
}

void box_texture_init(
    box_renderer_t *r)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    r->texture_max_layers = BOX_TEXTURE_PAGE_LAYERS;
    if (wgpuDeviceGetLimits(r->device, &limits) == WGPUStatus_Success) {
        if (limits.maxTextureArrayLayers < BOX_TEXTURE_PAGE_LAYERS) {
            r->texture_max_layers = limits.maxTextureArrayLayers;
            box_texture_report_limit(r);
        }
    }

    uint32_t anisotropy = r->desc.anisotropy ? r->desc.anisotropy : 1;
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_Repeat,
        .addressModeV = WGPUAddressMode_Repeat,
        .addressModeW = WGPUAddressMode_Repeat,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMaxClamp = (float)box_texture_mips(BOX_TEXTURE_MAX_SIZE),
        .maxAnisotropy = (uint16_t)anisotropy
    };
    r->texture_sampler = wgpuDeviceCreateSampler(r->device, &sampler_desc);

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
    r->texture_layout = box_texture_layout(r);
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

    for (i = 0; i < BOX_TEXTURE_PAGES; i ++) {
        if (r->texpage[i].used) {
            box_texture_fill(r, &r->texpage[i]);
        }
    }

    uint64_t layout = box_texture_layout(r);
    if (layout != r->texture_layout) {
        r->texture_layout = layout;
        box_environment_rebuild(r);
    }
}

void box_texture_fini(
    box_renderer_t *r)
{
    uint32_t page;
    for (page = 0; page < BOX_TEXTURE_PAGES; page ++) {
        if (r->texpage[page].view) wgpuTextureViewRelease(r->texpage[page].view);
        if (r->texpage[page].texture) wgpuTextureRelease(r->texpage[page].texture);
        memset(&r->texpage[page], 0, sizeof(box_gpu_texpage_t));
    }

    if (r->texture_sampler) wgpuSamplerRelease(r->texture_sampler);
    if (r->texture_dummy_view) wgpuTextureViewRelease(r->texture_dummy_view);
    if (r->texture_dummy) wgpuTextureRelease(r->texture_dummy);
    r->texture_sampler = NULL;
    r->texture_dummy_view = NULL;
    r->texture_dummy = NULL;
}
