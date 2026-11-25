/**
 * @file command/cmd_geometry.c
 * @brief The geometry command and geometry.check: a lint of the 3D shapes of the scene that
 * lists the pairs of shapes with faces that z-fight.
 *
 * Two faces z-fight when they lie in the same plane, face the same way and overlap. The check
 * sweeps the world bounds of the 3D shapes to find the pairs that may touch.
 *
 * Each pair of near parallel triangles of two shapes is clipped against each other in the plane
 * of the first; where the planes are closer than the tolerance, the overlap fights. It cannot be
 * seen when another shape covers it, or when both faces are flat shaded with the same look.
 */

#include "command.h"
#include "../geometry/shape2d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Most arguments of geometry.check: the root and the options */
#define BOX_CMD_GEOMETRY_OPTIONS_MAX (8)

/** Default distance in world units under which two parallel faces count as one plane, a little
 * over what a reversed Z Depth32Float depth buffer resolves at a few hundred units */
#define BOX_CMD_GEOMETRY_TOLERANCE (0.0001f)

/** Default smallest area in square world units of the faces of a pair that fight */
#define BOX_CMD_GEOMETRY_AREA (0.0001f)

/** Default most pairs listed, the others are counted on one line */
#define BOX_CMD_GEOMETRY_MAX (100)

/** Smallest cosine of the angle between the normals of two faces that may fight, half a
 * degree */
#define BOX_CMD_GEOMETRY_PARALLEL (0.99996f)

/** Area in square world units under which a triangle is degenerate and skipped */
#define BOX_CMD_GEOMETRY_DEGENERATE (1e-12f)

/** Most vertices of a polygon of the clip, a triangle clipped by five half planes */
#define BOX_CMD_GEOMETRY_POLY_MAX (16)

/** Smallest cosine of the angle between the vertex normals of a triangle that shades flat */
#define BOX_CMD_GEOMETRY_FLAT (0.9999f)

/** Distance in world units in front of a face at which a point inside another shape means the
 * face is covered, so it cannot be seen fighting */
#define BOX_CMD_GEOMETRY_COVER (0.002f)

/** A triangle of a shape in world space */
typedef struct box_cmd_geometry_tri_t {
    vec3 p[3];              /**< Corners */
    vec3 n;                 /**< Unit normal pointing out of the shape */
    bool flat;              /**< Its vertex normals are equal, so it shades as a flat face */
} box_cmd_geometry_tri_t;

/** A 3D shape and its triangles */
typedef struct box_cmd_geometry_shape_t {
    ecs_entity_t e;         /**< The entity */
    int32_t first;          /**< Index of its first triangle */
    int32_t count;          /**< Number of its triangles */
    vec3 min;               /**< Minimum corner of its world bounds */
    vec3 max;               /**< Maximum corner of its world bounds */
} box_cmd_geometry_shape_t;

/** A pair of shapes with faces that fight */
typedef struct box_cmd_geometry_finding_t {
    ecs_entity_t a;         /**< One shape */
    ecs_entity_t b;         /**< The other shape */
    float area;             /**< Area of the faces that fight and can be seen */
    float hidden;           /**< Area of the faces that fight inside another shape */
    float alike;            /**< Area of flat faces that fight with the same look */
    float gap;              /**< Smallest distance between the planes in that area */
    float best;             /**< Area of the largest overlap of two triangles */
    vec3 at;                /**< Center of the area */
    vec3 normal;            /**< Normal of the largest overlap of two triangles */
} box_cmd_geometry_finding_t;

/** Bounds of a triangle */
typedef struct box_cmd_geometry_box_t {
    vec3 min;               /**< Minimum corner */
    vec3 max;               /**< Maximum corner */
} box_cmd_geometry_box_t;

/** The state of a check */
typedef struct box_cmd_geometry_check_t {
    ecs_world_t *world;     /**< The world */
    ecs_entity_t root;      /**< Entity whose descendants are checked, 0 for the world */
    float tolerance;        /**< Distance under which two faces count as one plane */
    float area;             /**< Smallest area of a finding */
    int32_t max;            /**< Most findings listed */
    int32_t unplaced;       /**< Shapes without a WorldTransform, which are not drawn */
    bool list_hidden;       /**< List the pairs whose faces fight only where it cannot be
                             *   seen */
    ecs_vec_t tris;         /**< box_cmd_geometry_tri_t, of every shape */
    ecs_vec_t shapes;       /**< box_cmd_geometry_shape_t */
    ecs_vec_t findings;     /**< box_cmd_geometry_finding_t */
    ecs_vec_t list_a;       /**< int32_t, triangles of the first shape of a pair */
    ecs_vec_t list_b;       /**< int32_t, triangles of the second shape of a pair */
    ecs_vec_t box_a;        /**< box_cmd_geometry_box_t, bounds of list_a */
    ecs_vec_t box_b;        /**< box_cmd_geometry_box_t, bounds of list_b */
} box_cmd_geometry_check_t;

/** Axis along which box_cmd_geometry_shape_cmp orders the shapes */
static int32_t box_cmd_geometry_axis;

/** Get a string argument of a command.
 *
 * @param argc Number of arguments
 * @param argv The arguments
 * @param index Index of the argument
 * @return The argument, an empty string when it is missing
 */
static const char* box_cmd_geometry_arg(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t index)
{
    const char *result = argc > index ? *(const char**)argv[index].ptr : NULL;

    return result ? result : "";
}

/** Check whether an entity is a root or one of its descendants.
 *
 * @param world The world
 * @param e The entity
 * @param root The root
 * @return Whether it is
 */
static bool box_cmd_geometry_within(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t root)
{
    while (e && e != root) {
        e = ecs_get_parent(world, e);
    }

    return e == root;
}

/** Get the mesh of a shape with its positions, the one resolved on the CPU for a parametric
 * surface.
 *
 * @param cache The mesh cache
 * @param shape The shape
 * @return The mesh, NULL for none
 */
static const box_mesh_t* box_cmd_geometry_mesh(
    const box_mesh_cache_t *cache,
    const ShapeMesh *shape)
{
    const box_mesh_t *result = NULL;
    if (shape->render_mesh) {
        result = box_mesh_cache_get(cache, shape->render_mesh);
    }

    if (!result && shape->mesh) {
        result = box_mesh_cache_get(cache, shape->mesh);
    }

    return result;
}

/** Check whether the shapes of a table are not drawn as 3D shapes: prefabs, disabled entities
 * and 2D shapes.
 *
 * @param world The world
 * @param table The table
 * @return Whether to skip them
 */
static bool box_cmd_geometry_skip(
    ecs_world_t *world,
    const ecs_table_t *table)
{
    return ecs_table_has_id(world, table, EcsPrefab) ||
        ecs_table_has_id(world, table, EcsDisabled) ||
        ecs_table_has_id(world, table, ecs_id(Shape2D));
}

/** Add a shape and its world space triangles to a check. The normals point out of the shape,
 * also under a mirroring transform. A shape without a WorldTransform is not drawn, so it is
 * only counted.
 *
 * @param c The check
 * @param cache The mesh cache
 * @param e The entity
 * @param shape Its ShapeMesh
 */
static void box_cmd_geometry_add_shape(
    box_cmd_geometry_check_t *c,
    const box_mesh_cache_t *cache,
    ecs_entity_t e,
    const ShapeMesh *shape)
{
    const box_mesh_t *mesh = box_cmd_geometry_mesh(cache, shape);
    WorldTransform transform_buf;
    const WorldTransform *transform = box_transform_world(c->world, e, &transform_buf);
    box_cmd_geometry_shape_t *s;
    mat4 m;
    mat3 m3;
    float sign;
    int32_t i, j;
    if (!mesh || mesh->index_count < 3) {
        goto done;
    }

    if (!transform) {
        c->unplaced ++;
        goto done;
    }

    box_transform_mat4(transform, NULL, m);

    glm_mat4_pick3(m, m3);
    sign = glm_mat3_det(m3) * shape->scale[0] * shape->scale[1] * shape->scale[2];
    sign = sign < 0 ? -1.0f : 1.0f;

    s = ecs_vec_append_t(NULL, &c->shapes, box_cmd_geometry_shape_t);
    s->e = e;
    s->first = ecs_vec_count(&c->tris);
    s->count = 0;
    glm_vec3_fill(s->min, INFINITY);
    glm_vec3_fill(s->max, -INFINITY);

    for (i = 0; i + 2 < mesh->index_count; i += 3) {
        box_cmd_geometry_tri_t t;
        vec3 ab, ac;
        bool valid = true;
        for (j = 0; j < 3 && valid; j ++) {
            uint32_t index = mesh->indices[i + j];
            vec3 local;
            valid = index < (uint32_t)mesh->vertex_count;
            if (valid) {
                glm_vec3_mul((float*)mesh->vertices[index].position, (float*)shape->scale, local);
                glm_mat4_mulv3(m, local, 1.0f, t.p[j]);
            }
        }

        if (!valid) {
            continue;
        }

        glm_vec3_sub(t.p[1], t.p[0], ab);
        glm_vec3_sub(t.p[2], t.p[0], ac);
        glm_vec3_cross(ab, ac, t.n);
        float len = glm_vec3_norm(t.n);
        if (!isfinite(len) || len * 0.5f < BOX_CMD_GEOMETRY_DEGENERATE) {
            continue;
        }

        glm_vec3_scale(t.n, sign / len, t.n);
        const box_vertex_t *v0 = &mesh->vertices[mesh->indices[i]];
        const box_vertex_t *v1 = &mesh->vertices[mesh->indices[i + 1]];
        const box_vertex_t *v2 = &mesh->vertices[mesh->indices[i + 2]];
        t.flat = glm_vec3_dot((float*)v0->normal, (float*)v1->normal) > BOX_CMD_GEOMETRY_FLAT &&
            glm_vec3_dot((float*)v0->normal, (float*)v2->normal) > BOX_CMD_GEOMETRY_FLAT;
        for (j = 0; j < 3; j ++) {
            glm_vec3_minv(s->min, t.p[j], s->min);
            glm_vec3_maxv(s->max, t.p[j], s->max);
        }

        *ecs_vec_append_t(NULL, &c->tris, box_cmd_geometry_tri_t) = t;
        s->count ++;
    }

    if (!s->count) {
        ecs_vec_remove_last(&c->shapes);
    }
done:
    return;
}

/** Add the 3D shapes of the world, or of the root of the check, to a check.
 *
 * @param c The check
 */
static void box_cmd_geometry_collect(
    box_cmd_geometry_check_t *c)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(c->world);
    ecs_iter_t it = ecs_each(c->world, ShapeMesh);
    while (ecs_each_next(&it)) {
        const ShapeMesh *shapes = ecs_field(&it, ShapeMesh, 0);
        int32_t i;
        if (!cache || box_cmd_geometry_skip(c->world, it.table)) {
            continue;
        }

        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (!c->root || box_cmd_geometry_within(c->world, e, c->root)) {
                box_cmd_geometry_add_shape(c, cache, e, &shapes[i]);
            }
        }
    }
}

/** Clip a convex polygon by a half plane, keeping the points where a * x + b * y + k is not
 * negative.
 *
 * @param in The polygon, x and y per vertex
 * @param count Number of vertices of the polygon
 * @param out Receives the clipped polygon, room for BOX_CMD_GEOMETRY_POLY_MAX vertices
 * @param a Factor of x
 * @param b Factor of y
 * @param k Constant
 * @return Number of vertices of the clipped polygon
 */
static int32_t box_cmd_geometry_clip(
    const float *in,
    int32_t count,
    float *out,
    float a,
    float b,
    float k)
{
    int32_t i, result = 0;
    for (i = 0; i < count; i ++) {
        const float *p = &in[i * 2];
        const float *q = &in[((i + 1) % count) * 2];
        float dp = a * p[0] + b * p[1] + k;
        float dq = a * q[0] + b * q[1] + k;
        if (dp >= 0 && result < BOX_CMD_GEOMETRY_POLY_MAX) {
            out[result * 2] = p[0];
            out[result * 2 + 1] = p[1];
            result ++;
        }

        if (((dp >= 0) != (dq >= 0)) && result < BOX_CMD_GEOMETRY_POLY_MAX) {
            float t = dp / (dp - dq);
            out[result * 2] = p[0] + (q[0] - p[0]) * t;
            out[result * 2 + 1] = p[1] + (q[1] - p[1]) * t;
            result ++;
        }
    }

    return result;
}

/** Check whether two bounds overlap.
 *
 * @param amin Minimum corner of the first bounds
 * @param amax Maximum corner of the first bounds
 * @param bmin Minimum corner of the second bounds
 * @param bmax Maximum corner of the second bounds
 * @param margin Distance by which the bounds may be apart and still overlap
 * @return Whether they overlap
 */
static bool box_cmd_geometry_overlap(
    const vec3 amin,
    const vec3 amax,
    const vec3 bmin,
    const vec3 bmax,
    float margin)
{
    return amin[0] <= bmax[0] + margin && bmin[0] <= amax[0] + margin &&
        amin[1] <= bmax[1] + margin && bmin[1] <= amax[1] + margin &&
        amin[2] <= bmax[2] + margin && bmin[2] <= amax[2] + margin;
}

/** Get the bounds of a triangle.
 *
 * @param t The triangle
 * @param out Receives the bounds
 */
static void box_cmd_geometry_tri_box(
    const box_cmd_geometry_tri_t *t,
    box_cmd_geometry_box_t *out)
{
    glm_vec3_minv((float*)t->p[0], (float*)t->p[1], out->min);
    glm_vec3_minv(out->min, (float*)t->p[2], out->min);
    glm_vec3_maxv((float*)t->p[0], (float*)t->p[1], out->max);
    glm_vec3_maxv(out->max, (float*)t->p[2], out->max);
}

/** Collect the triangles of a shape that overlap bounds, with their own bounds.
 *
 * @param c The check
 * @param s The shape
 * @param min Minimum corner of the bounds
 * @param max Maximum corner of the bounds
 * @param list Receives the indices of the triangles
 * @param boxes Receives the bounds of the triangles
 */
static void box_cmd_geometry_tris(
    box_cmd_geometry_check_t *c,
    const box_cmd_geometry_shape_t *s,
    const vec3 min,
    const vec3 max,
    ecs_vec_t *list,
    ecs_vec_t *boxes)
{
    const box_cmd_geometry_tri_t *tris = ecs_vec_first(&c->tris);
    int32_t i;
    ecs_vec_clear(list);
    ecs_vec_clear(boxes);
    for (i = s->first; i < s->first + s->count; i ++) {
        box_cmd_geometry_box_t b;
        box_cmd_geometry_tri_box(&tris[i], &b);
        if (box_cmd_geometry_overlap(b.min, b.max, min, max, c->tolerance)) {
            *ecs_vec_append_t(NULL, list, int32_t) = i;
            *ecs_vec_append_t(NULL, boxes, box_cmd_geometry_box_t) = b;
        }
    }
}

/** Get the area of two near parallel triangles that fights: the second projected on the plane
 * of the first and clipped by it, then by the band in which the distance between their planes
 * is within the tolerance.
 *
 * @param c The check
 * @param t1 The first triangle
 * @param t2 The second triangle, facing the same way
 * @param at Receives the center of the area
 * @param gap Receives the smallest distance between the planes in the area, 0 where they cross
 * @return The area, 0 when they do not fight
 */
static float box_cmd_geometry_face(
    const box_cmd_geometry_check_t *c,
    const box_cmd_geometry_tri_t *t1,
    const box_cmd_geometry_tri_t *t2,
    vec3 at,
    float *gap)
{
    float poly[BOX_CMD_GEOMETRY_POLY_MAX * 2], tmp[BOX_CMD_GEOMETRY_POLY_MAX * 2];
    float tri[6];
    vec3 u, v, d;
    int32_t i, count = 3;
    float k = glm_vec3_dot((float*)t1->n, (float*)t2->n);
    float result = 0;

    glm_vec3_sub((float*)t1->p[1], (float*)t1->p[0], u);
    glm_vec3_normalize(u);
    glm_vec3_cross((float*)t1->n, u, v);

    for (i = 0; i < 3; i ++) {
        glm_vec3_sub((float*)t1->p[i], (float*)t1->p[0], d);
        tri[i * 2] = glm_vec3_dot(d, u);
        tri[i * 2 + 1] = glm_vec3_dot(d, v);
        glm_vec3_sub((float*)t2->p[i], (float*)t1->p[0], d);
        poly[i * 2] = glm_vec3_dot(d, u);
        poly[i * 2 + 1] = glm_vec3_dot(d, v);
    }

    float winding = (tri[2] - tri[0]) * (tri[5] - tri[1]) - (tri[3] - tri[1]) * (tri[4] - tri[0]);
    winding = winding < 0 ? -1.0f : 1.0f;

    for (i = 0; i < 3 && count >= 3; i ++) {
        const float *e0 = &tri[i * 2], *e1 = &tri[((i + 1) % 3) * 2];
        float a = -(e1[1] - e0[1]) * winding, b = (e1[0] - e0[0]) * winding;
        count = box_cmd_geometry_clip(poly, count, tmp, a, b, -(a * e0[0] + b * e0[1]));
        ecs_os_memcpy_n(poly, tmp, float, count * 2);
    }

    float d2 = glm_vec3_dot((float*)t2->n, (float*)t2->p[0]);
    float t0 = (d2 - glm_vec3_dot((float*)t2->n, (float*)t1->p[0])) / k;
    float tx = -glm_vec3_dot((float*)t2->n, u) / k;
    float ty = -glm_vec3_dot((float*)t2->n, v) / k;

    if (count >= 3) {
        count = box_cmd_geometry_clip(poly, count, tmp, -tx, -ty, c->tolerance - t0);
        ecs_os_memcpy_n(poly, tmp, float, count * 2);
    }

    if (count >= 3) {
        count = box_cmd_geometry_clip(poly, count, tmp, tx, ty, c->tolerance + t0);
        ecs_os_memcpy_n(poly, tmp, float, count * 2);
    }

    if (count < 3) {
        goto done;
    }

    float lo = INFINITY, hi = -INFINITY, cx = 0, cy = 0, twice = 0;
    for (i = 0; i < count; i ++) {
        const float *p = &poly[i * 2], *q = &poly[((i + 1) % count) * 2];
        float t = t0 + tx * p[0] + ty * p[1];
        float cross = p[0] * q[1] - q[0] * p[1];
        twice += cross;
        cx += (p[0] + q[0]) * cross;
        cy += (p[1] + q[1]) * cross;
        lo = glm_min(lo, t);
        hi = glm_max(hi, t);
    }

    if (fabsf(twice) <= BOX_CMD_GEOMETRY_DEGENERATE) {
        goto done;
    }

    result = fabsf(twice) * 0.5f;
    *gap = lo <= 0 && hi >= 0 ? 0 : glm_min(fabsf(lo), fabsf(hi));
    glm_vec3_copy((float*)t1->p[0], at);
    glm_vec3_muladds(u, cx / (3.0f * twice), at);
    glm_vec3_muladds(v, cy / (3.0f * twice), at);
done:
    return result;
}

/** Check whether a point is inside the closed mesh of a shape: whether a ray from it crosses
 * the triangles of the shape an odd number of times.
 *
 * @param c The check
 * @param s The shape
 * @param q The point
 * @return Whether it is inside
 */
static bool box_cmd_geometry_inside(
    const box_cmd_geometry_check_t *c,
    const box_cmd_geometry_shape_t *s,
    const vec3 q)
{
    static const vec3 dir = {0.4823f, 0.6127f, 0.6261f};
    const box_cmd_geometry_tri_t *tris = ecs_vec_first(&c->tris);
    int32_t i, hits = 0;
    for (i = s->first; i < s->first + s->count; i ++) {
        const box_cmd_geometry_tri_t *t = &tris[i];
        vec3 e1, e2, h, d, qv;
        glm_vec3_sub((float*)t->p[1], (float*)t->p[0], e1);
        glm_vec3_sub((float*)t->p[2], (float*)t->p[0], e2);
        glm_vec3_cross((float*)dir, e2, h);
        float a = glm_vec3_dot(e1, h);
        if (fabsf(a) < BOX_CMD_GEOMETRY_DEGENERATE) {
            continue;
        }

        float f = 1.0f / a;
        glm_vec3_sub((float*)q, (float*)t->p[0], d);
        float u = f * glm_vec3_dot(d, h);
        if (u < 0 || u > 1) {
            continue;
        }

        glm_vec3_cross(d, e1, qv);
        float v = f * glm_vec3_dot((float*)dir, qv);
        if (v < 0 || u + v > 1) {
            continue;
        }

        hits += f * glm_vec3_dot(e2, qv) > 0;
    }

    return (hits & 1) != 0;
}

/** Check whether the face of an overlap is covered: whether a point just in front of it is
 * inside a shape other than the two of the pair.
 *
 * @param c The check
 * @param a One shape of the pair
 * @param b The other shape of the pair
 * @param at A point of the overlap
 * @param normal The normal of the faces
 * @return Whether it is covered
 */
static bool box_cmd_geometry_covered(
    const box_cmd_geometry_check_t *c,
    ecs_entity_t a,
    ecs_entity_t b,
    const vec3 at,
    const vec3 normal)
{
    const box_cmd_geometry_shape_t *shapes = ecs_vec_first(&c->shapes);
    int32_t i, count = ecs_vec_count(&c->shapes);
    bool result = false;
    vec3 q;
    glm_vec3_copy((float*)at, q);
    glm_vec3_muladds((float*)normal, BOX_CMD_GEOMETRY_COVER, q);
    for (i = 0; i < count && !result; i ++) {
        const box_cmd_geometry_shape_t *s = &shapes[i];
        if (s->e == a || s->e == b || !box_cmd_geometry_overlap(q, q, s->min, s->max, 0)) {
            continue;
        }

        result = box_cmd_geometry_inside(c, s, q);
    }

    return result;
}

/** Check whether two shapes render their flat faces alike: the same material slot, the same
 * textures, and textures (if any) projected from the world, so their texture coordinates match.
 *
 * @param world The world
 * @param a One shape
 * @param b The other shape
 * @return Whether they look alike
 */
static bool box_cmd_geometry_alike(
    ecs_world_t *world,
    ecs_entity_t a,
    ecs_entity_t b)
{
    const MaterialId *ma = ecs_get(world, a, MaterialId), *mb = ecs_get(world, b, MaterialId);
    const PbrTextures *ta = ecs_get(world, a, PbrTextures);
    const PbrTextures *tb = ecs_get(world, b, PbrTextures);
    const TextureMapping *map = ecs_get(world, a, TextureMapping);
    const Opacity *oa = ecs_get(world, a, Opacity), *ob = ecs_get(world, b, Opacity);
    bool result = ma && mb && ma->value == mb->value && ta == tb &&
        map == ecs_get(world, b, TextureMapping) && (oa ? oa->value : 1) == (ob ? ob->value : 1);
    if (result && ta) {
        result = map && map->mode == MappingWorld;
    }

    return result;
}

/** Check a pair of shapes whose bounds overlap, and add a finding when the area of their faces
 * that fights is at least the area of the check. Overlaps of flat faces of shapes that look
 * alike, and overlaps whose faces are covered by another shape, cannot be seen.
 *
 * @param c The check
 * @param sa The first shape
 * @param sb The second shape
 */
static void box_cmd_geometry_pair(
    box_cmd_geometry_check_t *c,
    const box_cmd_geometry_shape_t *sa,
    const box_cmd_geometry_shape_t *sb)
{
    const box_cmd_geometry_tri_t *tris = ecs_vec_first(&c->tris);
    box_cmd_geometry_finding_t f = { .gap = INFINITY };
    vec3 min, max;
    int32_t i, j, na, nb;
    bool alike = box_cmd_geometry_alike(c->world, sa->e, sb->e);
    glm_vec3_maxv((float*)sa->min, (float*)sb->min, min);
    glm_vec3_minv((float*)sa->max, (float*)sb->max, max);

    box_cmd_geometry_tris(c, sa, min, max, &c->list_a, &c->box_a);
    box_cmd_geometry_tris(c, sb, min, max, &c->list_b, &c->box_b);
    const int32_t *la = ecs_vec_first(&c->list_a), *lb = ecs_vec_first(&c->list_b);
    const box_cmd_geometry_box_t *ba = ecs_vec_first(&c->box_a);
    const box_cmd_geometry_box_t *bb = ecs_vec_first(&c->box_b);
    na = ecs_vec_count(&c->list_a);
    nb = ecs_vec_count(&c->list_b);
    for (i = 0; i < na; i ++) {
        const box_cmd_geometry_tri_t *t1 = &tris[la[i]];
        for (j = 0; j < nb; j ++) {
            const box_cmd_geometry_tri_t *t2 = &tris[lb[j]];
            vec3 at;
            float gap = 0;
            if (glm_vec3_dot((float*)t1->n, (float*)t2->n) < BOX_CMD_GEOMETRY_PARALLEL ||
                !box_cmd_geometry_overlap(ba[i].min, ba[i].max, bb[j].min, bb[j].max,
                    c->tolerance))
            {
                continue;
            }

            float area = box_cmd_geometry_face(c, t1, t2, at, &gap);
            if (area <= 0) {
                continue;
            }

            bool same = alike && t1->flat && t2->flat;
            if (same || box_cmd_geometry_covered(c, sa->e, sb->e, at, t1->n)) {
                if (same) {
                    f.alike += area;
                } else {
                    f.hidden += area;
                }

                if (!f.area && !f.best) {
                    glm_vec3_copy(at, f.at);
                    glm_vec3_copy((float*)t1->n, f.normal);
                }

                continue;
            }

            if (!f.area) {
                glm_vec3_zero(f.at);
            }

            f.area += area;
            f.gap = glm_min(f.gap, gap);
            glm_vec3_muladds(at, area, f.at);
            if (area > f.best) {
                f.best = area;
                glm_vec3_copy((float*)t1->n, f.normal);
            }
        }
    }

    if ((f.area > 0 && f.area >= c->area) || (f.hidden + f.alike > 0 &&
        f.hidden + f.alike >= c->area))
    {
        if (f.area > 0) {
            glm_vec3_scale(f.at, 1.0f / f.area, f.at);
        }

        f.gap = isfinite(f.gap) ? f.gap : 0;
        f.a = sa->e;
        f.b = sb->e;
        *ecs_vec_append_t(NULL, &c->findings, box_cmd_geometry_finding_t) = f;
    }
}

/** Order shapes by the minimum of their bounds on box_cmd_geometry_axis.
 *
 * @param a A shape
 * @param b Another shape
 * @return Negative when a comes first
 */
static int box_cmd_geometry_shape_cmp(
    const void *a,
    const void *b)
{
    float ma = ((const box_cmd_geometry_shape_t*)a)->min[box_cmd_geometry_axis];
    float mb = ((const box_cmd_geometry_shape_t*)b)->min[box_cmd_geometry_axis];

    return (ma > mb) - (ma < mb);
}

/** Order findings by the area that can be seen, then by the hidden area, the largest first.
 *
 * @param a A finding
 * @param b Another finding
 * @return Negative when a comes first
 */
static int box_cmd_geometry_finding_cmp(
    const void *a,
    const void *b)
{
    const box_cmd_geometry_finding_t *fa = a, *fb = b;
    int result = (fa->area < fb->area) - (fa->area > fb->area);
    if (!result) {
        float ha = fa->hidden + fa->alike, hb = fb->hidden + fb->alike;
        result = (ha < hb) - (ha > hb);
    }

    return result;
}

/** Check every pair of shapes whose bounds overlap: sort the shapes along the axis on which
 * their centers spread most and sweep along it.
 *
 * @param c The check
 */
static void box_cmd_geometry_sweep(
    box_cmd_geometry_check_t *c)
{
    box_cmd_geometry_shape_t *shapes = ecs_vec_first(&c->shapes);
    int32_t i, j, axis = 0, count = ecs_vec_count(&c->shapes);
    vec3 lo = {INFINITY, INFINITY, INFINITY}, hi = {-INFINITY, -INFINITY, -INFINITY};
    for (i = 0; i < count; i ++) {
        vec3 center;
        glm_vec3_center(shapes[i].min, shapes[i].max, center);
        glm_vec3_minv(lo, center, lo);
        glm_vec3_maxv(hi, center, hi);
    }

    for (i = 1; i < 3; i ++) {
        if (hi[i] - lo[i] > hi[axis] - lo[axis]) {
            axis = i;
        }
    }

    box_cmd_geometry_axis = axis;
    if (count > 1) {
        qsort(shapes, (size_t)count, sizeof(box_cmd_geometry_shape_t),
            box_cmd_geometry_shape_cmp);
    }

    for (i = 0; i < count; i ++) {
        for (j = i + 1; j < count; j ++) {
            if (shapes[j].min[axis] > shapes[i].max[axis] + c->tolerance) {
                break;
            }

            if (box_cmd_geometry_overlap(shapes[i].min, shapes[i].max, shapes[j].min,
                shapes[j].max, c->tolerance))
            {
                box_cmd_geometry_pair(c, &shapes[i], &shapes[j]);
            }
        }
    }
}

/** Parse an option key=number.
 *
 * @param arg The argument
 * @param key The key
 * @param out Receives the number
 * @return 0 when the argument is the option, 1 when it is another argument, -1 when the number
 * is invalid or negative
 */
static int box_cmd_geometry_number(
    const char *arg,
    const char *key,
    double *out)
{
    int32_t len = ecs_os_strlen(key);
    char *end = NULL;
    int result = 1;
    if (ecs_os_strncmp(arg, key, len) || arg[len] != '=') {
        goto done;
    }

    *out = strtod(arg + len + 1, &end);
    result = (!end || end == arg + len + 1 || end[0] || !isfinite(*out) || *out < 0) ? -1 : 0;
done:
    return result;
}

/** Parse the arguments of geometry.check: the root and the options in any order.
 *
 * @param c The check
 * @param argc Number of arguments
 * @param argv The arguments
 * @return 0 on success, nonzero with an error logged
 */
static int box_cmd_geometry_args(
    box_cmd_geometry_check_t *c,
    int32_t argc,
    const char **argv)
{
    int32_t i;
    int result = -1;
    for (i = 0; i < argc; i ++) {
        const char *arg = argv[i] ? argv[i] : "";
        double value = 0;
        int r = 0;
        if (!arg[0]) {
            continue;
        }

        if ((r = box_cmd_geometry_number(arg, "tolerance", &value)) != 1) {
            c->tolerance = (float)value;
        } else if ((r = box_cmd_geometry_number(arg, "area", &value)) != 1) {
            c->area = (float)value;
        } else if ((r = box_cmd_geometry_number(arg, "max", &value)) != 1) {
            c->max = (int32_t)value;
        } else if (!ecs_os_strcmp(arg, "hidden")) {
            c->list_hidden = true;
        } else if (strchr(arg, '=')) {
            ecs_err("invalid option '%s', expected tolerance=, area=, max= or hidden", arg);
            goto done;
        } else if (c->root) {
            ecs_err("invalid argument '%s', the root is already set", arg);
            goto done;
        } else {
            r = 0;
            c->root = ecs_lookup_path_w_sep(c->world, box_command_scope(c->world), arg, ".",
                NULL, true);
            if (!c->root) {
                ecs_err("unknown entity '%s'", arg);
                goto done;
            }
        }

        if (r < 0) {
            ecs_err("invalid value in '%s', expected a number of 0 or more", arg);
            goto done;
        }
    }

    result = 0;
done:
    return result;
}

/** Free a check.
 *
 * @param c The check
 */
static void box_cmd_geometry_fini(
    box_cmd_geometry_check_t *c)
{
    ecs_vec_fini_t(NULL, &c->tris, box_cmd_geometry_tri_t);
    ecs_vec_fini_t(NULL, &c->shapes, box_cmd_geometry_shape_t);
    ecs_vec_fini_t(NULL, &c->findings, box_cmd_geometry_finding_t);
    ecs_vec_fini_t(NULL, &c->list_a, int32_t);
    ecs_vec_fini_t(NULL, &c->list_b, int32_t);
    ecs_vec_fini_t(NULL, &c->box_a, box_cmd_geometry_box_t);
    ecs_vec_fini_t(NULL, &c->box_b, box_cmd_geometry_box_t);
}

/** Get a value to print with two decimals, 0 when it rounds to zero so it does not print as
 * -0.00.
 *
 * @param value The value
 * @return The value to print
 */
static double box_cmd_geometry_print(
    float value)
{
    return fabsf(value) < 0.005f ? 0.0 : (double)value;
}

/** Append one finding.
 *
 * @param c The check
 * @param f The finding
 * @param visible Whether its faces can be seen fighting
 * @param out The output
 */
static void box_cmd_geometry_line(
    const box_cmd_geometry_check_t *c,
    const box_cmd_geometry_finding_t *f,
    bool visible,
    ecs_strbuf_t *out)
{
    char *pa = ecs_get_path(c->world, f->a), *pb = ecs_get_path(c->world, f->b);
    if (strcmp(pa, pb) > 0) {
        char *swap = pa;
        pa = pb;
        pb = swap;
    }

    if (visible) {
        ecs_strbuf_append(out, "warning coplanar %s and %s: area %.4f, gap %.5f, ", pa, pb,
            (double)f->area, (double)f->gap);
    } else if (f->hidden >= f->alike) {
        ecs_strbuf_append(out, "info coplanar-hidden %s and %s: area %.4f inside other shapes, ",
            pa, pb, (double)(f->hidden + f->alike));
    } else {
        ecs_strbuf_append(out, "info coplanar-alike %s and %s: area %.4f of flat faces that look "
            "alike, ", pa, pb, (double)(f->hidden + f->alike));
    }

    ecs_strbuf_append(out, "facing (%.2f, %.2f, %.2f) at (%.2f, %.2f, %.2f)\n",
        box_cmd_geometry_print(f->normal[0]), box_cmd_geometry_print(f->normal[1]),
        box_cmd_geometry_print(f->normal[2]), box_cmd_geometry_print(f->at[0]),
        box_cmd_geometry_print(f->at[1]), box_cmd_geometry_print(f->at[2]));
    ecs_os_free(pa);
    ecs_os_free(pb);
}

/** Append the findings whose faces can be seen, the largest area first, then with the hidden
 * option the ones whose faces fight only where it cannot be seen, and a summary line.
 *
 * @param c The check
 * @param out The output
 */
static void box_cmd_geometry_report(
    box_cmd_geometry_check_t *c,
    ecs_strbuf_t *out)
{
    box_cmd_geometry_finding_t *f = ecs_vec_first(&c->findings);
    int32_t i, count = ecs_vec_count(&c->findings), visible = 0, listed = 0;
    if (count > 1) {
        qsort(f, (size_t)count, sizeof(box_cmd_geometry_finding_t),
            box_cmd_geometry_finding_cmp);
    }

    for (i = 0; i < count; i ++) {
        visible += f[i].area > 0 && f[i].area >= c->area;
    }

    for (i = 0; i < count; i ++) {
        bool seen = i < visible;
        if ((seen || c->list_hidden) && listed < c->max) {
            box_cmd_geometry_line(c, &f[i], seen, out);
            listed ++;
        }
    }

    int32_t shown = c->list_hidden ? count : visible;
    if (shown > listed) {
        ecs_strbuf_append(out, "warning coplanar: %d more like the ones above\n",
            shown - listed);
    }

    if (visible) {
        ecs_strbuf_append(out, "%d coplanar pair%s", visible, visible == 1 ? "" : "s");
    } else {
        ecs_strbuf_appendstr(out, "no problems");
    }

    ecs_strbuf_append(out, " in %d shapes, %d triangles", ecs_vec_count(&c->shapes),
        ecs_vec_count(&c->tris));
    if (count > visible) {
        int32_t hidden = count - visible;
        ecs_strbuf_append(out, "; %d pair%s only where it cannot be seen", hidden,
            hidden == 1 ? " fights" : "s fight");
    }

    if (c->unplaced) {
        ecs_strbuf_append(out, "; %d shapes without a WorldTransform are not drawn and not "
            "checked", c->unplaced);
    }
}

char* box_cmd_geometry_check(
    ecs_world_t *world,
    int32_t argc,
    const char **argv)
{
    box_cmd_geometry_check_t c = {
        .world = world,
        .tolerance = BOX_CMD_GEOMETRY_TOLERANCE,
        .area = BOX_CMD_GEOMETRY_AREA,
        .max = BOX_CMD_GEOMETRY_MAX
    };
    ecs_strbuf_t out = ECS_STRBUF_INIT;
    char *result = NULL;
    if (box_cmd_geometry_args(&c, argc, argv)) {
        goto done;
    }

    box_cmd_geometry_collect(&c);
    box_cmd_geometry_sweep(&c);
    box_cmd_geometry_report(&c, &out);
    result = ecs_strbuf_get(&out);
done:
    box_cmd_geometry_fini(&c);

    return result;
}

/** The geometry command: print the number of 3D and 2D shapes, the triangles of the 3D shapes
 * and the meshes in the cache.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the text
 */
static void box_cmd_geometry_status(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    int64_t shapes = 0, tris = 0, flat = 0;
    ecs_iter_t it = ecs_each(world, ShapeMesh);
    while (ecs_each_next(&it)) {
        const ShapeMesh *s = ecs_field(&it, ShapeMesh, 0);
        int32_t i;
        if (ecs_table_has_id(world, it.table, EcsPrefab) ||
            ecs_table_has_id(world, it.table, EcsDisabled))
        {
            continue;
        }

        if (ecs_table_has_id(world, it.table, ecs_id(Shape2D))) {
            flat += it.count;
            continue;
        }

        for (i = 0; i < it.count; i ++) {
            const box_mesh_t *mesh = cache ? box_cmd_geometry_mesh(cache, &s[i]) : NULL;
            shapes ++;
            tris += mesh ? mesh->index_count / 3 : 0;
        }
    }

    *(char**)result->ptr = flecs_asprintf(
        "%lld 3D shapes, %lld triangles, %lld 2D shapes, %d meshes in the cache",
        (long long)shapes, (long long)tris, (long long)flat,
        cache ? box_mesh_cache_count(cache) : 0);
}

/** The geometry.check command: list the pairs of shapes with faces that z-fight.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The root and the options
 * @param result Receives the report
 */
static void box_cmd_geometry_check_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *args[BOX_CMD_GEOMETRY_OPTIONS_MAX];
    int32_t i, count = 0;
    for (i = 0; i < argc && count < BOX_CMD_GEOMETRY_OPTIONS_MAX; i ++) {
        args[count ++] = box_cmd_geometry_arg(argc, argv, i);
    }

    *(char**)result->ptr = box_cmd_geometry_check(ctx->world, count, args);
}

void box_cmd_geometry_init(
    ecs_world_t *world)
{
    int32_t i;
    ecs_entity_t geometry = box_command_register(world,
        "Print the number of shapes, triangles and cached meshes; the geometry.* commands "
        "inspect them",
        (ecs_function_desc_t){
            .name = "geometry",
            .return_type = ecs_id(ecs_string_t),
            .callback = box_cmd_geometry_status
        });

    ecs_function_desc_t check = {
        .name = "check",
        .parent = geometry,
        .return_type = ecs_id(ecs_string_t),
        .callback = box_cmd_geometry_check_cmd
    };

    for (i = 0; i < BOX_CMD_GEOMETRY_OPTIONS_MAX; i ++) {
        check.params[i] = (ecs_script_parameter_t){
            .name = "options", .type = ecs_id(ecs_string_t), .optional = true,
            .brief = "Entity whose descendants to check (default: the world), tolerance=units "
                "(default 0.0001), area=units^2 (default 0.0001), max=findings (default 100), "
                "hidden to also list the faces that fight where it cannot be seen" };
    }

    box_command_register(world,
        "List pairs of 3D shapes with faces that lie in the same plane, face the same way "
        "and overlap, which z-fight", check);
}
