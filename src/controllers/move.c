/**
 * @file controllers/move.c
 * @brief Moves a controller capsule through its scene: slide, step up and ground probe.
 *
 * A short move reads the shapes around its swept capsule from the spatial index once; a long one
 * reads them per stretch ahead, sized to how crowded the scene is, until the region read no
 * longer covers a chunk ahead.
 *
 * The motion goes in steps of half the radius, or as far as the capsule is clear of every shape
 * (conservative advancement); a step only tests shapes its sweep overlaps and pushes out after.
 */

#include "controllers.h"
#include "../spatial/spatial.h"
#include "../terrain/terrain.h"
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

/** Margin around the swept capsule within which shapes are read */
#define BOX_MOVE_MARGIN (0.05f)

/** Steps a long move advances at the most; one that needs more stops short, never skips */
#define BOX_MOVE_SWEEPS (1024)

/** Shortest stretch of a long move, in half radii, that it reads the shapes for at once */
#define BOX_MOVE_CHUNK (16)

/** Stretches of BOX_MOVE_CHUNK the first read of a long move covers */
#define BOX_MOVE_AHEAD (4)

/** Shapes a read may hold before a long move reads them for a shorter stretch */
#define BOX_MOVE_CROWD (64)

/** Contact normals a move keeps to clip the velocity with */
#define BOX_MOVE_NORMALS (8)

/** Depth the drop of a step sinks into the shape it lands on, so the push finds the contact */
#define BOX_MOVE_SINK (1e-3f)

/** Points under the footprint of a body that search the floor: the centre and four on the rim */
#define BOX_MOVE_FEET (5)

/** Distance of the rim points of the footprint from the centre, as a fraction of the radius */
#define BOX_MOVE_RIM (0.7f)

/** Terrains a move collides with at the most */
#define BOX_MOVE_TERRAINS (4)

/** Samples along each axis of the grid, a radius to each side of the lower cap, on which a move
 * searches the nearest point of a terrain */
#define BOX_MOVE_TERRAIN_GRID (5)

/** Times the nearest point of a terrain is moved towards the foot of the cap on its plane */
#define BOX_MOVE_TERRAIN_REFINE (3)

/** A shape a move can reach, with its bounds */
typedef struct box_move_shape_t {
    box_collider_t collider; /**< The shape */
    vec3 min;                /**< Minimum corner of its world bounds */
    vec3 max;                /**< Maximum corner of its world bounds */
} box_move_shape_t;

/** State of one move: the shapes around it and what it touched */
typedef struct box_move_t {
    ecs_world_t *world;                /**< The world */
    const box_controller_body_t *body; /**< The body */
    ecs_vec_t shapes;                  /**< box_move_shape_t of the region */
    vec3 region_min;                   /**< Minimum corner of the box the shapes were read for */
    vec3 region_max;                   /**< Maximum corner of the box the shapes were read for */
    bool region;                       /**< The shapes were read */
    bool borrowed;                     /**< The shapes belong to the move this one forked from */
    float ahead;                       /**< Stretch of motion the next read starts from */
    float floor;                       /**< Height of the feet where the step started */
    vec3 normals[BOX_MOVE_NORMALS];    /**< Normals of the contacts */
    int32_t normal_count;              /**< Normals kept */
    bool ground;                       /**< A contact was walkable */
    bool wall;                         /**< A contact was too steep to stand on */
    bool support;                      /**< Rest on edges as well, the drop of a step */
    vec3 back;                         /**< Unit way back along the motion, zero for none */
    ecs_entity_t terrains[BOX_MOVE_TERRAINS]; /**< Terrains of the scene */
    int32_t terrain_count;             /**< Terrains in terrains */
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

/** Get the box the capsule of a body sweeps over a motion, grown by a pad.
 *
 * @param body The body
 * @param pos The feet before the motion
 * @param motion The motion
 * @param pad Distance the box grows by beyond the capsule
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
static void box_move_sweep(
    const box_controller_body_t *body,
    const vec3 pos,
    const vec3 motion,
    float pad,
    vec3 min,
    vec3 max)
{
    vec3 a, b;
    int32_t i;
    box_move_segment(body, pos, a, b);
    for (i = 0; i < 3; i ++) {
        min[i] = fminf(a[i], a[i] + motion[i]) - body->radius - pad;
        max[i] = fmaxf(b[i], b[i] + motion[i]) + body->radius + pad;
    }
}

/** Test whether two boxes overlap.
 *
 * @param amin Minimum corner of the first box
 * @param amax Maximum corner of the first box
 * @param bmin Minimum corner of the second box
 * @param bmax Maximum corner of the second box
 * @return true when they overlap
 */
static bool box_move_overlap(
    const vec3 amin,
    const vec3 amax,
    const vec3 bmin,
    const vec3 bmax)
{
    return amin[0] <= bmax[0] && amax[0] >= bmin[0] && amin[1] <= bmax[1] &&
        amax[1] >= bmin[1] && amin[2] <= bmax[2] && amax[2] >= bmin[2];
}

/** Add a collider to the shapes a move tests, with the world bounds of its frame.
 *
 * @param m The move, its shapes are written
 * @param c The collider
 */
static void box_move_add(
    box_move_t *m,
    const box_collider_t *c)
{
    box_move_shape_t s = { .collider = *c };
    int32_t j, k;
    for (j = 0; j < 3; j ++) {
        float ext = 0;
        for (k = 0; k < 3; k ++) {
            ext += fabsf(s.collider.axis[k][j]) * s.collider.half[k];
        }

        s.min[j] = s.collider.center[j] - ext;
        s.max[j] = s.collider.center[j] + ext;
    }

    *ecs_vec_append_t(NULL, &m->shapes, box_move_shape_t) = s;
}

/** Read the shapes overlapping a box from the index of the scene, replacing the ones read.
 *
 * @param m The move, its shapes and region are written
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @param crowd Most shapes the box may hold, the shapes are not read when it holds more
 * @return false when the box holds more than crowd shapes
 */
static bool box_move_gather(
    box_move_t *m,
    const vec3 min,
    const vec3 max,
    int32_t crowd)
{
    ecs_vec_t hits, found, triangles;
    int32_t i, j, count, shapes;
    bool result = false;
    ecs_vec_init_t(NULL, &triangles, box_collider_t, 0);
    ecs_vec_init_t(NULL, &hits, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &found, ecs_entity_t, 0);
    count = box_spatial_overlap_box(m->world, m->body->scene, min, max, &hits);
    for (i = 0; i < count; i ++) {
        box_spatial_expand_box(m->world, *ecs_vec_get_t(&hits, ecs_entity_t, i), min, max,
            &found);
    }

    shapes = ecs_vec_count(&found);
    if (shapes > crowd) {
        goto done;
    }

    if (m->borrowed) {
        ecs_vec_init_t(NULL, &m->shapes, box_move_shape_t, count);
        m->borrowed = false;
    }

    ecs_vec_clear(&m->shapes);
    for (i = 0; i < shapes; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&found, ecs_entity_t, i);
        box_collider_t c;
        if (box_move_own(m->world, m->body, e) || !box_collider_solid(m->world, e)) {
            continue;
        }

        if (box_collider_has_triangles(m->world, e)) {
            ecs_vec_clear(&triangles);
            count = box_collider_triangles(m->world, e, min, max, &triangles);
            for (j = 0; j < count; j ++) {
                box_move_add(m, ecs_vec_get_t(&triangles, box_collider_t, j));
            }
        } else if (box_collider_resolve(m->world, e, &c)) {
            box_move_add(m, &c);
        }
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, min), m->region_min);
    glm_vec3_copy(ECS_CONST_CAST(float*, max), m->region_max);
    m->region = true;
    result = true;

done:
    ecs_vec_fini_t(NULL, &triangles, box_collider_t);
    ecs_vec_fini_t(NULL, &hits, ecs_entity_t);
    ecs_vec_fini_t(NULL, &found, ecs_entity_t);

    return result;
}

/** Make sure the shapes read cover what the capsule can reach over a motion.
 *
 * When the shapes read do not cover the box, the move reads the shapes of the box grown by the
 * step and probe heights, which its steps and probes then also reach.
 *
 * A box that holds more than BOX_MOVE_CROWD shapes is read for a quarter of the motion instead,
 * down to least. The next read starts from twice the stretch that worked, or from the whole
 * motion when it held an eighth of that or less.
 *
 * @param m The move
 * @param pos The feet before the motion
 * @param motion The motion, shortened to the stretch the shapes cover
 * @param least Shortest stretch to read the shapes for
 * @param slack Distance the box may reach beyond the shapes read, for what pushes moved it
 */
static void box_move_cover(
    box_move_t *m,
    const vec3 pos,
    vec3 motion,
    float least,
    float slack)
{
    const box_controller_body_t *body = m->body;
    float len = glm_vec3_norm(motion);
    bool done = false;
    vec3 min, max;
    while (!done) {
        box_move_sweep(body, pos, motion, BOX_MOVE_MARGIN - BOX_MOVE_FIT - slack, min, max);
        done = m->region && min[0] >= m->region_min[0] && min[1] >= m->region_min[1] &&
            min[2] >= m->region_min[2] && max[0] <= m->region_max[0] &&
            max[1] <= m->region_max[1] && max[2] <= m->region_max[2];
        glm_vec3_subs(min, BOX_MOVE_FIT + slack, min);
        glm_vec3_adds(max, BOX_MOVE_FIT + slack, max);
        min[1] -= body->probe;
        max[1] += body->step;
        done = done || box_move_gather(m, min, max, len > least ? BOX_MOVE_CROWD : INT32_MAX);
        if (!done) {
            float shorter = fmaxf(len * 0.25f, least);
            glm_vec3_scale(motion, shorter / len, motion);
            len = shorter;
        } else if (ecs_vec_count(&m->shapes) * 8 <= BOX_MOVE_CROWD) {
            m->ahead = FLT_MAX;
        } else {
            m->ahead = len * 2;
        }
    }
}

/** Find how far along a direction the capsule stays inside the region of the shapes read.
 *
 * @param m The move
 * @param pos The feet
 * @param dir The unit direction
 * @return Distance along dir the sweep stays inside the region, negative when pos is outside
 */
static float box_move_covered(
    const box_move_t *m,
    const vec3 pos,
    const vec3 dir)
{
    vec3 still = { 0, 0, 0 }, lo, hi;
    float result = m->region ? FLT_MAX : -1;
    int32_t i;
    box_move_sweep(m->body, pos, still, BOX_MOVE_MARGIN - BOX_MOVE_FIT, lo, hi);
    for (i = 0; i < 3 && result >= 0; i ++) {
        if (lo[i] < m->region_min[i] || hi[i] > m->region_max[i]) {
            result = -1;
        } else if (dir[i] > 0) {
            result = fminf(result, (m->region_max[i] - hi[i]) / dir[i]);
        } else if (dir[i] < 0) {
            result = fminf(result, (lo[i] - m->region_min[i]) / -dir[i]);
        }
    }

    return result;
}

/** Start a move: read the shapes the capsule can reach on its first stretch.
 *
 * A first stretch that holds few shapes is read again for the whole motion, so a long move
 * through an open scene steps without stopping at the end of the first read.
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
    float chunk = body->radius * BOX_MOVE_FRACTION * BOX_MOVE_CHUNK;
    float len = glm_vec3_norm(ECS_CONST_CAST(float*, motion));
    vec3 first;
    ecs_os_memset_t(m, 0, box_move_t);
    m->world = world;
    m->body = body;
    m->ahead = chunk * BOX_MOVE_AHEAD;
    m->terrain_count = box_terrain_scene(world, body->scene, m->terrains, BOX_MOVE_TERRAINS);
    ecs_vec_init_t(NULL, &m->shapes, box_move_shape_t, 0);
    glm_vec3_scale(ECS_CONST_CAST(float*, motion), len > m->ahead ? m->ahead / len : 1.0f, first);
    box_move_cover(m, start, first, chunk, 0);
    if (len > chunk * BOX_MOVE_AHEAD && m->ahead == FLT_MAX) {
        glm_vec3_copy(ECS_CONST_CAST(float*, motion), first);
        box_move_cover(m, start, first, chunk, 0);
    }
}

/** Copy a move for an attempt that may replace it; it borrows the shapes until it reads its own.
 *
 * @param m The move
 * @param out The copy
 */
static void box_move_fork(
    const box_move_t *m,
    box_move_t *out)
{
    *out = *m;
    out->borrowed = true;
}

/** Free the shapes of a move, unless it borrows them.
 *
 * @param m The move
 */
static void box_move_fini(
    box_move_t *m)
{
    if (!m->borrowed) {
        ecs_vec_fini_t(NULL, &m->shapes, box_move_shape_t);
    }
}

/** Let a move take the shapes an attempt it forked read for itself.
 *
 * @param m The move
 * @param attempt The attempt, which keeps a borrowed reference
 */
static void box_move_take(
    box_move_t *m,
    box_move_t *attempt)
{
    if (!attempt->borrowed) {
        box_move_fini(m);
        m->shapes = attempt->shapes;
        m->borrowed = false;
        m->region = attempt->region;
        m->ahead = attempt->ahead;
        glm_vec3_copy(attempt->region_min, m->region_min);
        glm_vec3_copy(attempt->region_max, m->region_max);
        attempt->borrowed = true;
    }
}

/** Test whether the body can stand on a contact.
 *
 * The drop of a step also rests on an edge of a surface the body can stand on, no higher than
 * the step above where the step started, as Unreal's StepUp reverts a step whose impact point
 * is above MaxStepHeight; it rests on the flank of a round shape only when the whole shape is.
 *
 * @param m The move
 * @param contact The contact
 * @param point Height of the point of the shape the contact reaches deepest
 * @return true when the contact holds the body up
 */
static bool box_move_ground(
    const box_move_t *m,
    const box_collider_contact_t *contact,
    float point)
{
    const box_controller_body_t *body = m->body;
    bool result = contact->normal[1] >= BOX_MOVE_WALKABLE;
    if (m->support) {
        result = contact->normal[1] >= BOX_CONTROLLER_SUPPORT &&
            contact->surface[1] >= BOX_MOVE_WALKABLE &&
            point <= m->floor + body->step + BOX_MOVE_FIT;
    }

    return result;
}

/** Remember a contact normal and whether the body can stand on it.
 *
 * @param m The move
 * @param contact The contact
 * @param point Height of the point of the shape the contact reaches deepest
 */
static void box_move_touch(
    box_move_t *m,
    const box_collider_contact_t *contact,
    float point)
{
    bool ground = box_move_ground(m, contact, point);
    m->ground |= ground;
    m->wall |= !ground;
    if (m->normal_count < BOX_MOVE_NORMALS) {
        glm_vec3_copy(ECS_CONST_CAST(float*, contact->normal), m->normals[m->normal_count ++]);
    }
}

/** Test whether an upright body treats a contact as a wall: too steep to stand on, not a ceiling.
 *
 * @param m The move
 * @param n The normal of the contact
 * @return true when the contact is steep
 */
static bool box_move_steep(
    const box_move_t *m,
    const vec3 n)
{
    return m->body->upright && n[1] >= 0 && n[1] < BOX_MOVE_WALKABLE &&
        (n[0] != 0 || n[2] != 0);
}

/** Find the signed distance between the lower cap of the capsule and the surface of a terrain.
 *
 * The nearest of a grid of points of the surface around the cap, a radius to each side, moves
 * to below the foot of the cap center on the plane of the surface there, which finds the
 * nearest point of a plane at once.
 *
 * The distance to it and the direction from it to the center of the cap give the contact; a
 * cap whose center is below the surface is as deep as the plane of the surface above it says.
 *
 * @param m The move
 * @param terrain The terrain
 * @param a Center of the lower cap
 * @param normal Receives the direction from the surface towards the cap
 * @param surface Receives the normal of the surface at the nearest point
 * @return The gap, negative by the depth when they overlap, FLT_MAX where the terrain is not
 * resident
 */
static float box_move_terrain_separation(
    const box_move_t *m,
    ecs_entity_t terrain,
    const vec3 a,
    vec3 normal,
    vec3 surface)
{
    float r = m->body->radius, y = 0, best = FLT_MAX, result = FLT_MAX;
    vec3 n, p = { 0, 0, 0 }, pn = { 0, 1, 0 }, d;
    int32_t i, j;
    if (!box_terrain_ground(m->world, terrain, a[0], a[2], &y, n)) {
        goto done;
    }

    if (a[1] <= y) {
        glm_vec3_copy(n, normal);
        glm_vec3_copy(n, surface);
        result = -(y - a[1]) * n[1] - r;
        goto done;
    }

    for (i = 0; i < BOX_MOVE_TERRAIN_GRID; i ++) {
        for (j = 0; j < BOX_MOVE_TERRAIN_GRID; j ++) {
            float fx = (float)i / (float)(BOX_MOVE_TERRAIN_GRID - 1) * 2.0f - 1.0f;
            float fz = (float)j / (float)(BOX_MOVE_TERRAIN_GRID - 1) * 2.0f - 1.0f;
            vec3 s = { a[0] + r * fx, 0, a[2] + r * fz };
            float len;
            if (!box_terrain_ground(m->world, terrain, s[0], s[2], &s[1], NULL)) {
                continue;
            }

            glm_vec3_sub(ECS_CONST_CAST(float*, a), s, d);
            len = glm_vec3_norm(d);
            if (len < best) {
                best = len;
                glm_vec3_copy(s, p);
            }
        }
    }

    if (best < FLT_MAX) {
        box_terrain_ground(m->world, terrain, p[0], p[2], NULL, pn);
    }

    for (i = 0; i < BOX_MOVE_TERRAIN_REFINE && best < FLT_MAX; i ++) {
        vec3 s, sn;
        float len;
        glm_vec3_sub(ECS_CONST_CAST(float*, a), p, d);
        glm_vec3_copy(ECS_CONST_CAST(float*, a), s);
        glm_vec3_muladds(pn, -glm_vec3_dot(d, pn), s);
        if (!box_terrain_ground(m->world, terrain, s[0], s[2], &s[1], sn)) {
            break;
        }

        glm_vec3_sub(ECS_CONST_CAST(float*, a), s, d);
        len = glm_vec3_norm(d);
        if (len >= best) {
            break;
        }

        best = len;
        glm_vec3_copy(s, p);
        glm_vec3_copy(sn, pn);
    }

    if (best < FLT_MAX) {
        result = best - r;
        glm_vec3_sub(ECS_CONST_CAST(float*, a), p, d);
        if (best > BOX_MOVE_EPS && d[1] > 0) {
            glm_vec3_scale(d, 1.0f / best, normal);
        } else {
            glm_vec3_copy(pn, normal);
        }

        glm_vec3_copy(pn, surface);
    }

done:
    return result;
}

/** Find how far the capsule reaches into the surface of a terrain.
 *
 * @param m The move
 * @param terrain The terrain
 * @param a Center of the lower cap
 * @param out The contact, written when the capsule overlaps the surface
 * @return true when it does
 */
static bool box_move_terrain_push(
    const box_move_t *m,
    ecs_entity_t terrain,
    const vec3 a,
    box_collider_contact_t *out)
{
    vec3 normal, surface;
    float gap = box_move_terrain_separation(m, terrain, a, normal, surface);
    bool result = gap < 0;
    if (result) {
        glm_vec3_copy(normal, out->normal);
        glm_vec3_copy(surface, out->surface);
        out->depth = -gap;
        out->round = false;
    }

    return result;
}

/** Find the deepest contact of the capsule at a position.
 *
 * @param m The move
 * @param pos The feet
 * @param out Receives the deepest contact
 * @param steep Receives the sum of the normals of the steep contacts, see box_move_steep
 * @return The depth of the deepest contact, 0 when there is none
 */
static float box_move_deepest(
    const box_move_t *m,
    const vec3 pos,
    box_collider_contact_t *out,
    vec3 steep)
{
    const box_move_shape_t *s = ecs_vec_first_t(&m->shapes, box_move_shape_t);
    int32_t i, count = ecs_vec_count(&m->shapes);
    float result = 0;
    vec3 a, b, min, max, still = { 0, 0, 0 };
    glm_vec3_zero(steep);
    box_move_segment(m->body, pos, a, b);
    box_move_sweep(m->body, pos, still, BOX_MOVE_EPS, min, max);
    for (i = 0; i < count; i ++) {
        box_collider_contact_t contact;
        if (!box_move_overlap(min, max, s[i].min, s[i].max) ||
            !box_collider_push(&s[i].collider, a, b, m->body->radius, m->back, m->support,
                &contact))
        {
            continue;
        }

        if (contact.round && s[i].max[1] > m->floor + m->body->step + BOX_MOVE_FIT) {
            glm_vec3_copy(contact.normal, contact.surface);
        }

        if (box_move_steep(m, contact.normal)) {
            glm_vec3_add(steep, contact.normal, steep);
        }

        if (contact.depth > result) {
            result = contact.depth;
            *out = contact;
        }
    }

    for (i = 0; i < m->terrain_count; i ++) {
        box_collider_contact_t contact;
        if (!box_move_terrain_push(m, m->terrains[i], a, &contact)) {
            continue;
        }

        if (box_move_steep(m, contact.normal)) {
            glm_vec3_add(steep, contact.normal, steep);
        }

        if (contact.depth > result) {
            result = contact.depth;
            *out = contact;
        }
    }

    return result;
}

/** Push the capsule out of the shapes it reaches into, the deepest contact first.
 *
 * A contact the body can stand on lifts it straight up, so standing on a slope does not slide.
 * An upright body is pushed off a surface too steep to stand on horizontally, like off a wall,
 * so walking or jumping into a steep slope does not climb it.
 *
 * Steep surfaces the body touches at once that together face up enough to stand on, a valley
 * of two slopes or a slope against a wall, form a crease that holds it like ground, as Jolt's
 * CharacterVirtual supports a character by the average normal of its steep contacts.
 *
 * @param m The move
 * @param pos The feet, written in place
 * @param rest Motion still to go, loses what pushes into the contacts, may be NULL
 */
static void box_move_push(
    box_move_t *m,
    vec3 pos,
    vec3 rest)
{
    bool again = true;
    int32_t i;
    for (i = 0; i < BOX_MOVE_PUSHES && again; i ++) {
        box_collider_contact_t contact;
        float *n = contact.normal;
        vec3 steep, away;
        float depth = box_move_deepest(m, pos, &contact, steep);
        float flat = 0, d = 0, point = 0;
        bool crease = false;
        again = depth > BOX_MOVE_EPS;
        if (!again) {
            continue;
        }

        point = pos[1] + m->body->radius - n[1] * (m->body->radius - depth);
        flat = sqrtf(n[0] * n[0] + n[2] * n[2]);
        glm_vec3_copy(n, away);
        if (box_move_steep(m, n)) {
            glm_vec3_normalize(steep);
            crease = steep[1] >= BOX_MOVE_WALKABLE;
        }

        if (n[1] >= (m->support ? BOX_CONTROLLER_SUPPORT : BOX_MOVE_WALKABLE)) {
            pos[1] += depth / n[1];
        } else if (crease && n[1] > 0) {
            pos[1] += depth / n[1];
            glm_vec3_copy(steep, contact.normal);
            glm_vec3_copy(steep, contact.surface);
        } else if (box_move_steep(m, n)) {
            away[0] = n[0] / flat;
            away[1] = 0;
            away[2] = n[2] / flat;
            glm_vec3_muladds(away, depth / flat, pos);
        } else {
            glm_vec3_muladds(n, depth, pos);
        }

        d = rest ? glm_vec3_dot(rest, away) : 0;
        if (d < 0) {
            glm_vec3_muladds(away, -d, rest);
        }

        box_move_touch(m, &contact, point);
    }
}

/** Find how far the capsule can move along a direction before it can touch a shape.
 *
 * Conservative advancement: the capsule and each shape are convex, so the capsule cannot reach
 * a shape before it crosses the plane that separates them, which it approaches at the speed of
 * the direction along the normal of that plane.
 *
 * A shape the capsule already touches limits the move only while it moves into it, by as much
 * as a push can undo.
 *
 * @param m The move
 * @param pos The feet
 * @param dir The unit direction of the motion
 * @param limit Depth a push can always undo
 * @param reach Distance along dir beyond which shapes do not matter
 * @return Distance along dir that reaches into no shape further than a push undoes
 */
static float box_move_clearance(
    const box_move_t *m,
    const vec3 pos,
    const vec3 dir,
    float limit,
    float reach)
{
    const box_move_shape_t *s = ecs_vec_first_t(&m->shapes, box_move_shape_t);
    int32_t i, count = ecs_vec_count(&m->shapes);
    float result = FLT_MAX;
    vec3 a, b, n, min, max, motion;
    box_move_segment(m->body, pos, a, b);
    glm_vec3_scale(ECS_CONST_CAST(float*, dir), reach, motion);
    box_move_sweep(m->body, pos, motion, BOX_MOVE_EPS, min, max);
    for (i = 0; i < count && result > 0; i ++) {
        float gap, approach;
        if (!box_move_overlap(min, max, s[i].min, s[i].max)) {
            continue;
        }

        gap = box_collider_separation(&s[i].collider, a, b, m->body->radius, m->back, n);
        approach = -glm_vec3_dot(ECS_CONST_CAST(float*, dir), n);
        gap = gap > 0 ? gap : fmaxf(gap + limit, 0);
        if (approach > 0 && gap < result * approach) {
            result = gap / approach;
        }
    }

    for (i = 0; i < m->terrain_count && result > 0; i ++) {
        vec3 surface;
        float gap = box_move_terrain_separation(m, m->terrains[i], a, n, surface), approach;
        if (gap == FLT_MAX) {
            continue;
        }

        approach = -glm_vec3_dot(ECS_CONST_CAST(float*, dir), n);
        gap = gap > 0 ? gap : fmaxf(gap + limit, 0);
        if (approach > 0 && gap < result * approach) {
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
 * The rest of the motion loses what pushes into the shapes a step touched (collide and slide).
 * A step only tests the shapes its sweep overlaps, read for the stretch ahead (box_move_cover).
 *
 * @param m The move
 * @param pos The feet, written in place
 * @param motion The motion, a non-finite one does not move
 */
static void box_move_slide(
    box_move_t *m,
    vec3 pos,
    const vec3 motion)
{
    float len = glm_vec3_norm(ECS_CONST_CAST(float*, motion));
    float limit = m->body->radius * BOX_MOVE_FRACTION;
    float chunk = limit * BOX_MOVE_CHUNK;
    int32_t i, steps = 0;
    vec3 rest = { 0, 0, 0 }, dir = { 0, 0, 0 }, ahead;
    if (isfinite(len) && len > 0) {
        glm_vec3_scale(ECS_CONST_CAST(float*, motion), -1.0f / len, m->back);
    }

    if (!isfinite(len)) {
        len = 0;
    } else if (len <= limit * BOX_MOVE_STEPS) {
        steps = len > limit ? (int32_t)ceilf(len / limit) : 1;
        glm_vec3_scale(ECS_CONST_CAST(float*, motion), 1.0f / (float)steps, dir);
        glm_vec3_copy(ECS_CONST_CAST(float*, motion), ahead);
        box_move_cover(m, pos, ahead, len, m->body->radius + BOX_MOVE_MARGIN);
        len = 0;
    } else {
        glm_vec3_copy(ECS_CONST_CAST(float*, motion), rest);
    }

    for (i = 0; i < steps; i ++) {
        glm_vec3_add(pos, dir, pos);
        box_move_push(m, pos, NULL);
    }

    for (i = 0; i < BOX_MOVE_SWEEPS && len > BOX_MOVE_EPS; i ++) {
        float step, far;
        glm_vec3_scale(rest, 1.0f / len, dir);
        glm_vec3_negate_to(dir, m->back);
        far = box_move_covered(m, pos, dir);
        if (far < fminf(len, chunk)) {
            far = fminf(len, m->ahead);
            glm_vec3_scale(dir, far, ahead);
            box_move_cover(m, pos, ahead, fminf(far, chunk), 0);
            far = glm_vec3_norm(ahead);
        }

        far = fminf(far, len);
        step = fminf(fmaxf(limit, box_move_clearance(m, pos, dir, limit, far)), far);
        glm_vec3_muladds(dir, step, pos);
        glm_vec3_scale(dir, len - step, rest);
        box_move_push(m, pos, rest);
        len = glm_vec3_norm(rest);
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

/** Lower the capsule straight down onto the first shape below it, by a distance at the most.
 *
 * The drop advances conservatively, so it lands on the edge of a ledge instead of sinking past
 * it in a step, where the push would take the edge for a wall and move the capsule off it.
 *
 * @param m The move, which keeps the contact of the landing
 * @param pos The feet, written in place
 * @param distance Distance the feet may come down
 */
static void box_move_drop(
    box_move_t *m,
    vec3 pos,
    float distance)
{
    vec3 down = { 0, -1, 0 };
    float left = distance;
    float gap = FLT_MAX;
    int32_t i;
    glm_vec3_negate_to(down, m->back);
    for (i = 0; i < BOX_MOVE_SWEEPS && left > 0 && gap > BOX_MOVE_SINK * 0.25f; i ++) {
        gap = fminf(box_move_clearance(m, pos, down, 0, left), left);
        pos[1] -= gap;
        left -= gap;
    }

    pos[1] -= fminf(left, BOX_MOVE_SINK);
    box_move_push(m, pos, NULL);
}

/** Find the highest floor below a height at a point.
 *
 * @param m The move
 * @param x X of the point
 * @param z Z of the point
 * @param top Height the search starts from
 * @param depth Distance below top the search reaches
 * @param floor Receives the height of the floor, written when it is higher
 */
static void box_move_floor_at(
    const box_move_t *m,
    float x,
    float z,
    float top,
    float depth,
    float *floor)
{
    const box_move_shape_t *s = ecs_vec_first_t(&m->shapes, box_move_shape_t);
    int32_t i, count = ecs_vec_count(&m->shapes);
    vec3 origin = { x, top, z };
    vec3 down = { 0, -1, 0 };
    for (i = 0; i < count; i ++) {
        bool inside = x >= s[i].min[0] && x <= s[i].max[0] &&
            z >= s[i].min[2] && z <= s[i].max[2];
        float t = 0;
        vec3 n;
        if (!inside || !box_collider_ray(&s[i].collider, origin, down, depth, &t, n)) {
            continue;
        }

        if (n[1] >= BOX_MOVE_WALKABLE && top - t > *floor) {
            *floor = top - t;
        }
    }
}

/** Find the highest floor under the footprint of the body, between the feet and a height.
 *
 * As the walker of the original portal demo, the centre and four points on the rim, at
 * BOX_MOVE_RIM of the radius, search the floor straight down, so a body whose round bottom
 * rests on the edge of a tread shorter than itself still finds the tread.
 *
 * @param m The move
 * @param pos The feet
 * @param top Highest floor that counts
 * @return The height of the highest floor, the feet when there is none above them
 */
static float box_move_floor(
    const box_move_t *m,
    const vec3 pos,
    float top)
{
    static const float feet[BOX_MOVE_FEET][2] = {
        { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };
    float rim = m->body->radius * BOX_MOVE_RIM;
    float result = pos[1];
    float depth = top - pos[1];
    int32_t i;
    for (i = 0; i < BOX_MOVE_FEET && depth > BOX_MOVE_EPS; i ++) {
        float x = pos[0] + feet[i][0] * rim;
        float z = pos[2] + feet[i][1] * rim;
        box_move_floor_at(m, x, z, top, depth, &result);
    }

    return result;
}

/** Lift the feet onto the highest floor under the footprint when the capsule fits there.
 *
 * @param m The move
 * @param pos The feet, written when they are lifted
 * @param top Highest floor the feet are lifted onto
 */
static void box_move_lift(
    const box_move_t *m,
    vec3 pos,
    float top)
{
    float floor = box_move_floor(m, pos, top + BOX_MOVE_FIT);
    box_collider_contact_t contact;
    vec3 lifted, steep;
    glm_vec3_copy(pos, lifted);
    lifted[1] = floor;
    if (floor > pos[1] + BOX_MOVE_EPS &&
        box_move_deepest(m, lifted, &contact, steep) <= BOX_MOVE_FIT)
    {
        pos[1] = floor;
    }
}

/** Try the horizontal part of a blocked move again on top of a ledge.
 *
 * The capsule rises by the step height, moves and comes down onto the first shape below it,
 * resting on the edge of a ledge too. The attempt wins when it lands further along than the
 * blocked move got.
 *
 * It then stands on the highest floor under its footprint within the step, as on a tread
 * shorter than itself.
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
    box_move_t attempt;
    vec3 up = { 0, m->body->step, 0 };
    vec3 flat = { motion[0], 0, motion[2] };
    vec3 at;
    glm_vec3_copy(ECS_CONST_CAST(float*, start), at);
    box_move_fork(m, &attempt);
    attempt.normal_count = 0;
    attempt.ground = attempt.wall = false;
    box_move_slide(&attempt, at, up);
    box_move_slide(&attempt, at, flat);
    attempt.ground = false;
    attempt.support = true;
    attempt.floor = start[1];
    box_move_drop(&attempt, at, at[1] - start[1] + BOX_MOVE_MARGIN);
    if (attempt.ground) {
        box_move_lift(&attempt, at, start[1] + m->body->step);
    }

    if (attempt.ground && box_move_flat(at, start) > box_move_flat(pos, start) + BOX_MOVE_EPS) {
        m->normal_count = attempt.normal_count;
        ecs_os_memcpy_n(m->normals, attempt.normals, vec3, attempt.normal_count);
        m->ground = true;
        m->wall = attempt.wall;
        glm_vec3_copy(at, pos);
        box_move_take(m, &attempt);
    } else {
        box_move_fini(&attempt);
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
    box_move_t attempt;
    vec3 down = { 0, -m->body->probe, 0 };
    vec3 at;
    glm_vec3_copy(pos, at);
    box_move_fork(m, &attempt);
    attempt.ground = false;
    box_move_slide(&attempt, at, down);
    if (attempt.ground) {
        box_move_take(m, &attempt);
        attempt.shapes = m->shapes;
        attempt.borrowed = m->borrowed;
        *m = attempt;
        glm_vec3_copy(at, pos);
    } else {
        box_move_fini(&attempt);
    }
}

/** Take out of a velocity what pushes into the surfaces a move touched.
 *
 * Clipping never speeds the body up along Y, so walking into a slope or an edge does not
 * launch it, and a surface the body stands on only stops the fall. An upright body clips
 * against a surface too steep to stand on as against a wall, so it does not stop the fall.
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
        vec3 n;
        float d, flat;
        glm_vec3_copy(ECS_CONST_CAST(float*, m->normals[i]), n);
        flat = sqrtf(n[0] * n[0] + n[2] * n[2]);
        if (m->body->upright && n[1] > 0 && n[1] < BOX_MOVE_WALKABLE && flat > 0) {
            n[0] /= flat;
            n[1] = 0;
            n[2] /= flat;
        }

        d = glm_vec3_dot(velocity, n);
        if (n[1] >= BOX_MOVE_WALKABLE) {
            velocity[1] = velocity[1] < 0 ? 0 : velocity[1];
        } else if (d < 0) {
            glm_vec3_muladds(n, -d, velocity);
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

    if (!isfinite(motion[0] + motion[1] + motion[2])) {
        glm_vec3_copy(start, pos);
        goto done;
    }

    if (body->radius <= 0) {
        goto done;
    }

    box_move_init(world, &m, body, start, motion);
    if (ecs_vec_count(&m.shapes) || m.terrain_count || glm_vec3_norm(motion) > body->radius *
        BOX_MOVE_FRACTION * BOX_MOVE_CHUNK)
    {
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

    box_move_fini(&m);

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
    box_collider_contact_t contact;
    box_move_t m;
    vec3 steep;
    bool result = true;
    if (body->radius > 0) {
        box_move_init(world, &m, body, pos, still);
        result = box_move_deepest(&m, pos, &contact, steep) <= BOX_MOVE_FIT;
        box_move_fini(&m);
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
    vec3 down = { 0, -1, 0 };

    return !box_move_own(r->world, r->body, e) && box_collider_solid(r->world, e) &&
        box_collider_cast(r->world, e, r->origin, down, FLT_MAX, distance, NULL);
}

/* A terrain of the scene of the body has ground under its feet, no higher than the origin of
 * the ray of box_controller_ground_shape */
static bool box_move_terrain_below(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet)
{
    ecs_entity_t terrains[BOX_MOVE_TERRAINS];
    int32_t i, count = body->radius > 0
        ? box_terrain_scene(world, body->scene, terrains, BOX_MOVE_TERRAINS)
        : 0;
    for (i = 0; i < count; i ++) {
        float y = 0;
        if (box_terrain_ground(world, terrains[i], feet->x, feet->z, &y, NULL) &&
            y <= feet->y + body->radius)
        {
            return true;
        }
    }

    return false;
}

bool box_controller_ground_below(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet)
{
    return box_move_terrain_below(world, body, feet) ||
        box_controller_ground_shape(world, body, feet) != 0;
}

ecs_entity_t box_controller_ground_shape(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet)
{
    box_spatial_hit_t hit = { 0 };
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
        .ctx = &ctx,
        .shapes = true
    };

    bool found = body->radius > 0 && box_spatial_ray(world, &ray, &hit);

    return found ? hit.entity : 0;
}
