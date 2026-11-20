/**
 * @file controllers/hull.c
 * @brief Whether the hull of a vessel comes down on the ground or on a structure.
 *
 * The outline of the hull is sampled about every BOX_HULL_SPACING units along each of its
 * edges. A ray down from the deck at each sample finds the highest solid shape below it, the
 * vessel's own shapes and Water left out.
 *
 * Along the keel line (the outline moved inset toward its middle in x) a shape higher than the
 * keel blocks, the seabed or a reef; along the outline itself a shape higher than the water
 * blocks, a quay or a pier below the deck.
 */

#include "controllers.h"
#include "../render/render.h"
#include "../spatial/spatial.h"
#include <float.h>
#include <math.h>

/** Longest distance between two samples along an edge of the outline */
#define BOX_HULL_SPACING (5.0f)

/** Height above the keel or the water at which a shape blocks */
#define BOX_HULL_CLEARANCE (0.3f)

/** Most corners of an outline that count */
#define BOX_HULL_CORNERS (64)

/** A ray down from the deck of a vessel */
typedef struct box_hull_ray_t {
    const ecs_world_t *world; /**< The world */
    ecs_entity_t vessel;      /**< The vessel, whose own shapes do not count */
    vec3 origin;              /**< Origin of the ray */
} box_hull_ray_t;

/** Accept a solid shape that is not the vessel's and that the ray down hits.
 *
 * @param ctx The ray
 * @param e The shape
 * @param distance Distance at which the ray enters the bounds, moved to the shape
 * @return true when the ray hits the shape
 */
static bool box_hull_filter(
    void *ctx,
    ecs_entity_t e,
    float *distance)
{
    const box_hull_ray_t *r = ctx;
    vec3 down = { 0, -1, 0 };
    ecs_entity_t p = e;
    bool own = false;
    while (p && !own) {
        own = p == r->vessel;
        p = ecs_get_parent(r->world, p);
    }

    return !own && box_collider_solid(r->world, e) &&
        box_collider_cast(r->world, e, r->origin, down, FLT_MAX, distance, NULL);
}

/** Find the height of the highest shape below a point.
 *
 * @param world The world
 * @param e The vessel
 * @param scene Its scene
 * @param parent World transform of its parent
 * @param point The point in the parent space
 * @param depth How far down to look
 * @return The height in the parent space, -FLT_MAX for none
 */
static float box_hull_ground(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t scene,
    mat4 parent,
    const vec3 point,
    float depth)
{
    box_hull_ray_t ctx = { .world = world, .vessel = e };
    glm_mat4_mulv3(parent, ECS_CONST_CAST(float*, point), 1.0f, ctx.origin);

    box_spatial_ray_t ray = {
        .scene = scene,
        .dir = { 0, -1, 0 },
        .max_distance = depth,
        .filter = box_hull_filter,
        .ctx = &ctx,
        .shapes = true
    };
    glm_vec3_copy(ctx.origin, ray.origin);

    box_spatial_hit_t hit;
    float ground = -FLT_MAX;
    if (box_spatial_ray(world, &ray, &hit)) {
        ground = point[1] - hit.distance;
    }

    return ground;
}

/** Read the corners of the outline of a hull.
 *
 * @param world The world
 * @param outline The entity with the Path2D
 * @param corners Receives the corners, x and local z
 * @return Number of corners
 */
static int32_t box_hull_corners(
    const ecs_world_t *world,
    ecs_entity_t outline,
    vec2 *corners)
{
    const Path2D *path = outline && ecs_is_alive(world, outline)
        ? ecs_get(world, outline, Path2D)
        : NULL;
    int32_t count = 0;
    if (path) {
        glm_vec2_copy(ECS_CONST_CAST(float*, path->start), corners[count ++]);
    }

    const PathSegment2D *segments = path ? ecs_vec_first(&path->segments) : NULL;
    int32_t n = path ? ecs_vec_count(&path->segments) : 0;
    for (int32_t i = 0; i < n && count < BOX_HULL_CORNERS; i ++) {
        glm_vec2_copy(ECS_CONST_CAST(float*, segments[i].end), corners[count ++]);
    }

    return count;
}

int32_t box_vessel_blocked(
    ecs_world_t *world,
    ecs_entity_t e,
    const Vessel *vessel,
    float y,
    float x,
    float z,
    float heading)
{
    vec2 corners[BOX_HULL_CORNERS];
    int32_t count = box_hull_corners(world, vessel->outline, corners);
    float middle_min = FLT_MAX;
    float middle_max = -FLT_MAX;
    for (int32_t i = 0; i < count; i ++) {
        middle_min = fminf(middle_min, corners[i][0]);
        middle_max = fmaxf(middle_max, corners[i][0]);
    }

    float middle = (middle_min + middle_max) / 2;

    mat4 parent = GLM_MAT4_IDENTITY_INIT;
    ecs_entity_t up = ecs_get_parent(world, e);
    WorldTransform buf;
    const WorldTransform *w = up ? box_transform_world(world, up, &buf) : NULL;
    if (w) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, w->m), parent);
    }

    ecs_entity_t scene = box_render_scene_of(world, e);
    float c = cosf(heading);
    float s = sinf(heading);
    float top = y + vessel->deck;
    float depth = vessel->deck - vessel->keel + 1;
    int32_t blocked = 0;

    for (int32_t i = 0; count > 1 && i < count; i ++) {
        const float *a = corners[i];
        const float *b = corners[(i + 1) % count];
        float length = glm_vec2_distance(ECS_CONST_CAST(float*, a), ECS_CONST_CAST(float*, b));
        int32_t steps = (int32_t)ceilf(length / BOX_HULL_SPACING);
        if (steps < 1) {
            steps = 1;
        }

        for (int32_t k = 0; k < steps; k ++) {
            float f = (float)k / (float)steps;
            float qx = a[0] + (b[0] - a[0]) * f;
            float qz = a[1] + (b[1] - a[1]) * f;
            float side = middle - qx;
            float keel_x = qx + (side < 0 ? -1.0f : 1.0f) * fminf(vessel->inset, fabsf(side));

            for (int32_t line = 0; line < 2; line ++) {
                float ox = (line ? keel_x : qx) - vessel->pivot[0];
                float oz = qz - vessel->pivot[2];
                vec3 point = { x + c * ox + s * oz, top, z - s * ox + c * oz };
                float ground = box_hull_ground(world, e, scene, parent, point, depth);
                float limit = line
                    ? y + vessel->keel + BOX_HULL_CLEARANCE
                    : vessel->water + BOX_HULL_CLEARANCE;
                blocked += ground > limit ? 1 : 0;
            }
        }
    }

    return blocked;
}
