/**
 * @file Reduce.c
 * @brief Benchmarks for the per frame tessellation choice of a field of spheres.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/geometry/geometry_internal.h"

/** Spheres the field holds */
#define BENCH_FIELD_COUNT (10000)

/** Segments the spheres of the field are authored with */
#define BENCH_FIELD_SEGMENTS (32)

/** Spheres along one row of the field */
#define BENCH_FIELD_ROW (40)

/** Height in pixels the field is measured against */
#define BENCH_FIELD_VIEWPORT (1080.0f)

void Reduce_select_10k_still(bench_t *b)
{
    // lod/primitive_reduction
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_lod_ladder_t ladder;
    uint32_t handles[BOX_LOD_LEVELS];
    double triangles[BOX_LOD_LEVELS];
    ecs_entity_t *field = ecs_os_malloc_n(ecs_entity_t, BENCH_FIELD_COUNT);
    ecs_entity_t camera;
    double reduced = 0;
    int32_t i;

    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = (uint32_t)BENCH_FIELD_VIEWPORT;
    camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_singleton_set(world, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    box_lod_ladder(BENCH_FIELD_SEGMENTS, BOX_LOD_MIN_SEGMENTS, &ladder);
    for (i = 0; i < ladder.count; i ++) {
        box_mesh_key_t key = {
            .kind = BoxShapeSphere, .i0 = ladder.segments[i], .smooth = true
        };
        box_mesh_data_t data = {0};
        vec3 min, max;

        handles[i] = box_mesh_cache_get(world, &key, &min, &max);
        box_mesh_sphere(&data, ladder.segments[i], true);
        triangles[i] = (double)data.index_count / 3.0;
        box_mesh_data_fini(&data);
    }

    for (i = 0; i < BENCH_FIELD_COUNT; i ++) {
        field[i] = ecs_new(world);
        ecs_set(world, field[i], Position, {
            (float)(i % BENCH_FIELD_ROW) * 4.0f - 80.0f, 0,
            20.0f + (float)(i / BENCH_FIELD_ROW) * 2.0f
        });
        ecs_set(world, field[i], Sphere, {
            .segments = BENCH_FIELD_SEGMENTS, .smooth = true, .radius = 1
        });
    }

    box_mesh_lod_select(world, BENCH_FIELD_VIEWPORT);
    for (i = 0; i < BENCH_FIELD_COUNT; i ++) {
        uint32_t handle = ecs_get(world, field[i], RenderMesh)->handle;
        int32_t level;

        for (level = 0; level < ladder.count; level ++) {
            if (handles[level] == handle) {
                reduced += triangles[level];
            }
        }
    }

    bench_set_items(b, BENCH_FIELD_COUNT);

    while (bench_iter(b)) {
        box_mesh_lod_select(world, BENCH_FIELD_VIEWPORT);
        bench_counter(b, "triangles_authored", triangles[0] * (double)BENCH_FIELD_COUNT);
        bench_counter(b, "triangles_reduced", reduced);
        bench_keep(reduced);
    }

    box_packet_fini(&packet);
    ecs_os_free(field);
    ecs_fini(world);
}

void Reduce_select_10k_walk(bench_t *b)
{
    // lod/primitive_reduction
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    ecs_entity_t *field = ecs_os_malloc_n(ecs_entity_t, BENCH_FIELD_COUNT);
    ecs_entity_t camera;
    float z = 0;
    int32_t i;

    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = (uint32_t)BENCH_FIELD_VIEWPORT;
    camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_singleton_set(world, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    for (i = 0; i < BENCH_FIELD_COUNT; i ++) {
        field[i] = ecs_new(world);
        ecs_set(world, field[i], Position, {
            (float)(i % BENCH_FIELD_ROW) * 4.0f - 80.0f, 0,
            20.0f + (float)(i / BENCH_FIELD_ROW) * 2.0f
        });
        ecs_set(world, field[i], Sphere, {
            .segments = BENCH_FIELD_SEGMENTS, .smooth = true, .radius = 1
        });
    }

    box_mesh_lod_select(world, BENCH_FIELD_VIEWPORT);
    bench_set_items(b, BENCH_FIELD_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        z += 1.0f;
        ecs_set(world, camera, Position, {0, 0, z});
        bench_resume(b);
        box_mesh_lod_select(world, BENCH_FIELD_VIEWPORT);
        bench_keep(z);
    }

    box_packet_fini(&packet);
    ecs_os_free(field);
    ecs_fini(world);
}
