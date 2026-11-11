/**
 * @file render/gpu_depth_of_field.c
 * @brief Depth of field passes of the webgpu backend: a gather bokeh at half resolution.
 *
 * Four half resolution images of the target: 0 the half image with its circle of confusion,
 * then the far field after the median; 1 the dilation along x, then the far gather; 2 the
 * dilated near CoC, then the near field after the median; 3 the near gather.
 */

#include "gpu_effects.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

void box_gpu_fx_dof_release(
    box_gpu_fx_target_t *ft)
{
    int32_t i;
    for (i = 0; i < BOX_GPU_DOF_IMAGES; i ++) {
        box_gpu_fx_release(&ft->dof[i], &ft->dof_views[i]);
    }
}

/** Create the half resolution images of the depth of field of a target on first use.
 *
 * @param gpu The backend
 * @param ft The images of the target
 */
static void box_gpu_fx_dof_images(
    box_gpu_t *gpu,
    box_gpu_fx_target_t *ft)
{
    uint32_t w = (uint32_t)(ft->width + 1) / 2, h = (uint32_t)(ft->height + 1) / 2;
    int32_t i;
    for (i = 0; i < BOX_GPU_DOF_IMAGES; i ++) {
        if (!ft->dof[i]) {
            ft->dof[i] = box_gpu_fx_texture(gpu, w, h, BOX_GPU_HDR_FORMAT, 1);
            ft->dof_views[i] = wgpuTextureCreateView(ft->dof[i], NULL);
        }
    }
}

/** Record a half resolution pass of a depth of field.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param s The state with the uniforms
 * @param entry The fragment entry point
 * @param input The image at binding 0
 * @param aux The image at binding 4, NULL for none
 * @param output The image the pass writes
 * @return Whether the pass was recorded
 */
static bool box_gpu_fx_dof_pass(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_gpu_fx_state_t *s,
    const char *entry,
    WGPUTextureView input,
    WGPUTextureView aux,
    WGPUTextureView output)
{
    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_DEPTH_OF_FIELD,
        .entry = entry,
        .input = input,
        .aux = aux,
        .uniforms = s->uniforms,
        .output = output,
        .format = BOX_GPU_HDR_FORMAT,
        .blend = BOX_GPU_FX_REPLACE
    });
}

bool box_gpu_fx_depth_of_field(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    const DepthOfField *d = &e->depth_of_field;
    box_gpu_fx_target_t *ft = ctx->ft;
    box_gpu_dof_uniforms_t u;
    WGPUTextureView *img = ft->dof_views;
    bool result = false;
    if (d->max_radius <= 0 || ft->width <= 0 || ft->height <= 0) {
        goto done;
    }

    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    ecs_os_memset_t(&u, 0, box_gpu_dof_uniforms_t);
    box_render_depth_of_field_linearize(&ctx->v->camera_data, u.depth);
    u.focus[0] = d->focus_distance;
    u.focus[1] = d->focus_range;
    u.focus[2] = d->near_transition;
    u.focus[3] = d->far_transition;
    u.params[0] = d->max_radius * (float)ft->height * 0.5f;
    u.params[1] = d->near_blur ? 1.0f : 0.0f;
    u.params[2] = box_render_depth_of_field_coc(d, INFINITY);
    if (d->mode == DofTiltShift) {
        u.tilt[0] = d->tilt_center;
        u.tilt[1] = d->tilt_band;
        u.tilt[2] = d->tilt_transition;
        u.tilt[3] = 1.0f;
    }

    u.tilt_normal[0] = sinf(d->tilt_angle);
    u.tilt_normal[1] = cosf(d->tilt_angle);
    u.tilt_normal[2] = (float)ft->width / (float)ft->height;
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));
    box_gpu_fx_dof_images(gpu, ft);
    if (!box_gpu_fx_dof_pass(gpu, ctx, s, "fs_prefilter", ctx->input, NULL, img[0])) {
        goto done;
    }

    if (d->near_blur) {
        box_gpu_fx_dof_pass(gpu, ctx, s, "fs_dilate_x", img[0], NULL, img[1]);
        box_gpu_fx_dof_pass(gpu, ctx, s, "fs_dilate_y", img[1], NULL, img[2]);
        box_gpu_fx_dof_pass(gpu, ctx, s, "fs_gather_near", img[0], img[2], img[3]);
    }

    box_gpu_fx_dof_pass(gpu, ctx, s, "fs_gather_far", img[0], NULL, img[1]);
    box_gpu_fx_dof_pass(gpu, ctx, s, "fs_post", img[1], NULL, img[0]);
    if (d->near_blur) {
        box_gpu_fx_dof_pass(gpu, ctx, s, "fs_post", img[3], NULL, img[2]);
    }

    box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_DEPTH_OF_FIELD,
        .entry = "fs_composite",
        .input = ctx->input,
        .aux = img[0],
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
    if (d->near_blur) {
        box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
            .shader = BOX_RENDER_SHADER_DEPTH_OF_FIELD,
            .entry = "fs_near",
            .input = img[2],
            .uniforms = s->uniforms,
            .output = ctx->output,
            .format = ctx->format,
            .blend = BOX_GPU_FX_OVER
        });
    }

    result = true;
done:
    return result;
}

#endif
