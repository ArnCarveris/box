/**
 * @file collision/index.c
 * @brief Two level spatial index over root bounds, colliders and movers.
 *
 * The first level is a hierarchy over root bounds and colliders outside a root;
 * the second is the list of colliders per root, rebuilt when its subtree
 * changes. Entities that move every frame live in a hierarchy rebuilt per flush.
 */

#include "collision_internal.h"

/** State a query walk hands to its leaf callback */
typedef struct box_index_ctx_t {
    const ecs_world_t *world;  /**< The world */
    const box_index_t *index;  /**< The index */
    box_bvh_query_t *q;        /**< The walk, whose reach a ray hit shortens */
    ecs_vec_t *out;            /**< Entities the query collects, NULL for a ray */
    box_ray_hit_t *hit;        /**< Nearest hit so far, NULL for a set query */
    int32_t count;             /**< Entities the query collected */
    box_collision_filter_t filter; /**< Accepts the colliders a ray may hit, NULL for all */
    void *filter_ctx;          /**< Context of the filter */
} box_index_ctx_t;

void box_index_init(
    box_index_t *index)
{
    box_bvh_init(&index->statics);
    box_bvh_init(&index->dynamics);
    ecs_map_init(&index->proxies, NULL);
    ecs_map_init(&index->lists, NULL);
    ecs_vec_init_t(NULL, &index->movers, box_index_dyn_t, 0);
    index->margin = 0.25f;
    index->enabled = true;
}

void box_index_fini(
    box_index_t *index)
{
    ecs_map_iter_t mit = ecs_map_iter(&index->lists);
    while (ecs_map_next(&mit)) {
        ecs_vec_t *list = ecs_map_ptr(&mit);
        ecs_vec_fini_t(NULL, list, ecs_entity_t);
    }

    ecs_map_fini(&index->lists);
    ecs_map_fini(&index->proxies);
    ecs_vec_fini_t(NULL, &index->movers, box_index_dyn_t);
    box_bvh_fini(&index->statics);
    box_bvh_fini(&index->dynamics);
}

ecs_vec_t* box_index_list(
    box_index_t *index,
    ecs_entity_t root)
{
    ecs_vec_t *result = ecs_map_get_deref(&index->lists, ecs_vec_t, root);
    if (!result) {
        result = ecs_map_ensure_alloc_t(&index->lists, ecs_vec_t, root);
        ecs_vec_init_t(NULL, result, ecs_entity_t, 0);
    }

    return result;
}

void box_index_set(
    box_index_t *index,
    ecs_entity_t e,
    vec3 min,
    vec3 max)
{
    ecs_map_val_t *slot = ecs_map_get(&index->proxies, e);
    if (slot) {
        box_bvh_move(&index->statics, (int32_t)slot[0] - 1, min, max, index->margin);
        goto done;
    }

    int32_t proxy = box_bvh_insert(&index->statics, min, max, index->margin, e);
    ecs_map_ensure(&index->proxies, e)[0] = (ecs_map_val_t)(proxy + 1);
done:
    return;
}

void box_index_unset(
    box_index_t *index,
    ecs_entity_t e)
{
    ecs_map_val_t *slot = ecs_map_get(&index->proxies, e);
    if (slot) {
        box_bvh_remove(&index->statics, (int32_t)slot[0] - 1);
        ecs_map_remove(&index->proxies, e);
    }

    ecs_vec_t *list = ecs_map_get_deref(&index->lists, ecs_vec_t, e);
    if (list) {
        ecs_vec_fini_t(NULL, list, ecs_entity_t);
        ecs_map_remove_free(&index->lists, e);
    }
}

/** Test whether two boxes overlap.
 *
 * @param amin Minimum corner of the first box
 * @param amax Maximum corner of the first box
 * @param bmin Minimum corner of the second box
 * @param bmax Maximum corner of the second box
 * @return true when the boxes overlap
 */
static bool box_index_overlap(
    vec3 amin,
    vec3 amax,
    vec3 bmin,
    vec3 bmax)
{
    return amin.x <= bmax.x && amax.x >= bmin.x && amin.y <= bmax.y &&
        amax.y >= bmin.y && amin.z <= bmax.z && amax.z >= bmin.z;
}

/** Test whether a box is not fully outside a plane bounded volume.
 *
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param f The planes
 * @return true when the box is inside or crosses the volume
 */
static bool box_index_in_frustum(
    vec3 min,
    vec3 max,
    const box_frustum_planes_t *f)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < BOX_FRUSTUM_PLANES; i ++) {
        vec4 p = f->planes[i];
        vec3 far = vec3_new(p.x > 0 ? max.x : min.x, p.y > 0 ? max.y : min.y,
            p.z > 0 ? max.z : min.z);
        if (p.x * far.x + p.y * far.y + p.z * far.z + p.w < 0.0f) {
            result = false;
            break;
        }
    }

    return result;
}

/** Run the test of a query against one collider and keep what it finds.
 *
 * @param ctx The query state
 * @param e The entity of the collider
 * @param c The collider
 */
static void box_index_test(
    box_index_ctx_t *ctx,
    ecs_entity_t e,
    const Collider *c)
{
    box_bvh_query_t *q = ctx->q;
    bool keep = false;
    float dist = 0;
    if (q->kind == BoxQueryRay) {
        if (box_collider_ray(c, q->origin, q->dir, q->max_dist, &dist) &&
            (dist < ctx->hit->distance ||
             (dist == ctx->hit->distance && e < ctx->hit->entity)) &&
            (!ctx->filter || ctx->filter(ctx->world, e, ctx->filter_ctx)))
        {
            ctx->hit->entity = e;
            ctx->hit->distance = dist;
            ctx->hit->point = vec3_add(q->origin, vec3_scale(q->dir, dist));
            q->max_dist = dist;
            ctx->count = 1;
        }

        goto done;
    }

    if (q->kind == BoxQuerySphere) {
        keep = box_collider_sphere(c, q->origin, q->radius);
    } else {
        vec3 min, max;
        box_collider_bounds(c, &min, &max);
        keep = q->kind == BoxQueryAabb
            ? box_index_overlap(min, max, q->min, q->max)
            : box_index_in_frustum(min, max, q->frustum);
    }

    if (keep) {
        ecs_vec_append_t(NULL, ctx->out, ecs_entity_t)[0] = e;
        ctx->count ++;
    }

done:
    return;
}

/** Test one leaf of the static hierarchy, which is a root or a loose collider.
 *
 * @param user The entity of the leaf
 * @param arg The query state
 * @return true to keep walking
 */
static bool box_index_on_static(
    uint64_t user,
    void *arg)
{
    box_index_ctx_t *ctx = arg;
    ecs_entity_t e = (ecs_entity_t)user;
    ecs_map_t *lists = ECS_CONST_CAST(ecs_map_t*, &ctx->index->lists);
    ecs_vec_t *list = ecs_map_get_deref(lists, ecs_vec_t, e);
    if (!list) {
        const Collider *c = ecs_get(ctx->world, e, Collider);
        if (c) {
            box_index_test(ctx, e, c);
        }

        goto done;
    }

    int32_t i, count = ecs_vec_count(list);
    ecs_entity_t *entities = ecs_vec_first_t(list, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        const Collider *c = ecs_get(ctx->world, entities[i], Collider);
        if (c) {
            box_index_test(ctx, entities[i], c);
        }
    }

done:
    return true;
}

/** Test one leaf of the hierarchy of movers.
 *
 * @param user The place of the mover in the vector of the index
 * @param arg The query state
 * @return true to keep walking
 */
static bool box_index_on_dynamic(
    uint64_t user,
    void *arg)
{
    box_index_ctx_t *ctx = arg;
    ecs_vec_t *movers = ECS_CONST_CAST(ecs_vec_t*, &ctx->index->movers);
    box_index_dyn_t *dyn = ecs_vec_get_t(movers, box_index_dyn_t, (int32_t)user);
    box_index_test(ctx, dyn->entity, &dyn->collider);

    return true;
}

/** Get the index of a world.
 *
 * @param world The world
 * @return The index, NULL when box.collision is not imported
 */
static const box_index_t* box_index_of(
    const ecs_world_t *world)
{
    const CollisionState *cs = ecs_singleton_get(world, CollisionState);

    return cs && cs->state ? &cs->state->index : NULL;
}

/** Walk both hierarchies of an index with one query.
 *
 * @param world The world
 * @param q The query
 * @param ctx The query state
 */
static void box_index_walk(
    const ecs_world_t *world,
    box_bvh_query_t *q,
    box_index_ctx_t *ctx)
{
    const box_index_t *index = box_index_of(world);
    if (!index || !index->enabled) {
        goto done;
    }

    ctx->world = world;
    ctx->index = index;
    ctx->q = q;
    box_bvh_query(&index->statics, q, box_index_on_static, ctx);
    box_bvh_query(&index->dynamics, q, box_index_on_dynamic, ctx);
done:
    return;
}

bool box_collision_ray(
    const ecs_world_t *world,
    vec3 origin,
    vec3 dir,
    float max_dist,
    box_ray_hit_t *out)
{
    return box_collision_ray_filter(world, origin, dir, max_dist, NULL, NULL, out);
}

bool box_collision_ray_filter(
    const ecs_world_t *world,
    vec3 origin,
    vec3 dir,
    float max_dist,
    box_collision_filter_t filter,
    void *filter_ctx,
    box_ray_hit_t *out)
{
    box_ray_hit_t hit = { .distance = max_dist };
    box_bvh_query_t q;
    box_bvh_ray_setup(&q, origin, dir, max_dist);
    box_index_ctx_t ctx = { .hit = &hit, .filter = filter, .filter_ctx = filter_ctx };
    box_index_walk(world, &q, &ctx);
    if (out) {
        out[0] = hit;
    }

    return hit.entity != 0;
}

int32_t box_collision_sphere(
    const ecs_world_t *world,
    vec3 center,
    float radius,
    ecs_vec_t *out)
{
    box_bvh_query_t q = {
        .kind = BoxQuerySphere, .origin = center, .radius = radius
    };

    box_index_ctx_t ctx = { .out = out };
    box_index_walk(world, &q, &ctx);

    return ctx.count;
}

int32_t box_collision_aabb(
    const ecs_world_t *world,
    vec3 min,
    vec3 max,
    ecs_vec_t *out)
{
    box_bvh_query_t q = { .kind = BoxQueryAabb, .min = min, .max = max };
    box_index_ctx_t ctx = { .out = out };
    box_index_walk(world, &q, &ctx);

    return ctx.count;
}

int32_t box_collision_frustum(
    const ecs_world_t *world,
    const box_frustum_planes_t *frustum,
    ecs_vec_t *out)
{
    box_bvh_query_t q = { .kind = BoxQueryFrustum, .frustum = frustum };
    box_index_ctx_t ctx = { .out = out };
    box_index_walk(world, &q, &ctx);

    return ctx.count;
}

void box_index_movers(
    ecs_world_t *world,
    box_collision_state_t *state)
{
    box_index_t *index = &state->index;
    box_bvh_clear(&index->dynamics);
    ecs_vec_clear(&index->movers);
    if (!index->dynamic || !index->enabled) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, index->query);
    while (ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (state->hide_used &&
                box_hierarchy_ancestor_has(world, it.entities[i], Hidden))
            {
                continue;
            }

            Collider c = {0};
            if (!box_collider_resolve(world, it.entities[i], &c)) {
                continue;
            }

            vec3 min, max;
            box_collider_bounds(&c, &min, &max);
            int32_t slot = ecs_vec_count(&index->movers);
            box_index_dyn_t *dyn = ecs_vec_append_t(
                NULL, &index->movers, box_index_dyn_t);
            dyn->entity = it.entities[i];
            dyn->collider = c;
            box_bvh_insert(&index->dynamics, min, max, 0, (uint64_t)slot);
        }
    }

done:
    return;
}

void box_collision_update(
    ecs_world_t *world)
{
    ecs_world_t *raw = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    const CollisionState *cs = ecs_singleton_get(raw, CollisionState);
    if (!cs || !cs->state) {
        goto done;
    }

    const CollisionSettings *settings = ecs_singleton_get(raw, CollisionSettings);
    box_collision_state_t *state = cs->state;
    if (settings) {
        state->index.margin = settings->margin;
        state->index.enabled = settings->enabled;
        state->index.dynamic = settings->dynamic;
    }

    box_collision_roots_promote(raw, state);
    box_collision_shapes_init(raw, state);
    box_collision_hidden_init(raw, state);
    box_collision_colliders_flush(raw, state);
    box_collision_roots_flush(raw, state);
    box_index_movers(raw, state);
done:
    return;
}
