/**
 * @file transform/transform_dynamic_parent.c
 * @brief Transform dynamic entities with Parent component.
 *
 * Siblings are usually stored next to each other, so the world transform of a parent is looked
 * up once per run of entities with the same parent, and once per run of the system through the
 * cache when children of several parents are interleaved.
 */

#include "transform.h"

/** Get the world transform of a parent through the cache of the run.
 *
 * @param world The world
 * @param cache Parent world transforms of this run
 * @param parent The parent
 * @return The world transform, NULL when no ancestor has one
 */
static const WorldTransform* transformParentCached(
    const ecs_world_t *world,
    transform_parent_cache_t *cache,
    ecs_entity_t parent)
{
    uint32_t slot = ((uint32_t)parent * 2654435761u) & (TRANSFORM_PARENT_CACHE - 1);
    transform_parent_cached_t *entry = &cache->entries[slot];
    if (entry->stamp != cache->stamp || entry->parent != parent) {
        entry->parent = parent;
        entry->t = transformGetParentWorldTransform(world, parent);
        entry->stamp = cache->stamp;
    }

    return entry->t;
}

void transformDynamicParent(
    ecs_world_t *world,
    ecs_query_t *query,
    uint64_t depth,
    transform_parent_cache_t *cache)
{
    ecs_iter_t it = ecs_query_iter(world, query);
    ecs_iter_set_group(&it, depth);
    while (ecs_query_next(&it)) {
        WorldTransform *t = ecs_field(&it, WorldTransform, 0);
        const Position *p = ecs_field(&it, Position, 1);
        const Rotation *r = ecs_field(&it, Rotation, 2);
        const Scale *s = ecs_field(&it, Scale, 3);
        const EcsParent *parents = ecs_field(&it, EcsParent, 4);
        ecs_entity_t last = 0;
        const WorldTransform *t_parent = NULL;

        for (int i = 0; i < it.count; i ++) {
            ecs_entity_t parent = parents[i].value;
            if (!i || parent != last) {
                t_parent = transformParentCached(world, cache, parent);
                last = parent;
            }

            transformSingle(t_parent, &t[i], &p[i], r ? &r[i] : NULL, s ? &s[i] : NULL);
        }
    }
}
