/**
 * @file controllers/collide.c
 * @brief Narrow phase of controller collision: a capsule and a ray against one shape.
 *
 * A query moves into the frame of the shape, where it is centered and axis aligned. The distance
 * between the capsule segment and a convex shape closes in by projecting onto each in turn.
 */

#include "controllers.h"
#include "../geometry/mesh_cache.h"
#include <math.h>

/** Iterations that close in on the nearest points of a segment and a shape */
#define BOX_COLLIDE_STEPS (8)

/** Relative difference below which two extents count as equal */
#define BOX_COLLIDE_ROUND (0.02f)

/** Distance below which the nearest points of a segment and a shape coincide */
#define BOX_COLLIDE_TOUCH (1e-5f)

/** Test whether two extents are equal within the tolerance of a round shape.
 *
 * @param a The first extent
 * @param b The second extent
 * @return true when they are equal enough
 */
static bool box_collide_same(
    float a,
    float b)
{
    float big = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);

    return fabsf(a - b) <= big * BOX_COLLIDE_ROUND;
}

/** Pick the shape of a collider from the shape component of its entity.
 *
 * @param world The world
 * @param e The entity
 * @param half Half extents along the axes, the round extents are averaged
 * @return The shape
 */
static box_collider_kind_t box_collide_kind(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec3 half)
{
    box_collider_kind_t result = BoxColliderBox;
    bool flat_round = box_collide_same(half[0], half[2]);
    if (ecs_has(world, e, Sphere) || ecs_has(world, e, IcoSphere)) {
        if (flat_round && box_collide_same(half[0], half[1])) {
            result = BoxColliderSphere;
            half[0] = half[1] = half[2] = (half[0] + half[1] + half[2]) / 3.0f;
        }
    } else if (flat_round && ecs_has(world, e, Cylinder)) {
        result = BoxColliderCylinder;
    } else if (flat_round && ecs_has(world, e, Capsule)) {
        result = BoxColliderCapsule;
    }

    if (result != BoxColliderBox && result != BoxColliderSphere) {
        half[0] = half[2] = (half[0] + half[2]) * 0.5f;
    }

    return result;
}

bool box_collider_resolve(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_collider_t *out)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    const box_mesh_t *mesh = cache && shape && shape->mesh
        ? box_mesh_cache_get(cache, shape->mesh)
        : NULL;
    bool result = mesh && mesh->vertex_count;
    vec3 c, e3;
    int32_t i, j;
    if (!result) {
        goto done;
    }

    out->entity = e;
    for (i = 0; i < 3; i ++) {
        c[i] = (mesh->min[i] + mesh->max[i]) * 0.5f * shape->scale[i];
        e3[i] = (mesh->max[i] - mesh->min[i]) * 0.5f * fabsf(shape->scale[i]);
    }

    for (j = 0; j < 3; j ++) {
        float len = 0;
        for (i = 0; i < 3; i ++) {
            out->axis[j][i] = t ? t->m[j][i] : (float)(i == j);
            len += out->axis[j][i] * out->axis[j][i];
        }

        len = sqrtf(len);
        for (i = 0; i < 3; i ++) {
            out->axis[j][i] = len > 0 ? out->axis[j][i] / len : (float)(i == j);
        }

        out->half[j] = e3[j] * len;
    }

    for (i = 0; i < 3; i ++) {
        out->center[i] = t ? t->m[3][i] : c[i];
        for (j = 0; t && j < 3; j ++) {
            out->center[i] += t->m[j][i] * c[j];
        }
    }

    out->kind = box_collide_kind(world, e, out->half);

done:
    return result;
}

/** Move a world point into the frame of a collider.
 *
 * @param c The collider
 * @param p The point
 * @param out The point in the frame of the collider
 */
static void box_collide_local(
    const box_collider_t *c,
    const vec3 p,
    vec3 out)
{
    vec3 d = { p[0] - c->center[0], p[1] - c->center[1], p[2] - c->center[2] };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = d[0] * c->axis[i][0] + d[1] * c->axis[i][1] + d[2] * c->axis[i][2];
    }
}

/** Move a world direction into the frame of a collider.
 *
 * @param c The collider
 * @param d The direction
 * @param out The direction in the frame of the collider
 */
static void box_collide_local_dir(
    const box_collider_t *c,
    const vec3 d,
    vec3 out)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = d[0] * c->axis[i][0] + d[1] * c->axis[i][1] + d[2] * c->axis[i][2];
    }
}

/** Find the point of a segment nearest to a point.
 *
 * @param a First end of the segment
 * @param b Second end of the segment
 * @param p The point
 * @param out The nearest point of the segment
 */
static void box_collide_on_segment(
    const vec3 a,
    const vec3 b,
    const vec3 p,
    vec3 out)
{
    vec3 ab = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    float len = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = 0;
    int32_t i;
    if (len > 1e-12f) {
        t = ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1] + (p[2] - a[2]) * ab[2]) / len;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
    }

    for (i = 0; i < 3; i ++) {
        out[i] = a[i] + ab[i] * t;
    }
}

/** Find the point of a collider nearest to a point, both in the frame of the collider.
 *
 * Balls and capsules answer with the nearest point of their core, a point or a segment.
 *
 * @param c The collider
 * @param p The point
 * @param out The nearest point
 */
static void box_collide_clamp(
    const box_collider_t *c,
    const vec3 p,
    vec3 out)
{
    const float *h = c->half;
    int32_t i;
    if (c->kind == BoxColliderSphere) {
        out[0] = out[1] = out[2] = 0;
    } else if (c->kind == BoxColliderCapsule) {
        float core = h[1] - h[0] > 0 ? h[1] - h[0] : 0;
        out[0] = out[2] = 0;
        out[1] = p[1] < -core ? -core : (p[1] > core ? core : p[1]);
    } else if (c->kind == BoxColliderCylinder) {
        float r = sqrtf(p[0] * p[0] + p[2] * p[2]);
        float s = r > h[0] ? h[0] / r : 1.0f;
        out[0] = p[0] * s;
        out[2] = p[2] * s;
        out[1] = p[1] < -h[1] ? -h[1] : (p[1] > h[1] ? h[1] : p[1]);
    } else {
        for (i = 0; i < 3; i ++) {
            out[i] = p[i] < -h[i] ? -h[i] : (p[i] > h[i] ? h[i] : p[i]);
        }
    }
}

/** Find the shortest way out of a collider for a point inside its core.
 *
 * @param c The collider
 * @param p The point in the frame of the collider
 * @param n Receives the way out in the frame of the collider
 * @return Distance from the point to the surface along the way out
 */
static float box_collide_inside(
    const box_collider_t *c,
    const vec3 p,
    vec3 n)
{
    float result = 0;
    int32_t i, best = 1;
    n[0] = n[2] = 0;
    n[1] = 1;
    if (c->kind == BoxColliderCylinder) {
        float r = sqrtf(p[0] * p[0] + p[2] * p[2]);
        float side = c->half[0] - r;
        result = c->half[1] - fabsf(p[1]);
        n[1] = p[1] < 0 ? -1.0f : 1.0f;
        if (side < result) {
            result = side;
            n[0] = r > 1e-6f ? p[0] / r : 1.0f;
            n[1] = 0;
            n[2] = r > 1e-6f ? p[2] / r : 0;
        }
    } else if (c->kind == BoxColliderBox) {
        result = c->half[1] - fabsf(p[1]);
        for (i = 0; i < 3; i ++) {
            float gap = c->half[i] - fabsf(p[i]);
            if (gap < result) {
                result = gap;
                best = i;
            }
        }

        n[1] = 0;
        n[best] = p[best] < 0 ? -1.0f : 1.0f;
    }

    return result;
}

bool box_collider_push(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    vec3 normal,
    float *depth)
{
    vec3 p0, p1, p, q, d, n;
    vec3 zero = { 0, 0, 0 };
    float reach = radius, dist, gap = 0;
    bool result = false;
    int32_t i;
    box_collide_local(c, a, p0);
    box_collide_local(c, b, p1);
    box_collide_on_segment(p0, p1, zero, p);
    box_collide_clamp(c, p, q);
    for (i = 0; i < BOX_COLLIDE_STEPS && c->kind != BoxColliderSphere; i ++) {
        box_collide_on_segment(p0, p1, q, p);
        box_collide_clamp(c, p, q);
    }

    if (c->kind == BoxColliderSphere || c->kind == BoxColliderCapsule) {
        reach += c->half[0];
    }

    for (i = 0; i < 3; i ++) {
        d[i] = p[i] - q[i];
    }

    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist >= reach) {
        goto done;
    }

    if (dist > BOX_COLLIDE_TOUCH) {
        for (i = 0; i < 3; i ++) {
            n[i] = d[i] / dist;
        }
    } else {
        gap = box_collide_inside(c, p, n);
    }

    for (i = 0; i < 3; i ++) {
        normal[i] = c->axis[0][i] * n[0] + c->axis[1][i] * n[1] + c->axis[2][i] * n[2];
    }

    *depth = reach - dist + gap;
    result = true;

done:
    return result;
}

/** Intersect a ray with a ball around the origin.
 *
 * @param o Origin of the ray
 * @param d Direction of the ray
 * @param radius Radius of the ball
 * @param max Longest distance that counts
 * @param t Receives the distance of the hit
 * @return true when the ray hits the ball
 */
static bool box_collide_ray_ball(
    const vec3 o,
    const vec3 d,
    float radius,
    float max,
    float *t)
{
    float a = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    float b = o[0] * d[0] + o[1] * d[1] + o[2] * d[2];
    float c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - radius * radius;
    float disc = b * b - a * c;
    float hit = 0;
    bool result = false;
    if (a > 1e-12f && disc >= 0) {
        hit = (-b - sqrtf(disc)) / a;
        hit = hit < 0 && c <= 0 ? 0 : hit;
        result = hit >= 0 && hit <= max;
    }

    if (result) {
        *t = hit;
    }

    return result;
}

bool box_collider_ray(
    const box_collider_t *c,
    const vec3 origin,
    const vec3 dir,
    float max,
    float *t)
{
    vec3 o, d;
    float lo = 0, hi = max;
    bool result = true;
    int32_t i;
    box_collide_local(c, origin, o);
    box_collide_local_dir(c, dir, d);
    if (c->kind == BoxColliderSphere) {
        result = box_collide_ray_ball(o, d, c->half[0], max, t);
        goto done;
    }

    for (i = 0; i < 3 && result; i ++) {
        if (fabsf(d[i]) < 1e-12f) {
            result = o[i] >= -c->half[i] && o[i] <= c->half[i];
        } else {
            float t0 = (-c->half[i] - o[i]) / d[i];
            float t1 = (c->half[i] - o[i]) / d[i];
            lo = fmaxf(lo, fminf(t0, t1));
            hi = fminf(hi, fmaxf(t0, t1));
            result = lo <= hi;
        }
    }

    if (result) {
        *t = lo;
    }

done:
    return result;
}
