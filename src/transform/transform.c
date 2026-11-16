/**
 * @file transform/transform.c
 * @brief Registers transform module.
 */

#define BOX_TRANSFORM_IMPL
#include "transform.h"
#include <math.h>

ECS_TAG_DECLARE(DynamicTransform);
ECS_TAG_DECLARE(ManualTransform);
ECS_DECLARE(DynamicTransformSpread);
ECS_COMPONENT_DECLARE(TransformWorldCache);

ECS_CTOR(Scale, ptr, {
    ptr->x = 1;
    ptr->y = 1;
    ptr->z = 1;
})

ECS_CTOR(WorldTransformVersion, ptr, {
    ptr->version = 0;
})

ECS_DTOR(TransformWorldCache, ptr, {
    if (ptr->cache) {
        ecs_os_free(ptr->cache->entries);
        ecs_os_free(ptr->cache);
    }
})

ECS_MOVE(TransformWorldCache, dst, src, {
    if (dst->cache && dst->cache != src->cache) {
        ecs_os_free(dst->cache->entries);
        ecs_os_free(dst->cache);
    }

    dst->cache = src->cache;
    src->cache = NULL;
})

void box_transform_local(
    const Position *p,
    const Rotation *r,
    const Scale *s,
    mat4 out)
{
    if (r) {
        glm_euler_yxz((vec3){ r->x, r->y, r->z }, out);
    } else {
        glm_mat4_identity(out);
    }

    if (s) {
        glm_vec4_scale(out[0], s->x, out[0]);
        glm_vec4_scale(out[1], s->y, out[1]);
        glm_vec4_scale(out[2], s->z, out[2]);
    }

    out[3][0] = p->x;
    out[3][1] = p->y;
    out[3][2] = p->z;
}

float box_transform_world_scale(
    const WorldTransform *transform)
{
    mat3 m;
    glm_mat4_pick3(ECS_CONST_CAST(vec4*, transform->m), m);
    float result = cbrtf(fabsf(glm_mat3_det(m)));
    if (!isfinite(result)) {
        result = 1.0f;
    } else if (fabsf(result - 1.0f) < 1e-5f) {
        result = 1.0f;
    }

    return result;
}

void transformSingle(
    const WorldTransform *t_parent,
    WorldTransform *t,
    const Position *p,
    const Rotation *r,
    const Scale *s)
{
    ecs_assert(p != NULL, ECS_INTERNAL_ERROR, "missing position");

    mat4 local;
    vec4 *m = t_parent ? local : t->m;
    box_transform_local(p, r, s, m);

    if (t_parent) {
        glm_mul((vec4*)t_parent->m, local, t->m);
    }
}

bool box_transform_spread(
    const ecs_world_t *world,
    const ecs_table_t *table)
{
    const ecs_world_t *real = world ? ecs_get_world(world) : NULL;

    return table && ecs_table_has_id(real, table, ecs_pair(EcsIsA, DynamicTransformSpread)) &&
        !ecs_table_has_id(real, table, DynamicTransform) &&
        !ecs_table_has_id(real, table, ManualTransform) &&
        ecs_table_has_id(real, table, ecs_id(WorldTransform)) &&
        ecs_search_relation(real, table, 0, ecs_id(Position), EcsIsA, EcsSelf | EcsUp,
            NULL, NULL, NULL) != -1;
}

void transformWorldCacheInvalidate(
    const ecs_world_t *world)
{
    const TransformWorldCache *c = ecs_singleton_get(world, TransformWorldCache);
    if (c && c->cache) {
        c->cache->epoch ++;
    }
}

/** Get the cache of computed world transforms of a world, allocating its entries.
 *
 * @param world The world
 * @return The cache, NULL when the world has none
 */
static transform_world_cache_t* transformWorldCache(
    const ecs_world_t *world)
{
    const TransformWorldCache *c = ecs_singleton_get(world, TransformWorldCache);
    transform_world_cache_t *result = c ? c->cache : NULL;
    if (result && !result->entries) {
        result->entries = ecs_os_calloc_n(transform_world_cached_t, TRANSFORM_WORLD_CACHE);
    }

    return result;
}

/** Get the world transform of an entity (box_transform_world) with the cache of the world.
 *
 * @param world The world
 * @param cache The cache, NULL for none
 * @param e The entity, 0 for none
 * @param buf Receives the transform when it is computed
 * @return The world transform, NULL when the entity has no WorldTransform
 */
static const WorldTransform* transformWorldIn(
    const ecs_world_t *world,
    transform_world_cache_t *cache,
    ecs_entity_t e,
    WorldTransform *buf)
{
    const ecs_table_t *table = e ? ecs_get_table(world, e) : NULL;
    const WorldTransform *result = NULL;
    if (!box_transform_spread(world, table)) {
        result = table ? ecs_get(world, e, WorldTransform) : NULL;
        goto done;
    }

    uint32_t slot = ((uint32_t)e * 2654435761u) & (TRANSFORM_WORLD_CACHE - 1);
    transform_world_cached_t *entry = cache ? &cache->entries[slot] : NULL;
    if (entry && entry->entity == e && entry->epoch == cache->epoch) {
        *buf = entry->t;
    } else {
        WorldTransform parent_buf;
        const WorldTransform *t_parent = NULL;
        ecs_entity_t parent = ecs_get_parent(world, e);
        while (parent && !(t_parent = transformWorldIn(world, cache, parent, &parent_buf))) {
            parent = ecs_get_parent(world, parent);
        }

        transformSingle(t_parent, buf, ecs_get(world, e, Position),
            ecs_get(world, e, Rotation), ecs_get(world, e, Scale));
        if (entry) {
            *entry = (transform_world_cached_t){ .entity = e, .epoch = cache->epoch, .t = *buf };
        }
    }

    result = buf;
done:
    return result;
}

const WorldTransform* transformGetParentWorldTransform(
    const ecs_world_t *world,
    ecs_entity_t parent,
    WorldTransform *buf)
{
    transform_world_cache_t *cache = transformWorldCache(world);
    const WorldTransform *t = transformWorldIn(world, cache, parent, buf);
    while (!t && parent) {
        parent = ecs_get_parent(world, parent);
        if (parent) {
            t = transformWorldIn(world, cache, parent, buf);
        }
    }

    return t;
}

const WorldTransform* box_transform_world(
    const ecs_world_t *world,
    ecs_entity_t e,
    WorldTransform *buf)
{
    return transformWorldIn(world, transformWorldCache(world), e, buf);
}

const WorldTransform* box_transform_world_field(
    const ecs_world_t *world,
    const ecs_iter_t *it,
    const WorldTransform *field,
    int32_t row,
    WorldTransform *buf)
{
    const WorldTransform *result = field ? &field[row] : NULL;
    if (result && box_transform_spread(world, it->table)) {
        result = box_transform_world(world, it->entities[row], buf);
    }

    return result;
}

bool box_transform_dynamic(
    const ecs_world_t *world,
    const ecs_table_t *table)
{
    return table && ecs_search_relation(world, table, 0, DynamicTransform, EcsIsA,
        EcsSelf | EcsUp, NULL, NULL, NULL) != -1;
}

/** Test whether the parent of an entity is being deleted.
 *
 * @param world The world
 * @param e The entity
 * @return true when flecs marked the children of the parent for deletion
 */
static bool transformParentDeleting(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t parent = ecs_get_parent(world, e);

    return parent && (ecs_id_get_flags(world, ecs_childof(parent)) & EcsIdMarkedForDelete);
}

bool transformDeleting(
    const ecs_iter_t *it)
{
    return it->event == EcsOnRemove && (!it->other_table ||
        (!ecs_table_get_type(it->other_table)->count && it->table && it->count &&
            (ecs_table_has_flags(it->table, EcsTableMarkedForDelete) ||
                transformParentDeleting(it->world, it->entities[0]))));
}

ecs_entity_t transformScopePush(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t scope = ecs_get_scope(world);
    ecs_entity_t ns = ecs_entity(world, { .name = name, .parent = scope });
    ecs_add_id(world, ns, EcsModule);
    ecs_set_scope(world, ns);

    return scope;
}

void BoxTransformImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTransform);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Position);
    ECS_META_COMPONENT(world, Rotation);
    ECS_META_COMPONENT(world, Scale);
    ECS_META_COMPONENT(world, WorldTransform);
    ECS_META_COMPONENT(world, WorldTransformVersion);

    ECS_TAG_DEFINE(world, DynamicTransform);
    ECS_TAG_DEFINE(world, ManualTransform);
    ecs_add_pair(world, DynamicTransform, EcsOnInstantiate, EcsInherit);
    DynamicTransformSpread = ecs_entity(world, { .name = "DynamicTransformSpread" });
    ecs_add_id(world, DynamicTransformSpread, EcsPrefab);
    ecs_add(world, DynamicTransformSpread, DynamicTransform);

    ecs_add_pair(world, ecs_id(WorldTransform), EcsWith, ecs_id(WorldTransformVersion));
    ecs_add_pair(world, ecs_id(Position), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Rotation), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Rotation), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(Scale), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Scale), EcsWith, ecs_id(Position));

    ecs_set_hooks(world, Scale, { .ctor = ecs_ctor(Scale) });
    ecs_set_hooks(world, WorldTransformVersion, {
        .ctor = ecs_ctor(WorldTransformVersion)
    });

    ecs_set_name_prefix(world, "Transform");
    ECS_COMPONENT_DEFINE(world, TransformWorldCache);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, TransformWorldCache, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(TransformWorldCache),
        .move = ecs_move(TransformWorldCache)
    });
    TransformWorldCache *c = ecs_singleton_ensure(world, TransformWorldCache);
    c->cache = ecs_os_calloc_t(transform_world_cache_t);
    c->cache->epoch = 1;

    TransformDynamicImport(world);
    TransformStaticImport(world);
}
