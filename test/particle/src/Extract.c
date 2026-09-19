/**
 * @file Extract.c
 * @brief Tests for the emitter block of the frame packet and the spawn clock.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/particle/particle_internal.h"
#include "../../../src/render/render_internal.h"

void Extract_no_block_without_an_emitter(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    box_particle_extract(w, &packet);
    test_int(packet.particles.count, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_block_carries_the_world_transform(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, ParticleEmitter);
    ecs_set(w, e, Position, {2, 3, 4});

    box_particle_extract(w, &packet);
    test_int(packet.particles.count, 1);

    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->transform[3], 2);
    test_flt(block->transform[7], 3);
    test_flt(block->transform[11], 4);
    test_flt(block->transform[0], 1);
    test_int(block->id, (uint32_t)e);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_block_carries_the_shape_and_the_spread(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .shape = EmitterBox, .radius = 3,
        .extents = {1, 2, 3}, .direction = {0, 0, 2}, .spread = 60
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->shape, BOX_PARTICLE_BOX);
    test_flt(block->extents[0], 1);
    test_flt(block->extents[1], 2);
    test_flt(block->extents[2], 3);
    test_flt(block->extents[3], 3);
    test_flt(block->direction[2], 1);
    test_assert(fabsf(block->direction[3] - 0.5f) < 0.0001f);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_block_carries_the_ranges_and_the_seed(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100,
        .speed = {2, 5}, .lifetime = {0.5f, 1.5f}, .size = {0.1f, 0.4f},
        .rotation = {10, 20}, .opacity = {0.25f, 0.75f},
        .acceleration = {0, -9.8f, 0}, .seed = 4242
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->speed[0], 2);
    test_flt(block->speed[1], 5);
    test_flt(block->lifetime[0], 0.5f);
    test_flt(block->lifetime[1], 1.5f);
    test_flt(block->size[1], 0.4f);
    test_flt(block->rotation[1], 20);
    test_flt(block->opacity[0], 0.25f);
    test_flt(block->acceleration[1], -9.8f);
    test_int(block->seed, 4242);
    test_int(block->max, 100);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_color_reaches_the_block_as_linear(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 10, .color = {255, 0, 0}});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->color[0], 1);
    test_flt(block->color[1], 0);
    test_flt(block->color[2], 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_spawn_count_on_the_fixed_clock(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 60});

    uint32_t total = 0;
    int32_t frame;
    for (frame = 0; frame < 60; frame ++) {
        ecs_progress(w, 1.0f / 60.0f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
        test_int(block->spawn, 1);
        total += block->spawn;
    }

    test_int(total, 60);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_fractional_rate_accumulates_over_frames(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 90});

    uint32_t total = 0;
    int32_t frame;
    for (frame = 0; frame < 4; frame ++) {
        ecs_progress(w, 1.0f / 60.0f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
        test_int(block->spawn, frame & 1 ? 2 : 1);
        total += block->spawn;
    }

    test_int(total, 6);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_a_rate_of_zero_spawns_nothing(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});

    int32_t frame;
    for (frame = 0; frame < 10; frame ++) {
        ecs_progress(w, 1.0f / 60.0f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
        test_int(block->spawn, 0);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_spawn_is_clamped_to_the_maximum(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 10, .rate = 100000});

    int32_t frame;
    for (frame = 0; frame < 5; frame ++) {
        ecs_progress(w, 1.0f / 60.0f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
        test_int(block->spawn, 10);
        test_int(block->max, 10);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_maximum_is_clamped_to_the_pool_ceiling(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100000000, .rate = 10});
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->max, BOX_PARTICLE_MAX);

    ecs_entity_t none = ecs_new(w);
    ecs_set(w, none, ParticleEmitter, {.max = -5, .rate = 10});
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(packet.particles.count, 2);
    uint32_t i;
    for (i = 0; i < packet.particles.count; i ++) {
        block = box_vec_at(&packet.particles, i);
        if (block->id == (uint32_t)none) {
            test_int(block->max, 0);
            test_int(block->spawn, 0);
        }
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_the_pool_resets_when_the_maximum_changes(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 10});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->flags & BOX_PARTICLE_RESET, BOX_PARTICLE_RESET);

    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    block = box_vec_at(&packet.particles, 0);
    test_int(block->flags & BOX_PARTICLE_RESET, 0);

    ParticleEmitter *emitter = ecs_get_mut(w, e, ParticleEmitter);
    emitter->max = 200;
    ecs_modified(w, e, ParticleEmitter);

    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    block = box_vec_at(&packet.particles, 0);
    test_int(block->max, 200);
    test_int(block->flags & BOX_PARTICLE_RESET, BOX_PARTICLE_RESET);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_first_counts_the_particles_spawned_before(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 120});

    uint32_t total = 0;
    int32_t frame;
    for (frame = 0; frame < 5; frame ++) {
        ecs_progress(w, 1.0f / 60.0f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
        test_int(block->first, total);
        total += block->spawn;
    }

    test_int(total, 10);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_mesh_shape_samples_the_surface(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t surface = ecs_new(w);
    Mesh3 *mesh = ecs_ensure(w, surface, Mesh3);
    ecs_vec_init_t(NULL, &mesh->vertices, vec3, 6);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(-1, 0, -1);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(1, 0, -1);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(1, 0, 1);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(-1, 0, -1);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(1, 0, 1);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(-1, 0, 1);
    ecs_modified(w, surface, Mesh3);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .rate = 10, .shape = EmitterMesh, .source = surface, .seed = 7
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->point_count, BOX_PARTICLE_POINTS);
    test_int(block->point_first, 0);
    test_int(packet.particle_points.count, BOX_PARTICLE_POINTS);

    uint32_t i;
    for (i = 0; i < block->point_count; i ++) {
        box_particle_point_t *point = box_vec_at(&packet.particle_points, i);
        test_assert(fabsf(point->position[1]) < 0.0001f);
        test_assert(fabsf(point->position[0]) <= 1.0001f);
        test_assert(fabsf(point->position[2]) <= 1.0001f);
        test_assert(fabsf(fabsf(point->normal[1]) - 1.0f) < 0.0001f);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_spline_shape_samples_the_curve(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t curve = ecs_new(w);
    Spline *spline = ecs_ensure(w, curve, Spline);
    spline->type = SplineLinear;
    ecs_vec_init_t(NULL, &spline->points, vec3, 2);
    ecs_vec_append_t(NULL, &spline->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &spline->points, vec3)[0] = vec3_new(10, 0, 0);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .rate = 10, .shape = EmitterSpline, .source = curve
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->point_count, BOX_PARTICLE_POINTS);

    box_particle_point_t *first = box_vec_at(&packet.particle_points, 0);
    box_particle_point_t *last = box_vec_at(&packet.particle_points,
        BOX_PARTICLE_POINTS - 1);
    test_assert(first->position[0] < last->position[0]);
    test_assert(first->position[0] > 0.0f);
    test_assert(last->position[0] < 10.0f);

    uint32_t i;
    for (i = 0; i < block->point_count; i ++) {
        box_particle_point_t *point = box_vec_at(&packet.particle_points, i);
        test_assert(fabsf(point->position[1]) < 0.0001f);
        test_assert(fabsf(point->position[2]) < 0.0001f);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_a_shape_without_a_table_carries_no_points(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .rate = 10, .shape = EmitterSphere, .radius = 2
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->shape, BOX_PARTICLE_SPHERE);
    test_int(block->point_count, 0);
    test_int(packet.particle_points.count, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Extract_the_render_extract_fills_the_emitters(void)
{
    // particles/emitter
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_set(w, e, Position, {0, 1, 0});

    ecs_progress(w, 1.0f / 60.0f);
    box_render_extract(w, &packet);
    test_int(packet.particles.count, 1);

    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 1);
    test_flt(block->transform[7], 1);

    box_packet_fini(&packet);
    ecs_fini(w);
}
