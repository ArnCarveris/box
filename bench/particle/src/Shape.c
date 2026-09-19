/**
 * @file Shape.c
 * @brief Benchmarks for sampling the spawn table of a mesh and a spline shape.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/particle/particle_internal.h"

/** Triangles the mesh of the sampling case holds */
#define BENCH_MESH_TRIANGLES (1000)

/** Control points the curve of the sampling case holds */
#define BENCH_SPLINE_POINTS (64)

void Shape_resample_mesh_1k_triangles(bench_t *b)
{
    // particles/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxParticle);

    ecs_entity_t surface = ecs_new(world);
    Mesh3 *mesh = ecs_ensure(world, surface, Mesh3);
    ecs_vec_init_t(NULL, &mesh->vertices, vec3, BENCH_MESH_TRIANGLES * 3);
    int32_t i;
    for (i = 0; i < BENCH_MESH_TRIANGLES; i ++) {
        float x = (float)(i % 32);
        float z = (float)(i / 32);
        ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(x, 0, z);
        ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(x + 1, 0, z);
        ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(x, 0, z + 1);
    }

    ecs_modified(world, surface, Mesh3);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {
        .max = 1000, .rate = 100, .shape = EmitterMesh, .source = surface, .seed = 1
    });

    ParticleEmitter *emitter = ecs_get_mut(world, e, ParticleEmitter);
    ParticleEmitterImpl *impl = ecs_get_mut(world, e, ParticleEmitterImpl);
    uint32_t seed = 1;

    bench_set_items(b, BOX_PARTICLE_POINTS);

    while (bench_iter(b)) {
        bench_pause(b);
        emitter->seed = ++ seed;
        bench_resume(b);
        box_particle_sample(world, emitter, impl);
        bench_keep(impl->points);
    }

    ecs_fini(world);
}

void Shape_resample_spline(bench_t *b)
{
    // particles/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxParticle);

    ecs_entity_t curve = ecs_new(world);
    Spline *spline = ecs_ensure(world, curve, Spline);
    ecs_vec_init_t(NULL, &spline->points, vec3, BENCH_SPLINE_POINTS);
    int32_t i;
    for (i = 0; i < BENCH_SPLINE_POINTS; i ++) {
        ecs_vec_append_t(NULL, &spline->points, vec3)[0] =
            vec3_new((float)i, (float)(i % 5), (float)(i % 3));
    }

    ecs_modified(world, curve, Spline);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {
        .max = 1000, .rate = 100, .shape = EmitterSpline, .source = curve, .seed = 1
    });

    ParticleEmitter *emitter = ecs_get_mut(world, e, ParticleEmitter);
    ParticleEmitterImpl *impl = ecs_get_mut(world, e, ParticleEmitterImpl);
    uint32_t seed = 1;

    bench_set_items(b, BOX_PARTICLE_POINTS);

    while (bench_iter(b)) {
        bench_pause(b);
        emitter->seed = ++ seed;
        bench_resume(b);
        box_particle_sample(world, emitter, impl);
        bench_keep(impl->points);
    }

    ecs_fini(world);
}
