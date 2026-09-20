/**
 * @file LookAt.c
 * @brief Tests for the system that aims the local +Z axis at a target.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void LookAt_target_in_front(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, LookAt, {0, 0, 10});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z - 1) < 1e-4f);
    ecs_fini(world);
}

void LookAt_target_behind(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 5});
    ecs_set(world, e, LookAt, {0, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z + 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 5) < 1e-4f);
    ecs_fini(world);
}

void LookAt_target_below(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 5, 0});
    ecs_set(world, e, LookAt, {0, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x) < 1e-4f);
    test_assert(fabsf(f.y + 1) < 1e-4f);
    test_assert(fabsf(f.z) < 1e-4f);
    ecs_fini(world);
}

void LookAt_target_above(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, -5, 0});
    ecs_set(world, e, LookAt, {0, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x) < 1e-4f);
    test_assert(fabsf(f.y - 1) < 1e-4f);
    test_assert(fabsf(f.z) < 1e-4f);
    ecs_fini(world);
}

void LookAt_target_to_the_side(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {3, 0, 0});
    ecs_set(world, e, LookAt, {0, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    vec3 r = mat4_transform_dir(&wt->m, vec3_new(1, 0, 0));
    test_assert(fabsf(f.x + 1) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z) < 1e-4f);
    test_assert(fabsf(r.y) < 1e-4f);
    ecs_fini(world);
}

void LookAt_rotation_round_trips(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {2, 3, 4});
    ecs_set(world, e, LookAt, {-5, 1, 7});
    ecs_progress(world, 0);
    vec3 expect = vec3_normalize(vec3_new(-7, -2, 3));
    const Rotation *r = ecs_get(world, e, Rotation);
    mat4 m = mat4_rotation_xyz(vec3_new(r->x, r->y, r->z));
    vec3 f = mat4_transform_dir(&m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x - expect.x) < 1e-4f);
    test_assert(fabsf(f.y - expect.y) < 1e-4f);
    test_assert(fabsf(f.z - expect.z) < 1e-4f);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 wf = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(wf.x - expect.x) < 1e-4f);
    test_assert(fabsf(wf.y - expect.y) < 1e-4f);
    test_assert(fabsf(wf.z - expect.z) < 1e-4f);
    ecs_fini(world);
}

void LookAt_follows_moving_entity(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, LookAt, {10, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x - 1) < 1e-4f);
    ecs_set(world, e, Position, {20, 0, 0});
    ecs_progress(world, 0);
    f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x + 1) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z) < 1e-4f);
    ecs_fini(world);
}

void LookAt_child_of_translated_parent(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_set(world, child, LookAt, {10, 0, 5});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(wt->m.v[12] - 10) < 1e-4f);
    test_assert(fabsf(f.x) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z - 1) < 1e-4f);
    ecs_fini(world);
}

void LookAt_dynamic_entity(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {0, 0, 5});
    ecs_set(world, e, LookAt, {0, 0, 0});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z + 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 5) < 1e-4f);
    ecs_fini(world);
}
