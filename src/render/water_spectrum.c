/**
 * @file render/water_spectrum.c
 * @brief The spectra of WaterSpectrum on the main thread: the parameters of the initial
 * spectrum of each scene, a revision that changes with them, the wrapped render time and the
 * water frame, appended to the packet for the backend (gpu_spectrum.c).
 *
 * The shapes that inherit one WaterSpectrum share its spectrum, keyed by the entity that owns
 * the component. A scene binds the first one its query meets; another owner logs an error once.
 *
 * The water frame integrates the WaterFlow in double precision, wrapped to the largest tile,
 * which the tiles of the other cascades divide, so the offset the fragments add stays small.
 */

#include "render.h"
#include <math.h>
#include <string.h>

/** The state of the spectrum of a WaterSpectrum the extract keeps between frames */
typedef struct box_spectrum_state_t {
    box_render_water_spectrum_t params; /**< The parameters of its initial spectrum */
    uint32_t revision;      /**< Revision of params */
    float height_scale;     /**< One over twice the standard deviation of its height */
    double frame[2];        /**< World XZ offset of the water frame, wrapped to its largest
                             *   tile */
    int64_t moved;          /**< Extract frame that moved the water frame last */
    int64_t seen;           /**< Extract frame that saw it last */
} box_spectrum_state_t;

/** A scene and the owner of the WaterSpectrum it binds this frame */
typedef struct box_spectrum_scene_t {
    ecs_entity_t scene;     /**< The scene root, 0 for the default scene */
    ecs_entity_t water;     /**< The owner of its WaterSpectrum */
} box_spectrum_scene_t;

/** The state of the spectrum extract */
typedef struct box_render_spectrum_state_t {
    ecs_query_t *spectra;   /**< WaterSpectrum with Water and their scene */
    ecs_map_t by_water;     /**< Owner of a WaterSpectrum to box_spectrum_state_t* */
    ecs_vec_t scenes;       /**< box_spectrum_scene_t, scratch: the scenes of this frame */
    uint32_t revisions;     /**< Revisions handed out, so a new spectrum never reuses one */
    int64_t frame;          /**< Frames extracted */
    bool warned;            /**< A scene with more than one owner was reported */
} box_render_spectrum_state_t;

int32_t box_render_spectrum_resolution(
    int32_t resolution)
{
    int32_t result = BOX_MATERIAL_SPECTRUM_RESOLUTION_MIN;
    while (result < BOX_MATERIAL_SPECTRUM_RESOLUTION_MAX &&
        (int64_t)resolution * 4 >= (int64_t)result * 6)
    {
        result *= 2;
    }

    return result;
}

int32_t box_render_spectrum_cascades(
    int32_t cascades)
{
    return cascades < 1 ? 1
        : cascades > BOX_RENDER_SPECTRUM_CASCADES ? BOX_RENDER_SPECTRUM_CASCADES : cascades;
}

double box_render_spectrum_time(
    double time)
{
    double result = 0.0;
    if (isfinite(time)) {
        result = fmod(time, BOX_RENDER_SPECTRUM_PERIOD);
        if (result < 0.0) {
            result += BOX_RENDER_SPECTRUM_PERIOD;
        }

        if (result >= BOX_RENDER_SPECTRUM_PERIOD) {
            result = 0.0;
        }
    }

    return result;
}

void box_render_spectrum_params(
    const WaterSpectrum *spectrum,
    box_render_water_spectrum_t *out)
{
    int32_t cascades = box_render_spectrum_cascades(spectrum->cascades), c;
    float size = isfinite(spectrum->size) && spectrum->size > 0.0f
        ? spectrum->size : BOX_MATERIAL_SPECTRUM_SIZE;
    float wx = spectrum->wind_direction[0], wz = spectrum->wind_direction[1];
    float len = sqrtf(wx * wx + wz * wz);
    out->resolution = (uint32_t)box_render_spectrum_resolution(spectrum->resolution);
    out->cascades = (uint32_t)cascades;
    for (c = 0; c < BOX_RENDER_SPECTRUM_CASCADES; c ++) {
        out->sizes[c] = c < cascades
            ? size / powf(BOX_RENDER_SPECTRUM_RATIO, (float)c) : 0.0f;
    }

    for (c = 0; c <= BOX_RENDER_SPECTRUM_CASCADES; c ++) {
        out->cuts[c] = c == 0 || c > cascades ? 0.0f
            : c == cascades ? (float)(GLM_PI * BOX_RENDER_SPECTRUM_NYQUIST *
                (double)out->resolution / out->sizes[c - 1])
            : (float)(2.0 * GLM_PI * BOX_RENDER_SPECTRUM_CUT / out->sizes[c]);
    }

    if (!isfinite(len) || !(len > 1e-6f)) {
        wx = 1.0f;
        wz = 0.0f;
    } else {
        wx /= len;
        wz /= len;
    }

    out->wind[0] = wx;
    out->wind[1] = wz;
    out->wind_speed = isfinite(spectrum->wind_speed) && spectrum->wind_speed > 0.0f
        ? spectrum->wind_speed : 0.0f;
    out->directionality = isfinite(spectrum->directionality)
        ? glm_clamp(spectrum->directionality, 0.0f, 1.0f) : 0.0f;
    out->amplitude = isfinite(spectrum->amplitude) && spectrum->amplitude > 0.0f
        ? spectrum->amplitude : 0.0f;
    out->detail = isfinite(spectrum->detail) && spectrum->detail > 0.0f
        ? spectrum->detail : 0.0f;
    out->seed = spectrum->seed;
}

bool box_render_spectrum_same(
    const box_render_water_spectrum_t *a,
    const box_render_water_spectrum_t *b)
{
    return a->resolution == b->resolution && a->cascades == b->cascades &&
        a->seed == b->seed && !memcmp(a->sizes, b->sizes, sizeof(a->sizes)) &&
        !memcmp(a->cuts, b->cuts, sizeof(a->cuts)) &&
        a->wind[0] == b->wind[0] && a->wind[1] == b->wind[1] &&
        a->wind_speed == b->wind_speed && a->directionality == b->directionality &&
        a->amplitude == b->amplitude;
}

double box_render_spectrum_omega(
    double k)
{
    double step = 2.0 * GLM_PI / BOX_RENDER_SPECTRUM_PERIOD;
    double omega = k > 0.0 ? sqrt((double)BOX_MATERIAL_WATER_GRAVITY * k) : 0.0;

    return floor(omega / step) * step;
}

double box_render_spectrum_density(
    const box_render_water_spectrum_t *s,
    double kx,
    double kz)
{
    double result = 0.0;
    double k = sqrt(kx * kx + kz * kz), g = BOX_MATERIAL_WATER_GRAVITY;
    if (!(k > 0.0) || !(s->wind_speed > 0.0f) || !(s->amplitude > 0.0f)) {
        goto done;
    }

    double omega = sqrt(g * k);
    double peak = 0.855 * g / (1.026 * s->wind_speed);
    double sigma = omega <= peak ? 0.07 : 0.09;
    double d = (omega - peak) / (sigma * peak);
    double ratio = peak / omega;
    double jonswap = 0.0081 * g * g / pow(omega, 5.0) *
        exp(-1.25 * ratio * ratio * ratio * ratio) * pow(3.3, exp(-0.5 * d * d));

    double c = (kx * s->wind[0] + kz * s->wind[1]) / k;
    double spread = (double)s->directionality * (c > 0.0 ? 2.0 / GLM_PI * c * c : 0.0) +
        (1.0 - (double)s->directionality) / (2.0 * GLM_PI);
    double short_ = k * BOX_RENDER_SPECTRUM_SHORTEST;

    result = jonswap * (g / (2.0 * omega)) / k * spread * exp(-short_ * short_) *
        (double)s->amplitude * (double)s->amplitude;

done:
    return result;
}

double box_render_spectrum_variance(
    const box_render_water_spectrum_t *s,
    int32_t cascade,
    bool slope)
{
    double result = 0.0;
    int32_t n = (int32_t)s->resolution, x, z;
    if (cascade < 0 || cascade >= (int32_t)s->cascades || !(s->sizes[cascade] > 0.0f)) {
        goto done;
    }

    double dk = 2.0 * GLM_PI / s->sizes[cascade];
    double lo = s->cuts[cascade], hi = s->cuts[cascade + 1];
    for (z = 0; z < n; z ++) {
        int32_t mz = z < n / 2 ? z : z - n;
        if (mz == -n / 2) {
            continue;
        }

        for (x = 0; x < n; x ++) {
            int32_t mx = x < n / 2 ? x : x - n;
            if (mx == -n / 2) {
                continue;
            }

            double kx = mx * dk, kz = mz * dk, k2 = kx * kx + kz * kz;
            double k = sqrt(k2);
            if (k < lo || k >= hi) {
                continue;
            }

            result += box_render_spectrum_density(s, kx, kz) * dk * dk * (slope ? k2 : 1.0);
        }
    }

done:
    return result;
}

/** Get the state of the spectrum extract of a world.
 *
 * @param world The world
 * @return The state, NULL before box.render was imported
 */
static box_render_spectrum_state_t* box_spectrum_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.WaterSpectrum");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;

    return s ? s->ctx : NULL;
}

bool box_render_spectrum_revision(
    const ecs_world_t *world,
    ecs_entity_t water,
    uint32_t *out)
{
    const box_render_spectrum_state_t *l = box_spectrum_state(world);
    const box_spectrum_state_t *st = l
        ? ecs_map_get_deref(&l->by_water, box_spectrum_state_t, water) : NULL;
    if (st) {
        *out = st->revision;
    }

    return st != NULL;
}

/** Find the owner of the WaterSpectrum a scene binds this frame, and remember the scene with an
 * owner when it has none yet.
 *
 * @param l The state
 * @param scene The scene root
 * @param water The owner to bind when the scene has none
 * @return The owner the scene binds
 */
static ecs_entity_t box_spectrum_scene(
    box_render_spectrum_state_t *l,
    ecs_entity_t scene,
    ecs_entity_t water)
{
    ecs_entity_t result = 0;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&l->scenes) && !result; i ++) {
        const box_spectrum_scene_t *s = ecs_vec_get_t(&l->scenes, box_spectrum_scene_t, i);
        if (s->scene == scene) {
            result = s->water;
        }
    }

    if (!result) {
        *ecs_vec_append_t(NULL, &l->scenes, box_spectrum_scene_t) =
            (box_spectrum_scene_t){ .scene = scene, .water = water };
        result = water;
    }

    return result;
}

/** Wrap a world offset to 0..size.
 *
 * @param v The offset
 * @param size The period, above 0
 * @return The wrapped offset
 */
static double box_spectrum_wrap(
    double v,
    double size)
{
    double result = isfinite(v) ? fmod(v, size) : 0.0;
    if (result < 0.0) {
        result += size;
    }

    return result >= size ? 0.0 : result;
}

/** Update the state of the spectrum of an owner for the frame and append its record.
 *
 * @param world The world
 * @param p The packet
 * @param l The state of the extract
 * @param water The owner of the WaterSpectrum
 * @param spectrum The component
 * @param shape The Water shape whose WaterFlow moves the water frame
 * @param scene Its scene root
 */
static void box_spectrum_append(
    ecs_world_t *world,
    box_render_packet_t *p,
    box_render_spectrum_state_t *l,
    ecs_entity_t water,
    const WaterSpectrum *spectrum,
    ecs_entity_t shape,
    ecs_entity_t scene)
{
    double delta = isfinite(p->delta) && p->delta > 0.0f ? (double)p->delta : 0.0;
    double origin[3];
    box_render_water_spectrum_t params;
    int32_t c;
    ecs_os_memset_t(&params, 0, box_render_water_spectrum_t);
    box_render_spectrum_params(spectrum, &params);

    box_spectrum_state_t **slot = (box_spectrum_state_t**)ecs_map_ensure(&l->by_water, water);
    box_spectrum_state_t *st = *slot;
    bool fresh = st == NULL;
    if (fresh) {
        st = *slot = ecs_os_calloc_t(box_spectrum_state_t);
        st->moved = l->frame - 1;
    }

    if (fresh || !box_render_spectrum_same(&st->params, &params)) {
        double variance = 0.0;
        st->params = params;
        st->revision = ++ l->revisions;
        for (c = 0; c < (int32_t)params.cascades; c ++) {
            variance += box_render_spectrum_variance(&params, c, false);
        }

        st->height_scale = variance > 0.0 ? (float)(0.5 / sqrt(variance)) : 0.0f;
    }

    st->params.detail = params.detail;
    st->seen = l->frame;
    double size = params.sizes[0];
    if (st->moved != l->frame) {
        const WaterFlow *flow = ecs_get(world, shape, WaterFlow);
        st->moved = l->frame;
        if (flow && isfinite(flow->velocity[0]) && isfinite(flow->velocity[1])) {
            st->frame[0] = box_spectrum_wrap(
                st->frame[0] + (double)flow->velocity[0] * delta, size);
            st->frame[1] = box_spectrum_wrap(
                st->frame[1] + (double)flow->velocity[1] * delta, size);
        }
    }

    box_render_origin(world, origin);
    box_render_water_spectrum_t *rec = ecs_vec_append_t(
        NULL, &p->water_spectra, box_render_water_spectrum_t);
    *rec = st->params;
    rec->water = water;
    rec->scene = scene;
    rec->revision = st->revision;
    rec->time = (float)box_render_spectrum_time(p->time);
    rec->offset[0] = (float)box_spectrum_wrap(origin[0] - st->frame[0], size);
    rec->offset[1] = (float)box_spectrum_wrap(origin[2] - st->frame[1], size);
    rec->height_scale = st->height_scale;
}

/** Append the spectrum of WaterSpectrum of every scene to the packet.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractSpectrum(
    ecs_iter_t *it)
{
    box_render_spectrum_state_t *l = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = box_render_packet(world);
    int32_t i;
    l->frame ++;
    ecs_vec_clear(&l->scenes);

    ecs_iter_t qit = ecs_query_iter(world, l->spectra);
    while (ecs_query_next(&qit)) {
        const WaterSpectrum *spectra = ecs_field(&qit, WaterSpectrum, 0);
        bool self = ecs_field_is_self(&qit, 0);
        for (i = 0; i < qit.count; i ++) {
            const WaterSpectrum *s = self ? &spectra[i] : spectra;
            ecs_entity_t water = self ? qit.entities[i] : ecs_field_src(&qit, 0);
            ecs_entity_t scene = box_render_scene_field(&qit, 2, i);
            if (!(box_render_scene_features(world, scene) & BOX_RENDER_FEATURE_WATER)) {
                continue;
            }

            ecs_entity_t bound = box_spectrum_scene(l, scene, water);
            if (bound != water) {
                if (!l->warned) {
                    char *name = ecs_get_path(world, water);
                    ecs_err("WaterSpectrum of '%s': its scene already has a spectrum, its "
                        "shapes shade with the first one", name);
                    ecs_os_free(name);
                    l->warned = true;
                }

                continue;
            }

            int32_t j, count = ecs_vec_count(&p->water_spectra);
            bool appended = false;
            for (j = 0; j < count && !appended; j ++) {
                const box_render_water_spectrum_t *r = ecs_vec_get_t(
                    &p->water_spectra, box_render_water_spectrum_t, j);
                appended = r->water == water && r->scene == scene;
            }

            if (!appended) {
                box_spectrum_append(world, p, l, water, s, qit.entities[i], scene);
            }
        }
    }

    ecs_map_iter_t mit = ecs_map_iter(&l->by_water);
    while (ecs_map_next(&mit)) {
        box_spectrum_state_t *st = ecs_map_ptr(&mit);
        if (st->seen != l->frame) {
            ecs_os_free(st);
            ecs_map_remove(&l->by_water, ecs_map_key(&mit));
            mit = ecs_map_iter(&l->by_water);
        }
    }
}

/** Free the state of the spectrum extract.
 *
 * @param ctx The state
 */
static void box_spectrum_free(
    void *ctx)
{
    box_render_spectrum_state_t *l = ctx;
    ecs_map_iter_t it = ecs_map_iter(&l->by_water);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&l->by_water);
    ecs_vec_fini_t(NULL, &l->scenes, box_spectrum_scene_t);
    ecs_os_free(l);
}

void box_render_water_spectrum_init(
    ecs_world_t *world)
{
    box_render_spectrum_state_t *l = ecs_os_calloc_t(box_render_spectrum_state_t);
    ecs_map_init(&l->by_water, NULL);
    ecs_vec_init_t(NULL, &l->scenes, box_spectrum_scene_t, 0);
    l->spectra = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "WaterSpectra" }),
        .terms = {
            { .id = ecs_id(WaterSpectrum), .inout = EcsIn },
            { .id = ecs_id(Water), .inout = EcsInOutNone },
            {
                .id = ecs_id(Scene),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsOptional
            }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.WaterSpectrum" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractSpectrum,
        .ctx = l,
        .ctx_free = box_spectrum_free
    });
}
