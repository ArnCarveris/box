/**
 * @file mesh_graph/values.c
 * @brief Values that flow between the nodes of mesh graphs: arrays, hashes, randomness, noise
 * and the frames of curves.
 */

#include "mesh_graph.h"
#include <math.h>

/** Multiplier of the 64 bit hash of the values */
#define BOX_MG_HASH_MUL (0x9E3779B97F4A7C15ull)

/** Prime of the FNV-1a step of the hash */
#define BOX_MG_HASH_PRIME (1099511628211ull)

box_mg_value_t* box_mg_value_new(
    MeshValueType type)
{
    box_mg_value_t *result = ecs_os_calloc_t(box_mg_value_t);
    result->type = type;
    result->refs = 1;

    return result;
}

box_mg_value_t* box_mg_value_keep(
    box_mg_value_t *value)
{
    if (value) {
        value->refs ++;
    }

    return value;
}

/** Free the arrays of attributes.
 *
 * @param attrs The attributes
 * @param count Receives 0
 */
static void box_mg_attrs_fini(
    box_mg_attr_t *attrs,
    int32_t *count)
{
    int32_t i;
    for (i = 0; i < *count; i ++) {
        ecs_os_free(attrs[i].values);
    }

    ecs_os_memset(attrs, 0, ECS_SIZEOF(box_mg_attr_t) * BOX_MESH_ATTRS);
    *count = 0;
}

void box_mg_value_clear(
    box_mg_value_t *value)
{
    box_mg_mesh_t *m = &value->mesh;
    box_mg_curves_t *c = &value->curves;
    box_mg_points_t *p = &value->points;
    ecs_os_free(m->positions);
    ecs_os_free(m->normals);
    ecs_os_free(m->uvs);
    ecs_os_free(m->indices);
    box_mg_attrs_fini(m->attrs, &m->attr_count);
    ecs_os_free(c->offsets);
    ecs_os_free(c->parents);
    ecs_os_free(c->positions);
    ecs_os_free(c->radii);
    ecs_os_free(c->tangents);
    ecs_os_free(c->normals);
    box_mg_attrs_fini(c->attrs, &c->attr_count);
    ecs_os_free(p->positions);
    ecs_os_free(p->rotations);
    ecs_os_free(p->scales);
    box_mg_attrs_fini(p->attrs, &p->attr_count);
    ecs_os_memset_t(m, 0, box_mg_mesh_t);
    ecs_os_memset_t(c, 0, box_mg_curves_t);
    ecs_os_memset_t(p, 0, box_mg_points_t);
}

void box_mg_value_release(
    box_mg_value_t *value)
{
    if (value && !-- value->refs) {
        box_mg_value_clear(value);
        ecs_os_free(value);
    }
}

/** Resize an array, zeroing the new elements.
 *
 * @param ptr The array, NULL for none
 * @param size Bytes of an element
 * @param from Elements it has
 * @param to Elements it gets
 * @return The array, NULL for 0 elements
 */
static void* box_mg_array(
    void *ptr,
    ecs_size_t size,
    int32_t from,
    int32_t to)
{
    void *result = NULL;
    if (to > 0) {
        result = ecs_os_realloc(ptr, size * to);
        if (to > from) {
            ecs_os_memset(ECS_ELEM(result, size, from), 0, size * (to - from));
        }
    } else {
        ecs_os_free(ptr);
    }

    return result;
}

/** Resize the arrays of attributes.
 *
 * @param attrs The attributes
 * @param count Number of attributes
 * @param from Elements they have
 * @param to Elements they get
 */
static void box_mg_attrs_resize(
    box_mg_attr_t *attrs,
    int32_t count,
    int32_t from,
    int32_t to)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        attrs[i].values = box_mg_array(attrs[i].values, ECS_SIZEOF(float), from, to);
    }
}

void box_mg_mesh_resize(
    box_mg_mesh_t *mesh,
    int32_t vertices,
    int32_t indices)
{
    int32_t from = mesh->vertex_count;
    mesh->positions = box_mg_array(mesh->positions, ECS_SIZEOF(vec3), from, vertices);
    mesh->normals = box_mg_array(mesh->normals, ECS_SIZEOF(vec3), from, vertices);
    mesh->uvs = box_mg_array(mesh->uvs, ECS_SIZEOF(vec2), from, vertices);
    mesh->indices = box_mg_array(mesh->indices, ECS_SIZEOF(uint32_t), mesh->index_count,
        indices);
    box_mg_attrs_resize(mesh->attrs, mesh->attr_count, from, vertices);
    mesh->vertex_count = vertices;
    mesh->index_count = indices;
}

void box_mg_curves_resize(
    box_mg_curves_t *curves,
    int32_t curve_count,
    int32_t point_count)
{
    int32_t from = curves->point_count, i;
    int32_t old = curves->offsets ? curves->curve_count + 1 : 0;
    curves->offsets = box_mg_array(curves->offsets, ECS_SIZEOF(int32_t), old, curve_count + 1);
    curves->parents = box_mg_array(curves->parents, ECS_SIZEOF(int32_t), curves->curve_count,
        curve_count);
    for (i = curves->curve_count; i < curve_count; i ++) {
        curves->parents[i] = -1;
    }

    curves->positions = box_mg_array(curves->positions, ECS_SIZEOF(vec3), from, point_count);
    curves->radii = box_mg_array(curves->radii, ECS_SIZEOF(float), from, point_count);
    curves->tangents = box_mg_array(curves->tangents, ECS_SIZEOF(vec3), from, point_count);
    curves->normals = box_mg_array(curves->normals, ECS_SIZEOF(vec3), from, point_count);
    box_mg_attrs_resize(curves->attrs, curves->attr_count, from, point_count);
    curves->curve_count = curve_count;
    curves->point_count = point_count;
}

void box_mg_points_resize(
    box_mg_points_t *points,
    int32_t count)
{
    int32_t from = points->count, i;
    points->positions = box_mg_array(points->positions, ECS_SIZEOF(vec3), from, count);
    points->rotations = box_mg_array(points->rotations, ECS_SIZEOF(versor), from, count);
    points->scales = box_mg_array(points->scales, ECS_SIZEOF(vec3), from, count);
    box_mg_attrs_resize(points->attrs, points->attr_count, from, count);
    for (i = from; i < count; i ++) {
        glm_quat_identity(points->rotations[i]);
        glm_vec3_one(points->scales[i]);
    }

    points->count = count;
}

float* box_mg_attr_find(
    const box_mg_attr_t *attrs,
    int32_t count,
    const char *name)
{
    float *result = NULL;
    int32_t i;
    for (i = 0; i < count && !result; i ++) {
        if (!strncmp(attrs[i].name, name, BOX_MESH_ATTR_NAME - 1)) {
            result = attrs[i].values;
        }
    }

    return result;
}

float* box_mg_attr_add(
    box_mg_attr_t *attrs,
    int32_t *count,
    const char *name,
    int32_t elements)
{
    float *result = box_mg_attr_find(attrs, *count, name);
    if (!result && *count < BOX_MESH_ATTRS) {
        box_mg_attr_t *attr = &attrs[(*count) ++];
        ecs_os_memset_t(attr, 0, box_mg_attr_t);
        ecs_os_strncpy(attr->name, name, BOX_MESH_ATTR_NAME - 1);
        attr->values = elements > 0 ? ecs_os_calloc_n(float, elements) : NULL;
        result = attr->values;
    }

    return result;
}

uint64_t box_mg_hash(
    uint64_t hash,
    const void *data,
    int64_t size)
{
    const uint8_t *bytes = data;
    int64_t i = 0;
    for (; i + 8 <= size; i += 8) {
        uint64_t word;
        ecs_os_memcpy(&word, bytes + i, 8);
        word *= BOX_MG_HASH_MUL;
        word ^= word >> 31;
        hash = (hash ^ word) * BOX_MG_HASH_MUL;
        hash ^= hash >> 29;
    }

    for (; i < size; i ++) {
        hash ^= bytes[i];
        hash *= BOX_MG_HASH_PRIME;
    }

    return hash;
}

/** Add attributes to a hash.
 *
 * @param hash The hash so far
 * @param attrs The attributes
 * @param count Number of attributes
 * @param elements Elements of each attribute
 * @return The hash with the attributes
 */
static uint64_t box_mg_attrs_hash(
    uint64_t hash,
    const box_mg_attr_t *attrs,
    int32_t count,
    int32_t elements)
{
    int32_t i;
    hash = box_mg_hash(hash, &count, ECS_SIZEOF(int32_t));
    for (i = 0; i < count; i ++) {
        hash = box_mg_hash(hash, attrs[i].name, BOX_MESH_ATTR_NAME);
        hash = box_mg_hash(hash, attrs[i].values, (int64_t)elements * ECS_SIZEOF(float));
    }

    return hash;
}

uint64_t box_mg_value_hash(
    uint64_t hash,
    const box_mg_value_t *value)
{
    const box_mg_mesh_t *m = &value->mesh;
    const box_mg_curves_t *c = &value->curves;
    const box_mg_points_t *p = &value->points;
    int64_t v = m->vertex_count, n = c->point_count, k = p->count;
    hash = box_mg_hash(hash, &value->type, ECS_SIZEOF(MeshValueType));
    if (value->type == MeshValueMesh) {
        hash = box_mg_hash(hash, &m->vertex_count, ECS_SIZEOF(int32_t) * 2);
        hash = box_mg_hash(hash, m->positions, v * ECS_SIZEOF(vec3));
        hash = box_mg_hash(hash, m->normals, v * ECS_SIZEOF(vec3));
        hash = box_mg_hash(hash, m->uvs, v * ECS_SIZEOF(vec2));
        hash = box_mg_hash(hash, m->indices, (int64_t)m->index_count * ECS_SIZEOF(uint32_t));
        hash = box_mg_attrs_hash(hash, m->attrs, m->attr_count, m->vertex_count);
    } else if (value->type == MeshValueCurves) {
        hash = box_mg_hash(hash, &c->curve_count, ECS_SIZEOF(int32_t) * 2);
        hash = box_mg_hash(hash, c->offsets, (c->curve_count + 1) * ECS_SIZEOF(int32_t));
        hash = box_mg_hash(hash, c->parents, c->curve_count * ECS_SIZEOF(int32_t));
        hash = box_mg_hash(hash, c->positions, n * ECS_SIZEOF(vec3));
        hash = box_mg_hash(hash, c->radii, n * ECS_SIZEOF(float));
        hash = box_mg_hash(hash, c->tangents, n * ECS_SIZEOF(vec3));
        hash = box_mg_hash(hash, c->normals, n * ECS_SIZEOF(vec3));
        hash = box_mg_attrs_hash(hash, c->attrs, c->attr_count, c->point_count);
    } else {
        hash = box_mg_hash(hash, &p->count, ECS_SIZEOF(int32_t));
        hash = box_mg_hash(hash, p->positions, k * ECS_SIZEOF(vec3));
        hash = box_mg_hash(hash, p->rotations, k * ECS_SIZEOF(versor));
        hash = box_mg_hash(hash, p->scales, k * ECS_SIZEOF(vec3));
        hash = box_mg_attrs_hash(hash, p->attrs, p->attr_count, p->count);
    }

    return hash;
}

int64_t box_mg_value_bytes(
    const box_mg_value_t *value)
{
    const box_mg_mesh_t *m = &value->mesh;
    const box_mg_curves_t *c = &value->curves;
    const box_mg_points_t *p = &value->points;
    int64_t result = ECS_SIZEOF(box_mg_value_t);
    result += (int64_t)m->vertex_count * (ECS_SIZEOF(vec3) * 2 + ECS_SIZEOF(vec2) +
        m->attr_count * ECS_SIZEOF(float)) + (int64_t)m->index_count * ECS_SIZEOF(uint32_t);
    result += (int64_t)c->curve_count * ECS_SIZEOF(int32_t) * 2 + (int64_t)c->point_count *
        (ECS_SIZEOF(vec3) * 3 + ECS_SIZEOF(float) * (1 + c->attr_count));
    result += (int64_t)p->count * (ECS_SIZEOF(vec3) * 2 + ECS_SIZEOF(versor) +
        p->attr_count * ECS_SIZEOF(float));

    return result;
}

/** Copy attributes.
 *
 * @param dst The attributes to fill, empty
 * @param dst_count Receives the number of attributes
 * @param src The attributes
 * @param count Number of attributes
 * @param elements Elements of each attribute
 */
static void box_mg_attrs_copy(
    box_mg_attr_t *dst,
    int32_t *dst_count,
    const box_mg_attr_t *src,
    int32_t count,
    int32_t elements)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        float *values = box_mg_attr_add(dst, dst_count, src[i].name, elements);
        if (values && elements) {
            ecs_os_memcpy_n(values, src[i].values, float, elements);
        }
    }
}

void box_mg_value_copy(
    box_mg_value_t *dst,
    const box_mg_value_t *src)
{
    const box_mg_mesh_t *m = &src->mesh;
    const box_mg_curves_t *c = &src->curves;
    const box_mg_points_t *p = &src->points;
    int32_t v = m->vertex_count, n = c->point_count, k = p->count;
    dst->type = src->type;
    if (src->type == MeshValueMesh) {
        box_mg_mesh_resize(&dst->mesh, v, m->index_count);
        if (v) {
            ecs_os_memcpy_n(dst->mesh.positions, m->positions, vec3, v);
            ecs_os_memcpy_n(dst->mesh.normals, m->normals, vec3, v);
            ecs_os_memcpy_n(dst->mesh.uvs, m->uvs, vec2, v);
        }

        if (m->index_count) {
            ecs_os_memcpy_n(dst->mesh.indices, m->indices, uint32_t, m->index_count);
        }

        box_mg_attrs_copy(dst->mesh.attrs, &dst->mesh.attr_count, m->attrs, m->attr_count, v);
    } else if (src->type == MeshValueCurves) {
        box_mg_curves_resize(&dst->curves, c->curve_count, n);
        ecs_os_memcpy_n(dst->curves.offsets, c->offsets, int32_t, c->curve_count + 1);
        if (c->curve_count) {
            ecs_os_memcpy_n(dst->curves.parents, c->parents, int32_t, c->curve_count);
        }

        if (n) {
            ecs_os_memcpy_n(dst->curves.positions, c->positions, vec3, n);
            ecs_os_memcpy_n(dst->curves.radii, c->radii, float, n);
            ecs_os_memcpy_n(dst->curves.tangents, c->tangents, vec3, n);
            ecs_os_memcpy_n(dst->curves.normals, c->normals, vec3, n);
        }

        box_mg_attrs_copy(dst->curves.attrs, &dst->curves.attr_count, c->attrs,
            c->attr_count, n);
    } else if (src->type == MeshValuePoints) {
        box_mg_points_resize(&dst->points, k);
        if (k) {
            ecs_os_memcpy_n(dst->points.positions, p->positions, vec3, k);
            ecs_os_memcpy_n(dst->points.rotations, p->rotations, versor, k);
            ecs_os_memcpy_n(dst->points.scales, p->scales, vec3, k);
        }

        box_mg_attrs_copy(dst->points.attrs, &dst->points.attr_count, p->attrs,
            p->attr_count, k);
    }
}

const char* box_mg_type_name(
    MeshValueType type)
{
    const char *result = "Any";
    if (type == MeshValueMesh) {
        result = "Mesh";
    } else if (type == MeshValueCurves) {
        result = "Curves";
    } else if (type == MeshValuePoints) {
        result = "Points";
    }

    return result;
}

float box_mg_lod_factor(
    float factor,
    float fallback,
    int32_t level)
{
    float f = factor > 0.0f ? glm_min(factor, 1.0f) : (factor < 0.0f ? 1.0f : fallback);
    float result = 1.0f;
    int32_t i;
    for (i = 0; i < level; i ++) {
        result *= f;
    }

    return result;
}

/** Mix the bits of a 64 bit number, the SplitMix64 finalizer.
 *
 * @param x The number
 * @return The mixed number
 */
static uint64_t box_mg_mix(
    uint64_t x)
{
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;

    return x;
}

void box_mg_rand_seed(
    box_mg_rand_t *rand,
    int32_t seed,
    uint32_t stream)
{
    rand->state = box_mg_mix(((uint64_t)(uint32_t)seed << 32) ^ stream ^ 0x5851F42D4C957F2Dull);
}

float box_mg_rand_float(
    box_mg_rand_t *rand)
{
    rand->state += BOX_MG_HASH_MUL;

    return (float)(box_mg_mix(rand->state) >> 40) * (1.0f / 16777216.0f);
}

void box_mg_sincos(
    float angle,
    float *sine,
    float *cosine)
{
    float turn = floorf(angle * (0.5f / GLM_PIf) + 0.5f);
    float r = (angle - turn * 6.28125f) - turn * 1.9353071795864769e-3f;
    float quarter = floorf(r * (2.0f / GLM_PIf) + 0.5f);
    float y = (r - quarter * 1.5703125f) - quarter * 4.8382679489661923e-4f;
    float y2 = y * y;
    float s = y + y * y2 * (-1.0f / 6.0f + y2 * (1.0f / 120.0f + y2 * (-1.0f / 5040.0f +
        y2 * (1.0f / 362880.0f))));
    float c = 1.0f + y2 * (-0.5f + y2 * (1.0f / 24.0f + y2 * (-1.0f / 720.0f +
        y2 * (1.0f / 40320.0f))));
    int32_t q = (int32_t)quarter & 3;
    *sine = q == 0 ? s : (q == 1 ? c : (q == 2 ? -s : -c));
    *cosine = q == 0 ? c : (q == 1 ? -s : (q == 2 ? -c : s));
}

void box_mg_euler(
    const vec3 angles,
    mat3 out)
{
    float sx, cx, sy, cy, sz, cz;
    box_mg_sincos(angles[0], &sx, &cx);
    box_mg_sincos(angles[1], &sy, &cy);
    box_mg_sincos(angles[2], &sz, &cz);
    float cycz = cy * cz, sysz = sy * sz, czsy = cz * sy, cysz = cy * sz;
    out[0][0] = cycz + sx * sysz;
    out[0][1] = cx * sz;
    out[0][2] = -czsy + cysz * sx;
    out[1][0] = -cysz + czsy * sx;
    out[1][1] = cx * cz;
    out[1][2] = cycz * sx + sysz;
    out[2][0] = cx * sy;
    out[2][1] = -sx;
    out[2][2] = cx * cy;
}

/** Gradient of a lattice point of the noise, one of the 12 edge directions of a cube.
 *
 * @param x Lattice x
 * @param y Lattice y
 * @param z Lattice z
 * @param seed Seed of the noise
 * @param d Offset of the point from the lattice point
 * @return The dot product of the gradient and the offset
 */
static float box_mg_grad(
    int32_t x,
    int32_t y,
    int32_t z,
    int32_t seed,
    const vec3 d)
{
    uint64_t h = box_mg_mix(((uint64_t)(uint32_t)x * 0x8DA6B343ull) ^
        ((uint64_t)(uint32_t)y * 0xD8163841ull << 16) ^
        ((uint64_t)(uint32_t)z * 0xCB1AB31Full << 32) ^ (uint64_t)(uint32_t)seed);
    int32_t g = (int32_t)(h % 12);
    float u = g < 8 ? d[0] : d[1];
    float v = g < 4 ? d[1] : (g == 12 || g == 14 ? d[0] : d[2]);

    return ((g & 1) ? -u : u) + ((g & 2) ? -v : v);
}

/** Quintic fade of the noise.
 *
 * @param t Fraction in the cell
 * @return The weight
 */
static float box_mg_fade(
    float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/** Gradient noise of a point, about -1..1.
 *
 * @param p The point
 * @param seed Seed of the noise
 * @return The noise
 */
static float box_mg_noise1(
    const vec3 p,
    int32_t seed)
{
    float fx = floorf(p[0]), fy = floorf(p[1]), fz = floorf(p[2]);
    int32_t x = (int32_t)fx, y = (int32_t)fy, z = (int32_t)fz, i;
    vec3 f = { p[0] - fx, p[1] - fy, p[2] - fz };
    float c[8];
    for (i = 0; i < 8; i ++) {
        int32_t ox = i & 1, oy = (i >> 1) & 1, oz = (i >> 2) & 1;
        vec3 d = { f[0] - (float)ox, f[1] - (float)oy, f[2] - (float)oz };
        c[i] = box_mg_grad(x + ox, y + oy, z + oz, seed, d);
    }

    float u = box_mg_fade(f[0]), v = box_mg_fade(f[1]), w = box_mg_fade(f[2]);
    float x00 = c[0] + (c[1] - c[0]) * u, x10 = c[2] + (c[3] - c[2]) * u;
    float x01 = c[4] + (c[5] - c[4]) * u, x11 = c[6] + (c[7] - c[6]) * u;
    float y0 = x00 + (x10 - x00) * v, y1 = x01 + (x11 - x01) * v;

    return y0 + (y1 - y0) * w;
}

float box_mg_noise(
    const vec3 p,
    int32_t octaves,
    int32_t seed)
{
    float sum = 0.0f, amplitude = 1.0f, total = 0.0f, frequency = 1.0f;
    int32_t i;
    for (i = 0; i < octaves; i ++) {
        vec3 q = { p[0] * frequency, p[1] * frequency, p[2] * frequency };
        sum += amplitude * box_mg_noise1(q, seed + i * 1013);
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return total > 0.0f ? sum / total : 0.0f;
}

/** Pick a unit vector perpendicular to a unit tangent, toward +Y unless the tangent is
 * nearly vertical, then toward +Z.
 *
 * @param tangent The tangent
 * @param out Receives the normal
 */
static void box_mg_perpendicular(
    const vec3 tangent,
    vec3 out)
{
    vec3 up = { 0.0f, 1.0f, 0.0f };
    if (fabsf(tangent[1]) > 0.99f) {
        glm_vec3_copy((vec3){ 0.0f, 0.0f, 1.0f }, up);
    }

    glm_vec3_scale((float*)tangent, glm_vec3_dot(up, (float*)tangent), out);
    glm_vec3_sub(up, out, out);
    glm_vec3_normalize(out);
}

/** Compute the tangents of a curve by the directions to its neighbors.
 *
 * @param curves The curves
 * @param first First point of the curve
 * @param count Points of the curve
 */
static void box_mg_curve_tangents(
    box_mg_curves_t *curves,
    int32_t first,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t a = first + (i > 0 ? i - 1 : 0), b = first + (i < count - 1 ? i + 1 : i);
        vec3 t;
        glm_vec3_sub(curves->positions[b], curves->positions[a], t);
        if (glm_vec3_norm2(t) < 1e-20f) {
            glm_vec3_copy(i > 0 ? curves->tangents[first + i - 1] : (vec3){ 0, 1, 0 }, t);
        }

        glm_vec3_normalize_to(t, curves->tangents[first + i]);
    }
}

void box_mg_curves_frames(
    box_mg_curves_t *curves)
{
    box_mg_curves_frames_range(curves, 0, curves->curve_count);
}

void box_mg_curves_frames_range(
    box_mg_curves_t *curves,
    int32_t from,
    int32_t to)
{
    int32_t c, i;
    for (c = from; c < to; c ++) {
        int32_t first = curves->offsets[c], count = curves->offsets[c + 1] - first;
        if (count <= 0) {
            continue;
        }

        box_mg_curve_tangents(curves, first, count);
        box_mg_perpendicular(curves->tangents[first], curves->normals[first]);
        for (i = 1; i < count; i ++) {
            float *x0 = curves->positions[first + i - 1], *x1 = curves->positions[first + i];
            float *t0 = curves->tangents[first + i - 1], *t1 = curves->tangents[first + i];
            float *r0 = curves->normals[first + i - 1], *r1 = curves->normals[first + i];
            vec3 v1, rl, tl, v2;
            glm_vec3_sub(x1, x0, v1);
            float c1 = glm_vec3_dot(v1, v1);
            if (c1 < 1e-20f) {
                glm_vec3_copy(r0, r1);
                continue;
            }

            glm_vec3_scale(v1, 2.0f * glm_vec3_dot(v1, r0) / c1, rl);
            glm_vec3_sub(r0, rl, rl);
            glm_vec3_scale(v1, 2.0f * glm_vec3_dot(v1, t0) / c1, tl);
            glm_vec3_sub(t0, tl, tl);
            glm_vec3_sub(t1, tl, v2);
            float c2 = glm_vec3_dot(v2, v2);
            if (c2 > 1e-20f) {
                glm_vec3_scale(v2, 2.0f * glm_vec3_dot(v2, rl) / c2, v2);
                glm_vec3_sub(rl, v2, rl);
            }

            glm_vec3_scale(t1, glm_vec3_dot(rl, t1), v2);
            glm_vec3_sub(rl, v2, rl);
            if (glm_vec3_norm2(rl) < 1e-12f) {
                box_mg_perpendicular(t1, r1);
            } else {
                glm_vec3_normalize_to(rl, r1);
            }
        }
    }
}

float box_mg_curve_lengths(
    const box_mg_curves_t *curves,
    int32_t curve,
    float *lengths)
{
    int32_t first = curves->offsets[curve], count = curves->offsets[curve + 1] - first, i;
    float length = 0.0f;
    for (i = 0; i < count; i ++) {
        if (i) {
            length += glm_vec3_distance(curves->positions[first + i - 1],
                curves->positions[first + i]);
        }

        lengths[i] = length;
    }

    return length;
}

void box_mg_curves_tips(
    box_mg_curves_t *curves)
{
    float *tip = box_mg_attr_add(curves->attrs, &curves->attr_count, "tip", curves->point_count);
    int32_t c;
    bool *parent = ecs_os_calloc_n(bool, curves->curve_count + 1);
    for (c = 0; c < curves->curve_count; c ++) {
        int32_t p = curves->parents[c];
        if (p >= 0 && p < curves->curve_count) {
            parent[p] = true;
        }
    }

    for (c = 0; tip && c < curves->curve_count; c ++) {
        int32_t first = curves->offsets[c], last = curves->offsets[c + 1] - 1, i;
        for (i = first; i <= last; i ++) {
            tip[i] = i == last && !parent[c] ? 1.0f : 0.0f;
        }
    }

    ecs_os_free(parent);
}
