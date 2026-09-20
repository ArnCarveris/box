/**
 * @file Mesh.c
 * @brief Benchmarks for regenerating the swept mesh of a long road.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/spline/spline_internal.h"

/** Control points of the benchmarked road */
#define BENCH_MESH_POINTS (1000)

/** Slices the benchmarked road is swept over */
#define BENCH_MESH_SLICES (2000)

/** Author a road of a thousand control points with a four point cross section.
 *
 * @param world The world
 * @param curve Receives the curve entity
 * @return The entity holding the sweep
 */
static ecs_entity_t bench_mesh_road(
    ecs_world_t *world,
    ecs_entity_t *curve)
{
    ecs_entity_t track = ecs_new(world);
    Spline *s = ecs_ensure(world, track, Spline);
    int32_t i;

    ecs_vec_init_t(NULL, &s->points, vec3, BENCH_MESH_POINTS);
    for (i = 0; i < BENCH_MESH_POINTS; i ++) {
        float a = (float)i * 0.25f;

        ecs_vec_append_t(NULL, &s->points, vec3)[0] =
            vec3_new(a, (float)(i % 7) * 0.1f, a * 0.5f);
    }

    ecs_modified(world, track, Spline);

    ecs_entity_t road = ecs_new(world);
    SplineMesh *sweep = ecs_ensure(world, road, SplineMesh);

    sweep->spline = track;
    sweep->segments = BENCH_MESH_SLICES;
    sweep->width = 8;
    sweep->height = 1;
    ecs_vec_init_t(NULL, &sweep->profile, vec2, 4);
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){-0.5f, 0.0f};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){-0.45f, 0.04f};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){0.45f, 0.04f};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){0.5f, 0.0f};
    ecs_modified(world, road, SplineMesh);
    *curve = track;

    return road;
}

void Mesh_road_1000_points(bench_t *b)
{
    // splines/spline_roads
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t curve = 0;
    ecs_entity_t road = bench_mesh_road(world, &curve);

    bench_set_items(b, BENCH_MESH_SLICES);

    while (bench_iter(b)) {
        ecs_modified(world, road, SplineMesh);
        bench_keep(ecs_vec_count(&ecs_get(world, road, Mesh3)->vertices));
    }

    ecs_fini(world);
}

void Mesh_road_1000_points_curve_edit(bench_t *b)
{
    // splines/spline_roads
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t curve = 0;
    ecs_entity_t road = bench_mesh_road(world, &curve);
    float jitter = 0;

    bench_set_items(b, BENCH_MESH_SLICES);

    while (bench_iter(b)) {
        bench_pause(b);
        Spline *s = ecs_get_mut(world, curve, Spline);
        ecs_vec_first_t(&s->points, vec3)[BENCH_MESH_POINTS / 2].y = jitter;
        jitter += 0.5f;
        bench_resume(b);

        ecs_modified(world, curve, Spline);
        bench_keep(ecs_vec_count(&ecs_get(world, road, Mesh3)->vertices));
    }

    ecs_fini(world);
}
