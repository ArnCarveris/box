/**
 * @file geometry/mesh_gen_solid.c
 * @brief Generators of the icosphere, the rounded and the beveled box, and the conversion of
 * authored meshes.
 */

#include "mesh_cache.h"
#include "mesh_gen.h"
#include <math.h>

/** Corners of the icosahedron before normalization */
static const float box_ico_vertices[12][3] = {
    { -1.0f, 1.618034f, 0.0f }, { 1.0f, 1.618034f, 0.0f },
    { -1.0f, -1.618034f, 0.0f }, { 1.0f, -1.618034f, 0.0f },
    { 0.0f, -1.0f, 1.618034f }, { 0.0f, 1.0f, 1.618034f },
    { 0.0f, -1.0f, -1.618034f }, { 0.0f, 1.0f, -1.618034f },
    { 1.618034f, 0.0f, -1.0f }, { 1.618034f, 0.0f, 1.0f },
    { -1.618034f, 0.0f, -1.0f }, { -1.618034f, 0.0f, 1.0f }
};

/** Faces of the icosahedron */
static const uint32_t box_ico_faces[20][3] = {
    { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
    { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
    { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
    { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 }
};

/** Get or create the normalized midpoint of an edge.
 *
 * @param edges Map from edge to midpoint index
 * @param positions Vertex positions
 * @param count Number of vertices, incremented when a midpoint is added
 * @param a First vertex of the edge
 * @param b Second vertex of the edge
 * @return Index of the midpoint
 */
static uint32_t box_ico_midpoint(
    ecs_map_t *edges,
    vec3 *positions,
    uint32_t *count,
    uint32_t a,
    uint32_t b)
{
    uint64_t key = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
    ecs_map_val_t *existing = ecs_map_get(edges, key);
    uint32_t result;
    if (existing) {
        result = (uint32_t)*existing;
    } else {
        result = (*count) ++;
        glm_vec3_add(positions[a], positions[b], positions[result]);
        glm_vec3_normalize(positions[result]);
        ecs_map_insert(edges, key, result);
    }

    return result;
}

void box_mesh_gen_icosphere(
    box_mesh_t *mesh,
    int32_t level,
    bool smooth)
{
    int32_t faces = 20 << (2 * level);
    int32_t verts = 10 * (1 << (2 * level)) + 2;
    vec3 *positions = ecs_os_malloc_n(vec3, verts);
    uint32_t *tris = ecs_os_malloc_n(uint32_t, faces * 3);
    uint32_t *next = ecs_os_malloc_n(uint32_t, faces * 3);
    uint32_t count = 12, tri_count = 20;
    for (int32_t i = 0; i < 12; i ++) {
        glm_vec3_normalize_to((float*)box_ico_vertices[i], positions[i]);
    }

    ecs_os_memcpy(tris, box_ico_faces, ECS_SIZEOF(box_ico_faces));
    ecs_map_t edges;
    ecs_map_init(&edges, NULL);
    for (int32_t l = 0; l < level; l ++) {
        for (uint32_t t = 0; t < tri_count; t ++) {
            uint32_t *f = &tris[t * 3];
            uint32_t ab = box_ico_midpoint(&edges, positions, &count, f[0], f[1]);
            uint32_t bc = box_ico_midpoint(&edges, positions, &count, f[1], f[2]);
            uint32_t ca = box_ico_midpoint(&edges, positions, &count, f[2], f[0]);
            uint32_t sub[12] = { f[0], ab, ca, f[1], bc, ab, f[2], ca, bc, ab, bc, ca };
            ecs_os_memcpy(&next[t * 12], sub, ECS_SIZEOF(sub));
        }

        tri_count *= 4;
        uint32_t *tmp = tris;
        tris = next;
        next = tmp;
    }

    for (uint32_t i = 0; i < count; i ++) {
        const float *p = positions[i];
        float y = glm_clamp(p[1], -1.0f, 1.0f);
        float u = 0.5f + atan2f(p[2], p[0]) / (2.0f * BOX_PI);
        box_mesh_vertex(mesh, p, p, glm_clamp(u, 0.0f, 1.0f), acosf(y) / BOX_PI);
    }

    for (uint32_t t = 0; t < tri_count; t ++) {
        box_mesh_triangle(mesh, (int32_t)tris[t * 3], (int32_t)tris[t * 3 + 1],
            (int32_t)tris[t * 3 + 2]);
    }

    ecs_map_fini(&edges);
    ecs_os_free(positions);
    ecs_os_free(tris);
    ecs_os_free(next);
    if (!smooth) {
        box_mesh_facet(mesh);
    }
}

/** Get the angle and the side of a grid line of a rounded box face.
 *
 * @param index The grid line, 0 to 2 * half + 1
 * @param half Grid lines per half face minus one
 * @param side Receives -1 for the lower half of the face and 1 for the upper half
 * @return Tangent of the angle between the line and the face normal
 */
static float box_rounded_line(
    int32_t index,
    int32_t half,
    float *side)
{
    float angle;
    if (index <= half) {
        *side = -1.0f;
        angle = -0.25f * BOX_PI * (float)(half - index) / (float)half;
    } else {
        *side = 1.0f;
        angle = 0.25f * BOX_PI * (float)(index - half - 1) / (float)half;
    }

    return tanf(angle);
}

/** Append one face of a rounded box.
 *
 * @param mesh The mesh
 * @param h Half sizes
 * @param r Corner radius
 * @param k Axis of the face normal
 * @param sign Direction of the face normal, 1 or -1
 * @param half Grid lines per half face minus one
 */
static void box_rounded_face(
    box_mesh_t *mesh,
    const vec3 h,
    float r,
    int32_t k,
    float sign,
    int32_t half)
{
    int32_t u = (k + 1) % 3, v = (k + 2) % 3;
    int32_t lines = 2 * half + 2;
    int32_t base = mesh->vertex_count;
    for (int32_t a = 0; a < lines; a ++) {
        for (int32_t b = 0; b < lines; b ++) {
            float side_a, side_b;
            vec3 dir, pos;
            dir[k] = sign;
            dir[u] = box_rounded_line(a, half, &side_a);
            dir[v] = box_rounded_line(b, half, &side_b);
            glm_vec3_normalize(dir);
            pos[k] = sign * (h[k] - r) + r * dir[k];
            pos[u] = side_a * (h[u] - r) + r * dir[u];
            pos[v] = side_b * (h[v] - r) + r * dir[v];
            vec2 uv;
            box_mesh_box_face_uv(k, sign, pos, h, uv);
            box_mesh_vertex(mesh, pos, dir, uv[0], uv[1]);
        }
    }

    bool flat_u = h[u] - r <= 1e-6f, flat_v = h[v] - r <= 1e-6f;
    for (int32_t a = 0; a < lines - 1; a ++) {
        for (int32_t b = 0; b < lines - 1; b ++) {
            if ((flat_u && a == half) || (flat_v && b == half)) {
                continue;
            }

            int32_t i0 = base + a * lines + b;
            int32_t i1 = i0 + lines;
            box_mesh_triangle(mesh, i0, i1, i1 + 1);
            box_mesh_triangle(mesh, i0, i1 + 1, i0 + 1);
        }
    }
}

void box_mesh_gen_rounded_box(
    box_mesh_t *mesh,
    const vec3 size,
    float radius,
    int32_t segments)
{
    if (radius <= 0.0f) {
        box_mesh_gen_box(mesh);
        for (int32_t i = 0; i < mesh->vertex_count; i ++) {
            glm_vec3_mul(mesh->vertices[i].position, (float*)size, mesh->vertices[i].position);
        }
    } else {
        int32_t half = (segments + 1) / 2 < 1 ? 1 : (segments + 1) / 2;
        vec3 h;
        glm_vec3_scale((float*)size, 0.5f, h);
        for (int32_t k = 0; k < 3; k ++) {
            box_rounded_face(mesh, h, radius, k, 1.0f, half);
            box_rounded_face(mesh, h, radius, k, -1.0f, half);
        }
    }
}

/** Largest number of steps per rounded edge of a bevel */
#define BOX_BEVEL_MAX_STEPS (128)

/** Share of the largest side that stays flat between two corners whose bevels would otherwise
 * take up the whole side between them. A flat part never collapses, also on a thin side. */
#define BOX_BEVEL_GAP (0.0005f)

/** Distance below which two vertices of a bevel are the same point */
#define BOX_BEVEL_EPSILON (1e-6f)

/** Member index of the Bevel radius of an edge by the axis along the edge and the sides of the
 * other two axes, the lower axis first, 1 for the positive side */
static const int32_t box_bevel_edge_member[3][2][2] = {
    { { 5, 4 }, { 1, 0 } },   /* X: bottom_back, bottom_front, top_back, top_front */
    { { 10, 8 }, { 11, 9 } }, /* Y: back_left, front_left, back_right, front_right */
    { { 6, 2 }, { 7, 3 } }    /* Z: bottom_left, top_left, bottom_right, top_right */
};

/** Corner of a beveled box, where three edges meet.
 *
 * The edge with the largest radius, along m, runs into the corner as a cylinder; the corner
 * sweeps a rounded rim around its end, from the face p to the face q, onto the cap face m.
 */
typedef struct box_bevel_corner_t {
    vec3 sign;  /**< Side of the corner along each axis, 1 or -1 */
    int32_t m;  /**< Axis of the edge with the largest radius and of the cap face */
    int32_t p;  /**< Axis of the face where the sweep starts */
    int32_t q;  /**< Axis of the face where the sweep ends */
    float r;    /**< Radius of the edge along m */
    float a;    /**< Radius of the edge between the faces m and p, which runs along q */
    float b;    /**< Radius of the edge between the faces m and q, which runs along p */
} box_bevel_corner_t;

/** Beveled box being generated */
typedef struct box_bevel_t {
    box_mesh_t *mesh;                         /**< Mesh to fill */
    vec3 h;                                   /**< Half sizes */
    box_bevel_corner_t corners[8];            /**< Corners, bit k set for the + side of axis k */
    int32_t steps;                            /**< Steps per rounded edge, even */
    float cos[BOX_BEVEL_MAX_STEPS + 1];       /**< Cosine of each step of a quarter turn */
    float sin[BOX_BEVEL_MAX_STEPS + 1];       /**< Sine of each step of a quarter turn */
} box_bevel_t;

/** Get the radius of an edge that ends in a corner.
 *
 * @param radii Radius per Bevel member
 * @param axis Axis along the edge
 * @param corner Index of the corner
 * @return The radius
 */
static float box_bevel_edge(
    const float *radii,
    int32_t axis,
    int32_t corner)
{
    int32_t lo = axis == 0 ? 1 : 0, hi = axis == 2 ? 1 : 2;

    return radii[box_bevel_edge_member[axis][(corner >> lo) & 1][(corner >> hi) & 1]];
}

/** Get how far the rounding of a corner reaches along an axis: the largest radius of the two
 * edges of the corner that are perpendicular to the axis. The edge along the axis takes up
 * length on the other two axes only.
 *
 * @param radii Radius per Bevel member
 * @param corner Index of the corner
 * @param axis The axis
 * @return The reach
 */
static float box_bevel_corner_reach(
    const float *radii,
    int32_t corner,
    int32_t axis)
{
    return glm_max(box_bevel_edge(radii, (axis + 1) % 3, corner),
        box_bevel_edge(radii, (axis + 2) % 3, corner));
}

/** Get a point of the rounded part of a corner.
 *
 * The corner is a grid over the sweep angle i, from the face p to the face q, and the rim angle
 * j, from the cylinder of the edge along m to the cap face m. A rim with a radius of 0 collapses.
 *
 * Row j = 0 is the end of the edge along m, column i = 0 the end of the edge along q, column
 * i = steps the end of the edge along p and row j = steps the rounded corner of the cap face.
 *
 * @param bevel The bevel
 * @param c The corner
 * @param i Step of the sweep angle
 * @param j Step of the rim angle
 * @param pos The position to fill
 * @param normal The normal to fill
 */
static void box_bevel_point(
    const box_bevel_t *bevel,
    const box_bevel_corner_t *c,
    int32_t i,
    int32_t j,
    vec3 pos,
    vec3 normal)
{
    float cp = bevel->cos[i], sp = bevel->sin[i];
    float cr = bevel->cos[j], sr = bevel->sin[j];
    float da = c->a * cp, db = c->b * sp;
    float rise = c->a;
    if (i == bevel->steps) {
        rise = c->b;
    } else if (i) {
        rise = sqrtf(da * da + db * db);
    }

    vec3 local, n;
    local[c->p] = bevel->h[c->p] - c->r + cp * (c->r - c->a * (1.0f - cr));
    local[c->q] = bevel->h[c->q] - c->r + sp * (c->r - c->b * (1.0f - cr));
    local[c->m] = bevel->h[c->m] - rise * (1.0f - sr);
    n[c->p] = cr * cp;
    n[c->q] = cr * sp;
    n[c->m] = sr;
    for (int32_t k = 0; k < 3; k ++) {
        pos[k] = local[k] * c->sign[k];
        normal[k] = n[k] * c->sign[k];
    }
}

/** Get a point of the end of an edge strip at a corner.
 *
 * Steps run from the face of axis (axis + 1) % 3 to the face of axis (axis + 2) % 3.
 *
 * @param bevel The bevel
 * @param corner Index of the corner
 * @param axis Axis along the edge
 * @param t Step around the edge
 * @param pos The position to fill
 * @param normal The normal to fill
 */
static void box_bevel_ring(
    const box_bevel_t *bevel,
    int32_t corner,
    int32_t axis,
    int32_t t,
    vec3 pos,
    vec3 normal)
{
    const box_bevel_corner_t *c = &bevel->corners[corner];
    if (c->m == axis) {
        box_bevel_point(bevel, c, t, 0, pos, normal);
    } else if (c->q == axis) {
        box_bevel_point(bevel, c, 0, bevel->steps - t, pos, normal);
    } else {
        box_bevel_point(bevel, c, bevel->steps, t, pos, normal);
    }
}

/** Append a vertex with the texture coordinates of a RoundedBox face.
 *
 * @param bevel The bevel
 * @param pos The position
 * @param normal The normal
 * @param face Axis of the face whose projection gives the texture coordinates
 * @param sign Side of that face, 1 or -1
 * @return The index of the vertex
 */
static int32_t box_bevel_vertex(
    box_bevel_t *bevel,
    const vec3 pos,
    const vec3 normal,
    int32_t face,
    float sign)
{
    vec2 uv;
    box_mesh_box_face_uv(face, sign, pos, bevel->h, uv);

    return box_mesh_vertex(bevel->mesh, pos, normal, uv[0], uv[1]);
}

/** Check whether two vertices of a bevel are the same point.
 *
 * @param a The first position
 * @param b The second position
 * @return True when they are closer than BOX_BEVEL_EPSILON along every axis
 */
static bool box_bevel_same(
    const float *a,
    const float *b)
{
    return fabsf(a[0] - b[0]) <= BOX_BEVEL_EPSILON && fabsf(a[1] - b[1]) <= BOX_BEVEL_EPSILON &&
        fabsf(a[2] - b[2]) <= BOX_BEVEL_EPSILON;
}

/** Append a triangle unless it collapsed because a radius or a side is 0.
 *
 * @param mesh The mesh
 * @param a First vertex index
 * @param b Second vertex index
 * @param c Third vertex index
 */
static void box_bevel_triangle(
    box_mesh_t *mesh,
    int32_t a,
    int32_t b,
    int32_t c)
{
    const float *pa = mesh->vertices[a].position;
    const float *pb = mesh->vertices[b].position;
    const float *pc = mesh->vertices[c].position;
    if (!box_bevel_same(pa, pb) && !box_bevel_same(pb, pc) && !box_bevel_same(pc, pa)) {
        box_mesh_triangle(mesh, a, b, c);
    }
}

/** Get the face normal of a triangle of a bevel, not normalized.
 *
 * @param mesh The mesh
 * @param a First vertex index
 * @param b Second vertex index
 * @param c Third vertex index
 * @param face The normal to fill, with the length of twice the area
 */
static void box_bevel_face_normal(
    const box_mesh_t *mesh,
    int32_t a,
    int32_t b,
    int32_t c,
    vec3 face)
{
    vec3 ab, ac;
    glm_vec3_sub(mesh->vertices[b].position, mesh->vertices[a].position, ab);
    glm_vec3_sub(mesh->vertices[c].position, mesh->vertices[a].position, ac);
    glm_vec3_cross(ab, ac, face);
}

/** Get whether the quads of a grid wind against the normals of their vertices.
 *
 * The winding is the same for every quad of a grid; summing over the grid keeps it reliable
 * where a corner pinches and a single triangle stands across the vertex normals.
 *
 * @param mesh The mesh
 * @param base Index of the first vertex
 * @param rows Number of rows
 * @param cols Number of vertices per row
 * @return -1 when the quads wind clockwise seen from outside, else 1
 */
static float box_bevel_winding(
    const box_mesh_t *mesh,
    int32_t base,
    int32_t rows,
    int32_t cols)
{
    float sum = 0.0f;
    for (int32_t r = 0; r < rows - 1; r ++) {
        for (int32_t c = 0; c < cols - 1; c ++) {
            int32_t i0 = base + r * cols + c;
            int32_t i1 = i0 + cols;
            vec3 face, other, normal;
            box_bevel_face_normal(mesh, i0, i1, i1 + 1, face);
            box_bevel_face_normal(mesh, i0, i1 + 1, i0 + 1, other);
            glm_vec3_add(face, other, face);
            glm_vec3_add(mesh->vertices[i0].normal, mesh->vertices[i1 + 1].normal, normal);
            sum += glm_vec3_dot(face, normal);
        }
    }

    return sum < 0.0f ? -1.0f : 1.0f;
}

/** Check whether the diagonal from the first to the third vertex of a quad folds it inward.
 *
 * The beveled box is convex, so the fourth vertex must not lie in front of the triangle of the
 * first three. A quad whose second and fourth vertex are the same point folds as well, into
 * two triangles back to back.
 *
 * @param mesh The mesh
 * @param winding Winding of the quad, from box_bevel_winding
 * @param a First vertex index
 * @param b Second vertex index
 * @param c Third vertex index
 * @param d Fourth vertex index
 * @return True when the quad must be split along the diagonal from b to d
 */
static bool box_bevel_concave(
    const box_mesh_t *mesh,
    float winding,
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d)
{
    const float *pa = mesh->vertices[a].position;
    const float *pd = mesh->vertices[d].position;
    bool result = box_bevel_same(mesh->vertices[b].position, pd);
    if (!result && !box_bevel_same(pa, mesh->vertices[c].position)) {
        vec3 face, ad;
        box_bevel_face_normal(mesh, a, b, c, face);
        glm_vec3_sub((float*)pd, (float*)pa, ad);
        result = winding * glm_vec3_dot(face, ad) >
            1e-4f * glm_vec3_norm(face) * glm_vec3_norm(ad);
    }

    return result;
}

/** Append the quads of a grid of vertices, rows of cols vertices.
 *
 * Each quad is split along the diagonal that keeps it convex. Where a corner pinches, as at
 * the end of a degenerate cap ellipse, the other diagonal gives a sliver across the surface.
 *
 * @param mesh The mesh
 * @param base Index of the first vertex
 * @param rows Number of rows
 * @param cols Number of vertices per row
 */
static void box_bevel_grid(
    box_mesh_t *mesh,
    int32_t base,
    int32_t rows,
    int32_t cols)
{
    float winding = box_bevel_winding(mesh, base, rows, cols);
    for (int32_t r = 0; r < rows - 1; r ++) {
        for (int32_t c = 0; c < cols - 1; c ++) {
            int32_t i0 = base + r * cols + c;
            int32_t i1 = i0 + cols;
            if (box_bevel_concave(mesh, winding, i0, i1, i1 + 1, i0 + 1)) {
                box_bevel_triangle(mesh, i0, i1, i0 + 1);
                box_bevel_triangle(mesh, i1, i1 + 1, i0 + 1);
            } else {
                box_bevel_triangle(mesh, i0, i1, i1 + 1);
                box_bevel_triangle(mesh, i0, i1 + 1, i0 + 1);
            }
        }
    }
}

/** Append part of the rounded corner, textured as one face.
 *
 * @param bevel The bevel
 * @param c The corner
 * @param i0 First step of the sweep angle
 * @param i1 Last step of the sweep angle
 * @param j0 First step of the rim angle
 * @param j1 Last step of the rim angle
 * @param face Axis of the face whose projection gives the texture coordinates
 */
static void box_bevel_patch(
    box_bevel_t *bevel,
    const box_bevel_corner_t *c,
    int32_t i0,
    int32_t i1,
    int32_t j0,
    int32_t j1,
    int32_t face)
{
    int32_t base = bevel->mesh->vertex_count;
    for (int32_t i = i0; i <= i1; i ++) {
        for (int32_t j = j0; j <= j1; j ++) {
            vec3 pos, normal;
            box_bevel_point(bevel, c, i, j, pos, normal);
            box_bevel_vertex(bevel, pos, normal, face, c->sign[face]);
        }
    }

    box_bevel_grid(bevel->mesh, base, i1 - i0 + 1, j1 - j0 + 1);
}

/** Append part of an edge strip between two corners, textured as one face.
 *
 * @param bevel The bevel
 * @param axis Axis along the edge
 * @param c0 Index of the corner on the negative side of the axis
 * @param c1 Index of the corner on the positive side of the axis
 * @param t0 First step around the edge
 * @param t1 Last step around the edge
 * @param face Axis of the face whose projection gives the texture coordinates
 */
static void box_bevel_strip(
    box_bevel_t *bevel,
    int32_t axis,
    int32_t c0,
    int32_t c1,
    int32_t t0,
    int32_t t1,
    int32_t face)
{
    int32_t base = bevel->mesh->vertex_count;
    float sign = bevel->corners[c0].sign[face];
    for (int32_t t = t0; t <= t1; t ++) {
        vec3 pos, normal;
        box_bevel_ring(bevel, c0, axis, t, pos, normal);
        box_bevel_vertex(bevel, pos, normal, face, sign);
        box_bevel_ring(bevel, c1, axis, t, pos, normal);
        box_bevel_vertex(bevel, pos, normal, face, sign);
    }

    box_bevel_grid(bevel->mesh, base, t1 - t0 + 1, 2);
}

/** Append the flat part of a face: the face inset by its edges, with the corners where it is
 * the cap face rounded. The polygon is convex, so it is a fan around its centroid.
 *
 * @param bevel The bevel
 * @param k Axis of the face
 * @param side 1 for the face on the positive side of the axis, 0 for the negative side
 */
static void box_bevel_face(
    box_bevel_t *bevel,
    int32_t k,
    int32_t side)
{
    static const int32_t order[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int32_t u = (k + 1) % 3, v = (k + 2) % 3, steps = bevel->steps;
    vec3 *points = ecs_os_malloc_n(vec3, 4 * (steps + 1));
    vec3 normal = {0}, center = {0}, unused;
    int32_t count = 0;
    normal[k] = side ? 1.0f : -1.0f;
    for (int32_t n = 0; n < 4; n ++) {
        int32_t su = order[n][0], sv = order[n][1];
        int32_t corner = (side << k) | (su << u) | (sv << v);
        const box_bevel_corner_t *c = &bevel->corners[corner];
        if (c->m == k) {
            for (int32_t step = 0; step <= steps; step ++) {
                int32_t i = su == sv ? step : steps - step;
                box_bevel_point(bevel, c, i, steps, points[count ++], unused);
            }
        } else if (c->p == k) {
            box_bevel_point(bevel, c, 0, 0, points[count ++], unused);
        } else {
            box_bevel_point(bevel, c, steps, 0, points[count ++], unused);
        }
    }

    for (int32_t n = 0; n < count; n ++) {
        glm_vec3_add(center, points[n], center);
    }

    glm_vec3_scale(center, 1.0f / (float)count, center);
    int32_t hub = box_bevel_vertex(bevel, center, normal, k, normal[k]);
    for (int32_t n = 0; n < count; n ++) {
        box_bevel_vertex(bevel, points[n], normal, k, normal[k]);
    }

    for (int32_t n = 0; n < count; n ++) {
        box_bevel_triangle(bevel->mesh, hub, hub + 1 + n, hub + 1 + (n + 1) % count);
    }

    ecs_os_free(points);
}

void box_mesh_gen_bevel(
    box_mesh_t *mesh,
    const vec3 size,
    const float *radii,
    int32_t segments)
{
    box_bevel_t bevel = { .mesh = mesh };
    float r[BOX_BEVEL_EDGES], fit = 1.0f;
    int32_t half = (segments + 1) / 2 < 1 ? 1 : (segments + 1) / 2;
    bevel.steps = glm_min(2 * half, BOX_BEVEL_MAX_STEPS);
    glm_vec3_scale((float*)size, 0.5f, bevel.h);
    for (int32_t e = 0; e < BOX_BEVEL_EDGES; e ++) {
        r[e] = glm_max(radii[e], 0.0f);
    }

    float gap = BOX_BEVEL_GAP * glm_max(glm_max(size[0], size[1]), size[2]);
    for (int32_t axis = 0; axis < 3; axis ++) {
        for (int32_t c0 = 0; c0 < 8; c0 ++) {
            if (c0 & (1 << axis)) {
                continue;
            }

            float sum = box_bevel_corner_reach(r, c0, axis) +
                box_bevel_corner_reach(r, c0 | (1 << axis), axis);
            float limit = glm_max(size[axis] - gap, 0.0f);
            if (sum > limit) {
                fit = glm_min(fit, limit / sum);
            }
        }
    }

    for (int32_t e = 0; e < BOX_BEVEL_EDGES; e ++) {
        r[e] *= fit;
    }

    for (int32_t i = 0; i <= bevel.steps; i ++) {
        bevel.cos[i] = cosf(0.5f * BOX_PI * (float)i / (float)bevel.steps);
    }

    bevel.cos[0] = 1.0f;
    bevel.cos[bevel.steps] = 0.0f;
    for (int32_t i = 0; i <= bevel.steps; i ++) {
        bevel.sin[i] = bevel.cos[bevel.steps - i];
    }

    for (int32_t corner = 0; corner < 8; corner ++) {
        box_bevel_corner_t *c = &bevel.corners[corner];
        float edge[3];
        for (int32_t k = 0; k < 3; k ++) {
            c->sign[k] = (corner >> k) & 1 ? 1.0f : -1.0f;
            edge[k] = box_bevel_edge(r, k, corner);
        }

        c->m = 1;
        if (edge[0] > edge[c->m]) {
            c->m = 0;
        }

        if (edge[2] > edge[c->m]) {
            c->m = 2;
        }

        c->p = (c->m + 1) % 3;
        c->q = (c->m + 2) % 3;
        c->r = edge[c->m];
        c->a = edge[c->q];
        c->b = edge[c->p];
    }

    int32_t mid = bevel.steps / 2, steps = bevel.steps;
    for (int32_t k = 0; k < 3; k ++) {
        box_bevel_face(&bevel, k, 0);
        box_bevel_face(&bevel, k, 1);
    }

    for (int32_t axis = 0; axis < 3; axis ++) {
        for (int32_t c0 = 0; c0 < 8; c0 ++) {
            if (!(c0 & (1 << axis))) {
                int32_t c1 = c0 | (1 << axis);
                box_bevel_strip(&bevel, axis, c0, c1, 0, mid, (axis + 1) % 3);
                box_bevel_strip(&bevel, axis, c0, c1, mid, steps, (axis + 2) % 3);
            }
        }
    }

    for (int32_t corner = 0; corner < 8; corner ++) {
        const box_bevel_corner_t *c = &bevel.corners[corner];
        box_bevel_patch(&bevel, c, 0, mid, 0, mid, c->p);
        box_bevel_patch(&bevel, c, mid, steps, 0, mid, c->q);
        box_bevel_patch(&bevel, c, 0, steps, mid, steps, c->m);
    }
}

void box_mesh_gen_mesh3(
    box_mesh_t *mesh,
    const Mesh3 *src)
{
    int32_t count = ecs_vec_count(&src->vertices);
    const vec3 *positions = ecs_vec_first(&src->vertices);
    const vec3 *normals = ecs_vec_count(&src->normals) == count ?
        ecs_vec_first(&src->normals) : NULL;
    const vec2 *uvs = ecs_vec_count(&src->uvs) == count ? ecs_vec_first(&src->uvs) : NULL;
    const rgba *colors = ecs_vec_count(&src->colors) == count ?
        ecs_vec_first(&src->colors) : NULL;
    const vec4 *data0 = ecs_vec_count(&src->data0) == count ?
        ecs_vec_first(&src->data0) : NULL;
    const vec4 *data1 = ecs_vec_count(&src->data1) == count ?
        ecs_vec_first(&src->data1) : NULL;
    const vec4 *data2 = ecs_vec_count(&src->data2) == count ?
        ecs_vec_first(&src->data2) : NULL;
    const vec4 *data3 = ecs_vec_count(&src->data3) == count ?
        ecs_vec_first(&src->data3) : NULL;
    for (int32_t i = 0; i < count; i ++) {
        vec3 n = {0};
        if (normals && glm_vec3_norm2((float*)normals[i]) > 0.0f) {
            glm_vec3_normalize_to((float*)normals[i], n);
        }

        box_mesh_vertex(mesh, positions[i], n, uvs ? uvs[i][0] : 0.0f, uvs ? uvs[i][1] : 0.0f);
        if (data0 || data1) {
            box_mesh_vertex_data(mesh, i, data0 ? data0[i] : GLM_VEC4_ZERO,
                data1 ? data1[i] : GLM_VEC4_ZERO);
        }

        if (data2 || data3) {
            box_mesh_vertex_data_upper(mesh, i, data2 ? data2[i] : GLM_VEC4_ZERO,
                data3 ? data3[i] : GLM_VEC4_ZERO);
        }
    }

    if (colors && count) {
        mesh->colors = ecs_os_memdup_n(colors, rgba, count);
    }

    int32_t src_indices = ecs_vec_count(&src->indices);
    const uint32_t *indices = ecs_vec_first(&src->indices);
    int32_t total = src_indices ? src_indices - src_indices % 3 : count - count % 3;
    mesh->indices = ecs_os_malloc_n(uint32_t, total ? total : 1);
    mesh->index_capacity = total ? total : 1;
    for (int32_t t = 0; t < total; t += 3) {
        uint32_t tri[3] = { (uint32_t)t, (uint32_t)t + 1, (uint32_t)t + 2 };
        if (src_indices) {
            ecs_os_memcpy(tri, &indices[t], ECS_SIZEOF(tri));
        }

        if (tri[0] < (uint32_t)count && tri[1] < (uint32_t)count && tri[2] < (uint32_t)count) {
            ecs_os_memcpy(&mesh->indices[mesh->index_count], tri, ECS_SIZEOF(tri));
            mesh->index_count += 3;
        }
    }

    if (!normals) {
        box_mesh_facet(mesh);
    }
}
