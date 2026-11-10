/**
 * @file render/gpu_atmosphere.c
 * @brief Atmosphere passes of the webgpu backend: the LUTs of each scene, the sky view and
 * aerial perspective of each view, and the GPU time of every pass.
 *
 * Every pass is a compute dispatch of etc/shaders/atmosphere.wgsl with one shared sampler. Scene
 * LUTs build before the targets of a packet, view passes right before the forward pass, and a
 * HeightFog colored by the atmosphere gets a 2x1 in scatter color after the sky view.
 */

#include "gpu_environment.h"

#ifdef BOX_RENDER_WGPU

/** Format of every atmosphere LUT */
#define BOX_GPU_ATMOS_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Bytes of a texel of an atmosphere LUT */
#define BOX_GPU_ATMOS_TEXEL (8)

/** Bytes of the uniform buffer of an atmosphere pass */
#define BOX_GPU_ATMOS_UNIFORMS (1024)

/** Anisotropic samples of the sampler of the cloud layers. The sky shader splits the long
 * footprint of the grazing plane near the horizon into up to 4 taps, so each tap filters a
 * footprint 4 times longer than wide at most; more samples cost more than they show */
#define BOX_GPU_ATMOS_CLOUD_ANISOTROPY (4)

/** First binding of the textures of the cloud layers in the sky draw, one per layer */
#define BOX_GPU_ATMOS_CLOUD_BINDING (9)

/** Invocations per axis of a workgroup, along x and y */
#define BOX_GPU_ATMOS_GROUP (8)

/** Timestamp copies in flight at most */
#define BOX_GPU_ATMOS_READBACKS (4)

/** Timestamps of the atmosphere passes of a packet at most, a begin and an end per pass */
#define BOX_GPU_ATMOS_STAMPS (64)

/** Bytes of the resolved timestamps of a packet, a multiple of the resolve alignment */
#define BOX_GPU_ATMOS_STAMP_BYTES (BOX_GPU_ATMOS_STAMPS * sizeof(uint64_t))

/** Nanoseconds above which a measured pass is dropped as a stale timestamp */
#define BOX_GPU_ATMOS_MEASURE_MAX (1e9)

/** State of a timestamp readback */
typedef enum box_gpu_atmos_stamp_state_t {
    BOX_GPU_ATMOS_FREE,         /**< Unused */
    BOX_GPU_ATMOS_RECORDING,    /**< The passes of the packet being rendered write into it */
    BOX_GPU_ATMOS_WRITTEN,      /**< Submitted, resolved a packet later */
    BOX_GPU_ATMOS_COPIED,       /**< Resolved and copied, not yet mapping */
    BOX_GPU_ATMOS_MAPPING,      /**< Waiting for the map */
    BOX_GPU_ATMOS_MAPPED,       /**< Mapped, the timestamps can be read */
    BOX_GPU_ATMOS_FAILED        /**< The map failed */
} box_gpu_atmos_stamp_state_t;

/** The timestamps of the atmosphere passes of a packet */
typedef struct box_gpu_atmos_readback_t {
    WGPUBuffer buffer;              /**< Mappable copy of the resolved timestamps */
    int32_t state;                  /**< box_gpu_atmos_stamp_state_t */
    uint32_t count;                 /**< Timestamps the passes wrote */
    uint8_t kinds[BOX_GPU_ATMOS_STAMPS / 2]; /**< box_render_atmos_pass_t of each pass */
} box_gpu_atmos_readback_t;

/** A cloud layer of the sky draw, as CloudLayer in etc/shaders/atmosphere.wgsl */
typedef struct box_gpu_atmos_cloud_t {
    float plane[4];                 /**< Height above the camera, one over the size, texture
                                     *   coordinate of the camera */
    float shape[4];                 /**< Threshold, softness, density, w unused */
    float tint[4];                  /**< Linear tint, w unused */
} box_gpu_atmos_cloud_t;

/** Uniforms of an atmosphere pass, as Atmosphere in etc/shaders/atmosphere.wgsl */
typedef struct box_gpu_atmos_uniforms_t {
    box_render_atmosphere_desc_t medium; /**< The medium, the uniforms of the scene LUTs */
    float sun[4];                   /**< Direction toward the sun */
    float sun_color[4];             /**< Top of atmosphere irradiance of the sun */
    float night[4];                 /**< Night floor of the sky view */
    float view[4];                  /**< Altitude, Mie anisotropy, aerial distance and intensity */
    float sun_disk[4];              /**< Cosine and sine of the sun radius, disk intensity */
    float moon[4];                  /**< Direction toward the moon, its illumination */
    float moon_color[4];            /**< Top of atmosphere irradiance of the moon */
    float moon_disk[4];             /**< Cosine and sine of the moon radius, disk intensity
                                     *   times albedo, earthshine */
    mat4 inv_view_proj;             /**< Clip to world space of the view */
    float fog_light[4];             /**< Direction toward the main light, its horizon fade */
    float fog_moon[4];              /**< Radiance toward the moon as the main light, 1 if it is */
    float clouds[4];                /**< Cloud layers of the sky draw, km per world unit */
    box_gpu_atmos_cloud_t cloud_layers[BOX_CLOUD_LAYER_MAX]; /**< By ascending altitude */
} box_gpu_atmos_uniforms_t;

_Static_assert(sizeof(box_gpu_atmos_uniforms_t) <= BOX_GPU_ATMOS_UNIFORMS,
    "the uniforms of an atmosphere pass fit its buffer");

/** The transmittance and multi scattering LUTs of the atmosphere of a scene */
typedef struct box_gpu_atmos_luts_t {
    WGPUBuffer uniforms;            /**< The medium */
    WGPUTexture transmittance;      /**< Transmittance LUT */
    WGPUTextureView transmittance_view; /**< View sampled and written */
    WGPUTexture multi;              /**< Multi scattering LUT */
    WGPUTextureView multi_view;     /**< View sampled and written */
    box_render_atmosphere_desc_t medium; /**< The medium */
    bool dirty;                     /**< Build before the next targets */
    int32_t revision;               /**< Revision of the shader of the last build, 0 for none */
    WGPUBuffer env_uniforms;        /**< Uniforms of the sky of the environment, NULL before the
                                     *   first one */
    WGPUTexture env_sky;            /**< Sky view LUT of the scene at its ground altitude */
    WGPUTextureView env_sky_view;   /**< View sampled and written */
} box_gpu_atmos_luts_t;

/** The sky view and aerial perspective of a view in a target */
typedef struct box_gpu_atmos_view_t {
    uint32_t target;                /**< Handle of the target */
    ecs_entity_t view;              /**< View entity */
    int64_t frame;                  /**< Frame of the last packet that rendered it */
    WGPUBuffer uniforms;            /**< box_gpu_atmos_uniforms_t of the view */
    WGPUTexture sky;                /**< Sky view LUT */
    WGPUTextureView sky_view;       /**< View sampled and written */
    WGPUTexture aerial;             /**< Aerial perspective volume */
    WGPUTextureView aerial_view;    /**< View sampled and written */
    WGPUTexture fog;                /**< Fog in scatter color, created on first use */
    WGPUTextureView fog_view;       /**< View sampled and written */
    int64_t fog_frame;              /**< Frame of the last packet that wrote the fog color */
    uint32_t clouds[BOX_CLOUD_LAYER_MAX]; /**< Texture handles of the cloud layers in the
                                     *   uniforms, 0 for none */
} box_gpu_atmos_view_t;

/** The atmosphere state of the backend */
struct box_gpu_atmos_t {
    WGPUBindGroupLayout layouts[BOX_RENDER_ATMOS_PASSES]; /**< Bindings of each pass */
    WGPUPipelineLayout pipeline_layouts[BOX_RENDER_ATMOS_PASSES]; /**< Layout of each pass */
    WGPUComputePipeline pipelines[BOX_RENDER_ATMOS_PASSES]; /**< Pipeline of each pass */
    int32_t revision;               /**< Revision of the shader of the pipelines, 0 for none */
    WGPUSampler sampler;            /**< Linear clamp sampler of the LUTs */
    ecs_map_t luts;                 /**< Handle to box_gpu_atmos_luts_t */
    ecs_vec_t views;                /**< box_gpu_atmos_view_t* */
    WGPUQuerySet queries;           /**< Timestamps per readback, NULL without the feature */
    WGPUBuffer resolve;             /**< Resolved timestamps */
    box_gpu_atmos_readback_t readbacks[BOX_GPU_ATMOS_READBACKS]; /**< Timestamp copies */
    int32_t current;                /**< Readback of the packet being rendered, -1 for none */
    WGPUBindGroupLayout sky_layout; /**< Bindings of the sky draw */
    WGPUPipelineLayout sky_pipeline_layout; /**< Layout of the sky draw */
    ecs_vec_t sky_pipelines;        /**< box_gpu_blit_t, the sky draw per color format */
    int32_t sky_revision;           /**< Revision of the shader of the sky pipelines */
    WGPUTexture placeholder;        /**< 1x1x1 volume bound by views without aerial perspective */
    WGPUTextureView placeholder_view; /**< View of the placeholder volume */
    ecs_vec_t env_requests;         /**< box_gpu_atmos_env_request_t that wait for their render */
};

/** A request to render the sky of an atmosphere into the cubemap of an environment */
typedef struct box_gpu_atmos_env_request_t {
    uint32_t handle;                /**< Handle of the cubemap */
    box_render_atmosphere_env_t desc; /**< The LUTs, the lights and the version */
} box_gpu_atmos_env_request_t;

/** Entry points of the passes, by box_render_atmos_pass_t */
static const char *box_gpu_atmos_entries[BOX_RENDER_ATMOS_PASSES] = {
    "transmittance", "multi_scattering", "sky_view", "aerial", "fog", "sky_view", "environment"
};

/** Create a texture the passes write and sample.
 *
 * @param gpu The backend
 * @param width Width in texels
 * @param height Height in texels
 * @param depth Depth in texels, above 1 for a 3D texture
 * @param view Receives the view of the texture
 * @return The texture
 */
static WGPUTexture box_gpu_atmos_texture(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    WGPUTextureView *view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc,
        .dimension = depth > 1 ? WGPUTextureDimension_3D : WGPUTextureDimension_2D,
        .size = { width, height, depth },
        .format = BOX_GPU_ATMOS_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &desc);
    *view = wgpuTextureCreateView(result, NULL);

    return result;
}

/** Create a uniform buffer of an atmosphere pass.
 *
 * @param gpu The backend
 * @return The buffer
 */
static WGPUBuffer box_gpu_atmos_uniform_buffer(
    box_gpu_t *gpu)
{
    return wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_GPU_ATMOS_UNIFORMS
    });
}

/** Release a texture and its view when they exist.
 *
 * @param texture The texture
 * @param view The view
 */
static void box_gpu_atmos_release(
    WGPUTexture *texture,
    WGPUTextureView *view)
{
    if (*view) {
        wgpuTextureViewRelease(*view);
        *view = NULL;
    }

    if (*texture) {
        wgpuTextureRelease(*texture);
        *texture = NULL;
    }
}

/** Create the bind group layout and pipeline layout of a pass.
 *
 * Every pass binds the uniforms at 0; the passes after the transmittance sample it at 1 with the
 * sampler at 2; the sky views and aerial perspective sample the multi scattering at 3, the
 * aerial perspective, the fog and the environment the sky view at 4.
 *
 * The 2D LUTs and the fog color are written at 5, the aerial perspective volume at 6, the
 * cubemap of an environment at 7.
 *
 * @param gpu The backend
 * @param a The atmosphere state
 * @param pass The pass
 */
static void box_gpu_atmos_layout(
    box_gpu_t *gpu,
    box_gpu_atmos_t *a,
    box_render_atmos_pass_t pass)
{
    WGPUShaderStage comp = WGPUShaderStage_Compute;
    WGPUTextureBindingLayout sampled = {
        .sampleType = WGPUTextureSampleType_Float,
        .viewDimension = WGPUTextureViewDimension_2D
    };
    bool sky = pass == BOX_RENDER_ATMOS_SKY || pass == BOX_RENDER_ATMOS_SCENE_SKY;
    WGPUBindGroupLayoutEntry entries[6];
    uint32_t count = 0;
    entries[count ++] = (WGPUBindGroupLayoutEntry){ .binding = 0, .visibility = comp,
        .buffer = { .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(box_gpu_atmos_uniforms_t) } };
    if (pass != BOX_RENDER_ATMOS_TRANSMITTANCE) {
        entries[count ++] = (WGPUBindGroupLayoutEntry){ .binding = 1, .visibility = comp,
            .texture = sampled };
        entries[count ++] = (WGPUBindGroupLayoutEntry){ .binding = 2, .visibility = comp,
            .sampler = { .type = WGPUSamplerBindingType_Filtering } };
    }

    if (sky || pass == BOX_RENDER_ATMOS_AERIAL) {
        entries[count ++] = (WGPUBindGroupLayoutEntry){ .binding = 3, .visibility = comp,
            .texture = sampled };
    }

    if (pass == BOX_RENDER_ATMOS_AERIAL || pass == BOX_RENDER_ATMOS_FOG ||
        pass == BOX_RENDER_ATMOS_ENVIRONMENT)
    {
        entries[count ++] = (WGPUBindGroupLayoutEntry){ .binding = 4, .visibility = comp,
            .texture = sampled };
    }

    if (pass == BOX_RENDER_ATMOS_ENVIRONMENT) {
        entries[count ++] = (WGPUBindGroupLayoutEntry){ .binding = 7, .visibility = comp,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = BOX_GPU_ENVIRONMENT_FORMAT,
                .viewDimension = WGPUTextureViewDimension_2DArray
            } };
    } else if (pass == BOX_RENDER_ATMOS_AERIAL) {
        entries[count ++] = (WGPUBindGroupLayoutEntry){ .binding = 6, .visibility = comp,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = BOX_GPU_ATMOS_FORMAT,
                .viewDimension = WGPUTextureViewDimension_3D
            } };
    } else {
        entries[count ++] = (WGPUBindGroupLayoutEntry){ .binding = 5, .visibility = comp,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = BOX_GPU_ATMOS_FORMAT,
                .viewDimension = WGPUTextureViewDimension_2D
            } };
    }

    a->layouts[pass] = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = count, .entries = entries });
    a->pipeline_layouts[pass] = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &a->layouts[pass] });
}

/** Create the bind group layout and pipeline layout of the sky draw: the uniforms of the view
 * at 0, the transmittance LUT of the sun disk at 1, the sampler at 2 and the sky view LUT at 4,
 * as the aerial perspective pass binds them.
 *
 * The cloud layers add their repeating sampler at 8 and their textures from
 * BOX_GPU_ATMOS_CLOUD_BINDING.
 *
 * @param gpu The backend
 * @param a The atmosphere state
 */
static void box_gpu_atmos_sky_layout(
    box_gpu_t *gpu,
    box_gpu_atmos_t *a)
{
    WGPUShaderStage frag = WGPUShaderStage_Fragment;
    WGPUBindGroupLayoutEntry entries[5 + BOX_CLOUD_LAYER_MAX] = {
        { .binding = 0, .visibility = frag, .buffer = { .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(box_gpu_atmos_uniforms_t) } },
        { .binding = 1, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D
        } },
        { .binding = 2, .visibility = frag,
            .sampler = { .type = WGPUSamplerBindingType_Filtering } },
        { .binding = 4, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D
        } },
        { .binding = 8, .visibility = frag,
            .sampler = { .type = WGPUSamplerBindingType_Filtering } }
    };
    int32_t i;
    for (i = 0; i < BOX_CLOUD_LAYER_MAX; i ++) {
        entries[5 + i] = (WGPUBindGroupLayoutEntry){
            .binding = (uint32_t)(BOX_GPU_ATMOS_CLOUD_BINDING + i),
            .visibility = frag,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        };
    }

    a->sky_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 5 + BOX_CLOUD_LAYER_MAX,
            .entries = entries });
    a->sky_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &a->sky_layout });
}

/** Release the sky pipelines of every color format.
 *
 * @param a The atmosphere state
 */
static void box_gpu_atmos_sky_reset(
    box_gpu_atmos_t *a)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&a->sky_pipelines); i ++) {
        box_gpu_blit_t *b = ecs_vec_get_t(&a->sky_pipelines, box_gpu_blit_t, i);
        if (b->pipeline) {
            wgpuRenderPipelineRelease(b->pipeline);
        }
    }

    ecs_vec_clear(&a->sky_pipelines);
}

/** Create the timestamp queries and their readbacks when the device has the feature.
 *
 * @param gpu The backend
 * @param a The atmosphere state
 */
static void box_gpu_atmos_queries_init(
    box_gpu_t *gpu,
    box_gpu_atmos_t *a)
{
    int32_t i;
    a->current = -1;
    if (!gpu->timestamps) {
        goto done;
    }

    a->queries = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
        .type = WGPUQueryType_Timestamp,
        .count = BOX_GPU_ATMOS_STAMPS * BOX_GPU_ATMOS_READBACKS
    });
    a->resolve = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = BOX_GPU_ATMOS_STAMP_BYTES * BOX_GPU_ATMOS_READBACKS
    });
    for (i = 0; i < BOX_GPU_ATMOS_READBACKS; i ++) {
        a->readbacks[i].buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = BOX_GPU_ATMOS_STAMP_BYTES
        });
    }

done:
    return;
}

void box_gpu_atmosphere_init(
    box_gpu_t *gpu)
{
    box_gpu_atmos_t *a = ecs_os_calloc_t(box_gpu_atmos_t);
    int32_t i;
    for (i = 0; i < BOX_RENDER_ATMOS_PASSES; i ++) {
        box_gpu_atmos_layout(gpu, a, (box_render_atmos_pass_t)i);
    }

    a->sampler = wgpuDeviceCreateSampler(gpu->device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 32,
        .maxAnisotropy = 1
    });
    ecs_map_init(&a->luts, NULL);
    ecs_vec_init_t(NULL, &a->views, box_gpu_atmos_view_t*, 0);
    ecs_vec_init_t(NULL, &a->sky_pipelines, box_gpu_blit_t, 0);
    ecs_vec_init_t(NULL, &a->env_requests, box_gpu_atmos_env_request_t, 0);
    box_gpu_atmos_sky_layout(gpu, a);
    a->placeholder = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_3D,
        .size = { 1, 1, 1 },
        .format = BOX_GPU_ATMOS_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    a->placeholder_view = wgpuTextureCreateView(a->placeholder, NULL);
    box_gpu_atmos_queries_init(gpu, a);
    gpu->atmosphere = a;
}

/** Release the LUTs of a scene.
 *
 * @param gpu The backend
 * @param l The LUTs
 */
static void box_gpu_atmos_luts_free(
    box_gpu_t *gpu,
    box_gpu_atmos_luts_t *l)
{
    box_gpu_atmos_release(&l->transmittance, &l->transmittance_view);
    box_gpu_atmos_release(&l->multi, &l->multi_view);
    box_gpu_atmos_release(&l->env_sky, &l->env_sky_view);
    if (l->env_uniforms) {
        wgpuBufferRelease(l->env_uniforms);
    }

    wgpuBufferRelease(l->uniforms);
    ecs_os_free(l);
    gpu->pending.atmosphere_luts --;
}

/** Release the sky view and aerial perspective of a view.
 *
 * @param gpu The backend
 * @param v The view state
 */
static void box_gpu_atmos_view_free(
    box_gpu_t *gpu,
    box_gpu_atmos_view_t *v)
{
    box_gpu_atmos_release(&v->sky, &v->sky_view);
    box_gpu_atmos_release(&v->aerial, &v->aerial_view);
    box_gpu_atmos_release(&v->fog, &v->fog_view);
    wgpuBufferRelease(v->uniforms);
    ecs_os_free(v);
    gpu->pending.atmosphere_views --;
}

void box_gpu_atmosphere_fini(
    box_gpu_t *gpu)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    int32_t i;
    if (!a) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&a->luts);
    while (ecs_map_next(&it)) {
        box_gpu_atmos_luts_free(gpu, ecs_map_ptr(&it));
    }

    for (i = 0; i < ecs_vec_count(&a->views); i ++) {
        box_gpu_atmos_view_free(gpu, *ecs_vec_get_t(&a->views, box_gpu_atmos_view_t*, i));
    }

    for (i = 0; i < BOX_GPU_ATMOS_READBACKS; i ++) {
        box_gpu_atmos_readback_t *r = &a->readbacks[i];
        if (r->state == BOX_GPU_ATMOS_MAPPED) {
            wgpuBufferUnmap(r->buffer);
        }

        if (r->buffer) {
            wgpuBufferRelease(r->buffer);
        }
    }

    for (i = 0; i < BOX_RENDER_ATMOS_PASSES; i ++) {
        if (a->pipelines[i]) {
            wgpuComputePipelineRelease(a->pipelines[i]);
        }

        wgpuPipelineLayoutRelease(a->pipeline_layouts[i]);
        wgpuBindGroupLayoutRelease(a->layouts[i]);
    }

    if (a->queries) {
        wgpuQuerySetRelease(a->queries);
    }

    if (a->resolve) {
        wgpuBufferRelease(a->resolve);
    }

    box_gpu_atmos_sky_reset(a);
    ecs_vec_fini_t(NULL, &a->sky_pipelines, box_gpu_blit_t);
    wgpuPipelineLayoutRelease(a->sky_pipeline_layout);
    wgpuBindGroupLayoutRelease(a->sky_layout);
    box_gpu_atmos_release(&a->placeholder, &a->placeholder_view);
    wgpuSamplerRelease(a->sampler);
    ecs_map_fini(&a->luts);
    ecs_vec_fini_t(NULL, &a->views, box_gpu_atmos_view_t*);
    ecs_vec_fini_t(NULL, &a->env_requests, box_gpu_atmos_env_request_t);
    ecs_os_free(a);
    gpu->atmosphere = NULL;
done:
    return;
}

void box_gpu_atmosphere_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_atmosphere_desc_t *desc)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    box_gpu_atmos_luts_t *l = a
        ? ecs_map_get_deref(&a->luts, box_gpu_atmos_luts_t, handle)
        : NULL;
    box_gpu_atmos_uniforms_t u;
    if (!a) {
        goto done;
    }

    if (op == BOX_RENDER_DESTROY || !desc) {
        if (l) {
            box_gpu_atmos_luts_free(gpu, l);
            ecs_map_remove(&a->luts, handle);
        }

        goto done;
    }

    if (!l) {
        l = ecs_os_calloc_t(box_gpu_atmos_luts_t);
        l->uniforms = box_gpu_atmos_uniform_buffer(gpu);
        l->transmittance = box_gpu_atmos_texture(gpu, BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH,
            BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT, 1, &l->transmittance_view);
        l->multi = box_gpu_atmos_texture(gpu, BOX_RENDER_ATMOS_MULTI_SIZE,
            BOX_RENDER_ATMOS_MULTI_SIZE, 1, &l->multi_view);
        ecs_map_insert_ptr(&a->luts, handle, l);
        gpu->pending.atmosphere_luts ++;
    }

    ecs_os_memset_t(&u, 0, box_gpu_atmos_uniforms_t);
    u.medium = *desc;
    l->medium = *desc;
    l->dirty = true;
    wgpuQueueWriteBuffer(gpu->queue, l->uniforms, 0, &u, sizeof(u));
done:
    return;
}

/** Get the pipelines of the passes, created again when the atmosphere shader was recompiled;
 * the previous ones stay until every new one exists.
 *
 * @param gpu The backend
 * @param a The atmosphere state
 * @return Whether every pipeline exists
 */
static bool box_gpu_atmos_pipelines(
    box_gpu_t *gpu,
    box_gpu_atmos_t *a)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_ATMOSPHERE);
    bool result = false;
    int32_t i;
    if (!s || !s->module) {
        goto done;
    }

    if (a->revision != s->revision || !a->pipelines[0]) {
        WGPUComputePipeline next[BOX_RENDER_ATMOS_PASSES];
        WGPUComputePipeline *current[BOX_RENDER_ATMOS_PASSES];
        for (i = 0; i < BOX_RENDER_ATMOS_PASSES; i ++) {
            current[i] = &a->pipelines[i];
            next[i] = box_gpu_compute_pipeline(gpu,
                &(WGPUComputePipelineDescriptor){
                    .layout = a->pipeline_layouts[i],
                    .compute = {
                        .module = s->module,
                        .entryPoint = { .data = box_gpu_atmos_entries[i], .length = WGPU_STRLEN }
                    }
                });
        }

        if (box_gpu_compute_pipelines_swap(current, next, BOX_RENDER_ATMOS_PASSES)) {
            a->revision = s->revision;
        }
    }

    result = true;
    for (i = 0; i < BOX_RENDER_ATMOS_PASSES; i ++) {
        result = result && a->pipelines[i] != NULL;
    }

done:
    return result;
}

/** Get the nanoseconds of one tick of the timestamps.
 *
 * @param gpu The backend
 * @return The period, 1 in the browser, which resolves timestamps in nanoseconds
 */
static double box_gpu_atmos_period(
    box_gpu_t *gpu)
{
#ifdef __EMSCRIPTEN__
    (void)gpu;

    return 1.0;
#else
    return (double)wgpuQueueGetTimestampPeriod(gpu->queue);
#endif
}

/** Record the outcome of the map of a timestamp readback.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_atmos_readback_t
 * @param userdata2 Unused
 */
static void box_gpu_atmos_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_atmos_readback_t *r = userdata1;
    (void)message;
    (void)userdata2;
    if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        r->state = status == WGPUMapAsyncStatus_Success
            ? BOX_GPU_ATMOS_MAPPED
            : BOX_GPU_ATMOS_FAILED;
    }
}

/** Add the times of the passes of a mapped readback to the counters of the backend.
 *
 * @param gpu The backend
 * @param r The readback
 */
static void box_gpu_atmos_measured(
    box_gpu_t *gpu,
    box_gpu_atmos_readback_t *r)
{
    const uint64_t *t = wgpuBufferGetConstMappedRange(r->buffer, 0, BOX_GPU_ATMOS_STAMP_BYTES);
    double period = box_gpu_atmos_period(gpu);
    uint32_t i;
    for (i = 0; t && i + 1 < r->count; i += 2) {
        double ns = (double)(t[i + 1] - t[i]) * period;
        if (t[i + 1] > t[i] && ns < BOX_GPU_ATMOS_MEASURE_MAX) {
            gpu->pending.atmosphere_ns[r->kinds[i / 2]] += (int64_t)ns;
            gpu->pending.atmosphere_passes[r->kinds[i / 2]] ++;
        }
    }
}

/** Advance the timestamp readbacks by a packet: publish the mapped ones, map the copied ones,
 * resolve the ones the packet before the last one wrote and pick one for this packet.
 *
 * Metal writes the timestamps of a pass when the pass completed, which a resolve in the same or
 * the next command buffer does not wait for, so the resolve waits for one more packet.
 *
 * @param gpu The backend
 * @param a The atmosphere state
 */
static void box_gpu_atmos_stamps(
    box_gpu_t *gpu,
    box_gpu_atmos_t *a)
{
    WGPUCommandEncoder encoder = NULL;
    int32_t i;
    if (!a->queries) {
        goto done;
    }

#ifndef __EMSCRIPTEN__
    for (i = 0; i < BOX_GPU_ATMOS_READBACKS; i ++) {
        if (a->readbacks[i].state == BOX_GPU_ATMOS_MAPPING) {
            wgpuDevicePoll(gpu->device, false, NULL);
            break;
        }
    }
#endif

    for (i = 0; i < BOX_GPU_ATMOS_READBACKS; i ++) {
        box_gpu_atmos_readback_t *r = &a->readbacks[i];
        uint64_t offset = (uint64_t)i * BOX_GPU_ATMOS_STAMP_BYTES;
        if (r->state == BOX_GPU_ATMOS_MAPPED) {
            box_gpu_atmos_measured(gpu, r);
            wgpuBufferUnmap(r->buffer);
            r->state = BOX_GPU_ATMOS_FREE;
        } else if (r->state == BOX_GPU_ATMOS_FAILED) {
            r->state = BOX_GPU_ATMOS_FREE;
        } else if (r->state == BOX_GPU_ATMOS_COPIED) {
            r->state = BOX_GPU_ATMOS_MAPPING;
            wgpuBufferMapAsync(r->buffer, WGPUMapMode_Read, 0, BOX_GPU_ATMOS_STAMP_BYTES,
                (WGPUBufferMapCallbackInfo){
                    .mode = WGPUCallbackMode_AllowSpontaneous,
                    .callback = box_gpu_atmos_on_map,
                    .userdata1 = r
                });
        } else if (r->state == BOX_GPU_ATMOS_WRITTEN) {
            if (!encoder) {
                encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
            }

            wgpuCommandEncoderResolveQuerySet(encoder, a->queries,
                (uint32_t)i * BOX_GPU_ATMOS_STAMPS, r->count, a->resolve, offset);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, a->resolve, offset, r->buffer, 0,
                (uint64_t)r->count * sizeof(uint64_t));
            r->state = BOX_GPU_ATMOS_COPIED;
        }
    }

    if (encoder) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

    if (a->current != -1) {
        box_gpu_atmos_readback_t *r = &a->readbacks[a->current];
        r->state = r->count ? BOX_GPU_ATMOS_WRITTEN : BOX_GPU_ATMOS_FREE;
        a->current = -1;
    }

    for (i = 0; i < BOX_GPU_ATMOS_READBACKS; i ++) {
        if (a->readbacks[i].state == BOX_GPU_ATMOS_FREE) {
            a->readbacks[i].state = BOX_GPU_ATMOS_RECORDING;
            a->readbacks[i].count = 0;
            a->current = i;
            break;
        }
    }

done:
    return;
}

/** Begin a compute pass of the atmosphere, timed when there is room for its timestamps.
 *
 * @param a The atmosphere state
 * @param encoder The encoder
 * @param pass The pass
 * @return The compute pass, with the pipeline of the pass set
 */
static WGPUComputePassEncoder box_gpu_atmos_begin(
    box_gpu_atmos_t *a,
    WGPUCommandEncoder encoder,
    box_render_atmos_pass_t pass)
{
    box_gpu_atmos_readback_t *r = a->current != -1 ? &a->readbacks[a->current] : NULL;
    WGPUPassTimestampWrites writes;
    const WGPUPassTimestampWrites *timed = NULL;
    if (r && r->count + 2 <= BOX_GPU_ATMOS_STAMPS) {
        uint32_t first = (uint32_t)a->current * BOX_GPU_ATMOS_STAMPS + r->count;
        writes = (WGPUPassTimestampWrites){
            .querySet = a->queries,
            .beginningOfPassWriteIndex = first,
            .endOfPassWriteIndex = first + 1
        };
        timed = &writes;
        r->kinds[r->count / 2] = (uint8_t)pass;
        r->count += 2;
    }

    WGPUComputePassEncoder result = box_gpu_compute_begin(encoder, timed);
    wgpuComputePassEncoderSetPipeline(result, a->pipelines[pass]);

    return result;
}

/** Record one dispatch of an atmosphere pass over a texture, a workgroup per 8x8 texels.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param pass The pass
 * @param entries The bindings of the pass
 * @param count Number of bindings
 * @param width Texels along x
 * @param height Texels along y
 * @param depth Texels along z
 */
static void box_gpu_atmos_dispatch(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    box_render_atmos_pass_t pass,
    const WGPUBindGroupEntry *entries,
    uint32_t count,
    uint32_t width,
    uint32_t height,
    uint32_t depth)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = a->layouts[pass],
        .entryCount = count,
        .entries = entries
    });
    WGPUComputePassEncoder cp = box_gpu_atmos_begin(a, encoder, pass);
    wgpuComputePassEncoderSetBindGroup(cp, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(cp,
        (width + BOX_GPU_ATMOS_GROUP - 1) / BOX_GPU_ATMOS_GROUP,
        (height + BOX_GPU_ATMOS_GROUP - 1) / BOX_GPU_ATMOS_GROUP, depth);
    wgpuComputePassEncoderEnd(cp);
    wgpuComputePassEncoderRelease(cp);
    wgpuBindGroupRelease(group);
}

/** Record the transmittance and then the multi scattering LUT of a scene.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param l The LUTs
 */
static void box_gpu_atmos_build(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    box_gpu_atmos_luts_t *l)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    uint64_t size = sizeof(box_gpu_atmos_uniforms_t);
    WGPUBindGroupEntry transmittance[2] = {
        { .binding = 0, .buffer = l->uniforms, .size = size },
        { .binding = 5, .textureView = l->transmittance_view }
    };
    WGPUBindGroupEntry multi[4] = {
        { .binding = 0, .buffer = l->uniforms, .size = size },
        { .binding = 1, .textureView = l->transmittance_view },
        { .binding = 2, .sampler = a->sampler },
        { .binding = 5, .textureView = l->multi_view }
    };
    box_gpu_atmos_dispatch(gpu, encoder, BOX_RENDER_ATMOS_TRANSMITTANCE, transmittance, 2,
        BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH, BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT, 1);
    box_gpu_atmos_dispatch(gpu, encoder, BOX_RENDER_ATMOS_MULTI, multi, 4,
        BOX_RENDER_ATMOS_MULTI_SIZE, BOX_RENDER_ATMOS_MULTI_SIZE, 1);
    l->dirty = false;
    l->revision = a->revision;
    gpu->pending.atmosphere_builds ++;
}

void box_gpu_atmosphere_build(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    WGPUCommandEncoder encoder = NULL;
    (void)packet;
    if (!a) {
        goto done;
    }

    box_gpu_atmos_stamps(gpu, a);
    if (!ecs_map_count(&a->luts) || !box_gpu_atmos_pipelines(gpu, a)) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&a->luts);
    while (ecs_map_next(&it)) {
        box_gpu_atmos_luts_t *l = ecs_map_ptr(&it);
        if (!l->dirty && l->revision == a->revision) {
            continue;
        }

        if (!encoder) {
            encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        }

        box_gpu_atmos_build(gpu, encoder, l);
    }

    if (encoder) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

done:
    return;
}

void box_gpu_atmosphere_environment_apply(
    box_gpu_t *gpu,
    uint32_t handle,
    const box_render_atmosphere_env_t *desc)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    int32_t i;
    if (!a) {
        goto done;
    }

    for (i = ecs_vec_count(&a->env_requests) - 1; i >= 0; i --) {
        if (ecs_vec_get_t(&a->env_requests, box_gpu_atmos_env_request_t, i)->handle == handle) {
            ecs_vec_remove_t(&a->env_requests, box_gpu_atmos_env_request_t, i);
        }
    }

    if (desc) {
        *ecs_vec_append_t(NULL, &a->env_requests, box_gpu_atmos_env_request_t) =
            (box_gpu_atmos_env_request_t){ .handle = handle, .desc = *desc };
    }

done:
    return;
}

/** Record the sky view LUT of a scene at its ground altitude and mip 0 of the cubemap of an
 * environment from it. The uniforms are written at once, so the commands are submitted
 * before the next request of the LUTs is recorded.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param l The LUTs of the scene, built with the current pipelines
 * @param in The request
 * @param target The target of the cubemap
 * @param res Face edge of the cubemap in texels
 */
static void box_gpu_atmos_env_record(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    box_gpu_atmos_luts_t *l,
    const box_render_atmosphere_env_t *in,
    WGPUTextureView target,
    uint32_t res)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    uint64_t size = sizeof(box_gpu_atmos_uniforms_t);
    box_gpu_atmos_uniforms_t u;
    if (!l->env_uniforms) {
        l->env_uniforms = box_gpu_atmos_uniform_buffer(gpu);
        l->env_sky = box_gpu_atmos_texture(gpu, BOX_RENDER_ATMOS_SKY_WIDTH,
            BOX_RENDER_ATMOS_SKY_HEIGHT, 1, &l->env_sky_view);
    }

    ecs_os_memset_t(&u, 0, box_gpu_atmos_uniforms_t);
    u.medium = l->medium;
    ecs_os_memcpy_n(u.sun, in->sun, float, 4);
    ecs_os_memcpy_n(u.sun_color, in->sun_color, float, 4);
    ecs_os_memcpy_n(u.moon, in->moon, float, 4);
    ecs_os_memcpy_n(u.moon_color, in->moon_color, float, 4);
    ecs_os_memcpy_n(u.night, in->night, float, 4);
    u.view[0] = in->altitude;
    u.view[1] = in->mie_anisotropy;
    wgpuQueueWriteBuffer(gpu->queue, l->env_uniforms, 0, &u, sizeof(u));

    WGPUBindGroupEntry sky[5] = {
        { .binding = 0, .buffer = l->env_uniforms, .size = size },
        { .binding = 1, .textureView = l->transmittance_view },
        { .binding = 2, .sampler = a->sampler },
        { .binding = 3, .textureView = l->multi_view },
        { .binding = 5, .textureView = l->env_sky_view }
    };
    WGPUBindGroupEntry cube[5] = {
        { .binding = 0, .buffer = l->env_uniforms, .size = size },
        { .binding = 1, .textureView = l->transmittance_view },
        { .binding = 2, .sampler = a->sampler },
        { .binding = 4, .textureView = l->env_sky_view },
        { .binding = 7, .textureView = target }
    };
    box_gpu_atmos_dispatch(gpu, encoder, BOX_RENDER_ATMOS_SCENE_SKY, sky, 5,
        BOX_RENDER_ATMOS_SKY_WIDTH, BOX_RENDER_ATMOS_SKY_HEIGHT, 1);
    box_gpu_atmos_dispatch(gpu, encoder, BOX_RENDER_ATMOS_ENVIRONMENT, cube, 5, res, res, 6);
}

void box_gpu_atmosphere_environment(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    bool ready = false;
    int32_t i;
    (void)packet;
    if (!a || !ecs_vec_count(&a->env_requests)) {
        goto done;
    }

    ready = box_gpu_atmos_pipelines(gpu, a) && box_gpu_environment_ready(gpu);
    for (i = ecs_vec_count(&a->env_requests) - 1; i >= 0; i --) {
        box_gpu_atmos_env_request_t *r = ecs_vec_get_t(
            &a->env_requests, box_gpu_atmos_env_request_t, i);
        box_gpu_atmos_luts_t *l = ecs_map_get_deref(
            &a->luts, box_gpu_atmos_luts_t, r->desc.atmosphere);
        uint32_t res = 0;
        WGPUTextureView target = box_gpu_environment_target(gpu, r->handle, &res);
        bool built = l && !l->dirty && l->revision == a->revision && a->revision;
        if (target && l && (!ready || !built)) {
            continue;
        }

        if (target && l) {
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
            box_gpu_atmos_env_record(gpu, encoder, l, &r->desc, target, res);
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
            wgpuQueueSubmit(gpu->queue, 1, &commands);
            wgpuCommandBufferRelease(commands);
            wgpuCommandEncoderRelease(encoder);
            box_gpu_environment_written(gpu, r->handle, r->desc.version);
        }

        ecs_vec_remove_t(&a->env_requests, box_gpu_atmos_env_request_t, i);
    }

done:
    return;
}

/** Find the sky view and aerial perspective of a view in a target.
 *
 * @param a The atmosphere state
 * @param t The target
 * @param v The view
 * @return The state, NULL when the view has none in the target
 */
static box_gpu_atmos_view_t* box_gpu_atmos_view_find(
    const box_gpu_atmos_t *a,
    const box_gpu_target_t *t,
    const box_render_view_t *v)
{
    box_gpu_atmos_view_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&a->views); i ++) {
        box_gpu_atmos_view_t *s = *ecs_vec_get_t(&a->views, box_gpu_atmos_view_t*, i);
        if (s->target == t->handle && s->view == v->view) {
            result = s;
            break;
        }
    }

    return result;
}

/** Get the LUTs of the scene of a view when they are built with the current pipelines.
 *
 * @param a The atmosphere state, NULL without one
 * @param v The view
 * @return The LUTs, NULL when the view runs no atmosphere pass
 */
static box_gpu_atmos_luts_t* box_gpu_atmos_luts_ready(
    const box_gpu_atmos_t *a,
    const box_render_view_t *v)
{
    box_gpu_atmos_luts_t *result = a && v->atmosphere
        ? ecs_map_get_deref(&a->luts, box_gpu_atmos_luts_t, v->atmosphere)
        : NULL;
    if (result && (result->dirty || result->revision != a->revision || !a->revision)) {
        result = NULL;
    }

    return result;
}

/** Get the sky view and aerial perspective of a view in a target, created on first use.
 *
 * @param gpu The backend
 * @param t The target
 * @param packet The packet
 * @param v The view
 * @return The state, marked as used by the packet
 */
static box_gpu_atmos_view_t* box_gpu_atmos_view(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    box_gpu_atmos_view_t *result = box_gpu_atmos_view_find(a, t, v);
    if (!result) {
        result = ecs_os_calloc_t(box_gpu_atmos_view_t);
        result->target = t->handle;
        result->view = v->view;
        result->uniforms = box_gpu_atmos_uniform_buffer(gpu);
        result->sky = box_gpu_atmos_texture(gpu, BOX_RENDER_ATMOS_SKY_WIDTH,
            BOX_RENDER_ATMOS_SKY_HEIGHT, 1, &result->sky_view);
        result->aerial = box_gpu_atmos_texture(gpu, BOX_RENDER_ATMOS_AERIAL_SIZE,
            BOX_RENDER_ATMOS_AERIAL_SIZE, BOX_RENDER_ATMOS_AERIAL_SIZE, &result->aerial_view);
        *ecs_vec_append_t(NULL, &a->views, box_gpu_atmos_view_t*) = result;
        gpu->pending.atmosphere_views ++;
    }

    result->frame = packet->frame;

    return result;
}

/** Check whether a view runs a HeightFog that takes its color from the atmosphere it renders.
 *
 * @param packet The packet
 * @param v The view
 * @return Whether one of its effect passes is such a fog
 */
static bool box_gpu_atmos_has_fog(
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    bool result = false;
    int32_t i;
    if (!v->atmos.sky || v->effect_first + v->effect_count > ecs_vec_count(&packet->effects)) {
        goto done;
    }

    for (i = 0; i < v->effect_count && !result; i ++) {
        const box_render_effect_t *e = ecs_vec_get_t(
            &packet->effects, box_render_effect_t, v->effect_first + i);
        result = e->kind == BOX_RENDER_EFFECT_FOG && e->fog.atmosphere &&
            e->fog.atmosphere == v->atmos.entity;
    }

done:
    return result;
}

/** Write the cloud layers of a view into its uniforms: those whose texture the backend has, in
 * the order of the view, and remember their textures for the sky draw.
 *
 * @param gpu The backend
 * @param v The view
 * @param s The view state
 * @param u The uniforms
 */
static void box_gpu_atmos_clouds(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_atmos_view_t *s,
    box_gpu_atmos_uniforms_t *u)
{
    const box_render_view_atmos_t *in = &v->atmos;
    int32_t i, count = 0;
    ecs_os_memset_n(s->clouds, 0, uint32_t, BOX_CLOUD_LAYER_MAX);
    for (i = 0; in->sky && i < in->cloud_count && i < BOX_CLOUD_LAYER_MAX; i ++) {
        const box_render_cloud_layer_t *l = &in->clouds[i];
        if (!box_gpu_texture_cookie(gpu, l->texture)) {
            continue;
        }

        box_gpu_atmos_cloud_t *c = &u->cloud_layers[count];
        c->plane[0] = l->height;
        c->plane[1] = l->inv_size;
        c->plane[2] = l->origin[0];
        c->plane[3] = l->origin[1];
        c->shape[0] = l->threshold;
        c->shape[1] = l->softness;
        c->shape[2] = l->density;
        glm_vec3_copy(ECS_CONST_CAST(float*, l->tint), c->tint);
        s->clouds[count ++] = l->texture;
    }

    u->clouds[0] = (float)count;
    u->clouds[1] = in->km_per_unit;
}

/** Record the pass that writes the fog in scatter color of a view from its sky view.
 *
 * @param gpu The backend
 * @param encoder The encoder
 * @param packet The packet
 * @param l The LUTs of the scene of the view
 * @param s The view state, its sky view written
 */
static void box_gpu_atmos_fog(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_gpu_atmos_luts_t *l,
    box_gpu_atmos_view_t *s)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    if (!s->fog) {
        s->fog = box_gpu_atmos_texture(gpu, BOX_RENDER_ATMOS_FOG_WIDTH, 1, 1, &s->fog_view);
    }

    WGPUBindGroupEntry entries[5] = {
        { .binding = 0, .buffer = s->uniforms, .size = sizeof(box_gpu_atmos_uniforms_t) },
        { .binding = 1, .textureView = l->transmittance_view },
        { .binding = 2, .sampler = a->sampler },
        { .binding = 4, .textureView = s->sky_view },
        { .binding = 5, .textureView = s->fog_view }
    };
    box_gpu_atmos_dispatch(gpu, encoder, BOX_RENDER_ATMOS_FOG, entries, 5,
        BOX_RENDER_ATMOS_FOG_WIDTH, 1, 1);
    s->fog_frame = packet->frame;
}

void box_gpu_atmosphere_view(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    box_gpu_atmos_luts_t *l = box_gpu_atmos_luts_ready(a, v);
    box_gpu_atmos_uniforms_t u;
    mat4 proj, view_proj;
    if (!l) {
        goto done;
    }

    box_gpu_atmos_view_t *s = box_gpu_atmos_view(gpu, t, packet, v);
    const box_render_view_atmos_t *in = &v->atmos;
    ecs_os_memset_t(&u, 0, box_gpu_atmos_uniforms_t);
    u.medium = l->medium;
    ecs_os_memcpy_n(u.sun, in->sun, float, 4);
    ecs_os_memcpy_n(u.sun_color, in->sun_color, float, 4);
    ecs_os_memcpy_n(u.night, in->night, float, 4);
    ecs_os_memcpy_n(u.sun_disk, in->sun_disk, float, 4);
    ecs_os_memcpy_n(u.moon, in->moon, float, 4);
    ecs_os_memcpy_n(u.moon_color, in->moon_color, float, 4);
    ecs_os_memcpy_n(u.moon_disk, in->moon_disk, float, 4);
    ecs_os_memcpy_n(u.fog_light, in->fog_light, float, 4);
    ecs_os_memcpy_n(u.fog_moon, in->fog_moon, float, 4);
    u.view[0] = in->altitude;
    u.view[1] = in->mie_anisotropy;
    u.view[2] = in->aerial_distance;
    u.view[3] = in->aerial_intensity;
    box_camera_projection(&v->camera_data,
        (float)t->desc.point_width, (float)t->desc.point_height, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), view_proj);
    glm_mat4_inv(view_proj, u.inv_view_proj);
    box_gpu_atmos_clouds(gpu, v, s, &u);
    wgpuQueueWriteBuffer(gpu->queue, s->uniforms, 0, &u, sizeof(u));

    uint64_t size = sizeof(box_gpu_atmos_uniforms_t);
    WGPUBindGroupEntry sky[5] = {
        { .binding = 0, .buffer = s->uniforms, .size = size },
        { .binding = 1, .textureView = l->transmittance_view },
        { .binding = 2, .sampler = a->sampler },
        { .binding = 3, .textureView = l->multi_view },
        { .binding = 5, .textureView = s->sky_view }
    };
    WGPUBindGroupEntry aerial[6] = {
        { .binding = 0, .buffer = s->uniforms, .size = size },
        { .binding = 1, .textureView = l->transmittance_view },
        { .binding = 2, .sampler = a->sampler },
        { .binding = 3, .textureView = l->multi_view },
        { .binding = 4, .textureView = s->sky_view },
        { .binding = 6, .textureView = s->aerial_view }
    };
    box_gpu_atmos_dispatch(gpu, encoder, BOX_RENDER_ATMOS_SKY, sky, 5,
        BOX_RENDER_ATMOS_SKY_WIDTH, BOX_RENDER_ATMOS_SKY_HEIGHT, 1);
    box_gpu_atmos_dispatch(gpu, encoder, BOX_RENDER_ATMOS_AERIAL, aerial, 6,
        BOX_RENDER_ATMOS_AERIAL_SIZE, BOX_RENDER_ATMOS_AERIAL_SIZE,
        BOX_RENDER_ATMOS_AERIAL_SIZE);
    if (box_gpu_atmos_has_fog(packet, v)) {
        box_gpu_atmos_fog(gpu, encoder, packet, l, s);
    }

done:
    return;
}

bool box_gpu_atmosphere_sky(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    return v->atmos.sky && box_gpu_atmos_luts_ready(gpu->atmosphere, v) != NULL;
}

WGPUTextureView box_gpu_atmosphere_fog(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    box_gpu_atmos_view_t *s = a && box_gpu_atmosphere_sky(gpu, v)
        ? box_gpu_atmos_view_find(a, t, v)
        : NULL;

    return s && s->fog && s->fog_frame == packet->frame ? s->fog_view : NULL;
}

void box_gpu_atmosphere_uniforms(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u)
{
    const box_render_view_atmos_t *in = &v->atmos;
    glm_vec4_zero(u->aerial);
    if ((v->features & BOX_RENDER_FEATURE_AERIAL) && box_gpu_atmosphere_sky(gpu, v)) {
        u->aerial[0] = in->km_per_unit;
        u->aerial[1] = in->aerial_distance > 0 ? 1.0f / in->aerial_distance : 0.0f;
        u->aerial[2] = (float)BOX_RENDER_ATMOS_AERIAL_SIZE;
        u->aerial[3] = 1.0f;
    }
}

void box_gpu_atmosphere_bindings(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v,
    WGPUBindGroupEntry *out)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    box_gpu_atmos_view_t *s = t && box_gpu_atmosphere_sky(gpu, v)
        ? box_gpu_atmos_view_find(a, t, v)
        : NULL;
    out[0] = (WGPUBindGroupEntry){
        .binding = 9,
        .textureView = s ? s->aerial_view : a->placeholder_view
    };
    out[1] = (WGPUBindGroupEntry){ .binding = 10, .sampler = a->sampler };
}

/** Get the sky pipeline of a color format, created on first use and again after the atmosphere
 * shader recompiled.
 *
 * @param gpu The backend
 * @param a The atmosphere state
 * @param format The format of the color target
 * @return The pipeline, NULL while the shader has no module
 */
static WGPURenderPipeline box_gpu_atmos_sky_pipeline(
    box_gpu_t *gpu,
    box_gpu_atmos_t *a,
    WGPUTextureFormat format)
{
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_ATMOSPHERE);
    WGPURenderPipeline result = NULL;
    int32_t i;
    if (!shader || !shader->module) {
        goto done;
    }

    if (a->sky_revision != shader->revision) {
        box_gpu_atmos_sky_reset(a);
        a->sky_revision = shader->revision;
    }

    for (i = 0; i < ecs_vec_count(&a->sky_pipelines); i ++) {
        box_gpu_blit_t *b = ecs_vec_get_t(&a->sky_pipelines, box_gpu_blit_t, i);
        if (b->format == format) {
            result = b->pipeline;
            goto done;
        }
    }

    WGPUColorTargetState target = { .format = format, .writeMask = WGPUColorWriteMask_All };
    WGPUFragmentState fragment = {
        .module = shader->module,
        .entryPoint = { .data = "sky_fs", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_False,
        .depthCompare = WGPUCompareFunction_Equal,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    result = box_gpu_render_pipeline(gpu, &(WGPURenderPipelineDescriptor){
        .layout = a->sky_pipeline_layout,
        .vertex = {
            .module = shader->module,
            .entryPoint = { .data = "sky_vs", .length = WGPU_STRLEN }
        },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    });
    if (result) {
        *ecs_vec_append_t(NULL, &a->sky_pipelines, box_gpu_blit_t) = (box_gpu_blit_t){
            .format = format,
            .pipeline = result
        };
    }

done:
    return result;
}

bool box_gpu_atmosphere_draw_sky(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_view_t *v)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    box_gpu_atmos_luts_t *l = v->atmos.sky ? box_gpu_atmos_luts_ready(a, v) : NULL;
    box_gpu_atmos_view_t *s = l ? box_gpu_atmos_view_find(a, t, v) : NULL;
    WGPURenderPipeline pipeline = s ? box_gpu_atmos_sky_pipeline(gpu, a, format) : NULL;
    bool result = pipeline != NULL;
    if (!result) {
        goto done;
    }

    WGPUBindGroupEntry entries[5 + BOX_CLOUD_LAYER_MAX] = {
        { .binding = 0, .buffer = s->uniforms, .size = sizeof(box_gpu_atmos_uniforms_t) },
        { .binding = 1, .textureView = l->transmittance_view },
        { .binding = 2, .sampler = a->sampler },
        { .binding = 4, .textureView = s->sky_view },
        { .binding = 8, .sampler = box_gpu_texture_sampler(gpu, WrapRepeat,
            BOX_GPU_ATMOS_CLOUD_ANISOTROPY) }
    };
    int32_t i;
    for (i = 0; i < BOX_CLOUD_LAYER_MAX; i ++) {
        WGPUTextureView cloud = s->clouds[i]
            ? box_gpu_texture_cookie(gpu, s->clouds[i])
            : NULL;
        entries[5 + i] = (WGPUBindGroupEntry){
            .binding = (uint32_t)(BOX_GPU_ATMOS_CLOUD_BINDING + i),
            .textureView = cloud ? cloud : gpu->no_cookie->linear
        };
    }

    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = a->sky_layout,
        .entryCount = 5 + BOX_CLOUD_LAYER_MAX,
        .entries = entries
    });
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuBindGroupRelease(group);
done:
    return result;
}

void box_gpu_atmosphere_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_atmos_t *a = gpu->atmosphere;
    int32_t i;
    for (i = a ? ecs_vec_count(&a->views) - 1 : -1; i >= 0; i --) {
        box_gpu_atmos_view_t *s = *ecs_vec_get_t(&a->views, box_gpu_atmos_view_t*, i);
        if (s->frame != packet->frame) {
            box_gpu_atmos_view_free(gpu, s);
            ecs_vec_remove_t(&a->views, box_gpu_atmos_view_t*, i);
        }
    }
}

/** Convert a half float to a float.
 *
 * @param h The bits of the half
 * @return The value
 */
static float box_gpu_atmos_half(
    uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) << 31, exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ff;
    uint32_t bits;
    float result;
    if (!exponent) {
        result = (float)mantissa * (1.0f / 16777216.0f);
        result = sign ? -result : result;
        goto done;
    }

    bits = exponent == 0x1f
        ? sign | 0x7f800000u | (mantissa << 13)
        : sign | ((exponent + 112) << 23) | (mantissa << 13);
    ecs_os_memcpy(&result, &bits, ECS_SIZEOF(float));
done:
    return result;
}

bool box_render_gpu_read_atmosphere(
    ecs_world_t *world,
    uint32_t handle,
    box_render_atmos_pass_t lut,
    float *out,
    int32_t count)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    uint16_t *texels = NULL;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready ||
        (lut != BOX_RENDER_ATMOS_TRANSMITTANCE && lut != BOX_RENDER_ATMOS_MULTI))
    {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_read_atmosphere needs RenderSettings.main_thread and box_render_sync");
    box_gpu_atmos_luts_t *l = renders && gpu->atmosphere
        ? ecs_map_get_deref(&gpu->atmosphere->luts, box_gpu_atmos_luts_t, handle)
        : NULL;
    bool transmittance = lut == BOX_RENDER_ATMOS_TRANSMITTANCE;
    uint32_t width = transmittance
        ? BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH
        : BOX_RENDER_ATMOS_MULTI_SIZE;
    uint32_t height = transmittance
        ? BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT
        : BOX_RENDER_ATMOS_MULTI_SIZE;
    if (!l || !l->revision || (int64_t)width * height * 4 > count) {
        goto done;
    }

    uint32_t x, y, row = width * BOX_GPU_ATMOS_TEXEL;
    uint32_t stride = (row + BOX_GPU_ROW_ALIGN - 1) / BOX_GPU_ROW_ALIGN * BOX_GPU_ROW_ALIGN;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)stride * height
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = transmittance ? l->transmittance : l->multi,
            .aspect = WGPUTextureAspect_All
        },
        &(WGPUTexelCopyBufferInfo){
            .layout = { .bytesPerRow = stride, .rowsPerImage = height },
            .buffer = buffer
        },
        &(WGPUExtent3D){ width, height, 1 });
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    texels = ecs_os_malloc((ecs_size_t)(stride * height));
    result = box_gpu_read(gpu, buffer, 0, stride * height, texels);
    wgpuBufferRelease(buffer);
    for (y = 0; result && y < height; y ++) {
        const uint16_t *line = texels + (size_t)y * (stride / 2);
        for (x = 0; x < width * 4; x ++) {
            out[(size_t)y * width * 4 + x] = box_gpu_atmos_half(line[x]);
        }
    }

done:
    ecs_os_free(texels);

    return result;
}

#else

bool box_render_gpu_read_atmosphere(
    ecs_world_t *world,
    uint32_t handle,
    box_render_atmos_pass_t lut,
    float *out,
    int32_t count)
{
    (void)world;
    (void)handle;
    (void)lut;
    (void)out;
    (void)count;

    return false;
}

#endif
