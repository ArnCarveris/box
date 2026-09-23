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

/** Burst schedules the playback case hangs under its emitter */
#define BENCH_BURST_COUNT (4)

/** Drops the pool of the rain column case holds */
#define BENCH_RAIN_MAX (20000)

/** Drops the rain column case spawns per second */
#define BENCH_RAIN_RATE (14000.0f)

void Extract_one_emitter(
    bench_t *b)
{
    // particles/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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

void Extract_emitters_1k(
    bench_t *b)
{
    // particles/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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

void Extract_mesh_shape_table(
    bench_t *b)
{
    // particles/emitter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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

void Extract_three_forces(
    bench_t *b)
{
    // particles/dynamics
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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

void Extract_collider_table(
    bench_t *b)
{
    // particles/dynamics
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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

void Extract_curves_and_appearance(
    bench_t *b)
{
    // particles/appearance
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t texture = ecs_new(world);
    ecs_set(world, texture, RenderTexture, {.handle = 3, .width = 64, .height = 64});

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {
        .max = 100000, .rate = 20000, .seed = 6
    });
    ecs_set(world, e, ParticleCurves, {
        .size = {0.4f, 1.2f, 1.9f, 2.4f},
        .opacity = {0, 1, 0.7f, 0},
        .rotation = {0, 0.2f, 0.4f, 0.6f},
        .color = {
            .start = {255, 245, 210}, .early = {255, 175, 60},
            .late = {225, 70, 25}, .end = {60, 18, 10}
        }
    });
    ecs_set(world, e, ParticleAppearance, {
        .shape = ParticleSprite, .blend = ParticleAdditive, .texture = texture,
        .columns = 4, .rows = 4, .fps = 24, .softness = 0.5f
    });
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

void Extract_playback_and_bursts(
    bench_t *b)
{
    // particles/control
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ParticleEmitter, {.max = 100000, .rate = 20000, .seed = 3});
    ecs_add(world, e, ParticlePlayback);

    int32_t i;
    for (i = 0; i < BENCH_BURST_COUNT; i ++) {
        ecs_entity_t schedule = ecs_new_w_pair(world, EcsChildOf, e);
        ecs_set(world, schedule, ParticleBurst, {
            .count = 500, .at = 0.25f * (float)(i + 1), .repeat = -1
        });
    }

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

void Extract_playback_1k(
    bench_t *b)
{
    // particles/control
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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
        ecs_add(world, e, ParticlePlayback);
        ecs_set(world, e, ParticleBurst, {.count = 100, .at = 0.5f, .repeat = -1});
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

void Extract_budget_1k(
    bench_t *b)
{
    // particles/scale
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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

    ecs_singleton_set(world, ParticleSettings, {
        .max_total = 1000000, .spawn_per_frame = 10000, .cull = true
    });
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

void Extract_cull_1k(
    bench_t *b)
{
    // particles/scale
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    int32_t i;
    for (i = 0; i < BENCH_EMITTER_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i - 500.0f, 1, 0});
        ecs_set(world, e, ParticleEmitter, {
            .max = 10000, .rate = 2000, .shape = EmitterSphere, .radius = 1,
            .speed = {1, 2}, .lifetime = {1, 2}, .seed = (uint32_t)i + 1
        });
    }

    ecs_singleton_set(world, ParticleSettings, {
        .cull = true, .cull_distance = 400, .step_distance = 120
    });
    ecs_progress(world, 1.0f / 60.0f);

    bench_set_items(b, BENCH_EMITTER_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        box_view_t *view = box_vec_push(&packet.views);
        memset(view, 0, sizeof(box_view_t));
        mat4 look = mat4_look_at(vec3_new(0, 0, -60), vec3_new(0, 0, 0),
            vec3_new(0, 1, 0));
        mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
        memcpy(view->view, look.v, sizeof(look.v));
        memcpy(view->proj, proj.v, sizeof(proj.v));
        view->eye[2] = -60;
        bench_resume(b);
        box_particle_extract(world, &packet);
        bench_keep(packet.particles.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Extract_rain_column(
    bench_t *b)
{
    // particles/rain
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 8, 0});
    ecs_set(world, e, ParticleEmitter, {
        .max = BENCH_RAIN_MAX, .rate = BENCH_RAIN_RATE, .shape = EmitterBox,
        .extents = {20, 13, 20}, .direction = {0, -1, 0},
        .speed = {14, 14}, .lifetime = {3, 3}, .size = {0.04f, 0.04f},
        .opacity = {0.35f, 0.35f}, .seed = 17
    });
    ecs_set(world, e, ParticleForces, {.gravity = {0, -8.4f, 0},
        .wind = {8, 0, 8}, .drag = 0.6f});
    ecs_set(world, e, ParticleCollision, {.ground = true, .die = true});
    ecs_set(world, e, ParticleAppearance, {.shape = ParticleCircle, .stretch = 0.03f});
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
