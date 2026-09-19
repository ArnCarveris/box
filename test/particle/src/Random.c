/**
 * @file Random.c
 * @brief Tests for the per particle draws and for the ranges they are read through.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/particle/particle_internal.h"
#include "../../../src/particle/simulate.h"

void Random_a_draw_stays_inside_the_unit_range(void)
{
    // particles/dynamics
    uint32_t i;
    for (i = 0; i < 256; i ++) {
        float value = box_particle_random(17, i, 6);
        test_assert(value >= 0.0f);
        test_assert(value < 1.0f);
    }
}

void Random_the_same_seed_draws_the_same_particle(void)
{
    // particles/dynamics
    uint32_t stream;
    for (stream = 0; stream < BOX_PARTICLE_STREAMS; stream ++) {
        test_flt(box_particle_random(42, 900, stream),
            box_particle_random(42, 900, stream));
    }
}

void Random_another_seed_draws_another_particle(void)
{
    // particles/dynamics
    uint32_t same = 0;
    uint32_t i;
    for (i = 0; i < 64; i ++) {
        if (box_particle_random(1, i, 6) == box_particle_random(2, i, 6)) {
            same ++;
        }
    }

    test_int(same, 0);
}

void Random_every_property_is_drawn_from_its_own_stream(void)
{
    // particles/dynamics
    uint32_t a, b;
    for (a = 0; a < BOX_PARTICLE_STREAMS; a ++) {
        for (b = a + 1; b < BOX_PARTICLE_STREAMS; b ++) {
            test_assert(box_particle_random(5, 31, a) != box_particle_random(5, 31, b));
        }
    }
}

void Random_two_particles_of_a_stream_differ(void)
{
    // particles/dynamics
    float sum = 0;
    uint32_t i;
    for (i = 0; i < 1000; i ++) {
        float value = box_particle_random(3, i, 7);
        test_assert(value != box_particle_random(3, i + 1, 7));
        sum += value;
    }

    test_assert(fabsf(sum / 1000.0f - 0.5f) < 0.05f);
}

void Random_a_range_that_does_not_rise_hands_out_its_minimum(void)
{
    // particles/dynamics
    test_flt(box_particle_between(2, 2, 0), 2);
    test_flt(box_particle_between(2, 2, 1), 2);
    test_flt(box_particle_between(2, 1, 0.75f), 2);
}

void Random_a_draw_covers_the_range(void)
{
    // particles/dynamics
    test_flt(box_particle_between(1, 3, 0), 1);
    test_flt(box_particle_between(1, 3, 0.5f), 2);
    test_assert(fabsf(box_particle_between(1, 3, 0.999f) - 3.0f) < 0.01f);
}

void Random_a_cone_of_no_spread_is_its_axis(void)
{
    // particles/dynamics
    vec3 axis = vec3_normalize(vec3_new(1, 2, -1));
    vec3 dir = box_particle_cone(axis, 1, 0.3f, 0.8f);

    test_assert(fabsf(vec3_dot(dir, axis) - 1.0f) < 0.0001f);
}

void Random_a_cone_stays_inside_its_spread(void)
{
    // particles/dynamics
    vec3 axis = vec3_new(0, 1, 0);
    float cos_spread = cosf(30.0f * 3.14159265f / 180.0f);
    float widest = 1.0f;
    uint32_t i;
    for (i = 0; i < 512; i ++) {
        vec3 dir = box_particle_cone(axis, cos_spread,
            box_particle_random(9, i, 4), box_particle_random(9, i, 5));
        float along = vec3_dot(dir, axis);
        test_assert(fabsf(vec3_length(dir) - 1.0f) < 0.0001f);
        test_assert(along >= cos_spread - 0.0001f);
        if (along < widest) {
            widest = along;
        }
    }

    test_assert(widest < cos_spread + 0.02f);
}

void Random_a_cone_spreads_over_the_whole_sphere(void)
{
    // particles/dynamics
    vec3 axis = vec3_new(0, 1, 0);
    float below = 0;
    uint32_t i;
    for (i = 0; i < 512; i ++) {
        vec3 dir = box_particle_cone(axis, -1,
            box_particle_random(4, i, 4), box_particle_random(4, i, 5));
        if (dir.y < 0.0f) {
            below ++;
        }
    }

    test_assert(below > 200);
    test_assert(below < 312);
}

void Random_the_color_range_reaches_the_block(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .color = {200, 100, 50}, .color_variance = {20, 10, 5}
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->color[0], box_srgb_to_linear(180));
    test_flt(block->color[1], box_srgb_to_linear(90));
    test_flt(block->color[2], box_srgb_to_linear(45));
    test_flt(block->color_max[0], box_srgb_to_linear(220));
    test_flt(block->color_max[1], box_srgb_to_linear(110));
    test_flt(block->color_max[2], box_srgb_to_linear(55));

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Random_an_emitter_without_a_variance_draws_one_color(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .color = {30, 60, 90}});

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->color[0], block->color_max[0]);
    test_flt(block->color[1], block->color_max[1]);
    test_flt(block->color[2], block->color_max[2]);
    test_flt(block->color[0], box_srgb_to_linear(30));

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Random_a_color_variance_is_held_inside_the_channel(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxParticle);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .color = {250, 5, 128}, .color_variance = {40, 40, 200}
    });

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->color_max[0], box_srgb_to_linear(255));
    test_flt(block->color[1], box_srgb_to_linear(0));
    test_flt(block->color[2], box_srgb_to_linear(0));
    test_flt(block->color_max[2], box_srgb_to_linear(255));

    box_packet_fini(&packet);
    ecs_fini(w);
}
