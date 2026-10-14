/**
 * @file perf/bounds.c
 * @brief World space bounds of the shapes of a scene.
 */

#include "perf.h"
#include "../geometry/mesh_cache.h"
#include <float.h>

/** Grow bounds by the eight corners of a mesh box transformed into world space.
 *
 * @param m The world transform
 * @param lo The minimum corner of the box in shape space
 * @param hi The maximum corner of the box in shape space
 * @param min The minimum corner to grow
 * @param max The maximum corner to grow
 */
static void box_perf_grow(
    mat4 m,
    const vec3 lo,
    const vec3 hi,
    vec3 min,
    vec3 max)
{
    int32_t i;
    for (i = 0; i < 8; i ++) {
        vec3 corner = {
            (i & 1) ? hi[0] : lo[0],
            (i & 2) ? hi[1] : lo[1],
            (i & 4) ? hi[2] : lo[2]
        };
        vec3 world;
        glm_mat4_mulv3(m, corner, 1.0f, world);
        glm_vec3_minv(min, world, min);
        glm_vec3_maxv(max, world, max);
    }
}

int32_t box_perf_bounds(
    ecs_world_t *world,
    vec3 min,
    vec3 max)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    int32_t result = 0;
    ecs_query_t *q;
    ecs_iter_t it;
    if (!cache) {
        goto done;
    }

    glm_vec3_fill(min, FLT_MAX);
    glm_vec3_fill(max, -FLT_MAX);
    q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        },
        .cache_kind = EcsQueryCacheNone
    });
    it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        const ShapeMesh *shape = ecs_field(&it, ShapeMesh, 0);
        const WorldTransform *wt = ecs_field(&it, WorldTransform, 1);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const box_mesh_t *mesh = box_mesh_cache_get(cache, shape[i].mesh);
            vec3 a, b, lo, hi;
            if (!mesh) {
                continue;
            }

            glm_vec3_mul(ECS_CONST_CAST(float*, mesh->min),
                ECS_CONST_CAST(float*, shape[i].scale), a);
            glm_vec3_mul(ECS_CONST_CAST(float*, mesh->max),
                ECS_CONST_CAST(float*, shape[i].scale), b);
            glm_vec3_minv(a, b, lo);
            glm_vec3_maxv(a, b, hi);
            box_perf_grow(ECS_CONST_CAST(vec4*, wt[i].m), lo, hi, min, max);
            result ++;
        }
    }

    ecs_query_fini(q);
done:
    return result;
}
