/**
 * @file spatial/layout_array.c
 * @brief Array instancing of the spatial layout: instances synced with the prototypes of arrays.
 *
 * Children of an Array entity become prefab prototypes, and every prototype gets count instances
 * that inherit from it and copy its layout inputs and shapes.
 */

#include "layout.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** An array entity with its depth in the hierarchy, to sync parents before children */
typedef struct array_entry_t {
    ecs_entity_t entity; /**< The array entity */
    int depth;           /**< Number of entities from the entity up to the root */
} array_entry_t;

/** Collect the entities that have a component.
 *
 * @param world The world
 * @param id The component
 * @return Vector of entities, to be freed by the caller
 */
static ecs_vec_t collect(
    ecs_world_t *world,
    ecs_id_t id)
{
    ecs_vec_t result;
    ecs_vec_init_t(NULL, &result, ecs_entity_t, 0);
    ecs_iter_t it = ecs_each_id(world, id);
    while (ecs_each_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            *ecs_vec_append_t(NULL, &result, ecs_entity_t) = it.entities[i];
        }
    }

    return result;
}

/** Copy a component from one entity to another, or remove it when the source does not have it.
 *
 * @param world The world
 * @param dst The entity to copy to
 * @param src The entity to copy from
 * @param id The component
 */
static void sync_component(
    ecs_world_t *world,
    ecs_entity_t dst,
    ecs_entity_t src,
    ecs_id_t id)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, id);
    if (ti) {
        const void *value = ecs_get_id(world, src, id);
        if (value) {
            const void *old = ecs_get_id(world, dst, id);
            if (!old || memcmp(old, value, (size_t)ti->size)) {
                ecs_set_id(world, dst, id, (size_t)ti->size, value);
            }
        } else if (ecs_owns_id(world, dst, id)) {
            ecs_remove_id(world, dst, id);
        }
    }
}

/** Sync an instance with its prototype, and the children of the instance with its children.
 *
 * Copies the layout inputs and shapes of the prototype, creates a copy of every prototype child
 * and deletes copies whose prototype child is gone.
 *
 * @param world The world
 * @param dst The instance
 * @param src The prototype
 */
static void sync_prototype(
    ecs_world_t *world,
    ecs_entity_t dst,
    ecs_entity_t src)
{
    ecs_id_t ids[] = {ecs_id(Position),   ecs_id(Rotation),       ecs_id(Scale),
                      ecs_id(Fit),        ecs_id(SpatialAlign),   ecs_id(Array),
                      ecs_id(Distribute), ecs_id(AttachmentFrame)};
    for (int i = 0; i < 8; i++) {
        if (ids[i] == ecs_id(Position) && !ecs_has(world, src, Position)) {
            continue;
        }

        sync_component(world, dst, src, ids[i]);
    }

    const ecs_type_t *type = ecs_get_type(world, src);
    ecs_vec_t shapes;
    ecs_vec_init_t(NULL, &shapes, ecs_id_t, 0);
    for (int i = 0; type && i < type->count; i++) {
        ecs_id_t id = type->array[i];
        if (!ECS_IS_PAIR(id) && ecs_has_pair(world, id, EcsWith, ecs_id(ShapeMesh))) {
            *ecs_vec_append_t(NULL, &shapes, ecs_id_t) = id;
        }
    }

    type = ecs_get_type(world, dst);
    ecs_vec_t removed;
    ecs_vec_init_t(NULL, &removed, ecs_id_t, 0);
    for (int i = 0; type && i < type->count; i++) {
        ecs_id_t id = type->array[i];
        if (!ECS_IS_PAIR(id) && ecs_has_pair(world, id, EcsWith, ecs_id(ShapeMesh)) &&
            !ecs_has_id(world, src, id)) {
            *ecs_vec_append_t(NULL, &removed, ecs_id_t) = id;
        }
    }

    for (int i = 0; i < ecs_vec_count(&removed); i++) {
        ecs_remove_id(world, dst, *ecs_vec_get_t(&removed, ecs_id_t, i));
    }

    for (int i = 0; i < ecs_vec_count(&shapes); i++) {
        sync_component(world, dst, src, *ecs_vec_get_t(&shapes, ecs_id_t, i));
    }

    ecs_vec_fini_t(NULL, &shapes, ecs_id_t);
    ecs_vec_fini_t(NULL, &removed, ecs_id_t);
    ecs_vec_t descendants = box_spatial_layout_children(world, src, true);
    for (int i = 0; i < ecs_vec_count(&descendants); i++) {
        ecs_entity_t child = *ecs_vec_get_t(&descendants, ecs_entity_t, i);
        const char *name = ecs_get_name(world, child);
        char generated[64];
        if (!name) {
            ecs_os_snprintf(generated, sizeof(generated), "__prototype_%llu",
                            (unsigned long long)child);
            name = generated;
        }

        ecs_entity_t copy = ecs_lookup_child(world, dst, name);
        if (!copy) {
            copy = ecs_entity(world, {.name = name, .parent = dst});
            ecs_add_pair(world, copy, EcsIsA, child);
        }

        PrototypeSource source = {child};
        box_spatial_layout_set_changed(
            world, copy, ecs_id(PrototypeSource), sizeof(source), &source);
        sync_prototype(world, copy, child);
    }

    ecs_vec_fini_t(NULL, &descendants, ecs_entity_t);
    descendants = box_spatial_layout_children(world, dst, true);
    for (int i = 0; i < ecs_vec_count(&descendants); i++) {
        ecs_entity_t copy = *ecs_vec_get_t(&descendants, ecs_entity_t, i);
        const PrototypeSource *source = ecs_get(world, copy, PrototypeSource);
        if (source && (!ecs_is_alive(world, source->entity) ||
                       ecs_get_parent(world, source->entity) != src)) {
            ecs_delete(world, copy);
        }
    }

    ecs_vec_fini_t(NULL, &descendants, ecs_entity_t);
}

/** Turn an entity and its children into prefabs, so that they can be instanced.
 *
 * @param world The world
 * @param e The prototype entity
 */
static void prepare_prototype(
    ecs_world_t *world,
    ecs_entity_t e)
{
    if (!ecs_has_id(world, e, EcsPrefab)) {
        if (ecs_has(world, e, ShapeMesh) && !ecs_has(world, e, Position)) {
            ecs_set(world, e, Position, {0});
        }

        ecs_add_id(world, e, EcsPrefab);
        ecs_add_id(world, e, ArrayPrototype);
    }

    ecs_vec_t list = box_spatial_layout_children(world, e, true);
    for (int i = 0; i < ecs_vec_count(&list); i++) {
        prepare_prototype(world, *ecs_vec_get_t(&list, ecs_entity_t, i));
    }

    ecs_vec_fini_t(NULL, &list, ecs_entity_t);
}

/** Compare two array entries by depth, then by entity, for qsort.
 *
 * @param a Pointer to the first array_entry_t
 * @param b Pointer to the second array_entry_t
 * @return Negative, zero or positive when a sorts before, equal to or after b
 */
static int array_compare(
    const void *a,
    const void *b)
{
    const array_entry_t *x = a, *y = b;

    return x->depth == y->depth ? box_spatial_layout_entity_compare(&x->entity, &y->entity)
                                : (x->depth > y->depth) - (x->depth < y->depth);
}

/** Collect the entities with an Array, parents before their children.
 *
 * @param world The world
 * @return Vector of array entities, to be freed by the caller
 */
static ecs_vec_t ordered_arrays(
    ecs_world_t *world)
{
    ecs_vec_t result = collect(world, ecs_id(Array));
    int count = ecs_vec_count(&result);
    array_entry_t *entries = count ? ecs_os_malloc_n(array_entry_t, count) : NULL;
    for (int i = 0; i < count; i++) {
        ecs_entity_t e = *ecs_vec_get_t(&result, ecs_entity_t, i);
        entries[i] = (array_entry_t){.entity = e};
        for (; e; e = ecs_get_parent(world, e)) {
            entries[i].depth++;
        }
    }

    if (count > 1) {
        qsort(entries, (size_t)count, sizeof(array_entry_t), array_compare);
    }

    for (int i = 0; i < count; i++) {
        *ecs_vec_get_t(&result, ecs_entity_t, i) = entries[i].entity;
    }

    ecs_os_free(entries);

    return result;
}

/** Test whether an entity is in a sorted vector of array roots.
 *
 * @param roots Sorted vector of entities
 * @param e The entity
 * @return Whether roots contains the entity
 */
static bool root_has(
    const ecs_vec_t *roots,
    ecs_entity_t e)
{
    int32_t count = ecs_vec_count(roots);

    return count && bsearch(&e, ecs_vec_first(roots), (size_t)count, sizeof(ecs_entity_t),
                            box_spatial_layout_entity_compare);
}

/** Test whether an entity or one of its ancestors is an array root.
 *
 * @param world The world
 * @param roots Sorted vector of array roots
 * @param e The entity
 * @return Whether the entity is at or below an array root
 */
static bool under_roots(
    ecs_world_t *world,
    const ecs_vec_t *roots,
    ecs_entity_t e)
{
    bool result = false;
    for (; e && !result; e = ecs_get_parent(world, e)) {
        result = root_has(roots, e);
    }

    return result;
}

/** Collect the arrays that contain an entity with changed inputs.
 *
 * Only the instances of these arrays are synced with their prototypes.
 *
 * @param ctx The layout context
 * @param seed_count Number of entities in layout_ctx_t::seeds with changed inputs
 */
static void array_roots(
    layout_ctx_t *ctx,
    int32_t seed_count)
{
    ecs_world_t *world = ctx->world;
    ecs_vec_t *roots = &ctx->roots;
    ecs_vec_clear(roots);
    for (int32_t i = 0; i < seed_count; i++) {
        ecs_entity_t e = *ecs_vec_get_t(&ctx->seeds, ecs_entity_t, i);
        if (!ecs_is_alive(world, e)) {
            continue;
        }

        for (; e; e = ecs_get_parent(world, e)) {
            if (ecs_owns(world, e, Array)) {
                *ecs_vec_append_t(NULL, roots, ecs_entity_t) = e;
            }
        }
    }

    int32_t count = ecs_vec_count(roots);
    if (count > 1) {
        ecs_entity_t *array = ecs_vec_first(roots);
        qsort(array, (size_t)count, sizeof(ecs_entity_t), box_spatial_layout_entity_compare);
        int32_t unique = 1;
        for (int32_t i = 1; i < count; i++) {
            if (array[i] != array[unique - 1]) {
                array[unique++] = array[i];
            }
        }

        ecs_vec_set_count_t(NULL, roots, ecs_entity_t, unique);
    }
}

/** Record the first error of an array sync on the node of the array.
 *
 * @param n The node of the array
 * @param code The error
 * @param related The entity the error is about
 */
static void array_fail(
    layout_node_t *n,
    SpatialError code,
    ecs_entity_t related)
{
    if (!n->array_error.code) {
        n->array_error = (SpatialDiagnostic){code, related};
    }
}

/** Create, update and delete the instances of an array.
 *
 * @param ctx The layout context
 * @param n The node of the array
 * @param a The Array of the entity
 */
static void array_sync(
    layout_ctx_t *ctx,
    layout_node_t *n,
    Array a)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t e = n->entity;
    if (box_spatial_layout_axis_index(a.axis) < 0 || a.count < 0 || a.count > 100000 ||
        !isfinite(a.spacing) || !box_spatial_layout_finite3(a.rotation_step) ||
        !box_spatial_layout_finite3(a.scale_step) || ecs_has(world, e, Distribute)) {
        array_fail(n, SpatialImpossible, e);
    } else {
        ecs_vec_t current = box_spatial_layout_children(world, e, false);
        for (int j = 0; j < ecs_vec_count(&current); j++) {
            ecs_entity_t child = *ecs_vec_get_t(&current, ecs_entity_t, j);
            const ArrayInstance *instance = ecs_get(world, child, ArrayInstance);
            if (instance && instance->owner == e && instance->index >= a.count) {
                ecs_delete(world, child);
            }
        }

        ecs_vec_fini_t(NULL, &current, ecs_entity_t);
        ecs_vec_t prototypes = box_spatial_layout_children(world, e, true);
        for (int p = 0; p < ecs_vec_count(&prototypes); p++) {
            ecs_entity_t prototype = *ecs_vec_get_t(&prototypes, ecs_entity_t, p);
            prepare_prototype(world, prototype);
            for (int index = 0; index < a.count; index++) {
                char name[96];
                ecs_os_snprintf(name, sizeof(name), "__array_%llu_%d",
                                (unsigned long long)prototype, index);
                ecs_entity_t instance = ecs_lookup_child(world, e, name);
                if (!instance) {
                    instance = ecs_entity(world, {.name = name, .parent = e});
                    ecs_add_pair(world, instance, EcsIsA, prototype);
                    ecs_set(world, instance, ArrayInstance, {e, prototype, index});
                }

                const ArrayInstance *identity = ecs_get(world, instance, ArrayInstance);
                if (!identity || identity->owner != e || identity->prototype != prototype ||
                    identity->index != index) {
                    array_fail(n, SpatialConflictingLayout, instance);
                    continue;
                }

                sync_prototype(world, instance, prototype);
            }
        }

        ecs_vec_fini_t(NULL, &prototypes, ecs_entity_t);
    }
}

/** Delete the instances whose array, prototype or index no longer exists.
 *
 * @param world The world
 */
static void instances_prune(
    ecs_world_t *world)
{
    ecs_vec_t instances = collect(world, ecs_id(ArrayInstance));
    for (int i = 0; i < ecs_vec_count(&instances); i++) {
        ecs_entity_t e = *ecs_vec_get_t(&instances, ecs_entity_t, i);
        if (!ecs_is_alive(world, e)) {
            continue;
        }

        ArrayInstance instance = *ecs_get(world, e, ArrayInstance);
        const Array *a =
            ecs_is_alive(world, instance.owner) ? ecs_get(world, instance.owner, Array) : NULL;
        if (!a || instance.index >= a->count || !ecs_is_alive(world, instance.prototype) ||
            ecs_get_parent(world, instance.prototype) != instance.owner) {
            ecs_delete(world, e);
        }
    }

    ecs_vec_fini_t(NULL, &instances, ecs_entity_t);
}

/** Sync the arrays under the array roots once per pass, until no new array shows up.
 *
 * A node created in this pass and not solved yet takes the array error as its error. Otherwise
 * the array is seeded again when its error changed.
 *
 * @param ctx The layout context
 */
static void arrays_sync(
    layout_ctx_t *ctx)
{
    ecs_world_t *world = ctx->world;
    ecs_vec_t list;
    bool pending = true;
    while (pending) {
        pending = false;
        list = ordered_arrays(world);
        for (int i = 0; i < ecs_vec_count(&list); i++) {
            ecs_entity_t e = *ecs_vec_get_t(&list, ecs_entity_t, i);
            if (!ecs_is_alive(world, e) || ecs_has_id(world, e, EcsPrefab) ||
                !under_roots(world, &ctx->roots, e)) {
                continue;
            }

            Array a = *ecs_get(world, e, Array);
            layout_node_t *n = box_spatial_layout_node_get(ctx, e);
            if (n->array_epoch == ctx->epoch) {
                continue;
            }

            n->array_epoch = ctx->epoch;
            pending = true;
            ctx->synced++;
            SpatialDiagnostic previous = n->array_error;
            n->array_error = (SpatialDiagnostic){0};
            array_sync(ctx, n, a);
            if (n->epoch == ctx->epoch) {
                n->error = n->array_error;
            } else if (previous.code != n->array_error.code ||
                       previous.related != n->array_error.related) {
                *ecs_vec_append_t(NULL, &ctx->seeds, ecs_entity_t) = e;
            }
        }

        ecs_vec_fini_t(NULL, &list, ecs_entity_t);
    }
}

/** Turn prototypes that are no longer under an array back into regular entities.
 *
 * @param world The world
 */
static void prototypes_release(
    ecs_world_t *world)
{
    ecs_vec_t list = collect(world, ArrayPrototype);
    for (int i = 0; i < ecs_vec_count(&list); i++) {
        ecs_entity_t e = *ecs_vec_get_t(&list, ecs_entity_t, i), parent = ecs_get_parent(world, e);
        while (parent && !ecs_has(world, parent, Array)) {
            parent = ecs_get_parent(world, parent);
        }

        if (!parent) {
            ecs_remove_id(world, e, EcsPrefab);
            ecs_remove_id(world, e, ArrayPrototype);
        }
    }

    ecs_vec_fini_t(NULL, &list, ecs_entity_t);
}

void box_spatial_layout_arrays(
    layout_ctx_t *ctx,
    int32_t seed_count)
{
    ecs_world_t *world = ctx->world;
    array_roots(ctx, seed_count);
    if (ecs_vec_count(&ctx->roots) || ctx->arrays_dirty) {
        ctx->arrays_dirty = false;
        instances_prune(world);
        arrays_sync(ctx);
        prototypes_release(world);
    }
}
