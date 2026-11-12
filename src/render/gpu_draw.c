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

#include "gpu_forward.h"
#include <math.h>
#include <stdlib.h>
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

/** Create a pipeline layout of the forward pass: the view, a material group, the instance
 * streams and the probe volume.
 *
 * @param gpu The backend
 * @param material Layout of group 1, material_layout or the blend_layout of the texture_blend
 * variant
 * @return The pipeline layout
 */
static WGPUPipelineLayout box_gpu_mesh_layout(
    box_gpu_t *gpu,
    WGPUBindGroupLayout material)
{
    WGPUBindGroupLayout groups[4] = {
        gpu->view_layout, material, gpu->instance_layout, gpu->volume_layout
    };

    return wgpuDeviceCreatePipelineLayout(gpu->device, &(WGPUPipelineLayoutDescriptor){
        .bindGroupLayoutCount = 4,
        .bindGroupLayouts = groups
    });
}

/** Create the layout of the view group of the forward pass: the view uniforms, the lights and
 * clusters, the shadows, the reflections, the aerial perspective, the environment, the
 * ambient occlusion, the cookie, the opaque snapshot, the planar images and the planet lights.
 *
 * @param gpu The backend
 * @return The layout
 */
static WGPUBindGroupLayout box_gpu_view_layout(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[24] = {
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
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
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
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
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
        },
        {
            .binding = 20,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 21,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        },
        {
            .binding = BOX_GPU_PLANET_LIGHTS_BINDING,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_GPU_PLANET_LIGHTS_SIZE
            }
        },
        {
            .binding = BOX_GPU_PLANET_LUTS_BINDING,
            .visibility = WGPUShaderStage_Fragment,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_ReadOnly,
                .format = WGPUTextureFormat_RGBA16Float,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 24, .entries = entries };

    return wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
}

void box_gpu_draw_init(
    box_gpu_t *gpu)
{
    box_gpu_cull_init(gpu);
    box_gpu_volume_init(gpu);
    box_gpu_reflect_init(gpu);
    box_gpu_environment_init(gpu);
    gpu->view_layout = box_gpu_view_layout(gpu);
    gpu->mesh_layout = box_gpu_mesh_layout(gpu, gpu->material_layout);
    gpu->blend_mesh_layout = box_gpu_mesh_layout(gpu, gpu->blend_layout);
    gpu->prepass_empty_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 0 });
    gpu->prepass_empty = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->prepass_empty_layout,
        .entryCount = 0
    });
    ecs_map_init(&gpu->meshes, NULL);
    ecs_vec_init_t(NULL, &gpu->mesh_pages, box_gpu_mesh_page_t, 0);
    ecs_map_init(&gpu->buffers, NULL);
    ecs_map_init(&gpu->forward, NULL);
    ecs_map_init(&gpu->forward_previous, NULL);
    ecs_vec_init_t(NULL, &gpu->view_data, uint8_t, 0);
    box_gpu_copy_init(gpu);
    box_gpu_light_init(gpu);
    box_gpu_shadow_init(gpu);
    box_gpu_effects_init(gpu);
    box_gpu_gtao_init(gpu);
    box_gpu_transmission_init(gpu);
    box_gpu_atmosphere_init(gpu);
    box_gpu_particles_init(gpu);
    box_gpu_fog_volumes_init(gpu);
    box_gpu_planets_init(gpu);
    box_gpu_debug_init(gpu);
    box_gpu_planar_init(gpu);
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
    box_gpu_debug_mesh_free(m);
    wgpuBufferRelease(m->surface_assets);
    if (m->buffer) {
        box_gpu_mesh_pool_free(gpu, m->page, m->offset, m->alloc_size);
    }

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
        .attenuation_distance = BOX_MATERIAL_ATTENUATION_DISTANCE,
        .diffuse_transmission = 0u,
        .thickness = 0.0f
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
 * A page without a surface stream, a plain mesh page or a 2D page, binds gpu->no_surfaces at
 * binding 3: one zero record, so the vertex shaders, which load the record of a slot only inside
 * the buffer, leave the surface of a plain mesh zero.
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
    bool surface = page >= BOX_RENDER_BUFFER_FIRST_SURFACE && page < BOX_RENDER_BUFFER_FIRST_2D;
    int32_t i, count = surface ? BOX_RENDER_INSTANCE_STREAMS : BOX_RENDER_INSTANCE_STREAMS_PLAIN;
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

/** Release the forward pipelines of a map and empty it.
 *
 * @param map The pipelines
 */
static void box_gpu_forward_release(
    ecs_map_t *map)
{
    ecs_map_iter_t it = ecs_map_iter(map);
    while (ecs_map_next(&it)) {
        WGPURenderPipeline pipeline = ecs_map_ptr(&it);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

    ecs_map_clear(map);
}

void box_gpu_forward_reset(
    box_gpu_t *gpu,
    bool keep)
{
    ecs_map_iter_t it;
    if (!ecs_map_is_init(&gpu->forward)) {
        goto done;
    }

    if (!keep) {
        box_gpu_forward_release(&gpu->forward_previous);
    }

    it = ecs_map_iter(&gpu->forward);
    while (keep && ecs_map_next(&it)) {
        ecs_map_val_t *previous = ecs_map_ensure(&gpu->forward_previous, ecs_map_key(&it));
        if (*previous) {
            wgpuRenderPipelineRelease((WGPURenderPipeline)(uintptr_t)*previous);
        }

        *previous = ecs_map_value(&it);
    }

    if (keep) {
        ecs_map_clear(&gpu->forward);
    } else {
        box_gpu_forward_release(&gpu->forward);
    }

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
    box_gpu_mesh_pool_fini(gpu);
    ecs_map_fini(&gpu->buffers);
    box_gpu_copy_fini(gpu);
    ecs_vec_fini_t(NULL, &gpu->view_data, uint8_t);
    box_gpu_forward_reset(gpu, false);
    ecs_map_fini(&gpu->forward);
    ecs_map_fini(&gpu->forward_previous);
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
    box_gpu_fog_volumes_fini(gpu);
    box_gpu_planets_fini(gpu);
    box_gpu_debug_fini(gpu);
    box_gpu_planar_fini(gpu);
    if (gpu->view_buffer) {
        wgpuBufferRelease(gpu->view_buffer);
        gpu->view_buffer = NULL;
    }

    if (gpu->mesh_layout) {
        wgpuPipelineLayoutRelease(gpu->mesh_layout);
        gpu->mesh_layout = NULL;
    }

    if (gpu->blend_mesh_layout) {
        wgpuPipelineLayoutRelease(gpu->blend_mesh_layout);
        gpu->blend_mesh_layout = NULL;
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
    uint64_t vertex_span = (vertex_size + BOX_GPU_MESH_ALIGN - 1) &
        ~(uint64_t)(BOX_GPU_MESH_ALIGN - 1);
    uint64_t alloc_size = vertex_span + index_size;
    if (alloc_size > limits.maxBufferSize) {
        ecs_err("surface mesh exceeds device buffer capacity");
        goto done;
    }

    m = ecs_os_calloc_t(box_gpu_mesh_t);
    if (!box_gpu_mesh_pool_alloc(gpu, alloc_size, &m->page, &m->offset)) {
        ecs_err("failed to allocate mesh buffer space");
        ecs_os_free(m);
        goto done;
    }

    m->buffer = box_gpu_mesh_pool_buffer(gpu, m->page);
    m->alloc_size = alloc_size;
    m->vertex_size = vertex_size;
    m->index_offset = m->offset + vertex_span;
    m->index_size = index_size;
    m->id = ++ gpu->mesh_ids;
    m->wireable = vertex_size <= limits.maxStorageBufferBindingSize &&
        index_size <= limits.maxStorageBufferBindingSize;
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
    wgpuQueueWriteBuffer(gpu->queue, m->buffer, m->offset, vertices, (size_t)vertex_size);
    wgpuQueueWriteBuffer(gpu->queue, m->buffer, m->index_offset, vertices + vertex_size,
        (size_t)index_size);
    box_gpu_debug_mesh_adjacency(gpu, m, (const box_vertex_t*)vertices, desc->vertex_count,
        (const uint32_t*)(vertices + vertex_size));
    ecs_map_insert_ptr(&gpu->meshes, handle, m);
    box_gpu_count(gpu, BOX_GPU_STAT_MESH_CREATED);
done:
    return;
}

bool box_gpu_buffer_copyable(
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

/** Write the uniforms of the views a target renders into the view buffer, then those of their
 * planar reflections after them (box_gpu_planar_add).
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
    int32_t i, result = 0, extra = 0, slot = 0, count = ecs_vec_count(&packet->views);
    ecs_vec_set_count_t(NULL, &gpu->view_data, uint8_t, count * BOX_GPU_VIEW_STRIDE);
    box_gpu_cull_begin(gpu, packet);
    box_gpu_planar_begin(gpu);
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
        glm_mat4_inv(u->view_proj, u->inv_view_proj);
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

    for (i = 0; i < count && result; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, i);
        if (!box_gpu_target_has_view(frame, v)) {
            continue;
        }

        extra += box_gpu_planar_add(gpu, t, packet, i, slot,
            (const box_gpu_view_uniforms_t*)ecs_vec_get_t(
                &gpu->view_data, uint8_t, slot * BOX_GPU_VIEW_STRIDE),
            result + extra);
        slot ++;
    }

    if (extra) {
        ecs_vec_set_count_t(NULL, &gpu->view_data, uint8_t,
            (result + extra) * BOX_GPU_VIEW_STRIDE);
        box_gpu_planar_uniforms(gpu, ecs_vec_first(&gpu->view_data));
    }

    if (result) {
        box_gpu_cull_upload(gpu);
        box_gpu_shadow_upload(gpu, result);
        box_gpu_view_reserve(gpu, result + extra);
        wgpuQueueWriteBuffer(gpu->queue, gpu->view_buffer, 0,
            ecs_vec_first(&gpu->view_data), (size_t)(result + extra) * BOX_GPU_VIEW_STRIDE);
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
    bool fog;                       /**< The fog volumes draw in a pass of their own that reads
                                     *   the depth instead of writing it */
    bool planets;                   /**< The planet atmosphere shells draw in a pass of their own
                                     *   that reads the depth instead of writing it */
    box_gpu_snapshot_t *snapshot;   /**< Snapshot the image is copied into before transmissive
                                     *   draws (box_gpu_draw_snapshot), NULL without them */
    int32_t snapshots;              /**< Snapshots the pass took, at most
                                     *   BOX_GPU_TRANSMISSION_SNAPSHOTS */
    bool drawn;                     /**< A draw that is not transmissive was recorded since the
                                     *   last snapshot */
    WGPUTexture source;             /**< Texture of the color attachment, which the snapshot
                                     *   copies */
    WGPUTexture depth_texture;      /**< Texture of the depth attachment, which the snapshot
                                     *   copies next to its color */
    int32_t front;                  /**< Index of the draw after the last transmissive draw of
                                     *   a view with fog volumes, before which the volumes march
                                     *   the rays in front of the glass, -1 for none */
    WGPUTextureView glass;          /**< Depth of the nearest transmissive surfaces over the
                                     *   opaque depth, NULL until box_gpu_draw_glass */
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
        .depthStoreOp = vp->snapshot && vp->snapshots < BOX_GPU_TRANSMISSION_SNAPSHOTS
            ? WGPUStoreOp_Store
            : vp->depth_store
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

/** Record the transmissive draws of a view depth only into the glass depth of its snapshot, a
 * copy of its opaque depth (box_gpu_transmission_glass), so it holds the nearest transmissive
 * surface in front of the opaque depth. Called between two passes, before the fog volumes.
 *
 * The transmissive draws use the depth prepass pipelines of their batches (vs_depth and
 * fs_depth), which write the depth with compare Greater.
 *
 * @param gpu The backend
 * @param vp The pass of the view, whose glass it sets
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 */
static void box_gpu_draw_glass(
    box_gpu_t *gpu,
    box_gpu_view_pass_t *vp,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup material = NULL, instances = NULL;
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    int32_t i;
    vp->glass = box_gpu_transmission_glass(gpu, vp->snapshot, vp->encoder, vp->depth_texture);
    WGPURenderPassDepthStencilAttachment depth = {
        .view = vp->glass,
        .depthLoadOp = WGPULoadOp_Load,
        .depthStoreOp = WGPUStoreOp_Store
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(vp->encoder,
        &(WGPURenderPassDescriptor){ .depthStencilAttachment = &depth });
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    for (i = 0; cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        if (!b->transmissive) {
            continue;
        }

        WGPURenderPipeline pipeline = box_gpu_prepass_pipeline(gpu, b,
            WGPUTextureFormat_Undefined);
        if (!pipeline) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup layers = box_gpu_material_group(gpu, packet, b);
        if (layers != material) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, layers, 1, &(uint32_t){0});
            material = layers;
        }

        if (box_gpu_cull_draw(gpu, pass, packet, d, 2, false, true, &instances)) {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Record the planet atmosphere shells of a view, then its fog volumes, then the particles of
 * its scene, then bind the groups of the forward pass again, which their pipelines do not share.
 *
 * A view with planet shells, fog volumes or a soft pool ends its pass, draws them in a pass that
 * samples the depth it reads and, unless they are the last draws of the view, continues in a new
 * pass. The shells come before the blended draws, so those in front of a planet are not hazed.
 *
 * The fog volumes of a view with transmissive draws march only the rays behind the glass
 * (box_gpu_draw_glass), which its snapshot then holds.
 *
 * @param gpu The backend
 * @param t The target
 * @param vp The pass of the view, its pass replaced by a split
 * @param format The format of the color target
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 * @param last Whether nothing draws after the particles
 * @return Whether the pipeline and material bound before are lost
 */
static bool box_gpu_draw_particles(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    box_gpu_view_pass_t *vp,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot,
    bool last)
{
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    bool result = true, rebind = true;
    if (!vp->soft && !vp->fog && !vp->planets) {
        result = box_gpu_particles_draw(gpu, vp->pass, format, v, NULL);
        rebind = result;
    } else {
        wgpuRenderPassEncoderEnd(vp->pass);
        wgpuRenderPassEncoderRelease(vp->pass);
        if (vp->fog && vp->front >= 0) {
            box_gpu_draw_glass(gpu, vp, packet, v, group, slot);
        }

        vp->pass = box_gpu_view_pass_continue(gpu, vp, true);
        wgpuRenderPassEncoderSetBindGroup(vp->pass, 0, group, 1, &offset);
        if (vp->planets) {
            box_gpu_planets_draw(gpu, vp->pass, t, format, packet, v, vp->depth);
        }

        if (vp->fog) {
            box_gpu_fog_volumes_draw(gpu, vp->pass, t, format, packet, v, vp->depth,
                vp->glass, BOX_GPU_FOG_SEGMENT_BEHIND);
        }

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

/** End the pass of a view after its last transmissive draw, march its fog volumes from the
 * camera to the nearest transmissive surface of each pixel that has one in a pass that reads the
 * depth, then continue in a new pass with the groups of the forward pass bound again.
 *
 * Together with the part behind the glass, which the snapshot holds, every ray is marched once,
 * so the glass is fogged as an opaque surface at its depth. Blended shapes drawn in front of the
 * glass before its last transmissive draw are fogged as the glass behind them.
 *
 * @param gpu The backend
 * @param t The target
 * @param vp The pass of the view, its pass replaced
 * @param format The format of the color target
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param offset Offset of the uniforms of the view in the view buffer
 */
static void box_gpu_draw_fog_front(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    box_gpu_view_pass_t *vp,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    uint32_t offset)
{
    wgpuRenderPassEncoderEnd(vp->pass);
    wgpuRenderPassEncoderRelease(vp->pass);
    vp->pass = box_gpu_view_pass_continue(gpu, vp, true);
    wgpuRenderPassEncoderSetBindGroup(vp->pass, 0, group, 1, &offset);
    box_gpu_fog_volumes_draw(gpu, vp->pass, t, format, packet, v, vp->depth, vp->glass,
        BOX_GPU_FOG_SEGMENT_FRONT);
    wgpuRenderPassEncoderEnd(vp->pass);
    wgpuRenderPassEncoderRelease(vp->pass);
    vp->pass = box_gpu_view_pass_continue(gpu, vp, false);
    wgpuRenderPassEncoderSetBindGroup(vp->pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderSetBindGroup(vp->pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
}

/** End the pass of a view before a transmissive draw, copy its image into the snapshot and
 * blur its pyramid (box_gpu_transmission_record), then continue in a new pass with the groups
 * of the forward pass bound again.
 *
 * It holds everything the view drew before back to front, 2D shapes, particles, fog volumes and
 * the blended meshes behind the glass. Each snapshot of the frame overwrites the same texture;
 * only the first copies the depth, which no blended or transmissive draw writes.
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
    box_gpu_transmission_record(gpu, vp->snapshot, vp->encoder, vp->source,
        vp->snapshots ? NULL : vp->depth_texture);
    vp->snapshots ++;
    vp->drawn = false;
    vp->pass = box_gpu_view_pass_continue(gpu, vp, false);
    wgpuRenderPassEncoderSetBindGroup(vp->pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderSetBindGroup(vp->pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
}

/** Record the indirect draws of the batches a view renders in a pass, in packet order.
 *
 * The sky of a view that renders its atmosphere, then its planet atmosphere shells, then its fog
 * volumes, then the particles of its scene, draw after the 3D meshes shared by every view and
 * before the 2D shapes and the blended draws, in the phases that draw those.
 *
 * After a depth prepass the opaque meshes draw once with the Equal pipelines and the early
 * arguments, which the late phase of occlusion appends to: what both prepasses drew. A
 * view with WireframeOnly draws none of its mesh batches: its debug pass wires them instead.
 *
 * @param gpu The backend
 * @param t The target
 * @param vp The render pass of the view, which the fog volumes and particles may split, and its
 * snapshot, taken right before the first transmissive draw and again before a later one that
 * follows other draws, BOX_GPU_TRANSMISSION_SNAPSHOTS at most (box_gpu_draw_snapshot). A view
 * with fog volumes and transmissive draws marches the rays behind the glass before the snapshot
 * and those in front of it after the last transmissive draw (box_gpu_draw_fog_front)
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
    uint32_t material_kind = 0;
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    bool sky = phase == 1 || !box_gpu_atmosphere_sky(gpu, v);
    bool particles = phase == 1;
    bool wired = v->debug.wireframe == WireframeOnly;
    int32_t i;
    vp->front = -1;
    vp->glass = NULL;
    for (i = 0; vp->fog && vp->snapshot && cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        if (ecs_vec_get_t(&packet->batches, box_render_batch_t, d->batch)->transmissive) {
            vp->front = i + 1;
        }
    }

    for (i = 0; cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        if (i == vp->front && vp->glass) {
            box_gpu_draw_fog_front(gpu, t, vp, format, packet, v, group, offset);
            bound = NULL;
            material = NULL;
            instances = NULL;
        }

        WGPURenderPipeline pipeline = box_gpu_forward_pipeline(gpu, format, b, phase == 3);
        bool mesh = b->view == -1 && box_render_is_mesh(b->pipeline);
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
            if (box_gpu_draw_particles(gpu, t, vp, format, packet, v, group, slot, false)) {
                bound = NULL;
                material = NULL;
                instances = NULL;
            }
        }

        if (vp->snapshot && phase != 1 && b->transmissive &&
            (!vp->snapshots ||
                (vp->drawn && vp->snapshots < BOX_GPU_TRANSMISSION_SNAPSHOTS)))
        {
            box_gpu_draw_snapshot(gpu, vp, v, group, offset);
            bound = NULL;
            material = NULL;
            instances = NULL;
        }

        if (!pipeline || (phase == 1 && !mesh) || (wired && box_render_is_mesh(b->pipeline))) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(vp->pass, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup group = box_gpu_material_group(gpu, packet, b);
        uint32_t kind = box_gpu_draw_kind(b, phase == 3);
        if (group != material || kind != material_kind) {
            wgpuRenderPassEncoderSetBindGroup(vp->pass, 1, group, 1, &kind);
            material = group;
            material_kind = kind;
        }

        if (box_gpu_cull_draw(gpu, vp->pass, packet, d, 2, phase == 2 && mesh, true,
            &instances))
        {
            box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
            vp->drawn |= !b->transmissive;
        }
    }

    if (cv && vp->front == cv->draw_count && vp->glass) {
        box_gpu_draw_fog_front(gpu, t, vp, format, packet, v, group, offset);
    }

    if (!sky) {
        box_gpu_draw_sky(gpu, t, vp->pass, format, v, group, offset);
    }

    if (!particles) {
        box_gpu_draw_particles(gpu, t, vp, format, packet, v, group, slot, true);
    }
}

/** Record a render pass of a view and its draws of one phase.
 *
 * Counts the pass among the forward passes, and each pass the snapshots of transmission, the
 * fog volumes and soft particles split it into (box_gpu_view_pass_continue).
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
 * @param snapshot Snapshot of a view with transmissive draws, which every phase but 1 copies
 * before its transmissive draws (box_gpu_draw_batches), NULL for none
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

    bool fog = phase != 1 && box_gpu_fog_volumes_ready(gpu, v);
    bool planets = phase != 1 && box_gpu_planets_ready(gpu, v);
    box_gpu_view_pass_t vp = {
        .encoder = encoder,
        .color = hdr ? hdr : color,
        .depth = t->depth_view,
        .depth_store = phase == 1 || hdr || fog || planets || box_gpu_debug_enabled(v)
            ? WGPUStoreOp_Store
            : WGPUStoreOp_Discard,
        .soft = phase != 1 && box_gpu_particles_soft(gpu, v),
        .fog = fog,
        .planets = planets,
        .snapshot = phase != 1 ? snapshot : NULL,
        .source = source,
        .depth_texture = t->depth
    };
    WGPURenderPassDepthStencilAttachment depth = {
        .view = t->depth_view,
        .depthLoadOp = phase < 2 ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .depthStoreOp = vp.soft || vp.snapshot || vp.fog || vp.planets
            ? WGPUStoreOp_Store
            : vp.depth_store,
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

/** Check whether a view draws a depth prepass: it is lit with LightSettings.depth_prepass, not
 * WireframeOnly, its camera has a depth range and it draws an opaque or alpha tested mesh with a
 * mesh shader.
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
    if (!v->lights.enabled || !(v->features & BOX_RENDER_FEATURE_DEPTH_PREPASS) ||
        !box_camera_has_depth(&v->camera_data) || v->debug.wireframe == WireframeOnly)
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

/** Record the draws of a depth prepass of a view into a pass: its opaque and alpha tested mesh
 * draws from the visible slots and indirect arguments of its cull view that its forward pass
 * draws too.
 *
 * Every mesh binds the groups its forward draw binds: the material group of its batch, the
 * instance group with the shading stream and the volume group of the view, which vs reads.
 *
 * @param gpu The backend
 * @param pass The pass
 * @param packet The packet
 * @param v The view
 * @param group The bind group of the view
 * @param slot Slot of the view in the view buffer
 * @param late Whether it draws the late arguments of a view that culls by occlusion, else the
 * early arguments, the only ones of a view without occlusion
 * @param count Format of the overdraw count target of the pass, Undefined for a depth only pass
 * @param stat Statistic each recorded draw counts
 */
static void box_gpu_draw_prepass_draws(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot,
    bool late,
    WGPUTextureFormat count,
    box_gpu_stat_t stat)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup material = NULL, instances = NULL;
    uint32_t offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    int32_t i;
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    for (i = 0; cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        if (!box_gpu_prepass_draws(b)) {
            continue;
        }

        WGPURenderPipeline pipeline = box_gpu_prepass_pipeline(gpu, b, count);
        if (!pipeline) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup layers = box_gpu_material_group(gpu, packet, b);
        if (layers != material) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, layers, 1, &(uint32_t){0});
            material = layers;
        }

        if (box_gpu_cull_draw(gpu, pass, packet, d, 2, late, true, &instances)) {
            box_gpu_count(gpu, stat);
        }
    }
}

/** Record a depth prepass of a view: its opaque and alpha tested mesh draws, depth only.
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
    WGPURenderPassDepthStencilAttachment depth = {
        .view = t->depth_view,
        .depthLoadOp = late ? WGPULoadOp_Load : WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 0.0f
    };
    WGPURenderPassDescriptor desc = { .depthStencilAttachment = &depth };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    box_gpu_draw_prepass_draws(gpu, pass, packet, v, group, slot, late,
        WGPUTextureFormat_Undefined, BOX_GPU_STAT_PREPASS_DRAW);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    box_gpu_count(gpu, BOX_GPU_STAT_PREPASS);
}

bool box_gpu_draw_overdraw(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot,
    WGPUTextureView count,
    WGPUTextureFormat format,
    WGPUTextureView depth,
    uint32_t width,
    uint32_t height)
{
    bool result = box_gpu_shader_get(gpu, BOX_RENDER_SHADER_MESH) != NULL ||
        box_gpu_shader_get(gpu, BOX_RENDER_SHADER_MESH_EXTENDED) != NULL;
    if (!result) {
        goto done;
    }

    WGPURenderPassColorAttachment attachment = {
        .view = count,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0, 0, 0, 0 }
    };
    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = depth,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Discard,
        .depthClearValue = 0.0f
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,
        &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
            .depthStencilAttachment = &depth_attachment
        });
    wgpuRenderPassEncoderSetViewport(pass, 0, 0, (float)width, (float)height, 0, 1);
    box_gpu_draw_prepass_draws(gpu, pass, packet, v, group, slot, false, format,
        BOX_GPU_STAT_DRAW);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
done:
    return result;
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
        box_gpu_planar_render(gpu, t, encoder, packet, v, slot);
        bool prepasses = box_gpu_draw_prepasses(gpu, packet, v, slot);
        box_gpu_draw_prewarm(gpu, hdr ? BOX_GPU_HDR_FORMAT : t->format, v,
            v->lights.enabled && (v->features & BOX_RENDER_FEATURE_DEPTH_PREPASS) &&
            box_camera_has_depth(&v->camera_data));
        WGPUTextureView ao = prepasses ? box_gpu_gtao_begin(gpu, t, packet, v) : NULL;
        box_gpu_snapshot_t *snapshot = v->debug.wireframe == WireframeOnly ? NULL :
            box_gpu_transmission_begin(gpu, t, packet, slot, source,
                hdr ? BOX_GPU_HDR_FORMAT : t->format);
        WGPUBindGroup group = box_gpu_light_group(gpu, t, v, slot, ao, snapshot);
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

        if (hdr) {
            box_gpu_effects_run(gpu, t, encoder, packet, v, color);
        }

        if (box_gpu_debug_enabled(v)) {
            box_gpu_debug_record(gpu, t, encoder, color, packet, v, group, slot);
        }

        wgpuBindGroupRelease(group);

        box_gpu_count(gpu, BOX_GPU_STAT_PASS);
        slot ++;
    }

    box_gpu_cull_finish(gpu, encoder);
done:
    return;
}

#endif
