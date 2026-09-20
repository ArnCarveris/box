/**
 * @file Collision.c
 * @brief Tests for the colliders an emitter collects and for the answer of a hit.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/particle/particle_internal.h"
#include "../../../src/particle/simulate.h"

void Collision_an_emitter_without_collision_hits_nothing(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t wall = ecs_new(w);
    ecs_set(w, wall, Position, {0, 0, 0});
    ecs_set(w, wall, BoxCollider, {{0, 0, 0}, {1, 1, 1}});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->collider_count, 0);
    test_int(block->flags & BOX_PARTICLE_GROUND, 0);
    test_int(packet.particle_points.count, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_the_ground_plane_reaches_the_block(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCollision, {
        .ground = true, .height = 0.5f, .bounce = 0.6f, .friction = 0.25f
    });
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_assert((block->flags & BOX_PARTICLE_GROUND) != 0);
    test_int(block->flags & BOX_PARTICLE_DIE, 0);
    test_flt(block->collision[0], 0.5f);
    test_flt(block->collision[1], 0.6f);
    test_flt(block->collision[2], 0.25f);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_a_particle_that_dies_on_a_hit_says_so(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCollision, {.ground = true, .die = true});
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_assert((block->flags & BOX_PARTICLE_DIE) != 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_a_collider_in_reach_lands_in_the_table(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {3, 1, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 2});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCollision, {.reach = 10});
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->collider_count, 1);
    test_int(packet.particle_points.count, 1);

    box_particle_point_t *point = box_vec_at(&packet.particle_points, 0);
    test_flt(point->position[0], 3);
    test_flt(point->position[1], 1);
    test_flt(point->position[3], BOX_PARTICLE_COLLIDER_SPHERE);
    test_flt(point->normal[0], 2);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_a_collider_out_of_reach_is_left_out(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {40, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCollision, {.reach = 5});
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->collider_count, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_an_emitter_does_not_collide_with_itself(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, BoxCollider, {{0, 0, 0}, {1, 1, 1}});
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCollision, {.reach = 10});
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->collider_count, 0);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_a_box_collider_is_written_as_its_world_bounds(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t wall = ecs_new(w);
    ecs_set(w, wall, Position, {2, 3, 4});
    ecs_set(w, wall, BoxCollider, {{0, 0, 0}, {1, 2, 3}});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCollision, {.reach = 20});
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_point_t *point = box_vec_at(&packet.particle_points, 0);
    test_flt(point->position[3], BOX_PARTICLE_COLLIDER_BOX);
    test_flt(point->position[0], 2);
    test_flt(point->position[1], 3);
    test_flt(point->position[2], 4);
    test_flt(point->normal[0], 1);
    test_flt(point->normal[1], 2);
    test_flt(point->normal[2], 3);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_the_colliders_follow_the_spawn_points(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    ecs_entity_t surface = ecs_new(w);
    Mesh3 *mesh = ecs_ensure(w, surface, Mesh3);
    ecs_vec_init_t(NULL, &mesh->vertices, vec3, 3);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(1, 0, 0);
    ecs_vec_append_t(NULL, &mesh->vertices, vec3)[0] = vec3_new(0, 0, 1);
    ecs_modified(w, surface, Mesh3);

    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 4, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {
        .max = 100, .shape = EmitterMesh, .source = surface, .seed = 2
    });
    ecs_set(w, e, ParticleCollision, {.reach = 10});
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->point_count, BOX_PARTICLE_POINTS);
    test_int(block->collider_count, 1);
    test_int(packet.particle_points.count, BOX_PARTICLE_POINTS + 1);

    box_particle_point_t *point = box_vec_at(&packet.particle_points,
        block->point_first + block->point_count);
    test_flt(point->position[1], 4);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_the_colliders_are_capped(void)
{
    // particles/dynamics
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);

    int32_t i;
    for (i = 0; i < BOX_PARTICLE_COLLIDERS + 8; i ++) {
        ecs_entity_t ball = ecs_new(w);
        ecs_set(w, ball, Position, {(float)i * 0.2f, 0, 0});
        ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 0.1f});
    }

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, ParticleEmitter, {.max = 100});
    ecs_set(w, e, ParticleCollision, {.reach = 40});
    ecs_progress(w, 1.0f / 60.0f);

    box_particle_extract(w, &packet);
    box_particle_emitter_t *block = box_vec_at(&packet.particles, 0);
    test_int(block->collider_count, BOX_PARTICLE_COLLIDERS);
    test_int(packet.particle_points.count, BOX_PARTICLE_COLLIDERS);

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Collision_a_particle_under_the_ground_is_lifted_onto_it(void)
{
    // particles/dynamics
    box_particle_hit_t hit;
    vec3 position = vec3_new(1, -0.25f, 2);
    vec3 velocity = vec3_new(0, -6, 0);

    test_assert(box_particle_ground(position, 0.5f, &hit));
    test_flt(hit.normal.y, 1);
    test_flt(hit.depth, 0.75f);

    box_particle_bounce(&hit, 0, 0, &position, &velocity);
    test_flt(position.y, 0.5f);
    test_flt(velocity.y, 0);
    test_assert(!box_particle_ground(vec3_new(0, 0.6f, 0), 0.5f, &hit));
}

void Collision_a_bounce_turns_the_speed_into_the_surface_around(void)
{
    // particles/dynamics
    box_particle_hit_t hit = { .normal = vec3_new(0, 1, 0), .depth = 0.1f };
    vec3 position = vec3_new(0, -0.1f, 0);
    vec3 velocity = vec3_new(0, -10, 0);

    box_particle_bounce(&hit, 0.5f, 0, &position, &velocity);
    test_flt(velocity.y, 5);
    test_flt(position.y, 0);
}

void Collision_friction_takes_the_speed_along_the_surface(void)
{
    // particles/dynamics
    box_particle_hit_t hit = { .normal = vec3_new(0, 1, 0), .depth = 0 };
    vec3 position = vec3_new(0, 0, 0);
    vec3 velocity = vec3_new(4, -2, 0);

    box_particle_bounce(&hit, 0.5f, 0.25f, &position, &velocity);
    test_flt(velocity.x, 3);
    test_flt(velocity.y, 1);
}

void Collision_a_particle_leaving_a_surface_keeps_its_course(void)
{
    // particles/dynamics
    box_particle_hit_t hit = { .normal = vec3_new(0, 1, 0), .depth = 0.05f };
    vec3 position = vec3_new(0, -0.05f, 0);
    vec3 velocity = vec3_new(0, 3, 0);

    box_particle_bounce(&hit, 0.5f, 0, &position, &velocity);
    test_flt(velocity.y, 3);
    test_flt(position.y, 0);
}

void Collision_a_particle_inside_a_sphere_leaves_along_its_normal(void)
{
    // particles/dynamics
    box_particle_hit_t hit;
    vec3 center = vec3_new(0, 2, 0);

    test_assert(box_particle_sphere(vec3_new(0, 1.5f, 0), center, 1, &hit));
    test_flt(hit.normal.y, -1);
    test_flt(hit.depth, 0.5f);
    test_assert(!box_particle_sphere(vec3_new(0, 0.5f, 0), center, 1, &hit));
}

void Collision_a_particle_leaves_a_box_over_its_nearest_face(void)
{
    // particles/dynamics
    box_particle_hit_t hit;
    vec3 center = vec3_new(0, 0, 0);
    vec3 half = vec3_new(2, 1, 3);

    test_assert(box_particle_box(vec3_new(0.5f, 0.9f, 0), center, half, &hit));
    test_flt(hit.normal.y, 1);
    test_flt(hit.depth, 0.1f);

    test_assert(box_particle_box(vec3_new(-1.9f, 0, 0), center, half, &hit));
    test_flt(hit.normal.x, -1);
    test_assert(fabsf(hit.depth - 0.1f) < 0.0001f);

    test_assert(!box_particle_box(vec3_new(0, 0, 4), center, half, &hit));
}
