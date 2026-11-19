/**
 * @file mesh_graph/nodes_curves.c
 * @brief Nodes of mesh graphs for curves and points: Tube, PointsOnCurves, PointsOnMesh,
 * Jitter and InstanceOnPoints.
 */

#include "mesh_graph.h"
#include <math.h>

/** A profile of a tube: points in the plane of the frame with their normals and u */
typedef struct box_mg_profile_t {
    int32_t count;               /**< Points */
    vec2 *points;                /**< Position of each point, x along the normal of the frame */
    vec2 *normals;               /**< Normal of each point */
    float *u;                    /**< u of each point, 0 to 1 around the profile */
    bool closed;                 /**< The last point repeats the first */
} box_mg_profile_t;

/** A point sampled on a curve */
typedef struct box_mg_sample_t {
    vec3 position;               /**< Position */
    vec3 tangent;                /**< Unit tangent */
    vec3 normal;                 /**< Unit normal, perpendicular to the tangent */
    float radius;                /**< Radius */
    int32_t point;               /**< First point of the segment of the sample */
    float fraction;              /**< Fraction along the segment */
} box_mg_sample_t;

/** Fix a scale of 0 to 1.
 *
 * @param in The scale
 * @param out Receives the scale without zeros
 */
static void box_mg_scale(
    const vec3 in,
    vec3 out)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = in[i] != 0.0f ? in[i] : 1.0f;
    }
}

/** Build the profile of a tube: the first curve of a Curves, or a circle.
 *
 * @param profile The Curves of the profile, NULL for a circle
 * @param sides Sides of the circle
 * @param out Receives the profile, freed by the caller
 */
static void box_mg_profile_build(
    const box_mg_value_t *profile,
    int32_t sides,
    box_mg_profile_t *out)
{
    const box_mg_curves_t *c = profile ? &profile->curves : NULL;
    bool custom = c && c->curve_count && c->offsets[1] - c->offsets[0] >= 2;
    int32_t i, count = custom ? c->offsets[1] - c->offsets[0] : sides + 1;

    out->count = count;
    out->points = ecs_os_calloc_n(vec2, count);
    out->normals = ecs_os_calloc_n(vec2, count);
    out->u = ecs_os_calloc_n(float, count);
    if (custom) {
        const vec3 *p = &c->positions[c->offsets[0]];
        for (i = 0; i < count; i ++) {
            out->points[i][0] = p[i][0];
            out->points[i][1] = p[i][1];
        }

        out->closed = glm_vec2_distance(out->points[0], out->points[count - 1]) < 1e-5f;
    } else {
        for (i = 0; i < count; i ++) {
            float a = 2.0f * GLM_PIf * (float)(i % sides) / (float)sides;
            box_mg_sincos(a, &out->points[i][1], &out->points[i][0]);
        }

        out->closed = true;
    }

    float length = 0.0f;
    for (i = 0; i < count; i ++) {
        int32_t a = i > 0 ? i - 1 : (out->closed ? count - 2 : 0);
        int32_t b = i < count - 1 ? i + 1 : (out->closed ? 1 : count - 1);
        vec2 d;
        glm_vec2_sub(out->points[b], out->points[a], d);
        out->normals[i][0] = d[1];
        out->normals[i][1] = -d[0];
        glm_vec2_normalize(out->normals[i]);
        length += i ? glm_vec2_distance(out->points[i - 1], out->points[i]) : 0.0f;
        out->u[i] = length;
    }

    for (i = 0; i < count; i ++) {
        out->u[i] = length > 0.0f ? out->u[i] / length : 0.0f;
    }
}

/** Free a profile.
 *
 * @param profile The profile
 */
static void box_mg_profile_fini(
    box_mg_profile_t *profile)
{
    ecs_os_free(profile->points);
    ecs_os_free(profile->normals);
    ecs_os_free(profile->u);
}

/** Add a cap to an end of a curve of a tube: a fan of the profile around the curve point.
 *
 * @param m The mesh
 * @param pr The profile
 * @param ring First vertex of the ring of the end
 * @param center Position of the curve point
 * @param tangent Tangent of the curve point
 * @param end Whether the cap is at the end, facing along the tangent
 * @return Number of vertices added
 */
static int32_t box_mg_tube_cap(
    box_mg_mesh_t *m,
    const box_mg_profile_t *pr,
    int32_t ring,
    const vec3 center,
    const vec3 tangent,
    bool end)
{
    int32_t base = m->vertex_count, i, count = pr->count - 1;
    int32_t index = m->index_count;
    vec3 n;
    glm_vec3_scale((float*)tangent, end ? 1.0f : -1.0f, n);
    box_mg_mesh_resize(m, base + 1 + count, index + count * 3);
    glm_vec3_copy((float*)center, m->positions[base]);
    glm_vec3_copy(n, m->normals[base]);
    m->uvs[base][0] = 0.5f;
    m->uvs[base][1] = 0.5f;
    for (i = 0; i < count; i ++) {
        glm_vec3_copy(m->positions[ring + i], m->positions[base + 1 + i]);
        glm_vec3_copy(n, m->normals[base + 1 + i]);
        m->uvs[base + 1 + i][0] = 0.5f + 0.5f * pr->points[i][0];
        m->uvs[base + 1 + i][1] = 0.5f - 0.5f * pr->points[i][1];
    }

    for (i = 0; i < count; i ++) {
        uint32_t a = (uint32_t)(base + 1 + i), b = (uint32_t)(base + 1 + (i + 1) % count);
        m->indices[index ++] = (uint32_t)base;
        m->indices[index ++] = end ? a : b;
        m->indices[index ++] = end ? b : a;
    }

    return count + 1;
}

/** Sweep the profile along one curve of a tube.
 *
 * @param t The MeshTube
 * @param c The curves
 * @param curve The curve
 * @param pr The profile
 * @param m The mesh the rings go to
 */
static void box_mg_tube_curve(
    const MeshTube *t,
    const box_mg_curves_t *c,
    int32_t curve,
    const box_mg_profile_t *pr,
    box_mg_mesh_t *m)
{
    int32_t first = c->offsets[curve], count = c->offsets[curve + 1] - first, i, j;
    int32_t ring = pr->count, base = m->vertex_count, index = m->index_count;
    float scale = t->radius > 0.0f ? t->radius : 1.0f;
    float v_scale = t->v_scale > 0.0f ? t->v_scale : 1.0f;
    float *lengths = ecs_os_malloc_n(float, count);
    box_mg_curve_lengths(c, curve, lengths);
    box_mg_mesh_resize(m, base + count * ring, index + (count - 1) * (ring - 1) * 6);
    for (j = 0; j < count; j ++) {
        const float *p = c->positions[first + j], *n = c->normals[first + j];
        float r = c->radii[first + j] * scale;
        vec3 b;
        glm_vec3_cross(c->tangents[first + j], (float*)n, b);
        for (i = 0; i < ring; i ++) {
            int32_t v = base + j * ring + i;
            const float *q = pr->points[i], *qn = pr->normals[i];
            for (int32_t k = 0; k < 3; k ++) {
                m->positions[v][k] = p[k] + r * (q[0] * n[k] + q[1] * b[k]);
                m->normals[v][k] = qn[0] * n[k] + qn[1] * b[k];
            }

            glm_vec3_normalize(m->normals[v]);
            m->uvs[v][0] = pr->u[i];
            m->uvs[v][1] = lengths[j] * v_scale;
        }
    }

    for (j = 0; j + 1 < count; j ++) {
        for (i = 0; i + 1 < ring; i ++) {
            uint32_t a = (uint32_t)(base + j * ring + i), b = a + 1;
            uint32_t d = a + (uint32_t)ring, e = d + 1;
            const uint32_t quad[6] = { a, b, d, b, e, d };
            ecs_os_memcpy_n(&m->indices[index], quad, uint32_t, 6);
            index += 6;
        }
    }

    if (t->caps && pr->closed && ring > 3) {
        box_mg_tube_cap(m, pr, base, c->positions[first], c->tangents[first], false);
        box_mg_tube_cap(m, pr, base + (count - 1) * ring, c->positions[first + count - 1],
            c->tangents[first + count - 1], true);
    }

    ecs_os_free(lengths);
}

/** Sweep a profile along every curve of a Curves.
 *
 * @param node The MeshTube
 * @param inputs The curves and the profile
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int box_mesh_tube_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshTube *t = node;
    const box_mg_curves_t *c = &inputs[0]->curves;
    int32_t sides = t->sides >= 3 ? t->sides : (t->sides > 0 ? 3 : 8), i;
    box_mg_profile_t pr = {0};
    (void)error;
    box_mg_profile_build(counts[1] ? inputs[1] : NULL, sides > 256 ? 256 : sides, &pr);
    for (i = 0; i < c->curve_count; i ++) {
        if (c->offsets[i + 1] - c->offsets[i] >= 2) {
            box_mg_tube_curve(t, c, i, &pr, &out->mesh);
        }
    }

    box_mg_profile_fini(&pr);

    return 0;
}

/** Sample a curve at a distance from its start.
 *
 * @param c The curves
 * @param first First point of the curve
 * @param count Points of the curve
 * @param lengths Length up to each point
 * @param distance The distance
 * @param out Receives the sample
 */
static void box_mg_curve_sample(
    const box_mg_curves_t *c,
    int32_t first,
    int32_t count,
    const float *lengths,
    float distance,
    box_mg_sample_t *out)
{
    int32_t lo = 0, hi = count - 1;
    while (hi - lo > 1) {
        int32_t mid = (lo + hi) / 2;
        if (lengths[mid] <= distance) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    float span = lengths[hi] - lengths[lo];
    float f = span > 0.0f ? glm_clamp((distance - lengths[lo]) / span, 0.0f, 1.0f) : 0.0f;
    int32_t a = first + lo, b = first + hi;
    glm_vec3_lerp(c->positions[a], c->positions[b], f, out->position);
    glm_vec3_lerp(c->tangents[a], c->tangents[b], f, out->tangent);
    if (glm_vec3_norm2(out->tangent) < 1e-12f) {
        glm_vec3_copy(c->tangents[a], out->tangent);
    }

    glm_vec3_normalize(out->tangent);
    vec3 n, along;
    glm_vec3_lerp(c->normals[a], c->normals[b], f, n);
    glm_vec3_scale(out->tangent, glm_vec3_dot(n, out->tangent), along);
    glm_vec3_sub(n, along, out->normal);
    if (glm_vec3_norm2(out->normal) < 1e-12f) {
        glm_vec3_copy(c->normals[a], out->normal);
    }

    glm_vec3_normalize(out->normal);
    out->radius = c->radii[a] + (c->radii[b] - c->radii[a]) * f;
    out->point = a;
    out->fraction = f;
}

/** Rotation of a frame that turns +Y to a tangent and +Z to a normal.
 *
 * @param tangent The unit tangent
 * @param normal The unit normal
 * @param out Receives the rotation
 */
static void box_mg_frame_rotation(
    const vec3 tangent,
    const vec3 normal,
    versor out)
{
    mat3 m;
    vec3 x;
    glm_vec3_cross((float*)tangent, (float*)normal, x);
    glm_vec3_copy(x, m[0]);
    glm_vec3_copy((float*)tangent, m[1]);
    glm_vec3_copy((float*)normal, m[2]);
    glm_mat3_quat(m, out);
    glm_quat_normalize(out);
}

/** Find the distances of the points on a curve.
 *
 * @param n The MeshPointsOnCurves
 * @param length Length of the curve
 * @param curve Index of the curve, the stream of its jitter
 * @param distances Receives the distances, NULL to count them
 * @return Number of points
 */
static int32_t box_mg_curve_distances(
    const MeshPointsOnCurves *n,
    float length,
    int32_t curve,
    float *distances)
{
    float start = glm_clamp(n->start, 0.0f, 1.0f) * length;
    float end = glm_clamp(n->end > 0.0f ? n->end : 1.0f, 0.0f, 1.0f) * length;
    int32_t i, count = n->count > 0 ? n->count : 8;
    float step;
    box_mg_rand_t rand;
    if (end < start) {
        float swap = start;
        start = end;
        end = swap;
    }

    if (n->spacing > 0.0f) {
        count = (int32_t)floorf((end - start) / n->spacing + 1e-4f) + 1;
        count = count > 1000000 ? 1000000 : count;
        step = n->spacing;
    } else {
        step = count > 1 ? (end - start) / (float)(count - 1) : 0.0f;
    }

    box_mg_rand_seed(&rand, n->seed, (uint32_t)curve);
    for (i = 0; distances && i < count; i ++) {
        float d = start + step * (float)i;
        if (n->jitter > 0.0f) {
            d += (box_mg_rand_float(&rand) - 0.5f) * n->jitter * step;
        }

        distances[i] = glm_clamp(d, start, end);
    }

    return count;
}

/** Write the attributes of a point placed on a curve.
 *
 * @param p The points
 * @param c The curves
 * @param k The point
 * @param s The sample of the point
 * @param values The values of t, distance, curve, radius and index
 */
static void box_mg_curve_point_attrs(
    box_mg_points_t *p,
    const box_mg_curves_t *c,
    int32_t k,
    const box_mg_sample_t *s,
    const float *values)
{
    static const char *names[] = { "t", "distance", "curve", "radius", "index" };
    int32_t a;
    for (a = 0; a < 5; a ++) {
        box_mg_attr_find(p->attrs, p->attr_count, names[a])[k] = values[a];
    }

    for (a = 0; a < c->attr_count; a ++) {
        float *dst = box_mg_attr_find(p->attrs, p->attr_count, c->attrs[a].name);
        const float *src = c->attrs[a].values;
        int32_t b = s->fraction > 0.0f ? s->point + 1 : s->point;
        if (dst) {
            dst[k] = src[s->point] + (src[b] - src[s->point]) * s->fraction;
        }
    }
}

/** Place points along every curve of a Curves.
 *
 * @param node The MeshPointsOnCurves
 * @param inputs The curves
 * @param counts Values per port
 * @param out The points
 * @param error Unused
 * @return 0
 */
static int box_mesh_points_on_curves_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshPointsOnCurves *n = node;
    const box_mg_curves_t *c = &inputs[0]->curves;
    box_mg_points_t *p = &out->points;
    int32_t i, j, total = 0, k = 0;
    (void)counts;
    (void)error;
    float *lengths = ecs_os_malloc_n(float, c->point_count + 1);
    for (i = 0; i < c->curve_count; i ++) {
        if (c->offsets[i + 1] - c->offsets[i] >= 2) {
            total += box_mg_curve_distances(n, box_mg_curve_lengths(c, i, lengths), i, NULL);
        }
    }

    box_mg_points_resize(p, total);
    box_mg_attr_add(p->attrs, &p->attr_count, "t", total);
    box_mg_attr_add(p->attrs, &p->attr_count, "distance", total);
    box_mg_attr_add(p->attrs, &p->attr_count, "curve", total);
    box_mg_attr_add(p->attrs, &p->attr_count, "radius", total);
    box_mg_attr_add(p->attrs, &p->attr_count, "index", total);
    for (i = 0; i < c->attr_count; i ++) {
        box_mg_attr_add(p->attrs, &p->attr_count, c->attrs[i].name, total);
    }

    for (i = 0; i < c->curve_count; i ++) {
        int32_t first = c->offsets[i], count = c->offsets[i + 1] - first;
        if (count < 2) {
            continue;
        }

        float length = box_mg_curve_lengths(c, i, lengths);
        int32_t points = box_mg_curve_distances(n, length, i, NULL);
        float *distances = ecs_os_malloc_n(float, points);
        box_mg_curve_distances(n, length, i, distances);
        for (j = 0; j < points; j ++, k ++) {
            box_mg_sample_t s;
            box_mg_curve_sample(c, first, count, lengths, distances[j], &s);
            glm_vec3_copy(s.position, p->positions[k]);
            box_mg_frame_rotation(s.tangent, s.normal, p->rotations[k]);
            const float values[5] = {
                length > 0.0f ? distances[j] / length : 0.0f, distances[j], (float)i,
                s.radius, (float)j
            };
            box_mg_curve_point_attrs(p, c, k, &s, values);
        }

        ecs_os_free(distances);
    }

    ecs_os_free(lengths);

    return 0;
}

/** Rotation that turns +Y to a unit normal along the shortest arc.
 *
 * @param normal The normal
 * @param out Receives the rotation
 */
static void box_mg_up_rotation(
    const vec3 normal,
    versor out)
{
    float d = normal[1];
    if (d < -0.99999f) {
        glm_quat_init(out, 1.0f, 0.0f, 0.0f, 0.0f);
    } else {
        glm_quat_init(out, normal[2], 0.0f, -normal[0], 1.0f + d);
        glm_quat_normalize(out);
    }
}

/** Pick a triangle by a fraction of the total area.
 *
 * @param areas Area of the triangles up to and with each
 * @param triangles Number of triangles
 * @param r The fraction times the total area
 * @return The triangle
 */
static int32_t box_mg_pick_triangle(
    const double *areas,
    int32_t triangles,
    double r)
{
    int32_t lo = 0, hi = triangles - 1;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (areas[mid] <= r) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

/** Check whether a point is farther than a distance from every point so far.
 *
 * @param p The points
 * @param count Points so far
 * @param position The point
 * @param spacing The distance
 * @return Whether it has room
 */
static bool box_mg_has_room(
    const box_mg_points_t *p,
    int32_t count,
    const vec3 position,
    float spacing)
{
    float limit = spacing * spacing;
    bool result = true;
    int32_t i;
    for (i = 0; i < count && result; i ++) {
        result = glm_vec3_distance2(p->positions[i], (float*)position) >= limit;
    }

    return result;
}

/** Sum the areas of the triangles of a mesh.
 *
 * @param m The mesh
 * @param areas Receives the area of the triangles up to and with each
 * @return The total area
 */
static double box_mg_areas(
    const box_mg_mesh_t *m,
    double *areas)
{
    int32_t i, triangles = m->index_count / 3;
    double total = 0.0;
    for (i = 0; i < triangles; i ++) {
        const uint32_t *t = &m->indices[i * 3];
        vec3 ab, ac, n;
        glm_vec3_sub(m->positions[t[1]], m->positions[t[0]], ab);
        glm_vec3_sub(m->positions[t[2]], m->positions[t[0]], ac);
        glm_vec3_cross(ab, ac, n);
        total += 0.5 * (double)glm_vec3_norm(n);
        areas[i] = total;
    }

    return total;
}

/** Place a point on a random spot of a random triangle, area weighted.
 *
 * @param m The mesh
 * @param areas Area of the triangles up to and with each
 * @param total The total area
 * @param rand The random sequence
 * @param position Receives the position
 * @param normal Receives the interpolated normal
 * @param uv Receives the interpolated uv
 */
static void box_mg_mesh_spot(
    const box_mg_mesh_t *m,
    const double *areas,
    double total,
    box_mg_rand_t *rand,
    vec3 position,
    vec3 normal,
    vec2 uv)
{
    int32_t tri = box_mg_pick_triangle(areas, m->index_count / 3,
        (double)box_mg_rand_float(rand) * total);
    float r1 = box_mg_rand_float(rand), r2 = box_mg_rand_float(rand), s = sqrtf(r1);
    float w[3] = { 1.0f - s, s * (1.0f - r2), s * r2 };
    const uint32_t *t = &m->indices[tri * 3];
    int32_t k;
    glm_vec3_zero(position);
    glm_vec3_zero(normal);
    glm_vec2_zero(uv);
    for (k = 0; k < 3; k ++) {
        glm_vec3_muladds(m->positions[t[k]], w[k], position);
        glm_vec3_muladds(m->normals[t[k]], w[k], normal);
        uv[0] += m->uvs[t[k]][0] * w[k];
        uv[1] += m->uvs[t[k]][1] * w[k];
    }

    if (glm_vec3_norm2(normal) < 1e-12f) {
        vec3 ab, ac;
        glm_vec3_sub(m->positions[t[1]], m->positions[t[0]], ab);
        glm_vec3_sub(m->positions[t[2]], m->positions[t[0]], ac);
        glm_vec3_cross(ab, ac, normal);
    }

    glm_vec3_normalize(normal);
}

/** Scatter points over the triangles of a mesh, area weighted.
 *
 * @param node The MeshPointsOnMesh
 * @param inputs The mesh
 * @param counts Values per port
 * @param out The points
 * @param error Unused
 * @return 0
 */
static int box_mesh_points_on_mesh_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshPointsOnMesh *n = node;
    const box_mg_mesh_t *m = &inputs[0]->mesh;
    box_mg_points_t *p = &out->points;
    int32_t want = n->count > 0 ? n->count : 32, made = 0, attempts;
    int32_t triangles = m->index_count / 3;
    double *areas = ecs_os_malloc_n(double, triangles + 1);
    double total = box_mg_areas(m, areas);
    box_mg_rand_t rand;
    (void)counts;
    (void)error;
    if (!triangles || total <= 0.0) {
        goto done;
    }

    box_mg_points_resize(p, want);
    float *u = box_mg_attr_add(p->attrs, &p->attr_count, "u", want);
    float *v = box_mg_attr_add(p->attrs, &p->attr_count, "v", want);
    box_mg_rand_seed(&rand, n->seed, 0);
    for (attempts = 0; made < want && attempts < want * 30; attempts ++) {
        vec3 position, normal;
        vec2 uv;
        box_mg_mesh_spot(m, areas, total, &rand, position, normal, uv);
        if (n->min_spacing > 0.0f && !box_mg_has_room(p, made, position, n->min_spacing)) {
            continue;
        }

        glm_vec3_copy(position, p->positions[made]);
        if (n->vertical) {
            glm_quat_identity(p->rotations[made]);
        } else {
            box_mg_up_rotation(normal, p->rotations[made]);
        }

        u[made] = uv[0];
        v[made] = uv[1];
        made ++;
    }

    box_mg_points_resize(p, made);
done:
    ecs_os_free(areas);

    return 0;
}

/** Turn and scale the points of a Points at random.
 *
 * @param node The MeshJitter
 * @param inputs The points
 * @param counts Values per port
 * @param out The points
 * @param error Unused
 * @return 0
 */
static int box_mesh_jitter_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshJitter *n = node;
    float lo = n->scale_min > 0.0f ? n->scale_min : 1.0f;
    float hi = n->scale_max > lo ? n->scale_max : lo;
    box_mg_rand_t rand;
    int32_t i;
    (void)counts;
    (void)error;
    box_mg_value_copy(out, inputs[0]);
    box_mg_points_t *p = &out->points;
    box_mg_rand_seed(&rand, n->seed, 0);
    for (i = 0; i < p->count; i ++) {
        float spin = (box_mg_rand_float(&rand) * 2.0f - 1.0f) * n->spin;
        float tilt = box_mg_rand_float(&rand) * n->tilt;
        float heading = box_mg_rand_float(&rand) * 2.0f * GLM_PIf;
        float scale = lo + (hi - lo) * box_mg_rand_float(&rand);
        float ss, sc, ts, tc, hs, hc;
        versor turn, lean, local, turned;
        box_mg_sincos(spin * 0.5f, &ss, &sc);
        box_mg_sincos(tilt * 0.5f, &ts, &tc);
        box_mg_sincos(heading, &hs, &hc);
        glm_quat_init(turn, 0.0f, ss, 0.0f, sc);
        glm_quat_init(lean, hc * ts, 0.0f, hs * ts, tc);
        glm_quat_mul(lean, turn, local);
        glm_quat_mul(p->rotations[i], local, turned);
        glm_quat_normalize_to(turned, p->rotations[i]);
        glm_vec3_scale(p->scales[i], scale, p->scales[i]);
    }

    return 0;
}

/** Copy a mesh to every point of a Points.
 *
 * @param node The MeshInstanceOnPoints
 * @param inputs The points and the mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int box_mesh_instance_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshInstanceOnPoints *n = node;
    const box_mg_points_t *p = &inputs[0]->points;
    const box_mg_mesh_t *src = &inputs[1]->mesh;
    box_mg_mesh_t *m = &out->mesh;
    int32_t vc = src->vertex_count, ic = src->index_count, k, i, a;
    (void)counts;
    (void)error;
    box_mg_mesh_resize(m, p->count * vc, p->count * ic);
    float *instance = box_mg_attr_add(m->attrs, &m->attr_count, "instance", m->vertex_count);
    for (a = 0; a < src->attr_count; a ++) {
        box_mg_attr_add(m->attrs, &m->attr_count, src->attrs[a].name, m->vertex_count);
    }

    for (k = 0; k < p->count; k ++) {
        mat3 r;
        vec3 s = { 1.0f, 1.0f, 1.0f };
        int32_t vbase = k * vc, ibase = k * ic;
        glm_mat3_identity(r);
        if (!n->ignore_rotation) {
            glm_quat_mat3(p->rotations[k], r);
        }

        if (!n->ignore_scale) {
            box_mg_scale(p->scales[k], s);
        }

        for (i = 0; i < vc; i ++) {
            vec3 v, nm = { src->normals[i][0] / s[0], src->normals[i][1] / s[1],
                src->normals[i][2] / s[2] };
            glm_vec3_mul(src->positions[i], s, v);
            glm_mat3_mulv(r, v, m->positions[vbase + i]);
            glm_vec3_add(m->positions[vbase + i], p->positions[k], m->positions[vbase + i]);
            glm_mat3_mulv(r, nm, m->normals[vbase + i]);
            glm_vec3_normalize(m->normals[vbase + i]);
            glm_vec2_copy(src->uvs[i], m->uvs[vbase + i]);
            instance[vbase + i] = (float)k;
            for (a = 0; a < src->attr_count; a ++) {
                box_mg_attr_find(m->attrs, m->attr_count, src->attrs[a].name)[vbase + i] =
                    src->attrs[a].values[i];
            }
        }

        bool flip = s[0] * s[1] * s[2] < 0.0f;
        for (i = 0; i < ic; i ++) {
            int32_t from = flip && i % 3 ? i + (i % 3 == 1 ? 1 : -1) : i;
            m->indices[ibase + i] = src->indices[from] + (uint32_t)vbase;
        }
    }

    return 0;
}

/** Construct MeshPointsOnCurves values with the documented defaults.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_points_on_curves_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshPointsOnCurves *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshPointsOnCurves){ .count = 8, .end = 1.0f };
    }
}

void box_mesh_nodes_curves_register(
    ecs_world_t *world)
{
    ecs_set_hooks(world, MeshPointsOnCurves, { .ctor = box_mesh_points_on_curves_ctor });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshTube),
        .output = MeshValueMesh,
        .inputs = {
            { "curves", offsetof(MeshTube, curves), MeshValueCurves },
            { "profile", offsetof(MeshTube, profile), MeshValueCurves, .optional = true }
        },
        .params = {
            { "sides", offsetof(MeshTube, sides), ECS_SIZEOF(int32_t) },
            { "radius", offsetof(MeshTube, radius), ECS_SIZEOF(float) },
            { "v_scale", offsetof(MeshTube, v_scale), ECS_SIZEOF(float) },
            { "caps", offsetof(MeshTube, caps), ECS_SIZEOF(bool) }
        },
        .eval = box_mesh_tube_eval
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshPointsOnCurves),
        .output = MeshValuePoints,
        .inputs = {{ "curves", offsetof(MeshPointsOnCurves, curves), MeshValueCurves }},
        .params = {
            { "count", offsetof(MeshPointsOnCurves, count), ECS_SIZEOF(int32_t) },
            { "spacing", offsetof(MeshPointsOnCurves, spacing), ECS_SIZEOF(float) },
            { "start", offsetof(MeshPointsOnCurves, start), ECS_SIZEOF(float) },
            { "end", offsetof(MeshPointsOnCurves, end), ECS_SIZEOF(float) },
            { "jitter", offsetof(MeshPointsOnCurves, jitter), ECS_SIZEOF(float) },
            { "seed", offsetof(MeshPointsOnCurves, seed), ECS_SIZEOF(int32_t) }
        },
        .eval = box_mesh_points_on_curves_eval,
        .flags = BOX_MESH_NODE_RANDOM
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshPointsOnMesh),
        .output = MeshValuePoints,
        .inputs = {{ "mesh", offsetof(MeshPointsOnMesh, mesh), MeshValueMesh }},
        .params = {
            { "count", offsetof(MeshPointsOnMesh, count), ECS_SIZEOF(int32_t) },
            { "min_spacing", offsetof(MeshPointsOnMesh, min_spacing), ECS_SIZEOF(float) },
            { "seed", offsetof(MeshPointsOnMesh, seed), ECS_SIZEOF(int32_t) },
            { "vertical", offsetof(MeshPointsOnMesh, vertical), ECS_SIZEOF(bool) }
        },
        .eval = box_mesh_points_on_mesh_eval,
        .flags = BOX_MESH_NODE_RANDOM
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshInstanceOnPoints),
        .output = MeshValueMesh,
        .inputs = {
            { "points", offsetof(MeshInstanceOnPoints, points), MeshValuePoints },
            { "instance", offsetof(MeshInstanceOnPoints, instance), MeshValueMesh }
        },
        .params = {
            { "ignore_rotation", offsetof(MeshInstanceOnPoints, ignore_rotation),
                ECS_SIZEOF(bool) },
            { "ignore_scale", offsetof(MeshInstanceOnPoints, ignore_scale), ECS_SIZEOF(bool) }
        },
        .eval = box_mesh_instance_eval
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshJitter),
        .output = MeshValuePoints,
        .inputs = {{ "points", offsetof(MeshJitter, points), MeshValuePoints }},
        .params = {
            { "spin", offsetof(MeshJitter, spin), ECS_SIZEOF(float) },
            { "tilt", offsetof(MeshJitter, tilt), ECS_SIZEOF(float) },
            { "scale_min", offsetof(MeshJitter, scale_min), ECS_SIZEOF(float) },
            { "scale_max", offsetof(MeshJitter, scale_max), ECS_SIZEOF(float) },
            { "seed", offsetof(MeshJitter, seed), ECS_SIZEOF(int32_t) }
        },
        .eval = box_mesh_jitter_eval,
        .flags = BOX_MESH_NODE_RANDOM
    });
}
