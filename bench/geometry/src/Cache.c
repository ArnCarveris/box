/**
 * @file Cache.c
 * @brief Benchmarks for mesh cache lookups of an already generated mesh.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/geometry/geometry_internal.h"

/** Shapes kept in the cache while one of them is looked up */
#define BENCH_CACHE_SHAPES (16)

void Cache_hit(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 32, .smooth = true };
    vec3 bounds_min, bounds_max;
    int32_t i;
    for (i = 0; i < BENCH_CACHE_SHAPES; i ++) {
        box_mesh_key_t warm = { .kind = BoxShapeNGon, .i0 = 3 + i };
        uint32_t handle = box_mesh_cache_get(
            world, &warm, &bounds_min, &bounds_max);
        bench_keep(handle);
    }

    uint32_t hit = box_mesh_cache_get(world, &key, &bounds_min, &bounds_max);
    bench_keep(hit);

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        hit = box_mesh_cache_get(world, &key, &bounds_min, &bounds_max);
        bench_keep(hit);
    }

    ecs_fini(world);
}
