/**
 * @file ArcLength.c
 * @brief Tests for the arc length table: lengths, distances and parameters.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/spline/spline_internal.h"

void ArcLength_length_of_a_straight_line(void)
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

    const SplineInfo *info = ecs_get(w, e, SplineInfo);
    test_assert(info != NULL);
    test_flt(info->length, 5);
    test_flt(box_spline_length(w, e), 5);

    ecs_fini(w);
}

void ArcLength_length_of_a_square_loop(void)
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

    test_flt(box_spline_length(w, e), 8);

    ecs_fini(w);
}

void ArcLength_length_of_a_collinear_catmull_curve(void)
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

    test_assert(fabsf(box_spline_length(w, e) - 3) < 0.0001f);

    ecs_fini(w);
}

void ArcLength_point_at_a_distance(void)
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
    test_bool(box_spline_sample_at(w, e, 1, &sample), true);
    test_assert(fabsf(sample.position.x - 1) < 0.001f);
    test_assert(fabsf(sample.position.z) < 0.001f);

    test_bool(box_spline_sample_at(w, e, 2, &sample), true);
    test_assert(fabsf(sample.position.x - 2) < 0.001f);
    test_assert(fabsf(sample.position.z) < 0.001f);

    test_bool(box_spline_sample_at(w, e, 4, &sample), true);
    test_assert(fabsf(sample.position.x - 2) < 0.001f);
    test_assert(fabsf(sample.position.z - 2) < 0.001f);

    ecs_fini(w);
}

void ArcLength_distance_of_a_parameter(void)
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

    const SplineTable *table = NULL;
    test_assert(box_spline_get(w, e, &table) != NULL);
    test_assert(table != NULL);
    test_flt(box_spline_table_distance(table, 0), 0);
    test_assert(fabsf(box_spline_table_distance(table, 0.5f) - 2) < 0.001f);
    test_assert(fabsf(box_spline_table_distance(table, 1) - 6) < 0.001f);

    ecs_fini(w);
}

void ArcLength_parameter_of_a_distance_round_trips(void)
{
    // splines/spline_core
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t e = ecs_new(w);
    Spline *s = ecs_ensure(w, e, Spline);
    ecs_vec_init_t(NULL, &s->points, vec3, 5);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 1);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 0, -1);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(6, 0, 1);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(8, 0, 0);
    ecs_modified(w, e, Spline);

    const SplineTable *table = NULL;
    float length = box_spline_length(w, e);
    int32_t i;
    test_assert(box_spline_get(w, e, &table) != NULL);
    test_assert(length > 8);
    for (i = 0; i <= 8; i ++) {
        float distance = length * (float)i / 8.0f;
        float t = box_spline_table_param(table, distance);
        float back = box_spline_table_distance(table, t);

        test_assert(t >= 0 && t <= 1);
        test_assert(fabsf(back - distance) < 0.001f);
    }

    ecs_fini(w);
}

void ArcLength_distance_past_the_end_clamps(void)
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
    test_bool(box_spline_sample_at(w, e, 100, &sample), true);
    test_flt(sample.position.x, 2);
    test_flt(sample.position.z, 4);

    test_bool(box_spline_sample_at(w, e, -10, &sample), true);
    test_flt(sample.position.x, 0);
    test_flt(sample.position.z, 0);

    ecs_fini(w);
}

void ArcLength_loop_wraps_the_distance(void)
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

    box_spline_sample_t near_start;
    box_spline_sample_t wrapped;
    test_bool(box_spline_sample_at(w, e, 3, &near_start), true);
    test_assert(fabsf(near_start.position.x - 2) < 0.001f);
    test_assert(fabsf(near_start.position.z - 1) < 0.001f);

    test_bool(box_spline_sample_at(w, e, 11, &wrapped), true);
    test_assert(fabsf(wrapped.position.x - near_start.position.x) < 0.001f);
    test_assert(fabsf(wrapped.position.z - near_start.position.z) < 0.001f);

    test_bool(box_spline_sample_at(w, e, -1, &wrapped), true);
    test_assert(fabsf(wrapped.position.x) < 0.001f);
    test_assert(fabsf(wrapped.position.z - 1) < 0.001f);

    ecs_fini(w);
}

void ArcLength_even_steps_along_a_curve(void)
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

    box_spline_sample_t sample;
    test_bool(box_spline_sample_at(w, e, 0, &sample), true);
    test_assert(fabsf(sample.position.x) < 0.001f);
    test_assert(fabsf(sample.position.z) < 0.001f);

    test_bool(box_spline_sample_at(w, e, 2, &sample), true);
    test_assert(fabsf(sample.position.x - 2) < 0.001f);
    test_assert(fabsf(sample.position.z) < 0.001f);

    test_bool(box_spline_sample_at(w, e, 4, &sample), true);
    test_assert(fabsf(sample.position.x - 2) < 0.001f);
    test_assert(fabsf(sample.position.z - 2) < 0.001f);

    test_bool(box_spline_sample_at(w, e, 6, &sample), true);
    test_assert(fabsf(sample.position.x) < 0.001f);
    test_assert(fabsf(sample.position.z - 2) < 0.001f);

    ecs_fini(w);
}
