/**
 * @file Generate.c
 * @brief Benchmarks for the procedural mesh generators.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/geometry/mesh_gen.h"

/** Subdivisions of the generated round shapes */
#define BENCH_SEGMENTS (32)

void Generate_sphere(
    bench_t *b)
{
    // project_tool/bench
    box_mesh_data_t probe = {0};

    ecs_os_init();
    box_mesh_sphere(&probe, BENCH_SEGMENTS, true);
    bench_set_items(b, probe.vertex_count);
    box_mesh_data_fini(&probe);

    while (bench_iter(b)) {
        box_mesh_data_t mesh = {0};
        box_mesh_sphere(&mesh, BENCH_SEGMENTS, true);
        bench_keep(mesh.vertex_count);
        box_mesh_data_fini(&mesh);
    }
}

void Generate_box(
    bench_t *b)
{
    // project_tool/bench
    box_mesh_data_t probe = {0};

    ecs_os_init();
    box_mesh_box(&probe);
    bench_set_items(b, probe.vertex_count);
    box_mesh_data_fini(&probe);

    while (bench_iter(b)) {
        box_mesh_data_t mesh = {0};
        box_mesh_box(&mesh);
        bench_keep(mesh.vertex_count);
        box_mesh_data_fini(&mesh);
    }
}

void Generate_torus(
    bench_t *b)
{
    // project_tool/bench
    box_mesh_data_t probe = {0};

    ecs_os_init();
    box_mesh_torus(&probe, BENCH_SEGMENTS, BENCH_SEGMENTS, true, 0.25f);
    bench_set_items(b, probe.vertex_count);
    box_mesh_data_fini(&probe);

    while (bench_iter(b)) {
        box_mesh_data_t mesh = {0};
        box_mesh_torus(&mesh, BENCH_SEGMENTS, BENCH_SEGMENTS, true, 0.25f);
        bench_keep(mesh.vertex_count);
        box_mesh_data_fini(&mesh);
    }
}

void Generate_capsule(
    bench_t *b)
{
    // project_tool/bench
    box_mesh_data_t probe = {0};

    ecs_os_init();
    box_mesh_capsule(&probe, BENCH_SEGMENTS, true, 2.0f);
    bench_set_items(b, probe.vertex_count);
    box_mesh_data_fini(&probe);

    while (bench_iter(b)) {
        box_mesh_data_t mesh = {0};
        box_mesh_capsule(&mesh, BENCH_SEGMENTS, true, 2.0f);
        bench_keep(mesh.vertex_count);
        box_mesh_data_fini(&mesh);
    }
}
