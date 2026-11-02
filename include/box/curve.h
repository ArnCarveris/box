/**
 * @file box/curve.h
 * @brief Curve: keyed values over time, linear, Catmull-Rom or Bezier, all evaluated as cubics.
 *
 * Every kind converts each span between two keys to one cubic in value whose time is linear in
 * the parameter, so one evaluator serves all; the WGSL twins are in etc/shaders/include/curve.wgsl.
 *
 * Without keys a curve has the default of its field, one key is a constant, and outside the keys
 * the curve holds the first or last. In script: {kind: CurveCatmullRom, keys: [{0, 0.3}, {1, 2}]}
 */

#ifndef BOX_CURVE_H
#define BOX_CURVE_H

#include "types.h"

#ifdef BOX_CURVE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** How the spans between the keys of a curve are shaped, per curve. Not Linear and Bezier:
 * Linear is a TimingFunction constant.
 */
ECS_ENUM(CurveKind, {
    CurveLinear = 0,     /**< Straight: controls at 1/3 and 2/3 of the way, the default */
    CurveCatmullRom = 1, /**< Smooth: slope (v[k + 1] - v[k - 1]) / (t[k + 1] - t[k - 1]) at a
                          *   key; an end key mirrors itself one span away, so even keys get
                          *   c1 = p1 + (p2 - p0) / 6 and c2 = p2 - (p3 - p1) / 6 */
    CurveBezier = 2      /**< Authored: controls a third along in time at the out and in slopes */
});

/** Key of a curve. In script {time, value} or {time, value, in, out}. */
ECS_STRUCT(CurveKey, {
    float time;  /**< Time of the key, strictly after the time of the previous key */
    float value; /**< Value at the time */
    float in;    /**< Slope into the key in value per unit of time, CurveBezier only */
    float out;   /**< Slope out of the key in value per unit of time, CurveBezier only */
});

/** Keyed values over time. Owns its keys: copying a Curve copies them, see BOX_OWNED_CURVE. */
ECS_STRUCT(Curve, {
    CurveKind kind;         /**< Shape of the spans, default CurveLinear */
    ecs_vec(CurveKey) keys; /**< Keys in order of strictly increasing time */
});

/** Owned Curve member of component T, for box_owned_hooks:
 *
 *     box_owned_hooks(world, Look, BOX_OWNED_CURVE(Look, size), BOX_OWNED_CURVE(Look, opacity));
 */
#define BOX_OWNED_CURVE(T, member) BOX_OWNED_VEC(T, member.keys, CurveKey)

/** Evaluate a scalar cubic Bezier with de Casteljau's algorithm.
 *
 * @param start Value at 0
 * @param control1 First control value
 * @param control2 Second control value
 * @param end Value at 1
 * @param t The curve parameter in [0, 1]
 * @return The value at t
 */
float box_curve_cubic(
    float start,
    float control1,
    float control2,
    float end,
    float t);

/** Evaluate a cubic Bezier of 3D points with de Casteljau's algorithm.
 *
 * @param start Point at 0
 * @param control1 First control point
 * @param control2 Second control point
 * @param end Point at 1
 * @param t The curve parameter in [0, 1]
 * @param out Receives the point at t, may alias an input
 */
void box_curve_cubic3(
    const vec3 start,
    const vec3 control1,
    const vec3 control2,
    const vec3 end,
    float t,
    vec3 out);

/** Control values of the cubic of one span of a curve, from key span to key span + 1.
 *
 * @param curve The curve
 * @param span Index of the first key of the span, 0 to the key count - 2
 * @param control1 Receives the first control value
 * @param control2 Receives the second control value
 */
void box_curve_span_controls(
    const Curve *curve,
    int32_t span,
    float *control1,
    float *control2);

/** Value of a curve at a time.
 *
 * @param curve The curve
 * @param time The time
 * @param fallback The value without keys
 * @return The value
 */
float box_curve_value(
    const Curve *curve,
    float time,
    float fallback);

/** Exact area under a curve between two times, in closed form per span.
 *
 * Before the first and after the last key the curve holds its value, so the area grows
 * linearly there. The area is negative when to is before from.
 *
 * @param curve The curve
 * @param from The start time
 * @param to The end time
 * @param fallback The value without keys
 * @return The integral of the curve from from to to
 */
float box_curve_area(
    const Curve *curve,
    float from,
    float to,
    float fallback);

/** Bake a curve to evenly spaced samples, the first at from and the last at to.
 *
 * @param curve The curve
 * @param from Time of the first sample
 * @param to Time of the last sample
 * @param count Number of samples, one sample is at from
 * @param fallback The value without keys
 * @param out Receives count samples
 */
void box_curve_bake(
    const Curve *curve,
    float from,
    float to,
    int32_t count,
    float fallback,
    float *out);

/** Curve parameter where the x of a cubic Bezier from (0, 0) to (1, 1) equals x, for CSS style
 * easing whose handles are free in x. Newton's method, falling back to bisection when Newton
 * leaves the curve or stalls.
 *
 * Keyed curves never need it, their time is linear in the parameter.
 *
 * @param x1 X of the first control point, 0..1
 * @param x2 X of the second control point, 0..1
 * @param x The x to solve for, clamped to 0..1
 * @return The curve parameter
 */
float box_curve_solve(
    float x1,
    float x2,
    float x);

/** Y of a cubic Bezier from (0, 0) to (1, 1) where its x equals x, as a CSS cubic-bezier().
 *
 * @param x1 X of the first control point, 0..1
 * @param y1 Y of the first control point
 * @param x2 X of the second control point, 0..1
 * @param y2 Y of the second control point
 * @param x The x, clamped to 0..1
 * @return The y at x
 */
float box_curve_ease(
    float x1,
    float y1,
    float x2,
    float y2,
    float x);

/** Check a curve: the kind is in range, and the time, value and slopes of every key are
 * finite and the times increase strictly.
 *
 * The message names the key index and the problem, for example
 * "key 2: time 0.5 is not after time 0.5 of key 1". Callers prefix the component and field.
 *
 * @param curve The curve
 * @return NULL when the curve is valid, else the first problem, to free with ecs_os_free
 */
char* box_curve_validate(
    const Curve *curve);

/** Register the box.curve module.
 *
 * @param world The world
 */
void BoxCurveImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
