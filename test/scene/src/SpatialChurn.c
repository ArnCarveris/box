/**
 * @file SpatialChurn.c
 * @brief Spatial index: query results after many frames of moves, resizes and deletes.
 *
 * Every case compares queries with a brute force pass over the shapes it placed itself.
 */

#include <box.h>
#include <bake_test.h>
#include <float.h>
#include "spatial.h"

/** Order entity ids ascending.
 *
 * @param a The first id
 * @param b The second id
 * @return Negative when a is smaller, positive when b is smaller
 */
static int entity_compare(
    const void *a,
    const void *b)
{
    ecs_entity_t ea = *(const ecs_entity_t*)a;
    ecs_entity_t eb = *(const ecs_entity_t*)b;

    return (ea > eb) - (ea < eb);
}

void SpatialChurn_static_vs_brute_force(void) {
    // spatial_perf/bvh_quality
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    enum { COUNT = 1000, FRAMES = 80, QUERIES = 30 };
    ecs_entity_t *shapes = ecs_os_malloc_n(ecs_entity_t, COUNT);
    vec3 *pos = ecs_os_malloc_n(vec3, COUNT);
    float *size = ecs_os_malloc_n(float, COUNT);
    ecs_entity_t *expect = ecs_os_malloc_n(ecs_entity_t, COUNT);
    int32_t i, f, q, a;
    srand(2024);
    for (i = 0; i < COUNT; i ++) {
        for (a = 0; a < 3; a ++) {
            pos[i][a] = (float)rand() / (float)RAND_MAX * 100.0f - 50.0f;
        }

        size[i] = 1.0f;
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {pos[i][0], pos[i][1], pos[i][2]});
        ecs_set(world, shapes[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0);
    ecs_vec_t out;
    ecs_vec_init_t(NULL, &out, ecs_entity_t, 0);
    for (f = 0; f < FRAMES; f ++) {
        for (i = 0; i < COUNT; i ++) {
            int32_t r = rand() % 100;
            if (r >= 30) {
                continue;
            }

            if (r < 2) {
                ecs_delete(world, shapes[i]);
                shapes[i] = ecs_new(world);
                size[i] = 1.0f;
                ecs_set(world, shapes[i], Box, {1, 1, 1});
            } else if (r < 4) {
                size[i] = 0.5f + (float)rand() / (float)RAND_MAX * 4.0f;
                ecs_set(world, shapes[i], Box, {size[i], size[i], size[i]});
            }

            for (a = 0; a < 3; a ++) {
                pos[i][a] = r < 7 ? (float)rand() / (float)RAND_MAX * 100.0f - 50.0f :
                    pos[i][a] + (float)rand() / (float)RAND_MAX * 4.0f - 2.0f;
            }

            ecs_set(world, shapes[i], Position, {pos[i][0], pos[i][1], pos[i][2]});
        }

        ecs_progress(world, 0);
        if (f % 10 != 9) {
            continue;
        }

        for (q = 0; q < QUERIES; q ++) {
            int32_t count = 0;
            float c[3], h = 1.0f + (float)rand() / (float)RAND_MAX * 10.0f;
            for (a = 0; a < 3; a ++) {
                c[a] = (float)rand() / (float)RAND_MAX * 110.0f - 55.0f;
            }

            for (i = 0; i < COUNT; i ++) {
                bool hit = true;
                for (a = 0; a < 3; a ++) {
                    hit = hit && fabsf(pos[i][a] - c[a]) <= h + size[i] * 0.5f;
                }

                if (hit) {
                    expect[count ++] = shapes[i];
                }
            }

            ecs_vec_clear(&out);
            test_int(box_spatial_overlap_box(world, 0, (vec3){c[0] - h, c[1] - h, c[2] - h},
                (vec3){c[0] + h, c[1] + h, c[2] + h}, &out), count);
            qsort(expect, (size_t)count, sizeof(ecs_entity_t), entity_compare);
            qsort(ecs_vec_first(&out), (size_t)count, sizeof(ecs_entity_t), entity_compare);
            for (i = 0; i < count; i ++) {
                test_uint(*ecs_vec_get_t(&out, ecs_entity_t, i), expect[i]);
            }

            box_spatial_hit_t hit = {0};
            box_spatial_ray_t ray = { .origin = {c[0], 80, c[2]}, .dir = {0, -1, 0} };
            float best = FLT_MAX;
            for (i = 0; i < COUNT; i ++) {
                float hs = size[i] * 0.5f;
                if (fabsf(pos[i][0] - c[0]) <= hs && fabsf(pos[i][2] - c[2]) <= hs &&
                    80.0f - (pos[i][1] + hs) < best)
                {
                    best = 80.0f - (pos[i][1] + hs);
                }
            }

            test_bool(box_spatial_ray(world, &ray, &hit), best < FLT_MAX);
            if (best < FLT_MAX) {
                test_assert(fabsf(hit.distance - best) < 1e-3f);
            }
        }
    }

    ecs_vec_fini_t(NULL, &out, ecs_entity_t);
    ecs_os_free(shapes);
    ecs_os_free(pos);
    ecs_os_free(size);
    ecs_os_free(expect);
    ecs_fini(world);
}
