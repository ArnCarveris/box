/**
 * @file renderer/view.c
 * @brief GPU resources of the views of a frame and of the render targets they draw into.
 *
 * Every view owns its depth target, view uniform, light and cluster buffers and
 * the copy of its opaque image, so the passes of one view never disturb another.
 */

#include "renderer_internal.h"

void box_view_group(
    box_renderer_t *r)
{
    WGPUBindGroupEntry group_entries[5] = {
        { .binding = 0, .buffer = r->v->view_buffer, .size = BOX_VIEW_UNIFORM_SIZE },
        { .binding = 1, .buffer = r->layer_buffer, .size = BOX_LAYER_UNIFORM_SIZE }
    };
    box_shadow_bind_group(r, group_entries + 2, 2);
    group_entries[4] = (WGPUBindGroupEntry){ .binding = 4,
        .buffer = r->modulator_buffer, .size = BOX_MODULATOR_UNIFORM_SIZE };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->view_layout,
        .entryCount = 5,
        .entries = group_entries
    };
    if (r->v->view_group) wgpuBindGroupRelease(r->v->view_group);
    r->v->view_group = wgpuDeviceCreateBindGroup(r->device, &group_desc);
}

/** Create the view uniform buffer and its bind group for the current view.
 *
 * @param r The renderer
 */
static void box_view_uniform(
    box_renderer_t *r)
{
    WGPUBufferDescriptor buffer_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = BOX_VIEW_UNIFORM_SIZE
    };
    r->v->view_buffer = wgpuDeviceCreateBuffer(r->device, &buffer_desc);
    box_view_group(r);
}

/** Create the depth target of the current view for its size.
 *
 * @param r The renderer
 */
static void box_view_depth(
    box_renderer_t *r)
{
    if (r->v->depth_sample) wgpuTextureViewRelease(r->v->depth_sample);
    if (r->v->depth_view) wgpuTextureViewRelease(r->v->depth_view);
    if (r->v->depth) wgpuTextureRelease(r->v->depth);
    WGPUTextureDescriptor depth_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { r->v->width, r->v->height, 1 },
        .format = WGPUTextureFormat_Depth32Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->v->depth = wgpuDeviceCreateTexture(r->device, &depth_desc);
    r->v->depth_view = wgpuTextureCreateView(r->v->depth, NULL);
    r->v->depth_sample = wgpuTextureCreateView(r->v->depth, NULL);
    if (r->v->sky_group) {
        wgpuBindGroupRelease(r->v->sky_group);
        r->v->sky_group = NULL;
    }

    box_hiz_resize(r->v);
}

void box_view_select(
    box_renderer_t *r,
    uint32_t index)
{
    while (r->views.count <= index) {
        box_gpu_view_t *created = box_vec_push(&r->views);
        memset(created, 0, sizeof(box_gpu_view_t));
    }

    r->v = box_vec_at(&r->views, index);
}

void box_view_prepare(
    box_renderer_t *r,
    const box_view_t *view)
{
    if (!r->v->view_buffer) {
        box_view_uniform(r);
        box_lights_view_init(r);
        box_vec_init(&r->v->draws, sizeof(box_cull_draw_t));
        box_vec_init(&r->v->staging, sizeof(box_instance_t));
        box_vec_init(&r->v->culls, sizeof(box_gpu_cull_t));
        box_vec_init(&r->v->shadow_draws, sizeof(box_shadow_draw_t));
        box_vec_init(&r->v->shadow_staging, sizeof(box_instance_t));
        box_vec_init(&r->v->shadow_culls, sizeof(box_gpu_cull_t));
        box_vec_init(&r->v->hiz_meshes, sizeof(box_hiz_mesh_t));
    }

    if (r->v->width != view->width || r->v->height != view->height) {
        r->v->width = view->width;
        r->v->height = view->height;
        box_view_depth(r);
        r->v->scene_format = 0;
    }

    WGPUTextureFormat format = r->effect_scene ? BOX_EFFECT_HDR_FORMAT : r->format;
    if (r->v->scene_format != format) {
        box_transparent_resize(r, format);
    }
}

void box_view_release(
    box_gpu_view_t *v)
{
    box_transparent_view_fini(v);
    box_effect_compute_view_fini(v);
    box_effect_mip_view_fini(v);
    box_effect_temporal_view_fini(v);
    box_lights_view_fini(v);
    box_cull_view_fini(v);
    box_hiz_view_fini(v);
    box_shadow_view_fini(v);
    box_sky_view_release(v);
    box_vec_fini(&v->shadow_draws);
    box_vec_fini(&v->shadow_staging);
    box_vec_fini(&v->shadow_culls);
    if (v->env_group) wgpuBindGroupRelease(v->env_group);
    if (v->view_group) wgpuBindGroupRelease(v->view_group);
    if (v->view_buffer) wgpuBufferRelease(v->view_buffer);
    if (v->depth_sample) wgpuTextureViewRelease(v->depth_sample);
    if (v->depth_view) wgpuTextureViewRelease(v->depth_view);
    if (v->depth) wgpuTextureRelease(v->depth);
    memset(v, 0, sizeof(box_gpu_view_t));
}

void box_view_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < r->views.count; i ++) {
        box_view_release(box_vec_at(&r->views, i));
    }

    box_vec_fini(&r->views);
    r->v = NULL;
}

/** Entry of a render target handle, growing the table when the handle is new.
 *
 * @param r The renderer
 * @param handle The target handle
 * @return The entry
 */
static box_gpu_target_t* box_target_entry(
    box_renderer_t *r,
    uint32_t handle)
{
    while (r->targets.count <= handle) {
        box_gpu_target_t *created = box_vec_push(&r->targets);
        memset(created, 0, sizeof(box_gpu_target_t));
    }

    return box_vec_at(&r->targets, handle);
}

/** Release the texture of a render target.
 *
 * @param t The target entry
 */
static void box_target_release(
    box_gpu_target_t *t)
{
    if (t->composite) wgpuBindGroupRelease(t->composite);
    if (t->view) wgpuTextureViewRelease(t->view);
    if (t->color) wgpuTextureRelease(t->color);
    memset(t, 0, sizeof(box_gpu_target_t));
}

void box_target_apply(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t i;
    for (i = 0; i < p->target_destroy.count; i ++) {
        uint32_t handle = *(uint32_t*)box_vec_at(&p->target_destroy, i);
        if (handle < r->targets.count) {
            box_target_release(box_vec_at(&r->targets, handle));
        }
    }

    for (i = 0; i < p->target_create.count; i ++) {
        box_target_cmd_t *cmd = box_vec_at(&p->target_create, i);
        box_gpu_target_t *t = box_target_entry(r, cmd->handle);
        box_target_release(t);
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
                WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { cmd->width, cmd->height, 1 },
            .format = r->format,
            .mipLevelCount = 1,
            .sampleCount = 1
        };
        t->color = wgpuDeviceCreateTexture(r->device, &desc);
        t->view = wgpuTextureCreateView(t->color, NULL);
        t->width = cmd->width;
        t->height = cmd->height;
        t->alive = true;
    }
}

box_gpu_target_t* box_target_get(
    box_renderer_t *r,
    uint32_t handle)
{
    box_gpu_target_t *result = NULL;
    if (handle && handle < r->targets.count) {
        box_gpu_target_t *t = box_vec_at(&r->targets, handle);
        result = t->alive ? t : NULL;
    }

    return result;
}

void box_target_fini(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < r->targets.count; i ++) {
        box_target_release(box_vec_at(&r->targets, i));
    }

    box_vec_fini(&r->targets);
}
