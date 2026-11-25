/**
 * @file spatial/query.c
 * @brief Ray casts and overlap queries on the static and dynamic tree of one scene.
 *
 * The leaf of a mover answers for the mover, unless a ray asks for shapes: it then walks the
 * tree over its shapes in its frame, placed by the frame, and tests the shapes it reaches from
 * their world transforms. A mover with few shapes tests them one by one.
 */

#include "index.h"
#include "../terrain/terrain.h"

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
    const ecs_world_t *world;         /**< The world */
    const box_spatial_t *index;       /**< The index */
    const box_spatial_mover_t *mover; /**< Mover whose tree a walk is in */
} box_spatial_walk_t;

/** Test whether a ray hits the bounds of an entity and the filter of the ray accepts it.
 *
 * @param w The walk
 * @param e The entity
 * @param min Minimum corner of its bounds
 * @param max Maximum corner of its bounds
 * @param t Receives the distance of the hit
 * @return true when the entity counts as a hit
 */
static bool box_spatial_ray_bounds(
    box_spatial_walk_t *w,
    ecs_entity_t e,
    const vec3 min,
    const vec3 max,
    float *t)
{
    bool result = !box_terrain_is_tile(w->world, e) &&
        box_bvh_ray_box(&w->ray, min, max, w->reach, t);
    if (result && w->desc->filter) {
        result = w->desc->filter(w->desc->ctx, e, t) && *t <= w->reach;
    }

    return result;
}

/** Keep a hit as the nearest one or append it, by the kind of walk.
 *
 * @param w The walk
 * @param e The entity hit
 * @param t Distance of the hit
 */
static void box_spatial_ray_keep(
    box_spatial_walk_t *w,
    ecs_entity_t e,
    float t)
{
    if (w->out) {
        box_spatial_hit_t *hit = ecs_vec_append_t(NULL, w->out, box_spatial_hit_t);
        hit->entity = e;
        hit->distance = t;
        w->count ++;
    } else if (!w->found || t < w->best.distance) {
        w->best.entity = e;
        w->best.distance = t;
        w->found = true;
        w->reach = t;
    }
}

/** Test a shape under a mover against a ray, from its world transform, and keep it on a hit.
 *
 * @param w The walk
 * @param member The shape
 */
static void box_spatial_ray_member(
    box_spatial_walk_t *w,
    const box_spatial_member_t *member)
{
    vec3 min, max;
    float t = 0;
    if (member->entity && !member->disabled) {
        box_spatial_member_bounds(w->world, member, min, max);
        if (box_spatial_ray_bounds(w, member->entity, min, max, &t)) {
            box_spatial_ray_keep(w, member->entity, t);
        }
    }
}

/** Test a shape the walk of the tree of a mover reached against a ray.
 *
 * @param item The index of the shape in the mover
 * @param ctx The walk
 * @return true to keep walking
 */
static bool box_spatial_visit_member(
    int32_t item,
    void *ctx)
{
    box_spatial_walk_t *w = ctx;
    box_spatial_ray_member(w, ecs_vec_get_t(&w->mover->members, box_spatial_member_t, item));

    return true;
}

/** Test the shapes under a mover whose leaf a ray hit, nearest first through the tree of the
 * mover, so a filter that shortens the reach skips the rest, or one by one without a tree.
 *
 * @param w The walk
 * @param mover The slot of the mover
 */
static void box_spatial_ray_mover(
    box_spatial_walk_t *w,
    int32_t mover)
{
    const box_bvh_t *tree = box_spatial_mover_tree(
        ECS_CONST_CAST(box_spatial_t*, w->index), mover);
    const box_spatial_mover_t *m = box_spatial_mover(w->index, mover);
    const box_spatial_member_t *members = ecs_vec_first(&m->members);
    int32_t i, count = ecs_vec_count(&m->members);
    if (tree) {
        w->mover = m;
        box_bvh_ray_placed(tree, &m->pose[0][0], &w->ray, &w->reach,
            box_spatial_visit_member, w);
    } else {
        for (i = 0; i < count; i ++) {
            box_spatial_ray_member(w, &members[i]);
        }
    }
}

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
    bool result = false;
    if (it->disabled) {
        goto done;
    }

    if (it->mover >= 0 && w->desc->shapes) {
        if (box_bvh_ray_box(&w->ray, it->min, it->max, w->reach, t)) {
            box_spatial_ray_mover(w, it->mover);
        }
    } else {
        result = box_spatial_ray_bounds(w, it->entity, it->min, it->max, t);
    }

done:
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
    if (box_spatial_ray_item(w, item, &t)) {
        box_spatial_ray_keep(w, box_spatial_item(w->scene, item)->entity, t);
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
        box_spatial_ray_keep(w, box_spatial_item(w->scene, item)->entity, t);
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
    if (!it->disabled && box_bvh_box_box(it->min, it->max, w->min, w->max)) {
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
    if (!it->disabled && box_bvh_sphere_box(it->min, it->max, w->center, w->radius)) {
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
    w->world = world;
    w->index = index;
    w->desc = ray;
    w->reach = ray->max_distance > 0 ? ray->max_distance : FLT_MAX;

    return w->scene && box_bvh_ray_init(&w->ray, ray->origin, ray->dir);
}

/** Hit the height fields of the terrains of the scene of a ray, the terrain entity standing
 * for its tiles, after the filter of the ray accepts it.
 *
 * @param world The world
 * @param ray The ray
 * @param w The walk
 */
static void box_spatial_ray_terrain(
    const ecs_world_t *world,
    const box_spatial_ray_t *ray,
    box_spatial_walk_t *w)
{
    ecs_entity_t terrain = 0;
    float t = 0;
    if (box_terrain_ray(world, ray->scene, ray->origin, ray->dir,
        w->reach < FLT_MAX ? w->reach : 0, &terrain, &t) &&
        (!ray->filter || ray->filter(ray->ctx, terrain, &t)) && t <= w->reach)
    {
        box_spatial_ray_keep(w, terrain, t);
    }
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

    box_spatial_ray_terrain(world, ray, &w);

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

    w.out = hits;
    box_spatial_ray_terrain(world, ray, &w);

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

/** Stop the walk at the first item that is not disabled.
 *
 * @param item The item
 * @param ctx The walk, which counts the item
 * @return false to stop the walk
 */
static bool box_spatial_visit_any(
    int32_t item,
    void *ctx)
{
    box_spatial_walk_t *w = ctx;
    w->count += !box_spatial_item(w->scene, item)->disabled;

    return !w->count;
}

bool box_spatial_movers_overlap_box(
    ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 min,
    const vec3 max)
{
    const SpatialIndex *si = ecs_singleton_get(world, SpatialIndex);
    box_spatial_walk_t w = {
        .scene = si && si->index ? box_spatial_scene(si->index, scene) : NULL,
        .min = min,
        .max = max
    };
    if (w.scene && w.scene->trees[BOX_SPATIAL_DYNAMIC].leaves) {
        box_spatial_of(world);
        box_bvh_overlap(&w.scene->trees[BOX_SPATIAL_DYNAMIC], min, max,
            box_spatial_visit_any, &w);
    }

    return w.count > 0;
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
    const box_spatial_mover_t *m = NULL;
    int32_t item = p ? p->item : -1;
    ecs_entity_t in = p ? p->scene : 0;
    bool result = false;
    if (p && p->mover >= 0) {
        m = box_spatial_mover(index, p->mover);
        box_spatial_member_bounds(world,
            ecs_vec_get_t(&m->members, box_spatial_member_t, p->member), min, max);
        in = m->scene;
        result = m->item >= 0;
    } else if (!p && index && box_spatial_mover_slot(index, e) >= 0) {
        m = box_spatial_mover(index, box_spatial_mover_slot(index, e));
        item = m->item;
        in = m->scene;
    }

    const box_spatial_scene_t *s = !result && item >= 0 ? box_spatial_scene(index, in) : NULL;
    if (s) {
        const box_spatial_item_t *it = box_spatial_item(s, item);
        glm_vec3_copy(ECS_CONST_CAST(float*, it->min), min);
        glm_vec3_copy(ECS_CONST_CAST(float*, it->max), max);
        result = true;
    }

    if (result && scene) {
        *scene = in;
    }

    return result;
}

/** State of the expansion of a mover into the shapes under it that overlap a box */
typedef struct box_spatial_expand_t {
    const ecs_world_t *world;         /**< The world */
    const box_spatial_mover_t *mover; /**< The mover */
    const float *min;                 /**< Minimum corner of the box */
    const float *max;                 /**< Maximum corner of the box */
    ecs_vec_t *out;                   /**< Receives the shapes */
    int32_t count;                    /**< Shapes appended */
} box_spatial_expand_t;

/** Append a shape under a mover when its world bounds overlap the box of an expansion.
 *
 * @param item The index of the shape in the mover
 * @param ctx The expansion
 * @return true to keep walking
 */
static bool box_spatial_visit_expand(
    int32_t item,
    void *ctx)
{
    box_spatial_expand_t *x = ctx;
    const box_spatial_member_t *member = ecs_vec_get_t(
        &x->mover->members, box_spatial_member_t, item);
    vec3 lo, hi;
    if (member->entity && !member->disabled) {
        box_spatial_member_bounds(x->world, member, lo, hi);
        if (box_bvh_box_box(lo, hi, x->min, x->max)) {
            *ecs_vec_append_t(NULL, x->out, ecs_entity_t) = member->entity;
            x->count ++;
        }
    }

    return true;
}

/** Collect a shape the walk of the tree of a mover reached, by its index in the mover.
 *
 * @param item The index of the shape in the mover
 * @param ctx The vector of indices
 * @return true to keep walking
 */
static bool box_spatial_visit_candidate(
    int32_t item,
    void *ctx)
{
    *ecs_vec_append_t(NULL, ctx, int32_t) = item;

    return true;
}

/** Order indices of shapes in a mover.
 *
 * @param a The first index
 * @param b The second index
 * @return Negative when a comes first, positive when b does
 */
static int box_spatial_item_compare(
    const void *a,
    const void *b)
{
    int32_t ia = *(const int32_t*)a, ib = *(const int32_t*)b;

    return (ia > ib) - (ia < ib);
}

int32_t box_spatial_expand_box(
    const ecs_world_t *world,
    ecs_entity_t e,
    const vec3 min,
    const vec3 max,
    ecs_vec_t *result)
{
    const box_spatial_t *index = box_spatial_of(world);
    int32_t slot = index ? box_spatial_mover_slot(index, e) : -1;
    int32_t i, count = 0;
    vec3 lo, hi;
    if (slot < 0) {
        if (box_spatial_bounds(world, e, NULL, lo, hi) && box_bvh_box_box(lo, hi, min, max)) {
            *ecs_vec_append_t(NULL, result, ecs_entity_t) = e;
            count ++;
        }

        goto done;
    }

    const box_bvh_t *tree = box_spatial_mover_tree(ECS_CONST_CAST(box_spatial_t*, index), slot);
    const box_spatial_mover_t *m = box_spatial_mover(index, slot);
    box_spatial_expand_t x = {
        .world = world, .mover = m, .min = min, .max = max, .out = result
    };
    if (tree) {
        ecs_vec_t found;
        ecs_vec_init_t(NULL, &found, int32_t, 0);
        box_bvh_overlap_placed(tree, &m->pose[0][0], min, max, box_spatial_visit_candidate,
            &found);
        int32_t *items = ecs_vec_first(&found);
        if (ecs_vec_count(&found) > 1) {
            qsort(items, (size_t)ecs_vec_count(&found), sizeof(int32_t),
                box_spatial_item_compare);
        }

        for (i = 0; i < ecs_vec_count(&found); i ++) {
            box_spatial_visit_expand(items[i], &x);
        }

        ecs_vec_fini_t(NULL, &found, int32_t);
    } else {
        for (i = 0; i < ecs_vec_count(&m->members); i ++) {
            box_spatial_visit_expand(i, &x);
        }
    }

    count = x.count;
done:
    return count;
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
        if (!it->entity || it->disabled || it->tree != BOX_SPATIAL_STATIC) {
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

/** Walk of the boxes of a tree for a box_spatial_box_visit_t */
typedef struct box_spatial_tree_walk_t {
    box_spatial_box_visit_t visit; /**< Receives the boxes */
    void *ctx;                     /**< Context passed to visit */
} box_spatial_tree_walk_t;

/** Pass a box of a tree on to the visit of a tree walk.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param depth Depth of the box
 * @param item The item of a leaf, BOX_BVH_NULL for a node
 * @param ctx The tree walk
 * @return false to skip the children of the node
 */
static bool box_spatial_tree_box(
    const vec3 min,
    const vec3 max,
    int32_t depth,
    int32_t item,
    void *ctx)
{
    const box_spatial_tree_walk_t *w = ctx;

    return w->visit(min, max, depth, item != BOX_BVH_NULL, w->ctx);
}

int32_t box_spatial_tree_walk(
    const ecs_world_t *world,
    ecs_entity_t scene,
    int32_t tree,
    box_spatial_box_visit_t visit,
    void *ctx)
{
    const box_spatial_t *index = box_spatial_of(world);
    const box_spatial_scene_t *s = index ? box_spatial_scene(index, scene) : NULL;
    box_spatial_tree_walk_t w = { .visit = visit, .ctx = ctx };
    int32_t result = 0;
    if (s && tree >= 0 && tree < BOX_SPATIAL_TREES) {
        result = box_bvh_walk(&s->trees[tree], box_spatial_tree_box, &w);
    }

    return result;
}

/** Walk of the trees of the movers of a scene */
typedef struct box_spatial_mover_walk_t {
    const box_spatial_mover_t *mover; /**< The mover walked */
    box_spatial_box_visit_t visit;    /**< Receives the boxes */
    void *ctx;                        /**< Context passed to visit */
    int32_t count;                    /**< Boxes visited */
} box_spatial_mover_walk_t;

/** Pass a box of the tree of a mover on to the visit of a mover walk, unless it is the leaf of
 * a shape that is disabled or left the mover.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param depth Depth of the box
 * @param item The member of a leaf, BOX_BVH_NULL for a node
 * @param ctx The mover walk
 * @return false to skip the children of the node
 */
static bool box_spatial_mover_box(
    const vec3 min,
    const vec3 max,
    int32_t depth,
    int32_t item,
    void *ctx)
{
    box_spatial_mover_walk_t *w = ctx;
    bool result = true;
    if (item != BOX_BVH_NULL) {
        const box_spatial_member_t *member = ecs_vec_get_t(
            &w->mover->members, box_spatial_member_t, item);
        if (!member->entity || member->disabled) {
            goto done;
        }
    }

    w->count ++;
    result = w->visit(min, max, depth, item != BOX_BVH_NULL, w->ctx);
done:
    return result;
}

int32_t box_spatial_mover_walk(
    const ecs_world_t *world,
    ecs_entity_t scene,
    box_spatial_mover_visit_t mover_visit,
    box_spatial_box_visit_t visit,
    void *ctx)
{
    box_spatial_t *index = ECS_CONST_CAST(box_spatial_t*, box_spatial_of(world));
    const box_spatial_scene_t *s = index ? box_spatial_scene(index, scene) : NULL;
    box_spatial_mover_walk_t w = { .visit = visit, .ctx = ctx };
    int32_t i, count = index ? ecs_vec_count(&index->movers) : 0;
    if (!s) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        const box_spatial_mover_t *m = box_spatial_mover(index, i);
        if (!m->entity || m->item < 0 || m->scene != s->entity) {
            continue;
        }

        const box_bvh_t *tree = box_spatial_mover_tree(index, i);
        if (!tree || !mover_visit(m->entity, &m->pose[0][0], ctx)) {
            continue;
        }

        w.mover = m;
        box_bvh_walk(tree, box_spatial_mover_box, &w);
    }

done:
    return w.count;
}
