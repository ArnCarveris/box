/**
 * @file Math.c
 * @brief Tests for the ray plane helpers a cursor is mapped to the ground with.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Math_ray_plane_y_hits_ground(void)
{
    // ore_engine/feature_audit
    vec3 origin = vec3_new(0, 10, -10);
    vec3 dir = vec3_normalize(vec3_new(0, -1, 1));
    vec3 point = vec3_new(-1, -1, -1);
    test_bool(box_ray_plane_y(origin, dir, 0, &point), true);
    test_assert(fabsf(point.x) < 1e-4f);
    test_assert(fabsf(point.y) < 1e-4f);
    test_assert(fabsf(point.z) < 1e-4f);
}

void Math_ray_plane_y_at_height(void)
{
    // ore_engine/feature_audit
    vec3 origin = vec3_new(2, 8, 3);
    vec3 dir = vec3_new(0, -1, 0);
    vec3 point = vec3_new(0, 0, 0);
    test_bool(box_ray_plane_y(origin, dir, 2.5f, &point), true);
    test_assert(fabsf(point.x - 2) < 1e-4f);
    test_assert(fabsf(point.y - 2.5f) < 1e-4f);
    test_assert(fabsf(point.z - 3) < 1e-4f);
}

void Math_ray_plane_y_parallel_misses(void)
{
    // ore_engine/feature_audit
    vec3 origin = vec3_new(0, 4, 0);
    vec3 dir = vec3_normalize(vec3_new(1, 0, 1));
    vec3 point = vec3_new(7, 7, 7);
    test_bool(box_ray_plane_y(origin, dir, 0, &point), false);
    test_assert(fabsf(point.x - 7) < 1e-4f);
    test_assert(fabsf(point.y - 7) < 1e-4f);
    test_assert(fabsf(point.z - 7) < 1e-4f);
}

void Math_ray_plane_y_behind_origin_misses(void)
{
    // ore_engine/feature_audit
    vec3 origin = vec3_new(0, 5, 0);
    vec3 dir = vec3_normalize(vec3_new(0, 1, 0.2f));
    test_bool(box_ray_plane_y(origin, dir, 0, NULL), false);
}

void Math_ray_plane_y_tests_without_point(void)
{
    // ore_engine/feature_audit
    vec3 origin = vec3_new(0, 3, 0);
    vec3 dir = vec3_new(0, -1, 0);
    test_bool(box_ray_plane_y(origin, dir, 0, NULL), true);
    test_bool(box_ray_plane_y(origin, dir, 4, NULL), false);
}

void Math_ray_plane_arbitrary_normal(void)
{
    // ore_engine/feature_audit
    vec3 origin = vec3_new(-4, 0, 0);
    vec3 dir = vec3_new(1, 0, 0);
    vec3 normal = vec3_normalize(vec3_new(1, 1, 0));
    vec3 point = vec3_new(0, 0, 0);
    test_bool(box_ray_plane(origin, dir, normal, 0, &point), true);
    test_assert(fabsf(point.x) < 1e-4f);
    test_assert(fabsf(point.y) < 1e-4f);
    test_assert(fabsf(point.z) < 1e-4f);
    test_bool(box_ray_plane(origin, dir, normal, -10, NULL), false);
}

void Math_ray_plane_unnormalized_direction(void)
{
    // ore_engine/feature_audit
    vec3 origin = vec3_new(1, 6, -2);
    vec3 dir = vec3_new(0, -3, 6);
    vec3 point = vec3_new(0, 0, 0);
    test_bool(box_ray_plane_y(origin, dir, 0, &point), true);
    test_assert(fabsf(point.x - 1) < 1e-4f);
    test_assert(fabsf(point.y) < 1e-4f);
    test_assert(fabsf(point.z - 10) < 1e-4f);
}
