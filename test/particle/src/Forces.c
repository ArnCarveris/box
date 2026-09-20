/**
 * @file Forces.c
 * @brief Tests for the forces of an emitter and for the integrator they drive.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/particle/particle_internal.h"
#include "../../../src/particle/simulate.h"

void Forces_an_emitter_without_forces_carries_none(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .acceleration = {0, -2, 0}});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->acceleration[1], -2);
    test_flt(block->acceleration[3], 0);
    test_flt(block->wind[0], 0);
    test_flt(block->wind[3], 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Forces_gravity_adds_to_the_acceleration_of_the_emitter(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .acceleration = {1, -2, 0}});
    ecs_set(w, e, ParticleForces, {.gravity = {0, -10, 3}});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->acceleration[0], 1);
    test_flt(block->acceleration[1], -12);
    test_flt(block->acceleration[2], 3);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Forces_wind_drag_and_turbulence_reach_the_block(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleForces, {
        .wind = {4, 0, -1}, .drag = 2.5f, .turbulence = 3
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->wind[0], 4);
    test_flt(block->wind[2], -1);
    test_flt(block->acceleration[3], 2.5f);
    test_flt(block->wind[3], 3);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Forces_the_forces_of_the_world_reach_every_emitter(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_singleton_set(w, ParticleForces, {.wind = {6, 0, 0}, .drag = 1});

    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, ParticleEmitter, {.max = 100});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, ParticleEmitter, {.max = 100});

    box_particle_extract(w, &packet);
    test_int(packet.particles.count, 2);

    int32_t i;
    for (i = 0; i < 2; i ++) {
        box_particle_emitter_t *block = box_vec_at(&packet.particles, (uint32_t)i);
        test_flt(block->wind[0], 6);
        test_flt(block->acceleration[3], 1);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Forces_a_referenced_entity_lends_its_forces(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t weather = ecs_new(w);
    ecs_set(w, weather, ParticleForces, {.wind = {0, 0, 5}, .turbulence = 2});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .forces = weather});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->wind[2], 5);
    test_flt(block->wind[3], 2);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Forces_the_three_sources_are_summed(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_singleton_set(w, ParticleForces, {.gravity = {0, -1, 0}, .drag = 1});

    ecs_entity_t shared = ecs_new(w);
    ecs_set(w, shared, ParticleForces, {.gravity = {0, -2, 0}, .drag = 2});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .forces = shared});
    ecs_set(w, e, ParticleForces, {.gravity = {0, -3, 0}, .drag = 3});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->acceleration[1], -6);
    test_flt(block->acceleration[3], 6);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Forces_authored_forces_fall_at_the_gravity_of_earth(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    test_int(ecs_script_run(w, "test",
        "using box.particle\n"
        "smoke {\n"
        "  ParticleEmitter: {rate: 100}\n"
        "  ParticleForces: {drag: 2}\n"
        "}\n", NULL), 0);

    ecs_entity_t e = ecs_lookup(w, "smoke");
    test_assert(e != 0);

    const ParticleForces *f = ecs_get(w, e, ParticleForces);
    test_assert(f != NULL);
    test_flt(f->gravity.y, -9.81f);
    test_flt(f->drag, 2);

    ecs_fini(w);
}

void Forces_gravity_matches_the_analytic_fall(void)
{
    // particles/dynamics
    box_particle_forces_t f = { .acceleration = vec3_new(0, -10, 0) };
    vec3 position = vec3_new(0, 0, 0);
    vec3 velocity = vec3_new(0, 0, 0);
    float delta = 0.01f;
    int32_t steps = 100;
    int32_t i;
    for (i = 0; i < steps; i ++) {
        box_particle_step(&f, vec3_new(0, 0, 0), delta, &position, &velocity);
    }

    float drop = -10.0f * delta * delta * (float)(steps * (steps + 1)) * 0.5f;
    test_assert(fabsf(velocity.y - -10.0f) < 0.0001f);
    test_assert(fabsf(position.y - drop) < 0.0001f);
    test_assert(fabsf(position.y - -5.0f) < 0.06f);
}

void Forces_drag_matches_the_analytic_decay(void)
{
    // particles/dynamics
    box_particle_forces_t f = { .drag = 2 };
    vec3 position = vec3_new(0, 0, 0);
    vec3 velocity = vec3_new(4, 0, 0);
    float delta = 0.01f;
    int32_t steps = 100;
    int32_t i;
    for (i = 0; i < steps; i ++) {
        box_particle_step(&f, vec3_new(0, 0, 0), delta, &position, &velocity);
    }

    float keep = expf(-f.drag * delta);
    float travelled = delta * 4.0f * keep *
        (1.0f - powf(keep, (float)steps)) / (1.0f - keep);
    test_assert(fabsf(velocity.x - 4.0f * expf(-2.0f)) < 0.0001f);
    test_assert(fabsf(position.x - travelled) < 0.0001f);
}

void Forces_drag_pulls_the_velocity_towards_the_wind(void)
{
    // particles/dynamics
    box_particle_forces_t f = { .wind = vec3_new(3, 0, 0), .drag = 5 };
    vec3 position = vec3_new(0, 0, 0);
    vec3 velocity = vec3_new(0, 0, 0);
    int32_t i;
    for (i = 0; i < 120; i ++) {
        box_particle_step(&f, vec3_new(0, 0, 0), 1.0f / 60.0f, &position, &velocity);
    }

    test_assert(fabsf(velocity.x - 3.0f) < 0.001f);
    test_assert(position.x > 5.0f);
}

void Forces_a_particle_without_drag_ignores_the_wind(void)
{
    // particles/dynamics
    box_particle_forces_t f = { .wind = vec3_new(9, 0, 0) };
    vec3 position = vec3_new(0, 0, 0);
    vec3 velocity = vec3_new(0, 2, 0);
    int32_t i;
    for (i = 0; i < 30; i ++) {
        box_particle_step(&f, vec3_new(0, 0, 0), 1.0f / 60.0f, &position, &velocity);
    }

    test_flt(velocity.x, 0);
    test_flt(position.x, 0);
    test_assert(position.y > 0.9f);
}

void Forces_turbulence_is_a_field_of_the_position(void)
{
    // particles/dynamics
    vec3 here = vec3_new(1.5f, 2.0f, -3.0f);
    vec3 a = box_particle_wander(here, 7, 2);
    vec3 b = box_particle_wander(here, 7, 2);
    vec3 elsewhere = box_particle_wander(vec3_new(9, 2, -3), 7, 2);
    vec3 other_seed = box_particle_wander(here, 8, 2);

    test_flt(a.x, b.x);
    test_flt(a.y, b.y);
    test_flt(a.z, b.z);
    test_assert(vec3_length(vec3_sub(a, elsewhere)) > 0.1f);
    test_assert(vec3_length(vec3_sub(a, other_seed)) > 0.1f);
    test_assert(vec3_length(a) <= 2.0f * 1.7321f);
}

void Forces_turbulence_scales_with_its_amplitude(void)
{
    // particles/dynamics
    vec3 here = vec3_new(0.5f, 1.25f, 4.0f);
    vec3 none = box_particle_wander(here, 3, 0);
    vec3 single = box_particle_wander(here, 3, 1);
    vec3 triple = box_particle_wander(here, 3, 3);

    test_flt(none.x, 0);
    test_flt(none.y, 0);
    test_flt(none.z, 0);
    test_assert(fabsf(triple.x - single.x * 3.0f) < 0.0001f);
    test_assert(fabsf(triple.y - single.y * 3.0f) < 0.0001f);
    test_assert(fabsf(triple.z - single.z * 3.0f) < 0.0001f);
}

void Forces_turbulence_pushes_a_particle_off_its_course(void)
{
    // particles/dynamics
    box_particle_forces_t f = { .acceleration = vec3_new(0, 0, 0) };
    vec3 straight = vec3_new(0, 0, 0);
    vec3 wandered = vec3_new(0, 0, 0);
    vec3 v1 = vec3_new(0, 4, 0);
    vec3 v2 = vec3_new(0, 4, 0);
    int32_t i;
    for (i = 0; i < 60; i ++) {
        box_particle_step(&f, vec3_new(0, 0, 0), 1.0f / 60.0f, &straight, &v1);
        box_particle_step(&f, box_particle_wander(wandered, 5, 6), 1.0f / 60.0f,
            &wandered, &v2);
    }

    test_flt(straight.x, 0);
    test_assert(vec3_length(vec3_sub(straight, wandered)) > 0.05f);
}
