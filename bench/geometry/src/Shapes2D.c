/**
 * @file Shapes2D.c
 * @brief Benchmarks for the 2D shapes and the analytic border of the fragment stage.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/geometry/geometry_internal.h"
#include "../../../src/render/render_internal.h"

/** Bordered rectangles the radius churn moves */
#define BENCH_SHAPES2D_RECTS (1000)

/** Points the coverage sweep evaluates */
#define BENCH_SHAPES2D_POINTS (1024)

void Shapes2D_generate_circle(bench_t *b)
{
    // shapes_2d/primitives
    box_mesh_data_t probe = {0};

    ecs_os_init();
    box_mesh_ngon(&probe, box_circle_segments(4.0f));
    bench_set_items(b, probe.vertex_count);
    box_mesh_data_fini(&probe);

    while (bench_iter(b)) {
        box_mesh_data_t mesh = {0};
        box_mesh_ngon(&mesh, box_circle_segments(4.0f));
        bench_keep(mesh.vertex_count);
        box_mesh_data_fini(&mesh);
    }
}

void Shapes2D_generate_line(bench_t *b)
{
    // shapes_2d/primitives
    box_mesh_data_t probe = {0};

    ecs_os_init();
    box_mesh_line(&probe, 0.0f, 0.0f, 1.0f, 0.5f, 0.1f);
    bench_set_items(b, probe.vertex_count);
    box_mesh_data_fini(&probe);

    while (bench_iter(b)) {
        box_mesh_data_t mesh = {0};
        box_mesh_line(&mesh, 0.0f, 0.0f, 1.0f, 0.5f, 0.1f);
        bench_keep(mesh.vertex_count);
        box_mesh_data_fini(&mesh);
    }
}

void Shapes2D_coverage_sweep(bench_t *b)
{
    // shapes_2d/primitives
    float radii[4] = { 0.4f, 0.1f, 0.6f, 0.25f };

    bench_set_items(b, BENCH_SHAPES2D_POINTS);

    while (bench_iter(b)) {
        int32_t i;
        for (i = 0; i < BENCH_SHAPES2D_POINTS; i ++) {
            float t = (float)i / (float)BENCH_SHAPES2D_POINTS;
            float d = box_shape2d_distance(t * 2.0f - 1.0f, t - 0.5f, 1.0f, 0.6f, radii);

            bench_keep((int32_t)(box_shape2d_coverage(d, 0.01f) * 255.0f));
        }
    }
}

void Shapes2D_extract_1000_borders(bench_t *b)
{
    // shapes_2d/primitives
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    ecs_entity_t entities[BENCH_SHAPES2D_RECTS];
    int32_t i;

    box_packet_init(&packet);
    packet.width = 1280;
    packet.height = 720;
    for (i = 0; i < BENCH_SHAPES2D_RECTS; i ++) {
        entities[i] = ecs_new(world);
        ecs_set(world, entities[i], Position, {(float)(i % 32), (float)(i / 32), 0});
        ecs_set(world, entities[i], Rect, {2.0f, 1.5f});
        ecs_set(world, entities[i], Border, {.radius = 0.2f, .width = 0.05f});
    }

    box_render_extract(world, &packet);
    for (i = 0; i < (int32_t)packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, (uint32_t)i);

        free(cmd->vertices);
        free(cmd->indices);
    }

    bench_set_items(b, BENCH_SHAPES2D_RECTS);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        for (i = 0; i < BENCH_SHAPES2D_RECTS; i ++) {
            ecs_set(world, entities[i], Border, {.radius = 0.1f + (float)(i & 7) * 0.02f});
        }

        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}
