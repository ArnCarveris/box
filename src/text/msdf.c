/**
 * @file text/msdf.c
 * @brief Multi channel signed distance fields of outlines, after the method of msdfgen.
 *
 * Edges are colored so that two channels meet at every corner; a texel takes per channel the
 * perpendicular distance of the nearest edge of that channel.
 *
 * Contours combine as in msdfgen's OverlappingContourCombiner, so edges inside another contour
 * leave no seam; the sign follows the nonzero fill and msdf_correct.c removes artifacts. Only
 * texels near the outline get distances, the others take the full value of their side.
 */

#include "msdf.h"
#include <math.h>
#include <stdlib.h>

/** Channel bit of red */
#define BOX_MSDF_RED (1)

/** Channel bit of green */
#define BOX_MSDF_GREEN (2)

/** Channel bit of blue */
#define BOX_MSDF_BLUE (4)

/** Channels of yellow, red and green */
#define BOX_MSDF_YELLOW (3)

/** Channels of magenta, red and blue */
#define BOX_MSDF_MAGENTA (5)

/** Channels of cyan, green and blue */
#define BOX_MSDF_CYAN (6)

/** Every channel */
#define BOX_MSDF_WHITE (7)

/** Sine of 3 radians: two edges that turn by more than this meet at a corner */
#define BOX_MSDF_CORNER (0.14112000805986721)

/** Lines a quadratic curve is split into for the fill test */
#define BOX_MSDF_FLATTEN (16)

/** Quadratic curves a cubic curve is approximated by */
#define BOX_MSDF_CUBIC_SPLIT (4)

/** Dot product of the directions at a vertex below which, minus one, the vertex is a cusp
 * (msdfgen's MSDFGEN_CORNER_DOT_EPSILON) */
#define BOX_MSDF_CUSP_DOT (0.000001)

/** Overshoot of the turn that pushes the curves of a cusp apart (msdfgen) */
#define BOX_MSDF_DECONVERGE (1.11111111111111111)

/** Largest magnitude of a value before quantization, far beyond the 0 to 1 that is kept */
#define BOX_MSDF_VALUE_LIMIT (1e6)

/** A distance that is larger than any distance of a glyph */
#define BOX_MSDF_FAR (1e240)

/** Relative margin of the lower bound of the distance to an edge for the rounding of the
 * distance, far above the error of double precision */
#define BOX_MSDF_BOUND_SLACK (1e-9)

/** Texels beyond half the range from which on a texel only takes the side of the outline: a
 * sample that interpolates it lies within sqrt(2) texels, so more than half the range from the
 * outline, where the distance saturates */
#define BOX_MSDF_NEAR (1.5)

/** Texels of an edge along which one box bounds the texels near the edge */
#define BOX_MSDF_NEAR_PIECE (1.0)

/** Texels of a row a span of edges covers before the edges of a contour are culled again */
#define BOX_MSDF_SPAN (4)

/** The nearest edges a contour had at the previous texel and their distances at the point */
typedef struct box_msdf_probe_t {
    int32_t edge[3];          /**< Edge of each channel, -1 for none */
    box_msdf_dist_t dist[3];  /**< Distance of the edge at the point */
    double param[3];          /**< Parameter of the nearest point of the edge */
    double limit[3];          /**< Magnitude of the distance, BOX_MSDF_FAR without an edge */
    double most;              /**< Largest limit */
} box_msdf_probe_t;

/** Crossing of a fill test line with the outline */
typedef struct box_msdf_cross_t {
    double x;      /**< Font x of the crossing */
    int32_t dir;   /**< 1 where the outline goes up, -1 where it goes down */
} box_msdf_cross_t;

/** Subtract two vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return a - b
 */
static box_msdf_point_t box_msdf_sub(
    box_msdf_point_t a,
    box_msdf_point_t b)
{
    return (box_msdf_point_t){ a.x - b.x, a.y - b.y };
}

/** Interpolate between two vectors.
 *
 * @param a The vector at 0
 * @param b The vector at 1
 * @param t The parameter
 * @return The interpolated vector
 */
static box_msdf_point_t box_msdf_mix(
    box_msdf_point_t a,
    box_msdf_point_t b,
    double t)
{
    return (box_msdf_point_t){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}

/** Dot product of two vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return The dot product
 */
static double box_msdf_dot(
    box_msdf_point_t a,
    box_msdf_point_t b)
{
    return a.x * b.x + a.y * b.y;
}

/** Cross product of two vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return The z of the cross product
 */
static double box_msdf_cross(
    box_msdf_point_t a,
    box_msdf_point_t b)
{
    return a.x * b.y - a.y * b.x;
}

/** Length of a vector.
 *
 * @param a The vector
 * @return The length
 */
static double box_msdf_length(
    box_msdf_point_t a)
{
    return sqrt(a.x * a.x + a.y * a.y);
}

/** Scale a vector to unit length.
 *
 * @param a The vector
 * @return The unit vector, up for a zero vector
 */
static box_msdf_point_t box_msdf_normalize(
    box_msdf_point_t a)
{
    double len = box_msdf_length(a);
    box_msdf_point_t result = { 0.0, 1.0 };
    if (len > 0.0) {
        result = (box_msdf_point_t){ a.x / len, a.y / len };
    }

    return result;
}

/** Sign of a value that counts zero as negative.
 *
 * @param v The value
 * @return 1 or -1
 */
static double box_msdf_sign(
    double v)
{
    return v > 0.0 ? 1.0 : -1.0;
}

/** Order two distances by magnitude, then by how head on the edge is.
 *
 * @param a The first distance
 * @param b The second distance
 * @return Whether a is nearer than b
 */
static bool box_msdf_less(
    box_msdf_dist_t a,
    box_msdf_dist_t b)
{
    double fa = fabs(a.distance), fb = fabs(b.distance);

    return fa < fb || (fa == fb && a.dot < b.dot);
}

/** Point of an edge at a parameter.
 *
 * @param e The edge
 * @param t The parameter
 * @return The point
 */
static box_msdf_point_t box_msdf_at(
    const box_msdf_edge_t *e,
    double t)
{
    box_msdf_point_t result = box_msdf_mix(e->p[0], e->p[2], t);
    if (e->degree == 2) {
        result = box_msdf_mix(box_msdf_mix(e->p[0], e->p[1], t),
            box_msdf_mix(e->p[1], e->p[2], t), t);
    }

    return result;
}

/** Direction of an edge at a parameter, not normalized.
 *
 * @param e The edge
 * @param t The parameter
 * @return The tangent
 */
static box_msdf_point_t box_msdf_direction(
    const box_msdf_edge_t *e,
    double t)
{
    box_msdf_point_t result = box_msdf_sub(e->p[2], e->p[0]);
    if (e->degree == 2) {
        box_msdf_point_t d = box_msdf_mix(box_msdf_sub(e->p[1], e->p[0]),
            box_msdf_sub(e->p[2], e->p[1]), t);
        if (d.x != 0.0 || d.y != 0.0) {
            result = d;
        }
    }

    return result;
}

int32_t box_msdf_quadratic(
    double x[2],
    double a,
    double b,
    double c)
{
    int32_t result = 0;
    if (a == 0.0 || fabs(b) > 1e12 * fabs(a)) {
        if (b != 0.0) {
            x[0] = -c / b;
            result = 1;
        }

        goto done;
    }

    double d = b * b - 4.0 * a * c;
    if (d > 0.0) {
        d = sqrt(d);
        x[0] = (-b + d) / (2.0 * a);
        x[1] = (-b - d) / (2.0 * a);
        result = 2;
    } else if (d == 0.0) {
        x[0] = -b / (2.0 * a);
        result = 1;
    }

done:
    return result;
}

/** Solve x^3 + a x^2 + b x + c = 0.
 *
 * @param x Receives the roots
 * @param a The quadratic coefficient
 * @param b The linear coefficient
 * @param c The constant
 * @return The number of roots
 */
static int32_t box_msdf_cubic_normed(
    double x[3],
    double a,
    double b,
    double c)
{
    int32_t result = 1;
    double a2 = a * a;
    double q = (a2 - 3.0 * b) / 9.0;
    double r = (a * (2.0 * a2 - 9.0 * b) + 27.0 * c) / 54.0;
    double r2 = r * r, q3 = q * q * q;
    a /= 3.0;
    if (r2 < q3) {
        double t = acos(fmax(-1.0, fmin(1.0, r / sqrt(q3))));
        q = -2.0 * sqrt(q);
        x[0] = q * cos(t / 3.0) - a;
        x[1] = q * cos((t + 2.0 * GLM_PI) / 3.0) - a;
        x[2] = q * cos((t - 2.0 * GLM_PI) / 3.0) - a;
        result = 3;
    } else {
        double u = (r < 0.0 ? 1.0 : -1.0) * pow(fabs(r) + sqrt(r2 - q3), 1.0 / 3.0);
        double v = u == 0.0 ? 0.0 : q / u;
        x[0] = (u + v) - a;
        if (u == v || fabs(u - v) < 1e-12 * fabs(u + v)) {
            x[1] = -0.5 * (u + v) - a;
            result = 2;
        }
    }

    return result;
}

/** Solve a x^3 + b x^2 + c x + d = 0.
 *
 * @param x Receives the roots
 * @param a The cubic coefficient
 * @param b The quadratic coefficient
 * @param c The linear coefficient
 * @param d The constant
 * @return The number of roots
 */
static int32_t box_msdf_cubic(
    double x[3],
    double a,
    double b,
    double c,
    double d)
{
    int32_t result;
    if (a != 0.0 && fabs(b / a) < 1e6) {
        result = box_msdf_cubic_normed(x, b / a, c / a, d / a);
    } else {
        result = box_msdf_quadratic(x, b, c, d);
    }

    return result;
}

/** Signed distance from a point to a line.
 *
 * @param e The line
 * @param origin The point
 * @param param Receives the parameter of the nearest point
 * @return The distance
 */
static box_msdf_dist_t box_msdf_line_distance(
    const box_msdf_edge_t *e,
    box_msdf_point_t origin,
    double *param)
{
    box_msdf_point_t aq = box_msdf_sub(origin, e->p[0]);
    box_msdf_point_t ab = box_msdf_sub(e->p[2], e->p[0]);
    double t = box_msdf_dot(aq, ab) / box_msdf_dot(ab, ab);
    box_msdf_point_t eq = box_msdf_sub(t > 0.5 ? e->p[2] : e->p[0], origin);
    double end = box_msdf_length(eq);
    box_msdf_dist_t result = {
        box_msdf_sign(box_msdf_cross(aq, ab)) * end,
        fabs(box_msdf_dot(box_msdf_normalize(ab), box_msdf_normalize(eq)))
    };
    *param = t;
    if (t > 0.0 && t < 1.0) {
        double ortho = box_msdf_cross(aq, ab) / box_msdf_length(ab);
        if (fabs(ortho) < end) {
            result = (box_msdf_dist_t){ ortho, 0.0 };
        }
    }

    return result;
}

/** Signed distance from a point to a quadratic curve.
 *
 * @param e The curve
 * @param origin The point
 * @param param Receives the parameter of the nearest point
 * @return The distance
 */
static box_msdf_dist_t box_msdf_quad_distance(
    const box_msdf_edge_t *e,
    box_msdf_point_t origin,
    double *param)
{
    box_msdf_point_t qa = box_msdf_sub(e->p[0], origin);
    box_msdf_point_t ab = box_msdf_sub(e->p[1], e->p[0]);
    box_msdf_point_t br = box_msdf_sub(box_msdf_sub(e->p[2], e->p[1]), ab);
    box_msdf_point_t qb = box_msdf_sub(e->p[2], origin);
    double t[3];
    int32_t i, count = box_msdf_cubic(t, box_msdf_dot(br, br), 3.0 * box_msdf_dot(ab, br),
        2.0 * box_msdf_dot(ab, ab) + box_msdf_dot(qa, br), box_msdf_dot(qa, ab));
    box_msdf_point_t dir = box_msdf_direction(e, 0.0);
    double best = box_msdf_sign(box_msdf_cross(dir, qa)) * box_msdf_length(qa);
    *param = -box_msdf_dot(qa, dir) / box_msdf_dot(dir, dir);
    dir = box_msdf_direction(e, 1.0);
    if (box_msdf_length(qb) < fabs(best)) {
        best = box_msdf_sign(box_msdf_cross(dir, qb)) * box_msdf_length(qb);
        *param = box_msdf_dot(box_msdf_sub(origin, e->p[1]), dir) / box_msdf_dot(dir, dir);
    }

    for (i = 0; i < count; i ++) {
        if (t[i] <= 0.0 || t[i] >= 1.0) {
            continue;
        }

        box_msdf_point_t qe = {
            qa.x + 2.0 * t[i] * ab.x + t[i] * t[i] * br.x,
            qa.y + 2.0 * t[i] * ab.y + t[i] * t[i] * br.y
        };
        double len = box_msdf_length(qe);
        if (len <= fabs(best)) {
            box_msdf_point_t d = { ab.x + t[i] * br.x, ab.y + t[i] * br.y };
            best = box_msdf_sign(box_msdf_cross(d, qe)) * len;
            *param = t[i];
        }
    }

    box_msdf_dist_t result = { best, 0.0 };
    if (*param < 0.0) {
        result.dot = fabs(box_msdf_dot(box_msdf_normalize(box_msdf_direction(e, 0.0)),
            box_msdf_normalize(qa)));
    } else if (*param > 1.0) {
        result.dot = fabs(box_msdf_dot(box_msdf_normalize(box_msdf_direction(e, 1.0)),
            box_msdf_normalize(qb)));
    }

    return result;
}

/** Turn the distance to the nearest edge into the distance to its extension beyond an end.
 *
 * @param ep The constants of the edge
 * @param origin The point
 * @param param Parameter of the nearest point on the edge
 * @param dist The distance, replaced by the pseudo distance when that is nearer
 */
static void box_msdf_pseudo(
    const box_msdf_prep_t *ep,
    box_msdf_point_t origin,
    double param,
    box_msdf_dist_t *dist)
{
    if (param >= 0.0 && param <= 1.0) {
        goto done;
    }

    box_msdf_point_t dir = param < 0.0 ? ep->a_unit : ep->b_unit;
    box_msdf_point_t q = box_msdf_sub(origin, param < 0.0 ? ep->a_end : ep->b_end);
    double ts = box_msdf_dot(q, dir);
    if ((param < 0.0 && ts < 0.0) || (param > 1.0 && ts > 0.0)) {
        double pseudo = box_msdf_cross(q, dir);
        if (fabs(pseudo) <= fabs(dist->distance)) {
            dist->distance = pseudo;
            dist->dot = 0.0;
        }
    }

done:
    return;
}

/** Signed distance from a point to an edge.
 *
 * @param e The edge
 * @param origin The point
 * @param param Receives the parameter of the nearest point
 * @return The distance
 */
static box_msdf_dist_t box_msdf_edge_distance(
    const box_msdf_edge_t *e,
    box_msdf_point_t origin,
    double *param)
{
    return e->degree == 2
        ? box_msdf_quad_distance(e, origin, param)
        : box_msdf_line_distance(e, origin, param);
}

double box_msdf_edges_distance(
    const box_msdf_edge_t *edges,
    const int32_t *indices,
    int32_t count,
    box_msdf_point_t p)
{
    box_msdf_dist_t result = { INFINITY, 0.0 };
    int32_t i;
    double param;
    for (i = 0; i < count; i ++) {
        box_msdf_dist_t d = box_msdf_edge_distance(&edges[indices[i]], p, &param);
        if (box_msdf_less(d, result)) {
            result = d;
        }
    }

    return result.distance;
}

void box_msdf_shape_init(
    box_msdf_shape_t *shape)
{
    ecs_os_memset_t(shape, 0, box_msdf_shape_t);
    ecs_vec_init_t(NULL, &shape->edges, box_msdf_edge_t, 0);
    ecs_vec_init_t(NULL, &shape->contours, int32_t, 0);
}

void box_msdf_shape_fini(
    box_msdf_shape_t *shape)
{
    ecs_vec_fini_t(NULL, &shape->edges, box_msdf_edge_t);
    ecs_vec_fini_t(NULL, &shape->contours, int32_t);
}

/** Append an edge to the contour being built.
 *
 * @param shape The shape
 * @param edge The edge
 */
static void box_msdf_push(
    box_msdf_shape_t *shape,
    const box_msdf_edge_t *edge)
{
    *ecs_vec_append_t(NULL, &shape->edges, box_msdf_edge_t) = *edge;
    *ecs_vec_last_t(&shape->contours, int32_t) += 1;
    shape->pen = edge->p[2];
}

void box_msdf_line_to(
    box_msdf_shape_t *shape,
    double x,
    double y)
{
    box_msdf_point_t end = { x, y };
    if (!shape->open || (end.x == shape->pen.x && end.y == shape->pen.y)) {
        goto done;
    }

    box_msdf_edge_t edge = {
        .p = { shape->pen, box_msdf_mix(shape->pen, end, 0.5), end },
        .degree = 1,
        .color = BOX_MSDF_WHITE
    };
    box_msdf_push(shape, &edge);
done:
    return;
}

void box_msdf_quad_to(
    box_msdf_shape_t *shape,
    double cx,
    double cy,
    double x,
    double y)
{
    box_msdf_point_t c = { cx, cy }, end = { x, y };
    bool flat = (c.x == shape->pen.x && c.y == shape->pen.y) || (c.x == x && c.y == y);
    if (!shape->open || (end.x == shape->pen.x && end.y == shape->pen.y)) {
        goto done;
    }

    if (flat || fabs(box_msdf_cross(box_msdf_sub(c, shape->pen),
        box_msdf_sub(end, shape->pen))) < 1e-9)
    {
        box_msdf_line_to(shape, x, y);
        goto done;
    }

    box_msdf_edge_t edge = {
        .p = { shape->pen, c, end },
        .degree = 2,
        .color = BOX_MSDF_WHITE
    };
    box_msdf_push(shape, &edge);
done:
    return;
}

/** Point of a cubic curve at a parameter.
 *
 * @param p The four points of the curve
 * @param t The parameter
 * @return The point
 */
static box_msdf_point_t box_msdf_cubic_at(
    const box_msdf_point_t *p,
    double t)
{
    double s = 1.0 - t;
    double a = s * s * s, b = 3.0 * s * s * t, c = 3.0 * s * t * t, d = t * t * t;

    return (box_msdf_point_t){
        a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x,
        a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y
    };
}

/** Tangent of a cubic curve at a parameter.
 *
 * @param p The four points of the curve
 * @param t The parameter
 * @return The derivative
 */
static box_msdf_point_t box_msdf_cubic_tangent(
    const box_msdf_point_t *p,
    double t)
{
    double s = 1.0 - t;
    double a = 3.0 * s * s, b = 6.0 * s * t, c = 3.0 * t * t;

    return (box_msdf_point_t){
        a * (p[1].x - p[0].x) + b * (p[2].x - p[1].x) + c * (p[3].x - p[2].x),
        a * (p[1].y - p[0].y) + b * (p[2].y - p[1].y) + c * (p[3].y - p[2].y)
    };
}

void box_msdf_cubic_to(
    box_msdf_shape_t *shape,
    double c1x,
    double c1y,
    double c2x,
    double c2y,
    double x,
    double y)
{
    box_msdf_point_t p[4] = { shape->pen, { c1x, c1y }, { c2x, c2y }, { x, y } };
    int32_t i;
    for (i = 0; i < BOX_MSDF_CUBIC_SPLIT; i ++) {
        double t0 = (double)i / BOX_MSDF_CUBIC_SPLIT;
        double t1 = (double)(i + 1) / BOX_MSDF_CUBIC_SPLIT;
        double h = (t1 - t0) / 3.0;
        box_msdf_point_t a = box_msdf_cubic_at(p, t0), d = box_msdf_cubic_at(p, t1);
        box_msdf_point_t da = box_msdf_cubic_tangent(p, t0), dd = box_msdf_cubic_tangent(p, t1);
        box_msdf_point_t b = { a.x + da.x * h, a.y + da.y * h };
        box_msdf_point_t c = { d.x - dd.x * h, d.y - dd.y * h };
        double cx = (3.0 * (b.x + c.x) - a.x - d.x) * 0.25;
        double cy = (3.0 * (b.y + c.y) - a.y - d.y) * 0.25;
        if (i == BOX_MSDF_CUBIC_SPLIT - 1) {
            d = p[3];
        }

        box_msdf_quad_to(shape, cx, cy, d.x, d.y);
    }
}

/** Pick the next color of an edge, never the banned one.
 *
 * @param color The current color
 * @param seed The state of the choice
 * @param banned Color that must not come next, 0 for none
 * @return The next color
 */
static int32_t box_msdf_switch(
    int32_t color,
    uint64_t *seed,
    int32_t banned)
{
    static const int32_t start[3] = { BOX_MSDF_CYAN, BOX_MSDF_MAGENTA, BOX_MSDF_YELLOW };
    int32_t combined = color & banned;
    int32_t result;
    if (combined == BOX_MSDF_RED || combined == BOX_MSDF_GREEN || combined == BOX_MSDF_BLUE) {
        result = combined ^ BOX_MSDF_WHITE;
    } else if (color == 0 || color == BOX_MSDF_WHITE) {
        result = start[*seed % 3];
        *seed /= 3;
    } else {
        int32_t shifted = color << (1 + (int32_t)(*seed & 1));
        result = (shifted | shifted >> 3) & BOX_MSDF_WHITE;
        *seed >>= 1;
    }

    return result;
}

/** Split an edge into three parts of equal parameter range.
 *
 * @param e The edge
 * @param out Receives the parts
 */
static void box_msdf_thirds(
    const box_msdf_edge_t *e,
    box_msdf_edge_t *out)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        double t0 = (double)i / 3.0, t1 = (double)(i + 1) / 3.0;
        out[i] = *e;
        out[i].p[0] = box_msdf_at(e, t0);
        out[i].p[2] = box_msdf_at(e, t1);
        out[i].p[1] = box_msdf_mix(out[i].p[0], out[i].p[2], 0.5);
        if (e->degree == 2) {
            out[i].p[1] = box_msdf_mix(box_msdf_mix(e->p[0], e->p[1], t1),
                box_msdf_mix(e->p[1], e->p[2], t1), t0);
        }
    }
}

/** Color a contour with one corner: the edges around it take three colors, an edge or two are
 * split in thirds first.
 *
 * @param shape The shape, the contour is its last
 * @param first Index of the first edge of the contour
 * @param corner Index of the corner edge in the contour
 * @param seed The state of the color choice
 */
static void box_msdf_teardrop(
    box_msdf_shape_t *shape,
    int32_t first,
    int32_t corner,
    uint64_t *seed)
{
    int32_t colors[3] = { BOX_MSDF_WHITE, BOX_MSDF_WHITE, BOX_MSDF_WHITE };
    int32_t i, count = ecs_vec_count(&shape->edges) - first;
    box_msdf_edge_t *edges = ecs_vec_get_t(&shape->edges, box_msdf_edge_t, first);
    box_msdf_edge_t parts[6];
    colors[0] = box_msdf_switch(colors[0], seed, 0);
    colors[2] = box_msdf_switch(colors[0], seed, 0);
    if (count >= 3) {
        for (i = 0; i < count; i ++) {
            int32_t third = (int32_t)(3.0 + 2.875 * i / (count - 1) - 1.4375 + 0.5) - 3;
            edges[(corner + i) % count].color = colors[1 + third];
        }

        goto done;
    }

    box_msdf_thirds(&edges[0], &parts[corner ? 3 : 0]);
    if (count == 2) {
        box_msdf_thirds(&edges[1], &parts[corner ? 0 : 3]);
        for (i = 0; i < 6; i ++) {
            parts[i].color = colors[i / 2];
        }
    } else {
        for (i = 0; i < 3; i ++) {
            parts[i].color = colors[i];
        }
    }

    ecs_vec_set_count_t(NULL, &shape->edges, box_msdf_edge_t, first);
    for (i = 0; i < count * 3; i ++) {
        *ecs_vec_append_t(NULL, &shape->edges, box_msdf_edge_t) = parts[i];
    }

    *ecs_vec_last_t(&shape->contours, int32_t) = count * 3;
done:
    return;
}

/** Color the edges of the last contour so that two channels meet at every corner.
 *
 * @param shape The shape
 * @param seed The state of the color choice
 */
static void box_msdf_color_contour(
    box_msdf_shape_t *shape,
    uint64_t *seed)
{
    int32_t count = *ecs_vec_last_t(&shape->contours, int32_t);
    int32_t first = ecs_vec_count(&shape->edges) - count;
    box_msdf_edge_t *edges = ecs_vec_get_t(&shape->edges, box_msdf_edge_t, first);
    ecs_vec_t corners;
    int32_t i, spline = 0;
    ecs_vec_init_t(NULL, &corners, int32_t, 0);
    box_msdf_point_t prev = box_msdf_normalize(box_msdf_direction(&edges[count - 1], 1.0));
    for (i = 0; i < count; i ++) {
        box_msdf_point_t dir = box_msdf_normalize(box_msdf_direction(&edges[i], 0.0));
        if (box_msdf_dot(prev, dir) <= 0.0 || fabs(box_msdf_cross(prev, dir)) > BOX_MSDF_CORNER) {
            *ecs_vec_append_t(NULL, &corners, int32_t) = i;
        }

        prev = box_msdf_normalize(box_msdf_direction(&edges[i], 1.0));
    }

    int32_t corner_count = ecs_vec_count(&corners);
    int32_t *c = ecs_vec_first(&corners);
    if (corner_count == 1) {
        box_msdf_teardrop(shape, first, c[0], seed);
    } else if (corner_count > 1) {
        int32_t color = box_msdf_switch(BOX_MSDF_WHITE, seed, 0);
        int32_t initial = color;
        for (i = 0; i < count; i ++) {
            int32_t index = (c[0] + i) % count;
            if (spline + 1 < corner_count && c[spline + 1] == index) {
                spline ++;
                color = box_msdf_switch(color, seed,
                    spline == corner_count - 1 ? initial : 0);
            }

            edges[index].color = color;
        }
    }

    ecs_vec_fini_t(NULL, &corners, int32_t);
}

/** Order two edges that leave a corner in opposite directions, after msdfgen's
 * convergentCurveOrdering: the sign of the cross product of the points of both edges at the
 * same small distance from the corner.
 *
 * @param a The edge that ends at the corner
 * @param b The edge that starts at the corner
 * @return 1 when b lies counter clockwise of a near the corner, -1 when clockwise, 0 for a tie
 */
static int32_t box_msdf_convergent_order(
    const box_msdf_edge_t *a,
    const box_msdf_edge_t *b)
{
    box_msdf_point_t corner = b->p[0];
    box_msdf_point_t a1 = box_msdf_sub(a->degree == 2 ? a->p[1] : a->p[0], corner);
    box_msdf_point_t b1 = box_msdf_sub(b->degree == 2 ? b->p[1] : b->p[2], corner);
    box_msdf_point_t a2 = { 0.0, 0.0 }, b2 = { 0.0, 0.0 };
    int32_t result = 0;
    if (a->degree == 2) {
        a2 = box_msdf_sub(box_msdf_sub(a->p[0], a->p[1]), a1);
    }

    if (b->degree == 2) {
        b2 = box_msdf_sub(box_msdf_sub(b->p[2], b->p[1]), b1);
    }

    a1 = (box_msdf_point_t){ a1.x * a->degree, a1.y * a->degree };
    b1 = (box_msdf_point_t){ b1.x * b->degree, b1.y * b->degree };
    double as = box_msdf_length(a1), bs = box_msdf_length(b1);
    double third = as * box_msdf_cross(a1, b2) + bs * box_msdf_cross(a2, b1);
    double fourth = as * bs * box_msdf_cross(a2, b2);
    if (third != 0.0) {
        result = third > 0.0 ? 1 : -1;
    } else if (fourth != 0.0) {
        result = fourth > 0.0 ? 1 : -1;
    }

    return result;
}

/** Push apart the curves of every cusp of the last contour, where the edges before and after a
 * vertex leave it in opposite directions, after msdfgen's Shape::normalize.
 *
 * The control point of each curve turns its tangent away from the other by a tiny angle, so the
 * distances of the two edges no longer tie beyond the tip. A line keeps its direction.
 *
 * @param shape The shape
 */
static void box_msdf_deconverge(
    box_msdf_shape_t *shape)
{
    int32_t count = *ecs_vec_last_t(&shape->contours, int32_t);
    int32_t i, first = ecs_vec_count(&shape->edges) - count;
    box_msdf_edge_t *edges = ecs_vec_get_t(&shape->edges, box_msdf_edge_t, first);
    double eps = BOX_MSDF_CUSP_DOT - 1.0;
    double factor = BOX_MSDF_DECONVERGE * sqrt(1.0 - eps * eps) / eps;
    for (i = 0; i < count && count > 1; i ++) {
        box_msdf_edge_t *a = &edges[(i + count - 1) % count], *b = &edges[i];
        box_msdf_point_t da = box_msdf_normalize(box_msdf_direction(a, 1.0));
        box_msdf_point_t db = box_msdf_normalize(box_msdf_direction(b, 0.0));
        if (box_msdf_dot(da, db) >= eps) {
            continue;
        }

        box_msdf_point_t axis = box_msdf_normalize(box_msdf_sub(db, da));
        double f = box_msdf_convergent_order(a, b) < 0 ? -factor : factor;
        axis = (box_msdf_point_t){ axis.x * f, axis.y * f };
        if (a->degree == 2) {
            double len = box_msdf_length(box_msdf_sub(a->p[1], a->p[2]));
            a->p[1].x -= axis.y * len;
            a->p[1].y += axis.x * len;
        }

        if (b->degree == 2) {
            double len = box_msdf_length(box_msdf_sub(b->p[1], b->p[0]));
            b->p[1].x += axis.y * len;
            b->p[1].y -= axis.x * len;
        }
    }
}

/** Close the contour being built and color its edges.
 *
 * @param shape The shape
 */
static void box_msdf_close(
    box_msdf_shape_t *shape)
{
    uint64_t seed = 0;
    if (!shape->open) {
        goto done;
    }

    box_msdf_line_to(shape, shape->start.x, shape->start.y);
    shape->open = false;
    if (!*ecs_vec_last_t(&shape->contours, int32_t)) {
        ecs_vec_remove_last(&shape->contours);
        goto done;
    }

    box_msdf_deconverge(shape);
    box_msdf_color_contour(shape, &seed);
done:
    return;
}

void box_msdf_move_to(
    box_msdf_shape_t *shape,
    double x,
    double y)
{
    box_msdf_close(shape);
    *ecs_vec_append_t(NULL, &shape->contours, int32_t) = 0;
    shape->start = (box_msdf_point_t){ x, y };
    shape->pen = shape->start;
    shape->open = true;
}

/** Shoelace sum of each contour, after msdfgen's Contour::winding: positive for a contour
 * that runs clockwise with y up.
 *
 * @param shape The shape
 * @param windings Receives the sign of each contour, or NULL
 * @param totals Receives the sum of each contour, or NULL
 */
static void box_msdf_windings(
    const box_msdf_shape_t *shape,
    int32_t *windings,
    double *totals)
{
    const box_msdf_edge_t *edges = ecs_vec_first(&shape->edges);
    const int32_t *counts = ecs_vec_first(&shape->contours);
    int32_t i, k, first = 0, contours = ecs_vec_count(&shape->contours);
    for (k = 0; k < contours; k ++) {
        const box_msdf_edge_t *e = &edges[first];
        box_msdf_point_t pts[4];
        int32_t n = 0;
        double total = 0.0;
        if (counts[k] == 1) {
            pts[n ++] = box_msdf_at(e, 0.0);
            pts[n ++] = box_msdf_at(e, 1.0 / 3.0);
            pts[n ++] = box_msdf_at(e, 2.0 / 3.0);
        } else if (counts[k] == 2) {
            pts[n ++] = box_msdf_at(e, 0.0);
            pts[n ++] = box_msdf_at(e, 0.5);
            pts[n ++] = box_msdf_at(e + 1, 0.0);
            pts[n ++] = box_msdf_at(e + 1, 0.5);
        }

        for (i = 0; i < n; i ++) {
            box_msdf_point_t a = pts[i], b = pts[(i + 1) % n];
            total += (b.x - a.x) * (a.y + b.y);
        }

        for (i = 0; counts[k] > 2 && i < counts[k]; i ++) {
            box_msdf_point_t a = e[(i + counts[k] - 1) % counts[k]].p[0], b = e[i].p[0];
            total += (b.x - a.x) * (a.y + b.y);
        }

        if (windings) {
            windings[k] = total > 0.0 ? 1 : (total < 0.0 ? -1 : 0);
        }

        if (totals) {
            totals[k] = total;
        }

        first += counts[k];
    }
}

/** Reverse every contour when the largest one runs counter clockwise, so that the distances of
 * a well formed outline are positive inside.
 *
 * @param shape The shape
 */
static void box_msdf_orient(
    box_msdf_shape_t *shape)
{
    int32_t i, contours = ecs_vec_count(&shape->contours), count = ecs_vec_count(&shape->edges);
    int32_t first = 0, k, largest = 0;
    int32_t *counts = ecs_vec_first(&shape->contours);
    box_msdf_edge_t *edges = ecs_vec_first(&shape->edges);
    double *totals = ecs_os_malloc_n(double, contours);
    box_msdf_windings(shape, NULL, totals);
    for (k = 1; k < contours; k ++) {
        largest = fabs(totals[k]) > fabs(totals[largest]) ? k : largest;
    }

    if (totals[largest] >= 0.0) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        box_msdf_point_t p = edges[i].p[0];
        edges[i].p[0] = edges[i].p[2];
        edges[i].p[2] = p;
    }

    for (k = 0; k < contours; k ++) {
        for (i = 0; i < counts[k] / 2; i ++) {
            box_msdf_edge_t e = edges[first + i];
            edges[first + i] = edges[first + counts[k] - 1 - i];
            edges[first + counts[k] - 1 - i] = e;
        }

        first += counts[k];
    }

done:
    ecs_os_free(totals);
}

void box_msdf_shape_finish(
    box_msdf_shape_t *shape)
{
    box_msdf_close(shape);
    if (ecs_vec_count(&shape->contours)) {
        box_msdf_orient(shape);
    }
}

/** Collect where a horizontal line crosses the outline, flattening curves.
 *
 * @param shape The shape
 * @param y Font y of the line
 * @param out Receives box_msdf_cross_t
 */
static void box_msdf_crossings(
    const box_msdf_shape_t *shape,
    double y,
    ecs_vec_t *out)
{
    int32_t i, s, count = ecs_vec_count(&shape->edges);
    const box_msdf_edge_t *edges = ecs_vec_first(&shape->edges);
    ecs_vec_clear(out);
    for (i = 0; i < count; i ++) {
        int32_t steps = edges[i].degree == 2 ? BOX_MSDF_FLATTEN : 1;
        if (y < fmin(fmin(edges[i].p[0].y, edges[i].p[1].y), edges[i].p[2].y) ||
            y >= fmax(fmax(edges[i].p[0].y, edges[i].p[1].y), edges[i].p[2].y))
        {
            continue;
        }

        box_msdf_point_t a = edges[i].p[0];
        for (s = 1; s <= steps; s ++) {
            box_msdf_point_t b = s == steps
                ? edges[i].p[2]
                : box_msdf_at(&edges[i], (double)s / steps);
            if ((a.y <= y) != (b.y <= y)) {
                box_msdf_cross_t *c = ecs_vec_append_t(NULL, out, box_msdf_cross_t);
                c->x = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
                c->dir = b.y > a.y ? 1 : -1;
            }

            a = b;
        }
    }
}

/** Order crossings by x.
 *
 * @param a The first crossing
 * @param b The second crossing
 * @return Negative, zero or positive as a lies left of, at or right of b
 */
static int box_msdf_cross_compare(
    const void *a,
    const void *b)
{
    const box_msdf_cross_t *ca = a, *cb = b;

    return (ca->x > cb->x) - (ca->x < cb->x);
}

/** Scale a vector to unit length, keeping a zero vector zero.
 *
 * @param a The vector
 * @return The unit vector, or zero
 */
static box_msdf_point_t box_msdf_normalize_zero(
    box_msdf_point_t a)
{
    double len = box_msdf_length(a);
    box_msdf_point_t result = { 0.0, 0.0 };
    if (len > 0.0) {
        result = (box_msdf_point_t){ a.x / len, a.y / len };
    }

    return result;
}

/** Median of three doubles.
 *
 * @param v The values
 * @return The median
 */
static double box_msdf_median_d(
    const double *v)
{
    return fmax(fmin(v[0], v[1]), fmin(fmax(v[0], v[1]), v[2]));
}

float box_msdf_median3(
    float a,
    float b,
    float c)
{
    return glm_max(glm_min(a, b), glm_min(glm_max(a, b), c));
}

float box_msdf_median(
    const float *v)
{
    return box_msdf_median3(v[0], v[1], v[2]);
}

/** Take a perpendicular distance beyond a corner into a selector.
 *
 * @param sel The selector
 * @param distance The perpendicular distance
 */
static void box_msdf_sel_perpendicular(
    box_msdf_sel_t *sel,
    double distance)
{
    if (distance <= 0.0 && distance > sel->neg) {
        sel->neg = distance;
    }

    if (distance >= 0.0 && distance < sel->pos) {
        sel->pos = distance;
    }
}

/** Merge the edges of one selector into another.
 *
 * @param sel The selector that takes the nearest of both
 * @param other The other selector
 */
static void box_msdf_sel_merge(
    box_msdf_sel_t *sel,
    const box_msdf_sel_t *other)
{
    if (box_msdf_less(other->near, sel->near)) {
        sel->near = other->near;
        sel->edge = other->edge;
        sel->param = other->param;
    }

    sel->neg = other->neg > sel->neg ? other->neg : sel->neg;
    sel->pos = other->pos < sel->pos ? other->pos : sel->pos;
}

/** Pseudo distance a selector gives a point: its nearest edge extended past the ends, or a
 * perpendicular distance beyond a corner when that is nearer.
 *
 * @param sel The selector
 * @param prep The constants of the edges
 * @param p The point
 * @return The signed distance, positive inside
 */
static double box_msdf_sel_distance(
    const box_msdf_sel_t *sel,
    const box_msdf_prep_t *prep,
    box_msdf_point_t p)
{
    double result = sel->near.distance < 0.0 ? sel->neg : sel->pos;
    if (sel->edge >= 0) {
        box_msdf_dist_t d = sel->near;
        box_msdf_pseudo(&prep[sel->edge], p, sel->param, &d);
        if (fabs(d.distance) < fabs(result)) {
            result = d.distance;
        }
    }

    return result;
}

/** Whether two selectors hold the same nearest edge and perpendicular distances, so that they
 * give a point the same distance.
 *
 * @param a The first selector
 * @param b The second selector
 * @return Whether the selectors are the same
 */
static bool box_msdf_sel_same(
    const box_msdf_sel_t *a,
    const box_msdf_sel_t *b)
{
    return a->edge == b->edge && a->param == b->param && a->near.distance == b->near.distance &&
        a->near.dot == b->near.dot && a->neg == b->neg && a->pos == b->pos;
}

/** Distance from a point to the line through an edge end, when the point lies ahead of it.
 *
 * @param distance The distance so far, replaced when the perpendicular distance is nearer
 * @param ep The point relative to the end
 * @param dir Unit direction from the end towards the side being tested
 * @return Whether the distance was replaced
 */
static bool box_msdf_perpendicular(
    double *distance,
    box_msdf_point_t ep,
    box_msdf_point_t dir)
{
    double pd = box_msdf_cross(ep, dir);
    bool result = false;
    if (box_msdf_dot(ep, dir) > 0.0 && fabs(pd) < fabs(*distance)) {
        *distance = pd;
        result = true;
    }

    return result;
}

/** Lower bound of the distance from a point to an edge: the distance to the box around the
 * points of the edge, which holds the curve, less a margin for the rounding of the distance.
 *
 * @param ep The constants of the edge
 * @param p The point
 * @return The bound, the computed distance to the edge is never smaller
 */
static double box_msdf_bound(
    const box_msdf_prep_t *ep,
    box_msdf_point_t p)
{
    double dx = fmax(fmax(ep->min.x - p.x, p.x - ep->max.x), 0.0);
    double dy = fmax(fmax(ep->min.y - p.y, p.y - ep->max.y), 0.0);

    return sqrt(dx * dx + dy * dy) * (1.0 - BOX_MSDF_BOUND_SLACK) -
        BOX_MSDF_BOUND_SLACK * (ep->extent + fabs(p.x) + fabs(p.y));
}

/** Add an edge to the selectors of the channels it shapes.
 *
 * An edge whose bound exceeds the nearest distance of each of its channels so far, or that of
 * the probe, skips its distance: it cannot become the nearest edge, and its perpendicular
 * distances only count below the bound, as larger ones lose to the pseudo distance.
 *
 * The field is the same as when every distance is computed.
 *
 * @param multi The selectors of red, green and blue
 * @param edges The edges of the shape
 * @param prep The constants of the edges
 * @param probe The distances the nearest edges of the previous texel have at the point
 * @param edge Index of the edge
 * @param mask Channels the edge shapes
 * @param p The point
 */
static void box_msdf_add_edge(
    box_msdf_multi_t *multi,
    const box_msdf_edge_t *edges,
    const box_msdf_prep_t *prep,
    const box_msdf_probe_t *probe,
    int32_t edge,
    int32_t mask,
    box_msdf_point_t p)
{
    const box_msdf_edge_t *e = &edges[edge];
    const box_msdf_prep_t *ep = &prep[edge];
    double param = 0.0, bound = box_msdf_bound(ep, p);
    box_msdf_dist_t d = { bound, 0.0 };
    bool far = true;
    int32_t c;
    for (c = 0; c < 3 && far && !(bound > probe->most); c ++) {
        double near = fabs(multi->c[c].near.distance);
        double limit = probe->limit[c] < near ? probe->limit[c] : near;
        if ((mask & (1 << c)) && !(bound > limit)) {
            far = false;
        }
    }

    for (c = 0; c < 3 && !far; c ++) {
        if (probe->edge[c] == edge) {
            d = probe->dist[c];
            param = probe->param[c];
            break;
        }
    }

    if (!far && c == 3) {
        d = box_msdf_edge_distance(e, p, &param);
    }

    box_msdf_point_t ap = { p.x - e->p[0].x, p.y - e->p[0].y };
    box_msdf_point_t bp = { p.x - e->p[2].x, p.y - e->p[2].y };
    double add = ap.x * ep->a_bis.x + ap.y * ep->a_bis.y;
    double bdd = -(bp.x * ep->b_bis.x + bp.y * ep->b_bis.y);
    double pa = d.distance, pb = d.distance;
    bool has_a = add > 0.0 &&
        box_msdf_perpendicular(&pa, ap, (box_msdf_point_t){ -ep->a_dir.x, -ep->a_dir.y });
    bool has_b = bdd > 0.0 && box_msdf_perpendicular(&pb, bp, ep->b_dir);
    if (far && !has_a && !has_b) {
        goto done;
    }

    for (c = 0; c < 3; c ++) {
        box_msdf_sel_t *sel = &multi->c[c];
        if (!(mask & (1 << c))) {
            continue;
        }

        if (!far && box_msdf_less(d, sel->near)) {
            sel->near = d;
            sel->edge = edge;
            sel->param = param;
        }

        if (has_a) {
            box_msdf_sel_perpendicular(sel, -pa);
        }

        if (has_b) {
            box_msdf_sel_perpendicular(sel, pb);
        }
    }

done:
    return;
}

/** Distances of the selectors of a set of edges: the three channels and the true distance.
 *
 * @param multi The selectors
 * @param prep The constants of the edges
 * @param p The point
 * @return The distances
 */
static box_msdf_value_t box_msdf_multi_value(
    const box_msdf_multi_t *multi,
    const box_msdf_prep_t *prep,
    box_msdf_point_t p)
{
    box_msdf_value_t result;
    box_msdf_dist_t near = multi->c[0].near;
    int32_t c;
    for (c = 0; c < 3; c ++) {
        const box_msdf_sel_t *sel = &multi->c[c], *prev = &multi->c[c ? c - 1 : 0];
        if (c && box_msdf_sel_same(sel, prev)) {
            result.v[c] = result.v[c - 1];
        } else {
            result.v[c] = box_msdf_sel_distance(sel, prep, p);
        }

        if (box_msdf_less(multi->c[c].near, near)) {
            near = multi->c[c].near;
        }
    }

    result.v[3] = near.distance;

    return result;
}

/** Merge the selectors of a contour into a set.
 *
 * @param multi The set
 * @param other The selectors of the contour
 */
static void box_msdf_multi_merge(
    box_msdf_multi_t *multi,
    const box_msdf_multi_t *other)
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        box_msdf_sel_merge(&multi->c[c], &other->c[c]);
    }
}

/** Reset the selectors of a set.
 *
 * @param multi The set
 */
static void box_msdf_multi_init(
    box_msdf_multi_t *multi)
{
    static const box_msdf_multi_t empty = {{
        { { -BOX_MSDF_FAR, 0.0 }, -BOX_MSDF_FAR, BOX_MSDF_FAR, -1, 0.0 },
        { { -BOX_MSDF_FAR, 0.0 }, -BOX_MSDF_FAR, BOX_MSDF_FAR, -1, 0.0 },
        { { -BOX_MSDF_FAR, 0.0 }, -BOX_MSDF_FAR, BOX_MSDF_FAR, -1, 0.0 }
    }};
    *multi = empty;
}

/** Compute the distances of the nearest edges a contour had at the previous texel, which
 * bound the nearest distance of each channel at the point.
 *
 * @param multi The selectors of the contour, still holding the previous texel
 * @param edges The edges of the shape
 * @param p The point
 * @param probe Receives the edges, their distances and the bound of each channel
 */
static void box_msdf_probe(
    const box_msdf_multi_t *multi,
    const box_msdf_edge_t *edges,
    box_msdf_point_t p,
    box_msdf_probe_t *probe)
{
    int32_t c, k;
    for (c = 0; c < 3; c ++) {
        int32_t edge = multi->c[c].edge, same = -1;
        probe->edge[c] = edge;
        probe->limit[c] = BOX_MSDF_FAR;
        if (edge < 0) {
            continue;
        }

        for (k = 0; k < c; k ++) {
            same = probe->edge[k] == edge ? k : same;
        }

        if (same >= 0) {
            probe->dist[c] = probe->dist[same];
            probe->param[c] = probe->param[same];
        } else {
            probe->dist[c] = box_msdf_edge_distance(&edges[edge], p, &probe->param[c]);
        }

        probe->limit[c] = fabs(probe->dist[c].distance);
    }

    probe->most = fmax(fmax(probe->limit[0], probe->limit[1]), probe->limit[2]);
}

/** Compute the row terms of an end of an edge.
 *
 * @param end Receives the terms
 * @param at The end
 * @param dir Unit direction away from the edge
 * @param bis Unit bisector, pointing away from the corner
 * @param y The y of the row
 */
static void box_msdf_cull_end(
    box_msdf_cull_end_t *end,
    box_msdf_point_t at,
    box_msdf_point_t dir,
    box_msdf_point_t bis,
    double y)
{
    double dy = y - at.y;
    end->x0 = at.x;
    end->ux = dir.x;
    end->uy = dir.y;
    end->bx = bis.x;
    end->ahead = dy * dir.y;
    end->side = dy * bis.y;
    end->perp = dy * dir.x;
}

/** Compute the constants of every edge for a row, and start the spans of the contours anew.
 *
 * @param gen The generation, its cull and span are filled
 * @param y The y of the row
 */
static void box_msdf_cull_row(
    box_msdf_gen_t *gen,
    double y)
{
    const box_msdf_edge_t *edges = ecs_vec_first(&gen->shape->edges);
    int32_t i, count = ecs_vec_count(&gen->shape->edges);
    int32_t contours = ecs_vec_count(&gen->shape->contours);
    for (i = 0; i < count; i ++) {
        const box_msdf_prep_t *ep = &gen->prep[i];
        box_msdf_cull_t *cull = &gen->cull[i];
        box_msdf_cull_end(&cull->a, edges[i].p[0],
            (box_msdf_point_t){ -ep->a_dir.x, -ep->a_dir.y }, ep->a_bis, y);
        box_msdf_cull_end(&cull->b, edges[i].p[2], ep->b_dir,
            (box_msdf_point_t){ -ep->b_bis.x, -ep->b_bis.y }, y);
        cull->dy = fmax(fmax(ep->min.y - y, y - ep->max.y), 0.0);
        cull->slack = BOX_MSDF_BOUND_SLACK * (ep->extent + fabs(y));
    }

    for (i = 0; i < contours; i ++) {
        gen->span[i].x1 = -BOX_MSDF_FAR;
    }
}

/** Whether an end of an edge has no perpendicular distance up to a limit along a span.
 *
 * The terms are linear in x, so their extremes along the span are at its ends.
 *
 * @param end The terms of the end
 * @param x0 The x of the first texel
 * @param x1 The x of the last texel
 * @param limit The limit
 * @param margin Margin for rounding
 * @return Whether each texel lies behind the end, on the other side of the corner bisector, or
 *         farther than the limit from the line through the end
 */
static bool box_msdf_cull_end_far(
    const box_msdf_cull_end_t *end,
    double x0,
    double x1,
    double limit,
    double margin)
{
    double d0 = x0 - end->x0, d1 = x1 - end->x0;
    double p0 = d0 * end->uy - end->perp, p1 = d1 * end->uy - end->perp;

    return fmax(d0 * end->ux, d1 * end->ux) + end->ahead < -margin ||
        fmax(d0 * end->bx, d1 * end->bx) + end->side < -margin ||
        fmin(p0, p1) > limit + margin || fmax(p0, p1) < -limit - margin;
}

/** Whether an edge can be left out of the texels of a span: it is farther than the limit, and
 * so are the perpendicular distances at its ends.
 *
 * Where the probe limits of the channels of the edge stay below the limit, the edge cannot be
 * the nearest edge, and a perpendicular distance above it loses to the pseudo distance of the
 * nearest edge, so the field is the same as with the edge.
 *
 * @param gen The generation
 * @param edge Index of the edge
 * @param x0 The x of the first texel
 * @param x1 The x of the last texel
 * @param limit The limit
 * @return Whether the edge can be left out
 */
static bool box_msdf_culled(
    const box_msdf_gen_t *gen,
    int32_t edge,
    double x0,
    double x1,
    double limit)
{
    const box_msdf_cull_t *cull = &gen->cull[edge];
    const box_msdf_prep_t *ep = &gen->prep[edge];
    double margin = 2.0 * (cull->slack + BOX_MSDF_BOUND_SLACK * fmax(fabs(x0), fabs(x1)));
    double dx = fmax(fmax(ep->min.x - x1, x0 - ep->max.x), 0.0);

    return fmax(dx, cull->dy) * (1.0 - BOX_MSDF_BOUND_SLACK) - 2.0 * margin > limit &&
        box_msdf_cull_end_far(&cull->a, x0, x1, limit, margin) &&
        box_msdf_cull_end_far(&cull->b, x0, x1, limit, margin);
}

/** Start a span of a contour at a texel: the edges that are not culled along the next texels
 * of the row, up to limits that leave room for the nearest distances to grow.
 *
 * @param gen The generation
 * @param k Index of the contour
 * @param first Index of the first edge of the contour
 * @param count Number of edges of the contour
 * @param x The x of the texel
 * @param probe The probe of the texel, with the limit of each channel
 */
static void box_msdf_span(
    box_msdf_gen_t *gen,
    int32_t k,
    int32_t first,
    int32_t count,
    double x,
    const box_msdf_probe_t *probe)
{
    const box_msdf_edge_t *edges = ecs_vec_first(&gen->shape->edges);
    box_msdf_span_t *span = &gen->span[k];
    double x1 = x + (double)BOX_MSDF_SPAN / gen->raster->scale;
    int32_t i, c;
    span->x1 = x1;
    for (c = 0; c < 3; c ++) {
        span->limit[c] = probe->limit[c] + (x1 - x);
    }

    span->count = 0;
    for (i = 0; i < count; i ++) {
        double limit = 0.0;
        for (c = 0; c < 3; c ++) {
            if (edges[first + i].color & (1 << c)) {
                limit = fmax(limit, span->limit[c]);
            }
        }

        if (!box_msdf_culled(gen, first + i, x, x1, limit)) {
            gen->span_edges[first + span->count] = first + i;
            span->count ++;
        }
    }
}

/** Run the edges of each contour through the selectors of the contour, leaving out the edges
 * the span of the contour culls.
 *
 * @param gen The generation, its multi and contour scratch are filled
 * @param p The point
 */
static void box_msdf_contours(
    box_msdf_gen_t *gen,
    box_msdf_point_t p)
{
    const box_msdf_edge_t *edges = ecs_vec_first(&gen->shape->edges);
    const int32_t *counts = ecs_vec_first(&gen->shape->contours);
    int32_t i, k, first = 0, contours = ecs_vec_count(&gen->shape->contours);
    box_msdf_probe_t probe;
    for (k = 0; k < contours; k ++) {
        const box_msdf_span_t *span = &gen->span[k];
        int32_t count = counts[k];
        box_msdf_probe(&gen->multi[k], edges, p, &probe);
        box_msdf_multi_init(&gen->multi[k]);
        if (p.x > span->x1 || !(probe.limit[0] <= span->limit[0]) ||
            !(probe.limit[1] <= span->limit[1]) || !(probe.limit[2] <= span->limit[2]))
        {
            box_msdf_span(gen, k, first, count, p.x, &probe);
        }

        for (i = 0; i < span->count; i ++) {
            int32_t edge = gen->span_edges[first + i];
            box_msdf_add_edge(&gen->multi[k], edges, gen->prep, &probe, edge,
                edges[edge].color, p);
        }

        gen->contour[k] = box_msdf_multi_value(&gen->multi[k], gen->prep, p);
        first += count;
    }
}

/** Compute the constants of every edge: its unit directions, the unit bisectors of the corners
 * at its ends and the box around its points.
 *
 * @param shape The shape
 * @param prep Receives the constants of each edge
 */
static void box_msdf_prepare(
    const box_msdf_shape_t *shape,
    box_msdf_prep_t *prep)
{
    const box_msdf_edge_t *edges = ecs_vec_first(&shape->edges);
    const int32_t *counts = ecs_vec_first(&shape->contours);
    int32_t i, j, k, first = 0, count = ecs_vec_count(&shape->edges);
    int32_t contours = ecs_vec_count(&shape->contours);
    for (i = 0; i < count; i ++) {
        const box_msdf_edge_t *e = &edges[i];
        box_msdf_prep_t *ep = &prep[i];
        ep->a_end = box_msdf_at(e, 0.0);
        ep->b_end = box_msdf_at(e, 1.0);
        ep->a_unit = box_msdf_normalize(box_msdf_direction(e, 0.0));
        ep->b_unit = box_msdf_normalize(box_msdf_direction(e, 1.0));
        ep->a_dir = box_msdf_normalize_zero(box_msdf_direction(e, 0.0));
        ep->b_dir = box_msdf_normalize_zero(box_msdf_direction(e, 1.0));
        ep->min = ep->max = e->p[0];
        ep->extent = 0.0;
        for (j = 0; j < 3; j ++) {
            ep->min.x = fmin(ep->min.x, e->p[j].x);
            ep->min.y = fmin(ep->min.y, e->p[j].y);
            ep->max.x = fmax(ep->max.x, e->p[j].x);
            ep->max.y = fmax(ep->max.y, e->p[j].y);
            ep->extent = fmax(ep->extent, fmax(fabs(e->p[j].x), fabs(e->p[j].y)));
        }

        ep->extent += 1.0;
    }

    for (k = 0; k < contours; k ++) {
        int32_t n = counts[k];
        for (i = 0; i < n; i ++) {
            box_msdf_prep_t *ep = &prep[first + i];
            box_msdf_point_t prev_dir = prep[first + (i + n - 1) % n].b_dir;
            box_msdf_point_t next_dir = prep[first + (i + 1) % n].a_dir;
            box_msdf_point_t a_bis = { prev_dir.x + ep->a_dir.x, prev_dir.y + ep->a_dir.y };
            box_msdf_point_t b_bis = { ep->b_dir.x + next_dir.x, ep->b_dir.y + next_dir.y };
            ep->a_bis = box_msdf_normalize_zero(a_bis);
            ep->b_bis = box_msdf_normalize_zero(b_bis);
        }

        first += n;
    }
}

/** Distances of a point to a shape whose contours may overlap, after msdfgen's
 * OverlappingContourCombiner: an edge inside the fill of another contour does not count.
 *
 * @param gen The generation
 * @param p The point
 * @return The distances, positive inside a contour that winds positively
 */
static box_msdf_value_t box_msdf_combine(
    box_msdf_gen_t *gen,
    box_msdf_point_t p)
{
    int32_t k, winding = 0, contours = ecs_vec_count(&gen->shape->contours);
    box_msdf_multi_t all, inner, outer;
    box_msdf_contours(gen, p);
    box_msdf_value_t result = gen->contour[0];
    if (contours == 1) {
        goto done;
    }

    box_msdf_multi_init(&all);
    box_msdf_multi_init(&inner);
    box_msdf_multi_init(&outer);
    for (k = 0; k < contours; k ++) {
        double d = box_msdf_median_d(gen->contour[k].v);
        box_msdf_multi_merge(&all, &gen->multi[k]);
        if (gen->windings[k] > 0 && d >= 0.0) {
            box_msdf_multi_merge(&inner, &gen->multi[k]);
        }

        if (gen->windings[k] < 0 && d <= 0.0) {
            box_msdf_multi_merge(&outer, &gen->multi[k]);
        }
    }

    box_msdf_value_t shape = box_msdf_multi_value(&all, gen->prep, p);
    box_msdf_value_t in = box_msdf_multi_value(&inner, gen->prep, p);
    box_msdf_value_t out = box_msdf_multi_value(&outer, gen->prep, p);
    double di = box_msdf_median_d(in.v), dout = box_msdf_median_d(out.v);
    result = shape;
    if (di >= 0.0 && fabs(di) <= fabs(dout)) {
        result = in;
        winding = 1;
    } else if (dout <= 0.0 && fabs(dout) < fabs(di)) {
        result = out;
        winding = -1;
    } else {
        goto done;
    }

    for (k = 0; k < contours; k ++) {
        double d = box_msdf_median_d(gen->contour[k].v), r = box_msdf_median_d(result.v);
        double bound = winding > 0 ? dout : di;
        if (gen->windings[k] == winding && fabs(d) < fabs(bound) &&
            (winding > 0 ? d > r : d < r))
        {
            result = gen->contour[k];
        }
    }

    for (k = 0; k < contours; k ++) {
        double d = box_msdf_median_d(gen->contour[k].v), r = box_msdf_median_d(result.v);
        if (gen->windings[k] != winding && d * r >= 0.0 && fabs(d) < fabs(r)) {
            result = gen->contour[k];
        }
    }

    if (box_msdf_median_d(result.v) == box_msdf_median_d(shape.v)) {
        result = shape;
    }

done:
    return result;
}

/** Mark the texels whose center lies within a distance of a box as near the outline.
 *
 * @param gen The generation
 * @param min Lower corner of the box, in texels of the field with y down
 * @param max Upper corner of the box
 * @param reach The distance in texels
 */
static void box_msdf_near_box(
    box_msdf_gen_t *gen,
    box_msdf_point_t min,
    box_msdf_point_t max,
    double reach)
{
    int32_t w = gen->raster->width, h = gen->raster->height;
    int32_t y = glm_imax((int32_t)ceil(min.y - reach), 0);
    int32_t y1 = glm_imin((int32_t)floor(max.y + reach), h - 1);
    for (; y <= y1; y ++) {
        double dy = fmax(fmax(min.y - y, y - max.y), 0.0);
        double dx = sqrt(fmax(reach * reach - dy * dy, 0.0));
        int32_t x = glm_imax((int32_t)ceil(min.x - dx), 0);
        int32_t x1 = glm_imin((int32_t)floor(max.x + dx), w - 1);
        if (x <= x1) {
            ecs_os_memset(&gen->near[y * w + x], 1, x1 - x + 1);
        }
    }
}

/** Distance from the outline from which on a texel only takes the side of the outline.
 *
 * @param raster The texels
 * @return The distance in texels
 */
static double box_msdf_reach(
    const box_msdf_raster_t *raster)
{
    return raster->range * 0.5 + BOX_MSDF_NEAR;
}

/** Mark the texels within box_msdf_reach of the outline as near. Each edge is split into
 * pieces of about BOX_MSDF_NEAR_PIECE texels, and the box around the control points of a piece
 * holds the piece, so a texel that is not marked is farther from the outline.
 *
 * @param gen The generation, its near is filled
 */
static void box_msdf_near(
    box_msdf_gen_t *gen)
{
    const box_msdf_raster_t *r = gen->raster;
    const box_msdf_edge_t *edges = ecs_vec_first(&gen->shape->edges);
    int32_t i, j, s, count = ecs_vec_count(&gen->shape->edges);
    double reach = box_msdf_reach(r);
    ecs_os_memset(gen->near, 0, r->width * r->height);
    for (i = 0; i < count; i ++) {
        box_msdf_point_t p[3];
        for (j = 0; j < 3; j ++) {
            p[j].x = (edges[i].p[j].x - r->left) * r->scale - 0.5;
            p[j].y = (r->top - edges[i].p[j].y) * r->scale - 0.5;
        }

        box_msdf_point_t d0 = box_msdf_sub(p[1], p[0]), d1 = box_msdf_sub(p[2], p[1]);
        double length = box_msdf_length(d0) + box_msdf_length(d1);
        int32_t steps = 1 + (int32_t)fmin(length / BOX_MSDF_NEAR_PIECE, (double)INT16_MAX);
        box_msdf_point_t a = p[0];
        for (s = 0; s < steps; s ++) {
            double t0 = (double)s / steps, t1 = (double)(s + 1) / steps;
            box_msdf_point_t b = box_msdf_mix(box_msdf_mix(p[0], p[1], t1),
                box_msdf_mix(p[1], p[2], t1), t1);
            box_msdf_point_t dir = box_msdf_mix(d0, d1, t0);
            box_msdf_point_t c = { a.x + dir.x * (t1 - t0), a.y + dir.y * (t1 - t0) };
            box_msdf_point_t min = { fmin(fmin(a.x, b.x), c.x), fmin(fmin(a.y, b.y), c.y) };
            box_msdf_point_t max = { fmax(fmax(a.x, b.x), c.x), fmax(fmax(a.y, b.y), c.y) };
            box_msdf_near_box(gen, min, max, reach);
            a = b;
        }
    }
}

/** Fill one row of texels: the distances, with the sign the nonzero fill gives.
 *
 * @param gen The generation
 * @param row The row
 * @param crossings Scratch vector of box_msdf_cross_t
 */
static void box_msdf_row(
    box_msdf_gen_t *gen,
    int32_t row,
    ecs_vec_t *crossings)
{
    const box_msdf_raster_t *raster = gen->raster;
    double y = raster->top - ((double)row + 0.5) / raster->scale;
    double to_value = raster->scale / raster->range;
    float far = (float)(box_msdf_reach(raster) / raster->range);
    int32_t x, c;
    box_msdf_crossings(gen->shape, y, crossings);
    box_msdf_cull_row(gen, y);
    box_msdf_cross_t *cross = ecs_vec_first(crossings);
    int32_t count = ecs_vec_count(crossings), passed = 0, winding = 0;
    qsort(cross, (size_t)count, sizeof(box_msdf_cross_t), box_msdf_cross_compare);
    for (c = 0; c < count; c ++) {
        winding += cross[c].dir;
    }

    for (x = 0; x < raster->width; x ++) {
        box_msdf_point_t p = { raster->left + ((double)x + 0.5) / raster->scale, y };
        float *v = &gen->values[(row * raster->width + x) * 4];
        for (; passed < count && !(cross[passed].x > p.x); passed ++) {
            winding -= cross[passed].dir;
        }

        bool inside = winding != 0;
        if (!gen->near[row * raster->width + x]) {
            v[0] = v[1] = v[2] = v[3] = 0.5f + (inside ? far : -far);
            continue;
        }

        box_msdf_value_t d = box_msdf_combine(gen, p);
        for (c = 0; c < 4; c ++) {
            v[c] = (float)fmax(-BOX_MSDF_VALUE_LIMIT,
                fmin(BOX_MSDF_VALUE_LIMIT, d.v[c] * to_value + 0.5));
        }

        if ((box_msdf_median(v) > 0.5f) != inside && box_msdf_median(v) != 0.5f) {
            v[0] = 1.0f - v[0];
            v[1] = 1.0f - v[1];
            v[2] = 1.0f - v[2];
        }

        if ((v[3] > 0.5f) != inside) {
            v[3] = 1.0f - v[3];
        }
    }
}

void box_msdf_generate(
    const box_msdf_shape_t *shape,
    const box_msdf_raster_t *raster,
    uint8_t *pixels)
{
    int32_t i, y, texels = raster->width * raster->height;
    int32_t contours = ecs_vec_count(&shape->contours);
    box_msdf_gen_t gen = { .shape = shape, .raster = raster };
    ecs_vec_t crossings;
    ecs_os_memset(pixels, 0, texels * 4);
    if (!ecs_vec_count(&shape->edges) || !texels) {
        goto done;
    }

    gen.values = ecs_os_malloc_n(float, texels * 4);
    gen.stencil = ecs_os_calloc_n(uint8_t, texels);
    gen.near = ecs_os_malloc_n(uint8_t, texels);
    gen.windings = ecs_os_malloc_n(int32_t, contours);
    gen.prep = ecs_os_malloc_n(box_msdf_prep_t, ecs_vec_count(&shape->edges));
    gen.cull = ecs_os_malloc_n(box_msdf_cull_t, ecs_vec_count(&shape->edges));
    gen.span = ecs_os_malloc_n(box_msdf_span_t, contours);
    gen.span_edges = ecs_os_malloc_n(int32_t, ecs_vec_count(&shape->edges));
    gen.multi = ecs_os_malloc_n(box_msdf_multi_t, contours);
    gen.contour = ecs_os_malloc_n(box_msdf_value_t, contours);
    box_msdf_windings(shape, gen.windings, NULL);
    box_msdf_prepare(shape, gen.prep);
    box_msdf_near(&gen);
    for (i = 0; i < contours; i ++) {
        box_msdf_multi_init(&gen.multi[i]);
    }

    ecs_vec_init_t(NULL, &crossings, box_msdf_cross_t, 0);
    for (y = 0; y < raster->height; y ++) {
        box_msdf_row(&gen, y, &crossings);
    }

    ecs_vec_fini_t(NULL, &crossings, box_msdf_cross_t);
    box_msdf_correct(&gen);
    for (i = 0; i < texels * 4; i ++) {
        pixels[i] = (uint8_t)(glm_clamp(gen.values[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    ecs_os_free(gen.contour);
    ecs_os_free(gen.multi);
    ecs_os_free(gen.span_edges);
    ecs_os_free(gen.span);
    ecs_os_free(gen.cull);
    ecs_os_free(gen.prep);
    ecs_os_free(gen.windings);
    ecs_os_free(gen.near);
    ecs_os_free(gen.stencil);
    ecs_os_free(gen.values);
done:
    return;
}
