/**
 * @file render/gpu_effects.c
 * @brief Effect passes of the webgpu backend: HDR targets, the effect chain and its state.
 *
 * A view with effects renders into an HDR image; its effects run in order as fullscreen passes
 * between two HDR images and the last pass writes the target. State kept across frames, such
 * as an adapted exposure or bloom history, is keyed by target, view and effect entity.
 */

#include "gpu.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

/** Color format of the edges SMAA finds */
#define BOX_GPU_SMAA_EDGES_FORMAT (WGPUTextureFormat_RG8Unorm)

/** Color format of the blending weights of SMAA */
#define BOX_GPU_SMAA_WEIGHTS_FORMAT (WGPUTextureFormat_RGBA8Unorm)

/** Bindings of the SMAA passes */
#define BOX_GPU_SMAA_BINDINGS (7)

/** Bytes of the uniforms of an effect pass */
#define BOX_GPU_FX_UNIFORMS (256)

/** Bloom mips at most */
#define BOX_GPU_BLOOM_MIPS (12)

/** Smallest edge of the last bloom mip, in pixels, of a chain that is long enough */
#define BOX_GPU_BLOOM_LEAF (4)

/** Bins of the luminance histogram of the exposure */
#define BOX_GPU_EXPOSURE_BINS (256)

/** Pixels along each axis of a workgroup of the histogram pass */
#define BOX_GPU_EXPOSURE_GROUP (16)

/** Stops the exposure is clamped to when no bound is set */
#define BOX_GPU_EXPOSURE_LIMIT (60.0f)

/** Format of the Tony McMapface LUT, a core sampleable and filterable format of WebGPU */
#define BOX_GPU_TONY_FORMAT (WGPUTextureFormat_RGB9E5Ufloat)

/** Kind of state that holds the measured exposure, next to box_render_effect_kind_t */
#define BOX_GPU_FX_MEASURE (100)

/** Kind of state of the pass that copies an HDR image into its target */
#define BOX_GPU_FX_COPY (101)

/** How a pass writes its output */
typedef enum box_gpu_fx_blend_t {
    BOX_GPU_FX_REPLACE,     /**< Replace the output */
    BOX_GPU_FX_OVER,        /**< Premultiplied alpha over the output */
    BOX_GPU_FX_ADD          /**< Add the output times the blend constant */
} box_gpu_fx_blend_t;

/** A render pipeline of an effect shader entry point, format and blend */
typedef struct box_gpu_fx_pipeline_t {
    uint32_t shader;                /**< Shader handle */
    int32_t revision;               /**< Revision of the module the pipeline was made with */
    const char *entry;              /**< Fragment entry point */
    WGPUTextureFormat format;       /**< Format of the output */
    box_gpu_fx_blend_t blend;       /**< How the output is written */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_fx_pipeline_t;

/** State of an effect of a view in a target, kept while the effect runs every frame */
typedef struct box_gpu_fx_state_t {
    uint32_t target;                /**< Handle of the target */
    ecs_entity_t view;              /**< View entity */
    ecs_entity_t effect;            /**< Effect entity */
    int32_t kind;                   /**< box_render_effect_kind_t or a BOX_GPU_FX_ kind */
    int64_t frame;                  /**< Frame of the last packet that used the state */
    WGPUBuffer uniforms;            /**< Uniforms of the passes */
    WGPUBuffer histogram;           /**< Luminance histogram of a measured exposure */
    WGPUBuffer exposure;            /**< Adapted exposure */
    WGPUTexture bloom;              /**< Mip chain of a bloom */
    WGPUTextureView mips[BOX_GPU_BLOOM_MIPS]; /**< View of every mip */
    int32_t mip_count;              /**< Mips of the chain */
    uint32_t width;                 /**< Width of the first mip */
    uint32_t height;                /**< Height of the first mip */
    WGPUTexture history[2];         /**< First mip of the previous and the current frame */
    WGPUTextureView history_views[2]; /**< Views of the history images */
    int32_t history_index;          /**< History image that holds the last frame */
    bool history_valid;             /**< The history holds a frame */
} box_gpu_fx_state_t;

/** HDR images of a target, the size it renders at */
typedef struct box_gpu_fx_target_t {
    uint32_t target;                /**< Handle of the target */
    int64_t frame;                  /**< Frame of the last packet that used the images */
    int32_t width;                  /**< Render width */
    int32_t height;                 /**< Render height */
    WGPUTexture hdr[2];             /**< The images a chain alternates between */
    WGPUTextureView hdr_views[2];   /**< Views of the images */
    WGPUTexture smaa_edges;         /**< Edges of SMAA, created on first use */
    WGPUTextureView smaa_edges_view; /**< View of the edges */
    WGPUTexture smaa_weights;       /**< Blending weights of SMAA, created on first use */
    WGPUTextureView smaa_weights_view; /**< View of the blending weights */
} box_gpu_fx_target_t;

struct box_gpu_effects_t {
    WGPUBindGroupLayout layout;     /**< Bindings of the fullscreen effect passes */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the fullscreen passes */
    WGPUBindGroupLayout smaa_layout; /**< Bindings of the SMAA passes */
    WGPUPipelineLayout smaa_pipeline_layout; /**< Pipeline layout of the SMAA passes */
    WGPUBindGroupLayout measure_layout; /**< Bindings of the exposure compute passes */
    WGPUPipelineLayout measure_pipeline_layout; /**< Pipeline layout of the compute passes */
    WGPUComputePipeline histogram;  /**< Pipeline that fills the luminance histogram */
    WGPUComputePipeline adapt;      /**< Pipeline that adapts the exposure */
    int32_t measure_revision;       /**< Revision of the exposure shader of those pipelines */
    WGPUSampler sampler;            /**< Linear clamp sampler */
    WGPUTexture placeholder;        /**< 1x1 image bound where a pass reads no second image */
    WGPUTextureView placeholder_view; /**< View of the placeholder image */
    WGPUTexture placeholder_3d;     /**< 1x1x1 volume bound where a pass reads no LUT */
    WGPUTextureView placeholder_3d_view; /**< View of the placeholder volume */
    WGPUTexture smaa_area;          /**< Area texture of SMAA, uploaded on first use */
    WGPUTextureView smaa_area_view; /**< View of the area texture */
    WGPUTexture smaa_search;        /**< Search texture of SMAA, uploaded with the area */
    WGPUTextureView smaa_search_view; /**< View of the search texture */
    WGPUTexture tony;               /**< The Tony McMapface LUT, loaded on first use */
    WGPUTextureView tony_view;      /**< View of the LUT */
    bool tony_failed;               /**< The LUT could not be loaded, which was logged once */
    WGPUBuffer unit_exposure;       /**< Exposure of 0 stops, bound where a pass scales nothing */
    WGPUBuffer no_uniforms;         /**< Uniforms bound where a pass has none */
    ecs_vec_t pipelines;            /**< box_gpu_fx_pipeline_t */
    ecs_vec_t states;               /**< box_gpu_fx_state_t* */
    ecs_vec_t targets;              /**< box_gpu_fx_target_t* */
    box_gpu_fx_target_t *current;   /**< HDR images of the view being rendered */
};

/** One fullscreen effect pass: its pipeline, inputs and output */
typedef struct box_gpu_fx_pass_t {
    uint32_t shader;                /**< Shader handle */
    const char *vertex;             /**< Vertex entry point, NULL for vs */
    const char *entry;              /**< Fragment entry point */
    WGPUTextureView input;          /**< Image at binding 0 */
    WGPUTextureView aux;            /**< Image at binding 4, NULL for the placeholder */
    WGPUBuffer uniforms;            /**< Uniforms at binding 2, NULL for none */
    WGPUBuffer exposure;            /**< Exposure at binding 5, NULL for 0 stops */
    WGPUTextureView lut;            /**< Volume at binding 6, NULL for the placeholder */
    WGPUTextureView depth;          /**< Depth at binding 3 */
    WGPUTextureView output;         /**< Image the pass writes */
    WGPUTextureFormat format;       /**< Format of the output */
    box_gpu_fx_blend_t blend;       /**< How the output is written */
    float constant;                 /**< Blend constant of BOX_GPU_FX_ADD */
} box_gpu_fx_pass_t;

/** Where the effect of a chain writes and what it reads */
typedef struct box_gpu_fx_ctx_t {
    box_gpu_target_t *t;            /**< The target */
    box_gpu_fx_target_t *ft;        /**< HDR images of the target */
    WGPUCommandEncoder encoder;     /**< The encoder of the frame */
    const box_render_packet_t *packet; /**< The packet */
    const box_render_view_t *v;     /**< The view */
    WGPUTextureView input;          /**< Image so far */
    WGPUTextureView output;         /**< Image the effect writes */
    WGPUTextureFormat format;       /**< Format of the output */
    box_gpu_fx_blend_t blend;       /**< How the effect writes its output */
} box_gpu_fx_ctx_t;

/** Uniforms of the tone map pass, matching Tonemap in etc/shaders/tonemap.wgsl */
typedef struct box_gpu_tonemap_uniforms_t {
    uint32_t flags[4];              /**< Scale by the exposure, operator (0 none, 1 PBR Neutral,
                                     *   2 Tony McMapface), two unused */
} box_gpu_tonemap_uniforms_t;

/** Uniforms of the exposure passes, matching Settings in etc/shaders/exposure.wgsl */
typedef struct box_gpu_measure_uniforms_t {
    float range[4];                 /**< Min log luminance, its range, low and high percentile */
    float adapt[4];                 /**< Speed up, speed down, log2 min and max brightness */
    float target[4];                /**< Log2 target, compensation, log2 min and max exposure */
    float frame[4];                 /**< Frame delta, 1 with a target brightness, two unused */
} box_gpu_measure_uniforms_t;

/** Uniforms of the bloom passes, matching Bloom in etc/shaders/bloom.wgsl */
typedef struct box_gpu_bloom_uniforms_t {
    float threshold[4];             /**< Threshold, threshold - knee, 2 knee, 0.25 / knee */
    float params[4];                /**< Firefly clamp, blend of the first mip, history blend */
    float first[4];                 /**< Tap offset of the first downsample in uv */
} box_gpu_bloom_uniforms_t;

/** Uniforms of the SMAA passes, matching Smaa in etc/shaders/smaa.wgsl */
typedef struct box_gpu_smaa_uniforms_t {
    float metrics[4];               /**< 1 / width, 1 / height, width, height */
    float params[4];                /**< Threshold, search steps, diagonal search steps,
                                     *   corner rounding 0..1 */
} box_gpu_smaa_uniforms_t;

/** Uniforms of the fog pass, matching Fog in etc/shaders/fog.wgsl */
typedef struct box_gpu_fog_uniforms_t {
    mat4 inv_view_proj;             /**< Inverse of the projection times the view matrix */
    float color[4];                 /**< Linear fog color, density */
    float params[4];                /**< Falloff, base height, max opacity, 1 when lit */
    float sun[4];                   /**< Direction toward the sun, 1 when there is one */
    float sun_color[4];             /**< Sun radiance times its inscatter */
    float ambient[4];               /**< Ambient light times its inscatter */
    float atmos[4];                 /**< 1 for the color of the atmosphere, phase g, sun and
                                     *   ambient inscatter */
} box_gpu_fog_uniforms_t;

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_gpu_fx_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Create a buffer that starts out zeroed.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer, besides copy destination
 * @return The buffer
 */
static WGPUBuffer box_gpu_fx_buffer(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    WGPUBufferDescriptor desc = { .usage = usage | WGPUBufferUsage_CopyDst, .size = size };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

/** Create a 2D image that passes render into and sample.
 *
 * @param gpu The backend
 * @param width Width in pixels
 * @param height Height in pixels
 * @param format Color format
 * @param mips Mip levels
 * @return The texture
 */
static WGPUTexture box_gpu_fx_texture(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format,
    uint32_t mips)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { width ? width : 1, height ? height : 1, 1 },
        .format = format,
        .mipLevelCount = mips,
        .sampleCount = 1
    };

    return wgpuDeviceCreateTexture(gpu->device, &desc);
}

/** Create the view of one mip level of a texture.
 *
 * @param texture The texture
 * @param level The mip level
 * @return The view
 */
static WGPUTextureView box_gpu_fx_level(
    WGPUTexture texture,
    uint32_t level)
{
    WGPUTextureViewDescriptor desc = {
        .format = wgpuTextureGetFormat(texture),
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = level,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &desc);
}

/** Create a 3D texture that passes sample, filled with texels of 4 bytes.
 *
 * @param gpu The backend
 * @param size Texels along each edge
 * @param format Color format, of 4 bytes per texel
 * @param texels The texels, x fastest, then y, then z
 * @param view Receives the view of the texture
 * @return The texture
 */
static WGPUTexture box_gpu_fx_volume(
    box_gpu_t *gpu,
    uint32_t size,
    WGPUTextureFormat format,
    const uint32_t *texels,
    WGPUTextureView *view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = { size, size, size },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &desc);
    WGPUTexelCopyTextureInfo dst = { .texture = result, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferLayout layout = { .bytesPerRow = size * 4, .rowsPerImage = size };
    WGPUExtent3D extent = { size, size, size };
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };
    wgpuQueueWriteTexture(gpu->queue, &dst, texels, (size_t)size * size * size * 4, &layout,
        &extent);
    *view = wgpuTextureCreateView(result, &view_desc);

    return result;
}

/** Get the view of the Tony McMapface LUT, loaded from its file on first use.
 *
 * @param gpu The backend
 * @return The view, NULL when the file cannot be loaded
 */
static WGPUTextureView box_gpu_fx_tony(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;
    char *error = NULL;
    uint32_t *texels = NULL;
    if (fx->tony_view || fx->tony_failed) {
        goto done;
    }

    texels = box_render_tony_load(BOX_RENDER_TONY_PATH, &error);
    if (!texels) {
        ecs_err("tonemap: %s", error);
        ecs_os_free(error);
        fx->tony_failed = true;
        goto done;
    }

    fx->tony = box_gpu_fx_volume(gpu, BOX_RENDER_TONY_SIZE, BOX_GPU_TONY_FORMAT, texels,
        &fx->tony_view);
    ecs_os_free(texels);
done:
    return fx->tony_view;
}

/** Release a texture and its view when they exist.
 *
 * @param texture The texture
 * @param view The view
 */
static void box_gpu_fx_release(
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

void box_gpu_effects_init(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = ecs_os_calloc_t(box_gpu_effects_t);
    WGPUShaderStage frag = WGPUShaderStage_Fragment;
    WGPUBindGroupLayoutEntry entries[7] = {
        { .binding = 0, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 1, .visibility = frag, .sampler = {
            .type = WGPUSamplerBindingType_Filtering } },
        { .binding = 2, .visibility = frag, .buffer = {
            .type = WGPUBufferBindingType_Uniform } },
        { .binding = 3, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 4, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 5, .visibility = frag, .buffer = {
            .type = WGPUBufferBindingType_ReadOnlyStorage } },
        { .binding = 6, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_3D } }
    };
    WGPUBindGroupLayoutEntry smaa[BOX_GPU_SMAA_BINDINGS] = {
        { .binding = 0, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 1, .visibility = frag, .sampler = {
            .type = WGPUSamplerBindingType_Filtering } },
        { .binding = 2, .visibility = frag | WGPUShaderStage_Vertex, .buffer = {
            .type = WGPUBufferBindingType_Uniform } },
        { .binding = 3, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 4, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 5, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 6, .visibility = frag, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } }
    };
    WGPUShaderStage comp = WGPUShaderStage_Compute;
    WGPUBindGroupLayoutEntry measure[4] = {
        { .binding = 0, .visibility = comp, .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 1, .visibility = comp, .buffer = {
            .type = WGPUBufferBindingType_Storage } },
        { .binding = 2, .visibility = comp, .buffer = {
            .type = WGPUBufferBindingType_Storage } },
        { .binding = 3, .visibility = comp, .buffer = {
            .type = WGPUBufferBindingType_Uniform } }
    };
    WGPUSamplerDescriptor sampler = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 32,
        .maxAnisotropy = 1
    };
    float unit[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    uint32_t black = 0;
    fx->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 7, .entries = entries });
    fx->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &fx->layout });
    fx->smaa_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = BOX_GPU_SMAA_BINDINGS,
            .entries = smaa });
    fx->smaa_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &fx->smaa_layout });
    fx->measure_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 4, .entries = measure });
    fx->measure_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &fx->measure_layout });
    fx->sampler = wgpuDeviceCreateSampler(gpu->device, &sampler);
    fx->placeholder = box_gpu_fx_texture(gpu, 1, 1, BOX_GPU_HDR_FORMAT, 1);
    fx->placeholder_view = wgpuTextureCreateView(fx->placeholder, NULL);
    fx->placeholder_3d = box_gpu_fx_volume(gpu, 1, BOX_GPU_TONY_FORMAT, &black,
        &fx->placeholder_3d_view);
    fx->unit_exposure = box_gpu_fx_buffer(gpu, sizeof(unit), WGPUBufferUsage_Storage);
    fx->no_uniforms = box_gpu_fx_buffer(gpu, BOX_GPU_FX_UNIFORMS, WGPUBufferUsage_Uniform);
    wgpuQueueWriteBuffer(gpu->queue, fx->unit_exposure, 0, unit, sizeof(unit));
    ecs_vec_init_t(NULL, &fx->pipelines, box_gpu_fx_pipeline_t, 0);
    ecs_vec_init_t(NULL, &fx->states, box_gpu_fx_state_t*, 0);
    ecs_vec_init_t(NULL, &fx->targets, box_gpu_fx_target_t*, 0);
    gpu->effects = fx;
}

/** Release the history images of a bloom.
 *
 * @param s The state
 */
static void box_gpu_fx_history_free(
    box_gpu_fx_state_t *s)
{
    box_gpu_fx_release(&s->history[0], &s->history_views[0]);
    box_gpu_fx_release(&s->history[1], &s->history_views[1]);
    s->history_valid = false;
    s->history_index = 0;
}

/** Release the mip chain of a bloom.
 *
 * @param s The state
 */
static void box_gpu_fx_mips_free(
    box_gpu_fx_state_t *s)
{
    int32_t i;
    for (i = 0; i < s->mip_count; i ++) {
        wgpuTextureViewRelease(s->mips[i]);
        s->mips[i] = NULL;
    }

    if (s->bloom) {
        wgpuTextureRelease(s->bloom);
        s->bloom = NULL;
    }

    s->mip_count = 0;
    s->width = 0;
    s->height = 0;
}

/** Release the state of an effect and everything it owns.
 *
 * @param s The state
 */
static void box_gpu_fx_state_free(
    box_gpu_fx_state_t *s)
{
    box_gpu_fx_mips_free(s);
    box_gpu_fx_history_free(s);
    if (s->uniforms) {
        wgpuBufferRelease(s->uniforms);
    }

    if (s->histogram) {
        wgpuBufferRelease(s->histogram);
    }

    if (s->exposure) {
        wgpuBufferRelease(s->exposure);
    }

    ecs_os_free(s);
}

/** Release the HDR images of a target.
 *
 * @param ft The images
 */
static void box_gpu_fx_target_free(
    box_gpu_fx_target_t *ft)
{
    box_gpu_fx_release(&ft->hdr[0], &ft->hdr_views[0]);
    box_gpu_fx_release(&ft->hdr[1], &ft->hdr_views[1]);
    box_gpu_fx_release(&ft->smaa_edges, &ft->smaa_edges_view);
    box_gpu_fx_release(&ft->smaa_weights, &ft->smaa_weights_view);
    ecs_os_free(ft);
}

/** Release a compute pipeline when it exists.
 *
 * @param pipeline The pipeline, set to NULL
 */
static void box_gpu_fx_compute_release(
    WGPUComputePipeline *pipeline)
{
    if (*pipeline) {
        wgpuComputePipelineRelease(*pipeline);
        *pipeline = NULL;
    }
}

/** Release the effect pipelines.
 *
 * @param fx The effects
 */
static void box_gpu_fx_pipelines_free(
    box_gpu_effects_t *fx)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&fx->pipelines); i ++) {
        box_gpu_fx_pipeline_t *p = ecs_vec_get_t(&fx->pipelines, box_gpu_fx_pipeline_t, i);
        if (p->pipeline) {
            wgpuRenderPipelineRelease(p->pipeline);
        }
    }

    ecs_vec_clear(&fx->pipelines);
    if (fx->histogram) {
        wgpuComputePipelineRelease(fx->histogram);
        fx->histogram = NULL;
    }

    if (fx->adapt) {
        wgpuComputePipelineRelease(fx->adapt);
        fx->adapt = NULL;
    }

}

void box_gpu_effects_fini(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;
    int32_t i;
    if (!fx) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&fx->states); i ++) {
        box_gpu_fx_state_free(*ecs_vec_get_t(&fx->states, box_gpu_fx_state_t*, i));
    }

    for (i = 0; i < ecs_vec_count(&fx->targets); i ++) {
        box_gpu_fx_target_free(*ecs_vec_get_t(&fx->targets, box_gpu_fx_target_t*, i));
    }

    box_gpu_fx_pipelines_free(fx);
    ecs_vec_fini_t(NULL, &fx->pipelines, box_gpu_fx_pipeline_t);
    ecs_vec_fini_t(NULL, &fx->states, box_gpu_fx_state_t*);
    ecs_vec_fini_t(NULL, &fx->targets, box_gpu_fx_target_t*);
    box_gpu_fx_release(&fx->placeholder, &fx->placeholder_view);
    box_gpu_fx_release(&fx->placeholder_3d, &fx->placeholder_3d_view);
    box_gpu_fx_release(&fx->tony, &fx->tony_view);
    box_gpu_fx_release(&fx->smaa_area, &fx->smaa_area_view);
    box_gpu_fx_release(&fx->smaa_search, &fx->smaa_search_view);
    wgpuBufferRelease(fx->unit_exposure);
    wgpuBufferRelease(fx->no_uniforms);
    wgpuSamplerRelease(fx->sampler);
    wgpuPipelineLayoutRelease(fx->smaa_pipeline_layout);
    wgpuBindGroupLayoutRelease(fx->smaa_layout);
    wgpuPipelineLayoutRelease(fx->measure_pipeline_layout);
    wgpuBindGroupLayoutRelease(fx->measure_layout);
    wgpuPipelineLayoutRelease(fx->pipeline_layout);
    wgpuBindGroupLayoutRelease(fx->layout);
    ecs_os_free(fx);
    gpu->effects = NULL;
done:
    return;
}

/** Get the revision of the module of a shader.
 *
 * @param gpu The backend
 * @param handle Handle of the shader
 * @param module Receives the module, NULL when the shader has none
 * @return The revision
 */
static int32_t box_gpu_fx_module(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule *module)
{
    box_gpu_shader_t *s = ecs_map_get_deref(&gpu->shaders, box_gpu_shader_t, handle);
    *module = s ? s->module : NULL;

    return s ? s->revision : 0;
}

/** Get the blend state of a way to write the output.
 *
 * @param blend How the output is written
 * @param out Receives the blend state
 * @return The blend state, NULL to replace the output
 */
static WGPUBlendState* box_gpu_fx_blend(
    box_gpu_fx_blend_t blend,
    WGPUBlendState *out)
{
    WGPUBlendState *result = out;
    WGPUBlendComponent over = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
    };
    WGPUBlendComponent add = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_Constant,
        .dstFactor = WGPUBlendFactor_One
    };
    WGPUBlendComponent keep = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_Zero,
        .dstFactor = WGPUBlendFactor_One
    };
    if (blend == BOX_GPU_FX_OVER) {
        out->color = over;
        out->alpha = over;
    } else if (blend == BOX_GPU_FX_ADD) {
        out->color = add;
        out->alpha = keep;
    } else {
        result = NULL;
    }

    return result;
}

/** Get the pipeline of an effect entry point, created on first use and again when its shader
 * was recompiled. The SMAA shader has a layout of its own; the vertex
 * entry point of a pass follows from its fragment entry point.
 *
 * @param gpu The backend
 * @param pass The pass
 * @return The pipeline, NULL while the shader has no module
 */
static WGPURenderPipeline box_gpu_fx_pipeline(
    box_gpu_t *gpu,
    const box_gpu_fx_pass_t *pass)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPUShaderModule module = NULL;
    int32_t revision = box_gpu_fx_module(gpu, pass->shader, &module);
    box_gpu_fx_pipeline_t *found = NULL;
    WGPUBlendState blend;
    int32_t i;
    if (!module) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&fx->pipelines); i ++) {
        box_gpu_fx_pipeline_t *p = ecs_vec_get_t(&fx->pipelines, box_gpu_fx_pipeline_t, i);
        if (p->shader == pass->shader && p->format == pass->format &&
            p->blend == pass->blend && !strcmp(p->entry, pass->entry))
        {
            found = p;
            break;
        }
    }

    if (found && found->revision == revision) {
        goto done;
    }

    if (!found) {
        found = ecs_vec_append_t(NULL, &fx->pipelines, box_gpu_fx_pipeline_t);
        ecs_os_memset_t(found, 0, box_gpu_fx_pipeline_t);
        found->shader = pass->shader;
        found->entry = pass->entry;
        found->format = pass->format;
        found->blend = pass->blend;
    } else if (found->pipeline) {
        wgpuRenderPipelineRelease(found->pipeline);
    }

    WGPUColorTargetState target = {
        .format = pass->format,
        .blend = box_gpu_fx_blend(pass->blend, &blend),
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = pass->entry, .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = pass->shader == BOX_RENDER_SHADER_SMAA
            ? fx->smaa_pipeline_layout
            : fx->pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = pass->vertex ? pass->vertex : "vs", .length = WGPU_STRLEN }
        },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };
    found->pipeline = wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
    found->revision = revision;
done:
    return module && found ? found->pipeline : NULL;
}

/** Record a fullscreen pass with its pipeline and bind group, and release the bind group.
 *
 * @param ctx The chain
 * @param pass The pass
 * @param pipeline The pipeline of the pass
 * @param group The bind group of the pass
 */
static void box_gpu_fx_record(
    const box_gpu_fx_ctx_t *ctx,
    const box_gpu_fx_pass_t *pass,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group)
{
    WGPURenderPassColorAttachment attachment = {
        .view = pass->output,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = pass->blend == BOX_GPU_FX_REPLACE ? WGPULoadOp_Clear : WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &attachment
    };
    WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(ctx->encoder, &desc);
    wgpuRenderPassEncoderSetPipeline(rp, pipeline);
    wgpuRenderPassEncoderSetBindGroup(rp, 0, group, 0, NULL);
    if (pass->blend == BOX_GPU_FX_ADD) {
        WGPUColor constant = { pass->constant, pass->constant, pass->constant, pass->constant };
        wgpuRenderPassEncoderSetBlendConstant(rp, &constant);
    }

    wgpuRenderPassEncoderDraw(rp, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);
    wgpuBindGroupRelease(group);
}

/** Record a fullscreen effect pass.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param pass The pass
 * @return Whether the pass was recorded, false while its shader has no module
 */
static bool box_gpu_fx_draw(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_gpu_fx_pass_t *pass)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPURenderPipeline pipeline = box_gpu_fx_pipeline(gpu, pass);
    if (!pipeline) {
        goto done;
    }

    WGPUBuffer uniforms = pass->uniforms ? pass->uniforms : fx->no_uniforms;
    WGPUBuffer exposure = pass->exposure ? pass->exposure : fx->unit_exposure;
    WGPUBindGroupEntry entries[7] = {
        { .binding = 0, .textureView = pass->input },
        { .binding = 1, .sampler = fx->sampler },
        { .binding = 2, .buffer = uniforms, .size = BOX_GPU_FX_UNIFORMS },
        { .binding = 3, .textureView = ctx->t->depth_view },
        { .binding = 4, .textureView = pass->aux ? pass->aux : fx->placeholder_view },
        { .binding = 5, .buffer = exposure, .size = 4 * sizeof(float) },
        { .binding = 6, .textureView = pass->lut ? pass->lut : fx->placeholder_3d_view }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = fx->layout,
        .entryCount = 7,
        .entries = entries
    });
    box_gpu_fx_record(ctx, pass, pipeline, group);
done:
    return pipeline != NULL;
}

/** Get the state of an effect of a view in a target, created on first use.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param effect The effect entity
 * @param kind Kind of state
 * @return The state, marked as used by the packet
 */
static box_gpu_fx_state_t* box_gpu_fx_state(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    ecs_entity_t effect,
    int32_t kind)
{
    box_gpu_effects_t *fx = gpu->effects;
    box_gpu_fx_state_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&fx->states); i ++) {
        box_gpu_fx_state_t *s = *ecs_vec_get_t(&fx->states, box_gpu_fx_state_t*, i);
        if (s->target == ctx->t->handle && s->view == ctx->v->view && s->effect == effect &&
            s->kind == kind)
        {
            result = s;
            break;
        }
    }

    if (!result) {
        result = ecs_os_calloc_t(box_gpu_fx_state_t);
        result->target = ctx->t->handle;
        result->view = ctx->v->view;
        result->effect = effect;
        result->kind = kind;
        result->uniforms = box_gpu_fx_buffer(gpu, BOX_GPU_FX_UNIFORMS, WGPUBufferUsage_Uniform);
        *ecs_vec_append_t(NULL, &fx->states, box_gpu_fx_state_t*) = result;
    }

    result->frame = ctx->packet->frame;

    return result;
}

/** Write the uniforms of a state.
 *
 * @param gpu The backend
 * @param s The state
 * @param data The uniforms
 * @param size Bytes of the uniforms, at most BOX_GPU_FX_UNIFORMS
 */
static void box_gpu_fx_uniforms(
    box_gpu_t *gpu,
    box_gpu_fx_state_t *s,
    const void *data,
    size_t size)
{
    wgpuQueueWriteBuffer(gpu->queue, s->uniforms, 0, data, size);
}

/** Get the exposure compute pipelines, created again when the exposure shader was recompiled.
 *
 * @param gpu The backend
 * @return Whether both pipelines exist
 */
static bool box_gpu_fx_measure_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPUShaderModule module = NULL;
    int32_t revision = box_gpu_fx_module(gpu, BOX_RENDER_SHADER_EXPOSURE, &module);
    if (!module || (fx->histogram && fx->measure_revision == revision)) {
        goto done;
    }

    if (fx->histogram) {
        wgpuComputePipelineRelease(fx->histogram);
        wgpuComputePipelineRelease(fx->adapt);
    }

    fx->histogram = wgpuDeviceCreateComputePipeline(gpu->device,
        &(WGPUComputePipelineDescriptor){
            .layout = fx->measure_pipeline_layout,
            .compute = { .module = module,
                .entryPoint = { .data = "histogram_main", .length = WGPU_STRLEN } }
        });
    fx->adapt = wgpuDeviceCreateComputePipeline(gpu->device,
        &(WGPUComputePipelineDescriptor){
            .layout = fx->measure_pipeline_layout,
            .compute = { .module = module,
                .entryPoint = { .data = "adapt_main", .length = WGPU_STRLEN } }
        });
    fx->measure_revision = revision;
done:
    return module && fx->histogram && fx->adapt;
}

/** Fill the uniforms of the exposure passes from resolved settings.
 *
 * @param a The settings
 * @param delta Seconds the frame advanced
 * @param out Receives the uniforms
 */
static void box_gpu_fx_measure_settings(
    const AutoExposure *a,
    float delta,
    box_gpu_measure_uniforms_t *out)
{
    ecs_os_memset_t(out, 0, box_gpu_measure_uniforms_t);
    out->range[0] = a->min_log_luma;
    out->range[1] = a->max_log_luma - a->min_log_luma;
    out->range[2] = a->low_percentile;
    out->range[3] = a->high_percentile;
    out->adapt[0] = a->speed_up;
    out->adapt[1] = a->speed_down;
    out->adapt[2] = log2f(a->min_brightness);
    out->adapt[3] = log2f(a->max_brightness);
    out->target[0] = a->target_brightness > 0 ? log2f(a->target_brightness) : 0.0f;
    out->target[1] = a->exposure_compensation;
    out->target[2] = a->min_exposure > 0 ? log2f(a->min_exposure) : -BOX_GPU_EXPOSURE_LIMIT;
    out->target[3] = a->max_exposure > 0 ? log2f(a->max_exposure) : BOX_GPU_EXPOSURE_LIMIT;
    out->target[3] = glm_max(out->target[3], out->target[2]);
    out->frame[0] = glm_max(delta, 0.0f);
    out->frame[1] = a->target_brightness > 0 ? 1.0f : 0.0f;
}

/** Measure the brightness of the image and adapt the exposure of an effect.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The effect pass, with an exposure
 * @return The buffer of the adapted exposure, NULL when the exposure shader has no module
 */
static WGPUBuffer box_gpu_fx_measure(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    box_gpu_effects_t *fx = gpu->effects;
    box_gpu_measure_uniforms_t u;
    WGPUBuffer result = NULL;
    if (!box_gpu_fx_measure_pipelines(gpu)) {
        goto done;
    }

    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->exposure, BOX_GPU_FX_MEASURE);
    if (!s->histogram) {
        s->histogram = box_gpu_fx_buffer(gpu,
            BOX_GPU_EXPOSURE_BINS * sizeof(uint32_t), WGPUBufferUsage_Storage);
        s->exposure = box_gpu_fx_buffer(gpu, 4 * sizeof(float), WGPUBufferUsage_Storage);
    }

    box_gpu_fx_measure_settings(&e->auto_exposure, ctx->packet->delta, &u);
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .textureView = ctx->input },
        { .binding = 1, .buffer = s->histogram,
            .size = BOX_GPU_EXPOSURE_BINS * sizeof(uint32_t) },
        { .binding = 2, .buffer = s->exposure, .size = 4 * sizeof(float) },
        { .binding = 3, .buffer = s->uniforms, .size = BOX_GPU_FX_UNIFORMS }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = fx->measure_layout,
        .entryCount = 4,
        .entries = entries
    });
    uint32_t w = (uint32_t)ctx->ft->width, h = (uint32_t)ctx->ft->height;
    WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(ctx->encoder, NULL);
    wgpuComputePassEncoderSetBindGroup(cp, 0, group, 0, NULL);
    wgpuComputePassEncoderSetPipeline(cp, fx->histogram);
    wgpuComputePassEncoderDispatchWorkgroups(cp,
        (w + BOX_GPU_EXPOSURE_GROUP - 1) / BOX_GPU_EXPOSURE_GROUP,
        (h + BOX_GPU_EXPOSURE_GROUP - 1) / BOX_GPU_EXPOSURE_GROUP, 1);
    wgpuComputePassEncoderSetPipeline(cp, fx->adapt);
    wgpuComputePassEncoderDispatchWorkgroups(cp, 1, 1, 1);
    wgpuComputePassEncoderEnd(cp);
    wgpuComputePassEncoderRelease(cp);
    wgpuBindGroupRelease(group);
    result = s->exposure;
done:
    return result;
}

/** Record a tone map or exposure pass, or a copy of the image when the pass is NULL.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass, NULL to copy the image
 * @return Whether the pass was recorded
 */
static bool box_gpu_fx_tonemap(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    box_gpu_tonemap_uniforms_t u = {{0}};
    WGPUBuffer exposure = e && e->exposure ? box_gpu_fx_measure(gpu, ctx, e) : NULL;
    WGPUTextureView lut = NULL;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e ? e->entity : 0,
        e ? (int32_t)e->kind : BOX_GPU_FX_COPY);
    u.flags[0] = exposure ? 1u : 0u;
    if (e && e->kind == BOX_RENDER_EFFECT_TONEMAP) {
        u.flags[1] = 1u;
        if (e->tonemap == TonemapOperatorTonyMcMapface) {
            lut = box_gpu_fx_tony(gpu);
            u.flags[1] = lut ? 2u : 1u;
        }
    }

    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));

    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_TONEMAP,
        .entry = "fs",
        .input = ctx->input,
        .uniforms = s->uniforms,
        .exposure = exposure,
        .lut = lut,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
}

/** Get the blend of a bloom mip into the mip above it, following the boost of the wide mips
 * and the high pass.
 *
 * @param b The settings
 * @param mip The mip
 * @param max_mip The last mip
 * @return The blend
 */
static float box_gpu_fx_bloom_blend(
    const Bloom *b,
    float mip,
    float max_mip)
{
    float result = b->intensity;
    if (max_mip <= 0) {
        goto done;
    }

    float t = mip / max_mip;
    float curve = 1.0f / (1.0f - b->low_frequency_boost_curvature);
    float boost = (1.0f - powf(1.0f - t, curve)) * b->low_frequency_boost;
    float high_pass = 1.0f - glm_clamp(
        (t - b->high_pass_frequency) / b->high_pass_frequency, 0.0f, 1.0f);
    boost *= 1.0f - b->intensity;
    result = (b->intensity + boost) * high_pass;
done:
    return result;
}

/** Make the mip chain of a bloom match the render size and its settings.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param s The state
 * @param b The settings
 */
static void box_gpu_fx_bloom_mips(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    box_gpu_fx_state_t *s,
    const Bloom *b)
{
    uint32_t rw = (uint32_t)ctx->ft->width, rh = (uint32_t)ctx->ft->height;
    uint32_t longer = rw > rh ? rw : rh;
    uint32_t limit = (uint32_t)b->max_mip_dimension;
    uint32_t edge = longer / 2 < limit ? longer / 2 : limit;
    uint32_t w, h, shorter, count = 1, i;
    edge = edge ? edge : 1;
    w = (uint32_t)(((uint64_t)rw * edge + longer / 2) / longer);
    h = (uint32_t)(((uint64_t)rh * edge + longer / 2) / longer);
    w = w ? w : 1;
    h = h ? h : 1;
    if (s->bloom && s->width == w && s->height == h) {
        goto done;
    }

    box_gpu_fx_mips_free(s);
    box_gpu_fx_history_free(s);
    shorter = w < h ? w : h;
    while (shorter >= BOX_GPU_BLOOM_LEAF * 2 && count < BOX_GPU_BLOOM_MIPS) {
        count ++;
        shorter >>= 1;
    }

    s->bloom = box_gpu_fx_texture(gpu, w, h, BOX_GPU_HDR_FORMAT, count);
    for (i = 0; i < count; i ++) {
        s->mips[i] = box_gpu_fx_level(s->bloom, i);
    }

    s->mip_count = (int32_t)count;
    s->width = w;
    s->height = h;
done:
    return;
}

/** Fill the uniforms of the bloom passes.
 *
 * @param s The state, with its mip chain
 * @param b The settings
 * @param out Receives the uniforms
 */
static void box_gpu_fx_bloom_settings(
    const box_gpu_fx_state_t *s,
    const Bloom *b,
    box_gpu_bloom_uniforms_t *out)
{
    float knee = b->threshold * b->threshold_softness;
    ecs_os_memset_t(out, 0, box_gpu_bloom_uniforms_t);
    out->threshold[0] = b->threshold;
    out->threshold[1] = b->threshold - knee;
    out->threshold[2] = 2.0f * knee;
    out->threshold[3] = 0.25f / (knee + 1e-5f);
    out->params[0] = b->firefly_clamp;
    out->params[1] = box_gpu_fx_bloom_blend(b, 0, (float)(s->mip_count - 1));
    out->params[2] = s->history_valid ? b->temporal : 0.0f;
    out->first[0] = 0.5f / (float)s->width;
    out->first[1] = 0.5f / (float)s->height;
}

/** Blend the first mip of a bloom with the previous frame.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param s The state
 * @return The image the composite reads, the first mip when the history pass did not run
 */
static WGPUTextureView box_gpu_fx_bloom_history(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    box_gpu_fx_state_t *s)
{
    WGPUTextureView result = s->mips[0];
    int32_t i, next = s->history_index ^ 1;
    for (i = 0; i < 2; i ++) {
        if (!s->history[i]) {
            s->history[i] = box_gpu_fx_texture(gpu, s->width, s->height, BOX_GPU_HDR_FORMAT, 1);
            s->history_views[i] = wgpuTextureCreateView(s->history[i], NULL);
        }
    }

    bool drawn = box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_BLOOM,
        .entry = "history",
        .input = s->mips[0],
        .aux = s->history_views[s->history_index],
        .uniforms = s->uniforms,
        .output = s->history_views[next],
        .format = BOX_GPU_HDR_FORMAT,
        .blend = BOX_GPU_FX_REPLACE
    });
    if (drawn) {
        s->history_index = next;
        s->history_valid = true;
        result = s->history_views[next];
    }

    return result;
}

/** Record the passes of a bloom: downsample into the mip chain, upsample back up and add the
 * first mip to the image.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the image was written to the output
 */
static bool box_gpu_fx_bloom(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    const Bloom *b = &e->bloom;
    box_gpu_bloom_uniforms_t u;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    box_gpu_fx_pass_t pass = {
        .shader = BOX_RENDER_SHADER_BLOOM,
        .uniforms = s->uniforms,
        .format = BOX_GPU_HDR_FORMAT,
        .blend = BOX_GPU_FX_REPLACE
    };
    bool result = false;
    int32_t i;
    box_gpu_fx_bloom_mips(gpu, ctx, s, b);
    if (b->temporal <= 0) {
        box_gpu_fx_history_free(s);
    }

    box_gpu_fx_bloom_settings(s, b, &u);
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));
    pass.entry = "downsample_first";
    pass.input = ctx->input;
    pass.output = s->mips[0];
    if (!box_gpu_fx_draw(gpu, ctx, &pass)) {
        goto done;
    }

    pass.entry = "downsample";
    for (i = 1; i < s->mip_count; i ++) {
        pass.input = s->mips[i - 1];
        pass.output = s->mips[i];
        box_gpu_fx_draw(gpu, ctx, &pass);
    }

    pass.entry = "upsample";
    pass.blend = BOX_GPU_FX_ADD;
    for (i = s->mip_count - 1; i > 0; i --) {
        pass.input = s->mips[i];
        pass.output = s->mips[i - 1];
        pass.constant = box_gpu_fx_bloom_blend(b, (float)i, (float)(s->mip_count - 1));
        box_gpu_fx_draw(gpu, ctx, &pass);
    }

    WGPUTextureView glow = b->temporal > 0 ? box_gpu_fx_bloom_history(gpu, ctx, s) : s->mips[0];
    result = box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_BLOOM,
        .entry = "composite",
        .input = ctx->input,
        .aux = glow,
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
done:
    return result;
}

/** Get the projection a view renders its target with.
 *
 * @param ctx The chain
 * @param proj Receives the projection
 */
static void box_gpu_fx_projection(
    const box_gpu_fx_ctx_t *ctx,
    mat4 proj)
{
    box_camera_projection(&ctx->v->camera_data,
        (float)ctx->t->desc.point_width, (float)ctx->t->desc.point_height, proj);
}

/** Highest phase asymmetry of the glow of a fog colored by the atmosphere */
#define BOX_GPU_FOG_ANISOTROPY_MAX (0.92f)

/** Record the height fog pass, lit by the sun and ambient light of the scene of the view, or
 * colored by the sky of a view that renders the atmosphere the fog refers to:
 * the in scatter color the atmosphere wrote for the view, toward its main light.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the image was written to the output
 */
static bool box_gpu_fx_fog(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    const HeightFog *f = &e->fog;
    const box_render_view_lights_t *l = &ctx->v->lights;
    box_gpu_fog_uniforms_t u;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    mat4 proj, view_proj;
    int32_t c;
    ecs_os_memset_t(&u, 0, box_gpu_fog_uniforms_t);
    box_gpu_fx_projection(ctx, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, ctx->v->view_matrix), view_proj);
    glm_mat4_inv(view_proj, u.inv_view_proj);
    u.color[0] = box_gpu_fx_linear(f->color.r);
    u.color[1] = box_gpu_fx_linear(f->color.g);
    u.color[2] = box_gpu_fx_linear(f->color.b);
    u.color[3] = f->density;
    u.params[0] = f->falloff;
    u.params[1] = f->base_height;
    u.params[2] = f->max_opacity;
    u.params[3] = l->enabled ? 1.0f : 0.0f;
    if (l->directional && glm_vec3_norm(ECS_CONST_CAST(float*, l->sun)) > 1e-6f) {
        glm_vec3_normalize_to(ECS_CONST_CAST(float*, l->sun), u.sun);
        u.sun[3] = 1.0f;
    }

    for (c = 0; c < 3; c ++) {
        u.sun_color[c] = l->sun_color[c] / GLM_PIf * f->sun_inscatter;
        u.ambient[c] = (l->ambient_sky[c] + l->ambient_ground[c]) * 0.5f * f->ambient_inscatter;
    }

    WGPUTextureView sky = f->atmosphere && f->atmosphere == ctx->v->atmos.entity
        ? box_gpu_atmosphere_fog(gpu, ctx->t, ctx->packet, ctx->v)
        : NULL;
    if (sky) {
        const box_render_view_atmos_t *a = &ctx->v->atmos;
        glm_vec4_zero(u.sun);
        if (glm_vec3_norm(ECS_CONST_CAST(float*, a->fog_light)) > 1e-6f) {
            glm_vec3_normalize_to(ECS_CONST_CAST(float*, a->fog_light), u.sun);
            u.sun[3] = 1.0f;
        }

        u.atmos[0] = 1.0f;
        u.atmos[1] = glm_clamp(a->mie_anisotropy, 0.0f, BOX_GPU_FOG_ANISOTROPY_MAX);
        u.atmos[2] = f->sun_inscatter;
        u.atmos[3] = f->ambient_inscatter;
    }

    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));

    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_FOG,
        .entry = "fs",
        .input = ctx->input,
        .aux = sky,
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
}

/** Record an SMAA pass, with the bindings of the SMAA layout.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param pass The pass
 * @param edges The edge image the pass reads, NULL when it writes it
 * @param weights The weight image the pass reads, NULL when it does not read it
 * @return Whether the pass was recorded, false while the shader has no module
 */
static bool box_gpu_fx_smaa_draw(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_gpu_fx_pass_t *pass,
    WGPUTextureView edges,
    WGPUTextureView weights)
{
    box_gpu_effects_t *fx = gpu->effects;
    WGPURenderPipeline pipeline = box_gpu_fx_pipeline(gpu, pass);
    if (!pipeline) {
        goto done;
    }

    WGPUBindGroupEntry entries[BOX_GPU_SMAA_BINDINGS] = {
        { .binding = 0, .textureView = pass->input },
        { .binding = 1, .sampler = fx->sampler },
        { .binding = 2, .buffer = pass->uniforms, .size = BOX_GPU_FX_UNIFORMS },
        { .binding = 3, .textureView = edges ? edges : fx->placeholder_view },
        { .binding = 4, .textureView = fx->smaa_area_view },
        { .binding = 5, .textureView = fx->smaa_search_view },
        { .binding = 6, .textureView = weights ? weights : fx->placeholder_view }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = fx->smaa_layout,
        .entryCount = BOX_GPU_SMAA_BINDINGS,
        .entries = entries
    });
    box_gpu_fx_record(ctx, pass, pipeline, group);
done:
    return pipeline != NULL;
}

/** Create the area and search textures of the device and the edge and weight images of the
 * target when they do not exist yet.
 *
 * @param gpu The backend
 * @param ft The images of the target, at its render size
 */
static void box_gpu_fx_smaa_images(
    box_gpu_t *gpu,
    box_gpu_fx_target_t *ft)
{
    box_gpu_effects_t *fx = gpu->effects;
    if (!fx->smaa_area) {
        fx->smaa_area = box_gpu_smaa_area(gpu);
        fx->smaa_area_view = wgpuTextureCreateView(fx->smaa_area, NULL);
        fx->smaa_search = box_gpu_smaa_search(gpu);
        fx->smaa_search_view = wgpuTextureCreateView(fx->smaa_search, NULL);
    }

    if (!ft->smaa_edges) {
        ft->smaa_edges = box_gpu_fx_texture(gpu, (uint32_t)ft->width, (uint32_t)ft->height,
            BOX_GPU_SMAA_EDGES_FORMAT, 1);
        ft->smaa_edges_view = wgpuTextureCreateView(ft->smaa_edges, NULL);
        ft->smaa_weights = box_gpu_fx_texture(gpu, (uint32_t)ft->width, (uint32_t)ft->height,
            BOX_GPU_SMAA_WEIGHTS_FORMAT, 1);
        ft->smaa_weights_view = wgpuTextureCreateView(ft->smaa_weights, NULL);
    }
}

/** Record the passes of SMAA 1x: the color edges of the image into the edge image, the
 * blending weights of the edges into the weight image, then the image blended with its
 * neighbors by the weights into the output.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the image was written to the output
 */
static bool box_gpu_fx_smaa(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    box_gpu_fx_target_t *ft = ctx->ft;
    const Smaa *a = &e->smaa;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    float w = (float)(ft->width > 0 ? ft->width : 1);
    float h = (float)(ft->height > 0 ? ft->height : 1);
    box_gpu_smaa_uniforms_t u = {
        .metrics = { 1.0f / w, 1.0f / h, w, h },
        .params = { a->threshold, (float)a->max_search_steps,
            (float)a->max_search_steps_diag, (float)a->corner_rounding / 100.0f }
    };
    bool result = false;
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));
    box_gpu_fx_smaa_images(gpu, ft);
    box_gpu_fx_pass_t pass = {
        .shader = BOX_RENDER_SHADER_SMAA,
        .vertex = "edges_vs",
        .entry = "edges",
        .input = ctx->input,
        .uniforms = s->uniforms,
        .output = ft->smaa_edges_view,
        .format = BOX_GPU_SMAA_EDGES_FORMAT,
        .blend = BOX_GPU_FX_REPLACE
    };
    if (!box_gpu_fx_smaa_draw(gpu, ctx, &pass, NULL, NULL)) {
        goto done;
    }

    pass.vertex = "weights_vs";
    pass.entry = "weights";
    pass.output = ft->smaa_weights_view;
    pass.format = BOX_GPU_SMAA_WEIGHTS_FORMAT;
    if (!box_gpu_fx_smaa_draw(gpu, ctx, &pass, ft->smaa_edges_view, NULL)) {
        goto done;
    }

    pass.vertex = "blend_vs";
    pass.entry = "blend";
    pass.output = ctx->output;
    pass.format = ctx->format;
    pass.blend = ctx->blend;
    result = box_gpu_fx_smaa_draw(gpu, ctx, &pass, ft->smaa_edges_view,
        ft->smaa_weights_view);
done:
    return result;
}

/** Get the HDR images of a target, created or resized to its render size.
 *
 * @param gpu The backend
 * @param t The target
 * @return The images
 */
static box_gpu_fx_target_t* box_gpu_fx_target(
    box_gpu_t *gpu,
    box_gpu_target_t *t)
{
    box_gpu_effects_t *fx = gpu->effects;
    box_gpu_fx_target_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&fx->targets); i ++) {
        box_gpu_fx_target_t *ft = *ecs_vec_get_t(&fx->targets, box_gpu_fx_target_t*, i);
        if (ft->target == t->handle) {
            result = ft;
            break;
        }
    }

    if (!result) {
        result = ecs_os_calloc_t(box_gpu_fx_target_t);
        result->target = t->handle;
        *ecs_vec_append_t(NULL, &fx->targets, box_gpu_fx_target_t*) = result;
    }

    if (result->hdr[0] && result->width == t->desc.render_width &&
        result->height == t->desc.render_height)
    {
        goto done;
    }

    box_gpu_fx_release(&result->hdr[0], &result->hdr_views[0]);
    box_gpu_fx_release(&result->hdr[1], &result->hdr_views[1]);
    box_gpu_fx_release(&result->smaa_edges, &result->smaa_edges_view);
    box_gpu_fx_release(&result->smaa_weights, &result->smaa_weights_view);
    result->width = t->desc.render_width;
    result->height = t->desc.render_height;
    for (i = 0; i < 2; i ++) {
        result->hdr[i] = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                WGPUTextureUsage_CopySrc,
            .dimension = WGPUTextureDimension_2D,
            .size = { (uint32_t)result->width, (uint32_t)result->height, 1 },
            .format = BOX_GPU_HDR_FORMAT,
            .mipLevelCount = 1,
            .sampleCount = 1
        });
        result->hdr_views[i] = wgpuTextureCreateView(result->hdr[i], NULL);
    }

done:
    return result;
}

WGPUTextureView box_gpu_effects_begin(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    WGPUTextureView result = NULL;
    box_gpu_effects_t *fx = gpu->effects;
    if (!fx || v->effect_count <= 0 ||
        v->effect_first + v->effect_count > ecs_vec_count(&packet->effects))
    {
        goto done;
    }

    fx->current = box_gpu_fx_target(gpu, t);
    fx->current->frame = packet->frame;
    result = fx->current->hdr_views[0];
done:
    return result;
}

WGPUTexture box_gpu_effects_image(
    box_gpu_t *gpu)
{
    box_gpu_effects_t *fx = gpu->effects;

    return fx && fx->current ? fx->current->hdr[0] : NULL;
}

/** Record one effect pass of a chain.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the pass wrote its output
 */
static bool box_gpu_fx_effect(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    bool result = false;
    switch (e->kind) {
    case BOX_RENDER_EFFECT_TONEMAP:
    case BOX_RENDER_EFFECT_EXPOSURE:
        result = box_gpu_fx_tonemap(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_BLOOM:
        result = box_gpu_fx_bloom(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_FOG:
        result = box_gpu_fx_fog(gpu, ctx, e);
        break;
    case BOX_RENDER_EFFECT_SMAA:
        result = box_gpu_fx_smaa(gpu, ctx, e);
        break;
    default:
        break;
    }

    return result;
}

void box_gpu_effects_run(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureView color)
{
    box_gpu_effects_t *fx = gpu->effects;
    box_gpu_fx_target_t *ft = fx ? fx->current : NULL;
    box_gpu_fx_blend_t final = v->clear.enabled ? BOX_GPU_FX_REPLACE : BOX_GPU_FX_OVER;
    int32_t i, current = 0;
    bool written = false;
    if (!ft) {
        goto done;
    }

    box_gpu_fx_ctx_t ctx = { .t = t, .ft = ft, .encoder = encoder, .packet = packet, .v = v };
    for (i = 0; i < v->effect_count; i ++) {
        const box_render_effect_t *e = ecs_vec_get_t(
            &packet->effects, box_render_effect_t, v->effect_first + i);
        bool last = i == v->effect_count - 1;
        ctx.input = ft->hdr_views[current];
        ctx.output = last ? color : ft->hdr_views[current ^ 1];
        ctx.format = last ? t->format : BOX_GPU_HDR_FORMAT;
        ctx.blend = last ? final : BOX_GPU_FX_REPLACE;
        if (!box_gpu_fx_effect(gpu, &ctx, e)) {
            continue;
        }

        if (last) {
            written = true;
        } else {
            current ^= 1;
        }
    }

    if (!written) {
        ctx.input = ft->hdr_views[current];
        ctx.output = color;
        ctx.format = t->format;
        ctx.blend = final;
        box_gpu_fx_tonemap(gpu, &ctx, NULL);
    }

    fx->current = NULL;
done:
    return;
}

void box_gpu_effects_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_effects_t *fx = gpu->effects;
    int32_t i;
    if (!fx) {
        goto done;
    }

    for (i = ecs_vec_count(&fx->states) - 1; i >= 0; i --) {
        box_gpu_fx_state_t *s = *ecs_vec_get_t(&fx->states, box_gpu_fx_state_t*, i);
        if (s->frame != packet->frame) {
            box_gpu_fx_state_free(s);
            ecs_vec_remove_t(&fx->states, box_gpu_fx_state_t*, i);
        }
    }

    for (i = ecs_vec_count(&fx->targets) - 1; i >= 0; i --) {
        box_gpu_fx_target_t *ft = *ecs_vec_get_t(&fx->targets, box_gpu_fx_target_t*, i);
        if (ft->frame != packet->frame) {
            box_gpu_fx_target_free(ft);
            ecs_vec_remove_t(&fx->targets, box_gpu_fx_target_t*, i);
        }
    }

done:
    box_gpu_gtao_sweep(gpu, packet);
}

#endif
