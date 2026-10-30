/**
 * @file text/msdf_correct.c
 * @brief Error correction of multi channel distance fields, after msdfgen's MSDFErrorCorrection.
 *
 * Texels at corners and edges are protected, texels whose interpolation with a neighbor leaves
 * the range the distance allows are flagged and flattened to the median of their channels.
 *
 * It runs after the scanline sign pass, so as msdfgen does with -scanline it never compares the
 * field with the distance of the contours, whose sign the pass may have overruled.
 */

#include "msdf.h"
#include <math.h>

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

/** Tests interpolation between a texel and a neighbor, after msdfgen's artifact classifiers */
typedef struct box_msdf_class_t {
    double span;          /**< Largest change of a value expected per unit of interpolation */
    bool protect;         /**< The texel is protected: only inversions are artifacts */
    const float *texel;   /**< Channels of the texel */
    int32_t x;            /**< Column of the texel */
    int32_t y;            /**< Row of the texel */
} box_msdf_class_t;

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

    ecs_os_free(gen->medians);
    gen->medians = NULL;
}
