/**
 * @file render/gpu_terrain.c
 * @brief The heights of the terrain tiles in the surface assets of their grid meshes: the WebGPU
 * backend copies the R32Float image of each tile into its slot, which the vertex stages read,
 * and the masks of a tile with masks into its cell of the mask atlas of the mesh, which the
 * fragment stages of a terrain TextureBlend read.
 *
 * The bindings of the terrains with TerrainSampled: per terrain the params and the cells of the
 * windows of its levels, bound with the heights and the mask atlas of its grid mesh, which the
 * compute and vertex stages read by world position (etc/shaders/include/terrain_sample.wgsl).
 */

#include "gpu.h"
#include "preprocess.h"
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

/** Bindings of the bind group of a sampled terrain (terrain_sample.wgsl) */
#define BOX_GPU_TERRAIN_SAMPLE_BINDINGS (4)

/** Bytes of the grid of a sampled terrain without cells */
#define BOX_GPU_TERRAIN_SAMPLE_EMPTY (16u)

_Static_assert(sizeof(box_gpu_terrain_sample_level_t) == 32,
    "TerrainSampleLevel of terrain_sample.wgsl");
_Static_assert(sizeof(box_gpu_terrain_sample_params_t) == 640,
    "TerrainSampleParams of terrain_sample.wgsl");

void box_gpu_terrain_init(
    box_gpu_t *gpu)
{
    WGPUShaderStage stages = WGPUShaderStage_Compute | WGPUShaderStage_Vertex |
        WGPUShaderStage_Fragment;
    WGPUBindGroupLayoutEntry entries[BOX_GPU_TERRAIN_SAMPLE_BINDINGS] = {
        {
            .binding = 0,
            .visibility = stages,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_terrain_sample_params_t)
            }
        },
        {
            .binding = 1,
            .visibility = stages,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 2,
            .visibility = stages,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 3,
            .visibility = stages,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    gpu->terrain_sample_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = BOX_GPU_TERRAIN_SAMPLE_BINDINGS,
            .entries = entries
        });
    ecs_map_init(&gpu->terrain_samples, NULL);
}

/** Release the bindings of a sampled terrain.
 *
 * @param ts The bindings, freed
 */
static void box_gpu_terrain_sample_free(
    box_gpu_terrain_sample_t *ts)
{
    if (ts->group) {
        wgpuBindGroupRelease(ts->group);
    }

    if (ts->params) {
        wgpuBufferRelease(ts->params);
    }

    if (ts->grid) {
        wgpuBufferRelease(ts->grid);
    }

    ecs_os_free(ts->cells);
    ecs_os_free(ts);
}

void box_gpu_terrain_fini(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->terrain_samples);
    while (ecs_map_next(&it)) {
        box_gpu_terrain_sample_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&gpu->terrain_samples);
    if (gpu->terrain_sample_layout) {
        wgpuBindGroupLayoutRelease(gpu->terrain_sample_layout);
        gpu->terrain_sample_layout = NULL;
    }
}

/** Split an absolute coordinate into whole meters and the rest of a meter.
 *
 * @param v The coordinate
 * @param whole Receives the whole meters, clamped to int32_t
 * @param rest Receives the rest, 0 to 1
 */
static void box_gpu_terrain_split(
    double v,
    int32_t *whole,
    float *rest)
{
    double w = floor(v);
    w = w < (double)INT32_MIN ? (double)INT32_MIN : w > (double)INT32_MAX ? (double)INT32_MAX : w;
    *whole = (int32_t)w;
    *rest = (float)(v - w);
}

/** Fill the params of a sampled terrain.
 *
 * @param packet The packet
 * @param s The terrain
 * @param m Its grid mesh, NULL before a tile draws
 * @param out Receives the params
 */
static void box_gpu_terrain_params(
    const box_render_packet_t *packet,
    const box_render_terrain_sample_t *s,
    const box_gpu_mesh_t *m,
    box_gpu_terrain_sample_params_t *out)
{
    int32_t levels = s->levels > BOX_RENDER_TERRAIN_LEVELS
        ? BOX_RENDER_TERRAIN_LEVELS : s->levels, foci, i;
    uint32_t texels = (uint32_t)s->resolution + 4u;
    foci = s->focus_count > BOX_RENDER_TERRAIN_FOCI ? BOX_RENDER_TERRAIN_FOCI : s->focus_count;
    ecs_os_memset_t(out, 0, box_gpu_terrain_sample_params_t);
    if (!m || !m->terrain_slots || levels <= 0 || s->resolution <= 0) {
        levels = 0;
    }

    out->counts[0] = (uint32_t)levels;
    out->counts[1] = (uint32_t)s->cells;
    out->counts[2] = (uint32_t)s->resolution;
    out->counts[3] = (uint32_t)foci;
    if (levels) {
        out->layout[0] = (uint32_t)(m->terrain_slot_size / texels / 4u);
        out->layout[1] = (uint32_t)(m->terrain_slot_size / 4u);
        if (s->masks && m->terrain_mask_view && m->terrain_mask_texels == texels) {
            out->layout[2] = m->terrain_mask_texels;
            out->layout[3] = m->terrain_mask_columns;
        }
    }

    box_gpu_terrain_split(s->origin[0], &out->origin_whole[0], &out->origin[0]);
    box_gpu_terrain_split(s->origin[2], &out->origin_whole[1], &out->origin[1]);
    out->origin_whole[2] = s->extent > 0.0 ? 1 : 0;
    out->origin[2] = (float)s->extent;
    out->origin[3] = (float)(s->origin[1] - packet->origin[1]);
    double corner[2] = {
        (double)s->first[0][0] * s->tile_size,
        (double)s->first[0][1] * s->tile_size
    };
    for (i = 0; i < foci; i ++) {
        out->foci[i / 2][(i % 2) * 2] = (float)(s->foci[i][0] - corner[0]);
        out->foci[i / 2][(i % 2) * 2 + 1] = (float)(s->foci[i][1] - corner[1]);
    }

    for (i = 0; i < levels; i ++) {
        box_gpu_terrain_sample_level_t *l = &out->levels[i];
        double size = ldexp(s->tile_size, i);
        box_gpu_terrain_split(s->origin[0] + (double)s->first[i][0] * size, &l->whole[0],
            &l->rest[0]);
        box_gpu_terrain_split(s->origin[2] + (double)s->first[i][1] * size, &l->whole[1],
            &l->rest[1]);
        l->size = (float)size;
        l->morph[0] = s->morph[i][0];
        l->morph[1] = s->morph[i][1];
        l->first = (uint32_t)(i * s->cells * s->cells);
    }
}

/** Write the params and the cells of a sampled terrain when they changed and make its bind
 * group again when what it binds changed.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param s The terrain
 */
static void box_gpu_terrain_sample(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_terrain_sample_t *s)
{
    box_gpu_terrain_sample_t *ts = ecs_map_get_deref(&gpu->terrain_samples,
        box_gpu_terrain_sample_t, s->terrain);
    box_gpu_mesh_t *m = s->mesh
        ? ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, s->mesh) : NULL;
    box_gpu_terrain_sample_params_t params;
    int32_t i;
    if (!ts) {
        ts = ecs_os_calloc_t(box_gpu_terrain_sample_t);
        ecs_map_insert_ptr(&gpu->terrain_samples, s->terrain, ts);
    }

    ts->frame = packet->frame;
    box_gpu_terrain_params(packet, s, m, &params);
    int32_t count = params.counts[0] ? (int32_t)params.counts[0] * s->cells * s->cells : 0;
    uint64_t bytes = (uint64_t)count * sizeof(uint32_t);
    bytes = bytes < BOX_GPU_TERRAIN_SAMPLE_EMPTY ? BOX_GPU_TERRAIN_SAMPLE_EMPTY : bytes;
    if (!ts->params) {
        ts->params = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(box_gpu_terrain_sample_params_t)
        });
        ecs_os_memset_t(&ts->written, 0, box_gpu_terrain_sample_params_t);
        wgpuQueueWriteBuffer(gpu->queue, ts->params, 0, &params, sizeof(params));
        ts->written = params;
    } else if (ecs_os_memcmp(&ts->written, &params, ECS_SIZEOF(params))) {
        wgpuQueueWriteBuffer(gpu->queue, ts->params, 0, &params, sizeof(params));
        ts->written = params;
    }

    if (!ts->grid || ts->grid_size < bytes) {
        if (ts->grid) {
            wgpuBufferRelease(ts->grid);
        }

        ts->grid = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            .size = bytes
        });
        ts->grid_size = bytes;
        ecs_os_free(ts->cells);
        ts->cells = NULL;
        ts->cell_count = 0;
    }

    if (count) {
        const uint32_t *cells = box_render_packet_data(packet, s->grid);
        uint32_t *valid = ecs_os_malloc_n(uint32_t, count);
        for (i = 0; i < count; i ++) {
            uint32_t v = cells[i];
            if (v && ((int64_t)(v - 1u) >= m->terrain_slots || !m->terrain_serials ||
                !m->terrain_serials[v - 1u]))
            {
                v = 0;
            }

            valid[i] = v;
        }

        if (ts->cell_count != count ||
            ecs_os_memcmp(ts->cells, valid, count * ECS_SIZEOF(uint32_t)))
        {
            wgpuQueueWriteBuffer(gpu->queue, ts->grid, 0, valid,
                (size_t)count * sizeof(uint32_t));
            ecs_os_free(ts->cells);
            ts->cells = valid;
            ts->cell_count = count;
        } else {
            ecs_os_free(valid);
        }
    }

    WGPUBuffer heights = m && m->surface_assets ? m->surface_assets : gpu->no_assets;
    WGPUTextureView masks = m && m->terrain_mask_view
        ? m->terrain_mask_view : gpu->white->linear;
    if (ts->group && ts->group_heights == heights && ts->group_masks == masks &&
        ts->group_grid == ts->grid)
    {
        goto done;
    }

    if (ts->group) {
        wgpuBindGroupRelease(ts->group);
    }

    WGPUBindGroupEntry entries[BOX_GPU_TERRAIN_SAMPLE_BINDINGS] = {
        {
            .binding = 0,
            .buffer = ts->params,
            .size = sizeof(box_gpu_terrain_sample_params_t)
        },
        { .binding = 1, .buffer = ts->grid, .size = ts->grid_size },
        { .binding = 2, .buffer = heights, .size = WGPU_WHOLE_SIZE },
        { .binding = 3, .textureView = masks }
    };
    ts->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->terrain_sample_layout,
        .entryCount = BOX_GPU_TERRAIN_SAMPLE_BINDINGS,
        .entries = entries
    });
    ts->group_heights = heights;
    ts->group_masks = masks;
    ts->group_grid = ts->grid;
done:
    return;
}

void box_gpu_terrain_samples(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->terrain_samples);
    ecs_entity_t stale[16];
    int32_t stale_count;
    for (i = 0; i < count; i ++) {
        box_gpu_terrain_sample(gpu, packet, ecs_vec_get_t(&packet->terrain_samples,
            box_render_terrain_sample_t, i));
    }

    do {
        ecs_map_iter_t it = ecs_map_iter(&gpu->terrain_samples);
        stale_count = 0;
        while (ecs_map_next(&it) && stale_count < 16) {
            box_gpu_terrain_sample_t *ts = ecs_map_ptr(&it);
            if (ts->frame != packet->frame) {
                stale[stale_count ++] = ecs_map_key(&it);
            }
        }

        for (i = 0; i < stale_count; i ++) {
            box_gpu_terrain_sample_free(ecs_map_get_deref(&gpu->terrain_samples,
                box_gpu_terrain_sample_t, stale[i]));
            ecs_map_remove(&gpu->terrain_samples, stale[i]);
        }
    } while (stale_count);
}

WGPUBindGroup box_gpu_terrain_sample_group(
    box_gpu_t *gpu,
    ecs_entity_t terrain)
{
    box_gpu_terrain_sample_t *ts = ecs_map_get_deref(&gpu->terrain_samples,
        box_gpu_terrain_sample_t, terrain);

    return ts ? ts->group : NULL;
}

bool box_render_gpu_terrain_compute(
    ecs_world_t *world,
    ecs_entity_t terrain,
    const char *path,
    const void *in,
    uint32_t in_size,
    void *out,
    uint32_t out_size,
    uint32_t groups)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    box_shader_output_t code = {0};
    WGPUShaderModule module = NULL;
    WGPUBindGroupLayout layout = NULL;
    WGPUPipelineLayout pipeline_layout = NULL;
    WGPUComputePipeline pipeline = NULL;
    WGPUBuffer input = NULL, output = NULL;
    WGPUBindGroup group = NULL;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready || !out_size || (out_size & 3) ||
        (in_size & 3) || !groups)
    {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_terrain_compute needs RenderSettings.main_thread and box_render_sync");
    WGPUBindGroup sampled = renders ? box_gpu_terrain_sample_group(gpu, terrain) : NULL;
    if (!sampled) {
        goto done;
    }

    if (box_shader_preprocess(&(box_shader_desc_t){ .path = path }, &code)) {
        ecs_err("%s", code.error ? code.error : path);
        goto done;
    }

    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = code.code, .length = WGPU_STRLEN }
    };
    module = wgpuDeviceCreateShaderModule(gpu->device, &(WGPUShaderModuleDescriptor){
        .nextInChain = &wgsl.chain,
        .label = { .data = path, .length = WGPU_STRLEN }
    });
    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }
    };
    layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &(WGPUBindGroupLayoutDescriptor){
        .entryCount = 2,
        .entries = entries
    });
    WGPUBindGroupLayout layouts[2] = { gpu->terrain_sample_layout, layout };
    pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = layouts
        });
    pipeline = wgpuDeviceCreateComputePipeline(gpu->device, &(WGPUComputePipelineDescriptor){
        .layout = pipeline_layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = "main", .length = WGPU_STRLEN }
        }
    });
    uint64_t in_bytes = in_size ? in_size : BOX_GPU_TERRAIN_SAMPLE_EMPTY;
    input = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = in_bytes
    });
    if (in_size) {
        wgpuQueueWriteBuffer(gpu->queue, input, 0, in, in_size);
    }

    output = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc,
        .size = out_size
    });
    WGPUBindGroupEntry bindings[2] = {
        { .binding = 0, .buffer = input, .size = in_bytes },
        { .binding = 1, .buffer = output, .size = out_size }
    };
    group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = layout,
        .entryCount = 2,
        .entries = bindings
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    WGPUComputePassEncoder pass = box_gpu_compute_begin(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, sampled, 0, NULL);
    wgpuComputePassEncoderSetBindGroup(pass, 1, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    result = box_gpu_read(gpu, output, 0, out_size, out);
done:
    if (group) {
        wgpuBindGroupRelease(group);
    }

    if (output) {
        wgpuBufferRelease(output);
    }

    if (input) {
        wgpuBufferRelease(input);
    }

    if (pipeline) {
        wgpuComputePipelineRelease(pipeline);
    }

    if (pipeline_layout) {
        wgpuPipelineLayoutRelease(pipeline_layout);
    }

    if (layout) {
        wgpuBindGroupLayoutRelease(layout);
    }

    if (module) {
        wgpuShaderModuleRelease(module);
    }

    box_shader_output_fini(&code);

    return result;
}

#else

bool box_render_gpu_terrain_compute(
    ecs_world_t *world,
    ecs_entity_t terrain,
    const char *path,
    const void *in,
    uint32_t in_size,
    void *out,
    uint32_t out_size,
    uint32_t groups)
{
    (void)world;
    (void)terrain;
    (void)path;
    (void)in;
    (void)in_size;
    (void)out;
    (void)out_size;
    (void)groups;

    return false;
}

#endif
