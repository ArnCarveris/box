#ifndef BOX_TEST_GEOMETRY_ASSETS_H
#define BOX_TEST_GEOMETRY_ASSETS_H

#include <box.h>
#include <math.h>

static ecs_entity_t test_path2(ecs_world_t *world, const vec2 *points, int count, bool closed)
{
    Path2D path = {.closed = closed};
    glm_vec2_copy(ECS_CONST_CAST(float*, points[0]), path.start);
    for (int i = 1; i < count; i ++) {
        PathSegment2D *segment = ecs_vec_append_t(NULL, &path.segments, PathSegment2D);
        *segment = (PathSegment2D){.kind = PathLine};
        glm_vec2_copy(ECS_CONST_CAST(float*, points[i]), segment->end);
    }
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path2D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);
    return e;
}

static ecs_entity_t test_profile(ecs_world_t *world, const vec2 *points,
    int count, const int32_t *holes, int hole_count)
{
    Profile2D profile = {0};
    int first = 0;
    for (int ring = 0; ring <= hole_count; ring ++) {
        int end = ring == hole_count ? count : holes[ring];
        ecs_entity_t path = test_path2(world, points + first, end - first, true);
        if (ring) {
            *ecs_vec_append_t(NULL, &profile.holes, ecs_entity_t) = path;
        } else {
            profile.outer = path;
        }
        first = end;
    }
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Profile2D, &profile);
    ecs_vec_fini_t(NULL, &profile.holes, ecs_entity_t);
    return e;
}

static ecs_entity_t test_circle(ecs_world_t *world, float inner)
{
    vec2 points[128];
    int count = inner > 0 ? 128 : 64;
    for (int i = 0; i < count; i ++) {
        float t = (float)(i % 64) * 2 * GLM_PI / 64;
        float r = i >= 64 ? inner : 1;
        points[i][0] = cosf(t) * r;
        points[i][1] = sinf(t) * r;
    }
    int32_t hole = 64;
    return test_profile(world, points, count, &hole, inner > 0);
}

static ecs_entity_t test_route(ecs_world_t *world, const vec3 start,
    const vec3 c1, const vec3 c2, const vec3 end)
{
    Path3D path = {0};
    glm_vec3_copy(ECS_CONST_CAST(float*, start), path.start);
    PathSegment3D segment = {.kind = PathCubicBezier};
    glm_vec3_copy(ECS_CONST_CAST(float*, c1), segment.control1);
    glm_vec3_copy(ECS_CONST_CAST(float*, c2), segment.control2);
    glm_vec3_copy(ECS_CONST_CAST(float*, end), segment.end);
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = segment;
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    return e;
}

static Sweep test_sweep(ecs_world_t *world)
{
    return (Sweep){.path = test_route(world, (vec3){0}, (vec3){0, 1, 0},
        (vec3){0, 2, 0}, (vec3){0, 3, 0}), .profile = test_circle(world, 0),
        .scale = {1, 1}, .taper = 1, .cap_start = true, .cap_end = true,
        .segments = 32, .rings = 12};
}

static void test_route_bend(ecs_world_t *world, ecs_entity_t route, float x, float x2)
{
    Path3D *path = ecs_get_mut(world, route, Path3D);
    PathSegment3D *segment = ecs_vec_first(&path->segments);
    segment->control1[0] = x;
    segment->control2[0] = x2;
    ecs_modified(world, route, Path3D);
}

#endif
