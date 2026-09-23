/**
 * @file collision/sweep.c
 * @brief Moves a capsule through the scene: sliding, a step up and a ground probe.
 *
 * A move reads the reachable colliders from the index once, then walks the
 * motion in steps of at most half the radius, pushing the capsule out after
 * each. A blocked move retries over a ledge up to the step height.
 */

#include "collision_internal.h"

/** Times a step pushes the capsule out of the colliders it reaches into */
#define BOX_SWEEP_PUSH (4)

/** Longest step of a move, as a fraction of the radius of the capsule */
#define BOX_SWEEP_FRACTION (0.5f)

/** Longest step of a drop, as a fraction of the radius of the capsule */
#define BOX_SWEEP_SETTLE (0.25f)

/** Steps a move is split into at the most */
#define BOX_SWEEP_STEPS (32)

/** Overlap a contact needs before it moves the capsule */
#define BOX_SWEEP_EPS (1e-5f)

/** One collider a move can reach, read from the index once */
typedef struct box_sweep_hit_t {
    ecs_entity_t entity; /**< The entity the collider belongs to */
    Collider collider;   /**< Its collider of this frame */
} box_sweep_hit_t;

/** Get the segment of the capsule of a move at a position.
 *
 * @param desc The move
 * @param pos Foot of the capsule
 * @param a Receives the center of the lower cap
 * @param b Receives the center of the upper cap
 */
static void box_sweep_segment(
    const box_move_desc_t *desc,
    vec3 pos,
    vec3 *a,
    vec3 *b)
{
    float radius = desc->radius;
    float top = desc->height - radius;
    a[0] = vec3_new(pos.x, pos.y + radius, pos.z);
    b[0] = vec3_new(pos.x, pos.y + (top > radius ? top : radius), pos.z);
}

/** Collect the colliders the capsule of a move can reach this frame.
 *
 * @param world The world
 * @param desc The move
 * @param out Vector of box_sweep_hit_t the colliders are appended to
 */
static void box_sweep_gather(
    const ecs_world_t *world,
    const box_move_desc_t *desc,
    ecs_vec_t *out)
{
    vec3 a, b;
    box_sweep_segment(desc, desc->position, &a, &b);
    vec3 min = vec3_min(a, vec3_add(a, desc->motion));
    vec3 max = vec3_max(b, vec3_add(b, desc->motion));
    float pad = desc->radius + desc->step;
    min = vec3_sub(min, vec3_new(pad, pad + desc->probe, pad));
    max = vec3_add(max, vec3_new(pad, pad, pad));

    ecs_vec_t found;
    ecs_vec_init_t(NULL, &found, ecs_entity_t, 0);
    box_collision_aabb(world, min, max, &found);

    int32_t i, count = ecs_vec_count(&found);
    const ecs_entity_t *entities = ecs_vec_first_t(&found, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        const Collider *c = entities[i] != desc->skip
            ? ecs_get(world, entities[i], Collider)
            : NULL;
        if (!c) {
            continue;
        }

        box_sweep_hit_t *hit = ecs_vec_append_t(NULL, out, box_sweep_hit_t);
        hit->entity = entities[i];
        hit->collider = c[0];
    }

    ecs_vec_fini_t(NULL, &found, ecs_entity_t);
}

/** Push the capsule out of the colliders it reaches into at a position.
 *
 * The deepest contact is resolved first, which is what lets a corner settle in
 * a handful of passes instead of fighting itself.
 *
 * @param desc The move
 * @param hits The colliders the move can reach
 * @param pos Foot of the capsule
 * @param touched Set when a collider moved the capsule
 * @return The foot of the capsule after the contacts
 */
static vec3 box_sweep_push(
    const box_move_desc_t *desc,
    const ecs_vec_t *hits,
    vec3 pos,
    bool *touched)
{
    int32_t iter, i, count = ecs_vec_count(hits);
    const box_sweep_hit_t *first = ecs_vec_first_t(hits, box_sweep_hit_t);
    bool again = true;
    for (iter = 0; iter < BOX_SWEEP_PUSH && again; iter ++) {
        vec3 a, b, normal = vec3_new(0, 0, 0);
        float deepest = BOX_SWEEP_EPS;
        box_sweep_segment(desc, pos, &a, &b);
        again = false;
        for (i = 0; i < count; i ++) {
            vec3 n;
            float depth;
            if (!box_collider_push(
                &first[i].collider, a, b, desc->radius, &n, &depth))
            {
                continue;
            }

            if (depth > deepest) {
                deepest = depth;
                normal = n;
                again = true;
            }
        }

        if (again) {
            pos = vec3_add(pos, vec3_scale(normal, deepest));
            touched[0] = true;
        }
    }

    return pos;
}

/** Move the capsule by a motion, pushing it out after every step.
 *
 * @param desc The move
 * @param hits The colliders the move can reach
 * @param pos Foot of the capsule
 * @param motion The motion to walk
 * @param touched Set when a collider moved the capsule
 * @return The foot of the capsule after the motion
 */
static vec3 box_sweep_slide(
    const box_move_desc_t *desc,
    const ecs_vec_t *hits,
    vec3 pos,
    vec3 motion,
    bool *touched)
{
    float len = vec3_length(motion);
    float limit = desc->radius * BOX_SWEEP_FRACTION;
    int32_t i, steps = 1;
    if (limit > 0 && len > limit) {
        steps = (int32_t)(len / limit) + 1;
        steps = steps > BOX_SWEEP_STEPS ? BOX_SWEEP_STEPS : steps;
    }

    vec3 delta = vec3_scale(motion, 1.0f / (float)steps);
    for (i = 0; i < steps; i ++) {
        pos = box_sweep_push(desc, hits, vec3_add(pos, delta), touched);
    }

    return pos;
}

/** Test whether the capsule overlaps a collider at a position.
 *
 * @param desc The move
 * @param hits The colliders the move can reach
 * @param pos Foot of the capsule
 * @return true when the capsule reaches into one of them
 */
static bool box_sweep_touches(
    const box_move_desc_t *desc,
    const ecs_vec_t *hits,
    vec3 pos)
{
    bool result = false;
    vec3 a, b;
    box_sweep_segment(desc, pos, &a, &b);

    int32_t i, count = ecs_vec_count(hits);
    const box_sweep_hit_t *first = ecs_vec_first_t(hits, box_sweep_hit_t);
    for (i = 0; i < count && !result; i ++) {
        vec3 n;
        float depth;
        result = box_collider_push(
            &first[i].collider, a, b, desc->radius, &n, &depth) &&
            depth > BOX_SWEEP_EPS;
    }

    return result;
}

/** Lower the capsule until it touches something or ran out of distance.
 *
 * The descent stops before the step that would overlap, which leaves the
 * capsule resting on what is under it instead of being pushed off it.
 *
 * @param desc The move
 * @param hits The colliders the move can reach
 * @param pos Foot of the capsule
 * @param dist How far the capsule may descend
 * @return The foot of the capsule after the descent
 */
static vec3 box_sweep_drop(
    const box_move_desc_t *desc,
    const ecs_vec_t *hits,
    vec3 pos,
    float dist)
{
    float limit = desc->radius * BOX_SWEEP_SETTLE;
    int32_t i, steps = 1;
    if (limit > 0 && dist > limit) {
        steps = (int32_t)(dist / limit) + 1;
        steps = steps > BOX_SWEEP_STEPS ? BOX_SWEEP_STEPS : steps;
    }

    vec3 delta = vec3_new(0, -dist / (float)steps, 0);
    bool clear = true;
    for (i = 0; i < steps && clear; i ++) {
        vec3 next = vec3_add(pos, delta);
        clear = !box_sweep_touches(desc, hits, next);
        if (clear) {
            pos = next;
        }
    }

    return pos;
}

/** Get how far two positions are apart in the plane the capsule walks in.
 *
 * @param a First position
 * @param b Second position
 * @return The distance in the XZ plane
 */
static float box_sweep_flat(
    vec3 a,
    vec3 b)
{
    float dx = a.x - b.x;
    float dz = a.z - b.z;

    return sqrtf(dx * dx + dz * dz);
}

/** Try the horizontal part of a move again over a ledge.
 *
 * The capsule is lifted by the step height, moved horizontally and dropped back
 * to the height it came from; the attempt only wins when it got further than
 * the move that ran into the ledge.
 *
 * @param desc The move
 * @param hits The colliders the move can reach
 * @param pos Foot of the capsule before the move
 * @param base Foot of the capsule after the move that was stopped
 * @return The foot of the capsule after the better of the two
 */
static vec3 box_sweep_step(
    const box_move_desc_t *desc,
    const ecs_vec_t *hits,
    vec3 pos,
    vec3 base)
{
    vec3 result = base;
    bool touched = false;
    vec3 flat = vec3_new(desc->motion.x, 0, desc->motion.z);
    vec3 up = box_sweep_slide(
        desc, hits, pos, vec3_new(0, desc->step, 0), &touched);
    vec3 over = box_sweep_slide(desc, hits, up, flat, &touched);
    vec3 down = box_sweep_drop(desc, hits, over, over.y - pos.y);
    if (box_sweep_flat(down, pos) > box_sweep_flat(base, pos) + BOX_SWEEP_EPS) {
        result = down;
    }

    return result;
}

/** Find the ground below the capsule with a ray down the colliders it reaches.
 *
 * @param desc The move
 * @param hits The colliders the move can reach
 * @param pos Foot of the capsule
 * @param out The result, whose ground is written when the probe found one
 */
static void box_sweep_ground(
    const box_move_desc_t *desc,
    const ecs_vec_t *hits,
    vec3 pos,
    box_move_result_t *out)
{
    vec3 origin = vec3_new(pos.x, pos.y + desc->radius, pos.z);
    vec3 down = vec3_new(0, -1, 0);
    float best = desc->radius + desc->probe;
    int32_t i, count = ecs_vec_count(hits);
    const box_sweep_hit_t *first = ecs_vec_first_t(hits, box_sweep_hit_t);
    for (i = 0; i < count; i ++) {
        float dist;
        if (box_collider_ray(&first[i].collider, origin, down, best, &dist)) {
            best = dist;
            out->ground = first[i].entity;
        }
    }

    if (out->ground) {
        out->ground_y = origin.y - best;
        out->grounded = true;
    }
}

void box_collision_move(
    const ecs_world_t *world,
    const box_move_desc_t *desc,
    box_move_result_t *out)
{
    box_move_result_t result = {
        .position = vec3_add(desc->position, desc->motion) };
    if (desc->radius > 0) {
        ecs_vec_t hits;
        ecs_vec_init_t(NULL, &hits, box_sweep_hit_t, 0);
        box_sweep_gather(world, desc, &hits);

        bool touched = false;
        vec3 pos = box_sweep_slide(
            desc, &hits, desc->position, desc->motion, &touched);
        bool walking = desc->motion.x != 0 || desc->motion.z != 0;
        if (touched && walking && desc->step > 0) {
            pos = box_sweep_step(desc, &hits, desc->position, pos);
        }

        result.position = pos;
        result.blocked = touched;
        if (desc->probe > 0 && desc->motion.y <= 0) {
            box_sweep_ground(desc, &hits, pos, &result);
            if (result.grounded) {
                result.position.y = result.ground_y;
            }
        }

        ecs_vec_fini_t(NULL, &hits, box_sweep_hit_t);
    }

    result.motion = vec3_sub(result.position, desc->position);
    out[0] = result;
}
