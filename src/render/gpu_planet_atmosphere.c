/**
 * @file render/gpu_planet_atmosphere.c
 * @brief Planet atmosphere shells of the webgpu backend: the shells of the PlanetAtmospheres a
 * view draws, raymarched in its forward pass (etc/shaders/planet_atmosphere.wgsl).
 *
 * The forward pass of a view with planet shells ends after its shared meshes and its sky and
 * continues in a pass that reads its depth, where one draw of the box around the top sphere of
 * each planet marches the shells back to front, before the fog volumes and the blended draws.
 *
 * Group 1 holds the uniform of a draw, the depth and the LUTs of its planet; the uniforms of
 * every draw of a packet follow each other in one buffer, which grows when a draw does not fit.
 * The planet lights of the meshes (planet_light.wgsl) are bound in the view group instead.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes between the uniforms of two draws: the offset alignment of webgpu */
#define BOX_GPU_PLANET_UNIFORM_ALIGN (256u)

/** Smallest buffer of the draws of a packet in bytes */
#define BOX_GPU_PLANET_BUFFER_MIN (16384u)

/** Uniform of a draw, matching Planet in planet_atmosphere.wgsl */
typedef struct box_gpu_planet_uniforms_t {
    box_render_atmosphere_desc_t medium; /**< The medium of the planet */
    float center[4];                /**< World center, km per world unit */
    float sun[4];                   /**< Direction toward the sun, Mie anisotropy */
    float sun_color[4];             /**< Top of atmosphere irradiance, intensity */
    float viewport[4];              /**< Inverse render width and height */
    mat4 view_proj;                 /**< World to clip space of the target */
    mat4 inv_view_proj;             /**< Clip space of the target to world */
} box_gpu_planet_uniforms_t;

/** Bytes a draw takes in the buffer */
#define BOX_GPU_PLANET_UNIFORM_STRIDE \
    ((sizeof(box_gpu_planet_uniforms_t) + BOX_GPU_PLANET_UNIFORM_ALIGN - 1) & \
        ~(size_t)(BOX_GPU_PLANET_UNIFORM_ALIGN - 1))

struct box_gpu_planets_t {
    WGPUBindGroupLayout layout;     /**< Uniform, depth and LUTs of a draw */
    WGPUPipelineLayout pipeline_layout; /**< View group and the group of a draw */
    ecs_map_t pipelines;            /**< Color format to the WGPURenderPipeline that draws into
                                     *   it */
    int32_t revision;               /**< Revision of the shader of the pipelines */
    WGPUBuffer buffer;              /**< Uniforms of the draws of the packet */
    uint64_t size;                  /**< Bytes of the buffer */
    uint64_t used;                  /**< Bytes of the buffer the draws of the packet took */
    WGPUBuffer lights;              /**< box_gpu_planet_light_t by planet light slot */
    bool lights_written;            /**< The table has a planet since it was last cleared */
    WGPUTexture luts;               /**< Transmittance LUT array, layer slot - 1, NULL before
                                     *   the first planet light */
    WGPUTextureView luts_view;      /**< 2D array view of luts */
    uint32_t layers;                /**< Layers of luts */
    WGPUTexture placeholder;        /**< 1x1 LUT array bound before the first planet light */
    WGPUTextureView placeholder_view; /**< 2D array view of the placeholder */
    uint32_t copied[BOX_RENDER_PLANET_LIGHT_MAX]; /**< By slot, the handle of the atmosphere
                                     *   whose LUT its layer holds, 0 for none */
    int32_t builds[BOX_RENDER_PLANET_LIGHT_MAX]; /**< By slot, the builds of that LUT */
};

/** Create a 2D array of transmittance LUTs that the planet light binding reads, a read only
 * storage texture: the sampled textures of the mesh pipelines are at the limit of the device.
 *
 * @param gpu The backend
 * @param width Texels of a row
 * @param height Rows
 * @param layers Layers
 * @param view Receives the 2D array view
 * @return The texture
 */
static WGPUTexture box_gpu_planet_luts_new(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    uint32_t layers,
    WGPUTextureView *view)
{
    WGPUTexture result = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, layers },
        .format = WGPUTextureFormat_RGBA16Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    *view = wgpuTextureCreateView(result, &(WGPUTextureViewDescriptor){
        .format = WGPUTextureFormat_RGBA16Float,
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = layers,
        .aspect = WGPUTextureAspect_All
    });

    return result;
}

void box_gpu_planets_init(
    box_gpu_t *gpu)
{
    box_gpu_planets_t *f = ecs_os_calloc_t(box_gpu_planets_t);
    WGPUBindGroupLayoutEntry entries[5] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_planet_uniforms_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    f->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 5, .entries = entries });
    WGPUBindGroupLayout groups[2] = { gpu->view_layout, f->layout };
    f->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = groups
        });
    ecs_map_init(&f->pipelines, NULL);
    f->lights = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_GPU_PLANET_LIGHTS_SIZE
    });
    f->placeholder = box_gpu_planet_luts_new(gpu, 1, 1, 1, &f->placeholder_view);
    gpu->planets = f;
}

/** Release the pipelines of every format.
 *
 * @param f The planet shell state
 */
static void box_gpu_planet_pipelines_free(
    box_gpu_planets_t *f)
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

void box_gpu_planets_fini(
    box_gpu_t *gpu)
{
    box_gpu_planets_t *f = gpu->planets;
    if (!f) {
        goto done;
    }

    box_gpu_planet_pipelines_free(f);
    ecs_map_fini(&f->pipelines);
    if (f->buffer) {
        wgpuBufferRelease(f->buffer);
    }

    if (f->luts) {
        wgpuTextureViewRelease(f->luts_view);
        wgpuTextureRelease(f->luts);
    }

    wgpuTextureViewRelease(f->placeholder_view);
    wgpuTextureRelease(f->placeholder);
    wgpuBufferRelease(f->lights);
    wgpuPipelineLayoutRelease(f->pipeline_layout);
    wgpuBindGroupLayoutRelease(f->layout);
    ecs_os_free(f);
    gpu->planets = NULL;
done:
    return;
}

void box_gpu_planets_begin(
    box_gpu_t *gpu)
{
    if (gpu->planets) {
        gpu->planets->used = 0;
    }
}

/** Get the pipeline that draws into a color format, created on first use and again when the
 * shader was recompiled.
 *
 * Front faces are culled, so the box covers its pixels from inside too; there is no depth test
 * and no depth write. The color adds the scattered light to the target times the transmittance,
 * the alpha of the target is kept.
 *
 * @param gpu The backend
 * @param format The format of the color target
 * @return The pipeline, NULL until the shader exists
 */
static WGPURenderPipeline box_gpu_planet_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_planets_t *f = gpu->planets;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_PLANET_ATMOSPHERE);
    WGPURenderPipeline result = NULL;
    ecs_map_val_t *cached;
    if (!shader || !shader->module) {
        goto done;
    }

    if (f->revision != shader->revision) {
        box_gpu_planet_pipelines_free(f);
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
        .layout = f->pipeline_layout,
        .vertex = {
            .module = shader->module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_Front
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

/** Make the LUT array hold a layer per slot up to a slot, replacing it with a larger one,
 * whose layers are copied again.
 *
 * @param gpu The backend
 * @param f The planet state
 * @param slots Highest slot that needs a layer
 */
static void box_gpu_planet_luts_reserve(
    box_gpu_t *gpu,
    box_gpu_planets_t *f,
    uint32_t slots)
{
    uint32_t layers = f->layers ? f->layers : 1u;
    if (f->luts && slots <= f->layers) {
        goto done;
    }

    while (layers < slots) {
        layers *= 2u;
    }

    if (f->luts) {
        wgpuTextureViewRelease(f->luts_view);
        wgpuTextureRelease(f->luts);
    }

    f->luts = box_gpu_planet_luts_new(gpu, BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH,
        BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT, layers, &f->luts_view);
    f->layers = layers;
    ecs_os_memset_n(f->copied, 0, uint32_t, BOX_RENDER_PLANET_LIGHT_MAX);
done:
    return;
}

void box_gpu_planet_lights_prepare(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_planets_t *f = gpu->planets;
    box_gpu_planet_light_t table[BOX_RENDER_PLANET_LIGHT_MAX];
    WGPUTexture sources[BOX_RENDER_PLANET_LIGHT_MAX];
    int32_t builds[BOX_RENDER_PLANET_LIGHT_MAX];
    WGPUCommandEncoder encoder = NULL;
    int32_t i, count = ecs_vec_count(&packet->planet_lights);
    uint32_t slots = 0, slot;
    bool any = false;
    if (!f || (!count && !f->lights_written)) {
        goto done;
    }

    ecs_os_memset_n(table, 0, box_gpu_planet_light_t, BOX_RENDER_PLANET_LIGHT_MAX);
    ecs_os_memset_n(sources, 0, WGPUTexture, BOX_RENDER_PLANET_LIGHT_MAX);
    for (i = 0; i < count; i ++) {
        const box_render_planet_light_t *l = ecs_vec_get_t(
            &packet->planet_lights, box_render_planet_light_t, i);
        slot = l->slot;
        if (!slot || slot >= BOX_RENDER_PLANET_LIGHT_MAX ||
            !box_gpu_atmosphere_transmittance(gpu, l->atmosphere, &sources[slot],
                &builds[slot]))
        {
            continue;
        }

        ecs_os_memcpy_n(table[slot].center, l->center, float, 4);
        table[slot].radii[0] = l->radii[0];
        table[slot].radii[1] = l->radii[1];
        table[slot].radii[2] = (float)(slot - 1u);
        ecs_os_memcpy_n(table[slot].sun, l->sun, float, 4);
        if (!(table[slot].center[3] > 0.0f)) {
            sources[slot] = NULL;
            ecs_os_memset_t(&table[slot], 0, box_gpu_planet_light_t);
            continue;
        }

        slots = slot > slots ? slot : slots;
        any = true;
    }

    if (any) {
        box_gpu_planet_luts_reserve(gpu, f, slots);
    }

    for (slot = 1; any && slot < BOX_RENDER_PLANET_LIGHT_MAX; slot ++) {
        const box_render_planet_light_t *l = NULL;
        if (!sources[slot]) {
            continue;
        }

        for (i = 0; i < count && !l; i ++) {
            l = ecs_vec_get_t(&packet->planet_lights, box_render_planet_light_t, i);
            l = l->slot == slot ? l : NULL;
        }

        if (!l || (f->copied[slot] == l->atmosphere && f->builds[slot] == builds[slot])) {
            continue;
        }

        if (!encoder) {
            encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        }

        wgpuCommandEncoderCopyTextureToTexture(encoder,
            &(WGPUTexelCopyTextureInfo){
                .texture = sources[slot], .aspect = WGPUTextureAspect_All
            },
            &(WGPUTexelCopyTextureInfo){
                .texture = f->luts,
                .origin = { 0, 0, slot - 1u },
                .aspect = WGPUTextureAspect_All
            },
            &(WGPUExtent3D){
                BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH, BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT, 1
            });
        f->copied[slot] = l->atmosphere;
        f->builds[slot] = builds[slot];
    }

    if (encoder) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

    wgpuQueueWriteBuffer(gpu->queue, f->lights, 0, table, sizeof(table));
    f->lights_written = any;
done:
    return;
}

void box_gpu_planet_lights_bindings(
    box_gpu_t *gpu,
    WGPUBindGroupEntry *out)
{
    box_gpu_planets_t *f = gpu->planets;
    out[0] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_PLANET_LIGHTS_BINDING,
        .buffer = f->lights,
        .size = BOX_GPU_PLANET_LIGHTS_SIZE
    };
    out[1] = (WGPUBindGroupEntry){
        .binding = BOX_GPU_PLANET_LUTS_BINDING,
        .textureView = f->luts ? f->luts_view : f->placeholder_view
    };
}

bool box_gpu_planets_ready(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    return gpu->planets && v->planet_count > 0 &&
        box_gpu_shader_get(gpu, BOX_RENDER_SHADER_PLANET_ATMOSPHERE) != NULL;
}

/** Make room for the uniforms of the draws of a view in the buffer of the packet, replacing the
 * buffer with a larger one when they do not fit; earlier draws keep the old one through their
 * bind groups.
 *
 * @param gpu The backend
 * @param need Bytes of the draws
 * @return Offset of the first draw in the buffer
 */
static uint64_t box_gpu_planet_reserve(
    box_gpu_t *gpu,
    uint64_t need)
{
    box_gpu_planets_t *f = gpu->planets;
    uint64_t offset = f->used;
    if (!f->buffer || offset + need > f->size) {
        uint64_t size = f->size ? f->size : BOX_GPU_PLANET_BUFFER_MIN;
        while (size < need * 2) {
            size *= 2;
        }

        if (f->buffer) {
            wgpuBufferRelease(f->buffer);
        }

        f->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = size
        });
        f->size = size;
        offset = 0;
    }

    f->used = offset + need;

    return offset;
}

bool box_gpu_planets_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    const box_gpu_target_t *t,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    WGPUTextureView depth)
{
    box_gpu_planets_t *f = gpu->planets;
    WGPURenderPipeline pipeline = box_gpu_planets_ready(gpu, v)
        ? box_gpu_planet_pipeline(gpu, format)
        : NULL;
    bool result = false;
    int32_t i;
    if (!pipeline ||
        v->planet_first + v->planet_count > ecs_vec_count(&packet->planet_atmospheres))
    {
        goto done;
    }

    const box_render_planet_atmos_t *planets = ecs_vec_get_t(
        &packet->planet_atmospheres, box_render_planet_atmos_t, v->planet_first);
    const uint64_t stride = BOX_GPU_PLANET_UNIFORM_STRIDE;
    uint64_t offset = box_gpu_planet_reserve(gpu, stride * (uint64_t)v->planet_count);
    box_gpu_planet_uniforms_t u;
    mat4 proj;
    ecs_os_memset_t(&u, 0, box_gpu_planet_uniforms_t);
    box_camera_projection(&v->camera_data,
        (float)t->desc.point_width, (float)t->desc.point_height, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), u.view_proj);
    glm_mat4_inv(u.view_proj, u.inv_view_proj);
    u.viewport[0] = 1.0f / (float)(t->desc.render_width > 0 ? t->desc.render_width : 1);
    u.viewport[1] = 1.0f / (float)(t->desc.render_height > 0 ? t->desc.render_height : 1);
    for (i = 0; i < v->planet_count; i ++) {
        const box_render_planet_atmos_t *planet = &planets[i];
        WGPUTextureView transmittance = NULL, multi = NULL;
        WGPUSampler sampler = NULL;
        uint64_t at = offset + stride * (uint64_t)i;
        if (!box_gpu_atmosphere_luts(gpu, planet->atmosphere, &transmittance, &multi,
            &sampler))
        {
            continue;
        }

        u.medium = planet->medium;
        ecs_os_memcpy_n(u.center, planet->center, float, 4);
        ecs_os_memcpy_n(u.sun, planet->sun, float, 4);
        ecs_os_memcpy_n(u.sun_color, planet->sun_color, float, 4);
        wgpuQueueWriteBuffer(gpu->queue, f->buffer, at, &u, sizeof(u));

        WGPUBindGroupEntry entries[5] = {
            {
                .binding = 0,
                .buffer = f->buffer,
                .offset = at,
                .size = sizeof(box_gpu_planet_uniforms_t)
            },
            { .binding = 1, .textureView = depth },
            { .binding = 2, .textureView = transmittance },
            { .binding = 3, .textureView = multi },
            { .binding = 4, .sampler = sampler }
        };
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device,
            &(WGPUBindGroupDescriptor){
                .layout = f->layout,
                .entryCount = 5,
                .entries = entries
            });
        if (!result) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        }

        wgpuRenderPassEncoderSetBindGroup(pass, 1, group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 36, 1, 0, 0);
        wgpuBindGroupRelease(group);
        box_gpu_count(gpu, BOX_GPU_STAT_DRAW);
        result = true;
    }

done:
    return result;
}

#endif
