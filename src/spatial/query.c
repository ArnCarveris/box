/**
 * @file spatial/query.c
 * @brief Ray casts and overlap queries on the static and dynamic tree of one scene.
 */

#include "index.h"

/** State of one query over the trees of a scene */
typedef struct box_spatial_walk_t {
    const box_spatial_scene_t *scene; /**< The scene */
    const box_spatial_ray_t *desc;    /**< Description of a ray cast */
    box_bvh_ray_t ray;                /**< Prepared ray of a ray cast */
    float reach;                      /**< Distance beyond which hits do not count */
    box_spatial_hit_t best;           /**< Nearest hit so far */
    bool found;                       /**< A hit was found */
    ecs_vec_t *out;                   /**< Results of the query */
    int32_t count;                    /**< Results appended */
    const float *min;                 /**< Minimum corner of a box query */
    const float *max;                 /**< Maximum corner of a box query */
    const float *center;              /**< Center of a sphere query */
    float radius;                     /**< Radius of a sphere query */
} box_spatial_walk_t;

/** Test whether a ray hits an item and the filter of the ray accepts it.
 *
 * @param w The walk
 * @param item The item
 * @param t Receives the distance of the hit
 * @return true when the item counts as a hit
 */
static bool box_spatial_ray_item(
    box_spatial_walk_t *w,
    int32_t item,
    float *t)
{
    const box_spatial_item_t *it = box_spatial_item(w->scene, item);
    bool result = box_bvh_ray_box(&w->ray, it->min, it->max, w->reach, t);
    if (result && w->desc->filter) {
        result = w->desc->filter(w->desc->ctx, it->entity, t) && *t <= w->reach;
    }

    return result;
}

/** Keep the nearest item a ray hits, shortening the reach of the walk.
 *
 * @param item The item
 * @param ctx The walk
 * @return true to keep walking
 */
static bool box_spatial_visit_nearest(
    int32_t item,
    void *ctx)
{
    box_spatial_walk_t *w = ctx;
    float t = 0;
    if (box_spatial_ray_item(w, item, &t) && (!w->found || t < w->best.distance)) {
        w->best.entity = box_spatial_item(w->scene, item)->entity;
        w->best.distance = t;
        w->found = true;
        w->reach = t;
    }

    return true;
}

/** Append every item a ray hits.
 *
 * @param item The item
 * @param ctx The walk
 * @return true to keep walking
 */
static bool box_spatial_visit_all(
    int32_t item,
    void *ctx)
{
    box_spatial_walk_t *w = ctx;
    float t = 0;
    if (box_spatial_ray_item(w, item, &t)) {
        box_spatial_hit_t *hit = ecs_vec_append_t(NULL, w->out, box_spatial_hit_t);
        hit->entity = box_spatial_item(w->scene, item)->entity;
        hit->distance = t;
        w->count ++;
    }

    return true;
}

/** Append an item whose bounds overlap the box of the walk.
 *
 * @param item The item
 * @param ctx The walk
 * @return true to keep walking
 */
static bool box_spatial_visit_box(
    int32_t item,
    void *ctx)
{
    box_spatial_walk_t *w = ctx;
    const box_spatial_item_t *it = box_spatial_item(w->scene, item);
    if (box_bvh_box_box(it->min, it->max, w->min, w->max)) {
        *ecs_vec_append_t(NULL, w->out, ecs_entity_t) = it->entity;
        w->count ++;
    }

    return true;
}

/** Append an item whose bounds overlap the sphere of the walk.
 *
 * @param item The item
 * @param ctx The walk
 * @return true to keep walking
 */
static bool box_spatial_visit_sphere(
    int32_t item,
    void *ctx)
{
    box_spatial_walk_t *w = ctx;
    const box_spatial_item_t *it = box_spatial_item(w->scene, item);
    if (box_bvh_sphere_box(it->min, it->max, w->center, w->radius)) {
        *ecs_vec_append_t(NULL, w->out, ecs_entity_t) = it->entity;
        w->count ++;
    }

    return true;
}

/** Set up the walk of a ray cast.
 *
 * @param world The world
 * @param ray The ray
 * @param w The walk
 * @return false when there is nothing to hit
 */
static bool box_spatial_ray_walk(
    const ecs_world_t *world,
    const box_spatial_ray_t *ray,
    box_spatial_walk_t *w)
{
    const box_spatial_t *index = box_spatial_of(world);
    ecs_os_memset_t(w, 0, box_spatial_walk_t);
    w->scene = index ? box_spatial_scene(index, ray->scene) : NULL;
    w->desc = ray;
    w->reach = ray->max_distance > 0 ? ray->max_distance : FLT_MAX;

    return w->scene && box_bvh_ray_init(&w->ray, ray->origin, ray->dir);
}

bool box_spatial_ray(
    const ecs_world_t *world,
    const box_spatial_ray_t *ray,
    box_spatial_hit_t *hit)
{
    box_spatial_walk_t w;
    if (box_spatial_ray_walk(world, ray, &w)) {
        int32_t i;
        for (i = 0; i < (ray->ui ? BOX_SPATIAL_TREES : BOX_SPATIAL_UI); i ++) {
            box_bvh_ray(&w.scene->trees[i], &w.ray, &w.reach, box_spatial_visit_nearest, &w);
        }
    }

    if (w.found) {
        *hit = w.best;
    }

    return w.found;
}

/** Order hits by distance.
 *
 * @param a The first hit
 * @param b The second hit
 * @return Negative when a is nearer, positive when b is nearer
 */
static int box_spatial_hit_compare(
    const void *a,
    const void *b)
{
    float da = ((const box_spatial_hit_t*)a)->distance;
    float db = ((const box_spatial_hit_t*)b)->distance;

    return (da > db) - (da < db);
}

int32_t box_spatial_ray_all(
    const ecs_world_t *world,
    const box_spatial_ray_t *ray,
    ecs_vec_t *hits)
{
    box_spatial_walk_t w;
    int32_t first = ecs_vec_count(hits);
    if (box_spatial_ray_walk(world, ray, &w)) {
        int32_t i;
        w.out = hits;
        for (i = 0; i < (ray->ui ? BOX_SPATIAL_TREES : BOX_SPATIAL_UI); i ++) {
            box_bvh_ray(&w.scene->trees[i], &w.ray, &w.reach, box_spatial_visit_all, &w);
        }
    }

    if (w.count > 1) {
        qsort(ecs_vec_get_t(hits, box_spatial_hit_t, first), (size_t)w.count,
            sizeof(box_spatial_hit_t), box_spatial_hit_compare);
    }

    return w.count;
}

int32_t box_spatial_overlap_box(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 min,
    const vec3 max,
    ecs_vec_t *result)
{
    const box_spatial_t *index = box_spatial_of(world);
    box_spatial_walk_t w = {
        .scene = index ? box_spatial_scene(index, scene) : NULL,
        .out = result,
        .min = min,
        .max = max
    };
    int32_t i;
    for (i = 0; w.scene && i < BOX_SPATIAL_UI; i ++) {
        box_bvh_overlap(&w.scene->trees[i], min, max, box_spatial_visit_box, &w);
    }

    return w.count;
}

int32_t box_spatial_overlap_sphere(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 center,
    float radius,
    ecs_vec_t *result)
{
    const box_spatial_t *index = box_spatial_of(world);
    box_spatial_walk_t w = {
        .scene = index ? box_spatial_scene(index, scene) : NULL,
        .out = result,
        .center = center,
        .radius = radius
    };
    int32_t i;
    for (i = 0; w.scene && i < BOX_SPATIAL_UI; i ++) {
        box_bvh_sphere(&w.scene->trees[i], center, radius, box_spatial_visit_sphere, &w);
    }

    return w.count;
}

bool box_spatial_bounds(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t *scene,
    vec3 min,
    vec3 max)
{
    const box_spatial_t *index = box_spatial_of(world);
    const SpatialProxy *p = index ? ecs_get(world, e, SpatialProxy) : NULL;
    const box_spatial_scene_t *s = p && p->item >= 0 ? box_spatial_scene(index, p->scene) : NULL;
    if (s) {
        const box_spatial_item_t *it = box_spatial_item(s, p->item);
        glm_vec3_copy(ECS_CONST_CAST(float*, it->min), min);
        glm_vec3_copy(ECS_CONST_CAST(float*, it->max), max);
        if (scene) {
            *scene = p->scene;
        }
    }

    return s != NULL;
}

bool box_spatial_static_bounds(
    const ecs_world_t *world,
    ecs_entity_t scene,
    vec3 min,
    vec3 max)
{
    const box_spatial_t *index = box_spatial_of(world);
    const box_spatial_scene_t *s = index ? box_spatial_scene(index, scene) : NULL;
    bool result = false;
    int32_t i, count = s ? ecs_vec_count(&s->items) : 0;
    const box_spatial_item_t *items = s ? ecs_vec_first(&s->items) : NULL;
    for (i = 0; i < count; i ++) {
        const box_spatial_item_t *it = &items[i];
        if (!it->entity || it->tree != BOX_SPATIAL_STATIC) {
            continue;
        }

        if (!result) {
            glm_vec3_copy(ECS_CONST_CAST(float*, it->min), min);
            glm_vec3_copy(ECS_CONST_CAST(float*, it->max), max);
            result = true;
        } else {
            glm_vec3_minv(min, ECS_CONST_CAST(float*, it->min), min);
            glm_vec3_maxv(max, ECS_CONST_CAST(float*, it->max), max);
        }
    }

    return result;
}
