/**
 * @file render/gpu_display.c
 * @brief How the 2D shapes of a view show their colors: in a view whose effects scale or tone
 * map its image, a 2D color is a display value that shape2d.wgsl takes back to the HDR value the
 * exposure and the operator of the view map to it (View.display of lights.wgsl).
 *
 * The operator is known when the uniforms of a view are written; a Tony McMapface whose LUT
 * could not be loaded maps by PBR Neutral, as the tone map pass does.
 *
 * The exposure adapts on the GPU, so the scale the last frame adapted to is copied from the
 * exposure of the view into its uniforms (the second float of Exposure in tonemap.wgsl).
 */

#include "gpu_effects.h"

void box_gpu_display_uniforms(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    box_gpu_view_uniforms_t *u)
{
    box_gpu_effects_t *fx = gpu->effects;
    bool tonemapped = false;
    int32_t i;
    u->display[0] = 1.0f;
    u->display[1] = 0.0f;
    u->display[2] = 0.0f;
    u->display[3] = 0.0f;
    for (i = 0; i < v->effect_count; i ++) {
        const box_render_effect_t *e = ecs_vec_get_t(&packet->effects, box_render_effect_t,
            v->effect_first + i);
        if (e->kind == BOX_RENDER_EFFECT_EXPOSURE) {
            u->display[2] = 1.0f;
        }

        if (e->kind == BOX_RENDER_EFFECT_TONEMAP && !tonemapped) {
            bool tony = e->tonemap == TonemapOperatorTonyMcMapface &&
                !(fx && fx->tony_failed);
            u->display[1] = tony ? 2.0f : 1.0f;
            u->display[2] = 1.0f;
            tonemapped = true;
        }
    }
}

/** Get the entity with the AutoExposure whose adapted exposure scales the image of a view: that
 * of its first pass that scales.
 *
 * @param packet The packet
 * @param v The view
 * @return The entity, 0 for none
 */
static ecs_entity_t box_gpu_fx_view_exposure(
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    ecs_entity_t result = 0;
    int32_t i;
    for (i = 0; i < v->effect_count && !result; i ++) {
        const box_render_effect_t *e = ecs_vec_get_t(&packet->effects, box_render_effect_t,
            v->effect_first + i);
        if (e->kind == BOX_RENDER_EFFECT_EXPOSURE || e->kind == BOX_RENDER_EFFECT_TONEMAP) {
            result = e->exposure;
        }
    }

    return result;
}

void box_gpu_display_copy(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    WGPUCommandEncoder encoder,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    int32_t slot)
{
    box_gpu_effects_t *fx = gpu->effects;
    ecs_entity_t exposure = box_gpu_fx_view_exposure(packet, v);
    int32_t i;
    if (!fx || !exposure || !gpu->view_buffer) {
        goto done;
    }

    for (i = 0; i < ecs_vec_count(&fx->states); i ++) {
        box_gpu_fx_state_t *s = *ecs_vec_get_t(&fx->states, box_gpu_fx_state_t*, i);
        if (s->target != t->handle || s->view != v->view || s->effect != exposure ||
            s->kind != BOX_GPU_FX_MEASURE || !s->exposure)
        {
            continue;
        }

        uint64_t offset = (uint64_t)slot * BOX_GPU_VIEW_STRIDE +
            offsetof(box_gpu_view_uniforms_t, display);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, s->exposure, sizeof(float),
            gpu->view_buffer, offset, sizeof(float));
        break;
    }

done:
    return;
}
