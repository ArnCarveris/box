/**
 * @file Curve.c
 * @brief Tests for the curve module: evaluation of every kind, area, baking, the x solver,
 * validation and the Curve type in script.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>

/** Two curves in one component, as later consumers embed them */
typedef struct CurvePair {
    Curve first;  /**< First curve */
    float scale;  /**< A plain member between the curves */
    Curve second; /**< Second curve */
} CurvePair;

static ECS_COMPONENT_DECLARE(CurvePair);

/* The math cases call no flecs function that would set up the OS API they allocate with. */
void Curve_setup(void) {
    ecs_os_set_api_defaults();
}

/** Make a curve that owns a copy of keys.
 *
 * @param kind The kind
 * @param keys The keys
 * @param count Number of keys
 * @return The curve, to free with curve_free
 */
static Curve curve_make(
    CurveKind kind,
    const CurveKey *keys,
    int32_t count)
{
    Curve result = { .kind = kind };
    ecs_vec_init_t(NULL, &result.keys, CurveKey, count);
    int32_t i;
    for (i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &result.keys, CurveKey) = keys[i];
    }

    return result;
}

/** Free the keys of a curve made with curve_make.
 *
 * @param curve The curve
 */
static void curve_free(
    Curve *curve)
{
    ecs_vec_fini_t(NULL, &curve->keys, CurveKey);
}

/** Cubic Bezier in Bernstein form, independent of the module.
 *
 * @param p0 Start value
 * @param c1 First control
 * @param c2 Second control
 * @param p1 End value
 * @param s Parameter
 * @return The value
 */
static double curve_bernstein(
    double p0,
    double c1,
    double c2,
    double p1,
    double s)
{
    double u = 1.0 - s;

    return u * u * u * p0 + 3.0 * u * u * s * c1 + 3.0 * u * s * s * c2 + s * s * s * p1;
}

/** Cubic Hermite span, independent of the module.
 *
 * @param t0 Time of the first key
 * @param v0 Value of the first key
 * @param m0 Slope out of the first key
 * @param t1 Time of the second key
 * @param v1 Value of the second key
 * @param m1 Slope in to the second key
 * @param t The time
 * @return The value at t
 */
static double curve_hermite(
    double t0,
    double v0,
    double m0,
    double t1,
    double v1,
    double m1,
    double t)
{
    double h = t1 - t0;

    return curve_bernstein(v0, v0 + m0 * h / 3.0, v1 - m1 * h / 3.0, v1, (t - t0) / h);
}

/** Integrate a curve numerically with Simpson's rule.
 *
 * @param curve The curve
 * @param from Start time
 * @param to End time
 * @param fallback Value without keys
 * @return The approximate area
 */
static double curve_simpson(
    const Curve *curve,
    float from,
    float to,
    float fallback)
{
    const int32_t steps = 4000;
    double h = ((double)to - from) / steps, sum = 0.0;
    int32_t i;
    for (i = 0; i <= steps; i ++) {
        double w = (i == 0 || i == steps) ? 1.0 : (i % 2 ? 4.0 : 2.0);
        sum += w * box_curve_value(curve, (float)(from + h * i), fallback);
    }

    return sum * h / 3.0;
}

/** Check that a curve has its key values at its keys, holds them outside and has the expected
 * values between the keys.
 *
 * @param curve The curve
 * @param between Times between keys
 * @param expected Expected values at the times
 * @param count Number of times
 */
static void curve_check(
    const Curve *curve,
    const float *between,
    const double *expected,
    int32_t count)
{
    const CurveKey *keys = ecs_vec_first_t(&curve->keys, CurveKey);
    int32_t n = ecs_vec_count(&curve->keys), i;
    for (i = 0; i < n; i ++) {
        test_flt(box_curve_value(curve, keys[i].time, -1.0f), keys[i].value);
    }

    for (i = 0; i < count; i ++) {
        test_assert(fabs(box_curve_value(curve, between[i], -1.0f) - expected[i]) < 1e-5);
    }

    test_flt(box_curve_value(curve, keys[0].time - 1.0f, -1.0f), keys[0].value);
    test_flt(box_curve_value(curve, keys[0].time - 1e6f, -1.0f), keys[0].value);
    test_flt(box_curve_value(curve, keys[n - 1].time + 0.5f, -1.0f), keys[n - 1].value);
    test_flt(box_curve_value(curve, keys[n - 1].time + 1e6f, -1.0f), keys[n - 1].value);
}

/** Check the area of a curve against numeric integration between several pairs of times.
 *
 * @param curve The curve
 */
static void curve_check_area(
    const Curve *curve)
{
    static const float ranges[][2] = {
        { 0.0f, 1.0f }, { 0.13f, 0.77f }, { -0.5f, 0.3f }, { 0.6f, 1.8f },
        { -1.0f, 2.0f }, { 0.41f, 0.43f }, { 0.9f, 0.2f }, { 0.25f, 0.25f }
    };
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(ranges) / sizeof(ranges[0])); i ++) {
        double expected = curve_simpson(curve, ranges[i][0], ranges[i][1], 7.0f);
        float area = box_curve_area(curve, ranges[i][0], ranges[i][1], 7.0f);
        test_assert(fabs(area - expected) < 1e-4);
    }
}

void Curve_linear(void) {
    // curve/math
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.5f }, { 2.0f, -0.5f }};
    Curve curve = curve_make(CurveLinear, keys, 4);
    float between[] = { 0.25f, 0.1f, 0.75f, 1.5f, 1.9f };
    double expected[] = { 0.5, 0.2, 0.75, 0.0, -0.4 };
    curve_check(&curve, between, expected, 5);
    curve_free(&curve);
}

void Curve_catmull_rom(void) {
    // curve/math
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 1.0f, 1.0f }, { 2.0f, 3.0f }, { 3.0f, 2.0f }};
    Curve curve = curve_make(CurveCatmullRom, keys, 4);
    double p[] = { 0.0, 0.0, 1.0, 3.0, 2.0, 2.0 };
    float between[6];
    double expected[6];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        double c1 = p[i + 1] + (p[i + 2] - p[i]) / 6.0;
        double c2 = p[i + 2] - (p[i + 3] - p[i + 1]) / 6.0;
        between[i * 2] = (float)i + 0.3f;
        expected[i * 2] = curve_bernstein(p[i + 1], c1, c2, p[i + 2], 0.3);
        between[i * 2 + 1] = (float)i + 0.5f;
        expected[i * 2 + 1] = curve_bernstein(p[i + 1], c1, c2, p[i + 2], 0.5);
    }

    curve_check(&curve, between, expected, 6);
    curve_free(&curve);
}

void Curve_bezier(void) {
    // curve/math
    CurveKey keys[] = {
        { .time = 0.0f, .value = 1.0f, .out = -2.0f },
        { .time = 0.4f, .value = 0.5f, .in = 1.0f, .out = 3.0f },
        { .time = 1.0f, .value = 0.0f, .in = 0.0f }
    };
    Curve curve = curve_make(CurveBezier, keys, 3);
    float between[] = { 0.1f, 0.2f, 0.3f, 0.5f, 0.7f, 0.95f };
    double expected[6];
    int32_t i;
    for (i = 0; i < 6; i ++) {
        int32_t k = between[i] < 0.4f ? 0 : 1;
        expected[i] = curve_hermite(keys[k].time, keys[k].value, keys[k].out,
            keys[k + 1].time, keys[k + 1].value, keys[k + 1].in, between[i]);
    }

    curve_check(&curve, between, expected, 6);
    curve_free(&curve);
}

void Curve_no_keys(void) {
    // curve/math
    CurveKind kinds[] = { CurveLinear, CurveCatmullRom, CurveBezier };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        Curve curve = { .kind = kinds[i] };
        test_flt(box_curve_value(&curve, 0.0f, 2.5f), 2.5f);
        test_flt(box_curve_value(&curve, -3.0f, 2.5f), 2.5f);
        test_flt(box_curve_value(&curve, 7.0f, 2.5f), 2.5f);
        test_flt(box_curve_area(&curve, 0.0f, 2.0f, 2.5f), 5.0f);
        test_flt(box_curve_area(&curve, 2.0f, 0.0f, 2.5f), -5.0f);
        float samples[4];
        box_curve_bake(&curve, 0.0f, 1.0f, 4, 2.5f, samples);
        test_flt(samples[0], 2.5f);
        test_flt(samples[3], 2.5f);
        test_assert(box_curve_validate(&curve) == NULL);
    }
}

void Curve_one_key(void) {
    // curve/math
    CurveKind kinds[] = { CurveLinear, CurveCatmullRom, CurveBezier };
    CurveKey key = { .time = 0.5f, .value = 3.0f, .in = 4.0f, .out = -4.0f };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        Curve curve = curve_make(kinds[i], &key, 1);
        test_flt(box_curve_value(&curve, 0.5f, 1.0f), 3.0f);
        test_flt(box_curve_value(&curve, -2.0f, 1.0f), 3.0f);
        test_flt(box_curve_value(&curve, 0.7f, 1.0f), 3.0f);
        test_flt(box_curve_value(&curve, 9.0f, 1.0f), 3.0f);
        test_assert(fabsf(box_curve_area(&curve, 0.0f, 2.0f, 1.0f) - 6.0f) < 1e-5f);
        test_assert(fabsf(box_curve_area(&curve, 1.0f, -1.0f, 1.0f) + 6.0f) < 1e-5f);
        test_assert(box_curve_validate(&curve) == NULL);
        curve_free(&curve);
    }
}

void Curve_uneven_linear(void) {
    // curve/math
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 0.1f, 1.0f }, { 0.9f, 3.0f }, { 1.0f, 0.0f }};
    Curve curve = curve_make(CurveLinear, keys, 4);
    test_assert(fabsf(box_curve_value(&curve, 0.05f, 0.0f) - 0.5f) < 1e-5f);
    test_assert(fabsf(box_curve_value(&curve, 0.5f, 0.0f) - 2.0f) < 1e-5f);
    test_assert(fabsf(box_curve_value(&curve, 0.3f, 0.0f) - 1.5f) < 1e-5f);
    test_assert(fabsf(box_curve_value(&curve, 0.95f, 0.0f) - 1.5f) < 1e-5f);
    curve_free(&curve);
}

void Curve_uneven_catmull_rom(void) {
    // curve/math
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 0.1f, 0.2f }, { 0.7f, 1.4f }, { 0.8f, 1.6f },
        { 2.0f, 4.0f }};
    Curve curve = curve_make(CurveCatmullRom, keys, 5);
    float t;
    for (t = 0.1f; t <= 0.8f; t += 0.05f) {
        test_assert(fabsf(box_curve_value(&curve, t, 0.0f) - 2.0f * t) < 1e-5f);
    }

    float s = 0.6f;
    double expected = curve_hermite(0.8, 1.6, 2.0, 2.0, 4.0, 1.0, 0.8 + 1.2 * s);
    test_assert(fabs(box_curve_value(&curve, 0.8f + 1.2f * s, 0.0f) - expected) < 1e-5);
    expected = curve_hermite(0.0, 0.0, 1.0, 0.1, 0.2, 2.0, 0.04);
    test_assert(fabs(box_curve_value(&curve, 0.04f, 0.0f) - expected) < 1e-5);
    curve_free(&curve);
}

/* The numeric slopes at a key are one sided differences of second order, so the curvature of
 * the short spans cancels. */
void Curve_catmull_rom_slope_continuous(void) {
    // curve/math
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 0.2f, 1.0f }, { 0.9f, -0.5f }, { 1.0f, 0.3f },
        { 1.6f, 0.0f }};
    Curve curve = curve_make(CurveCatmullRom, keys, 5);
    int32_t k;
    for (k = 1; k < 4; k ++) {
        float c1, c2, n1, n2;
        box_curve_span_controls(&curve, k - 1, &c1, &c2);
        box_curve_span_controls(&curve, k, &n1, &n2);
        float h0 = keys[k].time - keys[k - 1].time, h1 = keys[k + 1].time - keys[k].time;
        float left = 3.0f * (keys[k].value - c2) / h0;
        float right = 3.0f * (n1 - keys[k].value) / h1;
        test_assert(fabsf(left - right) < 1e-4f);
        float expected = (keys[k + 1].value - keys[k - 1].value) /
            (keys[k + 1].time - keys[k - 1].time);
        test_assert(fabsf(left - expected) < 1e-4f);

        float e = 1e-3f, t = keys[k].time, v = box_curve_value(&curve, t, 0.0f);
        float numeric_left = (3.0f * v - 4.0f * box_curve_value(&curve, t - e, 0.0f) +
            box_curve_value(&curve, t - 2.0f * e, 0.0f)) / (2.0f * e);
        float numeric_right = (4.0f * box_curve_value(&curve, t + e, 0.0f) - 3.0f * v -
            box_curve_value(&curve, t + 2.0f * e, 0.0f)) / (2.0f * e);
        test_assert(fabsf(numeric_left - expected) < 1e-2f);
        test_assert(fabsf(numeric_right - expected) < 1e-2f);
    }

    curve_free(&curve);
}

/** Uneven looping keys from 0 to 1 that end on their start value */
static const CurveKey curve_loop_keys[] = {
    { 0.0f, 0.4f }, { 0.15f, 1.2f }, { 0.45f, 0.7f }, { 0.8f, 1.1f }, { 1.0f, 0.4f }
};

/* Without a period the ends mirror themselves and the slopes differ at the wrap. The inner keys
 * keep their slopes, the keys their values. */
void Curve_loop_catmull_rom_wrap(void) {
    // curve/loop
    Curve curve = curve_make(CurveCatmullRom, curve_loop_keys, 5);
    const CurveKey *keys = curve_loop_keys;
    float expected = (keys[1].value - keys[3].value) / (keys[1].time - (keys[3].time - 1.0f));
    float c1, c2, n1, n2;
    box_curve_loop_span_controls(&curve, 1.0f, 0, &c1, &c2);
    box_curve_loop_span_controls(&curve, 1.0f, 3, &n1, &n2);
    float into_first = 3.0f * (c1 - keys[0].value) / keys[1].time;
    float out_of_last = 3.0f * (keys[4].value - n2) / (keys[4].time - keys[3].time);
    test_assert(fabsf(into_first - expected) < 1e-4f);
    test_assert(fabsf(out_of_last - expected) < 1e-4f);

    float e = 1e-3f, v0 = box_curve_loop_value(&curve, 1.0f, 0.0f, 0.0f);
    float v1 = box_curve_loop_value(&curve, 1.0f, 1.0f, 0.0f);
    test_flt(v0, v1);
    test_assert(fabsf(box_curve_loop_value(&curve, 1.0f, 1.0f - e, 0.0f) -
        box_curve_loop_value(&curve, 1.0f, e, 0.0f)) < 1e-3f);
    float before = (3.0f * v1 - 4.0f * box_curve_loop_value(&curve, 1.0f, 1.0f - e, 0.0f) +
        box_curve_loop_value(&curve, 1.0f, 1.0f - 2.0f * e, 0.0f)) / (2.0f * e);
    float after = (4.0f * box_curve_loop_value(&curve, 1.0f, e, 0.0f) - 3.0f * v0 -
        box_curve_loop_value(&curve, 1.0f, 2.0f * e, 0.0f)) / (2.0f * e);
    test_assert(fabsf(before - expected) < 1e-2f);
    test_assert(fabsf(after - expected) < 1e-2f);

    float plain_before = (3.0f * v1 - 4.0f * box_curve_value(&curve, 1.0f - e, 0.0f) +
        box_curve_value(&curve, 1.0f - 2.0f * e, 0.0f)) / (2.0f * e);
    float plain_after = (4.0f * box_curve_value(&curve, e, 0.0f) - 3.0f * v0 -
        box_curve_value(&curve, 2.0f * e, 0.0f)) / (2.0f * e);
    test_assert(fabsf(plain_before - plain_after) > 1.0f);

    int32_t k;
    for (k = 1; k < 3; k ++) {
        box_curve_span_controls(&curve, k, &c1, &c2);
        box_curve_loop_span_controls(&curve, 1.0f, k, &n1, &n2);
        test_flt(c1, n1);
        test_flt(c2, n2);
    }

    for (k = 0; k < 5; k ++) {
        test_flt(box_curve_loop_value(&curve, 1.0f, keys[k].time, 0.0f), keys[k].value);
    }

    curve_free(&curve);
}

void Curve_loop_two_keys(void) {
    // curve/loop
    CurveKey keys[] = {{ 0.0f, 1.0f }, { 1.0f, 3.0f }};
    Curve curve = curve_make(CurveCatmullRom, keys, 2);
    float c1, c2;
    box_curve_loop_span_controls(&curve, 1.0f, 0, &c1, &c2);
    test_assert(fabsf(c1 - (1.0f + 1.0f / 3.0f)) < 1e-5f);
    test_assert(fabsf(c2 - (3.0f - 1.0f / 3.0f)) < 1e-5f);
    curve_free(&curve);
}

void Curve_loop_area(void) {
    // curve/loop
    Curve curve = curve_make(CurveCatmullRom, curve_loop_keys, 5);
    static const float ranges[][2] = {
        { 0.0f, 1.0f }, { 0.13f, 0.77f }, { 0.9f, 1.0f }, { 0.0f, 0.1f }, { 0.9f, 0.2f }
    };
    int32_t i, j;
    for (i = 0; i < (int32_t)(sizeof(ranges) / sizeof(ranges[0])); i ++) {
        const int32_t steps = 4000;
        float from = ranges[i][0], to = ranges[i][1];
        double h = ((double)to - from) / steps, sum = 0.0;
        for (j = 0; j <= steps; j ++) {
            double w = (j == 0 || j == steps) ? 1.0 : (j % 2 ? 4.0 : 2.0);
            sum += w * box_curve_loop_value(&curve, 1.0f, (float)(from + h * j), 0.0f);
        }

        float area = box_curve_loop_area(&curve, 1.0f, from, to, 0.0f);
        test_assert(fabs(area - sum * h / 3.0) < 1e-4);
    }

    test_assert(fabsf(box_curve_loop_area(&curve, 1.0f, 0.0f, 1.0f, 0.0f) -
        box_curve_area(&curve, 0.0f, 1.0f, 0.0f)) > 1e-3f);
    curve_free(&curve);
}

/** Check that a curve looping over a period evaluates as without one: values and areas.
 *
 * @param curve The curve
 * @param period The period
 */
static void curve_check_loop_same(
    const Curve *curve,
    float period)
{
    int32_t i;
    for (i = -10; i <= 110; i ++) {
        float t = (float)i / 100.0f;
        test_flt(box_curve_loop_value(curve, period, t, 0.0f), box_curve_value(curve, t, 0.0f));
    }

    test_flt(box_curve_loop_area(curve, period, -0.2f, 1.3f, 0.0f),
        box_curve_area(curve, -0.2f, 1.3f, 0.0f));
    int32_t span;
    for (span = 0; span < ecs_vec_count(&curve->keys) - 1; span ++) {
        float c1, c2, n1, n2;
        box_curve_span_controls(curve, span, &c1, &c2);
        box_curve_loop_span_controls(curve, period, span, &n1, &n2);
        test_flt(c1, n1);
        test_flt(c2, n2);
    }
}

void Curve_loop_other_kinds_unchanged(void) {
    // curve/loop
    CurveKey keys[] = {
        { .time = 0.0f, .value = 0.4f, .out = 2.0f },
        { .time = 0.15f, .value = 1.2f, .in = -1.0f, .out = 0.5f },
        { .time = 0.45f, .value = 0.7f, .in = 0.0f, .out = 3.0f },
        { .time = 1.0f, .value = 0.4f, .in = -4.0f }
    };
    Curve linear = curve_make(CurveLinear, keys, 4);
    Curve bezier = curve_make(CurveBezier, keys, 4);
    curve_check_loop_same(&linear, 1.0f);
    curve_check_loop_same(&bezier, 1.0f);
    curve_free(&linear);
    curve_free(&bezier);
}

void Curve_loop_partial_keys_unchanged(void) {
    // curve/loop
    CurveKey late[] = {{ 0.1f, 0.4f }, { 0.5f, 1.2f }, { 1.0f, 0.4f }};
    CurveKey early[] = {{ 0.0f, 0.4f }, { 0.5f, 1.2f }, { 0.9f, 0.4f }};
    Curve a = curve_make(CurveCatmullRom, late, 3);
    Curve b = curve_make(CurveCatmullRom, early, 3);
    Curve c = curve_make(CurveCatmullRom, curve_loop_keys, 5);
    curve_check_loop_same(&a, 1.0f);
    curve_check_loop_same(&b, 1.0f);
    curve_check_loop_same(&c, 0.0f);
    curve_check_loop_same(&c, 2.0f);
    curve_free(&a);
    curve_free(&b);
    curve_free(&c);
}

void Curve_bezier_authored_slopes(void) {
    // curve/math
    CurveKey keys[] = {
        { .time = 0.0f, .value = 1.0f, .in = 9.0f, .out = -2.0f },
        { .time = 0.5f, .value = 0.5f, .in = 1.5f, .out = -3.0f },
        { .time = 1.25f, .value = 2.0f, .in = 0.25f, .out = 9.0f }
    };
    Curve curve = curve_make(CurveBezier, keys, 3);
    int32_t span;
    for (span = 0; span < 2; span ++) {
        float c1, c2;
        box_curve_span_controls(&curve, span, &c1, &c2);
        float h = keys[span + 1].time - keys[span].time;
        test_assert(fabsf(3.0f * (c1 - keys[span].value) / h - keys[span].out) < 1e-5f);
        test_assert(fabsf(3.0f * (keys[span + 1].value - c2) / h - keys[span + 1].in) < 1e-5f);
    }

    float e = 1e-3f;
    test_assert(fabsf((box_curve_value(&curve, e, 0.0f) - 1.0f) / e + 2.0f) < 0.05f);
    test_assert(fabsf((0.5f - box_curve_value(&curve, 0.5f - e, 0.0f)) / e - 1.5f) < 0.05f);
    test_assert(fabsf((box_curve_value(&curve, 0.5f + e, 0.0f) - 0.5f) / e + 3.0f) < 0.05f);
    test_assert(fabsf((2.0f - box_curve_value(&curve, 1.25f - e, 0.0f)) / e - 0.25f) < 0.05f);
    curve_free(&curve);
}

void Curve_bezier_time_linear(void) {
    // curve/math
    CurveKey keys[] = {
        { .time = 0.2f, .value = 1.0f, .out = 4.0f },
        { .time = 0.9f, .value = -1.0f, .in = -6.0f }
    };
    Curve curve = curve_make(CurveBezier, keys, 2);
    float t0 = keys[0].time, t1 = keys[1].time, h = t1 - t0;
    float c1, c2;
    box_curve_span_controls(&curve, 0, &c1, &c2);
    int32_t i;
    for (i = 0; i <= 16; i ++) {
        float s = (float)i / 16.0f;
        float time = box_curve_cubic(t0, t0 + h / 3.0f, t0 + 2.0f * h / 3.0f, t1, s);
        test_assert(fabsf(time - (t0 + s * h)) < 1e-6f);
        float value = box_curve_cubic(keys[0].value, c1, c2, keys[1].value, s);
        test_assert(fabsf(box_curve_value(&curve, time, 0.0f) - value) < 1e-5f);
    }

    curve_free(&curve);
}

void Curve_cubic3(void) {
    // curve/math
    vec3 a = { 0.0f, 1.0f, -2.0f }, b = { 1.0f, 3.0f, 0.5f }, c = { 2.0f, -1.0f, 4.0f };
    vec3 d = { 3.0f, 0.0f, 1.0f }, out;
    float s;
    for (s = 0.0f; s <= 1.0f; s += 0.125f) {
        box_curve_cubic3(a, b, c, d, s, out);
        int32_t i;
        for (i = 0; i < 3; i ++) {
            test_flt(out[i], box_curve_cubic(a[i], b[i], c[i], d[i], s));
            test_assert(fabs(out[i] - curve_bernstein(a[i], b[i], c[i], d[i], s)) < 1e-5);
        }
    }

    test_flt(box_curve_cubic(2.0f, 5.0f, -3.0f, 7.0f, 0.0f), 2.0f);
    test_flt(box_curve_cubic(2.0f, 5.0f, -3.0f, 7.0f, 1.0f), 7.0f);
}

void Curve_area_linear(void) {
    // curve/math
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 0.1f, 1.0f }, { 0.55f, -0.5f }, { 1.0f, 2.0f }};
    Curve curve = curve_make(CurveLinear, keys, 4);
    curve_check_area(&curve);
    test_assert(fabsf(box_curve_area(&curve, 0.0f, 0.1f, 0.0f) - 0.05f) < 1e-6f);
    curve_free(&curve);
}

void Curve_area_catmull_rom(void) {
    // curve/math
    CurveKey keys[] = {{ 0.0f, 0.3f }, { 0.4f, 1.8f }, { 0.5f, 1.0f }, { 1.0f, 2.2f }};
    Curve curve = curve_make(CurveCatmullRom, keys, 4);
    curve_check_area(&curve);
    curve_free(&curve);
}

void Curve_area_bezier(void) {
    // curve/math
    CurveKey keys[] = {
        { .time = 0.0f, .value = 1.0f, .out = -2.0f },
        { .time = 0.3f, .value = 0.5f, .in = 5.0f, .out = -1.0f },
        { .time = 1.0f, .value = 0.0f, .in = 0.0f }
    };
    Curve curve = curve_make(CurveBezier, keys, 3);
    curve_check_area(&curve);
    curve_free(&curve);
}

void Curve_bake(void) {
    // curve/math
    CurveKind kinds[] = { CurveLinear, CurveCatmullRom, CurveBezier };
    CurveKey keys[] = {
        { .time = 0.0f, .value = 0.0f, .out = 1.0f },
        { .time = 0.3f, .value = 1.0f, .in = -1.0f, .out = 2.0f },
        { .time = 0.8f, .value = 0.2f, .in = 0.0f }
    };
    float samples[64];
    int32_t i, j;
    for (i = 0; i < 3; i ++) {
        Curve curve = curve_make(kinds[i], keys, 3);
        box_curve_bake(&curve, -0.1f, 1.1f, 64, 0.0f, samples);
        for (j = 0; j < 64; j ++) {
            float t = -0.1f * (1.0f - (float)j / 63.0f) + 1.1f * ((float)j / 63.0f);
            test_flt(samples[j], box_curve_value(&curve, t, 0.0f));
        }

        test_flt(samples[0], 0.0f);
        test_flt(samples[63], 0.2f);
        box_curve_bake(&curve, 0.3f, 0.9f, 1, 0.0f, samples);
        test_flt(samples[0], 1.0f);
        curve_free(&curve);
    }
}

void Curve_solve_css(void) {
    // curve/math
    static const struct {
        float x1, y1, x2, y2;
        float expected[3];
    } cases[] = {
        { 0.25f, 0.1f, 0.25f, 1.0f, { 0.408511f, 0.802403f, 0.960459f } },
        { 0.42f, 0.0f, 1.0f, 1.0f, { 0.093465f, 0.315357f, 0.621862f } },
        { 0.0f, 0.0f, 0.58f, 1.0f, { 0.378138f, 0.684643f, 0.906535f } },
        { 0.42f, 0.0f, 0.58f, 1.0f, { 0.129162f, 0.5f, 0.870838f } }
    };
    float xs[] = { 0.25f, 0.5f, 0.75f };
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 3; j ++) {
            float y = box_curve_ease(cases[i].x1, cases[i].y1, cases[i].x2, cases[i].y2, xs[j]);
            test_assert(fabsf(y - cases[i].expected[j]) < 1e-4f);
            float s = box_curve_solve(cases[i].x1, cases[i].x2, xs[j]);
            test_assert(fabsf(box_curve_cubic(0.0f, cases[i].x1, cases[i].x2, 1.0f, s) -
                xs[j]) < 1e-5f);
        }

        test_flt(box_curve_ease(cases[i].x1, cases[i].y1, cases[i].x2, cases[i].y2, 0.0f), 0.0f);
        test_flt(box_curve_ease(cases[i].x1, cases[i].y1, cases[i].x2, cases[i].y2, 1.0f), 1.0f);
    }

    for (j = 0; j < 3; j ++) {
        test_assert(fabsf(box_curve_ease(0.42f, 0.0f, 0.58f, 1.0f, xs[j]) -
            box_transition_ease(EaseInOut, xs[j])) < 1e-5f);
    }
}

void Curve_solve_degenerate(void) {
    // curve/math
    static const float handles[][2] = {
        { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.5f, 0.5f },
        { 0.999f, 0.001f }
    };
    int32_t i, j;
    for (i = 0; i < (int32_t)(sizeof(handles) / sizeof(handles[0])); i ++) {
        float x1 = handles[i][0], x2 = handles[i][1];
        for (j = 0; j <= 20; j ++) {
            float x = (float)j / 20.0f;
            float s = box_curve_solve(x1, x2, x);
            test_assert(s >= 0.0f && s <= 1.0f);
            test_assert(fabsf(box_curve_cubic(0.0f, x1, x2, 1.0f, s) - x) < 1e-5f);
        }

        test_assert(fabsf(box_curve_solve(x1, x2, -1.0f)) < 1e-5f);
        test_assert(fabsf(box_curve_solve(x1, x2, 2.0f) - 1.0f) < 1e-5f);
    }

    test_assert(fabsf(box_curve_ease(1.0f / 3.0f, 1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f,
        0.3f) - 0.3f) < 1e-5f);
}

/** Validate a curve and compare the message.
 *
 * @param curve The curve
 * @param expected The expected message, NULL when valid
 */
static void curve_expect(
    const Curve *curve,
    const char *expected)
{
    char *msg = box_curve_validate(curve);
    if (expected) {
        test_assert(msg != NULL);
        test_str(msg, expected);
    } else {
        test_assert(msg == NULL);
    }

    ecs_os_free(msg);
}

void Curve_validate_valid(void) {
    // curve/validate
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 0.1f, 1.0f, -1.0f, 2.0f }, { 1.0f, 0.0f }};
    Curve curve = curve_make(CurveBezier, keys, 3);
    curve_expect(&curve, NULL);
    curve_free(&curve);
}

void Curve_validate_kind(void) {
    // curve/validate
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 1.0f, 1.0f }};
    Curve curve = curve_make((CurveKind)3, keys, 2);
    curve_expect(&curve, "kind 3 is not CurveLinear, CurveCatmullRom or CurveBezier");
    curve.kind = (CurveKind)-1;
    curve_expect(&curve, "kind -1 is not CurveLinear, CurveCatmullRom or CurveBezier");
    curve_free(&curve);
}

void Curve_validate_time_order(void) {
    // curve/validate
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 0.5f, 1.0f }, { 0.25f, 0.0f }};
    Curve curve = curve_make(CurveLinear, keys, 3);
    curve_expect(&curve, "key 2: time 0.25 is not after time 0.5 of key 1");
    curve_free(&curve);
}

void Curve_validate_time_equal(void) {
    // curve/validate
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 0.5f, 1.0f }, { 0.5f, 0.0f }, { 1.0f, 0.0f }};
    Curve curve = curve_make(CurveCatmullRom, keys, 4);
    curve_expect(&curve, "key 2: time 0.5 is not after time 0.5 of key 1");
    curve_free(&curve);
}

void Curve_validate_time_not_finite(void) {
    // curve/validate
    CurveKey keys[] = {{ 0.0f, 0.0f }, { NAN, 1.0f }, { 1.0f, 0.0f }};
    Curve curve = curve_make(CurveLinear, keys, 3);
    curve_expect(&curve, "key 1: time is not finite");
    ecs_vec_get_t(&curve.keys, CurveKey, 1)->time = 0.5f;
    ecs_vec_get_t(&curve.keys, CurveKey, 2)->time = INFINITY;
    curve_expect(&curve, "key 2: time is not finite");
    curve_free(&curve);
}

void Curve_validate_value_not_finite(void) {
    // curve/validate
    CurveKey keys[] = {{ 0.0f, INFINITY }, { 1.0f, 0.0f }};
    Curve curve = curve_make(CurveLinear, keys, 2);
    curve_expect(&curve, "key 0: value is not finite");
    curve_free(&curve);
}

void Curve_validate_slope_not_finite(void) {
    // curve/validate
    CurveKey keys[] = {{ 0.0f, 0.0f }, { 1.0f, 0.0f, -INFINITY, 0.0f }, { 2.0f, 0.0f }};
    Curve curve = curve_make(CurveBezier, keys, 3);
    curve_expect(&curve, "key 1: in is not finite");
    ecs_vec_get_t(&curve.keys, CurveKey, 1)->in = 0.0f;
    ecs_vec_get_t(&curve.keys, CurveKey, 2)->out = NAN;
    curve_expect(&curve, "key 2: out is not finite");

    curve.kind = CurveLinear;
    curve_expect(&curve, "key 2: out is not finite");
    curve_free(&curve);
}

/** Import the box module and register CurvePair.
 *
 * @return The world
 */
static ecs_world_t* curve_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_COMPONENT_DEFINE(world, CurvePair);
    ecs_struct(world, {
        .entity = ecs_id(CurvePair),
        .members = {
            { .name = "first", .type = ecs_id(Curve) },
            { .name = "scale", .type = ecs_id(ecs_f32_t) },
            { .name = "second", .type = ecs_id(Curve) }
        }
    });
    box_owned_hooks(world, CurvePair,
        BOX_OWNED_CURVE(CurvePair, first),
        BOX_OWNED_CURVE(CurvePair, second));

    return world;
}

void Curve_script_positional(void) {
    // curve/script
    ecs_world_t *world = curve_world();
    test_int(ecs_script_run(world, "curves",
        "using box.curve\n"
        "e {\n"
        "  CurvePair: {{CurveBezier, [{0, 1, 0, -2}, {1, 0}]}, 2, {CurveCatmullRom, [{0, 0.3}, "
        "{0.4, 1.8}, {1, 2.2}]}}\n"
        "}\n", NULL), 0);

    ecs_entity_t e = ecs_lookup(world, "e");
    const CurvePair *pair = ecs_get(world, e, CurvePair);
    test_assert(pair != NULL);
    test_int(pair->first.kind, CurveBezier);
    test_int(ecs_vec_count(&pair->first.keys), 2);
    const CurveKey *keys = ecs_vec_first_t(&pair->first.keys, CurveKey);
    test_flt(keys[0].time, 0.0f);
    test_flt(keys[0].value, 1.0f);
    test_flt(keys[0].in, 0.0f);
    test_flt(keys[0].out, -2.0f);
    test_flt(keys[1].time, 1.0f);
    test_flt(keys[1].value, 0.0f);
    test_flt(pair->scale, 2.0f);
    test_int(pair->second.kind, CurveCatmullRom);
    test_int(ecs_vec_count(&pair->second.keys), 3);
    keys = ecs_vec_first_t(&pair->second.keys, CurveKey);
    test_flt(keys[1].time, 0.4f);
    test_flt(keys[1].value, 1.8f);
    test_flt(keys[2].value, 2.2f);
    test_assert(box_curve_validate(&pair->first) == NULL);
    test_assert(box_curve_validate(&pair->second) == NULL);

    ecs_fini(world);
}

void Curve_script_named(void) {
    // curve/script
    ecs_world_t *world = curve_world();
    test_int(ecs_script_run(world, "curves",
        "using box.curve\n"
        "e {\n"
        "  CurvePair: {\n"
        "    first: {keys: [{0, 0}, {0.1, 1}, {1, 0}]}\n"
        "    second: {kind: CurveBezier, keys: [{time: 0, value: 1, out: -2}, "
        "{time: 1, value: 0, in: 0.5}]}\n"
        "  }\n"
        "}\n", NULL), 0);

    ecs_entity_t e = ecs_lookup(world, "e");
    const CurvePair *pair = ecs_get(world, e, CurvePair);
    test_assert(pair != NULL);
    test_int(pair->first.kind, CurveLinear);
    test_int(ecs_vec_count(&pair->first.keys), 3);
    test_flt(box_curve_value(&pair->first, 0.1f, 0.0f), 1.0f);
    test_int(pair->second.kind, CurveBezier);
    test_int(ecs_vec_count(&pair->second.keys), 2);
    const CurveKey *keys = ecs_vec_first_t(&pair->second.keys, CurveKey);
    test_flt(keys[0].value, 1.0f);
    test_flt(keys[0].out, -2.0f);
    test_flt(keys[0].in, 0.0f);
    test_flt(keys[1].time, 1.0f);
    test_flt(keys[1].in, 0.5f);
    test_flt(pair->scale, 0.0f);

    ecs_fini(world);
}

/** Allocations not freed yet, counted by the flecs os api.
 *
 * @return Allocations minus frees
 */
static int64_t curve_live_allocs(void) {
    return ecs_os_api_malloc_count + ecs_os_api_calloc_count - ecs_os_api_free_count;
}

/** Copy, move and delete CurvePair and Curve components of a prefab.
 *
 * @param world The world
 * @param prefab Prefab with a CurvePair and a Curve
 */
static void curve_churn(
    ecs_world_t *world,
    ecs_entity_t prefab)
{
    const CurvePair *src = ecs_get(world, prefab, CurvePair);
    ecs_entity_t a = ecs_new(world);
    ecs_set_id(world, a, ecs_id(CurvePair), sizeof(CurvePair), src);
    ecs_set_id(world, a, ecs_id(Curve), sizeof(Curve), ecs_get(world, prefab, Curve));
    ecs_set_id(world, a, ecs_id(CurvePair), sizeof(CurvePair), src);
    ecs_add(world, a, Position);

    const CurvePair *copy = ecs_get(world, a, CurvePair);
    test_assert(ecs_vec_first(&copy->first.keys) != ecs_vec_first(&src->first.keys));
    test_int(ecs_vec_count(&copy->first.keys), ecs_vec_count(&src->first.keys));
    test_int(ecs_vec_count(&copy->second.keys), ecs_vec_count(&src->second.keys));

    ecs_entity_t b = ecs_clone(world, 0, a, true);
    ecs_entity_t c = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_auto_override(world, c, CurvePair);
    ecs_add(world, c, Rotation);
    ecs_remove(world, b, Curve);
    ecs_delete(world, a);
    ecs_delete(world, b);
    ecs_delete(world, c);
}

void Curve_copy_delete_no_leak(void) {
    // curve/script
    ecs_world_t *world = curve_world();
    test_int(ecs_script_run(world, "curves",
        "using box.curve\n"
        "prefab p {\n"
        "  CurvePair: {\n"
        "    first: {kind: CurveCatmullRom, keys: [{0, 0}, {0.5, 1}, {1, 0}]}\n"
        "    second: {kind: CurveBezier, keys: [{0, 1, 0, -2}, {1, 0}]}\n"
        "  }\n"
        "  Curve: {keys: [{0, 1}, {1, 2}]}\n"
        "}\n", NULL), 0);
    ecs_entity_t prefab = ecs_lookup(world, "p");
    test_assert(prefab != 0);

    curve_churn(world, prefab);
    int64_t before = curve_live_allocs();
    int32_t i;
    for (i = 0; i < 4; i ++) {
        curve_churn(world, prefab);
    }

    test_int(curve_live_allocs(), before);

    ecs_delete(world, prefab);
    ecs_fini(world);
}
