/**
 * @file Intersect.c
 * @brief Tests for the ray, sphere and capsule tests against every collider.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Intersect_ray_hits_sphere(void)
{
    // collision/colliders
    Collider c = { ColliderSphere, {0, 0, 0}, {2, 2, 2},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    float t = 0;

    test_bool(box_collider_ray(&c, vec3_new(-10, 0, 0), vec3_new(1, 0, 0),
        100, &t), true);
    test_flt(t, 8);
}

void Intersect_ray_misses_sphere(void)
{
    // collision/colliders
    Collider c = { ColliderSphere, {0, 0, 0}, {2, 2, 2},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_ray(&c, vec3_new(-10, 5, 0), vec3_new(1, 0, 0),
        100, NULL), false);
}

void Intersect_ray_respects_max_distance(void)
{
    // collision/colliders
    Collider c = { ColliderSphere, {0, 0, 0}, {2, 2, 2},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_ray(&c, vec3_new(-10, 0, 0), vec3_new(1, 0, 0),
        5, NULL), false);
}

void Intersect_ray_hits_box(void)
{
    // collision/colliders
    Collider c = { ColliderBox, {0, 0, 0}, {1, 2, 3},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    float t = 0;

    test_bool(box_collider_ray(&c, vec3_new(0, 0, -10), vec3_new(0, 0, 1),
        100, &t), true);
    test_flt(t, 7);
}

void Intersect_ray_hits_rotated_box(void)
{
    // collision/colliders
    Collider c = { ColliderBox, {0, 0, 0}, {4, 1, 1},
        {0, 0, 1}, {0, 1, 0}, {-1, 0, 0} };
    float t = 0;

    test_bool(box_collider_ray(&c, vec3_new(0, 0, -10), vec3_new(0, 0, 1),
        100, &t), true);
    test_flt(t, 6);
    test_bool(box_collider_ray(&c, vec3_new(-10, 0, 0), vec3_new(1, 0, 0),
        100, &t), true);
    test_flt(t, 9);
}

void Intersect_ray_misses_box(void)
{
    // collision/colliders
    Collider c = { ColliderBox, {0, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_ray(&c, vec3_new(-10, 4, 0), vec3_new(1, 0, 0),
        100, NULL), false);
}

void Intersect_ray_hits_aabb(void)
{
    // collision/colliders
    Collider c = { ColliderAabb, {5, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    float t = 0;

    test_bool(box_collider_ray(&c, vec3_new(0, 0, 0), vec3_new(1, 0, 0),
        100, &t), true);
    test_flt(t, 4);
}

void Intersect_ray_hits_capsule_side(void)
{
    // collision/colliders
    Collider c = { ColliderCapsule, {0, 0, 0}, {1, 2, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    float t = 0;

    test_bool(box_collider_ray(&c, vec3_new(-10, 1, 0), vec3_new(1, 0, 0),
        100, &t), true);
    test_flt(t, 9);
}

void Intersect_ray_hits_capsule_cap(void)
{
    // collision/colliders
    Collider c = { ColliderCapsule, {0, 0, 0}, {1, 2, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    float t = 0;

    test_bool(box_collider_ray(&c, vec3_new(0, 10, 0), vec3_new(0, -1, 0),
        100, &t), true);
    test_flt(t, 7);
}

void Intersect_ray_misses_capsule(void)
{
    // collision/colliders
    Collider c = { ColliderCapsule, {0, 0, 0}, {1, 2, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_ray(&c, vec3_new(-10, 5, 0), vec3_new(1, 0, 0),
        100, NULL), false);
}

void Intersect_sphere_overlaps_sphere(void)
{
    // collision/colliders
    Collider c = { ColliderSphere, {0, 0, 0}, {2, 2, 2},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_sphere(&c, vec3_new(2.5f, 0, 0), 1), true);
    test_bool(box_collider_sphere(&c, vec3_new(4, 0, 0), 1), false);
}

void Intersect_sphere_overlaps_box(void)
{
    // collision/colliders
    Collider c = { ColliderBox, {0, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_sphere(&c, vec3_new(1.5f, 0, 0), 1), true);
    test_bool(box_collider_sphere(&c, vec3_new(3, 0, 0), 1), false);
}

void Intersect_sphere_overlaps_rotated_box(void)
{
    // collision/colliders
    Collider c = { ColliderBox, {0, 0, 0}, {4, 1, 1},
        {0, 0, 1}, {0, 1, 0}, {-1, 0, 0} };

    test_bool(box_collider_sphere(&c, vec3_new(0, 0, 3), 0.5f), true);
    test_bool(box_collider_sphere(&c, vec3_new(3, 0, 0), 0.5f), false);
}

void Intersect_sphere_overlaps_capsule(void)
{
    // collision/colliders
    Collider c = { ColliderCapsule, {0, 0, 0}, {1, 2, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_sphere(&c, vec3_new(1.5f, 1, 0), 1), true);
    test_bool(box_collider_sphere(&c, vec3_new(0, 3.5f, 0), 1), true);
    test_bool(box_collider_sphere(&c, vec3_new(0, 6, 0), 1), false);
}

void Intersect_sphere_overlaps_aabb(void)
{
    // collision/colliders
    Collider c = { ColliderAabb, {5, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_sphere(&c, vec3_new(3.5f, 0, 0), 1), true);
    test_bool(box_collider_sphere(&c, vec3_new(0, 0, 0), 1), false);
}

void Intersect_capsule_overlaps_sphere(void)
{
    // collision/colliders
    Collider c = { ColliderSphere, {0, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_capsule(&c, vec3_new(-5, 1.5f, 0),
        vec3_new(5, 1.5f, 0), 1), true);
    test_bool(box_collider_capsule(&c, vec3_new(-5, 4, 0),
        vec3_new(5, 4, 0), 1), false);
}

void Intersect_capsule_overlaps_box(void)
{
    // collision/colliders
    Collider c = { ColliderBox, {0, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_capsule(&c, vec3_new(0, 5, 0),
        vec3_new(0, 1.5f, 0), 1), true);
    test_bool(box_collider_capsule(&c, vec3_new(0, 5, 0),
        vec3_new(0, 3, 0), 1), false);
}

void Intersect_capsule_overlaps_capsule(void)
{
    // collision/colliders
    Collider c = { ColliderCapsule, {0, 0, 0}, {1, 2, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_capsule(&c, vec3_new(-5, 0, 1.5f),
        vec3_new(5, 0, 1.5f), 1), true);
    test_bool(box_collider_capsule(&c, vec3_new(-5, 0, 4),
        vec3_new(5, 0, 4), 1), false);
}

void Intersect_capsule_overlaps_aabb(void)
{
    // collision/colliders
    Collider c = { ColliderAabb, {5, 0, 0}, {1, 1, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };

    test_bool(box_collider_capsule(&c, vec3_new(0, 0, 0),
        vec3_new(3.5f, 0, 0), 1), true);
    test_bool(box_collider_capsule(&c, vec3_new(0, 0, 0),
        vec3_new(2, 0, 0), 1), false);
}

void Intersect_bounds_of_collider(void)
{
    // collision/colliders
    Collider s = { ColliderSphere, {1, 2, 3}, {2, 2, 2},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    vec3 min, max;
    box_collider_bounds(&s, &min, &max);
    test_flt(min.x, -1);
    test_flt(max.z, 5);

    Collider c = { ColliderCapsule, {0, 0, 0}, {1, 2, 1},
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    box_collider_bounds(&c, &min, &max);
    test_flt(min.y, -3);
    test_flt(max.x, 1);
}
