/**
 * @file Extract.c
 * @brief Benchmarks for writing the emitters of a world into a frame packet.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/particle/particle_internal.h"

/** Emitters the scaling case extracts */
#define BENCH_EMITTER_COUNT (1000)

/** Triangles the mesh shape of the table case is sampled from */
#define BENCH_TRIANGLE_COUNT (256)

/** Colliders the collision case puts around its emitter */
#define BENCH_COLLIDER_COUNT (256)

void Extract_one_emitter(bench_t *b)
{
    // particles/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {.max = 100000, .rate = 20000, .seed = 3});
    ecs_progress(world, 1.0f / 60.0f);

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_particle_extract(world, &packet);
        bench_keep(packet.particles.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Extract_emitters_1k(bench_t *b)
{
    // particles/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    int32_t i;
    for (i = 0; i < BENCH_EMITTER_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i, 1, 0});
        ecs_set(world, e, ParticleEmitter, {
            .max = 10000, .rate = 2000, .shape = EmitterSphere, .radius = 1,
            .seed = (uint32_t)i + 1
        });
    }

    ecs_progress(world, 1.0f / 60.0f);

    bench_set_items(b, BENCH_EMITTER_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_particle_extract(world, &packet);
        bench_keep(packet.particles.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Extract_mesh_shape_table(bench_t *b)
{
    // particles/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t surface = ecs_new(world);
    Mesh3 *mesh = ecs_ensure(world, surface, Mesh3);
    ecs_vec_init_t(NULL, &mesh->vertices, vec3, BENCH_TRIANGLE_COUNT * 3);
    int32_t i;
    for (i = 0; i < BENCH_TRIANGLE_COUNT; i ++) {
        float x = (float)(i % 16);
        float z = (float)(i / 16);
        ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(x, 0, z);
        ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(x + 1, 0, z);
        ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(x, 0, z + 1);
    }

    ecs_modified(world, surface, Mesh3);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {
        .max = 100000, .rate = 20000, .shape = EmitterMesh, .source = surface, .seed = 5
    });

    ecs_progress(world, 1.0f / 60.0f);

    bench_set_items(b, BOX_PARTICLE_POINTS);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_particle_extract(world, &packet);
        bench_keep(packet.particle_points.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Extract_three_forces(bench_t *b)
{
    // particles/dynamics
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_singleton_set(world, ParticleForces, {.wind = {4, 0, 0}, .drag = 1});

    ecs_entity_t weather = ecs_new(world);
    ecs_set(world, weather, ParticleForces, {.turbulence = 2, .drag = 0.5f});

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {
        .max = 100000, .rate = 20000, .forces = weather, .seed = 3
    });
    ecs_set(world, e, ParticleForces, {.gravity = {0, -9.81f, 0}});
    ecs_progress(world, 1.0f / 60.0f);

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_particle_extract(world, &packet);
        bench_keep(packet.particles.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Extract_collider_table(bench_t *b)
{
    // particles/dynamics
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    int32_t i;
    for (i = 0; i < BENCH_COLLIDER_COUNT; i ++) {
        ecs_entity_t wall = ecs_new(world);
        ecs_set(world, wall, Position, {(float)(i % 16) * 2, 0, (float)(i / 16) * 2});
        ecs_set(world, wall, BoxCollider, {{0, 0, 0}, {0.5f, 1, 0.5f}});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {
        .max = 100000, .rate = 20000, .seed = 4
    });
    ecs_set(world, e, ParticleCollision, {.ground = true, .reach = 12, .bounce = 0.4f});
    ecs_progress(world, 1.0f / 60.0f);

    bench_set_items(b, BOX_PARTICLE_COLLIDERS);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_particle_extract(world, &packet);
        bench_keep(packet.particle_points.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}
