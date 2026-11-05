/**
 * @file geometry/mesh_gen_bevel.c
 * @brief Generators of the round shapes with rounded rims and of the rounded triangle prisms.
 */

#include "mesh_cache.h"
#include "mesh_gen.h"
#include <math.h>

/** Largest number of corners of the cross section of a round shape */
#define BOX_SECTION_CORNERS (4)

/** Largest number of steps per rounded rim */
#define BOX_SECTION_STEPS (64)

/** Kind of a side of a cross section */
typedef enum box_section_side_t {
    BoxSideAxis, /**< On the axis, not a surface */
    BoxSideCap,  /**< Flat cap, projected from above */
    BoxSideWall  /**< Wall, wrapped around the axis */
} box_section_side_t;

/** Cross section of a round shape: a convex polygon in the units of the scale radius with r to
 * the right and y up, counter-clockwise, with a rounded arc at each rim corner.
 */
typedef struct box_section_t {
    int32_t count;                     /**< Number of corners */
    vec2 corners[BOX_SECTION_CORNERS]; /**< Corners as (r, y) */
    int32_t rims[BOX_SECTION_CORNERS]; /**< Rim of each corner, -1 for none */
    float radii[BOX_SECTION_CORNERS];  /**< Radius of each corner, 0 for a sharp corner */
    box_section_side_t sides[BOX_SECTION_CORNERS]; /**< Side from each corner to the next */
} box_section_t;

/** Build the cross section of a round shape.
 *
 * @param shape The shape
 * @param out The cross section
 */
static void box_section_build(
    const box_bevel_round_t *shape,
    box_section_t *out)
{
    float h = 0.5f * shape->length;
    int32_t n = 0;
    *out = (box_section_t){0};
    if (shape->inner > 0.0f) {
        const float r[4] = { shape->inner, shape->bottom, shape->top, shape->inner };
        const float y[4] = { -h, -h, h, h };
        const int32_t rims[4] = {
            BoxRimBottomInner, BoxRimBottomOuter, BoxRimTopOuter, BoxRimTopInner };
        for (n = 0; n < 4; n ++) {
            out->corners[n][0] = r[n];
            out->corners[n][1] = y[n];
            out->rims[n] = rims[n];
        }
    } else {
        out->corners[n][0] = 0.0f;
        out->corners[n][1] = -h;
        out->rims[n ++] = -1;
        if (shape->bottom > 0.0f) {
            out->corners[n][0] = shape->bottom;
            out->corners[n][1] = -h;
            out->rims[n ++] = BoxRimBottomOuter;
        }

        if (shape->top > 0.0f) {
            out->corners[n][0] = shape->top;
            out->corners[n][1] = h;
            out->rims[n ++] = BoxRimTopOuter;
        }

        out->corners[n][0] = 0.0f;
        out->corners[n][1] = h;
        out->rims[n ++] = -1;
    }

    out->count = n;
    for (int32_t i = 0; i < n; i ++) {
        const float *a = out->corners[i], *b = out->corners[(i + 1) % n];
        int32_t rim = out->rims[i];
        out->radii[i] = rim >= 0 ? glm_max(shape->rims[rim], 0.0f) : 0.0f;
        if (a[0] == 0.0f && b[0] == 0.0f) {
            out->sides[i] = BoxSideAxis;
        } else if (a[1] == b[1]) {
            out->sides[i] = BoxSideCap;
        } else {
            out->sides[i] = BoxSideWall;
        }
    }
}

/** Get the unit direction of a side of a cross section.
 *
 * @param section The cross section
 * @param side The side
 * @param out The direction
 * @return The length of the side
 */
static float box_section_direction(
    const box_section_t *section,
    int32_t side,
    vec2 out)
{
    const float *a = section->corners[side];
    const float *b = section->corners[(side + 1) % section->count];
    float length = hypotf(b[0] - a[0], b[1] - a[1]);
    out[0] = length > 0.0f ? (b[0] - a[0]) / length : 0.0f;
    out[1] = length > 0.0f ? (b[1] - a[1]) / length : 0.0f;

    return length;
}

/** Get the angle by which a cross section turns at a corner.
 *
 * @param section The cross section
 * @param corner The corner
 * @return The angle from the direction of the side before to the side after, 0 to pi
 */
static float box_section_turn(
    const box_section_t *section,
    int32_t corner)
{
    vec2 in, out;
    box_section_direction(section, (corner + section->count - 1) % section->count, in);
    box_section_direction(section, corner, out);

    return atan2f(in[0] * out[1] - in[1] * out[0], glm_vec2_dot(in, out));
}

void box_bevel_round_fit(
    box_bevel_round_t *shape)
{
    box_section_t section;
    float setback[BOX_SECTION_CORNERS];
    float scale = 1.0f;
    bool used[BoxRimCount] = { false };
    box_section_build(shape, &section);
    for (int32_t i = 0; i < section.count; i ++) {
        float turn = box_section_turn(&section, i);
        setback[i] = section.radii[i] > 0.0f && turn > 0.0f ?
            section.radii[i] * tanf(0.5f * turn) : 0.0f;
        if (section.rims[i] >= 0 && setback[i] > 0.0f) {
            used[section.rims[i]] = true;
        }
    }

    for (int32_t i = 0; i < section.count; i ++) {
        vec2 dir;
        float length = box_section_direction(&section, i, dir);
        float need = setback[i] + setback[(i + 1) % section.count];
        if (need > length) {
            scale = glm_min(scale, need > 0.0f ? length / need : 1.0f);
        }
    }

    for (int32_t r = 0; r < BoxRimCount; r ++) {
        shape->rims[r] = used[r] ? shape->rims[r] * scale : 0.0f;
    }
}

/** Point of a cross section with its outward normal, in the units of the scale radius */
typedef struct box_section_point_t {
    float r;  /**< Distance to the axis */
    float y;  /**< Height */
    float nr; /**< Radial component of the normal */
    float ny; /**< Vertical component of the normal */
} box_section_point_t;

/** Arc of a rim corner from the side before the corner to the side after it; a sharp corner
 * has one point.
 */
typedef struct box_section_arc_t {
    int32_t count;  /**< Number of points */
    float radius;   /**< Radius, 0 for a sharp corner */
    float turn;     /**< Angle the arc turns */
    box_section_point_t points[BOX_SECTION_STEPS + 1]; /**< Points from the side before */
} box_section_arc_t;

/** Build the arc of each corner of a cross section.
 *
 * @param section The cross section
 * @param steps Steps per rounded corner
 * @param arcs The arcs, one per corner
 */
static void box_section_arcs(
    const box_section_t *section,
    int32_t steps,
    box_section_arc_t *arcs)
{
    int32_t n = section->count;
    for (int32_t i = 0; i < n; i ++) {
        vec2 in, out;
        box_section_direction(section, (i + n - 1) % n, in);
        box_section_direction(section, i, out);
        const float *v = section->corners[i];
        box_section_arc_t *arc = &arcs[i];
        float turn = box_section_turn(section, i);
        float radius = turn > 0.0f ? section->radii[i] : 0.0f;
        vec2 n_in = { in[1], -in[0] };
        arc->radius = radius;
        arc->turn = turn;
        if (radius <= 0.0f) {
            arc->count = 1;
            arc->points[0] = (box_section_point_t){ v[0], v[1], n_in[0], n_in[1] };
            continue;
        }

        float t = radius * tanf(0.5f * turn);
        vec2 before = { v[0] - in[0] * t, v[1] - in[1] * t };
        vec2 after = { v[0] + out[0] * t, v[1] + out[1] * t };
        vec2 center = { before[0] - n_in[0] * radius, before[1] - n_in[1] * radius };
        arc->count = steps + 1;
        for (int32_t k = 0; k <= steps; k ++) {
            float angle = turn * (float)k / (float)steps;
            float ca = cosf(angle), sa = sinf(angle);
            float nr = n_in[0] * ca - n_in[1] * sa;
            float ny = n_in[0] * sa + n_in[1] * ca;
            box_section_point_t *p = &arc->points[k];
            *p = (box_section_point_t){
                center[0] + radius * nr, center[1] + radius * ny, nr, ny };
            if (k == 0) {
                p->r = before[0];
                p->y = before[1];
            } else if (k == steps) {
                p->r = after[0];
                p->y = after[1];
                p->nr = out[1];
                p->ny = -out[0];
            }

            if (fabsf(p->r) < 1e-6f) {
                p->r = 0.0f;
            }
        }
    }
}

/** Append a point of a cross section to a lathe profile in the unit mesh, skipping a point
 * equal to the last one.
 *
 * @param profile The profile
 * @param count The number of points, incremented when the point is added
 * @param p The point
 * @param length Length of the shape, the scale of y
 * @param v The V texture coordinate
 */
static void box_section_emit(
    box_lathe_point_t *profile,
    int32_t *count,
    const box_section_point_t *p,
    float length,
    float v)
{
    box_lathe_point_t point = { p->r, p->y / length, p->nr, p->ny * length, v };
    float scale = hypotf(point.nr, point.ny);
    if (scale > 0.0f) {
        point.nr /= scale;
        point.ny /= scale;
    }

    const box_lathe_point_t *last = *count ? &profile[*count - 1] : NULL;
    if (!last || fabsf(last->r - point.r) >= 1e-6f || fabsf(last->y - point.y) >= 1e-6f) {
        profile[(*count) ++] = point;
    }
}

/** Append the flat end of a partial Tube, the rounded cross section at an angle.
 *
 * @param mesh The mesh
 * @param outline The outline in the unit mesh
 * @param count Number of points of the outline
 * @param inner Inner radius of the tube in the unit mesh
 * @param angle Angle of the end
 * @param sign -1 for the start of the sweep, 1 for its end
 */
static void box_section_end(
    box_mesh_t *mesh,
    const box_lathe_point_t *outline,
    int32_t count,
    float inner,
    float angle,
    float sign)
{
    float ca = cosf(angle), sa = sinf(angle);
    vec3 normal = { -sa * sign, 0.0f, ca * sign };
    float width = 1.0f - inner;
    float cr = 0.0f, cy = 0.0f;
    for (int32_t i = 0; i < count; i ++) {
        cr += outline[i].r / (float)count;
        cy += outline[i].y / (float)count;
    }

    vec3 center_pos = { cr * ca, cy, cr * sa };
    int32_t center = box_mesh_vertex(mesh, center_pos, normal,
        width > 0.0f ? (cr - inner) / width : 0.0f, 0.5f - cy);
    int32_t base = mesh->vertex_count;
    for (int32_t i = 0; i < count; i ++) {
        const box_lathe_point_t *p = &outline[i];
        vec3 pos = { p->r * ca, p->y, p->r * sa };
        float u = width > 0.0f ? (p->r - inner) / width : 0.0f;
        box_mesh_vertex(mesh, pos, normal, u, 0.5f - p->y);
    }

    for (int32_t i = 0; i < count; i ++) {
        box_mesh_triangle(mesh, center, base + i, base + (i + 1) % count);
    }
}

void box_mesh_gen_bevel_round(
    box_mesh_t *mesh,
    const box_bevel_round_t *shape)
{
    float tau = 2.0f * BOX_PI;
    bool full = shape->arc <= 0.0f || shape->arc >= tau - 1e-4f;
    float arc = full ? tau : shape->arc;
    int32_t steps = shape->steps < 1 ? 1 :
        (shape->steps > BOX_SECTION_STEPS ? BOX_SECTION_STEPS : shape->steps);
    float length = shape->length > 0.0f ? shape->length : 1.0f;
    box_section_t section;
    box_section_arc_t *arcs = ecs_os_malloc_n(box_section_arc_t, BOX_SECTION_CORNERS);
    box_lathe_point_t *profile = ecs_os_malloc_n(box_lathe_point_t,
        2 * (BOX_SECTION_STEPS + 1) + 2);
    box_lathe_point_t *outline = ecs_os_malloc_n(box_lathe_point_t,
        BOX_SECTION_CORNERS * (BOX_SECTION_STEPS + 1));
    int32_t outline_count = 0;
    box_section_build(shape, &section);
    box_section_arcs(&section, steps, arcs);
    int32_t n = section.count;
    for (int32_t i = 0; i < n; i ++) {
        const box_section_arc_t *a = &arcs[i];
        const box_section_arc_t *b = &arcs[(i + 1) % n];
        const box_section_point_t *start = &a->points[a->count - 1];
        const box_section_point_t *end = &b->points[0];
        int32_t count = 0;
        if (section.sides[i] == BoxSideCap) {
            box_section_point_t flat[2] = { *start, *end };
            for (int32_t k = 0; k < 2; k ++) {
                flat[k].nr = 0.0f;
                flat[k].ny = start->y > 0.0f ? 1.0f : -1.0f;
                box_section_emit(profile, &count, &flat[k], length, (float)k);
            }

            if (count == 2) {
                box_mesh_lathe(mesh, profile, 2, shape->segments, arc, true);
            }
        } else if (section.sides[i] == BoxSideWall) {
            vec2 dir;
            float side = box_section_direction(&section, i, dir);
            const float *c0 = section.corners[i];
            const float *c1 = section.corners[(i + 1) % n];
            float v0 = 0.5f - c0[1] / length, v1 = 0.5f - c1[1] / length;
            float density = side > 0.0f ? (v1 - v0) / side : 0.0f;
            float va = 0.5f - start->y / length, vb = 0.5f - end->y / length;
            for (int32_t k = 0; k < a->count; k ++) {
                float along = a->radius * a->turn * (float)(a->count - 1 - k) /
                    (float)(a->count > 1 ? a->count - 1 : 1);
                box_section_point_t p = a->points[k];
                if (a->count == 1) {
                    p.nr = dir[1];
                    p.ny = -dir[0];
                }

                box_section_emit(profile, &count, &p, length, va - density * along);
            }

            for (int32_t k = 0; k < b->count; k ++) {
                float along = b->radius * b->turn * (float)k /
                    (float)(b->count > 1 ? b->count - 1 : 1);
                box_section_point_t p = b->points[k];
                if (b->count == 1) {
                    p.nr = dir[1];
                    p.ny = -dir[0];
                }

                box_section_emit(profile, &count, &p, length, vb + density * along);
            }

            if (count >= 2) {
                if (shape->smooth) {
                    box_mesh_lathe(mesh, profile, count, shape->segments, arc, false);
                } else {
                    box_mesh_lathe_facets(mesh, profile, count, shape->segments, arc);
                }
            }
        }
    }

    if (!full) {
        for (int32_t i = 0; i < n; i ++) {
            const box_section_arc_t *a = &arcs[i];
            for (int32_t k = 0; k < a->count; k ++) {
                box_section_emit(outline, &outline_count, &a->points[k], length, 0.0f);
            }
        }

        if (outline_count > 1 &&
            fabsf(outline[0].r - outline[outline_count - 1].r) < 1e-6f &&
            fabsf(outline[0].y - outline[outline_count - 1].y) < 1e-6f)
        {
            outline_count --;
        }

        if (outline_count >= 3) {
            box_section_end(mesh, outline, outline_count, shape->inner, 0.0f, -1.0f);
            box_section_end(mesh, outline, outline_count, shape->inner, arc, 1.0f);
        }
    }

    ecs_os_free(arcs);
    ecs_os_free(profile);
    ecs_os_free(outline);
}

/** Largest number of steps around a corner of a rounded prism */
#define BOX_PRISM_STEPS (256)

/** Rounded triangle prism in the units of its largest size */
typedef struct box_prism_t {
    vec2 p[3];        /**< Corners of the triangle */
    vec2 q[3];        /**< Corners of the triangle shrunk by the radius */
    vec2 n[3];        /**< Outward normal of the edge from each corner to the next */
    float turn[3];    /**< Angle the outline turns at each corner */
    int32_t steps[3]; /**< Steps around each corner, even */
    int32_t first[3]; /**< Index of the first ring point of each corner */
    int32_t points;   /**< Ring points */
    int32_t rows;     /**< Steps per quarter turn of the elevation, even */
    float radius;     /**< Radius */
    float half;       /**< Half the height of the straight part of the side faces */
    vec3 size;        /**< Size along X, Y and Z */
} box_prism_t;

/** Point of the ring grid of a rounded prism.
 *
 * @param prism The prism
 * @param row The ring, 0 at the bottom to 2 * rows + 1 at the top
 * @param col The ring point
 * @param corner Receives the corner of the point
 * @param angle Receives the angle of the point around its corner from the edge before it
 * @param elevation Receives the elevation of the normal
 * @param pos Receives the position in units
 * @param normal Receives the normal in units
 */
static void box_prism_point(
    const box_prism_t *prism,
    int32_t row,
    int32_t col,
    int32_t *corner,
    float *angle,
    float *elevation,
    vec3 pos,
    vec3 normal)
{
    int32_t c = col >= prism->first[2] ? 2 : (col >= prism->first[1] ? 1 : 0);
    int32_t k = col - prism->first[c];
    const float *before = prism->n[(c + 2) % 3];
    float a = prism->turn[c] * (float)k / (float)prism->steps[c];
    float ca = cosf(a), sa = sinf(a);
    vec2 dir = { before[0] * ca - before[1] * sa, before[0] * sa + before[1] * ca };
    if (k == prism->steps[c]) {
        glm_vec2_copy((float*)prism->n[c], dir);
    }

    int32_t rows = prism->rows;
    bool top = row > rows;
    int32_t j = top ? row - rows - 1 : row;
    float phi = 0.5f * BOX_PI * (top ? (float)j / (float)rows : (float)(j - rows) / (float)rows);
    float cp = cosf(phi), sp = sinf(phi);
    if (j == (top ? rows : 0)) {
        cp = 0.0f;
        sp = top ? 1.0f : -1.0f;
    } else if (j == (top ? 0 : rows)) {
        cp = 1.0f;
        sp = 0.0f;
    }

    float zc = top ? prism->half : -prism->half;
    pos[0] = prism->q[c][0] + prism->radius * cp * dir[0];
    pos[1] = prism->q[c][1] + prism->radius * cp * dir[1];
    pos[2] = zc + prism->radius * sp;
    normal[0] = cp * dir[0];
    normal[1] = cp * dir[1];
    normal[2] = sp;
    *corner = c;
    *angle = a;
    *elevation = phi;
}

/** Faces of a rounded prism that the strips continue */
typedef enum box_prism_face_t {
    BoxPrismBottom = -2, /**< Bottom cap */
    BoxPrismTop = -1     /**< Top cap, 0 to 2 are the side faces from each corner */
} box_prism_face_t;

/** Append a vertex of a rounded prism with the texture coordinates of a face.
 *
 * A cap unrolls the rim onto its plane along the normal. A side face unrolls the corners into
 * its plane and the rims of the caps along its height, as fractions of the unbeveled side.
 *
 * @param mesh The mesh
 * @param prism The prism
 * @param face The face whose coordinates the vertex continues, a side face from 0 to 2
 * @param row The ring
 * @param col The ring point
 * @return The vertex index
 */
static int32_t box_prism_vertex(
    box_mesh_t *mesh,
    const box_prism_t *prism,
    int32_t face,
    int32_t row,
    int32_t col)
{
    int32_t corner;
    float angle, phi;
    vec3 pos, normal, unit, unit_normal;
    box_prism_point(prism, row, col, &corner, &angle, &phi, pos, normal);
    const float *size = prism->size;
    float r = prism->radius;
    float u, v;
    if (face < 0) {
        float along = r * (0.5f * BOX_PI - fabsf(phi));
        float len = hypotf(normal[0], normal[1]);
        float dx = 0.0f, dy = 0.0f;
        if (len > 0.0f) {
            dx = normal[0] / len;
            dy = normal[1] / len;
        } else {
            const float *before = prism->n[(corner + 2) % 3];
            float ca = cosf(angle), sa = sinf(angle);
            dx = before[0] * ca - before[1] * sa;
            dy = before[0] * sa + before[1] * ca;
        }

        float x = prism->q[corner][0] + dx * along;
        float y = prism->q[corner][1] + dy * along;
        u = x / size[0] + 0.5f;
        v = 0.5f - y / size[1];
    } else {
        const float *p0 = prism->p[face], *p1 = prism->p[(face + 1) % 3];
        vec2 edge = { p1[0] - p0[0], p1[1] - p0[1] };
        float length = glm_vec2_norm(edge);
        glm_vec2_scale(edge, 1.0f / length, edge);
        float ring = r * cosf(phi);
        float base = (prism->q[corner][0] - p0[0]) * edge[0] +
            (prism->q[corner][1] - p0[1]) * edge[1];
        float offset = corner == face ?
            -ring * (prism->turn[corner] - angle) : ring * angle;
        float z = (row > prism->rows ? prism->half : -prism->half) + r * phi;
        u = (base + offset) / length;
        v = 0.5f - z / size[2];
    }

    for (int32_t k = 0; k < 3; k ++) {
        unit[k] = pos[k] / size[k];
        unit_normal[k] = normal[k] * size[k];
    }

    glm_vec3_normalize(unit_normal);

    return box_mesh_vertex(mesh, unit, unit_normal, u, v);
}

/** Append a triangle unless two of its corners coincide.
 *
 * @param mesh The mesh
 * @param a First vertex index
 * @param b Second vertex index
 * @param c Third vertex index
 */
static void box_prism_triangle(
    box_mesh_t *mesh,
    int32_t a,
    int32_t b,
    int32_t c)
{
    const float *pa = mesh->vertices[a].position;
    const float *pb = mesh->vertices[b].position;
    const float *pc = mesh->vertices[c].position;
    if (glm_vec3_distance2((float*)pa, (float*)pb) > 1e-12f &&
        glm_vec3_distance2((float*)pb, (float*)pc) > 1e-12f &&
        glm_vec3_distance2((float*)pc, (float*)pa) > 1e-12f)
    {
        box_mesh_triangle(mesh, a, b, c);
    }
}

/** Append a region of the ring grid of a rounded prism.
 *
 * @param mesh The mesh
 * @param prism The prism
 * @param face The face whose coordinates the region continues
 * @param row0 First ring
 * @param row1 Last ring
 * @param col0 First ring point
 * @param cols Number of ring points
 * @param closed True when the last ring point connects back to the first
 */
static void box_prism_region(
    box_mesh_t *mesh,
    const box_prism_t *prism,
    int32_t face,
    int32_t row0,
    int32_t row1,
    int32_t col0,
    int32_t cols,
    bool closed)
{
    int32_t base = mesh->vertex_count;
    for (int32_t row = row0; row <= row1; row ++) {
        for (int32_t c = 0; c < cols; c ++) {
            box_prism_vertex(mesh, prism, face, row, (col0 + c) % prism->points);
        }
    }

    int32_t quads = closed ? cols : cols - 1;
    for (int32_t row = 0; row < row1 - row0; row ++) {
        for (int32_t c = 0; c < quads; c ++) {
            int32_t a = base + row * cols + c;
            int32_t b = base + row * cols + (c + 1) % cols;
            int32_t d = a + cols, e = b + cols;
            box_prism_triangle(mesh, a, b, e);
            box_prism_triangle(mesh, a, e, d);
        }
    }

    if (face < 0) {
        int32_t row = face == BoxPrismTop ? row1 - row0 : 0;
        box_prism_triangle(mesh, base + row * cols + prism->first[0],
            base + row * cols + prism->first[1], base + row * cols + prism->first[2]);
    }
}

void box_mesh_gen_bevel_prism(
    box_mesh_t *mesh,
    bool right,
    const vec3 size,
    float radius,
    int32_t segments)
{
    box_prism_t prism = {0};
    const vec2 corners[3] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { right ? -0.5f : 0.0f, 0.5f } };
    float perimeter = 0.0f, lengths[3];
    glm_vec3_copy((float*)size, prism.size);
    for (int32_t i = 0; i < 3; i ++) {
        prism.p[i][0] = corners[i][0] * size[0];
        prism.p[i][1] = corners[i][1] * size[1];
    }

    for (int32_t i = 0; i < 3; i ++) {
        const float *a = prism.p[i], *b = prism.p[(i + 1) % 3];
        lengths[i] = hypotf(b[0] - a[0], b[1] - a[1]);
        prism.n[i][0] = (b[1] - a[1]) / lengths[i];
        prism.n[i][1] = (a[0] - b[0]) / lengths[i];
        perimeter += lengths[i];
    }

    float area = 0.5f * size[0] * size[1];
    float inradius = 2.0f * area / perimeter;
    float limit = glm_min(inradius, 0.5f * size[2]);
    float r = glm_min(radius, limit);
    if (r >= limit * (1.0f - 1e-3f)) {
        r = limit;
    }

    bool point = r >= inradius * (1.0f - 1e-3f);
    prism.half = r >= 0.5f * size[2] * (1.0f - 1e-3f) ? 0.0f : 0.5f * size[2] - r;
    prism.radius = r;
    vec2 incenter = { 0.0f, 0.0f };
    for (int32_t i = 0; i < 3; i ++) {
        float opposite = lengths[(i + 1) % 3];
        incenter[0] += prism.p[i][0] * opposite / perimeter;
        incenter[1] += prism.p[i][1] * opposite / perimeter;
    }

    int32_t rows = segments < 1 ? 2 : segments + (segments & 1);
    rows = rows > 64 ? 64 : rows;
    prism.rows = rows;
    for (int32_t i = 0; i < 3; i ++) {
        const float *before = prism.n[(i + 2) % 3], *after = prism.n[i];
        float sum = 1.0f + glm_vec2_dot((float*)before, (float*)after);
        if (point) {
            glm_vec2_copy(incenter, prism.q[i]);
        } else {
            prism.q[i][0] = prism.p[i][0] - r * (before[0] + after[0]) / sum;
            prism.q[i][1] = prism.p[i][1] - r * (before[1] + after[1]) / sum;
        }

        prism.turn[i] = atan2f(before[0] * after[1] - before[1] * after[0],
            glm_vec2_dot((float*)before, (float*)after));
        int32_t steps = (int32_t)ceilf((float)rows * prism.turn[i] / BOX_PI - 1e-4f) * 2;
        steps = steps < 2 ? 2 : (steps > BOX_PRISM_STEPS ? BOX_PRISM_STEPS : steps);
        prism.steps[i] = steps;
        prism.first[i] = prism.points;
        prism.points += steps + 1;
    }

    int32_t half = rows / 2;
    int32_t last = 2 * rows + 1;
    box_prism_region(mesh, &prism, BoxPrismBottom, 0, half, 0, prism.points, true);
    box_prism_region(mesh, &prism, BoxPrismTop, rows + 1 + half, last, 0, prism.points, true);
    for (int32_t i = 0; i < 3; i ++) {
        int32_t next = (i + 1) % 3;
        int32_t col0 = prism.first[i] + prism.steps[i] / 2;
        int32_t cols = prism.steps[i] / 2 + 1 + prism.steps[next] / 2 + 1;
        box_prism_region(mesh, &prism, i, half, rows + 1 + half, col0, cols, false);
    }
}
