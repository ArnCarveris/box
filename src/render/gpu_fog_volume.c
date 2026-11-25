/**
 * @file render/gpu_fog_volume.c
 * @brief Fog volumes of the webgpu backend: the FogVolumes a view draws, lit in a froxel grid
 * aligned with its frustum and applied in its forward pass (etc/shaders/fog_volume.wgsl).
 *
 * Each view with volumes in a target has a grid (box_gpu_froxels_t): before its forward pass a
 * compute pass lights every cell for all its volumes blended with its previous frame (inject) and
 * accumulates the cells front to back along every column (integrate). No volumes, no passes.
 *
 * The forward pass ends after the opaque meshes and the sky and continues in a pass that reads
 * the depth, where one fullscreen draw applies the grid before the particles. The uniforms and
 * records of a packet share one buffer; a grid unused for BOX_GPU_FROXELS_IDLE packets is freed.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes the uniform of a pass takes at the start of its range: the offset alignment of webgpu,
 * so the records after it start at an aligned offset */
#define BOX_GPU_FOG_UNIFORM_STRIDE (256u)

/** Bytes of the uniform of a pass, Froxel in froxel.wgsl */
#define BOX_GPU_FOG_UNIFORM_SIZE (176u)

/** Smallest buffer of the passes of a packet in bytes */
#define BOX_GPU_FOG_BUFFER_MIN (16384u)

/** Storage buffers the inject stage binds: the five of the view group (lights, clusters,
 * reflection probes and their SH, probe buffer of the view) and the records */
#define BOX_GPU_FOG_STORAGE (6u)

/** Sampled textures per shader stage the fragment stage of the forward pass binds: the twelve of
 * the view group, with the fog grid, and the five of the material group */
#define BOX_GPU_FOG_SAMPLED (17u)

/** Packets after which the grid of a view that drew no fog volumes is released */
#define BOX_GPU_FROXELS_IDLE (120u)

/** Uniform of a pass, matching Froxel in froxel.wgsl */
typedef struct box_gpu_fog_uniforms_t {
    mat4 inv_view_proj;             /**< Clip space of the target to world */
    float depth_plane[4];           /**< World position to view depth */
    uint32_t grid[4];               /**< Cells across and down, slices and volumes */
    float range[4];                 /**< Tile in pixels, view depth of the near plane and of
                                     *   the far end of the grid, 1 for orthographic */
    float size[4];                  /**< Render size and the share of the grid it covers */
    uint32_t segment[4];            /**< box_gpu_fog_segment_t an apply draw takes */
    float prev_depth_plane[4];      /**< depth_plane of the previous frame */
    float prev_range[4];            /**< View depth of the near plane and of the far end of the
                                     *   grid in the previous frame, 1 for orthographic, and 1
                                     *   when inject blends with the previous cells */
} box_gpu_fog_uniforms_t;

/** The froxel grid of a view in a target */
typedef struct box_gpu_froxels_t {
    WGPUTexture integrated;         /**< Light scattered toward the camera and transmittance
                                     *   from the camera to the far side of each cell */
    WGPUTextureView integrated_view; /**< View of integrated */
    uint32_t size[3];               /**< Cells across, down and slices */
    uint64_t used;                  /**< Packet the grid was injected in last */
    box_gpu_fog_uniforms_t current; /**< Uniform of the last frame the grid was injected in */
    box_gpu_fog_uniforms_t previous; /**< Uniform of the frame before that one */
    float current_distance;         /**< Fog distance of current */
    float previous_distance;        /**< Fog distance of previous */
    uint64_t height_fog;            /**< Packet the apply draw applied the height fog of the
                                     *   view in last */
    int64_t frame;                  /**< Packet frame of current, -1 for none */
    bool has_previous;              /**< previous holds a frame */
} box_gpu_froxels_t;

struct box_gpu_fog_volumes_t {
    WGPUBindGroupLayout inject_layout; /**< Records, uniform, noise, the cells written and the
                                     *   cells of the previous frame */
    WGPUBindGroupLayout integrate_layout; /**< Uniform, cells and the integrated grid written */
    WGPUBindGroupLayout apply_layout; /**< Records, uniform, depth, glass depth, integrated
                                       * grid, and the height fog and its sky color */
    WGPUPipelineLayout inject_pipeline_layout; /**< View group and inject_layout */
    WGPUPipelineLayout integrate_pipeline_layout; /**< Empty group and integrate_layout */
    WGPUPipelineLayout apply_pipeline_layout; /**< View group and apply_layout */
    WGPUComputePipeline inject;     /**< Pipeline of the inject entry point */
    WGPUComputePipeline integrate;  /**< Pipeline of the integrate entry point */
    int32_t compute_revision;       /**< Revision of the shader of the compute pipelines */
    ecs_map_t pipelines;            /**< Color format to the WGPURenderPipeline that applies the
                                     *   grid into it */
    int32_t revision;               /**< Revision of the shader of the apply pipelines */
    ecs_map_t grids;                /**< View and target to box_gpu_froxels_t */
    uint64_t packet;                /**< Packets begun */
    WGPUBuffer buffer;              /**< Uniforms and records of the passes of the packet */
    uint64_t size;                  /**< Bytes of the buffer */
    uint64_t used;                  /**< Bytes of the buffer the passes of the packet took */
    WGPUTexture noise;              /**< Noise texture, NULL until a volume with noise draws */
    WGPUTextureView noise_view;     /**< View of the noise texture */
    WGPUTexture placeholder;        /**< Texture of one texel views without noise bind */
    WGPUTextureView placeholder_view; /**< View of the placeholder */
    WGPUTexture no_sky;             /**< Sky color of one texel the apply draw binds without a
                                     *   height fog */
    WGPUTextureView no_sky_view;    /**< View of no_sky */
    WGPUTexture no_grid;            /**< Integrated grid of one texel of no light and full
                                     *   transmittance, which the view group of a view without
                                     *   fog volumes binds */
    WGPUTextureView no_grid_view;   /**< View of no_grid */
    WGPUSampler sampler;            /**< Repeating trilinear sampler of the noise */
    WGPUSampler grid_sampler;       /**< Clamping trilinear sampler of the integrated grid */
    bool unsupported;               /**< The device binds too few storage buffers or textures
                                     *   in a compute stage, so no volume draws */
};

/** Create an R8 3D texture that the compute stage samples, filled with texels, and its mip
 * chain down to levels, each level the 2x2x2 mean of the one above (box_render_fog_noise_mip).
 * The queue copies the texels of a level as it writes it, so the next level replaces them.
 *
 * @param gpu The backend
 * @param size Texels along each edge, a power of two of at least 2^(levels - 1)
 * @param levels Mip levels, 1 for the texels alone
 * @param texels The texels, x fastest, then y, then z
 * @param view Receives the view of the texture
 * @return The texture
 */
static WGPUTexture box_gpu_fog_texture(
    box_gpu_t *gpu,
    uint32_t size,
    uint32_t levels,
    const uint8_t *texels,
    WGPUTextureView *view)
{
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = { size, size, size },
        .format = WGPUTextureFormat_R8Unorm,
        .mipLevelCount = levels,
        .sampleCount = 1
    });
    uint8_t *mip = levels > 1 ? ecs_os_malloc((size_t)size * size * size / 8) : NULL;
    const uint8_t *level = texels;
    uint32_t i;
    for (i = 0; i < levels; i ++) {
        WGPUTexelCopyTextureInfo dst = {
            .texture = result,
            .mipLevel = i,
            .aspect = WGPUTextureAspect_All
        };
        WGPUTexelCopyBufferLayout layout = { .bytesPerRow = size, .rowsPerImage = size };
        WGPUExtent3D extent = { size, size, size };
        wgpuQueueWriteTexture(gpu->queue, &dst, level, (size_t)size * size * size, &layout,
            &extent);
        if (i + 1 < levels) {
            box_render_fog_noise_mip(level, (int32_t)size, mip);
            level = mip;
            size /= 2;
        }
    }

    ecs_os_free(mip);
    *view = wgpuTextureCreateView(result, &(WGPUTextureViewDescriptor){
        .format = WGPUTextureFormat_R8Unorm,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = levels,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });

    return result;
}

/** Get the view of the noise texture, generated on the CPU when the first view with noise draws.
 * It lives until the backend is released, so a backend restarted after a device loss makes it
 * again; views without noise bind the placeholder instead.
 *
 * @param gpu The backend
 * @return The view
 */
static WGPUTextureView box_gpu_fog_noise(
    box_gpu_t *gpu)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    if (!f->noise) {
        const size_t n = BOX_RENDER_FOG_NOISE_SIZE;
        uint8_t *texels = ecs_os_malloc(n * n * n);
        box_render_fog_noise(texels);
        uint32_t levels = 1;
        while ((n >> levels) > 0) {
            levels ++;
        }

        f->noise = box_gpu_fog_texture(gpu, (uint32_t)n, levels, texels, &f->noise_view);
        ecs_os_free(texels);
    }

    return f->noise_view;
}

/** Create the integrated grid of one texel that views without fog volumes bind in their view
 * group: no scattered light and full transmittance (the half floats 0, 0, 0, 1), so their
 * blended draws are not fogged.
 *
 * As a 2D texture, the sky color an apply draw without a height fog binds.
 *
 * @param gpu The backend
 * @param dimension WGPUTextureViewDimension_3D for the grid, _2D for the sky color
 * @param view Receives the view of the texture
 * @return The texture
 */
static WGPUTexture box_gpu_fog_no_grid(
    box_gpu_t *gpu,
    WGPUTextureViewDimension dimension,
    WGPUTextureView *view)
{
    const uint16_t texel[4] = { 0, 0, 0, 0x3C00 };
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = dimension == WGPUTextureViewDimension_3D
            ? WGPUTextureDimension_3D
            : WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = WGPUTextureFormat_RGBA16Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    WGPUTexelCopyTextureInfo dst = { .texture = result, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferLayout layout = { .bytesPerRow = sizeof(texel), .rowsPerImage = 1 };
    WGPUExtent3D extent = { 1, 1, 1 };
    wgpuQueueWriteTexture(gpu->queue, &dst, texel, sizeof(texel), &layout, &extent);
    *view = wgpuTextureCreateView(result, &(WGPUTextureViewDescriptor){
        .format = WGPUTextureFormat_RGBA16Float,
        .dimension = dimension,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });

    return result;
}

/** Create the layout of group 1 of the apply draws: the records, the uniform, the depth, the
 * glass depth, the integrated grid and its sampler, and the height fog and its sky color.
 *
 * @param gpu The backend
 * @return The layout
 */
static WGPUBindGroupLayout box_gpu_fog_apply_layout(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry apply[8] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(box_render_fog_volume_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_GPU_FOG_UNIFORM_SIZE
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
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
            .binding = 13,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_height_fog_t)
            }
        },
        {
            .binding = 14,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };

    return wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 8, .entries = apply });
}

/** Release the textures of a grid and the grid.
 *
 * @param g The grid
 */
static void box_gpu_froxels_free(
    box_gpu_froxels_t *g)
{
    if (g->integrated) {
        wgpuTextureViewRelease(g->integrated_view);
        wgpuTextureRelease(g->integrated);
    }

    ecs_os_free(g);
}

void box_gpu_fog_volumes_init(
    box_gpu_t *gpu)
{
    box_gpu_fog_volumes_t *f = ecs_os_calloc_t(box_gpu_fog_volumes_t);
    WGPULimits limits = WGPU_LIMITS_INIT;
    const uint8_t placeholder = 128;
    WGPUBindGroupLayoutEntry inject[7] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(box_render_fog_volume_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_GPU_FOG_UNIFORM_SIZE
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_3D
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = WGPUTextureFormat_RGBA16Float,
                .viewDimension = WGPUTextureViewDimension_3D
            }
        },
        {
            .binding = 11,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_3D
            }
        },
        {
            .binding = 12,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutEntry integrate[3] = {
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = BOX_GPU_FOG_UNIFORM_SIZE
            }
        },
        {
            .binding = 7,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_3D
            }
        },
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = WGPUTextureFormat_RGBA16Float,
                .viewDimension = WGPUTextureViewDimension_3D
            }
        }
    };
    f->inject_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 7, .entries = inject });
    f->integrate_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 3, .entries = integrate });
    f->apply_layout = box_gpu_fog_apply_layout(gpu);
    f->sampler = wgpuDeviceCreateSampler(gpu->device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_Repeat,
        .addressModeV = WGPUAddressMode_Repeat,
        .addressModeW = WGPUAddressMode_Repeat,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    });
    f->grid_sampler = wgpuDeviceCreateSampler(gpu->device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    });
    f->placeholder = box_gpu_fog_texture(gpu, 1, 1, &placeholder, &f->placeholder_view);
    f->no_grid = box_gpu_fog_no_grid(gpu, WGPUTextureViewDimension_3D, &f->no_grid_view);
    f->no_sky = box_gpu_fog_no_grid(gpu, WGPUTextureViewDimension_2D, &f->no_sky_view);
    WGPUBindGroupLayout inject_groups[2] = { gpu->view_layout, f->inject_layout };
    WGPUBindGroupLayout integrate_groups[2] = {
        gpu->prepass_empty_layout, f->integrate_layout
    };
    WGPUBindGroupLayout apply_groups[2] = { gpu->view_layout, f->apply_layout };
    f->inject_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = inject_groups
        });
    f->integrate_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = integrate_groups
        });
    f->apply_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = apply_groups
        });
    if (wgpuDeviceGetLimits(gpu->device, &limits) == WGPUStatus_Success) {
        if (limits.maxStorageBuffersPerShaderStage != WGPU_LIMIT_U32_UNDEFINED &&
            limits.maxStorageBuffersPerShaderStage < BOX_GPU_FOG_STORAGE)
        {
            ecs_warn("fog volumes need %u storage buffers per shader stage, the device has %u",
                BOX_GPU_FOG_STORAGE, limits.maxStorageBuffersPerShaderStage);
            f->unsupported = true;
        }

        if (limits.maxSampledTexturesPerShaderStage != WGPU_LIMIT_U32_UNDEFINED &&
            limits.maxSampledTexturesPerShaderStage < BOX_GPU_FOG_SAMPLED)
        {
            ecs_warn("the forward pass binds the fog grid in its view group and needs %u "
                "sampled textures per shader stage, the device has %u", BOX_GPU_FOG_SAMPLED,
                limits.maxSampledTexturesPerShaderStage);
        }
    }

    ecs_map_init(&f->pipelines, NULL);
    ecs_map_init(&f->grids, NULL);
    gpu->fog_volumes = f;
}

/** Release the apply pipelines of every format.
 *
 * @param f The fog volume state
 */
static void box_gpu_fog_pipelines_free(
    box_gpu_fog_volumes_t *f)
{
    ecs_map_iter_t it = ecs_map_iter(&f->pipelines);
    while (ecs_map_next(&it)) {
        WGPURenderPipeline pipeline = (WGPURenderPipeline)(uintptr_t)ecs_map_value(&it);
        if (pipeline) {
            wgpuRenderPipelineRelease(pipeline);
        }
    }

    ecs_map_clear(&f->pipelines);
}

void box_gpu_fog_volumes_fini(
    box_gpu_t *gpu)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    if (!f) {
        goto done;
    }

    box_gpu_fog_pipelines_free(f);
    ecs_map_fini(&f->pipelines);
    ecs_map_iter_t it = ecs_map_iter(&f->grids);
    while (ecs_map_next(&it)) {
        box_gpu_froxels_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&f->grids);
    WGPUComputePipeline pipelines[2] = { f->inject, f->integrate };
    box_gpu_compute_pipelines_release(pipelines, 2);
    if (f->buffer) {
        wgpuBufferRelease(f->buffer);
    }

    if (f->noise_view) {
        wgpuTextureViewRelease(f->noise_view);
    }

    if (f->noise) {
        wgpuTextureRelease(f->noise);
    }

    wgpuTextureViewRelease(f->placeholder_view);
    wgpuTextureViewRelease(f->no_sky_view);
    wgpuTextureRelease(f->no_sky);
    wgpuTextureRelease(f->placeholder);
    wgpuTextureViewRelease(f->no_grid_view);
    wgpuTextureRelease(f->no_grid);
    wgpuSamplerRelease(f->sampler);
    wgpuSamplerRelease(f->grid_sampler);
    wgpuPipelineLayoutRelease(f->inject_pipeline_layout);
    wgpuPipelineLayoutRelease(f->integrate_pipeline_layout);
    wgpuPipelineLayoutRelease(f->apply_pipeline_layout);
    wgpuBindGroupLayoutRelease(f->inject_layout);
    wgpuBindGroupLayoutRelease(f->integrate_layout);
    wgpuBindGroupLayoutRelease(f->apply_layout);
    ecs_os_free(f);
    gpu->fog_volumes = NULL;
done:
    return;
}

void box_gpu_fog_volumes_begin(
    box_gpu_t *gpu)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    if (!f) {
        goto done;
    }

    f->used = 0;
    f->packet ++;
    ecs_map_iter_t it = ecs_map_iter(&f->grids);
    while (ecs_map_next(&it)) {
        box_gpu_froxels_t *g = ecs_map_ptr(&it);
        if (f->packet - g->used > BOX_GPU_FROXELS_IDLE) {
            box_gpu_froxels_free(g);
            ecs_map_remove(&f->grids, ecs_map_key(&it));
            it = ecs_map_iter(&f->grids);
        }
    }

done:
    return;
}

/** Get the pipeline that applies the grid into a color format, created on first use and again
 * when the shader was recompiled.
 *
 * One fullscreen triangle without depth test or depth write. The color adds the scattered light
 * to the target times the transmittance, the alpha of the target is kept.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @return The pipeline, NULL until the shader exists
 */
static WGPURenderPipeline box_gpu_fog_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_FOG_VOLUME);
    WGPURenderPipeline result = NULL;
    ecs_map_val_t *cached;
    if (!shader || !shader->module) {
        goto done;
    }

    if (f->revision != shader->revision) {
        box_gpu_fog_pipelines_free(f);
        f->revision = shader->revision;
    }

    cached = ecs_map_get(&f->pipelines, (ecs_map_key_t)format);
    if (cached && *cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    WGPUBlendState blend = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_SrcAlpha
        },
        .alpha = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_Zero,
            .dstFactor = WGPUBlendFactor_One
        }
    };
    WGPUColorTargetState target = {
        .format = format,
        .blend = &blend,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = shader->module,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_False,
        .depthCompare = WGPUCompareFunction_Always,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    result = box_gpu_render_pipeline(gpu, &(WGPURenderPipelineDescriptor){
        .layout = f->apply_pipeline_layout,
        .vertex = {
            .module = shader->module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    });
    if (result) {
        *ecs_map_ensure(&f->pipelines, (ecs_map_key_t)format) = (ecs_map_val_t)(uintptr_t)result;
    }

done:
    return result;
}

/** Get the inject and integrate pipelines, created again when the shader was recompiled; the
 * previous ones stay until both new ones exist.
 *
 * @param gpu The backend
 * @return Whether both exist
 */
static bool box_gpu_fog_compute_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_FOG_VOLUME);
    if (!s || !s->module) {
        goto done;
    }

    if (f->inject && f->compute_revision == s->revision) {
        goto done;
    }

    WGPUComputePipeline *current[2] = { &f->inject, &f->integrate };
    WGPUComputePipeline next[2] = {
        box_gpu_compute_pipeline(gpu, &(WGPUComputePipelineDescriptor){
            .layout = f->inject_pipeline_layout,
            .compute = {
                .module = s->module,
                .entryPoint = { .data = "inject", .length = WGPU_STRLEN }
            }
        }),
        box_gpu_compute_pipeline(gpu, &(WGPUComputePipelineDescriptor){
            .layout = f->integrate_pipeline_layout,
            .compute = {
                .module = s->module,
                .entryPoint = { .data = "integrate", .length = WGPU_STRLEN }
            }
        })
    };
    if (box_gpu_compute_pipelines_swap(current, next, 2)) {
        f->compute_revision = s->revision;
    }

done:
    return s && s->module && f->inject && f->integrate;
}

bool box_gpu_fog_volumes_ready(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    const box_gpu_fog_volumes_t *f = gpu->fog_volumes;

    return f && !f->unsupported && v->fog_count > 0 &&
        box_gpu_shader_get(gpu, BOX_RENDER_SHADER_FOG_VOLUME) != NULL &&
        box_gpu_fog_compute_pipelines(gpu);
}

/** Make room for the uniform and the records of a pass in the buffer of the packet, replacing
 * the buffer with a larger one when they do not fit; earlier passes keep the old one through
 * their bind groups.
 *
 * @param gpu The backend
 * @param need Bytes of the pass
 * @return Offset of the pass in the buffer
 */
static uint64_t box_gpu_fog_reserve(
    box_gpu_t *gpu,
    uint64_t need)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    uint64_t offset = f->used;
    if (!f->buffer || offset + need > f->size) {
        uint64_t size = f->size ? f->size : BOX_GPU_FOG_BUFFER_MIN;
        while (size < need * 2) {
            size *= 2;
        }

        if (f->buffer) {
            wgpuBufferRelease(f->buffer);
        }

        f->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage |
                WGPUBufferUsage_CopyDst,
            .size = size
        });
        f->size = size;
        offset = 0;
    }

    f->used = (offset + need + BOX_GPU_FOG_UNIFORM_STRIDE - 1) &
        ~(uint64_t)(BOX_GPU_FOG_UNIFORM_STRIDE - 1);

    return offset;
}

/** Fill the uniform of a pass of a view in a target.
 *
 * @param t The target, whose size the projection and the grid follow
 * @param v The view
 * @param segment The segment an apply draw takes, FULL for the compute passes
 * @param u Receives the uniform
 */
static void box_gpu_fog_uniforms(
    const box_gpu_target_t *t,
    const box_render_view_t *v,
    box_gpu_fog_segment_t segment,
    box_gpu_fog_uniforms_t *u)
{
    mat4 proj, view_proj;
    vec4 near_point = { 0, 0, 1, 1 };
    int32_t grid[3];
    int32_t k;
    ecs_os_memset_t(u, 0, box_gpu_fog_uniforms_t);
    box_camera_projection(&v->camera_data,
        (float)t->desc.point_width, (float)t->desc.point_height, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), view_proj);
    glm_mat4_inv(view_proj, u->inv_view_proj);
    for (k = 0; k < 4; k ++) {
        u->depth_plane[k] = -v->view_matrix[k][2];
    }

    glm_mat4_mulv(u->inv_view_proj, near_point, near_point);
    float w = fabsf(near_point[3]) > 1e-12f ? near_point[3] : 1.0f;
    float near_ = (u->depth_plane[0] * near_point[0] + u->depth_plane[1] * near_point[1] +
        u->depth_plane[2] * near_point[2]) / w + u->depth_plane[3];
    int32_t width = t->desc.render_width > 0 ? t->desc.render_width : 1;
    int32_t height = t->desc.render_height > 0 ? t->desc.render_height : 1;
    box_render_froxel_grid(width, height, grid);
    u->grid[0] = (uint32_t)grid[0];
    u->grid[1] = (uint32_t)grid[1];
    u->grid[2] = BOX_FROXEL_SLICES;
    u->grid[3] = (uint32_t)v->fog_count;
    u->range[0] = (float)grid[2];
    u->range[1] = near_;
    u->range[2] = near_ + box_render_fog_distance(v->fog_distance);
    u->range[3] = v->camera_data.ortho ? 1.0f : 0.0f;
    u->size[0] = (float)width;
    u->size[1] = (float)height;
    u->size[2] = (float)width / (float)(grid[0] * grid[2]);
    u->size[3] = (float)height / (float)(grid[1] * grid[2]);
    u->segment[0] = (uint32_t)segment;
}

/** Create the integrated 3D texture of the grid that the compute stage writes and the passes
 * after it read.
 *
 * @param gpu The backend
 * @param size Cells across, down and slices
 * @param view Receives the view of the texture
 * @return The texture
 */
static WGPUTexture box_gpu_froxel_texture(
    box_gpu_t *gpu,
    const uint32_t *size,
    WGPUTextureView *view)
{
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding,
        .dimension = WGPUTextureDimension_3D,
        .size = { size[0], size[1], size[2] },
        .format = WGPUTextureFormat_RGBA16Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    *view = wgpuTextureCreateView(result, &(WGPUTextureViewDescriptor){
        .format = WGPUTextureFormat_RGBA16Float,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });

    return result;
}

/** Key of the grid of a view in a target.
 *
 * @param t The target
 * @param v The view
 * @return The key
 */
static uint64_t box_gpu_froxels_key(
    const box_gpu_target_t *t,
    const box_render_view_t *v)
{
    return (uint64_t)v->view ^ ((uint64_t)t->handle * 0x9E3779B97F4A7C15ull);
}

/** Get the grid of a view in a target with its size, its textures created again when the size
 * changed.
 *
 * @param gpu The backend
 * @param t The target
 * @param v The view
 * @param size Cells across, down and slices
 * @return The grid
 */
static box_gpu_froxels_t* box_gpu_froxels_get(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v,
    const uint32_t *size)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    uint64_t key = box_gpu_froxels_key(t, v);
    box_gpu_froxels_t *result = ecs_map_get_deref(&f->grids, box_gpu_froxels_t, key);
    if (!result) {
        result = ecs_os_calloc_t(box_gpu_froxels_t);
        result->frame = -1;
        ecs_map_insert_ptr(&f->grids, key, result);
    }

    if (!result->integrated || memcmp(result->size, size, sizeof(result->size))) {
        if (result->integrated) {
            wgpuTextureViewRelease(result->integrated_view);
            wgpuTextureRelease(result->integrated);
        }

        result->integrated = box_gpu_froxel_texture(gpu, size, &result->integrated_view);
        memcpy(result->size, size, sizeof(result->size));
    }

    return result;
}

/** Check whether the blended draws of a view fog themselves with its grid: a lit view, not an
 * impostor frame, that draws fog volumes in a target.
 *
 * @param gpu The backend
 * @param t The target, NULL for the captures
 * @param v The view
 * @return Whether its view group binds its grid
 */
static bool box_gpu_fog_blends(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v)
{
    return t && v->lights.enabled && !v->impostor && box_gpu_fog_volumes_ready(gpu, v);
}

void box_gpu_fog_volumes_uniforms(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u)
{
    box_gpu_fog_uniforms_t f;
    glm_vec4_zero(u->fog_grid);
    glm_vec4_zero(u->fog_range);
    if (!box_gpu_fog_blends(gpu, t, v)) {
        goto done;
    }

    box_gpu_fog_uniforms(t, v, BOX_GPU_FOG_SEGMENT_FULL, &f);
    u->fog_grid[0] = f.size[2];
    u->fog_grid[1] = f.size[3];
    u->fog_grid[2] = (float)f.grid[2];
    u->fog_grid[3] = 1.0f;
    u->fog_range[0] = f.range[1];
    u->fog_range[1] = f.range[2];
    u->fog_range[2] = f.range[3];
done:
    return;
}

WGPUTextureView box_gpu_fog_volumes_grid_view(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    box_gpu_fog_uniforms_t u;
    WGPUTextureView result = f->no_grid_view;
    if (box_gpu_fog_blends(gpu, t, v)) {
        box_gpu_fog_uniforms(t, v, BOX_GPU_FOG_SEGMENT_FULL, &u);
        result = box_gpu_froxels_get(gpu, t, v, u.grid)->integrated_view;
    }

    return result;
}

int32_t box_gpu_fog_volumes_unfogged_write(
    box_gpu_t *gpu,
    int32_t views,
    int32_t count)
{
    int32_t i, result = 0;
    ecs_vec_set_count_t(NULL, &gpu->view_unfogged, int32_t, views);
    for (i = 0; i < views; i ++) {
        int32_t *unfogged = ecs_vec_get_t(&gpu->view_unfogged, int32_t, i);
        const box_gpu_view_uniforms_t *u = (const box_gpu_view_uniforms_t*)ecs_vec_get_t(
            &gpu->view_data, uint8_t, i * BOX_GPU_VIEW_STRIDE);
        *unfogged = -1;
        if (u->fog_grid[3] <= 0.0f) {
            continue;
        }

        int32_t copy = count + result;
        ecs_vec_set_count_t(NULL, &gpu->view_data, uint8_t, (copy + 1) * BOX_GPU_VIEW_STRIDE);
        uint8_t *data = ecs_vec_first(&gpu->view_data);
        ecs_os_memcpy(data + copy * BOX_GPU_VIEW_STRIDE, data + i * BOX_GPU_VIEW_STRIDE,
            BOX_GPU_VIEW_STRIDE);
        box_gpu_view_uniforms_t *c = (box_gpu_view_uniforms_t*)(
            data + copy * BOX_GPU_VIEW_STRIDE);
        glm_vec4_zero(c->fog_grid);
        *unfogged = copy;
        result ++;
    }

    return result;
}

uint32_t box_gpu_fog_volumes_unfogged(
    const box_gpu_t *gpu,
    int32_t slot)
{
    int32_t copy = slot < ecs_vec_count(&gpu->view_unfogged)
        ? *ecs_vec_get_t(&gpu->view_unfogged, int32_t, slot)
        : -1;

    return (uint32_t)((copy >= 0 ? copy : slot) * BOX_GPU_VIEW_STRIDE);
}

/** Keep the uniform of the frame a grid is injected in and fill the members of the uniform
 * about the previous frame: its slicing, and whether inject blends with its cells, which needs
 * a valid history pair of the same grid size and fog distance.
 *
 * The injected cells of a view are kept in a ping-pong pair of gpu_history.c: inject samples
 * each cell at a point jittered inside it by the sequence of the frame and blends it with the
 * previous cells, found at its center through the previous view projection and this slicing.
 *
 * @param g The grid
 * @param packet The packet
 * @param distance Fog distance of the view
 * @param valid Whether the history pair holds the previous frame of the view
 * @param u The uniform of the frame, receives the members about the previous frame
 */
static void box_gpu_froxels_history(
    box_gpu_froxels_t *g,
    const box_render_packet_t *packet,
    float distance,
    bool valid,
    box_gpu_fog_uniforms_t *u)
{
    int32_t k;
    if (g->frame != packet->frame) {
        g->has_previous = g->frame >= 0;
        g->previous = g->current;
        g->previous_distance = g->current_distance;
    }

    valid = valid && g->has_previous && g->previous_distance == distance &&
        !memcmp(g->previous.grid, u->grid, 3 * sizeof(uint32_t));
    for (k = 0; k < 4; k ++) {
        u->prev_depth_plane[k] = g->previous.depth_plane[k];
    }

    u->prev_range[0] = g->previous.range[1];
    u->prev_range[1] = g->previous.range[2];
    u->prev_range[2] = g->previous.range[3];
    u->prev_range[3] = valid ? 1.0f : 0.0f;
    g->current = *u;
    g->current_distance = distance;
    g->frame = packet->frame;
}

void box_gpu_fog_volumes_inject(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUBindGroup group,
    int32_t slot)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    box_gpu_fog_uniforms_t u;
    int32_t i;
    if (!box_gpu_fog_volumes_ready(gpu, v) ||
        v->fog_first + v->fog_count > ecs_vec_count(&packet->fog_volumes))
    {
        goto done;
    }

    const box_render_fog_volume_t *fogs = ecs_vec_get_t(
        &packet->fog_volumes, box_render_fog_volume_t, v->fog_first);
    WGPUTextureView noise = f->placeholder_view;
    for (i = 0; i < v->fog_count; i ++) {
        if (fogs[i].noise_strength > 0.0f) {
            noise = box_gpu_fog_noise(gpu);
            break;
        }
    }

    box_gpu_fog_uniforms(t, v, BOX_GPU_FOG_SEGMENT_FULL, &u);
    box_gpu_froxels_t *g = box_gpu_froxels_get(gpu, t, v, u.grid);
    box_gpu_history_desc_t desc = {
        .target = t->handle,
        .view = v->view,
        .consumer = BOX_GPU_HISTORY_FOG_VOLUMES,
        .dimension = WGPUTextureDimension_3D,
        .width = g->size[0],
        .height = g->size[1],
        .depth = g->size[2],
        .format = WGPUTextureFormat_RGBA16Float
    };
    box_gpu_history_t h;
    if (!box_gpu_history_acquire(gpu, packet, &desc, &h)) {
        goto done;
    }

    box_gpu_froxels_history(g, packet, box_render_fog_distance(v->fog_distance), h.valid, &u);
    uint64_t records = (uint64_t)v->fog_count * sizeof(box_render_fog_volume_t);
    uint64_t offset = box_gpu_fog_reserve(gpu, BOX_GPU_FOG_UNIFORM_STRIDE + records);
    uint8_t *block = ecs_os_calloc((ecs_size_t)(BOX_GPU_FOG_UNIFORM_STRIDE + records));
    ecs_os_memcpy(block, &u, sizeof(u));
    ecs_os_memcpy(block + BOX_GPU_FOG_UNIFORM_STRIDE, fogs, (ecs_size_t)records);
    wgpuQueueWriteBuffer(gpu->queue, f->buffer, offset, block,
        (size_t)(BOX_GPU_FOG_UNIFORM_STRIDE + records));
    ecs_os_free(block);

    WGPUBindGroupEntry inject[7] = {
        {
            .binding = 0,
            .buffer = f->buffer,
            .offset = offset + BOX_GPU_FOG_UNIFORM_STRIDE,
            .size = records
        },
        {
            .binding = 1,
            .buffer = f->buffer,
            .offset = offset,
            .size = BOX_GPU_FOG_UNIFORM_SIZE
        },
        { .binding = 3, .textureView = noise },
        { .binding = 4, .sampler = f->sampler },
        { .binding = 6, .textureView = h.write },
        { .binding = 11, .textureView = h.read },
        { .binding = 12, .sampler = f->grid_sampler }
    };
    WGPUBindGroupEntry integrate[3] = {
        {
            .binding = 1,
            .buffer = f->buffer,
            .offset = offset,
            .size = BOX_GPU_FOG_UNIFORM_SIZE
        },
        { .binding = 7, .textureView = h.write },
        { .binding = 8, .textureView = g->integrated_view }
    };
    WGPUBindGroup inject_group = wgpuDeviceCreateBindGroup(gpu->device,
        &(WGPUBindGroupDescriptor){
            .layout = f->inject_layout,
            .entryCount = 7,
            .entries = inject
        });
    WGPUBindGroup integrate_group = wgpuDeviceCreateBindGroup(gpu->device,
        &(WGPUBindGroupDescriptor){
            .layout = f->integrate_layout,
            .entryCount = 3,
            .entries = integrate
        });
    uint32_t view_offset = (uint32_t)(slot * BOX_GPU_VIEW_STRIDE);
    WGPUComputePassEncoder pass = box_gpu_profile_compute_pass(gpu, encoder, "fog volumes");
    wgpuComputePassEncoderSetPipeline(pass, f->inject);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &view_offset);
    wgpuComputePassEncoderSetBindGroup(pass, 1, inject_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,
        ((g->size[0] + 1) / 2 + BOX_FROXEL_INJECT_GROUP - 1) / BOX_FROXEL_INJECT_GROUP,
        (g->size[1] + BOX_FROXEL_INJECT_GROUP - 1) / BOX_FROXEL_INJECT_GROUP,
        (g->size[2] + BOX_FROXEL_INJECT_GROUP - 1) / BOX_FROXEL_INJECT_GROUP);
    wgpuComputePassEncoderSetPipeline(pass, f->integrate);
    wgpuComputePassEncoderSetBindGroup(pass, 0, gpu->prepass_empty, 0, NULL);
    wgpuComputePassEncoderSetBindGroup(pass, 1, integrate_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,
        (g->size[0] + BOX_FROXEL_INTEGRATE_GROUP - 1) / BOX_FROXEL_INTEGRATE_GROUP,
        (g->size[1] + BOX_FROXEL_INTEGRATE_GROUP - 1) / BOX_FROXEL_INTEGRATE_GROUP, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(inject_group);
    wgpuBindGroupRelease(integrate_group);
    g->used = f->packet;
    gpu->pending.fog_dispatches += 2;
    for (i = 0; i < 3; i ++) {
        gpu->pending.fog_grid[i] = (int32_t)g->size[i];
    }

    for (i = 0; i < v->fog_count; i ++) {
        box_gpu_count(gpu, BOX_GPU_STAT_FOG_VOLUME);
    }

done:
    return;
}

bool box_gpu_fog_volumes_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_gpu_target_t *t,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureView depth,
    WGPUTextureView glass,
    box_gpu_fog_segment_t segment)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    WGPURenderPipeline pipeline = box_gpu_fog_volumes_ready(gpu, v)
        ? box_gpu_fog_pipeline(gpu, format)
        : NULL;
    box_gpu_froxels_t *g = f
        ? ecs_map_get_deref(&f->grids, box_gpu_froxels_t, box_gpu_froxels_key(t, v))
        : NULL;
    box_gpu_fog_uniforms_t u;
    box_gpu_height_fog_t height;
    WGPUTextureView sky = f ? f->no_sky_view : NULL;
    bool result = false;
    if (!glass) {
        glass = depth;
        segment = BOX_GPU_FOG_SEGMENT_FULL;
    }

    if (!pipeline || !g || g->used != f->packet) {
        goto done;
    }

    box_gpu_fog_uniforms(t, v, segment, &u);
    if (segment == BOX_GPU_FOG_SEGMENT_FULL &&
        box_gpu_effects_height_fog(gpu, t, packet, v, &height, &sky))
    {
        u.segment[1] = 1;
        g->height_fog = f->packet;
    }

    uint64_t records = (uint64_t)v->fog_count * sizeof(box_render_fog_volume_t);
    uint64_t offset = box_gpu_fog_reserve(gpu, 2 * BOX_GPU_FOG_UNIFORM_STRIDE + records);
    wgpuQueueWriteBuffer(gpu->queue, f->buffer, offset, &u, sizeof(u));
    if (u.segment[1]) {
        wgpuQueueWriteBuffer(gpu->queue, f->buffer, offset + BOX_GPU_FOG_UNIFORM_STRIDE,
            &height, sizeof(height));
    }

    wgpuQueueWriteBuffer(gpu->queue, f->buffer, offset + 2 * BOX_GPU_FOG_UNIFORM_STRIDE,
        ecs_vec_get_t(&packet->fog_volumes, box_render_fog_volume_t, v->fog_first),
        (size_t)records);
    WGPUBindGroupEntry entries[8] = {
        {
            .binding = 0,
            .buffer = f->buffer,
            .offset = offset + 2 * BOX_GPU_FOG_UNIFORM_STRIDE,
            .size = records
        },
        {
            .binding = 1,
            .buffer = f->buffer,
            .offset = offset,
            .size = BOX_GPU_FOG_UNIFORM_SIZE
        },
        { .binding = 2, .textureView = depth },
        { .binding = 5, .textureView = glass },
        { .binding = 9, .textureView = g->integrated_view },
        { .binding = 10, .sampler = f->grid_sampler },
        {
            .binding = 13,
            .buffer = f->buffer,
            .offset = offset + BOX_GPU_FOG_UNIFORM_STRIDE,
            .size = sizeof(box_gpu_height_fog_t)
        },
        { .binding = 14, .textureView = sky }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = f->apply_layout,
        .entryCount = 8,
        .entries = entries
    });
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuBindGroupRelease(group);
    box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
    result = true;
done:
    return result;
}

bool box_gpu_fog_volumes_height_fog(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v)
{
    box_gpu_fog_volumes_t *f = gpu->fog_volumes;
    const box_gpu_froxels_t *g = f
        ? ecs_map_get_deref(&f->grids, box_gpu_froxels_t, box_gpu_froxels_key(t, v))
        : NULL;

    return g && g->height_fog == f->packet;
}

#endif
