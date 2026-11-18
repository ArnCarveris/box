/**
 * @file controllers/triangle.c
 * @brief Narrow phase of a capsule against one triangle of a TriangleCollider mesh.
 *
 * The capsule is a segment with a radius, so the contact is the closest pair of points of the
 * segment and the triangle (Ericson, Real-Time Collision Detection 5.1.5 and 5.1.9).
 *
 * A segment that runs through the triangle is pushed out to the side its middle lies on.
 */

#include "controllers.h"
#include <math.h>

/** Squared length below which two points count as the same */
#define BOX_TRIANGLE_EPSILON (1e-12f)

/** Point at a fraction along the edge from a corner to another.
 *
 * @param from The first corner
 * @param edge The edge from the first corner to the second
 * @param t The fraction
 * @param out Receives the point
 */
static void box_triangle_along(
    const vec3 from,
    const vec3 edge,
    float t,
    vec3 out)
{
    glm_vec3_copy(ECS_CONST_CAST(float*, from), out);
    glm_vec3_muladds(ECS_CONST_CAST(float*, edge), t, out);
}

/** Find the point of a triangle closest to a point, by the Voronoi region it lies in.
 *
 * @param p The point
 * @param a First corner
 * @param b Second corner
 * @param c Third corner
 * @param out Receives the closest point
 */
static void box_triangle_closest(
    const vec3 p,
    const vec3 a,
    const vec3 b,
    const vec3 c,
    vec3 out)
{
    vec3 ab, ac, bc, ap, bp, cp;
    float d1, d2, d3, d4, d5, d6, va, vb, vc;
    glm_vec3_sub(ECS_CONST_CAST(float*, b), ECS_CONST_CAST(float*, a), ab);
    glm_vec3_sub(ECS_CONST_CAST(float*, c), ECS_CONST_CAST(float*, a), ac);
    glm_vec3_sub(ECS_CONST_CAST(float*, c), ECS_CONST_CAST(float*, b), bc);
    glm_vec3_sub(ECS_CONST_CAST(float*, p), ECS_CONST_CAST(float*, a), ap);
    glm_vec3_sub(ECS_CONST_CAST(float*, p), ECS_CONST_CAST(float*, b), bp);
    glm_vec3_sub(ECS_CONST_CAST(float*, p), ECS_CONST_CAST(float*, c), cp);
    d1 = glm_vec3_dot(ab, ap);
    d2 = glm_vec3_dot(ac, ap);
    d3 = glm_vec3_dot(ab, bp);
    d4 = glm_vec3_dot(ac, bp);
    d5 = glm_vec3_dot(ab, cp);
    d6 = glm_vec3_dot(ac, cp);
    vc = d1 * d4 - d3 * d2;
    vb = d5 * d2 - d1 * d6;
    va = d3 * d6 - d5 * d4;
    if (d1 <= 0 && d2 <= 0) {
        glm_vec3_copy(ECS_CONST_CAST(float*, a), out);
    } else if (d3 >= 0 && d4 <= d3) {
        glm_vec3_copy(ECS_CONST_CAST(float*, b), out);
    } else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        box_triangle_along(a, ab, d1 / (d1 - d3), out);
    } else if (d6 >= 0 && d5 <= d6) {
        glm_vec3_copy(ECS_CONST_CAST(float*, c), out);
    } else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        box_triangle_along(a, ac, d2 / (d2 - d6), out);
    } else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        box_triangle_along(b, bc, (d4 - d3) / ((d4 - d3) + (d5 - d6)), out);
    } else {
        float denom = 1.0f / (va + vb + vc);
        box_triangle_along(a, ab, vb * denom, out);
        glm_vec3_muladds(ac, vc * denom, out);
    }
}

/** Find the closest points of two segments.
 *
 * @param p1 Start of the first segment
 * @param q1 End of the first segment
 * @param p2 Start of the second segment
 * @param q2 End of the second segment
 * @param c1 Receives the point on the first segment
 * @param c2 Receives the point on the second segment
 */
static void box_triangle_segments(
    const vec3 p1,
    const vec3 q1,
    const vec3 p2,
    const vec3 q2,
    vec3 c1,
    vec3 c2)
{
    vec3 d1, d2, r;
    float a, e, f, s = 0, t = 0;
    glm_vec3_sub(ECS_CONST_CAST(float*, q1), ECS_CONST_CAST(float*, p1), d1);
    glm_vec3_sub(ECS_CONST_CAST(float*, q2), ECS_CONST_CAST(float*, p2), d2);
    glm_vec3_sub(ECS_CONST_CAST(float*, p1), ECS_CONST_CAST(float*, p2), r);
    a = glm_vec3_dot(d1, d1);
    e = glm_vec3_dot(d2, d2);
    f = glm_vec3_dot(d2, r);
    if (a <= BOX_TRIANGLE_EPSILON && e <= BOX_TRIANGLE_EPSILON) {
        s = 0;
        t = 0;
    } else if (a <= BOX_TRIANGLE_EPSILON) {
        s = 0;
        t = glm_clamp(f / e, 0, 1);
    } else {
        float c = glm_vec3_dot(d1, r);
        if (e <= BOX_TRIANGLE_EPSILON) {
            t = 0;
            s = glm_clamp(-c / a, 0, 1);
        } else {
            float b = glm_vec3_dot(d1, d2);
            float denom = a * e - b * b;
            s = denom != 0 ? glm_clamp((b * f - c * e) / denom, 0, 1) : 0;
            t = (b * s + f) / e;
            if (t < 0) {
                t = 0;
                s = glm_clamp(-c / a, 0, 1);
            } else if (t > 1) {
                t = 1;
                s = glm_clamp((b - c) / a, 0, 1);
            }
        }
    }

    box_triangle_along(p1, d1, s, c1);
    box_triangle_along(p2, d2, t, c2);
}

/** Get the unit normal of a triangle, by the winding of its corners.
 *
 * @param c The triangle collider
 * @param out Receives the normal, zero for a triangle without area
 */
static void box_triangle_normal(
    const box_collider_t *c,
    vec3 out)
{
    vec3 ab, ac;
    glm_vec3_sub(ECS_CONST_CAST(float*, c->corner[1]), ECS_CONST_CAST(float*, c->corner[0]), ab);
    glm_vec3_sub(ECS_CONST_CAST(float*, c->corner[2]), ECS_CONST_CAST(float*, c->corner[0]), ac);
    glm_vec3_cross(ab, ac, out);
    glm_vec3_normalize(out);
}

/** Get the height of a point above the plane of a triangle.
 *
 * @param c The triangle collider
 * @param n The unit normal of the triangle
 * @param p The point
 * @return The signed distance along the normal
 */
static float box_triangle_height(
    const box_collider_t *c,
    const vec3 n,
    const vec3 p)
{
    vec3 rel;
    glm_vec3_sub(ECS_CONST_CAST(float*, p), ECS_CONST_CAST(float*, c->corner[0]), rel);

    return glm_vec3_dot(rel, ECS_CONST_CAST(float*, n));
}

/** Keep a pair of closest points when it is closer than the best so far.
 *
 * @param s The point on the segment
 * @param t The point on the triangle
 * @param best Squared distance of the best pair, written
 * @param on_s The best point on the segment, written
 * @param on_t The best point on the triangle, written
 */
static void box_triangle_keep(
    const vec3 s,
    const vec3 t,
    float *best,
    vec3 on_s,
    vec3 on_t)
{
    float d = glm_vec3_distance2(ECS_CONST_CAST(float*, s), ECS_CONST_CAST(float*, t));
    if (d < *best) {
        *best = d;
        glm_vec3_copy(ECS_CONST_CAST(float*, s), on_s);
        glm_vec3_copy(ECS_CONST_CAST(float*, t), on_t);
    }
}

/** Test whether a segment runs through the inside of a triangle.
 *
 * @param c The triangle collider
 * @param n The unit normal of the triangle
 * @param a Start of the segment
 * @param b End of the segment
 * @return true when the segment crosses the inside of the triangle
 */
static bool box_triangle_crossing(
    const box_collider_t *c,
    const vec3 n,
    const vec3 a,
    const vec3 b)
{
    vec3 d, x, closest;
    float sa = box_triangle_height(c, n, a);
    float sb = box_triangle_height(c, n, b);
    bool result = false;
    if (sa * sb <= 0 && sa != sb) {
        glm_vec3_sub(ECS_CONST_CAST(float*, b), ECS_CONST_CAST(float*, a), d);
        box_triangle_along(a, d, sa / (sa - sb), x);
        box_triangle_closest(x, c->corner[0], c->corner[1], c->corner[2], closest);
        result = glm_vec3_distance2(x, closest) <= 1e-8f;
    }

    return result;
}

/** Find the contact of a segment that runs through a triangle.
 *
 * @param c The triangle collider
 * @param n The unit normal of the triangle
 * @param a Start of the segment
 * @param b End of the segment
 * @param normal Receives the normal of the triangle on the side of the middle of the segment
 * @return How far the end on the other side lies behind the triangle, negative
 */
static float box_triangle_through(
    const box_collider_t *c,
    const vec3 n,
    const vec3 a,
    const vec3 b,
    vec3 normal)
{
    vec3 mid;
    float sa, sb;
    glm_vec3_center(ECS_CONST_CAST(float*, a), ECS_CONST_CAST(float*, b), mid);
    glm_vec3_copy(ECS_CONST_CAST(float*, n), normal);
    if (box_triangle_height(c, normal, mid) < 0) {
        glm_vec3_negate(normal);
    }

    sa = box_triangle_height(c, normal, a);
    sb = box_triangle_height(c, normal, b);

    return fminf(fminf(sa, sb), 0);
}

/** Find the contact of a segment apart from a triangle, by the closest pair of points.
 *
 * @param c The triangle collider
 * @param n The unit normal of the triangle
 * @param a Start of the segment
 * @param b End of the segment
 * @param normal Receives the direction from the triangle towards the segment
 * @return Distance of the segment from the triangle
 */
static float box_triangle_apart(
    const box_collider_t *c,
    const vec3 n,
    const vec3 a,
    const vec3 b,
    vec3 normal)
{
    vec3 on_s, on_t, s, t;
    float best = FLT_MAX, result = 0;
    int32_t i;
    box_triangle_closest(a, c->corner[0], c->corner[1], c->corner[2], t);
    box_triangle_keep(a, t, &best, on_s, on_t);
    box_triangle_closest(b, c->corner[0], c->corner[1], c->corner[2], t);
    box_triangle_keep(b, t, &best, on_s, on_t);
    for (i = 0; i < 3; i ++) {
        box_triangle_segments(a, b, c->corner[i], c->corner[(i + 1) % 3], s, t);
        box_triangle_keep(s, t, &best, on_s, on_t);
    }

    result = sqrtf(best);
    glm_vec3_sub(on_s, on_t, normal);
    if (result > 1e-6f) {
        glm_vec3_scale(normal, 1.0f / result, normal);
    } else {
        glm_vec3_copy(ECS_CONST_CAST(float*, n), normal);
        if (box_triangle_height(c, n, on_s) < 0) {
            glm_vec3_negate(normal);
        }
    }

    return result;
}

/** Find the contact of a segment and a triangle.
 *
 * A segment apart from the triangle gets the direction from its closest point on the triangle.
 * A segment through it gets the normal of the triangle on the side of its middle, and a
 * negative distance: how far its end on the other side lies behind the triangle.
 *
 * @param c The triangle collider
 * @param a Start of the segment
 * @param b End of the segment
 * @param normal Receives the direction from the triangle towards the segment
 * @return Distance of the segment from the triangle, negative when it runs through it
 */
static float box_triangle_contact(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    vec3 normal)
{
    vec3 n;
    float result = 0;
    box_triangle_normal(c, n);
    if (box_triangle_crossing(c, n, a, b)) {
        result = box_triangle_through(c, n, a, b, normal);
    } else {
        result = box_triangle_apart(c, n, a, b, normal);
    }

    return result;
}

void box_triangle_collider(
    ecs_entity_t e,
    const vec3 v[3],
    box_collider_t *out)
{
    vec3 min, max;
    int32_t i;
    glm_vec3_copy(ECS_CONST_CAST(float*, v[0]), min);
    glm_vec3_copy(ECS_CONST_CAST(float*, v[0]), max);
    for (i = 1; i < 3; i ++) {
        glm_vec3_minv(min, ECS_CONST_CAST(float*, v[i]), min);
        glm_vec3_maxv(max, ECS_CONST_CAST(float*, v[i]), max);
    }

    *out = (box_collider_t){
        .entity = e,
        .kind = BoxColliderTriangle,
        .axis = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }
    };

    for (i = 0; i < 3; i ++) {
        out->center[i] = (min[i] + max[i]) * 0.5f;
        out->half[i] = (max[i] - min[i]) * 0.5f;
        glm_vec3_copy(ECS_CONST_CAST(float*, v[i]), out->corner[i]);
    }
}

float box_triangle_separation(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    vec3 normal)
{
    return box_triangle_contact(c, a, b, normal) - radius;
}

bool box_triangle_push(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    bool surface,
    box_collider_contact_t *out)
{
    vec3 normal, face;
    float distance = box_triangle_contact(c, a, b, normal);
    bool result = distance < radius;
    if (result) {
        box_triangle_normal(c, face);
        if (glm_vec3_dot(face, normal) < 0) {
            glm_vec3_negate(face);
        }

        glm_vec3_copy(normal, out->normal);
        glm_vec3_copy(normal, out->surface);
        out->depth = radius - distance;
        out->round = false;
        if (surface && face[1] >= BOX_CONTROLLER_SUPPORT) {
            glm_vec3_copy(face, out->surface);
        }
    }

    return result;
}

bool box_triangle_ray(
    const box_collider_t *c,
    const vec3 origin,
    const vec3 dir,
    float max,
    float *t,
    vec3 normal)
{
    vec3 e1, e2, p, q, s;
    float det, inv, u = -1, v = -1, hit = -1;
    bool result = false;
    glm_vec3_sub(ECS_CONST_CAST(float*, c->corner[1]), ECS_CONST_CAST(float*, c->corner[0]), e1);
    glm_vec3_sub(ECS_CONST_CAST(float*, c->corner[2]), ECS_CONST_CAST(float*, c->corner[0]), e2);
    glm_vec3_cross(ECS_CONST_CAST(float*, dir), e2, p);
    det = glm_vec3_dot(e1, p);
    if (fabsf(det) >= 1e-12f) {
        inv = 1.0f / det;
        glm_vec3_sub(ECS_CONST_CAST(float*, origin), ECS_CONST_CAST(float*, c->corner[0]), s);
        glm_vec3_cross(s, e1, q);
        u = glm_vec3_dot(s, p) * inv;
        v = glm_vec3_dot(ECS_CONST_CAST(float*, dir), q) * inv;
        hit = glm_vec3_dot(e2, q) * inv;
    }

    result = u >= 0 && v >= 0 && u + v <= 1 && hit >= 0 && hit <= max;
    if (result) {
        *t = hit;
    }

    if (result && normal) {
        box_triangle_normal(c, normal);
        if (glm_vec3_dot(normal, ECS_CONST_CAST(float*, dir)) > 0) {
            glm_vec3_negate(normal);
        }
    }

    return result;
}
