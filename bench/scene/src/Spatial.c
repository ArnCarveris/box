/**
 * @file Spatial.c
 * @brief Benchmarks for the spatial index of a scene with one million static shapes.
 *
 * Shapes of random size stand on a 1000 x 1000 grid with a spacing of 1.5 units. Query cases
 * cast rays down onto the grid and test small boxes and spheres around random grid points.
 * Idle cases time the per frame update of 100k shapes that do not move, a tenth dynamic.
 */

#include <box.h>
#include <bake_bench.h>
#include "spatial.h"
#include "index.h"

/** Shapes per row of the grid */
#define BENCH_ROW (1000)

/** Static shapes of the scene */
#define BENCH_STATIC (BENCH_ROW * BENCH_ROW)

/** Distance between grid points */
#define BENCH_SPACING (1.5f)

/** Queries per iteration of a query case */
#define BENCH_QUERIES (1000)

/** Milliseconds the first frame of the last world took, which indexed every shape */
static double bench_first_frame_ms;

/** Get a random number between 0 and 1.
 *
 * @param seed The state of the generator
 * @return The number
 */
static float bench_random(
    uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;

    return (float)(*seed >> 8) / 16777216.0f;
}

/** Create a world with the static shapes of the grid and run its first frame.
 *
 * @param shapes Receives the shapes, NULL when not needed
 * @return The world
 */
static ecs_world_t* bench_spatial_world(
    ecs_entity_t **shapes)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    uint32_t seed = 1;
    int32_t i;
    if (shapes) {
        *shapes = ecs_os_malloc_n(ecs_entity_t, BENCH_STATIC);
    }

    for (i = 0; i < BENCH_STATIC; i ++) {
        ecs_entity_t e = ecs_new(world);
        float s = 0.3f + bench_random(&seed) * 0.9f;
        ecs_set(world, e, Position, {(float)(i % BENCH_ROW) * BENCH_SPACING,
            bench_random(&seed) * 3.0f, (float)(i / BENCH_ROW) * BENCH_SPACING});
        ecs_set(world, e, Box, {s, s * (0.5f + bench_random(&seed)), s});
        if (shapes) {
            (*shapes)[i] = e;
        }
    }

    ecs_time_t t = {0};
    ecs_time_measure(&t);
    ecs_progress(world, 0);
    bench_first_frame_ms = ecs_time_measure(&t) * 1000.0;

    return world;
}

/** Fill random rays that point down onto the grid.
 *
 * @param rays The rays
 * @param seed The state of the generator
 */
static void bench_spatial_rays(
    box_spatial_ray_t *rays,
    uint32_t *seed)
{
    float extent = BENCH_ROW * BENCH_SPACING;
    int32_t i;
    for (i = 0; i < BENCH_QUERIES; i ++) {
        rays[i] = (box_spatial_ray_t){
            .origin = { bench_random(seed) * extent, 20, bench_random(seed) * extent },
            .dir = { bench_random(seed) - 0.5f, -1, bench_random(seed) - 0.5f }
        };
    }
}

/** Fill random grid points to test around.
 *
 * @param points The points
 * @param seed The state of the generator
 */
static void bench_spatial_points(
    vec3 *points,
    uint32_t *seed)
{
    float extent = BENCH_ROW * BENCH_SPACING;
    int32_t i;
    for (i = 0; i < BENCH_QUERIES; i ++) {
        points[i][0] = bench_random(seed) * extent;
        points[i][1] = 1.0f;
        points[i][2] = bench_random(seed) * extent;
    }
}

void Spatial_build_1m(
    bench_t *b)
{
    // scenes/spatial
    ecs_world_t *world = bench_spatial_world(NULL);
    bench_set_items(b, BENCH_STATIC);
    while (bench_iter(b)) {
        box_spatial_rebuild(world);
    }

    bench_counter(b, "first_frame_ms", bench_first_frame_ms);
    ecs_fini(world);
}

void Spatial_ray_1m(
    bench_t *b)
{
    // scenes/spatial
    ecs_world_t *world = bench_spatial_world(NULL);
    box_spatial_ray_t *rays = ecs_os_malloc_n(box_spatial_ray_t, BENCH_QUERIES);
    uint32_t seed = 7;
    int32_t i, hits = 0;
    bench_spatial_rays(rays, &seed);
    bench_set_items(b, BENCH_QUERIES);
    while (bench_iter(b)) {
        hits = 0;
        for (i = 0; i < BENCH_QUERIES; i ++) {
            box_spatial_hit_t hit;
            hits += box_spatial_ray(world, &rays[i], &hit);
        }

        bench_keep(hits);
    }

    bench_counter(b, "hits", hits);
    ecs_os_free(rays);
    bench_counter(b, "first_frame_ms", bench_first_frame_ms);
    ecs_fini(world);
}

void Spatial_ray_all_1m(
    bench_t *b)
{
    // scenes/spatial
    ecs_world_t *world = bench_spatial_world(NULL);
    box_spatial_ray_t *rays = ecs_os_malloc_n(box_spatial_ray_t, BENCH_QUERIES);
    ecs_vec_t hits;
    uint32_t seed = 7;
    int32_t i, count = 0;
    ecs_vec_init_t(NULL, &hits, box_spatial_hit_t, 0);
    bench_spatial_rays(rays, &seed);
    for (i = 0; i < BENCH_QUERIES; i ++) {
        rays[i].max_distance = 30;
    }

    bench_set_items(b, BENCH_QUERIES);
    while (bench_iter(b)) {
        count = 0;
        for (i = 0; i < BENCH_QUERIES; i ++) {
            ecs_vec_clear(&hits);
            count += box_spatial_ray_all(world, &rays[i], &hits);
        }

        bench_keep(count);
    }

    bench_counter(b, "hits_per_ray", (double)count / BENCH_QUERIES);
    ecs_vec_fini_t(NULL, &hits, box_spatial_hit_t);
    ecs_os_free(rays);
    bench_counter(b, "first_frame_ms", bench_first_frame_ms);
    ecs_fini(world);
}

void Spatial_overlap_box_1m(
    bench_t *b)
{
    // scenes/spatial
    ecs_world_t *world = bench_spatial_world(NULL);
    vec3 *points = ecs_os_malloc_n(vec3, BENCH_QUERIES);
    ecs_vec_t out;
    uint32_t seed = 11;
    int32_t i, count = 0;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    bench_spatial_points(points, &seed);
    bench_set_items(b, BENCH_QUERIES);
    while (bench_iter(b)) {
        count = 0;
        for (i = 0; i < BENCH_QUERIES; i ++) {
            vec3 min, max;
            glm_vec3_subs(points[i], 2.0f, min);
            glm_vec3_adds(points[i], 2.0f, max);
            ecs_vec_clear(&out);
            count += box_spatial_overlap_box(world, 0, min, max, &out);
        }

        bench_keep(count);
    }

    bench_counter(b, "results_per_query", (double)count / BENCH_QUERIES);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_os_free(points);
    bench_counter(b, "first_frame_ms", bench_first_frame_ms);
    ecs_fini(world);
}

void Spatial_overlap_sphere_1m(
    bench_t *b)
{
    // scenes/spatial
    ecs_world_t *world = bench_spatial_world(NULL);
    vec3 *points = ecs_os_malloc_n(vec3, BENCH_QUERIES);
    ecs_vec_t out;
    uint32_t seed = 13;
    int32_t i, count = 0;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    bench_spatial_points(points, &seed);
    bench_set_items(b, BENCH_QUERIES);
    while (bench_iter(b)) {
        count = 0;
        for (i = 0; i < BENCH_QUERIES; i ++) {
            ecs_vec_clear(&out);
            count += box_spatial_overlap_sphere(world, 0, points[i], 2.0f, &out);
        }

        bench_keep(count);
    }

    bench_counter(b, "results_per_query", (double)count / BENCH_QUERIES);
    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_os_free(points);
    bench_counter(b, "first_frame_ms", bench_first_frame_ms);
    ecs_fini(world);
}

/** Turn off the per frame update of the index, so a case can time it on its own.
 *
 * @param world The world
 */
static void bench_spatial_manual(
    ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.spatial.SpatialUpdate");
    ecs_enable(world, system, false);
}

void Spatial_update_static_1pct(
    bench_t *b)
{
    // scenes/spatial
    ecs_entity_t *shapes = NULL;
    ecs_world_t *world = bench_spatial_world(&shapes);
    uint32_t seed = 17;
    int32_t i, moved = BENCH_STATIC / 100;
    box_spatial_stats_t stats = {0};
    bench_spatial_manual(world);
    bench_set_items(b, moved);
    while (bench_iter(b)) {
        bench_pause(b);
        for (i = 0; i < moved; i ++) {
            int32_t index = (int32_t)(bench_random(&seed) * (BENCH_STATIC - 1));
            const Position *p = ecs_get(world, shapes[index], Position);
            ecs_set(world, shapes[index], Position, {p->x + bench_random(&seed) - 0.5f,
                p->y, p->z + bench_random(&seed) - 0.5f});
        }

        ecs_progress(world, 0);
        bench_resume(b);
        box_spatial_update(world);
    }

    box_spatial_stats(world, &stats);
    bench_counter(b, "updated", stats.updated);
    ecs_os_free(shapes);
    bench_counter(b, "first_frame_ms", bench_first_frame_ms);
    ecs_fini(world);
}

void Spatial_update_dynamic_10k(
    bench_t *b)
{
    // scenes/spatial
    ecs_world_t *world = bench_spatial_world(NULL);
    ecs_entity_t *movers = ecs_os_malloc_n(ecs_entity_t, 10000);
    uint32_t seed = 19;
    int32_t i, frame = 0;
    box_spatial_stats_t stats = {0};
    for (i = 0; i < 10000; i ++) {
        movers[i] = ecs_new(world);
        ecs_add(world, movers[i], DynamicTransform);
        ecs_set(world, movers[i], Position, {bench_random(&seed) * BENCH_ROW * BENCH_SPACING, 5,
            bench_random(&seed) * BENCH_ROW * BENCH_SPACING});
        ecs_set(world, movers[i], Sphere, {.segments = 8, .radius = 0.5f});
    }

    ecs_progress(world, 0);
    bench_spatial_manual(world);
    bench_set_items(b, 10000);
    while (bench_iter(b)) {
        bench_pause(b);
        frame ++;
        for (i = 0; i < 10000; i ++) {
            Position *p = ecs_get_mut(world, movers[i], Position);
            p->x += 0.05f * (float)((i + frame / 50) % 3 - 1);
            p->z += 0.05f;
        }

        ecs_progress(world, 0);
        bench_resume(b);
        box_spatial_update(world);
    }

    box_spatial_stats(world, &stats);
    bench_counter(b, "moved_in_tree", stats.moved);
    ecs_os_free(movers);
    bench_counter(b, "first_frame_ms", bench_first_frame_ms);
    ecs_fini(world);
}

void Spatial_update_dynamic_100k_1pct(
    bench_t *b)
{
    // spatial_perf/dynamic_bounds
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *shapes = ecs_os_malloc_n(ecs_entity_t, 100000);
    box_spatial_stats_t stats = {0};
    int32_t i, frame = 0, moved = 0, rebound = 0;
    for (i = 0; i < 100000; i ++) {
        shapes[i] = ecs_new(world);
        ecs_add(world, shapes[i], DynamicTransform);
        ecs_set(world, shapes[i], Position, {(float)(i % BENCH_ROW) * BENCH_SPACING, 0,
            (float)(i / BENCH_ROW) * BENCH_SPACING});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    bench_spatial_manual(world);
    box_spatial_update(world);
    bench_set_items(b, 100000);
    while (bench_iter(b)) {
        bench_pause(b);
        frame ++;
        for (i = 0; i < 100000; i += 100) {
            Position *p = ecs_get_mut(world, shapes[i], Position);
            p->x += 0.05f * (float)((i / 100 + frame / 50) % 3 - 1);
            p->z += 0.03f;
        }

        ecs_progress(world, 0);
        bench_resume(b);
        box_spatial_update(world);
        box_spatial_stats(world, &stats);
        moved += stats.moved;
        rebound += stats.rebound;
    }

    bench_counter(b, "moved_in_tree_per_frame", frame ? (double)moved / frame : 0);
    bench_counter(b, "rebound_per_frame", frame ? (double)rebound / frame : 0);
    ecs_os_free(shapes);
    ecs_fini(world);
}

/** Create a world with dynamic shapes on the grid and run its first frame.
 *
 * @param count Shapes of the world
 * @param shapes Receives the shapes
 * @return The world
 */
static ecs_world_t* bench_spatial_dynamic_world(
    int32_t count,
    ecs_entity_t **shapes)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    *shapes = ecs_os_malloc_n(ecs_entity_t, count);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = (*shapes)[i] = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position, {(float)(i % BENCH_ROW) * BENCH_SPACING, 0,
            (float)(i / BENCH_ROW) * BENCH_SPACING});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    box_spatial_update(world);

    return world;
}

/** Run box overlap queries around random points of a grid of 100k shapes.
 *
 * @param b The bench
 * @param world The world
 * @return Results of the last iteration
 */
static int32_t bench_spatial_overlap_100k(
    bench_t *b,
    ecs_world_t *world)
{
    vec3 *points = ecs_os_malloc_n(vec3, BENCH_QUERIES);
    ecs_vec_t out;
    uint32_t seed = 29;
    int32_t i, count = 0;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    for (i = 0; i < BENCH_QUERIES; i ++) {
        points[i][0] = bench_random(&seed) * BENCH_ROW * BENCH_SPACING;
        points[i][1] = 0;
        points[i][2] = bench_random(&seed) * 100 * BENCH_SPACING;
    }

    while (bench_iter(b)) {
        count = 0;
        for (i = 0; i < BENCH_QUERIES; i ++) {
            vec3 min, max;
            glm_vec3_subs(points[i], 2.0f, min);
            glm_vec3_adds(points[i], 2.0f, max);
            ecs_vec_clear(&out);
            count += box_spatial_overlap_box(world, 0, min, max, &out);
        }

        bench_keep(count);
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_os_free(points);

    return count;
}

void Spatial_overlap_after_teleports_100k(
    bench_t *b)
{
    // spatial_perf/dynamic_bounds
    ecs_entity_t *shapes = NULL;
    ecs_world_t *world = bench_spatial_dynamic_world(100000, &shapes);
    uint32_t seed = 23;
    int32_t i;
    for (i = 0; i < 100000; i += 10) {
        Position *p = ecs_get_mut(world, shapes[i], Position);
        p->x = bench_random(&seed) * BENCH_ROW * BENCH_SPACING;
        p->z = bench_random(&seed) * 100 * BENCH_SPACING;
    }

    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_spatial_update(world);
    }

    bench_set_items(b, BENCH_QUERIES);
    int32_t count = bench_spatial_overlap_100k(b, world);
    bench_counter(b, "results_per_query", (double)count / BENCH_QUERIES);
    ecs_os_free(shapes);
    ecs_fini(world);
}

void Spatial_overlap_sparse_movers_100k(
    bench_t *b)
{
    // spatial_perf/dynamic_bounds
    ecs_entity_t *shapes = NULL;
    ecs_world_t *world = bench_spatial_dynamic_world(100000, &shapes);
    int32_t i, f;
    for (f = 0; f < 30; f ++) {
        for (i = 0; i < 100000; i += 100) {
            ecs_get_mut(world, shapes[i], Position)->x += 0.1f;
        }

        ecs_progress(world, 0);
    }

    box_spatial_update(world);
    bench_set_items(b, BENCH_QUERIES);
    int32_t count = bench_spatial_overlap_100k(b, world);
    bench_counter(b, "results_per_query", (double)count / BENCH_QUERIES);
    ecs_os_free(shapes);
    ecs_fini(world);
}

/** Create a world with 100k static shapes on the grid and move every hundredth of them by up
 * to 3 units on each axis in each of 10 frames, 10k random moves in all.
 *
 * @return The world
 */
static ecs_world_t* bench_spatial_static_moves_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *shapes = ecs_os_malloc_n(ecs_entity_t, 100000);
    uint32_t seed = 37;
    int32_t i, f;
    for (i = 0; i < 100000; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)(i % BENCH_ROW) * BENCH_SPACING, 0,
            (float)(i / BENCH_ROW) * BENCH_SPACING});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    for (f = 0; f < 10; f ++) {
        for (i = 0; i < 100000; i += 100) {
            ecs_entity_t e = shapes[i];
            const Position *p = ecs_get(world, e, Position);
            ecs_set(world, e, Position, {p->x + (bench_random(&seed) - 0.5f) * 6.0f, p->y,
                p->z + (bench_random(&seed) - 0.5f) * 6.0f});
        }

        ecs_progress(world, 0);
    }

    box_spatial_update(world);
    ecs_os_free(shapes);

    return world;
}

/** Report the cost of the static tree, then of the tree built again from scratch.
 *
 * @param b The bench
 * @param world The world
 */
static void bench_spatial_rebuilt_cost(
    bench_t *b,
    ecs_world_t *world)
{
    bench_counter(b, "sah_cost", box_bvh_cost(&box_spatial_of(world)->root->trees[0]));
    box_spatial_rebuild(world);
    bench_counter(b, "sah_cost_rebuilt",
        box_bvh_cost(&box_spatial_of(world)->root->trees[0]));
}

void Spatial_ray_after_static_moves_100k(
    bench_t *b)
{
    // spatial_perf/bvh_quality
    ecs_world_t *world = bench_spatial_static_moves_world();
    box_spatial_ray_t *rays = ecs_os_malloc_n(box_spatial_ray_t, BENCH_QUERIES);
    uint32_t seed = 41;
    int32_t i, hits = 0;
    for (i = 0; i < BENCH_QUERIES; i ++) {
        rays[i] = (box_spatial_ray_t){
            .origin = { bench_random(&seed) * BENCH_ROW * BENCH_SPACING, 20,
                bench_random(&seed) * 100 * BENCH_SPACING },
            .dir = { bench_random(&seed) - 0.5f, -1, bench_random(&seed) - 0.5f }
        };
    }

    bench_set_items(b, BENCH_QUERIES);
    while (bench_iter(b)) {
        hits = 0;
        for (i = 0; i < BENCH_QUERIES; i ++) {
            box_spatial_hit_t hit;
            hits += box_spatial_ray(world, &rays[i], &hit);
        }

        bench_keep(hits);
    }

    bench_counter(b, "hits", hits);
    bench_spatial_rebuilt_cost(b, world);
    ecs_os_free(rays);
    ecs_fini(world);
}

void Spatial_overlap_after_static_moves_100k(
    bench_t *b)
{
    // spatial_perf/bvh_quality
    ecs_world_t *world = bench_spatial_static_moves_world();
    bench_set_items(b, BENCH_QUERIES);
    int32_t count = bench_spatial_overlap_100k(b, world);
    bench_counter(b, "results_per_query", (double)count / BENCH_QUERIES);
    bench_spatial_rebuilt_cost(b, world);
    ecs_fini(world);
}

/** Create a world with static and dynamic shapes that do not move and run its first frames.
 *
 * @param count Shapes of the world, a tenth of them dynamic
 * @return The world
 */
static ecs_world_t* bench_spatial_idle_world(
    int32_t count)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_new(world);
        if (!(i % 10)) {
            ecs_add(world, e, DynamicTransform);
        }

        ecs_set(world, e, Position, {(float)(i % BENCH_ROW) * BENCH_SPACING, 0,
            (float)(i / BENCH_ROW) * BENCH_SPACING});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

void Spatial_idle_100k(
    bench_t *b)
{
    // scenes/spatial_idle_cost
    ecs_world_t *world = bench_spatial_idle_world(100000);
    ecs_entity_t system = ecs_lookup(world, "box.spatial.SpatialUpdate");
    box_spatial_stats_t stats = {0};
    bench_set_items(b, 100000);
    while (bench_iter(b)) {
        ecs_run(world, system, 0, NULL);
    }

    box_spatial_stats(world, &stats);
    bench_counter(b, "indexed", stats.entities);
    bench_counter(b, "updated", stats.updated);
    ecs_fini(world);
}

void Spatial_idle_ray_100k(
    bench_t *b)
{
    // scenes/spatial_idle_cost
    ecs_world_t *world = bench_spatial_idle_world(100000);
    ecs_entity_t system = ecs_lookup(world, "box.spatial.SpatialUpdate");
    box_spatial_ray_t ray = { .origin = {10, 20, 10}, .dir = {0, -1, 0} };
    box_spatial_stats_t stats = {0};
    int32_t hits = 0;
    bench_set_items(b, 100000);
    while (bench_iter(b)) {
        box_spatial_hit_t hit;
        ecs_run(world, system, 0, NULL);
        hits += box_spatial_ray(world, &ray, &hit);
    }

    box_spatial_stats(world, &stats);
    bench_keep(hits);
    bench_counter(b, "indexed", stats.entities);
    bench_counter(b, "moved_in_tree", stats.moved);
    ecs_fini(world);
}
