/**
 * @file render/gpu_texture.c
 * @brief Textures, samplers and the texture layer bind groups of the webgpu backend.
 *
 * A texture is uploaded into its first mip level; the blit pipeline renders every further
 * level from the one above it. Batches bind their layers as one cached bind group.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Pixel of the default albedo, emissive and roughness layers */
static const uint8_t box_gpu_white[4] = { 255, 255, 255, 255 };

/** Pixel of the default normal layer, a normal along the surface normal */
static const uint8_t box_gpu_flat[4] = { 128, 128, 255, 255 };

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

/** Create a view of a range of mip levels of a texture.
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

    return wgpuTextureCreateView(texture, &desc);
}

/** Create a texture and upload its first mip level.
 *
 * @param gpu The backend
 * @param width Width in pixels
 * @param height Height in pixels
 * @param pixels RGBA8 rows, the first row is the top of the image
 * @return The texture
 */
static box_gpu_texture_t* box_gpu_texture_new(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    const uint8_t *pixels)
{
    box_gpu_texture_t *result = ecs_os_calloc_t(box_gpu_texture_t);
    WGPUTextureFormat srgb = BOX_GPU_TEXTURE_SRGB;
    result->mips = box_gpu_mip_count(width, height);
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = BOX_GPU_TEXTURE_FORMAT,
        .mipLevelCount = result->mips,
        .sampleCount = 1,
        .viewFormatCount = 1,
        .viewFormats = &srgb
    };
    result->texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    result->linear = box_gpu_texture_view(
        result->texture, BOX_GPU_TEXTURE_FORMAT, 0, result->mips);
    result->srgb = box_gpu_texture_view(result->texture, srgb, 0, result->mips);
    result->mipped = result->mips == 1;
    WGPUTexelCopyTextureInfo dst = {
        .texture = result->texture,
        .aspect = WGPUTextureAspect_All
    };
    WGPUTexelCopyBufferLayout layout = { .bytesPerRow = width * 4, .rowsPerImage = height };
    WGPUExtent3D extent = { width, height, 1 };
    wgpuQueueWriteTexture(gpu->queue, &dst, pixels, (size_t)width * height * 4, &layout,
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

/** Release every cached bind group, after a texture they may bind changed.
 *
 * @param gpu The backend
 */
static void box_gpu_materials_clear(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->materials);
    while (ecs_map_next(&it)) {
        box_gpu_material_t *m = ecs_map_ptr(&it);
        while (m) {
            box_gpu_material_t *next = m->next;
            wgpuBindGroupRelease(m->group);
            ecs_os_free(m);
            m = next;
        }
    }

    ecs_map_clear(&gpu->materials);
}

void box_gpu_texture_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[BOX_RENDER_TEXTURE_SLOTS + 1];
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
    WGPUBindGroupLayoutDescriptor layout = {
        .entryCount = BOX_RENDER_TEXTURE_SLOTS + 1,
        .entries = entries
    };
    gpu->material_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    gpu->white = box_gpu_texture_new(gpu, 1, 1, box_gpu_white);
    gpu->flat = box_gpu_texture_new(gpu, 1, 1, box_gpu_flat);
    ecs_map_init(&gpu->textures, NULL);
    ecs_map_init(&gpu->materials, NULL);
    ecs_vec_init_t(NULL, &gpu->mips, uint32_t, 0);
}

void box_gpu_texture_fini(
    box_gpu_t *gpu)
{
    int32_t w, a;
    box_gpu_materials_clear(gpu);
    ecs_map_fini(&gpu->materials);
    ecs_map_iter_t it = ecs_map_iter(&gpu->textures);
    while (ecs_map_next(&it)) {
        box_gpu_texture_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&gpu->textures);
    ecs_vec_fini_t(NULL, &gpu->mips, uint32_t);
    for (w = 0; w < BOX_GPU_WRAPS; w ++) {
        for (a = 0; a < BOX_GPU_ANISOTROPY_LEVELS; a ++) {
            if (gpu->samplers[w][a]) {
                wgpuSamplerRelease(gpu->samplers[w][a]);
                gpu->samplers[w][a] = NULL;
            }
        }
    }

    if (gpu->white) {
        box_gpu_texture_free(gpu->white);
        gpu->white = NULL;
    }

    if (gpu->flat) {
        box_gpu_texture_free(gpu->flat);
        gpu->flat = NULL;
    }

    if (gpu->material_layout) {
        wgpuBindGroupLayoutRelease(gpu->material_layout);
        gpu->material_layout = NULL;
    }
}

void box_gpu_texture_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_texture_desc_t *desc)
{
    box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle);
    box_gpu_materials_clear(gpu);
    if (tex) {
        ecs_map_remove(&gpu->textures, handle);
        box_gpu_texture_free(tex);
    }

    if (op == BOX_RENDER_DESTROY || !desc || !desc->width || !desc->height) {
        goto done;
    }

    tex = box_gpu_texture_new(gpu, desc->width, desc->height, (const uint8_t*)(desc + 1));
    ecs_map_insert_ptr(&gpu->textures, handle, tex);
    if (!tex->mipped) {
        *ecs_vec_append_t(NULL, &gpu->mips, uint32_t) = handle;
    }

done:
    return;
}

/** Render the mip levels of a texture, each from the level above it.
 *
 * @param gpu The backend
 * @param tex The texture
 * @param pipeline The blit pipeline of the texture format
 * @param encoder The encoder the passes are recorded in
 */
static void box_gpu_texture_mip_chain(
    box_gpu_t *gpu,
    box_gpu_texture_t *tex,
    WGPURenderPipeline pipeline,
    WGPUCommandEncoder encoder)
{
    uint32_t level;
    for (level = 1; level < tex->mips; level ++) {
        WGPUTextureView src = box_gpu_texture_view(
            tex->texture, BOX_GPU_TEXTURE_FORMAT, level - 1, 1);
        WGPUTextureView dst = box_gpu_texture_view(
            tex->texture, BOX_GPU_TEXTURE_FORMAT, level, 1);
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
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(group);
        wgpuTextureViewRelease(src);
        wgpuTextureViewRelease(dst);
    }

    tex->mipped = true;
}

void box_gpu_texture_mips(
    box_gpu_t *gpu)
{
    int32_t i, count = ecs_vec_count(&gpu->mips);
    WGPURenderPipeline pipeline = NULL;
    if (!count) {
        goto done;
    }

    pipeline = box_gpu_blit_pipeline(gpu, BOX_GPU_TEXTURE_FORMAT);
    if (!pipeline) {
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (i = 0; i < count; i ++) {
        uint32_t handle = *ecs_vec_get_t(&gpu->mips, uint32_t, i);
        box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle);
        if (tex && !tex->mipped) {
            box_gpu_texture_mip_chain(gpu, tex, pipeline, encoder);
        }
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    ecs_vec_clear(&gpu->mips);
done:
    return;
}

/** Get the sampler of a wrap mode and an anisotropy, created on first use.
 *
 * @param gpu The backend
 * @param wrap The TextureWrap
 * @param anisotropy Anisotropic samples, rounded down to a power of two up to 16
 * @return The sampler
 */
static WGPUSampler box_gpu_texture_sampler(
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
 * @param textures Texture handles of the layers
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
    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        h = (h ^ (h >> 31)) * 0xC2B2AE3D27D4EB4Full + textures[i];
    }

    return h ^ (h >> 29);
}

/** Get the view a layer binds: the texture of its handle or the default of the layer.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 for the default
 * @param slot The layer
 * @return The view
 */
static WGPUTextureView box_gpu_material_view(
    box_gpu_t *gpu,
    uint32_t handle,
    int32_t slot)
{
    box_gpu_texture_t *tex = handle
        ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, handle)
        : NULL;
    bool color = slot < 2;
    if (!tex) {
        tex = slot == BOX_RENDER_TEXTURE_SLOTS - 1 ? gpu->flat : gpu->white;
    }

    return color ? tex->srgb : tex->linear;
}

WGPUBindGroup box_gpu_material_group(
    box_gpu_t *gpu,
    const box_render_batch_t *b,
    uint32_t anisotropy)
{
    uint64_t hash = box_gpu_material_hash(b->textures, b->wrap, anisotropy);
    ecs_map_val_t *head = ecs_map_ensure(&gpu->materials, hash);
    box_gpu_material_t *m = (box_gpu_material_t*)(uintptr_t)*head;
    WGPUBindGroupEntry entries[BOX_RENDER_TEXTURE_SLOTS + 1];
    int32_t i;
    while (m) {
        if (m->wrap == b->wrap && m->anisotropy == anisotropy &&
            !ecs_os_memcmp(m->textures, b->textures, ECS_SIZEOF(m->textures)))
        {
            goto done;
        }

        m = m->next;
    }

    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        entries[i] = (WGPUBindGroupEntry){
            .binding = (uint32_t)i,
            .textureView = box_gpu_material_view(gpu, b->textures[i], i)
        };
    }

    entries[BOX_RENDER_TEXTURE_SLOTS] = (WGPUBindGroupEntry){
        .binding = BOX_RENDER_TEXTURE_SLOTS,
        .sampler = box_gpu_texture_sampler(gpu, b->wrap, anisotropy)
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->material_layout,
        .entryCount = BOX_RENDER_TEXTURE_SLOTS + 1,
        .entries = entries
    };
    m = ecs_os_calloc_t(box_gpu_material_t);
    ecs_os_memcpy(m->textures, b->textures, ECS_SIZEOF(m->textures));
    m->wrap = b->wrap;
    m->anisotropy = anisotropy;
    m->group = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    m->next = (box_gpu_material_t*)(uintptr_t)*head;
    *head = (ecs_map_val_t)(uintptr_t)m;
done:
    return m->group;
}

#endif
