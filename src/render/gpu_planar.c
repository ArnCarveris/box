/**
 * @file render/gpu_planar.c
 * @brief Planar reflections of the webgpu backend: the mirrored image of the scene a view
 * renders about each of its reflection planes, before its forward pass.
 *
 * A reflection draws with the projection of its view, its near plane made oblique to the plane
 * (box_render_planar_oblique), and the view matrix of the view times the reflection; its front
 * faces wind clockwise, it is lit like the view and culled by a frustum of its own.
 *
 * Its image, kept per target, view and slot, is copied with its coverage into a layer of the
 * reflection array of the view and blurred into its mips (planar.wgsl), which the forward pass
 * samples; both are released once no packet renders them.
 */

#include "gpu_environment.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

/** Mips of a reflection array at most */
#define BOX_GPU_PLANAR_MIPS (12)

/** The images of a planar reflection of a view in a target */
typedef struct box_gpu_planar_image_t {
    uint32_t target;                /**< Handle of the target */
    ecs_entity_t view;              /**< View entity */
    int32_t slot;                   /**< Slot of the plane in the view */
    int64_t frame;                  /**< Frame of the last packet that rendered it */
    uint32_t width;                 /**< Width in pixels */
    uint32_t height;                /**< Height in pixels */
    WGPUTexture color;              /**< BOX_GPU_HDR_FORMAT image of the reflection */
    WGPUTextureView color_view;     /**< View rendered into and sampled */
    WGPUTexture depth;              /**< Depth of the reflection */
    WGPUTextureView depth_view;     /**< View rendered into */
} box_gpu_planar_image_t;

/** The reflection array of a view in a target: a layer per plane slot, each image in the
 * corner of its layer, and a blur pyramid */
typedef struct box_gpu_planar_array_t {
    uint32_t target;                /**< Handle of the target */
    ecs_entity_t view;              /**< View entity */
    int64_t frame;                  /**< Frame of the last packet that rendered it */
    uint32_t width;                 /**< Width of mip 0, the widest image of the view */
    uint32_t height;                /**< Height of mip 0, the tallest image of the view */
    uint32_t mip_count;             /**< Mip levels */
    WGPUTexture texture;            /**< BOX_GPU_HDR_FORMAT, BOX_PLANAR_REFLECTION_MAX layers */
    WGPUTextureView all;            /**< 2D array view of every layer and mip, sampled */
    WGPUTextureView mips[BOX_PLANAR_REFLECTION_MAX][BOX_GPU_PLANAR_MIPS]; /**< 2D view of
                                     *   each mip of each layer */
} box_gpu_planar_array_t;

/** A planar reflection of a view of the target being rendered */
typedef struct box_gpu_planar_pass_t {
    int32_t view_slot;              /**< Slot of its view in the view buffer */
    int32_t record;                 /**< Index of its box_render_planar_t in the packet */
    int32_t mirror;                 /**< Slot of the plane in the view */
    int32_t slot;                   /**< Slot its meshes draw with; the next one draws its
                                     *   background and particles */
    uint32_t width;                 /**< Width of its image */
    uint32_t height;                /**< Height of its image */
    bool clips;                     /**< The plane crosses the frustum, so it draws */
    box_gpu_view_uniforms_t uniforms; /**< Uniforms of the slot its meshes draw with */
    mat4 view_matrix;               /**< View matrix of the mirrored camera */
    mat4 sky_inv_view_proj;         /**< Inverse view projection of the mirrored camera without
                                     *   the oblique near plane, whose rays the sky casts */
    uint32_t array_width;           /**< Width of the reflection array of its view */
    uint32_t array_height;          /**< Height of the reflection array of its view */
    uint32_t mip_count;             /**< Mips of the reflection array of its view */
    bool sampled;                   /**< The pyramid is built, so the shape of the plane samples
                                     *   the image; false until the shader compiles */
    float distortion;               /**< PlanarReflection.distortion */
    vec4 owner[3];                  /**< Transform of the instance of the shape of the plane */
    uint32_t owner_slot;            /**< Page slot plus one of that instance, 0 for none */
} box_gpu_planar_pass_t;

struct box_gpu_planar_t {
    ecs_vec_t images;               /**< box_gpu_planar_image_t* */
    ecs_vec_t arrays;               /**< box_gpu_planar_array_t* */
    ecs_vec_t passes;               /**< box_gpu_planar_pass_t of the target being rendered */
    WGPUBindGroupLayout layout;     /**< Source image and sampler of a pyramid pass */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the pyramid passes */
    WGPUSampler sampler;            /**< Linear clamp sampler of the downsample */
    WGPURenderPipeline copy;        /**< Pipeline that writes mip 0, NULL until the shader exists */
    WGPURenderPipeline down;        /**< Pipeline that writes the mips above */
    int32_t revision;               /**< Revision of the shader of the pipelines */
    WGPUTexture none;               /**< 1x1 transparent array of the views without reflections */
    WGPUTextureView none_view;      /**< 2D array view of it */
};

void box_gpu_planar_init(
    box_gpu_t *gpu)
{
    box_gpu_planar_t *result = ecs_os_calloc_t(box_gpu_planar_t);
    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    ecs_vec_init_t(NULL, &result->images, box_gpu_planar_image_t*, 0);
    ecs_vec_init_t(NULL, &result->arrays, box_gpu_planar_array_t*, 0);
    ecs_vec_init_t(NULL, &result->passes, box_gpu_planar_pass_t, 0);
    result->layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = entries });
    result->pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){ .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &result->layout });
    result->sampler = wgpuDeviceCreateSampler(gpu->device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    });
    result->none = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, BOX_PLANAR_REFLECTION_MAX },
        .format = BOX_GPU_HDR_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    result->none_view = wgpuTextureCreateView(result->none, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_HDR_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = BOX_PLANAR_REFLECTION_MAX,
        .aspect = WGPUTextureAspect_All
    });
    gpu->planar = result;
}

/** Release the textures of a reflection array.
 *
 * @param a The array
 */
static void box_gpu_planar_array_release(
    box_gpu_planar_array_t *a)
{
    int32_t k;
    uint32_t m;
    for (k = 0; k < BOX_PLANAR_REFLECTION_MAX; k ++) {
        for (m = 0; m < BOX_GPU_PLANAR_MIPS; m ++) {
            if (a->mips[k][m]) {
                wgpuTextureViewRelease(a->mips[k][m]);
                a->mips[k][m] = NULL;
            }
        }
    }

    if (a->all) {
        wgpuTextureViewRelease(a->all);
        a->all = NULL;
    }

    if (a->texture) {
        wgpuTextureRelease(a->texture);
        a->texture = NULL;
    }
}

/** Release the pyramid pipelines.
 *
 * @param p The planar reflection state
 */
static void box_gpu_planar_pipelines_free(
    box_gpu_planar_t *p)
{
    if (p->copy) {
        wgpuRenderPipelineRelease(p->copy);
        p->copy = NULL;
    }

    if (p->down) {
        wgpuRenderPipelineRelease(p->down);
        p->down = NULL;
    }
}

/** Create a pyramid pipeline of the planar shader into the HDR format.
 *
 * @param gpu The backend
 * @param module The shader module
 * @param entry Its fragment entry point
 * @return The pipeline, NULL while it is being created
 */
static WGPURenderPipeline box_gpu_planar_pipeline_new(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    const char *entry)
{
    WGPUColorTargetState target = {
        .format = BOX_GPU_HDR_FORMAT,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = entry, .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };

    return box_gpu_render_pipeline(gpu, &(WGPURenderPipelineDescriptor){
        .layout = gpu->planar->pipeline_layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN }
        },
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    });
}

/** Get whether the pyramid pipelines exist, created on first use and again when the shader was
 * recompiled.
 *
 * @param gpu The backend
 * @return Whether both pipelines exist
 */
static bool box_gpu_planar_pipelines(
    box_gpu_t *gpu)
{
    box_gpu_planar_t *p = gpu->planar;
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_PLANAR);
    bool result = false;
    if (!shader || !shader->module) {
        goto done;
    }

    if (p->revision != shader->revision) {
        box_gpu_planar_pipelines_free(p);
        p->revision = shader->revision;
    }

    if (!p->copy) {
        p->copy = box_gpu_planar_pipeline_new(gpu, shader->module, "fs_copy");
    }

    if (!p->down) {
        p->down = box_gpu_planar_pipeline_new(gpu, shader->module, "fs_down");
    }

    result = p->copy && p->down;
done:
    return result;
}

/** Release the textures of a reflection image.
 *
 * @param img The image
 */
static void box_gpu_planar_image_release(
    box_gpu_planar_image_t *img)
{
    if (img->color_view) {
        wgpuTextureViewRelease(img->color_view);
        img->color_view = NULL;
    }

    if (img->color) {
        wgpuTextureRelease(img->color);
        img->color = NULL;
    }

    if (img->depth_view) {
        wgpuTextureViewRelease(img->depth_view);
        img->depth_view = NULL;
    }

    if (img->depth) {
        wgpuTextureRelease(img->depth);
        img->depth = NULL;
    }
}

void box_gpu_planar_fini(
    box_gpu_t *gpu)
{
    box_gpu_planar_t *p = gpu->planar;
    int32_t i;
    if (!p) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&p->images); i ++) {
        box_gpu_planar_image_t *img = *ecs_vec_get_t(&p->images, box_gpu_planar_image_t*, i);
        box_gpu_planar_image_release(img);
        ecs_os_free(img);
    }

    for (i = 0; i < ecs_vec_count(&p->arrays); i ++) {
        box_gpu_planar_array_t *a = *ecs_vec_get_t(&p->arrays, box_gpu_planar_array_t*, i);
        box_gpu_planar_array_release(a);
        ecs_os_free(a);
    }

    box_gpu_planar_pipelines_free(p);
    wgpuPipelineLayoutRelease(p->pipeline_layout);
    wgpuBindGroupLayoutRelease(p->layout);
    wgpuSamplerRelease(p->sampler);
    wgpuTextureViewRelease(p->none_view);
    wgpuTextureRelease(p->none);
    ecs_vec_fini_t(NULL, &p->images, box_gpu_planar_image_t*);
    ecs_vec_fini_t(NULL, &p->arrays, box_gpu_planar_array_t*);
    ecs_vec_fini_t(NULL, &p->passes, box_gpu_planar_pass_t);
    ecs_os_free(p);
    gpu->planar = NULL;
done:
    return;
}

void box_gpu_planar_begin(
    box_gpu_t *gpu)
{
    if (gpu->planar) {
        ecs_vec_clear(&gpu->planar->passes);
    }
}

/** Get the size of a reflection image: the render size of the target times the resolution of
 * the plane, at least one pixel.
 *
 * @param size Render width or height of the target
 * @param resolution Resolution of the plane
 * @return The size in pixels
 */
static uint32_t box_gpu_planar_size(
    int32_t size,
    float resolution)
{
    float scaled = roundf((float)(size > 0 ? size : 1) * resolution);

    return scaled < 1.0f ? 1u : (uint32_t)scaled;
}

/** Count the mips of a reflection array: the full chain of its size minus one, at least one
 * and at most BOX_GPU_PLANAR_MIPS.
 *
 * @param width Width of mip 0
 * @param height Height of mip 0
 * @return The mips
 */
static uint32_t box_gpu_planar_mips(
    uint32_t width,
    uint32_t height)
{
    uint32_t edge = width > height ? width : height, chain = 1, result;
    while (edge > 1) {
        edge >>= 1;
        chain ++;
    }

    result = chain > 1 ? chain - 1 : 1;

    return result > BOX_GPU_PLANAR_MIPS ? BOX_GPU_PLANAR_MIPS : result;
}

int32_t box_gpu_planar_add(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    int32_t index,
    int32_t slot,
    const box_gpu_view_uniforms_t *u,
    int32_t first)
{
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    int32_t result = 0, k, i, j, start = gpu->planar ? ecs_vec_count(&gpu->planar->passes) : 0;
    uint32_t array_width = 1, array_height = 1, mips;
    bool sampled;
    mat4 proj;
    if (!gpu->planar || v->planar_count <= 0 ||
        v->planar_first + v->planar_count > ecs_vec_count(&packet->planar_reflections))
    {
        goto done;
    }

    box_camera_projection(&v->camera_data,
        (float)t->desc.point_width, (float)t->desc.point_height, proj);
    for (k = 0; k < v->planar_count; k ++) {
        const box_render_planar_t *rec = ecs_vec_get_t(&packet->planar_reflections,
            box_render_planar_t, v->planar_first + k);
        box_gpu_planar_pass_t *pass = ecs_vec_append_t(
            NULL, &gpu->planar->passes, box_gpu_planar_pass_t);
        box_gpu_view_uniforms_t *r = &pass->uniforms;
        mat4 inv, oblique;
        vec4 clip;
        ecs_os_memset_t(pass, 0, box_gpu_planar_pass_t);
        pass->view_slot = slot;
        pass->record = v->planar_first + k;
        pass->mirror = rec->slot;
        pass->slot = first + result;
        pass->width = box_gpu_planar_size(t->desc.render_width, rec->resolution);
        pass->height = box_gpu_planar_size(t->desc.render_height, rec->resolution);
        pass->distortion = rec->distortion;
        pass->owner_slot = rec->exclude_page ? rec->exclude_slot + 1u : 0u;
        for (i = 0; i < 3; i ++) {
            glm_vec4_copy(ECS_CONST_CAST(float*, rec->owner[i]), pass->owner[i]);
        }

        array_width = pass->width > array_width ? pass->width : array_width;
        array_height = pass->height > array_height ? pass->height : array_height;
        glm_mat4_copy(ECS_CONST_CAST(vec4*, rec->view_matrix), pass->view_matrix);

        glm_mat4_inv(pass->view_matrix, inv);
        for (j = 0; j < 4; j ++) {
            clip[j] = 0;
            for (i = 0; i < 4; i ++) {
                clip[j] += inv[j][i] * rec->clip[i];
            }
        }

        pass->clips = box_render_planar_oblique(proj, clip, oblique);
        *r = *u;
        glm_mat4_mul(oblique, pass->view_matrix, r->view_proj);
        glm_mat4_inv(r->view_proj, r->inv_view_proj);
        box_gpu_view_relative(gpu->render_delta, r);
        glm_mat4_mul(proj, pass->view_matrix, inv);
        glm_mat4_inv(inv, pass->sky_inv_view_proj);
        glm_vec3_copy(ECS_CONST_CAST(float*, rec->camera), r->camera);
        r->camera[3] = 1;
        r->viewport[0] = (float)pass->width;
        r->viewport[1] = (float)pass->height;
        r->viewport[2] = 1.0f / (float)pass->width;
        r->viewport[3] = 1.0f / (float)pass->height;
        glm_vec4_zero(r->aerial);
        glm_vec4_zero(r->fog_grid);
        glm_mat4_copy(r->view_proj, r->prev_view_proj);
        ecs_os_memset_n(r->temporal, 0, uint32_t, 4);
        box_gpu_cull_reflect_add(gpu, packet, index, pass->slot, r->view_proj, rec->camera,
            oblique[1][1] * (float)pass->height * 0.5f, rec->exclude_page, rec->exclude_slot);
        result += 2;
    }

    mips = box_gpu_planar_mips(array_width, array_height);
    sampled = box_gpu_planar_pipelines(gpu);
    for (k = start; k < ecs_vec_count(&gpu->planar->passes); k ++) {
        box_gpu_planar_pass_t *pass = ecs_vec_get_t(
            &gpu->planar->passes, box_gpu_planar_pass_t, k);
        pass->array_width = array_width;
        pass->array_height = array_height;
        pass->mip_count = mips;
        pass->sampled = sampled && pass->mirror >= 0 &&
            pass->mirror < BOX_PLANAR_REFLECTION_MAX;
    }

done:
    return result;
}

void box_gpu_planar_uniforms(
    box_gpu_t *gpu,
    uint8_t *data)
{
    int32_t i;
    for (i = 0; gpu->planar && i < ecs_vec_count(&gpu->planar->passes); i ++) {
        const box_gpu_planar_pass_t *pass = ecs_vec_get_t(
            &gpu->planar->passes, box_gpu_planar_pass_t, i);
        box_gpu_view_uniforms_t *lit = (box_gpu_view_uniforms_t*)
            (data + (size_t)pass->slot * BOX_GPU_VIEW_STRIDE);
        box_gpu_view_uniforms_t *mirror = (box_gpu_view_uniforms_t*)
            (data + (size_t)(pass->slot + 1) * BOX_GPU_VIEW_STRIDE);
        *lit = pass->uniforms;
        *mirror = pass->uniforms;
        glm_mat4_copy(ECS_CONST_CAST(vec4*, pass->view_matrix), mirror->view);
    }

    for (i = 0; gpu->planar && i < ecs_vec_count(&gpu->planar->passes); i ++) {
        const box_gpu_planar_pass_t *pass = ecs_vec_get_t(
            &gpu->planar->passes, box_gpu_planar_pass_t, i);
        box_gpu_view_uniforms_t *owner = (box_gpu_view_uniforms_t*)
            (data + (size_t)pass->view_slot * BOX_GPU_VIEW_STRIDE);
        int32_t k = pass->mirror, j;
        if (!pass->sampled) {
            continue;
        }

        owner->planar[k][0] = pass->distortion;
        owner->planar[k][1] = (float)pass->width / (float)pass->array_width;
        owner->planar[k][2] = (float)pass->height / (float)pass->array_height;
        owner->planar[k][3] = (float)pass->mip_count;
        for (j = 0; j < 3; j ++) {
            glm_vec4_copy(ECS_CONST_CAST(float*, pass->owner[j]), owner->planar_owner[k * 3 + j]);
        }

        owner->planar_slots[k] = pass->owner_slot;
    }
}

/** Get the images of a planar reflection of a view in a target, created or resized.
 *
 * @param gpu The backend
 * @param t The target
 * @param view The view entity
 * @param slot Slot of the plane in the view
 * @param width Width of the image
 * @param height Height of the image
 * @return The images
 */
static box_gpu_planar_image_t* box_gpu_planar_image(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    ecs_entity_t view,
    int32_t slot,
    uint32_t width,
    uint32_t height)
{
    box_gpu_planar_t *p = gpu->planar;
    box_gpu_planar_image_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&p->images) && !result; i ++) {
        box_gpu_planar_image_t *img = *ecs_vec_get_t(&p->images, box_gpu_planar_image_t*, i);
        if (img->target == t->handle && img->view == view && img->slot == slot) {
            result = img;
        }
    }

    if (!result) {
        result = ecs_os_calloc_t(box_gpu_planar_image_t);
        result->target = t->handle;
        result->view = view;
        result->slot = slot;
        *ecs_vec_append_t(NULL, &p->images, box_gpu_planar_image_t*) = result;
    }

    if (result->color && result->width == width && result->height == height) {
        goto done;
    }

    box_gpu_planar_image_release(result);
    result->width = width;
    result->height = height;
    result->color = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = BOX_GPU_HDR_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    result->color_view = wgpuTextureCreateView(result->color, NULL);
    result->depth = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = BOX_GPU_DEPTH_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    result->depth_view = wgpuTextureCreateView(result->depth, NULL);
done:
    return result;
}

/** Get the reflection array of a view in a target, created or resized.
 *
 * @param gpu The backend
 * @param t The target
 * @param view The view entity
 * @param width Width of mip 0
 * @param height Height of mip 0
 * @param mips Mips
 * @return The array
 */
static box_gpu_planar_array_t* box_gpu_planar_array(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    ecs_entity_t view,
    uint32_t width,
    uint32_t height,
    uint32_t mips)
{
    box_gpu_planar_t *p = gpu->planar;
    box_gpu_planar_array_t *result = NULL;
    int32_t i, k;
    uint32_t m;
    for (i = 0; i < ecs_vec_count(&p->arrays) && !result; i ++) {
        box_gpu_planar_array_t *a = *ecs_vec_get_t(&p->arrays, box_gpu_planar_array_t*, i);
        if (a->target == t->handle && a->view == view) {
            result = a;
        }
    }

    if (!result) {
        result = ecs_os_calloc_t(box_gpu_planar_array_t);
        result->target = t->handle;
        result->view = view;
        *ecs_vec_append_t(NULL, &p->arrays, box_gpu_planar_array_t*) = result;
    }

    if (result->texture && result->width == width && result->height == height &&
        result->mip_count == mips)
    {
        goto done;
    }

    box_gpu_planar_array_release(result);
    result->width = width;
    result->height = height;
    result->mip_count = mips;
    result->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, BOX_PLANAR_REFLECTION_MAX },
        .format = BOX_GPU_HDR_FORMAT,
        .mipLevelCount = mips,
        .sampleCount = 1
    });
    result->all = wgpuTextureCreateView(result->texture, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_HDR_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = mips,
        .baseArrayLayer = 0,
        .arrayLayerCount = BOX_PLANAR_REFLECTION_MAX,
        .aspect = WGPUTextureAspect_All
    });
    for (k = 0; k < BOX_PLANAR_REFLECTION_MAX; k ++) {
        for (m = 0; m < mips; m ++) {
            result->mips[k][m] = wgpuTextureCreateView(result->texture,
                &(WGPUTextureViewDescriptor){
                    .format = BOX_GPU_HDR_FORMAT,
                    .dimension = WGPUTextureViewDimension_2D,
                    .baseMipLevel = m,
                    .mipLevelCount = 1,
                    .baseArrayLayer = (uint32_t)k,
                    .arrayLayerCount = 1,
                    .aspect = WGPUTextureAspect_All
                });
        }
    }

done:
    return result;
}

/** Record a pass of the pyramid of a reflection: one draw into the corner of a mip of its layer
 * from a source image, the rest of the mip cleared to transparent.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param pipeline The pipeline, copy or down
 * @param target The mip of the layer
 * @param source The image it reads
 * @param width Width of the corner
 * @param height Height of the corner
 */
static void box_gpu_planar_pyramid_pass(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUTextureView target,
    WGPUTextureView source,
    uint32_t width,
    uint32_t height)
{
    box_gpu_planar_t *p = gpu->planar;
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .textureView = source },
        { .binding = 1, .sampler = p->sampler }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = p->layout,
        .entryCount = 2,
        .entries = entries
    });
    WGPURenderPassColorAttachment attachment = {
        .view = target,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = { 0, 0, 0, 0 }
    };
    WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(encoder,
        &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment
        });
    wgpuRenderPassEncoderSetViewport(rp, 0, 0, (float)width, (float)height, 0, 1);
    wgpuRenderPassEncoderSetScissorRect(rp, 0, 0, width, height);
    wgpuRenderPassEncoderSetPipeline(rp, pipeline);
    wgpuRenderPassEncoderSetBindGroup(rp, 0, group, 0, NULL);
    wgpuRenderPassEncoderDraw(rp, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);
    wgpuBindGroupRelease(group);
}

/** Record the pyramid of a reflection: its image copied into mip 0 of its layer of the array,
 * its colors multiplied by its coverage, then every mip above downsampled from the one below,
 * each into the corner its image covers.
 *
 * @param gpu The backend
 * @param encoder The encoder of the frame
 * @param a The reflection array of the view
 * @param img The image of the reflection
 * @param layer Slot of the plane in the view, its layer
 */
static void box_gpu_planar_pyramid(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_gpu_planar_array_t *a,
    const box_gpu_planar_image_t *img,
    int32_t layer)
{
    box_gpu_planar_t *p = gpu->planar;
    uint32_t m, width = img->width, height = img->height;
    box_gpu_planar_pyramid_pass(gpu, encoder, p->copy, a->mips[layer][0], img->color_view,
        width, height);
    for (m = 1; m < a->mip_count; m ++) {
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
        box_gpu_planar_pyramid_pass(gpu, encoder, p->down, a->mips[layer][m],
            a->mips[layer][m - 1], width, height);
    }
}

WGPUTextureView box_gpu_planar_view(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_view_t *v)
{
    box_gpu_planar_t *p = gpu->planar;
    WGPUTextureView result = p ? p->none_view : NULL;
    int32_t i;
    for (i = 0; p && t && v && i < ecs_vec_count(&p->arrays); i ++) {
        const box_gpu_planar_array_t *a = *ecs_vec_get_t(
            &p->arrays, box_gpu_planar_array_t*, i);
        if (a->target == t->handle && a->view == v->view && a->all) {
            result = a->all;
            break;
        }
    }

    return result;
}

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static double box_gpu_planar_linear(
    uint8_t value)
{
    double c = (double)value / 255.0;

    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/** Record the sky and the particles of a reflection, after its opaque meshes, then bind the
 * groups of its meshes again.
 *
 * @param gpu The backend
 * @param t The target
 * @param rp The render pass of the reflection
 * @param v The view
 * @param pass The reflection
 * @param mirror Slot of the plane in the view
 * @param group The bind group of the view
 */
static void box_gpu_planar_sky(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPURenderPassEncoder rp,
    const box_render_view_t *v,
    const box_gpu_planar_pass_t *pass,
    int32_t mirror,
    WGPUBindGroup group)
{
    uint32_t lit = (uint32_t)(pass->slot * BOX_GPU_VIEW_STRIDE);
    uint32_t mirrored = (uint32_t)((pass->slot + 1) * BOX_GPU_VIEW_STRIDE);
    mat4 inv_view_proj;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, pass->sky_inv_view_proj), inv_view_proj);
    box_gpu_atmosphere_draw_sky_mirrored(gpu, t, rp, BOX_GPU_HDR_FORMAT, v, mirror,
        inv_view_proj);
    wgpuRenderPassEncoderSetBindGroup(rp, 0, group, 1, &mirrored);
    wgpuRenderPassEncoderSetBindGroup(rp, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    box_gpu_particles_draw(gpu, rp, BOX_GPU_HDR_FORMAT, v, NULL);
    wgpuRenderPassEncoderSetBindGroup(rp, 0, group, 1, &lit);
    wgpuRenderPassEncoderSetBindGroup(rp, 3, box_gpu_volume_group(gpu, v), 0, NULL);
}

/** Record the draws of a reflection: its background, its 3D meshes in the order of its cull
 * view, the opaque ones first, then its sky and particles before the first blended draw.
 *
 * @param gpu The backend
 * @param t The target
 * @param rp The render pass of the reflection
 * @param packet The packet
 * @param v The view
 * @param pass The reflection
 * @param mirror Slot of the plane in the view
 * @param group The bind group of the view
 */
static void box_gpu_planar_draws(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPURenderPassEncoder rp,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    const box_gpu_planar_pass_t *pass,
    int32_t mirror,
    WGPUBindGroup group)
{
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, pass->slot, -1);
    WGPURenderPipeline bound = NULL;
    WGPUBindGroup material = NULL, instances = NULL;
    uint32_t material_kind = 0;
    uint32_t lit = (uint32_t)(pass->slot * BOX_GPU_VIEW_STRIDE);
    uint32_t mirrored = (uint32_t)((pass->slot + 1) * BOX_GPU_VIEW_STRIDE);
    bool sky = false;
    int32_t i;
    box_gpu_environment_draw_background(gpu, rp, BOX_GPU_HDR_FORMAT, v, mirrored);
    wgpuRenderPassEncoderSetBindGroup(rp, 0, group, 1, &lit);
    wgpuRenderPassEncoderSetBindGroup(rp, 3, box_gpu_volume_group(gpu, v), 0, NULL);
    for (i = 0; cv && i < cv->draw_count; i ++) {
        const box_gpu_cull_draw_t *d = ecs_vec_get_t(
            &gpu->cull.draws, box_gpu_cull_draw_t, cv->first_draw + i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, d->batch);
        if (!sky && b->view != -1) {
            sky = true;
            box_gpu_planar_sky(gpu, t, rp, v, pass, mirror, group);
            bound = NULL;
            material = NULL;
            instances = NULL;
        }

        WGPURenderPipeline pipeline = box_gpu_forward_pipeline_mirrored(
            gpu, BOX_GPU_HDR_FORMAT, b, false, true);
        if (!pipeline || d->skip) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(rp, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup layers = box_gpu_forward_group(gpu, packet, b);
        uint32_t kind = box_gpu_draw_kind(b, false);
        if (layers != material || kind != material_kind) {
            wgpuRenderPassEncoderSetBindGroup(rp, 1, layers, 1, &kind);
            material = layers;
            material_kind = kind;
        }

        box_gpu_cull_draw(gpu, rp, packet, d, 2, false, true, &instances);
    }

    if (!sky) {
        box_gpu_planar_sky(gpu, t, rp, v, pass, mirror, group);
    }
}

void box_gpu_planar_render(
    box_gpu_t *gpu,
    box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t slot)
{
    box_gpu_planar_t *p = gpu->planar;
    WGPUBindGroup group = NULL;
    box_gpu_planar_array_t *array = NULL;
    int32_t i;
    for (i = 0; p && i < ecs_vec_count(&p->passes); i ++) {
        const box_gpu_planar_pass_t *pass = ecs_vec_get_t(
            &p->passes, box_gpu_planar_pass_t, i);
        if (pass->view_slot != slot) {
            continue;
        }

        const box_render_planar_t *rec = ecs_vec_get_t(&packet->planar_reflections,
            box_render_planar_t, pass->record);
        box_gpu_planar_image_t *img = box_gpu_planar_image(gpu, t, v->view, rec->slot,
            pass->width, pass->height);
        img->frame = packet->frame;
        if (!group) {
            group = box_gpu_light_group(gpu, t, v, slot, NULL, NULL);
        }

        box_gpu_cull_record(gpu, encoder, pass->slot);
        WGPURenderPassColorAttachment attachment = {
            .view = img->color_view,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = { 0, 0, 0, 0 }
        };
        if (v->clear.enabled) {
            attachment.clearValue = (WGPUColor){
                box_gpu_planar_linear(v->clear.color.r),
                box_gpu_planar_linear(v->clear.color.g),
                box_gpu_planar_linear(v->clear.color.b),
                0.0
            };
        }

        WGPURenderPassDepthStencilAttachment depth = {
            .view = img->depth_view,
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Discard,
            .depthClearValue = 0.0f,
            .stencilLoadOp = WGPULoadOp_Clear,
            .stencilStoreOp = WGPUStoreOp_Discard
        };
        WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(encoder,
            &(WGPURenderPassDescriptor){
                .colorAttachmentCount = 1,
                .colorAttachments = &attachment,
                .depthStencilAttachment = &depth
            });
        if (pass->clips) {
            box_gpu_planar_draws(gpu, t, rp, packet, v, pass, rec->slot, group);
        }

        wgpuRenderPassEncoderEnd(rp);
        wgpuRenderPassEncoderRelease(rp);
        if (!pass->sampled || !p->copy || !p->down) {
            continue;
        }

        if (!array) {
            array = box_gpu_planar_array(gpu, t, v->view, pass->array_width,
                pass->array_height, pass->mip_count);
            array->frame = packet->frame;
        }

        box_gpu_planar_pyramid(gpu, encoder, array, img, pass->mirror);
    }

    if (group) {
        wgpuBindGroupRelease(group);
    }
}

void box_gpu_planar_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_planar_t *p = gpu->planar;
    int32_t i;
    for (i = p ? ecs_vec_count(&p->images) - 1 : -1; i >= 0; i --) {
        box_gpu_planar_image_t *img = *ecs_vec_get_t(&p->images, box_gpu_planar_image_t*, i);
        if (img->frame != packet->frame) {
            box_gpu_planar_image_release(img);
            ecs_os_free(img);
            ecs_vec_remove_t(&p->images, box_gpu_planar_image_t*, i);
        }
    }

    for (i = p ? ecs_vec_count(&p->arrays) - 1 : -1; i >= 0; i --) {
        box_gpu_planar_array_t *a = *ecs_vec_get_t(&p->arrays, box_gpu_planar_array_t*, i);
        if (a->frame != packet->frame) {
            box_gpu_planar_array_release(a);
            ecs_os_free(a);
            ecs_vec_remove_t(&p->arrays, box_gpu_planar_array_t*, i);
        }
    }
}

/** Convert a half float to a float.
 *
 * @param h The bits of the half
 * @return The value
 */
static float box_gpu_planar_half(
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

bool box_render_gpu_read_planar(
    ecs_world_t *world,
    ecs_entity_t view,
    int32_t slot,
    int32_t *width,
    int32_t *height,
    float *out,
    int32_t count)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    uint16_t *texels = NULL;
    const box_gpu_planar_image_t *img = NULL;
    int32_t i;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_read_planar needs RenderSettings.main_thread and box_render_sync");
    for (i = 0; renders && gpu->planar && i < ecs_vec_count(&gpu->planar->images) && !img;
        i ++)
    {
        const box_gpu_planar_image_t *candidate = *ecs_vec_get_t(
            &gpu->planar->images, box_gpu_planar_image_t*, i);
        if (candidate->view == view && candidate->slot == slot) {
            img = candidate;
        }
    }

    if (!img) {
        goto done;
    }

    if (width) {
        *width = (int32_t)img->width;
    }

    if (height) {
        *height = (int32_t)img->height;
    }

    if (!out) {
        result = true;
        goto done;
    }

    if ((int64_t)img->width * img->height * 4 > count) {
        goto done;
    }

    uint32_t x, y, w = img->width, h = img->height, row = w * 8;
    uint32_t stride = (row + BOX_GPU_ROW_ALIGN - 1) / BOX_GPU_ROW_ALIGN * BOX_GPU_ROW_ALIGN;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)stride * h
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = img->color,
            .aspect = WGPUTextureAspect_All
        },
        &(WGPUTexelCopyBufferInfo){
            .layout = { .bytesPerRow = stride, .rowsPerImage = h },
            .buffer = buffer
        },
        &(WGPUExtent3D){ w, h, 1 });
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    texels = ecs_os_malloc((ecs_size_t)(stride * h));
    result = box_gpu_read(gpu, buffer, 0, stride * h, texels);
    wgpuBufferRelease(buffer);
    for (y = 0; result && y < h; y ++) {
        const uint16_t *line = texels + (size_t)y * (stride / 2);
        for (x = 0; x < w * 4; x ++) {
            out[(size_t)y * w * 4 + x] = box_gpu_planar_half(line[x]);
        }
    }

done:
    ecs_os_free(texels);

    return result;
}

#else

bool box_render_gpu_read_planar(
    ecs_world_t *world,
    ecs_entity_t view,
    int32_t slot,
    int32_t *width,
    int32_t *height,
    float *out,
    int32_t count)
{
    (void)world;
    (void)view;
    (void)slot;
    (void)width;
    (void)height;
    (void)out;
    (void)count;

    return false;
}

#endif
