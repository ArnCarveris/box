/**
 * @file render/gpu_height_fog.c
 * @brief The height fog pass of the effect chains of the webgpu backend, and the height fog the
 * apply draw of the fog volumes applies in its place.
 */

#include "gpu_effects.h"

#ifdef BOX_RENDER_WGPU

/** Highest phase asymmetry of the glow of a fog colored by the atmosphere */
#define BOX_GPU_FOG_ANISOTROPY_MAX (0.92f)

/** Fill the uniforms of a height fog of a view, lit by the sun and ambient light of the scene of
 * the view, or colored by the sky of a view that renders the atmosphere the fog refers to: the
 * in scatter color the atmosphere wrote for the view, toward its main light.
 *
 * @param gpu The backend
 * @param t The target, whose size the projection follows
 * @param packet The packet
 * @param v The view
 * @param f The fog
 * @param u Receives the uniforms
 * @return The color of the sky the fog takes, NULL for none
 */
static WGPUTextureView box_gpu_fx_fog_uniforms(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    const HeightFog *f,
    box_gpu_height_fog_t *u)
{
    const box_render_view_lights_t *l = &v->lights;
    mat4 proj, view_proj;
    int32_t c;
    ecs_os_memset_t(u, 0, box_gpu_height_fog_t);
    box_camera_projection(&v->camera_data,
        (float)t->desc.point_width, (float)t->desc.point_height, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), view_proj);
    glm_mat4_inv(view_proj, u->inv_view_proj);
    u->color[0] = box_gpu_fx_linear(f->color.r);
    u->color[1] = box_gpu_fx_linear(f->color.g);
    u->color[2] = box_gpu_fx_linear(f->color.b);
    u->color[3] = f->density;
    u->params[0] = f->falloff;
    u->params[1] = f->base_height;
    u->params[2] = f->max_opacity;
    u->params[3] = l->enabled ? 1.0f : 0.0f;
    if (l->directional && glm_vec3_norm(ECS_CONST_CAST(float*, l->sun)) > 1e-6f) {
        glm_vec3_normalize_to(ECS_CONST_CAST(float*, l->sun), u->sun);
        u->sun[3] = 1.0f;
    }

    for (c = 0; c < 3; c ++) {
        u->sun_color[c] = l->sun_color[c] / GLM_PIf * f->sun_inscatter;
        u->ambient[c] = (l->ambient_sky[c] + l->ambient_ground[c]) * 0.5f *
            f->ambient_inscatter;
    }

    WGPUTextureView sky = f->atmosphere && f->atmosphere == v->atmos.entity
        ? box_gpu_atmosphere_fog(gpu, t, packet, v)
        : NULL;
    if (sky) {
        const box_render_view_atmos_t *a = &v->atmos;
        glm_vec4_zero(u->sun);
        if (glm_vec3_norm(ECS_CONST_CAST(float*, a->fog_light)) > 1e-6f) {
            glm_vec3_normalize_to(ECS_CONST_CAST(float*, a->fog_light), u->sun);
            u->sun[3] = 1.0f;
        }

        u->atmos[0] = 1.0f;
        u->atmos[1] = glm_clamp(a->mie_anisotropy, 0.0f, BOX_GPU_FOG_ANISOTROPY_MAX);
        u->atmos[2] = f->sun_inscatter;
        u->atmos[3] = f->ambient_inscatter;
    }

    return sky;
}

bool box_gpu_fx_fog(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e)
{
    box_gpu_height_fog_t u;
    box_gpu_fx_state_t *s = box_gpu_fx_state(gpu, ctx, e->entity, (int32_t)e->kind);
    WGPUTextureView sky = box_gpu_fx_fog_uniforms(gpu, ctx->t, ctx->packet, ctx->v, &e->fog,
        &u);
    box_gpu_fx_uniforms(gpu, s, &u, sizeof(u));

    return box_gpu_fx_draw(gpu, ctx, &(box_gpu_fx_pass_t){
        .shader = BOX_RENDER_SHADER_FOG,
        .entry = "fs",
        .input = ctx->input,
        .aux = sky,
        .uniforms = s->uniforms,
        .output = ctx->output,
        .format = ctx->format,
        .blend = ctx->blend
    });
}

bool box_gpu_effects_height_fog(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    box_gpu_height_fog_t *u,
    WGPUTextureView *sky)
{
    box_gpu_effects_t *fx = gpu->effects;
    const box_render_effect_t *e = NULL;
    bool result = false;
    if (!fx || !v->effect_count || !v->clear.enabled) {
        goto done;
    }

    e = ecs_vec_get_t(&packet->effects, box_render_effect_t, v->effect_first);
    if (e->kind != BOX_RENDER_EFFECT_FOG) {
        goto done;
    }

    *sky = box_gpu_fx_fog_uniforms(gpu, t, packet, v, &e->fog, u);
    if (!*sky) {
        *sky = fx->placeholder_view;
    }

    result = true;
done:
    return result;
}

#endif
