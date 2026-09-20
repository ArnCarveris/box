/**
 * @file Playback.c
 * @brief Tests for the playback states of an emitter and for its burst schedules.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/particle/particle_internal.h"

void Playback_an_emitter_without_a_playback_plays(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 1);
    test_int(block->flags & BOX_PARTICLE_PAUSED, 0);
    test_flt(block->delta, 1.0f / 60.0f);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_playback_starts_out_playing(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_add(w, e, ParticlePlayback);

    const ParticlePlayback *playback = ecs_get(w, e, ParticlePlayback);
    test_int(playback->state, ParticlePlaying);
    test_bool(playback->reset, false);

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 1);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_stopping_ends_the_emission(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_set(w, e, ParticlePlayback, {.state = ParticleStopped});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 0);
    test_int(block->flags & BOX_PARTICLE_PAUSED, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_stopped_emitter_keeps_its_particles_moving(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_set(w, e, ParticlePlayback, {.state = ParticleStopped});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->delta, 1.0f / 60.0f);
    test_int(block->flags & BOX_PARTICLE_PAUSED, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_pausing_freezes_the_pool(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_set(w, e, ParticlePlayback, {.state = ParticlePaused});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 0);
    test_flt(block->delta, 0);
    test_int(block->flags & BOX_PARTICLE_PAUSED, BOX_PARTICLE_PAUSED);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_playing_again_carries_on_from_the_pause(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 30});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 0);

    ecs_set(w, e, ParticlePlayback, {.state = ParticlePaused});
    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 0);

    ecs_set(w, e, ParticlePlayback, {.state = ParticlePlaying});
    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 1);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_the_clock_only_runs_while_the_emitter_plays(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_add(w, e, ParticlePlayback);

    ecs_progress(w, 0.5f);
    box_particle_extract(w, &packet);
    test_flt(ecs_get(w, e, ParticlePlayback)->time, 0.5);

    ParticlePlayback *playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->state = ParticlePaused;
    ecs_progress(w, 0.5f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_flt(ecs_get(w, e, ParticlePlayback)->time, 0.5);

    playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->state = ParticlePlaying;
    ecs_progress(w, 0.5f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_flt(ecs_get(w, e, ParticlePlayback)->time, 1.0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_starting_a_stopped_emitter_rewinds_its_clock(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_add(w, e, ParticlePlayback);

    ecs_progress(w, 0.5f);
    box_particle_extract(w, &packet);
    test_flt(ecs_get(w, e, ParticlePlayback)->time, 0.5);

    ParticlePlayback *playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->state = ParticleStopped;
    ecs_progress(w, 0.5f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);

    playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->state = ParticlePlaying;
    ecs_progress(w, 0.25f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_flt(ecs_get(w, e, ParticlePlayback)->time, 0.25);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_pause_and_a_play_keep_the_clock(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_add(w, e, ParticlePlayback);

    ecs_progress(w, 0.5f);
    box_particle_extract(w, &packet);

    ParticlePlayback *playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->state = ParticlePaused;
    ecs_progress(w, 0.5f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);

    playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->state = ParticlePlaying;
    ecs_progress(w, 0.25f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_flt(ecs_get(w, e, ParticlePlayback)->time, 0.75);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_reset_empties_the_pool_and_clears_the_trigger(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_add(w, e, ParticlePlayback);

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);

    ParticlePlayback *playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->reset = true;
    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->flags & BOX_PARTICLE_RESET, BOX_PARTICLE_RESET);
    test_bool(ecs_get(w, e, ParticlePlayback)->reset, false);

    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    block = box_vec_at(&packet.particles, 0);
    test_int(block->flags & BOX_PARTICLE_RESET, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_reset_rewinds_the_clock(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_add(w, e, ParticlePlayback);

    ecs_progress(w, 0.5f);
    box_particle_extract(w, &packet);
    test_flt(ecs_get(w, e, ParticlePlayback)->time, 0.5);

    ParticlePlayback *playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->reset = true;
    ecs_progress(w, 0.25f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_flt(ecs_get(w, e, ParticlePlayback)->time, 0.25);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_burst_without_a_time_goes_off_once(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});
    ecs_set(w, e, ParticleBurst, {.count = 200});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 200);

    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_burst_goes_off_at_its_time(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});
    ecs_set(w, e, ParticleBurst, {.count = 150, .at = 0.5f});

    uint32_t total = 0, frames_before = 0;
    int32_t frame;
    for (frame = 0; frame < 16; frame ++) {
        ecs_progress(w, 0.125f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        uint32_t spawn =
            ((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn;
        if (!total && !spawn) {
            frames_before ++;
        }

        total += spawn;
    }

    test_int(total, 150);
    test_int(frames_before, 3);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_burst_repeats_on_its_period(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});
    ecs_set(w, e, ParticleBurst, {.count = 10, .at = 0.25f, .repeat = 2});

    uint32_t total = 0;
    int32_t frame;
    for (frame = 0; frame < 16; frame ++) {
        ecs_progress(w, 0.125f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        total += ((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn;
    }

    test_int(total, 30);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_burst_repeats_while_the_emitter_plays(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});
    ecs_set(w, e, ParticleBurst, {.count = 10, .at = 0.25f, .repeat = -1});

    uint32_t total = 0;
    int32_t frame;
    for (frame = 0; frame < 16; frame ++) {
        ecs_progress(w, 0.125f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        total += ((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn;
    }

    test_int(total, 80);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_burst_runs_alongside_the_rate(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 60});
    ecs_set(w, e, ParticleBurst, {.count = 50});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 51);

    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 1);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_burst_is_capped_by_the_pool(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 64, .rate = 0});
    ecs_set(w, e, ParticleBurst, {.count = 5000});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 64);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_burst_on_a_child_reaches_its_emitter(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});
    ecs_entity_t schedule = ecs_new_w_pair(w, EcsChildOf, e);
    ecs_set(w, schedule, ParticleBurst, {.count = 120});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 120);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_two_schedules_of_an_emitter_add_up(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});
    ecs_set(w, e, ParticleBurst, {.count = 30});
    ecs_entity_t schedule = ecs_new_w_pair(w, EcsChildOf, e);
    ecs_set(w, schedule, ParticleBurst, {.count = 70});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 100);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_stopped_emitter_holds_its_schedule(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});
    ecs_set(w, e, ParticleBurst, {.count = 80, .at = 0.25f});
    ecs_set(w, e, ParticlePlayback, {.state = ParticleStopped});

    uint32_t total = 0;
    int32_t frame;
    for (frame = 0; frame < 60; frame ++) {
        ecs_progress(w, 1.0f / 60.0f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
        total += ((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn;
    }

    test_int(total, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_reset_replays_the_schedule(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 0});
    ecs_set(w, e, ParticleBurst, {.count = 90});
    ecs_add(w, e, ParticlePlayback);

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 90);

    ParticlePlayback *playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->reset = true;
    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 90);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Playback_a_schedule_counts_the_bursts_of_a_span(void)
{
    // particles/control
    ParticleBurst burst = { .count = 10, .at = 0.5f, .repeat = 3 };

    test_int(box_particle_due(&burst, 0.0, 0.4), 0);
    test_int(box_particle_due(&burst, 0.4, 0.6), 1);
    test_int(box_particle_due(&burst, 0.0, 1.2), 2);
    test_int(box_particle_due(&burst, 0.0, 100.0), 4);
    test_int(box_particle_due(&burst, 2.0, 100.0), 0);
    test_int(box_particle_due(&burst, 1.0, 1.0), 0);
}

void Playback_a_schedule_without_a_count_never_goes_off(void)
{
    // particles/control
    ParticleBurst burst = { .count = 0, .at = 0.5f, .repeat = 3 };

    test_int(box_particle_due(&burst, 0.0, 100.0), 0);

    burst.count = -5;
    test_int(box_particle_due(&burst, 0.0, 100.0), 0);
}

void Playback_the_script_functions_drive_the_playback(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});

    ecs_bool_t ok = false;
    ecs_entity_t arg = e;
    ecs_value_t argv = { .type = ecs_id(ecs_entity_t), .ptr = &arg };
    ecs_value_t out = { .type = ecs_id(ecs_bool_t), .ptr = &ok };

    test_int(ecs_function_call(w, ecs_lookup(w, "box.particle.pause"), 1, &argv, &out), 0);
    test_bool(ok, true);
    test_int(ecs_get(w, e, ParticlePlayback)->state, ParticlePaused);

    test_int(ecs_function_call(w, ecs_lookup(w, "box.particle.stop"), 1, &argv, &out), 0);
    test_int(ecs_get(w, e, ParticlePlayback)->state, ParticleStopped);

    test_int(ecs_function_call(w, ecs_lookup(w, "box.particle.start"), 1, &argv, &out), 0);
    test_int(ecs_get(w, e, ParticlePlayback)->state, ParticlePlaying);

    test_int(ecs_function_call(w, ecs_lookup(w, "box.particle.reset"), 1, &argv, &out), 0);
    test_bool(ecs_get(w, e, ParticlePlayback)->reset, true);
    test_int(ecs_get(w, e, ParticlePlayback)->state, ParticlePlaying);

    ecs_fini(w);
}

void Playback_a_script_function_answers_an_entity_without_an_emitter(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t e = ecs_new(w);
    ecs_bool_t ok = true;
    ecs_entity_t arg = e;
    ecs_value_t argv = { .type = ecs_id(ecs_entity_t), .ptr = &arg };
    ecs_value_t out = { .type = ecs_id(ecs_bool_t), .ptr = &ok };

    test_int(ecs_function_call(w, ecs_lookup(w, "box.particle.start"), 1, &argv, &out), 0);
    test_bool(ok, false);
    test_bool(ecs_has(w, e, ParticlePlayback), false);

    ecs_fini(w);
}

void Playback_the_script_clock_reads_the_playback(void)
{
    // particles/control
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100, .rate = 60});
    ecs_add(w, e, ParticlePlayback);

    ecs_progress(w, 0.25f);
    box_particle_extract(w, &packet);

    ecs_f64_t time = -1;
    ecs_entity_t arg = e;
    ecs_value_t argv = { .type = ecs_id(ecs_entity_t), .ptr = &arg };
    ecs_value_t out = { .type = ecs_id(ecs_f64_t), .ptr = &time };

    test_int(ecs_function_call(w, ecs_lookup(w, "box.particle.time"), 1, &argv, &out), 0);
    test_flt(time, 0.25);

    box_packet_fini(&packet);
    ecs_fini(w);
}
