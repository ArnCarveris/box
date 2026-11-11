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
#include "../texture/texture.h"

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

/** Smallest gamma of a color grading */
#define BOX_EFFECTS_GRADING_GAMMA_MIN (0.01f)

/** Range of the taps of a chromatic aberration */
#define BOX_EFFECTS_CA_SAMPLES_MIN (3)
#define BOX_EFFECTS_CA_SAMPLES_MAX (16)

/** Largest blur radius of a depth of field, a fraction of the image height */
#define BOX_EFFECTS_DOF_RADIUS_MAX (0.05f)

/** State of the extract of the effects, kept across frames */
typedef struct box_effects_state_t {
    ecs_map_t lut_errors;   /**< LUT entity to the shape it was rejected for, width << 32 |
                             *   height, so each shape logs once */
} box_effects_state_t;

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

void box_render_depth_of_field_settings(
    const DepthOfField *in,
    DepthOfField *out)
{
    *out = *in;
    out->focus_distance = glm_max(in->focus_distance, 0.0f);
    out->focus_range = glm_max(in->focus_range, 0.0f);
    out->near_transition = glm_max(in->near_transition, 0.0f);
    out->far_transition = glm_max(in->far_transition, 0.0f);
    out->max_radius = glm_clamp(in->max_radius, 0.0f, BOX_EFFECTS_DOF_RADIUS_MAX);
    out->tilt_band = glm_max(in->tilt_band, 0.0f);
    out->tilt_transition = glm_max(in->tilt_transition, 0.0f);
    if (in->mode == DofTiltShift) {
        out->near_blur = false;
    } else {
        out->mode = DofCamera;
    }
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

void box_render_grading_settings(
    const ColorGrading *in,
    ColorGrading *out)
{
    int32_t i;
    *out = *in;
    out->contrast = glm_max(in->contrast, 0.0f);
    out->saturation = glm_max(in->saturation, 0.0f);
    out->vibrance = glm_max(in->vibrance, -1.0f);
    for (i = 0; i < 3; i ++) {
        out->gamma[i] = glm_max(in->gamma[i], BOX_EFFECTS_GRADING_GAMMA_MIN);
    }

    out->lut_strength = glm_clamp(in->lut_strength, 0.0f, 1.0f);
}

void box_render_vignette_settings(
    const Vignette *in,
    Vignette *out)
{
    *out = *in;
    out->intensity = glm_clamp(in->intensity, 0.0f, 1.0f);
    out->radius = glm_max(in->radius, 0.0f);
    out->smoothness = glm_max(in->smoothness, 0.0f);
    out->roundness = glm_clamp(in->roundness, 0.0f, 1.0f);
}

void box_render_chromatic_aberration_settings(
    const ChromaticAberration *in,
    ChromaticAberration *out)
{
    *out = *in;
    out->intensity = glm_max(in->intensity, 0.0f);
    out->samples = glm_imax(glm_imin(in->samples, BOX_EFFECTS_CA_SAMPLES_MAX),
        BOX_EFFECTS_CA_SAMPLES_MIN);
    out->falloff = glm_max(in->falloff, 0.0f);
}

/** Resolve the strip LUT of a color grading pass: its texture handle once the image is loaded,
 * when its width is the square of its height. Another shape logs an error once.
 *
 * @param world The world
 * @param state The state of the extract
 * @param g The pass
 */
static void box_effects_grading_lut(
    ecs_world_t *world,
    box_effects_state_t *state,
    box_render_grading_t *g)
{
    ecs_entity_t e = g->settings.lut;
    const box_textures_t *t = e && g->settings.lut_strength > 0 && ecs_is_alive(world, e)
        ? box_textures_get(world)
        : NULL;
    uint32_t handle = t ? box_textures_handle(t, e) : 0;
    const box_texture_t *tex = handle ? ecs_map_get_deref(&t->textures, box_texture_t, e) : NULL;
    if (!tex) {
        goto done;
    }

    if (tex->height < 2 || tex->width != tex->height * tex->height || tex->single_channel) {
        uint64_t shape = ((uint64_t)(uint32_t)tex->width << 32) | (uint32_t)tex->height;
        ecs_map_val_t *logged = ecs_map_ensure(&state->lut_errors, e);
        if (*logged != shape + 1) {
            char *name = ecs_get_path(world, e);
            ecs_err("ColorGrading LUT %s is %dx%d%s, a strip LUT is N*N x N", name,
                tex->width, tex->height, tex->single_channel ? " single channel" : "");
            ecs_os_free(name);
            *logged = shape + 1;
        }

        goto done;
    }

    ecs_map_remove(&state->lut_errors, e);
    g->lut = handle;
    g->lut_size = tex->height;
done:
    return;
}

/** Check whether a Tonemap pass of a view comes before the passes appended next.
 *
 * @param p The packet
 * @param v The view
 * @return Whether the view has a tone map pass so far
 */
static bool box_effects_tonemapped(
    const box_render_packet_t *p,
    const box_render_view_t *v)
{
    int32_t i;
    bool result = false;
    for (i = 0; i < v->effect_count && !result; i ++) {
        result = ecs_vec_get_t(&p->effects, box_render_effect_t,
            v->effect_first + i)->kind == BOX_RENDER_EFFECT_TONEMAP;
    }

    return result;
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
 * @param state The state of the extract
 * @param p The packet
 * @param v The view
 * @param e The effect entity
 */
static void box_effects_entity(
    ecs_world_t *world,
    box_effects_state_t *state,
    box_render_packet_t *p,
    box_render_view_t *v,
    ecs_entity_t e)
{
    const HeightFog *fog = ecs_get(world, e, HeightFog);
    const DepthOfField *dof = ecs_get(world, e, DepthOfField);
    const Bloom *bloom = ecs_get(world, e, Bloom);
    const AutoExposure *exposure = ecs_get(world, e, AutoExposure);
    const Tonemap *tonemap = ecs_get(world, e, Tonemap);
    const ColorGrading *grading = ecs_get(world, e, ColorGrading);
    const ChromaticAberration *ca = ecs_get(world, e, ChromaticAberration);
    const Vignette *vignette = ecs_get(world, e, Vignette);
    const Smaa *smaa = ecs_get(world, e, Smaa);
    if (fog) {
        box_render_fog_settings(fog, &box_effects_append(p, v, BOX_RENDER_EFFECT_FOG, e)->fog);
    }

    if (dof) {
        box_render_depth_of_field_settings(dof, &box_effects_append(
            p, v, BOX_RENDER_EFFECT_DEPTH_OF_FIELD, e)->depth_of_field);
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

    if (grading) {
        bool display = box_effects_tonemapped(p, v);
        box_render_grading_t *g = &box_effects_append(
            p, v, BOX_RENDER_EFFECT_GRADING, e)->grading;
        box_render_grading_settings(grading, &g->settings);
        g->display = display;
        box_effects_grading_lut(world, state, g);
    }

    if (ca) {
        box_render_chromatic_aberration_settings(ca, &box_effects_append(
            p, v, BOX_RENDER_EFFECT_CHROMATIC_ABERRATION, e)->chromatic_aberration);
    }

    if (vignette) {
        box_render_vignette_settings(vignette, &box_effects_append(
            p, v, BOX_RENDER_EFFECT_VIGNETTE, e)->vignette);
    }

    if (smaa) {
        box_render_smaa_settings(smaa, &box_effects_append(
            p, v, BOX_RENDER_EFFECT_SMAA, e)->smaa);
    }
}

/** Add the effect passes of a view of the packet.
 *
 * @param world The world
 * @param state The state of the extract
 * @param p The packet
 * @param v The view
 */
static void box_effects_view(
    ecs_world_t *world,
    box_effects_state_t *state,
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

        box_effects_entity(world, state, p, v, e);
    }
}

/** Forget the LUT errors of deleted texture entities.
 *
 * @param world The world
 * @param state The state of the extract
 */
static void box_effects_sweep_lut_errors(
    ecs_world_t *world,
    box_effects_state_t *state)
{
    bool removed = true;
    while (removed) {
        removed = false;
        ecs_map_iter_t it = ecs_map_iter(&state->lut_errors);
        while (ecs_map_next(&it)) {
            ecs_entity_t e = ecs_map_key(&it);
            if (!ecs_is_alive(world, e)) {
                ecs_map_remove(&state->lut_errors, e);
                removed = true;
                break;
            }
        }
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
    box_effects_sweep_lut_errors(it->world, it->ctx);
    for (i = 0; i < count; i ++) {
        box_effects_view(it->world, it->ctx, p,
            ecs_vec_get_t(&p->views, box_render_view_t, i));
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
        case BOX_RENDER_SHADER_GRADING:
            result = e->kind == BOX_RENDER_EFFECT_GRADING;
            break;
        case BOX_RENDER_SHADER_VIGNETTE:
            result = e->kind == BOX_RENDER_EFFECT_VIGNETTE;
            break;
        case BOX_RENDER_SHADER_CHROMATIC_ABERRATION:
            result = e->kind == BOX_RENDER_EFFECT_CHROMATIC_ABERRATION;
            break;
        case BOX_RENDER_SHADER_DEPTH_OF_FIELD:
            result = e->kind == BOX_RENDER_EFFECT_DEPTH_OF_FIELD;
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

/** Free the state of the extract.
 *
 * @param ctx The state
 */
static void box_effects_state_free(
    void *ctx)
{
    box_effects_state_t *state = ctx;
    ecs_map_fini(&state->lut_errors);
    ecs_os_free(state);
}

void box_render_effects_init(
    ecs_world_t *world)
{
    box_effects_state_t *state = ecs_os_calloc_t(box_effects_state_t);
    ecs_map_init(&state->lut_errors, NULL);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractEffects" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractEffects,
        .ctx = state,
        .ctx_free = box_effects_state_free
    });
}
