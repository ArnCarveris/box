/**
 * @file transform/transform_static.c
 * @brief Transform static objects.
 */

#include "transform.h"

/** A function that receives the entities whose static transform TransformStatic computed */
typedef struct {
    box_transform_moved_t callback; /**< The function */
    void *ctx;                      /**< Context passed to the function */
    ecs_ctx_free_t ctx_free;        /**< Frees the context with the world, NULL for none */
} TransformStaticListener;

/** A function that receives the events of entities that gain or lose DynamicTransform */
typedef struct {
    ecs_iter_action_t callback; /**< The function */
    void *ctx;                  /**< Context the function finds in it->ctx */
    ecs_ctx_free_t ctx_free;    /**< Frees the context with the world, NULL for none */
} TransformDynamicListener;

/** State of the static transform: the entities to recompute and the listeners */
typedef struct {
    ecs_vec_t value;     /**< Vector of ecs_entity_t */
    ecs_vec_t manual;    /**< ManualTransform entities whose WorldTransform was set */
    ecs_vec_t moved;     /**< ecs_entity_t, entities that moved this run */
    ecs_vec_t listeners; /**< TransformStaticListener, receive the moved entities */
    ecs_vec_t dynamic;   /**< TransformDynamicListener, receive DynamicTransform changes */
    int32_t refs;        /**< Observers and systems that use the state */
} transform_static_t;

/** Singleton through which box_transform_on_moved finds the state of a world */
typedef struct {
    transform_static_t *state; /**< The state, owned by the observers and the system */
} TransformStaticState;

ECS_COMPONENT_DECLARE(TransformStaticState);

static void transformTree(
    const ecs_world_t *world,
    ecs_entity_t e,
    const WorldTransform *t,
    uint64_t version,
    ecs_vec_t *moved);

/** Drop a reference to the state, freeing it and the listener contexts with the last one.
 *
 * @param ctx The state
 */
static void transformStaticRelease(
    void *ctx)
{
    transform_static_t *state = ctx;
    if (-- state->refs) {
        goto done;
    }

    int32_t i, count = ecs_vec_count(&state->listeners);
    TransformStaticListener *listeners = ecs_vec_first(&state->listeners);
    for (i = 0; i < count; i ++) {
        if (listeners[i].ctx_free) {
            listeners[i].ctx_free(listeners[i].ctx);
        }
    }

    count = ecs_vec_count(&state->dynamic);
    TransformDynamicListener *dynamic = ecs_vec_first(&state->dynamic);
    for (i = 0; i < count; i ++) {
        if (dynamic[i].ctx_free) {
            dynamic[i].ctx_free(dynamic[i].ctx);
        }
    }

    ecs_vec_fini_t(NULL, &state->value, ecs_entity_t);
    ecs_vec_fini_t(NULL, &state->manual, ecs_entity_t);
    ecs_vec_fini_t(NULL, &state->moved, ecs_entity_t);
    ecs_vec_fini_t(NULL, &state->listeners, TransformStaticListener);
    ecs_vec_fini_t(NULL, &state->dynamic, TransformDynamicListener);
    ecs_os_free(state);
done:
    return;
}

/** Add the entities of an observer event to a list of the state.
 *
 * @param it The observer iterator, its context is the state
 * @param list The list
 * @param add Whether to add the entities
 */
static void transformStaticDirty(
    ecs_iter_t *it,
    ecs_vec_t *list,
    bool add)
{
    if (add) {
        ecs_os_memcpy_n(ecs_vec_grow_t(NULL, list, ecs_entity_t, it->count), it->entities,
            ecs_entity_t, it->count);
    }
}

/** Add entities with a changed transform input to the dirty list, unless they are dynamic.
 *
 * A Position that comes with another component through With, such as a shape or a light, is
 * added without being set, so OnAdd of Position counts as a change too.
 *
 * The DynamicTransform filter runs in the callback, because a Not term on an inherited tag
 * allocates for every event.
 *
 * @param it The observer iterator
 */
static void TransformStatic_OnChange(
    ecs_iter_t *it)
{
    transform_static_t *state = it->ctx;
    transformStaticDirty(it, &state->value,
        !transformDeleting(it) && !box_transform_dynamic(it->world, it->table));
}

/** Follow entities that gain or lose DynamicTransform, owned or inherited.
 *
 * Entities that stop being dynamic stop spreading the tag to their children and join the
 * dirty list, then every function registered with box_transform_on_dynamic gets the event,
 * unless the entities are being deleted.
 *
 * @param it The observer iterator
 */
static void Transform_OnDynamic(
    ecs_iter_t *it)
{
    transform_static_t *state = it->ctx;
    bool deleting = it->event == EcsOnRemove && transformDeleting(it);
    if (it->event == EcsOnRemove) {
        transformDynamicUnspread(it, deleting);
        transformStaticDirty(it, &state->value,
            !deleting && !box_transform_dynamic(it->world, it->other_table));
    }

    int32_t i, count = deleting ? 0 : ecs_vec_count(&state->dynamic);
    for (i = 0; i < count; i ++) {
        const TransformDynamicListener *l = ecs_vec_get_t(
            &state->dynamic, TransformDynamicListener, i);
        ecs_iter_t listener_it = *it;
        listener_it.ctx = l->ctx;
        l->callback(&listener_it);
    }
}

/** Add ManualTransform entities whose WorldTransform was set to the manual list.
 *
 * @param it The observer iterator
 */
static void TransformStatic_OnManual(
    ecs_iter_t *it)
{
    transform_static_t *state = it->ctx;
    transformStaticDirty(it, &state->manual, !box_transform_dynamic(it->world, it->table));
}

/** Remove the world transform of entities that lose their position.
 *
 * @param it The observer iterator
 */
static void Transform_OnRemovePosition(
    ecs_iter_t *it)
{
    int count = transformDeleting(it) ? 0 : it->count;

    for (int i = 0; i < count; i ++) {
        ecs_remove(it->stage, it->entities[i], WorldTransform);
        ecs_remove(it->stage, it->entities[i], WorldTransformVersion);
    }
}

/** Transform children returned by an iterator without table storage.
 *
 * @param it The children iterator
 * @param t_parent The world transform of the parent
 * @param version The transform version of the current frame
 * @param moved Receives the entities whose world transform was written
 */
static void transformChildren(
    ecs_iter_t *it,
    const WorldTransform *t_parent,
    uint64_t version,
    ecs_vec_t *moved)
{
    ecs_world_t *world = it->world;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        WorldTransform *t = ecs_get_mut(world, e, WorldTransform);
        const Position *p = ecs_get(world, e, Position);
        const Rotation *r = ecs_get(world, e, Rotation);
        const Scale *s = ecs_get(world, e, Scale);

        if (!t) {
            transformTree(world, e, t_parent, version, moved);
            continue;
        }

        if (p && !ecs_has(world, e, ManualTransform)) {
            transformSingle(t_parent, t, p, r, s);
            ecs_vec_append_t(NULL, moved, ecs_entity_t)[0] = e;

            WorldTransformVersion *v = ecs_get_mut(
                world, e, WorldTransformVersion);
            if (v) {
                v->version = version;
            }
        }

        transformTree(world, e, t, version, moved);
    }
}

/** Transform children stored in a table.
 *
 * @param it The children iterator
 * @param t_parent The world transform of the parent
 * @param version The transform version of the current frame
 * @param moved Receives the entities whose world transform was written
 */
static void transformChildrenTable(
    ecs_iter_t *it,
    const WorldTransform *t_parent,
    uint64_t version,
    ecs_vec_t *moved)
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
            transformTree(world, it->entities[i], t_parent, version, moved);
        }
    } else if (!p || ecs_table_has_id(world, table, ManualTransform)) {
        for (int i = 0; i < it->count; i ++) {
            transformTree(world, it->entities[i], &t[i], version, moved);
        }
    } else {
        ecs_os_memcpy_n(ecs_vec_grow_t(NULL, moved, ecs_entity_t, it->count), it->entities,
            ecs_entity_t, it->count);
        for (int i = 0; i < it->count; i ++) {
            transformSingle(
                t_parent, &t[i], &p[i], r ? &r[i] : NULL, s ? &s[i] : NULL);
            if (v) {
                v[i].version = version;
            }

            transformTree(world, it->entities[i], &t[i], version, moved);
        }
    }
}

/** Recursively transform the children of an entity.
 *
 * @param world The world
 * @param e The parent entity
 * @param t The world transform of the parent
 * @param version The transform version of the current frame
 * @param moved Receives the entities whose world transform was written
 */
static void transformTree(
    const ecs_world_t *world,
    ecs_entity_t e,
    const WorldTransform *t,
    uint64_t version,
    ecs_vec_t *moved)
{
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        if (it.table) {
            transformChildrenTable(&it, t, version, moved);
        } else {
            transformChildren(&it, t, version, moved);
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
 * @param moved Receives the entity when its subtree is computed, and every descendant whose
 * world transform is written. A world transform computed before that comes out the same leaves
 * the subtree as it is and passes nothing: a dirty descendant is a root of its own.
 */
static void transformRoot(
    const ecs_world_t *world,
    ecs_entity_t e,
    uint64_t version,
    ecs_vec_t *moved)
{
    WorldTransformVersion *v = NULL;
    bool skip = !ecs_is_alive(world, e) || ecs_has(world, e, DynamicTransform);

    if (!skip && ecs_has(world, e, ManualTransform)) {
        const WorldTransform *t = ecs_get(world, e, WorldTransform);
        if (t) {
            ecs_vec_append_t(NULL, moved, ecs_entity_t)[0] = e;
            transformTree(world, e, t, version, moved);
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

        bool changed = true;
        if (t && p) {
            WorldTransform was = *t;
            transformSingle(t_parent, t, p, r, s);
            changed = !v || !v->version || memcmp(&was, t, sizeof(WorldTransform));
            if (v) {
                v->version = version;
            }
        }

        if (changed) {
            ecs_vec_append_t(NULL, moved, ecs_entity_t)[0] = e;
            transformTree(world, e, t ? t : t_parent, version, moved);
        }
    }
}

/** Order entity ids for qsort.
 *
 * @param a The first entity
 * @param b The second entity
 * @return Negative, zero or positive as a is below, equal to or above b
 */
static int transformCompareEntity(
    const void *a,
    const void *b)
{
    ecs_entity_t ea = *(const ecs_entity_t*)a, eb = *(const ecs_entity_t*)b;

    return (ea > eb) - (ea < eb);
}

/** Transform all entities in the dirty list, then the subtrees of the manual list once per
 * entity, pass the entities that moved to the listeners and clear the lists.
 *
 * @param it The system iterator
 */
static void TransformStatic(
    ecs_iter_t *it)
{
    transform_static_t *dirty = it->ctx;

    int i = 0, count = ecs_vec_count(&dirty->value);
    ecs_entity_t *entities = ecs_vec_first(&dirty->value);
    uint64_t version = (uint64_t)ecs_get_world_info(
        it->world)->frame_count_total + 1;

    for (i = 0; i < count; i ++) {
        transformRoot(it->world, entities[i], version, &dirty->moved);
    }

    count = ecs_vec_count(&dirty->manual);
    entities = ecs_vec_first(&dirty->manual);
    if (count > 1) {
        qsort(entities, (size_t)count, sizeof(ecs_entity_t), transformCompareEntity);
    }

    for (i = 0; i < count; i ++) {
        if (!i || entities[i] != entities[i - 1]) {
            transformRoot(it->world, entities[i], version, &dirty->moved);
        }
    }

    count = ecs_vec_count(&dirty->moved);
    TransformStaticListener *listeners = ecs_vec_first(&dirty->listeners);
    for (i = 0; count && i < ecs_vec_count(&dirty->listeners); i ++) {
        listeners[i].callback(it->world, ecs_vec_first(&dirty->moved), count, listeners[i].ctx);
    }

    ecs_vec_clear(&dirty->value);
    ecs_vec_clear(&dirty->manual);
    ecs_vec_clear(&dirty->moved);
}

void box_transform_on_moved(
    ecs_world_t *world,
    box_transform_moved_t callback,
    void *ctx,
    ecs_ctx_free_t ctx_free)
{
    const TransformStaticState *s = ecs_singleton_get(world, TransformStaticState);
    ecs_assert(s != NULL, ECS_INVALID_OPERATION, "box.transform is not imported");
    ecs_vec_append_t(NULL, &s->state->listeners, TransformStaticListener)[0] =
        (TransformStaticListener){ .callback = callback, .ctx = ctx, .ctx_free = ctx_free };
}

void box_transform_on_dynamic(
    ecs_world_t *world,
    ecs_iter_action_t callback,
    void *ctx,
    ecs_ctx_free_t ctx_free)
{
    const TransformStaticState *s = ecs_singleton_get(world, TransformStaticState);
    ecs_assert(s != NULL, ECS_INVALID_OPERATION, "box.transform is not imported");
    ecs_vec_append_t(NULL, &s->state->dynamic, TransformDynamicListener)[0] =
        (TransformDynamicListener){ .callback = callback, .ctx = ctx, .ctx_free = ctx_free };
}

/** Register an observer that uses the state, counting the reference it holds.
 *
 * @param world The world
 * @param state The state
 * @param desc The observer without context
 */
static void transformStaticObserver(
    ecs_world_t *world,
    transform_static_t *state,
    ecs_observer_desc_t desc)
{
    desc.ctx = state;
    desc.ctx_free = transformStaticRelease;
    state->refs ++;
    ecs_observer_init(world, &desc);
}

void TransformStaticImport(
    ecs_world_t *world)
{
    transform_static_t *state = ecs_os_calloc_t(transform_static_t);
    ecs_vec_init_t(NULL, &state->value, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->manual, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->moved, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->listeners, TransformStaticListener, 0);
    ecs_vec_init_t(NULL, &state->dynamic, TransformDynamicListener, 0);

    ECS_COMPONENT_DEFINE(world, TransformStaticState);
    ecs_set_hooks(world, TransformStaticState, { .ctor = flecs_default_ctor });
    ecs_singleton_set(world, TransformStaticState, { .state = state });

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "TransformStatic_OnPosition" }),
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "TransformStatic_OnAddPosition" }),
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnAdd },
        .callback = TransformStatic_OnChange
    });

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "TransformStatic_OnRotation" }),
        .query.terms = {
            { .id = ecs_id(Rotation), .src.id = EcsSelf },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "TransformStatic_OnScale" }),
        .query.terms = {
            { .id = ecs_id(Scale), .src.id = EcsSelf },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "TransformStatic_OnManual" }),
        .query.terms = {
            { .id = ecs_id(WorldTransform), .src.id = EcsSelf },
            { .id = ManualTransform, .inout = EcsInOutFilter }
        },
        .events = { EcsOnSet },
        .callback = TransformStatic_OnManual
    });

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "Transform_OnDynamic" }),
        .query.terms = {{ .id = DynamicTransform }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = Transform_OnDynamic
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

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "TransformStatic_OnChildOf" }),
        .query.terms = {
            { .id = ecs_pair(EcsChildOf, EcsWildcard), .src.id = EcsSelf },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "TransformStatic_OnParent" }),
        .query.terms = {
            { .id = ecs_id(EcsParent), .src.id = EcsSelf },
            { .id = ManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    transformStaticObserver(world, state, (ecs_observer_desc_t){
        .entity = ecs_entity(world, { .name = "TransformStatic_OnEnable" }),
        .query.terms = {
            { .id = EcsDisabled, .src.id = EcsSelf },
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOutFilter }
        },
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnRemove },
        .callback = TransformStatic_OnChange
    });

    state->refs ++;
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "TransformStatic",
        }),
        .phase = EcsPreStore,
        .run = TransformStatic,
        .ctx = state,
        .ctx_free = transformStaticRelease
    });
}
