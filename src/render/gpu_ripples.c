/**
 * @file render/gpu_ripples.c
 * @brief Fields of WaterRipples in the webgpu backend: two r32float height textures and two
 * r32float foam textures per field, stepped by the damped wave equation in compute passes before
 * the targets, and the field of the scene of a view bound in its view group for the Water
 * fragments (mesh_extended.wgsl).
 *
 * A step reads the latest heights from one texture and the ones before from the other, which it
 * overwrites with the new heights; the two then swap, and the foam with them. Cells are addressed
 * modulo the resolution, so a moving window only clears the cells that entered it.
 *
 * A flow pass writes the velocity of every cell once a frame; when the water flows relative to
 * the frame each step first advects the heights and the foam into a scratch texture and back.
 */

#include "gpu.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

/** Invocations of a workgroup of water_ripples.wgsl along X and Y */
#define BOX_GPU_RIPPLES_GROUP (8u)

/** Bytes between the step uniforms of two dispatches, the dynamic offset alignment */
#define BOX_GPU_RIPPLES_STEP_STRIDE (256u)

/** Share of the resolution over which the heights and the normals fade out toward the border */
#define BOX_GPU_RIPPLES_BORDER (16u)

/** Step of water_ripples.wgsl */
typedef struct box_gpu_ripple_step_t {
    uint32_t grid[4];               /**< Resolution, texel of the window corner X and Z, sources */
    int32_t shift[4];               /**< Cells the window moved X and Z, 1 on the first step of
                                     *   the frame, first source */
    float wave[4];                  /**< (c dt / dx)^2, velocity kept per step, border cells,
                                     *   time fraction of the frame at the end of the step */
    float foam[4];                  /**< Foam spread share and kept factor per step, crest foam
                                     *   per step, 1 / (2 world units of a cell) */
    float flow[4];                  /**< Velocity of the frame X and Z, seconds of a step per
                                     *   world unit of a cell, cells of the fade of a rectangle */
    uint32_t flows[4];              /**< First flowing Water rectangle, rectangles */
} box_gpu_ripple_step_t;

/** The ripple members of View of lights.wgsl (ripple_a, ripple_b, ripple_c) */
typedef struct box_gpu_ripple_record_t {
    float a[4];                     /**< World XZ of the window corner, 1 / cell, strength */
    float b[4];                     /**< World Y of the Water, tolerance, border cells, cell */
    uint32_t c[4];                  /**< Resolution, texel of the window corner X and Z, 1 for a
                                     *   field */
} box_gpu_ripple_record_t;

/** A field of the backend */
typedef struct box_gpu_ripple_field_t {
    ecs_entity_t water;             /**< Water entity, the key */
    ecs_entity_t scene;             /**< Scene root of the Water */
    uint32_t resolution;            /**< Cells per side */
    WGPUTexture textures[2];        /**< The two height textures */
    WGPUTextureView views[2];       /**< Their views */
    WGPUTexture foam[2];            /**< The two foam textures, foam[k] goes with textures[k] */
    WGPUTextureView foam_views[2];  /**< Their views */
    WGPUTexture velocity;           /**< rgba32float velocity of the water of every cell: world
                                     *   velocity (xy) and velocity relative to the frame (zw) */
    WGPUTextureView velocity_view;  /**< Its view */
    WGPUTexture scratch;            /**< r32float texture the advect pass writes into */
    WGPUTextureView scratch_view;   /**< Its view */
    box_gpu_ripple_record_t record; /**< Record the views of its scene take */
    WGPUBindGroup groups[2];        /**< Group k reads textures[k] and foam[k] and writes the
                                     *   others, for the frame */
    WGPUBindGroup flow_group;       /**< Group of the flow pass, for the frame */
    WGPUBindGroup advect[4];        /**< Group k advects textures[0], textures[1], foam[0] or
                                     *   foam[1] into the scratch texture, for the frame */
    WGPUBindGroup settle[4];        /**< Group k copies the scratch texture back into the
                                     *   texture of advect[k], for the frame */
    int32_t current;                /**< Texture of the latest heights */
    int32_t origin[2];              /**< Texel of the window corner */
    float corner[2];                /**< World XZ of the window corner */
    float cell;                     /**< World units of a cell */
    float height;                   /**< World Y of the Water */
    int64_t frame;                  /**< Frame of the packet it was last in */
} box_gpu_ripple_field_t;

/** The entry point of a dispatch */
typedef enum box_gpu_ripple_pass_t {
    BoxGpuRipplesAdvance,           /**< advance, with groups[group] */
    BoxGpuRipplesClear,             /**< clear, with groups[group] */
    BoxGpuRipplesFlow,              /**< flow, with flow_group */
    BoxGpuRipplesAdvect,            /**< advect, with advect[group] */
    BoxGpuRipplesSettle             /**< settle, with settle[group] */
} box_gpu_ripple_pass_t;

/** A dispatch of the frame */
typedef struct box_gpu_ripple_dispatch_t {
    int32_t field;                  /**< Index of the field */
    int32_t group;                  /**< Group of the field */
    uint32_t slot;                  /**< Step uniform */
    box_gpu_ripple_pass_t pass;     /**< The entry point */
} box_gpu_ripple_dispatch_t;

struct box_gpu_ripples_t {
    WGPUBindGroupLayout layout;     /**< Current, previous, step, sources, foam current, foam
                                     *   next */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the passes */
    WGPUBindGroupLayout flow_layout; /**< Step, rectangles, velocity written */
    WGPUPipelineLayout flow_pipeline_layout; /**< Pipeline layout of the flow pass */
    WGPUBindGroupLayout advect_layout; /**< Step, texture read, texture written, velocity */
    WGPUPipelineLayout advect_pipeline_layout; /**< Pipeline layout of advect and settle */
    WGPUComputePipeline clear;      /**< Clears the cells that entered the window */
    WGPUComputePipeline advance;    /**< Steps the heights */
    WGPUComputePipeline flow;       /**< Writes the velocity of every cell */
    WGPUComputePipeline advect;     /**< Advects a texture into the scratch texture */
    WGPUComputePipeline settle;     /**< Copies the scratch texture back */
    int32_t revision;               /**< Revision of the shader the pipelines use */
    WGPUBuffer steps;               /**< Step uniforms of the frame */
    uint64_t steps_size;            /**< Bytes of the step buffer */
    WGPUBuffer sources;             /**< box_render_ripple_source_t of the frame */
    uint64_t sources_size;          /**< Bytes of the source buffer */
    WGPUBuffer flows;               /**< box_render_ripple_flow_t of the frame */
    uint64_t flows_size;            /**< Bytes of the rectangle buffer */
    WGPUTexture none;               /**< 1x1 zero r32float, bound by views without a field */
    WGPUTextureView none_view;      /**< Its view */
    WGPUTexture none_flow;          /**< 1x1 zero rgba32float, the velocity of views without a
                                     *   field */
    WGPUTextureView none_flow_view; /**< Its view */
    ecs_vec_t fields;               /**< box_gpu_ripple_field_t* */
    ecs_vec_t step_data;            /**< box_gpu_ripple_step_t, one per stride, of the frame */
    ecs_vec_t dispatches;           /**< box_gpu_ripple_dispatch_t of the frame */
};

/** Release a webgpu object when it exists.
 *
 * @param obj The object
 * @param release The release function of its type
 */
#define BOX_GPU_RIPPLES_RELEASE(obj, release) \
    if (obj) { \
        release(obj); \
        obj = NULL; \
    }

/** Create a texture the passes read and write as storage.
 *
 * @param gpu The backend
 * @param size Texels per side
 * @param format Its format
 * @return The texture, zero
 */
static WGPUTexture box_gpu_ripples_texture_format(
    box_gpu_t *gpu,
    uint32_t size,
    WGPUTextureFormat format)
{
    return wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { size, size, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
}

/** Create an r32float texture the passes read and write as storage.
 *
 * @param gpu The backend
 * @param size Texels per side
 * @return The texture, zero
 */
static WGPUTexture box_gpu_ripples_texture(
    box_gpu_t *gpu,
    uint32_t size)
{
    return box_gpu_ripples_texture_format(gpu, size, WGPUTextureFormat_R32Float);
}

/** Get the layout entry of a storage texture of a compute pass.
 *
 * @param binding The binding
 * @param access Its access
 * @param format Its format
 * @return The entry
 */
static WGPUBindGroupLayoutEntry box_gpu_ripples_storage(
    uint32_t binding,
    WGPUStorageTextureAccess access,
    WGPUTextureFormat format)
{
    return (WGPUBindGroupLayoutEntry){
        .binding = binding,
        .visibility = WGPUShaderStage_Compute,
        .storageTexture = {
            .access = access,
            .format = format,
            .viewDimension = WGPUTextureViewDimension_2D
        }
    };
}

/** Get the layout entry of the step uniform of a compute pass.
 *
 * @return The entry
 */
static WGPUBindGroupLayoutEntry box_gpu_ripples_step_entry(void)
{
    return (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Compute,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = true,
            .minBindingSize = sizeof(box_gpu_ripple_step_t)
        }
    };
}

/** Create a pipeline layout of one bind group layout.
 *
 * @param gpu The backend
 * @param layout The bind group layout
 * @return The pipeline layout
 */
static WGPUPipelineLayout box_gpu_ripples_pipeline_layout(
    box_gpu_t *gpu,
    WGPUBindGroupLayout layout)
{
    return wgpuDeviceCreatePipelineLayout(gpu->device, &(WGPUPipelineLayoutDescriptor){
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &layout
    });
}

/** Create a buffer that receives copies.
 *
 * @param gpu The backend
 * @param size Bytes
 * @param usage Usage, CopyDst is added
 * @return The buffer
 */
static WGPUBuffer box_gpu_ripples_buffer(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    return wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = usage | WGPUBufferUsage_CopyDst,
        .size = size
    });
}

void box_gpu_ripples_init(
    box_gpu_t *gpu)
{
    box_gpu_ripples_t *s = ecs_os_calloc_t(box_gpu_ripples_t);
    WGPUBindGroupLayoutEntry entries[6] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_ReadOnly,
                .format = WGPUTextureFormat_R32Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_ReadWrite,
                .format = WGPUTextureFormat_R32Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_gpu_ripple_step_t)
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_ReadOnly,
                .format = WGPUTextureFormat_R32Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 5,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = WGPUTextureFormat_R32Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    s->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = entries });
    s->pipeline_layout = box_gpu_ripples_pipeline_layout(gpu, s->layout);

    WGPUBindGroupLayoutEntry flow_entries[3] = {
        box_gpu_ripples_step_entry(),
        {
            .binding = 6,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        box_gpu_ripples_storage(7, WGPUStorageTextureAccess_WriteOnly,
            WGPUTextureFormat_RGBA32Float)
    };
    s->flow_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 3, .entries = flow_entries });
    s->flow_pipeline_layout = box_gpu_ripples_pipeline_layout(gpu, s->flow_layout);

    WGPUBindGroupLayoutEntry advect_entries[4] = {
        box_gpu_ripples_step_entry(),
        box_gpu_ripples_storage(8, WGPUStorageTextureAccess_ReadOnly,
            WGPUTextureFormat_R32Float),
        box_gpu_ripples_storage(9, WGPUStorageTextureAccess_WriteOnly,
            WGPUTextureFormat_R32Float),
        box_gpu_ripples_storage(10, WGPUStorageTextureAccess_ReadOnly,
            WGPUTextureFormat_RGBA32Float)
    };
    s->advect_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 4, .entries = advect_entries });
    s->advect_pipeline_layout = box_gpu_ripples_pipeline_layout(gpu, s->advect_layout);

    s->none = box_gpu_ripples_texture(gpu, 1);
    s->none_view = wgpuTextureCreateView(s->none, NULL);
    s->none_flow = box_gpu_ripples_texture_format(gpu, 1, WGPUTextureFormat_RGBA32Float);
    s->none_flow_view = wgpuTextureCreateView(s->none_flow, NULL);
    ecs_vec_init_t(NULL, &s->fields, box_gpu_ripple_field_t*, 0);
    ecs_vec_init_t(NULL, &s->step_data, uint8_t, 0);
    ecs_vec_init_t(NULL, &s->dispatches, box_gpu_ripple_dispatch_t, 0);
    gpu->ripples = s;
}

/** Release the bind groups of the frame of a field.
 *
 * @param f The field
 */
static void box_gpu_ripples_groups_release(
    box_gpu_ripple_field_t *f)
{
    int32_t k;
    BOX_GPU_RIPPLES_RELEASE(f->groups[0], wgpuBindGroupRelease);
    BOX_GPU_RIPPLES_RELEASE(f->groups[1], wgpuBindGroupRelease);
    BOX_GPU_RIPPLES_RELEASE(f->flow_group, wgpuBindGroupRelease);
    for (k = 0; k < 4; k ++) {
        BOX_GPU_RIPPLES_RELEASE(f->advect[k], wgpuBindGroupRelease);
        BOX_GPU_RIPPLES_RELEASE(f->settle[k], wgpuBindGroupRelease);
    }
}

/** Release the height, foam, velocity and scratch textures of a field.
 *
 * @param f The field
 */
static void box_gpu_ripples_textures_release(
    box_gpu_ripple_field_t *f)
{
    int32_t i;
    for (i = 0; i < 2; i ++) {
        BOX_GPU_RIPPLES_RELEASE(f->views[i], wgpuTextureViewRelease);
        BOX_GPU_RIPPLES_RELEASE(f->textures[i], wgpuTextureRelease);
        BOX_GPU_RIPPLES_RELEASE(f->foam_views[i], wgpuTextureViewRelease);
        BOX_GPU_RIPPLES_RELEASE(f->foam[i], wgpuTextureRelease);
    }

    BOX_GPU_RIPPLES_RELEASE(f->velocity_view, wgpuTextureViewRelease);
    BOX_GPU_RIPPLES_RELEASE(f->velocity, wgpuTextureRelease);
    BOX_GPU_RIPPLES_RELEASE(f->scratch_view, wgpuTextureViewRelease);
    BOX_GPU_RIPPLES_RELEASE(f->scratch, wgpuTextureRelease);
}

/** Release the textures and buffers of a field and free it.
 *
 * @param f The field
 */
static void box_gpu_ripples_field_free(
    box_gpu_ripple_field_t *f)
{
    box_gpu_ripples_groups_release(f);
    box_gpu_ripples_textures_release(f);
    ecs_os_free(f);
}

void box_gpu_ripples_fini(
    box_gpu_t *gpu)
{
    box_gpu_ripples_t *s = gpu->ripples;
    int32_t i;
    if (!s) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&s->fields); i ++) {
        box_gpu_ripples_field_free(*ecs_vec_get_t(&s->fields, box_gpu_ripple_field_t*, i));
    }

    BOX_GPU_RIPPLES_RELEASE(s->clear, wgpuComputePipelineRelease);
    BOX_GPU_RIPPLES_RELEASE(s->advance, wgpuComputePipelineRelease);
    BOX_GPU_RIPPLES_RELEASE(s->flow, wgpuComputePipelineRelease);
    BOX_GPU_RIPPLES_RELEASE(s->advect, wgpuComputePipelineRelease);
    BOX_GPU_RIPPLES_RELEASE(s->settle, wgpuComputePipelineRelease);
    BOX_GPU_RIPPLES_RELEASE(s->pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_RIPPLES_RELEASE(s->layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_RIPPLES_RELEASE(s->flow_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_RIPPLES_RELEASE(s->flow_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_RIPPLES_RELEASE(s->advect_pipeline_layout, wgpuPipelineLayoutRelease);
    BOX_GPU_RIPPLES_RELEASE(s->advect_layout, wgpuBindGroupLayoutRelease);
    BOX_GPU_RIPPLES_RELEASE(s->steps, wgpuBufferRelease);
    BOX_GPU_RIPPLES_RELEASE(s->sources, wgpuBufferRelease);
    BOX_GPU_RIPPLES_RELEASE(s->flows, wgpuBufferRelease);
    BOX_GPU_RIPPLES_RELEASE(s->none_view, wgpuTextureViewRelease);
    BOX_GPU_RIPPLES_RELEASE(s->none, wgpuTextureRelease);
    BOX_GPU_RIPPLES_RELEASE(s->none_flow_view, wgpuTextureViewRelease);
    BOX_GPU_RIPPLES_RELEASE(s->none_flow, wgpuTextureRelease);
    ecs_vec_fini_t(NULL, &s->fields, box_gpu_ripple_field_t*);
    ecs_vec_fini_t(NULL, &s->step_data, uint8_t);
    ecs_vec_fini_t(NULL, &s->dispatches, box_gpu_ripple_dispatch_t);
    ecs_os_free(s);
    gpu->ripples = NULL;
done:
    return;
}

/** Create a compute pipeline of an entry point of water_ripples.wgsl.
 *
 * @param gpu The backend
 * @param layout The pipeline layout
 * @param module The module
 * @param entry The entry point
 * @return The pipeline, NULL while it is created
 */
static WGPUComputePipeline box_gpu_ripples_compute(
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
static bool box_gpu_ripples_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_ripples_t *s = gpu->ripples;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_WATER_RIPPLES);
    if (!shader || !shader->module || (s->advance && s->revision == shader->revision)) {
        goto done;
    }

    WGPUComputePipeline *current[5] = {
        &s->clear, &s->advance, &s->flow, &s->advect, &s->settle
    };
    WGPUComputePipeline next[5] = {
        box_gpu_ripples_compute(gpu, s->pipeline_layout, shader->module, "clear"),
        box_gpu_ripples_compute(gpu, s->pipeline_layout, shader->module, "advance"),
        box_gpu_ripples_compute(gpu, s->flow_pipeline_layout, shader->module, "flow"),
        box_gpu_ripples_compute(gpu, s->advect_pipeline_layout, shader->module, "advect"),
        box_gpu_ripples_compute(gpu, s->advect_pipeline_layout, shader->module, "settle")
    };
    if (box_gpu_compute_pipelines_swap(current, next, 5)) {
        s->revision = shader->revision;
    }

done:
    return shader && shader->module && s->advance && s->clear && s->flow && s->advect &&
        s->settle;
}

/** Find the field of a Water.
 *
 * @param s The state
 * @param water The Water entity
 * @return The field, NULL for none
 */
static box_gpu_ripple_field_t* box_gpu_ripples_find(
    const box_gpu_ripples_t *s,
    ecs_entity_t water)
{
    box_gpu_ripple_field_t *result = NULL;
    int32_t i;
    for (i = 0; s && i < ecs_vec_count(&s->fields) && !result; i ++) {
        box_gpu_ripple_field_t *f = *ecs_vec_get_t(&s->fields, box_gpu_ripple_field_t*, i);
        if (f->water == water) {
            result = f;
        }
    }

    return result;
}

/** Get the field of a record of a packet, created (flat) when it has none or its resolution
 * changed.
 *
 * @param gpu The backend
 * @param rec The record
 * @param fresh Receives whether the textures were created, so they are flat already
 * @return The field
 */
static box_gpu_ripple_field_t* box_gpu_ripples_ensure(
    box_gpu_t *gpu,
    const box_render_water_ripples_t *rec,
    bool *fresh)
{
    box_gpu_ripples_t *s = gpu->ripples;
    box_gpu_ripple_field_t *f = box_gpu_ripples_find(s, rec->water);
    int32_t i;
    *fresh = false;
    if (!f) {
        f = ecs_os_calloc_t(box_gpu_ripple_field_t);
        f->water = rec->water;
        *ecs_vec_append_t(NULL, &s->fields, box_gpu_ripple_field_t*) = f;
    }

    if (!f->textures[0] || f->resolution != rec->resolution) {
        box_gpu_ripples_textures_release(f);
        for (i = 0; i < 2; i ++) {
            f->textures[i] = box_gpu_ripples_texture(gpu, rec->resolution);
            f->views[i] = wgpuTextureCreateView(f->textures[i], NULL);
            f->foam[i] = box_gpu_ripples_texture(gpu, rec->resolution);
            f->foam_views[i] = wgpuTextureCreateView(f->foam[i], NULL);
        }

        f->velocity = box_gpu_ripples_texture_format(gpu, rec->resolution,
            WGPUTextureFormat_RGBA32Float);
        f->velocity_view = wgpuTextureCreateView(f->velocity, NULL);
        f->scratch = box_gpu_ripples_texture(gpu, rec->resolution);
        f->scratch_view = wgpuTextureCreateView(f->scratch, NULL);

        f->resolution = rec->resolution;
        f->current = 0;
        *fresh = true;
    }

    return f;
}

/** Append a step uniform of the frame.
 *
 * @param s The state
 * @param rec The record of the field
 * @param first Whether it is the first step of the frame
 * @param t Time fraction of the frame at the end of the step
 * @return Its slot
 */
static uint32_t box_gpu_ripples_step(
    box_gpu_ripples_t *s,
    const box_render_water_ripples_t *rec,
    bool first,
    float t)
{
    uint32_t result = (uint32_t)ecs_vec_count(&s->step_data) / BOX_GPU_RIPPLES_STEP_STRIDE;
    uint8_t *bytes = ecs_vec_grow_t(NULL, &s->step_data, uint8_t,
        (int32_t)BOX_GPU_RIPPLES_STEP_STRIDE);
    box_gpu_ripple_step_t *step = (box_gpu_ripple_step_t*)bytes;
    ecs_os_memset(bytes, 0, (ecs_size_t)BOX_GPU_RIPPLES_STEP_STRIDE);
    step->grid[0] = rec->resolution;
    step->grid[1] = (uint32_t)rec->origin[0];
    step->grid[2] = (uint32_t)rec->origin[1];
    step->grid[3] = (uint32_t)rec->source_count;
    step->shift[0] = rec->shift[0];
    step->shift[1] = rec->shift[1];
    step->shift[2] = first ? 1 : 0;
    step->shift[3] = rec->source_first;
    step->wave[0] = rec->courant2;
    step->wave[1] = rec->keep;
    step->wave[2] = (float)(rec->resolution / BOX_GPU_RIPPLES_BORDER);
    step->wave[3] = t;
    step->foam[0] = rec->foam_spread;
    step->foam[1] = rec->foam_keep;
    step->foam[2] = rec->crest;
    step->foam[3] = rec->cell > 0.0f ? 0.5f / rec->cell : 0.0f;
    step->flow[0] = rec->velocity[0];
    step->flow[1] = rec->velocity[1];
    step->flow[2] = rec->cell > 0.0f ? (float)(BOX_RENDER_RIPPLE_STEP / rec->cell) : 0.0f;
    step->flow[3] = rec->cell > 0.0f ? BOX_RENDER_RIPPLE_FLOW_FADE / rec->cell : 0.0f;
    step->flows[0] = (uint32_t)rec->flow_first;
    step->flows[1] = (uint32_t)rec->flow_count;

    return result;
}

/** Append a dispatch of the frame.
 *
 * @param s The state
 * @param field Index of the field
 * @param group Group of the field
 * @param slot Step uniform
 * @param pass The entry point
 */
static void box_gpu_ripples_dispatch(
    box_gpu_ripples_t *s,
    int32_t field,
    int32_t group,
    uint32_t slot,
    box_gpu_ripple_pass_t pass)
{
    box_gpu_ripple_dispatch_t *d = ecs_vec_append_t(
        NULL, &s->dispatches, box_gpu_ripple_dispatch_t);
    d->field = field;
    d->group = group;
    d->slot = slot;
    d->pass = pass;
}

/** Make a buffer hold at least a size, created again larger when it does not.
 *
 * @param gpu The backend
 * @param buffer The buffer
 * @param size Its bytes
 * @param needed Bytes it must hold
 * @param usage Its usage
 */
static void box_gpu_ripples_reserve(
    box_gpu_t *gpu,
    WGPUBuffer *buffer,
    uint64_t *size,
    uint64_t needed,
    WGPUBufferUsage usage)
{
    if (!*buffer || *size < needed) {
        BOX_GPU_RIPPLES_RELEASE(*buffer, wgpuBufferRelease);
        *size = needed < 256 ? 256 : needed;
        *buffer = box_gpu_ripples_buffer(gpu, *size, usage);
    }
}

/** Keep the record the views of the scene of a field take.
 *
 * @param f The field
 * @param rec Its record of the packet
 */
static void box_gpu_ripples_record(
    box_gpu_ripple_field_t *f,
    const box_render_water_ripples_t *rec)
{
    f->record = (box_gpu_ripple_record_t){
        .a = { rec->corner[0], rec->corner[1], rec->cell > 0.0f ? 1.0f / rec->cell : 0.0f,
            rec->strength },
        .b = { rec->height, BOX_RENDER_RIPPLE_PLANE_TOLERANCE,
            (float)(rec->resolution / BOX_GPU_RIPPLES_BORDER), rec->cell },
        .c = { rec->resolution, (uint32_t)rec->origin[0], (uint32_t)rec->origin[1], 1u }
    };
    f->scene = rec->scene;
    f->origin[0] = rec->origin[0];
    f->origin[1] = rec->origin[1];
    f->corner[0] = rec->corner[0];
    f->corner[1] = rec->corner[1];
    f->cell = rec->cell;
    f->height = rec->height;
}

/** Create the groups of the frame of a field: the two of the steps, the one of the flow pass and
 * the ones of the advect and settle passes.
 *
 * @param gpu The backend
 * @param f The field
 */
static void box_gpu_ripples_groups(
    box_gpu_t *gpu,
    box_gpu_ripple_field_t *f)
{
    box_gpu_ripples_t *s = gpu->ripples;
    int32_t k;
    box_gpu_ripples_groups_release(f);
    for (k = 0; k < 2; k ++) {
        WGPUBindGroupEntry entries[6] = {
            { .binding = 0, .textureView = f->views[k] },
            { .binding = 1, .textureView = f->views[1 - k] },
            { .binding = 4, .textureView = f->foam_views[k] },
            { .binding = 5, .textureView = f->foam_views[1 - k] },
            {
                .binding = 2,
                .buffer = s->steps,
                .offset = 0,
                .size = sizeof(box_gpu_ripple_step_t)
            },
            { .binding = 3, .buffer = s->sources, .offset = 0, .size = s->sources_size }
        };
        f->groups[k] = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = s->layout,
            .entryCount = 6,
            .entries = entries
        });
    }

    WGPUBindGroupEntry step = {
        .binding = 2,
        .buffer = s->steps,
        .offset = 0,
        .size = sizeof(box_gpu_ripple_step_t)
    };
    WGPUBindGroupEntry flow_entries[3] = {
        step,
        { .binding = 6, .buffer = s->flows, .offset = 0, .size = s->flows_size },
        { .binding = 7, .textureView = f->velocity_view }
    };
    f->flow_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->flow_layout,
        .entryCount = 3,
        .entries = flow_entries
    });

    WGPUTextureView targets[4] = {
        f->views[0], f->views[1], f->foam_views[0], f->foam_views[1]
    };
    for (k = 0; k < 4; k ++) {
        WGPUBindGroupEntry advect[4] = {
            step,
            { .binding = 8, .textureView = targets[k] },
            { .binding = 9, .textureView = f->scratch_view },
            { .binding = 10, .textureView = f->velocity_view }
        };
        f->advect[k] = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = s->advect_layout,
            .entryCount = 4,
            .entries = advect
        });
        advect[1].textureView = f->scratch_view;
        advect[2].textureView = targets[k];
        f->settle[k] = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = s->advect_layout,
            .entryCount = 4,
            .entries = advect
        });
    }
}

void box_gpu_ripples_simulate(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_ripples_t *s = gpu->ripples;
    int32_t i, count = ecs_vec_count(&packet->water_ripples);
    if (!s) {
        goto done;
    }

    for (i = ecs_vec_count(&s->fields) - 1; i >= 0; i --) {
        box_gpu_ripple_field_t *f = *ecs_vec_get_t(&s->fields, box_gpu_ripple_field_t*, i);
        bool used = false;
        int32_t j;
        for (j = 0; j < count && !used; j ++) {
            used = ecs_vec_get_t(&packet->water_ripples, box_render_water_ripples_t,
                j)->water == f->water;
        }

        if (!used) {
            box_gpu_ripples_field_free(f);
            ecs_vec_remove_t(&s->fields, box_gpu_ripple_field_t*, i);
        }
    }

    if (!count) {
        goto done;
    }

    bool ready = box_gpu_ripples_pipelines(gpu);
    ecs_vec_clear(&s->step_data);
    ecs_vec_clear(&s->dispatches);
    for (i = 0; i < count; i ++) {
        const box_render_water_ripples_t *rec = ecs_vec_get_t(
            &packet->water_ripples, box_render_water_ripples_t, i);
        bool fresh;
        box_gpu_ripple_field_t *f = box_gpu_ripples_ensure(gpu, rec, &fresh);
        uint32_t k;
        f->frame = packet->frame;
        box_gpu_ripples_record(f, rec);
        if (!ready) {
            continue;
        }

        int32_t field = 0;
        while (*ecs_vec_get_t(&s->fields, box_gpu_ripple_field_t*, field) != f) {
            field ++;
        }

        uint32_t first = box_gpu_ripples_step(s, rec, false, 0.0f);
        if (!fresh && (rec->reset || rec->shift[0] || rec->shift[1])) {
            box_gpu_ripples_dispatch(s, field, 0, first, BoxGpuRipplesClear);
            box_gpu_ripples_dispatch(s, field, 1, first, BoxGpuRipplesClear);
        }

        box_gpu_ripples_dispatch(s, field, 0, first, BoxGpuRipplesFlow);
        int32_t current = f->current;
        for (k = 0; k < rec->steps; k ++) {
            uint32_t slot = box_gpu_ripples_step(s, rec, k == 0,
                (float)(k + 1) / (float)rec->steps);
            if (rec->flowing) {
                int32_t moved[3] = { current, 1 - current, 2 + current }, m;
                for (m = 0; m < 3; m ++) {
                    box_gpu_ripples_dispatch(s, field, moved[m], slot, BoxGpuRipplesAdvect);
                    box_gpu_ripples_dispatch(s, field, moved[m], slot, BoxGpuRipplesSettle);
                }
            }

            box_gpu_ripples_dispatch(s, field, current, slot, BoxGpuRipplesAdvance);
            current = 1 - current;
        }
    }

    if (!ecs_vec_count(&s->dispatches)) {
        goto done;
    }

    int32_t sources = ecs_vec_count(&packet->ripple_sources);
    int32_t flows = ecs_vec_count(&packet->ripple_flows);
    uint64_t source_bytes = (uint64_t)(sources ? sources : 1) *
        sizeof(box_render_ripple_source_t);
    uint64_t flow_bytes = (uint64_t)(flows ? flows : 1) * sizeof(box_render_ripple_flow_t);
    uint64_t step_bytes = (uint64_t)ecs_vec_count(&s->step_data);
    box_gpu_ripples_reserve(gpu, &s->steps, &s->steps_size, step_bytes,
        WGPUBufferUsage_Uniform);
    box_gpu_ripples_reserve(gpu, &s->sources, &s->sources_size, source_bytes,
        WGPUBufferUsage_Storage);
    box_gpu_ripples_reserve(gpu, &s->flows, &s->flows_size, flow_bytes,
        WGPUBufferUsage_Storage);
    if (flows) {
        wgpuQueueWriteBuffer(gpu->queue, s->flows, 0, ecs_vec_first(&packet->ripple_flows),
            (size_t)flows * sizeof(box_render_ripple_flow_t));
    }

    wgpuQueueWriteBuffer(gpu->queue, s->steps, 0, ecs_vec_first(&s->step_data),
        (size_t)step_bytes);
    if (sources) {
        wgpuQueueWriteBuffer(gpu->queue, s->sources, 0, ecs_vec_first(&packet->ripple_sources),
            (size_t)sources * sizeof(box_render_ripple_source_t));
    }

    for (i = 0; i < ecs_vec_count(&s->fields); i ++) {
        box_gpu_ripples_groups(gpu, *ecs_vec_get_t(&s->fields, box_gpu_ripple_field_t*, i));
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    for (i = 0; i < ecs_vec_count(&s->dispatches); i ++) {
        const box_gpu_ripple_dispatch_t *d = ecs_vec_get_t(
            &s->dispatches, box_gpu_ripple_dispatch_t, i);
        box_gpu_ripple_field_t *f = *ecs_vec_get_t(
            &s->fields, box_gpu_ripple_field_t*, d->field);
        uint32_t offset = d->slot * BOX_GPU_RIPPLES_STEP_STRIDE;
        uint32_t groups = (f->resolution + BOX_GPU_RIPPLES_GROUP - 1) / BOX_GPU_RIPPLES_GROUP;
        WGPUComputePipeline pipeline = s->advance;
        WGPUBindGroup group = f->groups[d->group];
        switch (d->pass) {
        case BoxGpuRipplesAdvance:
            f->current = 1 - d->group;
            break;
        case BoxGpuRipplesClear:
            pipeline = s->clear;
            break;
        case BoxGpuRipplesFlow:
            pipeline = s->flow;
            group = f->flow_group;
            break;
        case BoxGpuRipplesAdvect:
            pipeline = s->advect;
            group = f->advect[d->group];
            break;
        case BoxGpuRipplesSettle:
            pipeline = s->settle;
            group = f->settle[d->group];
            break;
        }

        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, groups, 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    for (i = 0; i < ecs_vec_count(&s->fields); i ++) {
        box_gpu_ripples_groups_release(*ecs_vec_get_t(&s->fields, box_gpu_ripple_field_t*, i));
    }

done:
    return;
}

/** Find the field of the scene of a view.
 *
 * @param gpu The backend
 * @param v The view, NULL for none
 * @return The first field of its scene, NULL for none
 */
static const box_gpu_ripple_field_t* box_gpu_ripples_of_view(
    const box_gpu_t *gpu,
    const box_render_view_t *v)
{
    const box_gpu_ripples_t *s = gpu->ripples;
    const box_gpu_ripple_field_t *field = NULL;
    int32_t i;
    for (i = 0; s && v && i < ecs_vec_count(&s->fields) && !field; i ++) {
        const box_gpu_ripple_field_t *f = *ecs_vec_get_t(
            &s->fields, box_gpu_ripple_field_t*, i);
        if (f->scene == v->scene) {
            field = f;
        }
    }

    return field;
}

void box_gpu_ripples_layout(
    WGPUBindGroupLayoutEntry *entries)
{
    uint32_t bindings[2] = { BOX_GPU_RIPPLES_HEIGHTS_BINDING, BOX_GPU_RIPPLES_FOAM_BINDING };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        entries[bindings[i]] = (WGPUBindGroupLayoutEntry){
            .binding = bindings[i],
            .visibility = WGPUShaderStage_Fragment,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_ReadOnly,
                .format = WGPUTextureFormat_R32Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        };
    }

    entries[BOX_GPU_RIPPLES_FLOW_BINDING] = (WGPUBindGroupLayoutEntry){
        .binding = BOX_GPU_RIPPLES_FLOW_BINDING,
        .visibility = WGPUShaderStage_Fragment,
        .storageTexture = {
            .access = WGPUStorageTextureAccess_ReadOnly,
            .format = WGPUTextureFormat_RGBA32Float,
            .viewDimension = WGPUTextureViewDimension_2D
        }
    };
}

void box_gpu_ripples_bindings(
    box_gpu_t *gpu,
    const box_render_view_t *v,
    WGPUBindGroupEntry *entries)
{
    const box_gpu_ripple_field_t *field = box_gpu_ripples_of_view(gpu, v);
    entries[BOX_GPU_RIPPLES_HEIGHTS_BINDING] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_RIPPLES_HEIGHTS_BINDING,
        .textureView = field ? field->views[field->current] : gpu->ripples->none_view
    };
    entries[BOX_GPU_RIPPLES_FOAM_BINDING] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_RIPPLES_FOAM_BINDING,
        .textureView = field ? field->foam_views[field->current] : gpu->ripples->none_view
    };
    entries[BOX_GPU_RIPPLES_FLOW_BINDING] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_RIPPLES_FLOW_BINDING,
        .textureView = field && field->velocity_view
            ? field->velocity_view : gpu->ripples->none_flow_view
    };
}

void box_gpu_ripples_uniforms(
    const box_gpu_t *gpu,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u)
{
    const box_gpu_ripple_field_t *field = box_gpu_ripples_of_view(gpu, v);
    box_gpu_ripple_record_t none = {0};
    const box_gpu_ripple_record_t *r = field ? &field->record : &none;
    ecs_os_memcpy_n(u->ripple_a, r->a, float, 4);
    ecs_os_memcpy_n(u->ripple_b, r->b, float, 4);
    ecs_os_memcpy_n(u->ripple_c, r->c, uint32_t, 4);
}

WGPUTextureView box_gpu_ripples_flow_view(
    const box_gpu_t *gpu,
    const box_render_view_t *v)
{
    const box_gpu_ripple_field_t *field = box_gpu_ripples_of_view(gpu, v);

    return field && field->velocity_view ? field->velocity_view : gpu->ripples->none_flow_view;
}

/** What box_gpu_ripples_read copies */
typedef enum box_gpu_ripple_read_t {
    BoxGpuRipplesReadHeights,       /**< The heights */
    BoxGpuRipplesReadFoam,          /**< The foam */
    BoxGpuRipplesReadFlow           /**< The world velocity X and Z */
} box_gpu_ripple_read_t;

/** Copy the heights, the foam or the velocity of the field of a Water into memory, see
 * box_render_gpu_read_ripples.
 *
 * @param world The world
 * @param water The Water entity
 * @param what What to copy
 * @param info Receives the window, NULL for none
 * @param out Receives the values, NULL for the window only
 * @param count Floats out holds
 * @return Whether the field exists and was copied
 */
static bool box_gpu_ripples_read(
    ecs_world_t *world,
    ecs_entity_t water,
    box_gpu_ripple_read_t what,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    float *texels = NULL;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_read_ripples needs RenderSettings.main_thread and box_render_sync");
    const box_gpu_ripple_field_t *f = renders ? box_gpu_ripples_find(gpu->ripples, water) : NULL;
    if (!f || !f->textures[0]) {
        goto done;
    }

    if (info) {
        info->resolution = (int32_t)f->resolution;
        info->cell = f->cell;
        info->corner[0] = f->corner[0];
        info->corner[1] = f->corner[1];
        info->height = f->height;
    }

    if (!out) {
        result = true;
        goto done;
    }

    uint32_t texel = what == BoxGpuRipplesReadFlow ? 4 : 1;
    uint32_t values = what == BoxGpuRipplesReadFlow ? 2 : 1, c;
    uint32_t x, z, res = f->resolution, row = res * 4 * texel;
    if ((int64_t)res * res * values > count) {
        goto done;
    }

    uint32_t stride = (row + BOX_GPU_ROW_ALIGN - 1) / BOX_GPU_ROW_ALIGN * BOX_GPU_ROW_ALIGN;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)stride * res
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = what == BoxGpuRipplesReadFlow ? f->velocity
                : what == BoxGpuRipplesReadFoam ? f->foam[f->current]
                : f->textures[f->current],
            .aspect = WGPUTextureAspect_All
        },
        &(WGPUTexelCopyBufferInfo){
            .layout = { .bytesPerRow = stride, .rowsPerImage = res },
            .buffer = buffer
        },
        &(WGPUExtent3D){ res, res, 1 });
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    texels = ecs_os_malloc((ecs_size_t)(stride * res));
    result = box_gpu_read(gpu, buffer, 0, stride * res, texels);
    wgpuBufferRelease(buffer);
    for (z = 0; result && z < res; z ++) {
        const float *line = texels + (size_t)((z + (uint32_t)f->origin[1]) % res) *
            (stride / 4);
        for (x = 0; x < res; x ++) {
            for (c = 0; c < values; c ++) {
                out[((size_t)z * res + x) * values + c] =
                    line[((x + (uint32_t)f->origin[0]) % res) * texel + c];
            }
        }
    }

done:
    ecs_os_free(texels);

    return result;
}

bool box_render_gpu_read_ripples(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count)
{
    return box_gpu_ripples_read(world, water, BoxGpuRipplesReadHeights, info, out, count);
}

bool box_render_gpu_read_ripple_foam(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count)
{
    return box_gpu_ripples_read(world, water, BoxGpuRipplesReadFoam, info, out, count);
}

bool box_render_gpu_read_ripple_flow(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count)
{
    return box_gpu_ripples_read(world, water, BoxGpuRipplesReadFlow, info, out, count);
}

#else

bool box_render_gpu_read_ripple_flow(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count)
{
    (void)world;
    (void)water;
    (void)info;
    (void)out;
    (void)count;

    return false;
}

bool box_render_gpu_read_ripples(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count)
{
    (void)world;
    (void)water;
    (void)info;
    (void)out;
    (void)count;

    return false;
}

bool box_render_gpu_read_ripple_foam(
    ecs_world_t *world,
    ecs_entity_t water,
    box_render_ripples_read_t *info,
    float *out,
    int32_t count)
{
    (void)world;
    (void)water;
    (void)info;
    (void)out;
    (void)count;

    return false;
}

#endif
