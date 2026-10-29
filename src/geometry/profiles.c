/**
 * @file geometry/profiles.c
 * @brief Surface asset packing, triangulation, and mesh generation for profile shapes.
 *
 * Covers Sweep, Loft, Lathe, and ProfileExtrusion, and re-triggers them when their paths change.
 */

#include "paths.h"
#include <limits.h>
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

static bool box_profile_resolve(const ecs_world_t *world, ecs_entity_t entity,
    ecs_entity_t contour, ecs_vec_t *data, int *outer)
{
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
        if (!box_path2_sample(world, path_entity, &samples)) {
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
        for (int i = 0; i < count; i ++) {
            int index = reverse ? count - 1 - i : i;
            int next = i + 1 == count ? (profile ? first : first + i) : first + i + 1;
            if (!box_asset_append(data, (vec4){points[index][0], points[index][1], ring, next})) {
                ecs_vec_fini_t(NULL, &samples, vec2);
                return false;
            }
        }
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
    *count = ecs_vec_count(three ? &p3->segments : &p2->segments);
    vec3 start = {0}, origin = {0};
    ecs_os_memcpy(start, three ? p3->start : p2->start, (three ? 3 : 2) * sizeof(float));
    glm_vec3_copy(start, origin);
    for (int i = 0; i < *count; i ++) {
        vec3 end = {0}, c1 = {0}, c2 = {0};
        PathSegmentKind kind;
        if (three) {
            const PathSegment3D *s = ecs_vec_get_t(&p3->segments, PathSegment3D, i);
            kind = s->kind;
            glm_vec3_copy(ECS_CONST_CAST(float*, s->end), end);
            glm_vec3_copy(ECS_CONST_CAST(float*, s->control1), c1);
            glm_vec3_copy(ECS_CONST_CAST(float*, s->control2), c2);
        } else {
            const PathSegment2D *s = ecs_vec_get_t(&p2->segments, PathSegment2D, i);
            kind = s->kind;
            glm_vec2_copy(ECS_CONST_CAST(float*, s->end), end);
            glm_vec2_copy(ECS_CONST_CAST(float*, s->control1), c1);
            glm_vec2_copy(ECS_CONST_CAST(float*, s->control2), c2);
        }
        if (kind == PathLine) {
            glm_vec3_lerp(start, end, 1.0f / 3, c1);
            glm_vec3_lerp(start, end, 2.0f / 3, c2);
        }
        if (!box_asset_append(data, (vec4){start[0], start[1], start[2], 0}) ||
            !box_asset_append(data, (vec4){c1[0], c1[1], c1[2], 0}) ||
            !box_asset_append(data, (vec4){c2[0], c2[1], c2[2], 0}) ||
            !box_asset_append(data, (vec4){end[0], end[1], end[2], 0}))
        {
            return false;
        }
        glm_vec3_copy(end, start);
    }
    if ((three ? p3->closed : p2->closed) && !glm_vec3_eqv(start, origin)) {
        for (int i = 0; i < 4; i ++) {
            vec3 point;
            glm_vec3_lerp(start, origin, (float)i / 3, point);
            if (!box_asset_append(data, (vec4){point[0], point[1], point[2], 0})) {
                return false;
            }
        }
        (*count) ++;
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
    ecs_vec_t data = {0}, profile = {0};
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
        const Loft *loft = kind == 2 ? value : NULL;
        const LoftSection *sections = loft ? ecs_vec_first(&loft->sections) : NULL;
        int section_count = loft ? ecs_vec_count(&loft->sections) : 0;
        if (kind == 1) {
            const Sweep *s = value;
            entity = s->profile;
            contour = s->contour;
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
        if (!box_profile_resolve(world, entity, contour, &profile, &outer)) {
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
                    !box_profile_resolve(world, s->profile, 0, &next, &next_outer))
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
    data = (ecs_vec_t){0};
    ok = true;
done:
    ecs_vec_fini_t(NULL, &data, vec4);
    ecs_vec_fini_t(NULL, &profile, vec4);
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

static void box_asset_key(const ecs_world_t *world, const void *value, int kind,
    box_shape_key_t *key)
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
    int edges = kind == 3 ? key->i[0] : count;
    int regions = (int)d.p[3][3];
    int64_t vertices = (int64_t)edges * (rows + 1) * 2 * (kind == 3 ? 4 : 1) + count * 2;
    int64_t indices = (int64_t)edges * rows * 6 * (kind == 3 ? 4 : 1) + (int64_t)count * 18;
    if (vertices > INT_MAX / (int)sizeof(box_vertex_t) ||
        indices > INT_MAX / (int)sizeof(uint32_t))
    {
        ecs_err("surface mesh capacity exceeded");
        ecs_os_free(d.assets);
        return;
    }
    uint64_t total_size = (uint64_t)vertices * sizeof(box_vertex_t) +
        (uint64_t)indices * sizeof(uint32_t) + (uint64_t)d.asset_count * sizeof(vec4);
    if (total_size > INT_MAX) {
        ecs_err("surface mesh allocation exceeds capacity");
        ecs_os_free(d.assets);
        return;
    }
    mesh->vertices = ecs_os_malloc_n(box_vertex_t, vertices);
    mesh->indices = ecs_os_malloc_n(uint32_t, indices);
    if (!mesh->vertices || !mesh->indices) {
        ecs_err("surface mesh allocation failed");
        ecs_os_free(d.assets);
        box_mesh_fini(mesh);
        return;
    }
    mesh->vertex_capacity = (int32_t)vertices;
    mesh->index_capacity = (int32_t)indices;
    const vec4 *points = kind == 3 ? NULL : d.assets + (int)d.p[5][0] - 24;
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
            int base = mesh->vertex_count;
            for (int j = 0; j <= nrows; j ++) {
                for (int side = 0; side < 2; side ++) {
                    float u = side ? u1 : u0, v = (float)j / nrows;
                    vec3 position = {u, v, kind == 3 ? region : inner};
                    vec3 normal = {0};
                    if (kind == 7) {
                        int p = side ? next : i;
                        glm_vec3_copy((vec3){points[p][0], v - 0.5f, points[p][1]}, position);
                        glm_vec3_copy((vec3){points[next][1] - points[i][1], 0,
                            points[i][0] - points[next][0]}, normal);
                        glm_vec3_normalize(normal);
                    }
                    box_mesh_vertex(mesh, position, normal, u + (inner ? 2 : 0), v);
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
    mesh->surface_assets = d.assets;
    mesh->surface_asset_count = d.asset_count;
}

#define BOX_ASSET_SHAPE(NAME, KIND) \
static void box_##NAME##_key(const ecs_world_t *world, const void *value, box_shape_key_t *key) \
{ \
    box_asset_key(world, value, KIND, key); \
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
        .surface_world = box_profile_surface});
    ecs_entity_t assets[] = {ecs_id(Path2D), ecs_id(Path3D), ecs_id(Profile2D)};
    for (int i = 0; i < 3; i ++) {
        ecs_observer(world, {.query.terms = {{.id = assets[i]}},
            .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
            .events = {EcsOnSet, EcsOnRemove}, .callback = box_asset_changed});
    }
}
