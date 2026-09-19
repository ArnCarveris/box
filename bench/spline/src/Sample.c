/**
 * @file Sample.c
 * @brief Benchmarks for sampling a curve by parameter and by arc length.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/spline/spline_internal.h"

/** Control points of the benchmarked curves */
#define BENCH_POINTS (65)

/** Samples taken per iteration */
#define BENCH_SAMPLES (1000)

void Sample_catmull_by_parameter(bench_t *b)
{
    // splines/spline_core
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxSpline);

    ecs_entity_t e = ecs_new(world);
    Spline *s = ecs_ensure(world, e, Spline);
    box_spline_sample_t sample;
    int32_t i;

    ecs_vec_init_t(NULL, &s->points, vec3, BENCH_POINTS);
    for (i = 0; i < BENCH_POINTS; i ++) {
        float a = (float)i * 0.2f;

        ecs_vec_append_t(NULL, &s->points, vec3)[0] =
            vec3_new(a, (float)(i % 5), a * 0.5f);
    }

    ecs_modified(world, e, Spline);

    const Spline *curve = ecs_get(world, e, Spline);
    bench_set_items(b, BENCH_SAMPLES);

    while (bench_iter(b)) {
        for (i = 0; i < BENCH_SAMPLES; i ++) {
            box_spline_eval(curve, (float)i / (float)BENCH_SAMPLES, &sample);
            bench_keep(sample.position.x);
        }
    }

    ecs_fini(world);
}

void Sample_linear_by_parameter(bench_t *b)
{
    // splines/spline_core
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxSpline);

    ecs_entity_t e = ecs_new(world);
    Spline *s = ecs_ensure(world, e, Spline);
    box_spline_sample_t sample;
    int32_t i;

    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, BENCH_POINTS);
    for (i = 0; i < BENCH_POINTS; i ++) {
        float a = (float)i * 0.2f;

        ecs_vec_append_t(NULL, &s->points, vec3)[0] =
            vec3_new(a, (float)(i % 5), a * 0.5f);
    }

    ecs_modified(world, e, Spline);

    const Spline *curve = ecs_get(world, e, Spline);
    bench_set_items(b, BENCH_SAMPLES);

    while (bench_iter(b)) {
        for (i = 0; i < BENCH_SAMPLES; i ++) {
            box_spline_eval(curve, (float)i / (float)BENCH_SAMPLES, &sample);
            bench_keep(sample.position.x);
        }
    }

    ecs_fini(world);
}

void Sample_bezier_by_parameter(bench_t *b)
{
    // splines/spline_core
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxSpline);

    ecs_entity_t e = ecs_new(world);
    Spline *s = ecs_ensure(world, e, Spline);
    box_spline_sample_t sample;
    int32_t i;

    s->type = SplineBezier;
    ecs_vec_init_t(NULL, &s->points, vec3, BENCH_POINTS);
    for (i = 0; i < BENCH_POINTS; i ++) {
        float a = (float)i * 0.2f;

        ecs_vec_append_t(NULL, &s->points, vec3)[0] =
            vec3_new(a, (float)(i % 5), a * 0.5f);
    }

    ecs_modified(world, e, Spline);

    const Spline *curve = ecs_get(world, e, Spline);
    bench_set_items(b, BENCH_SAMPLES);

    while (bench_iter(b)) {
        for (i = 0; i < BENCH_SAMPLES; i ++) {
            box_spline_eval(curve, (float)i / (float)BENCH_SAMPLES, &sample);
            bench_keep(sample.position.x);
        }
    }

    ecs_fini(world);
}

void Sample_catmull_by_distance(bench_t *b)
{
    // splines/spline_core
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxSpline);

    ecs_entity_t e = ecs_new(world);
    Spline *s = ecs_ensure(world, e, Spline);
    box_spline_sample_t sample;
    int32_t i;

    ecs_vec_init_t(NULL, &s->points, vec3, BENCH_POINTS);
    for (i = 0; i < BENCH_POINTS; i ++) {
        float a = (float)i * 0.2f;

        ecs_vec_append_t(NULL, &s->points, vec3)[0] =
            vec3_new(a, (float)(i % 5), a * 0.5f);
    }

    ecs_modified(world, e, Spline);

    float length = box_spline_length(world, e);
    bench_set_items(b, BENCH_SAMPLES);

    while (bench_iter(b)) {
        for (i = 0; i < BENCH_SAMPLES; i ++) {
            box_spline_sample_at(world, e,
                length * (float)i / (float)BENCH_SAMPLES, &sample);
            bench_keep(sample.position.x);
        }
    }

    ecs_fini(world);
}
