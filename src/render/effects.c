/**
 * @file render/effects.c
 * @brief Adds the effect passes of every view to the packet, in the order of View.effects.
 *
 * Disabled entries, entries whose entity is gone and entities without effect settings add no
 * pass; a view without passes renders straight into its target, without an HDR target.
 *
 * Gtao adds no pass: the first enabled one of a lit view with a perspective camera sets the
 * ambient occlusion of the view, which runs before its forward pass.
 */

#include "render.h"

/** Shortest edge of the first bloom mip */
#define BOX_EFFECTS_BLOOM_MIP_MIN (8)

/** Highest temporal blend of bloom */
#define BOX_EFFECTS_BLOOM_TEMPORAL (0.95f)

/** Smallest brightness and log2 luminance range the exposure measures */
#define BOX_EFFECTS_EXPOSURE_EPSILON (1e-4f)

/** Range of the edge detection threshold of SMAA */
#define BOX_EFFECTS_SMAA_THRESHOLD_MIN (0.01f)
#define BOX_EFFECTS_SMAA_THRESHOLD_MAX (0.5f)

/** Most search steps of SMAA, orthogonal and diagonal */
#define BOX_EFFECTS_SMAA_STEPS_MAX (112)
#define BOX_EFFECTS_SMAA_DIAG_STEPS_MAX (20)

/** Most edge aware denoise passes of Gtao */
#define BOX_EFFECTS_GTAO_DENOISE_MAX (3)

void box_render_bloom_settings(
    const Bloom *in,
    Bloom *out)
{
    *out = *in;
    out->intensity = glm_max(in->intensity, 0.0f);
    out->low_frequency_boost = glm_max(in->low_frequency_boost, 0.0f);
    out->low_frequency_boost_curvature = glm_clamp(in->low_frequency_boost_curvature, 0, 0.99f);
    out->high_pass_frequency = glm_clamp(in->high_pass_frequency, 0, 1);
    out->threshold = glm_max(in->threshold, 0.0f);
    out->threshold_softness = glm_clamp(in->threshold_softness, 0.0f, 1.0f);
    out->firefly_clamp = glm_max(in->firefly_clamp, 0.0f);
    out->temporal = glm_clamp(in->temporal, 0.0f, BOX_EFFECTS_BLOOM_TEMPORAL);
    if (out->max_mip_dimension < BOX_EFFECTS_BLOOM_MIP_MIN) {
        out->max_mip_dimension = BOX_EFFECTS_BLOOM_MIP_MIN;
    }
}

void box_render_gtao_settings(
    const Gtao *in,
    Gtao *out)
{
    *out = *in;
    out->radius = glm_max(in->radius, 0.0f);
    out->falloff_range = glm_clamp(in->falloff_range, 0.0f, 1.0f);
    out->final_value_power = glm_max(in->final_value_power, 0.0f);
    out->quality = in->quality >= GtaoLow && in->quality <= GtaoUltra ? in->quality : GtaoHigh;
    out->denoise_passes = glm_imax(glm_imin(in->denoise_passes,
        BOX_EFFECTS_GTAO_DENOISE_MAX), 0);
}

void box_render_fog_settings(
    const HeightFog *in,
    HeightFog *out)
{
    *out = *in;
    out->density = glm_max(in->density, 0.0f);
    out->falloff = glm_max(in->falloff, 0.0f);
    out->max_opacity = glm_clamp(in->max_opacity, 0.0f, 1.0f);
    out->sun_inscatter = glm_max(in->sun_inscatter, 0.0f);
    out->ambient_inscatter = glm_max(in->ambient_inscatter, 0.0f);
}

void box_render_exposure_settings(
    const AutoExposure *in,
    AutoExposure *out)
{
    *out = *in;
    out->target_brightness = glm_max(in->target_brightness, 0.0f);
    out->min_brightness = glm_max(in->min_brightness, BOX_EFFECTS_EXPOSURE_EPSILON);
    out->max_brightness = glm_max(in->max_brightness, out->min_brightness);
    out->max_log_luma = glm_max(in->max_log_luma, in->min_log_luma + BOX_EFFECTS_EXPOSURE_EPSILON);
    out->speed_up = glm_max(in->speed_up, 0.0f);
    out->speed_down = glm_max(in->speed_down, 0.0f);
    out->high_percentile = glm_clamp(in->high_percentile, 0, 1);
    out->low_percentile = glm_clamp(in->low_percentile, 0.0f, out->high_percentile);
    out->min_exposure = glm_max(in->min_exposure, 0.0f);
    out->max_exposure = glm_max(in->max_exposure, 0.0f);
}

void box_render_smaa_settings(
    const Smaa *in,
    Smaa *out)
{
    out->threshold = glm_clamp(in->threshold, BOX_EFFECTS_SMAA_THRESHOLD_MIN,
        BOX_EFFECTS_SMAA_THRESHOLD_MAX);
    out->max_search_steps = glm_imax(glm_imin(in->max_search_steps,
        BOX_EFFECTS_SMAA_STEPS_MAX), 1);
    out->max_search_steps_diag = glm_imax(glm_imin(in->max_search_steps_diag,
        BOX_EFFECTS_SMAA_DIAG_STEPS_MAX), 0);
    out->corner_rounding = glm_imax(glm_imin(in->corner_rounding, 100), 0);
}

/** Append an effect pass to the packet and count it for its view.
 *
 * @param p The packet
 * @param v The view
 * @param kind Kind of pass
 * @param e The effect entity
 * @return The zeroed pass
 */
static box_render_effect_t* box_effects_append(
    box_render_packet_t *p,
    box_render_view_t *v,
    box_render_effect_kind_t kind,
    ecs_entity_t e)
{
    box_render_effect_t *result = ecs_vec_append_t(NULL, &p->effects, box_render_effect_t);
    ecs_os_memset_t(result, 0, box_render_effect_t);
    result->kind = kind;
    result->entity = e;
    v->effect_count ++;

    return result;
}

/** Add the passes of one effect entity to a view, in a fixed order when the entity holds
 * the settings of several effects.
 *
 * @param world The world
 * @param p The packet
 * @param v The view
 * @param e The effect entity
 */
static void box_effects_entity(
    ecs_world_t *world,
    box_render_packet_t *p,
    box_render_view_t *v,
    ecs_entity_t e)
{
    const HeightFog *fog = ecs_get(world, e, HeightFog);
    const Bloom *bloom = ecs_get(world, e, Bloom);
    const AutoExposure *exposure = ecs_get(world, e, AutoExposure);
    const Tonemap *tonemap = ecs_get(world, e, Tonemap);
    const Smaa *smaa = ecs_get(world, e, Smaa);
    if (fog) {
        box_render_fog_settings(fog, &box_effects_append(p, v, BOX_RENDER_EFFECT_FOG, e)->fog);
    }

    if (bloom) {
        box_render_bloom_settings(bloom, &box_effects_append(
            p, v, BOX_RENDER_EFFECT_BLOOM, e)->bloom);
    }

    if (exposure && !tonemap) {
        box_render_effect_t *pass = box_effects_append(p, v, BOX_RENDER_EFFECT_EXPOSURE, e);
        pass->exposure = e;
        box_render_exposure_settings(exposure, &pass->auto_exposure);
    }

    if (tonemap) {
        box_render_effect_t *pass = box_effects_append(p, v, BOX_RENDER_EFFECT_TONEMAP, e);
        ecs_entity_t source = tonemap->auto_exposure;
        pass->tonemap = tonemap->operator_ == TonemapOperatorTonyMcMapface
            ? TonemapOperatorTonyMcMapface
            : TonemapOperatorPbrNeutral;
        const AutoExposure *settings = source && ecs_is_alive(world, source)
            ? ecs_get(world, source, AutoExposure)
            : exposure;
        if (settings) {
            pass->exposure = settings == exposure ? e : source;
            box_render_exposure_settings(settings, &pass->auto_exposure);
        }
    }

    if (smaa) {
        box_render_smaa_settings(smaa, &box_effects_append(
            p, v, BOX_RENDER_EFFECT_SMAA, e)->smaa);
    }
}

/** Add the effect passes of a view of the packet.
 *
 * @param world The world
 * @param p The packet
 * @param v The view
 */
static void box_effects_view(
    ecs_world_t *world,
    box_render_packet_t *p,
    box_render_view_t *v)
{
    const View *view = v->view && ecs_is_alive(world, v->view)
        ? ecs_get(world, v->view, View)
        : NULL;
    int32_t i, count = view ? ecs_vec_count(&view->effects) : 0;
    const ViewEffect *effects = view ? ecs_vec_first_t(&view->effects, ViewEffect) : NULL;
    bool gtao = false;
    v->effect_first = ecs_vec_count(&p->effects);
    v->effect_count = 0;
    ecs_os_memset_t(&v->gtao, 0, box_render_view_gtao_t);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = effects[i].effect;
        if (!effects[i].enabled || !e || !ecs_is_alive(world, e) ||
            ecs_has_id(world, e, EcsDisabled))
        {
            continue;
        }

        const Gtao *ao = gtao ? NULL : ecs_get(world, e, Gtao);
        if (ao) {
            gtao = true;
            v->gtao.enabled = v->lights.enabled && !v->camera_data.ortho;
            v->gtao.effect = e;
            box_render_gtao_settings(ao, &v->gtao.settings);
        }

        box_effects_entity(world, p, v, e);
    }
}

/** Add the effect passes of every view of the packet.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractEffects(
    ecs_iter_t *it)
{
    box_render_packet_t *p = box_render_packet(it->world);
    int32_t i, count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_effects_view(it->world, p, ecs_vec_get_t(&p->views, box_render_view_t, i));
    }
}

bool box_render_effects_use(
    const box_render_packet_t *p,
    uint32_t handle)
{
    bool result = false;
    int32_t i, count = ecs_vec_count(&p->effects);
    for (i = 0; i < count && !result; i ++) {
        const box_render_effect_t *e = ecs_vec_get_t(&p->effects, box_render_effect_t, i);
        switch (handle) {
        case BOX_RENDER_SHADER_TONEMAP:
            result = true;
            break;
        case BOX_RENDER_SHADER_EXPOSURE:
            result = e->exposure != 0;
            break;
        case BOX_RENDER_SHADER_BLOOM:
            result = e->kind == BOX_RENDER_EFFECT_BLOOM;
            break;
        case BOX_RENDER_SHADER_FOG:
            result = e->kind == BOX_RENDER_EFFECT_FOG;
            break;
        case BOX_RENDER_SHADER_SMAA:
            result = e->kind == BOX_RENDER_EFFECT_SMAA;
            break;
        default:
            break;
        }
    }

    count = handle == BOX_RENDER_SHADER_GTAO ? ecs_vec_count(&p->views) : 0;
    for (i = 0; i < count && !result; i ++) {
        result = ecs_vec_get_t(&p->views, box_render_view_t, i)->gtao.enabled;
    }

    return result;
}

void box_render_effects_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractEffects" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractEffects
    });
}
