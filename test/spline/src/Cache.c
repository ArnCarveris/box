/**
 * @file Cache.c
 * @brief Tests for the cached table: when it is written, rebuilt and removed.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/spline/spline_internal.h"

void Cache_info_is_written_on_set(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    s->loop = true;
    ecs_vec_init_t(NULL, &s->points, vec3, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 2);
    ecs_modified(w, e, Spline);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 4);
    test_flt(info->length, 8);

    const SplineTable *table = NULL;
    test_assert(box_spline_get(w, e, &table) != NULL);
    test_assert(table != NULL);
    test_int(table->segments, 4);
    test_int(table->steps, BOX_SPLINE_STEPS);
    test_int(ecs_vec_count(&table->distance), 4 * BOX_SPLINE_STEPS + 1);

    ecs_fini(w);
}

void Cache_table_is_rebuilt_when_the_points_change(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_modified(w, e, Spline);

    test_flt(box_spline_length(w, e), 2);

    Spline *longer = ecs_get_mut(w, e, Spline);
    test_assert(longer != NULL);
    ecs_vec_append_t(NULL, &longer->points, vec3)[0] = vec3_new(2, 0, 3);
    ecs_modified(w, e, Spline);

    test_flt(box_spline_length(w, e), 5);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 2);
    test_flt(info->length, 5);

    const SplineTable *table = NULL;
    test_assert(box_spline_get(w, e, &table) != NULL);
    test_int(ecs_vec_count(&table->distance), 2 * BOX_SPLINE_STEPS + 1);

    ecs_fini(w);
}

void Cache_table_is_rebuilt_when_the_type_changes(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 3);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 2);
    ecs_modified(w, e, Spline);

    test_flt(box_spline_length(w, e), 4);

    Spline *curved = ecs_get_mut(w, e, Spline);
    test_assert(curved != NULL);
    curved->type = SplineCatmullRom;
    ecs_modified(w, e, Spline);

    test_assert(box_spline_length(w, e) > 4.05f);

    ecs_fini(w);
}

void Cache_table_is_removed_with_the_spline(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_modified(w, e, Spline);

    test_assert(ecs_has(w, e, SplineTable));
    test_assert(ecs_has(w, e, SplineInfo));

    ecs_remove(w, e, Spline);

    test_assert(!ecs_has(w, e, SplineTable));
    test_assert(!ecs_has(w, e, SplineInfo));
    test_flt(box_spline_length(w, e), 0);

    ecs_fini(w);
}

void Cache_instance_gets_its_own_table(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    Spline *s = ecs_ensure(w, prefab, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 3);
    ecs_modified(w, prefab, Spline);

    ecs_entity_t instance = ecs_new_w_pair(w, EcsIsA, prefab);
    const Spline *copy = ecs_get(w, instance, Spline);
    const Spline *original = ecs_get(w, prefab, Spline);
    test_assert(copy != NULL);
    test_assert(original != NULL);
    test_assert(copy != original);
    test_int(ecs_vec_count(&copy->points), 2);
    test_assert(ecs_vec_first(&copy->points) != ecs_vec_first(&original->points));
    test_flt(box_spline_length(w, instance), 3);

    ecs_fini(w);
}

void Cache_deleting_the_curve_removes_it(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_modified(w, e, Spline);

    ecs_delete(w, e);

    box_spline_sample_t sample;
    test_assert(!ecs_is_alive(w, e));
    test_bool(box_spline_sample(w, e, 0.5f, &sample), false);
    test_flt(box_spline_length(w, e), 0);

    ecs_fini(w);
}
