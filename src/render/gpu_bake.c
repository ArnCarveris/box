/**
 * @file render/gpu_bake.c
 * @brief Irradiance probe volumes of the webgpu backend and the captures that bake them.
 *
 * A volume is a uniform with its grid, two storage buffers with the SH of every probe and one
 * with the validity mask, bound as group 3 of the forward pass with the probe buffer the view
 * shows. Views without a volume bind a 1x1x1 volume that keeps the hemisphere.
 *
 * A capture renders six faces (color and depth arrays) shaded with one probe buffer as DDGI does,
 * bake_sh.wgsl projects them onto SH in the other buffer with the direct light of Baked lights.
 * Captures run in chunks of command buffers; timestamp queries around them time the GPU.
 */

#include "gpu.h"
#include <float.h>

#ifdef BOX_RENDER_WGPU

/** Bytes of the SH of one probe */
#define BOX_GPU_PROBE_SIZE (BOX_RENDER_SH_COEFFICIENTS * 4 * sizeof(float))

/** Probes captured by one command buffer at most */
#define BOX_GPU_BAKE_CHUNK (32)

/** Visible slots the faces of a chunk may use at most */
#define BOX_GPU_BAKE_VISIBLE (1 << 22)

/** Texels of the face layers at most */
#define BOX_GPU_BAKE_TEXELS (1 << 22)

/** Near plane of the faces in world units */
#define BOX_GPU_BAKE_NEAR (0.01f)

/** Timestamp measurements in flight at most */
#define BOX_GPU_BAKE_READBACKS (4)

/** Milliseconds a measurement of the captures of one packet may take at most, longer ones are
 * stale timestamps or a stalled queue and are dropped */
#define BOX_GPU_BAKE_MEASURE_MAX (1000.0)

/** Byte alignment of the destination of a query resolve */
#define BOX_GPU_BAKE_RESOLVE_ALIGN (256)

/** Depth bias of the capture cascade in texels, as the View default */
#define BOX_GPU_BAKE_SHADOW_BIAS (2.0f)

/** Normal offset of the capture cascade in texels, as the View default */
#define BOX_GPU_BAKE_SHADOW_OFFSET (1.5f)

/** PCF taps per axis of the capture cascade, as the View default */
#define BOX_GPU_BAKE_SHADOW_FILTER (3)

/** View depth beyond every capture, where the capture cascade would end */
#define BOX_GPU_BAKE_FAR (1e30f)

/** State of a timestamp readback */
typedef enum box_gpu_bake_readback_state_t {
    BOX_GPU_BAKE_FREE,              /**< Unused */
    BOX_GPU_BAKE_WRITTEN,           /**< The timestamps are submitted, resolved a packet later */
    BOX_GPU_BAKE_COPIED,            /**< The copy is submitted, mapped at the next capture */
    BOX_GPU_BAKE_MAPPING,           /**< Waiting for the map */
    BOX_GPU_BAKE_MAPPED,            /**< Mapped, the timestamps can be read */
    BOX_GPU_BAKE_FAILED             /**< The map failed */
} box_gpu_bake_readback_state_t;

/** A copy of the two timestamps around the captures of a packet */
typedef struct box_gpu_bake_readback_t {
    WGPUBuffer buffer;              /**< Mappable copy of the resolved timestamps */
    int32_t state;                  /**< box_gpu_bake_readback_state_t */
    int32_t probes;                 /**< Probes the measured captures held */
} box_gpu_bake_readback_t;

/** A measurement of the captures of a packet from the CPU, used without timestamp queries:
 * from the start of their recording until the queue reports their work done */
typedef struct box_gpu_bake_done_t {
    struct box_gpu_bake_t *bake;    /**< The capture state */
    box_gpu_t *gpu;                 /**< The backend */
    uint64_t start;                 /**< ecs_os_now when the captures started recording */
    int32_t probes;                 /**< Probes the captures held, 0 while the slot is free */
} box_gpu_bake_done_t;

/** The timestamps around the captures of a packet */
typedef struct box_gpu_bake_timer_t {
    int32_t readback;               /**< Readback of the timestamps, -1 when not measured */
    bool begun;                     /**< The first cull pass wrote the starting timestamp */
    bool ended;                     /**< The last projection wrote the ending timestamp */
} box_gpu_bake_timer_t;

/** Uniforms of the SH projection, as in Params of etc/shaders/bake_sh.wgsl */
typedef struct box_gpu_bake_params_t {
    uint32_t size[4];               /**< Face edge in texels, probes of the chunk */
    float sky[4];                   /**< Linear ambient light from above */
    float ground[4];                /**< Linear ambient light from below */
    uint32_t probes[BOX_GPU_BAKE_CHUNK]; /**< Volume index of each probe of the chunk */
    uint32_t lights[4];             /**< Baked lights, the directional ones among them first */
    float near_[4];                 /**< Near plane of the faces */
    float positions[BOX_GPU_BAKE_CHUNK][4]; /**< World position of each probe of the chunk */
} box_gpu_bake_params_t;

struct box_gpu_bake_t {
    WGPUBindGroupLayout layout;     /**< Bind group layout of the projection */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the projection */
    WGPUComputePipeline project;    /**< Projection pipeline, NULL until the shader exists */
    int32_t revision;               /**< Revision of the shader the pipeline was made with */
    WGPUBuffer uniforms;            /**< View uniforms of the faces of a chunk */
    WGPUBuffer params;              /**< box_gpu_bake_params_t of a chunk */
    WGPUBuffer clusters;            /**< Cluster buffer the faces share in turn */
    ecs_vec_t view_data;            /**< Bytes of the view uniforms of a chunk */
    int32_t size;                   /**< Face edge of the face layers, 0 before they exist */
    int32_t layers;                 /**< Face layers, six per probe of a chunk */
    WGPUTexture color;              /**< rgba16float array of the face layers */
    WGPUTextureView color_view;     /**< Array view the projection reads */
    ecs_vec_t layer_views;          /**< WGPUTextureView, the view each face renders into */
    WGPUTexture depth;              /**< Depth32float array of the face layers */
    WGPUTextureView depth_view;     /**< Array view the projection reads */
    ecs_vec_t depth_views;          /**< WGPUTextureView, the depth each face renders into */
    WGPUQuerySet queries;           /**< Two timestamps per readback, NULL without the feature */
    WGPUBuffer resolve;             /**< Resolved timestamps */
    box_gpu_bake_readback_t readbacks[BOX_GPU_BAKE_READBACKS]; /**< Timestamp copies */
    box_gpu_bake_done_t done[BOX_GPU_BAKE_READBACKS]; /**< Measurements without timestamps */
    box_render_bake_timing_t timing; /**< Measured time not yet taken, under the gpu lock */
    ecs_vec_t captured;             /**< box_render_bake_result_t not taken, under the gpu lock */
};

/** Axis a face looks along and its up axis, the right axis is their cross product */
static const float box_gpu_bake_faces[6][2][3] = {
    {{1, 0, 0}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 1, 0}},
    {{0, 1, 0}, {0, 0, -1}},
    {{0, -1, 0}, {0, 0, 1}},
    {{0, 0, 1}, {0, 1, 0}},
    {{0, 0, -1}, {0, 1, 0}}
};

/** Create a buffer that is written from the CPU.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_bake_buffer(
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

/** Release the buffers and bind group of a volume and the volume.
 *
 * @param v The volume
 */
static void box_gpu_volume_free(
    box_gpu_volume_t *v)
{
    int32_t i;
    for (i = 0; i < 2; i ++) {
        wgpuBindGroupRelease(v->group[i]);
        wgpuBufferRelease(v->probes[i]);
    }

    wgpuBufferRelease(v->grid);
    wgpuBufferRelease(v->valid);
    ecs_os_free(v);
}

/** Create a volume with its buffers and a bind group per probe buffer.
 *
 * @param gpu The backend
 * @param count Probes of the volume
 * @return The volume
 */
static box_gpu_volume_t* box_gpu_volume_new(
    box_gpu_t *gpu,
    uint32_t count)
{
    box_gpu_volume_t *v = ecs_os_calloc_t(box_gpu_volume_t);
    uint64_t probes = (uint64_t)count * BOX_GPU_PROBE_SIZE;
    uint64_t valid = (uint64_t)(count + 31) / 32 * sizeof(uint32_t);
    int32_t i;
    v->count = count;
    v->grid = box_gpu_bake_buffer(gpu, sizeof(box_gpu_volume_uniforms_t),
        WGPUBufferUsage_Uniform);
    v->valid = box_gpu_bake_buffer(gpu, valid, WGPUBufferUsage_Storage);
    for (i = 0; i < 2; i ++) {
        v->probes[i] = box_gpu_bake_buffer(gpu, probes,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBindGroupEntry entries[3] = {
            { .binding = 0, .buffer = v->grid, .size = sizeof(box_gpu_volume_uniforms_t) },
            { .binding = 1, .buffer = v->probes[i], .size = probes },
            { .binding = 2, .buffer = v->valid, .size = valid }
        };
        WGPUBindGroupDescriptor desc = {
            .layout = gpu->volume_layout,
            .entryCount = 3,
            .entries = entries
        };
        v->group[i] = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }

    return v;
}

/** Write the grid of a volume, fill every probe of both buffers with one SH and write its
 * validity mask.
 *
 * @param gpu The backend
 * @param v The volume
 * @param desc The grid and the SH, followed by the mask
 */
static void box_gpu_volume_write(
    box_gpu_t *gpu,
    box_gpu_volume_t *v,
    const box_render_volume_desc_t *desc)
{
    box_gpu_volume_uniforms_t u = {
        .origin = { desc->origin[0], desc->origin[1], desc->origin[2], desc->spacing },
        .size = { desc->size[0], desc->size[1], desc->size[2], 1 }
    };
    size_t size = (size_t)v->count * BOX_GPU_PROBE_SIZE;
    uint8_t *probes = ecs_os_malloc((ecs_size_t)size);
    uint32_t i;
    for (i = 0; i < v->count; i ++) {
        ecs_os_memcpy(probes + i * BOX_GPU_PROBE_SIZE, desc->sh, BOX_GPU_PROBE_SIZE);
    }

    wgpuQueueWriteBuffer(gpu->queue, v->grid, 0, &u, sizeof(u));
    wgpuQueueWriteBuffer(gpu->queue, v->probes[0], 0, probes, size);
    wgpuQueueWriteBuffer(gpu->queue, v->probes[1], 0, probes, size);
    wgpuQueueWriteBuffer(gpu->queue, v->valid, 0, desc + 1,
        (size_t)(v->count + 31) / 32 * sizeof(uint32_t));
    ecs_os_free(probes);
}

/** Write the validity mask of a volume, after copying one probe buffer into the other for a
 * BOX_RENDER_VOLUME_COPY; the probes keep their values otherwise.
 *
 * @param gpu The backend
 * @param v The volume
 * @param desc The op, followed by the mask
 */
static void box_gpu_volume_keep(
    box_gpu_t *gpu,
    box_gpu_volume_t *v,
    const box_render_volume_desc_t *desc)
{
    if (desc->op == BOX_RENDER_VOLUME_COPY && desc->from < 2) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, v->probes[desc->from], 0,
            v->probes[1 - desc->from], 0, (uint64_t)v->count * BOX_GPU_PROBE_SIZE);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

    wgpuQueueWriteBuffer(gpu->queue, v->valid, 0, desc + 1,
        (size_t)(v->count + 31) / 32 * sizeof(uint32_t));
}

/** Copy the probes of both buffers of a volume that a resized grid keeps into the volume of
 * that grid, one run of x per row the two grids share.
 *
 * @param gpu The backend
 * @param from The volume of the old grid
 * @param to The volume of the new grid, already filled
 * @param desc The resize: new size, old size and the shift from new to old indices
 */
static void box_gpu_volume_carry(
    box_gpu_t *gpu,
    const box_gpu_volume_t *from,
    box_gpu_volume_t *to,
    const box_render_volume_desc_t *desc)
{
    const uint32_t *n = desc->size, *o = desc->old_size;
    const int32_t *d = desc->shift;
    int32_t x0 = glm_max(0, -d[0]), x1 = glm_min((int32_t)n[0], (int32_t)o[0] - d[0]);
    int32_t y, z, i;
    uint64_t probes = (uint64_t)o[0] * o[1] * o[2];
    if (x1 <= x0 || probes != from->count) {
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (z = 0; z < (int32_t)n[2]; z ++) {
        for (y = 0; y < (int32_t)n[1]; y ++) {
            int32_t oy = y + d[1], oz = z + d[2];
            if (oy < 0 || oz < 0 || oy >= (int32_t)o[1] || oz >= (int32_t)o[2]) {
                continue;
            }

            uint64_t src = (uint64_t)(x0 + d[0]) + ((uint64_t)oy + (uint64_t)oz * o[1]) * o[0];
            uint64_t dst = (uint64_t)x0 + ((uint64_t)y + (uint64_t)z * n[1]) * n[0];
            for (i = 0; i < 2; i ++) {
                wgpuCommandEncoderCopyBufferToBuffer(encoder, from->probes[i],
                    src * BOX_GPU_PROBE_SIZE, to->probes[i], dst * BOX_GPU_PROBE_SIZE,
                    (uint64_t)(x1 - x0) * BOX_GPU_PROBE_SIZE);
            }
        }
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
}

/** Release a texture view when it exists.
 *
 * @param view The view, set to NULL
 */
static void box_gpu_bake_view_release(
    WGPUTextureView *view)
{
    if (*view) {
        wgpuTextureViewRelease(*view);
        *view = NULL;
    }
}

/** Release the face layers and the depth of the captures.
 *
 * @param b The capture state
 */
static void box_gpu_bake_layers_free(
    box_gpu_bake_t *b)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&b->layer_views); i ++) {
        wgpuTextureViewRelease(*ecs_vec_get_t(&b->layer_views, WGPUTextureView, i));
    }

    for (i = 0; i < ecs_vec_count(&b->depth_views); i ++) {
        wgpuTextureViewRelease(*ecs_vec_get_t(&b->depth_views, WGPUTextureView, i));
    }

    ecs_vec_clear(&b->layer_views);
    ecs_vec_clear(&b->depth_views);
    box_gpu_bake_view_release(&b->color_view);
    box_gpu_bake_view_release(&b->depth_view);
    if (b->color) {
        wgpuTextureRelease(b->color);
        b->color = NULL;
    }

    if (b->depth) {
        wgpuTextureRelease(b->depth);
        b->depth = NULL;
    }

    b->size = 0;
    b->layers = 0;
}

/** Release the capture state.
 *
 * @param b The capture state
 */
static void box_gpu_bake_state_free(
    box_gpu_bake_t *b)
{
    int32_t i;
    box_gpu_bake_layers_free(b);
    ecs_vec_fini_t(NULL, &b->layer_views, WGPUTextureView);
    ecs_vec_fini_t(NULL, &b->depth_views, WGPUTextureView);
    ecs_vec_fini_t(NULL, &b->captured, box_render_bake_result_t);
    ecs_vec_fini_t(NULL, &b->view_data, uint8_t);
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_readback_t *r = &b->readbacks[i];
        if (r->state == BOX_GPU_BAKE_MAPPED) {
            wgpuBufferUnmap(r->buffer);
        }

        if (r->buffer) {
            wgpuBufferRelease(r->buffer);
        }
    }

    if (b->queries) {
        wgpuQuerySetRelease(b->queries);
    }

    if (b->resolve) {
        wgpuBufferRelease(b->resolve);
    }

    if (b->project) {
        wgpuComputePipelineRelease(b->project);
    }

    wgpuBufferRelease(b->uniforms);
    wgpuBufferRelease(b->params);
    wgpuBufferRelease(b->clusters);
    wgpuPipelineLayoutRelease(b->pipeline_layout);
    wgpuBindGroupLayoutRelease(b->layout);
    ecs_os_free(b);
}

/** Create the timestamp queries and their readbacks when the device has the feature.
 *
 * @param gpu The backend
 * @param b The capture state
 */
static void box_gpu_bake_queries_init(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    int32_t i;
    if (!gpu->timestamps) {
        goto done;
    }

    b->queries = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
        .type = WGPUQueryType_Timestamp,
        .count = 2 * BOX_GPU_BAKE_READBACKS
    });
    b->resolve = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = 2 * BOX_GPU_BAKE_RESOLVE_ALIGN * BOX_GPU_BAKE_READBACKS
    });
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        b->readbacks[i].buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = 2 * sizeof(uint64_t)
        });
    }

done:
    return;
}

/** Create the capture state: layouts, buffers and the timestamp queries.
 *
 * @param gpu The backend
 * @return The state
 */
static box_gpu_bake_t* box_gpu_bake_state_new(
    box_gpu_t *gpu)
{
    box_gpu_bake_t *b = ecs_os_calloc_t(box_gpu_bake_t);
    WGPUBindGroupLayoutEntry entries[5] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_bake_params_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    b->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 5, .entries = entries });
    b->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &b->layout
        });
    b->uniforms = box_gpu_bake_buffer(gpu,
        (uint64_t)6 * BOX_GPU_BAKE_CHUNK * BOX_GPU_VIEW_STRIDE, WGPUBufferUsage_Uniform);
    b->params = box_gpu_bake_buffer(gpu, sizeof(box_gpu_bake_params_t), WGPUBufferUsage_Uniform);
    b->clusters = box_gpu_light_clusters_new(gpu);
    ecs_vec_init_t(NULL, &b->view_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &b->layer_views, WGPUTextureView, 0);
    ecs_vec_init_t(NULL, &b->depth_views, WGPUTextureView, 0);
    ecs_vec_init_t(NULL, &b->captured, box_render_bake_result_t, 0);
    box_gpu_bake_queries_init(gpu, b);

    return b;
}

/** Get the projection pipeline, created again when the bake shader was recompiled.
 *
 * @param gpu The backend
 * @param b The capture state
 * @return The pipeline, NULL while the shader has no module
 */
static WGPUComputePipeline box_gpu_bake_pipeline(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_BAKE);
    if (!s || !s->module || (b->project && b->revision == s->revision)) {
        goto done;
    }

    if (b->project) {
        wgpuComputePipelineRelease(b->project);
    }

    b->project = wgpuDeviceCreateComputePipeline(gpu->device, &(WGPUComputePipelineDescriptor){
        .layout = b->pipeline_layout,
        .compute = {
            .module = s->module,
            .entryPoint = { .data = "project", .length = WGPU_STRLEN }
        }
    });
    b->revision = s->revision;
done:
    return s && s->module ? b->project : NULL;
}

/** Make the face layers hold the faces of a chunk at a face size.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param size Face edge in texels
 * @param probes Probes of a chunk
 */
static void box_gpu_bake_layers(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t size,
    int32_t probes)
{
    int32_t i, layers = probes * 6;
    if (b->color && b->size == size && b->layers >= layers) {
        goto done;
    }

    box_gpu_bake_layers_free(b);
    b->color = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { (uint32_t)size, (uint32_t)size, (uint32_t)layers },
        .format = BOX_GPU_HDR_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    b->color_view = wgpuTextureCreateView(b->color, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_HDR_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .mipLevelCount = 1,
        .arrayLayerCount = (uint32_t)layers,
        .aspect = WGPUTextureAspect_All
    });
    for (i = 0; i < layers; i ++) {
        *ecs_vec_append_t(NULL, &b->layer_views, WGPUTextureView) = wgpuTextureCreateView(
            b->color, &(WGPUTextureViewDescriptor){
                .format = BOX_GPU_HDR_FORMAT,
                .dimension = WGPUTextureViewDimension_2D,
                .baseArrayLayer = (uint32_t)i,
                .mipLevelCount = 1,
                .arrayLayerCount = 1,
                .aspect = WGPUTextureAspect_All
            });
    }

    b->depth = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { (uint32_t)size, (uint32_t)size, (uint32_t)layers },
        .format = BOX_GPU_DEPTH_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    b->depth_view = wgpuTextureCreateView(b->depth, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_DEPTH_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .mipLevelCount = 1,
        .arrayLayerCount = (uint32_t)layers,
        .aspect = WGPUTextureAspect_DepthOnly
    });
    for (i = 0; i < layers; i ++) {
        *ecs_vec_append_t(NULL, &b->depth_views, WGPUTextureView) = wgpuTextureCreateView(
            b->depth, &(WGPUTextureViewDescriptor){
                .format = BOX_GPU_DEPTH_FORMAT,
                .dimension = WGPUTextureViewDimension_2D,
                .baseArrayLayer = (uint32_t)i,
                .mipLevelCount = 1,
                .arrayLayerCount = 1,
                .aspect = WGPUTextureAspect_All
            });
    }

    b->size = size;
    b->layers = layers;
done:
    return;
}

/** Record the outcome of the map of a timestamp readback.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_bake_readback_t
 * @param userdata2 Unused
 */
static void box_gpu_bake_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_bake_readback_t *r = userdata1;
    (void)message;
    (void)userdata2;
    if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        r->state = status == WGPUMapAsyncStatus_Success
            ? BOX_GPU_BAKE_MAPPED
            : BOX_GPU_BAKE_FAILED;
    }
}

/** Add a measurement of the captures of a packet to the time the main thread takes.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param probes Probes the captures held
 * @param ms Milliseconds they took, dropped when not plausible
 */
static void box_gpu_bake_measured(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t probes,
    double ms)
{
    if (probes > 0 && ms > 0 && ms < BOX_GPU_BAKE_MEASURE_MAX) {
        ecs_os_mutex_lock(gpu->lock);
        b->timing.probes += probes;
        b->timing.ms += ms;
        ecs_os_mutex_unlock(gpu->lock);
    }
}

/** Measure the captures of a packet when the queue finished their work.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_bake_done_t
 * @param userdata2 Unused
 */
static void box_gpu_bake_on_done(
    WGPUQueueWorkDoneStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_bake_done_t *d = userdata1;
    (void)message;
    (void)userdata2;
    if (status == WGPUQueueWorkDoneStatus_Success) {
        box_gpu_bake_measured(d->gpu, d->bake, d->probes,
            (double)(ecs_os_now() - d->start) * 1e-6);
    }

    if (status != WGPUQueueWorkDoneStatus_CallbackCancelled) {
        d->probes = 0;
    }
}

/** Get the nanoseconds of one tick of the timestamps.
 *
 * @param gpu The backend
 * @return The period, 1 in the browser, which resolves timestamps in nanoseconds
 */
static double box_gpu_bake_period(
    box_gpu_t *gpu)
{
#ifdef __EMSCRIPTEN__
    (void)gpu;

    return 1.0;
#else
    return (double)wgpuQueueGetTimestampPeriod(gpu->queue);
#endif
}

/** Map the timestamp copies that were submitted and publish the ones that arrived.
 *
 * @param gpu The backend
 * @param b The capture state
 */
static void box_gpu_bake_readback_poll(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    int32_t i;
#ifndef __EMSCRIPTEN__
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        if (b->readbacks[i].state == BOX_GPU_BAKE_MAPPING || b->done[i].probes) {
            wgpuDevicePoll(gpu->device, false, NULL);
            break;
        }
    }
#endif

    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_readback_t *r = &b->readbacks[i];
        if (r->state == BOX_GPU_BAKE_MAPPED) {
            const uint64_t *t = wgpuBufferGetConstMappedRange(r->buffer, 0,
                2 * sizeof(uint64_t));
            if (t && t[1] > t[0]) {
                box_gpu_bake_measured(gpu, b, r->probes,
                    (double)(t[1] - t[0]) * box_gpu_bake_period(gpu) * 1e-6);
            }

            wgpuBufferUnmap(r->buffer);
            r->state = BOX_GPU_BAKE_FREE;
        } else if (r->state == BOX_GPU_BAKE_FAILED) {
            r->state = BOX_GPU_BAKE_FREE;
        } else if (r->state == BOX_GPU_BAKE_COPIED) {
            r->state = BOX_GPU_BAKE_MAPPING;
            wgpuBufferMapAsync(r->buffer, WGPUMapMode_Read, 0, 2 * sizeof(uint64_t),
                (WGPUBufferMapCallbackInfo){
                    .mode = WGPUCallbackMode_AllowSpontaneous,
                    .callback = box_gpu_bake_on_map,
                    .userdata1 = r
                });
        }
    }
}

/** Find a free timestamp readback.
 *
 * @param b The capture state
 * @return Index of the readback, -1 without timestamps or when every one is in flight
 */
static int32_t box_gpu_bake_readback(
    box_gpu_bake_t *b)
{
    int32_t i, result = -1;
    for (i = 0; b->queries && i < BOX_GPU_BAKE_READBACKS; i ++) {
        if (b->readbacks[i].state == BOX_GPU_BAKE_FREE) {
            result = i;
            break;
        }
    }

    return result;
}

/** Finish an encoder, submit it and release it.
 *
 * @param gpu The backend
 * @param encoder The encoder
 */
static void box_gpu_bake_submit(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder)
{
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

/** Resolve the timestamps of the readbacks whose captures were submitted before the last
 * packet and copy them into their readbacks.
 *
 * Metal writes the timestamps of a pass when the pass completed, which a resolve in the same
 * or the next command buffer does not wait for, so the resolve waits for one more packet.
 *
 * @param gpu The backend
 * @param b The capture state
 */
static void box_gpu_bake_resolve(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    WGPUCommandEncoder encoder = NULL;
    int32_t r;
    uint32_t i;
    for (r = 0; r < BOX_GPU_BAKE_READBACKS; r ++) {
        box_gpu_bake_readback_t *rb = &b->readbacks[r];
        uint64_t offset = (uint64_t)r * 2 * BOX_GPU_BAKE_RESOLVE_ALIGN;
        if (rb->state != BOX_GPU_BAKE_WRITTEN) {
            continue;
        }

        if (!encoder) {
            encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        }

        for (i = 0; i < 2; i ++) {
            wgpuCommandEncoderResolveQuerySet(encoder, b->queries, (uint32_t)r * 2 + i, 1,
                b->resolve, offset + i * BOX_GPU_BAKE_RESOLVE_ALIGN);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, b->resolve,
                offset + i * BOX_GPU_BAKE_RESOLVE_ALIGN, rb->buffer,
                i * sizeof(uint64_t), sizeof(uint64_t));
        }

        rb->state = BOX_GPU_BAKE_COPIED;
    }

    if (encoder) {
        box_gpu_bake_submit(gpu, encoder);
    }
}

/** Write the uniforms of a face of a probe and add its cull view.
 *
 * The ambient terms are black, so the captures hold direct light and emission; the sun is
 * shadowed by the capture cascade, which ends beyond every capture.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param batch The bake batch
 * @param rv View with the lights of the batch, its view matrix is set here
 * @param probe The probe
 * @param slot Slot of the face
 * @param cascade The capture cascade, NULL without a sun
 */
static void box_gpu_bake_face(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch,
    box_render_view_t *rv,
    const box_render_probe_capture_t *probe,
    int32_t slot,
    const box_render_cascade_t *cascade)
{
    const float (*face)[3] = box_gpu_bake_faces[slot % 6];
    box_gpu_view_uniforms_t *u = (box_gpu_view_uniforms_t*)ecs_vec_get_t(
        &b->view_data, uint8_t, slot * BOX_GPU_VIEW_STRIDE);
    vec3 eye, center;
    mat4 proj;
    float size = (float)batch->size;
    glm_vec3_copy(ECS_CONST_CAST(float*, probe->position), eye);
    glm_vec3_add(eye, ECS_CONST_CAST(float*, face[0]), center);
    glm_lookat(eye, center, ECS_CONST_CAST(float*, face[1]), rv->view_matrix);
    box_camera_projection(&rv->camera_data, size, size, proj);
    ecs_os_memset_t(u, 0, box_gpu_view_uniforms_t);
    glm_mat4_mul(proj, rv->view_matrix, u->view_proj);
    glm_vec3_copy(eye, u->camera);
    u->camera[3] = 1;
    u->viewport[0] = size;
    u->viewport[1] = size;
    u->viewport[2] = 1.0f / size;
    u->viewport[3] = 1.0f / size;
    box_gpu_light_uniforms(gpu, rv, proj, u);
    if (cascade) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, cascade->matrix), u->shadow_matrix[0]);
        u->shadow_split[0] = BOX_GPU_BAKE_FAR;
        u->shadow_bias[0] = BOX_GPU_BAKE_SHADOW_BIAS * cascade->texel / cascade->depth_range;
        u->shadow_offset[0] = BOX_GPU_BAKE_SHADOW_OFFSET * cascade->texel;
        u->shadow_params[0] = BOX_GPU_BAKE_FAR;
        u->shadow_params[1] = BOX_GPU_BAKE_FAR;
        u->shadow_params[2] = 1.0f / (float)BOX_GPU_CAPTURE_SHADOW_RESOLUTION;
        u->shadow_config[0] = 1;
        u->shadow_config[1] = BOX_GPU_BAKE_SHADOW_FILTER;
    }

    box_gpu_cull_capture_add(gpu, packet, batch->scene, slot, -1, u->view_proj,
        proj[1][1] * size * 0.5f);
}

/** Record the render passes of the faces of a chunk.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param packet The packet
 * @param rv View with the lights of the batch
 * @param faces Faces of the chunk
 * @param shadow Array view of the shadow map the faces sample
 * @param ambient Volume group the faces shade their ambient term with
 */
static void box_gpu_bake_faces_render(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *rv,
    int32_t faces,
    WGPUTextureView shadow,
    WGPUBindGroup ambient)
{
    WGPUBindGroup group = box_gpu_light_group_with(gpu, rv, b->uniforms, b->clusters, shadow);
    int32_t i;
    for (i = 0; i < faces; i ++) {
        uint32_t offset = (uint32_t)(i * BOX_GPU_VIEW_STRIDE);
        WGPURenderPassColorAttachment color = {
            .view = *ecs_vec_get_t(&b->layer_views, WGPUTextureView, i),
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = { 0, 0, 0, 0 }
        };
        WGPURenderPassDepthStencilAttachment depth = {
            .view = *ecs_vec_get_t(&b->depth_views, WGPUTextureView, i),
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Store,
            .depthClearValue = 0.0f
        };
        box_gpu_light_cluster_with(gpu, encoder, rv, b->uniforms, offset, b->clusters);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,
            &(WGPURenderPassDescriptor){
                .colorAttachmentCount = 1,
                .colorAttachments = &color,
                .depthStencilAttachment = &depth
            });
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        wgpuRenderPassEncoderSetBindGroup(pass, 3, ambient, 0, NULL);
        box_gpu_draw_capture(gpu, pass, packet, i);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    wgpuBindGroupRelease(group);
}

/** Record the projection of the faces of a chunk onto the SH of its probes.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param encoder The encoder of the chunk
 * @param pipeline The projection pipeline
 * @param volume The volume the probes are written into
 * @param batch The bake batch
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 * @param writes Timestamp the pass writes, NULL for none
 */
static void box_gpu_bake_project(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    WGPUCommandEncoder encoder,
    WGPUComputePipeline pipeline,
    box_gpu_volume_t *volume,
    const box_render_bake_batch_t *batch,
    const box_render_probe_capture_t *probes,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    box_gpu_buffer_t *baked = batch->baked
        ? ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, batch->baked)
        : NULL;
    uint32_t capacity = baked ? baked->size / (uint32_t)sizeof(box_render_light_t) : 0;
    box_gpu_bake_params_t params = {
        .size = { (uint32_t)batch->size, (uint32_t)count, 0, 0 },
        .near_ = { BOX_GPU_BAKE_NEAR, 0, 0, 0 }
    };
    int32_t i;
    params.lights[0] = glm_min(batch->baked_count, capacity);
    params.lights[1] = glm_min(batch->baked_directional, params.lights[0]);
    glm_vec4_copy(ECS_CONST_CAST(float*, batch->lights.ambient_sky), params.sky);
    glm_vec4_copy(ECS_CONST_CAST(float*, batch->lights.ambient_ground), params.ground);
    for (i = 0; i < count; i ++) {
        params.probes[i] = probes[i].index;
        glm_vec3_copy(ECS_CONST_CAST(float*, probes[i].position), params.positions[i]);
    }

    wgpuQueueWriteBuffer(gpu->queue, b->params, 0, &params, sizeof(params));
    WGPUBindGroupEntry entries[5] = {
        { .binding = 0, .buffer = b->params, .size = sizeof(params) },
        { .binding = 1, .textureView = b->color_view },
        {
            .binding = 2,
            .buffer = volume->probes[batch->target],
            .size = (uint64_t)volume->count * BOX_GPU_PROBE_SIZE
        },
        { .binding = 3, .textureView = b->depth_view },
        {
            .binding = 4,
            .buffer = baked ? baked->buffer : gpu->no_lights,
            .size = baked ? baked->size : sizeof(box_render_light_t)
        }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = b->layout,
        .entryCount = 5,
        .entries = entries
    });
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,
        &(WGPUComputePassDescriptor){ .timestampWrites = writes });
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, (uint32_t)count, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.probes += count;
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &b->captured, box_render_bake_result_t) =
            (box_render_bake_result_t){
                .volume = batch->volume,
                .generation = batch->generation,
                .pass = batch->pass,
                .probe = probes[i].index
            };
    }

    ecs_os_mutex_unlock(gpu->lock);
}

/** Get the probes of a chunk: as many as the face layers, slots and visible buffer allow.
 *
 * @param packet The packet
 * @param batch The bake batch
 * @return Probes per chunk, at least 1
 */
static int32_t box_gpu_bake_chunk(
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch)
{
    int64_t instances = 0, texels = (int64_t)batch->size * batch->size * 6;
    int32_t i, result = BOX_GPU_BAKE_CHUNK;
    for (i = 0; i < ecs_vec_count(&packet->batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        if (b->scene == batch->scene && b->bake && b->view == -1) {
            instances += b->instance_count;
        }
    }

    if (instances * 6 * result > BOX_GPU_BAKE_VISIBLE) {
        result = (int32_t)(BOX_GPU_BAKE_VISIBLE / (instances * 6));
    }

    if (texels * result > BOX_GPU_BAKE_TEXELS) {
        result = (int32_t)(BOX_GPU_BAKE_TEXELS / texels);
    }

    return result < 1 ? 1 : result;
}

/** Capture one chunk of probes of a bake batch in one command buffer.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param batch The bake batch
 * @param rv View with the lights of the batch
 * @param probes The probes of the chunk
 * @param count Probes of the chunk
 * @param cascade The capture cascade, NULL without a sun
 * @param shadow Receives the shadow map the chunk sampled, rendered by the first chunk
 * @param timer The timestamps of the packet
 * @param last Whether the chunk is the last of the packet
 */
static void box_gpu_bake_chunk_capture(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch,
    box_render_view_t *rv,
    const box_render_probe_capture_t *probes,
    int32_t count,
    const box_render_cascade_t *cascade,
    WGPUTextureView *shadow,
    box_gpu_bake_timer_t *timer,
    bool last)
{
    WGPUComputePipeline pipeline = box_gpu_bake_pipeline(gpu, b);
    box_gpu_volume_t *volume = ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, batch->volume);
    int32_t i, faces = count * 6;
    bool caster = cascade && !*shadow;
    if (!pipeline || !volume) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        if (probes[i].index >= volume->count) {
            goto done;
        }
    }

    box_gpu_cull_reset(gpu);
    ecs_vec_set_count_t(NULL, &b->view_data, uint8_t, faces * BOX_GPU_VIEW_STRIDE);
    for (i = 0; i < faces; i ++) {
        box_gpu_bake_face(gpu, b, packet, batch, rv, &probes[i / 6], i, cascade);
    }

    if (caster) {
        box_gpu_cull_capture_add(gpu, packet, batch->scene, faces, 0,
            ECS_CONST_CAST(vec4*, cascade->matrix), 0);
    }

    box_gpu_cull_upload(gpu);
    wgpuQueueWriteBuffer(gpu->queue, b->uniforms, 0, ecs_vec_first(&b->view_data),
        (size_t)faces * BOX_GPU_VIEW_STRIDE);
    uint32_t query = (uint32_t)timer->readback * 2;
    WGPUPassTimestampWrites begin = {
        .querySet = b->queries,
        .beginningOfPassWriteIndex = query,
        .endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED
    };
    WGPUPassTimestampWrites end = {
        .querySet = b->queries,
        .beginningOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED,
        .endOfPassWriteIndex = query + 1
    };
    bool timed = timer->readback != -1;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    if (box_gpu_cull_record_all(gpu, encoder, timed && !timer->begun ? &begin : NULL)) {
        timer->begun |= timed;
    }

    if (caster) {
        *shadow = box_gpu_shadow_capture(gpu, encoder, packet, faces, cascade);
    }

    timed = timed && timer->begun && last;
    box_gpu_bake_faces_render(gpu, b, encoder, packet, rv, faces,
        *shadow ? *shadow : gpu->shadow_empty_view,
        batch->source == BOX_RENDER_BAKE_BLACK ? gpu->no_volume->group[0]
            : volume->group[batch->source]);
    box_gpu_bake_project(gpu, b, encoder, pipeline, volume, batch, probes, count,
        timed ? &end : NULL);
    timer->ended |= timed;
    box_gpu_bake_submit(gpu, encoder);
done:
    return;
}

/** Capture the probes of a bake batch, chunk by chunk.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param packet The packet
 * @param batch The bake batch
 * @param timer The timestamps of the packet
 * @param last Whether the batch is the last of the packet
 */
static void box_gpu_bake_batch(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch,
    box_gpu_bake_timer_t *timer,
    bool last)
{
    box_render_view_t rv;
    box_render_cascade_t cascade;
    WGPUTextureView shadow = NULL;
    int32_t first, chunk = box_gpu_bake_chunk(packet, batch);
    int32_t size = batch->size;
    const box_render_probe_capture_t *probes = ecs_vec_get_t(
        &packet->probes, box_render_probe_capture_t, batch->first);
    ecs_os_memset_t(&rv, 0, box_render_view_t);
    rv.scene = batch->scene;
    rv.lights = batch->lights;
    rv.lights.enabled = true;
    glm_vec4_zero(rv.lights.ambient_sky);
    glm_vec4_zero(rv.lights.ambient_ground);
    rv.camera_data = (Camera){ .fov = 90, .near_ = BOX_GPU_BAKE_NEAR };
    bool sun = rv.lights.directional && glm_vec3_norm(rv.lights.sun) > 1e-6f;
    if (sun) {
        vec3 direction;
        glm_vec3_normalize_to(rv.lights.sun, direction);
        box_gpu_shadow_capture_fit(batch->bounds_min, batch->bounds_max, direction, &cascade);
    }

    box_gpu_bake_layers(gpu, b, size, chunk);
    for (first = 0; first < batch->count; first += chunk) {
        int32_t count = batch->count - first < chunk ? batch->count - first : chunk;
        box_gpu_bake_chunk_capture(gpu, b, packet, batch, &rv, &probes[first], count,
            sun ? &cascade : NULL, &shadow, timer, last && first + count >= batch->count);
    }
}

void box_gpu_bake_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_volume_uniforms_t)
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
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 3, .entries = entries };
    box_gpu_volume_uniforms_t none = { .size = { 1, 1, 1, 0 } };
    gpu->volume_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    ecs_map_init(&gpu->volumes, NULL);
    gpu->no_volume = box_gpu_volume_new(gpu, 1);
    wgpuQueueWriteBuffer(gpu->queue, gpu->no_volume->grid, 0, &none, sizeof(none));
}

void box_gpu_bake_fini(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->volumes);
    while (ecs_map_next(&it)) {
        box_gpu_volume_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&gpu->volumes);
    if (gpu->bake) {
        box_gpu_bake_t *b = gpu->bake;
        ecs_os_mutex_lock(gpu->lock);
        gpu->bake = NULL;
        ecs_os_mutex_unlock(gpu->lock);
        box_gpu_bake_state_free(b);
    }

    if (gpu->no_volume) {
        box_gpu_volume_free(gpu->no_volume);
        gpu->no_volume = NULL;
    }

    if (gpu->volume_layout) {
        wgpuBindGroupLayoutRelease(gpu->volume_layout);
        gpu->volume_layout = NULL;
    }
}

void box_gpu_bake_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_volume_desc_t *desc,
    uint32_t size)
{
    box_gpu_volume_t *v = ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, handle);
    uint64_t words = desc ? ((uint64_t)desc->probes + 31) / 32 : 0;
    bool valid = desc && desc->probes &&
        (uint64_t)desc->size[0] * desc->size[1] * desc->size[2] == desc->probes &&
        size >= sizeof(box_render_volume_desc_t) + words * sizeof(uint32_t);
    bool resize = valid && v && op == BOX_RENDER_UPDATE && desc->op == BOX_RENDER_VOLUME_RESIZE;
    if (resize) {
        box_gpu_volume_t *grown = box_gpu_volume_new(gpu, desc->probes);
        box_gpu_volume_write(gpu, grown, desc);
        box_gpu_volume_carry(gpu, v, grown, desc);
        box_gpu_volume_free(v);
        ecs_map_remove(&gpu->volumes, handle);
        ecs_map_insert_ptr(&gpu->volumes, handle, grown);
        goto done;
    }

    if (v && (op == BOX_RENDER_DESTROY || !valid || op == BOX_RENDER_CREATE ||
        v->count != desc->probes))
    {
        ecs_map_remove(&gpu->volumes, handle);
        box_gpu_volume_free(v);
        v = NULL;
    }

    if (op == BOX_RENDER_DESTROY) {
        goto done;
    }

    if (!valid) {
        ecs_err("probe volume %u has an invalid description", handle);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    if (!v) {
        v = box_gpu_volume_new(gpu, desc->probes);
        ecs_map_insert_ptr(&gpu->volumes, handle, v);
        box_gpu_volume_write(gpu, v, desc);
    } else if (op == BOX_RENDER_CREATE || desc->op == BOX_RENDER_VOLUME_FILL) {
        box_gpu_volume_write(gpu, v, desc);
    } else {
        box_gpu_volume_keep(gpu, v, desc);
    }

done:
    return;
}

WGPUBindGroup box_gpu_bake_group(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    box_gpu_volume_t *volume = v->volume
        ? ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, v->volume)
        : NULL;

    return volume ? volume->group[v->volume_buffer & 1u] : gpu->no_volume->group[0];
}

/** Check whether the backend can capture a bake batch of a packet.
 *
 * @param packet The packet
 * @param batch The bake batch
 * @return Whether the batch has probes, a face size and its probes are in the packet
 */
static bool box_gpu_bake_batch_valid(
    const box_render_packet_t *packet,
    const box_render_bake_batch_t *batch)
{
    bool source = batch->source == BOX_RENDER_BAKE_BLACK ||
        batch->source == 0 || batch->source == 1;
    bool target = batch->target == 0 || batch->target == 1;

    return batch->count > 0 && batch->size > 0 && batch->first >= 0 && source && target &&
        batch->first + batch->count <= ecs_vec_count(&packet->probes);
}

/** Measure captures from the CPU until the queue reports their work done, the fallback of
 * devices without timestamp queries. The time includes queued work before the captures and,
 * natively, the wait for the next poll, so it errs on the expensive side.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param start ecs_os_now when the captures started recording
 * @param probes Probes the captures held
 */
static void box_gpu_bake_done_measure(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    uint64_t start,
    int32_t probes)
{
    int32_t i;
    for (i = 0; probes && i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_done_t *d = &b->done[i];
        if (!d->probes) {
            d->bake = b;
            d->gpu = gpu;
            d->start = start;
            d->probes = probes;
            wgpuQueueOnSubmittedWorkDone(gpu->queue, (WGPUQueueWorkDoneCallbackInfo){
                .mode = WGPUCallbackMode_AllowSpontaneous,
                .callback = box_gpu_bake_on_done,
                .userdata1 = d
            });
            break;
        }
    }
}

void box_gpu_bake_capture(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->bakes), probes = 0, last = -1;
    box_gpu_bake_timer_t timer = { .readback = -1 };
    uint64_t start = ecs_os_now();
    if (gpu->bake) {
        box_gpu_bake_readback_poll(gpu, gpu->bake);
        box_gpu_bake_resolve(gpu, gpu->bake);
    }

    for (i = 0; i < count; i ++) {
        if (box_gpu_bake_batch_valid(packet,
            ecs_vec_get_t(&packet->bakes, box_render_bake_batch_t, i)))
        {
            last = i;
        }
    }

    if (last == -1) {
        goto done;
    }

    if (!gpu->bake) {
        box_gpu_bake_t *created = box_gpu_bake_state_new(gpu);
        ecs_os_mutex_lock(gpu->lock);
        gpu->bake = created;
        ecs_os_mutex_unlock(gpu->lock);
    }

    box_gpu_bake_t *b = gpu->bake;
    timer.readback = box_gpu_bake_readback(b);
    for (i = 0; i <= last; i ++) {
        const box_render_bake_batch_t *batch = ecs_vec_get_t(
            &packet->bakes, box_render_bake_batch_t, i);
        if (box_gpu_bake_batch_valid(packet, batch)) {
            box_gpu_bake_batch(gpu, b, packet, batch, &timer, i == last);
            probes += batch->count;
        }
    }

    if (timer.ended) {
        b->readbacks[timer.readback].probes = probes;
        b->readbacks[timer.readback].state = BOX_GPU_BAKE_WRITTEN;
    } else if (!b->queries) {
        box_gpu_bake_done_measure(gpu, b, start, probes);
    }

done:
    return;
}

bool box_render_gpu_bake_captured(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    box_render_gpu_stats_t stats;
    bool result = box_render_gpu_stats(world, &stats);
    ecs_vec_clear(out);
    if (!result) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    ecs_os_mutex_lock(gpu->lock);
    int32_t count = gpu->bake ? ecs_vec_count(&gpu->bake->captured) : 0;
    if (count) {
        ecs_vec_set_count_t(NULL, out, box_render_bake_result_t, count);
        ecs_os_memcpy_n(ecs_vec_first(out), ecs_vec_first(&gpu->bake->captured),
            box_render_bake_result_t, count);
        ecs_vec_clear(&gpu->bake->captured);
    }

    ecs_os_mutex_unlock(gpu->lock);
done:
    return result;
}

bool box_render_gpu_bake_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    box_render_gpu_stats_t stats;
    bool result = box_render_gpu_stats(world, &stats);
    ecs_os_memset_t(out, 0, box_render_bake_timing_t);
    if (!result) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    ecs_os_mutex_lock(gpu->lock);
    out->measured = gpu->timestamps;
    if (gpu->bake) {
        out->probes = gpu->bake->timing.probes;
        out->ms = gpu->bake->timing.ms;
        gpu->bake->timing.probes = 0;
        gpu->bake->timing.ms = 0;
    }

    ecs_os_mutex_unlock(gpu->lock);
done:
    return result;
}

#else

bool box_render_gpu_bake_captured(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    (void)world;
    ecs_vec_clear(out);

    return false;
}

bool box_render_gpu_bake_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    (void)world;
    ecs_os_memset_t(out, 0, box_render_bake_timing_t);

    return false;
}

#endif
