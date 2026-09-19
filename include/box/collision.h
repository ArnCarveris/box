/**
 * @file box/collision.h
 * @brief Collision components: root bounds, colliders and the shape tests.
 *
 * The box.collision module recognises a prefab or template instance as a root
 * at instantiation and keeps a RootBounds on it that covers the whole subtree.
 * A descendant that moves, appears or is destroyed marks its root, and the
 * marked roots are recomputed once per frame; a root whose subtree moves as one
 * piece transforms its cached local bounds instead of walking the subtree.
 *
 * Every renderable gets a Collider in the same pass, derived from its shape
 * where a cheap one exists and from its world bounds otherwise, with the
 * instance scale applied. The shape tests take a resolved collider, so picking,
 * controller collision and LOD all test the same geometry.
 *
 * With CollisionSettings.debug the extract of a frame turns the colliders and
 * the root bounds a view can see into a list of line segments the renderer
 * draws over the image, which is how a scene shows what it collides with.
 *
 * The same pass keeps a two level spatial index: a hierarchy over the bounds of
 * every root and over the colliders that are not below one, with the colliders
 * a root covers as its second level, next to a hierarchy of the entities that
 * move every frame. The ray, sphere, box and frustum queries read the index the
 * last update left behind, so they never walk the scene.
 */

#ifndef BOX_COLLISION_H
#define BOX_COLLISION_H

#include "types.h"

#ifdef BOX_COLLISION_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Bounds of a root and of every entity below it.
 *
 * The world corners are what queries test. The local corners are the same box
 * in the space of the root, which is what a rigid root transforms when it moves
 * instead of aggregating its subtree again. A root that covers no bounds at all
 * has a count of zero and corners of zero.
 */
ECS_STRUCT(RootBounds, {
    vec3 min;       /**< Minimum corner in world space */
    vec3 max;       /**< Maximum corner in world space */
    vec3 local_min; /**< Minimum corner in the space of the root */
    vec3 local_max; /**< Maximum corner in the space of the root */
    int32_t count;  /**< Entities with bounds the aggregate covers */
});

/** Shape of a resolved collider */
ECS_ENUM(ColliderKind, {
    ColliderNone = 0,    /**< The entity has no collider */
    ColliderSphere = 1,  /**< Sphere around the center */
    ColliderBox = 2,     /**< Box around the center, turned by the axes */
    ColliderCapsule = 3, /**< Capsule along the Y axis of the collider */
    ColliderAabb = 4     /**< Axis aligned box, the fallback for other shapes */
});

/** Sphere collider in the space of the entity, overrides the derived one */
ECS_STRUCT(SphereCollider, {
    vec3 center;  /**< Center in the space of the entity */
    float radius; /**< Radius before the instance scale */
});

/** Box collider in the space of the entity, overrides the derived one */
ECS_STRUCT(BoxCollider, {
    vec3 center; /**< Center in the space of the entity */
    vec3 half;   /**< Half extents before the instance scale */
});

/** Capsule collider along the Y axis of the entity, overrides the derived one */
ECS_STRUCT(CapsuleCollider, {
    vec3 center;  /**< Center in the space of the entity */
    float radius; /**< Radius before the instance scale */
    float height; /**< Length of the cylinder part between the two caps */
});

/** The collider of an entity in world space, written by the engine.
 *
 * A sphere keeps its radius in every component of half, a capsule keeps its
 * radius in x and z and half the length of its cylinder part in y, and a box
 * keeps its half extents. The axes are the normalized axes of the entity; an
 * axis aligned collider keeps the world axes.
 */
ECS_STRUCT(Collider, {
    ColliderKind kind; /**< Shape of the collider */
    vec3 center;       /**< Center in world space */
    vec3 half;         /**< Half extents in world space */
    vec3 axis_x;       /**< X axis of the collider in world space */
    vec3 axis_y;       /**< Y axis of the collider in world space */
    vec3 axis_z;       /**< Z axis of the collider in world space */
});

/** The entity gets no collider, whatever shape it has */
extern ECS_TAG_DECLARE(NoCollider);

/** What the debug drawing of the module adds to the image of a view */
ECS_ENUM(CollisionDebug, {
    DebugNone = 0,      /**< Nothing is drawn, which costs a frame nothing */
    DebugColliders = 1, /**< Wireframe of every collider in view */
    DebugBounds = 2,    /**< Box of the bounds of every root in view */
    DebugAll = 3        /**< Both the colliders and the root bounds */
});

/** Settings of the spatial index, read by every update */
ECS_STRUCT(CollisionSettings, {
    float margin;         /**< Slack around a leaf, so a small move keeps its place */
    bool dynamic;         /**< Index the entities that move every frame */
    bool enabled;         /**< False empties the index and leaves every query without hits */
    CollisionDebug debug; /**< Wireframes the extract of a frame draws */
});

/** Planes of a frustum query */
#define BOX_FRUSTUM_PLANES (6)

/** What a ray query found */
typedef struct box_ray_hit_t {
    ecs_entity_t entity; /**< The hit entity, the lowest id at equal distance, 0 for none */
    float distance;      /**< Distance of the hit along the ray */
    vec3 point;          /**< The point of the hit in world space */
} box_ray_hit_t;

/** Volume a frustum query tests, six planes with their normals pointing inward */
typedef struct box_frustum_planes_t {
    vec4 planes[BOX_FRUSTUM_PLANES]; /**< Normal in xyz and distance in w */
} box_frustum_planes_t;

/** Intersect a ray with a collider.
 *
 * @param c The collider
 * @param origin Origin of the ray in world space
 * @param dir Direction of the ray, distances are multiples of its length
 * @param max_dist Longest distance that counts as a hit
 * @param dist_out Receives the distance of the hit, NULL to ignore
 * @return true when the ray hits the collider
 */
bool box_collider_ray(
    const Collider *c,
    vec3 origin,
    vec3 dir,
    float max_dist,
    float *dist_out);

/** Test a sphere against a collider.
 *
 * @param c The collider
 * @param center Center of the sphere in world space
 * @param radius Radius of the sphere
 * @return true when the sphere overlaps the collider
 */
bool box_collider_sphere(
    const Collider *c,
    vec3 center,
    float radius);

/** Test a capsule against a collider.
 *
 * @param c The collider
 * @param a First end of the segment of the capsule in world space
 * @param b Second end of the segment of the capsule in world space
 * @param radius Radius of the capsule
 * @return true when the capsule overlaps the collider
 */
bool box_collider_capsule(
    const Collider *c,
    vec3 a,
    vec3 b,
    float radius);

/** Get the world bounds of a collider.
 *
 * @param c The collider
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
void box_collider_bounds(
    const Collider *c,
    vec3 *min,
    vec3 *max);

/** Find the nearest collider a ray hits.
 *
 * @param world The world
 * @param origin Origin of the ray in world space
 * @param dir Direction of the ray, distances are multiples of its length
 * @param max_dist Longest distance that counts as a hit
 * @param out Receives the hit, NULL to only ask whether there is one
 * @return true when the ray hit a collider
 */
bool box_collision_ray(
    const ecs_world_t *world,
    vec3 origin,
    vec3 dir,
    float max_dist,
    box_ray_hit_t *out);

/** Collect every collider a sphere overlaps.
 *
 * @param world The world
 * @param center Center of the sphere in world space
 * @param radius Radius of the sphere
 * @param out Vector of ecs_entity_t the entities are appended to
 * @return The number of entities that were appended
 */
int32_t box_collision_sphere(
    const ecs_world_t *world,
    vec3 center,
    float radius,
    ecs_vec_t *out);

/** Collect every collider whose bounds overlap a box.
 *
 * @param world The world
 * @param min Minimum corner of the box in world space
 * @param max Maximum corner of the box in world space
 * @param out Vector of ecs_entity_t the entities are appended to
 * @return The number of entities that were appended
 */
int32_t box_collision_aabb(
    const ecs_world_t *world,
    vec3 min,
    vec3 max,
    ecs_vec_t *out);

/** Collect every collider whose bounds reach into a frustum.
 *
 * @param world The world
 * @param frustum The planes of the frustum
 * @param out Vector of ecs_entity_t the entities are appended to
 * @return The number of entities that were appended
 */
int32_t box_collision_frustum(
    const ecs_world_t *world,
    const box_frustum_planes_t *frustum,
    ecs_vec_t *out);

/** A capsule move a controller asks the scene to resolve.
 *
 * The capsule stands on its foot: its lower cap sits a radius above the
 * position and its upper cap a radius below the top of its height, so a height
 * of twice the radius is a sphere around the middle.
 */
typedef struct box_move_desc_t {
    vec3 position;     /**< Foot of the capsule at the start of the move */
    vec3 motion;       /**< What the move would be without the scene */
    float radius;      /**< Radius of the capsule, zero moves through anything */
    float height;      /**< Height of the capsule above its foot */
    float step;        /**< Highest ledge the capsule steps onto, zero for none */
    float probe;       /**< How far below the foot the ground probe reaches */
    ecs_entity_t skip; /**< Entity the move ignores, which is its own collider */
} box_move_desc_t;

/** What a capsule move ended up as */
typedef struct box_move_result_t {
    vec3 position;       /**< Foot of the capsule after the move */
    vec3 motion;         /**< The move the capsule made, which a wall shortens */
    ecs_entity_t ground; /**< What the capsule stands on, 0 when it stands on nothing */
    float ground_y;      /**< Height of the ground the probe found */
    bool grounded;       /**< The probe found ground under the foot */
    bool blocked;        /**< The capsule was pushed out of something */
} box_move_result_t;

/** Move a capsule through the scene, sliding along what it runs into.
 *
 * The move reads the colliders of the cells the capsule can reach this frame
 * from the index once, walks its motion in small steps and pushes the capsule
 * out of what it reaches into after every one, which is what slides it along a
 * wall. A move a wall stopped is tried again over a ledge of at most the step
 * height, and the ground probe is a ray down the same colliders.
 *
 * @param world The world
 * @param desc The move
 * @param out Receives what the move ended up as
 */
void box_collision_move(
    const ecs_world_t *world,
    const box_move_desc_t *desc,
    box_move_result_t *out);

/** Bring the colliders, the root bounds and the index up to date.
 *
 * The engine runs this in OnStore, so a query reads what the last frame left
 * behind. Call it to see a change of this frame, never from a deferred system.
 *
 * @param world The world
 */
void box_collision_update(
    ecs_world_t *world);

/** Register the box.collision module.
 *
 * @param world The world
 */
void BoxCollisionImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
