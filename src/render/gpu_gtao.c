/**
 * @file render/gpu_gtao.c
 * @brief Ambient occlusion (Gtao) of the webgpu backend, a port of XeGTAO.
 *
 * Gtao is not a pass of the effect chain: its passes run between the depth prepass and the
 * forward pass of a view and write the ambient occlusion image the forward pass binds. The
 * images are kept per target and view while a view applies Gtao every frame.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Format of the depth mips, AO terms, edges and AO image of Gtao, a core storage format */
#define BOX_GPU_GTAO_FORMAT (WGPUTextureFormat_R32Float)

/** Mips of the view depth of Gtao (XE_GTAO_DEPTH_MIP_LEVELS) */
#define BOX_GPU_GTAO_MIPS (5)

/** Smallest edge of the depth mips of Gtao, so that the last mip has a texel */
#define BOX_GPU_GTAO_MIN_EDGE (16)

/** Invocations along each axis of a workgroup of the Gtao passes */
#define BOX_GPU_GTAO_GROUP (8)

/** Most denoise passes of Gtao */
#define BOX_GPU_GTAO_DENOISE_MAX (3)

/** Compute passes of Gtao: the prefilter to mip 3, the prefilter of mip 4, the main pass, the
 * denoise and the last denoise */
#define BOX_GPU_GTAO_PASSES (5)

/** Private settings of XeGTAO with its defaults: the radius multiplier, the sample distribution
 * power, the thin occluder compensation, the depth mip sampling offset and the blur beta of the
 * denoise, which is 1e4 (no blur) without denoise passes */
#define BOX_GPU_GTAO_RADIUS_MULTIPLIER (1.457f)
#define BOX_GPU_GTAO_SAMPLE_POWER (2.0f)
#define BOX_GPU_GTAO_THIN_OCCLUDER (0.0f)
#define BOX_GPU_GTAO_MIP_OFFSET (3.3f)
#define BOX_GPU_GTAO_BLUR_BETA (1.2f)
#define BOX_GPU_GTAO_NO_BLUR_BETA (1e4f)

/** A compute pass of Gtao */
typedef enum box_gpu_gtao_pass_t {
    BOX_GPU_GTAO_PREFILTER,         /**< View depth and its mips 1 to 3 */
    BOX_GPU_GTAO_PREFILTER_LAST,    /**< Mip 4 of the view depth */
    BOX_GPU_GTAO_MAIN,              /**< AO term and edges */
    BOX_GPU_GTAO_DENOISE,           /**< A denoise pass */
    BOX_GPU_GTAO_DENOISE_FINAL      /**< The last denoise pass, which scales the term */
} box_gpu_gtao_pass_t;

/** What a binding of a Gtao pass holds */
typedef enum box_gpu_gtao_binding_t {
    BOX_GPU_GTAO_UNIFORMS,          /**< The uniforms */
    BOX_GPU_GTAO_DEPTH,             /**< The depth of the target */
    BOX_GPU_GTAO_READ,              /**< An R32Float image read with textureLoad */
    BOX_GPU_GTAO_WRITE              /**< An R32Float storage image */
} box_gpu_gtao_binding_t;

/** Uniforms of the Gtao passes, matching Gtao in etc/shaders/gtao.wgsl */
typedef struct box_gpu_gtao_uniforms_t {
    float viewport[4];              /**< Working width, height and their inverse */
    float ndc_to_view[4];           /**< NDC to view multiplier xy and addend xy, for a working
                                     *   pixel position in 0..1 */
    float unpack[4];                /**< NDC to view multiplier times the working pixel size xy,
                                     *   the numerator and addend of the depth unpack */
    float effect[4];                /**< Radius times the multiplier, falloff range, final value
                                     *   power, denoise blur beta */
    float quality[4];               /**< Slices, steps per slice, sample distribution power,
                                     *   thin occluder compensation */
    float extent[4];                /**< Depth mip sampling offset, 1 at half resolution, two
                                     *   unused */
} box_gpu_gtao_uniforms_t;

/** Images of the ambient occlusion of a view in a target: R32Float at the working resolution,
 * the render size or half of it rounded up, except the AO image the forward pass reads at the
 * render size */
typedef struct box_gpu_gtao_view_t {
    uint32_t target;                /**< Handle of the target */
    ecs_entity_t view;              /**< View entity */
    int64_t frame;                  /**< Frame of the last packet that used the images */
    WGPUBuffer uniforms;            /**< Uniforms of the passes */
    uint32_t width;                 /**< Render width */
    uint32_t height;                /**< Render height */
    uint32_t work_width;            /**< Working width */
    uint32_t work_height;           /**< Working height */
    WGPUTexture depth;              /**< View depth and its mips, at least BOX_GPU_GTAO_MIN_EDGE */
    WGPUTextureView depth_view;     /**< View of every mip */
    WGPUTextureView depth_mips[BOX_GPU_GTAO_MIPS]; /**< View of each mip */
    WGPUTexture terms[2];           /**< AO terms the main pass and denoise alternate between */
    WGPUTextureView term_views[2];  /**< Views of the terms */
    WGPUTexture edges;              /**< Packed edges of the main pass */
    WGPUTextureView edges_view;     /**< View of the edges */
    WGPUTexture ao;                 /**< AO image at the render size */
    WGPUTextureView ao_view;        /**< View of the AO image */
    WGPUTextureView source;         /**< Depth of the target the bind groups read */
    WGPUBindGroup prefilter;        /**< Bindings of the prefilter to mip 3 */
    WGPUBindGroup prefilter_last;   /**< Bindings of the prefilter of mip 4 */
    WGPUBindGroup main_pass;        /**< Bindings of the main pass */
    WGPUBindGroup denoise[2];       /**< Bindings of a denoise of term i into the other term */
    WGPUBindGroup denoise_ao[2];    /**< Bindings of a denoise of term i into the AO image */
    WGPUBindGroup upsample[2];      /**< Bindings of the upsample of term i */
} box_gpu_gtao_view_t;

struct box_gpu_gtao_t {
    WGPUBindGroupLayout layouts[BOX_GPU_GTAO_PASSES]; /**< Bindings of each compute pass */
    WGPUPipelineLayout pipeline_layouts[BOX_GPU_GTAO_PASSES]; /**< Pipeline layouts of those */
    WGPUComputePipeline pipelines[BOX_GPU_GTAO_PASSES]; /**< Pipeline of each compute pass */
    WGPUBindGroupLayout upsample_layout; /**< Bindings of the bilateral upsample */
    WGPUPipelineLayout upsample_pipeline_layout; /**< Pipeline layout of the upsample */
    WGPURenderPipeline upsample;    /**< Bilateral upsample of a half resolution term */
    int32_t revision;               /**< Revision of the Gtao shader of the pipelines */
    WGPUTexture white;              /**< 1x1 R32Float of 1, the AO of a view without Gtao */
    WGPUTextureView white_view;     /**< View of the white image */
    ecs_vec_t views;                /**< box_gpu_gtao_view_t* */
};

/** Entry point of each compute pass in etc/shaders/gtao.wgsl */
static const char *box_gpu_gtao_entries[BOX_GPU_GTAO_PASSES] = {
    "prefilter", "prefilter_last", "main_pass", "denoise", "denoise_final"
};

/** Describe a binding of a Gtao pass.
 *
 * @param binding Binding number in etc/shaders/gtao.wgsl
 * @param stage Stage that reads it
 * @param kind What it holds
 * @return The layout entry
 */
static WGPUBindGroupLayoutEntry box_gpu_gtao_entry(
    uint32_t binding,
    WGPUShaderStage stage,
    box_gpu_gtao_binding_t kind)
{
    WGPUBindGroupLayoutEntry result = { .binding = binding, .visibility = stage };
    switch (kind) {
    case BOX_GPU_GTAO_UNIFORMS:
        result.buffer.type = WGPUBufferBindingType_Uniform;
        break;
    case BOX_GPU_GTAO_DEPTH:
        result.texture.sampleType = WGPUTextureSampleType_Depth;
        result.texture.viewDimension = WGPUTextureViewDimension_2D;
        break;
    case BOX_GPU_GTAO_READ:
        result.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        result.texture.viewDimension = WGPUTextureViewDimension_2D;
        break;
    case BOX_GPU_GTAO_WRITE:
        result.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        result.storageTexture.format = BOX_GPU_GTAO_FORMAT;
        result.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        break;
    }

    return result;
}

/** Create a bind group layout of a Gtao pass and its pipeline layout.
 *
 * @param gpu The backend
 * @param entries The bindings
 * @param count Number of bindings
 * @param pipeline_layout Receives the pipeline layout
 * @return The bind group layout
 */
static WGPUBindGroupLayout box_gpu_gtao_layout(
    box_gpu_t *gpu,
    const WGPUBindGroupLayoutEntry *entries,
    size_t count,
    WGPUPipelineLayout *pipeline_layout)
{
    WGPUBindGroupLayout result = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = count, .entries = entries });
    *pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &result });

    return result;
}

void box_gpu_gtao_init(
    box_gpu_t *gpu)
{
    box_gpu_gtao_t *g = ecs_os_calloc_t(box_gpu_gtao_t);
    WGPUShaderStage comp = WGPUShaderStage_Compute;
    WGPUShaderStage frag = WGPUShaderStage_Fragment;
    WGPUBindGroupLayoutEntry prefilter[6] = {
        box_gpu_gtao_entry(0, comp, BOX_GPU_GTAO_UNIFORMS),
        box_gpu_gtao_entry(1, comp, BOX_GPU_GTAO_DEPTH),
        box_gpu_gtao_entry(2, comp, BOX_GPU_GTAO_WRITE),
        box_gpu_gtao_entry(3, comp, BOX_GPU_GTAO_WRITE),
        box_gpu_gtao_entry(4, comp, BOX_GPU_GTAO_WRITE),
        box_gpu_gtao_entry(5, comp, BOX_GPU_GTAO_WRITE)
    };
    WGPUBindGroupLayoutEntry last[3] = {
        box_gpu_gtao_entry(0, comp, BOX_GPU_GTAO_UNIFORMS),
        box_gpu_gtao_entry(6, comp, BOX_GPU_GTAO_READ),
        box_gpu_gtao_entry(7, comp, BOX_GPU_GTAO_WRITE)
    };
    WGPUBindGroupLayoutEntry main_pass[4] = {
        box_gpu_gtao_entry(0, comp, BOX_GPU_GTAO_UNIFORMS),
        box_gpu_gtao_entry(8, comp, BOX_GPU_GTAO_READ),
        box_gpu_gtao_entry(9, comp, BOX_GPU_GTAO_WRITE),
        box_gpu_gtao_entry(10, comp, BOX_GPU_GTAO_WRITE)
    };
    WGPUBindGroupLayoutEntry denoise[4] = {
        box_gpu_gtao_entry(0, comp, BOX_GPU_GTAO_UNIFORMS),
        box_gpu_gtao_entry(11, comp, BOX_GPU_GTAO_READ),
        box_gpu_gtao_entry(12, comp, BOX_GPU_GTAO_READ),
        box_gpu_gtao_entry(13, comp, BOX_GPU_GTAO_WRITE)
    };
    WGPUBindGroupLayoutEntry upsample[4] = {
        box_gpu_gtao_entry(0, frag, BOX_GPU_GTAO_UNIFORMS),
        box_gpu_gtao_entry(1, frag, BOX_GPU_GTAO_DEPTH),
        box_gpu_gtao_entry(8, frag, BOX_GPU_GTAO_READ),
        box_gpu_gtao_entry(11, frag, BOX_GPU_GTAO_READ)
    };
    float one = 1.0f;
    g->layouts[BOX_GPU_GTAO_PREFILTER] = box_gpu_gtao_layout(gpu, prefilter, 6,
        &g->pipeline_layouts[BOX_GPU_GTAO_PREFILTER]);
    g->layouts[BOX_GPU_GTAO_PREFILTER_LAST] = box_gpu_gtao_layout(gpu, last, 3,
        &g->pipeline_layouts[BOX_GPU_GTAO_PREFILTER_LAST]);
    g->layouts[BOX_GPU_GTAO_MAIN] = box_gpu_gtao_layout(gpu, main_pass, 4,
        &g->pipeline_layouts[BOX_GPU_GTAO_MAIN]);
    g->layouts[BOX_GPU_GTAO_DENOISE] = box_gpu_gtao_layout(gpu, denoise, 4,
        &g->pipeline_layouts[BOX_GPU_GTAO_DENOISE]);
    g->layouts[BOX_GPU_GTAO_DENOISE_FINAL] = g->layouts[BOX_GPU_GTAO_DENOISE];
    g->pipeline_layouts[BOX_GPU_GTAO_DENOISE_FINAL] = g->pipeline_layouts[BOX_GPU_GTAO_DENOISE];
    g->upsample_layout = box_gpu_gtao_layout(gpu, upsample, 4, &g->upsample_pipeline_layout);
    g->white = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = BOX_GPU_GTAO_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    g->white_view = wgpuTextureCreateView(g->white, NULL);
    WGPUTexelCopyTextureInfo dst = { .texture = g->white, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyBufferLayout layout = { .bytesPerRow = sizeof(float), .rowsPerImage = 1 };
    WGPUExtent3D extent = { 1, 1, 1 };
    wgpuQueueWriteTexture(gpu->queue, &dst, &one, sizeof(one), &layout, &extent);
    ecs_vec_init_t(NULL, &g->views, box_gpu_gtao_view_t*, 0);
    gpu->gtao = g;
}

/** Release a texture and its view when they exist.
 *
 * @param texture The texture
 * @param view The view
 */
static void box_gpu_gtao_release(
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

/** Release a bind group when it exists.
 *
 * @param group The bind group, set to NULL
 */
static void box_gpu_gtao_group_release(
    WGPUBindGroup *group)
{
    if (*group) {
        wgpuBindGroupRelease(*group);
        *group = NULL;
    }
}

/** Release the bind groups of the Gtao passes of a view.
 *
 * @param s The state
 */
static void box_gpu_gtao_groups_free(
    box_gpu_gtao_view_t *s)
{
    int32_t i;
    box_gpu_gtao_group_release(&s->prefilter);
    box_gpu_gtao_group_release(&s->prefilter_last);
    box_gpu_gtao_group_release(&s->main_pass);
    for (i = 0; i < 2; i ++) {
        box_gpu_gtao_group_release(&s->denoise[i]);
        box_gpu_gtao_group_release(&s->denoise_ao[i]);
        box_gpu_gtao_group_release(&s->upsample[i]);
    }

    s->source = NULL;
}

/** Release the images of the ambient occlusion of a view.
 *
 * @param s The images
 */
static void box_gpu_gtao_images_free(
    box_gpu_gtao_view_t *s)
{
    int32_t i;
    box_gpu_gtao_groups_free(s);
    for (i = 0; i < BOX_GPU_GTAO_MIPS; i ++) {
        if (s->depth_mips[i]) {
            wgpuTextureViewRelease(s->depth_mips[i]);
            s->depth_mips[i] = NULL;
        }
    }

    box_gpu_gtao_release(&s->depth, &s->depth_view);
    box_gpu_gtao_release(&s->terms[0], &s->term_views[0]);
    box_gpu_gtao_release(&s->terms[1], &s->term_views[1]);
    box_gpu_gtao_release(&s->edges, &s->edges_view);
    box_gpu_gtao_release(&s->ao, &s->ao_view);
    s->width = 0;
    s->height = 0;
    s->work_width = 0;
    s->work_height = 0;
}

/** Release the ambient occlusion of a view and everything it owns.
 *
 * @param s The state
 */
static void box_gpu_gtao_view_free(
    box_gpu_gtao_view_t *s)
{
    box_gpu_gtao_images_free(s);
    if (s->uniforms) {
        wgpuBufferRelease(s->uniforms);
    }

    ecs_os_free(s);
}

/** Release the pipelines of Gtao.
 *
 * @param g The Gtao state
 */
static void box_gpu_gtao_pipelines_free(
    box_gpu_gtao_t *g)
{
    int32_t i;
    for (i = 0; i < BOX_GPU_GTAO_PASSES; i ++) {
        if (g->pipelines[i]) {
            wgpuComputePipelineRelease(g->pipelines[i]);
            g->pipelines[i] = NULL;
        }
    }

    if (g->upsample) {
        wgpuRenderPipelineRelease(g->upsample);
        g->upsample = NULL;
    }
}

void box_gpu_gtao_fini(
    box_gpu_t *gpu)
{
    box_gpu_gtao_t *g = gpu->gtao;
    int32_t i;
    if (!g) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&g->views); i ++) {
        box_gpu_gtao_view_free(*ecs_vec_get_t(&g->views, box_gpu_gtao_view_t*, i));
    }

    ecs_vec_fini_t(NULL, &g->views, box_gpu_gtao_view_t*);
    box_gpu_gtao_pipelines_free(g);
    for (i = 0; i < BOX_GPU_GTAO_DENOISE_FINAL; i ++) {
        wgpuPipelineLayoutRelease(g->pipeline_layouts[i]);
        wgpuBindGroupLayoutRelease(g->layouts[i]);
    }

    wgpuPipelineLayoutRelease(g->upsample_pipeline_layout);
    wgpuBindGroupLayoutRelease(g->upsample_layout);
    box_gpu_gtao_release(&g->white, &g->white_view);
    ecs_os_free(g);
    gpu->gtao = NULL;
done:
    return;
}

WGPUTextureView box_gpu_gtao_white(
    box_gpu_t *gpu)
{
    return gpu->gtao->white_view;
}

/** Get the pipelines of Gtao, created again when the Gtao shader was recompiled; the previous
 * ones stay until every new one exists.
 *
 * @param gpu The backend
 * @return Whether every pipeline exists
 */
static bool box_gpu_gtao_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_gtao_t *g = gpu->gtao;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_GTAO);
    WGPUShaderModule module = shader ? shader->module : NULL;
    WGPUComputePipeline next[BOX_GPU_GTAO_PASSES];
    WGPUComputePipeline *current[BOX_GPU_GTAO_PASSES];
    WGPURenderPipeline *upsample_current = &g->upsample, upsample;
    int32_t i;
    if (!module || (g->upsample && g->revision == shader->revision)) {
        goto done;
    }

    for (i = 0; i < BOX_GPU_GTAO_PASSES; i ++) {
        current[i] = &g->pipelines[i];
        next[i] = box_gpu_compute_pipeline(gpu,
            &(WGPUComputePipelineDescriptor){
                .layout = g->pipeline_layouts[i],
                .compute = { .module = module,
                    .entryPoint = { .data = box_gpu_gtao_entries[i], .length = WGPU_STRLEN } }
            });
    }

    WGPUColorTargetState target = {
        .format = BOX_GPU_GTAO_FORMAT,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "upsample", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    upsample = box_gpu_render_pipeline(gpu, &(WGPURenderPipelineDescriptor){
        .layout = g->upsample_pipeline_layout,
        .vertex = { .module = module, .entryPoint = { .data = "vs", .length = WGPU_STRLEN } },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    });
    if (!upsample) {
        box_gpu_compute_pipelines_release(next, BOX_GPU_GTAO_PASSES);
    } else if (box_gpu_compute_pipelines_swap(current, next, BOX_GPU_GTAO_PASSES)) {
        box_gpu_render_pipelines_swap(&upsample_current, &upsample, 1);
        g->revision = shader->revision;
    } else {
        wgpuRenderPipelineRelease(upsample);
    }

done:
    return module != NULL && g->upsample != NULL;
}

/** Create an R32Float image the Gtao passes write and read.
 *
 * @param gpu The backend
 * @param width Width in pixels
 * @param height Height in pixels
 * @param mips Mip levels
 * @param usage Usage besides storage and texture binding
 * @param view Receives the view of every mip
 * @return The texture
 */
static WGPUTexture box_gpu_gtao_texture(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    uint32_t mips,
    WGPUTextureUsage usage,
    WGPUTextureView *view)
{
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding | usage,
        .dimension = WGPUTextureDimension_2D,
        .size = { width ? width : 1, height ? height : 1, 1 },
        .format = BOX_GPU_GTAO_FORMAT,
        .mipLevelCount = mips,
        .sampleCount = 1
    });
    *view = wgpuTextureCreateView(result, NULL);

    return result;
}

/** Get the edge of the depth mips of Gtao for an edge of the working resolution.
 *
 * @param edge The working edge
 * @return The edge, at least BOX_GPU_GTAO_MIN_EDGE
 */
static uint32_t box_gpu_gtao_edge(
    uint32_t edge)
{
    return edge > BOX_GPU_GTAO_MIN_EDGE ? edge : BOX_GPU_GTAO_MIN_EDGE;
}

/** Make the images of the ambient occlusion of a view match the render size and resolution,
 * created on first use and again on a resize.
 *
 * @param gpu The backend
 * @param t The target
 * @param s The images
 * @param half Whether it computes at half the render size
 */
static void box_gpu_gtao_images(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    box_gpu_gtao_view_t *s,
    bool half)
{
    uint32_t w = (uint32_t)(t->desc.render_width > 0 ? t->desc.render_width : 1);
    uint32_t h = (uint32_t)(t->desc.render_height > 0 ? t->desc.render_height : 1);
    uint32_t ww = half ? (w + 1) / 2 : w, wh = half ? (h + 1) / 2 : h;
    int32_t i;
    if (s->ao && s->width == w && s->height == h && s->work_width == ww &&
        s->work_height == wh)
    {
        goto done;
    }

    box_gpu_gtao_images_free(s);
    s->width = w;
    s->height = h;
    s->work_width = ww;
    s->work_height = wh;
    s->depth = box_gpu_gtao_texture(gpu, box_gpu_gtao_edge(ww), box_gpu_gtao_edge(wh),
        BOX_GPU_GTAO_MIPS, WGPUTextureUsage_None, &s->depth_view);
    for (i = 0; i < BOX_GPU_GTAO_MIPS; i ++) {
        s->depth_mips[i] = wgpuTextureCreateView(s->depth, &(WGPUTextureViewDescriptor){
            .format = BOX_GPU_GTAO_FORMAT,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = (uint32_t)i,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        });
    }

    for (i = 0; i < 2; i ++) {
        s->terms[i] = box_gpu_gtao_texture(gpu, ww, wh, 1, WGPUTextureUsage_None,
            &s->term_views[i]);
    }

    s->edges = box_gpu_gtao_texture(gpu, ww, wh, 1, WGPUTextureUsage_None, &s->edges_view);
    s->ao = box_gpu_gtao_texture(gpu, w, h, 1, WGPUTextureUsage_RenderAttachment, &s->ao_view);
done:
    return;
}

/** Find the ambient occlusion of a view in a target.
 *
 * @param g The Gtao state
 * @param t The target
 * @param v The view
 * @return The state, NULL when the view has none
 */
static box_gpu_gtao_view_t* box_gpu_gtao_view_find(
    const box_gpu_gtao_t *g,
    const box_gpu_target_t *t,
    const box_render_view_t *v)
{
    box_gpu_gtao_view_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&g->views) && !result; i ++) {
        box_gpu_gtao_view_t *s = *ecs_vec_get_t(&g->views, box_gpu_gtao_view_t*, i);
        if (s->target == t->handle && s->view == v->view) {
            result = s;
        }
    }

    return result;
}

/** Fill the uniforms of the Gtao passes from the projection of the view: tan(fov / 2) per axis,
 * the view position of a working pixel and the view depth of a reversed Z depth, proj[3][2] /
 * (depth + proj[2][2]), for a finite far plane and for none.
 *
 * A working pixel at half resolution holds the depth of the top left pixel of its block: its
 * position u in 0..1 is (2 ww / w) u - 0.5 / w of the render size.
 *
 * @param t The target
 * @param v The view
 * @param s The images, at the working resolution
 * @param out Receives the uniforms
 */
static void box_gpu_gtao_settings(
    const box_gpu_target_t *t,
    const box_render_view_t *v,
    const box_gpu_gtao_view_t *s,
    box_gpu_gtao_uniforms_t *out)
{
    static const float slices[4] = { 1, 2, 3, 9 };
    static const float steps[4] = { 2, 2, 3, 3 };
    const Gtao *a = &v->gtao.settings;
    bool half = s->work_width != s->width || s->work_height != s->height;
    float w = (float)s->width, h = (float)s->height;
    float ww = (float)s->work_width, wh = (float)s->work_height;
    float scale = half ? 2.0f : 1.0f;
    mat4 proj;
    ecs_os_memset_t(out, 0, box_gpu_gtao_uniforms_t);
    box_camera_projection(&v->camera_data,
        (float)t->desc.point_width, (float)t->desc.point_height, proj);
    float tan_x = 1.0f / proj[0][0], tan_y = 1.0f / proj[1][1];
    float mul_x = tan_x * 2.0f, mul_y = tan_y * -2.0f;
    out->viewport[0] = ww;
    out->viewport[1] = wh;
    out->viewport[2] = 1.0f / ww;
    out->viewport[3] = 1.0f / wh;
    out->ndc_to_view[0] = mul_x * (scale * ww / w);
    out->ndc_to_view[1] = mul_y * (scale * wh / h);
    out->ndc_to_view[2] = -tan_x + (half ? mul_x * -0.5f / w : 0.0f);
    out->ndc_to_view[3] = tan_y + (half ? mul_y * -0.5f / h : 0.0f);
    out->unpack[0] = mul_x * scale / w;
    out->unpack[1] = mul_y * scale / h;
    out->unpack[2] = proj[3][2];
    out->unpack[3] = proj[2][2];
    out->effect[0] = a->radius * BOX_GPU_GTAO_RADIUS_MULTIPLIER;
    out->effect[1] = a->falloff_range;
    out->effect[2] = a->final_value_power;
    out->effect[3] = a->denoise_passes > 0 ? BOX_GPU_GTAO_BLUR_BETA : BOX_GPU_GTAO_NO_BLUR_BETA;
    out->quality[0] = slices[a->quality];
    out->quality[1] = steps[a->quality];
    out->quality[2] = BOX_GPU_GTAO_SAMPLE_POWER;
    out->quality[3] = BOX_GPU_GTAO_THIN_OCCLUDER;
    out->extent[0] = BOX_GPU_GTAO_MIP_OFFSET;
    out->extent[1] = half ? 1.0f : 0.0f;
}

WGPUTextureView box_gpu_gtao_begin(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    box_gpu_gtao_t *g = gpu->gtao;
    WGPUTextureView result = NULL;
    if (!g || !v->gtao.enabled || !box_gpu_gtao_pipelines(gpu)) {
        goto done;
    }

    box_gpu_gtao_view_t *s = box_gpu_gtao_view_find(g, t, v);
    if (!s) {
        s = ecs_os_calloc_t(box_gpu_gtao_view_t);
        s->target = t->handle;
        s->view = v->view;
        s->uniforms = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(box_gpu_gtao_uniforms_t)
        });
        *ecs_vec_append_t(NULL, &g->views, box_gpu_gtao_view_t*) = s;
    }

    s->frame = packet->frame;
    box_gpu_gtao_images(gpu, t, s, v->gtao.settings.half_resolution);
    result = s->ao_view;
done:
    return result;
}

/** Create a bind group of a Gtao pass.
 *
 * @param gpu The backend
 * @param layout The bind group layout of the pass
 * @param entries The bindings
 * @param count Number of bindings
 * @return The bind group
 */
static WGPUBindGroup box_gpu_gtao_group(
    box_gpu_t *gpu,
    WGPUBindGroupLayout layout,
    const WGPUBindGroupEntry *entries,
    size_t count)
{
    return wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = layout,
        .entryCount = count,
        .entries = entries
    });
}

/** Make the bind groups of the Gtao passes of a view match its images and the depth of the
 * target, created again when either was made again.
 *
 * @param gpu The backend
 * @param t The target
 * @param s The state, its images created
 */
static void box_gpu_gtao_groups(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    box_gpu_gtao_view_t *s)
{
    box_gpu_gtao_t *g = gpu->gtao;
    int32_t i;
    if (s->prefilter && s->source == t->depth_view) {
        goto done;
    }

    box_gpu_gtao_groups_free(s);
    s->source = t->depth_view;
    WGPUBindGroupEntry uniforms = {
        .binding = 0, .buffer = s->uniforms, .size = sizeof(box_gpu_gtao_uniforms_t)
    };
    WGPUBindGroupEntry prefilter[6] = {
        uniforms,
        { .binding = 1, .textureView = t->depth_view },
        { .binding = 2, .textureView = s->depth_mips[0] },
        { .binding = 3, .textureView = s->depth_mips[1] },
        { .binding = 4, .textureView = s->depth_mips[2] },
        { .binding = 5, .textureView = s->depth_mips[3] }
    };
    WGPUBindGroupEntry last[3] = {
        uniforms,
        { .binding = 6, .textureView = s->depth_mips[BOX_GPU_GTAO_MIPS - 2] },
        { .binding = 7, .textureView = s->depth_mips[BOX_GPU_GTAO_MIPS - 1] }
    };
    WGPUBindGroupEntry main_pass[4] = {
        uniforms,
        { .binding = 8, .textureView = s->depth_view },
        { .binding = 9, .textureView = s->term_views[0] },
        { .binding = 10, .textureView = s->edges_view }
    };
    s->prefilter = box_gpu_gtao_group(gpu, g->layouts[BOX_GPU_GTAO_PREFILTER], prefilter, 6);
    s->prefilter_last = box_gpu_gtao_group(gpu, g->layouts[BOX_GPU_GTAO_PREFILTER_LAST],
        last, 3);
    s->main_pass = box_gpu_gtao_group(gpu, g->layouts[BOX_GPU_GTAO_MAIN], main_pass, 4);
    for (i = 0; i < 2; i ++) {
        WGPUBindGroupEntry denoise[4] = {
            uniforms,
            { .binding = 11, .textureView = s->term_views[i] },
            { .binding = 12, .textureView = s->edges_view },
            { .binding = 13, .textureView = s->term_views[i ^ 1] }
        };
        WGPUBindGroupEntry upsample[4] = {
            uniforms,
            { .binding = 1, .textureView = t->depth_view },
            { .binding = 8, .textureView = s->depth_view },
            { .binding = 11, .textureView = s->term_views[i] }
        };
        s->denoise[i] = box_gpu_gtao_group(gpu, g->layouts[BOX_GPU_GTAO_DENOISE], denoise, 4);
        denoise[3].textureView = s->ao_view;
        s->denoise_ao[i] = box_gpu_gtao_group(gpu, g->layouts[BOX_GPU_GTAO_DENOISE],
            denoise, 4);
        s->upsample[i] = box_gpu_gtao_group(gpu, g->upsample_layout, upsample, 4);
    }

done:
    return;
}

/** Dispatch a Gtao compute pass.
 *
 * @param gpu The backend
 * @param cp The compute pass
 * @param pass The pass
 * @param group The bind group of the pass
 * @param x Workgroups along x
 * @param y Workgroups along y
 */
static void box_gpu_gtao_dispatch(
    box_gpu_t *gpu,
    WGPUComputePassEncoder cp,
    box_gpu_gtao_pass_t pass,
    WGPUBindGroup group,
    uint32_t x,
    uint32_t y)
{
    wgpuComputePassEncoderSetPipeline(cp, gpu->gtao->pipelines[pass]);
    wgpuComputePassEncoderSetBindGroup(cp, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(cp, x ? x : 1, y ? y : 1, 1);
}

/** Record the bilateral upsample of a half resolution term into the AO image.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param s The state
 * @param term Index of the term that holds the denoised result
 */
static void box_gpu_gtao_upsample(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_gpu_gtao_view_t *s,
    int32_t term)
{
    WGPURenderPassColorAttachment attachment = {
        .view = s->ao_view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassEncoder rp = box_gpu_profile_render_pass(gpu, encoder,
        &(WGPURenderPassDescriptor){ .colorAttachmentCount = 1,
            .colorAttachments = &attachment }, "gtao upsample");
    wgpuRenderPassEncoderSetPipeline(rp, gpu->gtao->upsample);
    wgpuRenderPassEncoderSetBindGroup(rp, 0, s->upsample[term], 0, NULL);
    wgpuRenderPassEncoderDraw(rp, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);
}

void box_gpu_gtao_record(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_view_t *v)
{
    box_gpu_gtao_view_t *s = gpu->gtao ? box_gpu_gtao_view_find(gpu->gtao, t, v) : NULL;
    box_gpu_gtao_uniforms_t u;
    if (!s || !s->ao || !box_gpu_gtao_pipelines(gpu)) {
        goto done;
    }

    bool half = s->work_width != s->width || s->work_height != s->height;
    uint32_t ww = s->work_width, wh = s->work_height, group = BOX_GPU_GTAO_GROUP;
    uint32_t last_w = box_gpu_gtao_edge(ww) >> (BOX_GPU_GTAO_MIPS - 1);
    uint32_t last_h = box_gpu_gtao_edge(wh) >> (BOX_GPU_GTAO_MIPS - 1);
    int32_t i, passes = glm_imax(v->gtao.settings.denoise_passes, 1), current = 0;
    box_gpu_gtao_settings(t, v, s, &u);
    wgpuQueueWriteBuffer(gpu->queue, s->uniforms, 0, &u, sizeof(u));
    box_gpu_gtao_groups(gpu, t, s);
    WGPUComputePassEncoder cp = box_gpu_profile_compute_pass(gpu, encoder, "gtao");
    box_gpu_gtao_dispatch(gpu, cp, BOX_GPU_GTAO_PREFILTER, s->prefilter,
        (ww + group * 2 - 1) / (group * 2), (wh + group * 2 - 1) / (group * 2));
    box_gpu_gtao_dispatch(gpu, cp, BOX_GPU_GTAO_PREFILTER_LAST, s->prefilter_last,
        (last_w + group - 1) / group, (last_h + group - 1) / group);
    box_gpu_gtao_dispatch(gpu, cp, BOX_GPU_GTAO_MAIN, s->main_pass,
        (ww + group - 1) / group, (wh + group - 1) / group);
    for (i = 0; i < passes; i ++) {
        bool final = i == passes - 1;
        box_gpu_gtao_dispatch(gpu, cp,
            final ? BOX_GPU_GTAO_DENOISE_FINAL : BOX_GPU_GTAO_DENOISE,
            final && !half ? s->denoise_ao[current] : s->denoise[current],
            (ww + group * 2 - 1) / (group * 2), (wh + group - 1) / group);
        current ^= 1;
    }

    wgpuComputePassEncoderEnd(cp);
    wgpuComputePassEncoderRelease(cp);
    if (half) {
        box_gpu_gtao_upsample(gpu, encoder, s, current);
    }

done:
    return;
}

void box_gpu_gtao_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_gtao_t *g = gpu->gtao;
    int32_t i;
    for (i = g ? ecs_vec_count(&g->views) - 1 : -1; i >= 0; i --) {
        box_gpu_gtao_view_t *s = *ecs_vec_get_t(&g->views, box_gpu_gtao_view_t*, i);
        if (s->frame != packet->frame) {
            box_gpu_gtao_view_free(s);
            ecs_vec_remove_t(&g->views, box_gpu_gtao_view_t*, i);
        }
    }
}

#endif
