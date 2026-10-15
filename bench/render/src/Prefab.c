/**
 * @file Prefab.c
 * @brief Benchmarks for instantiating prefabs with a shape.
 *
 * A case creates instances of one prefab with a shape and a material in a world that already
 * holds 10k of them. Only the instantiation is timed; deleting the instances again is not.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Instances created per iteration */
#define BENCH_PREFAB_INSTANCES (1000)

/** Instances that stay alive during the run */
#define BENCH_PREFAB_RESIDENT (10000)

/** Vertices per side of the Mesh3 grid */
#define BENCH_PREFAB_GRID (32)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_prefab_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Create a headless Box world with a view that drops its packets.
 *
 * @return The world
 */
static ecs_world_t* bench_prefab_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_prefab_drop };
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 50, -50});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .culling = {.enabled = true, .min_dimension_px = 1}});

    return world;
}

/** Give a prefab a material.
 *
 * @param world The world
 * @param prefab The prefab
 */
static void bench_prefab_material(
    ecs_world_t *world,
    ecs_entity_t prefab)
{
    ecs_set(world, prefab, Rgb, {200, 100, 50});
    ecs_set(world, prefab, PbrMaterial, {.metallic = 0, .roughness = 0.5f});
}

/** Instantiate a prefab into a resident population, then time creating more instances.
 *
 * @param b The bench
 * @param world The world
 * @param prefab The prefab
 */
static void bench_prefab_run(
    bench_t *b,
    ecs_world_t *world,
    ecs_entity_t prefab)
{
    ecs_entity_t *instances = ecs_os_malloc_n(ecs_entity_t, BENCH_PREFAB_INSTANCES);
    int32_t i;
    for (i = 0; i < BENCH_PREFAB_RESIDENT; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, e, Position, {(float)(i % 100) * 2, 0, (float)(i / 100) * 2});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    bench_set_items(b, BENCH_PREFAB_INSTANCES);
    while (bench_iter(b)) {
        for (i = 0; i < BENCH_PREFAB_INSTANCES; i ++) {
            ecs_entity_t e = instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
            ecs_set(world, e, Position, {(float)(i % 100) * 2, 2, (float)(i / 100) * 2});
        }

        bench_pause(b);
        for (i = 0; i < BENCH_PREFAB_INSTANCES; i ++) {
            ecs_delete(world, instances[i]);
        }

        bench_resume(b);
    }

    bench_counter(b, "alive", (double)ecs_count_id(world, ecs_pair(EcsIsA, prefab)));
    ecs_os_free(instances);
}

void Prefab_mesh3_instantiate(
    bench_t *b)
{
    // core_fixes/mesh3_inherit
    ecs_world_t *world = bench_prefab_world();
    int32_t n = BENCH_PREFAB_GRID, quads = (n - 1) * (n - 1), x, y;
    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, n * n);
    ecs_vec_init_t(NULL, &m.normals, vec3, n * n);
    ecs_vec_init_t(NULL, &m.uvs, vec2, n * n);
    ecs_vec_init_t(NULL, &m.indices, uint32_t, quads * 6);
    for (y = 0; y < n; y ++) {
        for (x = 0; x < n; x ++) {
            float u = (float)x / (float)(n - 1), v = (float)y / (float)(n - 1);
            glm_vec3_copy((vec3){u - 0.5f, 0.1f * sinf(6.0f * u), v - 0.5f},
                *ecs_vec_append_t(NULL, &m.vertices, vec3));
            glm_vec3_copy((vec3){0, 1, 0}, *ecs_vec_append_t(NULL, &m.normals, vec3));
            vec2 *uv = ecs_vec_append_t(NULL, &m.uvs, vec2);
            (*uv)[0] = u;
            (*uv)[1] = v;
        }
    }

    for (y = 0; y < n - 1; y ++) {
        for (x = 0; x < n - 1; x ++) {
            uint32_t a = (uint32_t)(y * n + x), c = a + (uint32_t)n;
            const uint32_t tris[6] = { a, c, a + 1, a + 1, c, c + 1 };
            ecs_os_memcpy(ecs_vec_grow_t(NULL, &m.indices, uint32_t, 6), tris, sizeof(tris));
        }
    }

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Mesh3, &m);
    bench_prefab_material(world, prefab);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.normals, vec3);
    ecs_vec_fini_t(NULL, &m.uvs, vec2);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);
    bench_prefab_run(b, world, prefab);
    ecs_fini(world);
}

void Prefab_box_instantiate(
    bench_t *b)
{
    // core_fixes/mesh3_inherit
    ecs_world_t *world = bench_prefab_world();
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    bench_prefab_material(world, prefab);
    bench_prefab_run(b, world, prefab);
    ecs_fini(world);
}
