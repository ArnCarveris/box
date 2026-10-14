/**
 * @file controllers/move.c
 * @brief Moves a controller capsule through its scene: slide, step up and ground probe.
 *
 * A move reads the shapes around the swept capsule from the spatial index once, then walks the
 * motion in steps of half the radius, or as far as the capsule is clear of every shape when the
 * motion is long (conservative advancement), and pushes the capsule out after each step.
 */

#include "controllers.h"
#include "../spatial/spatial.h"
#include <math.h>

/** Times a step pushes the capsule out of the shapes it reaches into */
#define BOX_MOVE_PUSHES (4)

/** Longest step of a move as a fraction of the radius */
#define BOX_MOVE_FRACTION (0.5f)

/** Even steps a move is split into at the most, a longer move advances conservatively */
#define BOX_MOVE_STEPS (8)

/** Overlap a contact needs before it counts */
#define BOX_MOVE_EPS (1e-5f)

/** Overlap a body may have and still fit */
#define BOX_MOVE_FIT (1e-4f)

/** Upward component of the normal of a surface the body can stand on */
#define BOX_MOVE_WALKABLE (0.7f)

/** Upward component of the normal of an edge the drop of a step rests on */
#define BOX_MOVE_SUPPORT (0.3f)

/** Margin around the swept capsule within which shapes are read */
#define BOX_MOVE_MARGIN (0.05f)

/** Contact normals a move keeps to clip the velocity with */
#define BOX_MOVE_NORMALS (8)

/** State of one move: the shapes it can reach and what it touched */
typedef struct box_move_t {
    const box_controller_body_t *body; /**< The body */
    ecs_vec_t colliders;               /**< box_collider_t the move can reach */
    vec3 normals[BOX_MOVE_NORMALS];    /**< Normals of the contacts */
    int32_t normal_count;              /**< Normals kept */
    bool ground;                       /**< A contact was walkable */
    bool wall;                         /**< A contact was too steep to stand on */
    bool support;                      /**< Rest on edges as well, the drop of a step */
} box_move_t;

/** Context of a ray that searches the ground below a body */
typedef struct box_move_ray_t {
    const ecs_world_t *world;          /**< The world */
    const box_controller_body_t *body; /**< The body */
    vec3 origin;                       /**< Origin of the ray */
} box_move_ray_t;

/** Test whether a shape belongs to the controlled entity.
 *
 * @param world The world
 * @param body The body
 * @param e The shape
 * @return true when the shape is the entity or below it
 */
static bool box_move_own(
    const ecs_world_t *world,
    const box_controller_body_t *body,
    ecs_entity_t e)
{
    ecs_entity_t p = e;
    bool result = false;
    while (p && !result) {
        result = p == body->entity;
        p = ecs_get_parent(world, p);
    }

    return result;
}

/** Read the colliders of the shapes overlapping a box from the index of the scene.
 *
 * @param world The world
 * @param m The move, its colliders are appended to
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 */
static void box_move_gather(
    ecs_world_t *world,
    box_move_t *m,
    const vec3 min,
    const vec3 max)
{
    ecs_vec_t found;
    int32_t i, count;
    ecs_vec_init_t(NULL, &found, ecs_entity_t, 0);
    count = box_spatial_overlap_box(world, m->body->scene, min, max, &found);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&found, ecs_entity_t, i);
        box_collider_t c;
        if (!box_move_own(world, m->body, e) && box_collider_resolve(world, e, &c)) {
            *ecs_vec_append_t(NULL, &m->colliders, box_collider_t) = c;
        }
    }

    ecs_vec_fini_t(NULL, &found, ecs_entity_t);
}

/** Get the segment of the capsule of a body at a position.
 *
 * @param body The body
 * @param pos The feet
 * @param a Receives the center of the lower cap
 * @param b Receives the center of the upper cap
 */
static void box_move_segment(
    const box_controller_body_t *body,
    const vec3 pos,
    vec3 a,
    vec3 b)
{
    float top = body->height - body->radius;
    a[0] = b[0] = pos[0];
    a[2] = b[2] = pos[2];
    a[1] = pos[1] + body->radius;
    b[1] = pos[1] + (top > body->radius ? top : body->radius);
}

/** Start a move: read the shapes the capsule can reach on its way.
 *
 * @param world The world
 * @param m The move, initialized
 * @param body The body
 * @param start The feet before the move
 * @param motion The motion of the move
 */
static void box_move_init(
    ecs_world_t *world,
    box_move_t *m,
    const box_controller_body_t *body,
    const vec3 start,
    const vec3 motion)
{
    vec3 a, b, min, max;
    float pad = body->radius + BOX_MOVE_MARGIN;
    int32_t i;
    ecs_os_memset_t(m, 0, box_move_t);
    m->body = body;
    ecs_vec_init_t(NULL, &m->colliders, box_collider_t, 0);
    box_move_segment(body, start, a, b);
    for (i = 0; i < 3; i ++) {
        min[i] = fminf(a[i], a[i] + motion[i]) - pad;
        max[i] = fmaxf(b[i], b[i] + motion[i]) + pad;
    }

    min[1] -= body->probe;
    max[1] += body->step;
    box_move_gather(world, m, min, max);
}

/** Remember a contact normal and whether the body can stand on it.
 *
 * @param m The move
 * @param n The normal
 */
static void box_move_touch(
    box_move_t *m,
    const vec3 n)
{
    bool ground = n[1] >= (m->support ? BOX_MOVE_SUPPORT : BOX_MOVE_WALKABLE);
    m->ground |= ground;
    m->wall |= !ground;
    if (m->normal_count < BOX_MOVE_NORMALS) {
        glm_vec3_copy(ECS_CONST_CAST(float*, n), m->normals[m->normal_count ++]);
    }
}

/** Find the deepest contact of the capsule at a position.
 *
 * @param m The move
 * @param pos The feet
 * @param normal Receives the normal of the deepest contact
 * @return The depth of the deepest contact, 0 when there is none
 */
static float box_move_deepest(
    const box_move_t *m,
    const vec3 pos,
    vec3 normal)
{
    const box_collider_t *c = ecs_vec_first_t(&m->colliders, box_collider_t);
    int32_t i, count = ecs_vec_count(&m->colliders);
    float result = 0;
    vec3 a, b, n;
    box_move_segment(m->body, pos, a, b);
    for (i = 0; i < count; i ++) {
        float depth = 0;
        if (box_collider_push(&c[i], a, b, m->body->radius, n, &depth) && depth > result) {
            result = depth;
            glm_vec3_copy(n, normal);
        }
    }

    return result;
}

/** Push the capsule out of the shapes it reaches into, the deepest contact first.
 *
 * A contact the body can stand on lifts it straight up, so standing on a slope does not slide.
 *
 * @param m The move
 * @param pos The feet, written in place
 */
static void box_move_push(
    box_move_t *m,
    vec3 pos)
{
    bool again = true;
    int32_t i;
    for (i = 0; i < BOX_MOVE_PUSHES && again; i ++) {
        vec3 n;
        float depth = box_move_deepest(m, pos, n);
        again = depth > BOX_MOVE_EPS;
        if (!again) {
            continue;
        }

        if (n[1] >= (m->support ? BOX_MOVE_SUPPORT : BOX_MOVE_WALKABLE)) {
            pos[1] += depth / n[1];
        } else {
            glm_vec3_muladds(n, depth, pos);
        }

        box_move_touch(m, n);
    }
}

/** Find how far the capsule can move along a direction before it can touch a shape.
 *
 * Conservative advancement: the capsule and each shape are convex, so the capsule cannot reach
 * a shape before it crosses the plane that separates them, which it approaches at the speed of
 * the direction along the normal of that plane.
 *
 * @param m The move
 * @param pos The feet
 * @param dir The unit direction of the motion
 * @return Distance along dir that touches no shape, 0 when the capsule already touches one
 */
static float box_move_clearance(
    const box_move_t *m,
    const vec3 pos,
    const vec3 dir)
{
    const box_collider_t *c = ecs_vec_first_t(&m->colliders, box_collider_t);
    int32_t i, count = ecs_vec_count(&m->colliders);
    float result = FLT_MAX;
    vec3 a, b, n;
    box_move_segment(m->body, pos, a, b);
    for (i = 0; i < count && result > 0; i ++) {
        float gap = box_collider_separation(&c[i], a, b, m->body->radius, n);
        float approach = -glm_vec3_dot(ECS_CONST_CAST(float*, dir), n);
        if (gap <= 0) {
            result = 0;
        } else if (approach > 0 && gap < result * approach) {
            result = gap / approach;
        }
    }

    return result;
}

/** Move the capsule by a motion, pushing it out after every step.
 *
 * A motion of a few times half the radius goes in even steps the push can always undo. A
 * longer one advances conservatively: a step goes as far as the capsule is clear of every
 * shape and at least half the radius, so no length of motion skips over a shape.
 *
 * @param m The move
 * @param pos The feet, written in place
 * @param motion The motion
 */
static void box_move_slide(
    box_move_t *m,
    vec3 pos,
    const vec3 motion)
{
    float remaining = glm_vec3_norm(ECS_CONST_CAST(float*, motion));
    float limit = m->body->radius * BOX_MOVE_FRACTION;
    int32_t i, steps = remaining > limit ? (int32_t)ceilf(remaining / limit) : 1;
    vec3 dir = { 0, 0, 0 };
    if (steps <= BOX_MOVE_STEPS) {
        glm_vec3_scale(ECS_CONST_CAST(float*, motion), 1.0f / (float)steps, dir);
        remaining = 0;
    } else {
        glm_vec3_scale(ECS_CONST_CAST(float*, motion), 1.0f / remaining, dir);
        steps = 0;
    }

    for (i = 0; i < steps; i ++) {
        glm_vec3_add(pos, dir, pos);
        box_move_push(m, pos);
    }

    while (remaining > 0) {
        float step = fminf(fmaxf(limit, box_move_clearance(m, pos, dir)), remaining);
        glm_vec3_muladds(dir, step, pos);
        box_move_push(m, pos);
        remaining -= step;
    }
}

/** Get how far apart two positions are in the XZ plane.
 *
 * @param a The first position
 * @param b The second position
 * @return The distance
 */
static float box_move_flat(
    const vec3 a,
    const vec3 b)
{
    float dx = a[0] - b[0], dz = a[2] - b[2];

    return sqrtf(dx * dx + dz * dz);
}

/** Try the horizontal part of a blocked move again on top of a ledge.
 *
 * The capsule rises by the step height, moves and comes down again, resting on the edge of a
 * ledge too; the attempt wins when it lands further along than the blocked move got.
 *
 * @param m The move, which keeps the contacts of the attempt that wins
 * @param start The feet before the move
 * @param motion The motion of the move
 * @param pos The feet after the blocked move, written when the step wins
 */
static void box_move_step(
    box_move_t *m,
    const vec3 start,
    const vec3 motion,
    vec3 pos)
{
    box_move_t attempt = *m;
    vec3 up = { 0, m->body->step, 0 };
    vec3 flat = { motion[0], 0, motion[2] };
    vec3 down = { 0, 0, 0 };
    vec3 at;
    glm_vec3_copy(ECS_CONST_CAST(float*, start), at);
    attempt.normal_count = 0;
    attempt.ground = attempt.wall = false;
    box_move_slide(&attempt, at, up);
    box_move_slide(&attempt, at, flat);
    down[1] = start[1] - at[1] - BOX_MOVE_MARGIN;
    attempt.ground = false;
    attempt.support = true;
    box_move_slide(&attempt, at, down);
    if (attempt.ground && box_move_flat(at, start) > box_move_flat(pos, start) + BOX_MOVE_EPS) {
        m->normal_count = attempt.normal_count;
        ecs_os_memcpy_n(m->normals, attempt.normals, vec3, attempt.normal_count);
        m->ground = true;
        m->wall = attempt.wall;
        glm_vec3_copy(at, pos);
    }
}

/** Search the ground below the feet and put them on it when it is close enough.
 *
 * @param m The move
 * @param pos The feet, written when ground was found
 */
static void box_move_probe(
    box_move_t *m,
    vec3 pos)
{
    box_move_t attempt = *m;
    vec3 down = { 0, -m->body->probe, 0 };
    vec3 at;
    glm_vec3_copy(pos, at);
    attempt.ground = false;
    box_move_slide(&attempt, at, down);
    if (attempt.ground) {
        *m = attempt;
        glm_vec3_copy(at, pos);
    }
}

/** Take out of a velocity what pushes into the surfaces a move touched.
 *
 * Clipping never speeds the body up along Y, so walking into a slope or an edge does not
 * launch it, and a surface the body stands on only stops the fall.
 *
 * @param m The move
 * @param velocity The velocity, written in place
 */
static void box_move_clip(
    const box_move_t *m,
    vec3 velocity)
{
    float rise = velocity[1] > 0 ? velocity[1] : 0;
    int32_t i;
    for (i = 0; i < m->normal_count; i ++) {
        const float *n = m->normals[i];
        float d = glm_vec3_dot(velocity, ECS_CONST_CAST(float*, n));
        if (n[1] >= BOX_MOVE_WALKABLE) {
            velocity[1] = velocity[1] < 0 ? 0 : velocity[1];
        } else if (d < 0) {
            glm_vec3_muladds(ECS_CONST_CAST(float*, n), -d, velocity);
        }
    }

    velocity[1] = velocity[1] > rise ? rise : velocity[1];
}

bool box_controller_move(
    ecs_world_t *world,
    const box_controller_body_t *body,
    Position *feet,
    vec3 velocity,
    float dt,
    box_controller_hit_t *hit)
{
    vec3 start = { feet->x, feet->y, feet->z };
    vec3 motion, pos;
    box_move_t m;
    bool result = false;
    glm_vec3_scale(velocity, dt, motion);
    glm_vec3_add(start, motion, pos);
    if (hit) {
        ecs_os_zeromem(hit);
    }

    if (body->radius <= 0) {
        goto done;
    }

    box_move_init(world, &m, body, start, motion);
    if (ecs_vec_count(&m.colliders)) {
        glm_vec3_copy(start, pos);
        box_move_slide(&m, pos, motion);
        if (m.wall && body->step > 0 && (motion[0] != 0 || motion[2] != 0)) {
            box_move_step(&m, start, motion, pos);
        }

        if (body->probe > 0 && motion[1] <= 0 && !m.ground) {
            box_move_probe(&m, pos);
        }

        box_move_clip(&m, velocity);
        if (hit) {
            hit->grounded = m.ground;
            hit->blocked = m.normal_count > 0;
        }
    }

    ecs_vec_fini_t(NULL, &m.colliders, box_collider_t);

done:
    result = pos[0] != feet->x || pos[1] != feet->y || pos[2] != feet->z;
    feet->x = pos[0];
    feet->y = pos[1];
    feet->z = pos[2];

    return result;
}

bool box_controller_fits(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet)
{
    vec3 pos = { feet->x, feet->y, feet->z };
    vec3 still = { 0, 0, 0 };
    vec3 n;
    box_move_t m;
    bool result = true;
    if (body->radius > 0) {
        box_move_init(world, &m, body, pos, still);
        result = box_move_deepest(&m, pos, n) <= BOX_MOVE_FIT;
        ecs_vec_fini_t(NULL, &m.colliders, box_collider_t);
    }

    return result;
}

/** Accept a shape a ground ray hit when the ray hits its collider.
 *
 * @param ctx The body
 * @param e The shape
 * @param distance Distance at which the ray enters the bounds, moved to the collider
 * @return true when the ray hits the collider
 */
static bool box_move_ground_filter(
    void *ctx,
    ecs_entity_t e,
    float *distance)
{
    const box_move_ray_t *r = ctx;
    box_collider_t c;
    vec3 down = { 0, -1, 0 };

    return !box_move_own(r->world, r->body, e) && box_collider_resolve(r->world, e, &c) &&
        box_collider_ray(&c, r->origin, down, FLT_MAX, distance);
}

bool box_controller_ground_below(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet)
{
    box_spatial_hit_t hit;
    box_move_ray_t ctx = {
        .world = world,
        .body = body,
        .origin = { feet->x, feet->y + body->radius, feet->z }
    };
    box_spatial_ray_t ray = {
        .scene = body->scene,
        .origin = { ctx.origin[0], ctx.origin[1], ctx.origin[2] },
        .dir = { 0, -1, 0 },
        .filter = box_move_ground_filter,
        .ctx = &ctx
    };

    return body->radius > 0 && box_spatial_ray(world, &ray, &hit);
}
