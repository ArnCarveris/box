/**
 * @file Eval.c
 * @brief Tests for evaluating a curve by parameter, per curve type.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/spline/spline_internal.h"

void Eval_linear_endpoints(void)
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
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 4);
    ecs_modified(w, e, Spline);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0, &sample), true);
    test_flt(sample.position.x, 0);
    test_flt(sample.position.y, 0);
    test_flt(sample.position.z, 0);

    test_bool(box_spline_sample(w, e, 1, &sample), true);
    test_flt(sample.position.x, 2);
    test_flt(sample.position.y, 0);
    test_flt(sample.position.z, 4);

    ecs_fini(w);
}

void Eval_linear_midpoint(void)
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
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 4);
    ecs_modified(w, e, Spline);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 2);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.25f, &sample), true);
    test_flt(sample.position.x, 1);
    test_flt(sample.position.z, 0);

    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 2);
    test_flt(sample.position.z, 0);

    test_bool(box_spline_sample(w, e, 0.75f, &sample), true);
    test_flt(sample.position.x, 2);
    test_flt(sample.position.z, 2);

    ecs_fini(w);
}

void Eval_linear_tangent_and_normal(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 5);
    ecs_modified(w, e, Spline);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.tangent.x, 0);
    test_flt(sample.tangent.y, 0);
    test_flt(sample.tangent.z, 1);
    test_flt(sample.normal.x, 0);
    test_flt(sample.normal.y, 1);
    test_flt(sample.normal.z, 0);

    ecs_fini(w);
}

void Eval_catmull_passes_through_its_points(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    ecs_vec_init_t(NULL, &s->points, vec3, 5);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(1, 1, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(3, 1, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 0, 0);
    ecs_modified(w, e, Spline);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.25f, &sample), true);
    test_flt(sample.position.x, 1);
    test_flt(sample.position.y, 1);

    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 2);
    test_flt(sample.position.y, 0);

    test_bool(box_spline_sample(w, e, 0.75f, &sample), true);
    test_flt(sample.position.x, 3);
    test_flt(sample.position.y, 1);

    ecs_fini(w);
}

void Eval_catmull_midpoint_matches_the_analytic_value(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    ecs_vec_init_t(NULL, &s->points, vec3, 5);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(1, 1, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(3, 1, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 0, 0);
    ecs_modified(w, e, Spline);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.375f, &sample), true);
    test_flt(sample.position.x, 1.5f);
    test_flt(sample.position.y, 0.5f);
    test_flt(sample.position.z, 0);
    test_flt(vec3_length(sample.tangent), 1);
    test_assert(fabsf(sample.tangent.x * 1.5f + sample.tangent.y) < 0.0001f);
    test_assert(fabsf(vec3_dot(sample.tangent, sample.normal)) < 0.0001f);

    ecs_fini(w);
}

void Eval_catmull_of_collinear_points_is_a_line(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    ecs_vec_init_t(NULL, &s->points, vec3, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(3, 0, 0);
    ecs_modified(w, e, Spline);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 1.5f);
    test_flt(sample.position.y, 0);
    test_flt(sample.tangent.x, 1);
    test_flt(sample.tangent.y, 0);
    test_flt(sample.tangent.z, 0);

    ecs_fini(w);
}

void Eval_bezier_endpoints(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineBezier;
    ecs_vec_init_t(NULL, &s->points, vec3, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 2, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 2, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 0, 0);
    ecs_modified(w, e, Spline);

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_int(info->segments, 1);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0, &sample), true);
    test_flt(sample.position.x, 0);
    test_flt(sample.position.y, 0);
    test_flt(sample.tangent.x, 0);
    test_flt(sample.tangent.y, 1);

    test_bool(box_spline_sample(w, e, 1, &sample), true);
    test_flt(sample.position.x, 4);
    test_flt(sample.position.y, 0);
    test_flt(sample.tangent.x, 0);
    test_flt(sample.tangent.y, -1);

    ecs_fini(w);
}

void Eval_bezier_midpoint_matches_the_analytic_value(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineBezier;
    ecs_vec_init_t(NULL, &s->points, vec3, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 2, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 2, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 0, 0);
    ecs_modified(w, e, Spline);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.position.x, 2);
    test_flt(sample.position.y, 1.5f);
    test_flt(sample.position.z, 0);
    test_flt(sample.tangent.x, 1);
    test_flt(sample.tangent.y, 0);

    ecs_fini(w);
}

void Eval_normal_is_perpendicular_to_a_vertical_tangent(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 3, 0);
    ecs_modified(w, e, Spline);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 0.5f, &sample), true);
    test_flt(sample.tangent.y, 1);
    test_flt(vec3_length(sample.normal), 1);
    test_flt(vec3_dot(sample.tangent, sample.normal), 0);

    ecs_fini(w);
}

void Eval_tension_changes_the_curve(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    ecs_vec_init_t(NULL, &s->points, vec3, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 2, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(3, 0, 0);
    ecs_modified(w, e, Spline);

    box_spline_sample_t loose;
    test_bool(box_spline_sample(w, e, 0.5f, &loose), true);

    Spline *tight = ecs_get_mut(w, e, Spline);
    test_assert(tight != NULL);
    tight->tension = 0;
    ecs_modified(w, e, Spline);

    box_spline_sample_t straight;
    test_bool(box_spline_sample(w, e, 0.5f, &straight), true);
    test_flt(straight.position.x, 1.5f);
    test_flt(straight.position.y, 1);
    test_assert(fabsf(loose.position.y - straight.position.y) > 0.01f);

    ecs_fini(w);
}

void Eval_parameter_clamps_outside_the_range(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(6, 0, 0);
    ecs_modified(w, e, Spline);

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, -2, &sample), true);
    test_flt(sample.position.x, 0);

    test_bool(box_spline_sample(w, e, 3, &sample), true);
    test_flt(sample.position.x, 6);

    ecs_fini(w);
}

void Eval_loop_wraps_the_parameter(void)
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

    box_spline_sample_t sample;
    test_bool(box_spline_sample(w, e, 1, &sample), true);
    test_flt(sample.position.x, 0);
    test_flt(sample.position.z, 0);

    test_bool(box_spline_sample(w, e, 1.25f, &sample), true);
    test_flt(sample.position.x, 2);
    test_flt(sample.position.z, 0);

    test_bool(box_spline_sample(w, e, -0.25f, &sample), true);
    test_flt(sample.position.x, 0);
    test_flt(sample.position.z, 2);

    ecs_fini(w);
}
