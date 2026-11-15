/**
 * @file geometry/paths.c
 * @brief Validation, evaluation and sampling of Path2D and Path3D components.
 */

#include "paths.h"
#include <limits.h>
#include <math.h>

/** Test whether all coordinates of a point are finite.
 *
 * @param p The point coordinates.
 * @param dimensions The number of coordinates.
 * @return True if all coordinates are finite.
 */
static bool box_path_point(
    const float *p,
    int dimensions)
{
    bool result = true;
    for (int i = 0; result && i < dimensions; i ++) {
        result = isfinite(p[i]);
    }

    return result;
}

/** Test whether two points are exactly equal.
 *
 * @param a The first point.
 * @param b The second point.
 * @param dimensions The number of coordinates.
 * @return True if all coordinates are equal.
 */
static bool box_path_equal(
    const float *a,
    const float *b,
    int dimensions)
{
    bool result = true;
    for (int i = 0; result && i < dimensions; i ++) {
        result = a[i] == b[i];
    }

    return result;
}

/** Resolve a segment to the four points of its cubic Bezier.
 *
 * @param p0 Anchor before the segment, used by a Catmull-Rom segment.
 * @param p1 Start anchor of the segment.
 * @param p2 End anchor of the segment.
 * @param p3 Anchor after the segment, used by a Catmull-Rom segment.
 * @param control1 First control point, used by a cubic Bezier segment.
 * @param control2 Second control point, used by a cubic Bezier segment.
 * @param kind The segment kind.
 * @param dimensions The number of coordinates.
 * @param out Receives the four cubic points, unused coordinates set to 0.
 */
static void box_path_cubic(const float *p0, const float *p1, const float *p2,
    const float *p3, const float *control1, const float *control2, PathSegmentKind kind,
    int dimensions, vec3 out[4])
{
    for (int k = 0; k < 4; k ++) {
        glm_vec3_zero(out[k]);
    }
    for (int i = 0; i < dimensions; i ++) {
        out[0][i] = p1[i];
        out[3][i] = p2[i];
        if (kind == PathCubicBezier) {
            out[1][i] = control1[i];
            out[2][i] = control2[i];
        } else if (kind == PathCatmullRom) {
            out[1][i] = p1[i] + (p2[i] - p0[i]) / 6;
            out[2][i] = p2[i] - (p3[i] - p1[i]) / 6;
        } else {
            out[1][i] = p1[i] + (p2[i] - p1[i]) / 3;
            out[2][i] = p1[i] + (p2[i] - p1[i]) * (2.0f / 3);
        }
    }
}

int box_path_steps(
    int32_t value)
{
    int result = value > BOX_PATH_MAX_STEPS ? BOX_PATH_MAX_STEPS : value;

    return result <= 0 ? BOX_PATH_STEPS : result;
}

#define BOX_PATH_IMPLEMENT(D, POINT, SEGMENT, PATH) \
bool box_path##D##_valid(const PATH *path) \
{ \
    if (!path || !box_path_point(path->start, D)) { \
        return false; \
    } \
    int count = ecs_vec_count(&path->segments); \
    if (count <= 0) { \
        return false; \
    } \
    if (count > (INT_MAX / (int)sizeof(POINT) - 1) / BOX_PATH_MAX_STEPS - 1) { \
        ecs_err("path segment capacity exceeded"); \
        return false; \
    } \
    const SEGMENT *segments = ecs_vec_first(&path->segments); \
    if (!segments) { \
        return false; \
    } \
    for (int i = 0; i < count; i ++) { \
        const SEGMENT *s = &segments[i]; \
        if ((s->kind != PathLine && s->kind != PathCubicBezier && \
                s->kind != PathCatmullRom) || \
            !box_path_point(s->end, D) || \
            (s->kind == PathCubicBezier && (!box_path_point(s->control1, D) || \
                !box_path_point(s->control2, D)))) { \
            return false; \
        } \
    } \
    return true; \
} \
int box_path##D##_count(const PATH *path) \
{ \
    int count = ecs_vec_count(&path->segments); \
    const SEGMENT *segments = ecs_vec_first(&path->segments); \
    return count + (path->closed && \
        !box_path_equal(segments[count - 1].end, path->start, D)); \
} \
static const float* box_path##D##_anchor(const PATH *path, int index) \
{ \
    int count = ecs_vec_count(&path->segments); \
    const SEGMENT *segments = ecs_vec_first(&path->segments); \
    return index > 0 && index <= count ? segments[index - 1].end : path->start; \
} \
PathSegmentKind box_path##D##_cubic(const PATH *path, int index, vec3 out[4]) \
{ \
    int count = ecs_vec_count(&path->segments); \
    int total = box_path##D##_count(path); \
    const SEGMENT *segments = ecs_vec_first(&path->segments); \
    const SEGMENT *s = index < count ? &segments[index] : NULL; \
    PathSegmentKind kind = s ? s->kind : \
        (segments[count - 1].kind == PathCatmullRom ? PathCatmullRom : PathLine); \
    int previous, next; \
    if (path->closed) { \
        previous = (index - 1 + total) % total; \
        next = (index + 2) % total; \
    } else { \
        previous = index ? index - 1 : 0; \
        next = index + 2 <= total ? index + 2 : total; \
    } \
    box_path_cubic(box_path##D##_anchor(path, previous), box_path##D##_anchor(path, index), \
        box_path##D##_anchor(path, index + 1), box_path##D##_anchor(path, next), \
        s ? s->control1 : NULL, s ? s->control2 : NULL, kind, D, out); \
    return kind; \
} \
bool box_path##D##_evaluate(const PATH *path, float t, POINT out) \
{ \
    if (!isfinite(t) || t < 0 || t > 1 || !box_path##D##_valid(path)) { \
        return false; \
    } \
    int total = box_path##D##_count(path); \
    double parameter = (double)t * total; \
    int index = (int)fmin(floor(parameter), total - 1); \
    vec3 cubic[4], point; \
    box_path##D##_cubic(path, index, cubic); \
    box_curve_cubic3(cubic[0], cubic[1], cubic[2], cubic[3], (float)(parameter - index), point); \
    ecs_os_memcpy(out, point, sizeof(POINT)); \
    return box_path_point(out, D); \
} \
bool box_path##D##_sample_steps(const ecs_world_t *world, ecs_entity_t entity, int steps, \
    ecs_vec_t *points) \
{ \
    const PATH *path = entity && ecs_is_alive(world, entity) ? \
        ecs_get(world, entity, PATH) : NULL; \
    if (!box_path##D##_valid(path)) { \
        return false; \
    } \
    steps = box_path_steps(steps); \
    int total = box_path##D##_count(path); \
    int length = 1; \
    for (int i = 0; i < total; i ++) { \
        vec3 cubic[4]; \
        length += box_path##D##_cubic(path, i, cubic) == PathLine ? 1 : steps; \
    } \
    if ((int64_t)ecs_vec_count(points) + length > INT_MAX / (int)sizeof(POINT)) { \
        ecs_err("path sample allocation exceeds capacity"); \
        return false; \
    } \
    ecs_vec_set_size_t(NULL, points, POINT, ecs_vec_count(points) + length); \
    POINT *first = ecs_vec_append_t(NULL, points, POINT); \
    ecs_os_memcpy(*first, path->start, sizeof(POINT)); \
    for (int i = 0; i < total; i ++) { \
        vec3 cubic[4], point; \
        int count = box_path##D##_cubic(path, i, cubic) == PathLine ? 1 : steps; \
        for (int j = 1; j <= count; j ++) { \
            POINT *sample = ecs_vec_append_t(NULL, points, POINT); \
            if (j == count) { \
                glm_vec3_copy(cubic[3], point); \
            } else { \
                box_curve_cubic3(cubic[0], cubic[1], cubic[2], cubic[3], \
                    (float)j / count, point); \
            } \
            ecs_os_memcpy(*sample, point, sizeof(POINT)); \
        } \
    } \
    if (path->closed) { \
        ecs_vec_remove_last(points); \
    } \
    return true; \
} \
bool box_path##D##_sample(const ecs_world_t *world, ecs_entity_t entity, ecs_vec_t *points) \
{ \
    return box_path##D##_sample_steps(world, entity, BOX_PATH_STEPS, points); \
}

BOX_PATH_IMPLEMENT(2, vec2, PathSegment2D, Path2D)
BOX_PATH_IMPLEMENT(3, vec3, PathSegment3D, Path3D)

void box_geometry_paths_init(
    ecs_world_t *world)
{
    box_owned_hooks(world, Path2D, BOX_OWNED_VEC(Path2D, segments, PathSegment2D));
    box_owned_hooks(world, Path3D, BOX_OWNED_VEC(Path3D, segments, PathSegment3D));
    ecs_add_pair(world, ecs_id(Path2D), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Path3D), EcsOnInstantiate, EcsInherit);
}
