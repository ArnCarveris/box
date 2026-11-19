/**
 * @file render/gpu_terrain.c
 * @brief The heights of the terrain tiles in the surface assets of their grid meshes: the WebGPU
 * backend copies the R32Float image of each tile into its slot, which the vertex stages read,
 * and the masks of a tile with masks into its cell of the mask atlas of the mesh, which the
 * fragment stages of a terrain TextureBlend read.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Most cells across the mask atlas of a grid mesh */
#define BOX_GPU_TERRAIN_MASK_COLUMNS (16u)

/** Make the mask atlas of a terrain grid mesh hold the cell of a slot: an atlas of twice the
 * rows, at least the row of the slot, whose cells are all copied again, of a new version that
 * the blend groups bind again. An atlas of other masks is made again from the first row.
 *
 * @param gpu The backend
 * @param m The mesh
 * @param slot The slot
 * @param texels Texels along an edge of the masks
 * @param format Format of the masks
 * @return Whether the atlas holds the cell
 */
static bool box_gpu_terrain_masks_grow(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m,
    uint32_t slot,
    uint32_t texels,
    WGPUTextureFormat format)
{
    bool result = true;
    WGPULimits limits = WGPU_LIMITS_INIT;
    uint32_t rows = 0, columns, limit;
    if (m->terrain_masks && (m->terrain_mask_texels != texels ||
        m->terrain_mask_format != format))
    {
        rows = 0;
    } else if (m->terrain_masks) {
        rows = m->terrain_mask_rows;
    }

    if (m->terrain_masks && rows && slot / m->terrain_mask_columns < rows) {
        goto done;
    }

    if (wgpuDeviceGetLimits(gpu->device, &limits) != WGPUStatus_Success) {
        result = false;
        goto done;
    }

    limit = limits.maxTextureDimension2D;
    columns = texels ? limit / (2u * texels) : 0;
    columns = columns > BOX_GPU_TERRAIN_MASK_COLUMNS ? BOX_GPU_TERRAIN_MASK_COLUMNS : columns;
    if (!columns) {
        result = false;
        goto done;
    }

    uint32_t need = slot / columns + 1u, most = limit / texels;
    rows = rows * 2u > need ? rows * 2u : need;
    rows = rows < 2u ? 2u : rows;
    rows = rows > most ? most : rows;
    if (rows < need) {
        ecs_err("terrain masks exceed device texture capacity");
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        result = false;
        goto done;
    }

    if (m->terrain_mask_view) {
        wgpuTextureViewRelease(m->terrain_mask_view);
    }

    if (m->terrain_masks) {
        wgpuTextureRelease(m->terrain_masks);
    }

    m->terrain_masks = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { columns * 2u * texels, rows * texels, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    m->terrain_mask_view = wgpuTextureCreateView(m->terrain_masks, NULL);
    m->terrain_mask_texels = texels;
    m->terrain_mask_format = format;
    m->terrain_mask_columns = columns;
    m->terrain_mask_rows = rows;
    m->terrain_mask_version = ++ gpu->texture_serial;
    ecs_os_free(m->terrain_mask_serials);
    m->terrain_mask_serials = ecs_os_calloc_n(uint32_t, (int32_t)(columns * rows * 2u));
done:
    return result;
}

/** Copy the masks of a tile into its cell of the mask atlas of its grid mesh when an image
 * changed since the last copy.
 *
 * @param gpu The backend
 * @param m The mesh
 * @param h The tile
 * @param grow Make the atlas hold the cell instead of copying
 * @param encoder The encoder, made on first use
 */
static void box_gpu_terrain_masks(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m,
    const box_render_terrain_height_t *h,
    bool grow,
    WGPUCommandEncoder *encoder)
{
    box_gpu_texture_t *fine = h->masks
        ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, h->masks) : NULL;
    box_gpu_texture_t *coarse = h->coarse
        ? ecs_map_get_deref(&gpu->textures, box_gpu_texture_t, h->coarse) : NULL;
    uint32_t sources[2] = { h->masks, h->coarse }, k;
    if (!fine || !coarse) {
        goto done;
    }

    WGPUTextureFormat format = wgpuTextureGetFormat(fine->texture);
    uint32_t texels = wgpuTextureGetWidth(fine->texture);
    if (wgpuTextureGetFormat(coarse->texture) != format ||
        wgpuTextureGetHeight(fine->texture) != texels ||
        wgpuTextureGetWidth(coarse->texture) > texels ||
        wgpuTextureGetHeight(coarse->texture) > texels)
    {
        goto done;
    }

    if (grow) {
        box_gpu_terrain_masks_grow(gpu, m, h->slot, texels, format);
        goto done;
    }

    if (!m->terrain_masks || m->terrain_mask_texels != texels ||
        m->terrain_mask_format != format ||
        h->slot / m->terrain_mask_columns >= m->terrain_mask_rows)
    {
        goto done;
    }

    uint32_t x = (h->slot % m->terrain_mask_columns) * 2u * texels;
    uint32_t y = (h->slot / m->terrain_mask_columns) * texels;
    for (k = 0; k < 2; k ++) {
        box_gpu_texture_t *src = k ? coarse : fine;
        uint32_t serial = box_gpu_texture_serial(gpu, sources[k]);
        uint32_t *copied = &m->terrain_mask_serials[h->slot * 2u + k];
        if (!serial || *copied == serial) {
            continue;
        }

        if (!*encoder) {
            *encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        }

        wgpuCommandEncoderCopyTextureToTexture(*encoder,
            &(WGPUTexelCopyTextureInfo){
                .texture = src->texture,
                .aspect = WGPUTextureAspect_All
            },
            &(WGPUTexelCopyTextureInfo){
                .texture = m->terrain_masks,
                .origin = { x + k * texels, y, 0 },
                .aspect = WGPUTextureAspect_All
            },
            &(WGPUExtent3D){ wgpuTextureGetWidth(src->texture),
                wgpuTextureGetHeight(src->texture), 1 });
        *copied = serial;
    }

done:
    return;
}

/** Make the surface assets of a terrain grid mesh hold a slot: a buffer of twice the slots, at
 * least slot + 1, whose slots are all copied again; the instance groups that bound the old
 * buffer bind the new one at their next draw.
 *
 * @param gpu The backend
 * @param m The mesh
 * @param slot The slot
 * @param slot_size Bytes of a slot
 * @return Whether the assets hold the slot
 */
static bool box_gpu_terrain_grow(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m,
    uint32_t slot,
    uint64_t slot_size)
{
    bool result = true;
    WGPULimits limits = WGPU_LIMITS_INIT;
    if (m->terrain_slot_size != slot_size) {
        ecs_os_free(m->terrain_serials);
        m->terrain_serials = NULL;
        m->terrain_slots = 0;
        m->terrain_slot_size = slot_size;
    }

    if ((int64_t)slot < m->terrain_slots) {
        goto done;
    }

    int64_t slots = m->terrain_slots * 2 > (int64_t)slot + 1
        ? m->terrain_slots * 2 : (int64_t)slot + 1;
    slots = slots < 16 ? 16 : slots;
    if (wgpuDeviceGetLimits(gpu->device, &limits) != WGPUStatus_Success) {
        result = false;
        goto done;
    }

    uint64_t limit = limits.maxStorageBufferBindingSize < limits.maxBufferSize
        ? limits.maxStorageBufferBindingSize : limits.maxBufferSize;
    if ((uint64_t)slots * slot_size > limit) {
        slots = (int64_t)(limit / slot_size);
    }

    if (slots <= (int64_t)slot) {
        ecs_err("terrain heights exceed device buffer capacity");
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        result = false;
        goto done;
    }

    wgpuBufferRelease(m->surface_assets);
    m->surface_asset_size = (uint64_t)slots * slot_size;
    m->surface_assets = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = m->surface_asset_size
    });
    m->assets = true;
    ecs_os_free(m->terrain_serials);
    m->terrain_serials = ecs_os_calloc_n(uint32_t, (int32_t)slots);
    m->terrain_slots = (int32_t)slots;
done:
    return result;
}

void box_gpu_terrain_heights(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    const box_render_terrain_height_t *heights = ecs_vec_first(&packet->terrain_heights);
    int32_t i, count = ecs_vec_count(&packet->terrain_heights), pass;
    WGPUCommandEncoder encoder = NULL;
    for (pass = 0; pass < 2; pass ++) {
        for (i = 0; i < count; i ++) {
            const box_render_terrain_height_t *h = &heights[i];
            box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, h->mesh);
            box_gpu_texture_t *tex = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t,
                h->texture);
            if (!m || !tex || wgpuTextureGetFormat(tex->texture) != WGPUTextureFormat_R32Float) {
                continue;
            }

            uint32_t width = wgpuTextureGetWidth(tex->texture);
            uint32_t height = wgpuTextureGetHeight(tex->texture);
            uint32_t row = (width * 4u + 255u) & ~255u;
            uint64_t slot_size = (uint64_t)row * height;
            if (pass == 0) {
                box_gpu_terrain_grow(gpu, m, h->slot, slot_size);
                box_gpu_terrain_masks(gpu, m, h, true, &encoder);
                continue;
            }

            box_gpu_terrain_masks(gpu, m, h, false, &encoder);

            uint32_t serial = box_gpu_texture_serial(gpu, h->texture);
            if (m->terrain_slot_size != slot_size || (int64_t)h->slot >= m->terrain_slots ||
                !serial || m->terrain_serials[h->slot] == serial)
            {
                continue;
            }

            if (!encoder) {
                encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
            }

            wgpuCommandEncoderCopyTextureToBuffer(encoder,
                &(WGPUTexelCopyTextureInfo){
                    .texture = tex->texture,
                    .aspect = WGPUTextureAspect_All
                },
                &(WGPUTexelCopyBufferInfo){
                    .layout = {
                        .offset = (uint64_t)h->slot * slot_size,
                        .bytesPerRow = row,
                        .rowsPerImage = height
                    },
                    .buffer = m->surface_assets
                },
                &(WGPUExtent3D){ width, height, 1 });
            m->terrain_serials[h->slot] = serial;
        }
    }

    if (encoder) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }
}

#endif
