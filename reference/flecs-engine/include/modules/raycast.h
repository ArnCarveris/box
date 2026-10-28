#ifndef FLECS_ENGINE_RAYCAST_H
#define FLECS_ENGINE_RAYCAST_H

/* Scene raycast. Pairs with flecsEngine_cameraScreenRay, which builds a ray
 * but has nothing to intersect it with.
 *
 * The broad phase is linear: there is no spatial acceleration structure in
 * the engine, and none is built here. What keeps that affordable is the
 * filter - a scene has far more entities than a user can meaningfully click,
 * and naming the id that marks the clickable ones turns the sweep from
 * "every renderable" into "the handful that matter".
 *
 * Measured on a 92 012-entity scene, release, M-series: 1180 us per
 * unfiltered ray (12.8 ns per candidate), 6.9 us filtered to 501 candidates.
 * So one ray per click is free either way, and a ray per frame per agent
 * needs the filter - a 16 ms frame holds about 13 unfiltered rays or 2300
 * filtered ones. Cost is linear in the candidate count:
 * us_per_ray ~= candidates * 0.013. Method and the rest in
 * docs/PERF_NOTES.md. */

typedef struct {
    ecs_entity_t entity;
    float t;
    float point[3];
    float normal[3];
    /* True when t came from a triangle test, false when it is the entity's
     * bounding box. A triangle_level raycast degrades to the box - and says
     * so here - for an entity whose CPU mesh data was released. */
    bool triangle;
} FlecsRayHit;

typedef struct {
    /* Only entities with this id are considered. 0 considers every entity
     * with a FlecsWorldTransform3. */
    ecs_id_t filter;
    /* Hits beyond this distance are ignored. 0 is unbounded. */
    float max_dist;
    /* Refine a box hit against the mesh triangles where CPU mesh data is
     * still resident. */
    bool triangle_level;
} FlecsRaycastDesc;

/* Nearest entity along the ray, or false when nothing is hit. desc may be
 * NULL for the defaults. dir does not have to be normalized, but t and
 * max_dist are in units of its length, so an unnormalized dir makes them
 * something other than distances.
 *
 * An entity is tested against the world AABB of its own mesh or primitive.
 * An entity that carries no geometry itself is tested against the union of
 * its descendants' boxes, so a filter can mark object roots whose parts live
 * in children. That subtree walk only runs under a filter: an unfiltered
 * sweep stays one slab test per renderable and never recurses.
 *
 * A ray whose origin is already inside a volume hits it at t = 0, with the
 * origin as the hit point and a zeroed normal.
 *
 * Read-only - safe from a system, an observer or a UI callback. */
bool flecsEngine_raycast(
    const ecs_world_t *world,
    const vec3 origin,
    const vec3 dir,
    const FlecsRaycastDesc *desc,
    FlecsRayHit *out);

/* Every hit along the ray, nearest first. Writes at most out_count hits and
 * returns the number written. */
int32_t flecsEngine_raycastAll(
    const ecs_world_t *world,
    const vec3 origin,
    const vec3 dir,
    const FlecsRaycastDesc *desc,
    FlecsRayHit *out,
    int32_t out_count);

/* The primitives the sweep is built from, for callers whose candidates are
 * not entities - a gizmo drawn through flecsEngine_draw, say. out_t is the
 * entry distance, clamped to 0 when the origin is inside the volume. */
bool flecsEngine_rayAABB(
    const vec3 origin,
    const vec3 dir,
    const FlecsAABB *aabb,
    float max_dist,
    float *out_t,
    vec3 out_normal);

bool flecsEngine_raySphere(
    const vec3 origin,
    const vec3 dir,
    const vec3 center,
    float radius,
    float max_dist,
    float *out_t);

#endif
