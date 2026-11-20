/**
 * @file mesh_graph/nodes_curves.c
 * @brief Nodes of mesh graphs for curves and points: Tube with the collars and bark data of
 * branching curves, PointsOnCurves, PointsOnMesh, Jitter and InstanceOnPoints.
 */

#include "mesh_graph.h"
#include <math.h>

/** A profile of a tube: points in the plane of the frame with their normals and u */
typedef struct box_mg_profile_t {
    int32_t count;               /**< Points */
    vec2 *points;                /**< Position of each point, x along the normal of the frame */
    vec2 *normals;               /**< Normal of each point */
    float *u;                    /**< u of each point, 0 to 1 around the profile */
    float inner;                 /**< Nearest distance of its edges to the axis, 1 for an open
                                  *   profile */
    bool closed;                 /**< The last point repeats the first */
} box_mg_profile_t;

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

    out->inner = 1.0f;
    for (i = 0; out->closed && i + 1 < count; i ++) {
        vec2 ab, q;
        glm_vec2_sub(out->points[i + 1], out->points[i], ab);
        float len2 = glm_vec2_dot(ab, ab);
        float f = len2 > 0.0f ? glm_clamp(-glm_vec2_dot(out->points[i], ab) / len2, 0.0f, 1.0f)
            : 0.0f;
        glm_vec2_lerp(out->points[i], out->points[i + 1], f, q);
        out->inner = glm_min(out->inner, glm_vec2_norm(q));
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

/** A tube being built: the node, the curves, the profile and the settings of its collars */
typedef struct box_mg_tube_t {
    const MeshTube *t;           /**< The node */
    const box_mg_curves_t *c;    /**< The curves */
    const box_mg_profile_t *pr;  /**< The profile */
    float scale;                 /**< Factor of the radii */
    float v_scale;               /**< v per unit of length */
    float collar;                /**< Length of a collar in base radii of its child, 0 for none */
    float flare;                 /**< Radius a collar adds, a fraction of the radius */
    const float *level;          /**< Level per point, NULL without bark data */
    const float *phase;          /**< Phase per point, NULL for none */
    const float *growth;         /**< Distance from the root per point, NULL for none */
} box_mg_tube_t;

/** A ring of a tube: its radius and the part of the parent it is in */
typedef struct box_mg_ring_t {
    float radius;                /**< Radius of the ring */
    float parent_radius;         /**< Radius of the parent near the ring, 0 without a collar */
    vec3 axis;                   /**< Nearest point of the axis of the parent */
    vec3 axis_tangent;           /**< Tangent of the axis of the parent there */
    float flute;                 /**< Weight of the flutes at the ring */
} box_mg_ring_t;

/** Check whether a curve has a parent with a segment.
 *
 * @param c The curves
 * @param curve The curve
 * @return The parent, -1 for none
 */
static int32_t box_mg_tube_parent(
    const box_mg_curves_t *c,
    int32_t curve)
{
    int32_t p = c->parents ? c->parents[curve] : -1;
    if (p < 0 || p >= c->curve_count || p == curve || c->offsets[p + 1] - c->offsets[p] < 2) {
        p = -1;
    }

    return p;
}

/** Find the nearest point of the axis of a curve to a point.
 *
 * @param c The curves
 * @param curve The curve, with at least two points
 * @param p The point
 * @param point Receives the nearest point of the axis
 * @param tangent Receives the direction of the axis there
 * @param radius Receives the radius of the curve there
 * @return The distance from the point to the axis
 */
static float box_mg_curve_nearest(
    const box_mg_curves_t *c,
    int32_t curve,
    const vec3 p,
    vec3 point,
    vec3 tangent,
    float *radius)
{
    int32_t first = c->offsets[curve], count = c->offsets[curve + 1] - first, i;
    float best = 1e30f;
    for (i = 0; i + 1 < count; i ++) {
        const float *a = c->positions[first + i], *b = c->positions[first + i + 1];
        vec3 ab, ap, q;
        glm_vec3_sub((float*)b, (float*)a, ab);
        glm_vec3_sub((float*)p, (float*)a, ap);
        float len2 = glm_vec3_dot(ab, ab);
        float f = len2 > 0.0f ? glm_clamp(glm_vec3_dot(ap, ab) / len2, 0.0f, 1.0f) : 0.0f;
        glm_vec3_lerp((float*)a, (float*)b, f, q);
        float d2 = glm_vec3_distance2(q, (float*)p);
        if (d2 < best) {
            best = d2;
            glm_vec3_copy(q, point);
            if (len2 > 0.0f) {
                glm_vec3_normalize_to(ab, tangent);
            } else {
                glm_vec3_copy(c->tangents[first + i], tangent);
            }

            *radius = c->radii[first + i] + (c->radii[first + i + 1] - c->radii[first + i]) * f;
        }
    }

    return sqrtf(best);
}

/** Rings a segment of a curve is split into where it is in the collar of its child */
#define BOX_MG_COLLAR_SPLIT (4)

/** Sample the rings of a curve: its points, and more in the segments of the collar of a child,
 * so the collar curves smoothly into the parent however long the segments are.
 *
 * @param tb The tube
 * @param curve The curve
 * @param lengths Length up to each point
 * @param samples Receives a sample per ring, room for (points - 1) * BOX_MG_COLLAR_SPLIT + 1
 * @param distances Receives the length up to each ring
 * @return The number of rings
 */
static int32_t box_mg_tube_samples(
    const box_mg_tube_t *tb,
    int32_t curve,
    const float *lengths,
    box_mg_sample_t *samples,
    float *distances)
{
    const box_mg_curves_t *c = tb->c;
    int32_t first = c->offsets[curve], count = c->offsets[curve + 1] - first, j, k;
    int32_t parent = box_mg_tube_parent(c, curve), split = 0, result = 0;
    if (parent >= 0 && tb->collar > 0.0f) {
        float base = c->radii[first] * tb->scale, reach = tb->collar * glm_max(base, 1e-4f);
        for (split = 0; split < count - 1; split ++) {
            vec3 axis, tangent;
            float rp;
            float d = box_mg_curve_nearest(c, parent, c->positions[first + split], axis,
                tangent, &rp);
            if (d - rp * tb->scale >= reach) {
                break;
            }
        }
    }

    for (j = 0; j < count; j ++) {
        int32_t parts = j < split ? BOX_MG_COLLAR_SPLIT : 1;
        for (k = 0; k < parts && (j < count - 1 || !k); k ++) {
            float d = lengths[j];
            if (k) {
                d += (lengths[j + 1] - lengths[j]) * (float)k / (float)parts;
            }

            box_mg_curve_sample(c, first, count, lengths, d, &samples[result]);
            distances[result ++] = d;
        }
    }

    return result;
}

/** Compute the rings of a curve: radii with the collar of a child, which stays inside its
 * parent where the parent hides it, and the weight of the flutes of a curve without parent.
 *
 * Inside means within the inner radius of the profile of the parent, which for a polygon is
 * nearer to the axis than the radius between its corners.
 *
 * @param tb The tube
 * @param curve The curve
 * @param samples The sample of each ring
 * @param distances Length up to each ring
 * @param count Number of rings
 * @param rings Receives the rings
 */
static void box_mg_tube_rings(
    const box_mg_tube_t *tb,
    int32_t curve,
    const box_mg_sample_t *samples,
    const float *distances,
    int32_t count,
    box_mg_ring_t *rings)
{
    const box_mg_curves_t *c = tb->c;
    const MeshTube *t = tb->t;
    int32_t first = c->offsets[curve], j;
    int32_t parent = box_mg_tube_parent(c, curve);
    float base = c->radii[first] * tb->scale;
    float height = t->flute_height > 0.0f ? t->flute_height : 3.0f * base;
    bool flutes = parent < 0 && t->flutes > 0 && t->flute_depth != 0.0f && height > 0.0f;
    for (j = 0; j < count; j ++) {
        box_mg_ring_t *ring = &rings[j];
        float raw = samples[j].radius * tb->scale;
        ecs_os_memset_t(ring, 0, box_mg_ring_t);
        ring->radius = raw;
        if (flutes) {
            float f = glm_max(0.0f, 1.0f - distances[j] / height);
            ring->flute = f * f;
        }

        if (parent < 0 || tb->collar <= 0.0f) {
            continue;
        }

        float rp;
        float d = box_mg_curve_nearest(c, parent, samples[j].position, ring->axis,
            ring->axis_tangent, &rp);
        rp *= tb->scale;
        float h = d - rp, reach = tb->collar * (base > 0.0f ? base : 1e-4f);
        float w = h <= 0.0f ? 1.0f : (h >= reach ? 0.0f : (1.0f - h / reach));
        w *= w;
        float inside = 0.97f * tb->pr->inner * rp;
        ring->radius = glm_min(raw * (1.0f + tb->flare * w),
            h <= 0.0f ? inside : glm_max(raw, inside));
        ring->parent_radius = rp;
    }
}

/** Compute a vertex of a ring of a tube: the profile point turned into the frame, with the
 * flutes and the normal blended into the parent near a collar.
 *
 * @param tb The tube
 * @param ring The ring
 * @param reach Length of the collar above the parent
 * @param p Position of the curve point
 * @param n Normal of the frame
 * @param b Binormal of the frame
 * @param i Point of the profile
 * @param position Receives the position
 * @param normal Receives the normal
 */
static void box_mg_tube_vertex(
    const box_mg_tube_t *tb,
    const box_mg_ring_t *ring,
    float reach,
    const vec3 p,
    const vec3 n,
    const vec3 b,
    int32_t i,
    vec3 position,
    vec3 normal)
{
    const box_mg_profile_t *pr = tb->pr;
    vec2 q = { pr->points[i][0], pr->points[i][1] };
    vec2 qn = { pr->normals[i][0], pr->normals[i][1] };
    int32_t k;
    if (ring->flute > 0.0f) {
        float s, co, count = (float)tb->t->flutes, depth = tb->t->flute_depth * ring->flute;
        box_mg_sincos(2.0f * GLM_PIf * pr->u[i] * count, &s, &co);
        float l = 0.5f + 0.5f * co, m = 1.0f + depth * l * l * l;
        float dm = depth * 3.0f * l * l * (-0.5f * count * s);
        vec2 bent = { m * qn[0] + dm * qn[1], m * qn[1] - dm * qn[0] };
        glm_vec2_scale(q, m, q);
        glm_vec2_normalize_to(bent, qn);
    }

    for (k = 0; k < 3; k ++) {
        position[k] = p[k] + ring->radius * (q[0] * n[k] + q[1] * b[k]);
        normal[k] = qn[0] * n[k] + qn[1] * b[k];
    }

    glm_vec3_normalize(normal);
    if (ring->parent_radius > 0.0f) {
        vec3 radial, along;
        glm_vec3_sub(position, (float*)ring->axis, radial);
        glm_vec3_scale((float*)ring->axis_tangent,
            glm_vec3_dot(radial, (float*)ring->axis_tangent), along);
        glm_vec3_sub(radial, along, radial);
        float h = glm_vec3_norm(radial) - ring->parent_radius;
        float w = h <= 0.0f ? 1.0f : glm_max(0.0f, 1.0f - h / reach);
        if (w > 0.0f && glm_vec3_norm2(radial) > 1e-20f) {
            glm_vec3_normalize(radial);
            glm_vec3_lerp(normal, radial, w * w, normal);
            glm_vec3_normalize(normal);
        }
    }
}

/** Find the stem a curve grows from by its parents.
 *
 * @param c The curves
 * @param curve The curve
 * @return The curve without a parent it descends from
 */
static int32_t box_mg_curve_root(
    const box_mg_curves_t *c,
    int32_t curve)
{
    int32_t result = curve, depth = 0, p;
    while (depth < 256 && (p = box_mg_tube_parent(c, result)) >= 0) {
        result = p;
        depth ++;
    }

    return result;
}

/** Write the bark data channels of the vertices of the tube of a curve.
 *
 * @param tb The tube
 * @param curve The curve
 * @param samples The sample of each ring
 * @param distances Length up to each ring
 * @param count Number of rings
 * @param m The mesh
 * @param base First vertex of the curve
 */
static void box_mg_tube_data(
    const box_mg_tube_t *tb,
    int32_t curve,
    const box_mg_sample_t *samples,
    const float *distances,
    int32_t count,
    box_mg_mesh_t *m,
    int32_t base)
{
    const box_mg_curves_t *c = tb->c;
    int32_t first = c->offsets[curve], k;
    int32_t ring = tb->pr->count, root = box_mg_curve_root(c, curve);
    float root_radius = c->radii[c->offsets[root]], total = distances[count - 1];
    float phase = 0.0f;
    float *data[8];
    if (tb->phase) {
        phase = tb->phase[first];
    } else {
        box_mg_rand_t rand;
        box_mg_rand_seed(&rand, 0, (uint32_t)curve);
        phase = box_mg_rand_float(&rand);
    }

    for (k = 0; k < 8; k ++) {
        char name[BOX_MESH_ATTR_NAME];
        ecs_os_snprintf(name, ECS_SIZEOF(name), "%s%d", BOX_MESH_DATA_ATTR, k);
        data[k] = box_mg_attr_find(m->attrs, m->attr_count, name);
    }

    for (k = base; k < m->vertex_count; k ++) {
        int32_t v = k - base, j = v / ring;
        j = v < count * ring ? j : (v < (count + 1) * ring ? 0 : count - 1);
        data[BOX_MESH_DATA_PIVOT][k] = c->positions[first][0];
        data[BOX_MESH_DATA_PIVOT + 1][k] = c->positions[first][1];
        data[BOX_MESH_DATA_PIVOT + 2][k] = c->positions[first][2];
        data[BOX_MESH_DATA_LEVEL][k] = tb->level[first];
        data[BOX_MESH_DATA_PHASE][k] = phase;
        data[BOX_MESH_DATA_STIFFNESS][k] = root_radius > 0.0f
            ? c->radii[first] / root_radius : 1.0f;
        const box_mg_sample_t *sj = &samples[j];
        float growth = distances[j];
        if (tb->growth) {
            const float *g = &tb->growth[sj->point];
            growth = g[0] + (g[1] - g[0]) * sj->fraction;
        }

        data[BOX_MESH_DATA_GROWTH][k] = growth;
        data[BOX_MESH_DATA_ALONG][k] = total > 0.0f ? distances[j] / total : 0.0f;
    }
}

/** Sweep the profile along one curve of a tube.
 *
 * @param tb The tube
 * @param curve The curve
 * @param m The mesh the rings go to
 */
static void box_mg_tube_curve(
    const box_mg_tube_t *tb,
    int32_t curve,
    box_mg_mesh_t *m)
{
    const box_mg_curves_t *c = tb->c;
    const box_mg_profile_t *pr = tb->pr;
    const MeshTube *t = tb->t;
    int32_t first = c->offsets[curve], points = c->offsets[curve + 1] - first, i, j;
    int32_t ring = pr->count, base = m->vertex_count, index = m->index_count;
    int32_t room = (points - 1) * BOX_MG_COLLAR_SPLIT + 1;
    float *lengths = ecs_os_malloc_n(float, points);
    float *distances = ecs_os_malloc_n(float, room);
    box_mg_sample_t *samples = ecs_os_malloc_n(box_mg_sample_t, room);
    box_mg_curve_lengths(c, curve, lengths);
    int32_t count = box_mg_tube_samples(tb, curve, lengths, samples, distances);
    box_mg_ring_t *rings = ecs_os_malloc_n(box_mg_ring_t, count);
    box_mg_tube_rings(tb, curve, samples, distances, count, rings);
    float root = c->radii[first] * tb->scale;
    float reach = tb->collar * (root > 0.0f ? root : 1e-4f);
    float tiles = 1.0f;
    if (t->square_uv) {
        tiles = glm_max(1.0f, floorf(2.0f * GLM_PIf * root * tb->v_scale + 0.5f));
    }

    box_mg_mesh_resize(m, base + count * ring, index + (count - 1) * (ring - 1) * 6);
    for (j = 0; j < count; j ++) {
        const box_mg_sample_t *sj = &samples[j];
        vec3 b;
        glm_vec3_cross((float*)sj->tangent, (float*)sj->normal, b);
        for (i = 0; i < ring; i ++) {
            int32_t v = base + j * ring + i;
            box_mg_tube_vertex(tb, &rings[j], reach, sj->position, sj->normal, b, i,
                m->positions[v], m->normals[v]);
            m->uvs[v][0] = pr->u[i] * tiles;
            m->uvs[v][1] = distances[j] * tb->v_scale;
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
        box_mg_tube_cap(m, pr, base, samples[0].position, samples[0].tangent, false);
        box_mg_tube_cap(m, pr, base + (count - 1) * ring, samples[count - 1].position,
            samples[count - 1].tangent, true);
    }

    if (tb->level) {
        box_mg_tube_data(tb, curve, samples, distances, count, m, base);
    }

    ecs_os_free(rings);
    ecs_os_free(samples);
    ecs_os_free(distances);
    ecs_os_free(lengths);
}

/** Copy curves keeping a fraction of the points of each curve, spread along it, the first and
 * the last always.
 *
 * @param in The curves
 * @param keep Fraction of the points kept, below 1
 * @param out The empty curves that receive the copy
 */
static void box_mg_curves_decimate(
    const box_mg_curves_t *in,
    float keep,
    box_mg_curves_t *out)
{
    int32_t i, j, a, points = 0;
    int32_t *kept = ecs_os_malloc_n(int32_t, in->curve_count + 1);
    for (i = 0; i < in->curve_count; i ++) {
        int32_t segments = in->offsets[i + 1] - in->offsets[i] - 1;
        int32_t n = (int32_t)((float)segments * keep + 0.5f);
        kept[i] = segments < 1 ? segments + 1 : (n < 1 ? 1 : n) + 1;
        points += kept[i];
    }

    for (a = 0; a < in->attr_count; a ++) {
        box_mg_attr_add(out->attrs, &out->attr_count, in->attrs[a].name, 0);
    }

    box_mg_curves_resize(out, in->curve_count, points);
    out->offsets[0] = 0;
    for (i = 0; i < in->curve_count; i ++) {
        int32_t first = in->offsets[i], segments = in->offsets[i + 1] - first - 1;
        int32_t to = out->offsets[i];
        out->offsets[i + 1] = to + kept[i];
        out->parents[i] = in->parents ? in->parents[i] : -1;
        for (j = 0; j < kept[i]; j ++) {
            int32_t k = kept[i] > 1 ? first + (j * segments + (kept[i] - 1) / 2) /
                (kept[i] - 1) : first;
            k = j == kept[i] - 1 ? first + (segments > 0 ? segments : 0) : k;
            glm_vec3_copy(in->positions[k], out->positions[to + j]);
            glm_vec3_copy(in->tangents[k], out->tangents[to + j]);
            glm_vec3_copy(in->normals[k], out->normals[to + j]);
            out->radii[to + j] = in->radii[k];
            for (a = 0; a < in->attr_count; a ++) {
                float *w = box_mg_attr_find(out->attrs, out->attr_count, in->attrs[a].name);
                if (w) {
                    w[to + j] = in->attrs[a].values[k];
                }
            }
        }
    }

    ecs_os_free(kept);
}

/** Reduce a MeshTube for a level of detail: lod_sides and lod_rings to the power of the level
 * of its sides and rings, and thin grown by lod_thin per level.
 *
 * @param node The snapshot of the MeshTube
 * @param level The level
 */
static void box_mesh_tube_lod(
    void *node,
    int32_t level)
{
    MeshTube *t = node;
    int32_t sides = t->sides >= 3 ? t->sides : (t->sides > 0 ? 3 : 8);
    int32_t reduced = (int32_t)((float)sides * box_mg_lod_factor(t->lod_sides, 0.6f, level) +
        0.5f);
    reduced = reduced < 3 ? 3 : reduced;
    if (reduced != sides) {
        t->sides = reduced;
    }

    float rings = box_mg_lod_factor(t->lod_rings, 0.6f, level);
    if (rings < 1.0f) {
        t->rings = (t->rings > 0.0f ? glm_min(t->rings, 1.0f) : 1.0f) * rings;
    }

    float thin = t->lod_thin > 0.0f ? t->lod_thin : (t->lod_thin < 0.0f ? 0.0f : 0.15f);
    if (thin > 0.0f) {
        t->thin = glm_max(t->thin, 0.0f) + thin * (float)level;
    }
}

/** Sweep a profile along every curve of a Curves. The stems of a fluted circle get four sides
 * or more per flute, a whole number per flute.
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
    box_mg_profile_t pr = {0}, fluted = {0}, slim = {0};
    box_mg_value_t *decimated = NULL;
    if (t->rings > 0.0f && t->rings < 1.0f) {
        decimated = box_mg_value_new(MeshValueCurves);
        box_mg_curves_decimate(c, t->rings, &decimated->curves);
        c = &decimated->curves;
    }

    box_mg_tube_t tb = {
        .t = t, .c = c, .pr = &pr,
        .scale = t->radius > 0.0f ? t->radius : 1.0f,
        .v_scale = t->v_scale > 0.0f ? t->v_scale : 1.0f,
        .collar = t->collar > 0.0f ? t->collar : (t->collar < 0.0f ? 0.0f : 2.0f),
        .flare = t->flare > 0.0f ? t->flare : (t->flare < 0.0f ? 0.0f : 0.5f),
        .level = box_mg_attr_find(c->attrs, c->attr_count, "level"),
        .phase = box_mg_attr_find(c->attrs, c->attr_count, "phase"),
        .growth = box_mg_attr_find(c->attrs, c->attr_count, "root_distance")
    };
    (void)error;
    sides = sides > 256 ? 256 : sides;
    const box_mg_curves_t *pc = counts[1] ? &inputs[1]->curves : NULL;
    bool circle = !pc || !pc->curve_count || pc->offsets[1] - pc->offsets[0] < 2;
    box_mg_profile_build(circle ? NULL : inputs[1], sides, &pr);
    box_mg_tube_t root = tb;
    if (t->flutes > 0 && t->flute_depth != 0.0f && circle) {
        int32_t flutes = t->flutes < 64 ? t->flutes : 64;
        int32_t per = (glm_max(sides, 4 * flutes) + flutes - 1) / flutes;
        box_mg_profile_build(NULL, per * flutes, &fluted);
        root.pr = &fluted;
    }

    box_mg_tube_t thin = tb;
    float thin_radius = 0.0f;
    if (t->thin > 0.0f && circle && sides > 3) {
        float largest = 0.0f;
        for (i = 0; i < c->curve_count; i ++) {
            if (c->offsets[i + 1] > c->offsets[i]) {
                largest = glm_max(largest, c->radii[c->offsets[i]]);
            }
        }

        thin_radius = t->thin * largest;
        box_mg_profile_build(NULL, sides / 2 > 3 ? sides / 2 : 3, &slim);
        thin.pr = &slim;
    }

    const float *dropped = box_mg_attr_find(c->attrs, c->attr_count, "dropped");
    for (i = 0; tb.level && i < 8; i ++) {
        char name[BOX_MESH_ATTR_NAME];
        ecs_os_snprintf(name, ECS_SIZEOF(name), "%s%d", BOX_MESH_DATA_ATTR, i);
        box_mg_attr_add(out->mesh.attrs, &out->mesh.attr_count, name, 0);
    }

    for (i = 0; i < c->curve_count; i ++) {
        int32_t first = c->offsets[i];
        if (c->offsets[i + 1] - first < 2 || (dropped && dropped[first] > 0.5f)) {
            continue;
        }

        const box_mg_tube_t *with = &tb;
        if (box_mg_tube_parent(c, i) < 0) {
            with = &root;
        } else if (thin.pr == &slim && c->radii[first] < thin_radius) {
            with = &thin;
        }

        box_mg_tube_curve(with, i, &out->mesh);
    }

    box_mg_profile_fini(&pr);
    if (root.pr == &fluted) {
        box_mg_profile_fini(&fluted);
    }

    if (thin.pr == &slim) {
        box_mg_profile_fini(&slim);
    }

    box_mg_value_release(decimated);

    return 0;
}

void box_mg_curve_sample(
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

/** Reduce a MeshPointsOnMesh for a level of detail: lod_keep to the power of the level of its
 * count, at least one point kept.
 *
 * @param node The snapshot of the MeshPointsOnMesh
 * @param level The level
 */
static void box_mesh_points_on_mesh_lod(
    void *node,
    int32_t level)
{
    MeshPointsOnMesh *n = node;
    int32_t count = n->count > 0 ? n->count : 32;
    int32_t reduced = (int32_t)((float)count * box_mg_lod_factor(n->lod_keep, 0.5f, level) +
        0.5f);
    reduced = reduced < 1 ? 1 : reduced;
    if (reduced != count) {
        n->count = reduced;
    }
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
            { "caps", offsetof(MeshTube, caps), ECS_SIZEOF(bool) },
            { "collar", offsetof(MeshTube, collar), ECS_SIZEOF(float) },
            { "flare", offsetof(MeshTube, flare), ECS_SIZEOF(float) },
            { "square_uv", offsetof(MeshTube, square_uv), ECS_SIZEOF(bool) },
            { "flutes", offsetof(MeshTube, flutes), ECS_SIZEOF(int32_t) },
            { "flute_depth", offsetof(MeshTube, flute_depth), ECS_SIZEOF(float) },
            { "flute_height", offsetof(MeshTube, flute_height), ECS_SIZEOF(float) },
            { "rings", offsetof(MeshTube, rings), ECS_SIZEOF(float) },
            { "thin", offsetof(MeshTube, thin), ECS_SIZEOF(float) },
            { "lod_sides", offsetof(MeshTube, lod_sides), ECS_SIZEOF(float) },
            { "lod_rings", offsetof(MeshTube, lod_rings), ECS_SIZEOF(float) },
            { "lod_thin", offsetof(MeshTube, lod_thin), ECS_SIZEOF(float) }
        },
        .eval = box_mesh_tube_eval,
        .lod = box_mesh_tube_lod
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
            { "vertical", offsetof(MeshPointsOnMesh, vertical), ECS_SIZEOF(bool) },
            { "lod_keep", offsetof(MeshPointsOnMesh, lod_keep), ECS_SIZEOF(float) }
        },
        .eval = box_mesh_points_on_mesh_eval,
        .lod = box_mesh_points_on_mesh_lod,
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
