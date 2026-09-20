/**
 * @file Table.c
 * @brief Benchmarks for building the arc length table of a curve.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/spline/spline_internal.h"

/** Control points of the benchmarked curve, one per segment plus one */
#define BENCH_TABLE_POINTS (65)

void Table_build_64_segments(bench_t *b)
{
    // splines/spline_core
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    Spline *s = ecs_ensure(world, e, Spline);
    SplineTable table = {0};
    int32_t i;

    ecs_vec_init_t(NULL, &s->points, vec3, BENCH_TABLE_POINTS);
    for (i = 0; i < BENCH_TABLE_POINTS; i ++) {
        float a = (float)i * 0.2f;

        ecs_vec_append_t(NULL, &s->points, vec3)[0] =
            vec3_new(a, (float)(i % 5), a * 0.5f);
    }

    ecs_modified(world, e, Spline);

    const Spline *curve = ecs_get(world, e, Spline);
    bench_set_items(b, BENCH_TABLE_POINTS - 1);

    while (bench_iter(b)) {
        box_spline_table_build(curve, &table);
        bench_keep(table.length);
    }

    box_spline_table_fini(&table);
    ecs_fini(world);
}

void Table_rebuild_on_set(bench_t *b)
{
    // splines/spline_core
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    Spline *s = ecs_ensure(world, e, Spline);
    float jitter = 0;
    int32_t i;

    ecs_vec_init_t(NULL, &s->points, vec3, BENCH_TABLE_POINTS);
    for (i = 0; i < BENCH_TABLE_POINTS; i ++) {
        float a = (float)i * 0.2f;

        ecs_vec_append_t(NULL, &s->points, vec3)[0] =
            vec3_new(a, (float)(i % 5), a * 0.5f);
    }

    ecs_modified(world, e, Spline);
    bench_set_items(b, BENCH_TABLE_POINTS - 1);

    while (bench_iter(b)) {
        bench_pause(b);
        Spline *curve = ecs_get_mut(world, e, Spline);
        ecs_vec_first_t(&curve->points, vec3)[0].y = jitter;
        jitter += 1.0f;
        bench_resume(b);

        ecs_modified(world, e, Spline);
        bench_keep(box_spline_length(world, e));
    }

    ecs_fini(world);
}
