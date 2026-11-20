/**
 * @file render/gpu_terrain.c
 * @brief The heights of the terrain tiles in the surface assets of their grid meshes: the WebGPU
 * backend copies the R32Float images of each tile into its slot, which the vertex stages read,
 * and the masks of a tile with masks into its cell of the mask atlas of the mesh, which the
 * fragment stages of a terrain TextureBlend read.
 *
 * The tiles with masks also bake their material into a cell of the bake atlas of the mesh, which
 * the fragments of a tile sample where a texel of the bake is no smaller than the fragment.
 */

#include "gpu.h"
#include "gpu_cull.h"
#include "../terrain/terrain.h"
#include <math.h>

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
    m->terrain_serials = ecs_os_calloc_n(uint32_t, (int32_t)slots * 2);
    m->terrain_slots = (int32_t)slots;
done:
    return result;
}

/** Copy an R32Float image of a terrain tile into the surface assets of its grid mesh when it
 * changed since the last copy.
 *
 * @param gpu The backend
 * @param m The mesh
 * @param tex The image
 * @param handle Handle of the texture of the image
 * @param offset Byte offset of the image in the assets
 * @param row Bytes of a row in the assets
 * @param copied Serial of the image last copied there
 * @param encoder The encoder, made on first use
 */
static void box_gpu_terrain_copy(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m,
    box_gpu_texture_t *tex,
    uint32_t handle,
    uint64_t offset,
    uint32_t row,
    uint32_t *copied,
    WGPUCommandEncoder *encoder)
{
    uint32_t serial = box_gpu_texture_serial(gpu, handle);
    uint32_t width = wgpuTextureGetWidth(tex->texture);
    uint32_t height = wgpuTextureGetHeight(tex->texture);
    if (!serial || *copied == serial) {
        goto done;
    }

    if (!*encoder) {
        *encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    }

    wgpuCommandEncoderCopyTextureToBuffer(*encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = tex->texture,
            .aspect = WGPUTextureAspect_All
        },
        &(WGPUTexelCopyBufferInfo){
            .layout = {
                .offset = offset,
                .bytesPerRow = row,
                .rowsPerImage = height
            },
            .buffer = m->surface_assets
        },
        &(WGPUExtent3D){ width, height, 1 });
    *copied = serial;
done:
    return;
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
            box_gpu_texture_t *rough = ecs_map_get_deref(&gpu->textures, box_gpu_texture_t,
                h->rough);
            if (!m || !tex || !rough ||
                wgpuTextureGetFormat(tex->texture) != WGPUTextureFormat_R32Float ||
                wgpuTextureGetFormat(rough->texture) != WGPUTextureFormat_R32Float)
            {
                continue;
            }

            uint32_t width = wgpuTextureGetWidth(tex->texture);
            uint32_t height = wgpuTextureGetHeight(tex->texture);
            uint32_t rows = wgpuTextureGetHeight(rough->texture);
            uint32_t row = (width * 4u + 255u) & ~255u;
            uint64_t slot_size = (uint64_t)row * (height + rows);
            if (wgpuTextureGetWidth(rough->texture) > width) {
                continue;
            }

            if (pass == 0) {
                box_gpu_terrain_grow(gpu, m, h->slot, slot_size);
                box_gpu_terrain_masks(gpu, m, h, true, &encoder);
                continue;
            }

            box_gpu_terrain_masks(gpu, m, h, false, &encoder);
            if (m->terrain_slot_size != slot_size || (int64_t)h->slot >= m->terrain_slots) {
                continue;
            }

            uint64_t offset = (uint64_t)h->slot * slot_size;
            box_gpu_terrain_copy(gpu, m, tex, h->texture, offset, row,
                &m->terrain_serials[h->slot * 2u], &encoder);
            box_gpu_terrain_copy(gpu, m, rough, h->rough, offset + (uint64_t)row * height, row,
                &m->terrain_serials[h->slot * 2u + 1u], &encoder);
        }
    }

    if (encoder) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }
}

/** Texels of the bake of a tile per interval of its heights */
#define BOX_GPU_TERRAIN_BAKE_DENSITY (2u)

/** Tiles baked per packet at most; new tiles first, the others again for a changed material
 * or sun */
#define BOX_GPU_TERRAIN_BAKE_TILES (64)

/** Cells across the bake atlas at most */
#define BOX_GPU_TERRAIN_BAKE_COLUMNS (64u)

/** Bytes between the parameters of two bakes, the alignment of a dynamic offset */
#define BOX_GPU_TERRAIN_BAKE_STRIDE (256u)

/** Binding of the parameters of a bake in its group (TerrainBakeParams of terrain_bake.wgsl) */
#define BOX_GPU_TERRAIN_BAKE_PARAMS_BINDING (30)

/** Binding of the terrain indices in the group of a bake */
#define BOX_GPU_TERRAIN_BAKE_INDEX_BINDING (31)

/** Binding of the surface assets of the grid mesh in the group of a bake, the heights its
 * fragment stage reads */
#define BOX_GPU_TERRAIN_BAKE_HEIGHTS_BINDING (32)

/** Parameters of one bake draw, TerrainBakeParams of terrain_bake.wgsl */
typedef struct box_gpu_terrain_bake_params_t {
    float cell[4];          /**< Pixel of the first texel of the cell in the scratch, texels along
                             *   its edge, morph: 0 for the tile, 1 for the next coarser level */
    float tile[4];          /**< Corner of the tile in tiles of level 0, x and z, its edge in
                             *   tiles of level 0, grid steps its edge vertices move out */
    float sun[4];           /**< Direction toward the sun, w 1 when there is one */
    float grid[4];          /**< Slot of the tile, floats per row of the heights of a slot,
                             *   texels per row and vertices along an edge */
    uint32_t info[4];       /**< Word offset of the terrain index, then 0 */
} box_gpu_terrain_bake_params_t;

/** A draw of a bake of a packet */
typedef struct box_gpu_terrain_bake_draw_t {
    const box_render_terrain_bake_t *bake; /**< The bake */
    box_gpu_mesh_t *mesh;   /**< Its grid mesh */
    bool coarse;            /**< The cell of the next coarser level */
} box_gpu_terrain_bake_draw_t;

void box_gpu_terrain_bake_free(
    box_gpu_mesh_t *m)
{
    if (m->terrain_bake_view) {
        wgpuTextureViewRelease(m->terrain_bake_view);
        m->terrain_bake_view = NULL;
    }

    if (m->terrain_bake) {
        wgpuTextureRelease(m->terrain_bake);
        m->terrain_bake = NULL;
    }

    ecs_os_free(m->terrain_bake_keys);
    m->terrain_bake_keys = NULL;
}

/** Get the texels along the edge of the bake of a tile of a grid mesh,
 * BOX_GPU_TERRAIN_BAKE_DENSITY per interval of its heights, from the texels of its masks.
 *
 * @param m The mesh
 * @return Texels, 0 without a mask atlas
 */
static uint32_t box_gpu_terrain_bake_texels(
    const box_gpu_mesh_t *m)
{
    uint32_t texels = m->terrain_masks ? m->terrain_mask_texels : 0u;
    uint32_t steps = texels > 2u * BOX_TERRAIN_BORDER + 1u
        ? texels - 1u - 2u * BOX_TERRAIN_BORDER : 0u;

    return steps ? steps * BOX_GPU_TERRAIN_BAKE_DENSITY + 1u : 0u;
}

/** Grow the bake atlas of a grid mesh to hold a slot, made again when the texels of its bakes
 * change; a new atlas bakes every cell again.
 *
 * @param gpu The backend
 * @param m The mesh
 * @param slot The slot
 * @return Whether the atlas holds the slot
 */
static bool box_gpu_terrain_bake_grow(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m,
    uint32_t slot)
{
    bool result = true;
    WGPULimits limits = WGPU_LIMITS_INIT;
    uint32_t texels = box_gpu_terrain_bake_texels(m), rows = 0, columns, width;
    if (!texels) {
        result = false;
        goto done;
    }

    if (m->terrain_bake && m->terrain_bake_texels == texels) {
        rows = m->terrain_bake_rows;
        if (slot / m->terrain_bake_columns < rows) {
            goto done;
        }
    }

    if (wgpuDeviceGetLimits(gpu->device, &limits) != WGPUStatus_Success) {
        result = false;
        goto done;
    }

    width = texels + (texels + 1u) / 2u;
    columns = limits.maxTextureDimension2D / width;
    columns = columns > BOX_GPU_TERRAIN_BAKE_COLUMNS ? BOX_GPU_TERRAIN_BAKE_COLUMNS : columns;
    if (!columns) {
        result = false;
        goto done;
    }

    uint32_t need = slot / columns + 1u, most = limits.maxTextureDimension2D / texels;
    rows = rows * 2u > need ? rows * 2u : need;
    rows = rows < 2u ? 2u : rows;
    rows = rows > most ? most : rows;
    if (rows < need) {
        ecs_err("terrain bakes exceed device texture capacity");
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        result = false;
        goto done;
    }

    WGPUTexture old = NULL;
    box_gpu_terrain_bake_key_t *keys = NULL;
    uint32_t kept = 0;
    if (m->terrain_bake && m->terrain_bake_texels == texels &&
        m->terrain_bake_columns == columns)
    {
        old = m->terrain_bake;
        wgpuTextureAddRef(old);
        kept = m->terrain_bake_rows;
        keys = m->terrain_bake_keys;
        m->terrain_bake_keys = NULL;
    }

    box_gpu_terrain_bake_free(m);
    m->terrain_bake = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { columns * width, rows * texels, BOX_GPU_TERRAIN_BAKE_LAYERS },
        .format = BOX_GPU_TERRAIN_BAKE_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    if (old) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        wgpuCommandEncoderCopyTextureToTexture(encoder,
            &(WGPUTexelCopyTextureInfo){ .texture = old, .aspect = WGPUTextureAspect_All },
            &(WGPUTexelCopyTextureInfo){
                .texture = m->terrain_bake, .aspect = WGPUTextureAspect_All },
            &(WGPUExtent3D){ columns * width, kept * texels, BOX_GPU_TERRAIN_BAKE_LAYERS });
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureRelease(old);
    }

    m->terrain_bake_view = wgpuTextureCreateView(m->terrain_bake,
        &(WGPUTextureViewDescriptor){
            .format = BOX_GPU_TERRAIN_BAKE_FORMAT,
            .dimension = WGPUTextureViewDimension_2DArray,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = BOX_GPU_TERRAIN_BAKE_LAYERS,
            .aspect = WGPUTextureAspect_All
        });
    m->terrain_bake_texels = texels;
    m->terrain_bake_columns = columns;
    m->terrain_bake_rows = rows;
    m->terrain_bake_version = ++ gpu->texture_serial;
    m->terrain_bake_keys = ecs_os_calloc_n(box_gpu_terrain_bake_key_t,
        (int32_t)(columns * rows));
    if (keys) {
        ecs_os_memcpy_n(m->terrain_bake_keys, keys, box_gpu_terrain_bake_key_t,
            (int32_t)(columns * kept));
        ecs_os_free(keys);
    }

done:
    return result;
}

WGPUTextureView box_gpu_terrain_bake_view(
    box_gpu_t *gpu,
    const box_render_texture_blend_t *blend,
    uint32_t *layout,
    uint32_t *version)
{
    WGPUTextureView result;
    box_gpu_mesh_t *m = blend->terrain
        ? ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, blend->terrain) : NULL;
    *layout = 0;
    *version = 0;
    if (m && m->terrain_bake_view) {
        *layout = m->terrain_bake_texels | (m->terrain_bake_columns << 16);
        *version = m->terrain_bake_version;
        result = m->terrain_bake_view;
        goto done;
    }

    if (!gpu->terrain_bake_empty) {
        gpu->terrain_bake_empty = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
            .usage = WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { 1, 1, BOX_GPU_TERRAIN_BAKE_LAYERS },
            .format = BOX_GPU_TERRAIN_BAKE_FORMAT,
            .mipLevelCount = 1,
            .sampleCount = 1
        });
        gpu->terrain_bake_empty_view = wgpuTextureCreateView(gpu->terrain_bake_empty,
            &(WGPUTextureViewDescriptor){
                .format = BOX_GPU_TERRAIN_BAKE_FORMAT,
                .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = 0,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = BOX_GPU_TERRAIN_BAKE_LAYERS,
                .aspect = WGPUTextureAspect_All
            });
    }

    result = gpu->terrain_bake_empty_view;
done:
    return result;
}

/** Make the bind group layout of the bakes and the pipeline layouts of both variants, once.
 *
 * @param gpu The backend
 */
static void box_gpu_terrain_bake_layouts(
    box_gpu_t *gpu)
{
    if (gpu->terrain_bake_layout) {
        goto done;
    }

    WGPUBindGroupLayoutEntry entries[3] = {
        {
            .binding = BOX_GPU_TERRAIN_BAKE_PARAMS_BINDING,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_terrain_bake_params_t)
            }
        },
        {
            .binding = BOX_GPU_TERRAIN_BAKE_INDEX_BINDING,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = 4 * sizeof(uint32_t)
            }
        },
        {
            .binding = BOX_GPU_TERRAIN_BAKE_HEIGHTS_BINDING,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = 4 * sizeof(float)
            }
        }
    };
    gpu->terrain_bake_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 3,
            .entries = entries
        });
    WGPUBindGroupLayout groups[4] = {
        gpu->view_layout, gpu->blend_layout, gpu->instance_layout, gpu->terrain_bake_layout
    };
    gpu->terrain_bake_layouts[0] = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 4,
            .bindGroupLayouts = groups
        });
    box_gpu_layer_pipeline_layout(gpu, true);
    groups[1] = gpu->blend_layer_layout;
    gpu->terrain_bake_layouts[1] = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 4,
            .bindGroupLayouts = groups
        });
done:
    return;
}

/** Make a buffer hold at least a number of bytes, made again at least twice as large when it is
 * too small.
 *
 * @param gpu The backend
 * @param buffer The buffer, NULL for none
 * @param size Its bytes
 * @param bytes Bytes it must hold
 * @param usage Its usage, besides CopyDst
 */
static void box_gpu_terrain_bake_buffer(
    box_gpu_t *gpu,
    WGPUBuffer *buffer,
    uint64_t *size,
    uint64_t bytes,
    WGPUBufferUsage usage)
{
    if (*buffer && *size >= bytes) {
        goto done;
    }

    if (*buffer) {
        wgpuBufferRelease(*buffer);
    }

    *size = bytes > *size * 2u ? bytes : *size * 2u;
    *size = (*size + 255u) & ~(uint64_t)255u;
    *buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = usage | WGPUBufferUsage_CopyDst,
        .size = *size
    });
done:
    return;
}

/** Get the key of the sun of a view: its direction quantized, 0 without a directional light.
 *
 * @param v The view
 * @return The key
 */
static uint32_t box_gpu_terrain_bake_sun(
    const box_render_view_t *v)
{
    uint32_t result = 0, k;
    if (!v->lights.directional) {
        goto done;
    }

    for (k = 0; k < 3; k ++) {
        float c = v->lights.sun[k];
        c = c < -1.0f ? -1.0f : c > 1.0f ? 1.0f : c;
        result = result * 1024u + (uint32_t)((c * 0.5f + 0.5f) * 1023.0f + 0.5f);
    }

    result |= 0x80000000u;
done:
    return result;
}

/** Clear the bake of a cell whose tile changed and that does not bake in this packet, so its
 * fragments shade their material in full until it bakes, once per change.
 *
 * @param gpu The backend
 * @param m The mesh
 * @param slot The slot of the cell
 */
static void box_gpu_terrain_bake_clear(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m,
    uint32_t slot)
{
    uint32_t texels = m->terrain_bake_texels, width = texels + (texels + 1u) / 2u;
    box_gpu_terrain_bake_key_t *key = &m->terrain_bake_keys[slot];
    if (!key->baked) {
        goto done;
    }

    void *zero = ecs_os_calloc((ecs_size_t)(width * texels * 4u));
    wgpuQueueWriteTexture(gpu->queue,
        &(WGPUTexelCopyTextureInfo){
            .texture = m->terrain_bake,
            .origin = { (slot % m->terrain_bake_columns) * width,
                (slot / m->terrain_bake_columns) * texels, 0 },
            .aspect = WGPUTextureAspect_All
        },
        zero, (size_t)width * texels * 4u,
        &(WGPUTexelCopyBufferLayout){ .bytesPerRow = width * 4u, .rowsPerImage = texels },
        &(WGPUExtent3D){ width, texels, 1 });
    ecs_os_free(zero);
    ecs_os_memset_t(key, 0, box_gpu_terrain_bake_key_t);
done:
    return;
}

/** Collect the bakes of a packet in the scene of a view whose tile changed since it was baked,
 * up to BOX_GPU_TERRAIN_BAKE_TILES: first the new tiles, then those whose material or sun
 * changed. A new tile beyond them clears its cell.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param v The view
 * @param list Receives two draws per bake, the tile and its coarser level
 * @param keys Receives the key of each bake
 * @return Draws in list
 */
static int32_t box_gpu_terrain_bake_collect(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    box_gpu_terrain_bake_draw_t *list,
    box_gpu_terrain_bake_key_t *keys)
{
    const box_render_terrain_bake_t *bakes = ecs_vec_first(&packet->terrain_bakes);
    int32_t count = ecs_vec_count(&packet->terrain_bakes), i, pass, draws = 0;
    uint32_t sun = box_gpu_terrain_bake_sun(v);
    int32_t batch = -1;
    WGPUBindGroup material = NULL;
    uint64_t content = 0;
    for (pass = 0; pass < 2; pass ++) {
        for (i = 0; i < count; i ++) {
            const box_render_terrain_bake_t *k = &bakes[i];
            const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t,
                k->batch);
            box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, k->mesh);
            if (b->scene != v->scene || !m || !m->terrain_serials || !m->terrain_mask_serials ||
                (int64_t)k->slot >= m->terrain_slots ||
                !box_gpu_terrain_bake_grow(gpu, m, k->slot))
            {
                continue;
            }

            if (k->batch != batch) {
                batch = k->batch;
                material = box_gpu_forward_group(gpu, packet, b);
                content = box_gpu_forward_content(gpu, packet, b);
            }

            box_gpu_terrain_bake_key_t key = {
                .serials = {
                    m->terrain_serials[k->slot * 2u], m->terrain_serials[k->slot * 2u + 1u],
                    m->terrain_mask_serials[k->slot * 2u],
                    m->terrain_mask_serials[k->slot * 2u + 1u]
                },
                .material = content,
                .sun = sun,
                .baked = 1
            };
            const box_gpu_terrain_bake_key_t *old = &m->terrain_bake_keys[k->slot];
            if (!key.serials[0] || !key.serials[1] || !key.serials[2] || !key.serials[3] ||
                !material || !ecs_os_memcmp(old, &key, ECS_SIZEOF(key)))
            {
                continue;
            }

            bool fresh = !old->baked ||
                ecs_os_memcmp(old->serials, key.serials, ECS_SIZEOF(key.serials));
            if (fresh != (pass == 0)) {
                continue;
            }

            if (draws >= 2 * BOX_GPU_TERRAIN_BAKE_TILES) {
                if (fresh) {
                    box_gpu_terrain_bake_clear(gpu, m, k->slot);
                }

                continue;
            }

            keys[draws / 2] = key;
            list[draws ++] = (box_gpu_terrain_bake_draw_t){
                .bake = k, .mesh = m, .coarse = false };
            list[draws ++] = (box_gpu_terrain_bake_draw_t){
                .bake = k, .mesh = m, .coarse = true };
        }
    }

    return draws;
}

/** Make the scratch image hold the cells of a number of bakes of meshes with bakes of some
 * texels, made again for other texels.
 *
 * @param gpu The backend
 * @param texels Texels of the bakes of a cell
 * @param tiles Cells it holds
 * @return Whether it holds them
 */
static bool box_gpu_terrain_bake_scratch(
    box_gpu_t *gpu,
    uint32_t texels,
    uint32_t tiles)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    uint32_t width = texels + (texels + 1u) / 2u, columns, rows, i;
    bool result = true;
    if (gpu->terrain_bake_scratch && gpu->terrain_bake_scratch_texels == texels) {
        goto done;
    }

    if (wgpuDeviceGetLimits(gpu->device, &limits) != WGPUStatus_Success) {
        result = false;
        goto done;
    }

    columns = limits.maxTextureDimension2D / width;
    columns = columns > tiles ? tiles : columns;
    rows = columns ? (tiles + columns - 1u) / columns : 0u;
    if (!columns || rows * texels > limits.maxTextureDimension2D) {
        result = false;
        goto done;
    }

    for (i = 0; i < BOX_GPU_TERRAIN_BAKE_LAYERS; i ++) {
        if (gpu->terrain_bake_scratch_layers[i]) {
            wgpuTextureViewRelease(gpu->terrain_bake_scratch_layers[i]);
        }
    }

    if (gpu->terrain_bake_scratch) {
        wgpuTextureRelease(gpu->terrain_bake_scratch);
    }

    gpu->terrain_bake_scratch = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { columns * width, rows * texels, BOX_GPU_TERRAIN_BAKE_LAYERS },
        .format = BOX_GPU_TERRAIN_BAKE_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    for (i = 0; i < BOX_GPU_TERRAIN_BAKE_LAYERS; i ++) {
        gpu->terrain_bake_scratch_layers[i] = wgpuTextureCreateView(gpu->terrain_bake_scratch,
            &(WGPUTextureViewDescriptor){
                .format = BOX_GPU_TERRAIN_BAKE_FORMAT,
                .dimension = WGPUTextureViewDimension_2D,
                .baseMipLevel = 0,
                .mipLevelCount = 1,
                .baseArrayLayer = i,
                .arrayLayerCount = 1,
                .aspect = WGPUTextureAspect_All
            });
    }

    gpu->terrain_bake_scratch_texels = texels;
    gpu->terrain_bake_scratch_columns = columns;
done:
    return result;
}

/** Get the pixel of the cell of the nth bake of a packet in the scratch image.
 *
 * @param gpu The backend
 * @param tile The bake
 * @param x Receives the column
 * @param y Receives the row
 */
static void box_gpu_terrain_bake_cell(
    const box_gpu_t *gpu,
    uint32_t tile,
    uint32_t *x,
    uint32_t *y)
{
    uint32_t texels = gpu->terrain_bake_scratch_texels;
    *x = (tile % gpu->terrain_bake_scratch_columns) * (texels + (texels + 1u) / 2u);
    *y = (tile / gpu->terrain_bake_scratch_columns) * texels;
}

/** Fill the parameters of the draws of the bakes of a packet, which render into the scratch
 * image.
 *
 * @param gpu The backend
 * @param v The view
 * @param list The draws
 * @param draws Number of draws
 * @param bases Word offset of each terrain index in the index buffer
 * @param params Receives the parameters of each draw, BOX_GPU_TERRAIN_BAKE_STRIDE apart
 * @param slots Receives the instance slot of each draw
 */
static void box_gpu_terrain_bake_params(
    const box_gpu_t *gpu,
    const box_render_view_t *v,
    const box_gpu_terrain_bake_draw_t *list,
    int32_t draws,
    const uint32_t *bases,
    uint8_t *params,
    uint32_t *slots)
{
    int32_t i;
    for (i = 0; i < draws; i ++) {
        const box_render_terrain_bake_t *k = list[i].bake;
        const box_gpu_mesh_t *m = list[i].mesh;
        box_gpu_terrain_bake_params_t *p = (box_gpu_terrain_bake_params_t*)(void*)
            (params + (size_t)i * BOX_GPU_TERRAIN_BAKE_STRIDE);
        uint32_t texels = m->terrain_bake_texels, coarse = (texels + 1u) / 2u, x, y;
        uint32_t bake = list[i].coarse ? coarse : texels;
        float span = ldexpf(1.0f, k->level);
        uint32_t w = m->terrain_mask_texels;
        float steps = (float)(w - 1u - 2u * BOX_TERRAIN_BORDER);
        box_gpu_terrain_bake_cell(gpu, (uint32_t)i / 2u, &x, &y);
        p->cell[0] = (float)(x + (list[i].coarse ? texels : 0u));
        p->cell[1] = (float)y;
        p->cell[2] = (float)bake;
        p->cell[3] = list[i].coarse ? 1.0f : 0.0f;
        p->tile[0] = (float)k->ix * span;
        p->tile[1] = (float)k->iz * span;
        p->tile[2] = span;
        p->tile[3] = 0.5f * steps / (float)(bake - 1u);
        p->sun[0] = v->lights.sun[0];
        p->sun[1] = v->lights.sun[1];
        p->sun[2] = v->lights.sun[2];
        p->sun[3] = v->lights.directional ? 1.0f : 0.0f;
        p->grid[0] = (float)k->slot;
        p->grid[1] = (float)(((w * 4u + 255u) & ~255u) / 4u);
        p->grid[2] = (float)w;
        p->grid[3] = steps + 1.0f;
        p->info[0] = bases[k->index];
        slots[i] = k->instance;
    }
}

/** Record the draws of the bakes of one grid mesh in a render pass into the scratch image, copy
 * the cells that baked into the bake atlas of the mesh and keep their keys.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param packet The packet
 * @param group The bind group of the view
 * @param view_offset Dynamic offset of the view in it
 * @param list The draws, those of the mesh from start to end
 * @param keys The key of each bake, one per two draws
 * @param params The parameters of the draws
 * @param start First draw of the mesh
 * @param end Draw after the last one of the mesh
 */
static void box_gpu_terrain_bake_pass(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    WGPUBindGroup group,
    uint32_t view_offset,
    const box_gpu_terrain_bake_draw_t *list,
    const box_gpu_terrain_bake_key_t *keys,
    const uint8_t *params,
    int32_t start,
    int32_t end)
{
    box_gpu_mesh_t *m = list[start].mesh;
    WGPURenderPassColorAttachment attachments[BOX_GPU_TERRAIN_BAKE_LAYERS];
    int32_t a, i, batch = -1;
    bool ready = false, *baked = ecs_os_calloc_n(bool, end - start);
    WGPUBindGroupEntry entries[3] = {
        {
            .binding = BOX_GPU_TERRAIN_BAKE_PARAMS_BINDING,
            .buffer = gpu->terrain_bake_params,
            .size = sizeof(box_gpu_terrain_bake_params_t)
        },
        {
            .binding = BOX_GPU_TERRAIN_BAKE_INDEX_BINDING,
            .buffer = gpu->terrain_bake_index,
            .size = gpu->terrain_bake_index_size
        },
        {
            .binding = BOX_GPU_TERRAIN_BAKE_HEIGHTS_BINDING,
            .buffer = m->surface_assets,
            .size = m->surface_asset_size
        }
    };
    WGPUBindGroup bake_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->terrain_bake_layout,
        .entryCount = 3,
        .entries = entries
    });
    for (a = 0; a < BOX_GPU_TERRAIN_BAKE_LAYERS; a ++) {
        attachments[a] = (WGPURenderPassColorAttachment){
            .view = gpu->terrain_bake_scratch_layers[a],
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store
        };
    }

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,
        &(WGPURenderPassDescriptor){
            .colorAttachmentCount = BOX_GPU_TERRAIN_BAKE_LAYERS,
            .colorAttachments = attachments
        });
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &view_offset);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->buffer, m->offset, m->vertex_size);
    wgpuRenderPassEncoderSetIndexBuffer(pass, m->buffer, WGPUIndexFormat_Uint32,
        m->index_offset, m->index_size);
    for (i = start; i < end; i ++) {
        const box_render_terrain_bake_t *k = list[i].bake;
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t,
            k->batch);
        const box_gpu_terrain_bake_params_t *p = (const box_gpu_terrain_bake_params_t*)
            (const void*)(params + (size_t)i * BOX_GPU_TERRAIN_BAKE_STRIDE);
        if (k->batch != batch) {
            batch = k->batch;
            WGPURenderPipeline pipeline = box_gpu_terrain_bake_pipeline(gpu, b,
                gpu->terrain_bake_layouts[box_gpu_layered(b) ? 1 : 0]);
            WGPUBindGroup instances = box_gpu_instance_group(gpu, b->instances, b->mesh, m,
                true);
            WGPUBindGroup material = box_gpu_forward_group(gpu, packet, b);
            uint32_t kind = box_gpu_draw_kind(b, false);
            ready = pipeline && instances && material;
            if (ready) {
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 1, material, 1, &kind);
                wgpuRenderPassEncoderSetBindGroup(pass, 2, instances, 0, NULL);
            }
        }

        if (!ready) {
            continue;
        }

        uint32_t offset = (uint32_t)i * BOX_GPU_TERRAIN_BAKE_STRIDE;
        wgpuRenderPassEncoderSetBindGroup(pass, 3, bake_group, 1, &offset);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 1, gpu->terrain_bake_slots,
            (uint64_t)i * sizeof(uint32_t), sizeof(uint32_t));
        wgpuRenderPassEncoderSetViewport(pass, p->cell[0], p->cell[1], p->cell[2], p->cell[2],
            0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)p->cell[0], (uint32_t)p->cell[1],
            (uint32_t)p->cell[2], (uint32_t)p->cell[2]);
        wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, 1, 0, 0, 0);
        baked[i - start] = true;
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bake_group);

    uint32_t texels = m->terrain_bake_texels, width = texels + (texels + 1u) / 2u;
    for (i = start; i + 1 < end; i += 2) {
        uint32_t slot = list[i].bake->slot, x, y;
        if (!baked[i - start] || !baked[i + 1 - start]) {
            continue;
        }

        box_gpu_terrain_bake_cell(gpu, (uint32_t)i / 2u, &x, &y);
        wgpuCommandEncoderCopyTextureToTexture(encoder,
            &(WGPUTexelCopyTextureInfo){
                .texture = gpu->terrain_bake_scratch,
                .origin = { x, y, 0 },
                .aspect = WGPUTextureAspect_All
            },
            &(WGPUTexelCopyTextureInfo){
                .texture = m->terrain_bake,
                .origin = { (slot % m->terrain_bake_columns) * width,
                    (slot / m->terrain_bake_columns) * texels, 0 },
                .aspect = WGPUTextureAspect_All
            },
            &(WGPUExtent3D){ width, texels, BOX_GPU_TERRAIN_BAKE_LAYERS });
        m->terrain_bake_keys[slot] = keys[i / 2];
    }

    ecs_os_free(baked);
}

void box_gpu_terrain_bake(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot)
{
    const box_render_terrain_index_t *indices = ecs_vec_first(&packet->terrain_indices);
    int32_t count = ecs_vec_count(&packet->terrain_bakes), i, draws, start, end;
    int32_t index_count = ecs_vec_count(&packet->terrain_indices);
    uint32_t words = 0, texels = 0, *bases = NULL, *slots = NULL;
    box_gpu_terrain_bake_draw_t *list = NULL;
    box_gpu_terrain_bake_key_t *keys = NULL;
    uint8_t *params = NULL;
    if (!count || !index_count || !v->lights.enabled) {
        goto done;
    }

    box_gpu_terrain_bake_layouts(gpu);
    list = ecs_os_malloc_n(box_gpu_terrain_bake_draw_t, count * 2);
    keys = ecs_os_malloc_n(box_gpu_terrain_bake_key_t, count);
    draws = box_gpu_terrain_bake_collect(gpu, packet, v, list, keys);
    for (i = 0; i < draws; i ++) {
        texels = list[i].mesh->terrain_bake_texels > texels
            ? list[i].mesh->terrain_bake_texels : texels;
    }

    if (!draws || !box_gpu_terrain_bake_scratch(gpu, texels, BOX_GPU_TERRAIN_BAKE_TILES)) {
        goto done;
    }

    bases = ecs_os_malloc_n(uint32_t, index_count);
    for (i = 0; i < index_count; i ++) {
        bases[i] = words;
        words += indices[i].count;
    }

    box_gpu_terrain_bake_buffer(gpu, &gpu->terrain_bake_index, &gpu->terrain_bake_index_size,
        (uint64_t)words * sizeof(uint32_t), WGPUBufferUsage_Storage);
    for (i = 0; i < index_count; i ++) {
        wgpuQueueWriteBuffer(gpu->queue, gpu->terrain_bake_index,
            (uint64_t)bases[i] * sizeof(uint32_t),
            box_render_packet_data(packet, indices[i].data),
            (size_t)indices[i].count * sizeof(uint32_t));
    }

    params = ecs_os_calloc((ecs_size_t)((size_t)draws * BOX_GPU_TERRAIN_BAKE_STRIDE));
    slots = ecs_os_malloc_n(uint32_t, draws);
    box_gpu_terrain_bake_params(gpu, v, list, draws, bases, params, slots);
    box_gpu_terrain_bake_buffer(gpu, &gpu->terrain_bake_params, &gpu->terrain_bake_params_size,
        (uint64_t)draws * BOX_GPU_TERRAIN_BAKE_STRIDE, WGPUBufferUsage_Uniform);
    box_gpu_terrain_bake_buffer(gpu, &gpu->terrain_bake_slots, &gpu->terrain_bake_slots_size,
        (uint64_t)draws * sizeof(uint32_t), WGPUBufferUsage_Vertex);
    wgpuQueueWriteBuffer(gpu->queue, gpu->terrain_bake_params, 0, params,
        (size_t)draws * BOX_GPU_TERRAIN_BAKE_STRIDE);
    wgpuQueueWriteBuffer(gpu->queue, gpu->terrain_bake_slots, 0, slots,
        (size_t)draws * sizeof(uint32_t));
    for (start = 0; start < draws; start = end) {
        for (end = start; end < draws && list[end].mesh == list[start].mesh; end ++) { }
        box_gpu_terrain_bake_pass(gpu, encoder, packet, group,
            (uint32_t)slot * BOX_GPU_VIEW_STRIDE, list, keys, params, start, end);
    }

done:
    ecs_os_free(bases);
    ecs_os_free(list);
    ecs_os_free(keys);
    ecs_os_free(params);
    ecs_os_free(slots);
}

void box_gpu_terrain_fini(
    box_gpu_t *gpu)
{
    WGPUBuffer *buffers[3] = {
        &gpu->terrain_bake_params, &gpu->terrain_bake_index, &gpu->terrain_bake_slots
    };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        if (gpu->terrain_bake_layouts[i]) {
            wgpuPipelineLayoutRelease(gpu->terrain_bake_layouts[i]);
            gpu->terrain_bake_layouts[i] = NULL;
        }
    }

    if (gpu->terrain_bake_layout) {
        wgpuBindGroupLayoutRelease(gpu->terrain_bake_layout);
        gpu->terrain_bake_layout = NULL;
    }

    for (i = 0; i < 3; i ++) {
        if (*buffers[i]) {
            wgpuBufferRelease(*buffers[i]);
            *buffers[i] = NULL;
        }
    }

    for (i = 0; i < BOX_GPU_TERRAIN_BAKE_LAYERS; i ++) {
        if (gpu->terrain_bake_scratch_layers[i]) {
            wgpuTextureViewRelease(gpu->terrain_bake_scratch_layers[i]);
            gpu->terrain_bake_scratch_layers[i] = NULL;
        }
    }

    if (gpu->terrain_bake_scratch) {
        wgpuTextureRelease(gpu->terrain_bake_scratch);
        gpu->terrain_bake_scratch = NULL;
    }

    if (gpu->terrain_bake_empty_view) {
        wgpuTextureViewRelease(gpu->terrain_bake_empty_view);
        gpu->terrain_bake_empty_view = NULL;
    }

    if (gpu->terrain_bake_empty) {
        wgpuTextureRelease(gpu->terrain_bake_empty);
        gpu->terrain_bake_empty = NULL;
    }
}

#endif
