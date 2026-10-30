/**
 * @file render/gpu_draw.c
 * @brief Meshes, buffers and the forward pass of the webgpu backend.
 *
 * Every view renders in order into the color texture of its target with a depth buffer cleared
 * to 0 and compared with greater (reversed Z), drawing its batches indirectly. A view that culls
 * by occlusion draws twice: what its early cull phase kept, then what its late phase kept.
 *
 * A lit view first draws its opaque meshes depth only (a prepass, early and late around the Hi-Z
 * build with occlusion), then shades them once per pixel with compare Equal and no depth writes,
 * the same instances placed by one function of mesh.wgsl with an invariant clip position.
 */

#include "gpu_environment.h"
#include <math.h>
#include <string.h>

#ifdef BOX_RENDER_WGPU

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static double box_gpu_linear(
    uint8_t value)
{
    double c = (double)value / 255.0;

    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/** Create a buffer.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_buffer_new(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    WGPUBufferDescriptor desc = {
        .usage = usage | WGPUBufferUsage_CopyDst,
        .size = (size + 3) & ~(uint64_t)3
    };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

void box_gpu_draw_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[20] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_view_uniforms_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Comparison }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_CubeArray
            }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 9,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_3D
            }
        },
        {
            .binding = 10,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 11,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 12,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube
            }
        },
        {
            .binding = 13,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_GPU_PROBE_SIZE
            }
        },
        {
            .binding = 14,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 15,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 16,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_GPU_ATLAS_TILES_SIZE
            }
        },
        {
            .binding = 17,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 18,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 19,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 20, .entries = entries };
    box_gpu_cull_init(gpu);
    box_gpu_volume_init(gpu);
    box_gpu_reflect_init(gpu);
    box_gpu_environment_init(gpu);
    gpu->view_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    WGPUBindGroupLayout groups[4] = {
        gpu->view_layout, gpu->material_layout, gpu->instance_layout, gpu->volume_layout
    };
    gpu->mesh_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 4,
            .bindGroupLayouts = groups
        });
    gpu->prepass_empty_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 0 });
    gpu->prepass_empty = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->prepass_empty_layout,
        .entryCount = 0
    });
    WGPUBindGroupLayout prepass[3] = {
        gpu->view_layout, gpu->prepass_empty_layout, gpu->transform_layout
    };
    WGPUBindGroupLayout prepass_cutout[3] = {
        gpu->view_layout, gpu->material_layout, gpu->instance_layout
    };
    gpu->prepass_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 3,
            .bindGroupLayouts = prepass
        });
    gpu->prepass_cutout_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 3,
            .bindGroupLayouts = prepass_cutout
        });
    ecs_map_init(&gpu->meshes, NULL);
    ecs_map_init(&gpu->buffers, NULL);
    ecs_map_init(&gpu->forward, NULL);
    ecs_vec_init_t(NULL, &gpu->view_data, uint8_t, 0);
    box_gpu_light_init(gpu);
    box_gpu_shadow_init(gpu);
    box_gpu_effects_init(gpu);
    box_gpu_gtao_init(gpu);
    box_gpu_transmission_init(gpu);
    box_gpu_atmosphere_init(gpu);
    box_gpu_particles_init(gpu);
}

/** Release the buffers of a mesh and the mesh.
 *
 * @param gpu The backend
 * @param m The mesh
 */
static void box_gpu_mesh_free(
    box_gpu_t *gpu,
    box_gpu_mesh_t *m)
{
    wgpuBufferRelease(m->surface_assets);
    wgpuBufferRelease(m->vertices);
    wgpuBufferRelease(m->indices);
    ecs_os_free(m);
    box_gpu_count(gpu, BOX_GPU_STAT_MESH_DESTROYED);
}

void box_gpu_instance_layouts_init(
    box_gpu_t *gpu)
{
    box_render_material_t fallback = {
        .color = 204u | (204u << 8) | (204u << 16),
        .metallic = BOX_MATERIAL_METALLIC,
        .roughness = BOX_MATERIAL_ROUGHNESS,
        .attenuation_color = 0xFFFFFFu,
        .uv = { 1.0f, 1.0f, 0.0f, 0.0f },
        .ior = BOX_MATERIAL_IOR,
        .attenuation_distance = BOX_MATERIAL_ATTENUATION_DISTANCE
    };
    WGPUBindGroupLayoutEntry entries[5] = {{
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Compute,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    }, {
        .binding = 1,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    }, {
        .binding = 3,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    }, {
        .binding = 2,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    }, {
        .binding = 4,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    }};
    gpu->instance_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 5, .entries = entries });
    gpu->transform_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 3, .entries = entries });
    gpu->no_materials = box_gpu_buffer_new(gpu, sizeof(box_render_material_t),
        WGPUBufferUsage_Storage);
    wgpuQueueWriteBuffer(gpu->queue, gpu->no_materials, 0, &fallback, sizeof(fallback));
}

void box_gpu_instance_layouts_fini(
    box_gpu_t *gpu)
{
    if (gpu->instance_layout) {
        wgpuBindGroupLayoutRelease(gpu->instance_layout);
        gpu->instance_layout = NULL;
    }

    if (gpu->transform_layout) {
        wgpuBindGroupLayoutRelease(gpu->transform_layout);
        gpu->transform_layout = NULL;
    }

    if (gpu->no_materials) {
        wgpuBufferRelease(gpu->no_materials);
        gpu->no_materials = NULL;
    }
}

/** Create a bind group of the streams of an instance page and surface assets.
 *
 * A page without a surface stream, a 2D page, binds gpu->no_surfaces at binding 3.
 *
 * A group with the shading stream binds the material table too, or the default record before
 * the main thread sent one.
 *
 * @param gpu The backend
 * @param streams The buffers of the streams of the page, by stream, NULL for the surface
 * stream of a page without one
 * @param assets The surface assets
 * @param size Bytes of the surface assets
 * @param shading Bind the shading stream too, instance_layout, else transform_layout
 * @return The bind group
 */
static WGPUBindGroup box_gpu_instance_group_new(
    box_gpu_t *gpu,
    box_gpu_buffer_t *const *streams,
    WGPUBuffer assets,
    uint64_t size,
    bool shading)
{
    const box_gpu_buffer_t *transforms = streams[BOX_RENDER_STREAM_TRANSFORM];
    const box_gpu_buffer_t *shadings = streams[BOX_RENDER_STREAM_SHADING];
    const box_gpu_buffer_t *surfaces = streams[BOX_RENDER_STREAM_SURFACE];
    WGPUBuffer surface_buffer = surfaces ? surfaces->buffer : gpu->no_surfaces;
    uint64_t surface_size = surfaces ? surfaces->size : sizeof(box_render_surface_t);
    const box_gpu_buffer_t *materials = ecs_map_get_deref(
        &gpu->buffers, box_gpu_buffer_t, BOX_RENDER_BUFFER_MATERIALS);
    WGPUBindGroupEntry entry[5] = {
        {.binding = 0, .buffer = transforms->buffer, .size = transforms->size},
        {.binding = 1, .buffer = assets, .size = size},
        {.binding = 3, .buffer = surface_buffer, .size = surface_size},
        {.binding = 2, .buffer = shadings->buffer, .size = shadings->size},
        {
            .binding = 4,
            .buffer = materials ? materials->buffer : gpu->no_materials,
            .size = materials ? materials->size : sizeof(box_render_material_t)
        }
    };

    return wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = shading ? gpu->instance_layout : gpu->transform_layout,
        .entryCount = shading ? 5 : 3,
        .entries = entry
    });
}

WGPUBindGroup box_gpu_instance_group(
    box_gpu_t *gpu,
    uint32_t page,
    uint32_t handle,
    const box_gpu_mesh_t *m,
    bool shading)
{
    WGPUBindGroup result = NULL;
    box_gpu_buffer_t *streams[BOX_RENDER_INSTANCE_STREAMS] = {0};
    int32_t i, count = page >= BOX_RENDER_BUFFER_FIRST_2D
        ? BOX_RENDER_INSTANCE_STREAMS_2D : BOX_RENDER_INSTANCE_STREAMS;
    for (i = 0; i < count; i ++) {
        streams[i] = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, page + (uint32_t)i);
        if (!streams[i] || !(streams[i]->usage & BOX_RENDER_BUFFER_STORAGE)) {
            goto done;
        }
    }

    box_gpu_buffer_t *b = streams[BOX_RENDER_STREAM_TRANSFORM];
    if (!m || !m->assets) {
        WGPUBindGroup *group = shading ? &b->instances : &b->transforms;
        if (!*group) {
            *group = box_gpu_instance_group_new(gpu, streams, gpu->no_assets, sizeof(vec4),
                shading);
        }

        result = *group;
        goto done;
    }

    box_gpu_instance_group_t *g = NULL;
    for (i = 0; i < ecs_vec_count(&b->mesh_groups); i ++) {
        g = ecs_vec_get_t(&b->mesh_groups, box_gpu_instance_group_t, i);
        if (g->mesh == handle && g->shading == shading) {
            break;
        }

        g = NULL;
    }

    if (g && g->assets != m->surface_assets) {
        wgpuBindGroupRelease(g->group);
        wgpuBufferRelease(g->assets);
    } else if (g) {
        result = g->group;
        goto done;
    } else {
        g = ecs_vec_append_t(NULL, &b->mesh_groups, box_gpu_instance_group_t);
        g->mesh = handle;
        g->shading = shading;
    }

    g->assets = m->surface_assets;
    wgpuBufferAddRef(g->assets);
    g->group = box_gpu_instance_group_new(gpu, streams, m->surface_assets,
        m->surface_asset_size, shading);
    result = g->group;
done:
    return result;
}

/** Release the instance bind groups of a buffer.
 *
 * @param b The buffer
 */
static void box_gpu_instance_groups_release(
    box_gpu_buffer_t *b)
{
    int32_t i;
    if (b->instances) {
        wgpuBindGroupRelease(b->instances);
        b->instances = NULL;
    }

    if (b->transforms) {
        wgpuBindGroupRelease(b->transforms);
        b->transforms = NULL;
    }

    for (i = 0; i < ecs_vec_count(&b->mesh_groups); i ++) {
        box_gpu_instance_group_t *g = ecs_vec_get_t(
            &b->mesh_groups, box_gpu_instance_group_t, i);
        wgpuBindGroupRelease(g->group);
        wgpuBufferRelease(g->assets);
    }

    ecs_vec_clear(&b->mesh_groups);
}

/** Release the bind groups of the instance pages with the surface assets of a mesh, once the
 * mesh is released.
 *
 * @param gpu The backend
 * @param handle Handle of the mesh
 */
static void box_gpu_instance_groups_drop(
    box_gpu_t *gpu,
    uint32_t handle)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->buffers);
    int32_t i;
    while (ecs_map_next(&it)) {
        box_gpu_buffer_t *b = ecs_map_ptr(&it);
        for (i = ecs_vec_count(&b->mesh_groups) - 1; i >= 0; i --) {
            box_gpu_instance_group_t *g = ecs_vec_get_t(
                &b->mesh_groups, box_gpu_instance_group_t, i);
            if (g->mesh == handle) {
                wgpuBindGroupRelease(g->group);
                wgpuBufferRelease(g->assets);
                ecs_vec_remove_t(&b->mesh_groups, box_gpu_instance_group_t, i);
            }
        }
    }
}

/** Release the bind groups of the instance page a buffer is a stream of, before the buffer is
 * created again or destroyed: they live with the transform stream of the page. The material
 * table is bound by the groups of every page, which are all released with it.
 *
 * @param gpu The backend
 * @param handle Handle of the buffer
 */
static void box_gpu_instance_page_changed(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_buffer_t *b;
    if (handle == BOX_RENDER_BUFFER_MATERIALS) {
        ecs_map_iter_t it = ecs_map_iter(&gpu->buffers);
        while (ecs_map_next(&it)) {
            box_gpu_instance_groups_release(ecs_map_ptr(&it));
        }

        goto done;
    }

    if (handle < BOX_RENDER_BUFFER_FIRST || handle >= BOX_RENDER_BUFFER_LIGHT_FIRST) {
        goto done;
    }

    handle -= (handle - BOX_RENDER_BUFFER_FIRST) % BOX_RENDER_INSTANCE_STREAMS;
    b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, handle);
    if (b) {
        box_gpu_instance_groups_release(b);
    }

done:
    return;
}

/** Release a buffer and its handle.
 *
 * @param gpu The backend
 * @param b The buffer
 */
static void box_gpu_buffer_free(
    box_gpu_t *gpu,
    box_gpu_buffer_t *b)
{
    box_gpu_instance_groups_release(b);
    ecs_vec_fini_t(NULL, &b->mesh_groups, box_gpu_instance_group_t);
    wgpuBufferRelease(b->buffer);
    ecs_os_free(b);
    box_gpu_count(gpu, BOX_GPU_STAT_BUFFER_DESTROYED);
}

void box_gpu_forward_reset(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it;
    if (!ecs_map_is_init(&gpu->forward)) {
        goto done;
    }

    it = ecs_map_iter(&gpu->forward);
    while (ecs_map_next(&it)) {
        WGPURenderPipeline pipeline = ecs_map_ptr(&it);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

    ecs_map_clear(&gpu->forward);
done:
    return;
}

void box_gpu_draw_fini(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->meshes);
    while (ecs_map_next(&it)) {
        box_gpu_mesh_free(gpu, ecs_map_ptr(&it));
    }

    it = ecs_map_iter(&gpu->buffers);
    while (ecs_map_next(&it)) {
        box_gpu_buffer_free(gpu, ecs_map_ptr(&it));
    }

    ecs_map_fini(&gpu->meshes);
    ecs_map_fini(&gpu->buffers);
    if (gpu->copy_staging) {
        wgpuBufferRelease(gpu->copy_staging);
        gpu->copy_staging = NULL;
        gpu->copy_staging_size = 0;
    }

    ecs_vec_fini_t(NULL, &gpu->view_data, uint8_t);
    box_gpu_forward_reset(gpu);
    ecs_map_fini(&gpu->forward);
    box_gpu_light_fini(gpu);
    box_gpu_shadow_fini(gpu);
    box_gpu_cull_fini(gpu);
    box_gpu_bake_fini(gpu);
    box_gpu_volume_fini(gpu);
    box_gpu_reflect_fini(gpu);
    box_gpu_environment_fini(gpu);
    box_gpu_effects_fini(gpu);
    box_gpu_gtao_fini(gpu);
    box_gpu_transmission_fini(gpu);
    box_gpu_atmosphere_fini(gpu);
    box_gpu_particles_fini(gpu);
    if (gpu->view_buffer) {
        wgpuBufferRelease(gpu->view_buffer);
        gpu->view_buffer = NULL;
    }

    if (gpu->mesh_layout) {
        wgpuPipelineLayoutRelease(gpu->mesh_layout);
        gpu->mesh_layout = NULL;
    }

    if (gpu->prepass_layout) {
        wgpuPipelineLayoutRelease(gpu->prepass_layout);
        gpu->prepass_layout = NULL;
    }

    if (gpu->prepass_cutout_layout) {
        wgpuPipelineLayoutRelease(gpu->prepass_cutout_layout);
        gpu->prepass_cutout_layout = NULL;
    }

    if (gpu->prepass_empty) {
        wgpuBindGroupRelease(gpu->prepass_empty);
        gpu->prepass_empty = NULL;
    }

    if (gpu->prepass_empty_layout) {
        wgpuBindGroupLayoutRelease(gpu->prepass_empty_layout);
        gpu->prepass_empty_layout = NULL;
    }

    if (gpu->view_layout) {
        wgpuBindGroupLayoutRelease(gpu->view_layout);
        gpu->view_layout = NULL;
    }
}

/** Compute the box around the vertex positions of a mesh.
 *
 * @param m The mesh
 * @param vertices The vertices
 * @param count Number of vertices
 */
static void box_gpu_mesh_bounds(
    box_gpu_mesh_t *m,
    const box_vertex_t *vertices,
    uint32_t count)
{
    vec3 lo, hi;
    uint32_t i;
    glm_vec3_copy(ECS_CONST_CAST(float*, vertices[0].position), lo);
    glm_vec3_copy(lo, hi);
    for (i = 1; i < count; i ++) {
        glm_vec3_minv(lo, ECS_CONST_CAST(float*, vertices[i].position), lo);
        glm_vec3_maxv(hi, ECS_CONST_CAST(float*, vertices[i].position), hi);
    }

    glm_vec3_center(lo, hi, m->center);
    glm_vec3_sub(hi, m->center, m->extent);
}

void box_gpu_mesh_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_mesh_desc_t *desc)
{
    box_gpu_mesh_t *m = ecs_map_get_deref(&gpu->meshes, box_gpu_mesh_t, handle);
    if (m) {
        if (m->assets) {
            box_gpu_instance_groups_drop(gpu, handle);
        }

        ecs_map_remove(&gpu->meshes, handle);
        box_gpu_mesh_free(gpu, m);
    }

    if (op == BOX_RENDER_DESTROY || !desc || !desc->vertex_count || !desc->index_count) {
        goto done;
    }

    uint64_t vertex_size = (uint64_t)desc->vertex_count * sizeof(box_vertex_t);
    uint64_t index_size = (uint64_t)desc->index_count * sizeof(uint32_t);
    uint64_t asset_size = desc->surface_asset_count ?
        (uint64_t)desc->surface_asset_count * sizeof(vec4) : sizeof(vec4);
    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuDeviceGetLimits(gpu->device, &limits) != WGPUStatus_Success ||
        vertex_size > limits.maxBufferSize || index_size > limits.maxBufferSize ||
        index_size > limits.maxStorageBufferBindingSize ||
        asset_size > limits.maxBufferSize || asset_size > limits.maxStorageBufferBindingSize)
    {
        ecs_err("surface mesh exceeds device buffer capacity");
        goto done;
    }

    const uint8_t *vertices = (const uint8_t*)(desc + 1);
    m = ecs_os_calloc_t(box_gpu_mesh_t);
    m->vertices = box_gpu_buffer_new(gpu, vertex_size, WGPUBufferUsage_Vertex);
    m->indices = box_gpu_buffer_new(gpu, index_size,
        WGPUBufferUsage_Index | WGPUBufferUsage_Storage);
    m->surface_asset_size = desc->surface_asset_count ?
        (uint64_t)desc->surface_asset_count * sizeof(vec4) : sizeof(vec4);
    m->assets = desc->surface_asset_count != 0;
    m->surface_assets = box_gpu_buffer_new(gpu, m->surface_asset_size, WGPUBufferUsage_Storage);
    if (desc->surface_asset_count) {
        wgpuQueueWriteBuffer(gpu->queue, m->surface_assets, 0,
            vertices + vertex_size + index_size, m->surface_asset_size);
    }

    m->index_count = desc->index_count;
    box_gpu_mesh_bounds(m, (const box_vertex_t*)vertices, desc->vertex_count);
    wgpuQueueWriteBuffer(gpu->queue, m->vertices, 0, vertices, (size_t)vertex_size);
    wgpuQueueWriteBuffer(gpu->queue, m->indices, 0, vertices + vertex_size, (size_t)index_size);
    ecs_map_insert_ptr(&gpu->meshes, handle, m);
    box_gpu_count(gpu, BOX_GPU_STAT_MESH_CREATED);
done:
    return;
}

/** Check whether a buffer can be the source of GPU copies.
 *
 * @param b The buffer
 * @return Whether it was created with CopySrc usage
 */
static bool box_gpu_buffer_copyable(
    const box_gpu_buffer_t *b)
{
    return (b->usage & (BOX_RENDER_BUFFER_STORAGE | BOX_RENDER_BUFFER_COPY)) != 0;
}

/** Copy the bytes a buffer created again has in common with the buffer it replaces from the old
 * one, before the old one is released. The copy is submitted on its own, ahead of the copies,
 * uploads and scatters of the packet, which the queue runs after it.
 *
 * @param gpu The backend
 * @param from The buffer replaced
 * @param to The buffer created
 */
static void box_gpu_buffer_keep(
    box_gpu_t *gpu,
    const box_gpu_buffer_t *from,
    const box_gpu_buffer_t *to)
{
    uint64_t size = from->size < to->size ? from->size : to->size;
    size &= ~(uint64_t)(BOX_RENDER_COPY_ALIGN - 1);
    if (!size) {
        goto done;
    }

    if (!box_gpu_buffer_copyable(from)) {
        ecs_err("buffer of %llu bytes cannot be kept, it was not created as a copy source",
            (unsigned long long)from->size);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, from->buffer, 0, to->buffer, 0, size);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
}

void box_gpu_buffer_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_buffer_desc_t *desc)
{
    box_gpu_buffer_t *b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, handle);
    box_gpu_buffer_t *created = NULL;
    WGPUBufferUsage usage = 0;
    box_gpu_instance_page_changed(gpu, handle);
    if (b) {
        ecs_map_remove(&gpu->buffers, handle);
    }

    if (op == BOX_RENDER_DESTROY || !desc || !desc->size) {
        goto done;
    }

    if (desc->usage & BOX_RENDER_BUFFER_INSTANCE) {
        usage |= WGPUBufferUsage_Vertex;
    }

    if (desc->usage & (BOX_RENDER_BUFFER_STORAGE | BOX_RENDER_BUFFER_COPY)) {
        usage |= WGPUBufferUsage_CopySrc;
    }

    if (desc->usage & BOX_RENDER_BUFFER_STORAGE) {
        usage |= WGPUBufferUsage_Storage;
    }

    if (desc->usage & BOX_RENDER_BUFFER_UNIFORM) {
        usage |= WGPUBufferUsage_Uniform;
    }

    created = ecs_os_calloc_t(box_gpu_buffer_t);
    created->buffer = box_gpu_buffer_new(gpu, desc->size, usage);
    ecs_vec_init_t(NULL, &created->mesh_groups, box_gpu_instance_group_t, 0);
    created->size = desc->size;
    created->usage = desc->usage;
    created->version = ++ gpu->buffer_writes;
    if (b && (desc->usage & BOX_RENDER_BUFFER_KEEP)) {
        box_gpu_buffer_keep(gpu, b, created);
    }

    ecs_map_insert_ptr(&gpu->buffers, handle, created);
    box_gpu_count(gpu, BOX_GPU_STAT_BUFFER_CREATED);
done:
    if (b) {
        box_gpu_buffer_free(gpu, b);
    }

    return;
}

/** Get the staging buffer the copies of a packet go through, holding at least a size.
 *
 * @param gpu The backend
 * @param size Bytes it must hold
 * @return The buffer
 */
static WGPUBuffer box_gpu_copy_staging(
    box_gpu_t *gpu,
    uint64_t size)
{
    if (gpu->copy_staging_size < size) {
        uint64_t capacity = gpu->copy_staging_size ? gpu->copy_staging_size : 64 * 1024;
        while (capacity < size) {
            capacity *= 2;
        }

        capacity = capacity > BOX_RENDER_COPY_MAX ? BOX_RENDER_COPY_MAX : capacity;
        if (gpu->copy_staging) {
            wgpuBufferRelease(gpu->copy_staging);
        }

        gpu->copy_staging = box_gpu_buffer_new(gpu, capacity, WGPUBufferUsage_CopySrc);
        gpu->copy_staging_size = capacity;
    }

    return gpu->copy_staging;
}

/** Find the buffers of a copy of a packet and check the copy against them.
 *
 * @param gpu The backend
 * @param c The copy
 * @param src Receives the source buffer
 * @param dst Receives the destination buffer
 * @return Whether the copy is valid
 */
static bool box_gpu_copy_buffers(
    box_gpu_t *gpu,
    const box_render_copy_t *c,
    box_gpu_buffer_t **src,
    box_gpu_buffer_t **dst)
{
    *src = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, c->src);
    *dst = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, c->dst);

    return *src && *dst && box_gpu_buffer_copyable(*src) &&
        !((c->src_offset | c->dst_offset | c->size) & (BOX_RENDER_COPY_ALIGN - 1)) &&
        (uint64_t)c->src_offset + c->size <= (*src)->size &&
        (uint64_t)c->dst_offset + c->size <= (*dst)->size;
}

/** Get the bytes a copy takes in the staging buffer.
 *
 * @param c The copy
 * @return Its size rounded up to BOX_RENDER_COPY_ALIGN
 */
static uint64_t box_gpu_copy_staged(
    const box_render_copy_t *c)
{
    return ((uint64_t)c->size + BOX_RENDER_COPY_ALIGN - 1) &
        ~(uint64_t)(BOX_RENDER_COPY_ALIGN - 1);
}

void box_gpu_copy(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->copies);
    uint64_t total = 0, offset = 0;
    box_gpu_buffer_t *src, *dst;
    if (!count) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        total += box_gpu_copy_staged(ecs_vec_get_t(&packet->copies, box_render_copy_t, i));
    }

    if (total > BOX_RENDER_COPY_MAX) {
        ecs_err("copies of %llu bytes exceed the %llu bytes of a packet",
            (unsigned long long)total, (unsigned long long)BOX_RENDER_COPY_MAX);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    /* Source and destination of a copy may be the same buffer, which copyBufferToBuffer does
     * not allow, so every source is copied into the staging buffer first and every
     * destination written from there after: each copy reads its source as it was before the
     * copies of the packet */
    WGPUBuffer staging = box_gpu_copy_staging(gpu, total);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (i = 0; i < count; i ++) {
        const box_render_copy_t *c = ecs_vec_get_t(&packet->copies, box_render_copy_t, i);
        if (!box_gpu_copy_buffers(gpu, c, &src, &dst)) {
            ecs_err("copy of %u bytes from %u in buffer %u to %u in buffer %u is invalid",
                c->size, c->src_offset, c->src, c->dst_offset, c->dst);
            box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        } else if (c->size) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, src->buffer, c->src_offset,
                staging, offset, c->size);
        }

        offset += box_gpu_copy_staged(c);
    }

    offset = 0;
    for (i = 0; i < count; i ++) {
        const box_render_copy_t *c = ecs_vec_get_t(&packet->copies, box_render_copy_t, i);
        if (c->size && box_gpu_copy_buffers(gpu, c, &src, &dst)) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, staging, offset,
                dst->buffer, c->dst_offset, c->size);
            dst->version = ++ gpu->buffer_writes;
        }

        offset += box_gpu_copy_staged(c);
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
}

void box_gpu_upload(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->uploads);
    for (i = 0; i < count; i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &packet->uploads, box_render_upload_t, i);
        box_gpu_buffer_t *b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, u->buffer);
        if (!b || (uint64_t)u->offset + u->size > b->size || (u->offset | u->size) & 3) {
            ecs_err("upload of %u bytes at %u does not fit buffer %u",
                u->size, u->offset, u->buffer);
            box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
            continue;
        }

        wgpuQueueWriteBuffer(gpu->queue, b->buffer, u->offset,
            box_render_packet_data(packet, u->data), u->size);
        b->version = ++ gpu->buffer_writes;
    }
}

/** Get the blend state of a blend mode.
 *
 * The fragment shader shapes its output for the equation: multiply and min mix toward
 * white by the alpha, screen and max scale by it.
 *
 * @param blend The BlendMode
 * @param out Receives the blend state
 */
static void box_gpu_blend_state(
    uint32_t blend,
    WGPUBlendState *out)
{
    WGPUBlendComponent color = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_SrcAlpha,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
    };
    WGPUBlendComponent alpha = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
    };
    switch (blend) {
    case PremultipliedAlpha:
        color.srcFactor = WGPUBlendFactor_One;
        break;
    case Additive:
        color.dstFactor = WGPUBlendFactor_One;
        alpha.dstFactor = WGPUBlendFactor_One;
        alpha.srcFactor = WGPUBlendFactor_Zero;
        break;
    case Multiply:
        color.srcFactor = WGPUBlendFactor_Dst;
        color.dstFactor = WGPUBlendFactor_Zero;
        break;
    case Screen:
        color.srcFactor = WGPUBlendFactor_One;
        color.dstFactor = WGPUBlendFactor_OneMinusSrc;
        break;
    case Subtractive:
        color.operation = WGPUBlendOperation_ReverseSubtract;
        color.dstFactor = WGPUBlendFactor_One;
        alpha.srcFactor = WGPUBlendFactor_Zero;
        alpha.dstFactor = WGPUBlendFactor_One;
        break;
    case Min:
    case Max:
        color.operation = blend == Min ? WGPUBlendOperation_Min : WGPUBlendOperation_Max;
        color.srcFactor = WGPUBlendFactor_One;
        color.dstFactor = WGPUBlendFactor_One;
        break;
    default:
        break;
    }

    out->color = color;
    out->alpha = alpha;
}

/** Create the forward pipeline of a shader, color format, blend mode, back face mode, alpha
 * test and depth mode.
 *
 * The 2D shape shader blends its edges in the opaque pass too and draws over equal depths, so
 * coplanar shapes draw in order; it lifts the depth of a fragment by the rank of its instance,
 * so the order holds where coplanar quads interpolate their depths differently.
 *
 * An alpha tested mesh pipeline sets the alpha_cutout constant of mesh.wgsl, which discards the
 * fragments below the cutoff of their instance, except after a depth prepass, where Equal rejects
 * them; the others leave it at its default, so a mesh shader without the constant makes them.
 *
 * @param gpu The backend
 * @param handle Handle of the shader, the mesh or the 2D shape shader
 * @param shader The shader module
 * @param format The format of the color target
 * @param blend The BlendMode, Opaque writes depth and does not blend
 * @param back_face The BackFaceMode, BackFaceCull culls the back faces
 * @param cutout Whether the fragments are alpha tested, ignored by the 2D shape shader
 * @param transmissive Whether the fragments sample the opaque snapshot (the transmissive
 * constant of mesh.wgsl): no blending, no depth writes, compare Greater; ignored by the 2D
 * shape shader
 * @param equal Whether the pipeline draws after a depth prepass: compare Equal, no depth
 * writes, no alpha test; ignored by the 2D shape shader
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_forward_create(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule shader,
    WGPUTextureFormat format,
    uint32_t blend,
    uint32_t back_face,
    bool cutout,
    bool transmissive,
    bool equal)
{
    bool shape2d = handle == BOX_RENDER_SHADER_SHAPE2D;
    transmissive = transmissive && !shape2d;
    cutout = cutout && !shape2d && !equal && !transmissive;
    equal = equal && !transmissive;
    WGPUCompareFunction compare = WGPUCompareFunction_Greater;
    equal = equal && !shape2d;
    if (shape2d) {
        compare = WGPUCompareFunction_GreaterEqual;
    } else if (equal) {
        compare = WGPUCompareFunction_Equal;
    }

    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    WGPUBlendState blend_state;
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
    box_gpu_blend_state(shape2d && blend == Opaque ? AlphaBlend : blend, &blend_state);
    WGPUColorTargetState target = {
        .format = format,
        .blend = (blend == Opaque && !shape2d) || transmissive ? NULL : &blend_state,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUConstantEntry constants[2] = {
        {
            .key = { .data = "blend_mode", .length = WGPU_STRLEN },
            .value = (double)blend
        }
    };
    if (cutout) {
        constants[1] = (WGPUConstantEntry){
            .key = { .data = "alpha_cutout", .length = WGPU_STRLEN },
            .value = 1.0
        };
    } else if (transmissive) {
        constants[1] = (WGPUConstantEntry){
            .key = { .data = "transmissive", .length = WGPU_STRLEN },
            .value = 1.0
        };
    }

    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .constantCount = cutout || transmissive ? 2 : 1,
        .constants = constants,
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = blend == Opaque && !equal && !transmissive
            ? WGPUOptionalBool_True : WGPUOptionalBool_False,
        .depthCompare = compare,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = gpu->mesh_layout,
        .vertex = {
            .module = shader,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN },
            .bufferCount = 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = back_face == BackFaceCull ? WGPUCullMode_Back : WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };

    return wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
}

/** Create a depth prepass pipeline of the mesh shader and a back face mode.
 *
 * It has no color target and writes the depth with compare Greater, as an unlit view does.
 *
 * The plain pipeline has no fragment stage and binds the transform stream of its page only; the
 * alpha tested one binds the shading stream and the texture layers and discards as fs does.
 *
 * @param gpu The backend
 * @param shader The mesh shader module
 * @param back_face The BackFaceMode, BackFaceCull culls the back faces
 * @param cutout Whether the fragments are alpha tested
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_prepass_create(
    box_gpu_t *gpu,
    WGPUShaderModule shader,
    uint32_t back_face,
    bool cutout)
{
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
    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = "fs_depth_cutout", .length = WGPU_STRLEN }
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = cutout ? gpu->prepass_cutout_layout : gpu->prepass_layout,
        .vertex = {
            .module = shader,
            .entryPoint = {
                .data = cutout ? "vs_depth_cutout" : "vs_depth",
                .length = WGPU_STRLEN
            },
            .bufferCount = 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = back_face == BackFaceCull ? WGPUCullMode_Back : WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = cutout ? &fragment : NULL
    };

    return wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
}

#ifndef __EMSCRIPTEN__
/** Keep the error a scope around a pipeline creation caught, as one line: the causes wgpu
 * lists below its "Caused by:" line joined by ": ".
 *
 * @param status Whether the scope was popped
 * @param type Kind of error, NoError when there was none
 * @param message Error text
 * @param userdata1 The char* that receives the error
 * @param userdata2 Unused
 */
static void box_gpu_forward_on_scope(
    WGPUPopErrorScopeStatus status,
    WGPUErrorType type,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    const char *text = message.data ? message.data : "";
    const char *end = text + (message.length == WGPU_STRLEN
        ? ecs_os_strlen(text) : (ecs_size_t)message.length);
    const char *line = text;
    int32_t lines = 0;
    (void)userdata2;
    if (status != WGPUPopErrorScopeStatus_Success || type == WGPUErrorType_NoError) {
        goto done;
    }

    while (line < end) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        const char *stop = next ? next : end;
        while (line < stop && (*line == ' ' || *line == '\t')) {
            line ++;
        }

        bool skip = line == stop || !strncmp(line, "Validation Error", 16) ||
            !strncmp(line, "Caused by:", 10) || !strncmp(line, "In wgpu", 7);
        if (!skip) {
            ecs_strbuf_append(&buf, "%s%.*s", lines ++ ? ": " : "", (int)(stop - line), line);
        }

        line = next ? next + 1 : end;
    }

    if (!lines) {
        ecs_strbuf_appendstr(&buf, "the pipeline cannot be created");
    }

    *(char**)userdata1 = ecs_strbuf_get(&buf);
done:
    ecs_strbuf_reset(&buf);
}
#endif

char* box_gpu_forward_check(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule shader)
{
    char *result = NULL;
#ifndef __EMSCRIPTEN__
    WGPURenderPipeline pipeline, prepass[2] = {NULL, NULL};
    int32_t i;
    if (!gpu->device || !gpu->mesh_layout) {
        goto done;
    }

    wgpuDevicePushErrorScope(gpu->device, WGPUErrorFilter_Validation);
    pipeline = box_gpu_forward_create(gpu, handle, shader, WGPUTextureFormat_RGBA8Unorm, Opaque,
        BackFaceCull, false, false, false);
    if (handle == BOX_RENDER_SHADER_MESH) {
        prepass[0] = box_gpu_prepass_create(gpu, shader, BackFaceCull, false);
        prepass[1] = box_gpu_prepass_create(gpu, shader, BackFaceCull, true);
    }

    wgpuDevicePopErrorScope(gpu->device, (WGPUPopErrorScopeCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_gpu_forward_on_scope,
        .userdata1 = &result
    });
    if (pipeline) {
        wgpuRenderPipelineRelease(pipeline);
    }

    for (i = 0; i < 2; i ++) {
        if (prepass[i]) {
            wgpuRenderPipelineRelease(prepass[i]);
        }
    }

done:
#else
    (void)gpu;
    (void)handle;
    (void)shader;
#endif

    return result;
}

/** Depth state of a pipeline of the forward cache */
typedef enum box_gpu_depth_mode_t {
    BOX_GPU_DEPTH_GREATER,          /**< Forward, compare Greater, opaque draws write depth */
    BOX_GPU_DEPTH_EQUAL,            /**< Forward after a depth prepass, compare Equal */
    BOX_GPU_DEPTH_PREPASS           /**< Depth prepass, no color target */
} box_gpu_depth_mode_t;

/** Pack the state a forward or depth prepass pipeline is created for into its cache key.
 *
 * @param shader Shader handle, below 2^31
 * @param format Format of the color target, below 2^24, Undefined for a depth prepass
 * @param blend BlendMode, below 16
 * @param back_face BackFaceMode, of which the pipeline only tells BackFaceCull from the rest
 * @param cutout Whether the fragments are alpha tested
 * @param transmissive Whether the fragments sample the opaque snapshot (Transmission)
 * @param depth Depth state of the pipeline
 * @return The key, distinct for every distinct pipeline
 */
static uint64_t box_gpu_forward_key(
    uint32_t shader,
    WGPUTextureFormat format,
    uint32_t blend,
    uint32_t back_face,
    bool cutout,
    bool transmissive,
    box_gpu_depth_mode_t depth)
{
    ecs_assert((uint32_t)format < (1u << 24), ECS_INTERNAL_ERROR, NULL);

    return ((uint64_t)shader << 33) | ((uint64_t)transmissive << 32) |
        ((uint64_t)format << 8) | ((uint64_t)blend << 4) | ((uint64_t)depth << 2) |
        (cutout ? 2u : 0u) | (back_face == BackFaceCull ? 0u : 1u);
}

/** Check whether a batch draws in the depth prepass of a lit view: an opaque mesh of the mesh
 * shader, plain or alpha tested. Its forward pipeline in such a view tests with Equal.
 *
 * @param b The batch
 * @return Whether the prepass draws it
 */
static bool box_gpu_prepass_draws(
    const box_render_batch_t *b)
{
    return b->view == -1 && b->pipeline == BOX_RENDER_SHADER_MESH && b->blend == Opaque;
}

WGPURenderPipeline box_gpu_forward_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_batch_t *b,
    bool equal)
{
    WGPURenderPipeline result = NULL;
    WGPUShaderModule shader;
    ecs_map_val_t *cached;
    uint64_t key;
    if (b->blend > Max) {
        goto done;
    }

    shader = box_gpu_shader_get(gpu, b->pipeline);
    if (!shader) {
        goto done;
    }

    equal = equal && box_gpu_prepass_draws(b);
    key = box_gpu_forward_key(b->pipeline, format, b->blend, b->back_face, b->cutout,
        b->transmissive, equal ? BOX_GPU_DEPTH_EQUAL : BOX_GPU_DEPTH_GREATER);
    cached = ecs_map_get(&gpu->forward, key);
    if (cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    result = box_gpu_forward_create(gpu, b->pipeline, shader, format, b->blend, b->back_face,
        b->cutout, b->transmissive, equal);
    ecs_map_insert_ptr(&gpu->forward, key, result);
done:
    return result;
}

/** Get the depth prepass pipeline of a batch the prepass draws, created on first use in the
 * forward cache, so a recompiled or released mesh shader empties it too.
 *
 * @param gpu The backend
 * @param b The batch, box_gpu_prepass_draws
 * @return The pipeline, NULL without a mesh shader
 */
static WGPURenderPipeline box_gpu_prepass_pipeline(
    box_gpu_t *gpu,
    const box_render_batch_t *b)
{
    WGPURenderPipeline result = NULL;
    WGPUShaderModule shader = box_gpu_shader_get(gpu, BOX_RENDER_SHADER_MESH);
    ecs_map_val_t *cached;
    uint64_t key;
    if (!shader) {
        goto done;
    }

    key = box_gpu_forward_key(BOX_RENDER_SHADER_MESH, WGPUTextureFormat_Undefined, Opaque,
        b->back_face, b->cutout, false, BOX_GPU_DEPTH_PREPASS);
    cached = ecs_map_get(&gpu->forward, key);
    if (cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    result = box_gpu_prepass_create(gpu, shader, b->back_face, b->cutout);
    ecs_map_insert_ptr(&gpu->forward, key, result);
done:
    return result;
}

/** Make the view buffer hold the uniforms of a number of views.
 *
 * @param gpu The backend
 * @param count Views of the target
 */
static void box_gpu_view_reserve(
    box_gpu_t *gpu,
    int32_t count)
{
    int32_t capacity = gpu->view_capacity ? gpu->view_capacity : 4;
    if (gpu->view_buffer && count <= gpu->view_capacity) {
        goto done;
    }

    while (capacity < count) {
        capacity *= 2;
    }

    if (gpu->view_buffer) {
        wgpuBufferRelease(gpu->view_buffer);
    }

    gpu->view_capacity = capacity;
    gpu->view_buffer = box_gpu_buffer_new(gpu,
        (uint64_t)capacity * BOX_GPU_VIEW_STRIDE, WGPUBufferUsage_Uniform);
done:
    return;
}

/** Check whether a target renders a view.
 *
 * @param frame The target of the packet
 * @param v The view
 * @return Whether the view is assigned to the target, or, when neither is exclusive, the view
 * is under the view root of the target, or the target has none and no surface claims the view
 */
static bool box_gpu_target_has_view(
    const box_render_target_t *frame,
    const box_render_view_t *v)
{
    bool assigned = v->target || frame->exclusive;

    bool root = frame->view_root ? v->parent == frame->view_root : !v->claimed;

    return assigned ? v->target == frame->handle : root;
}

/** Write the uniforms of the views a target renders into the view buffer.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param frame The target of the packet
 * @return The number of views the target renders
 */
static int32_t box_gpu_view_write(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_target_t *frame)
{
    int32_t i, result = 0, count = ecs_vec_count(&packet->views);
    ecs_vec_set_count_t(NULL, &gpu->view_data, uint8_t, count * BOX_GPU_VIEW_STRIDE);
    box_gpu_cull_begin(gpu, packet);
    for (i = 0; i < count; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, i);
        if (!box_gpu_target_has_view(frame, v)) {
            continue;
        }

        box_gpu_view_uniforms_t *u = (box_gpu_view_uniforms_t*)ecs_vec_get_t(
            &gpu->view_data, uint8_t, result * BOX_GPU_VIEW_STRIDE);
        mat4 proj;
        ecs_os_memset_t(u, 0, box_gpu_view_uniforms_t);
        box_camera_projection(&v->camera_data,
            (float)t->desc.point_width, (float)t->desc.point_height, proj);
        glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), u->view_proj);
        glm_vec3_copy(ECS_CONST_CAST(float*, v->camera_transform[3]), u->camera);
        u->camera[3] = 1;
        u->viewport[0] = (float)t->desc.render_width;
        u->viewport[1] = (float)t->desc.render_height;
        u->viewport[2] = 1.0f / (float)(t->desc.render_width > 0 ? t->desc.render_width : 1);
        u->viewport[3] = 1.0f / (float)(t->desc.render_height > 0 ? t->desc.render_height : 1);
        box_gpu_light_uniforms(gpu, v, proj, u);
        box_gpu_shadow_uniforms(gpu, v, proj, result, u);
        box_gpu_atlas_uniforms(gpu, v, u);
        box_gpu_atmosphere_uniforms(gpu, v, u);
        box_gpu_environment_uniforms(gpu, v, u);
        box_gpu_cookie_uniforms(gpu, v, u);
        box_gpu_cull_add(gpu, t, packet, i, result, proj, u);
        result ++;
    }

    if (result) {
        box_gpu_cull_upload(gpu);
        box_gpu_shadow_upload(gpu, result);
        box_gpu_view_reserve(gpu, result);
        wgpuQueueWriteBuffer(gpu->queue, gpu->view_buffer, 0,
            ecs_vec_first(&gpu->view_data), (size_t)result * BOX_GPU_VIEW_STRIDE);
    }

    return result;
}

/** Record the sky of a view in a pass of a phase that draws the background, then bind the
 * groups of the forward pass again, which the sky pipeline does not share.
 *
 * @param gpu The backend
 * @param t The target
 * @param pass The render pass of the view
 * @param format The format of the color target
 * @param v The view
 * @param group The bind group of the view
 * @param offset Offset of the uniforms of the view in the view buffer
 * @return Whether the sky was drawn, so the pipeline and material bound before are not
 */
static bool box_gpu_draw_sky(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    WGPUBindGroup group,
    uint32_t offset)
{
    bool result = box_gpu_atmosphere_draw_sky(gpu, t, pass, format, v);
    if (result) {
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        wgpuRenderPassEncoderSetBindGroup(pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    }

    return result;
}

/** The render pass of a view the batches record into, which a soft particle pool splits */
typedef struct box_gpu_view_pass_t {
    WGPURenderPassEncoder pass;     /**< The pass being recorded */
    WGPUCommandEncoder encoder;     /**< Encoder of the frame */
    WGPUTextureView color;          /**< Color attachment of the pass */
    WGPUTextureView depth;          /**< Depth attachment, which soft particles read */
    WGPUStoreOp depth_store;        /**< Store op of the depth of the passes after the split */
    bool soft;                      /**< The particles draw in a pass of their own that reads
                                     *   the depth instead of writing it */
    box_gpu_snapshot_t *snapshot;   /**< Snapshot the image is copied into before the first
                                     *   blended draw, NULL without transmissive draws or once
                                     *   copied */
    WGPUTexture source;             /**< Texture of the color attachment, which the snapshot
                                     *   copies */
} box_gpu_view_pass_t;

/** Begin a pass of a view that continues the image and depth of the pass before it, counted
 * among the forward passes.
 *
 * @param gpu The backend
 * @param vp The pass of the view
 * @param read_only Whether the depth is read only, so the pass can sample it
 * @return The pass
 */
static WGPURenderPassEncoder box_gpu_view_pass_continue(
    box_gpu_t *gpu,
    const box_gpu_view_pass_t *vp,
    bool read_only)
{
    WGPURenderPassColorAttachment attachment = {
        .view = vp->color,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = vp->depth,
        .depthLoadOp = WGPULoadOp_Load,
        .depthStoreOp = vp->depth_store
    };
    if (read_only) {
        depth.depthLoadOp = WGPULoadOp_Undefined;
        depth.depthStoreOp = WGPUStoreOp_Undefined;
        depth.depthReadOnly = true;
    }

    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment,
        .depthStencilAttachment = &depth
    };

    box_gpu_count(gpu, BOX_GPU_STAT_FORWARD_PASS);

    return wgpuCommandEncoderBeginRenderPass(vp->encoder, &desc);
}

/** Record the particles of the scene of a view, then bind the groups of the forward pass again,
 * which the particle pipeline does not share.
 *
 * A view with a soft pool ends its pass, draws the particles in a pass that samples the depth it
 * reads and, unless they are the last draws of the view, continues in a new pass.
 *
 * @param gpu The backend
 * @param vp The pass of the view, its pass replaced by a split
 * @param format The format of the color target
 * @param v The view
 * @param group The bind group of the view
 * @param offset Offset of the uniforms of the view in the view buffer
 * @param last Whether nothing draws after the particles
 * @return Whether the pipeline and material bound before are lost
 */
static bool box_gpu_draw_particles(
    box_gpu_t *gpu,
    box_gpu_view_pass_t *vp,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    WGPUBindGroup group,
    uint32_t offset,
    bool last)
{
    bool result = true, rebind = true;
    if (!vp->soft) {
        result = box_gpu_particles_draw(gpu, vp->pass, format, v, NULL);
        rebind = result;
    } else {
        wgpuRenderPassEncoderEnd(vp->pass);
        wgpuRenderPassEncoderRelease(vp->pass);
        vp->pass = box_gpu_view_pass_continue(gpu, vp, true);
        wgpuRenderPassEncoderSetBindGroup(vp->pass, 0, group, 1, &offset);
        box_gpu_particles_draw(gpu, vp->pass, format, v, vp->depth);
        rebind = !last;
        if (!last) {
            wgpuRenderPassEncoderEnd(vp->pass);
            wgpuRenderPassEncoderRelease(vp->pass);
            vp->pass = box_gpu_view_pass_continue(gpu, vp, false);
        }
    }

    if (rebind) {
        wgpuRenderPassEncoderSetBindGroup(vp->pass, 0, group, 1, &offset);
        wgpuRenderPassEncoderSetBindGroup(vp->pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    }

    return result;
}

/** End the pass of a view before its first blended draw, copy its image into the opaque
 * snapshot and blur its pyramid (box_gpu_transmission_record), then continue in a new pass with
 * the groups of the forward pass bound again.
 *
 * @param gpu The backend
 * @param vp The pass of the view, its pass replaced
 * @param v The view
 * @param group The bind group of the view
 * @param offset Offset of the uniforms of the view in the view buffer
 */
static void box_gpu_draw_snapshot(
    box_gpu_t *gpu,
    box_gpu_view_pass_t *vp,
    const box_render_view_t *v,
    WGPUBindGroup group,
    uint32_t offset)
{
    wgpuRenderPassEncoderEnd(vp->pass);
    wgpuRenderPassEncoderRelease(vp->pass);
    box_gpu_transmission_record(gpu, vp->snapshot, vp->encoder, vp->source);
    vp->snapshot = NULL;
    vp->pass = box_gpu_view_pass_continue(gpu, vp, false);
    wgpuRenderPassEncoderSetBindGroup(vp->pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderSetBindGroup(vp->pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
}

/** Record the indirect draws of the batches a view renders in a pass, in packet order.
 *
 * The sky of a view that renders its atmosphere, then the particles of its scene, draw after
 * the opaque 3D meshes and before the 2D shapes and the blended draws, in the phases that draw
 * those.
 *
 * After a depth prepass the opaque meshes draw with the Equal pipelines, both the early and the
 * late arguments of a view that culls by occlusion, so they draw what the prepasses drew.
 *
 * @param gpu The backend
 * @param t The target
 * @param vp The render pass of the view, which the particles may split, and its opaque snapshot,
 * which splits it right before its first blended draw (box_gpu_draw_snapshot)
 * @param format The format of the color target
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 * @param phase 0 for every draw of a view without occlusion, 1 for the opaque meshes of the
 * early phase, 2 for what the late phase kept, the 2D shapes and the blended draws, 3 for every
 * draw of a view after its depth prepasses
 */
static void box_gpu_draw_batches(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    box_gpu_view_pass_t *vp,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot,
    int32_t phase)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup material = NULL, instances = NULL;
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    bool sky = phase == 1 || !box_gpu_atmosphere_sky(gpu, v);
    bool particles = phase == 1;
    bool late = phase == 3 && box_gpu_cull_occludes(gpu, slot);
    int32_t i;
    for (i = 0; cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        WGPURenderPipeline pipeline = box_gpu_forward_pipeline(gpu, format, b, phase == 3);
        bool mesh = b->view == -1 && b->pipeline == BOX_RENDER_SHADER_MESH;
        if (!sky && !mesh) {
            sky = true;
            if (box_gpu_draw_sky(gpu, t, vp->pass, format, v, group, offset)) {
                bound = NULL;
                material = NULL;
                instances = NULL;
            }
        }

        if (!particles && !mesh) {
            particles = true;
            if (box_gpu_draw_particles(gpu, vp, format, v, group, offset, false)) {
                bound = NULL;
                material = NULL;
                instances = NULL;
            }
        }

        if (vp->snapshot && phase != 1 && b->view != -1 &&
            b->pipeline != BOX_RENDER_SHADER_SHAPE2D)
        {
            box_gpu_draw_snapshot(gpu, vp, v, group, offset);
            bound = NULL;
            material = NULL;
            instances = NULL;
        }

        if (!pipeline || (phase == 1 && !mesh)) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(vp->pass, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup group = box_gpu_material_group(gpu, b, packet->anisotropy);
        if (group != material) {
            wgpuRenderPassEncoderSetBindGroup(vp->pass, 1, group, 0, NULL);
            material = group;
        }

        if (box_gpu_cull_draw(gpu, vp->pass, packet, d, 2, phase == 2 && mesh, true,
            &instances))
        {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }

        if (late && mesh && box_gpu_cull_draw(gpu, vp->pass, packet, d, 2, true, true,
            &instances))
        {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }
    }

    if (!sky) {
        box_gpu_draw_sky(gpu, t, vp->pass, format, v, group, offset);
    }

    if (!particles) {
        box_gpu_draw_particles(gpu, vp, format, v, group, offset, true);
    }
}

/** Record a render pass of a view and its draws of one phase.
 *
 * Counts the pass among the forward passes, and each pass the opaque snapshot and soft
 * particles split it into (box_gpu_view_pass_continue).
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param color The texture view the view renders into
 * @param hdr The HDR image of a view with effects, NULL to render into color
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 * @param phase 0 for the only pass, 1 for the early and 2 for the late pass of a view that
 * culls by occlusion, 3 for the forward pass after the depth prepasses, which clears the color
 * and loads their depth
 * @param snapshot Opaque snapshot of a view with transmissive draws, which every phase but 1
 * copies before its first blended draw, NULL for none
 * @param source Texture of the color attachment, the HDR image of a view with effects
 */
static void box_gpu_draw_pass(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    WGPUTextureView color,
    WGPUTextureView hdr,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot,
    int32_t phase,
    box_gpu_snapshot_t *snapshot,
    WGPUTexture source)
{
    bool clear = (v->clear.enabled || hdr) && phase != 2;
    WGPURenderPassColorAttachment attachment = {
        .view = hdr ? hdr : color,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = {
            box_gpu_linear(v->clear.color.r),
            box_gpu_linear(v->clear.color.g),
            box_gpu_linear(v->clear.color.b),
            (double)v->clear.color.a / 255.0
        }
    };
    if (hdr && !v->clear.enabled) {
        attachment.clearValue = (WGPUColor){0};
    }

    box_gpu_view_pass_t vp = {
        .encoder = encoder,
        .color = hdr ? hdr : color,
        .depth = t->depth_view,
        .depth_store = phase == 1 || hdr ? WGPUStoreOp_Store : WGPUStoreOp_Discard,
        .soft = phase != 1 && box_gpu_particles_soft(gpu, v),
        .snapshot = phase != 1 ? snapshot : NULL,
        .source = source
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = t->depth_view,
        .depthLoadOp = phase < 2 ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .depthStoreOp = vp.soft || vp.snapshot ? WGPUStoreOp_Store : vp.depth_store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment,
        .depthStencilAttachment = &depth
    };
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    WGPUTextureFormat format = hdr ? BOX_GPU_HDR_FORMAT : t->format;
    vp.pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    box_gpu_count(gpu, BOX_GPU_STAT_FORWARD_PASS);
    if (phase != 2) {
        box_gpu_environment_draw_background(gpu, vp.pass, format, v, offset);
    }

    wgpuRenderPassEncoderSetBindGroup(vp.pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderSetBindGroup(vp.pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    box_gpu_draw_batches(gpu, t, &vp, format, packet, v, group, slot, phase);
    wgpuRenderPassEncoderEnd(vp.pass);
    wgpuRenderPassEncoderRelease(vp.pass);
}

/** Check whether a view draws a depth prepass: it is lit, its camera has a depth range and it
 * draws an opaque or alpha tested mesh with the mesh shader.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param v The view
 * @param slot Slot of the view in the view buffer
 * @return Whether the view draws its opaque meshes depth only before its forward pass
 */
static bool box_gpu_draw_prepasses(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t slot)
{
    const box_gpu_cull_view_t *cv = NULL;
    bool result = false;
    int32_t i;
    if (!v->lights.enabled || !box_camera_has_depth(&v->camera_data) ||
        !box_gpu_shader_get(gpu, BOX_RENDER_SHADER_MESH))
    {
        goto done;
    }

    cv = box_gpu_cull_view(gpu, slot, -1);
    for (i = 0; cv && i < cv->draw_count && !result; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        result = box_gpu_prepass_draws(ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch));
    }

done:
    return result;
}

/** Record a depth prepass of a view: its opaque and alpha tested mesh draws, depth only, from
 * the visible slots and indirect arguments of its cull view that its forward pass draws too.
 *
 * Plain meshes bind the empty group 1 and the transform stream of their page; alpha tested
 * ones the material group of their batch and the instance group with the shading stream.
 *
 * @param gpu The backend
 * @param t The target
 * @param encoder The encoder of the frame
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 * @param late Whether it loads the depth of the early prepass and draws the late arguments of
 * a view that culls by occlusion, else it clears the depth and draws the early arguments, the
 * only ones of a view without occlusion
 */
static void box_gpu_draw_prepass(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot,
    bool late)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup material = NULL, instances = NULL;
    WGPURenderPassDepthStencilAttachment depth = {
        .view = t->depth_view,
        .depthLoadOp = late ? WGPULoadOp_Load : WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor desc = { .depthStencilAttachment = &depth };
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    int32_t i;
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    for (i = 0; cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        if (!box_gpu_prepass_draws(b)) {
            continue;
        }

        WGPURenderPipeline pipeline = box_gpu_prepass_pipeline(gpu, b);
        if (!pipeline) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup layers = b->cutout
            ? box_gpu_material_group(gpu, b, packet->anisotropy) : gpu->prepass_empty;
        if (layers != material) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, layers, 0, NULL);
            material = layers;
        }

        if (box_gpu_cull_draw(gpu, pass, packet, d, 2, late, b->cutout, &instances)) {
            box_gpu_count(gpu, BOX_GPU_STAT_PREPASS_DRAW);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    box_gpu_count(gpu, BOX_GPU_STAT_PREPASS);
}

void box_gpu_draw_views(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_target_t *frame,
    WGPUCommandEncoder encoder,
    WGPUTextureView color,
    WGPUTexture texture)
{
    int32_t i, slot = 0, count = ecs_vec_count(&packet->views);
    if (!box_gpu_view_write(gpu, t, packet, frame)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, i);
        if (!box_gpu_target_has_view(frame, v)) {
            continue;
        }

        WGPUTextureView hdr = box_gpu_effects_begin(gpu, t, packet, v);
        WGPUTexture source = hdr ? box_gpu_effects_image(gpu) : texture;
        bool occludes = box_gpu_cull_occludes(gpu, slot);
        box_gpu_atmosphere_view(gpu, t, encoder, packet, v);
        box_gpu_light_cluster(gpu, t, encoder, v, slot);
        box_gpu_cull_record(gpu, encoder, slot);
        box_gpu_shadow_render(gpu, encoder, packet, i, slot);
        box_gpu_particles_sort(gpu, encoder, v, slot);
        bool prepasses = box_gpu_draw_prepasses(gpu, packet, v, slot);
        WGPUTextureView ao = prepasses ? box_gpu_gtao_begin(gpu, t, packet, v) : NULL;
        box_gpu_snapshot_t *snapshot = box_gpu_transmission_begin(gpu, t, packet, slot, source,
            hdr ? BOX_GPU_HDR_FORMAT : t->format);
        WGPUBindGroup group = box_gpu_light_group(gpu, t, v, slot, ao,
            box_gpu_transmission_view(gpu, snapshot));
        if (prepasses) {
            box_gpu_draw_prepass(gpu, t, encoder, packet, v, group, slot, false);
            if (occludes) {
                box_gpu_cull_record_late(gpu, encoder, t->depth_view, slot);
                box_gpu_draw_prepass(gpu, t, encoder, packet, v, group, slot, true);
            }

            if (ao) {
                box_gpu_gtao_record(gpu, t, encoder, v);
            }

            box_gpu_draw_pass(gpu, t, encoder, color, hdr, packet, v, group, slot, 3,
                snapshot, source);
        } else {
            box_gpu_draw_pass(gpu, t, encoder, color, hdr, packet, v, group, slot,
                occludes ? 1 : 0, snapshot, source);
            if (occludes) {
                box_gpu_cull_record_late(gpu, encoder, t->depth_view, slot);
                box_gpu_draw_pass(gpu, t, encoder, color, hdr, packet, v, group, slot, 2,
                    snapshot, source);
            }
        }

        wgpuBindGroupRelease(group);

        if (hdr) {
            box_gpu_effects_run(gpu, t, encoder, packet, v, color);
        }

        box_gpu_count(gpu, BOX_GPU_STAT_PASS);
        slot ++;
    }

    box_gpu_cull_finish(gpu, encoder);
done:
    return;
}

#endif
