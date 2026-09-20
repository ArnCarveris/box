/**
 * @file Scale.c
 * @brief Tests for the particle budgets, the emitter bounds and the culling decisions.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/particle/particle_internal.h"

void Scale_share_hands_out_the_ask_inside_the_budget(void)
{
    // particles/scale
    test_int(box_particle_share(100, 400, 1000), 100);
    test_int(box_particle_share(100, 1000, 1000), 100);
}

void Scale_share_is_proportional_to_the_ask(void)
{
    // particles/scale
    test_int(box_particle_share(1000, 4000, 2000), 500);
    test_int(box_particle_share(3000, 4000, 2000), 1500);
    test_int(box_particle_share(2000, 8000, 1000), 250);
}

void Scale_share_leaves_every_asker_one_particle(void)
{
    // particles/scale
    test_int(box_particle_share(1, 1000000, 10), 1);
    test_int(box_particle_share(0, 1000000, 10), 0);
}

void Scale_a_budget_of_zero_is_no_budget(void)
{
    // particles/scale
    test_int(box_particle_share(5000, 100000, 0), 5000);
}

void Scale_the_reach_covers_the_flight_of_a_particle(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .radius = 0, .speed = {10, 20}, .lifetime = {1, 2},
        .size = {0, 0}
    });

    ParticleEmitterImpl impl;
    memset(&impl, 0, sizeof(ParticleEmitterImpl));
    float reach = box_particle_reach(ecs_get(w, e, ParticleEmitter), &impl);
    test_assert(reach >= 40.0f);
    test_assert(reach < 41.0f);

    ecs_fini(w);
}

void Scale_the_reach_covers_the_spawn_volume(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .radius = 7, .speed = {0, 0}, .lifetime = {0, 0},
        .size = {0, 0}
    });

    ParticleEmitterImpl impl;
    memset(&impl, 0, sizeof(ParticleEmitterImpl));
    test_flt(box_particle_reach(ecs_get(w, e, ParticleEmitter), &impl), 7.0f);

    ecs_fini(w);
}

void Scale_the_reach_reaches_the_block(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .radius = 2, .speed = {4, 4}, .lifetime = {1, 1}, .size = {0, 0}
    });

    box_view_t *view = box_vec_push(&packet.views);
    memset(view, 0, sizeof(box_view_t));
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    memcpy(view->view, look.v, sizeof(look.v));
    memcpy(view->proj, proj.v, sizeof(proj.v));
    view->eye[2] = -10;

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_flt(block->reach, 6.0f);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_an_emitter_is_simulated_without_a_view(void)
{
    // particles/scale
    ParticleSettings settings;
    memset(&settings, 0, sizeof(ParticleSettings));
    settings.cull = true;
    float origin[3] = { 0, 0, 0 };

    test_int(box_particle_cull(NULL, 0, origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_FULL);
}

void Scale_an_emitter_in_the_frustum_is_simulated(void)
{
    // particles/scale
    box_particle_view_t view;
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    box_frustum_build(look.v, proj.v, &view.frustum);
    view.eye[0] = 0;
    view.eye[1] = 0;
    view.eye[2] = -10;

    ParticleSettings settings;
    memset(&settings, 0, sizeof(ParticleSettings));
    settings.cull = true;
    float origin[3] = { 0, 0, 0 };

    test_int(box_particle_cull(&view, 1, origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_FULL);
}

void Scale_an_emitter_behind_the_camera_is_culled(void)
{
    // particles/scale
    box_particle_view_t view;
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    box_frustum_build(look.v, proj.v, &view.frustum);
    view.eye[0] = 0;
    view.eye[1] = 0;
    view.eye[2] = -10;

    ParticleSettings settings;
    memset(&settings, 0, sizeof(ParticleSettings));
    settings.cull = true;
    float origin[3] = { 0, 0, -40 };

    test_int(box_particle_cull(&view, 1, origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_NONE);
}

void Scale_culling_off_keeps_an_emitter_behind_the_camera(void)
{
    // particles/scale
    box_particle_view_t view;
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    box_frustum_build(look.v, proj.v, &view.frustum);
    view.eye[0] = 0;
    view.eye[1] = 0;
    view.eye[2] = -10;

    ParticleSettings settings;
    memset(&settings, 0, sizeof(ParticleSettings));
    settings.cull = false;
    float origin[3] = { 0, 0, -40 };

    test_int(box_particle_cull(&view, 1, origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_FULL);
}

void Scale_the_margin_grows_the_sphere_of_an_emitter(void)
{
    // particles/scale
    box_particle_view_t view;
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    box_frustum_build(look.v, proj.v, &view.frustum);
    view.eye[0] = 0;
    view.eye[1] = 0;
    view.eye[2] = -10;

    ParticleSettings settings;
    memset(&settings, 0, sizeof(ParticleSettings));
    settings.cull = true;
    float origin[3] = { 0, 0, -40 };

    test_int(box_particle_cull(&view, 1, origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_NONE);

    settings.cull_margin = 40.0f;
    test_int(box_particle_cull(&view, 1, origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_FULL);
}

void Scale_an_emitter_past_the_cull_distance_is_dropped(void)
{
    // particles/scale
    box_particle_view_t view;
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    box_frustum_build(look.v, proj.v, &view.frustum);
    view.eye[0] = 0;
    view.eye[1] = 0;
    view.eye[2] = -10;

    ParticleSettings settings;
    memset(&settings, 0, sizeof(ParticleSettings));
    settings.cull = true;
    settings.cull_distance = 50.0f;
    float near_origin[3] = { 0, 0, 20 };
    float far_origin[3] = { 0, 0, 200 };

    test_int(box_particle_cull(&view, 1, near_origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_FULL);
    test_int(box_particle_cull(&view, 1, far_origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_NONE);
}

void Scale_an_emitter_past_the_step_distance_is_halved(void)
{
    // particles/scale
    box_particle_view_t view;
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    box_frustum_build(look.v, proj.v, &view.frustum);
    view.eye[0] = 0;
    view.eye[1] = 0;
    view.eye[2] = -10;

    ParticleSettings settings;
    memset(&settings, 0, sizeof(ParticleSettings));
    settings.cull = true;
    settings.step_distance = 50.0f;
    float near_origin[3] = { 0, 0, 20 };
    float far_origin[3] = { 0, 0, 200 };

    test_int(box_particle_cull(&view, 1, near_origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_FULL);
    test_int(box_particle_cull(&view, 1, far_origin, 1.0f, &settings),
        BOX_PARTICLE_STEP_HALF);
}

void Scale_the_maximum_budget_is_split_between_emitters(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, ParticleEmitter, {.max = 3000, .rate = 0, .seed = 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, ParticleEmitter, {.max = 1000, .rate = 0, .seed = 2});
    ecs_singleton_set(w, ParticleSettings, {.max_total = 2000, .cull = true});

    box_particle_extract(w, &packet);
    test_int(packet.particles.count, 2);
    uint32_t total = 0;
    uint32_t i;
    for (i = 0; i < packet.particles.count; i ++) {
        total += ((box_particle_emitter_t*)box_vec_at(&packet.particles, i))->max;
    }

    test_int(total, 2000);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_an_emitter_under_the_budget_keeps_its_maximum(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 3000, .rate = 0});
    ecs_singleton_set(w, ParticleSettings, {.max_total = 100000, .cull = true});

    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->max, 3000);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_the_spawn_budget_thins_the_next_frame(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100000, .rate = 60000});
    ecs_singleton_set(w, ParticleSettings, {.spawn_per_frame = 100, .cull = true});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 1000);

    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->spawn, 100);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_a_culled_emitter_is_paused_and_spawns_nothing(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 1000, .rate = 6000, .speed = {0, 0}, .lifetime = {1, 1}, .size = {0, 0}
    });
    ecs_set(w, e, Position, {0, 0, -40});

    box_view_t *view = box_vec_push(&packet.views);
    memset(view, 0, sizeof(box_view_t));
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    memcpy(view->view, look.v, sizeof(look.v));
    memcpy(view->proj, proj.v, sizeof(proj.v));
    view->eye[2] = -10;

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 0);
    test_int(block->flags & BOX_PARTICLE_PAUSED, BOX_PARTICLE_PAUSED);
    test_flt(block->delta, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_a_halved_emitter_steps_every_other_frame(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 1000, .rate = 60, .speed = {0, 0}, .lifetime = {1, 1}, .size = {0, 0}
    });
    ecs_singleton_set(w, ParticleSettings, {.cull = true, .step_distance = 50});

    box_view_t *view = box_vec_push(&packet.views);
    memset(view, 0, sizeof(box_view_t));
    mat4 look = mat4_look_at(vec3_new(0, 0, -200), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    memcpy(view->view, look.v, sizeof(look.v));
    memcpy(view->proj, proj.v, sizeof(proj.v));
    view->eye[2] = -200;

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 0);
    test_int(block->flags & BOX_PARTICLE_PAUSED, BOX_PARTICLE_PAUSED);

    ecs_progress(w, 1.0f / 60.0f);
    box_vec_clear(&packet.particles);
    box_vec_clear(&packet.particle_points);
    box_particle_extract(w, &packet);
    block = box_vec_at(&packet.particles, 0);
    test_int(block->spawn, 2);
    test_int(block->flags & BOX_PARTICLE_PAUSED, 0);
    test_flt(block->delta, 2.0f / 60.0f);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_the_span_grows_with_the_particles_spawned(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 600});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->span, 10);

    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->span, 20);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_the_span_is_capped_by_the_pool(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 64, .rate = 60000});

    int32_t frame;
    for (frame = 0; frame < 4; frame ++) {
        ecs_progress(w, 1.0f / 60.0f);
        box_packet_reset(&packet);
        box_particle_extract(w, &packet);
    }

    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->span, 64);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_a_reset_puts_the_span_back(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 1000, .rate = 600});
    ecs_add(w, e, ParticlePlayback);

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->span, 20);

    ParticlePlayback *playback = ecs_get_mut(w, e, ParticlePlayback);
    playback->reset = true;
    ecs_progress(w, 1.0f / 60.0f);
    box_packet_reset(&packet);
    box_particle_extract(w, &packet);
    test_int(((box_particle_emitter_t*)box_vec_at(&packet.particles, 0))->span, 10);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_the_stats_count_the_emitters_and_their_bytes(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, ParticleEmitter, {.max = 1000, .rate = 600, .seed = 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, ParticleEmitter, {.max = 2000, .rate = 0, .seed = 2});

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    const ParticleStats *stats = ecs_singleton_get(w, ParticleStats);
    test_int(stats->emitters, 2);
    test_int(stats->simulated, 2);
    test_int(stats->culled, 0);
    test_int(stats->particles, 3000);
    test_int(stats->spawned, 10);
    test_int(stats->bytes, 3000 * BOX_PARTICLE_BYTES);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Scale_the_stats_count_a_culled_emitter(void)
{
    // particles/scale
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 1000, .rate = 600, .speed = {0, 0}, .lifetime = {1, 1}, .size = {0, 0}
    });
    ecs_set(w, e, Position, {0, 0, -40});

    box_view_t *view = box_vec_push(&packet.views);
    memset(view, 0, sizeof(box_view_t));
    mat4 look = mat4_look_at(vec3_new(0, 0, -10), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(1.05f, 16.0f / 9.0f, 0.1f);
    memcpy(view->view, look.v, sizeof(look.v));
    memcpy(view->proj, proj.v, sizeof(proj.v));
    view->eye[2] = -10;

    ecs_progress(w, 1.0f / 60.0f);
    box_particle_extract(w, &packet);
    const ParticleStats *stats = ecs_singleton_get(w, ParticleStats);
    test_int(stats->emitters, 1);
    test_int(stats->simulated, 0);
    test_int(stats->culled, 1);
    test_int(stats->spawned, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}
