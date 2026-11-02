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
    out->steps = fog->steps < 4 ? 4 : fog->steps > 128 ? 128 : fog->steps;
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
