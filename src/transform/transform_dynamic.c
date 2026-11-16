/**
 * @file transform/transform_dynamic.c
 * @brief Transform dynamic objects.
 */

#include "transform.h"

/** Queries used by the TransformDynamic system */
typedef struct {
    ecs_query_t *q_childof; /**< Dynamic entities with a ChildOf parent */
    ecs_query_t *q_parent;  /**< Dynamic entities with a Parent component */
    transform_parent_cache_t cache; /**< Parent world transforms of a run */
} transform_dynamic_ctx_t;

/** Create the queries used by the TransformDynamic system.
 *
 * @param world The world
 * @return The system context
 */
static transform_dynamic_ctx_t* transform_dynamic_ctx_new(
    ecs_world_t *world)
{
    transform_dynamic_ctx_t *result = ecs_os_malloc_t(transform_dynamic_ctx_t);
    result->cache.entries = ecs_os_calloc_n(transform_parent_cached_t, TRANSFORM_PARENT_CACHE);
    result->cache.stamp = 0;

    result->q_childof = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ChildOfQuery" }),
        .terms = {{
            .id = ecs_id(WorldTransform),
            .inout = EcsOut,
        }, {
            .id = ecs_id(Position),
            .inout = EcsIn
        }, {
            .id = ecs_id(Rotation),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(Scale),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(WorldTransform),
            .inout = EcsIn,
            .oper = EcsOptional,
            .src.id = EcsCascade
        }, {
            .id = ecs_id(EcsParent),
            .oper = EcsNot,
            .src.id = EcsSelf
        }, {
            .id = ecs_id(DynamicTransform),
        }, {
            .id = ecs_id(ManualTransform),
            .oper = EcsNot
        }},
        .cache_kind = EcsQueryCacheAuto
    });

    result->q_parent = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ParentQuery" }),
        .terms = {{
            .id = ecs_id(WorldTransform),
            .inout = EcsOut,
        }, {
            .id = ecs_id(Position),
            .inout = EcsIn
        }, {
            .id = ecs_id(Rotation),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(Scale),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(EcsParent),
        }, {
            .id = ecs_id(DynamicTransform),
        }, {
            .id = ecs_id(ManualTransform),
            .oper = EcsNot
        }},
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsParentDepth,
        .flags = EcsQueryGroupByOrdered
    });

    return result;
}

/** Free the TransformDynamic system context.
 *
 * @param arg The system context
 */
static void transform_dynamic_ctx_free(
    void *arg)
{
    transform_dynamic_ctx_t *ctx = arg;
    ecs_os_free(ctx->cache.entries);
    ecs_os_free(ctx);
}

/** Mark children of dynamic entities as dynamic.
 *
 * A child inherits DynamicTransform from a prefab instead of owning it, so the tag the
 * application adds itself, before or after parenting, stays distinguishable.
 *
 * @param it The observer iterator
 */
static void TransformDynamic_Spread(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->stage;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!ecs_has(world, e, DynamicTransform)) {
            ecs_add_pair(world, e, EcsIsA, DynamicTransformSpread);
        }
    }
}

/** Mark entities with a dynamic Parent as dynamic.
 *
 * @param it The observer iterator
 */
static void TransformDynamic_SpreadToParented(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->stage;
    if (it->table && !box_transform_dynamic(ecs_get_world(world), it->table)) {
        const EcsParent *parents = ecs_field(it, EcsParent, 0);

        for (int i = 0; i < it->count; i ++) {
            ecs_entity_t parent = parents[i].value;
            if (!parent || !ecs_is_valid(world, parent)) {
                continue;
            }

            if (ecs_has(world, parent, DynamicTransform)) {
                ecs_add_pair(world, it->entities[i], EcsIsA, DynamicTransformSpread);
            }
        }
    }
}

void transformDynamicUnspread(
    ecs_iter_t *it,
    bool deleting)
{
    ecs_world_t *world = it->stage;
    ecs_id_t spread = ecs_pair(EcsIsA, DynamicTransformSpread);
    bool keep = deleting || box_transform_dynamic(it->world, it->other_table);
    int count = keep ? 0 : it->count;

    for (int i = 0; i < count; i ++) {
        ecs_iter_t child_it = ecs_children(world, it->entities[i]);
        while (ecs_children_next(&child_it)) {
            if (child_it.table && !ecs_table_has_id(it->world, child_it.table, spread)) {
                continue;
            }

            for (int c = 0; c < child_it.count; c ++) {
                ecs_entity_t child = child_it.entities[c];
                if (child_it.table || ecs_has_id(it->world, child, spread)) {
                    ecs_remove_id(world, child, spread);
                }
            }
        }
    }
}

/** Find the largest hierarchy depth of the groups of a query.
 *
 * @param query The query grouped by depth
 * @param max The largest depth found so far
 * @return The largest depth of the query groups and max
 */
static uint64_t transformDynamicMaxDepth(
    const ecs_query_t *query,
    uint64_t max)
{
    ecs_map_iter_t it = ecs_map_iter(ecs_query_get_groups(query));
    while (ecs_map_next(&it)) {
        uint64_t depth = ecs_map_key(&it);
        if (depth > max) {
            max = depth;
        }
    }

    return max;
}

/** Transform dynamic entities breadth first by hierarchy depth.
 *
 * @param it The system iterator
 */
static void TransformDynamic(
    ecs_iter_t *it)
{
    transform_dynamic_ctx_t *ctx = it->ctx;
    uint64_t max = transformDynamicMaxDepth(ctx->q_childof, 0);
    max = transformDynamicMaxDepth(ctx->q_parent, max);
    ctx->cache.stamp ++;

    for (uint64_t depth = 0; depth <= max; depth ++) {
        transformDynamicChildOf(it->world, ctx->q_childof, depth);
        transformDynamicParent(it->world, ctx->q_parent, depth, &ctx->cache);
    }
}

void TransformDynamicImport(
    ecs_world_t *world)
{
    ecs_entity_t scope = transformScopePush(world, "dynamic");

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "Update",
        }),
        .phase = EcsPreStore,
        .run = TransformDynamic,
        .ctx = transform_dynamic_ctx_new(world),
        .ctx_free = transform_dynamic_ctx_free
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "Spread" }),
        .query.terms = {
            { .id = DynamicTransform, .src.id = EcsUp }
        },
        .events = { EcsOnAdd },
        .callback = TransformDynamic_Spread,
        .yield_existing = true
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "SpreadToParented" }),
        .query.terms = {
            { .id = ecs_id(EcsParent) }
        },
        .events = { EcsOnAdd, EcsOnSet },
        .callback = TransformDynamic_SpreadToParented
    });

    ecs_set_scope(world, scope);
}
