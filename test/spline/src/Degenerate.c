/**
 * @file Degenerate.c
 * @brief Tests for curves with too few, repeated or incomplete control points.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/spline/spline_internal.h"

void Degenerate_no_points(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    ecs_ensure(w, e, Spline);
    ecs_modified(w, e, Spline);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 0);
    test_flt(info->length, 0);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 0);
    test_flt(sample.position.y, 0);
    test_flt(sample.position.z, 0);
    test_flt(sample.tangent.z, 1);
    test_flt(sample.normal.y, 1);

    ecs_fini(w);
}

void Degenerate_a_single_point(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    ecs_vec_init_t(NULL, &s->points, vec3, 1);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(3, 1, 2);
    ecs_modified(w, e, Spline);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 0);
    test_flt(info->length, 0);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 3);
    test_flt(sample.position.y, 1);
    test_flt(sample.position.z, 2);

    test_bool(box_spline_sample_at(w, e, 4, &sample), true);
    test_flt(sample.position.x, 3);

    ecs_fini(w);
}

void Degenerate_two_identical_points(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(1, 2, 3);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(1, 2, 3);
    ecs_modified(w, e, Spline);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 1);
    test_flt(info->length, 0);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 1);
    test_flt(sample.position.y, 2);
    test_flt(sample.position.z, 3);
    test_flt(vec3_length(sample.tangent), 1);
    test_flt(vec3_length(sample.normal), 1);

    ecs_fini(w);
}

void Degenerate_a_bezier_without_a_full_segment(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineBezier;
    ecs_vec_init_t(NULL, &s->points, vec3, 3);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(1, 2, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_modified(w, e, Spline);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 0);
    test_flt(info->length, 0);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 0);
    test_flt(sample.position.y, 0);

    ecs_fini(w);
}

void Degenerate_a_loop_of_two_points(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    s->loop = true;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_modified(w, e, Spline);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 2);
    test_flt(info->length, 4);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 2);

    test_bool(box_spline_sample(w, e, 0.75f, &sample), true);
    test_flt(sample.position.x, 1);

    ecs_fini(w);
}

void Degenerate_sampling_an_entity_without_a_spline(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), false);
    test_bool(box_spline_sample_at(w, e, 2, &sample), false);
    test_bool(box_spline_sample(w, 0, 0.5f, &sample), false);
    test_flt(box_spline_length(w, e), 0);
    test_flt(box_spline_length(w, 0), 0);

    ecs_fini(w);
}
