/**
 * @file render/gpu_shadow.c
 * @brief Cascaded shadow maps and spot and point light shadow atlases of the webgpu backend.
 *
 * The casters the cull pass kept render with etc/shaders/shadow.wgsl, depth only and reversed Z,
 * into each cascade before its view draws and into each atlas tile once per packet; alpha tested
 * ones discard below their cutoff. Probe captures use a map of their own with one cascade.
 *
 * A point light tile records the casters of each octant, which the cull keeps per octant, the
 * octant in the dynamic offset; the shadow_clip variant clips to the octant with clip distances,
 * the core one discards.
 */

#include "gpu.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

void box_gpu_shadow_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = true,
            .minBindingSize = BOX_GPU_SHADOW_UNIFORM_SIZE
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 1, .entries = &entry };
    WGPUSamplerDescriptor sampler = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 1.0f,
        .compare = WGPUCompareFunction_GreaterEqual,
        .maxAnisotropy = 1
    };
    WGPUTextureDescriptor empty = {
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = BOX_GPU_SHADOW_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTextureViewDescriptor view = {
        .format = BOX_GPU_SHADOW_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_DepthOnly
    };
    gpu->shadow_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    WGPUBindGroupLayout groups[3] = {
        gpu->shadow_layout, gpu->instance_layout, gpu->material_layout
    };
    gpu->shadow_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 3,
            .bindGroupLayouts = groups
        });
    gpu->shadow_clear_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 0 });
    gpu->shadow_sampler = wgpuDeviceCreateSampler(gpu->device, &sampler);
    gpu->shadow_empty = wgpuDeviceCreateTexture(gpu->device, &empty);
    gpu->shadow_empty_view = wgpuTextureCreateView(gpu->shadow_empty, &view);
    gpu->no_atlas = wgpuDeviceCreateTexture(gpu->device, &empty);
    gpu->no_atlas_view = wgpuTextureCreateView(gpu->no_atlas, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_SHADOW_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_DepthOnly
    });
    gpu->no_tiles = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform,
        .size = BOX_GPU_ATLAS_TILES_SIZE
    });
    ecs_vec_init_t(NULL, &gpu->shadow_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &gpu->cascade_stores, box_gpu_cascade_store_t, 0);
    ecs_vec_init_t(NULL, &gpu->cascade_slots, box_gpu_cascade_slot_t, 0);
    ecs_vec_init_t(NULL, &gpu->atlas_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &gpu->atlas_blocks, int32_t, 0);
    ecs_vec_init_t(NULL, &gpu->atlas_renders, bool, 0);
    ecs_vec_init_t(NULL, &gpu->atlas_reads, box_gpu_atlas_read_t, 0);
    ecs_map_init(&gpu->atlases, NULL);
}

/** Release a shadow atlas and its view.
 *
 * @param a The atlas
 */
static void box_gpu_atlas_free(
    box_gpu_atlas_t *a)
{
    if (a->view) {
        wgpuTextureViewRelease(a->view);
    }

    if (a->texture) {
        wgpuTextureRelease(a->texture);
    }

    ecs_os_free(a);
}

/** Release the shadow map and the views of its cascades.
 *
 * @param gpu The backend
 */
static void box_gpu_shadow_map_free(
    box_gpu_t *gpu)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_SHADOW_CASCADES; i ++) {
        if (gpu->shadow_layers[i]) {
            wgpuTextureViewRelease(gpu->shadow_layers[i]);
            gpu->shadow_layers[i] = NULL;
        }
    }

    if (gpu->shadow_map_view) {
        wgpuTextureViewRelease(gpu->shadow_map_view);
        gpu->shadow_map_view = NULL;
    }

    if (gpu->shadow_map) {
        wgpuTextureRelease(gpu->shadow_map);
        gpu->shadow_map = NULL;
    }

    gpu->shadow_resolution = 0;
    gpu->shadow_cascades = 0;
}

/** Release the shadow map of a cascade store and the views of its cascades.
 *
 * @param store The store
 */
static void box_gpu_cascade_store_free(
    box_gpu_cascade_store_t *store)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_SHADOW_CASCADES; i ++) {
        if (store->layers[i]) {
            wgpuTextureViewRelease(store->layers[i]);
            store->layers[i] = NULL;
        }
    }

    if (store->array) {
        wgpuTextureViewRelease(store->array);
        store->array = NULL;
    }

    if (store->texture) {
        wgpuTextureRelease(store->texture);
        store->texture = NULL;
    }

    store->resolution = 0;
    store->cascades = 0;
    store->valid = 0;
}

/** Release the shadow map of the probe captures and its uniform buffer.
 *
 * @param gpu The backend
 */
static void box_gpu_shadow_capture_free(
    box_gpu_t *gpu)
{
    if (gpu->capture_shadow_group) {
        wgpuBindGroupRelease(gpu->capture_shadow_group);
        gpu->capture_shadow_group = NULL;
    }

    if (gpu->capture_shadow_buffer) {
        wgpuBufferRelease(gpu->capture_shadow_buffer);
        gpu->capture_shadow_buffer = NULL;
    }

    if (gpu->capture_shadow_layer) {
        wgpuTextureViewRelease(gpu->capture_shadow_layer);
        gpu->capture_shadow_layer = NULL;
    }

    if (gpu->capture_shadow_view) {
        wgpuTextureViewRelease(gpu->capture_shadow_view);
        gpu->capture_shadow_view = NULL;
    }

    if (gpu->capture_shadow) {
        wgpuTextureRelease(gpu->capture_shadow);
        gpu->capture_shadow = NULL;
    }
}

/** Release the shadow pipelines made on first use of the cascades or the point light octants.
 *
 * @param gpu The backend
 * @param point Whether to release those of the point light octants
 */
static void box_gpu_shadow_variants_release(
    box_gpu_t *gpu,
    bool point)
{
    int32_t i;
    for (i = 0; i < BOX_GPU_SHADOW_VARIANTS; i ++) {
        if (gpu->shadow_variants[point][i]) {
            wgpuRenderPipelineRelease(gpu->shadow_variants[point][i]);
            gpu->shadow_variants[point][i] = NULL;
        }
    }
}

void box_gpu_shadow_fini(
    box_gpu_t *gpu)
{
    int32_t i;
    ecs_map_iter_t it = ecs_map_iter(&gpu->atlases);
    while (ecs_map_next(&it)) {
        box_gpu_atlas_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&gpu->atlases);
    box_gpu_shadow_map_free(gpu);
    for (i = 0; i < ecs_vec_count(&gpu->cascade_stores); i ++) {
        box_gpu_cascade_store_free(ecs_vec_get_t(
            &gpu->cascade_stores, box_gpu_cascade_store_t, i));
    }

    ecs_vec_fini_t(NULL, &gpu->cascade_stores, box_gpu_cascade_store_t);
    ecs_vec_fini_t(NULL, &gpu->cascade_slots, box_gpu_cascade_slot_t);
    box_gpu_shadow_capture_free(gpu);
    ecs_vec_fini_t(NULL, &gpu->shadow_data, uint8_t);
    ecs_vec_fini_t(NULL, &gpu->atlas_data, uint8_t);
    ecs_vec_fini_t(NULL, &gpu->atlas_blocks, int32_t);
    ecs_vec_fini_t(NULL, &gpu->atlas_renders, bool);
    ecs_vec_fini_t(NULL, &gpu->atlas_reads, box_gpu_atlas_read_t);
    if (gpu->atlas_counts) {
        wgpuBufferRelease(gpu->atlas_counts);
        gpu->atlas_counts = NULL;
        gpu->atlas_counts_size = 0;
    }

    if (gpu->atlas_buffer) {
        wgpuBufferRelease(gpu->atlas_buffer);
        gpu->atlas_buffer = NULL;
        gpu->atlas_capacity = 0;
    }

    if (gpu->no_tiles) {
        wgpuBufferRelease(gpu->no_tiles);
        gpu->no_tiles = NULL;
    }

    if (gpu->no_atlas_view) {
        wgpuTextureViewRelease(gpu->no_atlas_view);
        gpu->no_atlas_view = NULL;
    }

    if (gpu->no_atlas) {
        wgpuTextureRelease(gpu->no_atlas);
        gpu->no_atlas = NULL;
    }

    if (gpu->shadow) {
        wgpuRenderPipelineRelease(gpu->shadow);
        gpu->shadow = NULL;
    }

    if (gpu->shadow_cutout) {
        wgpuRenderPipelineRelease(gpu->shadow_cutout);
        gpu->shadow_cutout = NULL;
    }

    if (gpu->shadow_clear) {
        wgpuRenderPipelineRelease(gpu->shadow_clear);
        gpu->shadow_clear = NULL;
    }

    if (gpu->shadow_point) {
        wgpuRenderPipelineRelease(gpu->shadow_point);
        gpu->shadow_point = NULL;
    }

    if (gpu->shadow_point_cutout) {
        wgpuRenderPipelineRelease(gpu->shadow_point_cutout);
        gpu->shadow_point_cutout = NULL;
    }

    box_gpu_shadow_variants_release(gpu, false);
    box_gpu_shadow_variants_release(gpu, true);

    gpu->shadow_point_shader = 0;

    if (gpu->shadow_buffer) {
        wgpuBufferRelease(gpu->shadow_buffer);
        gpu->shadow_buffer = NULL;
    }

    if (gpu->shadow_empty_view) {
        wgpuTextureViewRelease(gpu->shadow_empty_view);
        gpu->shadow_empty_view = NULL;
    }

    if (gpu->shadow_empty) {
        wgpuTextureRelease(gpu->shadow_empty);
        gpu->shadow_empty = NULL;
    }

    if (gpu->shadow_sampler) {
        wgpuSamplerRelease(gpu->shadow_sampler);
        gpu->shadow_sampler = NULL;
    }

    if (gpu->shadow_pipeline_layout) {
        wgpuPipelineLayoutRelease(gpu->shadow_pipeline_layout);
        gpu->shadow_pipeline_layout = NULL;
    }

    if (gpu->shadow_blend_layout) {
        wgpuPipelineLayoutRelease(gpu->shadow_blend_layout);
        gpu->shadow_blend_layout = NULL;
    }

    if (gpu->shadow_clear_layout) {
        wgpuPipelineLayoutRelease(gpu->shadow_clear_layout);
        gpu->shadow_clear_layout = NULL;
    }

    if (gpu->shadow_layout) {
        wgpuBindGroupLayoutRelease(gpu->shadow_layout);
        gpu->shadow_layout = NULL;
    }
}

float box_gpu_shadow_margin(
    int32_t filter_size,
    float normal_offset)
{
    return 0.5f * (float)filter_size + 1.0f + glm_max(normal_offset, 0.0f);
}

void box_gpu_shadow_cascade_uniforms(
    box_gpu_view_uniforms_t *u,
    int32_t index,
    const box_render_cascade_t *cascade,
    float depth_bias,
    float normal_offset,
    int32_t resolution)
{
    float extent = 0.5f * cascade->texel * (float)resolution;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, cascade->matrix), u->shadow_matrix[index]);
    u->shadow_depth[index] = cascade->depth_range / glm_max(extent, 1e-6f);
    u->shadow_bias[index] = depth_bias * cascade->texel / cascade->depth_range;
    u->shadow_offset[index] = normal_offset * cascade->texel;
}

void box_gpu_shadow_filter_uniforms(
    box_gpu_view_uniforms_t *u,
    int32_t cascades,
    int32_t filter_size,
    float normal_offset,
    float blend,
    int32_t resolution)
{
    float texels = box_gpu_shadow_margin(filter_size, normal_offset);
    u->shadow_params[2] = 1.0f / (float)resolution;
    u->shadow_edge[0] = glm_min(2.0f * texels / (float)resolution, 0.5f);
    u->shadow_edge[1] = blend;
    u->shadow_config[0] = (uint32_t)cascades;
    u->shadow_config[1] = (uint32_t)filter_size;
}

/** Create a shadow map with a layer per cascade, its array view and the view of each layer.
 *
 * @param gpu The backend
 * @param resolution Edge of a layer in texels
 * @param cascades Layers
 * @param texture Receives the texture
 * @param array Receives the array view
 * @param layers Receives the view of each layer
 */
static void box_gpu_shadow_layers_create(
    box_gpu_t *gpu,
    int32_t resolution,
    int32_t cascades,
    WGPUTexture *texture,
    WGPUTextureView *array,
    WGPUTextureView *layers)
{
    int32_t i;
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { (uint32_t)resolution, (uint32_t)resolution, (uint32_t)cascades },
        .format = BOX_GPU_SHADOW_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    *texture = wgpuDeviceCreateTexture(gpu->device, &desc);
    *array = wgpuTextureCreateView(*texture, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_SHADOW_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .mipLevelCount = 1,
        .arrayLayerCount = (uint32_t)cascades,
        .aspect = WGPUTextureAspect_DepthOnly
    });
    for (i = 0; i < cascades; i ++) {
        layers[i] = wgpuTextureCreateView(*texture,
            &(WGPUTextureViewDescriptor){
                .format = BOX_GPU_SHADOW_FORMAT,
                .dimension = WGPUTextureViewDimension_2D,
                .mipLevelCount = 1,
                .baseArrayLayer = (uint32_t)i,
                .arrayLayerCount = 1,
                .aspect = WGPUTextureAspect_DepthOnly
            });
    }
}

/** Get the cascade store of a view, created on first use, with a shadow map at the resolution
 * and cascades of the view: a store recreates its map when they change, which drops its layers.
 *
 * @param gpu The backend
 * @param v The view
 * @param frame Frame of the packet
 * @return Index of the store in gpu->cascade_stores
 */
static int32_t box_gpu_cascade_store(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    int64_t frame)
{
    const box_render_view_shadows_t *s = &v->shadows;
    box_gpu_cascade_store_t *store = NULL;
    int32_t i, count = ecs_vec_count(&gpu->cascade_stores), result = count;
    for (i = 0; i < count; i ++) {
        store = ecs_vec_get_t(&gpu->cascade_stores, box_gpu_cascade_store_t, i);
        if (store->view == v->view) {
            result = i;
            break;
        }
    }

    if (result == count) {
        store = ecs_vec_append_t(NULL, &gpu->cascade_stores, box_gpu_cascade_store_t);
        ecs_os_memset_t(store, 0, box_gpu_cascade_store_t);
        store->view = v->view;
    }

    if (!store->texture || store->resolution != s->resolution ||
        store->cascades != s->cascades)
    {
        box_gpu_cascade_store_free(store);
        box_gpu_shadow_layers_create(gpu, s->resolution, s->cascades, &store->texture,
            &store->array, store->layers);
        store->resolution = s->resolution;
        store->cascades = s->cascades;
    }

    store->frame = frame;

    return result;
}

void box_gpu_shadow_begin(
    box_gpu_t *gpu,
    int64_t frame)
{
    int32_t i;
    ecs_vec_clear(&gpu->cascade_slots);
    for (i = ecs_vec_count(&gpu->cascade_stores) - 1; i >= 0; i --) {
        box_gpu_cascade_store_t *store = ecs_vec_get_t(
            &gpu->cascade_stores, box_gpu_cascade_store_t, i);
        if (frame - store->frame > BOX_GPU_CASCADE_STORE_FRAMES ||
            frame < store->frame)
        {
            box_gpu_cascade_store_free(store);
            ecs_vec_remove_t(&gpu->cascade_stores, box_gpu_cascade_store_t, i);
        }
    }
}

/** Get the cascades of a view slot of the target being drawn.
 *
 * @param gpu The backend
 * @param slot Slot of the view in the view buffer
 * @return The cascades, NULL for a slot box_gpu_shadow_uniforms did not write
 */
static const box_gpu_cascade_slot_t* box_gpu_cascade_slot(
    box_gpu_t *gpu,
    int32_t slot)
{
    return slot >= 0 && slot < ecs_vec_count(&gpu->cascade_slots)
        ? ecs_vec_get_t(&gpu->cascade_slots, box_gpu_cascade_slot_t, slot)
        : NULL;
}

uint32_t box_gpu_shadow_renders(
    box_gpu_t *gpu,
    int32_t slot)
{
    const box_gpu_cascade_slot_t *cs = box_gpu_cascade_slot(gpu, slot);

    return cs ? cs->renders : ~0u;
}

void box_gpu_shadow_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    mat4 proj,
    int32_t slot,
    int64_t frame,
    box_gpu_view_uniforms_t *u)
{
    const box_render_view_shadows_t *s = &v->shadows;
    box_gpu_cascade_store_t *store = NULL;
    box_render_cascade_t cascade;
    int32_t i, n = s->cascades;
    ecs_os_memset_t(&cascade, 0, box_render_cascade_t);
    if (ecs_vec_count(&gpu->cascade_slots) <= slot) {
        ecs_vec_set_count_t(NULL, &gpu->cascade_slots, box_gpu_cascade_slot_t, slot + 1);
    }

    box_gpu_cascade_slot_t *cs = ecs_vec_get_t(
        &gpu->cascade_slots, box_gpu_cascade_slot_t, slot);
    cs->store = -1;
    cs->renders = 0;
    if (!s->enabled || n < 1 || n > BOX_RENDER_SHADOW_CASCADES) {
        goto done;
    }

    int32_t size = (slot + 1) * BOX_RENDER_SHADOW_CASCADES * BOX_GPU_CASCADE_STRIDE;
    if (ecs_vec_count(&gpu->shadow_data) < size) {
        ecs_vec_set_count_t(NULL, &gpu->shadow_data, uint8_t, size);
    }

    cs->renders = (1u << n) - 1u;
    if (s->reuse && v->view) {
        cs->store = box_gpu_cascade_store(gpu, v, frame);
        store = ecs_vec_get_t(&gpu->cascade_stores, box_gpu_cascade_store_t, cs->store);
        if (store->revision != gpu->shadow_revision ||
            ecs_os_memcmp(store->proj, proj, sizeof(mat4)))
        {
            store->valid = 0;
        }

        glm_mat4_copy(proj, store->proj);
        cs->renders &= s->render | ~store->valid;
    }

    float distance = s->splits[n - 1];
    float last = n > 1 ? s->splits[n - 2] : s->near_;
    for (i = 0; i < n; i ++) {
        if (cs->renders & (1u << i)) {
            box_render_shadow_cascade(ECS_CONST_CAST(vec4*, v->camera_transform), proj,
                i ? s->splits[i - 1] : s->near_, s->splits[i], s->direction, s->resolution,
                distance, &cascade);
        }

        if (store && (cs->renders & (1u << i))) {
            store->fits[i] = cascade;
            store->valid &= ~(1u << i);
        } else if (store) {
            cascade = store->fits[i];
        }

        uint8_t *data = ecs_vec_get_t(&gpu->shadow_data, uint8_t,
            (slot * BOX_RENDER_SHADOW_CASCADES + i) * BOX_GPU_CASCADE_STRIDE);
        ecs_os_memcpy(data, cascade.matrix, sizeof(mat4));
        box_gpu_shadow_cascade_uniforms(u, i, &cascade, s->depth_bias, s->normal_offset,
            s->resolution);
    }

    u->shadow_params[0] = distance - s->fade * (distance - last);
    u->shadow_params[1] = distance;
    box_gpu_shadow_filter_uniforms(u, n, s->filter_size, s->normal_offset, s->blend,
        s->resolution);
done:
    return;
}

void box_gpu_shadow_upload(
    box_gpu_t *gpu,
    int32_t count)
{
    int32_t capacity = gpu->shadow_capacity ? gpu->shadow_capacity : 4;
    int32_t size = ecs_vec_count(&gpu->shadow_data);
    int32_t used = count * BOX_RENDER_SHADOW_CASCADES * BOX_GPU_CASCADE_STRIDE;
    if (!size) {
        goto done;
    }

    if (!gpu->shadow_buffer || count > gpu->shadow_capacity) {
        while (capacity < count) {
            capacity *= 2;
        }

        if (gpu->shadow_buffer) {
            wgpuBufferRelease(gpu->shadow_buffer);
        }

        gpu->shadow_capacity = capacity;
        gpu->shadow_buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)capacity * BOX_RENDER_SHADOW_CASCADES * BOX_GPU_CASCADE_STRIDE
        });
    }

    wgpuQueueWriteBuffer(gpu->queue, gpu->shadow_buffer, 0, ecs_vec_first(&gpu->shadow_data),
        (size_t)(used < size ? used : size));
done:
    return;
}

/** Create a shadow pipeline from the shadow shader.
 *
 * The plain vertex stages pass on the position only and the plain fragment stage writes nothing;
 * the alpha tested ones pass on what the alpha test reads and discard below the cutoff. The
 * surface variants of the vertex stages place the vertices of parametric surfaces.
 *
 * The point light pipelines project into an octant of the tile; without clip distances their
 * fragment stages discard outside the octant. An alpha tested pipeline of
 * a TextureBlend binds the blend group in group 2 (shadow_blend_layout, made on first use).
 *
 * @param gpu The backend
 * @param module The shadow shader module
 * @param cutout Whether to create the alpha tested pipeline
 * @param point Whether to create a pipeline of the octants of the point light tiles
 * @param discard Whether that pipeline discards outside the octant instead of clipping
 * @param texture_blend Whether the alpha tested pipeline blends the layers of a TextureBlend;
 * ignored without cutout
 * @param surface Whether the casters are parametric surfaces
 * @param wind Whether the vertex stage moves the vertices by the WindSway of their material
 * (the material_wind constant), as the mesh shaders do
 * @param range Whether the casters have a DrawRange: both stages have the draw_range constant
 * and a plain caster discards with fs_range; ignored by the point light pipelines
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_shadow_create(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    bool cutout,
    bool point,
    bool discard,
    bool texture_blend,
    bool surface,
    bool wind,
    bool range)
{
    WGPUConstantEntry vertex_constants[2];
    WGPUConstantEntry range_constant = {
        .key = { .data = "draw_range", .length = WGPU_STRLEN },
        .value = 1.0
    };
    size_t vertex_count = 0;
    range = range && !point;
    if (wind) {
        vertex_constants[vertex_count ++] = (WGPUConstantEntry){
            .key = { .data = "material_wind", .length = WGPU_STRLEN },
            .value = 1.0
        };
    }

    if (range) {
        vertex_constants[vertex_count ++] = range_constant;
    }

    static const char *vertex[2][2][2] = {
        { { "vs", "vs_surface" }, { "vs_cutout", "vs_cutout_surface" } },
        { { "vs_point", "vs_point_surface" }, { "vs_point_cutout", "vs_point_cutout_surface" } }
    };
    const char *vs = vertex[point][cutout][surface];
    const char *fs = point
        ? (cutout ? "fs_point_cutout" : (discard ? "fs_point" : "fs"))
        : (cutout ? "fs_cutout" : (range ? "fs_range" : "fs"));
    WGPUPipelineLayout layout = gpu->shadow_pipeline_layout;
    texture_blend = texture_blend && cutout;
    if (texture_blend) {
        WGPUBindGroupLayout groups[3] = {
            gpu->shadow_layout, gpu->instance_layout, gpu->blend_layout
        };
        if (!gpu->shadow_blend_layout) {
            gpu->shadow_blend_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
                &(WGPUPipelineLayoutDescriptor){
                    .bindGroupLayoutCount = 3,
                    .bindGroupLayouts = groups
                });
        }

        fs = point ? "fs_point_cutout_blend" : "fs_cutout_blend";
        layout = gpu->shadow_blend_layout;
    }

    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    WGPUVertexBufferLayout buffers[2] = {
        {
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = sizeof(box_vertex_t),
            .attributeCount = 3,
            .attributes = vertex_attrs
        },
        {
            .stepMode = WGPUVertexStepMode_Instance,
            .arrayStride = sizeof(uint32_t),
            .attributeCount = 1,
            .attributes = &slot_attr
        }
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_SHADOW_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = fs, .length = WGPU_STRLEN },
        .constantCount = range ? 1 : 0,
        .constants = range ? &range_constant : NULL
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = vs, .length = WGPU_STRLEN },
            .constantCount = vertex_count,
            .constants = vertex_count ? vertex_constants : NULL,
            .bufferCount = 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };

    return box_gpu_render_pipeline(gpu, &desc);
}

/** Create the pipeline that clears a tile of an atlas the pass loads: vs_clear of the shadow
 * shader writes depth 0 over the viewport with depth compare always.
 *
 * @param gpu The backend
 * @param module The shadow shader module
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_shadow_clear_create(
    box_gpu_t *gpu,
    WGPUShaderModule module)
{
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_SHADOW_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Always,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = gpu->shadow_clear_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs_clear", .length = WGPU_STRLEN }
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };

    return box_gpu_render_pipeline(gpu, &desc);
}

/** Get the shadow pipelines, created again when the shadow shader was recompiled; the previous
 * ones stay until all three new ones exist.
 *
 * @param gpu The backend
 * @return The plain pipeline, NULL while the shader has no module or the first ones are being
 * created; the alpha tested one is gpu->shadow_cutout, the one that clears a tile shadow_clear
 */
static WGPURenderPipeline box_gpu_shadow_pipeline(
    box_gpu_t *gpu)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_SHADOW);
    if (!s || !s->module) {
        goto done;
    }

    if (gpu->shadow && gpu->shadow_revision == s->revision) {
        goto done;
    }

    WGPURenderPipeline *current[3] = { &gpu->shadow, &gpu->shadow_cutout, &gpu->shadow_clear };
    WGPURenderPipeline next[3] = {
        box_gpu_shadow_create(gpu, s->module, false, false, false, false, false, false,
            false),
        box_gpu_shadow_create(gpu, s->module, true, false, false, false, false, false,
            false),
        box_gpu_shadow_clear_create(gpu, s->module)
    };
    if (!box_gpu_render_pipelines_swap(current, next, 3)) {
        goto done;
    }

    box_gpu_shadow_variants_release(gpu, false);
    gpu->shadow_revision = s->revision;
done:
    return s && s->module ? gpu->shadow : NULL;
}

/** Get the pipelines of the octants of the point light tiles, created again when the variant
 * they need changed or its shader was recompiled.
 *
 * They come from the clip distances variant when the device has the feature, it is not forced
 * off and that variant compiled, else from the core variant, which discards.
 *
 * @param gpu The backend
 * @return The plain pipeline, NULL while no variant has a module or the first ones are being
 * created; the alpha tested one is gpu->shadow_point_cutout
 */
static WGPURenderPipeline box_gpu_shadow_point_pipeline(
    box_gpu_t *gpu)
{
    uint32_t handle = BOX_RENDER_SHADER_SHADOW;
    box_gpu_shader_t *s = NULL;
    if (gpu->clip_distances && !gpu->shadow_core) {
        s = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_SHADOW_CLIP);
        handle = BOX_RENDER_SHADER_SHADOW_CLIP;
    }

    if (!s || !s->module) {
        s = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_SHADOW);
        handle = BOX_RENDER_SHADER_SHADOW;
    }

    if (!s || !s->module) {
        goto done;
    }

    if (gpu->shadow_point && gpu->shadow_point_shader == handle &&
        gpu->shadow_point_revision == s->revision)
    {
        goto done;
    }

    bool discard = handle == BOX_RENDER_SHADER_SHADOW;
    WGPURenderPipeline *current[2] = { &gpu->shadow_point, &gpu->shadow_point_cutout };
    WGPURenderPipeline next[2] = {
        box_gpu_shadow_create(gpu, s->module, false, true, discard, false, false, false,
            false),
        box_gpu_shadow_create(gpu, s->module, true, true, discard, false, false, false,
            false)
    };
    if (!box_gpu_render_pipelines_swap(current, next, 2)) {
        goto done;
    }

    box_gpu_shadow_variants_release(gpu, true);
    gpu->shadow_point_shader = handle;
    gpu->shadow_point_revision = s->revision;
done:
    return s && s->module ? gpu->shadow_point : NULL;
}

/** Make the shadow map hold the cascades of a view at its resolution.
 *
 * @param gpu The backend
 * @param s The shadows of the view
 */
static void box_gpu_shadow_map_ensure(
    box_gpu_t *gpu,
    const box_render_view_shadows_t *s)
{
    if (gpu->shadow_map && gpu->shadow_resolution == s->resolution &&
        gpu->shadow_cascades == s->cascades)
    {
        goto done;
    }

    box_gpu_shadow_map_free(gpu);
    box_gpu_shadow_layers_create(gpu, s->resolution, s->cascades, &gpu->shadow_map,
        &gpu->shadow_map_view, gpu->shadow_layers);
    gpu->shadow_resolution = s->resolution;
    gpu->shadow_cascades = s->cascades;
done:
    return;
}

/** Get a shadow pipeline of the alpha tested casters with a TextureBlend, of the parametric
 * surfaces, of the casters with a WindSway or with a DrawRange, created on first use from the
 * shader the other pipelines of the cascades, or of the point light tiles, were made with.
 *
 * @param gpu The backend
 * @param point Whether to get the pipeline of the octants of the point light tiles
 * @param cutout Whether the casters are alpha tested
 * @param texture_blend Whether the alpha tested casters blend the layers of a TextureBlend
 * @param surface Whether the casters are parametric surfaces
 * @param wind Whether the casters sway (the material_wind constant)
 * @param range Whether the casters have a DrawRange (the draw_range constant), ignored by the
 * point light tiles
 * @return The pipeline, NULL without a shader module or while it is being created
 */
static WGPURenderPipeline box_gpu_shadow_variant(
    box_gpu_t *gpu,
    bool point,
    bool cutout,
    bool texture_blend,
    bool surface,
    bool wind,
    bool range)
{
    texture_blend = texture_blend && cutout;
    range = range && !point;
    int32_t index = surface ? 1 + (int32_t)cutout + (int32_t)texture_blend : 0;
    if (wind) {
        index = 4 + (surface ? 3 : 0) + (int32_t)cutout + (int32_t)texture_blend;
    }

    if (range) {
        index = 10 + (wind ? 6 : 0) + (surface ? 3 : 0) + (int32_t)cutout +
            (int32_t)texture_blend;
    }

    WGPURenderPipeline *slot = &gpu->shadow_variants[point][index];
    uint32_t handle = point && gpu->shadow_point_shader
        ? gpu->shadow_point_shader : BOX_RENDER_SHADER_SHADOW;
    box_gpu_shader_t *s = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_t, handle);
    if (*slot || !s || !s->module) {
        goto done;
    }

    *slot = box_gpu_shadow_create(gpu, s->module, cutout, point,
        handle == BOX_RENDER_SHADER_SHADOW, texture_blend, surface, wind, range);
done:
    return *slot;
}

void box_gpu_shadow_request(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i;
    if (!box_gpu_shadow_pipeline(gpu)) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&packet->batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        bool blend = b->cutout && box_gpu_texture_blends(b);
        bool range = box_render_batch_ranged(b);
        if (b->shadow && b->view == -1 && b->blend == Opaque &&
            box_render_is_mesh(b->pipeline) && (b->surface || blend || b->wind || range))
        {
            box_gpu_shadow_variant(gpu, false, b->cutout, blend, b->surface, b->wind, range);
        }
    }

done:
    return;
}

/** Record the indirect draws of the shadow casters the cull pass kept in a cascade or a tile.
 *
 * Every caster binds the shading stream of its page. Plain casters draw with the plain pipeline
 * and the material group of a batch without textures; alpha tested ones with the alpha tested
 * pipeline and the material group of their batch, whose albedo texture they sample.
 *
 * Alpha tested casters with a TextureBlend draw with a pipeline of box_gpu_shadow_variant and
 * the blend group of their batch, whose layers they blend. Parametric surfaces and casters with
 * a WindSway draw with the pipelines of box_gpu_shadow_variant, and wait for them.
 *
 * @param gpu The backend
 * @param pass The shadow pass of the cascade or the atlas
 * @param packet The packet
 * @param cv The cull view of the cascade or the tile
 * @param plain Pipeline of the plain casters
 * @param alpha Pipeline of the alpha tested casters, NULL to draw them with the plain one
 * @param point Whether the pass draws the octants of a point light tile
 * @param counted Whether the draws count in the shadow draws of the stats, false for the
 * probe captures
 * @return Whether every caster drew with its own pipeline, false when an alpha tested caster
 * with a TextureBlend drew with the plain one or a parametric surface did not draw while its
 * pipeline is being created
 */
static bool box_gpu_shadow_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_gpu_cull_view_t *cv,
    WGPURenderPipeline plain,
    WGPURenderPipeline alpha,
    bool point,
    bool counted)
{
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup instances = NULL, material = NULL;
    bool result = true;
    int32_t i;
    for (i = 0; i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        bool cutout = b->cutout && alpha;
        bool blend = cutout && box_gpu_texture_blends(b);
        bool range = !point && box_render_batch_ranged(b);
        WGPURenderPipeline pipeline = cutout ? alpha : plain;
        if (blend || b->surface || b->wind || range) {
            pipeline = box_gpu_shadow_variant(gpu, point, cutout, blend, b->surface, b->wind,
                range);
            result = result && pipeline != NULL;
        }

        if (!pipeline && !b->surface && !b->wind && !range) {
            cutout = false;
            pipeline = plain;
        }

        if (!pipeline || d->skip) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup group = box_gpu_material_group(gpu, packet,
            cutout ? b : &(box_render_batch_t){0});
        if (group != material) {
            wgpuRenderPassEncoderSetBindGroup(pass, 2, group, 1, &(uint32_t){0});
            material = group;
        }

        if (box_gpu_cull_draw(gpu, pass, packet, d, 1, false, true, &instances) && counted) {
            box_gpu_count(gpu, BOX_GPU_STAT_SHADOW_DRAW);
        }
    }

    return result;
}

void box_gpu_shadow_render(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot)
{
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    const box_gpu_cascade_slot_t *cs = box_gpu_cascade_slot(gpu, slot);
    box_gpu_cascade_store_t *store = cs && cs->store >= 0
        ? ecs_vec_get_t(&gpu->cascade_stores, box_gpu_cascade_store_t, cs->store)
        : NULL;
    WGPUTextureView *layers = store ? store->layers : gpu->shadow_layers;
    WGPURenderPipeline pipeline = NULL;
    int32_t i;
    if (!v->shadows.enabled || !cs) {
        goto done;
    }

    if (!store && box_gpu_shadow_binding(gpu, v) != gpu->shadow_map_view) {
        goto done;
    }

    pipeline = box_gpu_shadow_pipeline(gpu);
    if (!pipeline || !gpu->shadow_buffer) {
        goto done;
    }

    if (store && store->revision != gpu->shadow_revision) {
        store->valid = 0;
        store->revision = gpu->shadow_revision;
    }

    WGPUBindGroupEntry entry = {
        .binding = 0, .buffer = gpu->shadow_buffer, .size = BOX_GPU_SHADOW_UNIFORM_SIZE
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->shadow_layout,
        .entryCount = 1,
        .entries = &entry
    });
    for (i = 0; i < v->shadows.cascades; i ++) {
        if (!(cs->renders & (1u << i))) {
            box_gpu_count(gpu, BOX_GPU_STAT_CASCADE_KEPT);
            continue;
        }

        const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, i);
        uint32_t offset = (uint32_t)((slot * BOX_RENDER_SHADOW_CASCADES + i) *
            BOX_GPU_CASCADE_STRIDE);
        bool complete = false;
        WGPURenderPassDepthStencilAttachment depth = {
            .view = layers[i],
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Store,
            .depthClearValue = 0.0f
        };
        WGPURenderPassDescriptor desc = { .depthStencilAttachment = &depth };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        if (cv) {
            complete = box_gpu_shadow_draw(gpu, pass, packet, cv, gpu->shadow,
                gpu->shadow_cutout, false, true);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        box_gpu_count(gpu, BOX_GPU_STAT_CASCADE);
        if (store && complete) {
            store->valid |= 1u << i;
        }
    }

    wgpuBindGroupRelease(group);
done:
    return;
}

void box_gpu_shadow_capture_fit(
    const float *min,
    const float *max,
    const float *direction,
    box_render_cascade_t *out)
{
    vec3 center, forward, right, up, pole = {0, 1, 0};
    float res = (float)BOX_GPU_CAPTURE_SHADOW_RESOLUTION;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        center[i] = (min[i] + max[i]) * 0.5f;
        forward[i] = -direction[i];
    }

    float radius = glm_max(glm_vec3_distance(ECS_CONST_CAST(float*, min),
        ECS_CONST_CAST(float*, max)) * 0.5f, 1e-3f);
    float extent = radius * res / (res - 2.0f * (float)BOX_GPU_CAPTURE_SHADOW_MARGIN);
    if (glm_vec3_norm(forward) < 1e-6f) {
        glm_vec3_copy((vec3){0, -1, 0}, forward);
    }

    glm_vec3_normalize(forward);
    if (fabsf(forward[1]) > 0.99f) {
        glm_vec3_copy((vec3){0, 0, 1}, pole);
    }

    glm_vec3_crossn(forward, pole, right);
    glm_vec3_cross(right, forward, up);
    float cx = glm_vec3_dot(right, center);
    float cy = glm_vec3_dot(up, center);
    float cz = glm_vec3_dot(forward, center);
    float d_near = cz - extent;
    float d_far = cz + extent;
    float range = d_far - d_near;
    glm_mat4_zero(out->matrix);
    for (i = 0; i < 3; i ++) {
        out->matrix[i][0] = right[i] / extent;
        out->matrix[i][1] = up[i] / extent;
        out->matrix[i][2] = -forward[i] / range;
    }

    out->matrix[3][0] = -cx / extent;
    out->matrix[3][1] = -cy / extent;
    out->matrix[3][2] = d_far / range;
    out->matrix[3][3] = 1.0f;
    out->texel = 2.0f * extent / res;
    out->depth_range = range;
}

/** Create the shadow map of the probe captures, its uniform buffer and bind group.
 *
 * @param gpu The backend
 */
static void box_gpu_shadow_capture_ensure(
    box_gpu_t *gpu)
{
    uint32_t res = BOX_GPU_CAPTURE_SHADOW_RESOLUTION;
    if (gpu->capture_shadow) {
        goto done;
    }

    gpu->capture_shadow = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { res, res, 1 },
        .format = BOX_GPU_SHADOW_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    gpu->capture_shadow_view = wgpuTextureCreateView(gpu->capture_shadow,
        &(WGPUTextureViewDescriptor){
            .format = BOX_GPU_SHADOW_FORMAT,
            .dimension = WGPUTextureViewDimension_2DArray,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_DepthOnly
        });
    gpu->capture_shadow_layer = wgpuTextureCreateView(gpu->capture_shadow,
        &(WGPUTextureViewDescriptor){
            .format = BOX_GPU_SHADOW_FORMAT,
            .dimension = WGPUTextureViewDimension_2D,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_DepthOnly
        });
    gpu->capture_shadow_buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_GPU_CASCADE_STRIDE
    });
    WGPUBindGroupEntry entry = {
        .binding = 0, .buffer = gpu->capture_shadow_buffer, .size = BOX_GPU_SHADOW_UNIFORM_SIZE
    };
    gpu->capture_shadow_group = wgpuDeviceCreateBindGroup(gpu->device,
        &(WGPUBindGroupDescriptor){
            .layout = gpu->shadow_layout,
            .entryCount = 1,
            .entries = &entry
        });
done:
    return;
}

WGPUTextureView box_gpu_shadow_capture(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    int32_t slot,
    const box_render_cascade_t *cascade)
{
    WGPURenderPipeline pipeline = box_gpu_shadow_pipeline(gpu);
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, 0);
    WGPUTextureView result = gpu->shadow_empty_view;
    uint32_t offset = 0;
    if (!pipeline) {
        goto done;
    }

    box_gpu_shadow_capture_ensure(gpu);
    wgpuQueueWriteBuffer(gpu->queue, gpu->capture_shadow_buffer, 0, cascade->matrix,
        sizeof(mat4));
    WGPURenderPassDepthStencilAttachment depth = {
        .view = gpu->capture_shadow_layer,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor desc = { .depthStencilAttachment = &depth };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, gpu->capture_shadow_group, 1, &offset);
    if (cv) {
        box_gpu_shadow_draw(gpu, pass, packet, cv, gpu->shadow, gpu->shadow_cutout, false,
            false);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    result = gpu->capture_shadow_view;
done:
    return result;
}

WGPUTextureView box_gpu_shadow_binding(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    WGPUTextureView result = gpu->shadow_empty_view;
    int32_t i;
    if (!v->shadows.enabled || v->shadows.cascades < 1 ||
        v->shadows.cascades > BOX_RENDER_SHADOW_CASCADES)
    {
        goto done;
    }

    if (v->shadows.reuse) {
        for (i = 0; i < ecs_vec_count(&gpu->cascade_stores); i ++) {
            const box_gpu_cascade_store_t *store = ecs_vec_get_t(
                &gpu->cascade_stores, box_gpu_cascade_store_t, i);
            if (store->view == v->view && store->array) {
                result = store->array;
                goto done;
            }
        }
    }

    box_gpu_shadow_map_ensure(gpu, &v->shadows);
    result = gpu->shadow_map_view;
done:
    return result;
}

void box_gpu_atlas_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_shadow_atlas_desc_t *desc)
{
    box_gpu_atlas_t *a = ecs_map_get_deref(&gpu->atlases, box_gpu_atlas_t, handle);
    if (a) {
        ecs_map_remove(&gpu->atlases, handle);
        box_gpu_atlas_free(a);
    }

    if (op == BOX_RENDER_DESTROY || !desc || desc->resolution < 1) {
        goto done;
    }

    uint32_t res = (uint32_t)desc->resolution;
    a = ecs_os_calloc_t(box_gpu_atlas_t);
    a->resolution = desc->resolution;
    a->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { res, res, 1 },
        .format = BOX_GPU_SHADOW_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    a->view = wgpuTextureCreateView(a->texture, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_SHADOW_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_DepthOnly
    });
    ecs_map_insert_ptr(&gpu->atlases, handle, a);
done:
    return;
}

const box_gpu_atlas_t* box_gpu_atlas_of(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    WGPUBuffer *tiles)
{
    const box_gpu_atlas_t *result = NULL;
    box_gpu_buffer_t *b = NULL;
    *tiles = NULL;
    if (!v->shadows.spots || !v->shadows.atlas) {
        goto done;
    }

    result = ecs_map_get_deref(&gpu->atlases, box_gpu_atlas_t, v->shadows.atlas);
    b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, v->shadows.atlas);
    if (!result || !b || b->size < BOX_GPU_ATLAS_TILES_SIZE ||
        !(b->usage & BOX_RENDER_BUFFER_UNIFORM))
    {
        result = NULL;
        goto done;
    }

    *tiles = b->buffer;
done:
    return result;
}

void box_gpu_atlas_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u)
{
    const box_render_view_shadows_t *s = &v->shadows;
    WGPUBuffer tiles;
    if (!box_gpu_atlas_of(gpu, v, &tiles)) {
        goto done;
    }

    u->spot_config[0] = s->tiles < BOX_RENDER_SHADOW_TILES ? s->tiles : BOX_RENDER_SHADOW_TILES;
    u->spot_config[1] = (uint32_t)(s->filter_size > 0 ? s->filter_size : 1);
    u->spot_bias[0] = s->depth_bias;
    u->spot_bias[1] = s->normal_offset;
done:
    return;
}

/** Make the matrix buffer of the atlas tiles hold the uniforms of a packet and write them: a
 * block with the matrix of each spot light tile, a block with the light, the octant and the
 * planes of each octant of a point light tile.
 *
 * @param gpu The backend
 * @param packet The packet
 */
static void box_gpu_atlas_matrices(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, o, count = ecs_vec_count(&packet->shadow_tiles), blocks = 0;
    int32_t capacity = gpu->atlas_capacity ? gpu->atlas_capacity : BOX_RENDER_SHADOW_TILES;
    ecs_vec_set_count_t(NULL, &gpu->atlas_blocks, int32_t, count);
    for (i = 0; i < count; i ++) {
        const box_render_shadow_tile_t *tile = ecs_vec_get_t(
            &packet->shadow_tiles, box_render_shadow_tile_t, i);
        *ecs_vec_get_t(&gpu->atlas_blocks, int32_t, i) = blocks;
        blocks += tile->projection == BOX_RENDER_SHADOW_OCTAHEDRAL
            ? BOX_RENDER_SHADOW_OCTANTS
            : 1;
    }

    ecs_vec_set_count_t(NULL, &gpu->atlas_data, uint8_t, blocks * BOX_GPU_CASCADE_STRIDE);
    ecs_os_memset(ecs_vec_first(&gpu->atlas_data), 0, blocks * BOX_GPU_CASCADE_STRIDE);
    for (i = 0; i < count; i ++) {
        const box_render_shadow_tile_t *tile = ecs_vec_get_t(
            &packet->shadow_tiles, box_render_shadow_tile_t, i);
        int32_t first = *ecs_vec_get_t(&gpu->atlas_blocks, int32_t, i);
        if (tile->projection != BOX_RENDER_SHADOW_OCTAHEDRAL) {
            ecs_os_memcpy(ecs_vec_get_t(&gpu->atlas_data, uint8_t,
                first * BOX_GPU_CASCADE_STRIDE), tile->matrix, sizeof(mat4));
            continue;
        }

        for (o = 0; o < BOX_RENDER_SHADOW_OCTANTS; o ++) {
            float *u = (float*)ecs_vec_get_t(&gpu->atlas_data, uint8_t,
                (first + o) * BOX_GPU_CASCADE_STRIDE);
            u[16] = tile->position[0];
            u[17] = tile->position[1];
            u[18] = tile->position[2];
            u[19] = (float)o;
            u[20] = tile->near_;
            u[21] = tile->far_;
        }
    }

    if (!gpu->atlas_buffer || blocks > gpu->atlas_capacity) {
        while (capacity < blocks) {
            capacity *= 2;
        }

        if (gpu->atlas_buffer) {
            wgpuBufferRelease(gpu->atlas_buffer);
        }

        gpu->atlas_capacity = capacity;
        gpu->atlas_buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)capacity * BOX_GPU_CASCADE_STRIDE
        });
    }

    wgpuQueueWriteBuffer(gpu->queue, gpu->atlas_buffer, 0, ecs_vec_first(&gpu->atlas_data),
        (size_t)blocks * BOX_GPU_CASCADE_STRIDE);
}

/** Get the backend atlas a packet atlas renders into.
 *
 * @param gpu The backend
 * @param a The atlas of the packet
 * @return The atlas, NULL when the backend lacks it at the resolution of the packet
 */
static box_gpu_atlas_t* box_gpu_atlas_target(
    box_gpu_t *gpu,
    const box_render_shadow_atlas_t *a)
{
    box_gpu_atlas_t *result = ecs_map_get_deref(&gpu->atlases, box_gpu_atlas_t, a->handle);

    return result && result->resolution == a->resolution ? result : NULL;
}

/** Decide which tiles of a packet render: those the packet flags, or every tile of an atlas
 * that cannot keep its tiles. The kept tiles of the atlases count as reused.
 *
 * An atlas cannot keep them when it was created since its last pass or a tile of that pass did
 * not render, when the shadow shader or the point light variant its tiles rendered with changed,
 * or when the backend lacks the pipeline that clears a tile.
 *
 * @param gpu The backend, after the shadow pipelines were brought up to date
 * @param packet The packet
 * @return Whether a tile renders
 */
static bool box_gpu_atlas_renders(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, j, count = ecs_vec_count(&packet->shadow_tiles);
    bool result = false;
    ecs_vec_set_count_t(NULL, &gpu->atlas_renders, bool, count);
    bool *renders = ecs_vec_first(&gpu->atlas_renders);
    for (i = 0; i < count; i ++) {
        renders[i] = false;
    }

    for (i = 0; i < ecs_vec_count(&packet->shadow_atlases); i ++) {
        const box_render_shadow_atlas_t *a = ecs_vec_get_t(
            &packet->shadow_atlases, box_render_shadow_atlas_t, i);
        const box_gpu_atlas_t *atlas = box_gpu_atlas_target(gpu, a);
        const box_render_shadow_tile_t *tiles = ecs_vec_get_t(
            &packet->shadow_tiles, box_render_shadow_tile_t, a->first);
        bool point = false, all;
        if (!atlas) {
            continue;
        }

        for (j = 0; j < a->count; j ++) {
            point = point || tiles[j].projection == BOX_RENDER_SHADOW_OCTAHEDRAL;
        }

        all = !atlas->valid || !gpu->shadow_clear || atlas->revision != gpu->shadow_revision ||
            (point && (atlas->point_shader != gpu->shadow_point_shader ||
            atlas->point_revision != gpu->shadow_point_revision));
        for (j = 0; j < a->count; j ++) {
            renders[a->first + j] = all || tiles[j].render;
            result = result || renders[a->first + j];
            if (!renders[a->first + j]) {
                box_gpu_count(gpu, BOX_GPU_STAT_SHADOW_REUSED);
            }
        }
    }

    return result;
}

/** Record the render pass that draws the casters of each tile of an atlas that renders, within
 * the viewport of the tile; a point light tile draws the casters of each octant, skipping an
 * octant without any. No pass when no tile renders.
 *
 * When every tile renders the pass clears the atlas, else it loads it and clears each tile it
 * renders with gpu->shadow_clear, so the other tiles stay. The atlas then remembers the shadow
 * pipelines its tiles rendered with, and whether every tile that was to render did.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param packet The packet
 * @param index Index of the atlas in the packet, the slot of the cull views of its tiles
 * @param group Binds the matrix buffer of the tiles
 * @param culled Whether the cull pass ran, false while the cull pipelines don't exist yet: the
 * tiles then get no casters and the atlas can't keep them
 */
static void box_gpu_atlas_pass(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    int32_t index,
    WGPUBindGroup group,
    bool culled)
{
    const box_render_shadow_atlas_t *a = ecs_vec_get_t(
        &packet->shadow_atlases, box_render_shadow_atlas_t, index);
    box_gpu_atlas_t *atlas = box_gpu_atlas_target(gpu, a);
    WGPURenderPipeline point = box_gpu_shadow_point_pipeline(gpu);
    const bool *renders = ecs_vec_get_t(&gpu->atlas_renders, bool, a->first);
    int32_t i, o, rendered = 0;
    bool every = true, kept = true;
    if (!atlas) {
        goto done;
    }

    for (i = 0; i < a->count; i ++) {
        rendered += renders[i];
        every = every && renders[i];
    }

    if (!rendered) {
        goto done;
    }

    WGPURenderPassDepthStencilAttachment depth = {
        .view = atlas->view,
        .depthLoadOp = every ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor desc = { .depthStencilAttachment = &depth };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    box_gpu_count(gpu, BOX_GPU_STAT_SHADOW_PASS);
    for (i = 0; i < a->count; i ++) {
        const box_render_shadow_tile_t *tile = ecs_vec_get_t(
            &packet->shadow_tiles, box_render_shadow_tile_t, a->first + i);
        int32_t block = *ecs_vec_get_t(&gpu->atlas_blocks, int32_t, a->first + i);
        bool octahedral = tile->projection == BOX_RENDER_SHADOW_OCTAHEDRAL;
        if (!renders[i]) {
            continue;
        }

        if (tile->size < 1 || tile->x < 0 || tile->y < 0 ||
            tile->x + tile->size > atlas->resolution || tile->y + tile->size > atlas->resolution)
        {
            continue;
        }

        if (octahedral && !point) {
            kept = false;
            continue;
        }

        wgpuRenderPassEncoderSetViewport(pass, (float)tile->x, (float)tile->y,
            (float)tile->size, (float)tile->size, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)tile->x, (uint32_t)tile->y,
            (uint32_t)tile->size, (uint32_t)tile->size);
        if (!every) {
            wgpuRenderPassEncoderSetPipeline(pass, gpu->shadow_clear);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        }

        for (o = 0; o < (octahedral ? BOX_RENDER_SHADOW_OCTANTS : 1); o ++) {
            const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, index,
                BOX_GPU_ATLAS_FRUSTUM(i, o));
            uint32_t offset = (uint32_t)((block + o) * BOX_GPU_CASCADE_STRIDE);
            if (!cv && !culled) {
                kept = false;
                continue;
            }

            if (cv && !cv->draw_count) {
                continue;
            }

            wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
            if (cv && octahedral) {
                kept = box_gpu_shadow_draw(gpu, pass, packet, cv, point,
                    gpu->shadow_point_cutout, true, true) && kept;
            } else if (cv) {
                kept = box_gpu_shadow_draw(gpu, pass, packet, cv, gpu->shadow,
                    gpu->shadow_cutout, false, true) && kept;
            }
        }

        box_gpu_count(gpu, BOX_GPU_STAT_SHADOW_TILE);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    atlas->valid = kept;
    atlas->revision = gpu->shadow_revision;
    atlas->point_shader = point ? gpu->shadow_point_shader : 0;
    atlas->point_revision = point ? gpu->shadow_point_revision : 0;
done:
    return;
}

/** Copy the draw arguments the cull of the tiles wrote and remember the draws of each tile, so
 * box_render_gpu_cull_tile can read what each tile kept.
 *
 * @param gpu The backend
 * @param encoder The encoder, after the cull dispatches
 * @param packet The packet
 */
static void box_gpu_atlas_counts(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet)
{
    box_gpu_cull_t *c = &gpu->cull;
    uint64_t size = (uint64_t)ecs_vec_count(&c->arg_data) * sizeof(uint32_t);
    int32_t i, j, o, d;
    ecs_vec_clear(&gpu->atlas_reads);
    if (!size || !c->args) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&packet->shadow_atlases); i ++) {
        const box_render_shadow_atlas_t *a = ecs_vec_get_t(
            &packet->shadow_atlases, box_render_shadow_atlas_t, i);
        for (j = 0; j < a->count; j ++) {
            for (o = 0; o < BOX_RENDER_SHADOW_OCTANTS; o ++) {
                const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, i,
                    BOX_GPU_ATLAS_FRUSTUM(j, o));
                for (d = 0; cv && d < cv->draw_count; d ++) {
                    const box_gpu_cull_draw_t *draw = ecs_vec_get_t(
                        &c->draws, box_gpu_cull_draw_t, cv->first_draw + d);
                    if (draw->skip) {
                        continue;
                    }

                    *ecs_vec_append_t(NULL, &gpu->atlas_reads, box_gpu_atlas_read_t) =
                        (box_gpu_atlas_read_t){ a->first + j, draw->args, draw->count };
                }
            }
        }
    }

    if (!gpu->atlas_counts || gpu->atlas_counts_size < size) {
        if (gpu->atlas_counts) {
            wgpuBufferRelease(gpu->atlas_counts);
        }

        gpu->atlas_counts = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
            .size = size
        });
        gpu->atlas_counts_size = size;
    }

    wgpuCommandEncoderCopyBufferToBuffer(encoder, c->args, 0, gpu->atlas_counts, 0, size);
done:
    return;
}

void box_gpu_atlas_render(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->shadow_atlases);
    WGPURenderPipeline pipeline = NULL;
    ecs_vec_clear(&gpu->atlas_reads);
    if (!count || !ecs_vec_count(&packet->shadow_tiles)) {
        goto done;
    }

    ecs_os_mutex_lock(gpu->lock);
    gpu->shadow_core = gpu->shadow_core_request;
    ecs_os_mutex_unlock(gpu->lock);
    pipeline = box_gpu_shadow_pipeline(gpu);
    if (!pipeline) {
        goto done;
    }

    box_gpu_shadow_point_pipeline(gpu);
    if (!box_gpu_atlas_renders(gpu, packet)) {
        goto done;
    }

    box_gpu_atlas_matrices(gpu, packet);
    box_gpu_cull_reset(gpu);
    for (i = 0; i < count; i ++) {
        const box_render_shadow_atlas_t *a = ecs_vec_get_t(
            &packet->shadow_atlases, box_render_shadow_atlas_t, i);
        if (box_gpu_atlas_target(gpu, a)) {
            box_gpu_cull_casters_add(gpu, packet, a->scene, i, ecs_vec_get_t(
                &packet->shadow_tiles, box_render_shadow_tile_t, a->first),
                ecs_vec_get_t(&gpu->atlas_renders, bool, a->first), a->count);
        }
    }

    box_gpu_cull_upload(gpu);
    WGPUBindGroupEntry entry = {
        .binding = 0, .buffer = gpu->atlas_buffer, .size = BOX_GPU_SHADOW_UNIFORM_SIZE
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->shadow_layout,
        .entryCount = 1,
        .entries = &entry
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    box_gpu_cull_record_all(gpu, encoder, NULL);
    bool culled = !ecs_vec_count(&gpu->cull.views) || (gpu->cull.bound && gpu->cull.parallel);
    box_gpu_atlas_counts(gpu, encoder, packet);
    for (i = 0; i < count; i ++) {
        box_gpu_atlas_pass(gpu, encoder, packet, i, group, culled);
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(group);
done:
    return;
}

bool box_render_gpu_cull_tile(
    ecs_world_t *world,
    int32_t tile,
    box_render_cull_stats_t *out)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    ecs_vec_t args = {0};
    uint32_t words = 0;
    int32_t i;
    ecs_os_memset_t(out, 0, box_render_cull_stats_t);
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_cull_tile needs RenderSettings.main_thread and box_render_sync");
    if (!renders || !gpu->atlas_counts) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&gpu->atlas_reads); i ++) {
        const box_gpu_atlas_read_t *r = ecs_vec_get_t(&gpu->atlas_reads, box_gpu_atlas_read_t, i);
        if (r->tile == tile && (r->args + 1) * BOX_GPU_CULL_ARGS > words) {
            words = (r->args + 1) * BOX_GPU_CULL_ARGS;
        }
    }

    if (!words || words * sizeof(uint32_t) > gpu->atlas_counts_size) {
        goto done;
    }

    ecs_vec_init_t(NULL, &args, uint32_t, (int32_t)words);
    ecs_vec_set_count_t(NULL, &args, uint32_t, (int32_t)words);
    uint32_t *counts = ecs_vec_first(&args);
    if (!box_gpu_read(gpu, gpu->atlas_counts, 0, words * (uint32_t)sizeof(uint32_t), counts)) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&gpu->atlas_reads); i ++) {
        const box_gpu_atlas_read_t *r = ecs_vec_get_t(&gpu->atlas_reads, box_gpu_atlas_read_t, i);
        if (r->tile == tile) {
            out->tested += (int32_t)r->count;
            out->visible += (int32_t)counts[r->args * BOX_GPU_CULL_ARGS + 1];
            result = true;
        }
    }

done:
    ecs_vec_fini_t(NULL, &args, uint32_t);

    return result;
}

#else

bool box_render_gpu_cull_tile(
    ecs_world_t *world,
    int32_t tile,
    box_render_cull_stats_t *out)
{
    (void)world;
    (void)tile;
    ecs_os_memset_t(out, 0, box_render_cull_stats_t);

    return false;
}

#endif
