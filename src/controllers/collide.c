/**
 * @file controllers/collide.c
 * @brief Narrow phase of controller collision: a capsule and a ray against one shape.
 *
 * A query moves into the frame of the shape, where it is centered and axis aligned. The distance
 * between the capsule segment and a convex shape closes in by projecting onto each in turn.
 */

#include "controllers.h"
#include "../geometry/mesh_cache.h"
#include "../terrain/terrain.h"
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

/** Remove from a vector its part along a unit axis and normalize what is left.
 *
 * @param v The vector, written in place
 * @param axis The unit axis, NULL for none
 * @return false when nothing is left
 */
static bool box_collide_orthogonal(
    vec3 v,
    const vec3 axis)
{
    float len;
    if (axis) {
        glm_vec3_muladds(ECS_CONST_CAST(float*, axis), -glm_vec3_dot(v, ECS_CONST_CAST(float*,
            axis)), v);
    }

    len = glm_vec3_norm(v);
    if (len > 1e-6f) {
        glm_vec3_scale(v, 1.0f / len, v);
    }

    return len > 1e-6f;
}

/** Fit the orthonormal frame of a collider around its transformed extents.
 *
 * A transform with non-uniform scale above a rotation shears the shape. The frame keeps the
 * direction of the second axis, the axis of round shapes, makes the first axis perpendicular
 * to it, and the half extents bound the sheared box along the new axes.
 *
 * The frame contains the sheared shape but adds empty space near the faces it does not line up
 * with: a body stops short of those faces, by up to 0.76 for Scale {2, 1, 1} over 45 degrees.
 *
 * @param t The world transform, NULL for none
 * @param e3 Half extents of the shape in its own frame
 * @param out The collider, its axes and half extents are written
 */
static void box_collide_frame(
    const WorldTransform *t,
    const vec3 e3,
    box_collider_t *out)
{
    vec3 h[3];
    int32_t i, j;
    for (j = 0; j < 3; j ++) {
        for (i = 0; i < 3; i ++) {
            out->axis[j][i] = t ? t->m[j][i] : (float)(i == j);
            h[j][i] = out->axis[j][i] * e3[j];
        }
    }

    if (!box_collide_orthogonal(out->axis[1], NULL)) {
        glm_vec3_copy((vec3){ 0, 1, 0 }, out->axis[1]);
    }

    if (!box_collide_orthogonal(out->axis[0], out->axis[1])) {
        glm_vec3_copy((vec3){ 1, 0, 0 }, out->axis[0]);
        if (!box_collide_orthogonal(out->axis[0], out->axis[1])) {
            glm_vec3_copy((vec3){ 0, 0, 1 }, out->axis[0]);
            box_collide_orthogonal(out->axis[0], out->axis[1]);
        }
    }

    glm_vec3_cross(out->axis[0], out->axis[1], out->axis[2]);
    for (j = 0; j < 3; j ++) {
        out->half[j] = 0;
        for (i = 0; i < 3; i ++) {
            out->half[j] += fabsf(glm_vec3_dot(h[i], out->axis[j]));
        }
    }
}

bool box_collider_resolve(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_collider_t *out)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    WorldTransform t_buf;
    const WorldTransform *t = box_transform_world(world, e, &t_buf);
    const box_mesh_t *mesh = cache && shape && shape->mesh
        ? box_mesh_cache_get(cache, shape->mesh)
        : NULL;
    bool result = mesh && mesh->vertex_count && !box_terrain_is_tile(world, e);
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

    box_collide_frame(t, e3, out);
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

/** Move a direction in the frame of a collider into world space.
 *
 * @param c The collider
 * @param d The direction in the frame of the collider
 * @param out The direction in world space
 */
static void box_collide_world_dir(
    const box_collider_t *c,
    const vec3 d,
    vec3 out)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = c->axis[0][i] * d[0] + c->axis[1][i] * d[1] + c->axis[2][i] * d[2];
    }
}

/** Pick the way out for a capsule segment that runs through the core of a ball or capsule.
 *
 * The nearest points coincide, so their difference has no direction. The way out is
 * perpendicular to both segments (to the capsule segment for a ball), on the side of a hint.
 *
 * @param c The collider
 * @param p0 First end of the capsule segment in the frame of the collider
 * @param p1 Second end of the capsule segment in the frame of the collider
 * @param hint Preferred direction in the frame of the collider, may be zero
 * @param n Receives the way out in the frame of the collider
 */
static void box_collide_coincident(
    const box_collider_t *c,
    const vec3 p0,
    const vec3 p1,
    const vec3 hint,
    vec3 n)
{
    vec3 axis = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    vec3 core = { 0, 1, 0 };
    bool segment = box_collide_orthogonal(axis, NULL);
    const float *along = segment ? axis : (c->kind == BoxColliderCapsule ? core : NULL);
    glm_vec3_cross(axis, core, n);
    if (c->kind == BoxColliderCapsule && segment && box_collide_orthogonal(n, NULL)) {
        glm_vec3_scale(n, glm_vec3_dot(n, ECS_CONST_CAST(float*, hint)) < 0 ? -1.0f : 1.0f, n);
        goto done;
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, hint), n);
    if (box_collide_orthogonal(n, along)) {
        goto done;
    }

    glm_vec3_copy((vec3){ 1, 0, 0 }, n);
    if (!box_collide_orthogonal(n, along)) {
        glm_vec3_copy((vec3){ 0, 0, 1 }, n);
        box_collide_orthogonal(n, along);
    }

done:
    return;
}

/** Find the flat face of a box or the cap of a cylinder a point of its surface lies on.
 *
 * @param c The collider
 * @param q The point in the frame of the collider
 * @param n Receives the normal of the face in the frame of the collider
 * @return false when the point lies on an edge, a corner, a round surface or inside
 */
static bool box_collide_face(
    const box_collider_t *c,
    const vec3 q,
    vec3 n)
{
    const float *h = c->half;
    int32_t i, count = 0;
    glm_vec3_zero(n);
    if (c->kind == BoxColliderBox) {
        for (i = 0; i < 3; i ++) {
            if (fabsf(q[i]) >= h[i] * (1.0f - BOX_COLLIDE_TOUCH)) {
                n[i] = q[i] < 0 ? -1.0f : 1.0f;
                count ++;
            }
        }
    } else if (c->kind == BoxColliderCylinder) {
        float r = sqrtf(q[0] * q[0] + q[2] * q[2]);
        if (fabsf(q[1]) >= h[1] * (1.0f - BOX_COLLIDE_TOUCH) &&
            r < h[0] * (1.0f - BOX_COLLIDE_TOUCH))
        {
            n[1] = q[1] < 0 ? -1.0f : 1.0f;
            count = 1;
        }
    }

    return count == 1;
}

/** Test whether a point of the surface of a collider lies on an edge between flat faces.
 *
 * @param c The collider
 * @param q The point in the frame of the collider
 * @return true for an edge or corner of a box or the rim of a cylinder
 */
static bool box_collide_edge(
    const box_collider_t *c,
    const vec3 q)
{
    const float *h = c->half;
    float r = sqrtf(q[0] * q[0] + q[2] * q[2]);
    bool result = c->kind == BoxColliderBox;
    if (c->kind == BoxColliderCylinder) {
        result = fabsf(q[1]) >= h[1] * (1.0f - BOX_COLLIDE_TOUCH) &&
            r >= h[0] * (1.0f - BOX_COLLIDE_TOUCH);
    }

    return result;
}

/** Find the nearest points of two segments in closed form (Ericson, Real-Time Collision
 * Detection, 5.1.9).
 *
 * @param p1 First end of the first segment
 * @param q1 Second end of the first segment
 * @param p2 First end of the second segment
 * @param q2 Second end of the second segment
 * @param c1 Receives the nearest point of the first segment
 * @param c2 Receives the nearest point of the second segment
 */
static void box_collide_segments(
    const vec3 p1,
    const vec3 q1,
    const vec3 p2,
    const vec3 q2,
    vec3 c1,
    vec3 c2)
{
    vec3 d1 = { q1[0] - p1[0], q1[1] - p1[1], q1[2] - p1[2] };
    vec3 d2 = { q2[0] - p2[0], q2[1] - p2[1], q2[2] - p2[2] };
    vec3 r = { p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2] };
    float a = glm_vec3_dot(d1, d1), e = glm_vec3_dot(d2, d2), f = glm_vec3_dot(d2, r);
    float s = 0, t = 0;
    if (a <= 1e-12f && e > 1e-12f) {
        t = glm_clamp(f / e, 0, 1);
    } else if (a > 1e-12f && e <= 1e-12f) {
        s = glm_clamp(-glm_vec3_dot(d1, r) / a, 0, 1);
    } else if (a > 1e-12f) {
        float c = glm_vec3_dot(d1, r), b = glm_vec3_dot(d1, d2);
        float denom = a * e - b * b;
        s = denom > 1e-12f ? glm_clamp((b * f - c * e) / denom, 0, 1) : 0;
        t = (b * s + f) / e;
        if (t < 0) {
            t = 0;
            s = glm_clamp(-c / a, 0, 1);
        } else if (t > 1) {
            t = 1;
            s = glm_clamp((b - c) / a, 0, 1);
        }
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, p1), c1);
    glm_vec3_muladds(d1, s, c1);
    glm_vec3_copy(ECS_CONST_CAST(float*, p2), c2);
    glm_vec3_muladds(d2, t, c2);
}

/** Find the nearest points of a capsule segment and a collider.
 *
 * @param c The collider
 * @param a Center of the lower cap of the capsule
 * @param b Center of the upper cap of the capsule
 * @param radius Radius of the capsule
 * @param p_out Receives the nearest point of the segment, in the frame of the collider
 * @param q_out Receives the nearest point of the collider or its core, in its frame
 * @return The gap between their surfaces, negative when they overlap
 */
static float box_collide_nearest(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    vec3 p_out,
    vec3 q_out)
{
    vec3 p0, p1, p, q;
    vec3 zero = { 0, 0, 0 };
    float reach = radius;
    int32_t i;
    box_collide_local(c, a, p0);
    box_collide_local(c, b, p1);
    box_collide_on_segment(p0, p1, zero, p);
    box_collide_clamp(c, p, q);
    if (c->kind == BoxColliderCapsule) {
        vec3 top = { 0, c->half[1] - c->half[0] > 0 ? c->half[1] - c->half[0] : 0, 0 };
        vec3 bottom = { 0, -top[1], 0 };
        box_collide_segments(p0, p1, bottom, top, p, q);
    }

    for (i = 0; i < BOX_COLLIDE_STEPS && c->kind != BoxColliderSphere &&
        c->kind != BoxColliderCapsule; i ++)
    {
        box_collide_on_segment(p0, p1, q, p);
        box_collide_clamp(c, p, q);
    }

    if (c->kind == BoxColliderSphere || c->kind == BoxColliderCapsule) {
        reach += c->half[0];
    }

    glm_vec3_copy(p, p_out);
    glm_vec3_copy(q, q_out);

    return glm_vec3_distance(p, q) - reach;
}

/** Find the direction from a collider towards the nearest point of a capsule segment.
 *
 * @param c The collider
 * @param a Center of the lower cap of the capsule
 * @param b Center of the upper cap of the capsule
 * @param hint Way out preferred when the segment runs through a core, may be NULL
 * @param p The nearest point of the segment, in the frame of the collider
 * @param q The nearest point of the collider or its core, in its frame
 * @param n Receives the direction in the frame of the collider
 * @return How much deeper the capsule is than the gap says, for a segment inside the core
 */
static float box_collide_normal(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    const vec3 hint,
    const vec3 p,
    const vec3 q,
    vec3 n)
{
    vec3 d = { p[0] - q[0], p[1] - q[1], p[2] - q[2] };
    vec3 p0, p1, h = { 0, 0, 0 };
    float dist = glm_vec3_norm(d), result = 0;
    if (dist > BOX_COLLIDE_TOUCH) {
        glm_vec3_scale(d, 1.0f / dist, n);
    } else if (c->kind == BoxColliderSphere || c->kind == BoxColliderCapsule) {
        box_collide_local(c, a, p0);
        box_collide_local(c, b, p1);
        if (hint) {
            box_collide_local_dir(c, hint, h);
        }

        box_collide_coincident(c, p0, p1, h, n);
    } else {
        result = box_collide_inside(c, p, n);
    }

    return result;
}

float box_collider_separation(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    const vec3 hint,
    vec3 normal)
{
    vec3 p, q, n;
    float result = box_collide_nearest(c, a, b, radius, p, q);
    result -= box_collide_normal(c, a, b, hint, p, q, n);
    box_collide_world_dir(c, n, normal);

    return result;
}

bool box_collider_push(
    const box_collider_t *c,
    const vec3 a,
    const vec3 b,
    float radius,
    const vec3 hint,
    bool surface,
    box_collider_contact_t *out)
{
    vec3 p, q, n, f;
    vec3 down = { 0, -1, 0 };
    float separation = box_collide_nearest(c, a, b, radius, p, q), t = 0;
    bool result = separation < 0, hit = false;
    if (!result) {
        goto done;
    }

    separation -= box_collide_normal(c, a, b, hint, p, q, n);
    box_collide_world_dir(c, n, out->normal);
    out->depth = -separation;
    out->round = false;
    glm_vec3_copy(out->normal, out->surface);
    if (!surface || out->normal[1] < BOX_CONTROLLER_SUPPORT) {
        goto done;
    }

    if (box_collide_face(c, q, f)) {
        box_collide_world_dir(c, f, out->surface);
        goto done;
    }

    hit = box_collider_ray(c, a, down, FLT_MAX, &t, f);
    if (hit && t > 0) {
        glm_vec3_copy(f, out->surface);
    } else if (!hit) {
        glm_vec3_copy((vec3){ 0, 1, 0 }, out->surface);
        out->round = !box_collide_edge(c, q);
    }

done:
    return result;
}

/** The nearest hit of a ray in the frame of a collider */
typedef struct box_collide_hit_t {
    float t;   /**< Distance of the hit in lengths of the direction */
    float max; /**< Longest distance that counts */
    vec3 n;    /**< Normal of the surface at the hit, in the frame of the collider */
    bool hit;  /**< The ray hits */
} box_collide_hit_t;

/** Keep a hit of a ray when it is the nearest so far.
 *
 * @param h The nearest hit, written
 * @param t Distance of the hit
 * @param n Normal of the surface at the hit
 */
static void box_collide_hit(
    box_collide_hit_t *h,
    float t,
    const vec3 n)
{
    if (t >= 0 && t <= h->max && (!h->hit || t < h->t)) {
        h->hit = true;
        h->t = t;
        glm_vec3_copy(ECS_CONST_CAST(float*, n), h->n);
    }
}

/** Intersect a ray with the outside of a ball.
 *
 * @param o Origin of the ray
 * @param d Direction of the ray
 * @param y Height of the center of the ball on the axis
 * @param radius Radius of the ball
 * @param h The nearest hit, written
 */
static void box_collide_ray_ball(
    const vec3 o,
    const vec3 d,
    float y,
    float radius,
    box_collide_hit_t *h)
{
    vec3 oc = { o[0], o[1] - y, o[2] };
    vec3 n;
    float a = glm_vec3_dot(ECS_CONST_CAST(float*, d), ECS_CONST_CAST(float*, d));
    float b = glm_vec3_dot(oc, ECS_CONST_CAST(float*, d));
    float c = glm_vec3_dot(oc, oc) - radius * radius;
    float disc = b * b - a * c, t = 0;
    if (a > 1e-12f && disc >= 0 && radius > 0) {
        t = (-b - sqrtf(disc)) / a;
        glm_vec3_muladds(ECS_CONST_CAST(float*, d), t, oc);
        glm_vec3_scale(oc, 1.0f / radius, n);
        box_collide_hit(h, t, n);
    }
}

/** Intersect a ray with the outside of the side of a cylinder along the axis.
 *
 * @param o Origin of the ray
 * @param d Direction of the ray
 * @param radius Radius of the cylinder
 * @param height Half height of the side
 * @param h The nearest hit, written
 */
static void box_collide_ray_side(
    const vec3 o,
    const vec3 d,
    float radius,
    float height,
    box_collide_hit_t *h)
{
    float a = d[0] * d[0] + d[2] * d[2];
    float b = o[0] * d[0] + o[2] * d[2];
    float c = o[0] * o[0] + o[2] * o[2] - radius * radius;
    float disc = b * b - a * c, t = 0;
    vec3 n = { 0, 0, 0 };
    if (a > 1e-12f && disc >= 0 && radius > 0) {
        t = (-b - sqrtf(disc)) / a;
        n[0] = (o[0] + d[0] * t) / radius;
        n[2] = (o[2] + d[2] * t) / radius;
        if (fabsf(o[1] + d[1] * t) <= height) {
            box_collide_hit(h, t, n);
        }
    }
}

/** Intersect a ray with the outside of a collider in its own frame.
 *
 * @param c The collider
 * @param o Origin of the ray
 * @param d Direction of the ray
 * @param h The nearest hit, written; a ray that starts inside hits at 0 with a zero normal
 */
static void box_collide_ray_local(
    const box_collider_t *c,
    const vec3 o,
    const vec3 d,
    box_collide_hit_t *h)
{
    const float *e = c->half;
    float core = e[1] - e[0] > 0 ? e[1] - e[0] : 0;
    float flat = o[0] * o[0] + o[2] * o[2];
    vec3 zero = { 0, 0, 0 }, n = { 0, 0, 0 };
    int32_t i;
    if (c->kind == BoxColliderSphere) {
        if (glm_vec3_norm2(ECS_CONST_CAST(float*, o)) <= e[0] * e[0]) {
            box_collide_hit(h, 0, zero);
        }

        box_collide_ray_ball(o, d, 0, e[0], h);
    } else if (c->kind == BoxColliderCapsule) {
        float y = fminf(fmaxf(o[1], -core), core);
        if (flat + (o[1] - y) * (o[1] - y) <= e[0] * e[0]) {
            box_collide_hit(h, 0, zero);
        }

        box_collide_ray_side(o, d, e[0], core, h);
        box_collide_ray_ball(o, d, core, e[0], h);
        box_collide_ray_ball(o, d, -core, e[0], h);
    } else if (c->kind == BoxColliderCylinder) {
        if (flat <= e[0] * e[0] && fabsf(o[1]) <= e[1]) {
            box_collide_hit(h, 0, zero);
        }

        box_collide_ray_side(o, d, e[0], e[1], h);
        for (i = -1; i <= 1 && fabsf(d[1]) > 1e-12f; i += 2) {
            float t = ((float)i * e[1] - o[1]) / d[1];
            float x = o[0] + d[0] * t, z = o[2] + d[2] * t;
            n[1] = (float)i;
            if ((float)i * d[1] < 0 && x * x + z * z <= e[0] * e[0]) {
                box_collide_hit(h, t, n);
            }
        }
    } else {
        float lo = 0, hi = h->max;
        int32_t entry = -1;
        bool result = true;
        for (i = 0; i < 3 && result; i ++) {
            if (fabsf(d[i]) < 1e-12f) {
                result = o[i] >= -e[i] && o[i] <= e[i];
            } else {
                float t0 = (-e[i] - o[i]) / d[i];
                float t1 = (e[i] - o[i]) / d[i];
                if (fminf(t0, t1) > lo) {
                    lo = fminf(t0, t1);
                    entry = i;
                }

                hi = fminf(hi, fmaxf(t0, t1));
                result = lo <= hi;
            }
        }

        if (result && entry >= 0) {
            n[entry] = d[entry] < 0 ? 1.0f : -1.0f;
        }

        if (result) {
            box_collide_hit(h, lo, n);
        }
    }
}

bool box_collider_ray(
    const box_collider_t *c,
    const vec3 origin,
    const vec3 dir,
    float max,
    float *t,
    vec3 normal)
{
    vec3 o, d;
    box_collide_hit_t h = { .max = max };
    box_collide_local(c, origin, o);
    box_collide_local_dir(c, dir, d);
    box_collide_ray_local(c, o, d, &h);
    if (h.hit) {
        *t = h.t;
    }

    if (h.hit && normal) {
        box_collide_world_dir(c, h.n, normal);
    }

    return h.hit;
}
