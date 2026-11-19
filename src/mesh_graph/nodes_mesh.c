/**
 * @file mesh_graph/nodes_mesh.c
 * @brief Nodes of mesh graphs that change values: Transform, Merge, Displace, Normals, Cut and
 * Strata.
 */

#include "mesh_graph.h"
#include <stdlib.h>
#include <math.h>

/** A position of a mesh with the vertex it is of, sorted to weld equal positions */
typedef struct box_mg_weld_t {
    vec3 position;               /**< The position */
    int32_t vertex;              /**< The vertex */
} box_mg_weld_t;

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

/** Turn and normalize a normal through a rotation and the inverse of a scale.
 *
 * @param r The rotation
 * @param s The scale
 * @param n The normal
 * @param out Receives the normal
 */
static void box_mg_normal(
    mat3 r,
    const vec3 s,
    const vec3 n,
    vec3 out)
{
    vec3 m = { n[0] / s[0], n[1] / s[1], n[2] / s[2] };
    glm_mat3_mulv(r, m, out);
    glm_vec3_normalize(out);
}

/** Reverse the winding of the triangles of a mesh from an index on.
 *
 * @param m The mesh
 * @param from First index
 */
static void box_mg_flip(
    box_mg_mesh_t *m,
    int32_t from)
{
    int32_t i;
    for (i = from; i + 2 < m->index_count; i += 3) {
        uint32_t t = m->indices[i + 1];
        m->indices[i + 1] = m->indices[i + 2];
        m->indices[i + 2] = t;
    }
}

/** Move, turn and scale a value.
 *
 * @param node The MeshTransform
 * @param inputs The input value
 * @param counts Values per port
 * @param out The value
 * @param error Unused
 * @return 0
 */
static int box_mesh_transform_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshTransform *t = node;
    mat3 r;
    vec3 s, rot = { t->rotation[0], t->rotation[1], t->rotation[2] };
    int32_t i;
    (void)counts;
    (void)error;
    box_mg_scale(t->scale, s);
    box_mg_euler(rot, r);
    box_mg_value_copy(out, inputs[0]);
    box_mg_mesh_t *m = &out->mesh;
    box_mg_curves_t *c = &out->curves;
    box_mg_points_t *p = &out->points;
    for (i = 0; i < m->vertex_count; i ++) {
        vec3 v;
        glm_vec3_mul(m->positions[i], s, v);
        glm_mat3_mulv(r, v, m->positions[i]);
        glm_vec3_add(m->positions[i], (float*)t->position, m->positions[i]);
        box_mg_normal(r, s, m->normals[i], m->normals[i]);
    }

    if (s[0] * s[1] * s[2] < 0.0f) {
        box_mg_flip(m, 0);
    }

    float mean = (fabsf(s[0]) + fabsf(s[1]) + fabsf(s[2])) / 3.0f;
    for (i = 0; i < c->point_count; i ++) {
        vec3 v;
        glm_vec3_mul(c->positions[i], s, v);
        glm_mat3_mulv(r, v, c->positions[i]);
        glm_vec3_add(c->positions[i], (float*)t->position, c->positions[i]);
        glm_vec3_mul(c->tangents[i], s, v);
        glm_mat3_mulv(r, v, c->tangents[i]);
        glm_vec3_normalize(c->tangents[i]);
        box_mg_normal(r, s, c->normals[i], v);
        glm_vec3_scale(c->tangents[i], glm_vec3_dot(v, c->tangents[i]), c->normals[i]);
        glm_vec3_sub(v, c->normals[i], c->normals[i]);
        glm_vec3_normalize(c->normals[i]);
        c->radii[i] *= mean;
    }

    versor q;
    glm_mat3_quat(r, q);
    for (i = 0; i < p->count; i ++) {
        vec3 v;
        versor turned;
        glm_vec3_mul(p->positions[i], s, v);
        glm_mat3_mulv(r, v, p->positions[i]);
        glm_vec3_add(p->positions[i], (float*)t->position, p->positions[i]);
        glm_quat_mul(q, p->rotations[i], turned);
        glm_quat_copy(turned, p->rotations[i]);
        glm_vec3_mul(p->scales[i], s, p->scales[i]);
    }

    return 0;
}

/** Join the attributes of one input into the attributes of a joined value.
 *
 * @param attrs The attributes of the joined value
 * @param count Number of those attributes
 * @param total Elements of the joined value
 * @param src The attributes of the input
 * @param src_count Number of those attributes
 * @param base First element of the input in the joined value
 * @param elements Elements of the input
 */
static void box_mg_merge_attrs(
    box_mg_attr_t *attrs,
    int32_t *count,
    int32_t total,
    const box_mg_attr_t *src,
    int32_t src_count,
    int32_t base,
    int32_t elements)
{
    int32_t i;
    for (i = 0; i < src_count; i ++) {
        float *values = box_mg_attr_add(attrs, count, src[i].name, total);
        if (values && elements) {
            ecs_os_memcpy_n(values + base, src[i].values, float, elements);
        }
    }
}

/** Join meshes.
 *
 * @param inputs The meshes
 * @param count Number of meshes
 * @param m The joined mesh
 */
static void box_mg_merge_meshes(
    const box_mg_value_t *const *inputs,
    int32_t count,
    box_mg_mesh_t *m)
{
    int32_t i, k, vertices = 0, indices = 0, vbase = 0, ibase = 0;
    for (i = 0; i < count; i ++) {
        vertices += inputs[i]->mesh.vertex_count;
        indices += inputs[i]->mesh.index_count;
    }

    box_mg_mesh_resize(m, vertices, indices);
    for (i = 0; i < count; i ++) {
        const box_mg_mesh_t *s = &inputs[i]->mesh;
        if (s->vertex_count) {
            ecs_os_memcpy_n(m->positions + vbase, s->positions, vec3, s->vertex_count);
            ecs_os_memcpy_n(m->normals + vbase, s->normals, vec3, s->vertex_count);
            ecs_os_memcpy_n(m->uvs + vbase, s->uvs, vec2, s->vertex_count);
        }

        for (k = 0; k < s->index_count; k ++) {
            m->indices[ibase + k] = s->indices[k] + (uint32_t)vbase;
        }

        box_mg_merge_attrs(m->attrs, &m->attr_count, vertices, s->attrs, s->attr_count, vbase,
            s->vertex_count);
        vbase += s->vertex_count;
        ibase += s->index_count;
    }
}

/** Join curves, the parents of each input offset by the curves before it.
 *
 * @param inputs The curves
 * @param count Number of inputs
 * @param c The joined curves
 */
static void box_mg_merge_curves(
    const box_mg_value_t *const *inputs,
    int32_t count,
    box_mg_curves_t *c)
{
    int32_t i, k, curves = 0, points = 0, cbase = 0, pbase = 0;
    for (i = 0; i < count; i ++) {
        curves += inputs[i]->curves.curve_count;
        points += inputs[i]->curves.point_count;
    }

    box_mg_curves_resize(c, curves, points);
    for (i = 0; i < count; i ++) {
        const box_mg_curves_t *s = &inputs[i]->curves;
        for (k = 0; k < s->curve_count; k ++) {
            c->offsets[cbase + k] = s->offsets[k] + pbase;
            c->parents[cbase + k] = s->parents[k] >= 0 ? s->parents[k] + cbase : -1;
        }

        if (s->point_count) {
            ecs_os_memcpy_n(c->positions + pbase, s->positions, vec3, s->point_count);
            ecs_os_memcpy_n(c->radii + pbase, s->radii, float, s->point_count);
            ecs_os_memcpy_n(c->tangents + pbase, s->tangents, vec3, s->point_count);
            ecs_os_memcpy_n(c->normals + pbase, s->normals, vec3, s->point_count);
        }

        box_mg_merge_attrs(c->attrs, &c->attr_count, points, s->attrs, s->attr_count, pbase,
            s->point_count);
        cbase += s->curve_count;
        pbase += s->point_count;
    }

    c->offsets[curves] = points;
}

/** Join points.
 *
 * @param inputs The points
 * @param count Number of inputs
 * @param p The joined points
 */
static void box_mg_merge_points(
    const box_mg_value_t *const *inputs,
    int32_t count,
    box_mg_points_t *p)
{
    int32_t i, total = 0, base = 0;
    for (i = 0; i < count; i ++) {
        total += inputs[i]->points.count;
    }

    box_mg_points_resize(p, total);
    for (i = 0; i < count; i ++) {
        const box_mg_points_t *s = &inputs[i]->points;
        if (s->count) {
            ecs_os_memcpy_n(p->positions + base, s->positions, vec3, s->count);
            ecs_os_memcpy_n(p->rotations + base, s->rotations, versor, s->count);
            ecs_os_memcpy_n(p->scales + base, s->scales, vec3, s->count);
        }

        box_mg_merge_attrs(p->attrs, &p->attr_count, total, s->attrs, s->attr_count, base,
            s->count);
        base += s->count;
    }
}

/** Join the values of the inputs, which have one type.
 *
 * @param node The MeshMerge
 * @param inputs The values
 * @param counts Values per port
 * @param out The value
 * @param error Unused
 * @return 0
 */
static int box_mesh_merge_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    (void)node;
    (void)error;
    if (out->type == MeshValueMesh) {
        box_mg_merge_meshes(inputs, counts[0], &out->mesh);
    } else if (out->type == MeshValueCurves) {
        box_mg_merge_curves(inputs, counts[0], &out->curves);
    } else {
        box_mg_merge_points(inputs, counts[0], &out->points);
    }

    return 0;
}

/** Move the vertices of a mesh along their normals by noise.
 *
 * @param node The MeshDisplace
 * @param inputs The mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int box_mesh_displace_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshDisplace *d = node;
    float frequency = d->frequency > 0.0f ? d->frequency : 1.0f;
    int32_t octaves = d->octaves < 1 ? 1 : (d->octaves > 8 ? 8 : d->octaves), i;
    (void)counts;
    (void)error;
    box_mg_value_copy(out, inputs[0]);
    box_mg_mesh_t *m = &out->mesh;
    for (i = 0; i < m->vertex_count; i ++) {
        vec3 q, offset;
        glm_vec3_scale(m->positions[i], frequency, q);
        float n = box_mg_noise(q, octaves, d->seed);
        glm_vec3_scale(m->normals[i], n * d->amount, offset);
        glm_vec3_add(m->positions[i], offset, m->positions[i]);
    }

    return 0;
}

/** Unit direction of a vector, +Y for a zero vector.
 *
 * @param in The vector
 * @param out Receives the direction
 */
static void box_mg_direction(
    const vec3 in,
    vec3 out)
{
    float length = glm_vec3_norm((float*)in);
    if (length > 0.0f) {
        glm_vec3_scale((float*)in, 1.0f / length, out);
    } else {
        glm_vec3_copy((vec3){ 0.0f, 1.0f, 0.0f }, out);
    }
}

/** Move the vertices of a mesh in front of a plane onto it.
 *
 * @param node The MeshCut
 * @param inputs The mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int box_mesh_cut_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshCut *c = node;
    float frequency = c->frequency > 0.0f ? c->frequency : 1.0f;
    vec3 normal;
    int32_t i;
    (void)counts;
    (void)error;
    box_mg_direction(c->normal, normal);
    float plane = glm_vec3_dot(normal, (float*)c->position);
    box_mg_value_copy(out, inputs[0]);
    box_mg_mesh_t *m = &out->mesh;
    for (i = 0; i < m->vertex_count; i ++) {
        float d = glm_vec3_dot(normal, m->positions[i]) - plane;
        if (d <= 0.0f) {
            continue;
        }

        float depth = 0.0f;
        if (c->relief > 0.0f) {
            vec3 q;
            glm_vec3_scale(m->positions[i], frequency, q);
            depth = (box_mg_noise(q, 3, c->seed) * 0.5f + 0.5f) * c->relief;
            if (depth > d) {
                depth = d;
            }
        }

        glm_vec3_muladds(normal, -(d + depth), m->positions[i]);
    }

    return 0;
}

/** Step of the terraces of strata: flat at 0 and 1, steep at 0.5.
 *
 * @param f Fraction of a layer, 0..1
 * @param sharpness 0 for f, 1 for the steepest step
 * @return The stepped fraction
 */
static float box_mg_strata_step(
    float f,
    float sharpness)
{
    float x = f < 0.5f ? 2.0f * f : 2.0f - 2.0f * f;
    float x4 = x * x * x * x;
    float s = f < 0.5f ? 0.5f * x4 : 1.0f - 0.5f * x4;

    return f + (s - f) * sharpness;
}

/** Inset of a layer of strata from the seed of the node.
 *
 * @param seed The seed
 * @param layer The layer
 * @param inset The largest inset
 * @return The inset of the layer
 */
static float box_mg_strata_inset(
    int32_t seed,
    int32_t layer,
    float inset)
{
    box_mg_rand_t rand;
    box_mg_rand_seed(&rand, seed, (uint32_t)layer + 1u);

    return box_mg_rand_float(&rand) * inset;
}

/** Layer a mesh into strata along an axis.
 *
 * @param node The MeshStrata
 * @param inputs The mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int box_mesh_strata_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshStrata *n = node;
    float thickness = n->thickness > 0.0f ? n->thickness : 0.2f;
    float sharpness = n->sharpness < 0.0f ? 0.0f : (n->sharpness > 1.0f ? 1.0f : n->sharpness);
    vec3 axis;
    box_mg_rand_t rand;
    int32_t i;
    (void)counts;
    (void)error;
    box_mg_direction(n->axis, axis);
    box_mg_rand_seed(&rand, n->seed, 0);
    float phase = box_mg_rand_float(&rand);
    box_mg_value_copy(out, inputs[0]);
    box_mg_mesh_t *m = &out->mesh;
    for (i = 0; i < m->vertex_count; i ++) {
        float h = glm_vec3_dot(axis, m->positions[i]);
        float t = h / thickness + phase;
        float layer = floorf(t);
        float step = box_mg_strata_step(t - layer, sharpness);
        int32_t k = (int32_t)layer;
        float a = box_mg_strata_inset(n->seed, k, n->inset);
        float b = box_mg_strata_inset(n->seed, k + 1, n->inset);
        float scale = 1.0f - (a + (b - a) * step);
        vec3 across;
        glm_vec3_scale(axis, h, across);
        glm_vec3_sub(m->positions[i], across, across);
        glm_vec3_scale(across, scale, m->positions[i]);
        glm_vec3_muladds(axis, (layer + step - phase) * thickness, m->positions[i]);
    }

    return 0;
}

void box_mg_mesh_facet(
    box_mg_mesh_t *mesh)
{
    box_mg_mesh_t flat = {0};
    int32_t i, k, a;
    box_mg_mesh_resize(&flat, mesh->index_count, mesh->index_count);
    for (a = 0; a < mesh->attr_count; a ++) {
        box_mg_attr_add(flat.attrs, &flat.attr_count, mesh->attrs[a].name, mesh->index_count);
    }

    for (i = 0; i + 2 < mesh->index_count; i += 3) {
        vec3 ab, ac, n;
        const uint32_t *t = &mesh->indices[i];
        glm_vec3_sub(mesh->positions[t[1]], mesh->positions[t[0]], ab);
        glm_vec3_sub(mesh->positions[t[2]], mesh->positions[t[0]], ac);
        glm_vec3_cross(ab, ac, n);
        glm_vec3_normalize(n);
        for (k = 0; k < 3; k ++) {
            glm_vec3_copy(mesh->positions[t[k]], flat.positions[i + k]);
            glm_vec3_copy(n, flat.normals[i + k]);
            glm_vec2_copy(mesh->uvs[t[k]], flat.uvs[i + k]);
            flat.indices[i + k] = (uint32_t)(i + k);
            for (a = 0; a < flat.attr_count; a ++) {
                flat.attrs[a].values[i + k] = mesh->attrs[a].values[t[k]];
            }
        }
    }

    box_mg_value_t old = { .type = MeshValueMesh, .mesh = *mesh };
    box_mg_value_clear(&old);
    *mesh = flat;
}

/** Order positions lexicographically, then by vertex.
 *
 * @param a The first box_mg_weld_t
 * @param b The second box_mg_weld_t
 * @return Below, at or above 0 as a sorts before, with or after b
 */
static int box_mg_weld_compare(
    const void *a,
    const void *b)
{
    const box_mg_weld_t *wa = a, *wb = b;
    int32_t i, result = 0;
    for (i = 0; i < 3 && !result; i ++) {
        result = (wa->position[i] > wb->position[i]) - (wa->position[i] < wb->position[i]);
    }

    if (!result) {
        result = (wa->vertex > wb->vertex) - (wa->vertex < wb->vertex);
    }

    return result;
}

/** Number every vertex of a mesh by its position, equal positions sharing a number.
 *
 * @param m The mesh
 * @param ids Receives the number of each vertex
 * @return Number of distinct positions
 */
static int32_t box_mg_weld(
    const box_mg_mesh_t *m,
    int32_t *ids)
{
    box_mg_weld_t *w = ecs_os_malloc_n(box_mg_weld_t, m->vertex_count + 1);
    int32_t i, result = 0;
    for (i = 0; i < m->vertex_count; i ++) {
        glm_vec3_copy(m->positions[i], w[i].position);
        w[i].vertex = i;
    }

    qsort(w, (size_t)m->vertex_count, sizeof(box_mg_weld_t), box_mg_weld_compare);
    for (i = 0; i < m->vertex_count; i ++) {
        if (i && glm_vec3_eqv(w[i].position, w[i - 1].position)) {
            ids[w[i].vertex] = result - 1;
        } else {
            ids[w[i].vertex] = result ++;
        }
    }

    ecs_os_free(w);

    return result;
}

/** Group the corners of a mesh by a key of each corner.
 *
 * @param keys Key of each corner, 0 to groups - 1
 * @param corners Number of corners
 * @param groups Number of keys
 * @param offsets Receives the first entry of each group, groups + 1 entries
 * @param entries Receives the corners of each group in order
 */
static void box_mg_group(
    const int32_t *keys,
    int32_t corners,
    int32_t groups,
    int32_t *offsets,
    int32_t *entries)
{
    int32_t i;
    ecs_os_memset_n(offsets, 0, int32_t, groups + 1);
    for (i = 0; i < corners; i ++) {
        offsets[keys[i] + 1] ++;
    }

    for (i = 0; i < groups; i ++) {
        offsets[i + 1] += offsets[i];
    }

    int32_t *fill = ecs_os_memdup_n(offsets, int32_t, groups + 1);
    for (i = 0; i < corners; i ++) {
        entries[fill[keys[i]] ++] = i;
    }

    ecs_os_free(fill);
}

/** Compute the smooth normal of every corner of a mesh: the area weighted face normals of the
 * corners at its position whose faces are within an angle of its face.
 *
 * @param m The mesh
 * @param angle The angle
 * @param normals Receives the normal of each corner
 */
static void box_mg_corner_normals(
    const box_mg_mesh_t *m,
    float angle,
    vec3 *normals)
{
    int32_t corners = m->index_count, triangles = corners / 3, i, j;
    int32_t *ids = ecs_os_malloc_n(int32_t, m->vertex_count + 1);
    int32_t positions = box_mg_weld(m, ids);
    int32_t *keys = ecs_os_malloc_n(int32_t, corners + 1);
    int32_t *offsets = ecs_os_malloc_n(int32_t, positions + 1);
    int32_t *entries = ecs_os_malloc_n(int32_t, corners + 1);
    vec3 *faces = ecs_os_malloc_n(vec3, triangles + 1);
    vec3 *units = ecs_os_malloc_n(vec3, triangles + 1);
    float sine, cosine;
    box_mg_sincos(angle, &sine, &cosine);
    float limit = cosine - 1e-4f;
    for (i = 0; i < triangles; i ++) {
        vec3 ab, ac;
        const uint32_t *t = &m->indices[i * 3];
        glm_vec3_sub(m->positions[t[1]], m->positions[t[0]], ab);
        glm_vec3_sub(m->positions[t[2]], m->positions[t[0]], ac);
        glm_vec3_cross(ab, ac, faces[i]);
        glm_vec3_normalize_to(faces[i], units[i]);
    }

    for (i = 0; i < corners; i ++) {
        keys[i] = ids[m->indices[i]];
    }

    box_mg_group(keys, corners, positions, offsets, entries);
    for (i = 0; i < corners; i ++) {
        int32_t g = keys[i], face = i / 3;
        glm_vec3_zero(normals[i]);
        for (j = offsets[g]; j < offsets[g + 1]; j ++) {
            int32_t other = entries[j] / 3;
            if (glm_vec3_dot(units[face], units[other]) >= limit) {
                glm_vec3_add(normals[i], faces[other], normals[i]);
            }
        }

        if (glm_vec3_norm2(normals[i]) < 1e-30f) {
            glm_vec3_copy(units[face], normals[i]);
        }

        glm_vec3_normalize(normals[i]);
    }

    ecs_os_free(ids);
    ecs_os_free(keys);
    ecs_os_free(offsets);
    ecs_os_free(entries);
    ecs_os_free(faces);
    ecs_os_free(units);
}

/** Check whether a vertex of a rebuilt mesh has the uv and attributes of a source vertex.
 *
 * @param src The source mesh
 * @param vertex The source vertex
 * @param dst The rebuilt mesh
 * @param made The vertex of the rebuilt mesh
 * @return Whether they match
 */
static bool box_mg_smooth_match(
    const box_mg_mesh_t *src,
    int32_t vertex,
    const box_mg_mesh_t *dst,
    int32_t made)
{
    bool result = src->uvs[vertex][0] == dst->uvs[made][0] &&
        src->uvs[vertex][1] == dst->uvs[made][1];
    int32_t a;
    for (a = 0; result && a < dst->attr_count; a ++) {
        result = src->attrs[a].values[vertex] == dst->attrs[a].values[made];
    }

    return result;
}

/** Rebuild a mesh with a vertex per welded position and distinct corner normal, uv and
 * attributes of the source, so a faceted mesh becomes smooth.
 *
 * @param src The source mesh
 * @param normals The normal of each corner
 * @param dst The empty mesh to fill
 */
static void box_mg_smooth_build(
    const box_mg_mesh_t *src,
    vec3 *normals,
    box_mg_mesh_t *dst)
{
    int32_t corners = src->index_count, i, j, k, a, count = 0;
    int32_t *ids = ecs_os_malloc_n(int32_t, src->vertex_count + 1);
    int32_t positions = box_mg_weld(src, ids);
    int32_t *keys = ecs_os_malloc_n(int32_t, corners + 1);
    int32_t *offsets = ecs_os_malloc_n(int32_t, positions + 1);
    int32_t *entries = ecs_os_malloc_n(int32_t, corners + 1);
    int32_t *made = ecs_os_malloc_n(int32_t, corners + 1);
    for (i = 0; i < corners; i ++) {
        keys[i] = ids[src->indices[i]];
    }

    box_mg_group(keys, corners, positions, offsets, entries);
    for (a = 0; a < src->attr_count; a ++) {
        box_mg_attr_add(dst->attrs, &dst->attr_count, src->attrs[a].name, 0);
    }

    box_mg_mesh_resize(dst, corners, corners);
    for (i = 0; i < positions; i ++) {
        int32_t first = count;
        for (j = offsets[i]; j < offsets[i + 1]; j ++) {
            int32_t corner = entries[j], vertex = (int32_t)src->indices[corner], found = -1;
            for (k = first; k < count && found < 0; k ++) {
                found = glm_vec3_eqv(dst->normals[k], normals[corner]) &&
                    box_mg_smooth_match(src, vertex, dst, k) ? k : -1;
            }

            if (found < 0) {
                found = count ++;
                glm_vec3_copy(src->positions[vertex], dst->positions[found]);
                glm_vec3_copy(normals[corner], dst->normals[found]);
                glm_vec2_copy(src->uvs[vertex], dst->uvs[found]);
                for (a = 0; a < dst->attr_count; a ++) {
                    dst->attrs[a].values[found] = src->attrs[a].values[vertex];
                }
            }

            made[corner] = found;
        }
    }

    box_mg_mesh_resize(dst, count, corners);
    for (i = 0; i < corners; i ++) {
        dst->indices[i] = (uint32_t)made[i];
    }

    ecs_os_free(ids);
    ecs_os_free(keys);
    ecs_os_free(offsets);
    ecs_os_free(entries);
    ecs_os_free(made);
}

/** Compute the normals of a mesh again, smooth within an angle or flat.
 *
 * @param node The MeshNormals
 * @param inputs The mesh
 * @param counts Values per port
 * @param out The mesh
 * @param error Unused
 * @return 0
 */
static int box_mesh_normals_eval(
    const void *node,
    const box_mg_value_t *const *inputs,
    const int32_t *counts,
    box_mg_value_t *out,
    char **error)
{
    const MeshNormals *n = node;
    const box_mg_mesh_t *src = &inputs[0]->mesh;
    float angle = n->angle > 0.0f ? n->angle : GLM_PIf;
    (void)counts;
    (void)error;
    if (n->flat) {
        box_mg_value_copy(out, inputs[0]);
        box_mg_mesh_facet(&out->mesh);
    } else if (src->index_count) {
        vec3 *normals = ecs_os_malloc_n(vec3, src->index_count);
        box_mg_corner_normals(src, angle, normals);
        box_mg_smooth_build(src, normals, &out->mesh);
        ecs_os_free(normals);
    }

    return 0;
}

/** Construct MeshTransform values with a scale of 1.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_transform_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshTransform *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshTransform){ .scale = { 1.0f, 1.0f, 1.0f } };
    }
}

/** Construct MeshDisplace values with the documented amount.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_mesh_displace_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    MeshDisplace *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (MeshDisplace){ .amount = 0.1f, .frequency = 1.0f, .octaves = 1 };
    }
}

void box_mesh_nodes_mesh_register(
    ecs_world_t *world)
{
    ecs_set_hooks(world, MeshTransform, { .ctor = box_mesh_transform_ctor });
    ecs_set_hooks(world, MeshDisplace, { .ctor = box_mesh_displace_ctor });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshTransform),
        .output = MeshValueAny,
        .inputs = {{ "input", offsetof(MeshTransform, input), MeshValueAny }},
        .params = {
            { "position", offsetof(MeshTransform, position), ECS_SIZEOF(vec3) },
            { "rotation", offsetof(MeshTransform, rotation), ECS_SIZEOF(vec3) },
            { "scale", offsetof(MeshTransform, scale), ECS_SIZEOF(vec3) }
        },
        .eval = box_mesh_transform_eval
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshMerge),
        .output = MeshValueAny,
        .inputs = {{ "inputs", offsetof(MeshMerge, inputs), MeshValueAny, .list = true }},
        .eval = box_mesh_merge_eval
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshDisplace),
        .output = MeshValueMesh,
        .inputs = {{ "mesh", offsetof(MeshDisplace, mesh), MeshValueMesh }},
        .params = {
            { "amount", offsetof(MeshDisplace, amount), ECS_SIZEOF(float) },
            { "frequency", offsetof(MeshDisplace, frequency), ECS_SIZEOF(float) },
            { "octaves", offsetof(MeshDisplace, octaves), ECS_SIZEOF(int32_t) },
            { "seed", offsetof(MeshDisplace, seed), ECS_SIZEOF(int32_t) }
        },
        .eval = box_mesh_displace_eval,
        .flags = BOX_MESH_NODE_RANDOM
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshNormals),
        .output = MeshValueMesh,
        .inputs = {{ "mesh", offsetof(MeshNormals, mesh), MeshValueMesh }},
        .params = {
            { "angle", offsetof(MeshNormals, angle), ECS_SIZEOF(float) },
            { "flat", offsetof(MeshNormals, flat), ECS_SIZEOF(bool) }
        },
        .eval = box_mesh_normals_eval
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshCut),
        .output = MeshValueMesh,
        .inputs = {{ "mesh", offsetof(MeshCut, mesh), MeshValueMesh }},
        .params = {
            { "position", offsetof(MeshCut, position), ECS_SIZEOF(vec3) },
            { "normal", offsetof(MeshCut, normal), ECS_SIZEOF(vec3) },
            { "relief", offsetof(MeshCut, relief), ECS_SIZEOF(float) },
            { "frequency", offsetof(MeshCut, frequency), ECS_SIZEOF(float) },
            { "seed", offsetof(MeshCut, seed), ECS_SIZEOF(int32_t) }
        },
        .eval = box_mesh_cut_eval,
        .flags = BOX_MESH_NODE_RANDOM
    });
    box_mesh_node_register(world, &(box_mesh_node_desc_t){
        .component = ecs_id(MeshStrata),
        .output = MeshValueMesh,
        .inputs = {{ "mesh", offsetof(MeshStrata, mesh), MeshValueMesh }},
        .params = {
            { "axis", offsetof(MeshStrata, axis), ECS_SIZEOF(vec3) },
            { "thickness", offsetof(MeshStrata, thickness), ECS_SIZEOF(float) },
            { "sharpness", offsetof(MeshStrata, sharpness), ECS_SIZEOF(float) },
            { "inset", offsetof(MeshStrata, inset), ECS_SIZEOF(float) },
            { "seed", offsetof(MeshStrata, seed), ECS_SIZEOF(int32_t) }
        },
        .eval = box_mesh_strata_eval,
        .flags = BOX_MESH_NODE_RANDOM
    });
}
