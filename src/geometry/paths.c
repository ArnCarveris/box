#include "paths.h"
#include <limits.h>
#include <math.h>

static bool box_path_point(const float *p, int dimensions)
{
    for (int i = 0; i < dimensions; i ++) {
        if (!isfinite(p[i])) {
            return false;
        }
    }
    return true;
}

static bool box_path_equal(const float *a, const float *b, int dimensions)
{
    for (int i = 0; i < dimensions; i ++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static void box_path_segment(const float *start, const float *end,
    const float *control1, const float *control2, PathSegmentKind kind,
    float t, int dimensions, float *out)
{
    vec3 a = {0}, b = {0}, c = {0}, d = {0}, point;
    for (int i = 0; i < dimensions; i ++) {
        a[i] = start[i];
        d[i] = end[i];
        b[i] = kind == PathLine ? start[i] + (end[i] - start[i]) / 3 : control1[i];
        c[i] = kind == PathLine ? start[i] + (end[i] - start[i]) * (2.0f / 3) : control2[i];
    }
    box_path_cubic_evaluate(a, b, c, d, t, point);
    for (int i = 0; i < dimensions; i ++) {
        out[i] = point[i];
    }
}

#define BOX_PATH_IMPLEMENT(D, POINT, SEGMENT, PATH) \
static bool box_path##D##_valid(const PATH *path) \
{ \
    if (!path || !box_path_point(path->start, D)) { \
        return false; \
    } \
    int count = ecs_vec_count(&path->segments); \
    if (count <= 0) { \
        return false; \
    } \
    if (count > (INT_MAX / (int)sizeof(POINT) - 1) / 16) { \
        ecs_err("path segment capacity exceeded"); \
        return false; \
    } \
    const SEGMENT *segments = ecs_vec_first(&path->segments); \
    if (!segments) { \
        return false; \
    } \
    for (int i = 0; i < count; i ++) { \
        const SEGMENT *s = &segments[i]; \
        if ((s->kind != PathLine && s->kind != PathCubicBezier) || \
            !box_path_point(s->end, D) || \
            (s->kind == PathCubicBezier && (!box_path_point(s->control1, D) || \
                !box_path_point(s->control2, D)))) { \
            return false; \
        } \
    } \
    return true; \
} \
bool box_path##D##_evaluate(const PATH *path, float t, POINT out) \
{ \
    if (!isfinite(t) || t < 0 || t > 1 || !box_path##D##_valid(path)) { \
        return false; \
    } \
    int count = ecs_vec_count(&path->segments); \
    const SEGMENT *segments = ecs_vec_first(&path->segments); \
    bool closing = path->closed && !box_path_equal(segments[count - 1].end, path->start, D); \
    double parameter = (double)t * (count + closing); \
    int index = (int)fmin(floor(parameter), count + closing - 1); \
    float local = (float)(parameter - index); \
    const float *start = index ? segments[index - 1].end : path->start; \
    if (index == count) { \
        box_path_segment(start, path->start, NULL, NULL, PathLine, local, D, out); \
    } else { \
        const SEGMENT *s = &segments[index]; \
        box_path_segment(start, s->end, s->control1, s->control2, s->kind, local, D, out); \
    } \
    return box_path_point(out, D); \
} \
bool box_path##D##_sample(const ecs_world_t *world, ecs_entity_t entity, ecs_vec_t *points) \
{ \
    const PATH *path = entity && ecs_is_alive(world, entity) ? ecs_get(world, entity, PATH) : NULL; \
    if (!box_path##D##_valid(path)) { \
        return false; \
    } \
    int count = ecs_vec_count(&path->segments); \
    const SEGMENT *segments = ecs_vec_first(&path->segments); \
    int length = 1; \
    for (int i = 0; i < count; i ++) { \
        length += segments[i].kind == PathLine ? 1 : 16; \
    } \
    if ((int64_t)ecs_vec_count(points) + length > INT_MAX / (int)sizeof(POINT)) { \
        ecs_err("path sample allocation exceeds capacity"); \
        return false; \
    } \
    ecs_vec_set_size_t(NULL, points, POINT, ecs_vec_count(points) + length); \
    POINT *first = ecs_vec_append_t(NULL, points, POINT); \
    ecs_os_memcpy(*first, path->start, sizeof(POINT)); \
    const float *start = path->start; \
    for (int i = 0; i < count; i ++) { \
        const SEGMENT *s = &segments[i]; \
        int steps = s->kind == PathLine ? 1 : 16; \
        for (int j = 1; j <= steps; j ++) { \
            POINT *point = ecs_vec_append_t(NULL, points, POINT); \
            box_path_segment(start, s->end, s->control1, s->control2, s->kind, \
                (float)j / steps, D, *point); \
        } \
        start = s->end; \
    } \
    if (path->closed && box_path_equal(start, path->start, D)) { \
        ecs_vec_remove_last(points); \
    } \
    return true; \
}

BOX_PATH_IMPLEMENT(2, vec2, PathSegment2D, Path2D)
BOX_PATH_IMPLEMENT(3, vec3, PathSegment3D, Path3D)

void box_geometry_paths_init(ecs_world_t *world)
{
    box_owned_hooks(world, Path2D, BOX_OWNED_VEC(Path2D, segments, PathSegment2D));
    box_owned_hooks(world, Path3D, BOX_OWNED_VEC(Path3D, segments, PathSegment3D));
    ecs_add_pair(world, ecs_id(Path2D), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Path3D), EcsOnInstantiate, EcsInherit);
}
