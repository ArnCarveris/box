/**
 * @file Upload.c
 * @brief Benchmarks for uploading application authored Mesh3 data.
 */

#include <box.h>
#include <bake_bench.h>

/** Vertices per side of the benchmark grid, which is about 100k triangles */
#define BENCH_MESH_SIDE (225)

void Upload_mesh_100k(bench_t *b)
{
    // meshes/mesh3
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e = ecs_new(world);
    Mesh3 *m = ecs_ensure(world, e, Mesh3);
    int32_t side = BENCH_MESH_SIDE;
    int32_t quads = (side - 1) * (side - 1);
    int32_t x, y;
    float jitter = 0.0f;

    ecs_vec_init_t(NULL, &m->vertices, vec3, side * side);
    for (y = 0; y < side; y ++) {
        for (x = 0; x < side; x ++) {
            float h = (float)((x * 7 + y * 13) % 11) * 0.1f;

            ecs_vec_append_t(NULL, &m->vertices, vec3)[0] =
                vec3_new((float)x, h, (float)y);
        }
    }

    ecs_vec_init_t(NULL, &m->indices, uint32_t, quads * 6);
    for (y = 0; y < side - 1; y ++) {
        for (x = 0; x < side - 1; x ++) {
            uint32_t a = (uint32_t)(y * side + x);
            uint32_t c = a + (uint32_t)side;

            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = a;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = a + 1;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = c;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = a + 1;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = c + 1;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = c;
        }
    }

    ecs_modified(world, e, Mesh3);
    bench_set_items(b, quads * 2);

    while (bench_iter(b)) {
        bench_pause(b);
        Mesh3 *data = ecs_get_mut(world, e, Mesh3);
        ecs_vec_first_t(&data->vertices, vec3)[0].y = jitter;
        jitter += 1.0f;
        bench_resume(b);

        ecs_modified(world, e, Mesh3);
        bench_keep(ecs_get(world, e, RenderMesh)->handle);
    }

    ecs_fini(world);
}

void Upload_share_100k(bench_t *b)
{
    // meshes/mesh3
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e = ecs_new(world);
    Mesh3 *m = ecs_ensure(world, e, Mesh3);
    int32_t side = BENCH_MESH_SIDE;
    int32_t quads = (side - 1) * (side - 1);
    int32_t x, y;

    ecs_vec_init_t(NULL, &m->vertices, vec3, side * side);
    for (y = 0; y < side; y ++) {
        for (x = 0; x < side; x ++) {
            float h = (float)((x * 7 + y * 13) % 11) * 0.1f;

            ecs_vec_append_t(NULL, &m->vertices, vec3)[0] =
                vec3_new((float)x, h, (float)y);
        }
    }

    ecs_vec_init_t(NULL, &m->indices, uint32_t, quads * 6);
    for (y = 0; y < side - 1; y ++) {
        for (x = 0; x < side - 1; x ++) {
            uint32_t a = (uint32_t)(y * side + x);
            uint32_t c = a + (uint32_t)side;

            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = a;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = a + 1;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = c;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = a + 1;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = c + 1;
            ecs_vec_append_t(NULL, &m->indices, uint32_t)[0] = c;
        }
    }

    ecs_modified(world, e, Mesh3);
    bench_set_items(b, quads * 2);

    while (bench_iter(b)) {
        ecs_modified(world, e, Mesh3);
        bench_keep(ecs_get(world, e, RenderMesh)->handle);
    }

    ecs_fini(world);
}
