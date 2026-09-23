/**
 * @file Proxy.c
 * @brief Benchmarks for the proxy generator on a thousand primitive subtree.
 */

#include <box.h>
#include <bake_bench.h>

/** Primitives in the benchmarked subtree */
#define BENCH_PRIM_COUNT (1000)

/** Primitives along one axis of the subtree */
#define BENCH_PRIM_SIDE (10)

void Proxy_subtree_1000(
    bench_t *b)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t *prims;
    box_lod_proxy_desc_t desc;
    box_lod_proxy_t proxy;
    int32_t i;

    box_lod_builder_init(&builder);
    prims = ecs_os_malloc_n(box_lod_primitive_t, BENCH_PRIM_COUNT);

    for (i = 0; i < BENCH_PRIM_COUNT; i ++) {
        float s = 0.5f + (float)(i % 7) * 0.1f;

        prims[i].transform = mat4_translation(vec3_new(
            (float)(i % BENCH_PRIM_SIDE) * 3.0f,
            (float)((i / BENCH_PRIM_SIDE) % BENCH_PRIM_SIDE) * 2.0f,
            (float)(i / (BENCH_PRIM_SIDE * BENCH_PRIM_SIDE)) * 3.0f));
        prims[i].min = vec3_new(-s, -s, -s);
        prims[i].max = vec3_new(s, s, s);
        prims[i].material = 1;
        prims[i].volume = 0;
    }

    desc = (box_lod_proxy_desc_t){
        .primitives = prims, .count = BENCH_PRIM_COUNT, .max_shapes = 8 };

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        box_lod_proxy_build(&builder, &desc, &proxy);
        bench_keep(proxy.count);
    }

    ecs_os_free(prims);
    box_lod_builder_fini(&builder);
}

void Proxy_subtree_1000_materials(
    bench_t *b)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t *prims;
    box_lod_proxy_desc_t desc;
    box_lod_proxy_t proxy;
    int32_t i;

    box_lod_builder_init(&builder);
    prims = ecs_os_malloc_n(box_lod_primitive_t, BENCH_PRIM_COUNT);

    for (i = 0; i < BENCH_PRIM_COUNT; i ++) {
        float s = 0.5f + (float)(i % 7) * 0.1f;

        prims[i].transform = mat4_translation(vec3_new(
            (float)(i % BENCH_PRIM_SIDE) * 3.0f,
            (float)((i / BENCH_PRIM_SIDE) % BENCH_PRIM_SIDE) * 2.0f,
            (float)(i / (BENCH_PRIM_SIDE * BENCH_PRIM_SIDE)) * 3.0f));
        prims[i].min = vec3_new(-s, -s, -s);
        prims[i].max = vec3_new(s, s, s);
        prims[i].material = (uint64_t)(i % 8) + 1;
        prims[i].volume = 0;
    }

    desc = (box_lod_proxy_desc_t){
        .primitives = prims, .count = BENCH_PRIM_COUNT, .max_shapes = 16 };

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        box_lod_proxy_build(&builder, &desc, &proxy);
        bench_keep(proxy.count);
    }

    ecs_os_free(prims);
    box_lod_builder_fini(&builder);
}

void Proxy_subtree_1000_error_bound(
    bench_t *b)
{
    // lod/hlod_proxies/reduced_primitives
    box_lod_builder_t builder;
    box_lod_primitive_t *prims;
    box_lod_proxy_desc_t desc;
    box_lod_proxy_t proxy;
    int32_t i;

    box_lod_builder_init(&builder);
    prims = ecs_os_malloc_n(box_lod_primitive_t, BENCH_PRIM_COUNT);

    for (i = 0; i < BENCH_PRIM_COUNT; i ++) {
        int32_t blob = i / (BENCH_PRIM_COUNT / 8);
        int32_t local = i % (BENCH_PRIM_COUNT / 8);
        float s = 0.5f + (float)(i % 7) * 0.1f;

        prims[i].transform = mat4_translation(vec3_new(
            (float)((blob & 1) * 100 + local % 5 * 2),
            (float)(((blob >> 1) & 1) * 100 + local / 5 % 5 * 2),
            (float)(((blob >> 2) & 1) * 100 + local / 25 * 2)));
        prims[i].min = vec3_new(-s, -s, -s);
        prims[i].max = vec3_new(s, s, s);
        prims[i].material = 1;
        prims[i].volume = 0;
    }

    desc = (box_lod_proxy_desc_t){ .primitives = prims, .count = BENCH_PRIM_COUNT,
        .max_shapes = 1, .max_error = 20.0f };

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        box_lod_proxy_build(&builder, &desc, &proxy);
        bench_keep(proxy.count);
    }

    ecs_os_free(prims);
    box_lod_builder_fini(&builder);
}
