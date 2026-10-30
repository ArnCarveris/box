/**
 * @file text/msdf_correct.c
 * @brief Error correction of multi channel distance fields, after msdfgen's MSDFErrorCorrection.
 *
 * Texels at corners and edges are protected, texels whose interpolation with a neighbor leaves
 * the range the distance allows are flagged and flattened to the median of their channels.
 *
 * It runs after the scanline sign pass, so as msdfgen does with -scanline it never compares the
 * field with the distance of the contours, whose sign the pass may have overruled, except near
 * a corner, against the distance of the edges near it where the true distance or the fill agrees.
 */

#include "msdf.h"
#include <math.h>
#include <stdlib.h>

/** Texel flag: flattening the texel to its median removes an artifact */
#define BOX_MSDF_ERROR (1)

/** Texel flag: the texel shapes a corner or an edge, only inversions count as its artifacts */
#define BOX_MSDF_PROTECTED (2)

/** Range test flag: the interpolated median leaves the range of its ends */
#define BOX_MSDF_CANDIDATE (1)

/** Range test flag: the interpolated median deviates more than its distance allows */
#define BOX_MSDF_ARTIFACT (2)

/** Ratio of the expected change of a value between texels from which it deviates (msdfgen) */
#define BOX_MSDF_DEVIATION (1.11111111111111111)

/** Tolerance of the distance between two texels an edge between them can have (msdfgen) */
#define BOX_MSDF_PROTECT_TOLERANCE (1.001)

/** Interpolation positions this near a texel are not tested for artifacts (msdfgen) */
#define BOX_MSDF_ARTIFACT_T (0.01)

/** Cells along each side of the block around a corner that the corner repair tests */
#define BOX_MSDF_CORNER_CELLS (3)

/** Points along each side of a cell that the corner repair tests */
#define BOX_MSDF_CORNER_SAMPLES (16)

/** Texels from the outline from which on a point must be well onto its side of it */
#define BOX_MSDF_CORNER_DEEP (0.25)

/** Texels from the outline from which on a point must be onto its side of it by
 * BOX_MSDF_CORNER_NEAR, as Msdf.reflex_corner requires it to be on its side from a 32nd on */
#define BOX_MSDF_CORNER_SHARP (1.0 / 32.0)

/** Texels onto its side of the outline the interpolated median of a point from
 * BOX_MSDF_CORNER_SHARP on must reach, about a step of the 8 bit texels at a range of 4 */
#define BOX_MSDF_CORNER_NEAR (1.0 / 64.0)

/** Texels onto its side of the outline the interpolated median of such a point must reach,
 * twice what Msdf.reflex_corner requires so that the 8 bit texels keep it */
#define BOX_MSDF_CORNER_MARGIN (0.125)

/** Texels onto its side of the outline the interpolated median of a point from
 * BOX_MSDF_CORNER_SHARP on that reaches it must keep, about half a step of the 8 bit texels */
#define BOX_MSDF_CORNER_NEAR_FLOOR (1.0 / 128.0)

/** Texels onto its side of the outline the interpolated median of a deep point that reaches it
 * must keep, so that between the tested points and in 8 bits it keeps the 1/16 of
 * Msdf.reflex_corner */
#define BOX_MSDF_CORNER_MARGIN_FLOOR (7.0 / 64.0)

/** Texels by which the distance of the edges near a corner may exceed the interpolated true
 * distance, beyond which the point is not tested */
#define BOX_MSDF_CORNER_SLACK (0.5)

/** Points the corner repair tests in the block around a corner */
#define BOX_MSDF_CORNER_POINTS (BOX_MSDF_CORNER_CELLS * BOX_MSDF_CORNER_CELLS * \
    BOX_MSDF_CORNER_SAMPLES * BOX_MSDF_CORNER_SAMPLES)

/** Side of a point of the block around a corner that is not found yet */
#define BOX_MSDF_CORNER_UNKNOWN (INT8_MIN)

/** Points of the four cells around a texel of the four around a corner */
#define BOX_MSDF_CORNER_REACH (4 * BOX_MSDF_CORNER_SAMPLES * BOX_MSDF_CORNER_SAMPLES)

/** Moves of a channel the repair of a corner makes at most */
#define BOX_MSDF_CORNER_PASSES (8)

/** Times the repair goes over the corners at most, so that a corner whose texels the repair of
 * another corner moved is repaired again */
#define BOX_MSDF_CORNER_ROUNDS (2)

/** Amounts by which the repair tries to move a channel without changing the median of its
 * texel, from a fraction of a texel of distance, which lifts a point that is just short of its
 * margin without pushing its neighbors across theirs, to half the value range */
static const float box_msdf_corner_steps[] = {
    1.0f / 128.0f, 1.0f / 64.0f, 1.0f / 32.0f, 1.0f / 16.0f, 1.0f / 8.0f, 3.0f / 16.0f,
    1.0f / 4.0f, 3.0f / 8.0f, 1.0f / 2.0f
};

/** Number of amounts in box_msdf_corner_steps */
#define BOX_MSDF_CORNER_STEPS \
    ((int32_t)(sizeof(box_msdf_corner_steps) / sizeof(box_msdf_corner_steps[0])))

/** Tests interpolation between a texel and a neighbor, after msdfgen's artifact classifiers */
typedef struct box_msdf_class_t {
    double span;          /**< Largest change of a value expected per unit of interpolation */
    bool protect;         /**< The texel is protected: only inversions are artifacts */
    const float *texel;   /**< Channels of the texel */
    int32_t x;            /**< Column of the texel */
    int32_t y;            /**< Row of the texel */
} box_msdf_class_t;

/** The block of cells around a corner that the corner repair tests */
typedef struct box_msdf_block_t {
    int32_t l;            /**< Column of the first texel */
    int32_t b;            /**< Row of the first texel */
    const int32_t *edges; /**< The edges that can be the nearest to a tested point of the block */
    int32_t count;        /**< Number of edges */
    int8_t *sides;        /**< Side of each point, BOX_MSDF_CORNER_UNKNOWN until it is found. The
                           *   repair never moves the true distance, so a side holds for every
                           *   repair of the corner */
} box_msdf_block_t;

/** A point of the block around a corner whose count a move of a channel changes */
typedef struct box_msdf_reach_t {
    int32_t point;        /**< Index of the point */
    float wy;             /**< Weight of the moved texel along y at the point */
    float wx;             /**< Weight of the moved texel along x at the point */
} box_msdf_reach_t;

/** The state of the push of the channels around a corner */
typedef struct box_msdf_push_t {
    const box_msdf_gen_t *gen;                       /**< The generation */
    int32_t l;                                       /**< Column of the first texel */
    int32_t b;                                       /**< Row of the first texel */
    const int8_t *sides;                             /**< Side of each point */
    float before[BOX_MSDF_CORNER_POINTS];            /**< Median of each point before it */
    float channels[BOX_MSDF_CORNER_POINTS][3];       /**< Interpolated channels of each point */
    int32_t counts[BOX_MSDF_CORNER_POINTS];          /**< Count of each point, as
                                                      *   box_msdf_corner_point gives it */
    int32_t wrongs[BOX_MSDF_CORNER_POINTS];          /**< The points that count */
    int32_t wrong_count;                             /**< Number of points that count */
    box_msdf_reach_t reach[BOX_MSDF_CORNER_REACH];   /**< Points a move changes */
    int32_t reach_count;                             /**< Number of points a move changes */
    int32_t fixed;                                   /**< Wrong points a move does not change */
} box_msdf_push_t;

/** A corner the corner repair repairs */
typedef struct box_msdf_corner_t {
    int32_t prev;         /**< The edge that ends at the corner */
    int32_t next;         /**< The edge that starts at the corner */
    int32_t l;            /**< Column of the first texel of its block */
    int32_t b;            /**< Row of the first texel of its block */
    int32_t done;         /**< Stamp of its last repair, 0 before the first */
    int32_t moved;        /**< Stamp of the last repair that moved a channel, 0 for none */
} box_msdf_corner_t;

/** Mark the texel at a position protected when it is on the field.
 *
 * @param gen The generation
 * @param x Column
 * @param y Row
 */
static void box_msdf_protect(
    box_msdf_gen_t *gen,
    int32_t x,
    int32_t y)
{
    if (x >= 0 && y >= 0 && x < gen->raster->width && y < gen->raster->height) {
        gen->stencil[y * gen->raster->width + x] |= BOX_MSDF_PROTECTED;
    }
}

/** Protect the four texels around every corner, where the color of the edges changes.
 *
 * @param gen The generation
 */
static void box_msdf_protect_corners(
    box_msdf_gen_t *gen)
{
    const box_msdf_raster_t *r = gen->raster;
    const box_msdf_edge_t *edges = ecs_vec_first(&gen->shape->edges);
    const int32_t *counts = ecs_vec_first(&gen->shape->contours);
    int32_t i, k, first = 0, contours = ecs_vec_count(&gen->shape->contours);
    for (k = 0; k < contours; k ++) {
        int32_t prev = first + counts[k] - 1;
        for (i = first; i < first + counts[k]; i ++) {
            int32_t common = edges[prev].color & edges[i].color;
            if (!(common & (common - 1))) {
                double px = (edges[i].p[0].x - r->left) * r->scale;
                double py = (r->top - edges[i].p[0].y) * r->scale;
                int32_t l = (int32_t)floor(px - 0.5), b = (int32_t)floor(py - 0.5);
                box_msdf_protect(gen, l, b);
                box_msdf_protect(gen, l + 1, b);
                box_msdf_protect(gen, l, b + 1);
                box_msdf_protect(gen, l + 1, b + 1);
            }

            prev = i;
        }

        first += counts[k];
    }
}

/** Check whether a channel is the median where it crosses the outline between two texels.
 *
 * @param a The channels of the first texel
 * @param b The channels of the second texel
 * @param channel The channel
 * @return Whether the channel shapes an edge between the texels
 */
static bool box_msdf_edge_channel(
    const float *a,
    const float *b,
    int32_t channel)
{
    double t = (a[channel] - 0.5) / (a[channel] - b[channel]);
    bool result = false;
    if (a[channel] != b[channel] && t > 0.0 && t < 1.0) {
        float c[3];
        int32_t i;
        for (i = 0; i < 3; i ++) {
            c[i] = (float)((1.0 - t) * a[i] + t * b[i]);
        }

        result = box_msdf_median(c) == c[channel];
    }

    return result;
}

/** Protect the texels of a pair whose extreme channels shape an edge between them.
 *
 * @param gen The generation
 * @param ia Index of the first texel
 * @param ib Index of the second texel
 * @param radius Largest sum of distances to the outline of a pair an edge can lie between
 */
static void box_msdf_protect_pair(
    box_msdf_gen_t *gen,
    int32_t ia,
    int32_t ib,
    float radius)
{
    const float *a = &gen->values[ia * 4], *b = &gen->values[ib * 4];
    float am = gen->medians[ia], bm = gen->medians[ib];
    int32_t mask = 0, c;
    if (fabsf(am - 0.5f) + fabsf(bm - 0.5f) >= radius) {
        goto done;
    }

    for (c = 0; c < 3; c ++) {
        mask |= box_msdf_edge_channel(a, b, c) ? 1 << c : 0;
    }

    for (c = 0; c < 3; c ++) {
        if ((mask & (1 << c)) && a[c] != am) {
            gen->stencil[ia] |= BOX_MSDF_PROTECTED;
        }

        if ((mask & (1 << c)) && b[c] != bm) {
            gen->stencil[ib] |= BOX_MSDF_PROTECTED;
        }
    }

done:
    return;
}

/** Protect the texels whose extreme channels shape the edges of the outline. A texel whose
 * median alone is farther from 0.5 than the radius of a pair has no edge to a neighbor.
 *
 * @param gen The generation
 */
static void box_msdf_protect_edges(
    box_msdf_gen_t *gen)
{
    int32_t x, y, w = gen->raster->width, h = gen->raster->height;
    float straight = (float)(BOX_MSDF_PROTECT_TOLERANCE / gen->raster->range);
    float diagonal = (float)(BOX_MSDF_PROTECT_TOLERANCE * GLM_SQRT2 / gen->raster->range);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            int32_t i = y * w + x;
            bool far = fabsf(gen->medians[i] - 0.5f) >= diagonal;
            if (!far && x < w - 1) {
                box_msdf_protect_pair(gen, i, i + 1, straight);
            }

            if (!far && y < h - 1) {
                box_msdf_protect_pair(gen, i, i + w, straight);
            }

            if (x < w - 1 && y < h - 1) {
                if (!far) {
                    box_msdf_protect_pair(gen, i, i + w + 1, diagonal);
                }

                box_msdf_protect_pair(gen, i + 1, i + w, diagonal);
            }
        }
    }
}

/** Classify an interpolated median between two ends, after msdfgen's rangeTest.
 *
 * @param cls The classifier
 * @param at Position of the first end
 * @param bt Position of the second end
 * @param xt Position of the interpolated median
 * @param am Median at the first end
 * @param bm Median at the second end
 * @param xm The interpolated median
 * @return BOX_MSDF_CANDIDATE and BOX_MSDF_ARTIFACT flags
 */
static int32_t box_msdf_range_test(
    const box_msdf_class_t *cls,
    double at,
    double bt,
    double xt,
    float am,
    float bm,
    float xm)
{
    int32_t result = 0;
    if ((am > 0.5f && bm > 0.5f && xm <= 0.5f) || (am < 0.5f && bm < 0.5f && xm >= 0.5f) ||
        (!cls->protect && box_msdf_median3(am, bm, xm) != xm))
    {
        double ax = (xt - at) * cls->span, bx = (bt - xt) * cls->span;
        result = BOX_MSDF_CANDIDATE;
        if (!(xm >= am - ax && xm <= am + ax && xm >= bm - bx && xm <= bm + bx)) {
            result |= BOX_MSDF_ARTIFACT;
        }
    }

    return result;
}

/** Decide whether the flags of an interpolated median make it an artifact. Without a check of
 * the true distance, as after msdfgen's scanline sign pass, only the range decides.
 *
 * @param cls Unused
 * @param t Unused
 * @param flags Flags of the range tests
 * @return Whether the texel causes an artifact
 */
static bool box_msdf_evaluate(
    const box_msdf_class_t *cls,
    double t,
    int32_t flags)
{
    (void)cls;
    (void)t;

    return (flags & BOX_MSDF_ARTIFACT) != 0;
}

/** Check for an artifact where two channels are equal between two texels.
 *
 * @param cls The classifier
 * @param am Median of the texel
 * @param bm Median of the neighbor
 * @param a Channels of the texel
 * @param b Channels of the neighbor
 * @param da Difference of the two channels at the texel
 * @param db Difference of the two channels at the neighbor
 * @return Whether the texel causes an artifact
 */
static bool box_msdf_linear_inner(
    const box_msdf_class_t *cls,
    float am,
    float bm,
    const float *a,
    const float *b,
    float da,
    float db)
{
    double t = da != db ? (double)da / (double)(da - db) : -1.0;
    bool result = false;
    if (t > BOX_MSDF_ARTIFACT_T && t < 1.0 - BOX_MSDF_ARTIFACT_T) {
        float xm = box_msdf_median3((float)(a[0] + (b[0] - a[0]) * t),
            (float)(a[1] + (b[1] - a[1]) * t), (float)(a[2] + (b[2] - a[2]) * t));
        result = box_msdf_evaluate(cls, t, box_msdf_range_test(cls, 0.0, 1.0, t, am, bm, xm));
    }

    return result;
}

/** Check whether linear interpolation between a texel and a side neighbor makes an artifact.
 *
 * @param cls The classifier
 * @param am Median of the texel
 * @param bm Median of the neighbor
 * @param a Channels of the texel
 * @param b Channels of the neighbor
 * @return Whether the texel causes an artifact
 */
static bool box_msdf_linear(
    const box_msdf_class_t *cls,
    float am,
    float bm,
    const float *a,
    const float *b)
{
    return fabsf(am - 0.5f) >= fabsf(bm - 0.5f) && (
        box_msdf_linear_inner(cls, am, bm, a, b, a[1] - a[0], b[1] - b[0]) ||
        box_msdf_linear_inner(cls, am, bm, a, b, a[2] - a[1], b[2] - b[1]) ||
        box_msdf_linear_inner(cls, am, bm, a, b, a[0] - a[2], b[0] - b[2]));
}

/** Median of the bilinear interpolation along a diagonal.
 *
 * @param a Constant terms
 * @param l Linear terms
 * @param q Quadratic terms
 * @param t Position along the diagonal
 * @return The median
 */
static float box_msdf_quad_median(
    const float *a,
    const float *l,
    const float *q,
    double t)
{
    return box_msdf_median3((float)(t * (t * q[0] + l[0]) + a[0]),
        (float)(t * (t * q[1] + l[1]) + a[1]), (float)(t * (t * q[2] + l[2]) + a[2]));
}

/** Range test of a diagonal median against a local extreme of a channel.
 *
 * @param cls The classifier
 * @param terms Constant, linear and quadratic terms, three floats each
 * @param am Median of the texel
 * @param dm Median of the diagonal neighbor
 * @param t Position of the median
 * @param xm The median
 * @param extreme Position of the local extreme
 * @return Flags of the range test, 0 when the extreme is outside the diagonal
 */
static int32_t box_msdf_extreme(
    const box_msdf_class_t *cls,
    const float *terms,
    float am,
    float dm,
    double t,
    float xm,
    double extreme)
{
    int32_t result = 0;
    if (extreme > 0.0 && extreme < 1.0) {
        double ends[2] = { 0.0, 1.0 };
        float m[2] = { am, dm };
        int32_t k = extreme > t ? 1 : 0;
        ends[k] = extreme;
        m[k] = box_msdf_quad_median(terms, terms + 3, terms + 6, extreme);
        result = box_msdf_range_test(cls, ends[0], ends[1], t, m[0], m[1], xm);
    }

    return result;
}

/** Check for an artifact where two channels are equal along a diagonal.
 *
 * @param cls The classifier
 * @param terms Constant, linear and quadratic terms, three floats each
 * @param am Median of the texel
 * @param dm Median of the diagonal neighbor
 * @param diff Difference of the channels at the texel, both side neighbors and the diagonal
 * @param ex0 Position of the extreme of the first channel
 * @param ex1 Position of the extreme of the second channel
 * @return Whether the texel causes an artifact
 */
static bool box_msdf_diagonal_inner(
    const box_msdf_class_t *cls,
    const float *terms,
    float am,
    float dm,
    const float *diff,
    double ex0,
    double ex1)
{
    double t[2];
    int32_t i, count = box_msdf_quadratic(t, diff[2] - diff[1] + diff[0],
        diff[1] - diff[0] - diff[0], diff[0]);
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        if (t[i] <= BOX_MSDF_ARTIFACT_T || t[i] >= 1.0 - BOX_MSDF_ARTIFACT_T) {
            continue;
        }

        float xm = box_msdf_quad_median(terms, terms + 3, terms + 6, t[i]);
        int32_t flags = box_msdf_range_test(cls, 0.0, 1.0, t[i], am, dm, xm);
        flags |= box_msdf_extreme(cls, terms, am, dm, t[i], xm, ex0);
        flags |= box_msdf_extreme(cls, terms, am, dm, t[i], xm, ex1);
        result = box_msdf_evaluate(cls, t[i], flags);
    }

    return result;
}

/** Check whether bilinear interpolation towards a diagonal neighbor makes an artifact.
 *
 * @param cls The classifier
 * @param am Median of the texel
 * @param dm Median of the diagonal neighbor
 * @param a Channels of the texel
 * @param b Channels of the side neighbor along x
 * @param c Channels of the side neighbor along y
 * @param d Channels of the diagonal neighbor
 * @return Whether the texel causes an artifact
 */
static bool box_msdf_diagonal(
    const box_msdf_class_t *cls,
    float am,
    float dm,
    const float *a,
    const float *b,
    const float *c,
    const float *d)
{
    float terms[9], diff[3][3];
    double ex[3];
    int32_t i, j;
    bool result = false;
    if (fabsf(am - 0.5f) < fabsf(dm - 0.5f)) {
        goto done;
    }

    for (i = 0; i < 3; i ++) {
        float abc = a[i] - b[i] - c[i];
        terms[i] = a[i];
        terms[3 + i] = -a[i] - abc;
        terms[6 + i] = d[i] + abc;
        ex[i] = -0.5 * terms[3 + i] / terms[6 + i];
    }

    for (i = 0; i < 3 && !result; i ++) {
        j = (i + 1) % 3;
        diff[i][0] = a[j] - a[i];
        diff[i][1] = b[j] - b[i] + c[j] - c[i];
        diff[i][2] = d[j] - d[i];
        result = box_msdf_diagonal_inner(cls, terms, am, dm, diff[i], ex[i], ex[j]);
    }

done:
    return result;
}

/** Check whether interpolation between a texel and any of its eight neighbors makes an artifact.
 *
 * @param gen The generation
 * @param cls The classifier, its texel and position set
 * @return Whether the texel causes an artifact
 */
static bool box_msdf_texel_error(
    const box_msdf_gen_t *gen,
    box_msdf_class_t *cls)
{
    static const int32_t dirs[8][2] = {
        { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }
    };
    int32_t i, w = gen->raster->width, h = gen->raster->height;
    double unit = BOX_MSDF_DEVIATION / gen->raster->range;
    float am = gen->medians[cls->y * w + cls->x];
    bool result = false;
    for (i = 0; i < 8 && !result; i ++) {
        int32_t nx = cls->x + dirs[i][0], ny = cls->y + dirs[i][1];
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
            continue;
        }

        const float *n = &gen->values[(ny * w + nx) * 4];
        float nm = gen->medians[ny * w + nx];
        if (i < 4) {
            cls->span = unit;
            result = box_msdf_linear(cls, am, nm, cls->texel, n);
        } else {
            cls->span = unit * GLM_SQRT2;
            result = box_msdf_diagonal(cls, am, nm, cls->texel,
                &gen->values[(cls->y * w + nx) * 4], &gen->values[(ny * w + cls->x) * 4], n);
        }
    }

    return result;
}

/** Flag the texels that cause artifacts, after msdfgen's findErrors without a distance check.
 * A texel whose channels are equal, or all on the same side of the 0 to 1 that is kept, is the
 * same flattened, so it is not tested.
 *
 * @param gen The generation
 */
static void box_msdf_find_errors(
    box_msdf_gen_t *gen)
{
    int32_t x, y, w = gen->raster->width, h = gen->raster->height;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            uint8_t *flags = &gen->stencil[y * w + x];
            const float *v = &gen->values[(y * w + x) * 4];
            if ((v[0] >= 1.0f && v[1] >= 1.0f && v[2] >= 1.0f) ||
                (v[0] <= 0.0f && v[1] <= 0.0f && v[2] <= 0.0f) ||
                (v[0] == v[1] && v[1] == v[2]))
            {
                continue;
            }

            box_msdf_class_t cls = {
                .protect = (*flags & BOX_MSDF_PROTECTED) != 0,
                .texel = v,
                .x = x,
                .y = y
            };
            if (box_msdf_texel_error(gen, &cls)) {
                *flags |= BOX_MSDF_ERROR;
            }
        }
    }
}

/** Interpolate the channels of the field bilinearly between the texel centers.
 *
 * @param gen The generation
 * @param x Column of the upper left texel
 * @param y Row of the upper left texel
 * @param fx Position between the columns
 * @param fy Position between the rows
 * @param out Receives the four channels
 */
static void box_msdf_bilinear(
    const box_msdf_gen_t *gen,
    int32_t x,
    int32_t y,
    double fx,
    double fy,
    double *out)
{
    int32_t c, w = gen->raster->width;
    const float *a = &gen->values[(y * w + x) * 4], *b = a + w * 4;
    for (c = 0; c < 4; c ++) {
        out[c] = (a[c] * (1.0 - fx) + a[c + 4] * fx) * (1.0 - fy) +
            (b[c] * (1.0 - fx) + b[c + 4] * fx) * fy;
    }
}

/** Find the side of the outline a point of a cell must be on. A point is tested when the
 * nearest of the edges near the corner is not much farther than the interpolated true distance
 * and puts it on the side of it, or on the side the fill gives the point where the two disagree.
 *
 * They disagree where interpolation rounds off the tip of a notch or a spike, whose points the
 * repair must also be able to move to their side.
 *
 * Edges other than those of the corner count, so that where the corners of a crossing or a
 * junction share texels each point is judged by the edge it lies next to.
 *
 * @param gen The generation
 * @param block The block
 * @param tx Column of the upper left texel of the cell
 * @param ty Row of the upper left texel of the cell
 * @param fx Position of the point between the columns
 * @param fy Position of the point between the rows
 * @param v The interpolated channels at the point
 * @return 3 when the point is at least BOX_MSDF_CORNER_DEEP inside, 2 when at least
 * BOX_MSDF_CORNER_SHARP inside, 1 inside, the negated values outside and 0 for a point that is
 * not tested
 */
static int8_t box_msdf_corner_side(
    const box_msdf_gen_t *gen,
    const box_msdf_block_t *block,
    int32_t tx,
    int32_t ty,
    double fx,
    double fy,
    const double *v)
{
    const box_msdf_raster_t *r = gen->raster;
    box_msdf_point_t p = {
        r->left + (tx + 0.5 + fx) / r->scale, r->top - (ty + 0.5 + fy) / r->scale
    };
    double d = box_msdf_edges_distance(ecs_vec_first(&gen->shape->edges), gen->prep,
        block->edges, block->count, p) * r->scale;
    double a = (v[3] - 0.5) * r->range;
    int8_t result = 0;
    if (fabs(d) <= fabs(a) + BOX_MSDF_CORNER_SLACK &&
        ((d > 0.0) == (a > 0.0) || (d > 0.0) == box_msdf_inside(gen->shape, p)))
    {
        result = (int8_t)((d > 0.0 ? 1 : -1) *
            (fabs(d) >= BOX_MSDF_CORNER_DEEP ? 3 : (fabs(d) >= BOX_MSDF_CORNER_SHARP ? 2 : 1)));
    }

    return result;
}

/** Find the side of a point of the block around a corner, once per corner.
 *
 * @param gen The generation
 * @param block The block
 * @param i Index of the point in the block
 * @param tx Column of the upper left texel of the cell
 * @param ty Row of the upper left texel of the cell
 * @param fx Position of the point between the columns
 * @param fy Position of the point between the rows
 * @param v The interpolated channels at the point
 * @return The side, as box_msdf_corner_side gives it
 */
static int8_t box_msdf_corner_known_side(
    const box_msdf_gen_t *gen,
    const box_msdf_block_t *block,
    int32_t i,
    int32_t tx,
    int32_t ty,
    double fx,
    double fy,
    const double *v)
{
    if (block->sides[i] == BOX_MSDF_CORNER_UNKNOWN) {
        block->sides[i] = box_msdf_corner_side(gen, block, tx, ty, fx, fy, v);
    }

    return block->sides[i];
}

/** Check whether the interpolated median of a tested point of the block around a corner is
 * short of its side: less than BOX_MSDF_CORNER_MARGIN onto it for a deep point, less than
 * BOX_MSDF_CORNER_NEAR for a point from BOX_MSDF_CORNER_SHARP on, not on it for another.
 *
 * Against the floor, BOX_MSDF_CORNER_MARGIN_FLOOR and BOX_MSDF_CORNER_NEAR_FLOOR take their place.
 *
 * @param gen The generation
 * @param side The side of the point, as box_msdf_corner_side gives it
 * @param m The interpolated median of the point
 * @param floor Whether to check against the floor, which the repair must not push a point below
 * @return Whether the median is short
 */
static bool box_msdf_corner_short(
    const box_msdf_gen_t *gen,
    int8_t side,
    float m,
    bool floor)
{
    double onto = (m - 0.5) * gen->raster->range * (side > 0 ? 1.0 : -1.0);
    bool result = onto <= 0.0;
    if (side == 3 || side == -3) {
        result = onto < (floor ? BOX_MSDF_CORNER_MARGIN_FLOOR : BOX_MSDF_CORNER_MARGIN);
    } else if (side == 2 || side == -2) {
        result = onto < (floor ? BOX_MSDF_CORNER_NEAR_FLOOR : BOX_MSDF_CORNER_NEAR);
    }

    return result;
}

/** Find how far the texels of a cell hold the points of a row of it on a side of the outline, or
 * less than BOX_MSDF_CORNER_SHARP off it, from the true distance of the texels alone.
 *
 * The true distance changes by at most the distance moved, so a texel whose true distance is on
 * the side by more than its distance from the point, less BOX_MSDF_CORNER_SHARP, holds the point
 * there. A texel far from the outline keeps less than its distance, so it only holds less.
 *
 * The interpolated true distance of a point cannot tell this: at the rounded-off tip of a notch
 * or a spike narrower than a texel it is on the side opposite to the fill.
 *
 * @param onto True distance of each texel of the cell onto the side, plus BOX_MSDF_CORNER_SHARP,
 * in texels, in the order upper left, upper right, lower left, lower right
 * @param fy Position of the row between the rows of texels
 * @param reach Receives how far from the left and from the right column the texels hold the
 * points of the row
 */
static void box_msdf_corner_held(
    const double *onto,
    double fy,
    double *reach)
{
    int32_t k;
    reach[0] = reach[1] = 0.0;
    for (k = 0; k < 4; k ++) {
        double dy = (k >> 1) - fy, r2 = onto[k] * onto[k] - dy * dy;
        if (onto[k] > 0.0 && r2 > 0.0) {
            reach[k & 1] = fmax(reach[k & 1], sqrt(r2));
        }
    }
}

/** Check whether every channel and the true distance of the four texels of a cell are at least
 * BOX_MSDF_CORNER_MARGIN onto the same side, so that no point of the cell can be short of it.
 *
 * Every point of the cell is within half a diagonal of a texel, so the true distance of the
 * texels must also be that far onto the side, less BOX_MSDF_CORNER_SHARP, for each of them to hold
 * every point there as box_msdf_corner_held does.
 *
 * @param gen The generation
 * @param tx Column of the upper left texel of the cell
 * @param ty Row of the upper left texel of the cell
 * @return Whether the cell lies well off the outline
 */
static bool box_msdf_corner_far(
    const box_msdf_gen_t *gen,
    int32_t tx,
    int32_t ty)
{
    const box_msdf_raster_t *r = gen->raster;
    double held = GLM_SQRT1_2 - BOX_MSDF_CORNER_SHARP;
    float lo = 1.0f, hi = 0.0f, alo = 1.0f, ahi = 0.0f;
    int32_t k, c;
    for (k = 0; k < 4; k ++) {
        const float *v = &gen->values[((ty + (k >> 1)) * r->width + tx + (k & 1)) * 4];
        for (c = 0; c < 3; c ++) {
            lo = glm_min(lo, v[c]);
            hi = glm_max(hi, v[c]);
        }

        alo = glm_min(alo, v[3]);
        ahi = glm_max(ahi, v[3]);
    }

    return ((lo - 0.5) * r->range >= BOX_MSDF_CORNER_MARGIN && (alo - 0.5) * r->range > held) ||
        ((0.5 - hi) * r->range >= BOX_MSDF_CORNER_MARGIN && (0.5 - ahi) * r->range > held);
}

/** Check whether a point of the block around a corner needs the repair: a deep point short of
 * BOX_MSDF_CORNER_MARGIN, or a point from BOX_MSDF_CORNER_SHARP on that is on the wrong side. One
 * that is only short of BOX_MSDF_CORNER_NEAR is lifted by the repair but does not start it.
 *
 * A point whose median is BOX_MSDF_CORNER_MARGIN onto the side of the interpolated true distance
 * can only be wrong where the fill puts it on the other side. So it finds the side from the edges
 * near the corner only for the others and for those box_msdf_corner_held does not hold there.
 *
 * A point the texels hold is at most BOX_MSDF_CORNER_SHARP onto the other side, too close to the
 * outline to start the repair.
 *
 * @param gen The generation
 * @param block The block
 * @return Whether a point is wrong
 */
static bool box_msdf_corner_clashes(
    const box_msdf_gen_t *gen,
    const box_msdf_block_t *block)
{
    const box_msdf_raster_t *r = gen->raster;
    int32_t cx, cy, sx, sy, n = BOX_MSDF_CORNER_SAMPLES;
    bool result = false;
    for (cy = 0; cy < BOX_MSDF_CORNER_CELLS && !result; cy ++) {
        for (cx = 0; cx < BOX_MSDF_CORNER_CELLS && !result; cx ++) {
            int32_t tx = block->l + cx, ty = block->b + cy;
            if (tx < 0 || ty < 0 || tx + 1 >= r->width || ty + 1 >= r->height ||
                box_msdf_corner_far(gen, tx, ty))
            {
                continue;
            }

            double held[2][4];
            int32_t k;
            for (k = 0; k < 4; k ++) {
                float a = gen->values[((ty + (k >> 1)) * r->width + tx + (k & 1)) * 4 + 3];
                held[0][k] = (0.5 - a) * r->range + BOX_MSDF_CORNER_SHARP;
                held[1][k] = (a - 0.5) * r->range + BOX_MSDF_CORNER_SHARP;
            }

            for (sy = 0; sy < n && !result; sy ++) {
                double reach[2][2];
                box_msdf_corner_held(held[0], (sy + 0.5) / n, reach[0]);
                box_msdf_corner_held(held[1], (sy + 0.5) / n, reach[1]);
                for (sx = 0; sx < n && !result; sx ++) {
                    double fx = (sx + 0.5) / n, fy = (sy + 0.5) / n, v[4];
                    box_msdf_bilinear(gen, tx, ty, fx, fy, v);
                    double m = fmax(fmin(v[0], v[1]), fmin(fmax(v[0], v[1]), v[2]));
                    const double *h = reach[v[3] > 0.5];
                    if ((m - 0.5) * r->range * (v[3] > 0.5 ? 1 : -1) >= BOX_MSDF_CORNER_MARGIN &&
                        (fx < h[0] || 1.0 - fx < h[1]))
                    {
                        continue;
                    }

                    int32_t i = ((cy * BOX_MSDF_CORNER_CELLS + cx) * n + sy) * n + sx;
                    int8_t side = box_msdf_corner_known_side(gen, block, i, tx, ty, fx, fy, v);
                    double onto = (m - 0.5) * r->range * (side > 0 ? 1 : -1);
                    result = (side <= -3 || side >= 3) ? onto < BOX_MSDF_CORNER_MARGIN :
                        (side == -2 || side == 2) && onto <= 0.0;
                }
            }
        }
    }

    return result;
}

/** Find the side of the outline each point of the block around a corner must be on.
 *
 * @param gen The generation
 * @param block The block, receives the sides it lacks, as box_msdf_corner_side gives them
 */
static void box_msdf_corner_sides(
    const box_msdf_gen_t *gen,
    const box_msdf_block_t *block)
{
    const box_msdf_raster_t *r = gen->raster;
    int32_t cx, cy, sx, sy, n = BOX_MSDF_CORNER_SAMPLES, i = 0;
    for (cy = 0; cy < BOX_MSDF_CORNER_CELLS; cy ++) {
        for (cx = 0; cx < BOX_MSDF_CORNER_CELLS; cx ++) {
            int32_t tx = block->l + cx, ty = block->b + cy;
            bool cell = tx >= 0 && ty >= 0 && tx + 1 < r->width && ty + 1 < r->height;
            for (sy = 0; sy < n; sy ++) {
                for (sx = 0; sx < n; sx ++, i ++) {
                    double fx = (sx + 0.5) / n, fy = (sy + 0.5) / n, v[4];
                    if (!cell) {
                        block->sides[i] = 0;
                    } else if (block->sides[i] == BOX_MSDF_CORNER_UNKNOWN) {
                        box_msdf_bilinear(gen, tx, ty, fx, fy, v);
                        block->sides[i] = box_msdf_corner_side(gen, block, tx, ty, fx, fy, v);
                    }
                }
            }
        }
    }
}

/** Interpolate the channels of the field at every point of the block around a corner.
 *
 * @param push The push, its block set, receives the channels and the medians before the push
 */
static void box_msdf_corner_channels(
    box_msdf_push_t *push)
{
    const box_msdf_raster_t *r = push->gen->raster;
    int32_t cx, cy, sx, sy, c, n = BOX_MSDF_CORNER_SAMPLES, i = 0;
    for (cy = 0; cy < BOX_MSDF_CORNER_CELLS; cy ++) {
        for (cx = 0; cx < BOX_MSDF_CORNER_CELLS; cx ++) {
            int32_t tx = push->l + cx, ty = push->b + cy;
            bool cell = tx >= 0 && ty >= 0 && tx + 1 < r->width && ty + 1 < r->height;
            for (sy = 0; sy < n; sy ++) {
                for (sx = 0; sx < n; sx ++, i ++) {
                    double v[4] = { 0.5, 0.5, 0.5, 0.5 };
                    if (cell) {
                        box_msdf_bilinear(push->gen, tx, ty, (sx + 0.5) / n, (sy + 0.5) / n, v);
                    }

                    for (c = 0; c < 3; c ++) {
                        push->channels[i][c] = (float)v[c];
                    }

                    push->before[i] = box_msdf_median(push->channels[i]);
                }
            }
        }
    }
}

/** Count whether a point of the block around a corner is wrong with a median: a tested point
 * that box_msdf_corner_short finds short.
 *
 * A tested point that is short of the floor and was not before the push, and a point that is not
 * tested and whose median is on another side than before, count as all the points of the block,
 * so that the repair never trades the points it fixes for others nor moves another edge.
 *
 * @param push The push
 * @param i Index of the point
 * @param m The median of the point
 * @return The count of the point
 */
static int32_t box_msdf_corner_point(
    const box_msdf_push_t *push,
    int32_t i,
    float m)
{
    const box_msdf_gen_t *gen = push->gen;
    int8_t side = push->sides[i];
    int32_t result;
    if (!side) {
        result = ((m > 0.5f) != (push->before[i] > 0.5f)) * BOX_MSDF_CORNER_POINTS;
    } else if (box_msdf_corner_short(gen, side, m, true) &&
        !box_msdf_corner_short(gen, side, push->before[i], true))
    {
        result = BOX_MSDF_CORNER_POINTS;
    } else {
        result = box_msdf_corner_short(gen, side, m, false);
    }

    return result;
}

/** Check whether a cell of the block around a corner lies on the field.
 *
 * @param push The push
 * @param cell The cell, row by row
 * @return Whether the four texels of the cell are on the field
 */
static bool box_msdf_corner_on(
    const box_msdf_push_t *push,
    int32_t cell)
{
    const box_msdf_raster_t *r = push->gen->raster;
    int32_t tx = push->l + cell % BOX_MSDF_CORNER_CELLS;
    int32_t ty = push->b + cell / BOX_MSDF_CORNER_CELLS;

    return tx >= 0 && ty >= 0 && tx + 1 < r->width && ty + 1 < r->height;
}

/** Count the wrong points of a cell of the block around a corner, as box_msdf_corner_point
 * counts them. A cell off the field has none.
 *
 * @param push The push, receives the count of each point of the cell
 * @param cell The cell, row by row
 * @return The number of wrong points
 */
static int32_t box_msdf_corner_wrong(
    box_msdf_push_t *push,
    int32_t cell)
{
    int32_t p, n = BOX_MSDF_CORNER_SAMPLES * BOX_MSDF_CORNER_SAMPLES, result = 0;
    bool on = box_msdf_corner_on(push, cell);
    for (p = cell * n; p < (cell + 1) * n; p ++) {
        push->counts[p] = on
            ? box_msdf_corner_point(push, p, box_msdf_median(push->channels[p]))
            : 0;
        result += push->counts[p];
    }

    return result;
}

/** Find the weights of a texel of the four around a corner at a point of the block.
 *
 * @param k The texel of the four around the corner, row by row
 * @param point Index of the point
 * @param reach Receives the point and the weights
 * @return Whether the point lies in one of the four cells around the texel
 */
static bool box_msdf_corner_weights(
    int32_t k,
    int32_t point,
    box_msdf_reach_t *reach)
{
    int32_t n = BOX_MSDF_CORNER_SAMPLES, cell = point / (n * n);
    int32_t cx = cell % BOX_MSDF_CORNER_CELLS, cy = cell / BOX_MSDF_CORNER_CELLS;
    int32_t sx = point % n, sy = point / n % n;
    bool right = 1 + (k & 1) > cx, below = 1 + (k >> 1) > cy;
    float fx = ((float)sx + 0.5f) / (float)n, fy = ((float)sy + 0.5f) / (float)n;
    reach->point = point;
    reach->wy = below ? fy : 1.0f - fy;
    reach->wx = right ? fx : 1.0f - fx;

    return cx - (k & 1) >= 0 && cx - (k & 1) <= 1 && cy - (k >> 1) >= 0 && cy - (k >> 1) <= 1;
}

/** Collect the points of the block around a corner that count.
 *
 * @param push The push, its counts set, receives the points
 */
static void box_msdf_corner_wrongs(
    box_msdf_push_t *push)
{
    int32_t p;
    push->wrong_count = 0;
    for (p = 0; p < BOX_MSDF_CORNER_POINTS; p ++) {
        if (push->counts[p]) {
            push->wrongs[push->wrong_count ++] = p;
        }
    }
}

/** Count whether a point of the block around a corner is wrong with a channel of a texel of
 * the four around the corner moved.
 *
 * @param push The push
 * @param reach The point and the weights of the texel at it
 * @param channel The channel of the texel
 * @param delta The change of the channel
 * @return The count of the point, as box_msdf_corner_point gives it
 */
static int32_t box_msdf_corner_moved(
    const box_msdf_push_t *push,
    const box_msdf_reach_t *reach,
    int32_t channel,
    float delta)
{
    float v[3];
    v[0] = push->channels[reach->point][0];
    v[1] = push->channels[reach->point][1];
    v[2] = push->channels[reach->point][2];
    v[channel] += delta * reach->wy * reach->wx;

    return box_msdf_corner_point(push, reach->point, box_msdf_median(v));
}

/** Find by how much a move of a channel of a texel of the four around a corner, by up to an
 * amount, can at most lower the number of wrong points of the block.
 *
 * The median of a point only moves one way as the channel moves one way, and so does its count.
 * The count of a point is therefore between its count before the move and its count with the
 * largest move, and only a point that counts now can count less.
 *
 * @param push The push
 * @param k The texel of the four around the corner, row by row
 * @param channel The channel of the texel
 * @param delta The largest change of the channel
 * @return The largest drop of the number of wrong points
 */
static int32_t box_msdf_corner_gain(
    const box_msdf_push_t *push,
    int32_t k,
    int32_t channel,
    float delta)
{
    int32_t p, result = 0;
    for (p = 0; p < push->wrong_count; p ++) {
        int32_t point = push->wrongs[p];
        box_msdf_reach_t reach;
        if (box_msdf_corner_weights(k, point, &reach) &&
            box_msdf_corner_on(push, point / (BOX_MSDF_CORNER_SAMPLES * BOX_MSDF_CORNER_SAMPLES)))
        {
            int32_t count = box_msdf_corner_moved(push, &reach, channel, delta);
            result += count < push->counts[point] ? push->counts[point] - count : 0;
        }
    }

    return result;
}

/** Collect the points whose count a move of a channel of a texel of the four around a corner,
 * by up to an amount, changes. As box_msdf_corner_gain finds, a point whose count the largest
 * move keeps keeps it for every smaller move.
 *
 * A channel that moves away from the median of a point keeps the median.
 *
 * @param push The push, receives the points and the count of the wrong points of the others
 * @param k The texel of the four around the corner, row by row
 * @param channel The channel of the texel
 * @param delta The largest change of the channel
 * @param wrong The number of wrong points of the block
 */
static void box_msdf_corner_reach(
    box_msdf_push_t *push,
    int32_t k,
    int32_t channel,
    float delta,
    int32_t wrong)
{
    int32_t c, sx, sy, n = BOX_MSDF_CORNER_SAMPLES;
    push->reach_count = 0;
    push->fixed = wrong;
    for (c = 0; c < 4; c ++) {
        int32_t cx = (k & 1) + (c & 1), cy = (k >> 1) + (c >> 1);
        int32_t cell = cy * BOX_MSDF_CORNER_CELLS + cx, i = cell * n * n;
        bool right = 1 + (k & 1) > cx, below = 1 + (k >> 1) > cy;
        if (!box_msdf_corner_on(push, cell)) {
            continue;
        }

        for (sy = 0; sy < n; sy ++) {
            float fy = ((float)sy + 0.5f) / (float)n, wy = below ? fy : 1.0f - fy;
            for (sx = 0; sx < n; sx ++, i ++) {
                const float *v = push->channels[i];
                float a = v[(channel + 1) % 3], b = v[(channel + 2) % 3];
                if (delta > 0.0f ? v[channel] >= glm_max(a, b) : v[channel] <= glm_min(a, b)) {
                    continue;
                }

                float fx = ((float)sx + 0.5f) / (float)n;
                box_msdf_reach_t reach = { .point = i, .wy = wy, .wx = right ? fx : 1.0f - fx };
                if (box_msdf_corner_moved(push, &reach, channel, delta) != push->counts[i]) {
                    push->fixed -= push->counts[i];
                    push->reach[push->reach_count ++] = reach;
                }
            }
        }
    }
}

/** Count the wrong points of the block around a corner with a channel of a texel of the four
 * around the corner moved, from the points box_msdf_corner_reach collected for the move.
 *
 * @param push The push
 * @param channel The channel of the texel
 * @param delta The change of the channel, between 0 and the amount the points were collected for
 * @param limit Count from which on the exact count does not matter
 * @return The number of wrong points, or at least limit
 */
static int32_t box_msdf_corner_count(
    const box_msdf_push_t *push,
    int32_t channel,
    float delta,
    int32_t limit)
{
    int32_t p, result = push->fixed;
    for (p = 0; p < push->reach_count && result < limit; p ++) {
        result += box_msdf_corner_moved(push, &push->reach[p], channel, delta);
    }

    return result;
}

/** Move a channel of a texel of the four around a corner in the interpolated channels of the
 * points of the block.
 *
 * @param push The push
 * @param k The texel of the four around the corner, row by row
 * @param channel The channel of the texel
 * @param delta The change of the channel
 */
static void box_msdf_corner_shift(
    box_msdf_push_t *push,
    int32_t k,
    int32_t channel,
    float delta)
{
    int32_t c, sx, sy, n = BOX_MSDF_CORNER_SAMPLES;
    for (c = 0; c < 4; c ++) {
        int32_t cx = (k & 1) + (c & 1), cy = (k >> 1) + (c >> 1);
        int32_t i = (cy * BOX_MSDF_CORNER_CELLS + cx) * n * n;
        for (sy = 0; sy < n; sy ++) {
            float fy = ((float)sy + 0.5f) / (float)n, wy = (c >> 1) ? 1.0f - fy : fy;
            for (sx = 0; sx < n; sx ++, i ++) {
                float fx = ((float)sx + 0.5f) / (float)n;
                push->channels[i][channel] += delta * wy * ((c & 1) ? 1.0f - fx : fx);
            }
        }
    }
}

/** Move, one at a time, a channel of one of the four texels around a corner up or down, without
 * changing the median of the texel, by the amount that leaves the fewest points of the block
 * wrong, as long as it leaves fewer.
 *
 * @param gen The generation
 * @param block The block
 * @return Whether a channel moved
 */
static bool box_msdf_push_corner(
    box_msdf_gen_t *gen,
    const box_msdf_block_t *block)
{
    const box_msdf_raster_t *r = gen->raster;
    box_msdf_push_t push = { .gen = gen, .l = block->l, .b = block->b, .sides = block->sides };
    int32_t cells[BOX_MSDF_CORNER_CELLS * BOX_MSDF_CORNER_CELLS];
    int32_t pass, k, c, s, wrong = 0, l = block->l, b = block->b;
    bool result = false;
    box_msdf_corner_sides(gen, block);
    box_msdf_corner_channels(&push);
    for (c = 0; c < BOX_MSDF_CORNER_CELLS * BOX_MSDF_CORNER_CELLS; c ++) {
        cells[c] = box_msdf_corner_wrong(&push, c);
        wrong += cells[c];
    }

    box_msdf_corner_wrongs(&push);

    for (pass = 0; pass < BOX_MSDF_CORNER_PASSES && wrong; pass ++) {
        int32_t best_k = -1, best_c = 0, best_wrong = wrong;
        float best_delta = 0.0f;
        for (k = 0; k < 4; k ++) {
            int32_t x = l + 1 + (k & 1), y = b + 1 + (k >> 1);
            if (x < 0 || y < 0 || x >= r->width || y >= r->height) {
                continue;
            }

            float *v = &gen->values[(y * r->width + x) * 4], median = box_msdf_median(v);
            for (c = 0; c < 6; c ++) {
                float keep = v[c >> 1], deltas[BOX_MSDF_CORNER_STEPS];
                float sign = (c & 1) ? -1.0f : 1.0f;
                int32_t steps = 0;
                for (s = 0; s < BOX_MSDF_CORNER_STEPS; s ++) {
                    float value = glm_clamp(keep + sign * box_msdf_corner_steps[s], 0.0f, 1.0f);
                    v[c >> 1] = value;
                    bool keeps = value != keep && box_msdf_median(v) == median;
                    v[c >> 1] = keep;
                    if (!keeps) {
                        break;
                    }

                    deltas[steps ++] = value - keep;
                }

                if (!steps ||
                    wrong - box_msdf_corner_gain(&push, k, c >> 1, deltas[steps - 1]) >= best_wrong)
                {
                    continue;
                }

                box_msdf_corner_reach(&push, k, c >> 1, deltas[steps - 1], wrong);
                for (s = 0; s < steps; s ++) {
                    int32_t count = box_msdf_corner_count(&push, c >> 1, deltas[s], best_wrong);
                    if (count < best_wrong) {
                        best_k = k;
                        best_c = c >> 1;
                        best_delta = deltas[s];
                        best_wrong = count;
                    }
                }
            }
        }

        if (best_k < 0) {
            break;
        }

        int32_t x = l + 1 + (best_k & 1), y = b + 1 + (best_k >> 1);
        gen->values[(y * r->width + x) * 4 + best_c] += best_delta;
        box_msdf_corner_shift(&push, best_k, best_c, best_delta);
        for (c = 0; c < 4; c ++) {
            int32_t cell = ((best_k >> 1) + (c >> 1)) * BOX_MSDF_CORNER_CELLS +
                (best_k & 1) + (c & 1);
            wrong -= cells[cell];
            cells[cell] = box_msdf_corner_wrong(&push, cell);
            wrong += cells[cell];
        }

        box_msdf_corner_wrongs(&push);
        result = true;
    }

    return result;
}

/** Collect the edges that can be the nearest to a point of the block around a corner that the
 * repair tests: those whose box comes within the distance a tested point can have of the block.
 * The two edges of the corner come first, as they are mostly the nearest.
 *
 * @param gen The generation
 * @param block The block, its position set, receives the edges
 * @param prev The edge that ends at the corner
 * @param next The edge that starts at the corner
 * @param edges Receives the indices of the edges, room for every edge of the shape
 */
static void box_msdf_block_edges(
    const box_msdf_gen_t *gen,
    box_msdf_block_t *block,
    int32_t prev,
    int32_t next,
    int32_t *edges)
{
    const box_msdf_raster_t *r = gen->raster;
    int32_t i, count = ecs_vec_count(&gen->shape->edges);
    double reach = (0.5 * r->range + BOX_MSDF_CORNER_SLACK) / r->scale;
    double min_x = r->left + (block->l + 0.5) / r->scale - reach;
    double max_x = r->left + (block->l + 0.5 + BOX_MSDF_CORNER_CELLS) / r->scale + reach;
    double max_y = r->top - (block->b + 0.5) / r->scale + reach;
    double min_y = r->top - (block->b + 0.5 + BOX_MSDF_CORNER_CELLS) / r->scale - reach;
    block->edges = edges;
    block->count = 0;
    edges[block->count ++] = next;
    if (prev != next) {
        edges[block->count ++] = prev;
    }

    for (i = 0; i < count; i ++) {
        const box_msdf_prep_t *ep = &gen->prep[i];
        if (i != prev && i != next && ep->max.x >= min_x && ep->min.x <= max_x &&
            ep->max.y >= min_y && ep->min.y <= max_y)
        {
            edges[block->count ++] = i;
        }
    }
}

/** Repair the clash of the channels around a corner. Where the edges of a sharp corner shape
 * two channels whose roles swap between neighboring texels, interpolation leaves the median on
 * the wrong side in a thin band next to the corner, which the protected texels let pass.
 *
 * Flattening a texel there would also erase the edges its other channels shape, such as those
 * of a nearby corner. Instead move a channel of one of the four texels around the corner without
 * changing the median of the texel, which keeps the edges the texel shapes.
 *
 * @param gen The generation
 * @param corner The corner
 * @param edges Room for the indices of every edge of the shape
 * @param sides The sides of the points of the block of the corner, as box_msdf_block_t keeps them
 * @return Whether a channel moved
 */
static bool box_msdf_repair_corner(
    box_msdf_gen_t *gen,
    const box_msdf_corner_t *corner,
    int32_t *edges,
    int8_t *sides)
{
    box_msdf_block_t block = { .l = corner->l, .b = corner->b, .sides = sides };
    bool result = false;
    box_msdf_block_edges(gen, &block, corner->prev, corner->next, edges);
    if (box_msdf_corner_clashes(gen, &block)) {
        result = box_msdf_push_corner(gen, &block);
    }

    return result;
}

/** Check whether a corner must be repaired again: whether another corner whose block shares
 * texels with its block moved a channel since its last repair.
 *
 * @param corners The corners
 * @param count Number of corners
 * @param c The corner
 * @return Whether to repair the corner again
 */
static bool box_msdf_corner_stale(
    const box_msdf_corner_t *corners,
    int32_t count,
    int32_t c)
{
    int32_t i;
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        result = i != c && corners[i].moved > corners[c].done &&
            abs(corners[i].l - corners[c].l) <= 2 && abs(corners[i].b - corners[c].b) <= 2;
    }

    return result;
}

/** Repair the clash of the channels around every corner where the color of the edges changes.
 * Corners close to each other share texels, so a corner whose texels the repair of another moved
 * is repaired again from the texels the other left, for up to BOX_MSDF_CORNER_ROUNDS rounds.
 *
 * @param gen The generation
 */
static void box_msdf_repair_corners(
    box_msdf_gen_t *gen)
{
    const box_msdf_raster_t *r = gen->raster;
    const box_msdf_edge_t *edges = ecs_vec_first(&gen->shape->edges);
    const int32_t *counts = ecs_vec_first(&gen->shape->contours);
    int32_t i, k, round, count = 0, first = 0, stamp = 0;
    int32_t contours = ecs_vec_count(&gen->shape->contours);
    int32_t *near = ecs_os_malloc_n(int32_t, ecs_vec_count(&gen->shape->edges) + 1);
    box_msdf_corner_t *corners = ecs_os_malloc_n(
        box_msdf_corner_t, ecs_vec_count(&gen->shape->edges) + 1);
    int8_t *sides = NULL;
    for (k = 0; k < contours; k ++) {
        int32_t prev = first + counts[k] - 1;
        for (i = first; i < first + counts[k]; i ++) {
            int32_t common = edges[prev].color & edges[i].color;
            if (!(common & (common - 1))) {
                double px = (edges[i].p[0].x - r->left) * r->scale;
                double py = (r->top - edges[i].p[0].y) * r->scale;
                corners[count ++] = (box_msdf_corner_t){
                    .prev = prev, .next = i,
                    .l = (int32_t)floor(px - 0.5) - 1, .b = (int32_t)floor(py - 0.5) - 1
                };
            }

            prev = i;
        }

        first += counts[k];
    }

    if (count) {
        sides = ecs_os_malloc_n(int8_t, count * BOX_MSDF_CORNER_POINTS);
        ecs_os_memset(sides, BOX_MSDF_CORNER_UNKNOWN, count * BOX_MSDF_CORNER_POINTS);
    }

    for (round = 0; round < BOX_MSDF_CORNER_ROUNDS; round ++) {
        bool repaired = false;
        for (i = 0; i < count; i ++) {
            if (round && !box_msdf_corner_stale(corners, count, i)) {
                continue;
            }

            corners[i].done = ++ stamp;
            int8_t *corner_sides = &sides[i * BOX_MSDF_CORNER_POINTS];
            if (box_msdf_repair_corner(gen, &corners[i], near, corner_sides)) {
                corners[i].moved = stamp;
            }

            repaired = true;
        }

        if (!repaired) {
            break;
        }
    }

    ecs_os_free(sides);
    ecs_os_free(corners);
    ecs_os_free(near);
}

void box_msdf_correct(
    box_msdf_gen_t *gen)
{
    int32_t i, texels = gen->raster->width * gen->raster->height;
    gen->medians = ecs_os_malloc_n(float, texels);
    for (i = 0; i < texels; i ++) {
        gen->medians[i] = box_msdf_median(&gen->values[i * 4]);
    }

    box_msdf_protect_corners(gen);
    box_msdf_protect_edges(gen);
    box_msdf_find_errors(gen);
    for (i = 0; i < texels; i ++) {
        float *v = &gen->values[i * 4];
        if (gen->stencil[i] & BOX_MSDF_ERROR) {
            v[0] = v[1] = v[2] = gen->medians[i];
        }
    }

    box_msdf_repair_corners(gen);

    ecs_os_free(gen->medians);
    gen->medians = NULL;
}
