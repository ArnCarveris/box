/**
 * @file Follow.c
 * @brief Tests for FollowPath: progress, modes, orientation and what it marks.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/spline/spline_internal.h"

void Follow_moves_by_speed_over_time(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(10, 0, 0);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath, {.spline = curve, .speed = 2, .mode = PathOnce});

    ecs_progress(w, 0.5f);

    const Position *p = ecs_get(w, e, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - 1) < 0.001f);

    ecs_progress(w, 0.5f);
    test_assert(fabsf(p->x - 2) < 0.001f);

    const FollowPath *path = ecs_get(w, e, FollowPath);
    test_assert(path != NULL);
    test_assert(fabsf(path->travelled - 2) < 0.001f);

    ecs_fini(w);
}

void Follow_offset_places_the_follower(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(10, 0, 0);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath, {.spline = curve, .speed = 0, .offset = 4});

    ecs_progress(w, 0.25f);

    const Position *p = ecs_get(w, e, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - 4) < 0.001f);

    ecs_fini(w);
}

void Follow_loops_at_the_end(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(10, 0, 0);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath, {.spline = curve, .speed = 4, .mode = PathLoop});

    ecs_progress(w, 1.0f);
    ecs_progress(w, 1.0f);

    const Position *p = ecs_get(w, e, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - 8) < 0.001f);

    ecs_progress(w, 1.0f);
    test_assert(fabsf(p->x - 2) < 0.001f);

    ecs_fini(w);
}

void Follow_ping_pong_turns_around(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(10, 0, 0);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath,
        {.spline = curve, .speed = 4, .mode = PathPingPong, .orient = true});

    ecs_progress(w, 1.0f);
    ecs_progress(w, 1.0f);

    const Position *p = ecs_get(w, e, Position);
    const Rotation *r = ecs_get(w, e, Rotation);
    test_assert(p != NULL);
    test_assert(r != NULL);
    test_assert(fabsf(p->x - 8) < 0.001f);
    test_assert(fabsf(r->y - 1.5707964f) < 0.001f);

    ecs_progress(w, 1.0f);
    test_assert(fabsf(p->x - 8) < 0.001f);
    test_assert(fabsf(r->y + 1.5707964f) < 0.001f);

    ecs_progress(w, 1.0f);
    test_assert(fabsf(p->x - 4) < 0.001f);

    ecs_fini(w);
}

void Follow_stops_at_the_end(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(10, 0, 0);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath, {.spline = curve, .speed = 4, .mode = PathOnce});

    ecs_progress(w, 5.0f);

    const Position *p = ecs_get(w, e, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - 10) < 0.001f);

    ecs_progress(w, 5.0f);
    test_assert(fabsf(p->x - 10) < 0.001f);

    ecs_fini(w);
}

void Follow_orientation_follows_the_tangent(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 3);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 10);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(10, 0, 10);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath,
        {.spline = curve, .speed = 5, .mode = PathOnce, .orient = true});

    ecs_progress(w, 1.0f);

    const Rotation *r = ecs_get(w, e, Rotation);
    test_assert(r != NULL);
    test_assert(fabsf(r->x) < 0.001f);
    test_assert(fabsf(r->y) < 0.001f);
    test_assert(fabsf(r->z) < 0.001f);

    ecs_progress(w, 2.0f);

    const Position *p = ecs_get(w, e, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - 5) < 0.001f);
    test_assert(fabsf(p->z - 10) < 0.001f);
    test_assert(fabsf(r->y - 1.5707964f) < 0.001f);

    ecs_fini(w);
}

void Follow_marks_the_entity_dynamic(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath, {.spline = curve, .speed = 1});

    test_assert(ecs_has(w, e, Position));
    test_assert(ecs_has(w, e, Rotation));
    test_assert(ecs_has(w, e, WorldTransform));
    test_assert(ecs_has_id(w, e, DynamicTransform));

    ecs_fini(w);
}

void Follow_a_light_follows_a_path(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 2, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 2, 8);
    ecs_modified(w, curve, Spline);

    ecs_entity_t lamp = ecs_new(w);
    ecs_set(w, lamp, PointLight, {.intensity = 5, .range = 20});
    ecs_set(w, lamp, FollowPath, {.spline = curve, .speed = 4, .mode = PathOnce});

    ecs_progress(w, 1.0f);

    const Position *p = ecs_get(w, lamp, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->y - 2) < 0.001f);
    test_assert(fabsf(p->z - 4) < 0.001f);
    test_assert(ecs_has(w, lamp, PointLight));

    ecs_fini(w);
}

void Follow_negative_speed_travels_backwards(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(10, 0, 0);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath, {.spline = curve, .speed = -2, .mode = PathLoop});

    ecs_progress(w, 1.0f);

    const Position *p = ecs_get(w, e, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - 8) < 0.001f);

    ecs_fini(w);
}

void Follow_without_a_spline_nothing_moves(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath, {.spline = 0, .speed = 4});
    ecs_set(w, e, Position, {7, 7, 7});

    ecs_progress(w, 1.0f);

    const Position *p = ecs_get(w, e, Position);
    test_assert(p != NULL);
    test_flt(p->x, 7);
    test_flt(p->y, 7);
    test_flt(p->z, 7);

    ecs_fini(w);
}

void Follow_speed_is_the_same_on_a_curve(void)
{
    // splines/spline_paths
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    ecs_vec_init_t(NULL, &s->points, vec3, 5);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(2, 0, 3);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(6, 0, -2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(9, 0, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(12, 0, 0);
    ecs_modified(w, curve, Spline);

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, FollowPath, {.spline = curve, .speed = 3, .mode = PathOnce});

    const Position *p = ecs_get(w, e, Position);
    int32_t i;
    test_assert(p != NULL);
    for (i = 1; i <= 4; i ++) {
        box_spline_sample_t expected;

        ecs_progress(w, 1.0f);
        test_bool(box_spline_sample_at(w, curve, 3.0f * (float)i, &expected), true);
        test_assert(fabsf(p->x - expected.position.x) < 0.001f);
        test_assert(fabsf(p->z - expected.position.z) < 0.001f);
    }

    ecs_fini(w);
}
