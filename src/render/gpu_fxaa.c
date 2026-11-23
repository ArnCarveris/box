/**
 * @file render/gpu_fxaa.c
 * @brief The FXAA and chromatic aberration passes of the effect chains of the webgpu backend.
 */

#include "gpu_effects.h"

#ifdef BOX_RENDER_WGPU

bool box_gpu_fx_fxaa(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    const Fxaa *a = &e->fxaa;
    box_gpu_fxaa_uniforms_t u;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    ecs_os_memset_t(&u, 0, box_gpu_fxaa_uniforms_t);
    u.params[0] = a->threshold;
    u.params[1] = a->threshold_min;
    u.params[2] = a->subpixel;
    u.texel[0] = 1.0f / (float)glm_imax(ctx->ft->width, 1);
    u.texel[1] = 1.0f / (float)glm_imax(ctx->ft->height, 1);
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));

    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_FXAA,
        .entry = "fs",
        .input = ctx->input,
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
}

bool box_gpu_fx_chromatic_aberration(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    const ChromaticAberration *ca = &e->chromatic_aberration;
    box_gpu_chromatic_aberration_uniforms_t u;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    float w = (float)glm_imax(ctx->ft->width, 1), h = (float)glm_imax(ctx->ft->height, 1);
    ecs_os_memset_t(&u, 0, box_gpu_chromatic_aberration_uniforms_t);
    u.params[0] = ca->intensity * w;
    u.params[1] = (float)ca->samples;
    u.params[2] = ca->falloff;
    u.params[3] = 1.0f / box_render_chromatic_aberration_corner(ca, w, h);
    u.shape[0] = ca->center[0] * w;
    u.shape[1] = ca->center[1] * h;
    u.shape[2] = 1.0f / w;
    u.shape[3] = 1.0f / h;
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));

    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_CHROMATIC_ABERRATION,
        .entry = "fs",
        .input = ctx->input,
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
}

#endif
