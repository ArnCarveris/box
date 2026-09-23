/**
 * @file Mesh.c
 * @brief Benchmarks for merging and simplifying a hundred thousand triangle subtree.
 */

#include <box.h>
#include <bake_bench.h>

/** Quads along one side of the grid every source of the subtree holds */
#define BENCH_GRID_SIDE (20)

/** Vertices of that grid */
#define BENCH_GRID_VERTS (441)

/** Indices of that grid */
#define BENCH_GRID_INDICES (2400)

/** Sources of the subtree, which together hold a hundred thousand triangles */
#define BENCH_MESH_COUNT (125)

void Mesh_merge_100k(
    bench_t *b)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    box_lod_mesh_source_t *sources;
    vec3 *verts;
    uint32_t *indices;
    box_lod_merge_desc_t desc;
    box_lod_mesh_t mesh;
    int32_t i, x, z, n = 0;

    box_lod_mesh_builder_init(&builder);
    verts = ecs_os_malloc_n(vec3, BENCH_GRID_VERTS);
    indices = ecs_os_malloc_n(uint32_t, BENCH_GRID_INDICES);
    sources = ecs_os_malloc_n(box_lod_mesh_source_t, BENCH_MESH_COUNT);

    for (z = 0; z <= BENCH_GRID_SIDE; z ++) {
        for (x = 0; x <= BENCH_GRID_SIDE; x ++) {
            verts[z * (BENCH_GRID_SIDE + 1) + x] = vec3_new((float)x,
                (float)((x * 7 + z * 13) % 5) * 0.02f, (float)z);
        }
    }

    for (z = 0; z < BENCH_GRID_SIDE; z ++) {
        for (x = 0; x < BENCH_GRID_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (BENCH_GRID_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 1;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 2;
        }
    }

    for (i = 0; i < BENCH_MESH_COUNT; i ++) {
        sources[i] = (box_lod_mesh_source_t){
            .transform = mat4_translation(vec3_new(
                (float)(i % 5) * 20.0f, (float)(i / 25) * 20.0f,
                (float)((i / 5) % 5) * 20.0f)),
            .vertices = verts, .indices = indices,
            .vertex_count = BENCH_GRID_VERTS, .index_count = BENCH_GRID_INDICES,
            .material = (uint64_t)(i % 4) + 1 };
    }

    desc = (box_lod_merge_desc_t){ .sources = sources, .count = BENCH_MESH_COUNT };

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        box_lod_merge(&builder, &desc, &mesh);
        bench_keep(mesh.index_count);
    }

    ecs_os_free(sources);
    ecs_os_free(indices);
    ecs_os_free(verts);
    box_lod_mesh_builder_fini(&builder);
}

void Mesh_merge_100k_welded(
    bench_t *b)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    box_lod_mesh_source_t *sources;
    vec3 *verts;
    uint32_t *indices;
    box_lod_merge_desc_t desc;
    box_lod_mesh_t mesh;
    int32_t i, x, z, n = 0;

    box_lod_mesh_builder_init(&builder);
    verts = ecs_os_malloc_n(vec3, BENCH_GRID_VERTS);
    indices = ecs_os_malloc_n(uint32_t, BENCH_GRID_INDICES);
    sources = ecs_os_malloc_n(box_lod_mesh_source_t, BENCH_MESH_COUNT);

    for (z = 0; z <= BENCH_GRID_SIDE; z ++) {
        for (x = 0; x <= BENCH_GRID_SIDE; x ++) {
            verts[z * (BENCH_GRID_SIDE + 1) + x] = vec3_new((float)x,
                (float)((x * 7 + z * 13) % 5) * 0.02f, (float)z);
        }
    }

    for (z = 0; z < BENCH_GRID_SIDE; z ++) {
        for (x = 0; x < BENCH_GRID_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (BENCH_GRID_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 1;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 2;
        }
    }

    for (i = 0; i < BENCH_MESH_COUNT; i ++) {
        sources[i] = (box_lod_mesh_source_t){
            .transform = mat4_translation(vec3_new(
                (float)(i % 5) * 20.0f, (float)(i / 25) * 20.0f,
                (float)((i / 5) % 5) * 20.0f)),
            .vertices = verts, .indices = indices,
            .vertex_count = BENCH_GRID_VERTS, .index_count = BENCH_GRID_INDICES,
            .material = (uint64_t)(i % 4) + 1 };
    }

    desc = (box_lod_merge_desc_t){
        .sources = sources, .count = BENCH_MESH_COUNT, .weld = true };

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        box_lod_merge(&builder, &desc, &mesh);
        bench_keep(mesh.index_count);
    }

    ecs_os_free(sources);
    ecs_os_free(indices);
    ecs_os_free(verts);
    box_lod_mesh_builder_fini(&builder);
}

void Mesh_simplify_100k(
    bench_t *b)
{
    // lod/hlod_proxies/merged_meshes
    box_lod_mesh_builder_t builder;
    box_lod_mesh_source_t *sources;
    vec3 *verts;
    uint32_t *indices;
    box_lod_merge_desc_t desc;
    box_lod_simplify_desc_t simplify = { .ratio = 0.25f };
    box_lod_mesh_t mesh;
    int32_t i, x, z, n = 0;

    box_lod_mesh_builder_init(&builder);
    verts = ecs_os_malloc_n(vec3, BENCH_GRID_VERTS);
    indices = ecs_os_malloc_n(uint32_t, BENCH_GRID_INDICES);
    sources = ecs_os_malloc_n(box_lod_mesh_source_t, BENCH_MESH_COUNT);

    for (z = 0; z <= BENCH_GRID_SIDE; z ++) {
        for (x = 0; x <= BENCH_GRID_SIDE; x ++) {
            verts[z * (BENCH_GRID_SIDE + 1) + x] = vec3_new((float)x,
                (float)((x * 7 + z * 13) % 5) * 0.02f, (float)z);
        }
    }

    for (z = 0; z < BENCH_GRID_SIDE; z ++) {
        for (x = 0; x < BENCH_GRID_SIDE; x ++) {
            uint32_t a = (uint32_t)(z * (BENCH_GRID_SIDE + 1) + x);
            indices[n ++] = a;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + 1;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 1;
            indices[n ++] = a + (uint32_t)BENCH_GRID_SIDE + 2;
        }
    }

    for (i = 0; i < BENCH_MESH_COUNT; i ++) {
        sources[i] = (box_lod_mesh_source_t){
            .transform = mat4_translation(vec3_new(
                (float)(i % 5) * 20.0f, (float)(i / 25) * 20.0f,
                (float)((i / 5) % 5) * 20.0f)),
            .vertices = verts, .indices = indices,
            .vertex_count = BENCH_GRID_VERTS, .index_count = BENCH_GRID_INDICES,
            .material = (uint64_t)(i % 4) + 1 };
    }

    desc = (box_lod_merge_desc_t){
        .sources = sources, .count = BENCH_MESH_COUNT, .weld = true };

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_lod_merge(&builder, &desc, &mesh);
        bench_resume(b);
        box_lod_simplify(&builder, &simplify, &mesh);
        bench_keep(mesh.index_count);
    }

    ecs_os_free(sources);
    ecs_os_free(indices);
    ecs_os_free(verts);
    box_lod_mesh_builder_fini(&builder);
}
