/**
 * @file curve/curve.c
 * @brief Curve types, the cubic evaluators, value, area, baking, the x solver and validation.
 */

#define BOX_CURVE_IMPL
#include <box.h>
#include <math.h>

/** Iterations of Newton's method before falling back to bisection */
#define BOX_CURVE_NEWTON_STEPS (8)

/** Iterations of bisection, enough for float precision */
#define BOX_CURVE_BISECT_STEPS (32)

/** Accepted error of the solved x of a curve */
#define BOX_CURVE_EPSILON (1e-6f)

/** Mix two values.
 *
 * @param a The value at 0
 * @param b The value at 1
 * @param t The interpolation factor
 * @return a * (1 - t) + b * t
 */
static float box_curve_mix(
    float a,
    float b,
    float t)
{
    return a * (1.0f - t) + b * t;
}

float box_curve_cubic(
    float start,
    float control1,
    float control2,
    float end,
    float t)
{
    float a = box_curve_mix(start, control1, t);
    float b = box_curve_mix(control1, control2, t);
    float c = box_curve_mix(control2, end, t);

    return box_curve_mix(box_curve_mix(a, b, t), box_curve_mix(b, c, t), t);
}

void box_curve_cubic3(
    const vec3 start,
    const vec3 control1,
    const vec3 control2,
    const vec3 end,
    float t,
    vec3 out)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = box_curve_cubic(start[i], control1[i], control2[i], end[i], t);
    }
}

/** Slope of a Catmull-Rom curve at a key, in value per unit of time.
 *
 * A missing neighbour of the first or last key has the value of the key and lies one span away
 * on the other side, so evenly spaced keys get the uniform controls p1 + (p2 - p0) / 6.
 *
 * @param keys The keys
 * @param count Number of keys, at least 2
 * @param k Index of the key
 * @return The slope
 */
static float box_curve_catmull_rom_slope(
    const CurveKey *keys,
    int32_t count,
    int32_t k)
{
    float t0, v0, t1, v1;
    if (k > 0) {
        t0 = keys[k - 1].time;
        v0 = keys[k - 1].value;
    } else {
        t0 = 2.0f * keys[0].time - keys[1].time;
        v0 = keys[0].value;
    }

    if (k < count - 1) {
        t1 = keys[k + 1].time;
        v1 = keys[k + 1].value;
    } else {
        t1 = 2.0f * keys[k].time - keys[k - 1].time;
        v1 = keys[k].value;
    }

    return (v1 - v0) / (t1 - t0);
}

void box_curve_span_controls(
    const Curve *curve,
    int32_t span,
    float *control1,
    float *control2)
{
    const CurveKey *keys = ecs_vec_first_t(&curve->keys, CurveKey);
    int32_t count = ecs_vec_count(&curve->keys);
    ecs_assert(span >= 0 && span < count - 1, ECS_INVALID_PARAMETER, NULL);
    const CurveKey *a = &keys[span], *b = &keys[span + 1];
    float third = (b->time - a->time) / 3.0f;

    if (curve->kind == CurveCatmullRom) {
        *control1 = a->value + box_curve_catmull_rom_slope(keys, count, span) * third;
        *control2 = b->value - box_curve_catmull_rom_slope(keys, count, span + 1) * third;
    } else if (curve->kind == CurveBezier) {
        *control1 = a->value + a->out * third;
        *control2 = b->value - b->in * third;
    } else {
        float step = (b->value - a->value) / 3.0f;
        *control1 = a->value + step;
        *control2 = b->value - step;
    }
}

/** Index of the span that holds a time: the last key at or before it.
 *
 * @param keys The keys
 * @param count Number of keys, at least 2
 * @param time A time from the first to before the last key
 * @return Index of the first key of the span, 0 to count - 2
 */
static int32_t box_curve_find_span(
    const CurveKey *keys,
    int32_t count,
    float time)
{
    int32_t lo = 0, hi = count - 2;
    while (lo < hi) {
        int32_t mid = (lo + hi + 1) / 2;
        if (keys[mid].time <= time) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    return lo;
}

float box_curve_value(
    const Curve *curve,
    float time,
    float fallback)
{
    const CurveKey *keys = ecs_vec_first_t(&curve->keys, CurveKey);
    int32_t count = ecs_vec_count(&curve->keys);
    float result = fallback;
    if (!count) {
        goto done;
    }

    if (count == 1 || !(time > keys[0].time)) {
        result = keys[0].value;
        goto done;
    }

    if (time >= keys[count - 1].time) {
        result = keys[count - 1].value;
        goto done;
    }

    int32_t span = box_curve_find_span(keys, count, time);
    const CurveKey *a = &keys[span], *b = &keys[span + 1];
    float c1, c2;
    box_curve_span_controls(curve, span, &c1, &c2);
    result = box_curve_cubic(a->value, c1, c2, b->value, (time - a->time) / (b->time - a->time));
done:
    return result;
}

/** Area under one span of a curve from its start to a parameter, from the power form of its
 * cubic, a + b s + c s^2 + d s^3, integrated in closed form.
 *
 * @param curve The curve
 * @param span Index of the first key of the span
 * @param s The curve parameter in [0, 1]
 * @return The area from the time of the first key to the time at s
 */
static double box_curve_span_area(
    const Curve *curve,
    int32_t span,
    double s)
{
    const CurveKey *keys = ecs_vec_first_t(&curve->keys, CurveKey);
    float c1, c2;
    box_curve_span_controls(curve, span, &c1, &c2);
    double p0 = keys[span].value, p1 = c1, p2 = c2, p3 = keys[span + 1].value;
    double h = (double)keys[span + 1].time - (double)keys[span].time;
    double a = p0;
    double b = 3.0 * (p1 - p0);
    double c = 3.0 * (p0 - 2.0 * p1 + p2);
    double d = p3 - p0 + 3.0 * (p1 - p2);

    return h * s * (a + s * (b / 2.0 + s * (c / 3.0 + s * d / 4.0)));
}

/** Area under a curve with at least one key from its first key to a time, negative before it.
 *
 * @param curve The curve
 * @param time The time
 * @return The area
 */
static double box_curve_area_to(
    const Curve *curve,
    float time)
{
    const CurveKey *keys = ecs_vec_first_t(&curve->keys, CurveKey);
    int32_t count = ecs_vec_count(&curve->keys);
    double first = keys[0].time, last = keys[count - 1].time;
    double result = 0.0;
    if (count == 1 || !(time > first)) {
        result = keys[0].value * ((double)time - first);
        goto done;
    }

    int32_t end = time >= last ? count - 1 : box_curve_find_span(keys, count, time);
    int32_t i;
    for (i = 0; i < end; i ++) {
        result += box_curve_span_area(curve, i, 1.0);
    }

    if (time >= last) {
        result += keys[count - 1].value * ((double)time - last);
    } else {
        const CurveKey *a = &keys[end], *b = &keys[end + 1];
        result += box_curve_span_area(curve, end,
            ((double)time - a->time) / ((double)b->time - a->time));
    }

done:
    return result;
}

float box_curve_area(
    const Curve *curve,
    float from,
    float to,
    float fallback)
{
    float result;
    if (!ecs_vec_count(&curve->keys)) {
        result = fallback * (to - from);
    } else {
        result = (float)(box_curve_area_to(curve, to) - box_curve_area_to(curve, from));
    }

    return result;
}

void box_curve_bake(
    const Curve *curve,
    float from,
    float to,
    int32_t count,
    float fallback,
    float *out)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        float f = count > 1 ? (float)i / (float)(count - 1) : 0.0f;
        out[i] = box_curve_value(curve, box_curve_mix(from, to, f), fallback);
    }
}

/** Derivative of one coordinate of a cubic Bezier from 0 to 1.
 *
 * @param p1 Coordinate of the first control point
 * @param p2 Coordinate of the second control point
 * @param s Curve parameter, 0..1
 * @return The derivative at s
 */
static float box_curve_unit_slope(
    float p1,
    float p2,
    float s)
{
    float u = 1.0f - s;

    return 3.0f * u * u * p1 + 6.0f * u * s * (p2 - p1) + 3.0f * s * s * (1.0f - p2);
}

float box_curve_solve(
    float x1,
    float x2,
    float x)
{
    float t = x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f;
    float s = t;
    bool solved = false;
    int32_t i;
    for (i = 0; i < BOX_CURVE_NEWTON_STEPS && !solved; i ++) {
        float err = box_curve_cubic(0.0f, x1, x2, 1.0f, s) - t;
        float slope = box_curve_unit_slope(x1, x2, s);
        if (fabsf(err) < BOX_CURVE_EPSILON) {
            solved = true;
        } else if (fabsf(slope) < BOX_CURVE_EPSILON) {
            break;
        } else {
            s -= err / slope;
        }

        if (s < 0.0f || s > 1.0f) {
            break;
        }
    }

    if (!solved) {
        float lo = 0.0f, hi = 1.0f;
        s = t;
        for (i = 0; i < BOX_CURVE_BISECT_STEPS; i ++) {
            float v = box_curve_cubic(0.0f, x1, x2, 1.0f, s);
            if (fabsf(v - t) < BOX_CURVE_EPSILON) {
                break;
            }

            if (v < t) {
                lo = s;
            } else {
                hi = s;
            }

            s = 0.5f * (lo + hi);
        }
    }

    return s;
}

float box_curve_ease(
    float x1,
    float y1,
    float x2,
    float y2,
    float x)
{
    return box_curve_cubic(0.0f, y1, y2, 1.0f, box_curve_solve(x1, x2, x));
}

char* box_curve_validate(
    const Curve *curve)
{
    const CurveKey *keys = ecs_vec_first_t(&curve->keys, CurveKey);
    int32_t count = ecs_vec_count(&curve->keys);
    char *result = NULL;
    int32_t i;
    if (curve->kind < CurveLinear || curve->kind > CurveBezier) {
        result = flecs_asprintf(
            "kind %d is not CurveLinear, CurveCatmullRom or CurveBezier", (int)curve->kind);
        goto done;
    }

    for (i = 0; i < count && !result; i ++) {
        const CurveKey *k = &keys[i];
        if (!isfinite(k->time)) {
            result = flecs_asprintf("key %d: time is not finite", (int)i);
        } else if (!isfinite(k->value)) {
            result = flecs_asprintf("key %d: value is not finite", (int)i);
        } else if (!isfinite(k->in)) {
            result = flecs_asprintf("key %d: in is not finite", (int)i);
        } else if (!isfinite(k->out)) {
            result = flecs_asprintf("key %d: out is not finite", (int)i);
        } else if (i > 0 && !(k->time > keys[i - 1].time)) {
            result = flecs_asprintf("key %d: time %g is not after time %g of key %d",
                (int)i, (double)k->time, (double)keys[i - 1].time, (int)(i - 1));
        }
    }

done:
    return result;
}

void BoxCurveImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCurve);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, CurveKind);
    ECS_META_COMPONENT(world, CurveKey);
    ECS_META_COMPONENT(world, Curve);
    box_owned_hooks(world, Curve, BOX_OWNED_VEC(Curve, keys, CurveKey));
}
