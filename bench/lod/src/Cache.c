/**
 * @file Cache.c
 * @brief Benchmarks what the generation of one cached proxy costs a worker.
 *
 * A job clusters the primitives of a subtree into a handful of shapes and, when
 * they carry more error than the node allows, merges and simplifies their
 * meshes into one. The extract only pays the content hash, once per subtree.
 */

#include <box.h>
#include <bake_bench.h>

/** Primitives the benchmark subtree holds */
#define BENCH_CACHE_INPUTS (512)

/** Clusters those primitives are spread over */
#define BENCH_CACHE_CLUSTERS (32)

/** World units between two clusters */
#define BENCH_CACHE_SPACING (9.0f)

/** Shapes the primitive set of a proxy aims for */
#define BENCH_CACHE_SHAPES (8)

/** Shapes the merged mesh of a proxy aims for */
#define BENCH_CACHE_FINE (48)

/** Offset of the 64 bit FNV hash the cache compares subtrees with */
#define BENCH_CACHE_OFFSET (14695981039346656037ull)

/** Prime of the 64 bit FNV hash the cache compares subtrees with */
#define BENCH_CACHE_PRIME (1099511628211ull)

/** Fill the primitives of a subtree spread over a grid of clusters.
 *
 * @param out The primitives to fill
 * @param materials Material keys the primitives cycle through
 */
static void bench_cache_subtree(
    box_lod_primitive_t *out,
    int32_t materials)
{
    int32_t i;
    for (i = 0; i < BENCH_CACHE_INPUTS; i ++) {
        int32_t cluster = i % BENCH_CACHE_CLUSTERS;
        float x = (float)(cluster % 8) * BENCH_CACHE_SPACING;
        float z = (float)(cluster / 8) * BENCH_CACHE_SPACING;
        float y = (float)(i / BENCH_CACHE_CLUSTERS) * 1.4f;

        out[i] = (box_lod_primitive_t){
            .transform = mat4_translation(vec3_new(x, y, z)),
            .min = vec3_new(-0.5f, -0.5f, -0.5f),
            .max = vec3_new(0.5f, 0.5f, 0.5f),
            .material = (uint64_t)(i % materials) + 1
        };
    }
}

void Cache_proxy_set_512(
    bench_t *b)
{
    // lod/cache_and_jobs
    box_lod_builder_t builder;
    box_lod_primitive_t *primitives;
    box_lod_proxy_t proxy;

    box_lod_builder_init(&builder);
    primitives = ecs_os_malloc_n(box_lod_primitive_t, BENCH_CACHE_INPUTS);
    bench_cache_subtree(primitives, 4);

    box_lod_proxy_desc_t desc = {
        .primitives = primitives, .count = BENCH_CACHE_INPUTS,
        .max_shapes = BENCH_CACHE_SHAPES
    };

    bench_set_items(b, BENCH_CACHE_INPUTS);

    while (bench_iter(b)) {
        box_lod_proxy_build(&builder, &desc, &proxy);
        bench_counter(b, "shapes", (double)proxy.count);
        bench_counter(b, "error", (double)proxy.error);
        bench_keep(proxy.count);
    }

    box_lod_builder_fini(&builder);
    ecs_os_free(primitives);
}

void Cache_proxy_mesh_512(
    bench_t *b)
{
    // lod/cache_and_jobs
    box_lod_builder_t builder;
    box_lod_mesh_builder_t meshes;
    box_lod_primitive_t *primitives;
    box_lod_mesh_source_t *sources;
    vec3 *verts;
    uint32_t *indices;
    box_lod_proxy_t proxy;
    box_lod_mesh_t mesh;
    int32_t i;
    static const uint32_t faces[36] = {
        0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
        2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5
    };

    box_lod_builder_init(&builder);
    box_lod_mesh_builder_init(&meshes);
    primitives = ecs_os_malloc_n(box_lod_primitive_t, BENCH_CACHE_INPUTS);
    sources = ecs_os_malloc_n(box_lod_mesh_source_t, BENCH_CACHE_FINE);
    verts = ecs_os_malloc_n(vec3, 8);
    indices = ecs_os_malloc_n(uint32_t, 36);
    for (i = 0; i < 8; i ++) {
        verts[i] = vec3_new((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f,
            (i & 4) ? 0.5f : -0.5f);
    }

    memcpy(indices, faces, sizeof(faces));
    bench_cache_subtree(primitives, 4);

    box_lod_proxy_desc_t desc = {
        .primitives = primitives, .count = BENCH_CACHE_INPUTS,
        .max_shapes = BENCH_CACHE_FINE, .max_error = 0.05f
    };
    box_lod_simplify_desc_t simplify = { .ratio = 0.4f, .max_error = 0.2f };

    bench_set_items(b, BENCH_CACHE_INPUTS);

    while (bench_iter(b)) {
        box_lod_merge_desc_t merge = { .sources = sources };

        box_lod_proxy_build(&builder, &desc, &proxy);
        merge.count = proxy.count < BENCH_CACHE_FINE ? proxy.count : BENCH_CACHE_FINE;
        for (i = 0; i < merge.count; i ++) {
            sources[i] = (box_lod_mesh_source_t){
                .transform = mat4_translation(proxy.shapes[i].position),
                .vertices = verts, .indices = indices, .vertex_count = 8,
                .index_count = 36, .material = proxy.shapes[i].material };
        }

        box_lod_merge(&meshes, &merge, &mesh);
        box_lod_simplify(&meshes, &simplify, &mesh);
        bench_counter(b, "shapes", (double)proxy.count);
        bench_counter(b, "vertices", (double)mesh.vertex_count);
        bench_counter(b, "triangles", (double)(mesh.index_count / 3));
        bench_keep(mesh.vertex_count);
    }

    box_lod_mesh_builder_fini(&meshes);
    box_lod_builder_fini(&builder);
    ecs_os_free(primitives);
    ecs_os_free(sources);
    ecs_os_free(verts);
    ecs_os_free(indices);
}

void Cache_content_hash_512(
    bench_t *b)
{
    // lod/cache_and_jobs
    box_lod_primitive_t *primitives;
    const uint8_t *bytes;
    size_t size = BENCH_CACHE_INPUTS * sizeof(box_lod_primitive_t);

    ecs_os_init();
    primitives = ecs_os_malloc_n(box_lod_primitive_t, BENCH_CACHE_INPUTS);
    bytes = (const uint8_t*)primitives;
    bench_cache_subtree(primitives, 4);
    bench_set_items(b, BENCH_CACHE_INPUTS);

    while (bench_iter(b)) {
        uint64_t hash = BENCH_CACHE_OFFSET;
        size_t i;

        for (i = 0; i < size; i ++) {
            hash = (hash ^ bytes[i]) * BENCH_CACHE_PRIME;
        }

        bench_counter(b, "bytes", (double)size);
        bench_keep((int32_t)hash);
    }

    ecs_os_free(primitives);
}
