/**
 * @file transform/transform_static.c
 * @brief Transform static objects.
 */

#include "transform.h"

/** Entities whose static transform must be recomputed this frame */
typedef struct {
    ecs_vec_t value; /**< Vector of ecs_entity_t */
} TransformStaticDirtyEntities;

static void transformTree(
    const ecs_world_t *world,
    ecs_entity_t e,
    const WorldTransform *t,
    uint64_t version);

/** Free the storage of a dirty entity list.
 *
 * @param ptr The dirty entity list
 */
static void TransformStaticDirtyEntities_free(
    TransformStaticDirtyEntities *ptr)
{
    ecs_vec_fini_t(NULL, &ptr->value, ecs_entity_t);
}

ECS_CTOR(TransformStaticDirtyEntities, ptr, {
    ecs_vec_init_t(NULL, &ptr->value, ecs_entity_t, 0);
})

ECS_MOVE(TransformStaticDirtyEntities, dst, src, {
    TransformStaticDirtyEntities_free(dst);
    *dst = *src;
    ecs_vec_init_t(NULL, &src->value, ecs_entity_t, 0);
})

ECS_DTOR(TransformStaticDirtyEntities, ptr, {
    TransformStaticDirtyEntities_free(ptr);
})

ECS_COMPONENT_DECLARE(TransformStaticDirtyEntities);

/** Add entities with a changed transform input to the dirty list.
 *
 * @param it The observer iterator
 */
static void TransformStatic_OnChange(
    ecs_iter_t *it)
{
    TransformStaticDirtyEntities *dirty = ecs_singleton_get_mut(
        it->world, TransformStaticDirtyEntities);
    if (dirty) {
        for (int i = 0; i < it->count; i ++) {
            ecs_vec_append_t(NULL, &dirty->value, ecs_entity_t)[0] = it->entities[i];
        }
    }
}

/** Remove the world transform of entities that lose their position.
 *
 * @param it The observer iterator
 */
static void Transform_OnRemovePosition(
    ecs_iter_t *it)
{
    for (int i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], WorldTransform);
        ecs_remove(it->stage, it->entities[i], WorldTransformVersion);
    }
}

/** Transform children returned by an iterator without table storage.
 *
 * @param it The children iterator
 * @param t_parent The world transform of the parent
 * @param version The transform version of the current frame
 */
static void transformChildren(
    ecs_iter_t *it,
    const WorldTransform *t_parent,
    uint64_t version)
{
    ecs_world_t *world = it->world;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        WorldTransform *t = ecs_get_mut(world, e, WorldTransform);
        const Position *p = ecs_get(world, e, Position);
        const Rotation *r = ecs_get(world, e, Rotation);
        const Scale *s = ecs_get(world, e, Scale);

        if (!t) {
            transformTree(world, e, t_parent, version);
            continue;
        }

        if (p && !ecs_has(world, e, ManualTransform)) {
            transformSingle(t_parent, t, p, r, s);

            WorldTransformVersion *v = ecs_get_mut(
                world, e, WorldTransformVersion);
            if (v) {
                v->version = version;
            }
        }

        transformTree(world, e, t, version);
    }
}

/** Transform children stored in a table.
 *
 * @param it The children iterator
 * @param t_parent The world transform of the parent
 * @param version The transform version of the current frame
 */
static void transformChildrenTable(
    ecs_iter_t *it,
    const WorldTransform *t_parent,
    uint64_t version)
{
    ecs_world_t *world = it->world;
    ecs_table_t *table = it->table;
    int offset = it->offset;

    WorldTransform *t = ecs_table_get(world, table, WorldTransform, offset);
    Position *p = ecs_table_get(world, table, Position, offset);
    Rotation *r = ecs_table_get(world, table, Rotation, offset);
    Scale *s = ecs_table_get(world, table, Scale, offset);
    WorldTransformVersion *v = ecs_table_get(
        world, table, WorldTransformVersion, offset);

    if (!t) {
        for (int i = 0; i < it->count; i ++) {
            transformTree(world, it->entities[i], t_parent, version);
        }
    } else if (!p || ecs_table_has_id(world, table, ManualTransform)) {
        for (int i = 0; i < it->count; i ++) {
            transformTree(world, it->entities[i], &t[i], version);
        }
    } else {
        for (int i = 0; i < it->count; i ++) {
            transformSingle(
                t_parent, &t[i], &p[i], r ? &r[i] : NULL, s ? &s[i] : NULL);
            if (v) {
                v[i].version = version;
            }

            transformTree(world, it->entities[i], &t[i], version);
        }
    }
}

/** Recursively transform the children of an entity.
 *
 * @param world The world
 * @param e The parent entity
 * @param t The world transform of the parent
 * @param version The transform version of the current frame
 */
static void transformTree(
    const ecs_world_t *world,
    ecs_entity_t e,
    const WorldTransform *t,
    uint64_t version)
{
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        if (it.table) {
            transformChildrenTable(&it, t, version);
        } else {
            transformChildren(&it, t, version);
        }
    }
}

/** Transform a dirty entity and its children if not yet done this frame.
 *
 * A ManualTransform entity keeps the world transform the application wrote; only its
 * children are recomputed from it.
 *
 * @param world The world
 * @param e The dirty entity
 * @param version The transform version of the current frame
 */
static void transformRoot(
    const ecs_world_t *world,
    ecs_entity_t e,
    uint64_t version)
{
    WorldTransformVersion *v = NULL;
    bool skip = !ecs_is_alive(world, e) || ecs_has(world, e, DynamicTransform);

    if (!skip && ecs_has(world, e, ManualTransform)) {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        if (t) {
            transformTree(world, e, t, version);
        }

        skip = true;
    }

    if (!skip) {
        v = ecs_get_mut(world, e, WorldTransformVersion);
        skip = v && v->version == version;
    }

    if (!skip) {
        ecs_entity_t parent = ecs_get_parent(world, e);
        const WorldTransform *t_parent = parent ?
            transformGetParentWorldTransform(world, parent) : NULL;
        WorldTransform *t = ecs_get_mut(world, e, WorldTransform);
        const Position *p = ecs_get(world, e, Position);
        const Rotation *r = ecs_get(world, e, Rotation);
        const Scale *s = ecs_get(world, e, Scale);

        if (t && p) {
            transformSingle(t_parent, t, p, r, s);
            if (v) {
                v->version = version;
            }
        }

        transformTree(world, e, t ? t : t_parent, version);
    }
}

/** Transform all entities in the dirty list and clear the list.
 *
 * @param it The system iterator
 */
static void TransformStatic(
    ecs_iter_t *it)
{
    TransformStaticDirtyEntities *dirty = ecs_singleton_get_mut(
        it->world, TransformStaticDirtyEntities);

    int i = 0, count = ecs_vec_count(&dirty->value);
    ecs_entity_t *entities = ecs_vec_first(&dirty->value);
    uint64_t version = (uint64_t)ecs_get_world_info(
        it->world)->frame_count_total + 1;

    for (i = 0; i < count; i ++) {
        transformRoot(it->world, entities[i], version);
    }

    ecs_vec_clear(&dirty->value);
}

void TransformStaticImport(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, TransformStaticDirtyEntities);

    ecs_set_hooks(world, TransformStaticDirtyEntities, {
        .ctor = ecs_ctor(TransformStaticDirtyEntities),
        .dtor = ecs_dtor(TransformStaticDirtyEntities),
        .move = ecs_move(TransformStaticDirtyEntities),
    });

    ecs_singleton_add(world, TransformStaticDirtyEntities);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransformStatic_OnPosition" }),
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = DynamicTransform, .oper = EcsNot },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransformStatic_OnRotation" }),
        .query.terms = {
            { .id = ecs_id(Rotation), .src.id = EcsSelf },
            { .id = DynamicTransform, .oper = EcsNot },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransformStatic_OnScale" }),
        .query.terms = {
            { .id = ecs_id(Scale), .src.id = EcsSelf },
            { .id = DynamicTransform, .oper = EcsNot },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransformStatic_OnManual" }),
        .query.terms = {
            { .id = ecs_id(WorldTransform), .src.id = EcsSelf },
            { .id = ManualTransform, .inout = EcsInOutFilter },
            { .id = DynamicTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .callback = TransformStatic_OnChange
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "Transform_OnRemovePosition" }),
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ManualTransform, .oper = EcsNot, .inout = EcsInOutFilter }
        },
        .events = { EcsOnRemove },
        .callback = Transform_OnRemovePosition
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransformStatic_OnChildOf" }),
        .query.terms = {
            { .id = ecs_pair(EcsChildOf, EcsWildcard), .src.id = EcsSelf },
            { .id = DynamicTransform, .oper = EcsNot },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransformStatic_OnParent" }),
        .query.terms = {
            { .id = ecs_id(EcsParent), .src.id = EcsSelf },
            { .id = DynamicTransform, .oper = EcsNot },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "TransformStatic",
        }),
        .phase = EcsPreStore,
        .run = TransformStatic,
    });
}
