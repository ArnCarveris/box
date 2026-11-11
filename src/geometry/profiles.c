/**
 * @file geometry/profiles.c
 * @brief Surface asset packing, triangulation, and mesh generation for profile shapes.
 *
 * Covers Sweep, Loft, Lathe, and ProfileExtrusion, and re-triggers them when their paths change.
 */

#include "paths.h"
#include "mesh_gen.h"
#include <limits.h>
#include <stddef.h>
#include <math.h>

/** Append a finite vec4 to a surface asset buffer.
 *
 * @param data The asset buffer (vector of vec4)
 * @param value The value to append
 * @return False if the value is not finite or the asset capacity is exceeded
 */
static bool box_asset_append(
    ecs_vec_t *data,
    const vec4 value)
{
    bool result = true;
    for (int i = 0; result && i < 4; i ++) {
        result = isfinite(value[i]);
    }

    if (result && (ecs_vec_count(data) >= INT_MAX / (int)sizeof(vec4) ||
        ecs_vec_count(data) >= (1 << 24) - 24))
    {
        ecs_err("surface asset capacity exceeded");
        result = false;
    }

    if (result) {
        glm_vec4_copy(ECS_CONST_CAST(float*, value), *ecs_vec_append_t(NULL, data, vec4));
    }

    return result;
}

/** Direction of a cubic at its start or end, from the nearest distinct control point.
 *
 * @param cubic The four points of the cubic
 * @param end Whether to take the direction at the end
 * @param out Receives the unit direction, zero for a degenerate cubic
 */
static void box_path2_tangent(
    vec3 cubic[4],
    bool end,
    vec2 out)
{
    out[0] = out[1] = 0;
    bool found = false;
    for (int i = 1; i < 4 && !found; i ++) {
        const float *a = end ? cubic[3 - i] : cubic[0];
        const float *b = end ? cubic[3] : cubic[i];
        float x = b[0] - a[0], y = b[1] - a[1], length = hypotf(x, y);
        if (length > 1e-6f) {
            out[0] = x / length;
            out[1] = y / length;
            found = true;
        }
    }
}

/** Cosine of the angle between the tangents of two curves above which their anchor is smooth */
#define BOX_PATH_TANGENT 0.9998477f

/** Flag the samples of a closed Path2D that are corner anchors: between a Line and a curve
 * segment, or between two curve segments whose tangents do not line up.
 *
 * @param path The path, sampled by box_path2_sample_steps
 * @param steps Steps of each curve segment of the samples
 * @param count Number of samples of the path
 * @param flags Receives 1 for each corner sample, 0 otherwise
 */
static void box_path2_corners(
    const Path2D *path,
    int steps,
    int count,
    int *flags)
{
    int total = box_path2_count(path), index = 0;
    vec3 cubic[4];
    vec2 in, out;
    ecs_os_memset_n(flags, 0, int, count);
    PathSegmentKind previous = box_path2_cubic(path, total - 1, cubic);
    box_path2_tangent(cubic, true, in);
    for (int i = 0; i < total && index < count; i ++) {
        PathSegmentKind kind = box_path2_cubic(path, i, cubic);
        box_path2_tangent(cubic, false, out);
        if ((kind == PathLine) != (previous == PathLine) || (kind != PathLine &&
            previous != PathLine && glm_vec2_dot(in, out) < BOX_PATH_TANGENT))
        {
            flags[index] = 1;
        }

        box_path2_tangent(cubic, true, in);

        index += kind == PathLine ? 1 : steps;
        previous = kind;
    }
}

static bool box_profile_resolve(const ecs_world_t *world, ecs_entity_t entity,
    ecs_entity_t contour, int steps, ecs_vec_t *data, int *outer, ecs_vec_t *corners)
{
    steps = box_path_steps(steps);
    const Profile2D *profile = entity && ecs_is_alive(world, entity) ?
        ecs_get(world, entity, Profile2D) : NULL;
    if ((!profile && !contour) || (entity && contour)) {
        return false;
    }
    int holes = profile ? ecs_vec_count(&profile->holes) : 0;
    const ecs_entity_t *hole = profile ? ecs_vec_first(&profile->holes) : NULL;
    if (holes < 0 || (holes && !hole)) {
        return false;
    }
    for (int ring = 0; ring <= holes; ring ++) {
        ecs_entity_t path_entity = ring ? hole[ring - 1] : (profile ? profile->outer : contour);
        const Path2D *path = path_entity && ecs_is_alive(world, path_entity) ?
            ecs_get(world, path_entity, Path2D) : NULL;
        if (!path || path->closed != (profile != NULL)) {
            return false;
        }
        ecs_vec_t samples = {0};
        if (!box_path2_sample_steps(world, path_entity, steps, &samples)) {
            ecs_vec_fini_t(NULL, &samples, vec2);
            return false;
        }
        int count = ecs_vec_count(&samples), first = ecs_vec_count(data);
        const vec2 *points = ecs_vec_first(&samples);
        double area = 0;
        if (count < (profile ? 3 : 2)) {
            ecs_vec_fini_t(NULL, &samples, vec2);
            return false;
        }
        for (int i = 0; i < count; i ++) {
            int next = (i + 1) % count;
            area += (double)points[i][0] * points[next][1] - (double)points[next][0] * points[i][1];
        }
        if (profile && fabs(area) < 1e-8) {
            ecs_vec_fini_t(NULL, &samples, vec2);
            return false;
        }
        bool reverse = profile && ((area > 0) != (ring == 0));
        int *flags = NULL;
        if (corners) {
            flags = ecs_os_calloc_n(int, count);
            if (!flags) {
                ecs_vec_fini_t(NULL, &samples, vec2);
                return false;
            }
            if (profile) {
                box_path2_corners(path, steps, count, flags);
            }
        }
        for (int i = 0; i < count; i ++) {
            int index = reverse ? count - 1 - i : i;
            int next = i + 1 == count ? (profile ? first : first + i) : first + i + 1;
            if (!box_asset_append(data, (vec4){points[index][0], points[index][1], ring, next})) {
                ecs_os_free(flags);
                ecs_vec_fini_t(NULL, &samples, vec2);
                return false;
            }
            if (corners) {
                *ecs_vec_append_t(NULL, corners, int) = flags[index];
            }
        }
        ecs_os_free(flags);
        if (!ring) {
            *outer = count;
        }
        ecs_vec_fini_t(NULL, &samples, vec2);
    }
    return true;
}

/** Compute the 2D cross product of (b - a) and (c - a) in double precision.
 *
 * @param a The origin point
 * @param b The first point
 * @param c The second point
 * @return The signed cross product, positive if a, b, c turn counterclockwise
 */
static double box_cross2(
    const float *a,
    const float *b,
    const float *c)
{
    return ((double)b[0] - a[0]) * ((double)c[1] - a[1]) -
        ((double)b[1] - a[1]) * ((double)c[0] - a[0]);
}

/** Test whether two 2D points are exactly equal.
 *
 * @param a The first point
 * @param b The second point
 * @return True if both coordinates are equal
 */
static bool box_equal2(
    const float *a,
    const float *b)
{
    return a[0] == b[0] && a[1] == b[1];
}

/** Test whether segments ab and cd intersect, ignoring segments that share an endpoint.
 *
 * @param a Start of the first segment
 * @param b End of the first segment
 * @param c Start of the second segment
 * @param d End of the second segment
 * @return True if the segments intersect and share no endpoint
 */
static bool box_intersects(
    const float *a,
    const float *b,
    const float *c,
    const float *d)
{
    return !(box_equal2(a, c) || box_equal2(a, d) || box_equal2(b, c) || box_equal2(b, d)) &&
        box_cross2(a, b, c) * box_cross2(a, b, d) <= 0 &&
        box_cross2(c, d, a) * box_cross2(c, d, b) <= 0;
}

/** Even-odd point in polygon test over profile points linked by their next index (w).
 *
 * @param points The profile points
 * @param count Number of points
 * @param x The x coordinate of the test point
 * @param z The z coordinate of the test point
 * @return True if the point is inside
 */
static bool box_polygon_contains(
    const vec4 *points,
    int count,
    float x,
    float z)
{
    bool inside = false;
    for (int i = 0; i < count; i ++) {
        const float *a = points[i], *b = points[(int)points[i][3]];
        if ((a[1] > z) != (b[1] > z) &&
            x < (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]) + a[0])
        {
            inside = !inside;
        }
    }

    return inside;
}

/** Triangulate a profile by bridging holes into the outer ring and ear clipping.
 *
 * @param points The profile points, outer ring first followed by hole rings
 * @param count Number of points
 * @param outer Number of points in the outer ring
 * @param triangles Output vector of int triangle indices, cleared on failure
 * @return True if triangulation succeeded
 */
static bool box_triangulate(
    const vec4 *points,
    int count,
    int outer,
    ecs_vec_t *triangles)
{
    bool success = false;
    if (count > INT_MAX / (int)(sizeof(int) * 9)) {
        ecs_err("profile triangulation capacity exceeded");
        goto error;
    }

    int capacity = count * 3;
    int *polygon = ecs_os_malloc_n(int, capacity);
    int *merged = ecs_os_malloc_n(int, capacity);
    if (!polygon || !merged) {
        ecs_os_free(polygon);
        ecs_os_free(merged);
        goto error;
    }

    int length = outer;
    for (int i = 0; i < outer; i ++) {
        polygon[i] = i;
    }

    for (int first = outer; first < count;) {
        int end = first + 1;
        while (end < count && points[end][2] == points[first][2]) {
            end ++;
        }

        int n = end - first, best_i = -1, best_j = -1;
        double best = INFINITY;
        for (int i = 0; i < length; i ++) {
            for (int j = first; j < end; j ++) {
                const float *a = points[polygon[i]], *b = points[j];
                bool valid = box_polygon_contains(points, count,
                    (a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f);
                for (int k = 0; valid && k < count; k ++) {
                    valid = !box_intersects(a, b, points[k], points[(int)points[k][3]]);
                }

                for (int k = 0; valid && k < length; k ++) {
                    valid = !box_intersects(a, b, points[polygon[k]],
                        points[polygon[(k + 1) % length]]);
                }

                double dx = (double)a[0] - b[0], dy = (double)a[1] - b[1];
                double distance = dx * dx + dy * dy;
                if (valid && distance < best) {
                    best = distance;
                    best_i = i;
                    best_j = j - first;
                }
            }
        }

        if (best_i < 0 || length + n + 2 > capacity) {
            goto done;
        }

        int next = 0;
        for (int i = 0; i < length; i ++) {
            merged[next ++] = polygon[i];
            if (i == best_i) {
                for (int j = 0; j <= n; j ++) {
                    merged[next ++] = first + (best_j + j) % n;
                }

                merged[next ++] = polygon[i];
            }
        }

        int *swap = polygon;
        polygon = merged;
        merged = swap;
        length = next;
        first = end;
    }
    while (length > 2) {
        bool found = false;
        for (int i = 0; i < length; i ++) {
            int ia = polygon[(i + length - 1) % length], ib = polygon[i];
            int ic = polygon[(i + 1) % length];
            const float *a = points[ia], *b = points[ib], *c = points[ic];
            bool ear = box_cross2(a, b, c) > 1e-8;
            for (int j = 0; ear && j < length; j ++) {
                const float *p = points[polygon[j]];
                if (box_equal2(p, a) || box_equal2(p, b) || box_equal2(p, c)) {
                    continue;
                }

                if (box_cross2(a, b, p) >= -1e-8 && box_cross2(b, c, p) >= -1e-8 &&
                    box_cross2(c, a, p) >= -1e-8)
                {
                    ear = false;
                }
            }

            if (ear) {
                *ecs_vec_append_t(NULL, triangles, int) = ia;
                *ecs_vec_append_t(NULL, triangles, int) = ib;
                *ecs_vec_append_t(NULL, triangles, int) = ic;
                for (int j = i; j + 1 < length; j ++) {
                    polygon[j] = polygon[j + 1];
                }

                length --;
                found = true;
                break;
            }
        }

        if (!found) {
            goto done;
        }
    }

    success = true;
done:
    ecs_os_free(polygon);
    ecs_os_free(merged);
    if (!success) {
        ecs_vec_clear(triangles);
        ecs_err("profile triangulation failed");
    }
error:
    return success;
}

/** Normal of the side wall over the profile edge from a to b, in the XZ plane.
 *
 * @param a The start point of the edge
 * @param b The end point of the edge
 * @param out Receives the unit normal, zero for an edge of zero length
 */
static void box_profile_edge_normal(
    const float *a,
    const float *b,
    vec2 out)
{
    out[0] = b[1] - a[1];
    out[1] = a[0] - b[0];
    float length = hypotf(out[0], out[1]);
    if (length > 0) {
        out[0] /= length;
        out[1] /= length;
    } else {
        out[0] = out[1] = 0;
    }
}

/** Cosine of the crease angle, 30 degrees, above which a smooth extrusion keeps a corner */
#define BOX_PROFILE_CREASE 0.8660254f

/** Append the smooth side normals of a profile, one vector per point.
 *
 * Each vector holds the normal for the edge that ends at the point in xy and for the edge that
 * starts at it in zw: one blended normal where the profile is smooth, the edge normals at a
 * flagged corner or where the outline turns by more than the crease angle.
 *
 * @param points The profile points, as box_profile_resolve() packs them
 * @param corners The corner flag of each point
 * @param count Number of points
 * @param data The asset buffer to append to
 * @return False if the asset capacity is exceeded
 */
static bool box_profile_normals(
    const vec4 *points,
    const int *corners,
    int count,
    ecs_vec_t *data)
{
    int *previous = ecs_os_malloc_n(int, count);
    bool result = previous != NULL;
    for (int i = 0; result && i < count; i ++) {
        previous[i] = i;
    }

    for (int i = 0; result && i < count; i ++) {
        int next = (int)points[i][3];
        if (next >= 0 && next < count && next != i) {
            previous[next] = i;
        }
    }

    for (int i = 0; result && i < count; i ++) {
        vec2 in, out;
        box_profile_edge_normal(points[previous[i]], points[i], in);
        box_profile_edge_normal(points[i], points[(int)points[i][3]], out);
        if (in[0] == 0 && in[1] == 0) {
            glm_vec2_copy(out, in);
        } else if (out[0] == 0 && out[1] == 0) {
            glm_vec2_copy(in, out);
        }

        if (!corners[i] && glm_vec2_dot(in, out) >= BOX_PROFILE_CREASE) {
            vec2 n = {in[0] + out[0], in[1] + out[1]};
            glm_vec2_normalize(n);
            glm_vec2_copy(n, in);
            glm_vec2_copy(n, out);
        }

        result = box_asset_append(data, (vec4){in[0], in[1], out[0], out[1]});
    }

    ecs_os_free(previous);

    return result;
}

static bool box_path_pack(const ecs_world_t *world, ecs_entity_t entity, bool three,
    ecs_vec_t *data, int *offset, int *count)
{
    if (!entity || !ecs_is_alive(world, entity)) {
        return false;
    }
    const Path2D *p2 = three ? NULL : ecs_get(world, entity, Path2D);
    const Path3D *p3 = three ? ecs_get(world, entity, Path3D) : NULL;
    vec3 test;
    if (three ? !box_path3_evaluate(p3, 0, test) : !box_path2_evaluate(p2, 0, test)) {
        return false;
    }
    *offset = 24 + ecs_vec_count(data);
    *count = three ? box_path3_count(p3) : box_path2_count(p2);
    for (int i = 0; i < *count; i ++) {
        vec3 cubic[4];
        if (three) {
            box_path3_cubic(p3, i, cubic);
        } else {
            box_path2_cubic(p2, i, cubic);
        }
        for (int k = 0; k < 4; k ++) {
            if (!box_asset_append(data, (vec4){cubic[k][0], cubic[k][1], cubic[k][2], 0})) {
                return false;
            }
        }
    }
    return true;
}

/** Test whether all values in an array are finite.
 *
 * @param value The values
 * @param count Number of values
 * @return True if every value is finite
 */
static bool box_finite(
    const float *value,
    int count)
{
    bool result = true;
    for (int i = 0; result && i < count; i ++) {
        result = isfinite(value[i]);
    }

    return result;
}

static bool box_resolve(const ecs_world_t *world, const void *value, int kind,
    box_surface_data_t *d, vec3 min, vec3 max)
{
    ecs_vec_t data = {0}, profile = {0}, corners = {0};
    int outer = 0, count = 0, offset = 0, segments = 0;
    bool start = false, end = false, ok = false;
    glm_vec3_broadcast(INFINITY, min);
    glm_vec3_broadcast(-INFINITY, max);
    if (kind == 3) {
        const Lathe *s = value;
        if (!box_path_pack(world, s->profile, false, &data, &offset, &segments)) {
            goto done;
        }
        d->p[4][0] = offset;
        d->p[4][1] = segments;
        if (s->inner) {
            if (!box_path_pack(world, s->inner, false, &data, &offset, &segments)) {
                goto done;
            }
            d->p[4][2] = offset;
            d->p[4][3] = segments;
            d->p[1][0] = 1;
        }
        const vec4 *points = ecs_vec_first(&data);
        for (int i = 0; i < ecs_vec_count(&data); i ++) {
            float r = fabsf(points[i][0]);
            min[0] = min[2] = fminf(min[0], -r);
            max[0] = max[2] = fmaxf(max[0], r);
            min[1] = fminf(min[1], points[i][1]);
            max[1] = fmaxf(max[1], points[i][1]);
        }
        start = s->cap_start;
        end = s->cap_end;
    } else {
        ecs_entity_t entity = 0, contour = 0;
        int steps = BOX_PATH_STEPS;
        const Loft *loft = kind == 2 ? value : NULL;
        const LoftSection *sections = loft ? ecs_vec_first(&loft->sections) : NULL;
        int section_count = loft ? ecs_vec_count(&loft->sections) : 0;
        if (kind == 1) {
            const Sweep *s = value;
            entity = s->profile;
            contour = s->contour;
            steps = s->segments;
            if (!box_finite(s->scale, 2) || !isfinite(s->taper) || !isfinite(s->roll) ||
                !box_path_pack(world, s->path, true, &data, &offset, &segments))
            {
                goto done;
            }
            d->p[4][0] = offset;
            d->p[4][1] = segments;
            d->p[1][1] = s->taper;
            d->p[1][2] = s->roll;
            d->p[2][0] = s->scale[0];
            d->p[2][1] = s->scale[1];
            start = s->cap_start && !contour;
            end = s->cap_end && !contour;
            d->p[4][3] = contour ? 0 : 1;
            int64_t steps = (int64_t)segments * 32;
            if (steps + 1 > (1 << 24) - 24 - ecs_vec_count(&data) ||
                steps + 1 > INT_MAX / (int)sizeof(vec4))
            {
                goto done;
            }
            vec4 *frames = ecs_os_malloc_n(vec4, steps + 1);
            if (!frames) {
                goto done;
            }
            d->assets = ecs_vec_first(&data);
            box_surface_build_frames(d, frames, (int)steps);
            box_surface_build_lengths(d, frames, (int)steps, &d->p[9][2]);
            d->assets = NULL;
            d->p[8][0] = 24 + ecs_vec_count(&data);
            d->p[8][1] = (float)steps;
            for (int i = 0; i <= steps; i ++) {
                if (!box_asset_append(&data, frames[i])) {
                    ecs_os_free(frames);
                    goto done;
                }
            }
            ecs_os_free(frames);
        } else if (kind == 2) {
            if (section_count < 2 || !sections) {
                goto done;
            }
            entity = sections[0].profile;
            steps = loft->segments;
            start = loft->cap_start;
            end = loft->cap_end;
            d->p[0][2] = section_count;
        } else {
            const ProfileExtrusion *s = value;
            entity = s->profile;
            if (!isfinite(s->depth)) {
                goto done;
            }
            d->p[1][1] = fabsf(s->depth);
            start = s->cap_start;
            end = s->cap_end;
        }
        if (!box_profile_resolve(world, entity, contour, steps, &profile, &outer, &corners)) {
            goto done;
        }
        count = ecs_vec_count(&profile);
        d->p[5][0] = 24 + ecs_vec_count(&data);
        d->p[5][1] = count;
        d->p[5][2] = outer;
        d->p[1][0] = count > outer;
        const vec4 *points = ecs_vec_first(&profile);
        d->p[6][0] = d->p[6][2] = INFINITY;
        d->p[6][1] = d->p[6][3] = -INFINITY;
        for (int i = 0; i < count; i ++) {
            if (!box_asset_append(&data, points[i])) {
                goto done;
            }
            d->p[6][0] = fminf(d->p[6][0], points[i][0]);
            d->p[6][1] = fmaxf(d->p[6][1], points[i][0]);
            d->p[6][2] = fminf(d->p[6][2], points[i][1]);
            d->p[6][3] = fmaxf(d->p[6][3], points[i][1]);
        }
        if (kind == 1) {
            float radius = hypotf(fmaxf(fabsf(d->p[6][0]), fabsf(d->p[6][1])) * d->p[2][0],
                fmaxf(fabsf(d->p[6][2]), fabsf(d->p[6][3])) * d->p[2][1]);
            radius *= fmaxf(1, fabsf(d->p[1][1]));
            const vec4 *path = ecs_vec_first(&data);
            for (int i = 0; i < segments * 4; i ++) {
                for (int j = 0; j < 3; j ++) {
                    min[j] = fminf(min[j], path[i][j] - radius);
                    max[j] = fmaxf(max[j], path[i][j] + radius);
                }
            }
        } else if (kind == 2) {
            d->p[4][0] = 24 + ecs_vec_count(&data);
            for (int section = 0; section < section_count; section ++) {
                ecs_vec_t next = {0};
                int next_outer = 0;
                const LoftSection *s = &sections[section];
                if (!box_finite(s->offset, 3) || !box_finite(s->rotation, 3) ||
                    !box_finite(s->scale, 2) ||
                    !box_profile_resolve(world, s->profile, 0, steps, &next, &next_outer, NULL))
                {
                    ecs_vec_fini_t(NULL, &next, vec4);
                    goto done;
                }
                const vec4 *samples = ecs_vec_first(&next);
                bool match = ecs_vec_count(&next) == count && next_outer == outer;
                for (int i = 0; match && i < count; i ++) {
                    match = samples[i][2] == points[i][2] && samples[i][3] == points[i][3];
                }
                if (!match) {
                    ecs_vec_fini_t(NULL, &next, vec4);
                    goto done;
                }
                mat4 rotation;
                glm_euler_yxz(ECS_CONST_CAST(float*, s->rotation), rotation);
                for (int i = 0; i < count; i ++) {
                    vec3 p = {samples[i][0] * s->scale[0], 0, samples[i][1] * s->scale[1]};
                    glm_mat4_mulv3(rotation, p, 1, p);
                    glm_vec3_add(p, ECS_CONST_CAST(float*, s->offset), p);
                    if (!box_finite(p, 3) ||
                        !box_asset_append(&data, (vec4){p[0], p[1], p[2], 0}))
                    {
                        ecs_vec_fini_t(NULL, &next, vec4);
                        goto done;
                    }
                    glm_vec3_minv(min, p, min);
                    glm_vec3_maxv(max, p, max);
                }
                ecs_vec_fini_t(NULL, &next, vec4);
            }
        } else {
            min[0] = d->p[6][0]; max[0] = d->p[6][1];
            min[2] = d->p[6][2]; max[2] = d->p[6][3];
            min[1] = -d->p[1][1] * 0.5f; max[1] = -min[1];
            if (((const ProfileExtrusion*)value)->smooth) {
                d->p[8][0] = 24 + ecs_vec_count(&data);
                if (!box_profile_normals(points, ecs_vec_first(&corners), count, &data))
                {
                    goto done;
                }
            }
        }
    }
    if (kind != 3 && (start || end)) {
        ecs_vec_t triangles = {0};
        if (!box_triangulate(ecs_vec_first(&profile), count, outer, &triangles)) {
            ecs_vec_fini_t(NULL, &triangles, int);
            goto done;
        }
        d->p[7][0] = 24 + ecs_vec_count(&data);
        d->p[7][1] = ecs_vec_count(&triangles) / 3;
        const int *indices = ecs_vec_first(&triangles);
        for (int i = 0; i < ecs_vec_count(&triangles); i += 3) {
            if (!box_asset_append(&data, (vec4){indices[i], indices[i + 1], indices[i + 2], 0})) {
                ecs_vec_fini_t(NULL, &triangles, int);
                goto done;
            }
        }
        ecs_vec_fini_t(NULL, &triangles, int);
    }
    if (!box_finite(min, 3) || !box_finite(max, 3)) {
        goto done;
    }
    d->p[0][0] = kind;
    d->p[3][3] = 1 | (d->p[1][0] > 0 ? 2 : 0) | (start ? 4 : 0) | (end ? 8 : 0);
    d->asset_count = ecs_vec_count(&data);
    d->assets = ecs_vec_first(&data);
    if (kind == 1) {
        d->p[9][0] = box_surface_profile_length(d, 0);
        d->p[9][1] = count > outer ? box_surface_profile_length(d, 1) : 0;
    }
    data = (ecs_vec_t){0};
    ok = true;
done:
    ecs_vec_fini_t(NULL, &data, vec4);
    ecs_vec_fini_t(NULL, &profile, vec4);
    ecs_vec_fini_t(NULL, &corners, int);
    if (!ok) {
        *d = (box_surface_data_t){0};
        glm_vec3_zero(min);
        glm_vec3_zero(max);
    }
    return ok;
}

/** Resolve a tessellation step count.
 *
 * @param value The requested step count
 * @param fallback The step count to use if value is not positive
 * @return The fallback, or value clamped to [3, 256]
 */
static int box_steps(
    int value,
    int fallback)
{
    return value <= 0 ? fallback : (int)glm_clamp(value, 3, 256);
}

/** Rims of a beveled ProfileExtrusion, quantized */
typedef struct box_profile_bevel_t {
    float bottom;  /**< Radius of the edges of cap_start, 0 for none */
    float top;     /**< Radius of the edges of cap_end, 0 for none */
    int32_t steps; /**< Steps per rim */
} box_profile_bevel_t;

static void box_asset_key(const ecs_world_t *world, const void *value, int kind,
    const box_profile_bevel_t *bevel, box_shape_key_t *key)
{
    box_surface_data_t d = {0};
    vec3 min, max;
    if (!box_resolve(world, value, kind, &d, min, max)) {
        return;
    }
    int segments = 0, rings = 0;
    if (kind == 1) {
        segments = ((const Sweep*)value)->segments; rings = ((const Sweep*)value)->rings;
    } else if (kind == 2) {
        segments = ((const Loft*)value)->segments; rings = ((const Loft*)value)->rings;
    } else if (kind == 3) {
        segments = ((const Lathe*)value)->segments; rings = ((const Lathe*)value)->rings;
    }
    key->i[0] = box_steps(segments, 32);
    key->i[1] = box_steps(rings, 24);
    key->i[2] = (int)d.p[3][3];
    key->i[3] = kind;
    uint64_t hash = 14695981039346656037ull;
    const uint8_t *bytes = (const uint8_t*)d.assets;
    for (size_t i = 0; i < (size_t)d.asset_count * sizeof(vec4); i ++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    if (kind == 1) {
        /* The texture coordinates around the profile follow its length with the scale, which
         * only the ratio of the scale changes */
        float sx = fabsf(d.p[2][0]), sy = fabsf(d.p[2][1]);
        float ratio = sx > sy ? (sx ? sy / sx : 0) : (sy ? 2 - sx / sy : 0);
        bytes = (const uint8_t*)&ratio;
        for (size_t i = 0; i < sizeof(ratio); i ++) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
    }
    if (bevel) {
        /* Only the bevel variant hashes its rims, so that plain extrusions keep their keys. The
         * rims are in units while the mesh spans a unit depth, so the depth changes the mesh */
        float rims[4] = {bevel->bottom, bevel->top, (float)bevel->steps,
            box_shape_quantize(d.p[1][1])};
        bytes = (const uint8_t*)rims;
        for (size_t i = 0; i < sizeof(rims); i ++) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
    }
    ecs_os_memcpy(key->f, &hash, sizeof(hash));
    key->f[2] = d.p[4][1];
    key->f[3] = d.p[4][3];
    ecs_os_free(d.assets);
}

static void box_asset_generate(const ecs_world_t *world, const void *value, int kind,
    const box_shape_key_t *key, box_mesh_t *mesh)
{
    box_surface_data_t d = {0};
    vec3 min, max;
    if (!key->i[2] || !box_resolve(world, value, kind, &d, min, max)) {
        return;
    }
    int count = (int)d.p[5][1], outer = (int)d.p[5][2];
    int rows = kind == 7 ? 1 : key->i[1];
    /* A Sweep puts a ring on each corner of its path, the other rings at even lengths between */
    float *row_v = NULL, *row_p = NULL;
    if (kind == 1 && d.p[4][1] >= 2) {
        int segments = (int)d.p[4][1];
        float *corner_p = ecs_os_malloc_n(float, segments * 2);
        int corners = corner_p ?
            box_surface_sweep_corners(&d, corner_p, corner_p + segments) : 0;
        if (corners) {
            rows = rows > corners + 1 ? rows : corners + 1;
            row_v = ecs_os_malloc_n(float, (rows + 1) * 2);
        }
        if (row_v) {
            row_p = row_v + rows + 1;
            const float *corner_v = corner_p + segments;
            int anchor = 0;
            row_v[0] = 0;
            row_p[0] = -1;
            for (int c = 0; c <= corners; c ++) {
                int j = rows;
                float v = 1;
                if (c < corners) {
                    v = corner_v[c];
                    j = (int)roundf(v * rows);
                    j = (int)glm_clamp(j, anchor + 1, rows - corners + c);
                }
                for (int k = anchor + 1; k < j; k ++) {
                    row_v[k] = row_v[anchor] + (v - row_v[anchor]) * (k - anchor) / (j - anchor);
                    row_p[k] = -1;
                }
                row_v[j] = v;
                row_p[j] = c < corners ? corner_p[c] : -1;
                anchor = j;
            }
        }
        ecs_os_free(corner_p);
    }
    int edges = kind == 3 ? key->i[0] : count;
    int regions = (int)d.p[3][3];
    int64_t vertices = (int64_t)edges * (rows + 1) * 2 * (kind == 3 ? 4 : 1) + count * 2;
    int64_t indices = (int64_t)edges * rows * 6 * (kind == 3 ? 4 : 1) + (int64_t)count * 18;
    if (vertices > INT_MAX / (int)sizeof(box_vertex_t) ||
        indices > INT_MAX / (int)sizeof(uint32_t))
    {
        ecs_err("surface mesh capacity exceeded");
        ecs_os_free(row_v);
        ecs_os_free(d.assets);
        return;
    }
    uint64_t total_size = (uint64_t)vertices * sizeof(box_vertex_t) +
        (uint64_t)indices * sizeof(uint32_t) + (uint64_t)d.asset_count * sizeof(vec4);
    if (total_size > INT_MAX) {
        ecs_err("surface mesh allocation exceeds capacity");
        ecs_os_free(row_v);
        ecs_os_free(d.assets);
        return;
    }
    mesh->vertices = ecs_os_malloc_n(box_vertex_t, vertices);
    mesh->indices = ecs_os_malloc_n(uint32_t, indices);
    if (!mesh->vertices || !mesh->indices) {
        ecs_err("surface mesh allocation failed");
        ecs_os_free(row_v);
        ecs_os_free(d.assets);
        box_mesh_fini(mesh);
        return;
    }
    mesh->vertex_capacity = (int32_t)vertices;
    mesh->index_capacity = (int32_t)indices;
    const vec4 *points = kind == 3 ? NULL : d.assets + (int)d.p[5][0] - 24;
    float ring_length[2] = {d.p[9][0], d.p[9][1]};
    float along[2] = {0, 0};
    for (int region = 0; region < (kind == 3 ? 4 : 1); region ++) {
        if (!(regions & (1 << region))) {
            continue;
        }
        int nrows = region >= 2 ? 1 : rows;
        for (int i = 0; i < edges; i ++) {
            int next = kind == 3 ? i + 1 : (int)points[i][3];
            if (next == i) {
                continue;
            }
            int inner = kind != 3 && i >= outer;
            int first = inner ? outer : 0;
            int n = kind == 3 ? edges : (inner ? count - outer : outer);
            if (kind == 1 && d.p[4][3] == 0) {
                n = count - 1;
            }
            float u0 = (float)(i - first) / n;
            float u1 = (float)(next - first) / n;
            float t0 = u0, t1 = u1;
            if (kind == 1 && ring_length[inner] > 0) {
                /* A Sweep maps its texture by the length around the profile, the closing edge
                 * of a ring included, and its rows sit at even lengths along the path */
                t0 = along[inner] / ring_length[inner];
                along[inner] += box_surface_profile_edge(&d, i);
                t1 = fminf(along[inner] / ring_length[inner], 1);
            }
            int base = mesh->vertex_count;
            for (int j = 0; j <= nrows; j ++) {
                for (int side = 0; side < 2; side ++) {
                    float u = side ? u1 : u0, v = (float)j / nrows;
                    float tu = side ? t1 : t0;
                    if (row_v && nrows == rows) {
                        v = row_v[j];
                    }
                    float pv = kind == 1 ? box_surface_sweep_parameter(&d, v) : v;
                    if (row_v && nrows == rows && row_p[j] >= 0) {
                        pv = row_p[j];
                    }
                    vec3 position = {u, pv, kind == 3 ? region : inner};
                    vec3 normal = {0};
                    if (kind == 7) {
                        int p = side ? next : i;
                        glm_vec3_copy((vec3){points[p][0], v - 0.5f, points[p][1]}, position);
                        glm_vec3_copy((vec3){points[next][1] - points[i][1], 0,
                            points[i][0] - points[next][0]}, normal);
                        glm_vec3_normalize(normal);
                        if (d.p[8][0] > 0) {
                            const vec4 *smooth = d.assets + (int)d.p[8][0] - 24;
                            glm_vec3_copy(side ? (vec3){smooth[next][0], 0, smooth[next][1]} :
                                (vec3){smooth[i][2], 0, smooth[i][3]}, normal);
                        }
                    }
                    box_mesh_vertex(mesh, position, normal, tu + (inner ? 2 : 0), v);
                }
            }
            for (int j = 0; j < nrows; j ++) {
                int a = base + j * 2, b = a + 2;
                if (kind == 3 && (region == 1 || region == 3)) {
                    box_mesh_triangle(mesh, a, a + 1, b);
                    box_mesh_triangle(mesh, a + 1, b + 1, b);
                } else {
                    box_mesh_triangle(mesh, a, b, a + 1);
                    box_mesh_triangle(mesh, a + 1, b, b + 1);
                }
            }
        }
    }
    if (kind != 3) {
        for (int side = 0; side < 2; side ++) {
            if (!(regions & (4 << side))) {
                continue;
            }
            int base = mesh->vertex_count;
            for (int i = 0; i < count; i ++) {
                vec3 p = {(float)i, 0, (float)(8 + side)};
                if (kind == 7) {
                    glm_vec3_copy((vec3){points[i][0], (float)side - 0.5f, points[i][1]}, p);
                }
                box_mesh_vertex(mesh, p, (vec3){0, side ? 1 : -1, 0}, points[i][0], points[i][1]);
            }
            const vec4 *triangles = d.assets + (int)d.p[7][0] - 24;
            for (int i = 0; i < (int)d.p[7][1]; i ++) {
                box_mesh_triangle(mesh, base + (int)triangles[i][0],
                    base + (int)triangles[i][side ? 2 : 1],
                    base + (int)triangles[i][side ? 1 : 2]);
            }
        }
    }
    ecs_os_free(row_v);
    mesh->surface_assets = d.assets;
    mesh->surface_asset_count = d.asset_count;
}

#define BOX_ASSET_SHAPE(NAME, KIND) \
static void box_##NAME##_key(const ecs_world_t *world, const void *value, box_shape_key_t *key) \
{ \
    box_asset_key(world, value, KIND, NULL, key); \
} \
static void box_##NAME##_generate(const ecs_world_t *world, const void *value, \
    const box_shape_key_t *key, box_mesh_t *mesh) \
{ \
    box_asset_generate(world, value, KIND, key, mesh); \
} \
static void box_##NAME##_surface(const ecs_world_t *world, const void *value, \
    box_surface_data_t *d, vec3 min, vec3 max) \
{ \
    box_resolve(world, value, KIND, d, min, max); \
}

BOX_ASSET_SHAPE(sweep, 1)
BOX_ASSET_SHAPE(loft, 2)
BOX_ASSET_SHAPE(lathe, 3)
BOX_ASSET_SHAPE(profile, 7)

/** Get the rims of the Bevel of a ProfileExtrusion: radius or bottom for the edges of
 * cap_start and radius or top for those of cap_end, none for an end without a cap.
 *
 * @param value The ProfileExtrusion and the Bevel
 * @param out The rims
 */
static void box_profile_bevel_of(
    const box_bevel_extrusion_t *value,
    box_profile_bevel_t *out)
{
    const Bevel *bevel = &value->bevel;
    out->bottom = value->host.cap_start ?
        glm_max(box_shape_quantize(box_bevel_rim(bevel, bevel->bottom)), 0.0f) : 0.0f;
    out->top = value->host.cap_end ?
        glm_max(box_shape_quantize(box_bevel_rim(bevel, bevel->top)), 0.0f) : 0.0f;
    out->steps = box_bevel_segments(bevel);
}

/** Key of a beveled ProfileExtrusion: the key of the extrusion with its rims hashed in.
 *
 * @param world The world
 * @param value The ProfileExtrusion and the Bevel
 * @param key The key
 */
static void box_profile_bevel_key(
    const ecs_world_t *world,
    const void *value,
    box_shape_key_t *key)
{
    box_profile_bevel_t bevel;
    box_profile_bevel_of(value, &bevel);
    box_asset_key(world, value, 7, &bevel, key);
}

/** Surface of a beveled ProfileExtrusion: the surface of the extrusion without the bevel, with
 * the rim radii in p[2] so that picking hits the rounded caps through their triangles.
 *
 * @param world The world
 * @param value The ProfileExtrusion and the Bevel
 * @param d The surface parameters to fill
 * @param min The minimum corner of the bounds to fill
 * @param max The maximum corner of the bounds to fill
 */
static void box_profile_bevel_surface(
    const ecs_world_t *world,
    const void *value,
    box_surface_data_t *d,
    vec3 min,
    vec3 max)
{
    if (box_resolve(world, value, 7, d, min, max)) {
        box_profile_bevel_t bevel;
        box_profile_bevel_of(value, &bevel);
        d->p[2][0] = bevel.bottom;
        d->p[2][1] = bevel.top;
    }
}

/** Check whether the Bevel of a ProfileExtrusion rounds no cap edge.
 *
 * @param value The ProfileExtrusion and the Bevel
 * @return True when the extrusion resolves as a plain extrusion
 */
static bool box_profile_bevel_flat(
    const void *value)
{
    const box_bevel_extrusion_t *v = value;
    box_profile_bevel_t bevel;
    box_profile_bevel_of(v, &bevel);

    return (bevel.bottom == 0.0f && bevel.top == 0.0f) || !isfinite(v->host.depth) ||
        v->host.depth == 0.0f;
}

/** Test whether a point lies inside one ring of a profile.
 *
 * @param ring The points of the rings, linked by the next index in w
 * @param count Number of points
 * @param id The ring, the z of its points
 * @param x The X coordinate of the point
 * @param z The Z coordinate of the point
 * @return True when the point lies inside the ring
 */
static bool box_profile_ring_contains(
    const vec4 *ring,
    int count,
    float id,
    float x,
    float z)
{
    bool inside = false;
    for (int i = 0; i < count; i ++) {
        const float *a = ring[i], *b = ring[(int)ring[i][3]];
        if (a[2] == id && (a[1] > z) != (b[1] > z) &&
            x < (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]) + a[0])
        {
            inside = !inside;
        }
    }

    return inside;
}

/** Test whether the rings of an inset profile are no longer valid: two edges that share no
 * point cross, a hole left the outer ring or a ring lies inside a hole.
 *
 * @param ring The points of the rings, linked by the next index in w
 * @param count Number of points
 * @param outer Number of points of the outer ring
 * @return True when the rings are not valid
 */
static bool box_profile_crosses(
    const vec4 *ring,
    int count,
    int outer)
{
    bool result = false;
    for (int i = 0; !result && i < count; i ++) {
        int a = (int)ring[i][3];
        for (int j = i + 1; !result && j < count; j ++) {
            int b = (int)ring[j][3];
            if (a == j || b == i || a == i || b == j) {
                continue;
            }

            result = box_intersects(ring[i], ring[a], ring[j], ring[b]);
        }
    }

    for (int h = outer; !result && h < count; h ++) {
        if (h > outer && ring[h][2] == ring[h - 1][2]) {
            continue;
        }

        result = !box_profile_ring_contains(ring, count, ring[0][2], ring[h][0], ring[h][1]);
        for (int g = 0; !result && g < count; g ++) {
            float id = ring[g][2];
            if (g >= outer && id != ring[h][2] && (g == outer || id != ring[g - 1][2])) {
                result = box_profile_ring_contains(ring, count, id, ring[0][0], ring[0][1]) ||
                    box_profile_ring_contains(ring, count, id, ring[h][0], ring[h][1]);
            }
        }
    }

    return result;
}

/** Inset the points of a profile by a distance along their offset directions.
 *
 * @param points The profile points, as box_profile_resolve() packs them
 * @param offsets The inset direction of each point
 * @param count Number of points
 * @param inset The distance
 * @param out The inset points, with the ring and the next index of the profile
 */
static void box_profile_inset(
    const vec4 *points,
    const vec2 *offsets,
    int count,
    float inset,
    vec4 *out)
{
    for (int i = 0; i < count; i ++) {
        out[i][0] = points[i][0] + offsets[i][0] * inset;
        out[i][1] = points[i][1] + offsets[i][1] * inset;
        out[i][2] = points[i][2];
        out[i][3] = points[i][3];
    }
}

/** Clamp the rim radius of a capped end of a beveled extrusion.
 *
 * The bound is deterministic: 0.9 times the smallest inset at which an edge of the profile
 * shrinks to zero length, then halved, up to 12 times and else 0, while the innermost rings
 * are not valid (see box_profile_crosses()).
 *
 * @param points The profile points
 * @param offsets The inset direction of each point
 * @param count Number of points
 * @param outer Number of points of the outer ring
 * @param limit The inset at which an edge shrinks to zero length
 * @param radius The authored radius
 * @param ring Scratch room for count points
 * @return The radius
 */
static float box_profile_bevel_clamp(
    const vec4 *points,
    const vec2 *offsets,
    int count,
    int outer,
    float limit,
    float radius,
    vec4 *ring)
{
    float result = glm_min(radius, 0.9f * limit);
    int tries = 0;
    for (; result > 0.0f && tries < 12; tries ++) {
        box_profile_inset(points, offsets, count, result, ring);
        if (!box_profile_crosses(ring, count, outer)) {
            break;
        }

        result *= 0.5f;
    }

    return tries < 12 ? result : 0.0f;
}

/** Append the cap of an end of an extrusion, triangulated on its innermost ring.
 *
 * @param mesh The mesh
 * @param ring The points of the ring
 * @param count Number of points
 * @param outer Number of points of the outer ring
 * @param side 0 for the bottom, 1 for the top
 */
static void box_profile_bevel_cap(
    box_mesh_t *mesh,
    const vec4 *ring,
    int count,
    int outer,
    int side)
{
    ecs_vec_t triangles = {0};
    if (box_triangulate(ring, count, outer, &triangles)) {
        int base = mesh->vertex_count;
        for (int i = 0; i < count; i ++) {
            box_mesh_vertex(mesh, (vec3){ring[i][0], (float)side - 0.5f, ring[i][1]},
                (vec3){0, side ? 1.0f : -1.0f, 0}, ring[i][0], ring[i][1]);
        }

        const int *indices = ecs_vec_first(&triangles);
        for (int i = 0; i + 2 < ecs_vec_count(&triangles); i += 3) {
            box_mesh_triangle(mesh, base + indices[i], base + indices[i + 1],
                base + indices[i + 2]);
        }
    }

    ecs_vec_fini_t(NULL, &triangles, int);
}

/** Generate the mesh of a beveled ProfileExtrusion.
 *
 * The bevel takes the place of the outer steps of each capped end: step k at angle phi insets
 * the profile by r * (1 - cos(phi)) and lowers the end by r * (1 - sin(phi)), each point
 * against its smooth normal, or along the mitre of its two edge normals at a corner.
 *
 * The rims belong to the wall of their contour. Their normals blend from the wall normal to the
 * cap normal and keep a small horizontal part at the cap, which only caps lack.
 *
 * @param world The world
 * @param value The ProfileExtrusion and the Bevel
 * @param key The key
 * @param mesh The mesh
 */
static void box_profile_bevel_generate(
    const ecs_world_t *world,
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    const box_bevel_extrusion_t *v = value;
    box_profile_bevel_t bevel;
    box_surface_data_t d = {0};
    ecs_vec_t profile = {0}, corners = {0}, normals = {0};
    vec2 *offsets = NULL;
    vec4 *rings = NULL;
    int outer = 0;
    vec3 min, max;
    box_profile_bevel_of(v, &bevel);
    if (!key->i[2] || !box_resolve(world, value, 7, &d, min, max)) {
        goto done;
    }

    int count = (int)d.p[5][1];
    const vec4 *points = d.assets + (int)d.p[5][0] - 24;
    const vec4 *smooth = d.p[8][0] > 0 ? d.assets + (int)d.p[8][0] - 24 : NULL;
    float depth = d.p[1][1];
    int regions = (int)d.p[3][3];
    int steps = bevel.steps;
    if (!box_profile_resolve(world, v->host.profile, 0, BOX_PATH_STEPS, &profile, &outer,
        &corners) ||
        ecs_vec_count(&profile) != count || outer != (int)d.p[5][2] ||
        !box_profile_normals(ecs_vec_first(&profile), ecs_vec_first(&corners), count, &normals))
    {
        goto done;
    }

    const vec4 *n = ecs_vec_first(&normals);
    offsets = ecs_os_malloc_n(vec2, count);
    rings = ecs_os_malloc_n(vec4, (size_t)count * (size_t)(2 * (steps + 1) + 1));
    float limit = INFINITY;
    for (int i = 0; i < count; i ++) {
        if (n[i][0] == n[i][2] && n[i][1] == n[i][3]) {
            offsets[i][0] = -n[i][0];
            offsets[i][1] = -n[i][1];
        } else {
            float den = glm_max(1.0f + n[i][0] * n[i][2] + n[i][1] * n[i][3], 0.05f);
            offsets[i][0] = -(n[i][0] + n[i][2]) / den;
            offsets[i][1] = -(n[i][1] + n[i][3]) / den;
        }
    }

    for (int i = 0; i < count; i ++) {
        int next = (int)points[i][3];
        float ex = points[next][0] - points[i][0], ey = points[next][1] - points[i][1];
        float shrink = -((offsets[next][0] - offsets[i][0]) * ex +
            (offsets[next][1] - offsets[i][1]) * ey);
        if (next != i && shrink > 0.0f) {
            limit = glm_min(limit, (ex * ex + ey * ey) / shrink);
        }
    }

    vec4 *scratch = rings + (size_t)count * (size_t)(2 * (steps + 1));
    float radii[2] = {
        box_profile_bevel_clamp(points, offsets, count, outer, limit, bevel.bottom, scratch),
        box_profile_bevel_clamp(points, offsets, count, outer, limit, bevel.top, scratch)
    };
    if (radii[0] + radii[1] > depth) {
        float scale = depth / (radii[0] + radii[1]);
        radii[0] *= scale;
        radii[1] *= scale;
    }

    float ends[2] = {-0.5f * depth + radii[0], 0.5f * depth - radii[1]};
    for (int side = 0; side < 2; side ++) {
        for (int k = 0; k <= steps; k ++) {
            float phi = 0.5f * BOX_PI * (float)k / (float)steps;
            float inset = k == steps ? radii[side] : radii[side] * (1.0f - cosf(phi));
            box_profile_inset(points, offsets, count, k ? inset : 0.0f,
                rings + (size_t)count * (size_t)(side * (steps + 1) + k));
        }
    }

    float wall_v[2] = {ends[0] / depth + 0.5f, ends[1] / depth + 0.5f};
    for (int i = 0; i < count; i ++) {
        int next = (int)points[i][3];
        if (next == i) {
            continue;
        }

        int inner = i >= outer;
        int first = inner ? outer : 0;
        int m = inner ? count - outer : outer;
        float us[2] = {(float)(i - first) / m + (inner ? 2 : 0),
            (float)(next - first) / m + (inner ? 2 : 0)};
        vec3 wall[2];
        for (int side = 0; side < 2; side ++) {
            glm_vec3_copy((vec3){points[next][1] - points[i][1], 0,
                points[i][0] - points[next][0]}, wall[side]);
            glm_vec3_normalize(wall[side]);
            if (smooth) {
                glm_vec3_copy(side ? (vec3){smooth[next][0], 0, smooth[next][1]} :
                    (vec3){smooth[i][2], 0, smooth[i][3]}, wall[side]);
            }
        }

        if (ends[1] - ends[0] > 1e-6f * depth) {
            int base = mesh->vertex_count;
            for (int j = 0; j < 2; j ++) {
                for (int side = 0; side < 2; side ++) {
                    const float *p = points[side ? next : i];
                    box_mesh_vertex(mesh, (vec3){p[0], ends[j] / depth, p[1]}, wall[side],
                        us[side], wall_v[j]);
                }
            }

            box_mesh_triangle(mesh, base, base + 2, base + 1);
            box_mesh_triangle(mesh, base + 1, base + 2, base + 3);
        }

        for (int end = 0; end < 2; end ++) {
            float r = radii[end], sign = end ? 1.0f : -1.0f;
            if (r <= 0.0f || !(regions & (4 << end))) {
                continue;
            }

            int base = mesh->vertex_count;
            for (int k = 0; k <= steps; k ++) {
                float phi = 0.5f * BOX_PI * (float)k / (float)steps;
                float c = k == steps ? 1e-3f : cosf(phi);
                float sn = k == 0 ? 0.0f : (k == steps ? 1.0f : sinf(phi));
                float y = k == 0 ? ends[end] :
                    sign * (k == steps ? 0.5f * depth : 0.5f * depth - r * (1.0f - sn));
                const vec4 *ring = rings + (size_t)count * (size_t)(end * (steps + 1) + k);
                float tv = wall_v[end] + sign * r * phi / depth;
                for (int side = 0; side < 2; side ++) {
                    const float *p = ring[side ? next : i];
                    vec3 normal = {wall[side][0] * c, sn * sign, wall[side][2] * c};
                    glm_vec3_normalize(normal);
                    box_mesh_vertex(mesh, (vec3){p[0], y / depth, p[1]}, normal, us[side], tv);
                }
            }

            for (int k = 0; k < steps; k ++) {
                int a = base + k * 2, b = a + 2;
                box_mesh_triangle(mesh, a, b, a + 1);
                box_mesh_triangle(mesh, a + 1, b, b + 1);
            }
        }
    }

    for (int end = 0; end < 2; end ++) {
        if (regions & (4 << end)) {
            const vec4 *ring = rings + (size_t)count * (size_t)(end * (steps + 1) +
                (radii[end] > 0.0f ? steps : 0));
            box_profile_bevel_cap(mesh, ring, count, outer, end);
        }
    }

    mesh->surface_assets = d.assets;
    mesh->surface_asset_count = d.asset_count;
    d.assets = NULL;
done:
    ecs_os_free(d.assets);
    ecs_os_free(offsets);
    ecs_os_free(rings);
    ecs_vec_fini_t(NULL, &profile, vec4);
    ecs_vec_fini_t(NULL, &corners, int);
    ecs_vec_fini_t(NULL, &normals, vec4);
}

int32_t box_profile_region(
    const box_vertex_t *vertex)
{
    const float *n = vertex->normal;
    int32_t result = vertex->uv[0] >= 2.0f ? SurfaceInner : SurfaceOuter;
    if (n[0] == 0.0f && n[2] == 0.0f && n[1] != 0.0f) {
        result = n[1] > 0.0f ? SurfaceEnd : SurfaceStart;
    }

    return result;
}

bool box_profile_contains(
    const box_surface_data_t *d,
    float x,
    float z)
{
    bool result = false;
    if (d->assets) {
        result = box_polygon_contains(d->assets + (int)d->p[5][0] - 24, (int)d->p[5][1], x, z);
    }

    return result;
}

/** Constructor for Sweep that sets unit scale and taper with both caps enabled.
 *
 * @param ptr The component values
 * @param count Number of values
 * @param ti The type info
 */
static void box_sweep_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Sweep *values = ptr;
    for (int i = 0; i < count; i ++) {
        values[i] = (Sweep){.scale = {1, 1}, .taper = 1, .cap_start = true, .cap_end = true};
    }
}

/** Constructor for LoftSection that sets unit scale.
 *
 * @param ptr The component values
 * @param count Number of values
 * @param ti The type info
 */
static void box_section_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    LoftSection *values = ptr;
    for (int i = 0; i < count; i ++) {
        values[i] = (LoftSection){.scale = {1, 1}};
    }
}

/** Test whether a profile is, or references, an asset entity.
 *
 * @param world The world
 * @param profile The Profile2D entity
 * @param asset The asset entity
 * @return True if profile equals asset or uses it as outer path or hole
 */
static bool box_profile_uses(
    const ecs_world_t *world,
    ecs_entity_t profile,
    ecs_entity_t asset)
{
    bool result = profile == asset;
    if (!result) {
        const Profile2D *p = profile && ecs_is_alive(world, profile) ?
            ecs_get(world, profile, Profile2D) : NULL;
        if (p) {
            result = p->outer == asset;
            const ecs_entity_t *holes = ecs_vec_first(&p->holes);
            for (int i = 0; !result && i < ecs_vec_count(&p->holes); i ++) {
                result = holes[i] == asset;
            }
        }
    }

    return result;
}

/** Test whether a surface entity depends on an asset entity.
 *
 * @param world The world
 * @param e The surface entity
 * @param c Component index: 0 Sweep, 1 Loft, 2 Lathe, otherwise ProfileExtrusion
 * @param asset The asset entity
 * @return True if the surface references the asset
 */
static bool box_consumer_uses(
    const ecs_world_t *world,
    ecs_entity_t e,
    int c,
    ecs_entity_t asset)
{
    bool result = false;
    if (c == 0) {
        const Sweep *s = ecs_get(world, e, Sweep);
        result = s->path == asset || s->contour == asset ||
            box_profile_uses(world, s->profile, asset);
    } else if (c == 1) {
        const Loft *s = ecs_get(world, e, Loft);
        const LoftSection *sections = ecs_vec_first(&s->sections);
        for (int i = 0; !result && i < ecs_vec_count(&s->sections); i ++) {
            result = box_profile_uses(world, sections[i].profile, asset);
        }
    } else if (c == 2) {
        const Lathe *s = ecs_get(world, e, Lathe);
        result = s->profile == asset || s->inner == asset;
    } else {
        result = box_profile_uses(world, ecs_get(world, e, ProfileExtrusion)->profile, asset);
    }

    return result;
}

/** Observer that marks surfaces modified when a path or profile they use changes.
 *
 * On removal of an asset, dependent ShapeMesh components are released and reset instead.
 *
 * @param it The observer iterator
 */
static void box_asset_changed(
    ecs_iter_t *it)
{
    ecs_entity_t components[] = {
        ecs_id(Sweep), ecs_id(Loft), ecs_id(Lathe), ecs_id(ProfileExtrusion)};
    for (int c = 0; c < 4; c ++) {
        ecs_query_t *query = ecs_query(it->world, {.terms = {{.id = components[c]}},
            .flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled});
        ecs_iter_t entities = ecs_query_iter(it->world, query);
        while (ecs_query_next(&entities)) {
            for (int i = 0; i < entities.count; i ++) {
                ecs_entity_t e = entities.entities[i];
                bool affected = false;
                for (int j = 0; j < it->count; j ++) {
                    affected |= box_consumer_uses(it->world, e, c, it->entities[j]);
                }

                if (!affected) {
                    continue;
                }

                if (it->event == EcsOnRemove) {
                    ShapeMesh *shape = ecs_get_mut(it->world, e, ShapeMesh);
                    if (shape) {
                        box_shape_mesh_release(box_mesh_cache_of(it->world), shape);
                        *shape = (ShapeMesh){0};
                        glm_vec3_one(shape->scale);
                        ecs_modified(it->world, e, ShapeMesh);
                    }
                } else if (ecs_owns_id(it->world, e, components[c])) {
                    ecs_modified_id(it->world, e, components[c]);
                }
            }
        }

        ecs_query_fini(query);
    }
}

void box_geometry_profiles_init(
    ecs_world_t *world)
{
    ecs_set_hooks(world, Sweep, {.ctor = box_sweep_ctor});
    ecs_set_hooks(world, LoftSection, {.ctor = box_section_ctor});
    box_owned_hooks(world, Loft, BOX_OWNED_VEC(Loft, sections, LoftSection));
    box_owned_hooks(world, Profile2D, BOX_OWNED_VEC(Profile2D, holes, ecs_entity_t));
    ecs_add_pair(world, ecs_id(Profile2D), EcsOnInstantiate, EcsInherit);
    box_shape_register(world, &(box_shape_desc_t){.component = ecs_id(Sweep),
        .key_world = box_sweep_key, .generate_world = box_sweep_generate,
        .surface_world = box_sweep_surface});
    box_shape_register(world, &(box_shape_desc_t){.component = ecs_id(Loft),
        .key_world = box_loft_key, .generate_world = box_loft_generate,
        .surface_world = box_loft_surface});
    box_shape_register(world, &(box_shape_desc_t){.component = ecs_id(Lathe),
        .key_world = box_lathe_key, .generate_world = box_lathe_generate,
        .surface_world = box_lathe_surface});
    box_shape_register(world, &(box_shape_desc_t){.component = ecs_id(ProfileExtrusion),
        .key_world = box_profile_key, .generate_world = box_profile_generate,
        .surface_world = box_profile_surface, .exclude = ecs_id(Bevel)});
    box_geometry_bevel_host(world, &(box_shape_desc_t){.key_world = box_profile_bevel_key,
        .generate_world = box_profile_bevel_generate, .surface_world = box_profile_bevel_surface},
        ecs_id(ProfileExtrusion), ECS_SIZEOF(ProfileExtrusion),
        (ecs_size_t)offsetof(box_bevel_extrusion_t, bevel), ECS_SIZEOF(box_bevel_extrusion_t),
        box_profile_bevel_flat);
    ecs_entity_t assets[] = {ecs_id(Path2D), ecs_id(Path3D), ecs_id(Profile2D)};
    for (int i = 0; i < 3; i ++) {
        ecs_observer(world, {.query.terms = {{.id = assets[i]}},
            .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
            .events = {EcsOnSet, EcsOnRemove}, .callback = box_asset_changed});
    }
}
