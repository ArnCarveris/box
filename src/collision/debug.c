/**
 * @file collision/debug.c
 * @brief Turns the colliders and the root bounds a view sees into debug lines.
 *
 * A setting of DebugNone returns before anything is read. Otherwise colliders
 * come from the spatial index with the frustum of the first view and root
 * bounds are walked directly.
 *
 * A sphere is drawn as three circles, a capsule as its two cap circles joined by
 * four lines, and a box, fallback or root bounds as twelve edges, each kind in
 * a color of its own.
 */

#include "collision_internal.h"
#include "../renderer/frustum.h"

/** Segments one circle of a wireframe is drawn with */
#define BOX_DEBUG_SEGMENTS (24)

/** Pack a display encoded color into the word a line vertex carries */
#define BOX_DEBUG_RGBA(r, g, b) \
    ((uint32_t)(0xFFu << 24) | (uint32_t)((b) << 16) | \
        (uint32_t)((g) << 8) | (uint32_t)(r))

/** Color of a sphere collider */
#define BOX_DEBUG_SPHERE BOX_DEBUG_RGBA(80, 220, 120)

/** Color of an oriented box collider */
#define BOX_DEBUG_BOX BOX_DEBUG_RGBA(250, 190, 70)

/** Color of a capsule collider */
#define BOX_DEBUG_CAPSULE BOX_DEBUG_RGBA(90, 190, 250)

/** Color of the axis aligned fallback collider */
#define BOX_DEBUG_AABB BOX_DEBUG_RGBA(230, 110, 210)

/** Color of the bounds of a root */
#define BOX_DEBUG_BOUNDS BOX_DEBUG_RGBA(245, 245, 245)

/** Append one segment to the line list of a packet.
 *
 * @param p The packet
 * @param a First end of the segment in world space
 * @param b Second end of the segment in world space
 * @param color Display encoded color of both ends
 */
static void box_debug_line(
    box_packet_t *p,
    vec3 a,
    vec3 b,
    uint32_t color)
{
    box_line_vertex_t *first = box_vec_push(&p->lines);
    first->pos[0] = a.x;
    first->pos[1] = a.y;
    first->pos[2] = a.z;
    first->color = color;

    box_line_vertex_t *second = box_vec_push(&p->lines);
    second->pos[0] = b.x;
    second->pos[1] = b.y;
    second->pos[2] = b.z;
    second->color = color;
}

/** Append the segments of a circle around a center.
 *
 * @param p The packet
 * @param center Center of the circle in world space
 * @param u First axis of its plane, scaled to the radius
 * @param v Second axis of its plane, scaled to the radius
 * @param color Display encoded color of the circle
 */
static void box_debug_circle(
    box_packet_t *p,
    vec3 center,
    vec3 u,
    vec3 v,
    uint32_t color)
{
    vec3 prev = vec3_add(center, u);
    int32_t i;
    for (i = 1; i <= BOX_DEBUG_SEGMENTS; i ++) {
        float angle = 2.0f * 3.14159265358979f * (float)i / (float)BOX_DEBUG_SEGMENTS;
        vec3 point = vec3_add(center,
            vec3_add(vec3_scale(u, cosf(angle)), vec3_scale(v, sinf(angle))));
        box_debug_line(p, prev, point, color);
        prev = point;
    }
}

/** Append the twelve edges of an oriented box.
 *
 * @param p The packet
 * @param center Center of the box in world space
 * @param x X axis of the box, scaled to its half extent
 * @param y Y axis of the box, scaled to its half extent
 * @param z Z axis of the box, scaled to its half extent
 * @param color Display encoded color of the edges
 */
static void box_debug_box(
    box_packet_t *p,
    vec3 center,
    vec3 x,
    vec3 y,
    vec3 z,
    uint32_t color)
{
    vec3 corner[8];
    int32_t i;
    for (i = 0; i < 8; i ++) {
        vec3 point = vec3_add(center, vec3_scale(x, (i & 1) ? 1.0f : -1.0f));
        point = vec3_add(point, vec3_scale(y, (i & 2) ? 1.0f : -1.0f));
        corner[i] = vec3_add(point, vec3_scale(z, (i & 4) ? 1.0f : -1.0f));
    }

    for (i = 0; i < 4; i ++) {
        int32_t base = i * 2;
        box_debug_line(p, corner[base], corner[base + 1], color);
    }

    for (i = 0; i < 4; i ++) {
        int32_t base = (i & 1) + (i & 2) * 2;
        box_debug_line(p, corner[base], corner[base + 2], color);
    }

    for (i = 0; i < 4; i ++) {
        box_debug_line(p, corner[i], corner[i + 4], color);
    }
}

/** Append the wireframe of one collider.
 *
 * @param p The packet
 * @param c The collider
 */
static void box_debug_collider(
    box_packet_t *p,
    const Collider *c)
{
    vec3 x = vec3_scale(c->axis_x, c->half.x);
    vec3 y = vec3_scale(c->axis_y, c->half.y);
    vec3 z = vec3_scale(c->axis_z, c->half.z);
    if (c->kind == ColliderSphere) {
        box_debug_circle(p, c->center, x, y, BOX_DEBUG_SPHERE);
        box_debug_circle(p, c->center, y, z, BOX_DEBUG_SPHERE);
        box_debug_circle(p, c->center, z, x, BOX_DEBUG_SPHERE);
        goto done;
    }

    if (c->kind == ColliderCapsule) {
        vec3 top = vec3_add(c->center, y);
        vec3 bottom = vec3_sub(c->center, y);
        box_debug_circle(p, top, x, z, BOX_DEBUG_CAPSULE);
        box_debug_circle(p, bottom, x, z, BOX_DEBUG_CAPSULE);
        box_debug_line(p, vec3_add(top, x), vec3_add(bottom, x), BOX_DEBUG_CAPSULE);
        box_debug_line(p, vec3_sub(top, x), vec3_sub(bottom, x), BOX_DEBUG_CAPSULE);
        box_debug_line(p, vec3_add(top, z), vec3_add(bottom, z), BOX_DEBUG_CAPSULE);
        box_debug_line(p, vec3_sub(top, z), vec3_sub(bottom, z), BOX_DEBUG_CAPSULE);
        goto done;
    }

    box_debug_box(p, c->center, x, y, z,
        c->kind == ColliderBox ? BOX_DEBUG_BOX : BOX_DEBUG_AABB);
done:
    return;
}

/** Append the colliders the frustum of a view reaches.
 *
 * @param world The world
 * @param state The collision state
 * @param f The planes of the view
 * @param p The packet
 */
static void box_debug_colliders(
    ecs_world_t *world,
    box_collision_state_t *state,
    const box_frustum_planes_t *f,
    box_packet_t *p)
{
    ecs_vec_clear(&state->debug_hits);
    box_collision_frustum(world, f, &state->debug_hits);

    int32_t i, count = ecs_vec_count(&state->debug_hits);
    ecs_entity_t *hits = ecs_vec_first_t(&state->debug_hits, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        Collider c = {0};
        if (box_collider_resolve(world, hits[i], &c) && c.kind != ColliderNone) {
            box_debug_collider(p, &c);
        }
    }
}

/** Append the bounds of the roots the frustum of a view reaches.
 *
 * @param world The world
 * @param f The planes of the view
 * @param p The packet
 */
static void box_debug_bounds(
    ecs_world_t *world,
    const box_frustum_t *f,
    box_packet_t *p)
{
    ecs_iter_t it = ecs_each(world, RootBounds);
    while (ecs_each_next(&it)) {
        const RootBounds *bounds = ecs_field(&it, RootBounds, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!bounds[i].count) {
                continue;
            }

            float min[3] = { bounds[i].min.x, bounds[i].min.y, bounds[i].min.z };
            float max[3] = { bounds[i].max.x, bounds[i].max.y, bounds[i].max.z };
            if (!box_frustum_aabb(f, min, max)) {
                continue;
            }

            vec3 center = vec3_scale(vec3_add(bounds[i].min, bounds[i].max), 0.5f);
            vec3 half = vec3_scale(vec3_sub(bounds[i].max, bounds[i].min), 0.5f);
            box_debug_box(p, center, vec3_new(half.x, 0, 0), vec3_new(0, half.y, 0),
                vec3_new(0, 0, half.z), BOX_DEBUG_BOUNDS);
        }
    }
}

void box_collision_debug_extract(
    ecs_world_t *world,
    box_packet_t *p)
{
    if (!ecs_id(CollisionSettings) || !p->views.count ||
        ecs_lookup(world, "box.collision.CollisionSettings") != ecs_id(CollisionSettings))
    {
        goto done;
    }

    const CollisionSettings *settings = ecs_singleton_get(world, CollisionSettings);
    if (!settings || settings->debug == DebugNone) {
        goto done;
    }

    const CollisionState *cs = ecs_singleton_get(world, CollisionState);
    if (!cs || !cs->state) {
        goto done;
    }

    box_frustum_t frustum;
    box_frustum_of_view(box_vec_at(&p->views, 0), &frustum);
    box_frustum_planes_t planes;
    memcpy(planes.planes, frustum.planes, sizeof(planes.planes));
    if (settings->debug & DebugColliders) {
        box_debug_colliders(world, cs->state, &planes, p);
    }

    if (settings->debug & DebugBounds) {
        box_debug_bounds(world, &frustum, p);
    }

done:
    return;
}
