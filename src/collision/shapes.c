/**
 * @file collision/shapes.c
 * @brief Ray, sphere and capsule tests against a resolved collider.
 *
 * Every test moves the query into the frame of the collider, where a box is an
 * axis aligned box around the origin, a sphere is a ball and a capsule is a
 * segment along Y. The distance between a segment and a box is found by moving
 * a point back and forth between the two convex shapes.
 */

#include "collision_internal.h"

/** Iterations that close in on the distance between a segment and a box */
#define BOX_SHAPE_STEPS (8)

/** Move a world point into the frame of a collider.
 *
 * @param c The collider
 * @param p The point
 * @return The point in the frame of the collider
 */
static vec3 box_shape_to_local(
    const Collider *c,
    vec3 p)
{
    vec3 d = vec3_sub(p, c->center);

    return vec3_new(vec3_dot(d, c->axis_x), vec3_dot(d, c->axis_y),
        vec3_dot(d, c->axis_z));
}

/** Move a world direction into the frame of a collider.
 *
 * @param c The collider
 * @param d The direction
 * @return The direction in the frame of the collider
 */
static vec3 box_shape_to_dir(
    const Collider *c,
    vec3 d)
{
    return vec3_new(vec3_dot(d, c->axis_x), vec3_dot(d, c->axis_y),
        vec3_dot(d, c->axis_z));
}

/** Clamp a point into a box around the origin.
 *
 * @param p The point
 * @param half Half extents of the box
 * @return The nearest point of the box
 */
static vec3 box_shape_clamp(
    vec3 p,
    vec3 half)
{
    vec3 lo = vec3_scale(half, -1.0f);

    return vec3_min(vec3_max(p, lo), half);
}

/** Find the point of a segment nearest to a point.
 *
 * @param a First end of the segment
 * @param b Second end of the segment
 * @param p The point
 * @return The nearest point of the segment
 */
static vec3 box_shape_on_segment(
    vec3 a,
    vec3 b,
    vec3 p)
{
    vec3 ab = vec3_sub(b, a);
    float len = vec3_dot(ab, ab);
    float t = len > 1e-12f ? vec3_dot(vec3_sub(p, a), ab) / len : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    return vec3_add(a, vec3_scale(ab, t));
}

/** Find the distance between a segment and a box around the origin.
 *
 * @param a First end of the segment
 * @param b Second end of the segment
 * @param half Half extents of the box
 * @return The distance, zero when the segment reaches into the box
 */
static float box_shape_segment_box(
    vec3 a,
    vec3 b,
    vec3 half)
{
    vec3 p = box_shape_on_segment(a, b, vec3_new(0, 0, 0));
    int32_t i;
    for (i = 0; i < BOX_SHAPE_STEPS; i ++) {
        vec3 q = box_shape_clamp(p, half);
        p = box_shape_on_segment(a, b, q);
    }

    return vec3_length(vec3_sub(p, box_shape_clamp(p, half)));
}

/** Find the distance between two segments.
 *
 * @param p0 First end of the first segment
 * @param p1 Second end of the first segment
 * @param q0 First end of the second segment
 * @param q1 Second end of the second segment
 * @return The distance
 */
static float box_shape_segment_segment(
    vec3 p0,
    vec3 p1,
    vec3 q0,
    vec3 q1)
{
    vec3 p = box_shape_on_segment(p0, p1, q0);
    vec3 q = q0;
    int32_t i;
    for (i = 0; i < BOX_SHAPE_STEPS; i ++) {
        q = box_shape_on_segment(q0, q1, p);
        p = box_shape_on_segment(p0, p1, q);
    }

    return vec3_length(vec3_sub(p, q));
}

/** Get the segment of a capsule collider in its own frame.
 *
 * @param c The collider
 * @param a Receives the first end
 * @param b Receives the second end
 */
static void box_shape_capsule_segment(
    const Collider *c,
    vec3 *a,
    vec3 *b)
{
    a[0] = vec3_new(0, -c->half.y, 0);
    b[0] = vec3_new(0, c->half.y, 0);
}

/** Intersect a ray with a box around the origin.
 *
 * @param o Origin of the ray
 * @param d Direction of the ray
 * @param half Half extents of the box
 * @param max_dist Longest distance that counts as a hit
 * @param dist_out Receives the distance of the hit
 * @return true when the ray hits the box
 */
static bool box_shape_ray_box(
    vec3 o,
    vec3 d,
    vec3 half,
    float max_dist,
    float *dist_out)
{
    bool result = false;
    float lo = 0.0f;
    float hi = max_dist;
    const float *od = &o.x;
    const float *dd = &d.x;
    const float *hd = &half.x;
    int32_t axis;
    for (axis = 0; axis < 3; axis ++) {
        if (dd[axis] > -1e-8f && dd[axis] < 1e-8f) {
            if (od[axis] < -hd[axis] || od[axis] > hd[axis]) {
                goto done;
            }

            continue;
        }

        float inv = 1.0f / dd[axis];
        float t0 = (-hd[axis] - od[axis]) * inv;
        float t1 = (hd[axis] - od[axis]) * inv;
        if (t0 > t1) {
            float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }

        lo = t0 > lo ? t0 : lo;
        hi = t1 < hi ? t1 : hi;
        if (lo > hi) {
            goto done;
        }
    }

    result = true;
    if (dist_out) {
        dist_out[0] = lo;
    }

done:
    return result;
}

/** Intersect a ray with a ball around the origin.
 *
 * @param o Origin of the ray
 * @param d Direction of the ray
 * @param radius Radius of the ball
 * @param max_dist Longest distance that counts as a hit
 * @param dist_out Receives the distance of the hit
 * @return true when the ray hits the ball
 */
static bool box_shape_ray_sphere(
    vec3 o,
    vec3 d,
    float radius,
    float max_dist,
    float *dist_out)
{
    bool result = false;
    float a = vec3_dot(d, d);
    float b = 2.0f * vec3_dot(o, d);
    float c = vec3_dot(o, o) - radius * radius;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f || a < 1e-12f) {
        goto done;
    }

    float root = sqrtf(disc);
    float t = (-b - root) / (2.0f * a);
    if (t < 0.0f) {
        t = (-b + root) / (2.0f * a);
    }

    if (t < 0.0f || t > max_dist) {
        goto done;
    }

    result = true;
    if (dist_out) {
        dist_out[0] = t;
    }

done:
    return result;
}

/** Intersect a ray with a capsule along the Y axis.
 *
 * @param o Origin of the ray
 * @param d Direction of the ray
 * @param radius Radius of the capsule
 * @param half Half the length of the cylinder part
 * @param max_dist Longest distance that counts as a hit
 * @param dist_out Receives the distance of the hit
 * @return true when the ray hits the capsule
 */
static bool box_shape_ray_capsule(
    vec3 o,
    vec3 d,
    float radius,
    float half,
    float max_dist,
    float *dist_out)
{
    bool result = false;
    float best = max_dist;
    float a = d.x * d.x + d.z * d.z;
    float b = 2.0f * (o.x * d.x + o.z * d.z);
    float c = o.x * o.x + o.z * o.z - radius * radius;
    float disc = b * b - 4.0f * a * c;
    if (a > 1e-12f && disc >= 0.0f) {
        float root = sqrtf(disc);
        float t = (-b - root) / (2.0f * a);
        if (t < 0.0f) {
            t = (-b + root) / (2.0f * a);
        }

        float y = o.y + d.y * t;
        if (t >= 0.0f && t < best && y >= -half && y <= half) {
            best = t;
            result = true;
        }
    }

    int32_t cap;
    for (cap = 0; cap < 2; cap ++) {
        vec3 center = vec3_new(0, cap ? half : -half, 0);
        float t;
        if (box_shape_ray_sphere(vec3_sub(o, center), d, radius, best, &t)) {
            best = t;
            result = true;
        }
    }

    if (result && dist_out) {
        dist_out[0] = best;
    }

    return result;
}

bool box_collider_ray(
    const Collider *c,
    vec3 origin,
    vec3 dir,
    float max_dist,
    float *dist_out)
{
    bool result = false;
    if (c->kind == ColliderNone) {
        goto done;
    }

    vec3 o = box_shape_to_local(c, origin);
    vec3 d = box_shape_to_dir(c, dir);
    if (c->kind == ColliderSphere) {
        result = box_shape_ray_sphere(o, d, c->half.x, max_dist, dist_out);
        goto done;
    }

    if (c->kind == ColliderCapsule) {
        result = box_shape_ray_capsule(o, d, c->half.x, c->half.y, max_dist, dist_out);
        goto done;
    }

    result = box_shape_ray_box(o, d, c->half, max_dist, dist_out);
done:
    return result;
}

bool box_collider_sphere(
    const Collider *c,
    vec3 center,
    float radius)
{
    bool result = false;
    if (c->kind == ColliderNone) {
        goto done;
    }

    vec3 p = box_shape_to_local(c, center);
    if (c->kind == ColliderSphere) {
        result = vec3_length(p) <= radius + c->half.x;
        goto done;
    }

    if (c->kind == ColliderCapsule) {
        vec3 a, b;
        box_shape_capsule_segment(c, &a, &b);
        vec3 q = box_shape_on_segment(a, b, p);
        result = vec3_length(vec3_sub(p, q)) <= radius + c->half.x;
        goto done;
    }

    result = vec3_length(vec3_sub(p, box_shape_clamp(p, c->half))) <= radius;
done:
    return result;
}

bool box_collider_capsule(
    const Collider *c,
    vec3 a,
    vec3 b,
    float radius)
{
    bool result = false;
    if (c->kind == ColliderNone) {
        goto done;
    }

    vec3 p = box_shape_to_local(c, a);
    vec3 q = box_shape_to_local(c, b);
    if (c->kind == ColliderSphere) {
        vec3 near = box_shape_on_segment(p, q, vec3_new(0, 0, 0));
        result = vec3_length(near) <= radius + c->half.x;
        goto done;
    }

    if (c->kind == ColliderCapsule) {
        vec3 s0, s1;
        box_shape_capsule_segment(c, &s0, &s1);
        result = box_shape_segment_segment(p, q, s0, s1) <= radius + c->half.x;
        goto done;
    }

    result = box_shape_segment_box(p, q, c->half) <= radius;
done:
    return result;
}

void box_collider_bounds(
    const Collider *c,
    vec3 *min,
    vec3 *max)
{
    vec3 e = c->half;
    if (c->kind == ColliderSphere) {
        e = vec3_new(c->half.x, c->half.x, c->half.x);
    } else if (c->kind == ColliderCapsule) {
        e = vec3_new(c->half.x, c->half.y + c->half.x, c->half.x);
    }

    vec3 span = e;
    if (c->kind == ColliderBox || c->kind == ColliderCapsule) {
        vec3 x = vec3_scale(c->axis_x, e.x);
        vec3 y = vec3_scale(c->axis_y, e.y);
        vec3 z = vec3_scale(c->axis_z, e.z);
        span = vec3_new(fabsf(x.x) + fabsf(y.x) + fabsf(z.x),
            fabsf(x.y) + fabsf(y.y) + fabsf(z.y),
            fabsf(x.z) + fabsf(y.z) + fabsf(z.z));
    }

    min[0] = vec3_sub(c->center, span);
    max[0] = vec3_add(c->center, span);
}
