/**
 * @file Colliders.c
 * @brief Tests for the collider derived from a shape, an override or the bounds.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

/** Pi as a float constant, used instead of the possibly undefined M_PI */
#define COLLIDERS_PI (3.14159265358979323846f)

void Colliders_sphere_shape_derives_sphere(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {16, true, 2});
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderSphere);
    test_flt(c->half.x, 2);
    test_flt(c->center.x, 1);
    test_flt(c->center.y, 2);
    test_flt(c->center.z, 3);

    ecs_fini(world);
}

void Colliders_ico_sphere_derives_sphere(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, IcoSphere, {2, true, 3});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderSphere);
    test_flt(c->half.x, 3);

    ecs_fini(world);
}

void Colliders_box_shape_derives_box(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {2, 4, 6});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderBox);
    test_flt(c->half.x, 1);
    test_flt(c->half.y, 2);
    test_flt(c->half.z, 3);

    ecs_fini(world);
}

void Colliders_rounded_box_derives_box(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RoundedBox, {2, 2, 2, 0.2f, 3});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderBox);
    test_flt(c->half.y, 1);

    ecs_fini(world);
}

void Colliders_capsule_shape_derives_capsule(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Capsule, {16, true, 4, 0.5f});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderCapsule);
    test_flt(c->half.x, 0.5f);
    test_flt(c->half.y, 2);

    ecs_fini(world);
}

void Colliders_other_shape_derives_aabb(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Cylinder, {16, true, 2, 1});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderAabb);
    test_flt(c->half.x, 1);

    ecs_fini(world);
}

void Colliders_bounds_without_shape_give_aabb(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AABB, {{0, 0, 0}, {2, 4, 6}});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderAabb);
    test_flt(c->center.x, 1);
    test_flt(c->center.y, 2);
    test_flt(c->center.z, 3);
    test_flt(c->half.z, 3);

    ecs_fini(world);
}

void Colliders_empty_bounds_give_no_collider(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e, Collider));

    ecs_fini(world);
}

void Colliders_no_collider_opts_out(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, NoCollider);
    ecs_set(world, e, AABB, {{0, 0, 0}, {2, 2, 2}});
    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e, Collider));

    ecs_fini(world);
}

void Colliders_removing_no_collider_restores(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, NoCollider);
    ecs_set(world, e, AABB, {{0, 0, 0}, {2, 2, 2}});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, e, Collider));

    ecs_remove(world, e, NoCollider);
    ecs_progress(world, 0);

    test_assert(ecs_has(world, e, Collider));

    ecs_fini(world);
}

void Colliders_adding_no_collider_removes(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AABB, {{0, 0, 0}, {2, 2, 2}});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, e, Collider));

    ecs_add(world, e, NoCollider);
    ecs_progress(world, 0);

    test_assert(!ecs_has(world, e, Collider));

    ecs_fini(world);
}

void Colliders_sphere_override_wins_over_shape(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {2, 2, 2});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 1, 0}, 5});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderSphere);
    test_flt(c->half.x, 5);
    test_flt(c->center.y, 1);

    ecs_fini(world);
}

void Colliders_box_override_wins_over_shape(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {16, true, 1});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, BoxCollider, {{0, 0, 0}, {1, 2, 3}});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderBox);
    test_flt(c->half.z, 3);

    ecs_fini(world);
}

void Colliders_capsule_override_wins_over_shape(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {16, true, 1});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, CapsuleCollider, {{0, 0, 0}, 0.4f, 2});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderCapsule);
    test_flt(c->half.x, 0.4f);
    test_flt(c->half.y, 1);

    ecs_fini(world);
}

void Colliders_scale_applied_to_sphere(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Scale, {2, 3, 1});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_flt(c->half.x, 3);

    ecs_fini(world);
}

void Colliders_scale_applied_to_box(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Scale, {2, 3, 4});
    ecs_set(world, e, BoxCollider, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_flt(c->half.x, 2);
    test_flt(c->half.y, 3);
    test_flt(c->half.z, 4);

    ecs_fini(world);
}

void Colliders_scale_applied_to_capsule(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Scale, {2, 3, 1});
    ecs_set(world, e, CapsuleCollider, {{0, 0, 0}, 1, 2});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_flt(c->half.x, 2);
    test_flt(c->half.y, 3);

    ecs_fini(world);
}

void Colliders_rotation_turns_box_axes(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {0, COLLIDERS_PI * 0.5f, 0});
    ecs_set(world, e, BoxCollider, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_assert(fabsf(c->axis_x.x) < 1e-4f);
    test_assert(fabsf(fabsf(c->axis_x.z) - 1) < 1e-4f);
    test_assert(fabsf(c->axis_y.y - 1) < 1e-4f);

    ecs_fini(world);
}

void Colliders_move_updates_collider(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);
    test_flt(ecs_get(world, e, Collider)->center.x, 0);

    ecs_set(world, e, Position, {7, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, e, Collider)->center.x, 7);

    ecs_fini(world);
}

void Colliders_shape_change_updates_collider(void)
{
    // collision/colliders
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, {16, true, 1});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_progress(world, 0);
    test_flt(ecs_get(world, e, Collider)->half.x, 1);

    ecs_set(world, e, Sphere, {16, true, 4});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, e, Collider)->half.x, 4);

    ecs_fini(world);
}

void Colliders_rendered_bounds_derive_aabb(void)
{
    // collision/aabb_modified
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Cylinder, {16, true, 2, 1});
    ecs_set(world, e, Position, {1, 0, 0});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    box_packet_fini(&packet);
    ecs_progress(world, 0);

    test_assert(ecs_has(world, e, AABB));
    const Collider *c = ecs_get(world, e, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderAabb);
    test_flt(c->center.x, 1);
    test_flt(c->half.x, 1);
    test_flt(c->half.y, 1);

    ecs_fini(world);
}
