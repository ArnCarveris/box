/**
 * @file text/msdf.c
 * @brief Multi channel signed distance fields of outlines, after the method of msdfgen.
 *
 * Edges are colored so that two channels meet at every corner; a texel takes per channel the
 * pseudo distance of the nearest edge of that channel, its sign follows the nonzero fill and
 * texels whose channels clash with a neighbor are flattened to their median.
 */

#include "msdf.h"
#include <math.h>

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

/** Texels of difference between two neighbor texels from which their channels clash */
#define BOX_MSDF_CLASH (1.001)

/** A distance that is larger than any distance of a glyph */
#define BOX_MSDF_FAR (1e240)

/** Distance from a point to an edge, ordered by magnitude, then by how head on the edge is */
typedef struct box_msdf_dist_t {
    double distance; /**< Signed distance in font units, positive inside */
    double dot;      /**< Cosine between the edge end and the point, 0 along the edge */
} box_msdf_dist_t;

/** Nearest edge of a channel and where on the edge the nearest point is */
typedef struct box_msdf_near_t {
    box_msdf_dist_t dist;  /**< Distance to the edge */
    int32_t edge;          /**< Index of the edge, -1 before one is found */
    double param;          /**< Parameter of the nearest point, outside 0..1 beyond an end */
} box_msdf_near_t;

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

/** Solve a x^2 + b x + c = 0.
 *
 * @param x Receives the roots
 * @param a The quadratic coefficient
 * @param b The linear coefficient
 * @param c The constant
 * @return The number of roots
 */
static int32_t box_msdf_quadratic(
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
 * @param e The edge
 * @param origin The point
 * @param param Parameter of the nearest point on the edge
 * @param dist The distance, replaced by the pseudo distance when that is nearer
 */
static void box_msdf_pseudo(
    const box_msdf_edge_t *e,
    box_msdf_point_t origin,
    double param,
    box_msdf_dist_t *dist)
{
    double end = param < 0.0 ? 0.0 : 1.0;
    if (param >= 0.0 && param <= 1.0) {
        goto done;
    }

    box_msdf_point_t dir = box_msdf_normalize(box_msdf_direction(e, end));
    box_msdf_point_t q = box_msdf_sub(origin, box_msdf_at(e, end));
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

void box_msdf_shape_finish(
    box_msdf_shape_t *shape)
{
    box_msdf_close(shape);
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

/** Find the nearest edge of each channel and of all channels for a point.
 *
 * @param shape The shape
 * @param p The point
 * @param near Receives the nearest edge of red, green, blue and of any channel
 */
static void box_msdf_nearest(
    const box_msdf_shape_t *shape,
    box_msdf_point_t p,
    box_msdf_near_t *near)
{
    int32_t i, c, count = ecs_vec_count(&shape->edges);
    const box_msdf_edge_t *edges = ecs_vec_first(&shape->edges);
    for (c = 0; c < 4; c ++) {
        near[c] = (box_msdf_near_t){ .dist = { BOX_MSDF_FAR, 1.0 }, .edge = -1 };
    }

    for (i = 0; i < count; i ++) {
        double param;
        box_msdf_dist_t d = box_msdf_edge_distance(&edges[i], p, &param);
        for (c = 0; c < 4; c ++) {
            bool channel = c == 3 || (edges[i].color & (1 << c));
            if (channel && box_msdf_less(d, near[c].dist)) {
                near[c] = (box_msdf_near_t){ .dist = d, .edge = i, .param = param };
            }
        }
    }
}

/** Compute the four channels of a texel, before the sign follows the fill.
 *
 * @param shape The shape
 * @param raster The texels
 * @param p Font position of the texel center
 * @param out Receives red, green, blue and the true distance, 0.5 on the outline
 */
static void box_msdf_texel(
    const box_msdf_shape_t *shape,
    const box_msdf_raster_t *raster,
    box_msdf_point_t p,
    float *out)
{
    const box_msdf_edge_t *edges = ecs_vec_first(&shape->edges);
    double to_value = raster->scale / raster->range;
    box_msdf_near_t near[4];
    int32_t c;
    box_msdf_nearest(shape, p, near);
    for (c = 0; c < 3; c ++) {
        if (near[c].edge < 0) {
            near[c] = near[3];
        }

        if (near[c].edge >= 0) {
            box_msdf_pseudo(&edges[near[c].edge], p, near[c].param, &near[c].dist);
        }

        out[c] = (float)(near[c].dist.distance * to_value + 0.5);
    }

    out[3] = (float)(near[3].dist.distance * to_value + 0.5);
}

/** Median of three values.
 *
 * @param v The values
 * @return The median
 */
static float box_msdf_median(
    const float *v)
{
    return glm_max(glm_min(v[0], v[1]), glm_min(glm_max(v[0], v[1]), v[2]));
}

/** Swap the values of a channel of two texels.
 *
 * @param a The channels of the first texel
 * @param b The channels of the second texel
 * @param i The first channel
 * @param j The second channel
 */
static void box_msdf_swap(
    float *a,
    float *b,
    int32_t i,
    int32_t j)
{
    float ta = a[i], tb = b[i];
    a[i] = a[j];
    b[i] = b[j];
    a[j] = ta;
    b[j] = tb;
}

/** Check whether two neighbor texels interpolate into a false edge.
 *
 * @param pa The texel to test
 * @param pb Its neighbor
 * @param threshold Smallest channel difference that clashes
 * @return Whether a clashes with b and is the texel farther from the outline
 */
static bool box_msdf_clash(
    const float *pa,
    const float *pb,
    float threshold)
{
    float a[3] = { pa[0], pa[1], pa[2] }, b[3] = { pb[0], pb[1], pb[2] };
    if (fabsf(b[0] - a[0]) < fabsf(b[1] - a[1])) {
        box_msdf_swap(a, b, 0, 1);
    }

    if (fabsf(b[1] - a[1]) < fabsf(b[2] - a[2])) {
        box_msdf_swap(a, b, 1, 2);
        if (fabsf(b[0] - a[0]) < fabsf(b[1] - a[1])) {
            box_msdf_swap(a, b, 0, 1);
        }
    }

    return fabsf(b[1] - a[1]) >= threshold && !(b[0] == b[1] && b[0] == b[2]) &&
        fabsf(a[2] - 0.5f) >= fabsf(b[2] - 0.5f);
}

/** Flatten the texels that clash with a neighbor to the median of their channels.
 *
 * @param raster The texels
 * @param values Four floats per texel
 */
static void box_msdf_correct(
    const box_msdf_raster_t *raster,
    float *values)
{
    int32_t x, y, w = raster->width, h = raster->height, i, count = 0;
    float threshold = (float)(BOX_MSDF_CLASH / raster->range);
    int32_t *clashes = ecs_os_malloc_n(int32_t, w * h);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const float *v = &values[(y * w + x) * 4];
            if ((x > 0 && box_msdf_clash(v, v - 4, threshold)) ||
                (x < w - 1 && box_msdf_clash(v, v + 4, threshold)) ||
                (y > 0 && box_msdf_clash(v, v - w * 4, threshold)) ||
                (y < h - 1 && box_msdf_clash(v, v + w * 4, threshold)))
            {
                clashes[count ++] = y * w + x;
            }
        }
    }

    for (i = 0; i < count; i ++) {
        float *v = &values[clashes[i] * 4];
        float m = box_msdf_median(v);
        v[0] = v[1] = v[2] = m;
    }

    ecs_os_free(clashes);
}

/** Fill one row of texels: the distances, with the sign the nonzero fill gives.
 *
 * @param shape The shape
 * @param raster The texels
 * @param row The row
 * @param crossings Scratch vector of box_msdf_cross_t
 * @param values Four floats per texel of the row
 */
static void box_msdf_row(
    const box_msdf_shape_t *shape,
    const box_msdf_raster_t *raster,
    int32_t row,
    ecs_vec_t *crossings,
    float *values)
{
    double y = raster->top - ((double)row + 0.5) / raster->scale;
    int32_t x, c;
    box_msdf_crossings(shape, y, crossings);
    const box_msdf_cross_t *cross = ecs_vec_first(crossings);
    int32_t count = ecs_vec_count(crossings);
    for (x = 0; x < raster->width; x ++) {
        box_msdf_point_t p = { raster->left + ((double)x + 0.5) / raster->scale, y };
        float *v = &values[x * 4];
        int32_t winding = 0;
        for (c = 0; c < count; c ++) {
            winding += cross[c].x > p.x ? cross[c].dir : 0;
        }

        box_msdf_texel(shape, raster, p, v);
        bool inside = winding != 0;
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
    float *values = NULL;
    ecs_vec_t crossings;
    ecs_os_memset(pixels, 0, texels * 4);
    if (!ecs_vec_count(&shape->edges) || !texels) {
        goto done;
    }

    values = ecs_os_malloc_n(float, texels * 4);
    ecs_vec_init_t(NULL, &crossings, box_msdf_cross_t, 0);
    for (y = 0; y < raster->height; y ++) {
        box_msdf_row(shape, raster, y, &crossings, &values[y * raster->width * 4]);
    }

    ecs_vec_fini_t(NULL, &crossings, box_msdf_cross_t);
    box_msdf_correct(raster, values);
    for (i = 0; i < texels * 4; i ++) {
        pixels[i] = (uint8_t)(glm_clamp(values[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    ecs_os_free(values);
done:
    return;
}
