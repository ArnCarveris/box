/**
 * @file render/gpu_smaa.c
 * @brief The precomputed area and search textures of SMAA, from the data of the reference.
 *
 * The data is the reference's AreaTex.h and SearchTex.h, vendored unchanged in src/render/smaa
 * (MIT, see its LICENSE). Rows are uploaded in the order the headers store them.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

#include "smaa/AreaTex.inc"
#include "smaa/SearchTex.inc"

_Static_assert(sizeof(areaTexBytes) == AREATEX_SIZE, "AreaTex.inc must hold the whole image");
_Static_assert(sizeof(searchTexBytes) == SEARCHTEX_SIZE, "SearchTex.inc must hold the whole image");

/** Create a 2D texture that passes sample, filled with rows of texels.
 *
 * @param gpu The backend
 * @param width Width in texels
 * @param height Height in texels
 * @param pitch Bytes of a row
 * @param format Color format
 * @param bytes The rows, top first
 * @return The texture
 */
static WGPUTexture box_gpu_smaa_upload(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    WGPUTextureFormat format,
    const unsigned char *bytes)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &desc);
    WGPUTexelCopyTextureInfo dst = { .texture = result, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferLayout layout = { .bytesPerRow = pitch, .rowsPerImage = height };
    WGPUExtent3D extent = { width, height, 1 };
    wgpuQueueWriteTexture(gpu->queue, &dst, bytes, (size_t)pitch * height, &layout, &extent);

    return result;
}

WGPUTexture box_gpu_smaa_area(
    box_gpu_t *gpu)
{
    return box_gpu_smaa_upload(gpu, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH,
        WGPUTextureFormat_RG8Unorm, areaTexBytes);
}

WGPUTexture box_gpu_smaa_search(
    box_gpu_t *gpu)
{
    return box_gpu_smaa_upload(gpu, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH,
        WGPUTextureFormat_R8Unorm, searchTexBytes);
}

#endif
