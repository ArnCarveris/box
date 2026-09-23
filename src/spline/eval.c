/**
 * @file spline/eval.c
 * @brief Evaluates the position, tangent and normal of a curve at a parameter.
 *
 * The parameter runs from 0 to 1 over the curve and splits into a segment index
 * and a local parameter. The normal is perpendicular to the tangent in the plane
 * it spans with world up, so a swept cross section does not roll.
 */

#include "spline_internal.h"
#include <math.h>

/** Shortest tangent that still gives a direction */
#define BOX_SPLINE_EPSILON (1e-6f)

/** Control points a bezier segment advances over */
#define BOX_SPLINE_BEZIER_STRIDE (3)

/** Step the tangent of a degenerate segment is measured over */
#define BOX_SPLINE_NUDGE (0.01f)

/** One control point, wrapped when the curve loops and clamped when it does not.
 *
 * @param spline The curve
 * @param index Index of the point, may fall outside the control points
 * @return The control point, the origin when the curve holds none
 */
static vec3 box_spline_point(
    const Spline *spline,
    int32_t index)
{
    int32_t count = ecs_vec_count(&spline->points);
    const vec3 *points = ecs_vec_first_t(&spline->points, vec3);
    vec3 result = vec3_new(0, 0, 0);
    if (!count) {
        goto done;
    }

    if (spline->loop) {
        index = index % count;
        if (index < 0) {
            index += count;
        }

    } else if (index < 0) {
        index = 0;
    } else if (index >= count) {
        index = count - 1;
    }

    result = points[index];
done:
    return result;
}

/** Evaluate a straight segment between two control points.
 *
 * @param spline The curve
 * @param segment Index of the segment
 * @param u Parameter within the segment
 * @param position Receives the point on the segment
 * @param tangent Receives the derivative of the segment
 */
static void box_spline_linear(
    const Spline *spline,
    int32_t segment,
    float u,
    vec3 *position,
    vec3 *tangent)
{
    vec3 p0 = box_spline_point(spline, segment);
    vec3 p1 = box_spline_point(spline, segment + 1);

    *position = vec3_add(p0, vec3_scale(vec3_sub(p1, p0), u));
    *tangent = vec3_sub(p1, p0);
}

/** Evaluate a catmull rom segment from the two points around it.
 *
 * @param spline The curve
 * @param segment Index of the segment
 * @param u Parameter within the segment
 * @param position Receives the point on the segment
 * @param tangent Receives the derivative of the segment
 */
static void box_spline_catmull(
    const Spline *spline,
    int32_t segment,
    float u,
    vec3 *position,
    vec3 *tangent)
{
    vec3 p0 = box_spline_point(spline, segment - 1);
    vec3 p1 = box_spline_point(spline, segment);
    vec3 p2 = box_spline_point(spline, segment + 1);
    vec3 p3 = box_spline_point(spline, segment + 2);
    vec3 m1 = vec3_scale(vec3_sub(p2, p0), spline->tension);
    vec3 m2 = vec3_scale(vec3_sub(p3, p1), spline->tension);
    float u2 = u * u;
    float u3 = u2 * u;

    *position = vec3_add(
        vec3_add(vec3_scale(p1, 2 * u3 - 3 * u2 + 1), vec3_scale(m1, u3 - 2 * u2 + u)),
        vec3_add(vec3_scale(p2, -2 * u3 + 3 * u2), vec3_scale(m2, u3 - u2)));

    *tangent = vec3_add(
        vec3_add(vec3_scale(p1, 6 * u2 - 6 * u), vec3_scale(m1, 3 * u2 - 4 * u + 1)),
        vec3_add(vec3_scale(p2, -6 * u2 + 6 * u), vec3_scale(m2, 3 * u2 - 2 * u)));
}

/** Evaluate a cubic bezier segment from its two ends and its two handles.
 *
 * @param spline The curve
 * @param segment Index of the segment
 * @param u Parameter within the segment
 * @param position Receives the point on the segment
 * @param tangent Receives the derivative of the segment
 */
static void box_spline_bezier(
    const Spline *spline,
    int32_t segment,
    float u,
    vec3 *position,
    vec3 *tangent)
{
    int32_t base = segment * BOX_SPLINE_BEZIER_STRIDE;
    vec3 p0 = box_spline_point(spline, base);
    vec3 p1 = box_spline_point(spline, base + 1);
    vec3 p2 = box_spline_point(spline, base + 2);
    vec3 p3 = box_spline_point(spline, base + 3);
    float v = 1 - u;

    *position = vec3_add(
        vec3_add(vec3_scale(p0, v * v * v), vec3_scale(p1, 3 * v * v * u)),
        vec3_add(vec3_scale(p2, 3 * v * u * u), vec3_scale(p3, u * u * u)));

    *tangent = vec3_add(
        vec3_scale(vec3_sub(p1, p0), 3 * v * v),
        vec3_add(vec3_scale(vec3_sub(p2, p1), 6 * v * u),
            vec3_scale(vec3_sub(p3, p2), 3 * u * u)));
}

/** Evaluate the segment of a curve with the interpolation of its type.
 *
 * @param spline The curve
 * @param segment Index of the segment
 * @param u Parameter within the segment
 * @param position Receives the point on the segment
 * @param tangent Receives the derivative of the segment
 */
static void box_spline_curve(
    const Spline *spline,
    int32_t segment,
    float u,
    vec3 *position,
    vec3 *tangent)
{
    if (spline->type == SplineLinear) {
        box_spline_linear(spline, segment, u, position, tangent);
    } else if (spline->type == SplineBezier) {
        box_spline_bezier(spline, segment, u, position, tangent);
    } else {
        box_spline_catmull(spline, segment, u, position, tangent);
    }
}

/** The up of the curve at a tangent: perpendicular to it and as close to world up.
 *
 * @param tangent Unit tangent of the curve
 * @return The unit normal
 */
static vec3 box_spline_up(
    vec3 tangent)
{
    vec3 up = vec3_new(0, 1, 0);
    vec3 side = vec3_cross(up, tangent);
    if (vec3_length(side) < BOX_SPLINE_EPSILON) {
        side = vec3_cross(vec3_new(0, 0, 1), tangent);
    }

    return vec3_normalize(vec3_cross(tangent, vec3_normalize(side)));
}

/** Split a parameter into the segment it falls in and a local parameter.
 *
 * @param spline The curve
 * @param t Parameter over the whole curve
 * @param local Receives the parameter within the segment
 * @return The segment index, -1 when the curve has no segments
 */
static int32_t box_spline_segment_at(
    const Spline *spline,
    float t,
    float *local)
{
    int32_t segments = box_spline_segments(spline);
    int32_t result = -1;
    *local = 0;
    if (segments <= 0) {
        goto done;
    }

    if (spline->loop) {
        t -= floorf(t);
    } else {
        t = fminf(fmaxf(t, 0.0f), 1.0f);
    }

    float scaled = t * (float)segments;
    result = (int32_t)floorf(scaled);
    if (result >= segments) {
        result = segments - 1;
        *local = 1;
    } else {
        *local = scaled - (float)result;
    }

done:
    return result;
}

int32_t box_spline_segments(
    const Spline *spline)
{
    int32_t count = ecs_vec_count(&spline->points);
    int32_t result = 0;
    if (count < 2) {
        goto done;
    }

    if (spline->type == SplineBezier) {
        result = spline->loop ? count / BOX_SPLINE_BEZIER_STRIDE :
            (count - 1) / BOX_SPLINE_BEZIER_STRIDE;
    } else {
        result = spline->loop ? count : count - 1;
    }

done:
    return result;
}

void box_spline_eval(
    const Spline *spline,
    float t,
    box_spline_sample_t *out)
{
    float u = 0;
    int32_t segment = box_spline_segment_at(spline, t, &u);
    out->position = box_spline_point(spline, 0);
    out->tangent = vec3_new(0, 0, 1);
    out->normal = vec3_new(0, 1, 0);
    if (segment < 0) {
        goto done;
    }

    vec3 tangent;
    box_spline_curve(spline, segment, u, &out->position, &tangent);
    if (vec3_length(tangent) < BOX_SPLINE_EPSILON) {
        vec3 before, after, unused;

        box_spline_curve(spline, segment, fmaxf(u - BOX_SPLINE_NUDGE, 0), &before, &unused);
        box_spline_curve(spline, segment, fminf(u + BOX_SPLINE_NUDGE, 1), &after, &unused);
        tangent = vec3_sub(after, before);
    }

    if (vec3_length(tangent) >= BOX_SPLINE_EPSILON) {
        out->tangent = vec3_normalize(tangent);
        out->normal = box_spline_up(out->tangent);
    }

done:
    return;
}
