/**
 * @file render/gpu_texture.c
 * @brief Textures, samplers and the texture layer bind groups of the webgpu backend.
 *
 * A texture is uploaded into its first mip level; the blit pipeline renders every further
 * level from the one above it, in linear light for the color layers. Batches bind their
 * layers as one cached bind group.
 *
 * A single channel texture is R8Unorm, sampled and averaged as stored by every layer. A
 * TextureBlend batch binds a texture array per map of its layers instead, shared by every blend
 * with the same sources and built again only when an image of a source changes.
 */

#include "gpu.h"
#include "gpu_texture_blend.h"
#include <stddef.h>

#ifdef BOX_RENDER_WGPU

/** Layers below this one sample color: albedo and emissive, decoded from sRGB */
#define BOX_GPU_COLOR_LAYERS (2)

#define BOX_GPU_MIP_PASSES (256)

/** Pixel of the default albedo, emissive and ORM layers: no occlusion, the material roughness
 * and metallic */
static const uint8_t box_gpu_white[4] = { 255, 255, 255, 255 };

/** Pixel of the default normal layer, a normal along the surface normal */
static const uint8_t box_gpu_flat[4] = { 128, 128, 255, 255 };

/** Color space the mip levels of a texture are averaged in */
typedef enum box_gpu_mip_space_t {
    BOX_GPU_MIP_UNKNOWN,    /**< No layer bound the texture yet */
    BOX_GPU_MIP_LINEAR,     /**< As stored, for the ORM, normal and distance layers */
    BOX_GPU_MIP_SRGB        /**< In linear light through the sRGB view, for the color layers */
} box_gpu_mip_space_t;

/** A texture of the backend with the color space of its mip levels */
typedef struct box_gpu_texture_entry_t {
    box_gpu_texture_t base;     /**< The texture, first so the entry is used as one */
    box_gpu_mip_space_t space;  /**< Space of the generated levels or of the layer that binds it */
    bool single;                /**< BOX_GPU_TEXTURE_SINGLE, whose levels average stored values */
    uint32_t serial;            /**< Serial of the image it holds, unique across textures, which
                                 *   the blend arrays and groups compare */
    WGPUTextureFormat format;   /**< Format of the texture of a texture graph, undefined for
                                 *   an uploaded image */
} box_gpu_texture_entry_t;

/** Count the mip levels of a full chain.
 *
 * @param width Width of the first level
 * @param height Height of the first level
 * @return The number of levels down to one pixel
 */
static uint32_t box_gpu_mip_count(
    uint32_t width,
    uint32_t height)
{
    uint32_t result = 1, size = width > height ? width : height;
    while (size > 1) {
        size >>= 1;
        result ++;
    }

    return result;
}

/** Create a view of a range of mip levels of a texture. An sRGB view leaves out the storage
 * usage of the texture, which sRGB formats do not have.
 *
 * @param texture The texture
 * @param format Format of the view
 * @param level First mip level
 * @param count Mip levels of the view
 * @return The view
 */
static WGPUTextureView box_gpu_texture_view(
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t level,
    uint32_t count)
{
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = format;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.baseMipLevel = level;
    desc.mipLevelCount = count;
    desc.baseArrayLayer = 0;
    desc.arrayLayerCount = 1;
    desc.aspect = WGPUTextureAspect_All;
    if (format == BOX_GPU_TEXTURE_SRGB) {
        desc.usage = wgpuTextureGetUsage(texture) & ~WGPUTextureUsage_StorageBinding;
    }

    return wgpuTextureCreateView(texture, &desc);
}

/** Create a texture and upload its first mip level.
 *
 * @param gpu The backend
 * @param width Width in pixels
 * @param height Height in pixels
 * @param pixels RGBA8 rows, or R8 rows of a single channel texture, the first row is the top
 * of the image
 * @param single Whether the texture has a single channel, BOX_GPU_TEXTURE_SINGLE
 * @return The texture
 */
static box_gpu_texture_t* box_gpu_texture_new(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    const uint8_t *pixels,
    bool single)
{
    box_gpu_texture_entry_t *entry = ecs_os_calloc_t(box_gpu_texture_entry_t);
    box_gpu_texture_t *result = &entry->base;
    WGPUTextureFormat format = single ? BOX_GPU_TEXTURE_SINGLE : BOX_GPU_TEXTURE_FORMAT;
    WGPUTextureFormat srgb = single ? BOX_GPU_TEXTURE_SINGLE : BOX_GPU_TEXTURE_SRGB;
    uint32_t texel = single ? 1 : 4;
    result->mips = box_gpu_mip_count(width, height);
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = format,
        .mipLevelCount = result->mips,
        .sampleCount = 1,
        .viewFormatCount = single ? 0 : 1,
        .viewFormats = single ? NULL : &srgb
    };
    entry->single = single;
    entry->serial = ++ gpu->texture_serial;
    if (single) {
        entry->space = BOX_GPU_MIP_LINEAR;
    }

    result->texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    result->linear = box_gpu_texture_view(result->texture, format, 0, result->mips);
    result->srgb = box_gpu_texture_view(result->texture, srgb, 0, result->mips);
    result->mipped = result->mips == 1;
    WGPUTexelCopyTextureInfo dst = {
        .texture = result->texture,
        .aspect = WGPUTextureAspect_All
    };
    WGPUTexelCopyBufferLayout layout = {
        .bytesPerRow = width * texel,
        .rowsPerImage = height
    };
    WGPUExtent3D extent = { width, height, 1 };
    wgpuQueueWriteTexture(gpu->queue, &dst, pixels, (size_t)width * height * texel, &layout,
        &extent);

    return result;
}

/** Release a texture and its views.
 *
 * @param tex The texture
 */
static void box_gpu_texture_free(
    box_gpu_texture_t *tex)
{
    wgpuTextureViewRelease(tex->linear);
    wgpuTextureViewRelease(tex->srgb);
    wgpuTextureRelease(tex->texture);
    ecs_os_free(tex);
}

void box_gpu_materials_clear(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->materials);
    while (ecs_map_next(&it)) {
        box_gpu_material_t *m = ecs_map_ptr(&it);
        while (m) {
            box_gpu_material_t *next = m->next;
            wgpuBindGroupRelease(m->group);
            gpu->forward_stamp ++;
            ecs_os_free(m);
            m = next;
        }
    }

    ecs_map_clear(&gpu->materials);
}

void box_gpu_texture_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[BOX_RENDER_TEXTURE_SLOTS + 3];
    WGPUBindGroupLayoutEntry kind = {
        .binding = BOX_GPU_DRAW_KIND_BINDING,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = true,
            .minBindingSize = 4 * sizeof(uint32_t)
        }
    };
    uint32_t kinds[BOX_GPU_DRAW_KINDS * BOX_GPU_DRAW_KIND_STRIDE / sizeof(uint32_t)] = {0};
    int32_t i;
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        entries[i] = (WGPUBindGroupLayoutEntry){
            .binding = (uint32_t)i,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        };
    }

    entries[BOX_RENDER_TEXTURE_SLOTS] = (WGPUBindGroupLayoutEntry){
        .binding = BOX_RENDER_TEXTURE_SLOTS,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = { .type = WGPUSamplerBindingType_Filtering }
    };
    entries[BOX_RENDER_TEXTURE_SLOTS + 1] = kind;
    entries[BOX_RENDER_TEXTURE_SLOTS + 2] = (WGPUBindGroupLayoutEntry){
        .binding = BOX_GPU_MATERIAL_FOAM_BINDING,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D
        }
    };
    WGPUBindGroupLayoutDescriptor layout = {
        .entryCount = BOX_RENDER_TEXTURE_SLOTS + 3,
        .entries = entries
    };
    gpu->material_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    for (i = 0; i < BOX_GPU_DRAW_KINDS; i ++) {
        uint32_t *k = &kinds[i * BOX_GPU_DRAW_KIND_STRIDE / sizeof(uint32_t)];
        k[0] = (uint32_t)i & 15u;
        k[1] = ((uint32_t)i >> 4) & 1u;
        k[2] = ((uint32_t)i >> 5) & 1u;
    }

    gpu->draw_kinds = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(kinds)
    });
    wgpuQueueWriteBuffer(gpu->queue, gpu->draw_kinds, 0, kinds, sizeof(kinds));
    WGPUBindGroupLayoutEntry blend[BOX_GPU_BLEND_ENTRIES] = {
        {
            .binding = BOX_GPU_BLEND_EMISSIVE,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = BOX_GPU_BLEND_SAMPLER,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = BOX_GPU_BLEND_MASK,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = BOX_GPU_BLEND_UNIFORMS,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_blend_uniforms_t)
            }
        }
    };
    blend[BOX_GPU_BLEND_ENTRIES - 1] = kind;
    blend[BOX_GPU_BLEND_ENTRIES - 2] = (WGPUBindGroupLayoutEntry){
        .binding = BOX_GPU_BLEND_BAKE,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2DArray
        }
    };
    for (i = 0; i < BOX_RENDER_BLEND_SOURCES; i ++) {
        blend[4 + i] = (WGPUBindGroupLayoutEntry){
            .binding = i == BOX_RENDER_BLEND_HEIGHT
                ? BOX_GPU_BLEND_HEIGHTS : BOX_GPU_BLEND_ARRAYS + (uint32_t)i,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        };
    }

    gpu->blend_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = BOX_GPU_BLEND_ENTRIES,
            .entries = blend
        });
    gpu->white = box_gpu_texture_new(gpu, 1, 1, box_gpu_white, false);
    WGPUTextureViewDescriptor heights = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    heights.format = BOX_GPU_TEXTURE_FORMAT;
    heights.dimension = WGPUTextureViewDimension_2DArray;
    heights.baseMipLevel = 0;
    heights.mipLevelCount = 1;
    heights.baseArrayLayer = 0;
    heights.arrayLayerCount = 1;
    heights.aspect = WGPUTextureAspect_All;
    gpu->blend_flat_heights = wgpuTextureCreateView(gpu->white->texture, &heights);
    gpu->flat = box_gpu_texture_new(gpu, 1, 1, box_gpu_flat, false);
    gpu->no_cookie = box_gpu_texture_new(gpu, 1, 1, box_gpu_white, true);
    ecs_map_init(&gpu->textures, NULL);
    ecs_map_init(&gpu->materials, NULL);
    ecs_vec_init_t(NULL, &gpu->mips, uint32_t, 0);
    ecs_vec_init_t(NULL, &gpu->blend_hints, box_gpu_blend_group_t*, 0);
    ecs_vec_init_t(NULL, &gpu->layer_hints, box_gpu_layer_group_t*, 0);
    ecs_vec_init_t(NULL, &gpu->forward_memo, box_gpu_forward_memo_t, 0);
}

void box_gpu_texture_fini(
    box_gpu_t *gpu)
{
    int32_t w, a;
    box_gpu_materials_clear(gpu);
    box_gpu_blends_clear(gpu);
    box_gpu_layer_layouts_fini(gpu);
    ecs_map_fini(&gpu->materials);
    ecs_map_iter_t it = ecs_map_iter(&gpu->textures);
    while (ecs_map_next(&it)) {
        box_gpu_texture_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&gpu->textures);
    ecs_vec_fini_t(NULL, &gpu->mips, uint32_t);
    ecs_vec_fini_t(NULL, &gpu->blend_hints, box_gpu_blend_group_t*);
    ecs_vec_fini_t(NULL, &gpu->layer_hints, box_gpu_layer_group_t*);
    ecs_vec_fini_t(NULL, &gpu->forward_memo, box_gpu_forward_memo_t);
    for (w = 0; w < BOX_GPU_WRAPS; w ++) {
        for (a = 0; a < BOX_GPU_ANISOTROPY_LEVELS; a ++) {
            if (gpu->samplers[w][a]) {
                wgpuSamplerRelease(gpu->samplers[w][a]);
                gpu->samplers[w][a] = NULL;
            }
        }
    }

    if (gpu->blend_flat_heights) {
        wgpuTextureViewRelease(gpu->blend_flat_heights);
        gpu->blend_flat_heights = NULL;
    }

    if (gpu->white) {
        box_gpu_texture_free(gpu->white);
        gpu->white = NULL;
    }

    if (gpu->flat) {
        box_gpu_texture_free(gpu->flat);
        gpu->flat = NULL;
    }

    if (gpu->no_cookie) {
        box_gpu_texture_free(gpu->no_cookie);
        gpu->no_cookie = NULL;
    }

    if (gpu->material_layout) {
        wgpuBindGroupLayoutRelease(gpu->material_layout);
        gpu->material_layout = NULL;
    }

    if (gpu->draw_kinds) {
        wgpuBufferRelease(gpu->draw_kinds);
        gpu->draw_kinds = NULL;
    }

    if (gpu->blend_layout) {
        wgpuBindGroupLayoutRelease(gpu->blend_layout);
        gpu->blend_layout = NULL;
    }
}

/** Create a texture with one mip level and no pixels, which the backend clears to zero.
 *
 * @param gpu The backend
 * @param width Width in pixels
 * @param height Height in pixels
 * @return The texture
 */
static box_gpu_texture_t* box_gpu_texture_single(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height)
{
    box_gpu_texture_entry_t *entry = ecs_os_calloc_t(box_gpu_texture_entry_t);
    box_gpu_texture_t *result = &entry->base;
    WGPUTextureFormat srgb = BOX_GPU_TEXTURE_SRGB;
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = BOX_GPU_TEXTURE_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1,
        .viewFormatCount = 1,
        .viewFormats = &srgb
    };
    entry->space = BOX_GPU_MIP_LINEAR;
    entry->serial = ++ gpu->texture_serial;
    result->mips = 1;
    result->mipped = true;
    result->texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    result->linear = box_gpu_texture_view(result->texture, BOX_GPU_TEXTURE_FORMAT, 0, 1);
    result->srgb = box_gpu_texture_view(result->texture, srgb, 0, 1);

    return result;
}

/** Write a region of a texture with one mip level, creating the texture when the handle has none
 * of the size of the update.
 *
 * @param gpu The backend
 * @param handle Handle of the texture
 * @param region The region, followed by its pixels
 */
static void box_gpu_texture_update(
    box_gpu_t *gpu,
    uint32_t handle,
    const box_render_texture_region_t *region)
{
    box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle);
    uint32_t width = region->texture.width, height = region->texture.height;
    if (!width || !height || !region->width || !region->height ||
        region->x + region->width > width || region->y + region->height > height)
    {
        goto done;
    }

    if (tex && (tex->mips != 1 || wgpuTextureGetWidth(tex->texture) != width ||
        wgpuTextureGetHeight(tex->texture) != height))
    {
        box_gpu_materials_clear(gpu);
        ecs_map_remove(&gpu->textures, handle);
        box_gpu_texture_free(tex);
        tex = NULL;
    }

    if (!tex) {
        box_gpu_materials_clear(gpu);
        tex = box_gpu_texture_single(gpu, width, height);
        ecs_map_insert_ptr(&gpu->textures, handle, tex);
    }

    WGPUTexelCopyTextureInfo dst = {
        .texture = tex->texture,
        .origin = { region->x, region->y, 0 },
        .aspect = WGPUTextureAspect_All
    };
    WGPUTexelCopyBufferLayout layout = {
        .bytesPerRow = region->width * 4,
        .rowsPerImage = region->height
    };
    WGPUExtent3D extent = { region->width, region->height, 1 };
    wgpuQueueWriteTexture(gpu->queue, &dst, region + 1,
        (size_t)region->width * region->height * 4, &layout, &extent);
    ((box_gpu_texture_entry_t*)tex)->serial = ++ gpu->texture_serial;
done:
    return;
}

void box_gpu_texture_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_texture_desc_t *desc)
{
    if (op == BOX_RENDER_UPDATE && desc) {
        box_gpu_texture_update(gpu, handle, (const box_render_texture_region_t*)desc);
        goto done;
    }

    box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle);
    box_gpu_mip_space_t space = BOX_GPU_MIP_UNKNOWN;
    box_gpu_materials_clear(gpu);
    if (tex) {
        space = ((box_gpu_texture_entry_t*)tex)->space;
        ecs_map_remove(&gpu->textures, handle);
        box_gpu_texture_free(tex);
        gpu->texture_serial ++;
    }

    if (op == BOX_RENDER_DESTROY || !desc || !desc->width || !desc->height) {
        goto done;
    }

    tex = box_gpu_texture_new(gpu, desc->width, desc->height, (const uint8_t*)(desc + 1),
        desc->single_channel);
    if (!desc->single_channel) {
        ((box_gpu_texture_entry_t*)tex)->space = space;
    }

    ecs_map_insert_ptr(&gpu->textures, handle, tex);
    if (!tex->mipped) {
        *ecs_vec_append_t(NULL, &gpu->mips, uint32_t) = handle;
    }

done:
    return;
}

box_gpu_texture_t* box_gpu_texture_output(
    box_gpu_t *gpu,
    uint32_t handle,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format,
    bool color)
{
    box_gpu_texture_t *result = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle);
    box_gpu_texture_entry_t *entry = (box_gpu_texture_entry_t*)result;
    box_gpu_mip_space_t space = BOX_GPU_MIP_UNKNOWN;
    bool rgba8 = format == BOX_GPU_TEXTURE_FORMAT;
    WGPUTextureFormat srgb = rgba8 ? BOX_GPU_TEXTURE_SRGB : format;
    if (result && entry->format == format && wgpuTextureGetWidth(result->texture) == width &&
        wgpuTextureGetHeight(result->texture) == height)
    {
        goto done;
    }

    box_gpu_materials_clear(gpu);
    if (result) {
        space = entry->space;
        ecs_map_remove(&gpu->textures, handle);
        box_gpu_texture_free(result);
    }

    entry = ecs_os_calloc_t(box_gpu_texture_entry_t);
    result = &entry->base;
    result->mips = format == WGPUTextureFormat_R32Float ||
        format == WGPUTextureFormat_RGBA32Float ? 1 : box_gpu_mip_count(width, height);
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment |
            (format != BOX_GPU_TEXTURE_SINGLE ? WGPUTextureUsage_StorageBinding : 0),
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = format,
        .mipLevelCount = result->mips,
        .sampleCount = 1,
        .viewFormatCount = rgba8 ? 1 : 0,
        .viewFormats = rgba8 ? &srgb : NULL
    };
    entry->format = format;
    entry->single = format == BOX_GPU_TEXTURE_SINGLE;
    entry->serial = ++ gpu->texture_serial;
    entry->space = entry->single || !rgba8 ? BOX_GPU_MIP_LINEAR
        : color ? BOX_GPU_MIP_SRGB : space;
    result->texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    result->linear = box_gpu_texture_view(result->texture, format, 0, result->mips);
    result->srgb = box_gpu_texture_view(result->texture, srgb, 0, result->mips);
    result->mipped = true;
    ecs_map_insert_ptr(&gpu->textures, handle, result);
done:
    return result;
}

void box_gpu_texture_output_written(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle);
    int32_t i;
    if (!tex) {
        goto done;
    }

    ((box_gpu_texture_entry_t*)tex)->serial = ++ gpu->texture_serial;
    if (tex->mips == 1 || !tex->mipped) {
        goto done;
    }

    tex->mipped = false;
    for (i = 0; i < ecs_vec_count(&gpu->mips); i ++) {
        if (*ecs_vec_get_t(&gpu->mips, uint32_t, i) == handle) {
            goto done;
        }
    }

    *ecs_vec_append_t(NULL, &gpu->mips, uint32_t) = handle;
done:
    return;
}

/** Test whether a texture can be sampled with filtering, which a float32 texture of a texture
 * graph (GraphFloat, GraphFloat4) can not on the web.
 *
 * @param tex The texture
 * @return True when it is filterable
 */
static bool box_gpu_texture_filterable(
    const box_gpu_texture_t *tex)
{
    WGPUTextureFormat format = ((const box_gpu_texture_entry_t*)tex)->format;

    return format != WGPUTextureFormat_R32Float && format != WGPUTextureFormat_RGBA32Float;
}

uint32_t box_gpu_texture_serial(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_texture_t *tex = handle && !(handle & BOX_RENDER_TEXTURE_TARGET)
        ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle)
        : NULL;

    return tex ? ((box_gpu_texture_entry_t*)tex)->serial : 0;
}

WGPUTextureView box_gpu_texture_sampled_view(
    box_gpu_t *gpu,
    uint32_t handle,
    bool linear)
{
    box_gpu_texture_t *tex = handle && !(handle & BOX_RENDER_TEXTURE_TARGET)
        ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle)
        : NULL;
    box_gpu_target_t *target = handle & BOX_RENDER_TEXTURE_TARGET
        ? ecs_map_get_deref(&gpu->targets, box_gpu_target_t, handle & ~BOX_RENDER_TEXTURE_TARGET)
        : NULL;
    WGPUTextureView result = NULL;
    if (target) {
        result = target->output_view;
    } else if (tex) {
        result = linear ? tex->linear : tex->srgb;
    }

    return result;
}

static void box_gpu_texture_blit(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUTextureView src,
    WGPUTextureView dst);

bool box_gpu_texture_output_blit(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    uint32_t handle,
    WGPUTextureView src)
{
    box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle);
    box_gpu_texture_entry_t *entry = (box_gpu_texture_entry_t*)tex;
    WGPURenderPipeline pipeline = tex ? box_gpu_blit_pipeline(gpu, entry->format) : NULL;
    if (pipeline) {
        WGPUTextureView dst = box_gpu_texture_view(tex->texture, entry->format, 0, 1);
        box_gpu_texture_blit(gpu, encoder, pipeline, src, dst);
        wgpuTextureViewRelease(dst);
    }

    return pipeline != NULL;
}

/** Record a pass that renders a view into another through the blit pipeline: a full screen
 * triangle that samples the source with the linear sampler of the blit.
 *
 * @param gpu The backend
 * @param encoder The encoder the pass is recorded in
 * @param pipeline The blit pipeline of the format of the destination
 * @param src The view sampled
 * @param dst The view rendered into, one level of one layer
 */
static void box_gpu_texture_blit(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUTextureView src,
    WGPUTextureView dst)
{
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .textureView = src },
        { .binding = 1, .sampler = gpu->sampler }
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = gpu->blit_layout,
        .entryCount = 2,
        .entries = entries
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &group_desc);
    WGPURenderPassColorAttachment attachment = {
        .view = dst,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment
    };
    WGPURenderPassEncoder pass = box_gpu_profile_render_pass(gpu, encoder, &pass_desc,
        "texture mips");
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
}

/** Render the mip levels of a texture, each from the level above it.
 *
 * The sRGB space samples and writes the levels through the sRGB view, so the filter averages
 * linear light and the result is encoded again; the linear space averages the stored values.
 * A single channel texture always averages its stored values, in its R8 format.
 *
 * @param gpu The backend
 * @param tex The texture
 * @param space BOX_GPU_MIP_SRGB or BOX_GPU_MIP_LINEAR
 * @param encoder The encoder the passes are recorded in
 */
static void box_gpu_texture_mip_chain(
    box_gpu_t *gpu,
    box_gpu_texture_t *tex,
    box_gpu_mip_space_t space,
    WGPUCommandEncoder encoder)
{
    if (((box_gpu_texture_entry_t*)tex)->single) {
        space = BOX_GPU_MIP_LINEAR;
    }

    WGPUTextureFormat stored = ((box_gpu_texture_entry_t*)tex)->format;
    WGPUTextureFormat format = ((box_gpu_texture_entry_t*)tex)->single
        ? BOX_GPU_TEXTURE_SINGLE
        : stored && stored != BOX_GPU_TEXTURE_FORMAT ? stored
        : space == BOX_GPU_MIP_SRGB ? BOX_GPU_TEXTURE_SRGB : BOX_GPU_TEXTURE_FORMAT;
    WGPURenderPipeline pipeline = box_gpu_blit_pipeline(gpu, format);
    uint32_t level;
    for (level = 1; pipeline && level < tex->mips; level ++) {
        WGPUTextureView src = box_gpu_texture_view(tex->texture, format, level - 1, 1);
        WGPUTextureView dst = box_gpu_texture_view(tex->texture, format, level, 1);
        box_gpu_texture_blit(gpu, encoder, pipeline, src, dst);
        wgpuTextureViewRelease(src);
        wgpuTextureViewRelease(dst);
    }

    if (pipeline) {
        tex->mipped = true;
        ((box_gpu_texture_entry_t*)tex)->space = space;
    }
}

static void box_gpu_blends_prepare(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

static WGPUBindGroup box_gpu_blend_group(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b);

/** Mark the queued textures that a batch of the packet binds in a color layer.
 *
 * @param gpu The backend
 * @param packet The packet whose batches the frame draws
 */
static void box_gpu_texture_mips_usage(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, slot, count = ecs_vec_count(&packet->batches);
    for (i = 0; i < count; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        for (slot = 0; slot < BOX_GPU_COLOR_LAYERS; slot ++) {
            uint32_t handle = b->textures[slot];
            box_gpu_texture_t *tex = handle && !(handle & BOX_RENDER_TEXTURE_TARGET)
                ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle)
                : NULL;
            if (tex && !tex->mipped && !((box_gpu_texture_entry_t*)tex)->single) {
                ((box_gpu_texture_entry_t*)tex)->space = BOX_GPU_MIP_SRGB;
            }
        }

        const box_render_texture_blend_t *r = box_gpu_texture_blends(b)
            ? box_render_packet_data(packet, b->blend_layers) : NULL;
        for (slot = 0; r && slot < (int32_t)r->count && slot < BOX_RENDER_BLEND_SLICES; slot ++) {
            uint32_t handle = r->layers[slot][BOX_RENDER_BLEND_ALBEDO];
            box_gpu_texture_t *tex = handle && !(handle & BOX_RENDER_TEXTURE_TARGET)
                ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle)
                : NULL;
            if (tex && !tex->mipped && !((box_gpu_texture_entry_t*)tex)->single) {
                ((box_gpu_texture_entry_t*)tex)->space = BOX_GPU_MIP_SRGB;
            }
        }
    }
}

void box_gpu_texture_mips(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, kept = 0, count = ecs_vec_count(&gpu->mips);
    if (!count) {
        goto blends;
    }

    box_gpu_texture_mips_usage(gpu, packet);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    uint32_t passes = 0;
    for (i = 0; i < count; i ++) {
        uint32_t handle = *ecs_vec_get_t(&gpu->mips, uint32_t, i);
        box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle);
        box_gpu_texture_entry_t *entry = (box_gpu_texture_entry_t*)tex;
        if (!tex || tex->mipped) {
            continue;
        }

        if (entry->space == BOX_GPU_MIP_UNKNOWN) {
            entry->space = BOX_GPU_MIP_LINEAR;
        }

        if (passes && passes + tex->mips > BOX_GPU_MIP_PASSES) {
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
            wgpuQueueSubmit(gpu->queue, 1, &commands);
            wgpuCommandBufferRelease(commands);
            wgpuCommandEncoderRelease(encoder);
            encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
            passes = 0;
        }

        passes += tex->mips;
        box_gpu_texture_mip_chain(gpu, tex, entry->space, encoder);
        if (!tex->mipped) {
            *ecs_vec_get_t(&gpu->mips, uint32_t, kept ++) = handle;
        }
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    ecs_vec_set_count_t(NULL, &gpu->mips, uint32_t, kept);
blends:
    box_gpu_blends_prepare(gpu, packet);
}

WGPUSampler box_gpu_texture_sampler(
    box_gpu_t *gpu,
    uint32_t wrap,
    uint32_t anisotropy)
{
    static const WGPUAddressMode modes[BOX_GPU_WRAPS] = {
        WGPUAddressMode_Repeat, WGPUAddressMode_ClampToEdge, WGPUAddressMode_MirrorRepeat
    };
    uint32_t level = 0;
    if (wrap >= BOX_GPU_WRAPS) {
        wrap = WrapRepeat;
    }

    while (level + 1 < BOX_GPU_ANISOTROPY_LEVELS && (2u << level) <= anisotropy) {
        level ++;
    }

    WGPUSampler *result = &gpu->samplers[wrap][level];
    if (!*result) {
        WGPUSamplerDescriptor desc = {
            .addressModeU = modes[wrap],
            .addressModeV = modes[wrap],
            .addressModeW = modes[wrap],
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 32,
            .maxAnisotropy = (uint16_t)(1u << level)
        };
        *result = wgpuDeviceCreateSampler(gpu->device, &desc);
    }

    return *result;
}

/** Hash the key of a texture layer bind group.
 *
 * @param textures Texture handles of the layers, then of the foam texture
 * @param wrap The TextureWrap
 * @param anisotropy Anisotropic samples
 * @return The hash
 */
static uint64_t box_gpu_material_hash(
    const uint32_t *textures,
    uint32_t wrap,
    uint32_t anisotropy)
{
    uint64_t h = ((uint64_t)wrap * 32 + anisotropy + 1) * 0x9E3779B97F4A7C15ull;
    int32_t i;
    for (i = 0; i < BOX_GPU_MATERIAL_TEXTURES; i ++) {
        h = (h ^ (h >> 31)) * 0xC2B2AE3D27D4EB4Full + textures[i];
    }

    return h ^ (h >> 29);
}

/** Queue the textures a new bind group binds in a color layer whose levels average stored values.
 *
 * The next frame generates their levels again in linear light, which then stays: a texture
 * shared by a color and a data layer is filtered as color. A single channel texture keeps its
 * levels, which every layer reads as stored.
 *
 * @param gpu The backend
 * @param textures Texture handles of the layers, 0 for the default
 */
void box_gpu_material_mips(
    box_gpu_t *gpu,
    const uint32_t *textures)
{
    int32_t i;
    for (i = 0; i < BOX_GPU_COLOR_LAYERS; i ++) {
        box_gpu_texture_t *tex = textures[i] && !(textures[i] & BOX_RENDER_TEXTURE_TARGET)
            ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, textures[i])
            : NULL;
        box_gpu_texture_entry_t *entry = (box_gpu_texture_entry_t*)tex;
        if (!tex || tex->mips == 1 || !tex->mipped || entry->single ||
            entry->space == BOX_GPU_MIP_SRGB)
        {
            continue;
        }

        entry->space = BOX_GPU_MIP_SRGB;
        tex->mipped = false;
        *ecs_vec_append_t(NULL, &gpu->mips, uint32_t) = textures[i];
    }
}

/** Get the view a layer binds: the texture of its handle or the default of the layer, also for
 * a float32 texture, which a layer can not filter.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 for the default
 * @param slot The layer, BOX_RENDER_TEXTURE_SLOTS for the foam texture of a water batch, stored
 * with a white default
 * @return The view
 */
WGPUTextureView box_gpu_material_view(
    box_gpu_t *gpu,
    uint32_t handle,
    int32_t slot)
{
    box_gpu_texture_t *tex = handle && !(handle & BOX_RENDER_TEXTURE_TARGET)
        ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle)
        : NULL;
    box_gpu_target_t *target = handle & BOX_RENDER_TEXTURE_TARGET
        ? ecs_map_get_deref(&gpu->targets, box_gpu_target_t, handle & ~BOX_RENDER_TEXTURE_TARGET)
        : NULL;
    bool color = slot < BOX_GPU_COLOR_LAYERS;
    WGPUTextureView result = NULL;
    if (target && target->output_view) {
        result = target->output_view;
        goto done;
    }

    if (!tex || !box_gpu_texture_filterable(tex)) {
        tex = slot == BOX_RENDER_TEXTURE_SLOTS - 1 ? gpu->flat : gpu->white;
    }

    result = color ? tex->srgb : tex->linear;
done:
    return result;
}

WGPUTextureView box_gpu_texture_color(
    box_gpu_t *gpu,
    uint32_t handle)
{
    uint32_t textures[BOX_RENDER_TEXTURE_SLOTS] = {0};
    textures[0] = handle & BOX_RENDER_TEXTURE_TARGET ? 0 : handle;
    box_gpu_texture_graph_sampled(gpu, textures[0]);
    box_gpu_material_mips(gpu, textures);

    return box_gpu_material_view(gpu, textures[0], 0);
}

WGPUTextureView box_gpu_texture_cookie(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_texture_t *tex = handle && !(handle & BOX_RENDER_TEXTURE_TARGET)
        ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle)
        : NULL;
    box_gpu_texture_graph_sampled(gpu, handle);

    return tex && box_gpu_texture_filterable(tex) ? tex->linear : NULL;
}

WGPUBindGroup box_gpu_material_group(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b)
{
    uint32_t textures[BOX_GPU_MATERIAL_TEXTURES];
    WGPUBindGroupEntry entries[BOX_RENDER_TEXTURE_SLOTS + 3];
    uint32_t anisotropy = packet->anisotropy;
    WGPUBindGroup result = NULL;
    box_gpu_material_t *m = NULL;
    uint64_t hash;
    ecs_map_val_t *head;
    int32_t i;
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        box_gpu_texture_graph_sampled(gpu, b->textures[i]);
    }

    if (b->water) {
        box_gpu_texture_graph_sampled(gpu, b->foam);
    }

    if (box_gpu_texture_blends(b)) {
        result = box_gpu_blend_group(gpu, packet, b);
        goto done;
    }

    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        bool self = gpu->rendering &&
            b->textures[i] == (BOX_RENDER_TEXTURE_TARGET | gpu->rendering);
        textures[i] = self ? 0 : b->textures[i];
    }

    textures[BOX_RENDER_TEXTURE_SLOTS] = b->water && !(gpu->rendering &&
        b->foam == (BOX_RENDER_TEXTURE_TARGET | gpu->rendering)) ? b->foam : 0;

    hash = box_gpu_material_hash(textures, b->wrap, anisotropy);
    head = ecs_map_ensure(&gpu->materials, hash);
    m = (box_gpu_material_t*)(uintptr_t)*head;
    while (m) {
        if (m->wrap == b->wrap && m->anisotropy == anisotropy &&
            !ecs_os_memcmp(m->textures, textures, ECS_SIZEOF(m->textures)))
        {
            result = m->group;
            goto done;
        }

        m = m->next;
    }

    box_gpu_material_mips(gpu, textures);
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        entries[i] = (WGPUBindGroupEntry){
            .binding = (uint32_t)i,
            .textureView = box_gpu_material_view(gpu, textures[i], i)
        };
    }

    entries[BOX_RENDER_TEXTURE_SLOTS] = (WGPUBindGroupEntry){
        .binding = BOX_RENDER_TEXTURE_SLOTS,
        .sampler = box_gpu_texture_sampler(gpu, b->wrap, anisotropy)
    };
    entries[BOX_RENDER_TEXTURE_SLOTS + 1] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_DRAW_KIND_BINDING,
        .buffer = gpu->draw_kinds,
        .size = 4 * sizeof(uint32_t)
    };
    entries[BOX_RENDER_TEXTURE_SLOTS + 2] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_MATERIAL_FOAM_BINDING,
        .textureView = box_gpu_material_view(gpu, textures[BOX_RENDER_TEXTURE_SLOTS],
            BOX_RENDER_TEXTURE_SLOTS)
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->material_layout,
        .entryCount = BOX_RENDER_TEXTURE_SLOTS + 3,
        .entries = entries
    };
    m = ecs_os_calloc_t(box_gpu_material_t);
    ecs_os_memcpy(m->textures, textures, ECS_SIZEOF(m->textures));
    m->wrap = b->wrap;
    m->anisotropy = anisotropy;
    m->group = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    m->next = (box_gpu_material_t*)(uintptr_t)*head;
    *head = (ecs_map_val_t)(uintptr_t)m;
    result = m->group;
done:
    return result;
}

bool box_gpu_texture_blends(
    const box_render_batch_t *b)
{
    return b->texture_blend && box_render_is_mesh(b->pipeline);
}

/** Get a texture a blend samples: an image of the backend, not the image of a target nor a
 * float32 texture.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 for none
 * @return The texture, NULL for none
 */
box_gpu_texture_t* box_gpu_blend_source(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_texture_t *result = handle && !(handle & BOX_RENDER_TEXTURE_TARGET)
        ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle)
        : NULL;

    return result && box_gpu_texture_filterable(result) ? result : NULL;
}

/** Get the serial of the image a blend samples through a handle.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 for none
 * @return The serial, 0 for none
 */
uint32_t box_gpu_blend_serial(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_texture_t *tex = box_gpu_blend_source(gpu, handle);

    return tex ? ((box_gpu_texture_entry_t*)tex)->serial : 0;
}

/** Get the grid mesh whose mask atlas a terrain blend binds.
 *
 * @param gpu The backend
 * @param blend The blend record
 * @return The mesh, NULL for another blend or while it has no atlas
 */
static box_gpu_mesh_t* box_gpu_blend_terrain(
    box_gpu_t *gpu,
    const box_render_texture_blend_t *blend)
{
    box_gpu_mesh_t *result = blend->terrain
        ? ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, blend->terrain) : NULL;

    return result && result->terrain_mask_view ? result : NULL;
}

/** Get the serial of the mask a blend group binds: the version of the mask atlas of a terrain
 * blend, else the serial of the image of the mask.
 *
 * @param gpu The backend
 * @param blend The blend record
 * @return The serial, 0 for none
 */
static uint32_t box_gpu_blend_mask_serial(
    box_gpu_t *gpu,
    const box_render_texture_blend_t *blend)
{
    box_gpu_mesh_t *m = box_gpu_blend_terrain(gpu, blend);

    return blend->terrain ? (m ? m->terrain_mask_version : 0)
        : box_gpu_blend_serial(gpu, blend->mask);
}

WGPUTextureView box_gpu_blend_mask_view(
    box_gpu_t *gpu,
    const box_render_texture_blend_t *blend)
{
    box_gpu_mesh_t *m = box_gpu_blend_terrain(gpu, blend);
    box_gpu_texture_t *mask = blend->terrain ? NULL : box_gpu_blend_source(gpu, blend->mask);

    return m ? m->terrain_mask_view : mask ? mask->linear : gpu->white->linear;
}

/** Create a view of one level of one slice of a texture array.
 *
 * @param texture The array
 * @param format Format of the view
 * @param level Mip level
 * @param slice Array layer
 * @return The view
 */
static WGPUTextureView box_gpu_blend_slice(
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t level,
    uint32_t slice)
{
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = format;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.baseMipLevel = level;
    desc.mipLevelCount = 1;
    desc.baseArrayLayer = slice;
    desc.arrayLayerCount = 1;
    desc.aspect = WGPUTextureAspect_All;
    if (format == BOX_GPU_TEXTURE_SRGB) {
        desc.usage = wgpuTextureGetUsage(texture) & ~WGPUTextureUsage_StorageBinding;
    }

    return wgpuTextureCreateView(texture, &desc);
}

/** Release the texture of a blend array.
 *
 * @param gpu The backend
 * @param a The array
 */
static void box_gpu_blend_array_release(
    box_gpu_t *gpu,
    box_gpu_blend_array_t *a)
{
    if (a->view) {
        wgpuTextureViewRelease(a->view);
        a->view = NULL;
    }

    if (a->texture) {
        wgpuTextureRelease(a->texture);
        a->texture = NULL;
    }

    gpu->blend_bytes -= a->bytes;
    a->bytes = 0;
}

/** Build a blend array from its sources: every slice of the size of the largest source of the
 * map, capped by the texture quality, each source scaled into its slice by the blit pipeline
 * with filtering (through its mip chain when it has one), then the mip chain of every slice.
 *
 * The albedo renders and averages in linear light through the sRGB views, the rest as stored;
 * a missing source fills its slice with white or a flat normal, a single channel one reads as
 * (r, 0, 0, 1). Without a blit pipeline the next check builds it again.
 *
 * @param gpu The backend
 * @param a The array, with its map, count, quality and sources
 * @param dirty Slices whose source changed, bit per slice
 */
static void box_gpu_blend_array_build(
    box_gpu_t *gpu,
    box_gpu_blend_array_t *a,
    uint32_t dirty)
{
    box_gpu_texture_t *src[BOX_GPU_ARRAY_SLICES] = {0};
    bool albedo = a->map == BOX_RENDER_BLEND_ALBEDO;
    WGPUTextureFormat srgb = BOX_GPU_TEXTURE_SRGB;
    WGPUTextureFormat format = albedo ? BOX_GPU_TEXTURE_SRGB : BOX_GPU_TEXTURE_FORMAT;
    uint32_t width = 1, height = 1, largest, mips, s, level;
    for (s = 0; s < a->count; s ++) {
        src[s] = box_gpu_blend_source(gpu, a->sources[s]);
        a->serials[s] = src[s] ? ((box_gpu_texture_entry_t*)src[s])->serial : 0;
        if (src[s]) {
            uint32_t w = wgpuTextureGetWidth(src[s]->texture);
            uint32_t h = wgpuTextureGetHeight(src[s]->texture);
            width = w > width ? w : width;
            height = h > height ? h : height;
        }
    }

    largest = width > height ? width : height;
    if (a->quality && largest > a->quality) {
        width = (uint32_t)(((uint64_t)width * a->quality + largest - 1) / largest);
        height = (uint32_t)(((uint64_t)height * a->quality + largest - 1) / largest);
        width = width ? width : 1;
        height = height ? height : 1;
    }

    mips = box_gpu_mip_count(width, height);
    if (!a->texture || wgpuTextureGetWidth(a->texture) != width ||
        wgpuTextureGetHeight(a->texture) != height)
    {
        dirty = UINT32_MAX;
        box_gpu_blend_array_release(gpu, a);
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            .size = { width, height, a->count },
            .format = BOX_GPU_TEXTURE_FORMAT,
            .mipLevelCount = mips,
            .sampleCount = 1,
            .viewFormatCount = albedo ? 1 : 0,
            .viewFormats = albedo ? &srgb : NULL
        };
        a->texture = wgpuDeviceCreateTexture(gpu->device, &desc);
        WGPUTextureViewDescriptor view = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        view.format = format;
        view.dimension = WGPUTextureViewDimension_2DArray;
        view.baseMipLevel = 0;
        view.mipLevelCount = mips;
        view.baseArrayLayer = 0;
        view.arrayLayerCount = a->count;
        view.aspect = WGPUTextureAspect_All;
        a->view = wgpuTextureCreateView(a->texture, &view);
        for (level = 0; level < mips; level ++) {
            uint64_t w = width >> level, h = height >> level;
            a->bytes += (w ? w : 1) * (h ? h : 1) * 4 * a->count;
        }

        gpu->blend_bytes += a->bytes;
        a->version ++;
    }

    gpu->pending.blend_builds ++;
    WGPURenderPipeline pipeline = box_gpu_blit_pipeline(gpu, format);
    if (!pipeline) {
        for (s = 0; s < a->count; s ++) {
            a->serials[s] = UINT32_MAX;
        }

        gpu->blend_retry = true;
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (s = 0; s < a->count; s ++) {
        box_gpu_texture_t *tex = src[s];
        if (!((dirty >> s) & 1u)) {
            continue;
        }

        if (!tex) {
            tex = (a->flat >> s) & 1u ? gpu->flat : gpu->white;
        }

        const box_gpu_texture_entry_t *entry = (const box_gpu_texture_entry_t*)tex;
        WGPUTextureFormat from = entry->single ? BOX_GPU_TEXTURE_SINGLE : format;
        if (!entry->single && entry->format != WGPUTextureFormat_Undefined &&
            entry->format != BOX_GPU_TEXTURE_FORMAT)
        {
            from = entry->format;
        }

        WGPUTextureView sv = box_gpu_texture_view(tex->texture, from, 0,
            tex->mipped ? tex->mips : 1);
        WGPUTextureView dv = box_gpu_blend_slice(a->texture, format, 0, s);
        box_gpu_texture_blit(gpu, encoder, pipeline, sv, dv);
        wgpuTextureViewRelease(sv);
        wgpuTextureViewRelease(dv);
    }

    for (level = 1; level < mips; level ++) {
        for (s = 0; s < a->count; s ++) {
            if (!((dirty >> s) & 1u)) {
                continue;
            }

            WGPUTextureView sv = box_gpu_blend_slice(a->texture, format, level - 1, s);
            WGPUTextureView dv = box_gpu_blend_slice(a->texture, format, level, s);
            box_gpu_texture_blit(gpu, encoder, pipeline, sv, dv);
            wgpuTextureViewRelease(sv);
            wgpuTextureViewRelease(dv);
        }
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
}

/** Get a texture array of the blends and layers, created on first use and built again when an
 * image of one of its sources changed.
 *
 * @param gpu The backend
 * @param map BOX_RENDER_BLEND_ALBEDO, _ORM, _NORMAL, _HEIGHT or BOX_GPU_LAYER_MAP
 * @param in Texture handle of each slice, 0 for the default; a target is the default
 * @param count Slices, 1 to BOX_GPU_ARRAY_SLICES
 * @param quality Largest dimension of a slice
 * @param flat Slices whose default is a flat normal, bit per slice
 * @return The array
 */
box_gpu_blend_array_t* box_gpu_array_get(
    box_gpu_t *gpu,
    uint32_t map,
    const uint32_t *in,
    uint32_t count,
    uint32_t quality,
    uint32_t flat)
{
    uint32_t sources[BOX_GPU_ARRAY_SLICES] = {0};
    box_gpu_blend_array_t *a;
    uint32_t s, dirty = 0;
    bool stale = false;
    for (s = 0; s < count; s ++) {
        sources[s] = in[s] & BOX_RENDER_TEXTURE_TARGET ? 0 : in[s];
    }

    for (a = gpu->blend_arrays; a; a = a->next) {
        if (a->map == map && a->count == count && a->quality == quality && a->flat == flat &&
            !ecs_os_memcmp(a->sources, sources, ECS_SIZEOF(sources)))
        {
            break;
        }
    }

    if (!a) {
        a = ecs_os_calloc_t(box_gpu_blend_array_t);
        a->map = map;
        a->count = count;
        a->quality = quality;
        a->flat = flat;
        ecs_os_memcpy(a->sources, sources, ECS_SIZEOF(sources));
        a->next = gpu->blend_arrays;
        gpu->blend_arrays = a;
        gpu->pending.blend_arrays ++;
        stale = true;
    }

    for (s = 0; s < count; s ++) {
        if (stale || box_gpu_blend_serial(gpu, sources[s]) != a->serials[s]) {
            dirty |= 1u << s;
        }
    }

    if (dirty) {
        box_gpu_blend_array_build(gpu, a, dirty);
    }

    a->used = gpu->blend_frame;

    return a;
}

/** Get the array of one map of the layers of a blend, created on first use and built again
 * when an image of one of its sources changed.
 *
 * @param gpu The backend
 * @param r The blend
 * @param map BOX_RENDER_BLEND_ALBEDO, _ORM, _NORMAL or _HEIGHT
 * @return The array
 */
static box_gpu_blend_array_t* box_gpu_blend_array(
    box_gpu_t *gpu,
    const box_render_texture_blend_t *r,
    uint32_t map)
{
    uint32_t sources[BOX_RENDER_BLEND_SLICES] = {0};
    uint32_t count = r->count < 1 ? 1 : r->count > BOX_RENDER_BLEND_SLICES
        ? BOX_RENDER_BLEND_SLICES : r->count;
    uint32_t s;
    for (s = 0; s < count; s ++) {
        sources[s] = r->layers[s][map];
    }

    return box_gpu_array_get(gpu, map, sources, count, r->quality,
        map == BOX_RENDER_BLEND_NORMAL ? UINT32_MAX : 0u);
}

/** Release a blend group and its references to its arrays, and forget the groups the batches
 * drew with.
 *
 * @param gpu The backend
 * @param g The group
 */
static void box_gpu_blend_group_free(
    box_gpu_t *gpu,
    box_gpu_blend_group_t *g)
{
    int32_t m;
    gpu->blend_alive --;
    gpu->blend_arrays_stale = true;
    ecs_vec_clear(&gpu->blend_hints);
    for (m = 0; m < BOX_RENDER_BLEND_SOURCES; m ++) {
        if (g->arrays[m]) {
            g->arrays[m]->refs --;
        }
    }

    if (g->group) {
        wgpuBindGroupRelease(g->group);
        gpu->forward_stamp ++;
    }

    if (g->uniforms) {
        wgpuBufferRelease(g->uniforms);
    }

    ecs_os_free(g);
}

/** Make the bind group of a blend group: its arrays, mask, emissive layer, sampler and
 * uniforms, from the images the backend has now.
 *
 * @param gpu The backend
 * @param g The group, with its blend and arrays
 */
static void box_gpu_blend_group_bind(
    box_gpu_t *gpu,
    box_gpu_blend_group_t *g)
{
    box_gpu_texture_t *mask = g->blend.terrain
        ? NULL : box_gpu_blend_source(gpu, g->blend.mask);
    box_gpu_mesh_t *terrain = box_gpu_blend_terrain(gpu, &g->blend);
    box_gpu_blend_uniforms_t u = {0};
    uint32_t textures[BOX_RENDER_TEXTURE_SLOTS] = { 0, g->emissive, 0, 0 };
    int32_t m;
    if (g->group) {
        wgpuBindGroupRelease(g->group);
        gpu->forward_stamp ++;
    }

    ecs_os_memcpy(u.transforms, g->blend.transforms, ECS_SIZEOF(u.transforms));
    u.count = g->arrays[0]->count;
    u.flags = (mask ? BOX_GPU_BLEND_MASKED : 0) |
        (mask && ((box_gpu_texture_entry_t*)mask)->single ? BOX_GPU_BLEND_SINGLE : 0) |
        (g->blend.tiles ? BOX_GPU_BLEND_TILES : 0) |
        (g->blend.terrain ? BOX_GPU_BLEND_TERRAIN : 0) |
        (terrain ? BOX_GPU_BLEND_MASKED : 0);
    u.contrast = g->blend.contrast;
    u.variation = g->blend.variation;
    u.strata = g->blend.strata |
        ((uint32_t)(glm_clamp(g->blend.strata_tint, 0.0f, 1.0f) * 255.0f + 0.5f) << 8);
    u.strata_fold = g->blend.strata_fold;
    u.grass = g->grass;
    u.terrain = terrain
        ? terrain->terrain_mask_texels | (terrain->terrain_mask_columns << 16) : 0;
    WGPUTextureView bake = box_gpu_terrain_bake_view(gpu, &g->blend, &u.bake, &g->bake_serial);
    for (m = 0; g->arrays[BOX_RENDER_BLEND_HEIGHT] && m < (int32_t)u.count; m ++) {
        uint32_t serial = g->arrays[BOX_RENDER_BLEND_HEIGHT]->serials[m];
        if (serial && serial != UINT32_MAX) {
            u.flags |= BOX_GPU_BLEND_HEIGHT << m;
        }
    }

    if (!g->uniforms) {
        g->uniforms = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(box_gpu_blend_uniforms_t)
        });
    }

    wgpuQueueWriteBuffer(gpu->queue, g->uniforms, 0, &u, sizeof(u));
    box_gpu_material_mips(gpu, textures);
    WGPUBindGroupEntry entries[BOX_GPU_BLEND_ENTRIES] = {
        {
            .binding = BOX_GPU_BLEND_EMISSIVE,
            .textureView = box_gpu_material_view(gpu, g->emissive, 1)
        },
        {
            .binding = BOX_GPU_BLEND_SAMPLER,
            .sampler = box_gpu_texture_sampler(gpu, g->wrap, g->anisotropy)
        },
        {
            .binding = BOX_GPU_BLEND_MASK,
            .textureView = box_gpu_blend_mask_view(gpu, &g->blend)
        },
        {
            .binding = BOX_GPU_BLEND_UNIFORMS,
            .buffer = g->uniforms,
            .offset = 0,
            .size = sizeof(box_gpu_blend_uniforms_t)
        }
    };
    entries[BOX_GPU_BLEND_ENTRIES - 1] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_DRAW_KIND_BINDING,
        .buffer = gpu->draw_kinds,
        .size = 4 * sizeof(uint32_t)
    };
    entries[BOX_GPU_BLEND_ENTRIES - 2] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_BLEND_BAKE,
        .textureView = bake
    };
    for (m = 0; m < BOX_RENDER_BLEND_MAPS; m ++) {
        entries[4 + m] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_BLEND_ARRAYS + (uint32_t)m,
            .textureView = g->arrays[m]->view
        };
        g->versions[m] = g->arrays[m]->version;
    }

    entries[4 + BOX_RENDER_BLEND_HEIGHT] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_BLEND_HEIGHTS,
        .textureView = g->arrays[BOX_RENDER_BLEND_HEIGHT]
            ? g->arrays[BOX_RENDER_BLEND_HEIGHT]->view : gpu->blend_flat_heights
    };
    g->versions[BOX_RENDER_BLEND_HEIGHT] = g->arrays[BOX_RENDER_BLEND_HEIGHT]
        ? g->arrays[BOX_RENDER_BLEND_HEIGHT]->version : 0;

    g->mask_serial = box_gpu_blend_mask_serial(gpu, &g->blend);
    g->emissive_serial = box_gpu_blend_serial(gpu, g->emissive);
    g->bound = gpu->blend_frame;
    g->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->blend_layout,
        .entryCount = BOX_GPU_BLEND_ENTRIES,
        .entries = entries
    });
}

void** box_gpu_batch_hint(
    ecs_vec_t *hints,
    const box_render_packet_t *packet,
    const box_render_batch_t *b)
{
    const box_render_batch_t *first = ecs_vec_first(&packet->batches);
    int32_t count = ecs_vec_count(&packet->batches), have = ecs_vec_count(hints);
    void **result = NULL;
    if (!first || b < first || b >= first + count) {
        goto done;
    }

    if (have < count) {
        ecs_vec_set_count_t(NULL, hints, void*, count);
        ecs_os_memset_n(ecs_vec_get_t(hints, void*, have), 0, void*, count - have);
    }

    result = ecs_vec_get_t(hints, void*, (int32_t)(b - first));
done:
    return result;
}

/** Get the blend group of a TextureBlend batch, created on first use. Once per packet it writes
 * the far field of the Grass on the terrain of a terrain blend into its uniforms when it
 * changed and, after a texture or mesh changed, binds its arrays and images again if they did.
 *
 * The emissive layer and the mask sample loaded images only, not the image of a target. The
 * heights make an array only with a contrast above 0 and a height texture on some layer.
 * Without one every layer is flat at 0.5 and the group binds the one-slice placeholder.
 *
 * @param gpu The backend
 * @param packet The packet of the batch
 * @param b The batch, box_gpu_texture_blends
 * @return The blend group
 */
box_gpu_blend_group_t* box_gpu_blend_group_get(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b)
{
    const box_render_texture_blend_t *r = box_render_packet_data(packet, b->blend_layers);
    uint32_t emissive = b->textures[1] & BOX_RENDER_TEXTURE_TARGET ? 0 : b->textures[1];
    box_gpu_blend_group_t *g;
    int32_t m;
    box_render_grass_far_t grass;
    bool heights = false;
    bool stale, moved;
    box_gpu_blend_group_t **hint = (box_gpu_blend_group_t**)box_gpu_batch_hint(
        &gpu->blend_hints, packet, b);
    g = hint ? *hint : NULL;
    if (g && g->emissive == emissive && g->wrap == b->wrap &&
        g->anisotropy == packet->anisotropy &&
        !ecs_os_memcmp(&g->blend, r, ECS_SIZEOF(box_render_texture_blend_t)))
    {
        goto found;
    }

    for (g = gpu->blend_groups; g; g = g->next) {
        if (g->emissive == emissive && g->wrap == b->wrap &&
            g->anisotropy == packet->anisotropy &&
            !ecs_os_memcmp(&g->blend, r, ECS_SIZEOF(box_render_texture_blend_t)))
        {
            break;
        }
    }

    if (!g) {
        g = ecs_os_calloc_t(box_gpu_blend_group_t);
        g->blend = *r;
        g->emissive = emissive;
        g->wrap = b->wrap;
        g->anisotropy = packet->anisotropy;
        g->checked = -1;
        g->used = -1;
        g->next = gpu->blend_groups;
        gpu->blend_groups = g;
        gpu->blend_alive ++;
    }

    if (hint) {
        *hint = g;
    }

found:
    if (g->used != gpu->blend_frame) {
        g->used = gpu->blend_frame;
        gpu->blend_marks ++;
    }

    if (g->checked == gpu->blend_frame && g->group) {
        goto done;
    }

    g->checked = gpu->blend_frame;
    box_render_grass_far_of(packet, r->terrain, &grass);
    moved = ecs_os_memcmp(&grass, &g->grass, ECS_SIZEOF(box_render_grass_far_t)) != 0;
    g->grass = grass;
    if (g->group && g->seen_textures == gpu->texture_serial &&
        g->seen_meshes == gpu->mesh_serial)
    {
        goto grass;
    }

    for (m = 0; r->contrast > 0.0f && m < (int32_t)r->count; m ++) {
        heights |= r->layers[m][BOX_RENDER_BLEND_HEIGHT] != 0;
    }

    uint32_t bake_layout, bake_serial;
    box_gpu_terrain_bake_view(gpu, r, &bake_layout, &bake_serial);
    stale = !g->group || g->mask_serial != box_gpu_blend_mask_serial(gpu, r) ||
        g->emissive_serial != box_gpu_blend_serial(gpu, emissive) ||
        g->bake_serial != bake_serial;
    for (m = 0; m < BOX_RENDER_BLEND_SOURCES; m ++) {
        box_gpu_blend_array_t *a = m < BOX_RENDER_BLEND_MAPS || heights
            ? box_gpu_blend_array(gpu, r, (uint32_t)m) : NULL;
        if (a != g->arrays[m]) {
            if (g->arrays[m]) {
                g->arrays[m]->refs --;
                gpu->blend_arrays_stale = true;
            }

            if (a) {
                a->refs ++;
            }

            g->arrays[m] = a;
            stale = true;
        }

        stale |= a && g->versions[m] != a->version;
    }

    g->seen_textures = gpu->blend_retry ? 0 : gpu->texture_serial;
    g->seen_meshes = gpu->mesh_serial;
    if (stale) {
        box_gpu_blend_group_bind(gpu, g);
        moved = false;
    }

grass:
    if (moved && g->uniforms) {
        wgpuQueueWriteBuffer(gpu->queue, g->uniforms,
            offsetof(box_gpu_blend_uniforms_t, grass), &g->grass,
            sizeof(box_render_grass_far_t));
    }

done:
    return g;
}

/** Get the bind group of a TextureBlend batch, created on first use (box_gpu_blend_group_get).
 *
 * @param gpu The backend
 * @param packet The packet of the batch
 * @param b The batch, box_gpu_texture_blends
 * @return The bind group
 */
static WGPUBindGroup box_gpu_blend_group(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b)
{
    return box_gpu_blend_group_get(gpu, packet, b)->group;
}

/** Make the bind groups and texture arrays of the TextureBlend batches of a packet, after the
 * mip chains of the frame, and release those no batch of the packet draws with.
 *
 * @param gpu The backend
 * @param packet The packet
 */
static void box_gpu_blends_prepare(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    const box_render_batch_t *batches = ecs_vec_first(&packet->batches);
    int32_t i, count = ecs_vec_count(&packet->batches);
    box_gpu_blend_group_t **gp;
    box_gpu_blend_array_t **ap;
    gpu->blend_frame ++;
    gpu->blend_marks = 0;
    gpu->blend_retry = false;
    for (i = 0; i < count; i ++) {
        const box_render_batch_t *b = &batches[i];
        if (!b->texture_blend && !b->texture_layers) {
            continue;
        }

        if (box_gpu_texture_blends(b)) {
            box_gpu_blend_group(gpu, packet, b);
        }

        if (box_gpu_layered(b)) {
            box_gpu_layer_group(gpu, packet, b);
        }
    }

    if (gpu->blend_marks == gpu->blend_alive) {
        goto arrays;
    }

    box_gpu_layers_sweep(gpu);
    for (gp = &gpu->blend_groups; *gp; ) {
        box_gpu_blend_group_t *g = *gp;
        if (g->used == gpu->blend_frame) {
            gp = &g->next;
            continue;
        }

        *gp = g->next;
        box_gpu_blend_group_free(gpu, g);
    }

arrays:
    if (!gpu->blend_arrays_stale) {
        goto done;
    }

    gpu->blend_arrays_stale = false;
    for (ap = &gpu->blend_arrays; *ap; ) {
        box_gpu_blend_array_t *a = *ap;
        if (a->refs > 0 || a->used == gpu->blend_frame) {
            ap = &a->next;
            continue;
        }

        *ap = a->next;
        box_gpu_blend_array_release(gpu, a);
        gpu->pending.blend_arrays --;
        ecs_os_free(a);
    }

done:
    return;
}

void box_gpu_blends_clear(
    box_gpu_t *gpu)
{
    box_gpu_layers_clear(gpu);
    while (gpu->blend_groups) {
        box_gpu_blend_group_t *g = gpu->blend_groups;
        gpu->blend_groups = g->next;
        box_gpu_blend_group_free(gpu, g);
    }

    while (gpu->blend_arrays) {
        box_gpu_blend_array_t *a = gpu->blend_arrays;
        gpu->blend_arrays = a->next;
        box_gpu_blend_array_release(gpu, a);
        gpu->pending.blend_arrays --;
        ecs_os_free(a);
    }
}

#endif
