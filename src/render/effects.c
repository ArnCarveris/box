/**
 * @file render/effects.c
 * @brief Adds the effect passes of every view to the packet, in the order of View.effects.
 *
 * Disabled entries, entries whose entity is gone and entities without effect settings add no
 * pass; a view without passes renders straight into its target, without an HDR target.
 */

#include "render.h"

/** Longest edge of the first bloom mip a view that does not set it uses */
#define BOX_EFFECTS_BLOOM_MIP (512)

/** Shortest edge of the first bloom mip */
#define BOX_EFFECTS_BLOOM_MIP_MIN (8)

/** Highest temporal blend of bloom */
#define BOX_EFFECTS_BLOOM_TEMPORAL (0.95f)

/** Take a default for a value of zero or less.
 *
 * @param value The value
 * @param fallback The value that replaces zero or less
 * @return The value or the default
 */
static float box_effects_positive(
    float value,
    float fallback)
{
    return value > 0 ? value : fallback;
}

void box_render_bloom_settings(
    const Bloom *in,
    Bloom *out)
{
    *out = *in;
    out->intensity = box_effects_positive(in->intensity, 0.3f);
    out->low_frequency_boost = glm_max(in->low_frequency_boost, 0.0f);
    out->low_frequency_boost_curvature = glm_clamp(in->low_frequency_boost_curvature, 0, 0.99f);
    out->high_pass_frequency = glm_clamp(box_effects_positive(in->high_pass_frequency, 1), 0, 1);
    out->threshold = glm_max(in->threshold, 0.0f);
    out->threshold_softness = glm_clamp(in->threshold_softness, 0.0f, 1.0f);
    out->firefly_clamp = glm_max(in->firefly_clamp, 0.0f);
    out->temporal = glm_clamp(in->temporal, 0.0f, BOX_EFFECTS_BLOOM_TEMPORAL);
    out->max_mip_dimension = in->max_mip_dimension > 0
        ? in->max_mip_dimension
        : BOX_EFFECTS_BLOOM_MIP;
    if (out->max_mip_dimension < BOX_EFFECTS_BLOOM_MIP_MIN) {
        out->max_mip_dimension = BOX_EFFECTS_BLOOM_MIP_MIN;
    }
}

void box_render_ssao_settings(
    const Ssao *in,
    Ssao *out)
{
    out->radius = box_effects_positive(in->radius, 0.5f);
    out->bias = glm_max(in->bias, 0.0f);
    out->intensity = box_effects_positive(in->intensity, 1.0f);
}

void box_render_fog_settings(
    const HeightFog *in,
    HeightFog *out)
{
    *out = *in;
    out->density = box_effects_positive(in->density, 0.1f);
    out->falloff = glm_max(in->falloff, 0.0f);
    out->max_opacity = glm_clamp(box_effects_positive(in->max_opacity, 1.0f), 0.0f, 1.0f);
    out->sun_inscatter = glm_max(in->sun_inscatter, 0.0f);
    out->ambient_inscatter = glm_max(in->ambient_inscatter, 0.0f);
    if (!in->color.r && !in->color.g && !in->color.b) {
        out->color = (rgb){191, 158, 140};
    }
}

void box_render_exposure_settings(
    const AutoExposure *in,
    AutoExposure *out)
{
    *out = *in;
    out->target_brightness = glm_max(in->target_brightness, 0.0f);
    out->min_brightness = box_effects_positive(in->min_brightness, 0.1f);
    out->max_brightness = glm_max(box_effects_positive(in->max_brightness, 0.3f),
        out->min_brightness);
    if (in->max_log_luma <= in->min_log_luma) {
        out->min_log_luma = -8.0f;
        out->max_log_luma = 4.0f;
    }

    out->speed_up = box_effects_positive(in->speed_up, 3.0f);
    out->speed_down = box_effects_positive(in->speed_down, 1.0f);
    out->high_percentile = glm_clamp(box_effects_positive(in->high_percentile, 0.95f), 0, 1);
    out->low_percentile = glm_clamp(in->low_percentile, 0.0f, out->high_percentile);
    out->min_exposure = glm_max(in->min_exposure, 0.0f);
    out->max_exposure = glm_max(in->max_exposure, 0.0f);
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
    const Ssao *ssao = ecs_get(world, e, Ssao);
    const HeightFog *fog = ecs_get(world, e, HeightFog);
    const Bloom *bloom = ecs_get(world, e, Bloom);
    const AutoExposure *exposure = ecs_get(world, e, AutoExposure);
    const Tonemap *tonemap = ecs_get(world, e, Tonemap);
    if (ssao) {
        box_render_ssao_settings(ssao, &box_effects_append(
            p, v, BOX_RENDER_EFFECT_SSAO, e)->ssao);
    }

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
        const AutoExposure *settings = source && ecs_is_alive(world, source)
            ? ecs_get(world, source, AutoExposure)
            : exposure;
        if (settings) {
            pass->exposure = settings == exposure ? e : source;
            box_render_exposure_settings(settings, &pass->auto_exposure);
        }
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
    v->effect_first = ecs_vec_count(&p->effects);
    v->effect_count = 0;
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = effects[i].effect;
        if (!effects[i].enabled || !e || !ecs_is_alive(world, e) ||
            ecs_has_id(world, e, EcsDisabled))
        {
            continue;
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
        case BOX_RENDER_SHADER_SSAO:
            result = e->kind == BOX_RENDER_EFFECT_SSAO;
            break;
        case BOX_RENDER_SHADER_FOG:
            result = e->kind == BOX_RENDER_EFFECT_FOG;
            break;
        default:
            break;
        }
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
