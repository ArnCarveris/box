/**
 * @file render/gpu_spectrum.c
 * @brief Spectra of WaterSpectrum in the webgpu backend: per spectrum an initial spectrum buffer,
 * a buffer of the rows transformed and an rgba16float texture array with mips, a layer per
 * cascade, which compute passes fill before the targets (water_spectrum.wgsl), and the spectrum
 * of the scene of a view bound in its view group for the Water fragments (mesh_extended.wgsl).
 *
 * When a spectrum is new or its revision changed the spectrum pass draws its initial spectrum.
 * Every frame the rows pass evolves it to the time and inverse FFTs its rows, the columns pass
 * its columns into mip 0, and the mip passes average each mip into the next.
 */

#include "gpu.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

/** Invocations of a workgroup of the spectrum and mip passes along X and Y */
#define BOX_GPU_SPECTRUM_GROUP (8u)

/** Mips of a texture array of the largest resolution, log2 of
 * BOX_MATERIAL_SPECTRUM_RESOLUTION_MAX plus one */
#define BOX_GPU_SPECTRUM_MIPS (9)

/** Format of the texture arrays of the spectra */
#define BOX_GPU_SPECTRUM_FORMAT WGPUTextureFormat_RGBA16Float

/** Params of water_spectrum.wgsl */
typedef struct box_gpu_spectrum_params_t {
    uint32_t grid[4];               /**< Texels per side, cascades, log2 of the texels per
                                     *   side, 1 to transform the initial spectrum as is */
    uint32_t seed[4];               /**< Seed, three unused */
    float sizes[4];                 /**< World units of the tile of each cascade, time */
    float cuts[4];                  /**< Wave numbers between the cascades */
    float wind[4];                  /**< Wind direction X and Z, wind speed, directionality */
    float shape[4];                 /**< Amplitude, three unused */
} box_gpu_spectrum_params_t;

/** The spectrum members of View of lights.wgsl (spectrum_a, spectrum_b, spectrum_c) */
typedef struct box_gpu_spectrum_record_t {
    float a[4];                     /**< World XZ offset of the tiles, detail, height scale */
    float b[4];                     /**< 1 / world units of the tile of each cascade, unused */
    uint32_t c[4];                  /**< Cascades, 0 without a spectrum, three unused */
} box_gpu_spectrum_record_t;

/** A spectrum of the backend */
typedef struct box_gpu_spectrum_t {
    ecs_entity_t water;             /**< Owner of the WaterSpectrum, the key with the scene */
    ecs_entity_t scene;             /**< Scene root of the shapes */
    uint32_t resolution;            /**< Texels per side */
    uint32_t cascades;              /**< Layers of the texture array */
    uint32_t mips;                  /**< Mips of the texture array */
    uint32_t revision;              /**< Revision of the initial spectrum in the buffer */
    bool generated;                 /**< The initial spectrum of revision was drawn */
    bool evolved;                   /**< The texture array holds the waves of a frame */
    WGPUBuffer params;              /**< box_gpu_spectrum_params_t, uniform */
    WGPUBuffer initial;             /**< h0(k) and h0(-k) of every texel, vec4f */
    WGPUBuffer transformed;         /**< The rows transformed, vec4f per texel */
    WGPUTexture texture;            /**< The texture array */
    WGPUTextureView view;           /**< Its 2D array view of every mip, which the views bind */
    WGPUTextureView levels[BOX_GPU_SPECTRUM_MIPS]; /**< 2D array view of each mip */
    WGPUBindGroup group;            /**< Group of the spectrum, rows and columns passes */
    WGPUBindGroup mip_groups[BOX_GPU_SPECTRUM_MIPS]; /**< mip_groups[l] reads levels[l - 1] and
                                     *   writes levels[l], from 1 */
    box_gpu_spectrum_record_t record; /**< Record the views of its scene take */
} box_gpu_spectrum_t;

struct box_gpu_spectra_t {
    WGPUBindGroupLayout layout;     /**< Params, initial, transformed, mip 0 written */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the spectrum, rows and columns
                                     *   passes */
    WGPUBindGroupLayout mip_layout; /**< Mip read, mip written */
    WGPUPipelineLayout mip_pipeline_layout; /**< Pipeline layout of the mip pass */
    WGPUComputePipeline spectrum;   /**< Draws the initial spectrum */
    WGPUComputePipeline rows;       /**< Evolves and transforms the rows */
    WGPUComputePipeline columns;    /**< Transforms the columns into mip 0 */
    WGPUComputePipeline mip;        /**< Averages a mip into the next */
    int32_t revision;               /**< Revision of the shader the pipelines use */
    WGPUTexture none;               /**< 1x1 zero array of one layer, bound by views without a
                                     *   spectrum */
    WGPUTextureView none_view;      /**< Its view */
    ecs_vec_t spectra;              /**< box_gpu_spectrum_t* */
};

/** Release a webgpu object when it exists.
 *
 * @param obj The object
 * @param release The release function of its type
 */
#define BOX_GPU_SPECTRUM_RELEASE(obj, release) \
    if (obj) { \
        release(obj); \
        obj = NULL; \
    }

/** Create a texture array of the spectra.
 *
 * @param gpu The backend
 * @param size Texels per side
 * @param layers Layers
 * @param mips Mips
 * @return The texture, zero
 */
static WGPUTexture box_gpu_spectrum_texture(
    box_gpu_t *gpu,
    uint32_t size,
    uint32_t layers,
    uint32_t mips)
{
    return wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { size, size, layers },
        .format = BOX_GPU_SPECTRUM_FORMAT,
        .mipLevelCount = mips,
        .sampleCount = 1
    });
}

/** Create a 2D array view of a range of the mips of a texture array.
 *
 * @param texture The texture
 * @param first First mip
 * @param count Mips
 * @param layers Layers
 * @return The view
 */
static WGPUTextureView box_gpu_spectrum_view(
    WGPUTexture texture,
    uint32_t first,
    uint32_t count,
    uint32_t layers)
{
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = BOX_GPU_SPECTRUM_FORMAT;
    desc.dimension = WGPUTextureViewDimension_2DArray;
    desc.baseMipLevel = first;
    desc.mipLevelCount = count;
    desc.baseArrayLayer = 0;
    desc.arrayLayerCount = layers;
    desc.aspect = WGPUTextureAspect_All;

    return wgpuTextureCreateView(texture, &desc);
}

/** Get the layout entry of a written mip of a compute pass.
 *
 * @param binding The binding
 * @return The entry
 */
static WGPUBindGroupLayoutEntry box_gpu_spectrum_storage(
    uint32_t binding)
{
    return (WGPUBindGroupLayoutEntry){
        .binding = binding,
        .visibility = WGPUShaderStage_Compute,
        .storageTexture = {
            .access = WGPUStorageTextureAccess_WriteOnly,
            .format = BOX_GPU_SPECTRUM_FORMAT,
            .viewDimension = WGPUTextureViewDimension_2DArray
        }
    };
}

/** Create a pipeline layout of one bind group layout.
 *
 * @param gpu The backend
 * @param layout The bind group layout
 * @return The pipeline layout
 */
static WGPUPipelineLayout box_gpu_spectrum_pipeline_layout(
    box_gpu_t *gpu,
    WGPUBindGroupLayout layout)
{
    return wgpuDeviceCreatePipelineLayout(gpu->device, &(WGPUPipelineLayoutDescriptor){
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &layout
    });
}

void box_gpu_spectra_init(
    box_gpu_t *gpu)
{
    box_gpu_spectra_t *s = ecs_os_calloc_t(box_gpu_spectra_t);
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_spectrum_params_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        box_gpu_spectrum_storage(3)
    };
    s->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 4, .entries = entries });
    s->pipeline_layout = box_gpu_spectrum_pipeline_layout(gpu, s->layout);

    WGPUBindGroupLayoutEntry mip_entries[2] = {
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        },
        box_gpu_spectrum_storage(5)
    };
    s->mip_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = mip_entries });
    s->mip_pipeline_layout = box_gpu_spectrum_pipeline_layout(gpu, s->mip_layout);

    s->none = box_gpu_spectrum_texture(gpu, 1, 1, 1);
    s->none_view = box_gpu_spectrum_view(s->none, 0, 1, 1);
    ecs_vec_init_t(NULL, &s->spectra, box_gpu_spectrum_t*, 0);
    gpu->spectra = s;
}

/** Release the buffers, textures and groups of a spectrum.
 *
 * @param f The spectrum
 */
static void box_gpu_spectrum_release(
    box_gpu_spectrum_t *f)
{
    int32_t i;
    BOX_GPU_SPECTRUM_RELEASE(f->group, wgpuBindGroupRelease);
    for (i = 0; i < BOX_GPU_SPECTRUM_MIPS; i ++) {
        BOX_GPU_SPECTRUM_RELEASE(f->mip_groups[i], wgpuBindGroupRelease);
        BOX_GPU_SPECTRUM_RELEASE(f->levels[i], wgpuTextureViewRelease);
    }

    BOX_GPU_SPECTRUM_RELEASE(f->view, wgpuTextureViewRelease);
    BOX_GPU_SPECTRUM_RELEASE(f->texture, wgpuTextureRelease);
    BOX_GPU_SPECTRUM_RELEASE(f->params, wgpuBufferRelease);
    BOX_GPU_SPECTRUM_RELEASE(f->initial, wgpuBufferRelease);
    BOX_GPU_SPECTRUM_RELEASE(f->transformed, wgpuBufferRelease);
}

/** Release a spectrum and free it.
 *
 * @param f The spectrum
 */
static void box_gpu_spectrum_free(
    box_gpu_spectrum_t *f)
{
    box_gpu_spectrum_release(f);
    ecs_os_free(f);
}

void box_gpu_spectra_fini(
    box_gpu_t *gpu)
{
    box_gpu_spectra_t *s = gpu->spectra;
    int32_t i;
    if (!s) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&s->spectra); i ++) {
        box_gpu_spectrum_free(*ecs_vec_get_t(&s->spectra, box_gpu_spectrum_t*, i));
    }

    BOX_GPU_SPECTRUM_RELEASE(s->spectrum, wgpuComputePipelineRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->rows, wgpuComputePipelineRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->columns, wgpuComputePipelineRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->mip, wgpuComputePipelineRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->mip_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->mip_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->none_view, wgpuTextureViewRelease);
    BOX_GPU_SPECTRUM_RELEASE(s->none, wgpuTextureRelease);
    ecs_vec_fini_t(NULL, &s->spectra, box_gpu_spectrum_t*);
    ecs_os_free(s);
    gpu->spectra = NULL;
done:
    return;
}

/** Create a compute pipeline of an entry point of water_spectrum.wgsl.
 *
 * @param gpu The backend
 * @param layout The pipeline layout
 * @param module The module
 * @param entry The entry point
 * @return The pipeline, NULL while it is created
 */
static WGPUComputePipeline box_gpu_spectrum_compute(
    box_gpu_t *gpu,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    const char *entry)
{
    WGPUComputePipelineDescriptor desc = {
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    };

    return box_gpu_compute_pipeline(gpu, &desc);
}

/** Make the compute pipelines match the shader; the previous ones stay until every new one
 * exists.
 *
 * @param gpu The backend
 * @return Whether they exist
 */
static bool box_gpu_spectra_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_spectra_t *s = gpu->spectra;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_WATER_SPECTRUM);
    if (!shader || !shader->module || (s->rows && s->revision == shader->revision)) {
        goto done;
    }

    WGPUComputePipeline *current[4] = { &s->spectrum, &s->rows, &s->columns, &s->mip };
    WGPUComputePipeline next[4] = {
        box_gpu_spectrum_compute(gpu, s->pipeline_layout, shader->module, "spectrum"),
        box_gpu_spectrum_compute(gpu, s->pipeline_layout, shader->module, "rows"),
        box_gpu_spectrum_compute(gpu, s->pipeline_layout, shader->module, "columns"),
        box_gpu_spectrum_compute(gpu, s->mip_pipeline_layout, shader->module, "mip")
    };
    if (box_gpu_compute_pipelines_swap(current, next, 4)) {
        s->revision = shader->revision;
    }

done:
    return shader && shader->module && s->spectrum && s->rows && s->columns && s->mip;
}

/** Create the buffers, the texture array and the groups of a spectrum of a size.
 *
 * @param gpu The backend
 * @param f The spectrum, released first
 * @param resolution Texels per side, a power of two
 * @param cascades Layers
 */
static void box_gpu_spectrum_create(
    box_gpu_t *gpu,
    box_gpu_spectrum_t *f,
    uint32_t resolution,
    uint32_t cascades)
{
    box_gpu_spectra_t *s = gpu->spectra;
    uint64_t bytes = (uint64_t)resolution * resolution * cascades * 4 * sizeof(float);
    uint32_t mips = 1, l;
    box_gpu_spectrum_release(f);
    while ((resolution >> mips) && mips < BOX_GPU_SPECTRUM_MIPS) {
        mips ++;
    }

    f->resolution = resolution;
    f->cascades = cascades;
    f->mips = mips;
    f->generated = false;
    f->evolved = false;
    f->params = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(box_gpu_spectrum_params_t)
    });
    f->initial = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = bytes
    });
    f->transformed = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage,
        .size = bytes
    });
    f->texture = box_gpu_spectrum_texture(gpu, resolution, cascades, mips);
    f->view = box_gpu_spectrum_view(f->texture, 0, mips, cascades);
    for (l = 0; l < mips; l ++) {
        f->levels[l] = box_gpu_spectrum_view(f->texture, l, 1, cascades);
    }

    WGPUBindGroupEntry entries[4] = {
        {
            .binding = 0,
            .buffer = f->params,
            .offset = 0,
            .size = sizeof(box_gpu_spectrum_params_t)
        },
        { .binding = 1, .buffer = f->initial, .offset = 0, .size = bytes },
        { .binding = 2, .buffer = f->transformed, .offset = 0, .size = bytes },
        { .binding = 3, .textureView = f->levels[0] }
    };
    f->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->layout,
        .entryCount = 4,
        .entries = entries
    });
    for (l = 1; l < mips; l ++) {
        WGPUBindGroupEntry mip[2] = {
            { .binding = 4, .textureView = f->levels[l - 1] },
            { .binding = 5, .textureView = f->levels[l] }
        };
        f->mip_groups[l] = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = s->mip_layout,
            .entryCount = 2,
            .entries = mip
        });
    }
}

/** Get the log2 of a power of two.
 *
 * @param v The power of two
 * @return Its log2
 */
static uint32_t box_gpu_spectrum_log2(
    uint32_t v)
{
    uint32_t result = 0;
    while ((1u << result) < v) {
        result ++;
    }

    return result;
}

/** Fill the params of a record of a packet.
 *
 * @param rec The record
 * @param raw Whether the passes transform the initial spectrum as is
 * @param out The params
 */
static void box_gpu_spectrum_params(
    const box_render_water_spectrum_t *rec,
    bool raw,
    box_gpu_spectrum_params_t *out)
{
    int32_t c;
    ecs_os_memset_t(out, 0, box_gpu_spectrum_params_t);
    out->grid[0] = rec->resolution;
    out->grid[1] = rec->cascades;
    out->grid[2] = box_gpu_spectrum_log2(rec->resolution);
    out->grid[3] = raw ? 1u : 0u;
    out->seed[0] = rec->seed;
    for (c = 0; c < BOX_RENDER_SPECTRUM_CASCADES; c ++) {
        out->sizes[c] = rec->sizes[c];
    }

    out->sizes[3] = rec->time;
    for (c = 0; c <= BOX_RENDER_SPECTRUM_CASCADES; c ++) {
        out->cuts[c] = rec->cuts[c];
    }

    out->wind[0] = rec->wind[0];
    out->wind[1] = rec->wind[1];
    out->wind[2] = rec->wind_speed;
    out->wind[3] = rec->directionality;
    out->shape[0] = rec->amplitude;
}

/** Record the passes that transform a spectrum into its texture array: the initial spectrum
 * when asked, the rows, the columns and the mips.
 *
 * @param s The state
 * @param pass The compute pass
 * @param f The spectrum
 * @param generate Whether to draw the initial spectrum first
 */
static void box_gpu_spectrum_dispatch(
    const box_gpu_spectra_t *s,
    WGPUComputePassEncoder pass,
    const box_gpu_spectrum_t *f,
    bool generate)
{
    uint32_t n = f->resolution, l;
    uint32_t groups = (n + BOX_GPU_SPECTRUM_GROUP - 1) / BOX_GPU_SPECTRUM_GROUP;
    wgpuComputePassEncoderSetBindGroup(pass, 0, f->group, 0, NULL);
    if (generate) {
        wgpuComputePassEncoderSetPipeline(pass, s->spectrum);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, groups, f->cascades);
    }

    wgpuComputePassEncoderSetPipeline(pass, s->rows);
    wgpuComputePassEncoderDispatchWorkgroups(pass, n, f->cascades, 1);
    wgpuComputePassEncoderSetPipeline(pass, s->columns);
    wgpuComputePassEncoderDispatchWorkgroups(pass, n, f->cascades, 1);
    wgpuComputePassEncoderSetPipeline(pass, s->mip);
    for (l = 1; l < f->mips; l ++) {
        uint32_t size = n >> l;
        uint32_t mip_groups = (size + BOX_GPU_SPECTRUM_GROUP - 1) / BOX_GPU_SPECTRUM_GROUP;
        wgpuComputePassEncoderSetBindGroup(pass, 0, f->mip_groups[l], 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, mip_groups, mip_groups, f->cascades);
    }
}

/** Keep the record the views of the scene of a spectrum take.
 *
 * @param f The spectrum
 * @param rec Its record of the packet
 */
static void box_gpu_spectrum_record(
    box_gpu_spectrum_t *f,
    const box_render_water_spectrum_t *rec)
{
    int32_t c;
    ecs_os_memset_t(&f->record, 0, box_gpu_spectrum_record_t);
    if (!f->evolved) {
        goto done;
    }

    f->record.a[0] = rec->offset[0];
    f->record.a[1] = rec->offset[1];
    f->record.a[2] = rec->detail;
    f->record.a[3] = rec->height_scale;
    for (c = 0; c < (int32_t)rec->cascades && c < BOX_RENDER_SPECTRUM_CASCADES; c ++) {
        f->record.b[c] = rec->sizes[c] > 0.0f ? 1.0f / rec->sizes[c] : 0.0f;
    }

    f->record.c[0] = rec->cascades;
    f->record.c[1] = rec->resolution;
    f->record.c[2] = f->mips;
done:
    return;
}

void box_gpu_spectra_evolve(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_spectra_t *s = gpu->spectra;
    int32_t i, count = ecs_vec_count(&packet->water_spectra);
    if (!s) {
        goto done;
    }

    for (i = ecs_vec_count(&s->spectra) - 1; i >= 0; i --) {
        box_gpu_spectrum_t *f = *ecs_vec_get_t(&s->spectra, box_gpu_spectrum_t*, i);
        bool used = false;
        int32_t j;
        for (j = 0; j < count && !used; j ++) {
            const box_render_water_spectrum_t *rec = ecs_vec_get_t(
                &packet->water_spectra, box_render_water_spectrum_t, j);
            used = rec->water == f->water && rec->scene == f->scene;
        }

        if (!used) {
            box_gpu_spectrum_free(f);
            ecs_vec_remove_t(&s->spectra, box_gpu_spectrum_t*, i);
        }
    }

    if (!count) {
        goto done;
    }

    bool ready = box_gpu_spectra_pipelines(gpu);
    WGPUCommandEncoder encoder = NULL;
    WGPUComputePassEncoder pass = NULL;
    for (i = 0; i < count; i ++) {
        const box_render_water_spectrum_t *rec = ecs_vec_get_t(
            &packet->water_spectra, box_render_water_spectrum_t, i);
        box_gpu_spectrum_t *f = NULL;
        int32_t j;
        for (j = 0; j < ecs_vec_count(&s->spectra) && !f; j ++) {
            box_gpu_spectrum_t *g = *ecs_vec_get_t(&s->spectra, box_gpu_spectrum_t*, j);
            if (g->water == rec->water && g->scene == rec->scene) {
                f = g;
            }
        }

        if (!f) {
            f = ecs_os_calloc_t(box_gpu_spectrum_t);
            f->water = rec->water;
            f->scene = rec->scene;
            *ecs_vec_append_t(NULL, &s->spectra, box_gpu_spectrum_t*) = f;
        }

        if (!f->texture || f->resolution != rec->resolution || f->cascades != rec->cascades) {
            box_gpu_spectrum_create(gpu, f, rec->resolution, rec->cascades);
        }

        if (ready) {
            box_gpu_spectrum_params_t params;
            bool generate = !f->generated || f->revision != rec->revision;
            box_gpu_spectrum_params(rec, false, &params);
            wgpuQueueWriteBuffer(gpu->queue, f->params, 0, &params, sizeof(params));
            if (!encoder) {
                encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
                pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
            }

            box_gpu_spectrum_dispatch(s, pass, f, generate);
            f->generated = true;
            f->revision = rec->revision;
            f->evolved = true;
        }

        box_gpu_spectrum_record(f, rec);
    }

    if (encoder) {
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

done:
    return;
}

/** Find the spectrum of the scene of a view.
 *
 * @param gpu The backend
 * @param v The view, NULL for none
 * @return The first spectrum of its scene, NULL for none
 */
static const box_gpu_spectrum_t* box_gpu_spectra_of_view(
    const box_gpu_t *gpu,
    const box_render_view_t *v)
{
    const box_gpu_spectra_t *s = gpu->spectra;
    const box_gpu_spectrum_t *result = NULL;
    int32_t i;
    for (i = 0; s && v && i < ecs_vec_count(&s->spectra) && !result; i ++) {
        const box_gpu_spectrum_t *f = *ecs_vec_get_t(&s->spectra, box_gpu_spectrum_t*, i);
        if (f->scene == v->scene && f->view) {
            result = f;
        }
    }

    return result;
}

void box_gpu_spectra_layout(
    WGPUBindGroupLayoutEntry *entries)
{
    entries[BOX_GPU_SPECTRUM_BINDING] = (WGPUBindGroupLayoutEntry){
        .binding = BOX_GPU_SPECTRUM_BINDING,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2DArray
        }
    };
}

void box_gpu_spectra_bindings(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    WGPUBindGroupEntry *entries)
{
    const box_gpu_spectrum_t *f = box_gpu_spectra_of_view(gpu, v);
    entries[BOX_GPU_SPECTRUM_BINDING] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_SPECTRUM_BINDING,
        .textureView = f ? f->view : gpu->spectra->none_view
    };
}

void box_gpu_spectra_uniforms(
    const box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u)
{
    const box_gpu_spectrum_t *f = box_gpu_spectra_of_view(gpu, v);
    box_gpu_spectrum_record_t none = {0};
    const box_gpu_spectrum_record_t *r = f ? &f->record : &none;
    ecs_os_memcpy_n(u->spectrum_a, r->a, float, 4);
    ecs_os_memcpy_n(u->spectrum_b, r->b, float, 4);
    ecs_os_memcpy_n(u->spectrum_c, r->c, uint32_t, 4);
}

void box_gpu_water_fields_init(
    box_gpu_t *gpu)
{
    box_gpu_ripples_init(gpu);
    box_gpu_spectra_init(gpu);
}

void box_gpu_water_fields_fini(
    box_gpu_t *gpu)
{
    box_gpu_ripples_fini(gpu);
    box_gpu_spectra_fini(gpu);
}

void box_gpu_water_fields_layout(
    WGPUBindGroupLayoutEntry *entries)
{
    box_gpu_ripples_layout(entries);
    box_gpu_spectra_layout(entries);
}

void box_gpu_water_fields_uniforms(
    const box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u)
{
    box_gpu_ripples_uniforms(gpu, v, u);
    box_gpu_spectra_uniforms(gpu, v, u);
}

/** Convert a half float to a float.
 *
 * @param h The bits of the half
 * @return The value
 */
static float box_gpu_spectrum_half(
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

/** Get the backend of a world that renders on the calling thread and has a device.
 *
 * @param world The world
 * @return The backend, NULL when it is not ready
 */
static box_gpu_t* box_gpu_spectrum_backend(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    box_gpu_t *result = NULL;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    result = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == result->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "reading a spectrum needs RenderSettings.main_thread and box_render_sync");
    result = renders ? result : NULL;
done:
    return result;
}

/** Copy mip 0 of a layer of a texture array into memory as floats, four per texel.
 *
 * @param gpu The backend
 * @param texture The texture array
 * @param resolution Texels per side
 * @param layer The layer
 * @param out Receives four times resolution squared floats
 * @return Whether it was copied
 */
static bool box_gpu_spectrum_copy(
    box_gpu_t *gpu,
    WGPUTexture texture,
    uint32_t resolution,
    uint32_t layer,
    float *out)
{
    uint32_t row = resolution * 4 * (uint32_t)sizeof(uint16_t), x, z, c;
    uint32_t stride = (row + BOX_GPU_ROW_ALIGN - 1) / BOX_GPU_ROW_ALIGN * BOX_GPU_ROW_ALIGN;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)stride * resolution
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = texture,
            .mipLevel = 0,
            .origin = { 0, 0, layer },
            .aspect = WGPUTextureAspect_All
        },
        &(WGPUTexelCopyBufferInfo){
            .layout = { .bytesPerRow = stride, .rowsPerImage = resolution },
            .buffer = buffer
        },
        &(WGPUExtent3D){ resolution, resolution, 1 });
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    uint16_t *texels = ecs_os_malloc((ecs_size_t)(stride * resolution));
    bool result = box_gpu_read(gpu, buffer, 0, stride * resolution, texels);
    wgpuBufferRelease(buffer);
    for (z = 0; result && z < resolution; z ++) {
        const uint16_t *line = texels + (size_t)z * (stride / sizeof(uint16_t));
        for (x = 0; x < resolution; x ++) {
            for (c = 0; c < 4; c ++) {
                out[((size_t)z * resolution + x) * 4 + c] =
                    box_gpu_spectrum_half(line[x * 4 + c]);
            }
        }
    }

    ecs_os_free(texels);

    return result;
}

bool box_render_gpu_read_spectrum(
    ecs_world_t *world,
    ecs_entity_t water,
    int32_t cascade,
    int32_t *resolution,
    float *out,
    int32_t count)
{
    bool result = false;
    box_gpu_t *gpu = box_gpu_spectrum_backend(world);
    const box_gpu_spectrum_t *f = NULL;
    int32_t i;
    for (i = 0; gpu && gpu->spectra && i < ecs_vec_count(&gpu->spectra->spectra) && !f; i ++) {
        const box_gpu_spectrum_t *g = *ecs_vec_get_t(
            &gpu->spectra->spectra, box_gpu_spectrum_t*, i);
        if (g->water == water) {
            f = g;
        }
    }

    if (!f || !f->texture || !f->evolved || cascade < 0 || cascade >= (int32_t)f->cascades) {
        goto done;
    }

    if (resolution) {
        *resolution = (int32_t)f->resolution;
    }

    if (!out) {
        result = true;
        goto done;
    }

    if ((int64_t)f->resolution * f->resolution * 4 > count) {
        goto done;
    }

    result = box_gpu_spectrum_copy(gpu, f->texture, f->resolution, (uint32_t)cascade, out);
done:
    return result;
}

bool box_render_gpu_spectrum_fft(
    ecs_world_t *world,
    int32_t resolution,
    const float *in,
    float *out)
{
    bool result = false;
    box_gpu_t *gpu = box_gpu_spectrum_backend(world);
    box_gpu_spectrum_t f = {0};
    if (!gpu || !gpu->spectra || resolution < BOX_MATERIAL_SPECTRUM_RESOLUTION_MIN ||
        resolution > BOX_MATERIAL_SPECTRUM_RESOLUTION_MAX ||
        (resolution & (resolution - 1)) || !box_gpu_spectra_pipelines(gpu))
    {
        goto done;
    }

    box_render_water_spectrum_t rec = {
        .resolution = (uint32_t)resolution,
        .cascades = 1,
        .sizes = { 1.0f },
        .cuts = { 0.0f, BOX_RENDER_SPECTRUM_K_MAX }
    };
    box_gpu_spectrum_params_t params;
    box_gpu_spectrum_create(gpu, &f, (uint32_t)resolution, 1);
    box_gpu_spectrum_params(&rec, true, &params);
    wgpuQueueWriteBuffer(gpu->queue, f.params, 0, &params, sizeof(params));
    wgpuQueueWriteBuffer(gpu->queue, f.initial, 0, in,
        (size_t)resolution * (size_t)resolution * 4 * sizeof(float));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    box_gpu_spectrum_dispatch(gpu->spectra, pass, &f, false);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    result = box_gpu_spectrum_copy(gpu, f.texture, (uint32_t)resolution, 0, out);
    box_gpu_spectrum_release(&f);
done:
    return result;
}

#else

bool box_render_gpu_read_spectrum(
    ecs_world_t *world,
    ecs_entity_t water,
    int32_t cascade,
    int32_t *resolution,
    float *out,
    int32_t count)
{
    (void)world;
    (void)water;
    (void)cascade;
    (void)resolution;
    (void)out;
    (void)count;

    return false;
}

bool box_render_gpu_spectrum_fft(
    ecs_world_t *world,
    int32_t resolution,
    const float *in,
    float *out)
{
    (void)world;
    (void)resolution;
    (void)in;
    (void)out;

    return false;
}

#endif
