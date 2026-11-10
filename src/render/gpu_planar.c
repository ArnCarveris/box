/**
 * @file render/gpu_planar.c
 * @brief Planar reflections of the webgpu backend: the mirrored image of the scene a view
 * renders about each of its reflection planes, before its forward pass.
 *
 * A reflection draws with the projection of its view, its near plane made oblique to the plane
 * (box_render_planar_oblique), and the view matrix of the view times the reflection; its front
 * faces wind clockwise. It is lit like the view and culled by a frustum of its own.
 *
 * The images live per target, view and plane slot, sized by the render size of the target and
 * the resolution of the plane, and are released once no packet renders them.
 */

#include "gpu_environment.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

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

/** A planar reflection of a view of the target being rendered */
typedef struct box_gpu_planar_pass_t {
    int32_t view_slot;              /**< Slot of its view in the view buffer */
    int32_t record;                 /**< Index of its box_render_planar_t in the packet */
    int32_t slot;                   /**< Slot its meshes draw with; the next one draws its
                                     *   background and particles */
    uint32_t width;                 /**< Width of its image */
    uint32_t height;                /**< Height of its image */
    bool clips;                     /**< The plane crosses the frustum, so it draws */
    box_gpu_view_uniforms_t uniforms; /**< Uniforms of the slot its meshes draw with */
    mat4 view_matrix;               /**< View matrix of the mirrored camera */
} box_gpu_planar_pass_t;

struct box_gpu_planar_t {
    ecs_vec_t images;               /**< box_gpu_planar_image_t* */
    ecs_vec_t passes;               /**< box_gpu_planar_pass_t of the target being rendered */
};

void box_gpu_planar_init(
    box_gpu_t *gpu)
{
    box_gpu_planar_t *result = ecs_os_calloc_t(box_gpu_planar_t);
    ecs_vec_init_t(NULL, &result->images, box_gpu_planar_image_t*, 0);
    ecs_vec_init_t(NULL, &result->passes, box_gpu_planar_pass_t, 0);
    gpu->planar = result;
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

    ecs_vec_fini_t(NULL, &p->images, box_gpu_planar_image_t*);
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
    int32_t result = 0, k, i, j;
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
        pass->slot = first + result;
        pass->width = box_gpu_planar_size(t->desc.render_width, rec->resolution);
        pass->height = box_gpu_planar_size(t->desc.render_height, rec->resolution);
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
        glm_vec3_copy(ECS_CONST_CAST(float*, rec->camera), r->camera);
        r->camera[3] = 1;
        r->viewport[0] = (float)pass->width;
        r->viewport[1] = (float)pass->height;
        r->viewport[2] = 1.0f / (float)pass->width;
        r->viewport[3] = 1.0f / (float)pass->height;
        glm_vec4_zero(r->aerial);
        box_gpu_cull_reflect_add(gpu, packet, index, pass->slot, r->view_proj,
            oblique[1][1] * (float)pass->height * 0.5f, rec->exclude_page, rec->exclude_slot);
        result += 2;
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
    glm_mat4_copy(ECS_CONST_CAST(vec4*, pass->uniforms.inv_view_proj), inv_view_proj);
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
        if (!pipeline) {
            continue;
        }

        if (pipeline != bound) {
            wgpuRenderPassEncoderSetPipeline(rp, pipeline);
            bound = pipeline;
        }

        WGPUBindGroup layers = box_gpu_material_group(gpu, packet, b);
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
                (double)v->clear.color.a / 255.0
            };
        }

        WGPURenderPassDepthStencilAttachment depth = {
            .view = img->depth_view,
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Discard,
            .depthClearValue = 0.0f
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
