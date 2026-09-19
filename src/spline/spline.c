/**
 * @file spline/spline.c
 * @brief Registers the box.spline module, its component hooks and its observers.
 *
 * Spline and SplineTable own a vector each, so both carry their own lifecycle
 * hooks: a component registered from C keeps the hooks of its C type and never
 * gets the ones flecs generates for a reflected type. Setting a Spline rebuilds
 * the table of the entity and writes its SplineInfo.
 */

#define BOX_SPLINE_IMPL
#include "spline_internal.h"
#include <string.h>

ECS_COMPONENT_DECLARE(SplineTable);

/** Spline constructor: an empty curve with the uniform catmull rom tension.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_spline_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Spline *spline = ptr;
    int32_t i;

    memset(ptr, 0, (size_t)count * sizeof(Spline));
    for (i = 0; i < count; i ++) {
        spline[i].tension = 0.5f;
    }
}

/** FollowPath constructor: a follower that loops its curve at one unit a second.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_follow_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    FollowPath *path = ptr;
    int32_t i;

    memset(ptr, 0, (size_t)count * sizeof(FollowPath));
    for (i = 0; i < count; i ++) {
        path[i].speed = 1.0f;
    }
}

/** Release the control points of a Spline.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_spline_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Spline *spline = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &spline[i].points, vec3);
    }
}

/** Deep copy the control points of a Spline.
 *
 * @param dst_ptr The components to overwrite
 * @param src_ptr The components to copy
 * @param count Number of components
 * @param ti Type info
 */
static void box_spline_copy(
    void *dst_ptr,
    const void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Spline *dst = dst_ptr;
    Spline *src = ECS_CONST_CAST(Spline*, src_ptr);
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t points = ecs_vec_count(&src[i].points);

        ecs_vec_fini_t(NULL, &dst[i].points, vec3);
        ecs_vec_init_t(NULL, &dst[i].points, vec3, points);
        ecs_vec_set_count_t(NULL, &dst[i].points, vec3, points);
        if (points) {
            memcpy(ecs_vec_first(&dst[i].points), ecs_vec_first(&src[i].points),
                (size_t)points * sizeof(vec3));
        }

        dst[i].type = src[i].type;
        dst[i].tension = src[i].tension;
        dst[i].loop = src[i].loop;
    }
}

/** Hand the control points of a Spline over, leaving the source empty.
 *
 * @param dst_ptr The components to overwrite
 * @param src_ptr The components to empty
 * @param count Number of components
 * @param ti Type info
 */
static void box_spline_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    box_spline_dtor(dst_ptr, count, ti);
    memcpy(dst_ptr, src_ptr, (size_t)count * sizeof(Spline));
    memset(src_ptr, 0, (size_t)count * sizeof(Spline));
}

/** Release the samples of a SplineTable.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_spline_table_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    SplineTable *table = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_spline_table_fini(&table[i]);
    }
}

/** Deep copy the samples of a SplineTable.
 *
 * @param dst_ptr The components to overwrite
 * @param src_ptr The components to copy
 * @param count Number of components
 * @param ti Type info
 */
static void box_spline_table_copy(
    void *dst_ptr,
    const void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    SplineTable *dst = dst_ptr;
    SplineTable *src = ECS_CONST_CAST(SplineTable*, src_ptr);
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t samples = ecs_vec_count(&src[i].distance);

        box_spline_table_fini(&dst[i]);
        ecs_vec_init_t(NULL, &dst[i].distance, float, samples);
        ecs_vec_set_count_t(NULL, &dst[i].distance, float, samples);
        if (samples) {
            memcpy(ecs_vec_first(&dst[i].distance), ecs_vec_first(&src[i].distance),
                (size_t)samples * sizeof(float));
        }

        dst[i].segments = src[i].segments;
        dst[i].steps = src[i].steps;
        dst[i].length = src[i].length;
    }
}

/** Hand the samples of a SplineTable over, leaving the source empty.
 *
 * @param dst_ptr The components to overwrite
 * @param src_ptr The components to empty
 * @param count Number of components
 * @param ti Type info
 */
static void box_spline_table_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    box_spline_table_dtor(dst_ptr, count, ti);
    memcpy(dst_ptr, src_ptr, (size_t)count * sizeof(SplineTable));
    memset(src_ptr, 0, (size_t)count * sizeof(SplineTable));
}

/** Rebuild the arc length table and the info of one curve.
 *
 * The table is written through the stage of the observer, because a curve that
 * gets its first table changes table itself.
 *
 * @param it The observer iterator
 * @param e The curve entity
 * @param spline The curve
 */
static void box_spline_update(
    ecs_iter_t *it,
    ecs_entity_t e,
    const Spline *spline)
{
    SplineTable *cached = ecs_get_mut(it->world, e, SplineTable);
    SplineInfo info = {0};
    if (cached) {
        box_spline_table_build(spline, cached);
        info.length = cached->length;
        info.segments = cached->segments;
    } else {
        SplineTable table = {0};

        box_spline_table_build(spline, &table);
        info.length = table.length;
        info.segments = table.segments;
        ecs_set_ptr(it->stage, e, SplineTable, &table);
        box_spline_table_fini(&table);
    }

    ecs_set_ptr(it->stage, e, SplineInfo, &info);
}

/** Spline observer: the curve changed, so its table and info follow.
 *
 * @param it Iterator over the changed curves
 */
static void box_spline_on_set(
    ecs_iter_t *it)
{
    const Spline *spline = ecs_field(it, Spline, 0);
    bool self = ecs_field_is_self(it, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_spline_update(it, it->entities[i], self ? &spline[i] : spline);
    }
}

/** An entity lost its Spline: the table and the info go with it.
 *
 * @param it Iterator over the entities that lost the component
 */
static void box_spline_on_remove(
    ecs_iter_t *it)
{
    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count ||
        ecs_is_fini(it->world) || ecs_should_quit(it->world);
    int32_t i;
    for (i = 0; !deleting && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (ecs_has(it->world, e, SplineTable)) {
            ecs_remove(it->stage, e, SplineTable);
            ecs_remove(it->stage, e, SplineInfo);
        }
    }
}

void BoxSplineImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxSpline);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);

    ECS_META_COMPONENT(world, SplineType);
    ECS_META_COMPONENT(world, Spline);
    ECS_META_COMPONENT(world, SplineInfo);
    ECS_META_COMPONENT(world, PathMode);
    ECS_META_COMPONENT(world, FollowPath);
    ECS_COMPONENT_DEFINE(world, SplineTable);

    ecs_set_hooks(world, Spline, {
        .ctor = box_spline_ctor,
        .dtor = box_spline_dtor,
        .copy = box_spline_copy,
        .move = box_spline_move
    });

    ecs_set_hooks(world, SplineTable, {
        .ctor = flecs_default_ctor,
        .dtor = box_spline_table_dtor,
        .copy = box_spline_table_copy,
        .move = box_spline_table_move
    });

    ecs_set_hooks(world, SplineInfo, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, FollowPath, { .ctor = box_follow_ctor });

    ecs_add_pair(world, ecs_id(SplineTable), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(SplineInfo), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(FollowPath), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(FollowPath), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(FollowPath), EcsWith, DynamicTransform);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Spline), .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = box_spline_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Spline), .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = box_spline_on_remove
    });

    box_spline_follow_init(world);
    box_spline_functions_init(world, ecs_id(BoxSpline));
}
