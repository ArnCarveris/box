/**
 * @file Movement.c
 * @brief Tests for the box.movement module.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Movement_velocity_integrates_position(void)
{
    // scene_core/render
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxMovement);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Velocity, {10, 0, -10});
    ecs_progress(w, 0.5f);
    const Position *p = ecs_get(w, e, Position);
    test_assert(fabsf(p->x - 6) < 1e-4f);
    test_assert(fabsf(p->y - 2) < 1e-4f);
    test_assert(fabsf(p->z + 2) < 1e-4f);
    ecs_fini(w);
}

void Movement_angular_velocity_integrates_rotation(void)
{
    // scene_core/render
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxMovement);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, AngularVelocity, {0, 2, 0});
    ecs_progress(w, 0.25f);
    const Rotation *r = ecs_get(w, e, Rotation);
    test_assert(r != NULL);
    test_assert(fabsf(r->y - 0.5f) < 1e-4f);
    ecs_fini(w);
}

void Movement_velocity_marks_dynamic(void)
{
    // scene_core/render
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxMovement);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Velocity, {1, 0, 0});
    test_assert(ecs_has(w, e, Position));
    test_assert(ecs_has_id(w, e, DynamicTransform));
    test_assert(ecs_has(w, e, WorldTransform));
    ecs_fini(w);
}
