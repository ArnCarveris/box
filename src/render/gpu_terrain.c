/**
 * @file render/gpu_terrain.c
 * @brief The heights of the terrain tiles in the surface assets of their grid meshes: the WebGPU
 * backend copies the R32Float image of each tile into its slot, which the vertex stages read.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

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
                continue;
            }

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
