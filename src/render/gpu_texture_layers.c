/**
 * @file render/gpu_texture_layers.c
 * @brief The bind groups, layer buffers and layouts of the TextureLayers batches of the webgpu
 * backend.
 *
 * A layers group binds the entries of the material or blend group of its batch, one texture
 * array of the maps of its layers and the uniform buffer of each layer entity, which every
 * batch with the layer shares and which is written only when the values of the layer change.
 */

#include "gpu.h"
#include "gpu_texture_blend.h"

#ifdef BOX_RENDER_WGPU

/** Binding of the texture array of the layer maps in the layers group of the plain variant
 * (layer_maps of mesh_layers.wgsl); the blend variant binds them in the heights after the
 * heights of its layers */
#define BOX_GPU_LAYER_MAPS_BINDING (13)

/** Binding of the uniform buffer of the first layer, the others follow */
#define BOX_GPU_LAYER_UNIFORMS_BINDING (14)

/** Entries of the layers group of the plain variant: the material group, the array and the four
 * layer buffers */
#define BOX_GPU_LAYER_ENTRIES (BOX_RENDER_TEXTURE_SLOTS + 3 + 1 + BOX_RENDER_LAYERS)

/** Entries of the layers group of the blend variant: the blend group and the four layer
 * buffers */
#define BOX_GPU_BLEND_LAYER_ENTRIES (BOX_GPU_BLEND_ENTRIES + BOX_RENDER_LAYERS)

_Static_assert(sizeof(box_render_texture_layer_t) == 128, "TextureLayer of mesh_layers.wgsl");

/** Make a fragment texture entry of a bind group layout.
 *
 * @param binding The binding
 * @param dimension Dimension of the view
 * @return The entry
 */
static WGPUBindGroupLayoutEntry box_gpu_layer_texture_entry(
    uint32_t binding,
    WGPUTextureViewDimension dimension)
{
    return (WGPUBindGroupLayoutEntry){
        .binding = binding,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = dimension
        }
    };
}

/** Make the bind group layouts and pipeline layouts of the layers variants, once. A scene
 * without a TextureLayers batch never makes them.
 *
 * @param gpu The backend
 */
static void box_gpu_layer_layouts(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry plain[BOX_GPU_LAYER_ENTRIES];
    WGPUBindGroupLayoutEntry blend[BOX_GPU_BLEND_LAYER_ENTRIES];
    WGPUBindGroupLayoutEntry kind = {
        .binding = BOX_GPU_DRAW_KIND_BINDING,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = true,
            .minBindingSize = 4 * sizeof(uint32_t)
        }
    };
    WGPUBindGroupLayoutEntry sampler = {
        .binding = BOX_RENDER_TEXTURE_SLOTS,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = { .type = WGPUSamplerBindingType_Filtering }
    };
    int32_t i, n = 0;
    if (gpu->layer_layout) {
        goto done;
    }

    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        plain[n ++] = box_gpu_layer_texture_entry((uint32_t)i, WGPUTextureViewDimension_2D);
    }

    plain[n ++] = sampler;
    plain[n ++] = kind;
    plain[n ++] = box_gpu_layer_texture_entry(BOX_GPU_MATERIAL_FOAM_BINDING,
        WGPUTextureViewDimension_2D);
    plain[n ++] = box_gpu_layer_texture_entry(BOX_GPU_LAYER_MAPS_BINDING,
        WGPUTextureViewDimension_2DArray);
    for (i = 0; i < BOX_RENDER_LAYERS; i ++) {
        plain[n ++] = (WGPUBindGroupLayoutEntry){
            .binding = BOX_GPU_LAYER_UNIFORMS_BINDING + (uint32_t)i,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_render_texture_layer_t)
            }
        };
    }

    ecs_assert(n == BOX_GPU_LAYER_ENTRIES, ECS_INTERNAL_ERROR, NULL);
    n = 0;
    blend[n ++] = box_gpu_layer_texture_entry(BOX_GPU_BLEND_EMISSIVE,
        WGPUTextureViewDimension_2D);
    sampler.binding = BOX_GPU_BLEND_SAMPLER;
    blend[n ++] = sampler;
    blend[n ++] = box_gpu_layer_texture_entry(BOX_GPU_BLEND_MASK, WGPUTextureViewDimension_2D);
    blend[n ++] = (WGPUBindGroupLayoutEntry){
        .binding = BOX_GPU_BLEND_UNIFORMS,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(box_gpu_blend_uniforms_t)
        }
    };
    for (i = 0; i < BOX_RENDER_BLEND_MAPS; i ++) {
        blend[n ++] = box_gpu_layer_texture_entry(BOX_GPU_BLEND_ARRAYS + (uint32_t)i,
            WGPUTextureViewDimension_2DArray);
    }

    blend[n ++] = box_gpu_layer_texture_entry(BOX_GPU_BLEND_HEIGHTS,
        WGPUTextureViewDimension_2DArray);
    blend[n ++] = kind;
    for (i = 0; i < BOX_RENDER_LAYERS; i ++) {
        blend[n ++] = plain[BOX_RENDER_TEXTURE_SLOTS + 4 + i];
    }

    ecs_assert(n == BOX_GPU_BLEND_LAYER_ENTRIES, ECS_INTERNAL_ERROR, NULL);
    gpu->layer_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = BOX_GPU_LAYER_ENTRIES,
            .entries = plain
        });
    gpu->blend_layer_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = BOX_GPU_BLEND_LAYER_ENTRIES,
            .entries = blend
        });
    WGPUBindGroupLayout groups[4] = {
        gpu->view_layout, gpu->layer_layout, gpu->instance_layout, gpu->volume_layout
    };
    gpu->layer_mesh_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 4,
            .bindGroupLayouts = groups
        });
    groups[1] = gpu->blend_layer_layout;
    gpu->blend_layer_mesh_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 4,
            .bindGroupLayouts = groups
        });
    box_render_texture_layer_t none = {0};
    gpu->layer_none = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(box_render_texture_layer_t)
    });
    wgpuQueueWriteBuffer(gpu->queue, gpu->layer_none, 0, &none, sizeof(none));
done:
    return;
}

WGPUPipelineLayout box_gpu_layer_pipeline_layout(
    box_gpu_t *gpu,
    bool blend)
{
    box_gpu_layer_layouts(gpu);

    return blend ? gpu->blend_layer_mesh_layout : gpu->layer_mesh_layout;
}

bool box_gpu_layered(
    const box_render_batch_t *b)
{
    return b->texture_layers && box_render_is_mesh(b->pipeline) && (!b->transmissive ||
        (b->water && b->pipeline == BOX_RENDER_SHADER_MESH_EXTENDED));
}

/** Get the uniform buffer of a layer entity, created on first use, and write its values when
 * they changed since the last write: every batch with the layer shares it, so one write
 * animates all of them.
 *
 * @param gpu The backend
 * @param entity The layer entity
 * @param values Its values of this packet
 * @return The buffer
 */
static WGPUBuffer box_gpu_layer_buffer(
    box_gpu_t *gpu,
    ecs_entity_t entity,
    const box_render_texture_layer_t *values)
{
    box_gpu_layer_buffer_t *l;
    for (l = gpu->layer_buffers; l; l = l->next) {
        if (l->entity == entity) {
            break;
        }
    }

    if (!l) {
        l = ecs_os_calloc_t(box_gpu_layer_buffer_t);
        l->entity = entity;
        l->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(box_render_texture_layer_t)
        });
        wgpuQueueWriteBuffer(gpu->queue, l->buffer, 0, values, sizeof(*values));
        l->values = *values;
        l->next = gpu->layer_buffers;
        gpu->layer_buffers = l;
    } else if (ecs_os_memcmp(&l->values, values, ECS_SIZEOF(*values))) {
        wgpuQueueWriteBuffer(gpu->queue, l->buffer, 0, values, sizeof(*values));
        l->values = *values;
    }

    l->used = gpu->blend_frame;

    return l->buffer;
}

/** Release a layers group and its reference to its array.
 *
 * @param g The group
 */
static void box_gpu_layer_group_free(
    box_gpu_layer_group_t *g)
{
    if (g->array) {
        g->array->refs --;
    }

    if (g->group) {
        wgpuBindGroupRelease(g->group);
    }

    ecs_os_free(g);
}

/** Make the bind group of a layers group from the images the backend has now.
 *
 * @param gpu The backend
 * @param g The group, with its key, array, buffers and, for a blend batch, its blend group
 * @param bg The blend group of a blend batch, NULL for the plain variant
 */
static void box_gpu_layer_group_bind(
    box_gpu_t *gpu,
    box_gpu_layer_group_t *g,
    const box_gpu_blend_group_t *bg)
{
    WGPUBindGroupEntry entries[BOX_GPU_BLEND_LAYER_ENTRIES];
    int32_t i, n = 0;
    if (g->group) {
        wgpuBindGroupRelease(g->group);
        g->group = NULL;
    }

    if (bg) {
        box_gpu_texture_t *mask = box_gpu_blend_source(gpu, bg->blend.mask);
        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_BLEND_EMISSIVE,
            .textureView = box_gpu_material_view(gpu, bg->emissive, 1)
        };
        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_BLEND_SAMPLER,
            .sampler = box_gpu_texture_sampler(gpu, bg->wrap, bg->anisotropy)
        };
        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_BLEND_MASK,
            .textureView = mask ? mask->linear : gpu->white->linear
        };
        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_BLEND_UNIFORMS,
            .buffer = bg->uniforms,
            .offset = 0,
            .size = sizeof(box_gpu_blend_uniforms_t)
        };
        for (i = 0; i < BOX_RENDER_BLEND_MAPS; i ++) {
            entries[n ++] = (WGPUBindGroupEntry){
                .binding = BOX_GPU_BLEND_ARRAYS + (uint32_t)i,
                .textureView = bg->arrays[i]->view
            };
        }

        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_BLEND_HEIGHTS,
            .textureView = g->array->view
        };
    } else {
        box_gpu_material_mips(gpu, g->key.textures);
        for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
            entries[n ++] = (WGPUBindGroupEntry){
                .binding = (uint32_t)i,
                .textureView = box_gpu_material_view(gpu, g->key.textures[i], i)
            };
        }

        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_RENDER_TEXTURE_SLOTS,
            .sampler = box_gpu_texture_sampler(gpu, g->key.wrap, g->key.anisotropy)
        };
        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_MATERIAL_FOAM_BINDING,
            .textureView = box_gpu_material_view(gpu,
                g->key.textures[BOX_RENDER_TEXTURE_SLOTS], BOX_RENDER_TEXTURE_SLOTS)
        };
        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_LAYER_MAPS_BINDING,
            .textureView = g->array->view
        };
        for (i = 0; i < BOX_GPU_MATERIAL_TEXTURES; i ++) {
            g->serials[i] = box_gpu_blend_serial(gpu, g->key.textures[i]);
        }
    }

    entries[n ++] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_DRAW_KIND_BINDING,
        .buffer = gpu->draw_kinds,
        .size = 4 * sizeof(uint32_t)
    };
    for (i = 0; i < BOX_RENDER_LAYERS; i ++) {
        entries[n ++] = (WGPUBindGroupEntry){
            .binding = BOX_GPU_LAYER_UNIFORMS_BINDING + (uint32_t)i,
            .buffer = g->buffers[i],
            .offset = 0,
            .size = sizeof(box_render_texture_layer_t)
        };
    }

    g->version = g->array->version;
    g->base = bg ? bg->group : NULL;
    g->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = bg ? gpu->blend_layer_layout : gpu->layer_layout,
        .entryCount = (size_t)n,
        .entries = entries
    });
}

/** Get the bind group of a TextureLayers batch, created on first use, and write the buffers of
 * its layers whose values changed. Once per packet it checks its array, the blend group of a
 * blend batch and the images of a plain one, and binds them again when one changed.
 *
 * The array holds the albedo, ORM, normal and mask of each layer, after the heights of the
 * layers of a TextureBlend, whose blend group lends the rest of its entries.
 *
 * @param gpu The backend
 * @param packet The packet of the batch
 * @param b The batch, box_gpu_layered
 * @return The bind group
 */
static WGPUBindGroup box_gpu_layer_group(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b)
{
    const box_render_texture_layers_t *l = box_render_packet_data(packet, b->layers);
    uint32_t sources[BOX_GPU_ARRAY_SLICES] = {0};
    WGPUBuffer buffers[BOX_RENDER_LAYERS];
    box_gpu_blend_group_t *bg = NULL;
    box_gpu_layer_group_t *g;
    box_gpu_layer_key_t key;
    uint32_t base = 0, count, flat = 0, k, m;
    int32_t i;
    bool stale;
    box_gpu_layer_layouts(gpu);
    ecs_os_memset_t(&key, 0, box_gpu_layer_key_t);
    for (k = 0; k < BOX_RENDER_LAYERS; k ++) {
        bool shows = l->values[k].color[3] > 0.0f;
        for (m = 0; shows && m < BOX_RENDER_LAYER_MAPS; m ++) {
            if (m != BOX_RENDER_LAYER_NORMAL || l->values[k].coverage[2] > 0.0f) {
                box_gpu_texture_graph_sampled(gpu, l->maps[k][m]);
            }
        }

        buffers[k] = l->entities[k]
            ? box_gpu_layer_buffer(gpu, l->entities[k], &l->values[k]) : gpu->layer_none;
    }

    key.wrap = b->wrap;
    key.anisotropy = packet->anisotropy;
    if (box_gpu_texture_blends(b)) {
        bg = box_gpu_blend_group_get(gpu, packet, b);
        key.blend = 1;
        key.record = bg->blend;
        key.emissive = bg->emissive;
        base = bg->arrays[0]->count;
        for (k = 0; k < base; k ++) {
            sources[k] = bg->blend.contrast > 0.0f ? bg->blend.layers[k][BOX_RENDER_BLEND_HEIGHT]
                : 0;
        }
    } else {
        for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
            bool self = gpu->rendering &&
                b->textures[i] == (BOX_RENDER_TEXTURE_TARGET | gpu->rendering);
            key.textures[i] = self ? 0 : b->textures[i];
        }

        key.textures[BOX_RENDER_TEXTURE_SLOTS] = b->water && !(gpu->rendering &&
            b->foam == (BOX_RENDER_TEXTURE_TARGET | gpu->rendering)) ? b->foam : 0;
    }

    ecs_os_memcpy_n(key.entities, l->entities, ecs_entity_t, BOX_RENDER_LAYERS);
    ecs_os_memcpy(key.maps, l->maps, ECS_SIZEOF(key.maps));
    key.count = l->count < 1 ? 1 : l->count > BOX_RENDER_LAYERS ? BOX_RENDER_LAYERS : l->count;
    key.quality = l->quality;
    for (g = gpu->layer_groups; g; g = g->next) {
        if (!ecs_os_memcmp(&g->key, &key, ECS_SIZEOF(key))) {
            break;
        }
    }

    if (!g) {
        g = ecs_os_calloc_t(box_gpu_layer_group_t);
        g->key = key;
        g->checked = -1;
        g->next = gpu->layer_groups;
        gpu->layer_groups = g;
    }

    g->used = gpu->blend_frame;
    if (g->checked == gpu->blend_frame && g->group) {
        goto done;
    }

    g->checked = gpu->blend_frame;
    for (k = 0; k < key.count; k ++) {
        for (m = 0; m < BOX_RENDER_LAYER_MAPS; m ++) {
            uint32_t slice = base + k * BOX_RENDER_LAYER_MAPS + m;
            sources[slice] = l->maps[k][m];
            flat |= m == BOX_RENDER_LAYER_NORMAL ? 1u << slice : 0u;
        }
    }

    count = base + key.count * BOX_RENDER_LAYER_MAPS;
    box_gpu_blend_array_t *a = box_gpu_array_get(gpu, BOX_GPU_LAYER_MAP, sources, count,
        key.quality, flat);
    stale = !g->group || a != g->array || a->version != g->version ||
        (bg && bg->group != g->base) ||
        ecs_os_memcmp(buffers, g->buffers, ECS_SIZEOF(buffers));
    for (i = 0; !bg && !stale && i < BOX_GPU_MATERIAL_TEXTURES; i ++) {
        stale = box_gpu_blend_serial(gpu, key.textures[i]) != g->serials[i];
    }

    if (a != g->array) {
        if (g->array) {
            g->array->refs --;
        }

        a->refs ++;
        g->array = a;
    }

    if (stale) {
        ecs_os_memcpy(g->buffers, buffers, ECS_SIZEOF(buffers));
        box_gpu_layer_group_bind(gpu, g, bg);
    }

done:
    return g->group;
}

WGPUBindGroup box_gpu_forward_group(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b)
{
    WGPUBindGroup result = NULL;
    int32_t i;
    if (!box_gpu_layered(b)) {
        result = box_gpu_material_group(gpu, packet, b);
        goto done;
    }

    for (i = 0; i < BOX_RENDER_TEXTURE_SLOTS; i ++) {
        box_gpu_texture_graph_sampled(gpu, b->textures[i]);
    }

    if (b->water) {
        box_gpu_texture_graph_sampled(gpu, b->foam);
    }

    result = box_gpu_layer_group(gpu, packet, b);
done:
    return result;
}

/** Make the bind groups, arrays and buffers of the TextureLayers batches of a packet, and
 * release the groups and buffers no batch of the packet draws with. A packet without such a
 * batch only walks its batches.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_layers_prepare(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->batches);
    box_gpu_layer_group_t **gp;
    box_gpu_layer_buffer_t **lp;
    for (i = 0; i < count; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        if (box_gpu_layered(b)) {
            box_gpu_layer_group(gpu, packet, b);
        }
    }

    for (gp = &gpu->layer_groups; *gp; ) {
        box_gpu_layer_group_t *g = *gp;
        if (g->used == gpu->blend_frame) {
            gp = &g->next;
            continue;
        }

        *gp = g->next;
        box_gpu_layer_group_free(g);
    }

    for (lp = &gpu->layer_buffers; *lp; ) {
        box_gpu_layer_buffer_t *l = *lp;
        if (l->used == gpu->blend_frame) {
            lp = &l->next;
            continue;
        }

        *lp = l->next;
        wgpuBufferRelease(l->buffer);
        ecs_os_free(l);
    }
}

/** Release the layers groups, the layer buffers and the layouts of the layers variants.
 *
 * @param gpu The backend
 */
void box_gpu_layers_clear(
    box_gpu_t *gpu)
{
    while (gpu->layer_groups) {
        box_gpu_layer_group_t *g = gpu->layer_groups;
        gpu->layer_groups = g->next;
        box_gpu_layer_group_free(g);
    }

    while (gpu->layer_buffers) {
        box_gpu_layer_buffer_t *l = gpu->layer_buffers;
        gpu->layer_buffers = l->next;
        wgpuBufferRelease(l->buffer);
        ecs_os_free(l);
    }
}

void box_gpu_layer_layouts_fini(
    box_gpu_t *gpu)
{
    box_gpu_layers_clear(gpu);
    if (gpu->layer_mesh_layout) {
        wgpuPipelineLayoutRelease(gpu->layer_mesh_layout);
        gpu->layer_mesh_layout = NULL;
    }

    if (gpu->blend_layer_mesh_layout) {
        wgpuPipelineLayoutRelease(gpu->blend_layer_mesh_layout);
        gpu->blend_layer_mesh_layout = NULL;
    }

    if (gpu->layer_layout) {
        wgpuBindGroupLayoutRelease(gpu->layer_layout);
        gpu->layer_layout = NULL;
    }

    if (gpu->blend_layer_layout) {
        wgpuBindGroupLayoutRelease(gpu->blend_layer_layout);
        gpu->blend_layer_layout = NULL;
    }

    if (gpu->layer_none) {
        wgpuBufferRelease(gpu->layer_none);
        gpu->layer_none = NULL;
    }
}

#endif
