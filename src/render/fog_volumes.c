/**
 * @file render/fog_volumes.c
 * @brief Gives every lit view the FogVolumes of its scene in its frustum, for the light extract.
 *
 * The volumes are collected once per frame; a view with LightSettings.fog_volumes gets those of
 * the scene of its camera that may be in its frustum, back to front by the distance of their
 * centers from the camera, at most BOX_RENDER_FOG_VOLUMES_MAX of the nearest.
 */

#include "render.h"
#include <math.h>
#include <stdlib.h>

/** Aspect ratio the frustum of a camera that follows its surface is assumed to have at most
 * when what it sees is picked, so a shape it may see is never left out */
#define BOX_RENDER_SEES_ASPECT (4.0f)

/** A FogVolume of this frame */
typedef struct box_fog_volume_t {
    ecs_entity_t scene;     /**< Scene root of the volume, 0 for the default scene */
    box_render_fog_volume_t record; /**< Its record */
} box_fog_volume_t;

/** A fog volume a view sees, by its distance from the camera */
typedef struct box_fog_order_t {
    float distance;         /**< Distance of its center from the camera */
    int32_t index;          /**< Index of the volume in the volumes of the frame */
} box_fog_order_t;

struct box_render_fog_t {
    ecs_query_t *query;     /**< Entities with FogVolume, their transform and scene */
    ecs_vec_t fog_volumes;  /**< box_fog_volume_t, the fog volumes of this frame */
    ecs_vec_t fog_order;    /**< box_fog_order_t, scratch: the volumes of a view */
    ecs_vec_t fog_over;     /**< ecs_entity_t, the views that saw more fog volumes than they
                             *   draw in the last frame, which were warned about */
    ecs_vec_t fog_over_next; /**< ecs_entity_t, those views of this frame */
};

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_fog_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Seed of the fog volume noise */
#define BOX_FOG_NOISE_SEED (0x9E3779B9u)

/** Octaves of the fog volume noise */
#define BOX_FOG_NOISE_OCTAVES (4)

/** Bins of the histogram the fog volume noise finds its percentiles in */
#define BOX_FOG_NOISE_BINS (4096)

/** Hash an integer to a well mixed one (the finalizer of MurmurHash3).
 *
 * @param x The integer
 * @return The hash
 */
static uint32_t box_fog_hash(
    uint32_t x)
{
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;

    return x;
}

/** Get a random value in [0, 1) of a seed and three integers.
 *
 * @param seed The seed
 * @param i First integer
 * @param j Second integer
 * @param k Third integer
 * @return The value
 */
static float box_fog_random(
    uint32_t seed,
    uint32_t i,
    uint32_t j,
    uint32_t k)
{
    uint32_t h = box_fog_hash(seed ^ box_fog_hash(i ^ box_fog_hash(j ^ box_fog_hash(k))));

    return (float)(h >> 8) / 16777216.0f;
}

/** Quintic smoothstep of a fraction, with zero first and second derivatives at 0 and 1.
 *
 * @param t The fraction
 * @return The smoothed fraction
 */
static float box_fog_fade(
    float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/** Lattice cell and fraction of the texels along an axis for one octave.
 *
 * @param period Lattice cells across the texture
 * @param offset Offset of the lattice in cells
 * @param cell Receives the cell of each texel, 0 to period - 1
 * @param next Receives the cell after it, wrapped
 * @param frac Receives the smoothed fraction of each texel in its cell
 */
static void box_fog_noise_axis(
    int32_t period,
    float offset,
    int32_t *cell,
    int32_t *next,
    float *frac)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_FOG_NOISE_SIZE; i ++) {
        float p = ((float)i + 0.5f) * (float)period / (float)BOX_RENDER_FOG_NOISE_SIZE + offset;
        float c = floorf(p);
        int32_t ci = ((int32_t)c % period + period) % period;
        cell[i] = ci;
        next[i] = (ci + 1) % period;
        frac[i] = box_fog_fade(p - c);
    }
}

void box_render_fog_noise(
    uint8_t *out)
{
    const int32_t n = BOX_RENDER_FOG_NOISE_SIZE;
    int32_t count = n * n * n, o, x, y, z, i, k;
    float *values = ecs_os_calloc_n(float, count);
    float *lattice = ecs_os_malloc_n(float, 16 * 16 * 16);
    int32_t *histogram = ecs_os_calloc_n(int32_t, BOX_FOG_NOISE_BINS), total;
    const double share[3] = { 0.01, 0.5, 0.99 };
    float amplitude = 1.0f, lo, hi, scale, at[3];
    int32_t cx[BOX_RENDER_FOG_NOISE_SIZE], nx[BOX_RENDER_FOG_NOISE_SIZE];
    int32_t cy[BOX_RENDER_FOG_NOISE_SIZE], ny[BOX_RENDER_FOG_NOISE_SIZE];
    int32_t cz[BOX_RENDER_FOG_NOISE_SIZE], nz[BOX_RENDER_FOG_NOISE_SIZE];
    float fx[BOX_RENDER_FOG_NOISE_SIZE], fy[BOX_RENDER_FOG_NOISE_SIZE];
    float fz[BOX_RENDER_FOG_NOISE_SIZE];
    for (o = 0; o < BOX_FOG_NOISE_OCTAVES; o ++) {
        int32_t period = 2 << o;
        uint32_t seed = box_fog_hash(BOX_FOG_NOISE_SEED + (uint32_t)o);
        for (z = 0; z < period; z ++) {
            for (y = 0; y < period; y ++) {
                for (x = 0; x < period; x ++) {
                    lattice[(z * period + y) * period + x] = box_fog_random(
                        seed, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                }
            }
        }

        box_fog_noise_axis(period, box_fog_random(seed, 1u << 20, 0, 0) * (float)period,
            cx, nx, fx);
        box_fog_noise_axis(period, box_fog_random(seed, 0, 1u << 20, 0) * (float)period,
            cy, ny, fy);
        box_fog_noise_axis(period, box_fog_random(seed, 0, 0, 1u << 20) * (float)period,
            cz, nz, fz);
        for (z = 0; z < n; z ++) {
            const float *z0 = &lattice[cz[z] * period * period];
            const float *z1 = &lattice[nz[z] * period * period];
            for (y = 0; y < n; y ++) {
                const float *r00 = &z0[cy[y] * period], *r01 = &z0[ny[y] * period];
                const float *r10 = &z1[cy[y] * period], *r11 = &z1[ny[y] * period];
                float *row = &values[(z * n + y) * n];
                for (x = 0; x < n; x ++) {
                    int32_t a = cx[x], b = nx[x];
                    float u = fx[x];
                    float v00 = r00[a] + u * (r00[b] - r00[a]);
                    float v01 = r01[a] + u * (r01[b] - r01[a]);
                    float v10 = r10[a] + u * (r10[b] - r10[a]);
                    float v11 = r11[a] + u * (r11[b] - r11[a]);
                    float v0 = v00 + fy[y] * (v01 - v00);
                    float v1 = v10 + fy[y] * (v11 - v10);
                    row[x] += amplitude * (v0 + fz[z] * (v1 - v0));
                }
            }
        }

        amplitude *= 0.5f;
    }

    lo = hi = values[0];
    for (i = 1; i < count; i ++) {
        lo = glm_min(lo, values[i]);
        hi = glm_max(hi, values[i]);
    }

    scale = hi > lo ? (float)BOX_FOG_NOISE_BINS / (hi - lo) : 0.0f;
    for (i = 0; i < count; i ++) {
        int32_t bin = (int32_t)((values[i] - lo) * scale);
        histogram[bin < BOX_FOG_NOISE_BINS ? bin : BOX_FOG_NOISE_BINS - 1] ++;
    }

    for (i = 0, total = 0, k = 0; k < 3; i ++) {
        total += histogram[i];
        while (k < 3 && total >= (int32_t)((double)count * share[k])) {
            at[k ++] = lo + ((float)i + 0.5f) / (float)BOX_FOG_NOISE_BINS * (hi - lo);
        }
    }

    for (i = 0; i < count; i ++) {
        float v = values[i] <= at[1]
            ? 0.5f - 0.5f * (at[1] - values[i]) / glm_max(at[1] - at[0], 1e-6f)
            : 0.5f + 0.5f * (values[i] - at[1]) / glm_max(at[2] - at[1], 1e-6f);
        out[i] = (uint8_t)roundf(255.0f * glm_clamp(v, 0.0f, 1.0f));
    }

    ecs_os_free(histogram);
    ecs_os_free(lattice);
    ecs_os_free(values);
}

/** Write the record of a FogVolume: its box placed by the rotation and position of the world
 * transform, scale ignored, and its values clamped to their ranges.
 *
 * @param fog The FogVolume
 * @param transform World transform of the entity, NULL for the origin
 * @param out Receives the record
 */
static void box_fog_record(
    const FogVolume *fog,
    const WorldTransform *transform,
    box_render_fog_volume_t *out)
{
    int32_t k;
    ecs_os_memset_t(out, 0, box_render_fog_volume_t);
    for (k = 0; k < 3; k ++) {
        vec3 axis = {0, 0, 0};
        axis[k] = 1.0f;
        if (transform) {
            vec3 column;
            glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[k]), column);
            if (glm_vec3_norm(column) > 1e-12f) {
                glm_vec3_normalize_to(column, axis);
            }
        }

        glm_vec3_copy(axis, out->axes[k]);
        out->extent[k] = glm_max(fog->extent[k], 0.0f);
    }

    if (transform) {
        glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[3]), out->center);
    }

    out->density = glm_max(fog->density, 0.0f);
    out->absorption = glm_clamp(fog->absorption, 0.0f, 1.0f);
    out->asymmetry = glm_clamp(fog->asymmetry, -0.9f, 0.9f);
    out->edge_fade = glm_max(fog->edge_fade, 0.0f);
    out->sun_inscatter = glm_max(fog->sun_inscatter, 0.0f);
    out->ambient_inscatter = glm_max(fog->ambient_inscatter, 0.0f);
    out->light_inscatter = glm_max(fog->light_inscatter, 0.0f);
    out->steps = fog->steps < 4 ? 4 : fog->steps > 128 ? 128 : fog->steps;
    out->noise_strength = glm_clamp(fog->noise_strength, 0.0f, 1.0f);
    out->noise_scale = glm_max(fog->noise_scale, BOX_RENDER_FOG_NOISE_SCALE_MIN);
    glm_vec3_copy(ECS_CONST_CAST(float*, fog->wind), out->wind);
    out->color[0] = box_fog_linear(fog->color.r);
    out->color[1] = box_fog_linear(fog->color.g);
    out->color[2] = box_fog_linear(fog->color.b);
}

/** Collect the FogVolume of every entity with its scene.
 *
 * @param world The world
 * @param l The fog state
 */
static void box_fog_collect(
    ecs_world_t *world,
    box_render_fog_t *l)
{
    ecs_vec_clear(&l->fog_volumes);
    ecs_iter_t it = ecs_query_iter(world, l->query);
    while (ecs_query_next(&it)) {
        const FogVolume *fogs = ecs_field(&it, FogVolume, 0);
        const WorldTransform *transforms = ecs_field(&it, WorldTransform, 1);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_fog_volume_t *f = ecs_vec_append_t(NULL, &l->fog_volumes, box_fog_volume_t);
            f->scene = box_render_scene_field(&it, 2, i);
            box_fog_record(&fogs[i], transforms ? &transforms[i] : NULL, &f->record);
        }
    }
}

/** Order fog volumes by their distance from the camera, nearest first, then by index.
 *
 * @param a The first volume
 * @param b The second volume
 * @return Negative when a is nearer, positive when b is
 */
static int box_fog_cmp(
    const void *a,
    const void *b)
{
    const box_fog_order_t *x = a, *y = b;
    int result = (x->distance > y->distance) - (x->distance < y->distance);
    if (!result) {
        result = (x->index > y->index) - (x->index < y->index);
    }

    return result;
}

/** Check whether a view was over the fog volume limit in the last frame.
 *
 * @param list The views, ecs_entity_t
 * @param view The view
 * @return Whether the list holds the view
 */
static bool box_fog_listed(
    const ecs_vec_t *list,
    ecs_entity_t view)
{
    const ecs_entity_t *views = ecs_vec_first_t(list, ecs_entity_t);
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(list) && !result; i ++) {
        result = views[i] == view;
    }

    return result;
}

/** Check whether a sphere or a box may be in the frustum of a view: whether it is on the inner
 * side of every plane, by the radius of the sphere or the extent of the box along the normal.
 *
 * @param v The view
 * @param center Center of the shape
 * @param radius Radius of the sphere, used without axes
 * @param axes Unit axes of the box, NULL for a sphere
 * @param extent Half size of the box along its axes
 * @return Whether the shape may be in the frustum
 */
static bool box_view_sees(
    const box_render_view_t *v,
    const float *center,
    float radius,
    const vec4 *axes,
    const float *extent)
{
    const Camera *c = &v->camera_data;
    bool sides = !c->ortho || c->ortho_scale <= 0, result = true;
    float aspect = c->aspect_ratio > 0 ? c->aspect_ratio : BOX_RENDER_SEES_ASPECT;
    mat4 proj, m;
    int32_t i, j, k;
    box_camera_projection(c, aspect, 1.0f, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), m);
    for (i = sides ? 0 : 4; i < 6 && result; i ++) {
        vec4 plane;
        for (j = 0; j < 4; j ++) {
            if (i < 4) {
                plane[j] = m[j][3] + ((i & 1) ? -1.0f : 1.0f) * m[j][i / 2];
            } else {
                plane[j] = i == 4 ? m[j][3] - m[j][2] : m[j][2];
            }
        }

        float len = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
        if (len > 1e-12f) {
            float d = (plane[0] * center[0] + plane[1] * center[1] + plane[2] * center[2] +
                plane[3]) / len;
            float r = radius;
            if (axes) {
                r = 0;
                for (k = 0; k < 3; k ++) {
                    r += extent[k] * fabsf(plane[0] * axes[k][0] + plane[1] * axes[k][1] +
                        plane[2] * axes[k][2]) / len;
                }
            }

            result = d >= -r;
        }
    }

    return result;
}

void box_render_fog_views(
    ecs_world_t *world,
    box_render_fog_t *l,
    box_render_packet_t *p,
    box_render_lights_stats_t *stats)
{
    const box_fog_volume_t *fogs = NULL;
    int32_t i, j, count = ecs_vec_count(&p->views), fog_count = 0;
    ecs_vec_clear(&l->fog_over_next);
    box_fog_collect(world, l);
    fogs = ecs_vec_first_t(&l->fog_volumes, box_fog_volume_t);
    fog_count = ecs_vec_count(&l->fog_volumes);
    for (i = 0; i < count; i ++) {
        box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const View *view = v->view && ecs_is_alive(world, v->view)
            ? ecs_get(world, v->view, View)
            : NULL;
        v->fog_first = ecs_vec_count(&p->fog_volumes);
        v->fog_count = 0;
        if (!fog_count || !view || !view->lighting.enabled || !view->lighting.fog_volumes ||
            !box_camera_has_depth(&v->camera_data))
        {
            continue;
        }

        const float *eye = v->camera_transform[3];
        ecs_vec_clear(&l->fog_order);
        for (j = 0; j < fog_count; j ++) {
            const box_fog_volume_t *f = &fogs[j];
            if (f->scene != v->scene || !box_view_sees(v, f->record.center, 0,
                ECS_CONST_CAST(vec4*, f->record.axes), f->record.extent))
            {
                continue;
            }

            box_fog_order_t *o = ecs_vec_append_t(
                NULL, &l->fog_order, box_fog_order_t);
            o->distance = glm_vec3_distance(ECS_CONST_CAST(float*, eye),
                ECS_CONST_CAST(float*, f->record.center));
            o->index = j;
        }

        int32_t seen = ecs_vec_count(&l->fog_order);
        box_fog_order_t *order = ecs_vec_first_t(&l->fog_order, box_fog_order_t);
        if (seen > 1) {
            qsort(order, (size_t)seen, sizeof(box_fog_order_t), box_fog_cmp);
        }

        if (seen > BOX_RENDER_FOG_VOLUMES_MAX) {
            if (!box_fog_listed(&l->fog_over, v->view)) {
                char *path = ecs_get_path(world, v->view);
                ecs_warn("view '%s' sees %d fog volumes, it draws the nearest %d", path, seen,
                    BOX_RENDER_FOG_VOLUMES_MAX);
                ecs_os_free(path);
            }

            *ecs_vec_append_t(NULL, &l->fog_over_next, ecs_entity_t) = v->view;
            stats->fog_dropped += seen - BOX_RENDER_FOG_VOLUMES_MAX;
            seen = BOX_RENDER_FOG_VOLUMES_MAX;
        }

        for (j = seen - 1; j >= 0; j --) {
            *ecs_vec_append_t(NULL, &p->fog_volumes, box_render_fog_volume_t) =
                fogs[order[j].index].record;
        }

        v->fog_count = seen;
        stats->fog_volumes += seen;
    }

    ecs_vec_t swap = l->fog_over;
    l->fog_over = l->fog_over_next;
    l->fog_over_next = swap;
}


box_render_fog_t* box_render_fog_new(
    ecs_world_t *world)
{
    box_render_fog_t *result = ecs_os_calloc_t(box_render_fog_t);
    ecs_vec_init_t(NULL, &result->fog_volumes, box_fog_volume_t, 0);
    ecs_vec_init_t(NULL, &result->fog_order, box_fog_order_t, 0);
    ecs_vec_init_t(NULL, &result->fog_over, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &result->fog_over_next, ecs_entity_t, 0);
    result->query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "RenderFogVolumes" }),
        .terms = {
            { .id = ecs_id(FogVolume), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
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

    return result;
}

void box_render_fog_free(
    box_render_fog_t *fog)
{
    if (fog) {
        ecs_vec_fini_t(NULL, &fog->fog_volumes, box_fog_volume_t);
        ecs_vec_fini_t(NULL, &fog->fog_order, box_fog_order_t);
        ecs_vec_fini_t(NULL, &fog->fog_over, ecs_entity_t);
        ecs_vec_fini_t(NULL, &fog->fog_over_next, ecs_entity_t);
        ecs_os_free(fog);
    }
}

bool box_render_view_sees(
    const box_render_view_t *v,
    const float *center,
    float radius)
{
    return box_view_sees(v, center, radius, NULL, NULL);
}
